#pragma once
#include <stdint.h>

// Convert a cycle's measured durations into consumed mAh and add to the running
// total. cpuMs is total awake time; gpsMs the GPS-on time; epdMs refresh time.
float accountCycleMah(uint32_t cpuMs, uint32_t gpsMs, uint32_t epdMs);

// Remaining usable capacity (mAh) and an estimated number of button-press
// checks left, given the average per-cycle cost so far.
float remainingMah(float consumedMah);
int   estimatedChecksLeft(float consumedMah, uint32_t bootCount);

// LiPo discharge-curve estimate of state-of-charge from a battery voltage.
int   batteryPctFromVoltage(float volts);
float readBatteryVolts();
