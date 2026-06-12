#!/usr/bin/env python3
"""Convert a GPX track into a simplified route for the trail computer.

Reads a GPX, simplifies the track with Douglas-Peucker to ~150 m spacing,
computes cumulative distance per point, marks the points nearest to named huts
as end-of-day destinations, and emits either a C array (--c) or a CSV matching
data/route.csv.

Usage:
    python gpx_to_route.py track.gpx --huts huts.csv --csv route.csv
    python gpx_to_route.py track.gpx --huts huts.csv --c   > route_table.h

huts.csv: lines of  name,lat,lon  (one per destination).
"""
import argparse
import math
import sys
import xml.etree.ElementTree as ET

R_EARTH = 6371000.0


def haversine(a, b):
    lat1, lon1 = math.radians(a[0]), math.radians(a[1])
    lat2, lon2 = math.radians(b[0]), math.radians(b[1])
    dlat, dlon = lat2 - lat1, lon2 - lon1
    h = math.sin(dlat / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2) ** 2
    return 2 * R_EARTH * math.asin(math.sqrt(h))


def perpendicular_m(pt, a, b):
    """Planar perpendicular distance (m) of pt from segment a-b, cos-lat scaled."""
    k = math.cos(math.radians(a[0]))
    ax, ay = 0.0, 0.0
    bx = (b[1] - a[1]) * k * R_EARTH * math.pi / 180
    by = (b[0] - a[0]) * R_EARTH * math.pi / 180
    px = (pt[1] - a[1]) * k * R_EARTH * math.pi / 180
    py = (pt[0] - a[0]) * R_EARTH * math.pi / 180
    seg2 = bx * bx + by * by
    if seg2 == 0:
        return math.hypot(px, py)
    t = max(0.0, min(1.0, (px * bx + py * by) / seg2))
    qx, qy = ax + t * bx, ay + t * by
    return math.hypot(px - qx, py - qy)


def douglas_peucker(pts, eps_m):
    if len(pts) < 3:
        return pts[:]
    dmax, idx = 0.0, 0
    for i in range(1, len(pts) - 1):
        d = perpendicular_m(pts[i], pts[0], pts[-1])
        if d > dmax:
            dmax, idx = d, i
    if dmax > eps_m:
        left = douglas_peucker(pts[: idx + 1], eps_m)
        right = douglas_peucker(pts[idx:], eps_m)
        return left[:-1] + right
    return [pts[0], pts[-1]]


def douglas_peucker_idx(pts, eps_m):
    """Like douglas_peucker but returns the kept indices (iterative)."""
    if len(pts) < 3:
        return list(range(len(pts)))
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        lo, hi = stack.pop()
        if hi <= lo + 1:
            continue
        dmax, idx = 0.0, lo
        for i in range(lo + 1, hi):
            d = perpendicular_m(pts[i], pts[lo], pts[hi])
            if d > dmax:
                dmax, idx = d, i
        if dmax > eps_m:
            keep[idx] = True
            stack.append((lo, idx))
            stack.append((idx, hi))
    return [i for i, k in enumerate(keep) if k]


def cum_climb(pts, deadband=3.0):
    """Cumulative ascent/descent along the full track, dead-banded to drop DEM
    noise. Returns two lists aligned with pts."""
    asc = [0.0]
    desc = [0.0]
    ref = pts[0][2]
    a = d = 0.0
    for i in range(1, len(pts)):
        e = pts[i][2]
        if e - ref > deadband:
            a += e - ref
            ref = e
        elif ref - e > deadband:
            d += ref - e
            ref = e
        asc.append(a)
        desc.append(d)
    return asc, desc


def parse_gpx(path):
    ns = {"g": "http://www.topografix.com/GPX/1/1"}
    root = ET.parse(path).getroot()
    pts = []
    for tp in root.iter("{http://www.topografix.com/GPX/1/1}trkpt"):
        lat, lon = float(tp.get("lat")), float(tp.get("lon"))
        ele_el = tp.find("g:ele", ns)
        ele = float(ele_el.text) if ele_el is not None else 0.0
        pts.append((lat, lon, ele))
    return pts


def load_huts(path):
    huts = []
    if not path:
        return huts
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            name, lat, lon = line.split(",")[:3]
            huts.append((name.strip(), float(lat), float(lon)))
    return huts


def nearest_index(pts, lat, lon):
    best_i, best_d = 0, float("inf")
    for i, p in enumerate(pts):
        d = haversine((lat, lon), (p[0], p[1]))
        if d < best_d:
            best_i, best_d = i, d
    return best_i


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gpx")
    ap.add_argument("--huts")
    ap.add_argument("--eps", type=float, default=150.0, help="simplify spacing, m")
    ap.add_argument("--reverse", action="store_true",
                    help="reverse point order (e.g. so travel direction is N->S)")
    ap.add_argument("--csv")
    ap.add_argument("--c", action="store_true", help="emit C array to stdout")
    args = ap.parse_args()

    raw = parse_gpx(args.gpx)
    if not raw:
        sys.exit("no track points found")

    # Work in travel order so cumulative climb accumulates correctly.
    track = raw[::-1] if args.reverse else raw

    # Cumulative ascent/descent from the FULL-resolution track (simplification
    # would otherwise smooth out ~quarter of the real climbing).
    asc_full, desc_full = cum_climb(track, deadband=3.0)

    idxs = douglas_peucker_idx(track, args.eps)
    simp = [track[i] for i in idxs]
    cumA = [asc_full[i] for i in idxs]
    cumD = [desc_full[i] for i in idxs]

    cum = [0.0]
    for i in range(1, len(simp)):
        cum.append(cum[-1] + haversine(simp[i - 1][:2], simp[i][:2]))

    huts = load_huts(args.huts)
    names = ["" for _ in simp]
    for name, lat, lon in huts:
        names[nearest_index(simp, lat, lon)] = name

    if args.c:
        print("// Generated by gpx_to_route.py")
        print("#pragma once")
        print('#include "geo.h"')
        print(f"static const RoutePoint ROUTE[] = {{")
        for (lat, lon, ele), c, ca, cd in zip(simp, cum, cumA, cumD):
            print(f"  {{{lat:.6f}f, {lon:.6f}f, {c:.1f}f, {ele:.1f}f, {ca:.1f}f, {cd:.1f}f}},")
        print("};")
        print(f"static const int ROUTE_N = {len(simp)};")
        hut_idx = [i for i, n in enumerate(names) if n]
        print("static const int ROUTE_HUTS[] = {" +
              ", ".join(str(i) for i in hut_idx) + "};")
        print("static const char* ROUTE_HUT_NAMES[] = {" +
              ", ".join('"%s"' % names[i] for i in hut_idx) + "};")
        print(f"static const int ROUTE_HUTS_N = {len(hut_idx)};")
        return

    out = open(args.csv, "w") if args.csv else sys.stdout
    out.write("# lat,lon,ele_m,name\n")
    for (lat, lon, ele), n in zip(simp, names):
        out.write(f"{lat:.6f},{lon:.6f},{ele:.1f},{n}\n")
    if args.csv:
        out.close()


if __name__ == "__main__":
    main()
