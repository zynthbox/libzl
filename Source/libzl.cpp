/*
  ==============================================================================

    libzl.cpp
    Created: 9 Aug 2021 6:28:51pm
    Author:  root

  ==============================================================================
*/

// #include "JuceHeader.h"
#include <iostream>
#include "libzl.h"
//#include <juce_events/juce_events.h>
#include <chrono>
#include <thread>
//#include <QCoreApplication>

//using namespace std::chrono;

#include "ZynthiLoopsComponent.h"

using namespace std;

ZynthiLoopsComponent *zlComponent;

void playWav() {
    zlComponent->play();
    cout << "Play \n";
    
    //std::this_thread::sleep_for(2000ms);
}

void stopWav() {
    zlComponent->stop();
    cout << "Stop \n";
}

void init() {
    cout << "Init \n";
    //char *argv[] = {"libzl", NULL};
    //int argc = sizeof(argv) / sizeof(char*) - 1;

    //QCoreApplication app(argc, argv);
    
    juce::ScopedJuceInitialiser_GUI platform;
    
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(2,2);
    //deviceManager.initialise(255,255,NULL,true);
    deviceManager.playTestSound();
    
    auto setup = deviceManager.getAudioDeviceSetup ();
    cout << "Output Device Name :" << setup.outputDeviceName << "\n";
    

    //std::this_thread::sleep_for(2000ms);
    
    zlComponent = new ZynthiLoopsComponent();
    
    //app.exec();
}

/*juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    DBG("Create Plugin instance");
}*/