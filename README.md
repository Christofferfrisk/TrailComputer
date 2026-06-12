# Trail Computer

A battery-powered, fully-offline handheld **trail computer** for multi-day hiking
(built around the Kungsleden in Swedish Lapland). It deep-sleeps and wakes on a
button press to show navigation and weather on an e-paper screen. All route data,
STF hut info, and configuration live on-device — no internet on the trail. A phone
config portal (on-demand Wi-Fi) handles route/target/settings.

- **MCU:** DFRobot FireBeetle 2 ESP32-E N16R2 (DFR1139, ESP32-WROOM-32E-N16R2, with PSRAM)
- **Display:** Waveshare 2.9" e-paper, 296×128, B/W, SSD1680
- **Sensors:** BME280 (baro/temp/humidity), QMC5883L (compass), u-blox GPS (UART)
- **Input:** one momentary button (wake + long-press for config)

---

## Architecture

A **"wake → do one cycle → sleep"** device. `setup()` *is* the whole cycle;
`loop()` is empty. Each wake: read sensors → power GPS for a warm-start fix →
read compass → fuse GPS-calibrated baro altitude → snap to route, compute
remaining distance + Naismith ETA → render one e-paper screen → account energy →
deep sleep. State that must survive sleep lives in `RTC_DATA_ATTR` (and mirrors to
NVS so it survives a battery swap). The route table lives in flash, never RTC.

| File | Role |
|---|---|
| `src/main.cpp` | wake-cycle orchestration |
| `src/config.h` | pins, power model, thresholds (all **VERIFY** items) |
| `src/state.{h,cpp}` | RTC/NVS persistent state, settings, trip log |
| `src/geo.{h,cpp}` | haversine, bearing, snap-to-segment, cumulative distance |
| `src/fusion.{h,cpp}` | GPS-calibrated altitude, ascent accumulator, pressure trend |
| `src/gps.{h,cpp}` | MOSFET-switched warm-start fix (HDOP gate + timeout) |
| `src/power.{h,cpp}` | mAh energy accounting, battery curve |
| `src/sun.{h,cpp}` | sunrise/sunset → daylight remaining |
| `src/display.{h,cpp}` | the e-paper screens (GxEPD2 + U8g2) |
| `src/config_portal.{h,cpp}` | SoftAP + offline web UI, persists to NVS |
| `src/route_table.h` | generated main route polyline + hut indices (from GPX) |
| `src/spur_table.h` | generated Kebnekaise spur polyline (Nikkaluokta→Singi) |
| `src/route_stages.h` | STF leg distances/times, shops, sauna, transport |
| `src/route_spurs.h` | spur metadata (names/coords) for the timeline |
| `tools/gpx_to_route.py` | GPX → simplified main route table |
| `tools/build_spur.py` | GPX + OSM + DEM → simplified spur table |
| `tools/preview_display.py` | render the e-paper screens to PNG on the PC |

---

## Bill of materials

| Part | Notes |
|---|---|
| DFRobot FireBeetle 2 ESP32-E N16R2 (DFR1139) | onboard LiPo charging, low deep-sleep current; **GPIO16/D11 is NC** (PSRAM), GPIO5/D8 = WS2812 LED, GPIO27/D4 = user button |
| Waveshare 2.9" e-paper 296×128 B/W (SSD1680) | **B/W only** — tri-color refreshes ~15 s |
| u-blox GPS module | NEO-M9N (fast) or NEO-6M (budget); must have a **VBCKP** backup pin |
| BME280 breakout | I²C, addr 0x76 |
| QMC5883L magnetometer | I²C, addr 0x0D |
| P-channel MOSFET + 100 kΩ gate pull-up | high-side switch for GPS power |
| Momentary push button | wake; to GND |
| 1S LiPo (or 18650) | charged via the FireBeetle USB-C |
| Resistor divider for battery sense | e.g. 2× 100 kΩ → ratio 2.0 |

---

## Wiring

Pins are defined in [`src/config.h`](src/config.h). **Every value is a `VERIFY`
item** — confirm against the FireBeetle 2 ESP32-E silk→GPIO map before soldering.

| Function | GPIO | Notes |
|---|---|---|
| e-paper CS | 14 | D6 — **not 5**, which drives the onboard WS2812 RGB LED |
| e-paper DC | 25 | D2 |
| e-paper RST | 26 | D3 |
| e-paper BUSY | 35 | A3, input-only (BUSY is push-pull) — **not 27**, the onboard user button |
| e-paper SCK / MOSI | 18 / 23 | HW SPI defaults |
| I²C SDA / SCL | 21 / 22 | BME280 + QMC5883L share the bus |
| GPS RX / TX | 19 / 17 | ESP RX←GPS TX (19=MISO header, free), ESP TX→GPS RX; **16/D11 is NC on the N16R2**, 9600 baud |
| GPS power MOSFET gate | 13 | high-side P-FET, **active LOW**, gate pull-up to source |
| Wake button | 4 | RTC-capable, ext0, active-low (`INPUT_PULLUP`) |
| Battery sense | 34 | through the divider (set `BAT_DIVIDER_RATIO`) |

**GPS power note:** the P-FET high-side switch assumes you switch the **3.3 V**
rail with the gate driven directly from GPIO13 (gate pull-up keeps GPS off in
sleep). If you switch raw VBAT or use an NPN/N-FET gate driver, flip
`GPS_PWR_ACTIVE_LEVEL` to `HIGH`.

**Physical placement (learned the hard way):**
- GPS antenna faces the sky, never behind the battery; keep its feed away from the compass.
- Read the compass only while the GPS is powered down (the firmware already does this); keep it away from battery current leads; brass/nylon screws nearby; calibrate in the assembled case.
- BME280 reads high from board self-heat → vented (PTFE) edge, thermally gapped; tune `BME_TEMP_OFFSET_C`.
- Don't bend the e-paper FPC toward the front. Don't charge LiPo below 0 °C.

---

## Build & flash

PlatformIO (framework: arduino). If `pio` isn't on PATH, use `python -m platformio`.

```bash
# main firmware
pio run -e firebeetle2_esp32e -t upload --upload-port COM3
pio device monitor -b 115200

# bring-up helpers (separate envs, don't touch the main firmware):
pio run -e smoketest -t upload   # chip info, I²C scan, LED blink (no peripherals)
pio run -e wifitest  -t upload   # config portal standalone (join Wi-Fi TrailComputer)
```

The FireBeetle auto-resets into the bootloader. If an upload fails to sync, hold
**BOOT**, tap **RST**, release BOOT, retry. The board uses a **CH340** USB-serial
chip — install its driver if no COM port appears.

> The device deep-sleeps after one cycle, so the monitor shows one burst then goes
> quiet — that's normal. Press the wake button (or RST) for another cycle.

---

## Tools

```bash
# Regenerate the route table from a GPX (Topo GPS / Gaia / Waymarked Trails):
python tools/gpx_to_route.py track.gpx --huts data/huts.csv --eps 150 --reverse --c > src/route_table.h
#   --reverse : travel direction N→S (index 0 = Abisko)
#   --eps     : Douglas-Peucker spacing in metres (~150)
# Adding/removing huts shifts stop codes → re-pick hike Start/End in the portal.

# Preview the e-paper screens as PNG (no hardware needed):
python tools/preview_display.py        # -> tools/preview/*.png

# Regenerate the Kebnekaise spur (Nikkaluokta -> Kebnekaise -> Singi). Geometry
# from a Topo GPS track + OSM "Kungsleden Etapp 9"; elevation from EU-DEM
# (cached in data/*_track_ele.json):
python tools/build_spur.py > src/spur_table.h

# Host-side math tests (compiles the real src/*.cpp on the PC, runs 47 asserts
# on geo/route/spur/fusion/sun; needs `pip install --user ziglang` once):
test\run_tests.cmd
```

Run the tests after any change to `geo`, `fusion`, `sun`, `route_util`, or after
regenerating `route_table.h` — a failure means distances/ETA/daylight on the
device would be wrong.

---

## Usage

- **Short press** — wake, show the nav screen, sleep.
- **Long press (hold ≥2 s at wake)** — config mode: brings up Wi-Fi AP
  **`TrailComputer`** (open) → browse **`http://192.168.4.1`** from your phone to
  set the route section (Start/End), target, day-planner pace, and settings. The
  radio is on-demand only and shuts off after ~2 min idle.

Screens: **NAV** (compass needle + distance + ETA/climb/alt + weather) or **MAP**
(north-up dot map: heading arrow, nearby trail, hut markers, destination, scale
bar) — pick which in the portal **Settings → Nav screen**. Plus **NO_FIX**,
**ARRIVED / End of hike** (trip stats), **CONFIG**, **LOW_BATTERY**.

---

## Configuration & calibration

Tunable at runtime from the config portal (saved to NVS — no reflash): battery
capacity & usable %, BME temp offset, day-planner hours. Compile-time defaults and
thresholds are in `config.h`.

Calibration checklist:
1. **Battery** — set `BAT_DIVIDER_RATIO` to your divider; refine `batteryPctFromVoltage` LUT against the real pack.
2. **BME temp offset** — compare to a reference thermometer after warm-up; set in the portal.
3. **Compass** — do a hard/soft-iron calibration **in the assembled case** (routine TBD).
4. **e-paper** — if your panel revision differs, switch `EPD_USE_T94` (GxEPD2_290_T94 ↔ _290_BS).

### VERIFY checklist (before first real assembly)
- [ ] Pin map matches the FireBeetle silk (all rows above)
- [ ] GPS MOSFET polarity (`GPS_PWR_ACTIVE_LEVEL`)
- [ ] Battery divider ratio
- [ ] BME temperature offset
- [ ] e-paper driver class (T94 vs BS)
- [ ] GPIO4 is RTC-capable on your board (ext0 wake)

---

## Status

Firmware and portal are written and compile for the ESP32 target; the bare board
flashes and runs (smoke + Wi-Fi portal verified on hardware). Sensors, GPS, and the
e-paper are **not yet wired/tested** — those `VERIFY` items and on-trail calibration
remain. The Nikkaluokta→Kebnekaise→Singi approach is fully navigable (the nav snaps
to whichever of the two polylines is nearest).
