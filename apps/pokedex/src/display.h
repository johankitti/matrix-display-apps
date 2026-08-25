#pragma once
#include <Arduino.h>
#include <display_core.h>   // shared panel object (dma_display) + brightness helper

// Initialise the HUB75 panel (pins/geometry from board-config, via display-core).
void displayInit();

// Blank the whole panel.
void displayClear();

// State 1: the "POKEDEX" splash — a title header + Poké Ball, with an animated
// "<label>" / "<label>." / ... status line whose dots cycle on a background task
// so it keeps moving during blocking network fetches. `label` is the status word
// ("Loading" for sprites, "Wifi" while connecting); calling it again just swaps
// the word without restarting the animation. Any sprite or status draw stops it
// automatically; displayLoadingStop() is idempotent.
void displayLoadingStart(const char* label = "Loading");
void displayLoadingStop();

// Begin animating a GIF held in RAM: opens it, draws the first frame + the "#NNN"
// dex label (top-right), and freezes a crop window centered on the sprite. The
// sprite height depends on how many rows `name` needs (1 or 2), so pass the same
// name you'll hand to displayDrawName(). The caller must keep `gif`/`len` valid
// until displayAnimStop() — the decoder reads the buffer on every frame. Returns
// false if the GIF can't be opened.
bool displayAnimStart(const uint8_t* gif, size_t len, const char* name, int id);

// Render the next GIF frame if its delay has elapsed (call every loop iteration);
// loops back to the first frame at the end so motion continues for the whole
// slide. Cheap no-op until the next frame is due. Redraws only the sprite region
// and the dex label, never the name band.
void displayAnimTick();

// Stop animation, close the GIF, and free the frame canvas. Idempotent. Call
// before freeing the GIF buffer handed to displayAnimStart().
void displayAnimStop();

// Decode a still PNG and draw it centered above the name (same crop/label/name
// pipeline as the animated path, but a single frame — displayAnimTick() is a
// no-op afterward). The PNG is decoded into the canvas, so the caller may free
// `png` as soon as this returns. Returns false if the PNG can't be decoded.
bool displayShowStatic(const uint8_t* png, size_t len, const char* name, int id);

// Draw `name` in the bottom strip. Centered if it fits, else marquee-scrolled
// (call every frame to animate). Clears the strip first.
void displayDrawName(const char* name);

// True if `name` is too wide to fit and will marquee-scroll.
bool displayNameScrolls(const char* name);

// Big seconds value in the bottom strip, shown while adjusting the duration.
void displayDrawDuration(int seconds);

// State 0: Wi-Fi setup screen shown while the captive portal is open — the AP
// name to join and the portal address.
void displaySetup(const char* ssid, const char* ip);

// displaySetBrightness() is provided by display-core (display_core.h).

// Set GIF playback speed as a percentage of authored timing (100 = original).
void displaySetSpeed(uint16_t pct);
