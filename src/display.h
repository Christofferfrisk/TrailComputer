#pragma once
#include <Arduino.h>

enum ScreenState { SCREEN_NAV, SCREEN_NO_FIX, SCREEN_ARRIVED, SCREEN_CONFIG,
                   SCREEN_LOW_BATT, SCREEN_MAP };

// Everything the renderer needs for one wake, assembled by main.cpp.
struct ViewModel {
    // status bar
    char    clockHHMM[6];      // "13:42" from GPS time, or "--:--"
    int     daylightLeftMin;   // minutes of daylight remaining, -1 if unknown
    int     satCount;
    int     batteryPct;

    // nav body
    char    nextStopName[24];  // UTF-8, Swedish glyphs allowed
    float   remainingKm;
    float   headingDeg;        // device heading (compass), 0..360
    float   bearingToNextDeg;  // great-circle bearing to target
    float   climbLeftM;
    int     etaMin;            // Naismith
    float   altitudeM;

    // trip + power
    float   cumAscentM;
    float   cumDescentM;
    float   remainingMah;
    int     checksLeft;
    bool    lowBattery;
    bool    endOfHike;         // reached the configured hike End

    // weather strip
    float   pressureHpa;       // QNH
    bool    weatherTurning;    // falling
    int     pressureTrend;     // -1 falling, 0 steady, +1 rising
    const float* sparkline;    // pressure samples, oldest..newest
    int     sparklineLen;

    // no-fix / config
    int     satsInView;
    char    apSsid[24];
    char    apUrl[24];

    // map screen: nearby route points as east/north offsets (m) from current
    // position (origin); north is up. Built by main.cpp.
    const float*   mapE;
    const float*   mapNo;
    const uint8_t* mapHut;     // 1 = a hut sits at this point
    int     mapCount;
    float   mapRangeM;         // half-extent (m) the map should span vertically
    float   mapDestE, mapDestN;// offset of the destination (in-view marker / edge arrow)
};

void displayBegin();
void displayRender(ScreenState state, const ViewModel& vm);
void displaySleep();           // hibernate the panel before deep sleep
