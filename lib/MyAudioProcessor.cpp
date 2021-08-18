/*******************************************************************************
 The block below describes the properties of this PIP. A PIP is a short snippet
 of code that can be read by the Projucer and used to generate a JUCE project.

 BEGIN_JUCE_PIP_METADATA

  name:             juce_plugin_modules
  version:          0.0.1
  vendor:           Tracktion
  website:          www.tracktion.com
  description:      This example shows how to load an audio clip and adjust its speed and pitch so you can play along with it in a different key or tempo.

  dependencies:     juce_audio_basics, juce_audio_devices, juce_audio_formats, juce_audio_processors, juce_audio_utils,
                    juce_core, juce_data_structures, juce_dsp, juce_events, juce_graphics,
                    juce_gui_basics, juce_gui_extra, juce_osc, tracktion_engine
  exporters:        linux_make, vs2017, xcode_iphone, xcode_mac

  moduleFlags:      JUCE_STRICT_REFCOUNTEDPOINTER=1, JUCE_PLUGINHOST_VST3=1, JUCE_PLUGINHOST_AU=1, TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1

  type:             Component
  mainClass:        TestComponent

 END_JUCE_PIP_METADATA

*******************************************************************************/

#pragma once

#include "JUCEHeaders.h"

#include "../tracktion_engine/modules/tracktion_engine/tracktion_engine.h"

namespace te = tracktion_engine;
using namespace juce;

namespace EngineHelpers
{
    te::Project::Ptr createTempProject (te::Engine& engine)
    {
        auto file = engine.getTemporaryFileManager().getTempDirectory().getChildFile ("temp_project").withFileExtension (te::projectFileSuffix);
        te::ProjectManager::TempProject tempProject (engine.getProjectManager(), file, true);
        return tempProject.project;
    }

    void removeAllClips (te::AudioTrack& track)
    {
        auto clips = track.getClips();

        for (int i = clips.size(); --i >= 0;)
            clips.getUnchecked (i)->removeFromParentTrack();
    }

    te::AudioTrack* getOrInsertAudioTrackAt (te::Edit& edit, int index)
    {
        edit.ensureNumberOfAudioTracks (index + 1);
        return te::getAudioTracks (edit)[index];
    }

    te::WaveAudioClip::Ptr loadAudioFileAsClip (te::Edit& edit, const File& file)
    {
        // Find the first track and delete all clips from it
        if (auto track = getOrInsertAudioTrackAt (edit, 0))
        {
            removeAllClips (*track);

            // Add a new clip to this track
            te::AudioFile audioFile (edit.engine, file);

            if (audioFile.isValid())
                if (auto newClip = track->insertWaveClip (file.getFileNameWithoutExtension(), file,
                                                          { { 0.0, audioFile.getLength() }, 0.0 }, false))
                    return newClip;
        }

        return {};
    }

    template<typename ClipType>
    typename ClipType::Ptr loopAroundClip (ClipType& clip)
    {
        auto& transport = clip.edit.getTransport();
        transport.setLoopRange (clip.getEditTimeRange());
        transport.looping = true;
        transport.position = 0.0;
        transport.play (false);

        return clip;
    }

    void togglePlay (te::Edit& edit)
    {
        auto& transport = edit.getTransport();

        if (transport.isPlaying())
            transport.stop (false, false);
        else
            transport.play (false);
    }

    void toggleRecord (te::Edit& edit)
    {
        auto& transport = edit.getTransport();

        if (transport.isRecording())
            transport.stop (true, false);
        else
            transport.record (false);
    }

    void armTrack (te::AudioTrack& t, bool arm, int position = 0)
    {
        auto& edit = t.edit;
        for (auto instance : edit.getAllInputDevices())
            if (instance->isOnTargetTrack (t, position))
                instance->setRecordingEnabled (t, arm);
    }

    bool isTrackArmed (te::AudioTrack& t, int position = 0)
    {
        auto& edit = t.edit;
        for (auto instance : edit.getAllInputDevices())
            if (instance->isOnTargetTrack (t, position))
                return instance->isRecordingEnabled (t);

        return false;
    }

    bool isInputMonitoringEnabled (te::AudioTrack& t, int position = 0)
    {
        auto& edit = t.edit;
        for (auto instance : edit.getAllInputDevices())
            if (instance->isOnTargetTrack (t, position))
                return instance->getInputDevice().isEndToEndEnabled();

        return false;
    }

    void enableInputMonitoring (te::AudioTrack& t, bool im, int position = 0)
    {
        if (isInputMonitoringEnabled (t, position) != im)
        {
            auto& edit = t.edit;
            for (auto instance : edit.getAllInputDevices())
                if (instance->isOnTargetTrack (t, position))
                    instance->getInputDevice().flipEndToEnd();
        }
    }

    bool trackHasInput (te::AudioTrack& t, int position = 0)
    {
        auto& edit = t.edit;
        for (auto instance : edit.getAllInputDevices())
            if (instance->isOnTargetTrack (t, position))
                return true;

        return false;
    }

    inline std::unique_ptr<juce::KnownPluginList::PluginTree> createPluginTree (te::Engine& engine)
    {
        auto& list = engine.getPluginManager().knownPluginList;

        if (auto tree = list.createTree (list.getTypes(), KnownPluginList::sortByManufacturer))
            return tree;

        return {};
    }

}

class TestComponent   : public Component
{
public:
    TestComponent()
    {
        File f("/zynthian/zynthian-my-data/capture/c4.wav");
        setFile(f);
        updateTempoAndKey();
    }

private:
    //==============================================================================
    te::Engine engine { "test_component" };
    te::Edit edit { engine, te::createEmptyEdit (engine), te::Edit::forEditing, nullptr, 0 };
    te::TransportControl& transport { edit.getTransport() };

    //==============================================================================
    te::WaveAudioClip::Ptr getClip()
    {
        if (auto track = EngineHelpers::getOrInsertAudioTrackAt (edit, 0))
            if (auto clip = dynamic_cast<te::WaveAudioClip*> (track->getClips()[0]))
                return *clip;

        return {};
    }

    File getSourceFile()
    {
        if (auto clip = getClip())
            return clip->getSourceFileReference().getFile();

        return {};
    }

    void setFile (const File& f)
    {
        if (auto clip = EngineHelpers::loadAudioFileAsClip (edit, f))
        {
            // Disable auto tempo and pitch, we'll handle these manually
            clip->setAutoTempo (false);
            clip->setAutoPitch (false);
            clip->setTimeStretchMode (te::TimeStretcher::defaultMode);

            EngineHelpers::loopAroundClip (*clip);
        }
        else
        {
        }
    }

    void updateTempoAndKey()
    {
        if (auto clip = getClip())
        {
            auto f = getSourceFile();
            const auto audioFileInfo = te::AudioFile (engine, f).getInfo();
            const double baseTempo = 120.0;

            // First update the tempo based on the ratio between the root tempo and tempo slider value
            if (baseTempo > 0.0)
            {
                const double ratio = 220.0 / baseTempo;
                clip->setSpeedRatio (ratio);
                clip->setLength (audioFileInfo.getLengthInSeconds() / clip->getSpeedRatio(), true);
            }

            // Then update the pitch change based on the slider value
            clip->setPitchChange (12);
        }
    }
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return nullptr;
}

extern "C" {
void init() {
    auto p = TestComponent();
}
}
