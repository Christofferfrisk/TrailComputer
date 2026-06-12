#include "state.h"
#include "config.h"
#include "route_table.h"
#include <Preferences.h>
#include <string.h>

static const char* NVS_NS = "trail";

void stateInitIfNeeded() {
    if (g_state->magic == STATE_MAGIC) return;

    // Cold boot, corrupt RTC, or a firmware whose struct layout changed: reset
    // to defaults, then recover the durable counters from NVS if present.
    memset(g_state, 0, sizeof(PersistentState));
    g_state->seaLevelPressureRef = 1013.25f;
    g_state->markedWaypointIdx = -1;
    g_state->hikeStartCode = -1;
    g_state->hikeEndCode = -1;
    g_state->lastSegStart = -1;
    g_state->lastAltitudeM = -1e5f;    // sentinel: seed (don't count) the first altitude
    g_state->nextWaypointIdx = ROUTE_HUTS[0];
    strncpy(g_state->lastNav.clock, "--:--", sizeof(g_state->lastNav.clock));

    // isKey() guards keep Preferences from logging NOT_FOUND on first boot.
    Preferences p;
    p.begin(NVS_NS, true);
    auto gi = [&](const char* k, int32_t d)  { return p.isKey(k) ? p.getInt(k, d)   : d; };
    auto gf = [&](const char* k, float d)    { return p.isKey(k) ? p.getFloat(k, d) : d; };
    auto gu = [&](const char* k, uint8_t d)  { return p.isKey(k) ? p.getUChar(k, d) : d; };

    g_state->nextWaypointIdx   = gi("next",   ROUTE_HUTS[0]);
    g_state->markedWaypointIdx = gi("marked", -1);
    g_state->hikeStartCode     = gi("hs", -1);
    g_state->hikeEndCode       = gi("he", -1);
    g_state->consumedMah       = gf("mah",  0.0f);
    g_state->cumAscentM        = gf("asc",  0.0f);
    g_state->cumDescentM       = gf("desc", 0.0f);
    g_state->seaLevelPressureRef = gf("qnh", 1013.25f);

    g_state->settings.batCapacityMah = gf("cap",  BAT_CAPACITY_MAH);
    g_state->settings.batUsableFrac  = gf("frac", BAT_USABLE_FRACTION);
    g_state->settings.bmeTempOffsetC = gf("toff", BME_TEMP_OFFSET_C);
    g_state->settings.dayTargetH     = gi("dayh", 6);
    g_state->settings.mapMode        = gi("map", 0);

    g_state->tripDayCount   = gu("tdc", 0);
    g_state->tripLastAlongM = gf("tla", 0.0f);
    g_state->tripLastAscM   = gf("tasc", 0.0f);
    g_state->tripLastMon = gu("tmon", 0);
    g_state->tripLastDay = gu("tday", 0);
    if (p.isKey("tdays"))
        p.getBytes("tdays", g_state->tripDays, sizeof(g_state->tripDays));
    p.end();

    g_state->magic = STATE_MAGIC;
}

void statePersist() {
    Preferences p;
    p.begin(NVS_NS, false);
    p.putInt("next",   g_state->nextWaypointIdx);
    p.putInt("marked", g_state->markedWaypointIdx);
    p.putInt("hs",     g_state->hikeStartCode);
    p.putInt("he",     g_state->hikeEndCode);
    p.putFloat("mah",  g_state->consumedMah);
    p.putFloat("asc",  g_state->cumAscentM);
    p.putFloat("desc", g_state->cumDescentM);
    p.putFloat("qnh",  g_state->seaLevelPressureRef);

    p.putFloat("cap",  g_state->settings.batCapacityMah);
    p.putFloat("frac", g_state->settings.batUsableFrac);
    p.putFloat("toff", g_state->settings.bmeTempOffsetC);
    p.putInt("dayh",   g_state->settings.dayTargetH);
    p.putInt("map",    g_state->settings.mapMode);

    p.putUChar("tdc",  g_state->tripDayCount);
    p.putFloat("tla",  g_state->tripLastAlongM);
    p.putFloat("tasc", g_state->tripLastAscM);
    p.putUChar("tmon", g_state->tripLastMon);
    p.putUChar("tday", g_state->tripLastDay);
    p.putBytes("tdays", g_state->tripDays, sizeof(g_state->tripDays));
    p.end();
}

void tripLog(int mon, int day, float alongM, float ascM) {
    bool newDay = (g_state->tripDayCount == 0) ||
                  (mon != g_state->tripLastMon) || (day != g_state->tripLastDay);
    if (newDay) {
        if (g_state->tripDayCount < 14) {
            DayLog& d = g_state->tripDays[g_state->tripDayCount++];
            d.mon = mon; d.day = day; d.km10 = 0; d.ascM = 0;
        }
        g_state->tripLastAlongM = alongM;     // no cross-day delta
        g_state->tripLastAscM   = ascM;
    } else if (g_state->tripDayCount > 0) {
        DayLog& d = g_state->tripDays[g_state->tripDayCount - 1];
        float dkm = (alongM - g_state->tripLastAlongM) / 1000.0f;
        float dasc = ascM - g_state->tripLastAscM;
        if (dkm > 0)  d.km10 += (uint16_t)(dkm * 10.0f + 0.5f);
        if (dasc > 0) d.ascM += (uint16_t)(dasc + 0.5f);
        g_state->tripLastAlongM = alongM;
        g_state->tripLastAscM   = ascM;
    }
    g_state->tripLastMon = mon;
    g_state->tripLastDay = day;
}
