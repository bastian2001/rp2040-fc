#pragma once

#include <Arduino.h>

#define MAX_RTTTL_NOTES 256
#define MAX_RTTTL_TEXT_LENGTH 1024

void initSpeaker();

void speakerLoop();

void makeSound(u16 frequency, u16 duration, u16 tOnMs, u16 tOffMs);

void stopSound();

// sweep from startFrequency to endFrequency over tOnMs, then stop for tOffMs, repeat for duration
void makeSweepSound(u16 startFrequency, u16 endFrequency, u16 duration, u16 tOnMs, u16 tOffMs);

void makeRtttlSound(const char *song);