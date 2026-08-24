#pragma once
#include <Arduino.h>

// Set up the rotary encoder (quadrature, brightness knob) and its push button.
void inputInit();

// Net detent steps turned since the last call: +N clockwise, -N counter-clockwise,
// 0 if the knob hasn't moved a full detent.
int inputReadDelta();

// True exactly once per debounced button press (falling edge on PIN_ENC_SW, which
// is the onboard BOOT button — wire the encoder switch in parallel if desired).
bool inputButtonPressed();
