/*
  ==============================================================================

    libzl.h
    Created: 10 Aug 2021 10:12:17am
    Author:  root

  ==============================================================================
*/

#pragma once

#include "ClipAudioSource.h"
#include "SyncTimer.h"

extern "C" {

//////////////
/// ClipAudioSource API Bridge
//////////////
ClipAudioSource* ClipAudioSource_new(const char* filepath);
void ClipAudioSource_setProgressCallback(ClipAudioSource* c, void *obj, void (*functionPtr)(void*));
void ClipAudioSource_connectProgress(ClipAudioSource* c, void *obj);
void ClipAudioSource_play(ClipAudioSource* c, bool loop);
void ClipAudioSource_stop(ClipAudioSource* c);
float ClipAudioSource_getDuration(ClipAudioSource* c);
float ClipAudioSource_getProgress(ClipAudioSource* c);
const char* ClipAudioSource_getFileName(ClipAudioSource* c);
void ClipAudioSource_setStartPosition(ClipAudioSource* c,
                                      float startPositionInSeconds);
void ClipAudioSource_setLength(ClipAudioSource* c, float lengthInSeconds);
void ClipAudioSource_setSpeedRatio(ClipAudioSource* c, float speedRatio);
void ClipAudioSource_setPitch(ClipAudioSource* c, float pitchChange);
void ClipAudioSource_destroy(ClipAudioSource* c);
//////////////
/// END ClipAudioSource API Bridge
//////////////

//////////////
/// SyncTimer API Bridge
//////////////
void SyncTimer_startTimer(int interval);
void SyncTimer_stopTimer();
void SyncTimer_registerTimerCallback(void (*functionPtr)());
void SyncTimer_queueClipToStart(ClipAudioSource* clip);
void SyncTimer_queueClipToStop(ClipAudioSource* clip);
//////////////
/// END SyncTimer API Bridge
//////////////

//////////////
/// WavMetadataHelper API Bridge
//////////////
void WavMetadataHelper_readMetadataFromWav(const char *file);
void WavMetadataHelper_writeMetadataToWav(const char *file);
//////////////
/// END WavMetadataHelper API Bridge
//////////////

void initJuce();
void shutdownJuce();
void registerGraphicTypes();
void stopClips(int size, ClipAudioSource** clips);
}
