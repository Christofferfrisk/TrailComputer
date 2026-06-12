// Host-side tests for the firmware's pure math (geo / fusion / sun / route).
// Compiles the real src/*.cpp on the PC — no Arduino, no hardware.
//
//   python -m ziglang c++ -std=c++17 -Isrc src/geo.cpp src/fusion.cpp \
//       src/sun.cpp src/route_util.cpp test/test_math.cpp -o test/test_math.exe
//   test/test_math.exe
//
// Run after any change to the math; a failure here means distances/ETA/daylight
// on the device would be wrong.

#include <cmath>
#include <cstdio>

#include "geo.h"
#include "fusion.h"
#include "sun.h"
#include "route_util.h"
#include "route_table.h"
#include "spur_table.h"

static int g_fail = 0, g_pass = 0;

static void check(const char* name, bool ok) {
    printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    (ok ? g_pass : g_fail)++;
}
static void near(const char* name, float got, float want, float tol) {
    bool ok = std::fabs(got - want) <= tol;
    printf("%s  %s  (got %.2f, want %.2f +/-%.2f)\n",
           ok ? "PASS" : "FAIL", name, got, want, tol);
    (ok ? g_pass : g_fail)++;
}

int main() {
    // ---- geo: haversine / bearing -----------------------------------------
    near("haversine: 1 deg latitude ~111.2 km",
         haversineM(60.0f, 18.0f, 61.0f, 18.0f), 111195.0f, 300.0f);
    near("bearing: due east = 90", bearingDeg(0, 0, 0, 1), 90.0f, 0.5f);
    near("bearing: due north = 0", bearingDeg(0, 0, 1, 0), 0.0f, 0.5f);
    near("bearing: due south = 180", bearingDeg(1, 0, 0, 0), 180.0f, 0.5f);

    // ---- route table invariants -------------------------------------------
    check("route has 294 points", ROUTE_N == 294);
    check("route has 21 huts", ROUTE_HUTS_N == 21);
    check("route starts at Abisko (N->S)",
          hutSlotByName("Abisko") == 0 && ROUTE_HUTS[0] < ROUTE_HUTS[1]);
    near("total route length ~453 km",
         ROUTE[ROUTE_N - 1].cumDistM, 434427.0f, 2000.0f);
    near("total route ascent ~8341 m (full-res)",
         ROUTE[ROUTE_N - 1].cumAscM, 8341.0f, 100.0f);
    check("cumDist is monotonic", [] {
        for (int i = 1; i < ROUTE_N; i++)
            if (ROUTE[i].cumDistM <= ROUTE[i - 1].cumDistM) return false;
        return true;
    }());
    check("cumAsc/cumDesc are monotonic", [] {
        for (int i = 1; i < ROUTE_N; i++)
            if (ROUTE[i].cumAscM < ROUTE[i - 1].cumAscM ||
                ROUTE[i].cumDescM < ROUTE[i - 1].cumDescM) return false;
        return true;
    }());

    // ---- route snap ---------------------------------------------------------
    {   // a point exactly on the route snaps with ~zero lateral error
        int p = 100;
        SnapResult s = routeSnap(ROUTE, ROUTE_N, ROUTE[p].lat, ROUTE[p].lon);
        near("snap on-route point: lateral ~0", s.lateralM, 0.0f, 5.0f);
        float here = ROUTE[s.segStart].cumDistM + s.alongM;
        near("snap on-route point: along-route distance", here, ROUTE[p].cumDistM, 30.0f);

        // windowed snap with a good hint agrees with the global snap
        SnapResult w = routeSnapWindowed(ROUTE, ROUTE_N, ROUTE[p].lat, ROUTE[p].lon, p - 3, 25);
        check("windowed snap matches global", w.segStart == s.segStart);

        // stale hint far away -> global reacquire still finds it
        SnapResult r = routeSnapWindowed(ROUTE, ROUTE_N, ROUTE[p].lat, ROUTE[p].lon, 250, 25);
        near("windowed snap reacquires after lost hint",
             ROUTE[r.segStart].cumDistM + r.alongM, ROUTE[p].cumDistM, 30.0f);
    }
    {   // remaining distance from a snapped position to a hut = cumDist diff
        int p = ROUTE_HUTS[4];                      // at Sälka
        SnapResult s = routeSnap(ROUTE, ROUTE_N, ROUTE[p].lat, ROUTE[p].lon);
        float rem = remainingToM(ROUTE, ROUTE_N, s, ROUTE_HUTS[5]);   // to Singi
        near("remaining Sälka->Singi ~12 km (STF says 12)",
             rem, ROUTE[ROUTE_HUTS[5]].cumDistM - ROUTE[p].cumDistM, 30.0f);
    }

    // ---- route_util ----------------------------------------------------------
    check("hutSlotByName finds Sälka", hutSlotByName("Sälka") == 4);
    check("hutSlotByName rejects unknown", hutSlotByName("Mordor") == -1);
    check("nextHutAfter(Abisko) = Abiskojaure",
          nextHutAfter(ROUTE_HUTS[0]) == ROUTE_HUTS[1]);
    check("nextHutAfter(last hut) = -1",
          nextHutAfter(ROUTE_HUTS[ROUTE_HUTS_N - 1]) == -1);
    check("hutName roundtrip",
          hutName(ROUTE_HUTS[4]) && hutSlotByName(hutName(ROUTE_HUTS[4])) == 4);
    near("naismith: 9 km flat + 600 m climb = 3 h",
         (float)naismithMin(9000.0f, 600.0f), 180.0f, 1.0f);
    near("naismith: 4.5 km flat = 1 h", (float)naismithMin(4500.0f, 0.0f), 60.0f, 1.0f);
    check("remainingAscentM never negative",
          remainingAscentM(ROUTE_HUTS[5], ROUTE_HUTS[1]) == 0.0f);

    // ---- fusion: altitude ----------------------------------------------------
    near("baro altitude: p == QNH -> 0 m",
         baroAltitudeM(1013.25f, 15.0f, 1013.25f), 0.0f, 0.5f);
    near("baro altitude: ~900 hPa -> ~988 m (ISA-ish)",
         baroAltitudeM(900.0f, 15.0f, 1013.25f), 988.0f, 30.0f);
    {   // steering the QNH ref toward GPS altitude converges
        float ref = 1013.25f;
        for (int i = 0; i < 4000; i++) steerSeaLevelRef(ref, 950.0f, 10.0f, 700.0f);
        near("QNH steer converges (alt error -> 0)",
             baroAltitudeM(950.0f, 10.0f, ref), 700.0f, 2.0f);
    }

    // ---- fusion: climb accumulator --------------------------------------------
    {
        float last = 100.0f, asc = 0.0f, desc = 0.0f;
        accumulateClimb(101.0f, last, asc, desc);     // inside 1.5 m deadband
        check("deadband swallows 1.0 m", asc == 0.0f && last == 100.0f);
        accumulateClimb(103.0f, last, asc, desc);
        near("3 m step counts as ascent", asc, 3.0f, 0.01f);
        accumulateClimb(100.0f, last, asc, desc);
        near("drop counts as descent", desc, 3.0f, 0.01f);
    }

    // ---- fusion: pressure trend -----------------------------------------------
    {
        float falling[] = {1013, 1012, 1011, 1010, 1009, 1008, 1007};
        near("trend: steady fall = -6 hPa", pressureTrendHpa(falling, 7), -6.0f, 0.1f);
        float flat[] = {1010, 1010, 1010, 1010, 1010};
        near("trend: flat = 0", pressureTrendHpa(flat, 5), 0.0f, 0.01f);
        float noisy[] = {1010, 1011, 1009, 1010, 1011, 1009, 1010};
        check("trend: noise stays under turn threshold",
              std::fabs(pressureTrendHpa(noisy, 7)) < 1.0f);
        check("trend: too few samples = 0", pressureTrendHpa(falling, 2) == 0.0f);
    }

    // ---- spur table (Nikkaluokta -> Kebnekaise -> Singi) -----------------------
    check("spur has points", SPUR_N >= 20);
    near("spur total ~32.6 km", SPUR_ROUTE[SPUR_N - 1].cumDistM, 32600.0f, 600.0f);
    near("spur ascent ~633 m", SPUR_ROUTE[SPUR_N - 1].cumAscM, 633.0f, 60.0f);
    near("spur starts at Nikkaluokta",
         haversineM(SPUR_ROUTE[0].lat, SPUR_ROUTE[0].lon, 67.8508353f, 19.0137288f),
         0.0f, 300.0f);
    near("spur ends at Singi",
         haversineM(SPUR_ROUTE[SPUR_N - 1].lat, SPUR_ROUTE[SPUR_N - 1].lon,
                    67.8504f, 18.316f), 0.0f, 300.0f);
    near("SPUR_KEB_IDX is at the fjällstation",
         haversineM(SPUR_ROUTE[SPUR_KEB_IDX].lat, SPUR_ROUTE[SPUR_KEB_IDX].lon,
                    67.8671f, 18.6194f), 0.0f, 500.0f);
    check("spur cumDist monotonic", [] {
        for (int i = 1; i < SPUR_N; i++)
            if (SPUR_ROUTE[i].cumDistM <= SPUR_ROUTE[i - 1].cumDistM) return false;
        return true;
    }());
    {   // a point on the spur snaps to the spur far closer than to the main line
        int p = SPUR_KEB_IDX;
        SnapResult ss = routeSnap(SPUR_ROUTE, SPUR_N, SPUR_ROUTE[p].lat, SPUR_ROUTE[p].lon);
        SnapResult sm = routeSnap(ROUTE, ROUTE_N, SPUR_ROUTE[p].lat, SPUR_ROUTE[p].lon);
        check("Kebnekaise point prefers spur over main line",
              ss.lateralM < 10.0f && sm.lateralM > 5000.0f);
    }

    // ---- sun -------------------------------------------------------------------
    check("Abisko midsummer = midnight sun (1439)",
          daylightRemainingMin(68.36f, 18.78f, 2025, 7, 1, 12, 0) == 1439);
    near("Abisko 5 Sep 13:30 UTC -> ~272 min left",
         (float)daylightRemainingMin(68.36f, 18.78f, 2025, 9, 5, 13, 30), 272.0f, 6.0f);
    near("Stockholm equinox sunset ~17:01 UTC",
         (float)daylightRemainingMin(59.33f, 18.07f, 2025, 3, 20, 0, 0), 1021.0f, 10.0f);
    check("after sunset -> 0",
          daylightRemainingMin(59.33f, 18.07f, 2025, 3, 20, 22, 0) == 0);
    check("invalid date -> -1",
          daylightRemainingMin(68.0f, 18.0f, 1999, 13, 1, 12, 0) == -1);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
