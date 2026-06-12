#pragma once

// Side trails that branch off the main route polyline. These are NOT in the GPX
// (they leave the main line), so they're shown as dashed branches in the
// timeline with approximate leg distances rather than snapped inline stops.
// Coordinates are real; leg distances are approximate -> VERIFY.

struct SpurStop {
    const char* name;
    float lat, lon;
    float legKm;        // distance from the previous point (junction, then prior stop)
    const char* time;   // STF walking time for that leg
    const char* note;
};

struct Spur {
    const char* afterHut;   // attaches after this main-route hut (travel order)
    const char* junction;   // junction name shown in the branch header
    const SpurStop* stops;
    int n;
};

// Kebnekaise spur: leaves the Kungsleden at the Singi junction; Nikkaluokta is
// the road/bus exit. Distances/times are the official STF étappe figures.
static const SpurStop KEB_STOPS[] = {
    {"Kebnekaise Fjällstation", 67.8671f,    18.6194f,    15.0f, "4-6 h", ""},               // Singi<->Keb 15 km
    {"Nikkaluokta",             67.8508353f, 19.0137288f, 19.0f, "5-7 h", "road / bus exit"} // Keb<->Nikka 19 km
};

static const Spur SPURS[] = {
    {"Singi", "Singi", KEB_STOPS, 2},
};
static const int SPURS_N = 1;
