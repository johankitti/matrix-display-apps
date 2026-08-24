#include "input.h"
#include "config.h"

#include <ESP32Encoder.h>

static ESP32Encoder enc;
static int64_t lastCount = 0;
static int lastBtn = HIGH;
static uint32_t lastBtnMs = 0;

void inputInit() {
    ESP32Encoder::useInternalWeakPullResistors = puType::up;
    enc.attachFullQuad(PIN_ENC_A, PIN_ENC_B);   // GPIO44 (A) + GPIO14 (B)
    enc.clearCount();
    lastCount = 0;

    pinMode(PIN_ENC_SW, INPUT_PULLUP);          // GPIO0 (BOOT)
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
