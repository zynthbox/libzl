/*
  ==============================================================================

    libzl.cpp
    Created: 9 Aug 2021 6:28:51pm
    Author:  root

  ==============================================================================
*/

#include "libzl.h"

#include <iostream>

#include "JUCEHeaders.h"
#include "ZynthiLoopsComponent.h"

using namespace std;

ZynthiLoopsComponent *zlComponent;

void playWav() {
  zlComponent->play();
  cout << "Play \n";
}

void stopWav() {
  zlComponent->stop();
  cout << "Stop \n";
}

void init() {
  cout << "Init \n";

  juce::ScopedJuceInitialiser_GUI platform;

  juce::AudioDeviceManager deviceManager;
  deviceManager.initialiseWithDefaultDevices(2, 2);

  zlComponent = new ZynthiLoopsComponent();
}
