#include "Helper.h"

tracktion_engine::AudioTrack *Helper::getOrInsertAudioTrackAt(
    tracktion_engine::Edit &edit, int index) {
  edit.ensureNumberOfAudioTracks(index + 1);
  return te::getAudioTracks(edit)[index];
}

void Helper::removeAllClips(tracktion_engine::AudioTrack &track) {
  auto clips = track.getClips();

  for (int i = clips.size(); --i >= 0;)
    clips.getUnchecked(i)->removeFromParentTrack();
}

tracktion_engine::WaveAudioClip::Ptr Helper::loadAudioFileAsClip(
    tracktion_engine::Edit &edit, const File &file) {
  // Find the first track and delete all clips from it
  if (auto track = getOrInsertAudioTrackAt(edit, 0)) {
    removeAllClips(*track);

    // Add a new clip to this track
    te::AudioFile audioFile(edit.engine, file);

    if (audioFile.isValid())
      if (auto newClip =
              track->insertWaveClip(file.getFileNameWithoutExtension(), file,
                                    {{0.0, audioFile.getLength()}, 0.0}, false))
        return newClip;
  }

  return {};
}
