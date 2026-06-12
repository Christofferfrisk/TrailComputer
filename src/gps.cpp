#include "gps.h"
#include "config.h"
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <driver/gpio.h>

static TinyGPSPlus tinygps;

// UBX-CFG-SBAS: enable SBAS (EGNOS over Europe) with ranging + differential
// corrections, 3 channels, auto-scan PRNs. Sent each wake because the module is
// power-cycled between fixes, so the setting isn't relied on to persist.
// Bytes: hdr B5 62, class/id 06 16, len 0008, payload, checksum 2B B9.
static const uint8_t UBX_CFG_SBAS[] = {
    0xB5, 0x62, 0x06, 0x16, 0x08, 0x00,
    0x01, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2B, 0xB9
};

void gpsPower(bool on) {
    gpio_hold_dis((gpio_num_t)PIN_GPS_PWR);    // release any deep-sleep hold first
    pinMode(PIN_GPS_PWR, OUTPUT);
    bool active = (GPS_PWR_ACTIVE_LEVEL == LOW);
    digitalWrite(PIN_GPS_PWR, on == active ? LOW : HIGH);
}

GpsFix gpsAcquireFix(uint32_t timeoutMs, int hdopGate) {
    GpsFix fix{};
    uint32_t t0 = millis();

    gpsPower(true);
    Serial1.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    delay(100);                                    // let the module's UART come up
    Serial1.write(UBX_CFG_SBAS, sizeof(UBX_CFG_SBAS));

    while (millis() - t0 < timeoutMs) {
        while (Serial1.available()) tinygps.encode(Serial1.read());

        bool goodHdop = tinygps.hdop.isValid() && tinygps.hdop.value() <= hdopGate;
        if (tinygps.location.isValid() && tinygps.location.isUpdated() && goodHdop) {
            fix.valid = true;
            fix.lat   = tinygps.location.lat();
            fix.lon   = tinygps.location.lng();
            fix.altM  = tinygps.altitude.meters();
            fix.courseDeg = tinygps.course.isValid() ? tinygps.course.deg() : 0.0f;
            fix.speedMps  = tinygps.speed.isValid()  ? tinygps.speed.mps()  : 0.0f;
            fix.sats  = tinygps.satellites.value();
            fix.hdop  = tinygps.hdop.value();
            fix.hh    = tinygps.time.hour();
            fix.mm    = tinygps.time.minute();
            fix.year  = tinygps.date.year();
            fix.month = tinygps.date.month();
            fix.day   = tinygps.date.day();
            break;
        }
        delay(10);
    }
    fix.sats = tinygps.satellites.isValid() ? tinygps.satellites.value() : fix.sats;

    Serial1.end();
    gpsPower(false);            // VBCKP stays powered for next warm start
    fix.onTimeMs = millis() - t0;
    return fix;
}
