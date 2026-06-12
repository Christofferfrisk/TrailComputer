#pragma once

// ---------------------------------------------------------------------------
// Pin map. Defaults from the project spec; every value tagged VERIFY must be
// checked against the FireBeetle 2 ESP32-E silk->GPIO mapping before wiring.
// ---------------------------------------------------------------------------

// e-paper (SSD1680, hardware SPI)            // VERIFY silk labels
// CS on 14/D6 (5/D8 drives the onboard WS2812); BUSY on input-only 35/A3
// (27/D4 is the onboard user button).
#define PIN_EPD_CS    14
#define PIN_EPD_DC    25
#define PIN_EPD_RST   26
#define PIN_EPD_BUSY  35
#define PIN_EPD_SCK   18   // HW SPI default
#define PIN_EPD_MOSI  23   // HW SPI default

// I2C bus (BME280 + QMC5883L)
#define PIN_I2C_SDA   21
#define PIN_I2C_SCL   22
#define BME280_ADDR   0x76

// GPS UART
#define PIN_GPS_RX    19   // ESP RX  <- GPS TX  (16/D11 is NC on the N16R2)
#define PIN_GPS_TX    17   // ESP TX  -> GPS RX
#define GPS_BAUD      9600
#define PIN_GPS_PWR   13   // high-side P-MOSFET gate; see GPS_PWR_ACTIVE

// A P-MOSFET high-side switch is on when the gate is pulled LOW.   // VERIFY polarity
#define GPS_PWR_ACTIVE_LEVEL  LOW

// Wake button: momentary to GND on an RTC-capable GPIO (ext0), active low.
#define PIN_BUTTON    4    // VERIFY this is RTC GPIO on the FireBeetle

// Onboard WS2812 RGB LED (hardwired to GPIO5 on the FireBeetle 2 ESP32-E). It
// latches its last colour and the 3V3 rail stays up in deep sleep, so it must be
// driven dark each boot or it burns battery the whole trip.
#define PIN_RGB_LED   5

// Battery sense
#define PIN_BAT_ADC   34
#define BAT_DIVIDER_RATIO  2.0f   // VERIFY actual resistor divider

// ---------------------------------------------------------------------------
// Display geometry / GxEPD2 driver class
// ---------------------------------------------------------------------------
// Panel: Waveshare 2.9" 296x128 B/W. Use GxEPD2_290_T94; switch to
// GxEPD2_290_BS if the panel revision differs.                     // VERIFY
#define EPD_USE_T94   1
#define EPD_WIDTH     296
#define EPD_HEIGHT    128
#define EPD_ROTATION  1    // landscape

// ---------------------------------------------------------------------------
// Cadence / thresholds
// ---------------------------------------------------------------------------
#define GPS_FIX_TIMEOUT_MS   45000UL
#define GPS_HDOP_GATE        300       // TinyGPS hdop is value*100; 3.0 -> 300
#define LONG_PRESS_MS        2000UL    // hold at wake -> config mode
#define WAYPOINT_ARRIVE_M    55.0f     // above typical GPS noise (NEO-6M ~5-40 m)
#define ASCENT_DEADBAND_M    1.5f
#define ALT_FILTER_ALPHA     0.02f     // GPS->QNH steering rate (good fix only)
#define WAKE_TIMER_S         0         // 0 = button-only wake (no RTC drift budget)
#define NAISMITH_SPEED_KMH   4.5f      // flat walking pace with a pack
#define EPD_REFRESH_MS       2500      // full-window refresh duration estimate
#define CONFIG_PORTAL_TIMEOUT_MS  120000UL

// RTC/NVS state validity. Bump the low byte whenever PersistentState's layout
// changes so stale RTC memory from an older firmware is rejected.
#define STATE_MAGIC          0xC0FFEE08u

// Route snap: search this many segments around the last known position; if the
// best match is farther off-route than this, fall back to a global search.
#define SNAP_WINDOW_SEGS     25
#define SNAP_REACQUIRE_M     500.0f

// Weather: total QNH drop (hPa) across the pressure history that flags "turning".
#define WEATHER_WINDOW_DROP_HPA  1.0f

// Battery thresholds and ADC averaging.
#define BAT_LOW_PCT          15
#define BAT_CRIT_PCT         5
#define BAT_ADC_SAMPLES      16

// Needle reference: course-over-ground when moving faster than this, else the
// magnetometer heading (COG is noise when nearly stationary).
#define MOVING_SPEED_MPS     0.7f

// ---------------------------------------------------------------------------
// Simulation: set SIM_MODE 1 to fake a GPS fix that walks along the route on
// each button press — indoor testing without satellites. Set back to 0 before
// the trail.
// ---------------------------------------------------------------------------
#define SIM_MODE             0
#define SIM_START_ROUTE_IDX  240       // start near here (Kaitumjaure area)
#define SIM_STEP_M           800.0f    // advance per wake/button press
#define SIM_PRESSURE_HPA     1006.0f   // base QNH; drifts to exercise weather

// ---------------------------------------------------------------------------
// Power model (editable; see power.{h,cpp})
// ---------------------------------------------------------------------------
#define BAT_CAPACITY_MAH     2000.0f
#define BAT_USABLE_FRACTION  0.80f
#define PWR_SLEEP_UA         60.0f
#define PWR_CPU_ACTIVE_MA    80.0f
#define PWR_GPS_ON_MA        45.0f
#define PWR_EPD_REFRESH_MA   25.0f     // averaged over a full-window refresh

// BME280 reads high from board self-heat.
#define BME_TEMP_OFFSET_C    -1.5f     // VERIFY against a reference thermometer
