// Bare-board connectivity check for the FireBeetle 2 ESP32-E. No peripherals
// required: prints chip/flash info, the reset reason, scans the I2C bus, and
// blinks the onboard LED with a serial heartbeat.
//   pio run -e smoketest -t upload && pio device monitor

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

#ifndef LED_BUILTIN
#define LED_BUILTIN 2          // FireBeetle 2 ESP32-E onboard LED (GPIO2)
#endif

static void i2cScan() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  I2C device at 0x%02X\n", a);
            found++;
        }
    }
    Serial.printf("I2C scan on SDA=%d SCL=%d: %d device(s)\n",
                  PIN_I2C_SDA, PIN_I2C_SCL, found);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== Trail Computer smoke test ===");
    Serial.printf("Chip   : %s rev%d, %d core(s) @ %d MHz\n",
                  ESP.getChipModel(), ESP.getChipRevision(),
                  ESP.getChipCores(), ESP.getCpuFreqMHz());
    Serial.printf("Flash  : %u bytes\n", ESP.getFlashChipSize());
    Serial.printf("MAC    : %012llX\n", ESP.getEfuseMac());
    Serial.printf("Reset  : reason %d\n", (int)esp_reset_reason());
    i2cScan();
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.println("Heartbeat starting (LED on GPIO2 should blink)...");
}

void loop() {
    static uint32_t n = 0;
    digitalWrite(LED_BUILTIN, n & 1);
    Serial.printf("alive %lu  uptime %lus  heap %u\n",
                  n, millis() / 1000, ESP.getFreeHeap());
    n++;
    delay(500);
}
