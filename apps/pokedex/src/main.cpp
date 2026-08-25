#include <Arduino.h>

#include <input_core.h>   // shared rotary-encoder brightness knob + button

#include "config.h"
#include "display.h"
#include "net.h"
#include "pokemon.h"
#include "settings.h"
#include "web.h"

static char curName[24] = "";
static int  curId = -1;
static bool nameScrolls = false;
static uint32_t slideStartMs = 0;

// Prefetched next slide: its sprite bytes are fetched over the network while the
// current slide is still on screen, so the swap only costs decoding + the slide
// timing tracks the duration clock instead of duration + fetch time.
// The fetch runs on a background task (core 0) so the network latency never blocks
// the animation ticking on the main loop (core 1). pendReady is the handshake flag:
// the task writes the buffer/metadata then sets it last; the main loop consumes it
// only while true and clears it before requesting the next fetch.
static uint8_t*   pendBuf   = nullptr;
static size_t     pendLen   = 0;
static char       pendName[24] = "";
static int        pendId    = -1;
static SpriteType pendType  = SPRITE_PNG;
static volatile bool pendReady = false;
static volatile bool fetchBusy = false;   // task is mid-fetch
static TaskHandle_t  fetchTask = nullptr;

// Raw sprite bytes of the slide currently on screen. For GIFs the animation decoder
// re-reads this on every frame, so it must live until the next advanceSlide(). (PNG
// slides decode into the canvas up front, so their bytes are freed immediately.)
static uint8_t* curGifBuf = nullptr;

// Core-0 worker: blocks on the network (incl. TLS + retries) so the render loop on
// core 1 keeps animating. Sleeps until poked, fetches one sprite, publishes it.
static void fetchTaskFn(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // wait for a prefetch request
        if (!pendReady) {
            uint8_t* buf = nullptr; size_t len = 0; int id = -1; char nm[24];
            SpriteType type = SPRITE_PNG;
            if (pokemonPrefetch(nm, sizeof(nm), &id, &buf, &len, &type)) {
                pendBuf = buf; pendLen = len; pendId = id; pendType = type;
                strncpy(pendName, nm, sizeof(pendName) - 1);
                pendName[sizeof(pendName) - 1] = 0;
                pendReady = true;                  // publish last: gates the reader
            }
        }
        fetchBusy = false;
    }
}

// Ask the background task to fetch the next sprite (no-op if one is ready or in
// flight). Returns immediately — never blocks the render loop.
static void requestPrefetch() {
    if (pendReady || fetchBusy || !fetchTask) return;
    fetchBusy = true;
    xTaskNotifyGive(fetchTask);
}

// Swap to the prefetched slide if one is ready. If not, show the loading animation
// and keep the request in flight; the loop retries once pendReady flips. Either
// way, kick off the following fetch.
static void advanceSlide() {
    // Stop the current animation and free its GIF before we reuse the slot. Order
    // matters: close the decoder (it references curGifBuf) before freeing the buffer.
    displayAnimStop();
    if (curGifBuf) { netFree(curGifBuf); curGifBuf = nullptr; }

    bool ok = false;
    if (pendReady) {
        if (pendType == SPRITE_GIF) {
            ok = displayAnimStart(pendBuf, pendLen, pendName, pendId);
            if (ok) curGifBuf = pendBuf;         // keep alive — decoded frame by frame
            else    netFree(pendBuf);
        } else {
            ok = displayShowStatic(pendBuf, pendLen, pendName, pendId);
            netFree(pendBuf);                    // decoded into the canvas; raw not needed
        }
        pendBuf = nullptr;
        pendReady = false;                       // consumed — free the slot for the next
    }

    if (ok) {
        curId = pendId;
        strncpy(curName, pendName, sizeof(curName) - 1);
        curName[sizeof(curName) - 1] = 0;
        nameScrolls = displayNameScrolls(curName);
        displayDrawName(curName);
    } else {
        displayLoadingStart();                   // nothing ready — animate while we fetch
        curId = -1;
    }
    slideStartMs = millis();
    // NB: the next prefetch is deliberately NOT kicked here — loop() starts it a
    // bit into the slide so the network doesn't stutter the opening frames.
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[boot] Pokedex Display");

    settingsLoad();
    Serial.printf("[boot] duration=%ds order=%s anim=%u brightness=%u\n",
                  g_settings.durationSec, g_settings.randomOrder ? "random" : "seq",
                  g_settings.animMode, g_settings.brightness);

    displayInit();
    displaySetBrightness(g_settings.brightness);   // apply the saved brightness
    displaySetSpeed(g_settings.speedPct);          // apply the saved playback speed
    inputInit();                                   // rotary encoder + BOOT button

    // No physical setup button on the finished unit: connect straight away. netStart()
    // loops until connected — patiently retrying the saved network (riding out a
    // router still booting after a power cut), and offering the setup portal only if
    // that keeps failing. It shows the connecting / setup screens itself.
    netStart();
    Serial.println("[net] connected");

    // Start the clock over NTP so the night schedule knows the local hour.
    sleepBeginNtp(g_settings.night);

    webInit();   // settings server at http://<HOSTNAME>.local/

    // Background fetcher on core 0. Big stack: TLS handshakes are stack-hungry.
    xTaskCreatePinnedToCore(fetchTaskFn, "fetch", 12288, nullptr, 1, &fetchTask, 0);

    // Kick the first fetch and show the loading animation; loop() flips to the first
    // slide as soon as it lands (curId < 0 branch), then keeps the pipeline full.
    displayLoadingStart();
    curId = -1;
    slideStartMs = millis();
    requestPrefetch();
}

// Adjust brightness from the encoder and persist it once the knob goes idle. The
// read/step/clamp/debounced-save logic is shared (input-core); we just hand it our
// brightness field, the clamp/step tuning, and settingsSave to persist.
static void handleBrightnessKnob() {
    if (brightnessKnobTick(g_settings.brightness, BRIGHTNESS_MIN, BRIGHTNESS_MAX,
                           BRIGHTNESS_STEP, BRIGHTNESS_SAVE_IDLE_MS, settingsSave))
        Serial.printf("[main] brightness=%u\n", g_settings.brightness);
}

void loop() {
    // Inside the night window: stop the render tasks and deep-sleep till morning.
    if (sleepIsNight(g_settings.night)) {
        displayLoadingStop();
        sleepUntilMorning(g_settings.night);
    }

    webTick();                 // serve any pending settings requests (non-blocking)
    handleBrightnessKnob();    // rotary encoder -> live brightness (+ debounced save)

    if (inputButtonPressed())  // BOOT / encoder switch: skip to the next Pokémon
        advanceSlide();

    if (curId < 0) {                              // waiting on a fetch (boot or dropout)
        requestPrefetch();                        // keep a request in flight (retries)
        if (pendReady) advanceSlide();            // landed — show it
    } else {
        uint32_t elapsed = millis() - slideStartMs;
        bool overdue = elapsed >= (uint32_t)g_settings.durationSec * 1000UL;

        // Start the next fetch once we're past the opening moment (or when overdue),
        // capped at half the slide so short durations still prefetch in time.
        uint32_t lead = (uint32_t)g_settings.durationSec * 1000UL / 2;
        if (lead > PREFETCH_LEAD_MS) lead = PREFETCH_LEAD_MS;
        if (overdue || elapsed >= lead) requestPrefetch();

        if (overdue && pendReady) {
            advanceSlide();                       // next is ready — swap on schedule
        } else {
            displayAnimTick();                    // keep animating this slide
            if (nameScrolls) displayDrawName(curName);   // and the long-name marquee
        }
    }

    delay(10);
}
