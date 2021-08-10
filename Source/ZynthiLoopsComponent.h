/*
  ==============================================================================

    ZynthiLoopsComponent.h
    Created: 9 Aug 2021 6:25:01pm
    Author:  Anupam Basak <anupam.basak27@gmail.com>

  ==============================================================================
*/

#pragma once

using namespace std;

//==============================================================================
class ZynthiLoopsComponent   : public juce::AudioAppComponent
{
public:
    ZynthiLoopsComponent()
    {
        deviceManager.initialiseWithDefaultDevices(2,2);
        dumpDeviceInfo();

        formatManager.registerBasicFormats();
    }

    ~ZynthiLoopsComponent() override
    {
        shutdownAudio();
    }

    void prepareToPlay (int, double) override {}

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        auto numInputChannels = fileBuffer.getNumChannels();
        auto numOutputChannels = bufferToFill.buffer->getNumChannels();

        auto outputSamplesRemaining = bufferToFill.numSamples;                                  // [8]
        auto outputSamplesOffset = bufferToFill.startSample;                                    // [9]

        while (outputSamplesRemaining > 0)
        {
            auto bufferSamplesRemaining = fileBuffer.getNumSamples() - position;                // [10]
            auto samplesThisTime = juce::jmin (outputSamplesRemaining, bufferSamplesRemaining); // [11]

            for (auto channel = 0; channel < numOutputChannels; ++channel)
            {
                bufferToFill.buffer->copyFrom (channel,                                         // [12]
                                               outputSamplesOffset,                             //  [12.1]
                                               fileBuffer,                                      //  [12.2]
                                               channel % numInputChannels,                      //  [12.3]
                                               position,                                        //  [12.4]
                                               samplesThisTime);                                //  [12.5]
            }

            outputSamplesRemaining -= samplesThisTime;                                          // [13]
            outputSamplesOffset += samplesThisTime;                                             // [14]
            position += samplesThisTime;                                                        // [15]

            if (position == fileBuffer.getNumSamples())
                position = 0;                                                                   // [16]
        }
    }

    void releaseResources() override
    {
        fileBuffer.setSize (0, 0);
    }

    void resized() override
    {
    }

    void play()
    {
        shutdownAudio();
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (juce::File("/zynthian/zynthian-my-data/capture/c4.wav"))); // [2]

        if (reader.get() != nullptr)
        {
            auto duration = (float) reader->lengthInSamples / reader->sampleRate;               // [3]

                fileBuffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);  // [4]
                reader->read (&fileBuffer,                                                      // [5]
                              0,                                                                //  [5.1]
                              (int) reader->lengthInSamples,                                    //  [5.2]
                              0,                                                                //  [5.3]
                              true,                                                             //  [5.4]
                              true);                                                            //  [5.5]
                position = 0;                                                                   // [6]
                setAudioChannels (0, (int) reader->numChannels);                                // [7]
        }
    }

    void stop()
    {
        shutdownAudio();
    }

    void dumpDeviceInfo()
    {
        cout << "--------------------------------------" << "\n";
        cout << "Current audio device type: " + (deviceManager.getCurrentDeviceTypeObject() != nullptr
                                                     ? deviceManager.getCurrentDeviceTypeObject()->getTypeName()
                                                     : "<none>") << "\n";

        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            cout << "Current audio device: "   + device->getName().quoted() << "\n";
            cout << "Sample rate: "    + juce::String (device->getCurrentSampleRate()) + " Hz" << "\n";
            cout << "Block size: "     + juce::String (device->getCurrentBufferSizeSamples()) + " samples" << "\n";
            cout << "Bit depth: "      + juce::String (device->getCurrentBitDepth()) << "\n";
            cout << "Input channel names: "    + device->getInputChannelNames().joinIntoString (", ") << "\n";
            cout << "Active input channels: "  + getListOfActiveBits (device->getActiveInputChannels()) << "\n";
            cout << "Output channel names: "   + device->getOutputChannelNames().joinIntoString (", ") << "\n";
            cout << "Active output channels: " + getListOfActiveBits (device->getActiveOutputChannels()) << "\n";
        }
        else
        {
            cout << "No audio device open" << "\n";
        }
    }

    static juce::String getListOfActiveBits (const juce::BigInteger& b)
    {
        juce::StringArray bits;

        for (auto i = 0; i <= b.getHighestBit(); ++i)
            if (b[i])
                bits.add (juce::String (i));

        return bits.joinIntoString (", ");
    }


    //==========================================================================
    juce::AudioFormatManager formatManager;
    juce::AudioSampleBuffer fileBuffer;
    int position;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZynthiLoopsComponent)
};