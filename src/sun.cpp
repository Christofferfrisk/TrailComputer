#include "sun.h"
#include <math.h>

// Sunset in UTC via the "Almanac for Computers" / NOAA hour-angle method.
// All trig in degrees. Returns minutes until sunset; see header for sentinels.

static const float ZENITH = 90.833f;   // official sunset, includes refraction
static const float D2R = 0.01745329f;
static const float R2D = 57.2957795f;

static float norm360(float x) {
    x = fmodf(x, 360.0f);
    return x < 0 ? x + 360.0f : x;
}

int daylightRemainingMin(float lat, float lon,
                         int year, int month, int day, int hh, int mm) {
    if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31) return -1;

    int n1 = (int)floorf(275.0f * month / 9.0f);
    int n2 = (int)floorf((month + 9) / 12.0f);
    int n3 = (int)(1 + floorf((year - 4 * (int)floorf(year / 4.0f) + 2) / 3.0f));
    int N = n1 - (n2 * n3) + day - 30;   // day of year

    float lngHour = lon / 15.0f;
    float t = N + ((18.0f - lngHour) / 24.0f);   // 18 = rising-set hint for sunset

    float M = (0.9856f * t) - 3.289f;
    float L = norm360(M + (1.916f * sinf(M * D2R)) + (0.020f * sinf(2 * M * D2R)) + 282.634f);

    float RA = norm360(R2D * atanf(0.91764f * tanf(L * D2R)));
    // align RA quadrant with L, then to hours
    RA += (floorf(L / 90.0f) * 90.0f) - (floorf(RA / 90.0f) * 90.0f);
    RA /= 15.0f;

    float sinDec = 0.39782f * sinf(L * D2R);
    float cosDec = cosf(asinf(sinDec));

    float cosH = (cosf(ZENITH * D2R) - (sinDec * sinf(lat * D2R))) /
                 (cosDec * cosf(lat * D2R));
    if (cosH < -1.0f) return 1439;   // sun never sets (midnight sun)
    if (cosH >  1.0f) return 0;      // sun never rises (polar night)

    float H = acosf(cosH) * R2D / 15.0f;          // sunset hour angle, hours
    float T = H + RA - (0.06571f * t) - 6.622f;
    float UT = fmodf(T - lngHour, 24.0f);
    if (UT < 0) UT += 24.0f;

    float nowH = hh + mm / 60.0f;
    float remH = UT - nowH;
    if (remH < 0) return 0;
    if (remH > 24) remH = 24;
    return (int)(remH * 60.0f);
}
