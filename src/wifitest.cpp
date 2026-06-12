// Config-portal GUI test. Brings up the SoftAP + web page on a bare board so
// the phone UI can be exercised without button/display/GPS.
//   pio run -e wifitest -t upload && pio device monitor
// Then join Wi-Fi "TrailComputer" and open http://192.168.4.1

#include <Arduino.h>
#include <WiFi.h>
#include "state.h"
#include "config_portal.h"

// Stand-in for the RTC_DATA_ATTR instance main.cpp normally provides.
static PersistentState testState;
PersistentState* g_state = &testState;

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== Config portal GUI test ===");
    stateInitIfNeeded();

    // Seed a fake "last fix" so the Status view shows populated data.
    g_state->lastNav.hasFix = true;
    g_state->lastNav.lat = 67.9000f;  g_state->lastNav.lon = 18.2900f;
    g_state->lastNav.altM = 712;      g_state->lastNav.remainingKm = 8.4f;
    g_state->lastNav.bearingDeg = 65; g_state->lastNav.etaMin = 155;
    g_state->lastNav.climbLeftM = 240; g_state->lastNav.satCount = 9;
    g_state->lastNav.batteryPct = 78;  g_state->lastNav.pressureHpa = 1009;
    g_state->lastNav.destRouteIdx = g_state->nextWaypointIdx;
    strncpy(g_state->lastNav.clock, "13:42", sizeof(g_state->lastNav.clock));
    g_state->cumAscentM = 1234; g_state->cumDescentM = 1180;

    // Seed a few trip days so the history card shows data (mon, day, km*10, ascM).
    g_state->tripDayCount = 3;
    g_state->tripDays[0] = {6, 9,  190, 620};
    g_state->tripDays[1] = {6, 10, 150, 480};
    g_state->tripDays[2] = {6, 11, 130, 540};

    // Seed pressure/temperature/humidity history so the Conditions card draws.
    const float sp[8] = {1012, 1011.6f, 1011, 1010.2f, 1009.5f, 1009, 1008.6f, 1009};
    const float st[8] = {12, 12.5f, 13.2f, 14.1f, 15, 14.4f, 13.6f, 14};
    const float sh[8] = {52, 50, 47, 45, 44, 46, 48, 46};
    g_state->pressHistN = 8;
    for (int i = 0; i < 8; i++) {
        g_state->pressHist[i] = sp[i];
        g_state->tempHist[i]  = st[i];
        g_state->humHist[i]   = sh[i];
    }

    Serial.println("Join Wi-Fi SSID: TrailComputer  (open network)");
    Serial.println("Then browse to: http://192.168.4.1");
    Serial.println("Portal stays up 10 min of inactivity; resets on each click.");

    runConfigPortal(600000UL);

    Serial.println("Portal closed.");
}

void loop() {}
