#include "net.h"
#include "config.h"

#include <net_core.h>   // shared resilient Wi-Fi provisioning (WiFiManager)

// net-core owns the whole bring-up (patient saved-network retries + timed portal
// windows, never dead-ending). This file is just the buss wrapper: it feeds
// net-core the app's AP name / timeouts and draws the panel status callbacks.

// Fires before each saved-network attempt.
static void onConnecting() { netShowStatus("wifi...", nullptr); }

// Fires when the setup AP comes up — show "join <ssid>" and the portal IP so
// Wi-Fi can be configured from a phone with no serial console.
static void onPortal(const char *ssid, const char *ip) {
  static char line[24];
  snprintf(line, sizeof(line), "join %s", ssid);
  netShowStatus(line, ip);
}

bool netStart() {
  NetConfig cfg;
  cfg.apSsid = AP_SETUP_SSID;
  cfg.connectTimeoutMs = NET_CONNECT_TIMEOUT_MS;
  cfg.portalTimeoutSec = NET_PORTAL_TIMEOUT_SEC;
  cfg.onConnecting = onConnecting;
  cfg.onPortal = onPortal;
  return netStart(cfg);
}
