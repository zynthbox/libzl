#pragma once

#include <QObject>
#include <QList>
#include <QQueue>

// #include "ClipAudioSource.h"

using namespace std;

class ClipAudioSource;
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

  bool timerRunning();
  Q_SIGNAL void timerRunningChanged();
private:
  class Private;
  Private *d = nullptr;
};
