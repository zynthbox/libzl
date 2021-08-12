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

ZynthiLoopsComponent* ZynthiLoopsComponent_new(const char* filepath) {
  return new ZynthiLoopsComponent(filepath);
}

void ZynthiLoopsComponent_play(ZynthiLoopsComponent* c) { c->play(); }

void ZynthiLoopsComponent_stop(ZynthiLoopsComponent* c) { c->stop(); }

float ZynthiLoopsComponent_getDuration(ZynthiLoopsComponent* c) {
  return c->getDuration();
}

const char* ZynthiLoopsComponent_getFileName(ZynthiLoopsComponent* c) {
  return c->getFileName();
}

void ZynthiLoopsComponent_setStartPosition(ZynthiLoopsComponent* c,
                                           float startPositionInSeconds) {
  c->setStartPosition(startPositionInSeconds);
}

void ZynthiLoopsComponent_setLength(ZynthiLoopsComponent* c,
                                    float lengthInSeconds) {
  c->setLength(lengthInSeconds);
}
