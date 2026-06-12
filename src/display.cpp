#include "display.h"
#include "config.h"
#include <math.h>

#define ENABLE_GxEPD2_GFX 0
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

#if EPD_USE_T94
using EpdPanel = GxEPD2_290_T94;
#else
using EpdPanel = GxEPD2_290_BS;
#endif

static GxEPD2_BW<EpdPanel, EpdPanel::HEIGHT> epd(
    EpdPanel(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));
static U8G2_FOR_ADAFRUIT_GFX u8g2;

// Swedish glyphs (å ä ö) require these latin font tables, not Adafruit GFX.
static const uint8_t* F_TINY = u8g2_font_5x7_tf;
static const uint8_t* F_SM   = u8g2_font_helvR08_tf;
static const uint8_t* F_MD   = u8g2_font_helvR10_tf;
static const uint8_t* F_MDB  = u8g2_font_helvB10_tf;   // bold, same height as F_MD
static const uint8_t* F_BD   = u8g2_font_helvB12_tf;
static const uint8_t* F_HUGE = u8g2_font_helvB18_tf;

static const float D2R = 0.01745329f;

// --- text helpers ----------------------------------------------------------
static void text(int x, int y, const char* s, const uint8_t* font, bool inv = false) {
    u8g2.setFont(font);
    u8g2.setForegroundColor(inv ? GxEPD_WHITE : GxEPD_BLACK);
    u8g2.setBackgroundColor(inv ? GxEPD_BLACK : GxEPD_WHITE);
    u8g2.setCursor(x, y);
    u8g2.print(s);
}
static int textW(const char* s, const uint8_t* font) {
    u8g2.setFont(font);
    return u8g2.getUTF8Width(s);
}
static void textR(int xRight, int y, const char* s, const uint8_t* font) {
    text(xRight - textW(s, font), y, s, font);
}
static void textC(int xCenter, int y, const char* s, const uint8_t* font, bool inv = false) {
    text(xCenter - textW(s, font) / 2, y, s, font, inv);
}

// --- small icons ------------------------------------------------------------
static void drawBattery(int x, int y, int pct, bool low) {
    const int w = 20, h = 10;
    epd.drawRect(x, y, w, h, GxEPD_BLACK);
    epd.fillRect(x + w, y + 3, 2, h - 6, GxEPD_BLACK);   // terminal nub
    int fill = (pct * (w - 4)) / 100;
    if (fill < 0) fill = 0; if (fill > w - 4) fill = w - 4;
    if (low) {
        text(x + 7, y + h - 2, "!", F_SM);               // hollow + bang when low
    } else {
        epd.fillRect(x + 2, y + 2, fill, h - 4, GxEPD_BLACK);
    }
}

static void drawSun(int cx, int cy) {
    epd.fillCircle(cx, cy, 2, GxEPD_BLACK);
    for (int a = 0; a < 360; a += 45) {
        float r = a * D2R;
        epd.drawPixel(cx + (int)(5 * cosf(r)), cy + (int)(5 * sinf(r)), GxEPD_BLACK);
    }
}

// signal-bars glyph for satellites
static void drawSat(int x, int y) {
    epd.fillRect(x,     y + 4, 2, 3, GxEPD_BLACK);
    epd.fillRect(x + 3, y + 2, 2, 5, GxEPD_BLACK);
    epd.fillRect(x + 6, y,     2, 7, GxEPD_BLACK);
}

static void drawTrend(int x, int y, int trend) {   // y is the arrow's mid height
    if (trend > 0)      { epd.fillTriangle(x, y - 4, x - 3, y + 1, x + 3, y + 1, GxEPD_BLACK); }
    else if (trend < 0) { epd.fillTriangle(x, y + 4, x - 3, y - 1, x + 3, y - 1, GxEPD_BLACK); }
    else                { epd.fillRect(x - 3, y - 1, 6, 2, GxEPD_BLACK); }
}

// --- status bar (shared) ----------------------------------------------------
static void drawStatusBar(const ViewModel& vm) {
    char buf[24];
    text(2, 11, vm.clockHHMM, F_MD);

    if (vm.daylightLeftMin >= 0) {
        drawSun(60, 6);
        snprintf(buf, sizeof(buf), "%d:%02d", vm.daylightLeftMin / 60, vm.daylightLeftMin % 60);
        text(68, 11, buf, F_SM);
    }

    drawSat(150, 2);
    snprintf(buf, sizeof(buf), "%d", vm.satCount);
    text(160, 11, buf, F_SM);

    drawBattery(EPD_WIDTH - 22, 1, vm.batteryPct, vm.lowBattery);
    snprintf(buf, sizeof(buf), "%d%%", vm.batteryPct);
    textR(EPD_WIDTH - 24, 10, buf, F_SM);

    epd.drawFastHLine(0, 14, EPD_WIDTH, GxEPD_BLACK);
}

// --- compass ----------------------------------------------------------------
static void polar(int cx, int cy, float deg, float r, int& x, int& y) {
    float a = deg * D2R;
    x = cx + (int)lroundf(r * sinf(a));
    y = cy - (int)lroundf(r * cosf(a));
}

static void drawCompass(const ViewModel& vm, int cx, int cy, int r) {
    epd.drawCircle(cx, cy, r, GxEPD_BLACK);

    // ticks every 30°, longer at the cardinals; bezel rotates with heading.
    for (int d = 0; d < 360; d += 30) {
        float screen = d - vm.headingDeg;
        bool card = (d % 90 == 0);
        int x0, y0, x1, y1;
        polar(cx, cy, screen, r, x0, y0);
        polar(cx, cy, screen, r - (card ? 6 : 3), x1, y1);
        epd.drawLine(x0, y0, x1, y1, GxEPD_BLACK);
    }
    // north marker: filled triangle just outside the ring
    int nx, ny;
    polar(cx, cy, -vm.headingDeg, r + 4, nx, ny);
    int lx, ly, rx, ry;
    polar(cx, cy, -vm.headingDeg + 8, r, lx, ly);
    polar(cx, cy, -vm.headingDeg - 8, r, rx, ry);
    epd.fillTriangle(nx, ny, lx, ly, rx, ry, GxEPD_BLACK);

    // target needle (filled arrowhead + tail), relative to heading
    float rel = vm.bearingToNextDeg - vm.headingDeg;
    int tipx, tipy, plx, ply, prx, pry, tailx, taily;
    polar(cx, cy, rel, r - 6, tipx, tipy);
    polar(cx, cy, rel + 90, 5, plx, ply);
    polar(cx, cy, rel - 90, 5, prx, pry);
    polar(cx, cy, rel + 180, r - 10, tailx, taily);
    epd.drawLine(cx, cy, tailx, taily, GxEPD_BLACK);
    epd.fillTriangle(tipx, tipy, plx, ply, prx, pry, GxEPD_BLACK);
    epd.fillCircle(cx, cy, 2, GxEPD_BLACK);
}

// --- weather strip ----------------------------------------------------------
static void drawSparkline(const ViewModel& vm, int x, int y, int w, int h) {
    epd.drawRect(x, y, w, h, GxEPD_BLACK);
    if (!vm.sparkline || vm.sparklineLen < 2) return;
    float lo = vm.sparkline[0], hi = vm.sparkline[0];
    for (int i = 1; i < vm.sparklineLen; i++) {
        lo = min(lo, vm.sparkline[i]); hi = max(hi, vm.sparkline[i]);
    }
    float span = max(0.5f, hi - lo);
    int px = x + 2, py = 0;
    for (int i = 0; i < vm.sparklineLen; i++) {
        int cx = x + 2 + i * (w - 4) / (vm.sparklineLen - 1);
        int cy = y + h - 2 - (int)((vm.sparkline[i] - lo) / span * (h - 4));
        if (i > 0) epd.drawLine(px, py, cx, cy, GxEPD_BLACK);
        epd.fillCircle(cx, cy, 1, GxEPD_BLACK);
        px = cx; py = cy;
    }
}

static void drawWeather(const ViewModel& vm, int top) {
    char buf[24];
    epd.drawFastHLine(0, top, EPD_WIDTH, GxEPD_BLACK);

    // left column, two rows: pressure + trend, then temperature + humidity
    snprintf(buf, sizeof(buf), "%.0f hPa", vm.pressureHpa);
    text(2, top + 13, buf, F_MD);
    drawTrend(textW(buf, F_MD) + 10, top + 9, vm.pressureTrend);

    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0""C  %.0f%%", vm.temperatureC, vm.humidityPct);
    text(2, top + 26, buf, F_SM);

    // right column: weather-turning chip or a pressure sparkline
    const int rxs = 150, rw = EPD_WIDTH - rxs - 2;
    if (vm.weatherTurning) {
        epd.fillRoundRect(rxs, top + 3, rw, 14, 3, GxEPD_BLACK);
        textC(rxs + rw / 2, top + 13, "Weather turning", F_SM, true);
        drawSparkline(vm, rxs, top + 19, rw, 8);
    } else {
        drawSparkline(vm, rxs, top + 5, rw, 20);
    }
}

// --- screens ----------------------------------------------------------------
static void drawTile(int cx, int yl, int yv, const char* label, const char* value) {
    text(cx - textW(label, F_TINY) / 2, yl, label, F_TINY);
    text(cx - textW(value, F_MD) / 2,   yv, value, F_MD);
}
static void drawStat(int cx, const char* label, const char* value) {
    drawTile(cx, 77, 93, label, value);
}

static void renderNav(const ViewModel& vm) {
    drawStatusBar(vm);

    // left: compass panel with a heading readout, divided from the nav column
    drawCompass(vm, 38, 54, 27);
    char hd[8];
    snprintf(hd, sizeof(hd), "%d\xC2\xB0", (int)(vm.headingDeg + 0.5f));
    text(38 - textW(hd, F_SM) / 2, 94, hd, F_SM);
    epd.drawLine(74, 18, 74, 98, GxEPD_BLACK);

    int rx = 82;
    text(rx, 31, vm.nextStopName, F_BD);

    char num[16]; const char* unit;
    if (vm.remainingKm < 1.0f) { snprintf(num, sizeof(num), "%.0f", vm.remainingKm * 1000); unit = "m"; }
    else                       { snprintf(num, sizeof(num), "%.1f", vm.remainingKm);        unit = "km"; }
    text(rx, 59, num, F_HUGE);
    text(rx + textW(num, F_HUGE) + 4, 59, unit, F_MD);

    epd.drawFastHLine(rx, 66, EPD_WIDTH - rx, GxEPD_BLACK);

    // three stat tiles with small-caps labels
    epd.drawLine(153, 70, 153, 97, GxEPD_BLACK);
    epd.drawLine(224, 70, 224, 97, GxEPD_BLACK);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", vm.etaMin / 60, vm.etaMin % 60);
    drawStat(117, "ETA", buf);
    snprintf(buf, sizeof(buf), "%.0f m", vm.climbLeftM);
    drawStat(188, "CLIMB", buf);
    snprintf(buf, sizeof(buf), "%.0f m", vm.altitudeM);
    drawStat(259, "ALT", buf);

    drawWeather(vm, 100);
}

static void renderNoFix(const ViewModel& vm) {
    drawStatusBar(vm);
    textC(EPD_WIDTH / 2, 52, "Acquiring GPS", F_HUGE);
    char buf[28];
    snprintf(buf, sizeof(buf), "%d satellites in view", vm.satsInView);
    textC(EPD_WIDTH / 2, 80, buf, F_MD);
    textC(EPD_WIDTH / 2, 110, "move to open sky for a fix", F_TINY);
}

static void renderArrived(const ViewModel& vm) {
    drawStatusBar(vm);
    textC(EPD_WIDTH / 2, 40, vm.endOfHike ? "End of hike" : "Arrived", F_HUGE);
    textC(EPD_WIDTH / 2, 60, vm.nextStopName, F_BD);

    epd.drawFastHLine(8, 70, EPD_WIDTH - 16, GxEPD_BLACK);
    epd.drawLine(EPD_WIDTH / 3, 74, EPD_WIDTH / 3, 104, GxEPD_BLACK);
    epd.drawLine(2 * EPD_WIDTH / 3, 74, 2 * EPD_WIDTH / 3, 104, GxEPD_BLACK);

    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f m", vm.cumAscentM);
    drawTile(EPD_WIDTH / 6, 84, 100, "ASCENT", buf);
    snprintf(buf, sizeof(buf), "%.0f m", vm.cumDescentM);
    drawTile(EPD_WIDTH / 2, 84, 100, "DESCENT", buf);
    snprintf(buf, sizeof(buf), "~%d", vm.checksLeft);
    drawTile(5 * EPD_WIDTH / 6, 84, 100, "CHECKS", buf);

    textC(EPD_WIDTH / 2, 122,
          vm.endOfHike ? "long-press to change hike" : "long-press to set next stop", F_TINY);
}

static void renderConfig(const ViewModel& vm) {
    drawStatusBar(vm);
    textC(EPD_WIDTH / 2, 40, "Config mode", F_HUGE);
    textC(EPD_WIDTH / 2, 60, "Connect to Wi-Fi", F_TINY);
    textC(EPD_WIDTH / 2, 80, vm.apSsid, F_BD);
    char buf[40];
    snprintf(buf, sizeof(buf), "http://%s", vm.apUrl);
    textC(EPD_WIDTH / 2, 100, buf, F_MD);
    textC(EPD_WIDTH / 2, 120, "open in your phone browser", F_TINY);
}

static void renderLowBatt(const ViewModel& vm) {
    drawStatusBar(vm);
    drawBattery(EPD_WIDTH / 2 - 11, 28, vm.batteryPct, true);
    textC(EPD_WIDTH / 2, 70, "LOW BATTERY", F_HUGE);
    char buf[24];
    snprintf(buf, sizeof(buf), "~%.0f mAh left", vm.remainingMah);
    textC(EPD_WIDTH / 2, 92, buf, F_MD);
    textC(EPD_WIDTH / 2, 112, "GPS off - make for nearest hut", F_TINY);
}

// Cohen-Sutherland clip of a segment to a rectangle (GxEPD2 only clips to the
// whole screen, so the map polyline would otherwise overdraw the status bar).
static int clipCode(float x, float y, int xmin, int ymin, int xmax, int ymax) {
    int c = 0;
    if (x < xmin) c |= 1; else if (x > xmax) c |= 2;
    if (y < ymin) c |= 4; else if (y > ymax) c |= 8;
    return c;
}
static bool clipLine(float& x0, float& y0, float& x1, float& y1,
                     int xmin, int ymin, int xmax, int ymax) {
    int c0 = clipCode(x0, y0, xmin, ymin, xmax, ymax);
    int c1 = clipCode(x1, y1, xmin, ymin, xmax, ymax);
    for (int guard = 0; guard < 8; guard++) {
        if (!(c0 | c1)) return true;        // both inside
        if (c0 & c1) return false;          // trivially outside
        int c = c0 ? c0 : c1;
        float x = 0, y = 0;
        if (c & 8)      { x = x0 + (x1 - x0) * (ymax - y0) / (y1 - y0); y = ymax; }
        else if (c & 4) { x = x0 + (x1 - x0) * (ymin - y0) / (y1 - y0); y = ymin; }
        else if (c & 2) { y = y0 + (y1 - y0) * (xmax - x0) / (x1 - x0); x = xmax; }
        else            { y = y0 + (y1 - y0) * (xmin - x0) / (x1 - x0); x = xmin; }
        if (c == c0) { x0 = x; y0 = y; c0 = clipCode(x0, y0, xmin, ymin, xmax, ymax); }
        else         { x1 = x; y1 = y; c1 = clipCode(x1, y1, xmin, ymin, xmax, ymax); }
    }
    return false;
}

// 2 px line: a second pass offset perpendicular to the dominant axis so the
// width stays even at any angle (e-paper has no anti-aliasing).
static void drawLine2(int ax, int ay, int bx, int by) {
    epd.drawLine(ax, ay, bx, by, GxEPD_BLACK);
    int dx = bx - ax, dy = by - ay;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx >= dy) epd.drawLine(ax, ay + 1, bx, by + 1, GxEPD_BLACK);
    else          epd.drawLine(ax + 1, ay, bx + 1, by, GxEPD_BLACK);
}

static void renderMap(const ViewModel& vm) {
    drawStatusBar(vm);

    const int x0 = 2, y0 = 16, w = 182, h = EPD_HEIGHT - 18;  // map = left panel
    const int cx = x0 + w / 2, cy = y0 + h / 2;
    const float halfPx = h / 2 - 3;
    epd.drawRect(x0, y0, w, h, GxEPD_BLACK);
    float scale = halfPx / (vm.mapRangeM > 1 ? vm.mapRangeM : 1);   // px per metre
    const int cl = x0 + 1, ct = y0 + 1, cr = x0 + w - 1, cb = y0 + h - 1;

    // route polyline (clipped to the box) + hut squares (north up)
    int px = 0, py = 0; bool have = false;
    for (int i = 0; i < vm.mapCount; i++) {
        int mx = cx + (int)lroundf(vm.mapE[i] * scale);
        int my = cy - (int)lroundf(vm.mapNo[i] * scale);
        if (have) {
            float ax = px, ay = py, bx = mx, by = my;
            if (clipLine(ax, ay, bx, by, cl, ct, cr, cb))
                drawLine2((int)lroundf(ax), (int)lroundf(ay),
                          (int)lroundf(bx), (int)lroundf(by));
        }
        px = mx; py = my; have = true;
        if (vm.mapHut && vm.mapHut[i] && mx >= cl && mx <= cr && my >= ct && my <= cb)
            epd.fillRect(mx - 2, my - 2, 5, 5, GxEPD_BLACK);
    }

    // destination: a ringed dot if on-screen, else an edge arrow toward it
    int dx = cx + (int)lroundf(vm.mapDestE * scale);
    int dy = cy - (int)lroundf(vm.mapDestN * scale);
    if (dx > x0 && dx < x0 + w && dy > y0 && dy < y0 + h) {
        epd.drawCircle(dx, dy, 6, GxEPD_BLACK);
        epd.drawCircle(dx, dy, 5, GxEPD_BLACK);
        epd.fillCircle(dx, dy, 3, GxEPD_BLACK);
    } else {
        float a = atan2f(vm.mapDestE, vm.mapDestN);     // angle east-of-north
        int ax, ay; polar(cx, cy, a / D2R, halfPx - 5, ax, ay);
        int lx, ly, rx, ry, tx, ty;
        polar(cx, cy, a / D2R, halfPx, tx, ty);
        polar(cx, cy, a / D2R + 145, 6, lx, ly);
        polar(cx, cy, a / D2R - 145, 6, rx, ry);
        epd.fillTriangle(tx, ty, ax + (lx - cx), ay + (ly - cy),
                         ax + (rx - cx), ay + (ry - cy), GxEPD_BLACK);
    }

    // you-are-here: an arrow pointing in the travel direction
    int tx, ty, blx, bly, brx, bry;
    polar(cx, cy, vm.headingDeg,       11, tx, ty);
    polar(cx, cy, vm.headingDeg + 132,  9, blx, bly);
    polar(cx, cy, vm.headingDeg - 132,  9, brx, bry);
    epd.fillTriangle(tx, ty, blx, bly, brx, bry, GxEPD_BLACK);

    // north indicator (top-left of the map)
    epd.fillTriangle(x0 + 7, y0 + 4, x0 + 4, y0 + 10, x0 + 10, y0 + 10, GxEPD_BLACK);
    text(x0 + 13, y0 + 11, "N", F_TINY);

    // scale bar (bottom-left): pick a round distance ~50 px wide
    const float nice[] = {100, 200, 500, 1000, 2000, 5000};
    float barM = nice[0];
    for (float n : nice) if (n * scale <= 60) barM = n;
    int barPx = (int)(barM * scale);
    int by = y0 + h - 6, bx = x0 + 6;
    epd.drawFastHLine(bx, by, barPx, GxEPD_BLACK);
    epd.drawFastVLine(bx, by - 3, 3, GxEPD_BLACK);
    epd.drawFastVLine(bx + barPx, by - 3, 3, GxEPD_BLACK);
    char sb[12];
    if (barM >= 1000) snprintf(sb, sizeof(sb), "%.0f km", barM / 1000);
    else              snprintf(sb, sizeof(sb), "%.0f m", barM);
    text(bx + barPx + 4, by + 2, sb, F_TINY);

    // --- right sidebar: destination + key stats ---
    int xs = x0 + w + 6;
    epd.drawLine(x0 + w + 3, y0, x0 + w + 3, EPD_HEIGHT - 1, GxEPD_BLACK);
    text(xs, 28, vm.nextStopName, F_MDB);

    char num[16]; const char* unit;
    if (vm.remainingKm < 1.0f) { snprintf(num, sizeof(num), "%.0f", vm.remainingKm * 1000); unit = "m"; }
    else                       { snprintf(num, sizeof(num), "%.1f", vm.remainingKm);        unit = "km"; }
    text(xs, 51, num, F_BD);
    text(xs + textW(num, F_BD) + 3, 51, unit, F_MDB);
    epd.drawFastHLine(xs, 58, EPD_WIDTH - xs - 2, GxEPD_BLACK);

    char buf[16];
    snprintf(buf, sizeof(buf), "ETA %d:%02d", vm.etaMin / 60, vm.etaMin % 60);
    text(xs, 78, buf, F_MDB);
    snprintf(buf, sizeof(buf), "climb %.0fm", vm.climbLeftM);
    text(xs, 98, buf, F_MDB);
    snprintf(buf, sizeof(buf), "alt %.0fm", vm.altitudeM);
    text(xs, 118, buf, F_MDB);
}

// --- public -----------------------------------------------------------------
void displayBegin() {
    epd.init(115200, true, 2, false);
    epd.setRotation(EPD_ROTATION);
    u8g2.begin(epd);
}

void displayRender(ScreenState state, const ViewModel& vm) {
    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);
        switch (state) {
            case SCREEN_NAV:      renderNav(vm); break;
            case SCREEN_NO_FIX:   renderNoFix(vm); break;
            case SCREEN_ARRIVED:  renderArrived(vm); break;
            case SCREEN_CONFIG:   renderConfig(vm); break;
            case SCREEN_LOW_BATT: renderLowBatt(vm); break;
            case SCREEN_MAP:      renderMap(vm); break;
        }
    } while (epd.nextPage());
}

void displaySleep() {
    epd.hibernate();
}
