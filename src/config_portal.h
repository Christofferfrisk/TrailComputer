#pragma once
#include <stdint.h>

// Bring up a WiFi SoftAP and serve an offline page listing the route's
// destinations (Start day / Set target / Next / Previous). Blocks for up to
// timeoutMs of inactivity, persists the choice to NVS, then tears the radio
// down. Radio is on-demand only; never call this in a normal wake cycle.
void runConfigPortal(uint32_t timeoutMs);
