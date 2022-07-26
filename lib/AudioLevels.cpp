/*
  ==============================================================================

    AudioLevels.cpp
    Created: 8 Feb 2022
    Author:  Anupam Basak <anupam.basak27@gmail.com>

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

#include "JUCEHeaders.h"

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

  void startRecording(const QString& fileName, double sampleRate = 44100, int bitRate = 16) {
    m_file = juce::File(fileName.toStdString());
    m_sampleRate = sampleRate;
    if (m_sampleRate > 0) {
      // In case there's a file there already, get rid of it - at this point, the user should have been made aware, so we can be ruthless
      m_file.deleteFile();
      // Create our file stream, so we have somewhere to write data to
      if (auto fileStream = std::unique_ptr<FileOutputStream>(m_file.createOutputStream())) {
        // Now create a WAV writer, which will be writing to our output stream
        WavAudioFormat wavFormat;
        if (auto writer = wavFormat.createWriterFor(fileStream.get(), sampleRate, CHANNEL_COUNT, bitRate, {}, 0)) {
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

class AudioLevelsPrivate {
public:
  AudioLevelsPrivate() {
    for(int i = 0; i < TRACKS_COUNT; ++i) {
      trackWriters << new DiskWriter;
      tracksToRecord << false;
      levels.append(QVariant::fromValue<float>(0));
    }
  }
  ~AudioLevelsPrivate() {
    for (DiskWriter* trackWriter : trackWriters) {
      delete trackWriter;
    }
    delete globalPlaybackWriter;
  }
  DiskWriter* globalPlaybackWriter{new DiskWriter};
  QList<DiskWriter*> trackWriters;
  QVariantList tracksToRecord;
  QVariantList levels;
};

static int audioLevelsJackProcessCb(jack_nframes_t nframes, void* arg) {
    return static_cast<AudioLevels*>(arg)->_audioLevelsJackProcessCb(nframes);
}

AudioLevels::AudioLevels(QObject *parent)
  : QObject(parent)
  , d(new AudioLevelsPrivate)
{
    audioLevelsJackClient = jack_client_open(
      "zynthiloops_audio_levels_client",
      JackNullOption,
      &audioLevelsJackStatus
    );

    if (audioLevelsJackClient) {
      qWarning() << "Initialized Audio Levels Jack Client zynthiloops_client";

      capturePortA = jack_port_register(
        audioLevelsJackClient,
        "capture_port_a",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      capturePortB = jack_port_register(
        audioLevelsJackClient,
        "capture_port_b",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );

      playbackPortA = jack_port_register(
        audioLevelsJackClient,
        "playback_port_a",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );
      playbackPortB = jack_port_register(
        audioLevelsJackClient,
        "playback_port_b",
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
      );

      // TRACKS PORT A/B REGISTRATION
      for (int i=0; i<TRACKS_COUNT; i++) {
          tracksPortA[i] = jack_port_register(
            audioLevelsJackClient,
            QString("T%1A").arg(i+1).toStdString().c_str(),
            JACK_DEFAULT_AUDIO_TYPE,
            JackPortIsInput,
            0
          );
          tracksPortB[i] = jack_port_register(
            audioLevelsJackClient,
            QString("T%1B").arg(i+1).toStdString().c_str(),
            JACK_DEFAULT_AUDIO_TYPE,
            JackPortIsInput,
            0
          );
      }

      if (
        jack_set_process_callback(
          audioLevelsJackClient,
          audioLevelsJackProcessCb,
          static_cast<void*>(this)
        ) != 0
      ) {
        qWarning() << "Failed to set the Audio Levels Jack Client processing callback";
      } else {
        if (jack_activate(audioLevelsJackClient) == 0) {
          qWarning() << "Successfully created and set up the Audio Levels Jack client";

          if (jack_connect(audioLevelsJackClient, "system:capture_1", jack_port_name(capturePortA)) == 0) {
              qDebug() << "Successfully connected audio level jack output to the system capture port A";
          } else {
              qWarning() << "Failed to connect audio level jack output to the system capture port A";
          }

          if (jack_connect(audioLevelsJackClient, "system:capture_2", jack_port_name(capturePortB)) == 0) {
              qDebug() << "Successfully connected audio level jack output to the system capture port B";
          } else {
              qWarning() << "Failed to connect audio level jack output to the system capture port B";
          }
        } else {
          qWarning() << "Failed to activate Audio Levels Jack client";
        }
      }
    } else {
      qWarning() << "Error initializing Audio Levels Jack Client zynthiloops_client";
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

void AudioLevels::timerCallback() {
    Q_EMIT audioLevelsChanged();
}

inline float addFloat(const float& db1, const float &db2) {
   return 10 * log10f(pow(10, db1/10) + pow(10, db2/10));
}
const float** recordingPassthroughBuffer = new const float* [2];
jack_default_audio_sample_t *captureBufA{nullptr},
                            *captureBufB{nullptr},
                            *playbackBufA{nullptr},
                            *playbackBufB{nullptr},
                            *tracksBufA[TRACKS_COUNT] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
                            *tracksBufB[TRACKS_COUNT] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
int trackIndex{0};
jack_nframes_t frameIndex{0};
int AudioLevels::_audioLevelsJackProcessCb(jack_nframes_t nframes) {
    capturePeakA = 0.0f;
    capturePeakB = 0.0f;
    playbackPeakA = 0.0f;
    playbackPeakB = 0.0f;

    captureBufA = (jack_default_audio_sample_t *)jack_port_get_buffer(capturePortA, nframes);
    captureBufB = (jack_default_audio_sample_t *)jack_port_get_buffer(capturePortB, nframes);
    playbackBufA = (jack_default_audio_sample_t *)jack_port_get_buffer(playbackPortA, nframes);
    playbackBufB = (jack_default_audio_sample_t *)jack_port_get_buffer(playbackPortB, nframes);

    for (trackIndex=0; trackIndex<TRACKS_COUNT; trackIndex++) {
        tracksPeakA[trackIndex] = 0.0f;
        tracksPeakB[trackIndex] = 0.0f;
        tracksBufA[trackIndex] = (jack_default_audio_sample_t *)jack_port_get_buffer(tracksPortA[trackIndex], nframes);
        tracksBufB[trackIndex] = (jack_default_audio_sample_t *)jack_port_get_buffer(tracksPortB[trackIndex], nframes);
    }

    if (d->globalPlaybackWriter->isRecording()) {
      recordingPassthroughBuffer[0] = playbackBufA;
      recordingPassthroughBuffer[1] = playbackBufB;
      d->globalPlaybackWriter->processBlock(recordingPassthroughBuffer, (int)nframes);
    }

    for (trackIndex = 0; trackIndex < TRACKS_COUNT; ++trackIndex) {
      const DiskWriter *diskWriter = d->trackWriters.at(trackIndex);
      if (diskWriter->isRecording()) {
        // we need booooth left and right channels in a single array...
        recordingPassthroughBuffer[0] = tracksBufA[trackIndex];
        recordingPassthroughBuffer[1] = tracksBufB[trackIndex];
        diskWriter->processBlock(recordingPassthroughBuffer, (int)nframes);
      }
    }

    for (frameIndex=0; frameIndex<nframes; frameIndex++) {
        const float captureSampleA = fabs(captureBufA[frameIndex]),
                    captureSampleB = fabs(captureBufB[frameIndex]),
                    playbackSampleA = fabs(playbackBufA[frameIndex]),
                    playbackSampleB = fabs(playbackBufB[frameIndex]);

        if (captureSampleA > capturePeakA) {
            capturePeakA = captureSampleA;
        }
        if (captureSampleB > capturePeakB) {
            capturePeakB = captureSampleB;
        }

        if (playbackSampleA > playbackPeakA) {
            playbackPeakA = playbackSampleA;
        }
        if (playbackSampleB > playbackPeakB) {
            playbackPeakB = playbackSampleB;
        }

        for (trackIndex=0; trackIndex<TRACKS_COUNT; trackIndex++) {
            const float tracksSampleA = fabs(tracksBufA[trackIndex][frameIndex]);
            if (tracksSampleA > tracksPeakA[trackIndex]) {
                tracksPeakA[trackIndex] = tracksSampleA;
            }
            const float tracksSampleB = fabs(tracksBufB[trackIndex][frameIndex]);
            if (tracksSampleB > tracksPeakB[trackIndex]) {
                tracksPeakB[trackIndex] = tracksSampleB;
            }
        }
    }

    const float captureDbA{convertTodbFS(capturePeakA * 0.2)},
                captureDbB{convertTodbFS(capturePeakB * 0.2)},
                playbackDbA{convertTodbFS(playbackPeakA * 0.2)},
                playbackDbB{convertTodbFS(playbackPeakB * 0.2)};

    captureA = captureDbA <= -200 ? -200 : captureDbA;
    captureB = captureDbB <= -200 ? -200 : captureDbB;

    playbackA = playbackDbA <= -200 ? -200 : playbackDbA;
    playbackB = playbackDbB <= -200 ? -200 : playbackDbB;

    for (trackIndex = 0; trackIndex < TRACKS_COUNT; ++trackIndex) {
      const float dbA = convertTodbFS(tracksPeakA[trackIndex] * 0.2),
                  dbB = convertTodbFS(tracksPeakB[trackIndex] * 0.2);
      tracksA[trackIndex] = dbA <= -200 ? -200 : dbA;
      tracksB[trackIndex] = dbB <= -200 ? -200 : dbB;
    }

    return 0;
}

float AudioLevels::add(float db1, float db2) {
    return addFloat(db1, db2);
}

const QVariantList AudioLevels::getTracksAudioLevels() {
    for (int trackIndex=0; trackIndex<TRACKS_COUNT; trackIndex++) {
      d->levels[trackIndex].setValue<float>(addFloat(tracksA[trackIndex], tracksB[trackIndex]));
    }

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

void AudioLevels::setTrackToRecord(int track, bool shouldRecord)
{
  if (track > -1 && track < d->trackWriters.count()) {
    d->trackWriters[track]->setShouldRecord(shouldRecord);
    d->tracksToRecord[track] = shouldRecord;
    Q_EMIT tracksToRecordChanged();
  }
}

QVariantList AudioLevels::tracksToRecord() const
{
  return d->tracksToRecord;
}

void AudioLevels::setTrackFilenamePrefix(int track, const QString& fileNamePrefix)
{
  if (track > -1 && track < d->trackWriters.count()) {
    d->trackWriters[track]->setFilenamePrefix(fileNamePrefix);
  }
}

void AudioLevels::startRecording()
{
  const QString timestamp{QDateTime::currentDateTime().toString(Qt::ISODate)};
  const double sampleRate = jack_get_sample_rate(audioLevelsJackClient);
  // Doing this in two goes, because when we ask recording to start, it will very extremely start,
  // and we kind of want to at least get pretty close to them starting at the same time, so let's
  // do this bit of the ol' filesystem work first
  QString dirPath = d->globalPlaybackWriter->filenamePrefix().left(d->globalPlaybackWriter->filenamePrefix().lastIndexOf('/'));
  if (!QDir().exists(dirPath)) {
    QDir().mkpath(dirPath);
  }
  for (DiskWriter *trackWriter : d->trackWriters) {
    QString dirPath = trackWriter->filenamePrefix().left(trackWriter->filenamePrefix().lastIndexOf('/'));
    if (!QDir().exists(dirPath)) {
      QDir().mkpath(dirPath);
    }
  }
  if (d->globalPlaybackWriter->shouldRecord()) {
    const QString filename = QString("%1-%2.wav").arg(d->globalPlaybackWriter->filenamePrefix()).arg(timestamp);
    d->globalPlaybackWriter->startRecording(filename, sampleRate);
  }
  for (DiskWriter *trackWriter : d->trackWriters) {
    if (trackWriter->shouldRecord()) {
      const QString filename = QString("%1-%2.wav").arg(trackWriter->filenamePrefix()).arg(timestamp);
      trackWriter->startRecording(filename, sampleRate);
    }
  }
}

void AudioLevels::stopRecording()
{
  d->globalPlaybackWriter->stop();
  for (DiskWriter *trackWriter : d->trackWriters) {
    trackWriter->stop();
  }
}
