#pragma once
#include <Arduino.h>

// Which decoder the fetched sprite bytes need.
enum SpriteType { SPRITE_PNG = 0, SPRITE_GIF = 1 };

// Look up a display name for a national-dex id (1-based). Returns a pointer to a
// static buffer valid until the next call.
const char* pokemonName(int id);

// Pick the next Pokémon (random or sequential, per g_settings) and fetch ONLY its
// sprite bytes into a freshly allocated buffer (no decode/draw). Chooses the
// animated GIF or the static PNG per g_settings.animMode and the id, falling back
// to the static sprite for the same id if the animated fetch fails. Retries other
// ids on hard failure. On success fills outName/outId/outBuf/outLen/outType and
// returns true; the caller owns the buffer and must netFree() it.
bool pokemonPrefetch(char* outName, size_t nameLen, int* outId,
                     uint8_t** outBuf, size_t* outLen, SpriteType* outType);
