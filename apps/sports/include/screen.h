#pragma once
#include <time.h>

// Draw all team blocks for wall-clock `now` (UTC epoch). clockOk=false while
// the clock hasn't been set yet (countdowns show "..." instead of nonsense).
void screenRender(time_t now, bool clockOk);

// Full-screen status (Wi-Fi bring-up etc). line2 may be nullptr.
void screenStatus(const char* line1, const char* line2);
