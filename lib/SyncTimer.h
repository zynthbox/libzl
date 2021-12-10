#pragma once

#include <QObject>
#include <QList>
#include <QQueue>

// #include "ClipAudioSource.h"

using namespace std;

namespace juce {
    class MidiBuffer;
}
class ClipAudioSource;
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
