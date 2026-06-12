#pragma once

// Altitude from BME280 pressure + its own temperature (hypsometric form),
// using the GPS-steered sea-level reference in PersistentState.
float baroAltitudeM(float pressureHpa, float tempC, float seaLevelHpa);

// Complementary steer of the QNH reference toward GPS altitude. Only call with
// a good fix; alpha is small (ALT_FILTER_ALPHA). Updates seaLevelRef in place.
void steerSeaLevelRef(float& seaLevelRef, float pressureHpa, float tempC, float gpsAltM);

// Accumulate ascent/descent with a deadband; updates cum* and lastAlt in place.
void accumulateClimb(float newAltM, float& lastAltM, float& cumAsc, float& cumDesc);

// Total change (hPa) across a rolling QNH history, oldest..newest, via a
// least-squares slope scaled over the window. Negative = falling. Because the
// history holds sea-level (QNH) values, altitude-driven pressure change is
// already factored out — a falling trend is weather, not climbing.
float pressureTrendHpa(const float* hist, int n);
