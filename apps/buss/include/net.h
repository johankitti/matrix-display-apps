#pragma once

// Wi-Fi bring-up (WiFiManager pattern, same as the pokedex/golf builds):
// credentials come from NVS, provisioned once via a captive portal. Blocks
// until connected; can't dead-end (alternates saved-network retries with
// portal windows forever).
bool netStart();

// Implemented by main.cpp: show connection progress on the panel.
// line2 may be nullptr.
void netShowStatus(const char *line1, const char *line2);
