#include "power.h"
#include "config.h"
#include "state.h"
#include <Arduino.h>

float accountCycleMah(uint32_t cpuMs, uint32_t gpsMs, uint32_t epdMs) {
    float h = 1.0f / 3600000.0f;   // ms -> h
    return PWR_CPU_ACTIVE_MA * cpuMs * h +
           PWR_GPS_ON_MA     * gpsMs * h +
           PWR_EPD_REFRESH_MA * epdMs * h;
}

float remainingMah(float consumedMah) {
    float usable = g_state->settings.batCapacityMah * g_state->settings.batUsableFrac;
    float left = usable - consumedMah;
    return left < 0 ? 0 : left;
}

int estimatedChecksLeft(float consumedMah, uint32_t bootCount) {
    if (bootCount == 0) return 0;
    float perCycle = consumedMah / bootCount;
    if (perCycle <= 0) return 0;
    return (int)(remainingMah(consumedMah) / perCycle);
}

float readBatteryVolts() {
    // analogReadMilliVolts applies the chip's factory ADC calibration (the raw
    // ADC is markedly nonlinear). Average a few samples; call this while the
    // GPS/WiFi are off so the rail isn't sagging under load.
    uint32_t mv = 0;
    for (int i = 0; i < BAT_ADC_SAMPLES; i++) mv += analogReadMilliVolts(PIN_BAT_ADC);
    mv /= BAT_ADC_SAMPLES;
    return (mv / 1000.0f) * BAT_DIVIDER_RATIO;
}

int batteryPctFromVoltage(float v) {
    // Coarse single-cell LiPo curve; refine against the real pack.   // VERIFY
    static const float vlut[] = {3.30f, 3.60f, 3.70f, 3.80f, 3.90f, 4.00f, 4.20f};
    static const float plut[] = {0,     10,    30,    55,    75,    90,    100};
    if (v <= vlut[0]) return 0;
    for (int i = 1; i < 7; i++) {
        if (v <= vlut[i]) {
            float f = (v - vlut[i - 1]) / (vlut[i] - vlut[i - 1]);
            return (int)(plut[i - 1] + f * (plut[i] - plut[i - 1]));
        }
    }
    return 100;
}
