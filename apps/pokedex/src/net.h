#pragma once
#include <Arduino.h>
#include <net_core.h>   // netEnsure/httpGetBinary/netFree + NetConfig-driven netStart

// Bring up Wi-Fi. Credentials are provisioned via the on-device captive portal
// (WiFiManager) and stored in NVS by the ESP core — no secrets.h needed. Tries
// saved creds; opens the "Pokedex-Setup" AP only if there are none / can't join.
// Returns true once connected as a station.
//
// Thin wrapper over net-core's netStart(const NetConfig&): fills in the pokedex's
// AP name / timeouts and its connecting / setup display callbacks. netEnsure(),
// httpGetBinary() and netFree() come straight from net-core (net_core.h above).
bool netStart();
