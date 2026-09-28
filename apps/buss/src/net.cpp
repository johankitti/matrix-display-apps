#include "net.h"
#include "config.h"

#include <net_core.h>   // shared resilient Wi-Fi provisioning (WiFiManager)

// net-core owns the whole bring-up (patient saved-network retries + timed portal
// windows, never dead-ending). This file is just the buss wrapper: it feeds
// net-core the app's AP name / timeouts and draws the panel status callbacks.

// Fires before each saved-network attempt.
static void onConnecting() { netShowStatus("wifi...", nullptr); }

// Fires when the setup AP comes up — show the AP name and the portal IP so
// Wi-Fi can be configured from a phone with no serial console. (No "join "
// prefix: with it the SSID overflows the 16 chars a TomThumb row fits.)
static void onPortal(const char *ssid, const char *ip) { netShowStatus(ssid, ip); }

bool netStart() {
  NetConfig cfg;
  cfg.apSsid = AP_SETUP_SSID;
  cfg.connectTimeoutMs = NET_CONNECT_TIMEOUT_MS;
  cfg.portalTimeoutSec = NET_PORTAL_TIMEOUT_SEC;
  cfg.onConnecting = onConnecting;
  cfg.onPortal = onPortal;
  return netStart(cfg);
}
