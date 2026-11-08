#pragma once

// Settings page at http://sports-display.local/ (or 192.168.4.1 on the
// Sports-Settings hotspot): the upcoming games, brightness, night schedule.
void webStart();
void webHandle();
bool webRefreshRequested();  // true once after "Refresh now" (clears on read)
