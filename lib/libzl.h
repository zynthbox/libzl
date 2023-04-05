/*
  ==============================================================================

    libzl.h
    Created: 10 Aug 2021 10:12:17am
    Author:  root

  ==============================================================================
*/

#pragma once

#include <QObject>

class ClipAudioSource;
class SyncTimer;

extern "C" {

//////////////
/// BEGIN ClipAudioSource API Bridge
//////////////
ClipAudioSource *ClipAudioSource_byID(int id);
ClipAudioSource *ClipAudioSource_new(const char *filepath, bool muted = false);
void ClipAudioSource_setProgressCallback(ClipAudioSource *c,
                                         void (*functionPtr)(float));
void ClipAudioSource_connectProgress(ClipAudioSource *c, void *obj);
void ClipAudioSource_play(ClipAudioSource *c, bool loop);
void ClipAudioSource_stop(ClipAudioSource *c);
void ClipAudioSource_playOnChannel(ClipAudioSource *c, bool loop, int midiChannel);
void ClipAudioSource_stopOnChannel(ClipAudioSource *c, int midiChannel);
float ClipAudioSource_getDuration(ClipAudioSource *c);
const char *ClipAudioSource_getFileName(ClipAudioSource *c);
void ClipAudioSource_setStartPosition(ClipAudioSource *c,
                                      float startPositionInSeconds);
void ClipAudioSource_setLength(ClipAudioSource *c, float beat, int bpm);
void ClipAudioSource_setPan(ClipAudioSource *c, float pan);
void ClipAudioSource_setSpeedRatio(ClipAudioSource *c, float speedRatio);
void ClipAudioSource_setPitch(ClipAudioSource *c, float pitchChange);
void ClipAudioSource_setGain(ClipAudioSource *c, float db);
void ClipAudioSource_setVolume(ClipAudioSource *c, float vol);
void ClipAudioSource_setAudioLevelChangedCallback(ClipAudioSource *c,
                                                  void (*functionPtr)(float));
void ClipAudioSource_setSlices(ClipAudioSource *c, int slices);
int ClipAudioSource_keyZoneStart(ClipAudioSource *c);
void ClipAudioSource_setKeyZoneStart(ClipAudioSource *c, int keyZoneStart);
int ClipAudioSource_keyZoneEnd(ClipAudioSource *c);
void ClipAudioSource_setKeyZoneEnd(ClipAudioSource *c, int keyZoneEnd);
int ClipAudioSource_rootNote(ClipAudioSource *c);
void ClipAudioSource_setRootNote(ClipAudioSource *c, int rootNote);
void ClipAudioSource_destroy(ClipAudioSource *c);
int ClipAudioSource_id(ClipAudioSource *c);
//////////////
/// END ClipAudioSource API Bridge
//////////////

//////////////
/// BEGIN SyncTimer API Bridge
//////////////
QObject *SyncTimer_instance();
void SyncTimer_startTimer(int interval);
void SyncTimer_setBpm(uint bpm);
int SyncTimer_getMultiplier();
void SyncTimer_stopTimer();
void SyncTimer_registerTimerCallback(void (*functionPtr)(int));
void SyncTimer_deregisterTimerCallback(void (*functionPtr)(int));
void SyncTimer_queueClipToStart(ClipAudioSource *clip);
void SyncTimer_queueClipToStartOnChannel(ClipAudioSource *clip, int midiChannel);
void SyncTimer_queueClipToStop(ClipAudioSource *clip);
void SyncTimer_queueClipToStopOnChannel(ClipAudioSource *clip, int midiChannel);
//////////////
/// END SyncTimer API Bridge
//////////////

void initJuce();
void shutdownJuce();
// Called by zynthbox when the configuration in webconf has been changed (for example the midi setup, so our MidiRouter can pick up any changes)
void reloadZynthianConfiguration();
void registerGraphicTypes();
void stopClips(int size, ClipAudioSource **clips);
float dBFromVolume(float vol);

//////////////
/// BEGIN AudioLevels API Bridge
//////////////
bool AudioLevels_isRecording();
void AudioLevels_setRecordGlobalPlayback(bool shouldRecord);
void AudioLevels_setGlobalPlaybackFilenamePrefix(const char *fileNamePrefix);
void AudioLevels_startRecording();
void AudioLevels_stopRecording();
void AudioLevels_setRecordPortsFilenamePrefix(const char *fileNamePrefix);
void AudioLevels_addRecordPort(const char *portName, int channel);
void AudioLevels_removeRecordPort(const char *portName, int channel);
void AudioLevels_clearRecordPorts();
void AudioLevels_setShouldRecordPorts(bool shouldRecord);
/// //////////////
/// END AudioLevels API Bridge
//////////////

//////////////
/// BEGIN JackPassthrough API Bridge
//////////////
/**
 * \brief Set the panning amount for the given channel
 * @param channel The channel you wish to set the pan amount for (-1 is GlobalFX, -2 is GlobalPlayback, 0-9 is the channel with that index)
 * @param amount The amount (-1 through 1, 0 being neutral) that you wish to set as the new pan amount
 */
void JackPassthrough_setPanAmount(int channel, float amount);
/**
* \brief Retrieve the panning amount for the given channel
 * @param channel The channel you wish to get the pan amount for (-1 is GlobalFX, -2 is GlobalPlayback, 0-9 is the channel with that index)
 * @return The panning amount for the given channel (-1 through 1, 0 being neutral or if channel is out of bounds)
 */
float JackPassthrough_getPanAmount(int channel);
//////////////
/// END JackPassthrough API Bridge
//////////////
}
