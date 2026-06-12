#!/usr/bin/env python3
"""Generate the project logo and the GitHub social-preview banner.

    python tools/make_logo.py   ->  docs/img/logo.png, docs/img/social-preview.png

Drawn in the device's 1-bit e-paper style and supersampled for sharp edges.
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

FD = r"C:\Windows\Fonts"
BLK = (17, 17, 17, 255)
WHT = (255, 255, 255, 255)
SS = 4


def font(name, size):
    return ImageFont.truetype(os.path.join(FD, name), size)


def rot(px, py, cx, cy, deg):
    a = math.radians(deg)
    x, y = px - cx, py - cy
    return cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a)


def draw_icon(size):
    """The square app-style icon: an e-paper device showing the compass rose
    from the NAV screen, with a mountain band at the base. Returns an RGBA
    image of the given size."""
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    u = lambda v: v * s / 256.0                       # logical 256 -> pixels

    # device body: white screen, black bezel
    d.rounded_rectangle((u(18), u(18), u(238), u(238)), radius=u(30),
                        fill=WHT, outline=BLK, width=int(u(10)))

    cx, cy, r = 128, 116, 64

    # compass ring
    d.ellipse((u(cx - r), u(cy - r), u(cx + r), u(cy + r)),
              outline=BLK, width=int(u(6)))

    # tick marks every 30 degrees
    for a in range(0, 360, 30):
        long = (a % 90 == 0)
        r0 = r - (16 if long else 10)
        x0, y0 = rot(cx, cy - r0, cx, cy, a)
        x1, y1 = rot(cx, cy - r, cx, cy, a)
        d.line((u(x0), u(y0), u(x1), u(y1)), fill=BLK, width=int(u(5 if long else 3)))

    # north marker (filled triangle just outside the ring at the top)
    d.polygon([(u(cx), u(cy - r - 14)), (u(cx - 9), u(cy - r + 2)),
               (u(cx + 9), u(cy - r + 2))], fill=BLK)

    # needle, pointing north-east
    needle = [rot(x, y, cx, cy, 35) for x, y in
              [(cx, cy - 52), (cx + 20, cy + 40), (cx, cy + 22), (cx - 20, cy + 40)]]
    d.polygon([(u(x), u(y)) for x, y in needle], fill=BLK)

    # mountain band at the base
    ridge = [(40, 226), (76, 188), (104, 212), (140, 182), (172, 210), (216, 226)]
    d.polygon([(u(x), u(y)) for x, y in ridge] + [(u(216), u(228)), (u(40), u(228))],
              fill=BLK)

    return img.resize((size, size), Image.LANCZOS)


def out_path(name):
    p = os.path.join(os.path.dirname(__file__), "..", "docs", "img", name)
    return os.path.normpath(p)


def main():
    icon = draw_icon(512)
    icon.save(out_path("logo.png"))
    print("wrote", out_path("logo.png"))

    # social preview: 1280x640, icon on the left, title on the right
    banner = Image.new("RGB", (1280, 640), (255, 255, 255))
    big = draw_icon(440)
    banner.paste(big, (110, 100), big)
    d = ImageDraw.Draw(banner)
    d.text((600, 232), "Trail Computer", font=font("arialbd.ttf", 80),
           fill=(17, 17, 17), anchor="ls")
    d.text((602, 300), "Fully-offline ESP32 e-paper", font=font("arial.ttf", 38),
           fill=(17, 17, 17), anchor="ls")
    d.text((602, 350), "hiking navigator", font=font("arial.ttf", 38),
           fill=(17, 17, 17), anchor="ls")
    banner.save(out_path("social-preview.png"))
    print("wrote", out_path("social-preview.png"))


if __name__ == "__main__":
    main()
