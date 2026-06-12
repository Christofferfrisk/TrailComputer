#pragma once
#include <stdint.h>

struct GpsFix {
    bool   valid;
    float  lat;
    float  lon;
    float  altM;
    float  courseDeg;   // course over ground (direction of travel)
    float  speedMps;    // ground speed
    int    sats;
    int    hdop;        // value*100
    int    hh, mm;      // UTC time of fix
    int    year, month, day;  // UTC date of fix (for daylight calc)
    uint32_t onTimeMs;  // GPS powered duration this cycle, for energy accounting
};

// Power the module via the MOSFET, wait for a fix passing the HDOP gate or
// until timeout, then power it down (VBCKP keeps the warm start alive).
GpsFix gpsAcquireFix(uint32_t timeoutMs, int hdopGate);

void gpsPower(bool on);
