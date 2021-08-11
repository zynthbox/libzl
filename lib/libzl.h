/*
  ==============================================================================

    libzl.h
    Created: 10 Aug 2021 10:12:17am
    Author:  root

  ==============================================================================
*/

#pragma once

#include "ZynthiLoopsComponent.h"

extern "C" {
void playWav();
void stopWav();
void init();

ZynthiLoopsComponent* ZynthiLoopsComponent_new() {
  return new ZynthiLoopsComponent();
}
void ZynthiLoopsComponent_play(ZynthiLoopsComponent* c) { c->play(); }
void ZynthiLoopsComponent_stop(ZynthiLoopsComponent* c) { c->stop(); }
}
