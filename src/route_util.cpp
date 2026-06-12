#include "route_util.h"
#include "config.h"
#include "route_table.h"
#include <string.h>

int nextHutAfter(int routeIdx) {
    for (int k = 0; k < ROUTE_HUTS_N; k++)
        if (ROUTE_HUTS[k] > routeIdx) return ROUTE_HUTS[k];
    return -1;
}

const char* hutName(int routeIdx) {
    for (int k = 0; k < ROUTE_HUTS_N; k++)
        if (ROUTE_HUTS[k] == routeIdx) return ROUTE_HUT_NAMES[k];
    return "";
}

int hutSlot(int routeIdx) {
    for (int k = 0; k < ROUTE_HUTS_N; k++)
        if (ROUTE_HUTS[k] == routeIdx) return k;
    return -1;
}

int hutSlotByName(const char* name) {
    for (int k = 0; k < ROUTE_HUTS_N; k++)
        if (strcmp(ROUTE_HUT_NAMES[k], name) == 0) return k;
    return -1;
}

float remainingAscentM(int fromIdx, int toIdx) {
    if (fromIdx < 0 || toIdx < 0 || fromIdx >= ROUTE_N || toIdx >= ROUTE_N) return 0.0f;
    float d = ROUTE[toIdx].cumAscM - ROUTE[fromIdx].cumAscM;   // full-res, dead-banded
    return d > 0 ? d : 0.0f;
}

int naismithMin(float remainingM, float climbM) {
    float hours = (remainingM / 1000.0f) / NAISMITH_SPEED_KMH + climbM / 600.0f;
    return (int)(hours * 60.0f);
}
