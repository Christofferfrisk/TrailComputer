#pragma once

// Helpers over the flash route table (route_table.h), shared by the wake cycle
// and the config portal.

int   nextHutAfter(int routeIdx);     // next end-of-day hut strictly past routeIdx, or -1
const char* hutName(int routeIdx);    // name of the hut at routeIdx, or ""
int   hutSlot(int routeIdx);          // index into ROUTE_HUTS for routeIdx, or -1
int   hutSlotByName(const char* name);// index into ROUTE_HUTS for a hut name, or -1
float remainingAscentM(int fromIdx, int toIdx);   // summed positive climb along route
int   naismithMin(float remainingM, float climbM); // ETA: distance + 1h per 600 m up
