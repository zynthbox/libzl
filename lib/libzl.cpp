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

juce::ScopedJuceInitialiser_GUI platform;

ZynthiLoopsComponent* ZynthiLoopsComponent_new() {
  return new ZynthiLoopsComponent();
}

void ZynthiLoopsComponent_play(ZynthiLoopsComponent* c) { c->play(); }

void ZynthiLoopsComponent_stop(ZynthiLoopsComponent* c) { c->stop(); }

int ZynthiLoopsComponent_getDuration(ZynthiLoopsComponent* c) {
  return c->getDuration();
}
