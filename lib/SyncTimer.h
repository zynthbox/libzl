#pragma once

#include <QObject>
#include <QList>
#include <QQueue>

class ClipAudioSource;

using namespace std;

/**
 * \brief Used to schedule clips into the timer's playback queue
 *
 * Roughly equivalent to a midi message, but for clips
 */
struct ClipCommand {
    ClipAudioSource* clip{nullptr};
    int midiNote{-1};
    bool startPlayback{false};
    bool stopPlayback{false};
    // Which slice to use (-1 means no slice, play normal)
    bool changeSlice{false};
    int slice{-1};
    bool changeLooping{false};
    bool looping{false};
    bool changePitch{false};
    float pitchChange{0.0f};
    bool changeSpeed{false};
    float speedRatio{0.0f};
    bool changeGainDb{false};
    float gainDb{0.0f};
    bool changeVolume{false};
    float volume{0.0f};
};

namespace juce {
    class MidiBuffer;
}
class SyncTimerPrivate;
class SyncTimer : public QObject {
  // HighResolutionTimer facade
  Q_OBJECT
public:
  explicit SyncTimer(QObject *parent = nullptr);
  void addCallback(void (*functionPtr)(int));
  void removeCallback(void (*functionPtr)(int));
  void queueClipToStart(ClipAudioSource *clip);
  void queueClipToStop(ClipAudioSource *clip);
  void start(int bpm);
  void stop();
  void stopClip(ClipAudioSource *clip);
  int getInterval(int bpm);
  float subbeatCountToSeconds(quint64 bpm, quint64 beats) const;
  int getMultiplier();

  /**
   * \brief The current beat, where that makes useful sense
   * @returns An integer from 0 through 128
   */
  int beat() const;
  /**
   * \brief The number of ticks since the timer was most recently started
   * @returns The number of times the timer has fired since it was most recently started
   */
  quint64 cumulativeBeat() const;

  /**
   * \brief Schedule an audio clip to have one or more commands run on it on the next tick of the timer
   * If a command with the associated clip is already scheduled at the position and the given midiNote you're attempting to schedule it into,
   * this function will change the existing to match any new settings (that is, things marked to be done on the command
   * will be marked to be done on the existing command).
   * @note This function will take ownership of the command, and you should expect it to no longer exist after (especially if the above happens)
   * @note If you want the clip to loop (or not), set this on the clip itself along with the other clip properties
   * @param clip The audio clip command you wish to fire on at the specified time
   * @param delay A delay in number of timer ticks counting from the current position
   */
  void scheduleClipCommand(ClipCommand *clip, quint64 delay);
  /**
   * \brief Schedule an audio clip to stop on the next tick of the timer
   * If the clip is already scheduled at the position you're attempting to schedule it into, this function will not add multiple
   * @param clip The audio clip you wish to stop playback of
   * @param delay A delay in number of timer ticks counting from the current position
   */
  void scheduleClipToStop(ClipAudioSource *clip, quint64 delay);

  /**
   * \brief Schedule a note message to be sent on the next tick of the timer
   * @note This is not thread-safe in itself - when the timer is running, don't call this function outside of a callback
   * @param midiNote The note you wish to change the state of
   * @param midiChannel The channel you wish to change the given note on
   * @param setOn Whether or not you are turning the note on
   * @param velocity The velocity of the note (only matters if you're turning it on)
   * @param duration An optional duration for on notes (0 means don't schedule a release, higher will schedule an off at the durationth beat from the start of the note)
   * @param delay A delay in numbers of timer ticks counting from the current position
   */
  void scheduleNote(unsigned char midiNote, unsigned char midiChannel, bool setOn, unsigned char velocity, quint64 duration, quint64 delay);

  /**
   * \brief Schedule a buffer of midi messages (the Juce type) to be sent with the given delay
   * @note This is not thread-safe in itself - when the timer is running, don't call this function outside of a callback
   * @param buffer The buffer that you wish to add to the schedule
   * @param delay The delay (if any) you wish to add
   */
  void scheduleMidiBuffer(const juce::MidiBuffer& buffer, quint64 delay);

  /**
   * \brief Send a note message immediately (ensuring it goes through the step sequencer output)
   * @param midiNote The note you wish to change the state of
   * @param midiChannel The channel you wish to change the given note on
   * @param setOn Whether or not you are turning the note on
   * @param velocity The velocity of the note (only matters if you're turning it on)
   */
  void sendNoteImmediately(unsigned char midiNote, unsigned char midiChannel, bool setOn, unsigned char velocity);

  /**
   * \brief Send a set of midi messages out immediately (ensuring they go through the step sequencer output)
   * @param buffer The buffer that you wish to send out immediately
   */
  void sendMidiBufferImmediately(const juce::MidiBuffer& buffer);

  bool timerRunning();
  Q_SIGNAL void timerRunningChanged();
private:
  SyncTimerPrivate *d = nullptr;
};
