#pragma once

// Official STF étappe figures for the legs of the route, keyed by the hut you
// ARRIVE at travelling north->south. Where present these override the
// GPX-computed leg distance and add a walking time; `boat` flags a ferry on the
// leg. Huts without an entry just show the computed distance.
//   Source: svenskaturistforeningen.se (Kungsleden Nikkaluokta–Saltoluokta etc.)

struct Stage {
    const char* arriveHut;
    float km;
    const char* time;
    bool  boat;
};

static const Stage STAGES[] = {
    {"Abiskojaure", 15, "4-6 h",  false},   // from Abisko
    {"Alesjaure",   21, "6-8 h",  false},   // from Abiskojaure
    {"Tjäktja",     13, "4-5 h",  false},   // from Alesjaure
    {"Sälka",       12, "3-5 h",  false},   // from Tjäktja
    {"Singi",       12, "3-4 h",  false},   // from Sälka (main line)
    {"Kaitumjaure", 13, "4-5 h",  false},   // from Singi
    {"Teusajaure",   9, "4-5 h",  false},   // from Kaitumjaure
    {"Vakkotavare", 15, "4-6 h",  true},    // from Teusajaure, boat across the lake
    {"Saltoluokta", 30, "9-11 h", true},    // from Vakkotavare, M/S Langas boat (or bus)
    {"Sitojaure",   19, "6-8 h",  false},   // from Saltoluokta
    {"Aktse",       10, "3-4 h",  true},    // from Sitojaure, boat over Kaskajaure/Kåbtajaure
    {"Pårte",       22, "7-9 h",  true},    // from Aktse, boat over Laitaure
    {"Kvikkjokk",   17, "6-8 h",  false},   // from Pårte
};
static const int STAGES_N = sizeof(STAGES) / sizeof(STAGES[0]);

static const Stage* findStage(const char* hut) {
    for (int i = 0; i < STAGES_N; i++)
        if (__builtin_strcmp(STAGES[i].arriveHut, hut) == 0) return &STAGES[i];
    return nullptr;
}

// Shop/proviantering at each hut: 1 = small assortment, 2 = large (or a
// fjällstation with a full shop). Source: STF "Fjällstugornas butiker".
struct HutStore { const char* hut; uint8_t level; };
static const HutStore HUT_STORES[] = {
    {"Abisko", 2}, {"Abiskojaure", 2}, {"Alesjaure", 2}, {"Sälka", 2}, {"Aktse", 2},
    {"Kebnekaise Fjällstation", 2}, {"Saltoluokta", 2}, {"Kvikkjokk", 2}, {"Nikkaluokta", 2},
    {"Kaitumjaure", 1}, {"Teusajaure", 1},
};
static const int HUT_STORES_N = sizeof(HUT_STORES) / sizeof(HUT_STORES[0]);

static uint8_t hutStore(const char* hut) {
    for (int i = 0; i < HUT_STORES_N; i++)
        if (__builtin_strcmp(HUT_STORES[i].hut, hut) == 0) return HUT_STORES[i].level;
    return 0;
}

// Sauna (bastu) — confirmed huts only (others may also have one).
static bool hutSauna(const char* n) {
    static const char* S[] = {"Alesjaure", "Sälka", "Kaitumjaure", "Teusajaure",
                              "Saltoluokta", "Kebnekaise Fjällstation"};
    for (const char* s : S) if (__builtin_strcmp(s, n) == 0) return true;
    return false;
}

// Full-service fjällstation (restaurant/meals), vs self-service stugor.
static bool hutStation(const char* n) {
    static const char* S[] = {"Abisko", "Kebnekaise Fjällstation", "Saltoluokta", "Kvikkjokk"};
    for (const char* s : S) if (__builtin_strcmp(s, n) == 0) return true;
    return false;
}

// Public transport at trail exits (Länstrafiken Norrbotten).
static const char* hutTransport(const char* n) {
    if (__builtin_strcmp(n, "Nikkaluokta") == 0) return "Bus 92 to Kiruna";
    if (__builtin_strcmp(n, "Vakkotavare") == 0) return "Bus 93 (E10)";
    if (__builtin_strcmp(n, "Saltoluokta") == 0) return "Boat to Kebnats + bus 93";
    if (__builtin_strcmp(n, "Kvikkjokk")   == 0) return "Bus 47 to Jokkmokk";
    return nullptr;
}
