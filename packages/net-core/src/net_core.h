#pragma once
// =============================================================================
//  net-core — Wi-Fi provisioning + HTTP fetch shared across the display apps
//
//  Credentials are provisioned on-device via WiFiManager's captive portal and
//  stored in NVS by the ESP core — there is no secrets.h and nothing compiled
//  in. netStart() is the headless-resilient bring-up every app wants: it can
//  never dead-end, alternating "patiently retry the saved network" (rides out a
//  router still booting after a power cut) with a timed setup-portal window.
//
//  net-core stays UI-agnostic: it reports progress through the callbacks in
//  NetConfig, so each app draws its own connecting / setup screens.
// =============================================================================

#include <Arduino.h>

struct NetConfig {
  const char* apSsid = "Display-Setup";  // setup access-point name
  uint32_t connectTimeoutMs = 180000;    // retry saved creds this long (3 min)
  uint16_t portalTimeoutSec = 180;       // then a portal window this long, then retry
  // Optional UI hooks (nullptr = silent). onConnecting fires before each
  // saved-network attempt; onPortal fires when the setup AP comes up, with the
  // AP SSID and the portal IP to display.
  void (*onConnecting)() = nullptr;
  void (*onPortal)(const char* ssid, const char* ip) = nullptr;
};

// Bring up Wi-Fi as a station. Blocks until connected (cycling saved-network
// retries and portal windows forever, so it cannot get permanently stuck).
// Returns true once connected.
bool netStart(const NetConfig& cfg);

// Reconnect using saved creds if the link dropped (never opens the portal).
// Returns true if connected within a short window.
bool netEnsure();

// HTTP(S) GET `url` into a freshly-allocated buffer (PSRAM preferred, internal
// RAM fallback). On success sets *outBuf/*outLen and returns true; the caller
// owns the buffer and must netFree(*outBuf). TLS certs are not verified (the
// content is public).
bool httpGetBinary(const char* url, uint8_t** outBuf, size_t* outLen);

// Free a buffer returned by httpGetBinary().
void netFree(uint8_t* buf);
