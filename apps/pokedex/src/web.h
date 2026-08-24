#pragma once
#include <Arduino.h>

// Start the settings web server + mDNS responder (http://<HOSTNAME>.local/).
// Call once after Wi-Fi is connected.
void webInit();

// Service pending HTTP requests. Call every main-loop iteration (non-blocking).
void webTick();
