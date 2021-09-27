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

private:
  class Private;
  Private *d = nullptr;
};
