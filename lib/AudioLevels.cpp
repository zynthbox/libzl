/*
  ==============================================================================

    AudioLevels.cpp
    Created: 8 Feb 2022
    Author:  Anupam Basak <anupam.basak27@gmail.com>
    Author: Dan Leinir Turthra Jensen <admin@leinir.dk>

  ==============================================================================
*/

#include "AudioLevels.h"

#include <cmath>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QVariantList>
#include <QWaitCondition>

#include <jack/ringbuffer.h>

#include "JUCEHeaders.h"

#define DebugAudioLevels false

struct RecordPort {
    QString portName;
    int channel{-1};
};

// that is one left and one right channel
#define CHANNEL_COUNT 2
class DiskWriter {
public:
    explicit DiskWriter() {
        m_backgroundThread.startThread();
    }
    ~DiskWriter() {
        stop();
    }

    void startRecording(const QString& fileName, double sampleRate = 44100, int bitRate = 16, int channelCount=CHANNEL_COUNT) {
        m_file = juce::File(fileName.toStdString());
        m_sampleRate = sampleRate;
        if (m_sampleRate > 0) {
            // In case there's a file there already, get rid of it - at this point, the user should have been made aware, so we can be ruthless
            m_file.deleteFile();
            // Create our file stream, so we have somewhere to write data to
            if (auto fileStream = std::unique_ptr<FileOutputStream>(m_file.createOutputStream())) {
                // Now create a WAV writer, which will be writing to our output stream
                WavAudioFormat wavFormat;
                if (auto writer = wavFormat.createWriterFor(fileStream.get(), sampleRate, quint32(qMin(channelCount, CHANNEL_COUNT)), bitRate, {}, 0)) {
                    fileStream.release(); // (passes responsibility for deleting the stream to the writer object that is now using it)
                    // Now we'll create one of these helper objects which will act as a FIFO buffer, and will
                    // write the data to disk on our background thread.
                    m_threadedWriter.reset(new AudioFormatWriter::ThreadedWriter(writer, m_backgroundThread, 32768));

                    // And now, swap over our active writer pointer so that the audio callback will start using it..
                    const ScopedLock sl (m_writerLock);
                    m_activeWriter = m_threadedWriter.get();
                    m_isRecording = true;
                }
            }
        }
    }

    // The input data must be an array with the same number of channels as the writer expects (that is, in our general case CHANNEL_COUNT)
    void processBlock(const float** inputChannelData, int numSamples) const {
        const ScopedLock sl (m_writerLock);
        if (m_activeWriter.load() != nullptr) {
            m_activeWriter.load()->write (inputChannelData, numSamples);
        }
    }

    void stop() {
        // First, clear this pointer to stop the audio callback from using our writer object..
        {
            const ScopedLock sl(m_writerLock);
            m_activeWriter = nullptr;
            m_sampleRate = 0;
            m_isRecording = false;
        }

        // Now we can delete the writer object. It's done in this order because the deletion could
        // take a little time while remaining data gets flushed to disk, so it's best to avoid blocking
        // the audio callback while this happens.
        m_threadedWriter.reset();
    }

    bool isRecording() const {
        return m_isRecording;
    }
    QString filenamePrefix() const {
        return m_fileNamePrefix;
    }
    void setFilenamePrefix(const QString& fileNamePrefix) {
        m_fileNamePrefix = fileNamePrefix;
    }
    bool shouldRecord() const {
        return m_shouldRecord;
    }
    void setShouldRecord(bool shouldRecord) {
        m_shouldRecord = shouldRecord;
    }
private:
    QString m_fileNamePrefix;
    bool m_shouldRecord{false};
    bool m_isRecording{false};

    juce::File m_file;
    juce::TimeSliceThread m_backgroundThread{"AudioLevel Disk Recorder"}; // the thread that will write our audio data to disk
    std::unique_ptr<AudioFormatWriter::ThreadedWriter> m_threadedWriter; // the FIFO used to buffer the incoming data
    double m_sampleRate = 0.0;

    CriticalSection m_writerLock;
    std::atomic<AudioFormatWriter::ThreadedWriter*> m_activeWriter { nullptr };
};

class AudioLevelsChannel {
public:
    explicit AudioLevelsChannel(const QString &clientName);
    ~AudioLevelsChannel() {
        if (jackClient) {
            jack_client_close(jackClient);
        }
        delete diskRecorder;
    }
    QString clientName;
    jack_client_t *jackClient{nullptr};
    jack_port_t *leftPort{nullptr};
    QString portNameLeft{"left_in"};
    jack_port_t *rightPort{nullptr};
    QString portNameRight{"right_in"};
    DiskWriter* diskRecorder{new DiskWriter};
    int process(jack_nframes_t nframes);
    int peakA{0}, peakB{0};
    float peakAHoldSignal{0}, peakBHoldSignal{0};
    quint32 bufferReadSize{0};
    jack_default_audio_sample_t *bufferA{nullptr}, *bufferB{nullptr};
private:
    const float** recordingPassthroughBuffer{new const float* [2]};
    jack_default_audio_sample_t *leftBuffer{nullptr}, *rightBuffer{nullptr};
};

class AudioLevelsPrivate {
public:
    AudioLevelsPrivate() {
        for(int i = 0; i < CHANNELS_COUNT; ++i) {
            channelsToRecord << false;
            levels.append(QVariant::fromValue<float>(0));
        }
    }
    ~AudioLevelsPrivate() {
        qDeleteAll(audioLevelsChannels);
    }
    QList<AudioLevelsChannel*> audioLevelsChannels;
    DiskWriter* globalPlaybackWriter{new DiskWriter};
    DiskWriter* portsRecorder{new DiskWriter};
    QList<RecordPort> recordPorts;
    QList<DiskWriter*> channelWriters;
    QVariantList channelsToRecord;
    QVariantList levels;
    jack_client_t* jackClient{nullptr};

    void connectPorts(const QString &from, const QString &to) {
        int result = jack_connect(jackClient, from.toUtf8(), to.toUtf8());
        if (result == 0 || result == EEXIST) {
            if (DebugAudioLevels) { qDebug() << "AudioLevels:" << (result == EEXIST ? "Retaining existing connection from" : "Successfully created new connection from" ) << from << "to" << to; }
        } else {
            qWarning() << "AudioLevels: Failed to connect" << from << "with" << to << "with error code" << result;
            // This should probably reschedule an attempt in the near future, with a limit to how long we're trying for?
        }
    }
    void disconnectPorts(const QString &from, const QString &to) {
        // Don't attempt to connect already connected ports
        int result = jack_disconnect(jackClient, from.toUtf8(), to.toUtf8());
        if (result == 0) {
            if (DebugAudioLevels) { qDebug() << "AudioLevels: Successfully disconnected" << from << "from" << to; }
        } else {
            qWarning() << "AudioLevels: Failed to disconnect" << from << "from" << to << "with error code" << result;
        }
    }

    const QStringList recorderPortNames{QLatin1String{"AudioLevels-SystemRecorder:left_in"}, QLatin1String{"AudioLevels-SystemRecorder:right_in"}};
    void disconnectPort(const QString& portName, int channel) {
        jackClient = audioLevelsChannels[2]->jackClient;
        disconnectPorts(portName, recorderPortNames[channel]);
        jackClient = audioLevelsChannels[0]->jackClient;
    }
    void connectPort(const QString& portName, int channel) {
        jackClient = audioLevelsChannels[2]->jackClient;
        connectPorts(portName, recorderPortNames[channel]);
        jackClient = audioLevelsChannels[0]->jackClient;
    }
};

static int audioLevelsChannelProcess(jack_nframes_t nframes, void* arg) {
  return static_cast<AudioLevelsChannel*>(arg)->process(nframes);
}

AudioLevelsChannel::AudioLevelsChannel(const QString &clientName)
  : clientName(clientName)
{
    jack_status_t real_jack_status{};
    jackClient = jack_client_open(clientName.toUtf8(), JackNullOption, &real_jack_status);
    if (jackClient) {
        // Set the process callback.
        if (jack_set_process_callback(jackClient, audioLevelsChannelProcess, this) != 0) {
            qWarning() << "Failed to set the AudioLevelsChannel Jack processing callback";
        } else {
            leftPort = jack_port_register(jackClient, portNameLeft.toUtf8(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
            rightPort = jack_port_register(jackClient, portNameRight.toUtf8(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
            // Activate the client.
            if (jack_activate(jackClient) == 0) {
                qDebug() << "Successfully created and set up" << clientName;
            } else {
                qWarning() << "Failed to activate AudioLevelsChannel Jack client" << clientName;
            }
        }
    }
}

inline float addFloat(const float& db1, const float &db2) {
   return 10 * log10f(pow(10, db1/10) + pow(10, db2/10));
}

int AudioLevelsChannel::process(jack_nframes_t nframes)
{
    leftBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(leftPort, nframes);
    rightBuffer = (jack_default_audio_sample_t *)jack_port_get_buffer(rightPort, nframes);
    if (diskRecorder->isRecording()) {
        recordingPassthroughBuffer[0] = leftBuffer;
        recordingPassthroughBuffer[1] = rightBuffer;
        diskRecorder->processBlock(recordingPassthroughBuffer, (int)nframes);
    }

    bufferA = leftBuffer;
    bufferB = rightBuffer;
    bufferReadSize = nframes;
    return 0;
}

AudioLevels::AudioLevels(QObject *parent)
  : QObject(parent)
  , d(new AudioLevelsPrivate)
{
    static const QStringList audioLevelClientNames{
        "AudioLevels-SystemCapture",
        "AudioLevels-SystemPlayback",
        "AudioLevels-SystemRecorder",
        "AudioLevels-Channel1",
        "AudioLevels-Channel2",
        "AudioLevels-Channel3",
        "AudioLevels-Channel4",
        "AudioLevels-Channel5",
        "AudioLevels-Channel6",
        "AudioLevels-Channel7",
        "AudioLevels-Channel8",
        "AudioLevels-Channel9",
        "AudioLevels-Channel10",
    };
    int channelIndex{0};
    for (const QString &clientName : audioLevelClientNames) {
        AudioLevelsChannel *channel = new AudioLevelsChannel(clientName);
        if (channelIndex == 0) {
            d->jackClient = channel->jackClient;
            d->connectPorts("system:capture_1", "AudioLevels-SystemCapture:left_in");
            d->connectPorts("system:capture_2", "AudioLevels-SystemCapture:right_in");
        } else if (channelIndex == 1) {
            d->globalPlaybackWriter = channel->diskRecorder;
        } else if (channelIndex == 2) {
            d->portsRecorder = channel->diskRecorder;
        } else {
          const int sketchpadChannelIndex{channelIndex - 3};
          d->channelWriters[sketchpadChannelIndex] = channel->diskRecorder;
        }
        d->audioLevelsChannels << channel;
        ++channelIndex;
    }

    startTimerHz(30);
}

inline float AudioLevels::convertTodbFS(float raw) {
    if (raw <= 0) {
        return -200;
    }

    const float fValue = 20 * log10f(raw);
    if (fValue < -200) {
        return -200;
    }

    return fValue;
}

float AudioLevels::add(float db1, float db2) {
    return addFloat(db1, db2);
}

void AudioLevels::timerCallback() {
    // 0.2/131072 = 0.00000152587
    static const float intToFloatMultiplier{0.00000152587};

    int channelIndex{0};
    const jack_default_audio_sample_t *portBuffer{nullptr};
    const jack_default_audio_sample_t *portBufferEnd{nullptr};
    const int quarterSpot = 1;//nframes / 4;
    // 2^17 = 131072
    static const float floatToIntMultiplier{131072};
    for (AudioLevelsChannel *channel : d->audioLevelsChannels) {
        channel->peakA = qMax(0, channel->peakA - 10000);
        channel->peakB = qMax(0, channel->peakB - 10000);
        if (channel->bufferReadSize > 0) {
            // Peak checkery for the left channel
            portBuffer = channel->bufferA;
            portBufferEnd = portBuffer + channel->bufferReadSize;
            for (const float* channelSample = portBuffer; channelSample < portBufferEnd; channelSample += quarterSpot) {
                if (channelSample == nullptr || channelSample >= portBufferEnd) { break; }
                const int sampleValue = abs(floatToIntMultiplier * (*channelSample));
                if (sampleValue > channel->peakA) {
                    channel->peakA = sampleValue;
                }
            }

            // Peak checkery for the right channel
            portBuffer = channel->bufferB;
            portBufferEnd = portBuffer + channel->bufferReadSize;
            for (const float* channelSample = portBuffer; channelSample < portBufferEnd; channelSample += quarterSpot) {
                if (channelSample == nullptr || channelSample >= portBufferEnd) { break; }
                const int sampleValue = abs(floatToIntMultiplier * (*channelSample));
                if (sampleValue > channel->peakB) {
                    channel->peakB = sampleValue;
                }
            }
            channel->bufferReadSize = 0;
        }
        const float peakA{channel->peakA * intToFloatMultiplier}, peakB{channel->peakB * intToFloatMultiplier};
        const float peakDbA{convertTodbFS(peakA)},
                    peakDbB{convertTodbFS(peakB)};
        if (channelIndex == 0) {
            captureA = peakDbA <= -200 ? -200 : peakDbA;
            captureB = peakDbB <= -200 ? -200 : peakDbB;
        } else if (channelIndex == 1) {
            playbackA = peakDbA <= -200 ? -200 : peakDbA;
            playbackB = peakDbB <= -200 ? -200 : peakDbB;
            channel->peakAHoldSignal = (peakA >= channel->peakAHoldSignal) ? peakA : channel->peakAHoldSignal * 0.9f;
            channel->peakBHoldSignal = (peakB >= channel->peakBHoldSignal) ? peakB : channel->peakBHoldSignal * 0.9f;
            playbackAHold = convertTodbFS(channel->peakAHoldSignal);
            playbackBHold = convertTodbFS(channel->peakBHoldSignal);
        } else if (channelIndex == 2) {
            recordingA = peakDbA <= -200 ? -200 : peakDbA;
            recordingB = peakDbB <= -200 ? -200 : peakDbB;
        } else {
            const int sketchpadChannelIndex{channelIndex - 3};
            channelsA[sketchpadChannelIndex] = peakDbA <= -200 ? -200 : peakDbA;
            channelsB[sketchpadChannelIndex] = peakDbB <= -200 ? -200 : peakDbB;
            d->levels[sketchpadChannelIndex].setValue<float>(addFloat(channelsA[sketchpadChannelIndex], channelsB[sketchpadChannelIndex]));
        }
        ++channelIndex;
    }
    Q_EMIT audioLevelsChanged();
}

const QVariantList AudioLevels::getChannelsAudioLevels() {
    return d->levels;
}

void AudioLevels::setRecordGlobalPlayback(bool shouldRecord)
{
    if (d->globalPlaybackWriter->shouldRecord() != shouldRecord) {
        d->globalPlaybackWriter->setShouldRecord(shouldRecord);
        Q_EMIT recordGlobalPlaybackChanged();
    }
}

bool AudioLevels::recordGlobalPlayback() const
{
    return d->globalPlaybackWriter->shouldRecord();
}

void AudioLevels::setGlobalPlaybackFilenamePrefix(const QString &fileNamePrefix)
{
    d->globalPlaybackWriter->setFilenamePrefix(fileNamePrefix);
}

void AudioLevels::setChannelToRecord(int channel, bool shouldRecord)
{
    if (channel > -1 && channel < d->channelWriters.count()) {
        d->channelWriters[channel]->setShouldRecord(shouldRecord);
        d->channelsToRecord[channel] = shouldRecord;
        Q_EMIT channelsToRecordChanged();
    }
}

QVariantList AudioLevels::channelsToRecord() const
{
    return d->channelsToRecord;
}

void AudioLevels::setChannelFilenamePrefix(int channel, const QString& fileNamePrefix)
{
    if (channel > -1 && channel < d->channelWriters.count()) {
        d->channelWriters[channel]->setFilenamePrefix(fileNamePrefix);
    }
}

void AudioLevels::setRecordPortsFilenamePrefix(const QString &fileNamePrefix)
{
    d->portsRecorder->setFilenamePrefix(fileNamePrefix);
}

void AudioLevels::addRecordPort(const QString &portName, int channel)
{
    bool addPort{true};
    for (const RecordPort &port : qAsConst(d->recordPorts)) {
        if (port.portName == portName && port.channel == channel) {
            addPort = false;
            break;
        }
    }
    if (addPort) {
        RecordPort port;
        port.portName = portName;
        port.channel = channel;
        d->recordPorts << port;
        d->connectPort(portName, channel);
    }
}

void AudioLevels::removeRecordPort(const QString &portName, int channel)
{
    QMutableListIterator<RecordPort> iterator(d->recordPorts);
    while (iterator.hasNext()) {
        const RecordPort &port = iterator.value();
        if (port.portName == portName && port.channel == channel) {
            d->disconnectPort(port.portName, port.channel);
            iterator.remove();
            break;
        }
    }
}

void AudioLevels::clearRecordPorts()
{
    for (const RecordPort &port : qAsConst(d->recordPorts)) {
        d->disconnectPort(port.portName, port.channel);
    }
    d->recordPorts.clear();
}

void AudioLevels::setShouldRecordPorts(bool shouldRecord)
{
    if (d->portsRecorder->shouldRecord() != shouldRecord) {
        d->portsRecorder->setShouldRecord(shouldRecord);
        Q_EMIT shouldRecordPortsChanged();
    }
}

bool AudioLevels::shouldRecordPorts() const
{
    return d->portsRecorder->shouldRecord();
}

void AudioLevels::startRecording()
{
    const QString timestamp{QDateTime::currentDateTime().toString(Qt::ISODate)};
    const double sampleRate = jack_get_sample_rate(d->jackClient);
    // Doing this in two goes, because when we ask recording to start, it will very extremely start,
    // and we kind of want to at least get pretty close to them starting at the same time, so let's
    // do this bit of the ol' filesystem work first
    QString dirPath = d->globalPlaybackWriter->filenamePrefix().left(d->globalPlaybackWriter->filenamePrefix().lastIndexOf('/'));
    if (d->globalPlaybackWriter->shouldRecord() && !QDir().exists(dirPath)) {
        QDir().mkpath(dirPath);
    }
    dirPath = d->portsRecorder->filenamePrefix().left(d->portsRecorder->filenamePrefix().lastIndexOf('/'));
    if (d->portsRecorder->shouldRecord() && !QDir().exists(dirPath)) {
        QDir().mkpath(dirPath);
    }
    for (DiskWriter *channelWriter : d->channelWriters) {
        dirPath = channelWriter->filenamePrefix().left(channelWriter->filenamePrefix().lastIndexOf('/'));
        if (channelWriter->shouldRecord() && !QDir().exists(dirPath)) {
            QDir().mkpath(dirPath);
        }
    }
    if (d->globalPlaybackWriter->shouldRecord()) {
        if (d->globalPlaybackWriter->filenamePrefix().endsWith(".wav")) {
            // If prefix already ends with `.wav` do not add timestamp and suffix to filename
            d->globalPlaybackWriter->startRecording(d->globalPlaybackWriter->filenamePrefix(), sampleRate);
        } else {
            const QString filename = QString("%1-%2.wav").arg(d->globalPlaybackWriter->filenamePrefix()).arg(timestamp);
            d->globalPlaybackWriter->startRecording(filename, sampleRate);
        }
    }
    if (d->portsRecorder->shouldRecord()) {
        if (d->portsRecorder->filenamePrefix().endsWith(".wav")) {
            // If prefix already ends with `.wav` do not add timestamp and suffix to filename
            d->portsRecorder->startRecording(d->portsRecorder->filenamePrefix(), sampleRate, 16, d->recordPorts.count());
        } else {
            const QString filename = QString("%1-%2.wav").arg(d->portsRecorder->filenamePrefix()).arg(timestamp);
            d->portsRecorder->startRecording(filename, sampleRate, 16, d->recordPorts.count());
        }
    }
    for (DiskWriter *channelWriter : d->channelWriters) {
        if (channelWriter->shouldRecord()) {
            const QString filename = QString("%1-%2.wav").arg(channelWriter->filenamePrefix()).arg(timestamp);
            channelWriter->startRecording(filename, sampleRate);
        }
    }
    Q_EMIT isRecordingChanged();
}

void AudioLevels::stopRecording()
{
    d->globalPlaybackWriter->stop();
    d->portsRecorder->stop();
    for (DiskWriter *channelWriter : d->channelWriters) {
        channelWriter->stop();
    }
}

bool AudioLevels::isRecording() const
{
    bool channelIsRecording{false};
    for (DiskWriter *channelWriter : d->channelWriters) {
        if (channelWriter->isRecording()) {
            channelIsRecording = true;
            break;
        }
    }
    return d->globalPlaybackWriter->isRecording() || d->portsRecorder->isRecording() || channelIsRecording;
}
