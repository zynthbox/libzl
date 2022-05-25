#pragma once

#include "JUCEHeaders.h"

namespace Helper {

template <typename Function>
void callFunctionOnMessageThread(Function&& func, bool shouldWait = false, int waitLimit = -1) {
  {
    if (MessageManager::getInstance()->isThisTheMessageThread()) {
      func();
    } else {
      jassert(!MessageManager::getInstance()
                   ->currentThreadHasLockedMessageManager());
      WaitableEvent finishedSignal;
      MessageManager::callAsync([&] {
        func();
        finishedSignal.signal();
      });

      if (shouldWait) {
        finishedSignal.wait(waitLimit);
      }
    }
  }
}

void removeAllClips(te::AudioTrack& track);

te::AudioTrack* getOrInsertAudioTrackAt(te::Edit& edit, int index);

te::WaveAudioClip::Ptr loadAudioFileAsClip(te::Edit& edit, const File& file);

}  // namespace Helper
