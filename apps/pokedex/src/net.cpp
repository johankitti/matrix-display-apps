#include "net.h"
#include "config.h"
#include "display.h"

#include <net_core.h>   // netStart(const NetConfig&) + the reused net helpers

// net-core drives Wi-Fi bring-up and reports progress through plain function
// pointers, so the display callbacks below are non-capturing free functions.

// Shown while net-core patiently retries the saved network on each attempt: the
// POKEDEX splash with an animated "Wifi..." status (dots keep cycling through the
// multi-minute connect window, so the panel doesn't look frozen).
static void onWifiConnecting() { displayLoadingStart("Wifi"); }

// Shown when the setup access point comes up, so Wi-Fi can be configured from a
// phone (join the AP, browse to the portal IP) — no serial console needed.
static void onWifiPortal(const char* ssid, const char* ip) { displaySetup(ssid, ip); }

bool netStart() {
    // net-core cycles saved-network retries with timed portal windows and never
    // dead-ends — exactly what a headless unit with no setup button needs. The
    // AP name and timeouts come from config.h; the callbacks draw our screens.
    NetConfig cfg;
    cfg.apSsid = AP_SETUP_SSID;
    cfg.connectTimeoutMs = NET_CONNECT_TIMEOUT_MS;
    cfg.portalTimeoutSec = NET_PORTAL_TIMEOUT_SEC;
    cfg.onConnecting = onWifiConnecting;
    cfg.onPortal = onWifiPortal;
    return netStart(cfg);
}
