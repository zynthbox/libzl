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
ZynthiLoopsComponent* ZynthiLoopsComponent_new();
void ZynthiLoopsComponent_play(ZynthiLoopsComponent* c);
void ZynthiLoopsComponent_stop(ZynthiLoopsComponent* c);
float ZynthiLoopsComponent_getDuration(ZynthiLoopsComponent* c);
const char* ZynthiLoopsComponent_getFileName(ZynthiLoopsComponent* c);
}
