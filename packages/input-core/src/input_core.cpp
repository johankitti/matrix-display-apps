#include "input_core.h"

#include <board_config.h>    // PIN_ENC_A/B/SW + ENC_COUNTS_PER_DETENT
#include <display_core.h>    // displaySetBrightness()
#include <ESP32Encoder.h>

static ESP32Encoder enc;
static int64_t lastCount = 0;
static int lastBtn = HIGH;
static uint32_t lastBtnMs = 0;

void inputInit() {
    ESP32Encoder::useInternalWeakPullResistors = puType::up;
    enc.attachFullQuad(PIN_ENC_A, PIN_ENC_B);   // e.g. GPIO44 (A) + GPIO14 (B)
    enc.clearCount();
    lastCount = 0;

    pinMode(PIN_ENC_SW, INPUT_PULLUP);          // e.g. GPIO0 (BOOT)
    lastBtn = digitalRead(PIN_ENC_SW);
}

int inputReadDelta() {
    int64_t c = enc.getCount();
    int64_t diff = c - lastCount;
    int steps = (int)(diff / ENC_COUNTS_PER_DETENT);
    if (steps != 0) {
        // Consume whole detents only; keep the sub-detent remainder for next time.
        lastCount += (int64_t)steps * ENC_COUNTS_PER_DETENT;
    }
    return steps;
}

bool inputButtonPressed() {
    int b = digitalRead(PIN_ENC_SW);
    bool pressed = false;
    if (b == LOW && lastBtn == HIGH && (millis() - lastBtnMs) > 200) {
        pressed = true;
        lastBtnMs = millis();
    }
    lastBtn = b;
    return pressed;
}

// Shared knob->brightness glue. State is process-global (one encoder per board).
static bool     briDirty = false;
static uint32_t briChangedMs = 0;

bool brightnessKnobTick(uint8_t& brightness, uint8_t lo, uint8_t hi,
                        uint8_t step, uint32_t saveIdleMs, void (*saveFn)()) {
    bool changed = false;
    int delta = inputReadDelta();
    if (delta != 0) {
        int b = (int)brightness + delta * (int)step;
        if (b < lo) b = lo;
        if (b > hi) b = hi;
        if ((uint8_t)b != brightness) {
            brightness = (uint8_t)b;
            displaySetBrightness(brightness);   // apply live
            changed = true;
        }
        briDirty = true;                        // note the turn even at a clamp edge
        briChangedMs = millis();
    }
    // Persist once the knob has gone quiet, so a fast spin makes one NVS write.
    if (briDirty && (millis() - briChangedMs) > saveIdleMs) {
        if (saveFn) saveFn();
        briDirty = false;
    }
    return changed;
}
