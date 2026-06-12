#include "fusion.h"
#include "config.h"
#include <math.h>

// Hypsometric: h = (T/L) * (1 - (p/p0)^(R L / g M)). Using the common
// barometric-formula coefficient 0.190284 with the sensor's own temperature.
float baroAltitudeM(float pressureHpa, float tempC, float seaLevelHpa) {
    float tK = tempC + 273.15f;
    return (tK / 0.0065f) * (1.0f - powf(pressureHpa / seaLevelHpa, 0.190284f));
}

void steerSeaLevelRef(float& seaLevelRef, float pressureHpa, float tempC, float gpsAltM) {
    // Sea-level pressure that would place baro altitude exactly at gpsAltM.
    float tK = tempC + 273.15f;
    float implied = pressureHpa / powf(1.0f - (gpsAltM * 0.0065f) / tK, 5.255f);
    seaLevelRef += ALT_FILTER_ALPHA * (implied - seaLevelRef);
}

void accumulateClimb(float newAltM, float& lastAltM, float& cumAsc, float& cumDesc) {
    float d = newAltM - lastAltM;
    if (d > ASCENT_DEADBAND_M)  { cumAsc  += d; lastAltM = newAltM; }
    else if (d < -ASCENT_DEADBAND_M) { cumDesc += -d; lastAltM = newAltM; }
}

float pressureTrendHpa(const float* hist, int n) {
    if (n < 3) return 0.0f;
    float sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < n; i++) {
        sx += i; sy += hist[i];
        sxx += (float)i * i; sxy += (float)i * hist[i];
    }
    float denom = n * sxx - sx * sx;
    if (denom == 0) return 0.0f;
    float slope = (n * sxy - sx * sy) / denom;   // hPa per sample
    return slope * (n - 1);                       // total change across window
}
