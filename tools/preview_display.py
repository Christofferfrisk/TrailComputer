#!/usr/bin/env python3
"""Render the e-paper screens to PNG on the PC, mirroring src/display.cpp so we
can eyeball the 296x128 layout without hardware. Fonts/metrics are approximate
(Arial vs u8g2 helv), so use it for layout, not pixel-perfect text.

    python tools/preview_display.py   ->  tools/preview/*.png
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

W, H = 296, 128
SS = 6        # supersample: render at SS x logical resolution, then save
FD = r"C:\Windows\Fonts"
BLACK, WHITE = 0, 255


def font(name, size):
    return ImageFont.truetype(os.path.join(FD, name), size * SS)


F_TINY = font("arial.ttf", 8)
F_SM = font("arial.ttf", 10)
F_MD = font("arial.ttf", 12)
F_MDB = font("arialbd.ttf", 12)
F_BD = font("arialbd.ttf", 13)
F_HUGE = font("arialbd.ttf", 20)


class Epd:
    """Draws in logical 296x128 coordinates but rasterises at SS x resolution.
    Every primitive scales its coordinates; strokes are SS px wide so a 1 px
    logical line stays 1 logical px. width() reports logical units so the
    layout math in the render_* functions needs no changes."""

    def __init__(self):
        self.img = Image.new("L", (W * SS, H * SS), WHITE)
        self.d = ImageDraw.Draw(self.img)

    # --- GxEPD2-like primitives ---
    def line(self, x0, y0, x1, y1, c=BLACK):
        self.d.line((x0 * SS, y0 * SS, x1 * SS, y1 * SS), fill=c, width=SS)
    def line2(self, x0, y0, x1, y1, c=BLACK):
        self.line(x0, y0, x1, y1, c)
        if abs(x1 - x0) >= abs(y1 - y0):
            self.line(x0, y0 + 1, x1, y1 + 1, c)
        else:
            self.line(x0 + 1, y0, x1 + 1, y1, c)
    def hline(self, x, y, w, c=BLACK):
        self.d.line((x * SS, y * SS, (x + w - 1) * SS, y * SS), fill=c, width=SS)
    def rect(self, x, y, w, h, c=BLACK):
        self.d.rectangle((x * SS, y * SS, (x + w - 1) * SS, (y + h - 1) * SS), outline=c, width=SS)
    def fillrect(self, x, y, w, h, c=BLACK):
        self.d.rectangle((x * SS, y * SS, (x + w - 1) * SS, (y + h - 1) * SS), fill=c)
    def roundrect(self, x, y, w, h, r, c=BLACK):
        self.d.rounded_rectangle((x * SS, y * SS, (x + w - 1) * SS, (y + h - 1) * SS), radius=r * SS, fill=c)
    def circle(self, cx, cy, r, c=BLACK):
        self.d.ellipse(((cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS), outline=c, width=SS)
    def fillcircle(self, cx, cy, r, c=BLACK):
        self.d.ellipse(((cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS), fill=c)
    def tri(self, x0, y0, x1, y1, x2, y2, c=BLACK):
        self.d.polygon((x0 * SS, y0 * SS, x1 * SS, y1 * SS, x2 * SS, y2 * SS), fill=c)
    def pixel(self, x, y, c=BLACK):
        self.d.rectangle((x * SS, y * SS, (x + 1) * SS - 1, (y + 1) * SS - 1), fill=c)

    # --- U8g2-like text (y = baseline) ---
    def text(self, x, y, s, fnt, inv=False):
        self.d.text((x * SS, y * SS), s, font=fnt, fill=(WHITE if inv else BLACK), anchor="ls")
    def width(self, s, fnt):
        return self.d.textlength(s, font=fnt) / SS
    def textR(self, xr, y, s, fnt):
        self.text(xr - self.width(s, fnt), y, s, fnt)
    def textC(self, xc, y, s, fnt, inv=False):
        self.d.text((xc * SS, y * SS), s, font=fnt, fill=(WHITE if inv else BLACK), anchor="ms")


D2R = math.pi / 180


def polar(cx, cy, deg, r):
    a = deg * D2R
    return cx + round(r * math.sin(a)), cy - round(r * math.cos(a))


def battery(e, x, y, pct, low):
    w, h = 20, 10
    e.rect(x, y, w, h)
    e.fillrect(x + w, y + 3, 2, h - 6)
    fill = max(0, min(w - 4, pct * (w - 4) // 100))
    if low:
        e.text(x + 7, y + h - 2, "!", F_SM)
    else:
        e.fillrect(x + 2, y + 2, fill, h - 4)


def sun(e, cx, cy):
    e.fillcircle(cx, cy, 2)
    for a in range(0, 360, 45):
        r = a * D2R
        e.pixel(cx + round(5 * math.cos(r)), cy + round(5 * math.sin(r)))


def sat(e, x, y):
    e.fillrect(x, y + 4, 2, 3)
    e.fillrect(x + 3, y + 2, 2, 5)
    e.fillrect(x + 6, y, 2, 7)


def trend(e, x, y, t):
    if t > 0:
        e.tri(x, y - 4, x - 3, y + 1, x + 3, y + 1)
    elif t < 0:
        e.tri(x, y + 4, x - 3, y - 1, x + 3, y - 1)
    else:
        e.fillrect(x - 3, y - 1, 6, 2)


def status_bar(e, vm):
    e.text(2, 11, vm["clock"], F_MD)
    if vm["daylight"] >= 0:
        sun(e, 60, 6)
        e.text(68, 11, "%d:%02d" % (vm["daylight"] // 60, vm["daylight"] % 60), F_SM)
    sat(e, 150, 2)
    e.text(160, 11, str(vm["sats"]), F_SM)
    battery(e, W - 22, 1, vm["batt"], vm.get("low", False))
    e.textR(W - 24, 10, "%d%%" % vm["batt"], F_SM)
    e.hline(0, 14, W)


def compass(e, vm, cx, cy, r):
    e.circle(cx, cy, r)
    for dd in range(0, 360, 30):
        screen = dd - vm["heading"]
        card = dd % 90 == 0
        x0, y0 = polar(cx, cy, screen, r)
        x1, y1 = polar(cx, cy, screen, r - (6 if card else 3))
        e.line(x0, y0, x1, y1)
    nx, ny = polar(cx, cy, -vm["heading"], r + 4)
    lx, ly = polar(cx, cy, -vm["heading"] + 8, r)
    rx, ry = polar(cx, cy, -vm["heading"] - 8, r)
    e.tri(nx, ny, lx, ly, rx, ry)
    rel = vm["bearing"] - vm["heading"]
    tx, ty = polar(cx, cy, rel, r - 6)
    plx, ply = polar(cx, cy, rel + 90, 5)
    prx, pry = polar(cx, cy, rel - 90, 5)
    txx, tyy = polar(cx, cy, rel + 180, r - 10)
    e.line(cx, cy, txx, tyy)
    e.tri(tx, ty, plx, ply, prx, pry)
    e.fillcircle(cx, cy, 2)


def sparkline(e, vm, x, y, w, h):
    e.rect(x, y, w, h)
    s = vm["spark"]
    lo, hi = min(s), max(s)
    span = max(0.5, hi - lo)
    px = py = None
    for i, v in enumerate(s):
        cx = x + 2 + i * (w - 4) // (len(s) - 1)
        cy = y + h - 2 - int((v - lo) / span * (h - 4))
        if px is not None:
            e.line(px, py, cx, cy)
        e.fillcircle(cx, cy, 1)
        px, py = cx, cy


def weather(e, vm, top):
    e.hline(0, top, W)
    buf = "%.0f hPa" % vm["press"]
    e.text(2, top + 16, buf, F_MD)
    trend(e, int(e.width(buf, F_MD)) + 12, top + 12, vm["trend"])
    if vm["turning"]:
        cw, ch, cx, cy = 96, 16, 84, top + 4
        e.roundrect(cx, cy, cw, ch, 3)
        e.textC(cx + cw // 2, cy + 12, "Weather turning", F_SM, inv=True)
        sparkline(e, vm, W - 100, top + 4, 96, 18)
    else:
        sparkline(e, vm, 96, top + 4, W - 100, 18)


def draw_tile(e, cx, yl, yv, label, value):
    e.textC(cx, yl, label, F_TINY)
    e.textC(cx, yv, value, F_MD)


def draw_stat(e, cx, label, value):
    draw_tile(e, cx, 77, 93, label, value)


def render_nav(e, vm):
    status_bar(e, vm)
    compass(e, vm, 38, 54, 27)
    e.textC(38, 94, "%d°" % round(vm["heading"]), F_SM)
    e.line(74, 18, 74, 98)

    rx = 82
    e.text(rx, 31, vm["stop"], F_BD)
    if vm["km"] < 1:
        num, unit = "%.0f" % (vm["km"] * 1000), "m"
    else:
        num, unit = "%.1f" % vm["km"], "km"
    e.text(rx, 59, num, F_HUGE)
    e.text(rx + int(e.width(num, F_HUGE)) + 4, 59, unit, F_MD)
    e.hline(rx, 66, W - rx)

    e.line(153, 70, 153, 97)
    e.line(224, 70, 224, 97)
    draw_stat(e, 117, "ETA", "%d:%02d" % (vm["eta"] // 60, vm["eta"] % 60))
    draw_stat(e, 188, "CLIMB", "%.0f m" % vm["climb"])
    draw_stat(e, 259, "ALT", "%.0f m" % vm["alt"])
    weather(e, vm, 100)


def render_nofix(e, vm):
    status_bar(e, vm)
    e.textC(W // 2, 52, "Acquiring GPS", F_HUGE)
    e.textC(W // 2, 80, "%d satellites in view" % vm["sats"], F_MD)
    e.textC(W // 2, 110, "move to open sky for a fix", F_TINY)


def render_arrived(e, vm, endhike=False):
    status_bar(e, vm)
    e.textC(W // 2, 40, "End of hike" if endhike else "Arrived", F_HUGE)
    e.textC(W // 2, 60, vm["stop"], F_BD)
    e.hline(8, 70, W - 16)
    e.line(W // 3, 74, W // 3, 104)
    e.line(2 * W // 3, 74, 2 * W // 3, 104)
    draw_tile(e, W // 6, 84, 100, "ASCENT", "%.0f m" % vm["asc"])
    draw_tile(e, W // 2, 84, 100, "DESCENT", "%.0f m" % vm["desc"])
    draw_tile(e, 5 * W // 6, 84, 100, "CHECKS", "~%d" % vm["checks"])
    e.textC(W // 2, 122, "long-press to set next stop", F_TINY)


def render_config(e, vm):
    status_bar(e, vm)
    e.textC(W // 2, 40, "Config mode", F_HUGE)
    e.textC(W // 2, 60, "Connect to Wi-Fi", F_TINY)
    e.textC(W // 2, 80, "TrailComputer", F_BD)
    e.textC(W // 2, 100, "http://192.168.4.1", F_MD)
    e.textC(W // 2, 120, "open in your phone browser", F_TINY)


def render_lowbatt(e, vm):
    status_bar(e, vm)
    battery(e, W // 2 - 11, 28, vm["batt"], True)
    e.textC(W // 2, 70, "LOW BATTERY", F_HUGE)
    e.textC(W // 2, 92, "~%.0f mAh left" % vm["mah"], F_MD)
    e.textC(W // 2, 112, "GPS off - make for nearest hut", F_TINY)


def _clip(x0, y0, x1, y1, xmin, ymin, xmax, ymax):
    def code(x, y):
        c = 0
        if x < xmin: c |= 1
        elif x > xmax: c |= 2
        if y < ymin: c |= 4
        elif y > ymax: c |= 8
        return c
    c0, c1 = code(x0, y0), code(x1, y1)
    for _ in range(8):
        if not (c0 | c1): return (x0, y0, x1, y1)
        if c0 & c1: return None
        c = c0 or c1
        if c & 8: x, y = x0 + (x1 - x0) * (ymax - y0) / (y1 - y0), ymax
        elif c & 4: x, y = x0 + (x1 - x0) * (ymin - y0) / (y1 - y0), ymin
        elif c & 2: y, x = y0 + (y1 - y0) * (xmax - x0) / (x1 - x0), xmax
        else: y, x = y0 + (y1 - y0) * (xmin - x0) / (x1 - x0), xmin
        if c == c0: x0, y0, c0 = x, y, code(x, y)
        else: x1, y1, c1 = x, y, code(x, y)
    return None


def render_map(e, vm):
    status_bar(e, vm)
    x0, y0, w, h = 2, 16, 182, H - 18
    cx, cy = x0 + w // 2, y0 + h // 2
    halfPx = h // 2 - 3
    e.rect(x0, y0, w, h)
    scale = halfPx / max(1, vm["range"])
    cl, ct, cr, cb = x0 + 1, y0 + 1, x0 + w - 1, y0 + h - 1

    px = py = None
    for (E, N), hut in vm["pts"]:
        mx = cx + round(E * scale); my = cy - round(N * scale)
        if px is not None:
            seg = _clip(px, py, mx, my, cl, ct, cr, cb)
            if seg:
                e.line2(round(seg[0]), round(seg[1]), round(seg[2]), round(seg[3]))
        px, py = mx, my
        if hut and cl <= mx <= cr and ct <= my <= cb:
            e.fillrect(mx - 2, my - 2, 5, 5)

    dE, dN = vm["dest"]
    dx = cx + round(dE * scale); dy = cy - round(dN * scale)
    if x0 < dx < x0 + w and y0 < dy < y0 + h:
        e.circle(dx, dy, 6); e.circle(dx, dy, 5); e.fillcircle(dx, dy, 3)
    else:
        a = math.atan2(dE, dN)
        tx, ty = polar(cx, cy, a / D2R, halfPx)
        ax, ay = polar(cx, cy, a / D2R, halfPx - 5)
        lx, ly = polar(cx, cy, a / D2R + 145, 6)
        rx, ry = polar(cx, cy, a / D2R - 145, 6)
        e.tri(tx, ty, ax + (lx - cx), ay + (ly - cy), ax + (rx - cx), ay + (ry - cy))

    tx, ty = polar(cx, cy, vm["heading"], 11)
    blx, bly = polar(cx, cy, vm["heading"] + 132, 9)
    brx, bry = polar(cx, cy, vm["heading"] - 132, 9)
    e.tri(tx, ty, blx, bly, brx, bry)

    e.tri(x0 + 7, y0 + 4, x0 + 4, y0 + 10, x0 + 10, y0 + 10)
    e.text(x0 + 13, y0 + 11, "N", F_TINY)

    nice = [100, 200, 500, 1000, 2000, 5000]; barM = nice[0]
    for n in nice:
        if n * scale <= 60:
            barM = n
    barPx = int(barM * scale)
    by = y0 + h - 6; bx = x0 + 6
    e.hline(bx, by, barPx); e.line(bx, by - 3, bx, by); e.line(bx + barPx, by - 3, bx + barPx, by)
    sb = ("%.0f km" % (barM / 1000)) if barM >= 1000 else ("%.0f m" % barM)
    e.text(bx + barPx + 4, by + 2, sb, F_TINY)

    xs = x0 + w + 6
    e.line(x0 + w + 3, y0, x0 + w + 3, H - 1)
    e.text(xs, 28, vm["stop"], F_MDB)
    if vm["km"] < 1:
        num, unit = "%.0f" % (vm["km"] * 1000), "m"
    else:
        num, unit = "%.1f" % vm["km"], "km"
    e.text(xs, 51, num, F_BD)
    e.text(xs + int(e.width(num, F_BD)) + 3, 51, unit, F_MDB)
    e.hline(xs, 58, W - xs - 2)
    e.text(xs, 78, "ETA %d:%02d" % (vm["eta"] // 60, vm["eta"] % 60), F_MDB)
    e.text(xs, 98, "climb %.0fm" % vm["climb"], F_MDB)
    e.text(xs, 118, "alt %.0fm" % vm["alt"], F_MDB)


def save(e, name):
    out = os.path.join(os.path.dirname(__file__), "preview")
    os.makedirs(out, exist_ok=True)
    big = e.img.resize((W * 3, H * 3), Image.LANCZOS)
    p = os.path.join(out, name + ".png")
    big.save(p)
    print("wrote", p)


def main():
    vm = dict(clock="13:42", daylight=380, sats=9, batt=78, stop="Tjäktja",
              km=8.4, heading=20, bearing=65, climb=240, eta=155, alt=712,
              press=1009, turning=True, trend=-1, asc=1234, desc=1180,
              mah=1480, checks=38, low=False,
              spark=[1011, 1011.5, 1012, 1011.2, 1010.4, 1009.8, 1009.1])

    for name, fn in [("nav", render_nav), ("nofix", render_nofix),
                     ("arrived", render_arrived), ("config", render_config)]:
        e = Epd(); fn(e, vm); save(e, name)
    e = Epd(); render_lowbatt(e, dict(vm, batt=4, low=True)); save(e, "lowbatt")

    pts = []
    for t in range(-1500, 2550, 150):
        pts.append(((200 * math.sin(t / 600.0), float(t)), t in (-1200, 1500)))
    mapvm = dict(vm, pts=pts, range=1840, heading=18,
                 dest=(200 * math.sin(1500 / 600.0), 1500.0), stop="Sälka", km=1.6)
    e = Epd(); render_map(e, mapvm); save(e, "map")


if __name__ == "__main__":
    main()
