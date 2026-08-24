#include "pokemon.h"
#include "config.h"
#include "net.h"
#include "display.h"
#include "names.h"
#include "settings.h"

#include <pgmspace.h>
#include <esp_random.h>
#include <ctype.h>

const char* pokemonName(int id) {
    static char buf[24];
    if (id < 1 || id > DEX_NAME_COUNT) {
        strncpy(buf, "???", sizeof(buf));
        return buf;
    }
    PGM_P p = (PGM_P)pgm_read_ptr(&DEX_NAMES[id - 1]);
    strncpy_P(buf, p, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (char* c = buf; *c; ++c) *c = toupper((unsigned char)*c);   // panel shows NAMES uppercase
    return buf;
}

// Sequential cursor for "in order" mode; starts just below DEX_MIN so the first
// nextId() returns DEX_MIN. Advances one per fetched slide, wrapping at DEX_MAX.
static int s_seqId = DEX_MIN - 1;

// Highest id to draw from: "animated only" restricts to the animated range, the
// other modes span the full dex.
static int dexMax() {
    return (g_settings.animMode == ANIM_MODE_ONLY) ? ANIM_MAX_ID : DEX_MAX;
}

static int nextId() {
    int hi = dexMax();
    if (g_settings.randomOrder) {
        uint32_t span = (uint32_t)(hi - DEX_MIN + 1);
        return DEX_MIN + (int)(esp_random() % span);
    }
    if (++s_seqId > hi || s_seqId < DEX_MIN) s_seqId = DEX_MIN;
    return s_seqId;
}

bool pokemonPrefetch(char* outName, size_t nameLen, int* outId,
                     uint8_t** outBuf, size_t* outLen, SpriteType* outType) {
    char url[200];
    for (int attempt = 0; attempt < FETCH_MAX_RETRIES; attempt++) {
        int id = nextId();
        const char* nm = pokemonName(id);   // captured now so the layout can size
                                            // itself for a 1- or 2-line name later
        uint8_t* buf = nullptr;
        size_t len = 0;

        // Prefer the animated GIF when enabled and available.
        bool wantGif = (g_settings.animMode != ANIM_MODE_STATIC) && id <= ANIM_MAX_ID;
        if (wantGif) {
            snprintf(url, sizeof(url), ANIM_SPRITE_URL_FMT, id);
            if (httpGetBinary(url, &buf, &len)) {
                *outType = SPRITE_GIF;
                strncpy(outName, nm, nameLen - 1); outName[nameLen - 1] = 0;
                *outId = id; *outBuf = buf; *outLen = len;
                Serial.printf("[poke] prefetch #%d %s (gif, %u bytes)\n", id, nm, (unsigned)len);
                return true;
            }
            netFree(buf); buf = nullptr;
            // "Animated only" must never show a still: a failed GIF (e.g. cold-boot
            // DNS/TLS hiccup) retries another id rather than falling back to static.
            // The loading screen covers the wait until the network settles.
            if (g_settings.animMode == ANIM_MODE_ONLY) {
                Serial.printf("[poke] gif fetch failed id=%d (animated-only, retry)\n", id);
                delay(200);
                continue;
            }
            // Full-dex mode: fall back to the static PNG for the SAME id so ordered
            // mode never skips a Pokémon just because its GIF didn't load.
            Serial.printf("[poke] gif miss id=%d -> static fallback\n", id);
        }

        snprintf(url, sizeof(url), STATIC_SPRITE_URL_FMT, id);
        if (httpGetBinary(url, &buf, &len)) {
            *outType = SPRITE_PNG;
            strncpy(outName, nm, nameLen - 1); outName[nameLen - 1] = 0;
            *outId = id; *outBuf = buf; *outLen = len;
            Serial.printf("[poke] prefetch #%d %s (png, %u bytes)\n", id, nm, (unsigned)len);
            return true;
        }
        netFree(buf);
        Serial.printf("[poke] fetch failed id=%d (attempt %d)\n", id, attempt + 1);
        delay(200);
    }
    return false;
}
