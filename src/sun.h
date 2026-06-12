#pragma once

// Minutes of daylight remaining at the given UTC moment and position.
// Returns 1439 under midnight sun, 0 if the sun is already down / never rises,
// and -1 if the inputs are not a valid date/time.
int daylightRemainingMin(float lat, float lon,
                         int year, int month, int day, int hh, int mm);
