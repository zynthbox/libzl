/*
  ==============================================================================

    libzl.h
    Created: 10 Aug 2021 10:12:17am
    Author:  root

  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_utils/juce_audio_utils.h>

extern "C" {
    void playWav();
    void stopWav();
    void init();
}

/*class Application    : public juce::JUCEApplication
{
public:
    //==============================================================================
    Application() = default;

    const juce::String getApplicationName() override       { return "LoopingAudioSampleBufferTutorial"; }
    const juce::String getApplicationVersion() override    { return "1.0.0"; }

    void initialise (const juce::String&) override
    {
        DBG("Initializing library");
    }

    void shutdown() override
    {
        DBG("Shutting Down");
    }
};

START_JUCE_APPLICATION (Application)*/
