#pragma once
// =============================================================================
//  input-core — shared rotary-encoder brightness knob for the matrix apps
//
//  Every app drives the same 64x64 HUB75 rig and, now, the same brightness
//  control: a 24-detent quadrature rotary encoder (electrokit art. 41021049)
//  with a push switch. The pins live in board-config (PIN_ENC_A/B/SW,
//  ENC_COUNTS_PER_DETENT); this package owns the quadrature read, the detent
//  stepping, the debounced button, and the "knob -> live brightness -> save"
//  glue so each app spends just one line in loop().
//
//  The encoder is a passive mechanical switch: common -> GND, internal pull-ups
//  on A/B/SW, no VCC. Call inputInit() once from setup(), after displayInit().
// =============================================================================

#include <Arduino.h>

// Set up the rotary encoder (quadrature) and its push button. Call once from
// setup(), after the panel is up. No-op-safe to call before Wi-Fi/net init.
void inputInit();

// Net detent steps turned since the last call: +N clockwise, -N counter-clockwise,
// 0 if the knob hasn't moved a full detent. Sub-detent motion is carried over.
int inputReadDelta();

// True exactly once per debounced button press (falling edge on PIN_ENC_SW,
// which defaults to the onboard BOOT button — wire the encoder switch there).
bool inputButtonPressed();

// Encoder -> brightness glue shared by every app. Reads the knob; on any turn it
// steps `brightness` by `step` per detent, clamps to [lo, hi], applies it live
// via displaySetBrightness(), and marks it dirty. Once the knob has been idle
// for `saveIdleMs`, calls saveFn() to persist (so NVS isn't hammered mid-turn).
// Returns true on the ticks where the brightness value changed.
//
//   uint8_t& brightness  the app's live+persisted brightness field
//   lo, hi               clamp range (e.g. BRIGHTNESS_MIN..MAX)
//   step                 brightness units per detent (e.g. BRIGHTNESS_STEP)
//   saveIdleMs           idle time before persisting (e.g. 1500)
//   saveFn               the app's settingsSave (nullptr = live only, no persist)
bool brightnessKnobTick(uint8_t& brightness, uint8_t lo, uint8_t hi,
                        uint8_t step, uint32_t saveIdleMs, void (*saveFn)());
