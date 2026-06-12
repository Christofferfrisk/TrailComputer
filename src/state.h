#pragma once
#include <stdint.h>

// Last nav cycle's result, so the config portal (radio-only, no GPS) can show a
// status view. Updated each normal wake.
struct NavSnapshot {
    bool   hasFix;
    float  lat, lon, altM;
    float  remainingKm;
    float  bearingDeg;
    int    etaMin;
    float  climbLeftM;
    float  pressureHpa;
    bool   weatherTurning;
    int    satCount;
    int    batteryPct;
    int    destRouteIdx;
    char   clock[6];
};

// User-editable settings (config portal), initialised from config.h defaults.
struct Settings {
    float batCapacityMah;
    float batUsableFrac;
    float bmeTempOffsetC;
    int   dayTargetH;          // day-planner target walking hours/day
    int   mapMode;             // 0 = compass nav screen, 1 = map screen
};

// One trip day's tally.
struct DayLog {
    uint8_t  mon, day;
    uint16_t km10;             // distance walked that day, km*10
    uint16_t ascM;             // ascent that day, m
};

// Persistent across deep sleep. Kept small: RTC slow memory is ~8 KB and the
// route table lives in flash, never here.
struct PersistentState {
    uint32_t bootCount;
    int32_t  nextWaypointIdx;     // index into the route polyline
    int32_t  markedWaypointIdx;   // user-marked target (config portal)
    int32_t  hikeStartCode;       // this-hike section start (stop code, -1 = full route)
    int32_t  hikeEndCode;         // this-hike section end (stop code, -1 = full route)

    float    seaLevelPressureRef; // QNH reference, hPa, GPS-steered
    float    lastAltitudeM;
    float    cumAscentM;
    float    cumDescentM;

    float    consumedMah;

    float    pressHist[12];        // rolling QNH samples, oldest..newest
    float    tempHist[12];         // rolling temperature samples (deg C), same index as pressHist
    float    humHist[12];          // rolling humidity samples (%RH), same index as pressHist
    uint8_t  pressHistN;

    int32_t  lastSegStart;         // route snap hint from last fix (-1 = unknown)
    bool     wasOnSpur;            // last fix snapped to the spur (trip-log domain)

    float    simDistM;             // SIM_MODE: distance walked past SIM_START_ROUTE_IDX

    Settings settings;

    // Trip history: per-day distance/ascent, accumulated on the trail.
    DayLog   tripDays[14];
    uint8_t  tripDayCount;
    float    tripLastAlongM;       // along-route distance at last fix
    float    tripLastAscM;         // cumulative ascent at last fix
    uint8_t  tripLastMon, tripLastDay;

    NavSnapshot lastNav;

    uint32_t magic;                // == STATE_MAGIC once initialized (see config.h)
};

extern PersistentState* g_state;   // points at the RTC_DATA_ATTR instance

void stateInitIfNeeded();          // first boot defaults (+ NVS recovery)
void statePersist();               // mirror durable counters to NVS (survives battery swap)
void tripLog(int mon, int day, float alongM, float ascM);  // accumulate today's distance/ascent
