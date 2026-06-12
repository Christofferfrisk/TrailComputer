#include "geo.h"
#include "config.h"
#include <math.h>

static const float R_EARTH_M = 6371000.0f;
static const float DEG2RAD   = 0.01745329251f;

float haversineM(float lat1, float lon1, float lat2, float lon2) {
    float dLat = (lat2 - lat1) * DEG2RAD;
    float dLon = (lon2 - lon1) * DEG2RAD;
    float a = sinf(dLat / 2) * sinf(dLat / 2) +
              cosf(lat1 * DEG2RAD) * cosf(lat2 * DEG2RAD) *
              sinf(dLon / 2) * sinf(dLon / 2);
    return 2.0f * R_EARTH_M * atan2f(sqrtf(a), sqrtf(1.0f - a));
}

float bearingDeg(float lat1, float lon1, float lat2, float lon2) {
    float y = sinf((lon2 - lon1) * DEG2RAD) * cosf(lat2 * DEG2RAD);
    float x = cosf(lat1 * DEG2RAD) * sinf(lat2 * DEG2RAD) -
              sinf(lat1 * DEG2RAD) * cosf(lat2 * DEG2RAD) *
              cosf((lon2 - lon1) * DEG2RAD);
    float b = atan2f(y, x) / DEG2RAD;
    return fmodf(b + 360.0f, 360.0f);
}

void routeComputeCumDist(RoutePoint* pts, int n) {
    if (n <= 0) return;
    pts[0].cumDistM = 0.0f;
    for (int i = 1; i < n; i++) {
        pts[i].cumDistM = pts[i - 1].cumDistM +
            haversineM(pts[i - 1].lat, pts[i - 1].lon, pts[i].lat, pts[i].lon);
    }
}

// Local equirectangular projection around a reference latitude (metres).
static void toLocal(float refLat, float lat, float lon, float lon0, float& x, float& y) {
    float k = cosf(refLat * DEG2RAD);
    x = (lon - lon0) * DEG2RAD * R_EARTH_M * k;
    y = (lat - refLat) * DEG2RAD * R_EARTH_M;
}

static SnapResult snapRange(const RoutePoint* pts, int lo, int hi, float lat, float lon) {
    SnapResult best{lo, 0.0f, 1e9f};
    for (int i = lo; i < hi; i++) {
        float refLat = pts[i].lat, lon0 = pts[i].lon;
        float ax, ay, bx, by, px, py;
        toLocal(refLat, pts[i].lat,     pts[i].lon,     lon0, ax, ay);
        toLocal(refLat, pts[i + 1].lat, pts[i + 1].lon, lon0, bx, by);
        toLocal(refLat, lat,            lon,            lon0, px, py);

        float vx = bx - ax, vy = by - ay;
        float segLen2 = vx * vx + vy * vy;
        float t = segLen2 > 0 ? ((px - ax) * vx + (py - ay) * vy) / segLen2 : 0.0f;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);

        float qx = ax + t * vx, qy = ay + t * vy;
        float dx = px - qx, dy = py - qy;
        float lateral = sqrtf(dx * dx + dy * dy);

        if (lateral < best.lateralM) {
            best.segStart  = i;
            best.alongM    = t * sqrtf(segLen2);
            best.lateralM  = lateral;
        }
    }
    return best;
}

SnapResult routeSnap(const RoutePoint* pts, int n, float lat, float lon) {
    if (n < 2) return SnapResult{0, 0.0f, 1e9f};
    return snapRange(pts, 0, n - 1, lat, lon);
}

SnapResult routeSnapWindowed(const RoutePoint* pts, int n, float lat, float lon,
                             int hintSeg, int window) {
    if (n < 2) return SnapResult{0, 0.0f, 1e9f};
    if (hintSeg < 0) return snapRange(pts, 0, n - 1, lat, lon);

    int lo = hintSeg - window; if (lo < 0) lo = 0;
    int hi = hintSeg + window; if (hi > n - 1) hi = n - 1;
    SnapResult s = snapRange(pts, lo, hi, lat, lon);

    // Lost the trail (big detour, or hint was stale) -> reacquire globally.
    if (s.lateralM > SNAP_REACQUIRE_M) s = snapRange(pts, 0, n - 1, lat, lon);
    return s;
}

float remainingToM(const RoutePoint* pts, int n, const SnapResult& s, int destIdx) {
    if (destIdx < 0 || destIdx >= n) return 0.0f;
    float here = pts[s.segStart].cumDistM + s.alongM;
    return pts[destIdx].cumDistM - here;
}
