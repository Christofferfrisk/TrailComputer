#include <Arduino.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <QMC5883LCompass.h>

#include "config.h"
#include "state.h"
#include "display.h"
#include "geo.h"
#include "fusion.h"
#include "gps.h"
#include "power.h"
#include "sun.h"
#include "config_portal.h"
#include "route_table.h"
#include "route_spurs.h"
#include "spur_table.h"
#include "route_util.h"

// Survives deep sleep; see state.h. Route table is in flash, never here.
RTC_DATA_ATTR static PersistentState rtcState;
PersistentState* g_state = &rtcState;

struct BmeReading { bool ok; float pressHpa; float tempC; float humPct; };

static BmeReading readBme() {
    BmeReading r{};
    Adafruit_BME280 bme;
    if (!bme.begin(BME280_ADDR, &Wire)) return r;   // r.ok stays false
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1,   // temp
                    Adafruit_BME280::SAMPLING_X1,   // pressure
                    Adafruit_BME280::SAMPLING_X1,   // humidity
                    Adafruit_BME280::FILTER_OFF);
    bme.takeForcedMeasurement();
    r.pressHpa = bme.readPressure() / 100.0f;
    r.tempC    = bme.readTemperature() + g_state->settings.bmeTempOffsetC;
    r.humPct   = bme.readHumidity();
    r.ok = true;
    return r;
}

static float readCompassHeading() {
    QMC5883LCompass compass;
    compass.init();          // call with the GPS powered down (less switching noise)
    compass.read();
    float h = compass.getAzimuth();
    return h < 0 ? h + 360.0f : h;
}

// Route index of the first main-line hut of the configured hike. For a main-hut
// start that's the hut itself; for a spur start it's the junction hut where the
// spur rejoins the trail (Singi), so the approach targets it and auto-advances
// onward from there. -1 = no hike start set.
static int hikeStartRouteIdx() {
    int c = g_state->hikeStartCode;
    if (c < 0) return -1;
    if (c < 1000) return ROUTE_HUTS[c];
    int i = (c - 1000) / 10;
    int a = hutSlotByName(SPURS[i].afterHut);
    return (a >= 0) ? ROUTE_HUTS[a] : -1;
}

// Route index of the hike End (main-hut codes only), or -1.
static int hikeEndRouteIdx() {
    int c = g_state->hikeEndCode;
    return (c >= 0 && c < 1000) ? ROUTE_HUTS[c] : -1;
}

static void pushSample(float qnh, float tempC, float humPct) {
    if (g_state->pressHistN >= 12) {
        for (int i = 1; i < 12; i++) {
            g_state->pressHist[i - 1] = g_state->pressHist[i];
            g_state->tempHist[i - 1]  = g_state->tempHist[i];
            g_state->humHist[i - 1]   = g_state->humHist[i];
        }
        g_state->pressHistN = 11;
    }
    int i = g_state->pressHistN++;
    g_state->pressHist[i] = qnh;
    g_state->tempHist[i]  = tempC;
    g_state->humHist[i]   = humPct;
}

#if SIM_MODE
// Fake a fix that walks the route SIM_STEP_M further on each wake, so pressing
// the button repeatedly indoors simulates progress toward the next hut.
static GpsFix simFix() {
    g_state->simDistM += SIM_STEP_M;
    float endCum = ROUTE[ROUTE_N - 1].cumDistM;
    float d = ROUTE[SIM_START_ROUTE_IDX].cumDistM + g_state->simDistM;
    if (d > endCum) d = endCum;

    int i = SIM_START_ROUTE_IDX;
    while (i < ROUTE_N - 1 && ROUTE[i + 1].cumDistM < d) i++;
    float seg = ROUTE[i + 1].cumDistM - ROUTE[i].cumDistM;
    float t = seg > 0 ? (d - ROUTE[i].cumDistM) / seg : 0.0f;

    if (g_state->markedWaypointIdx < 0 && g_state->nextWaypointIdx <= i) {
        int nx = nextHutAfter(i);
        if (nx >= 0) g_state->nextWaypointIdx = nx;
    }

    GpsFix f{};
    f.valid = true; f.sats = 12; f.hdop = 80;
    f.lat = ROUTE[i].lat + t * (ROUTE[i + 1].lat - ROUTE[i].lat);
    f.lon = ROUTE[i].lon + t * (ROUTE[i + 1].lon - ROUTE[i].lon);
    f.altM = ROUTE[i].eleM + t * (ROUTE[i + 1].eleM - ROUTE[i].eleM);
    f.courseDeg = bearingDeg(ROUTE[i].lat, ROUTE[i].lon, ROUTE[i + 1].lat, ROUTE[i + 1].lon);
    f.speedMps = 1.2f;
    f.hh = 12; f.mm = 0; f.year = 2025; f.month = 8; f.day = 1;
    return f;
}
#endif

static bool buttonHeldLong() {
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    uint32_t t0 = millis();
    while (digitalRead(PIN_BUTTON) == LOW) {
        if (millis() - t0 >= LONG_PRESS_MS) return true;
        delay(10);
    }
    return false;
}

static void finishCycle(uint32_t cpuMsSoFar, uint32_t gpsMs) {
    uint32_t cpuMs = (millis() - cpuMsSoFar);
    g_state->consumedMah += accountCycleMah(cpuMs, gpsMs, EPD_REFRESH_MS);
    statePersist();                       // survive a battery swap

    // Pin the GPS MOSFET gate OFF through sleep; non-RTC GPIOs otherwise float.
    gpsPower(false);
    gpio_hold_en((gpio_num_t)PIN_GPS_PWR);
    gpio_deep_sleep_hold_en();

    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BUTTON, 0);   // button to GND
#if WAKE_TIMER_S > 0
    esp_sleep_enable_timer_wakeup((uint64_t)WAKE_TIMER_S * 1000000ULL);
#endif
    displaySleep();
    esp_deep_sleep_start();
}

// Project a window of the active polyline around the snapped position into
// east/north metre offsets (current position = origin) for the map screen.
static const int MAP_WIN = 80;
static float   g_mapE[MAP_WIN], g_mapN[MAP_WIN];
static uint8_t g_mapHut[MAP_WIN];

static void buildMap(ViewModel& vm, const RoutePoint* poly, int polyN, bool mainRoute,
                     int segStart, float lat0, float lon0, int destIdx) {
    const float R = 6371000.0f, D2R = 0.01745329f;
    float k = cosf(lat0 * D2R);
    int lo = max(0, segStart - 25), hi = min(polyN - 1, segStart + 50);
    int n = 0;
    for (int i = lo; i <= hi && n < MAP_WIN; i++, n++) {
        g_mapE[n] = (poly[i].lon - lon0) * D2R * R * k;
        g_mapN[n] = (poly[i].lat - lat0) * D2R * R;
        g_mapHut[n] = (mainRoute ? (hutSlot(i) >= 0) : (i == SPUR_KEB_IDX)) ? 1 : 0;
    }
    vm.mapE = g_mapE; vm.mapNo = g_mapN; vm.mapHut = g_mapHut; vm.mapCount = n;

    if (destIdx >= 0 && destIdx < polyN) {
        vm.mapDestE = (poly[destIdx].lon - lon0) * D2R * R * k;
        vm.mapDestN = (poly[destIdx].lat - lat0) * D2R * R;
    } else { vm.mapDestE = vm.mapDestN = 0; }

    float dist = sqrtf(vm.mapDestE * vm.mapDestE + vm.mapDestN * vm.mapDestN) * 1.15f;
    vm.mapRangeM = dist < 700 ? 700 : (dist > 4000 ? 4000 : dist);
}

// Full sensor/GPS/route pipeline. Fills vm and the g_state->lastNav snapshot;
// returns the screen to show and, via gpsMs, the GPS-on time for accounting.
static ScreenState computeNavCycle(ViewModel& vm, uint32_t& gpsMs) {
    vm.lowBattery   = vm.batteryPct <= BAT_LOW_PCT;
    vm.remainingMah = remainingMah(g_state->consumedMah);
    vm.checksLeft   = estimatedChecksLeft(g_state->consumedMah, g_state->bootCount);
    bool critical   = vm.batteryPct <= BAT_CRIT_PCT;

    BmeReading bme = readBme();

#if SIM_MODE
    GpsFix fix = simFix();
    if (!bme.ok) {                               // synthesize weather if no BME wired
        bme.ok = true; bme.tempC = 8.0f; bme.humPct = 60.0f;
        bme.pressHpa = SIM_PRESSURE_HPA - (g_state->bootCount % 12);
    }
#else
    GpsFix fix{};
    if (!critical) fix = gpsAcquireFix(GPS_FIX_TIMEOUT_MS, GPS_HDOP_GATE);  // skip GPS to save power
#endif

    float heading = readCompassHeading();        // GPS now powered down

    // Needle reference: course-over-ground when moving, else the magnetometer.
    float referenceDeg = (fix.valid && fix.speedMps > MOVING_SPEED_MPS)
                         ? fix.courseDeg : heading;

    float prevAlt = g_state->lastAltitudeM;
    float altM = prevAlt;
    bool weatherTurning = false;

    if (bme.ok) {
        if (fix.valid && fix.hdop <= GPS_HDOP_GATE)
            steerSeaLevelRef(g_state->seaLevelPressureRef, bme.pressHpa, bme.tempC, fix.altM);

        altM = baroAltitudeM(bme.pressHpa, bme.tempC, g_state->seaLevelPressureRef);

        if (g_state->lastAltitudeM < -9.0e4f)        // first altitude after a cold boot
            g_state->lastAltitudeM = altM;           // seed without counting a phantom climb
        else
            accumulateClimb(altM, g_state->lastAltitudeM,
                            g_state->cumAscentM, g_state->cumDescentM);

        pushSample(g_state->seaLevelPressureRef, bme.tempC, bme.humPct);
    }

    // Weather from the rolling QNH window, not a single step.
    float trend = pressureTrendHpa(g_state->pressHist, g_state->pressHistN);
    weatherTurning = trend < -WEATHER_WINDOW_DROP_HPA;

    ScreenState screen = SCREEN_NO_FIX;
    vm.satCount   = fix.sats;
    vm.satsInView = fix.sats;
    vm.pressureHpa     = g_state->seaLevelPressureRef;
    vm.temperatureC    = bme.tempC;
    vm.humidityPct     = bme.humPct;
    vm.weatherTurning  = weatherTurning;
    vm.pressureTrend   = trend < -0.3f ? -1 : (trend > 0.3f ? 1 : 0);
    vm.cumAscentM      = g_state->cumAscentM;
    vm.cumDescentM     = g_state->cumDescentM;
    vm.sparkline       = g_state->pressHist;
    vm.sparklineLen    = g_state->pressHistN;

    if (fix.valid) {
        snprintf(vm.clockHHMM, sizeof(vm.clockHHMM), "%02d:%02d", fix.hh, fix.mm);
        vm.daylightLeftMin = daylightRemainingMin(fix.lat, fix.lon,
                                                  fix.year, fix.month, fix.day,
                                                  fix.hh, fix.mm);

        int hikeStart = hikeStartRouteIdx();
        int hikeEnd   = hikeEndRouteIdx();

        // Keep the auto-advance target inside the configured hike section.
        if (hikeStart >= 0 && g_state->nextWaypointIdx < hikeStart)
            g_state->nextWaypointIdx = hikeStart;
        if (hikeEnd >= 0 && g_state->nextWaypointIdx > hikeEnd)
            g_state->nextWaypointIdx = hikeEnd;

        int dest = (g_state->markedWaypointIdx >= 0)
                   ? g_state->markedWaypointIdx : g_state->nextWaypointIdx;
        if (hikeEnd >= 0 && dest > hikeEnd) {       // never target past the End
            dest = hikeEnd;
            g_state->nextWaypointIdx = hikeEnd;
            g_state->markedWaypointIdx = -1;
        }

        // Snap to both the main route and the Kebnekaise spur; navigate on
        // whichever trail we're actually closer to.
        SnapResult sm = routeSnapWindowed(ROUTE, ROUTE_N, fix.lat, fix.lon,
                                          g_state->lastSegStart, SNAP_WINDOW_SEGS);
        SnapResult sp = routeSnap(SPUR_ROUTE, SPUR_N, fix.lat, fix.lon);
        bool onSpur = sp.lateralM < sm.lateralM;

        if (onSpur) {
            float here    = SPUR_ROUTE[sp.segStart].cumDistM + sp.alongM;
            float hereAsc = SPUR_ROUTE[sp.segStart].cumAscM;
            if (!g_state->wasOnSpur) {               // domain change: no phantom delta
                g_state->tripLastAlongM = here;
                g_state->tripLastAscM   = hereAsc;
            }
            g_state->wasOnSpur = true;
            tripLog(fix.month, fix.day, here, hereAsc);

            int junction = ROUTE_HUTS[hutSlotByName("Singi")];
            float kebCum = SPUR_ROUTE[SPUR_KEB_IDX].cumDistM;
            bool beforeKeb = here < kebCum - WAYPOINT_ARRIVE_M;

            float remM, climb;
            if (beforeKeb) {                         // first leg: walk to the station
                strncpy(vm.nextStopName, "Kebnekaise", sizeof(vm.nextStopName));
                remM  = kebCum - here;
                climb = SPUR_ROUTE[SPUR_KEB_IDX].cumAscM - hereAsc;
            } else {                                 // onward: Singi (or marked target)
                int d2 = dest > junction ? dest : junction;
                strncpy(vm.nextStopName, hutName(d2), sizeof(vm.nextStopName));
                remM  = (SPUR_ROUTE[SPUR_N - 1].cumDistM - here)
                      + (ROUTE[d2].cumDistM - ROUTE[junction].cumDistM);
                climb = (SPUR_ROUTE[SPUR_N - 1].cumAscM - hereAsc)
                      + remainingAscentM(junction, d2);
            }

            if (remM <= WAYPOINT_ARRIVE_M) {
                screen = SCREEN_ARRIVED;             // at Kebnekaise (or the junction)
            } else {
                screen = SCREEN_NAV;
                int ahead = min(sp.segStart + 1, SPUR_N - 1);
                vm.remainingKm      = remM / 1000.0f;
                vm.headingDeg       = referenceDeg;
                vm.bearingToNextDeg = bearingDeg(fix.lat, fix.lon,
                                                 SPUR_ROUTE[ahead].lat, SPUR_ROUTE[ahead].lon);
                vm.climbLeftM = climb;
                vm.etaMin     = naismithMin(remM, climb);
                vm.altitudeM  = altM;
                buildMap(vm, SPUR_ROUTE, SPUR_N, false, sp.segStart, fix.lat, fix.lon,
                         beforeKeb ? SPUR_KEB_IDX : SPUR_N - 1);
            }
        } else {
            if (g_state->wasOnSpur) {                // domain change: no phantom delta
                g_state->tripLastAlongM = ROUTE[sm.segStart].cumDistM + sm.alongM;
                g_state->tripLastAscM   = ROUTE[sm.segStart].cumAscM;
            }
            g_state->wasOnSpur = false;
            g_state->lastSegStart = sm.segStart;
            tripLog(fix.month, fix.day,
                    ROUTE[sm.segStart].cumDistM + sm.alongM, ROUTE[sm.segStart].cumAscM);
            float remM = remainingToM(ROUTE, ROUTE_N, sm, dest);

            strncpy(vm.nextStopName, hutName(dest), sizeof(vm.nextStopName));

            if (remM <= WAYPOINT_ARRIVE_M) {
                screen = SCREEN_ARRIVED;
                bool atEnd = (hikeEnd >= 0 && dest >= hikeEnd);
                vm.endOfHike = atEnd;
                if (!atEnd) {                        // advance, but not past the End
                    int nx = nextHutAfter(dest);
                    if (hikeEnd >= 0 && nx > hikeEnd) nx = hikeEnd;
                    if (nx >= 0) g_state->nextWaypointIdx = nx;
                    g_state->markedWaypointIdx = -1;
                }
            } else {
                screen = SCREEN_NAV;
                int aheadIdx = min(sm.segStart + 1, ROUTE_N - 1);
                float climb = remainingAscentM(sm.segStart, dest);

                vm.remainingKm      = remM / 1000.0f;
                vm.headingDeg       = referenceDeg;
                vm.bearingToNextDeg = bearingDeg(fix.lat, fix.lon,
                                                 ROUTE[aheadIdx].lat, ROUTE[aheadIdx].lon);
                vm.climbLeftM = climb;
                vm.etaMin     = naismithMin(remM, climb);
                vm.altitudeM  = altM;
                buildMap(vm, ROUTE, ROUTE_N, true, sm.segStart, fix.lat, fix.lon, dest);
            }
        }

        // The map is the same nav cycle, just a different presentation.
        if (screen == SCREEN_NAV && g_state->settings.mapMode) screen = SCREEN_MAP;
    }

    NavSnapshot& snap = g_state->lastNav;
    snap.hasFix        = fix.valid;
    snap.satCount      = fix.sats;
    snap.batteryPct    = vm.batteryPct;
    snap.pressureHpa   = vm.pressureHpa;
    snap.weatherTurning = vm.weatherTurning;
    strncpy(snap.clock, vm.clockHHMM, sizeof(snap.clock));
    if (fix.valid) {
        snap.lat = fix.lat; snap.lon = fix.lon; snap.altM = altM;
        snap.remainingKm  = vm.remainingKm;
        snap.bearingDeg   = vm.bearingToNextDeg;
        snap.etaMin       = vm.etaMin;
        snap.climbLeftM   = vm.climbLeftM;
        snap.destRouteIdx = (g_state->markedWaypointIdx >= 0)
                            ? g_state->markedWaypointIdx : g_state->nextWaypointIdx;
    }

    gpsMs = fix.onTimeMs;
    return critical ? SCREEN_LOW_BATT : screen;
}

void setup() {
    uint32_t tBoot = millis();
    Serial.begin(115200);
    neopixelWrite(PIN_RGB_LED, 0, 0, 0);
    stateInitIfNeeded();
    g_state->bootCount++;

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    displayBegin();

    ViewModel vm{};
    vm.daylightLeftMin = -1;
    vm.batteryPct = batteryPctFromVoltage(readBatteryVolts());
    strncpy(vm.clockHHMM, "--:--", sizeof(vm.clockHHMM));

    bool configMode = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0)
                      && buttonHeldLong();

    uint32_t gpsMs = 0;
    if (configMode) {
        strncpy(vm.apSsid, "TrailComputer", sizeof(vm.apSsid));
        strncpy(vm.apUrl,  "192.168.4.1",   sizeof(vm.apUrl));
        displayRender(SCREEN_CONFIG, vm);        // immediate feedback while we read sensors
        computeNavCycle(vm, gpsMs);              // fresh GPS/sensors -> g_state->lastNav
        runConfigPortal(CONFIG_PORTAL_TIMEOUT_MS);
    } else {
        ScreenState screen = computeNavCycle(vm, gpsMs);
        displayRender(screen, vm);
    }
    finishCycle(tBoot, gpsMs);
}

void loop() {}
