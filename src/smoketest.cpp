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

    pinMode(PIN_GPS_PWR, OUTPUT);
    digitalWrite(PIN_GPS_PWR, GPS_PWR_ACTIVE_LEVEL);   // power GPS (harmless if wired straight to 3V3)
    Serial2.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    Serial.printf("GPS UART on RX=%d TX=%d @ %d baud. Echoing NMEA below "
                  "(lines starting with $ = good wiring; a fix needs sky view):\n",
                  PIN_GPS_RX, PIN_GPS_TX, GPS_BAUD);

    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    static uint32_t lastBeat = 0, gpsBytes = 0;
    while (Serial2.available()) {
        Serial.write(Serial2.read());
        gpsBytes++;
    }
    if (millis() - lastBeat >= 2000) {
        lastBeat = millis();
        digitalWrite(LED_BUILTIN, (millis() / 2000) & 1);
        Serial.printf("\n[uptime %lus  GPS bytes seen: %lu]\n",
                      millis() / 1000, gpsBytes);
    }
}
