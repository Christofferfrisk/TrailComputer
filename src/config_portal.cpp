#include "config_portal.h"
#include "config.h"
#include "state.h"
#include "route_table.h"
#include "route_spurs.h"
#include "spur_table.h"
#include "route_stages.h"
#include "route_util.h"
#include "geo.h"
#include "power.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <string.h>
#include <stdio.h>

static WebServer server(80);
static DNSServer dns;
static uint32_t lastActivity;
static bool s_done;            // set by /done to exit config mode immediately

static void persist() { statePersist(); }

static int currentSlot() {
    int ref = (g_state->markedWaypointIdx >= 0)
              ? g_state->markedWaypointIdx : g_state->nextWaypointIdx;
    int s = hutSlot(ref);
    return s < 0 ? 0 : s;
}

static String etaStr(int m) {
    return String(m / 60) + "h" + (m % 60 < 10 ? "0" : "") + String(m % 60);
}
static void kv(String& h, const char* k, const String& v) {
    h += "<div class='k'>"; h += k; h += "</div><div class='v'>" + v + "</div>";
}

static String head(const char* title) {
    String h = F("<!doctype html><html lang='en'><head><meta charset='utf-8'>"
                 "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                 "<title>");
    h += title;
    h += F("</title><style>"
           ":root{--bg:#eef1f4;--card:#fff;--ink:#1b2430;--mut:#697483;--line:#e3e7ec;"
           "--accent:#2e7d4f;--accent2:#1d5c39;--warn:#c0392b}"
           "*{box-sizing:border-box}"
           "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
           "background:var(--bg);color:var(--ink);font-size:16px;line-height:1.45}"
           "header{background:var(--accent2);color:#fff;padding:14px 16px;display:flex;"
           "align-items:baseline;gap:9px;position:sticky;top:0;z-index:1}"
           "header h1{font-size:18px;margin:0;font-weight:600;letter-spacing:.01em}"
           "header .sub{font-size:13px;opacity:.82}"
           "header .done{margin-left:auto;align-self:center;background:#fff;color:var(--accent2);"
           "text-decoration:none;font-weight:600;font-size:13px;padding:6px 13px;border-radius:8px}"
           "main{max-width:540px;margin:0 auto;padding:14px}"
           ".card{background:var(--card);border:1px solid var(--line);border-radius:14px;"
           "padding:14px 16px;margin:0 0 14px;box-shadow:0 1px 3px rgba(20,30,40,.05)}"
           ".card h2{font-size:12px;text-transform:uppercase;letter-spacing:.06em;"
           "color:var(--mut);margin:0 0 11px}"
           ".grid{display:grid;grid-template-columns:auto 1fr;gap:7px 14px;align-items:baseline}"
           ".grid .k{color:var(--mut);font-size:14px}"
           ".grid .v{text-align:right;font-variant-numeric:tabular-nums;font-weight:600}"
           ".muted{color:var(--mut);font-size:12px;margin:10px 0 0}"
           ".chip{display:inline-block;background:var(--warn);color:#fff;border-radius:999px;"
           "padding:1px 9px;font-size:12px;font-weight:600;margin-left:6px}"
           "form.row{display:flex;gap:10px;align-items:flex-end;flex-wrap:wrap}"
           ".field{flex:1;min-width:96px}label{display:block;font-size:12px;color:var(--mut);"
           "margin-bottom:4px}input,select{width:100%;font-size:16px;padding:9px 10px;border:1px "
           "solid var(--line);border-radius:9px;background:#fbfcfd}"
           ".dim{opacity:.4}.tag{background:var(--accent2);color:#fff;font-size:10px;font-weight:700;"
           "border-radius:4px;padding:1px 5px;margin-left:6px;letter-spacing:.03em}"
           ".boat{background:#3d6a8a;color:#fff;font-size:10px;border-radius:4px;padding:1px 5px;"
           "margin-left:6px}"
           ".store{font-size:10px;border-radius:4px;padding:1px 5px;margin-left:6px}"
           ".store.big{background:#2e7d4f;color:#fff}"
           ".store.sm{background:#fff;color:#2e7d4f;border:1px solid #2e7d4f}"
           ".amen{font-size:10px;border-radius:4px;padding:1px 5px;margin-left:6px;"
           "background:#eef1f4;color:#444}"
           ".btn{display:inline-block;text-decoration:none;text-align:center;font:600 14px/1 inherit;"
           "padding:10px 13px;border-radius:9px;border:1px solid var(--accent);background:var(--accent);"
           "color:#fff;cursor:pointer}"
           ".btn.sec{background:#fff;color:var(--accent)}.btn.sm{padding:7px 11px;font-size:13px}"
           ".nav{display:flex;gap:9px;margin-bottom:12px}.nav .btn{flex:1}"
           "ul.tl{list-style:none;margin:0;padding:0}"
           "ul.tl li{position:relative;padding:11px 0 11px 42px}"
           "ul.tl li::before{content:'';position:absolute;left:10px;top:0;bottom:0;width:2px;"
           "background:var(--line)}"
           "ul.tl li:first-child::before{top:21px}"
           "ul.tl li:last-child::before{bottom:auto;height:21px}"
           "ul.tl li.done::before{background:var(--accent)}"
           ".node{position:absolute;left:0;top:10px;width:22px;height:22px;border-radius:50%;"
           "background:#fff;border:2px solid var(--mut);color:var(--mut);font:700 11px/1 inherit;"
           "display:flex;align-items:center;justify-content:center}"
           "li.done .node{border-color:var(--accent);color:var(--accent)}"
           "li.cur .node{background:var(--accent);border-color:var(--accent);color:#fff;"
           "box-shadow:0 0 0 3px rgba(46,125,79,.18)}"
           ".body{display:flex;align-items:center;gap:9px}"
           ".name{flex:1;font-weight:600}.cur .name{color:var(--accent2)}"
           ".name small{display:block;font-weight:400;color:var(--mut);font-size:12px;"
           "font-variant-numeric:tabular-nums}.acts{display:flex;gap:6px}"
           "li.here .node{width:16px;height:16px;left:3px;top:13px;background:var(--warn);"
           "border-color:var(--warn);box-shadow:0 0 0 3px rgba(192,57,43,.18)}"
           "li.here::before{background:var(--warn)}li.here .name{color:var(--warn)}"
           ".prog{font-size:13px;color:var(--mut);margin:-2px 0 12px}.prog b{color:var(--ink)}"
           "small .ahead{color:var(--accent);font-weight:600}small .behind{color:var(--mut)}"
           "li.sbranch{padding:1px 0 1px 64px;position:relative}"
           "li.sbranch::before{content:'';position:absolute;left:10px;top:11px;width:24px;height:0;"
           "border-top:2px dashed var(--mut);background:none}"
           "li.sbranch .name{color:var(--mut);font-style:italic;font-weight:600}"
           "li.spur{padding:9px 0 9px 64px;position:relative}"
           "li.spur::before{content:'';position:absolute;left:33px;top:0;bottom:0;width:0;"
           "border-left:2px dashed var(--mut);background:none}"
           "li.spur .node{position:absolute;left:27px;top:13px;width:11px;height:11px;background:#fff;"
           "border:2px solid var(--mut);border-radius:2px;transform:rotate(45deg)}"
           "li.spur .name{font-weight:600}"
           "</style></head><body>"
           "<header><h1>Trail Computer</h1><span class='sub'>Kungsleden</span>"
           "<a class='done' href='/done'>Done &amp; sleep</a></header><main>");
    return h;
}
static const char* foot() { return "</main></body></html>"; }

static void appendStatus(String& h) {
    const NavSnapshot& s = g_state->lastNav;
    h += F("<section class='card'><h2>Status</h2><div class='grid'>");
    kv(h, "Time (UTC)", String(s.clock));
    kv(h, "Battery", String(s.batteryPct) + "%");
    kv(h, "Satellites", String(s.satCount));
    kv(h, "Pressure", String(s.pressureHpa, 0) + " hPa" +
       (s.weatherTurning ? F("<span class='chip'>falling</span>") : String("")));
    if (s.hasFix) {
        kv(h, "Position", String(s.lat, 5) + ", " + String(s.lon, 5));
        kv(h, "Altitude", String(s.altM, 0) + " m");
        kv(h, "Next stop", String(hutName(s.destRouteIdx)));
        kv(h, "Remaining", String(s.remainingKm, 1) + " km");
        kv(h, "Bearing", String(s.bearingDeg, 0) + "&deg;");
        kv(h, "Climb left", String(s.climbLeftM, 0) + " m");
        kv(h, "ETA", etaStr(s.etaMin));
    } else {
        kv(h, "Position", F("no fix yet"));
    }
    kv(h, "Trip climb", "&uarr;" + String(g_state->cumAscentM, 0) +
       " &darr;" + String(g_state->cumDescentM, 0) + " m");
    kv(h, "Energy", String(remainingMah(g_state->consumedMah), 0) + " mAh, ~" +
       String(estimatedChecksLeft(g_state->consumedMah, g_state->bootCount)) + " checks");
    h += F("</div><p class='muted'>Fresh read taken on entering config.</p></section>");
}

static bool hikeActive();
static int  slotByName(const char* name);

static void appendProfile(String& h) {
    int iStart = 0, iEnd = ROUTE_N - 1;
    if (hikeActive()) {
        int sc = g_state->hikeStartCode, ec = g_state->hikeEndCode;
        int ss = (sc < 1000) ? sc : slotByName(SPURS[(sc - 1000) / 10].afterHut);
        int es = (ec < 1000) ? ec : slotByName(SPURS[(ec - 1000) / 10].afterHut);
        if (ss >= 0 && es >= 0) { iStart = ROUTE_HUTS[ss]; iEnd = ROUTE_HUTS[es]; }
    }
    if (iEnd <= iStart) { iStart = 0; iEnd = ROUTE_N - 1; }

    float e0 = 1e9f, e1 = -1e9f;
    for (int i = iStart; i <= iEnd; i++) { e0 = min(e0, ROUTE[i].eleM); e1 = max(e1, ROUTE[i].eleM); }
    if (e1 - e0 < 1) e1 = e0 + 1;
    float km0 = ROUTE[iStart].cumDistM / 1000.0f, km1 = ROUTE[iEnd].cumDistM / 1000.0f;
    if (km1 - km0 < 0.1f) km1 = km0 + 0.1f;
    float asc  = ROUTE[iEnd].cumAscM  - ROUTE[iStart].cumAscM;   // full-res totals
    float desc = ROUTE[iEnd].cumDescM - ROUTE[iStart].cumDescM;

    const char* startName = ""; const char* endName = "";
    int startK = -1, endK = -1;
    for (int k = 0; k < ROUTE_HUTS_N; k++) {
        int ri = ROUTE_HUTS[k];
        if (ri >= iStart && ri <= iEnd) {
            if (!startName[0]) { startName = ROUTE_HUT_NAMES[k]; startK = k; }
            endName = ROUTE_HUT_NAMES[k]; endK = k;
        }
    }

    const float W = 520, pL = 8, pR = 8, pT = 14, pB = 46;
    const float H = 190, plotW = W - pL - pR, plotH = H - pT - pB, base = H - pB;

    String pts;
    for (int i = iStart; i <= iEnd; i++) {
        float x = pL + (ROUTE[i].cumDistM / 1000.0f - km0) / (km1 - km0) * plotW;
        float y = pT + (1.0f - (ROUTE[i].eleM - e0) / (e1 - e0)) * plotH;
        pts += String(x, 1) + "," + String(y, 1) + " ";
    }
    float xN = pL + plotW;

    h += F("<section class='card'><h2>Elevation profile</h2><p class='prog'><b>");
    h += String(startName) + "</b> &rarr; <b>" + String(endName) + "</b> &middot; " +
         String(km1 - km0, 0) + " km &middot; &uarr;" + String(asc, 0) + " &darr;" +
         String(desc, 0) + " m</p>";
    h += F("<svg viewBox='0 0 520 190' style='width:100%;height:auto;display:block'>");
    h += "<path d='M " + String(pL, 1) + "," + String(base, 1) + " L " + pts + "L " +
         String(xN, 1) + "," + String(base, 1) + " Z' fill='#d8e6dd'/>";
    h += "<polyline points='" + pts + "' fill='none' stroke='#2e7d4f' stroke-width='1.5'/>";
    h += "<line x1='" + String(pL, 1) + "' y1='" + String(base, 1) + "' x2='" + String(xN, 1) +
         "' y2='" + String(base, 1) + "' stroke='#c7ccd2'/>";
    h += "<text x='2' y='" + String(pT + 6, 1) + "' font-size='9' fill='#697483'>" +
         String(e1, 0) + " m</text>";
    h += "<text x='2' y='" + String(base, 1) + "' font-size='9' fill='#697483'>" +
         String(e0, 0) + " m</text>";

    for (int k = 0; k < ROUTE_HUTS_N; k++) {
        int ri = ROUTE_HUTS[k];
        if (ri < iStart || ri > iEnd) continue;
        float x = pL + (ROUTE[ri].cumDistM / 1000.0f - km0) / (km1 - km0) * plotW;
        float y = pT + (1.0f - (ROUTE[ri].eleM - e0) / (e1 - e0)) * plotH;
        bool endpt = (k == startK || k == endK);
        h += "<line x1='" + String(x, 1) + "' y1='" + String(pT, 1) + "' x2='" + String(x, 1) +
             "' y2='" + String(base, 1) + "' stroke='" + (endpt ? "#9aa3ad" : "#e7ebef") + "'/>";
        h += "<circle cx='" + String(x, 1) + "' cy='" + String(y, 1) + "' r='" +
             (endpt ? "3.5" : "2") + "' fill='#1d5c39'/>";
        if (!endpt)
            h += "<text x='" + String(x, 1) + "' y='" + String(base + 5, 1) +
                 "' font-size='8' fill='#697483' text-anchor='end' transform='rotate(-45 " +
                 String(x, 1) + " " + String(base + 5, 1) + ")'>" + String(ROUTE_HUT_NAMES[k]) + "</text>";
    }

    // bold, horizontal labels make the two ends unambiguous
    h += "<text x='" + String(pL, 1) + "' y='" + String(base + 16, 1) +
         "' font-size='11' font-weight='bold' fill='#1b2430' text-anchor='start'>&#9650; " +
         String(startName) + "</text>";
    h += "<text x='" + String(xN, 1) + "' y='" + String(base + 16, 1) +
         "' font-size='11' font-weight='bold' fill='#1b2430' text-anchor='end'>" +
         String(endName) + " &#9650;</text>";

    const NavSnapshot& nv = g_state->lastNav;
    if (nv.hasFix) {
        SnapResult sp = routeSnap(ROUTE, ROUTE_N, nv.lat, nv.lon);
        SnapResult ssp = routeSnap(SPUR_ROUTE, SPUR_N, nv.lat, nv.lon);
        float cur = (ROUTE[sp.segStart].cumDistM + sp.alongM) / 1000.0f;
        if (ssp.lateralM < sp.lateralM) cur = -1.0f;   // on the spur: no main-line red dot
        if (cur >= km0 && cur <= km1) {
            float x = pL + (cur - km0) / (km1 - km0) * plotW;
            h += "<line x1='" + String(x, 1) + "' y1='" + String(pT, 1) + "' x2='" + String(x, 1) +
                 "' y2='" + String(base, 1) + "' stroke='#c0392b' stroke-width='1.5'/>";
            h += "<circle cx='" + String(x, 1) + "' cy='" + String(base, 1) + "' r='3' fill='#c0392b'/>";
        }
    }
    h += F("</svg></section>");
}

// A small line chart of a rolling history array (oldest..newest, left..right).
static void historyChart(String& h, const char* label, const float* data, int n,
                         int dec, const char* unit, const char* color) {
    h += "<p class='prog' style='margin:8px 0 4px'><b>";
    h += label;
    h += "</b>";
    if (n >= 1) { h += " &middot; now " + String(data[n - 1], dec) + unit; }
    h += "</p>";
    if (n < 2) { h += F("<p class='muted'>Collecting samples&hellip;</p>"); return; }

    float lo = data[0], hi = data[0];
    for (int i = 1; i < n; i++) { if (data[i] < lo) lo = data[i]; if (data[i] > hi) hi = data[i]; }
    if (hi - lo < 1.0f) { float m = (hi + lo) / 2; lo = m - 0.5f; hi = m + 0.5f; }

    const float pL = 36, pR = 8, pT = 8, pB = 8;
    const float plotW = 520 - pL - pR, plotH = 120 - pT - pB, base = 120 - pB;
    String pts;
    for (int i = 0; i < n; i++) {
        float x = pL + (float)i / (n - 1) * plotW;
        float y = pT + (1.0f - (data[i] - lo) / (hi - lo)) * plotH;
        pts += String(x, 1) + "," + String(y, 1) + " ";
    }
    float lx = pL + plotW, ly = pT + (1.0f - (data[n - 1] - lo) / (hi - lo)) * plotH;

    h += F("<svg viewBox='0 0 520 120' style='width:100%;height:auto;display:block'>");
    h += "<line x1='" + String(pL, 1) + "' y1='" + String(base, 1) + "' x2='" +
         String(pL + plotW, 1) + "' y2='" + String(base, 1) + "' stroke='#c7ccd2'/>";
    h += "<polyline points='" + pts + "' fill='none' stroke='" + color + "' stroke-width='1.6'/>";
    h += "<circle cx='" + String(lx, 1) + "' cy='" + String(ly, 1) + "' r='2.6' fill='" + color + "'/>";
    h += "<text x='2' y='" + String(pT + 7, 1) + "' font-size='9' fill='#697483'>" +
         String(hi, dec) + unit + "</text>";
    h += "<text x='2' y='" + String(base, 1) + "' font-size='9' fill='#697483'>" +
         String(lo, dec) + unit + "</text>";
    h += F("</svg>");
}

static void appendConditions(String& h) {
    int n = g_state->pressHistN;
    h += F("<section class='card'><h2>Conditions</h2>");
    historyChart(h, "Temperature", g_state->tempHist, n, 1, "&deg;C", "#c0392b");
    historyChart(h, "Humidity",    g_state->humHist,  n, 0, "%",      "#3d6a8a");
    h += F("</section>");
}

static void appendSetPos(String& h) {
    h += F("<section class='card'><h2>Test a position</h2>"
           "<form class='row' action='/setpos'>"
           "<div class='field'><label>Latitude</label>"
           "<input name='lat' inputmode='decimal' placeholder='67.90'></div>"
           "<div class='field'><label>Longitude</label>"
           "<input name='lon' inputmode='decimal' placeholder='18.29'></div>"
           "<button class='btn' type='submit'>Compute</button></form>"
           "<p class='muted'>Runs the route math against a typed point &mdash; no GPS needed.</p>"
           "</section>");
}

static void emitHere(String& h, float curKm, const char* nextName, float toNextKm, float frac) {
    int pt = 4 + (int)(frac * 26);          // nudge the dot down within the leg
    int pb = 4 + (int)((1.0f - frac) * 26);
    h += "<li class='here' style='padding-top:" + String(pt) + "px;padding-bottom:" +
         String(pb) + "px'><span class='node'></span><div class='body'>"
         "<div class='name'>You are here<small>";
    h += String(curKm, 1) + " km along route";
    if (nextName) h += " &middot; " + String(toNextKm, 1) + " km to " + String(nextName);
    h += F("</small></div></div></li>");
}

// Stop codes: main hut -> slot k (0..N-1); spur i stop j -> 1000 + i*10 + j.
static int slotByName(const char* name) {
    for (int k = 0; k < ROUTE_HUTS_N; k++)
        if (strcmp(ROUTE_HUT_NAMES[k], name) == 0) return k;
    return -1;
}
static float stopOrder(int code) {           // travel-order position (N->S)
    if (code < 0) return -1;
    if (code < 1000) return (float)code;
    int i = (code - 1000) / 10, j = (code - 1000) % 10;
    int a = slotByName(SPURS[i].afterHut);
    return (a + 1) - 0.1f * (j + 1);          // farther spur stop sorts earlier (inbound)
}
static String codeName(int code) {
    if (code < 0) return String("");
    if (code < 1000) return String(ROUTE_HUT_NAMES[code]);
    int i = (code - 1000) / 10, j = (code - 1000) % 10;
    return String(SPURS[i].stops[j].name);
}
static bool hikeActive() {
    return g_state->hikeStartCode >= 0 && g_state->hikeEndCode >= 0;
}
static bool inHike(int code) {
    if (!hikeActive()) return true;
    float o = stopOrder(code);
    return o >= stopOrder(g_state->hikeStartCode) - 0.001f &&
           o <= stopOrder(g_state->hikeEndCode)   + 0.001f;
}
static String hikeTag(int code) {
    if (code == g_state->hikeStartCode) return F("<span class='tag'>START</span>");
    if (code == g_state->hikeEndCode)   return F("<span class='tag'>END</span>");
    return String("");
}

static void hikeOptions(String& h, int sel) {
    for (int k = 0; k < ROUTE_HUTS_N; k++) {
        h += "<option value='" + String(k) + "'" + (sel == k ? " selected" : "") + ">" +
             String(ROUTE_HUT_NAMES[k]) + "</option>";
        for (int i = 0; i < SPURS_N; i++)
            if (strcmp(SPURS[i].afterHut, ROUTE_HUT_NAMES[k]) == 0)
                for (int j = 0; j < SPURS[i].n; j++) {
                    int code = 1000 + i * 10 + j;
                    h += "<option value='" + String(code) + "'" +
                         (sel == code ? " selected" : "") + ">&nbsp;&nbsp;&#8627; " +
                         String(SPURS[i].stops[j].name) + "</option>";
                }
    }
}

static void appendHike(String& h) {
    h += F("<section class='card'><h2>This hike</h2>");
    if (hikeActive()) {
        int sc = g_state->hikeStartCode, ec = g_state->hikeEndCode;
        int mainStart = (sc < 1000) ? sc : slotByName(SPURS[(sc - 1000) / 10].afterHut) + 1;
        int mainEnd   = (ec < 1000) ? ec : slotByName(SPURS[(ec - 1000) / 10].afterHut);
        float mainKm = (ROUTE[ROUTE_HUTS[mainEnd]].cumDistM -
                        ROUTE[ROUTE_HUTS[mainStart]].cumDistM) / 1000.0f;
        if (mainKm < 0) mainKm = -mainKm;
        float spurKm = 0;
        if (sc >= 1000) {
            int i = (sc - 1000) / 10, j = (sc - 1000) % 10;
            for (int x = 0; x <= j; x++) spurKm += SPURS[i].stops[x].legKm;
        }
        h += "<p class='prog'><b>" + codeName(sc) + "</b> &rarr; <b>" + codeName(ec) +
             "</b> &middot; &#8776; " + String(mainKm + spurKm, 0) + " km";
        if (spurKm > 0) h += " <span class='muted'>(incl. ~" + String(spurKm, 0) +
                             " km approach)</span>";
        h += F("</p>");
    }
    h += F("<form class='row' action='/hike'>"
           "<div class='field'><label>Start</label><select name='start'>");
    hikeOptions(h, g_state->hikeStartCode);
    h += F("</select></div><div class='field'><label>End</label><select name='end'>");
    hikeOptions(h, g_state->hikeEndCode);
    h += F("</select></div><button class='btn' type='submit'>Save</button></form>"
           "<p class='muted'><a href='/hike?start=-1&amp;end=-1'>Use full route</a> "
           "&middot; the timeline highlights only this section.</p></section>");
}

static String storeChip(const char* name) {
    uint8_t lv = hutStore(name);
    if (lv == 2) return F("<span class='store big'>&#128722; shop</span>");
    if (lv == 1) return F("<span class='store sm'>&#128722; small</span>");
    return String("");
}
static String amenChips(const char* name) {
    String s;
    if (hutSauna(name))   s += F("<span class='amen'>&#129494; bastu</span>");
    if (hutStation(name)) s += F("<span class='amen'>&#127869; station</span>");
    return s;
}
static String transportMeta(const char* name) {
    const char* tr = hutTransport(name);
    return tr ? (" &middot; &#128652; " + String(tr)) : String("");
}

// Set per page-render: position along the spur (m) when the last fix snapped
// closer to the spur than to the main line; -1 otherwise.
static float s_spurHereM = -1.0f;

static void emitSpurs(String& h, const char* afterHut) {
    for (int i = 0; i < SPURS_N; i++) {
        if (strcmp(SPURS[i].afterHut, afterHut) != 0) continue;
        const Spur& sp = SPURS[i];

        if (s_spurHereM >= 0) {
            float toSingi = (SPUR_ROUTE[SPUR_N - 1].cumDistM - s_spurHereM) / 1000.0f;
            h += "<li class='here'><span class='node'></span><div class='body'>"
                 "<div class='name'>You are here &mdash; on the Kebnekaise trail<small>" +
                 String(s_spurHereM / 1000.0f, 1) + " km from Nikkaluokta &middot; " +
                 String(toSingi, 1) + " km to Singi</small></div></div></li>";
        }

        // Inbound (far end first) when the hike starts on this spur, else outbound.
        bool inbound = g_state->hikeStartCode >= 1000 &&
                       (g_state->hikeStartCode - 1000) / 10 == i;

        h += "<li class='sbranch'><div class='body'><div class='name'>&#8627; " +
             String(sp.junction) + " junction</div></div></li>";

        for (int t = 0; t < sp.n; t++) {
            int j = inbound ? (sp.n - 1 - t) : t;
            int code = 1000 + i * 10 + j;
            const char* nbr = (j == 0) ? sp.junction : sp.stops[j - 1].name;
            h += inHike(code) ? F("<li class='spur'>") : F("<li class='spur dim'>");
            h += F("<span class='node'></span><div class='body'><div class='name'>");
            h += String(sp.stops[j].name) + hikeTag(code) + storeChip(sp.stops[j].name) +
                 amenChips(sp.stops[j].name) + "<small>" +
                 String(sp.stops[j].legKm, 0) + " km " + (inbound ? "to " : "from ") + String(nbr);
            if (sp.stops[j].time[0]) h += " &middot; " + String(sp.stops[j].time);
            if (sp.stops[j].note[0]) h += " &middot; " + String(sp.stops[j].note);
            h += transportMeta(sp.stops[j].name);
            h += F("</small></div></div></li>");
        }
    }
}

static void appendDestinations(String& h) {
    int cur = currentSlot();

    // Current distance along the route, by snapping the last fix. If the fix
    // is closer to the Kebnekaise spur, mark position there instead.
    float curM = -1.0f;
    s_spurHereM = -1.0f;
    const NavSnapshot& nv = g_state->lastNav;
    if (nv.hasFix) {
        SnapResult sm = routeSnap(ROUTE, ROUTE_N, nv.lat, nv.lon);
        SnapResult sp = routeSnap(SPUR_ROUTE, SPUR_N, nv.lat, nv.lon);
        if (sp.lateralM < sm.lateralM)
            s_spurHereM = SPUR_ROUTE[sp.segStart].cumDistM + sp.alongM;
        else
            curM = ROUTE[sm.segStart].cumDistM + sm.alongM;
    }
    float totalKm = ROUTE[ROUTE_N - 1].cumDistM / 1000.0f;

    h += F("<section class='card'><h2>Route &amp; destination</h2><p class='prog'>");
    if (s_spurHereM >= 0) {
        float toSingi = (SPUR_ROUTE[SPUR_N - 1].cumDistM - s_spurHereM) / 1000.0f;
        h += "On the <b>Kebnekaise trail</b> &middot; Singi junction in <b>" +
             String(toSingi, 1) + " km</b>";
    } else if (curM >= 0) {
        float rem = (ROUTE[ROUTE_HUTS[cur]].cumDistM - curM) / 1000.0f;
        if (rem < 0) rem = -rem;
        h += "<b>" + String(curM / 1000.0f, 0) + " km</b> of " + String(totalKm, 0) +
             " km &middot; " + String(ROUTE_HUT_NAMES[cur]) + " in <b>" + String(rem, 1) + " km</b>";
    } else {
        h += String(ROUTE_HUTS_N) + " stops &middot; " + String(totalKm, 0) +
             " km &middot; target <b>" + String(ROUTE_HUT_NAMES[cur]) + "</b>";
    }
    h += F("</p><div class='nav'><a class='btn sec' href='/prev'>&uarr; Previous</a>"
           "<a class='btn sec' href='/next'>Next &darr;</a></div><ul class='tl'>");

    bool placed = false;
    for (int k = 0; k < ROUTE_HUTS_N; k++) {
        float cumKm  = ROUTE[ROUTE_HUTS[k]].cumDistM / 1000.0f;
        float prevKm = (k > 0) ? ROUTE[ROUTE_HUTS[k - 1]].cumDistM / 1000.0f : 0.0f;
        float legKm  = cumKm - prevKm;

        if (!placed && curM >= 0 && curM <= ROUTE[ROUTE_HUTS[k]].cumDistM) {
            float f = (cumKm > prevKm) ? (curM / 1000.0f - prevKm) / (cumKm - prevKm) : 0.5f;
            f = f < 0 ? 0 : (f > 1 ? 1 : f);
            emitHere(h, curM / 1000.0f, ROUTE_HUT_NAMES[k], cumKm - curM / 1000.0f, f);
            placed = true;
        }

        String cls = (k == cur) ? "cur" : (k < cur ? "done" : "");
        if (!inHike(k)) cls += " dim";
        h += "<li class='" + cls + "'>";
        const Stage* st = findStage(ROUTE_HUT_NAMES[k]);
        h += "<span class='node'>" + String(k + 1) + "</span>";
        h += "<div class='body'><div class='name'>" + String(ROUTE_HUT_NAMES[k]) + hikeTag(k);
        if (st && st->boat) h += F("<span class='boat'>&#9972; boat</span>");
        h += storeChip(ROUTE_HUT_NAMES[k]) + amenChips(ROUTE_HUT_NAMES[k]);
        h += "<small>";
        if (k == cur) h += F("target &middot; ");
        if (st) h += String(st->km, 0) + " km &middot; " + String(st->time);
        else    h += String(legKm, 1) + " km leg";
        h += " &middot; " + String(cumKm, 0) + " km";
        if (curM >= 0) {
            float rel = cumKm - curM / 1000.0f;
            if (rel >= 0) h += " &middot; <span class='ahead'>+" + String(rel, 0) + " km</span>";
            else          h += " &middot; <span class='behind'>" + String(rel, 0) + " km</span>";
        }
        h += transportMeta(ROUTE_HUT_NAMES[k]);
        h += F("</small></div>");
        h += "<div class='acts'>"
             "<a class='btn sm' href='/set?h=" + String(k) + "'>Set</a>"
             "<a class='btn sm sec' href='/startday?h=" + String(k) + "'>Start day</a></div>";
        h += "</div></li>";
        emitSpurs(h, ROUTE_HUT_NAMES[k]);
    }
    if (!placed && curM >= 0) emitHere(h, curM / 1000.0f, nullptr, 0, 1.0f);
    h += F("</ul></section>");
}

static float parseHours(const char* s) {
    int a = 0, b = 0;
    if (sscanf(s, "%d-%d", &a, &b) == 2) return (a + b) / 2.0f;
    if (sscanf(s, "%d", &a) == 1) return (float)a;
    return 0.0f;
}

static void dayRow(String& h, int num, const String& from, const String& to,
                   float km, float hrs, bool boat) {
    h += "<div style='display:flex;align-items:center;gap:9px;padding:9px 0;"
         "border-top:1px solid var(--line)'><span class='tag'>Day " + String(num) +
         "</span><div style='flex:1'><b>" + from + "</b> &rarr; <b>" + to +
         "</b><small style='display:block;color:var(--mut)'>" + String(km, 0) +
         " km &middot; ~" + String(hrs, 0) + " h" + (boat ? " &middot; &#9972;" : "") +
         "</small></div></div>";
}

static void appendPlanner(String& h) {
    h += F("<section class='card'><h2>Day planner</h2>");
    if (!hikeActive()) {
        h += F("<p class='muted'>Set a hike Start &amp; End above to plan daily stages.</p></section>");
        return;
    }
    int sc = g_state->hikeStartCode, ec = g_state->hikeEndCode;

    struct PLeg { const char* to; float km; float hrs; bool boat; };
    PLeg legs[40]; int n = 0;
    String startName;
    int mainStartSlot;
    if (sc >= 1000) {
        int i = (sc - 1000) / 10, jS = (sc - 1000) % 10;
        startName = SPURS[i].stops[jS].name;
        for (int j = jS; j >= 0 && n < 40; j--) {
            const char* to = (j > 0) ? SPURS[i].stops[j - 1].name : SPURS[i].junction;
            legs[n++] = { to, SPURS[i].stops[j].legKm, parseHours(SPURS[i].stops[j].time), false };
        }
        mainStartSlot = slotByName(SPURS[i].afterHut);
    } else {
        startName = ROUTE_HUT_NAMES[sc];
        mainStartSlot = sc;
    }
    int endSlot = (ec < 1000) ? ec : slotByName(SPURS[(ec - 1000) / 10].afterHut);
    for (int k = mainStartSlot + 1; k <= endSlot && n < 40; k++) {
        const Stage* st = findStage(ROUTE_HUT_NAMES[k]);
        float km  = st ? st->km : (ROUTE[ROUTE_HUTS[k]].cumDistM - ROUTE[ROUTE_HUTS[k - 1]].cumDistM) / 1000.0f;
        float asc = ROUTE[ROUTE_HUTS[k]].cumAscM - ROUTE[ROUTE_HUTS[k - 1]].cumAscM;
        float hrs = st ? parseHours(st->time) : (km / NAISMITH_SPEED_KMH + (asc > 0 ? asc : 0) / 600.0f);
        legs[n++] = { ROUTE_HUT_NAMES[k], km, hrs, st ? st->boat : false };
    }

    float tgt = g_state->settings.dayTargetH > 0 ? g_state->settings.dayTargetH : 6;
    h += "<form class='row' action='/settings' style='margin-bottom:10px'>"
         "<div class='field'><label>Target h/day</label><input name='dayh' inputmode='numeric' value='" +
         String((int)tgt) + "'></div><button class='btn' type='submit'>Update</button></form>";

    String from = startName, prevTo = startName;
    float dkm = 0, dhrs = 0; bool dboat = false; int cnt = 0, dayNum = 0;
    for (int x = 0; x < n; x++) {
        if (cnt > 0 && dhrs + legs[x].hrs > tgt) {
            dayRow(h, ++dayNum, from, prevTo, dkm, dhrs, dboat);
            from = prevTo; dkm = dhrs = 0; dboat = false; cnt = 0;
        }
        dkm += legs[x].km; dhrs += legs[x].hrs; dboat |= legs[x].boat; cnt++;
        prevTo = String(legs[x].to);
    }
    if (cnt > 0) dayRow(h, ++dayNum, from, prevTo, dkm, dhrs, dboat);
    h += "<p class='muted'>" + String(dayNum) + " days at ~" + String((int)tgt) +
         " h/day &middot; stages from STF.</p></section>";
}

static void appendHistory(String& h) {
    h += F("<section class='card'><h2>Trip history</h2>");
    if (g_state->tripDayCount == 0) {
        h += F("<p class='muted'>No trip data yet &mdash; days log automatically once you're "
               "walking with a GPS fix.</p></section>");
        return;
    }
    float totKm = 0; int totAsc = 0;
    h += F("<div class='grid'>");
    for (int i = 0; i < g_state->tripDayCount; i++) {
        const DayLog& d = g_state->tripDays[i];
        float km = d.km10 / 10.0f; totKm += km; totAsc += d.ascM;
        h += "<div class='k'>Day " + String(i + 1) + " (" + String(d.day) + "/" + String(d.mon) +
             ")</div><div class='v'>" + String(km, 1) + " km &middot; &uarr;" + String(d.ascM) + " m</div>";
    }
    h += "<div class='k'><b>Total</b></div><div class='v'><b>" + String(totKm, 1) +
         " km &middot; &uarr;" + String(totAsc) + " m</b></div>";
    h += F("</div></section>");
}

static void appendSettings(String& h) {
    const Settings& s = g_state->settings;
    h += F("<section class='card'><h2>Settings</h2><form action='/settings'><div class='row'>");
    h += "<div class='field'><label>Battery (mAh)</label><input name='cap' inputmode='numeric' value='" +
         String(s.batCapacityMah, 0) + "'></div>";
    h += "<div class='field'><label>Usable %</label><input name='frac' inputmode='numeric' value='" +
         String(s.batUsableFrac * 100, 0) + "'></div></div><div class='row' style='margin-top:8px'>";
    h += "<div class='field'><label>Temp offset &deg;C</label><input name='toff' inputmode='decimal' value='" +
         String(s.bmeTempOffsetC, 1) + "'></div>";
    h += "<div class='field'><label>Day hours</label><input name='dayh' inputmode='numeric' value='" +
         String(s.dayTargetH) + "'></div>";
    h += "<div class='field'><label>Nav screen</label><select name='map'><option value='0'" +
         String(s.mapMode ? "" : " selected") + ">Compass</option><option value='1'" +
         String(s.mapMode ? " selected" : "") + ">Map</option></select></div></div>";
    h += F("<p style='margin:10px 0 0'><button class='btn' type='submit'>Save settings</button></p>"
           "</form></section>");
}

static void sendHome() {
    String h = head("Trail Computer");
    appendStatus(h);
    appendHike(h);
    appendPlanner(h);
    appendProfile(h);
    appendConditions(h);
    appendDestinations(h);
    appendHistory(h);
    appendSettings(h);
    appendSetPos(h);
    h += foot();
    server.send(200, "text/html; charset=utf-8", h);
}

static void sendSetPos() {
    float lat = server.arg("lat").toFloat();
    float lon = server.arg("lon").toFloat();
    int dest = (g_state->markedWaypointIdx >= 0)
               ? g_state->markedWaypointIdx : g_state->nextWaypointIdx;

    SnapResult s = routeSnap(ROUTE, ROUTE_N, lat, lon);
    float remM = remainingToM(ROUTE, ROUTE_N, s, dest);
    int aheadIdx = min(s.segStart + 1, ROUTE_N - 1);
    float brg = bearingDeg(lat, lon, ROUTE[aheadIdx].lat, ROUTE[aheadIdx].lon);
    float climb = remainingAscentM(s.segStart, dest);
    int eta = naismithMin(remM, climb);

    String h = head("Computed");
    h += F("<section class='card'><h2>Computed for typed position</h2><div class='grid'>");
    kv(h, "Input", String(lat, 5) + ", " + String(lon, 5));
    kv(h, "Off route", String(s.lateralM, 0) + " m");
    kv(h, "Next stop", String(hutName(dest)));
    kv(h, "Remaining", String(remM / 1000.0f, 1) + " km");
    kv(h, "Bearing", String(brg, 0) + "&deg;");
    kv(h, "Climb left", String(climb, 0) + " m");
    kv(h, "ETA", etaStr(eta));
    h += F("</div><p style='margin:12px 0 0'><a class='btn sec' href='/'>&larr; Back</a></p></section>");
    h += foot();
    server.send(200, "text/html; charset=utf-8", h);
}

static void touch() { lastActivity = millis(); }

void runConfigPortal(uint32_t timeoutMs) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("TrailComputer");

    // Captive portal: answer every DNS query with our own IP, and redirect any
    // unknown URL (the phone's connectivity probe) to the portal page so the
    // "sign in to network" sheet pops up automatically.
    dns.start(53, "*", WiFi.softAPIP());
    server.onNotFound([]() {
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302);
    });

    server.on("/", []() { touch(); sendHome(); });
    server.on("/setpos", []() { touch(); sendSetPos(); });
    server.on("/done", []() {
        touch();
        server.send(200, "text/html; charset=utf-8",
                    F("<!doctype html><meta charset='utf-8'>"
                      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                      "<body style='font-family:sans-serif;text-align:center;margin-top:3em;color:#1b2430'>"
                      "<h2>Sleeping&hellip;</h2><p>The trail computer is going back to sleep.<br>"
                      "You can close this tab.</p></body>"));
        s_done = true;            // break the portal loop after this response
    });
    server.on("/settings", []() {
        touch();
        Settings& s = g_state->settings;
        if (server.hasArg("cap"))  s.batCapacityMah = server.arg("cap").toFloat();
        if (server.hasArg("frac")) s.batUsableFrac  = server.arg("frac").toFloat() / 100.0f;
        if (server.hasArg("toff")) s.bmeTempOffsetC = server.arg("toff").toFloat();
        if (server.hasArg("dayh")) s.dayTargetH     = server.arg("dayh").toInt();
        if (server.hasArg("map"))  s.mapMode        = server.arg("map").toInt();
        persist();
        server.sendHeader("Location", "/"); server.send(303);
    });
    server.on("/hike", []() {
        touch();
        if (server.hasArg("start")) g_state->hikeStartCode = server.arg("start").toInt();
        if (server.hasArg("end"))   g_state->hikeEndCode   = server.arg("end").toInt();
        if (hikeActive() &&
            stopOrder(g_state->hikeStartCode) > stopOrder(g_state->hikeEndCode)) {
            int t = g_state->hikeStartCode;
            g_state->hikeStartCode = g_state->hikeEndCode;
            g_state->hikeEndCode = t;
        }
        persist();
        server.sendHeader("Location", "/"); server.send(303);
    });
    server.on("/set", []() {
        touch();
        int k = server.arg("h").toInt();
        if (k >= 0 && k < ROUTE_HUTS_N) g_state->markedWaypointIdx = ROUTE_HUTS[k];
        persist();
        server.sendHeader("Location", "/"); server.send(303);
    });
    server.on("/startday", []() {
        touch();
        int k = server.arg("h").toInt();
        if (k >= 0 && k < ROUTE_HUTS_N) {
            g_state->nextWaypointIdx = ROUTE_HUTS[k];
            g_state->markedWaypointIdx = -1;
        }
        persist();
        server.sendHeader("Location", "/"); server.send(303);
    });
    server.on("/next", []() {
        touch();
        int k = min(currentSlot() + 1, ROUTE_HUTS_N - 1);
        g_state->markedWaypointIdx = ROUTE_HUTS[k];
        persist();
        server.sendHeader("Location", "/"); server.send(303);
    });
    server.on("/prev", []() {
        touch();
        int k = max(currentSlot() - 1, 0);
        g_state->markedWaypointIdx = ROUTE_HUTS[k];
        persist();
        server.sendHeader("Location", "/"); server.send(303);
    });
    server.begin();

    s_done = false;
    lastActivity = millis();
    while (!s_done && millis() - lastActivity < timeoutMs) {
        dns.processNextRequest();
        server.handleClient();
        delay(10);
    }
    if (s_done) delay(250);      // let the "Sleeping..." response flush before radio off

    dns.stop();
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);     // radio is on-demand only; never left running
}
