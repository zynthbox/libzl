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
  int getMultiplier();

  /**
   * \brief Schedule a note message to be sent on the next tick of the timer
   * @param midiNote The note you wish to change the state of
   * @param midiChannel The channel you wish to change the given note on
   * @param setOn Whether or not you are turning the note on
   * @param velocity The velocity of the note (only matters if you're turning it on)
   * @param duration An optional duration (0 means don't schedule a release)         (not used yet!)
   * @param delay A delay in ms counting from the beat                               (not used yet!)
   */
  void scheduleNote(unsigned char midiNote, unsigned char midiChannel, bool setOn, unsigned char velocity, int duration, int delay);

  bool timerRunning();
  Q_SIGNAL void timerRunningChanged();
private:
  class Private;
  Private *d = nullptr;
};
