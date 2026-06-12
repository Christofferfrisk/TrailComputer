#pragma once
#include <stdint.h>

struct RoutePoint {
    float lat;
    float lon;
    float cumDistM;   // filled by routeComputeCumDist()
    float eleM;
    float cumAscM;    // cumulative ascent from route start (full-res, dead-banded)
    float cumDescM;   // cumulative descent from route start
};

// Great-circle distance (m) and initial bearing (deg, 0..360) A->B.
float haversineM(float lat1, float lon1, float lat2, float lon2);
float bearingDeg(float lat1, float lon1, float lat2, float lon2);

// One-time pass: fill cumDistM along an ordered polyline.
void routeComputeCumDist(RoutePoint* pts, int n);

// Snap (lat,lon) onto the route; returns the segment start index and the
// distance progressed along that segment (m). Local planar, cos-lat corrected.
struct SnapResult {
    int   segStart;
    float alongM;
    float lateralM;   // perpendicular offset from the route
};
SnapResult routeSnap(const RoutePoint* pts, int n, float lat, float lon);

// Snap searching only segments within `window` of `hintSeg` (the last known
// segment). With hintSeg < 0 this is a full search. Cheaper, and avoids
// snapping to a far part of the trail that happens to pass nearby.
SnapResult routeSnapWindowed(const RoutePoint* pts, int n, float lat, float lon,
                             int hintSeg, int window);

// Remaining distance from a snapped position to point destIdx.
float remainingToM(const RoutePoint* pts, int n, const SnapResult& s, int destIdx);
