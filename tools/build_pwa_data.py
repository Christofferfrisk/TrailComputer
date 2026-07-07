#!/usr/bin/env python3
"""Extract the route/hut/stage/spur tables from the firmware headers into a JS
data file for the offline web app (PWA).

    python tools/build_pwa_data.py   ->  docs/data.js
"""
import os
import re

SRC = os.path.join(os.path.dirname(__file__), "..", "src")
OUT = os.path.join(os.path.dirname(__file__), "..", "docs", "data.js")


def read(name):
    with open(os.path.join(SRC, name), encoding="utf-8", errors="replace") as f:
        return f.read()


def parse_polyline(text, arr):
    body = re.search(arr + r"\[\]\s*=\s*\{(.*?)\n\};", text, re.S).group(1)
    pts = []
    for m in re.finditer(r"\{([-\d.]+)f,\s*([-\d.]+)f,\s*([-\d.]+)f,\s*([-\d.]+)f,\s*([-\d.]+)f,\s*([-\d.]+)f\}", body):
        lat, lon, cum, ele, asc, desc = (float(x) for x in m.groups())
        pts.append([round(lat, 6), round(lon, 6), round(cum, 1), round(ele, 1), round(asc, 1), round(desc, 1)])
    return pts


def strlist_after(text, marker):
    seg = text[text.index(marker):]
    body = re.search(r"S\[\]\s*=\s*\{([^}]*)\}", seg).group(1)
    return re.findall(r'"([^"]+)"', body)


rt = read("route_table.h")
st = read("route_stages.h")
rs = read("route_spurs.h")
sp = read("spur_table.h")

route = parse_polyline(rt, "ROUTE")
huts = [int(x) for x in re.search(r"ROUTE_HUTS\[\]\s*=\s*\{([^}]*)\}", rt).group(1).split(",")]
hut_names = re.findall(r'"([^"]*)"', re.search(r"ROUTE_HUT_NAMES\[\]\s*=\s*\{([^}]*)\}", rt).group(1))

stages = {}
for m in re.finditer(r'\{"([^"]+)",\s*([\d.]+),\s*"([^"]*)",\s*(true|false)\}', st):
    stages[m.group(1)] = {"km": float(m.group(2)), "time": m.group(3), "boat": m.group(4) == "true"}

stores = {}
sb = re.search(r"HUT_STORES\[\]\s*=\s*\{(.*?)\};", st, re.S).group(1)
for m in re.finditer(r'\{"([^"]+)",\s*(\d+)\}', sb):
    stores[m.group(1)] = int(m.group(2))

sauna = strlist_after(st, "hutSauna")
station = strlist_after(st, "hutStation")

transport = {}
for m in re.finditer(r'strcmp\(n,\s*"([^"]+)"\)\s*==\s*0\)\s*return\s*"([^"]+)"', st):
    transport[m.group(1)] = m.group(2)

keb_stops = []
kb = re.search(r"KEB_STOPS\[\]\s*=\s*\{(.*?)\};", rs, re.S).group(1)
for m in re.finditer(r'\{"([^"]+)",\s*([\d.]+)f?,\s*([\d.]+)f?,\s*([\d.]+)f,\s*"([^"]*)",\s*"([^"]*)"\}', kb):
    keb_stops.append({"name": m.group(1), "lat": float(m.group(2)), "lon": float(m.group(3)),
                      "legKm": float(m.group(4)), "time": m.group(5), "note": m.group(6)})

spur_route = parse_polyline(sp, "SPUR_ROUTE")
m = re.search(r"SPUR_KEB_IDX\s*=\s*(\d+)", sp)
spur_keb_idx = int(m.group(1)) if m else 0
m = re.search(r"SPURS\[\]\s*=\s*\{\s*\{\s*\"([^\"]+)\",\s*\"([^\"]+)\"", rs)
after_hut, junction = (m.group(1), m.group(2)) if m else ("Singi", "Singi")

# App-only reference notes (not in the firmware tables). Source: STF trail guide.
info = {
    "season": "STF huts are staffed roughly late June to mid-September. Outside that they are closed or unstaffed. Reserve a Saltoluokta dinner and the Vakkotavare-Kebnats bus ahead in high season.",
    "emergency": "In an emergency call 112 and ask for Fjallraddningen (mountain rescue). Give the nearest hut or a lake/peak name. Reception is patchy - a satellite messenger is the reliable option.",
    "marking": "Summer route is marked with cairns and red-painted poles; winter with red crosses. Boats cross Teusajaure and the Kebnats sound (Saltoluokta) - both run on a timetable, not on demand.",
    "boatShortcut": "Nikkaluokta start: a boat across Laddjujavri runs several times a day and cuts about 6 km off the 19 km leg to Kebnekaise.",
    "lastLeg": "Skip the 30 km Vakkotavare-Saltoluokta walk: the bus leaves Vakkotavare 14:40 and reaches Kebnats 15:40 (roadtoritsem.com), then the 16:55 M/S Langas boat crosses to Saltoluokta. Boat runs 18 Jun-20 Sep, daily - Saltoluokta->Kebnats 10:00 & 16:30, Kebnats->Saltoluokta 10:25 & 16:55. Miss the 14:40 bus and there is no later connection the same day, so plan the day around it.",
    # Fixed departures the day planner works backwards from. buffer = minutes to be there early.
    "depart": {
        "Vakkotavare": {"time": "14:40", "mode": "bus", "buffer": 15, "arrive": "17:10",
                        "label": "Bus to Kebnats (arr 15:40), then the 16:55 M/S Langas boat to Saltoluokta"},
    },
}

# "Don't miss" side-trips keyed by the hut you overnight at. Source: STF trail guide.
tips = {
    "Aktse": "In clear weather, a side-trip up Skierffe - the photographers' favourite, with a huge view over Rapadalen and the Parte massif in Sarek.",
}

# Stage-specific practical notes, keyed by the hut the leg ends at (matches "stages").
notes = {
    "Sitojaure": "No shop - carry your own food. Streams for water on the way; a wind shelter at Autsutjvagge marks the halfway point. Steep climb at the start, high point ~775 m.",
    "Aktse": "Starts with the mandatory boat across Kaskajaure and Kåbtajaure (from Sitojaure). Water is scarce on the open plateau - fill up in the birch forest below before the climb.",
    "Pårte": "Starts with the boat across Laitaure - a self-service rowing boat or the motorboat. Enters Sarek National Park. No shop - carry provisions. Final descent is tricky when wet.",
    "Kvikkjokk": "Rough and rocky in places, with a bridge over Tjåltajåkka. The slope at Tingstallstenen can turn into a stream in prolonged rain.",
}

data = {
    "route": route, "huts": huts, "hutNames": hut_names,
    "stages": stages, "stores": stores, "sauna": sauna, "station": station,
    "transport": transport, "info": info, "tips": tips, "notes": notes,
    "spur": {"route": spur_route, "kebIdx": spur_keb_idx, "stops": keb_stops,
             "afterHut": after_hut, "junction": junction},
}

import json
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    f.write("// Generated by tools/build_pwa_data.py from the firmware route tables.\n")
    f.write("window.TC_DATA = ")
    json.dump(data, f, ensure_ascii=False, separators=(",", ":"))
    f.write(";\n")

print("wrote", os.path.normpath(OUT),
      f"({len(route)} route pts, {len(huts)} huts, {len(spur_route)} spur pts)")
