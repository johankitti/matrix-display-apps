#pragma once

// Settings web UI at http://<HOSTNAME>.local/ — station picker + direction.
// The page runs entirely in the browser (station search + previews hit the SL
// API directly; it sends CORS `*`); the device only serves the page and a tiny
// GET/POST /api/settings JSON endpoint backed by NVS.

void webStart();
void webHandle();
bool webSettingsChanged();  // true once after each saved change (clears on read)
