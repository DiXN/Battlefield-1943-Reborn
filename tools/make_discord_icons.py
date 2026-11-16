"""Two original Discord Rich Presence icons for Battlefield 1943 Reborn, drawn from scratch (no game art).

Everything is drawn here with Pillow: shapes, gradients and noise. The only outside piece is the Oswald Bold font
from bf/fonts (SIL Open Font License, free to use in images). No EA logo, game font or game picture is used.

    python tools/make_discord_icons.py            # writes runs/discord-art/original/
Output: icon_a_island.png and icon_b_stencil.png (1024 px, what Discord asks for), preview.png (both at the
sizes Discord shows them) and banner.png (680 x 240, the application's profile banner) and cover.png (1024 x 576, the Rich Presence
cover image), both in the stencil plate's style.
"""

import math
import os
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "bf", "fonts", "Oswald-Bold.ttf")
OUT = os.path.join(ROOT, "runs", "discord-art", "original")
S = 2048  # drawn at twice the size, then scaled down for smooth edges
C = S // 2
random.seed(1943)


def font(px):
    return ImageFont.truetype(FONT, px)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def vgradient(w, h, stops):
    im = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / (h - 1)
        c = stops[-1][1]
        for i in range(len(stops) - 1):
            if stops[i][0] <= t <= stops[i + 1][0]:
                u = (t - stops[i][0]) / max(stops[i + 1][0] - stops[i][0], 1e-6)
                c = lerp(stops[i][1], stops[i + 1][1], u)
                break
        d.line([(0, y), (w, y)], fill=c)
    return im


def disc(size, cx, cy, r):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    return m


def noise(size, sigma, blur=0):
    n = Image.effect_noise(size, sigma)
    return n.filter(ImageFilter.GaussianBlur(blur)) if blur else n


def bezier(p0, p1, p2, n=160):
    out = []
    for i in range(n):
        t = i / (n - 1)
        out.append(((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0],
                    (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]))
    return out


def tapered(d, pts, w0, w1, fill):
    for i, (x, y) in enumerate(pts):
        r = (w0 + (w1 - w0) * i / (len(pts) - 1)) / 2
        d.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def frond(d, base, ctrl, tip, width, fill):
    """A drooping palm leaf: a curve with leaflets cut along both edges."""
    pts = bezier(base, ctrl, tip, 140)
    upper, lower = [], []
    for i, (x, y) in enumerate(pts):
        t = i / (len(pts) - 1)
        a, b = pts[max(i - 1, 0)], pts[min(i + 1, len(pts) - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dy) or 1
        nx, ny = -dy / ln, dx / ln
        w = width * math.sin(math.pi * t) ** 0.55 * (0.3 + 0.7 * ((t * 9) % 1.0))
        upper.append((x + nx * w, y + ny * w))
        lower.append((x - nx * w * 0.8, y - ny * w * 0.8))
    d.polygon(upper + lower[::-1], fill=fill)


def palm(d, foot, top, bend, fronds, ink):
    tapered(d, bezier(foot, bend, top, 200), 56, 26, ink)
    for ang, length, droop in fronds:
        a = math.radians(ang)
        tip = (top[0] + math.cos(a) * length, top[1] + math.sin(a) * length + droop)
        mid = (top[0] + math.cos(a) * length * 0.55, top[1] + math.sin(a) * length * 0.55 - abs(droop) * 0.35)
        frond(d, top, mid, tip, 58, ink)
    d.ellipse([top[0] - 34, top[1] - 30, top[0] + 34, top[1] + 30], fill=ink)


def fighter(span_px, ink):
    """A generic single engine 1940s fighter seen from above, nose up (drawn here, not traced)."""
    k = span_px / 124.0
    pad = 70
    w = int(124 * k) + pad * 2
    im = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    o = w / 2

    def P(pts):
        return [(o + x * k, o + y * k) for x, y in pts]

    half = [(0, -50), (4.5, -49), (7.5, -45), (8.5, -36), (8.5, -18), (7.5, 2), (5.5, 24), (3.2, 44), (2, 52), (0, 53)]
    d.polygon(P(half + [(-x, y) for x, y in reversed(half)]), fill=ink)
    wing = [(-62, -9), (-59, -14), (-30, -19), (0, -21), (30, -19), (59, -14), (62, -9), (60, -5),
            (24, 1), (0, 3), (-24, 1), (-60, -5)]
    d.polygon(P(wing), fill=ink)
    tail = [(-21, 45), (-19, 40), (0, 37), (19, 40), (21, 45), (19, 49), (0, 50), (-19, 49)]
    d.polygon(P(tail), fill=ink)
    d.rectangle(P([(-15, -51.5), (15, -49.5)]), fill=ink)  # propeller
    return im


def arc_text(img, text, radius, center_deg, fnt, fill, bottom=False, track=1.0):
    """Letters set on a circle around the image center, upright at the top or at the bottom."""
    d = ImageDraw.Draw(img)
    widths = [d.textlength(ch, font=fnt) * track for ch in text]
    total = sum(widths) / radius
    if bottom:
        ang = math.radians(center_deg) + total / 2
    else:
        ang = math.radians(center_deg) - total / 2
    asc = fnt.getbbox("H")[3]
    for ch, wch in zip(text, widths):
        step = wch / radius
        mid = ang - step / 2 if bottom else ang + step / 2
        box = int(asc * 2.2)
        g = Image.new("RGBA", (box, box), (0, 0, 0, 0))
        gd = ImageDraw.Draw(g)
        gd.text((box / 2, box / 2), ch, font=fnt, fill=fill, anchor="mm")
        deg = math.degrees(mid)
        g = g.rotate(-(deg - 90) if bottom else -(deg + 90), resample=Image.BICUBIC)
        x = C + math.cos(mid) * radius - box / 2
        y = C + math.sin(mid) * radius - box / 2
        img.alpha_composite(g, (int(x), int(y)))
        ang = ang - step if bottom else ang + step


# ---------------------------------------------------------------- icon A: the island badge

def icon_a():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ring_out, ring_in, field = 1010, 862, 842
    brass = vgradient(S, S, [(0, (246, 214, 138)), (0.5, (200, 150, 66)), (1, (122, 82, 30))]).convert("RGBA")
    img.paste(brass, (0, 0), disc((S, S), C, C, ring_out))
    ImageDraw.Draw(img).ellipse([C - ring_in, C - ring_in, C + ring_in, C + ring_in], fill=(26, 20, 14, 255))

    horizon = 1236
    scene = vgradient(S, S, [(0, (36, 22, 58)), (0.28, (122, 40, 52)), (0.5, (224, 92, 38)),
                             (horizon / S, (252, 184, 72)), (horizon / S + 0.001, (24, 52, 70)),
                             (1, (8, 18, 28))]).convert("RGBA")
    sd = ImageDraw.Draw(scene)
    sun_r = 360
    sun = vgradient(S, S, [(0, (255, 236, 160)), (0.62, (255, 190, 90)), (1, (240, 120, 40))]).convert("RGBA")
    sun_mask = disc((S, S), C + 60, horizon, sun_r)
    sun_mask.paste(0, (0, horizon, S, S))
    scene.paste(sun, (0, 0), sun_mask)
    # the sun on the water: short gold strokes, narrower further down
    for i in range(9):
        y = horizon + 26 + i * 44
        half = (sun_r * 0.95) * (1 - i / 10.5)
        sd.rounded_rectangle([C + 60 - half, y, C + 60 + half, y + 16 - i * 0.8], radius=8,
                             fill=(252, 180, 80, 255 - i * 22))
    ink = (16, 12, 20, 255)
    island = [(150, horizon + 6), (300, horizon - 40), (470, horizon - 74), (640, horizon - 88), (780, horizon - 66),
              (930, horizon - 30), (1060, horizon + 6)]
    sd.polygon(island, fill=ink)
    sd.rectangle([150, horizon - 2, 1060, horizon + 10], fill=ink)
    palm(sd, (560, horizon - 70), (420, 610), (600, 900),
         [(-170, 330, 120), (-140, 300, 60), (-95, 220, 40), (-40, 310, 70), (-5, 330, 150), (160, 260, 170)], ink)
    palm(sd, (720, horizon - 70), (830, 790), (690, 1000),
         [(-160, 250, 120), (-110, 210, 40), (-60, 220, 40), (-15, 260, 110), (20, 220, 160)], ink)
    plane = fighter(640, ink).rotate(38, resample=Image.BICUBIC, expand=True)
    scene.alpha_composite(plane, (1340 - plane.width // 2, 560 - plane.height // 2))
    img.paste(scene, (0, 0), disc((S, S), C, C, field))

    # the banner: 1943
    d = ImageDraw.Draw(img)
    by0, by1, bx0, bx1 = 1430, 1700, 360, 1688
    tails = [(bx0 - 150, by0 + 50), (bx0 + 10, by0 + 50), (bx0 + 10, by1 + 50), (bx0 - 150, by1 + 50),
             (bx0 - 70, (by0 + by1) / 2 + 50)]
    for pts in (tails, [(S - x, y) for x, y in tails]):
        d.polygon(pts, fill=(70, 74, 38, 255))
    d.polygon([(bx0 - 150 + 140, by1 + 50), (bx0 + 10, by1), (bx0 + 10, by1 + 50)], fill=(36, 38, 18, 255))
    d.polygon([(S - bx0 + 150 - 140, by1 + 50), (S - bx0 - 10, by1), (S - bx0 - 10, by1 + 50)], fill=(36, 38, 18, 255))
    d.rectangle([bx0, by0, bx1, by1], fill=(98, 104, 52, 255))
    d.rectangle([bx0, by0, bx1, by0 + 14], fill=(130, 136, 72, 255))
    d.rectangle([bx0 + 26, by0 + 30, bx1 - 26, by1 - 30], outline=(214, 196, 132, 255), width=8)
    d.text((C, (by0 + by1) / 2 + 6), "1943", font=font(250), fill=(238, 224, 176, 255), anchor="mm")

    engrave = (64, 38, 10, 255)
    arc_text(img, "PACIFIC  THEATER", 936, -90, font(104), engrave, track=1.12)
    arc_text(img, "REBORN", 936, 90, font(110), engrave, bottom=True, track=1.25)
    for deg in (-148, -32):
        x, y = C + math.cos(math.radians(deg)) * 936, C + math.sin(math.radians(deg)) * 936
        star(d, x, y, 34, engrave)
    return img


def star(d, x, y, r, fill, rot=-90):
    pts = []
    for i in range(10):
        rr = r if i % 2 == 0 else r * 0.42
        a = math.radians(rot + i * 36)
        pts.append((x + math.cos(a) * rr, y + math.sin(a) * rr))
    d.polygon(pts, fill=fill)


# ---------------------------------------------------------------- icon B: the stencil plate

def icon_b():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    m, rad = 36, 250
    d.rounded_rectangle([m, m, S - m, S - m], radius=rad, fill=(30, 32, 20, 255))
    plate = vgradient(S, S, [(0, (112, 118, 72)), (0.55, (84, 90, 52)), (1, (60, 64, 36))]).convert("RGBA")
    grain = noise((S, S), 40, 1.2).convert("RGBA")
    plate = Image.blend(plate, Image.merge("RGBA", (grain.split()[0],) * 3 + (Image.new("L", (S, S), 255),)), 0.07)
    pm = Image.new("L", (S, S), 0)
    ImageDraw.Draw(pm).rounded_rectangle([m + 24, m + 24, S - m - 24, S - m - 24], radius=rad - 24, fill=255)
    img.paste(plate, (0, 0), pm)
    d.rounded_rectangle([m + 40, m + 40, S - m - 40, S - m - 40], radius=rad - 40, outline=(138, 142, 92, 255), width=6)

    for x, y in ((210, 210), (S - 210, 210), (210, S - 210), (S - 210, S - 210)):
        d.ellipse([x - 46, y - 46, x + 46, y + 46], fill=(38, 40, 24, 255))
        d.ellipse([x - 38, y - 40, x + 38, y + 36], fill=(122, 126, 82, 255))
        d.ellipse([x - 22, y - 28, x + 8, y - 4], fill=(168, 170, 122, 255))

    sight = (56, 60, 34, 255)
    cy = 890
    d.ellipse([C - 700, cy - 700, C + 700, cy + 700], outline=sight, width=22)
    d.ellipse([C - 520, cy - 520, C + 520, cy + 520], outline=sight, width=10)
    for x0, y0, x1, y1 in ((C - 820, cy, C - 560, cy), (C + 560, cy, C + 820, cy),
                           (C, cy - 820, C, cy - 560), (C, cy + 560, C, cy + 820)):
        d.line([(x0, y0), (x1, y1)], fill=sight, width=22)

    # the stencil 43: each digit gets the gaps a cut stencil needs, then the paint wears
    paint = Image.new("L", (S, S), 0)
    pd = ImageDraw.Draw(paint)
    f = font(1040)
    gap = 40
    gx = [C - 320, C + 320]
    for ch, x in zip("43", gx):
        pd.text((x, cy + 20), ch, font=f, fill=255, anchor="mm")
        l, t, r, b = pd.textbbox((x, cy + 20), ch, font=f, anchor="mm")
        w, h = r - l, b - t
        if ch == "4":  # the bar cut from the stem, so the inside of the 4 would stay in a real stencil
            pd.rectangle([l + w * 0.465 - gap, t + h * 0.58, l + w * 0.465, t + h * 0.82], fill=0)
        else:  # the 3 cut at its waist, top half from bottom half
            pd.rectangle([l + w * 0.5, t + h * 0.5 - gap / 2, r + 10, t + h * 0.5 + gap / 2], fill=0)
    wear = noise((S, S), 90, 2.5).point(lambda v: 0 if v > 172 else 255)
    paint = ImageChops.multiply(paint, wear)
    shadow = paint.filter(ImageFilter.GaussianBlur(10)).point(lambda v: int(v * 0.55))
    img.paste((20, 22, 12, 255), (10, 14), shadow)
    img.paste((232, 220, 172, 255), (0, 0), paint)

    # the red stamp: REBORN
    stamp = Image.new("L", (1300, 420), 0)
    sdr = ImageDraw.Draw(stamp)
    sdr.rounded_rectangle([20, 20, 1280, 400], radius=34, outline=255, width=30)
    sdr.text((650, 214), "REBORN", font=font(280), fill=255, anchor="mm")
    stamp = ImageChops.multiply(stamp, noise(stamp.size, 110, 1.6).point(lambda v: 0 if v > 168 else 255))
    stamp = stamp.rotate(6, resample=Image.BICUBIC, expand=True)
    sx, sy = C - stamp.width // 2, 1660 - stamp.height // 2
    img.paste((186, 34, 30, 255), (sx, sy), stamp)
    return img


# ---------------------------------------------------------------- the profile banner (680 x 240), in the plate's style

BANNER = dict(size=(680, 240), seam=16, rivet=6, rivet_step=76, sight=(590, 122, 92, 62), sight_w=4,
              planes=((150, 70, 92), (92, 46, 70), (98, 108, 70)), title=(402, 34, 17), digits=(402, 118, 150, 8),
              gap=6, stamp=(508, 192, 190, 62, 42, 5))
COVER = dict(size=(1024, 576), seam=22, rivet=8, rivet_step=96, sight=(812, 300, 196, 132), sight_w=5,
             planes=((240, 150, 150), (150, 108, 112), (160, 204, 112)), title=(512, 160, 30), digits=(512, 312, 250, 12),
             gap=9, stamp=(660, 462, 300, 96, 66, 5))


def plate_art(L):
    """The stencil plate's style on a wide picture: the profile banner and the Rich Presence cover."""
    k = 4  # drawn at four times the size
    W, H = L["size"][0] * k, L["size"][1] * k
    img = vgradient(W, H, [(0, (110, 116, 70)), (0.6, (82, 88, 50)), (1, (58, 62, 36))]).convert("RGBA")
    grain = noise((W, H), 40, 1.2)
    img = Image.blend(img, Image.merge("RGBA", (grain,) * 3 + (Image.new("L", (W, H), 255),)), 0.07)
    d = ImageDraw.Draw(img)
    seam, rv = L["seam"] * k, L["rivet"] * k
    for y0, y1 in ((0, seam), (H - seam, H)):  # the plate's seams, riveted
        d.rectangle([0, y0, W, y1], fill=(46, 48, 28, 255))
        edge = y1 if y0 == 0 else y0
        d.line([(0, edge), (W, edge)], fill=(132, 136, 88, 255), width=k)
        cy = (y0 + y1) // 2
        for x in range(L["rivet_step"] * k // 3, W, L["rivet_step"] * k):
            d.ellipse([x - rv, cy - rv, x + rv, cy + rv], fill=(36, 38, 22, 255))
            d.ellipse([x - rv * 0.83, cy - rv * 0.9, x + rv * 0.83, cy + rv * 0.67], fill=(122, 126, 82, 255))
            d.ellipse([x - rv * 0.5, cy - rv * 0.67, x + rv * 0.08, cy - rv * 0.25], fill=(168, 170, 122, 255))

    sight = (60, 64, 36, 255)
    sx, sy, r1, r2 = (v * k for v in L["sight"])
    sw = L["sight_w"] * k
    d.ellipse([sx - r1, sy - r1, sx + r1, sy + r1], outline=sight, width=sw)
    d.ellipse([sx - r2, sy - r2, sx + r2, sy + r2], outline=sight, width=sw // 2)
    o, i = r1 + r1 * 0.22, r1 - r1 * 0.22
    for a, b in (((sx - o, sy), (sx - i, sy)), ((sx + i, sy), (sx + o, sy)),
                 ((sx, sy - o), (sx, sy - i)), ((sx, sy + i), (sx, sy + o))):
        d.line([a, b], fill=sight, width=sw)

    # three fighters in formation, heading right, painted darker than the plate
    ink = (50, 54, 30, 255)
    for x, y, span in L["planes"]:
        pl = fighter(span * k, ink).rotate(-78, resample=Image.BICUBIC, expand=True)
        img.alpha_composite(pl, (x * k - pl.width // 2, y * k - pl.height // 2))

    cream = (232, 220, 172, 255)
    tx, ty, ts = L["title"]
    d.text((tx * k, ty * k), "P A C I F I C   T H E A T E R", font=font(ts * k), fill=cream, anchor="mm")
    # 1943 in stencil, each digit with the gaps a cut stencil needs, then worn
    paint = Image.new("L", (W, H), 0)
    pd = ImageDraw.Draw(paint)
    dx, dy, dsize, dtrack = L["digits"]
    f = font(dsize * k)
    text, track = "1943", dtrack * k
    total = sum(pd.textlength(ch, font=f) for ch in text) + track * (len(text) - 1)
    x = dx * k - total / 2
    gap = L["gap"] * k
    for ch in text:
        pd.text((x, dy * k), ch, font=f, fill=255, anchor="lm")
        l, t, r, b = pd.textbbox((x, dy * k), ch, font=f, anchor="lm")
        w, h = r - l, b - t
        if ch == "4":
            pd.rectangle([l + w * 0.465 - gap, t + h * 0.58, l + w * 0.465, t + h * 0.82], fill=0)
        elif ch == "3":
            pd.rectangle([l + w * 0.5, t + h * 0.5 - gap / 2, r + 4, t + h * 0.5 + gap / 2], fill=0)
        elif ch == "9":
            pd.rectangle([l - 4, t + h * 0.30 - gap / 2, l + w * 0.5, t + h * 0.30 + gap / 2], fill=0)
        x += pd.textlength(ch, font=f) + track
    paint = ImageChops.multiply(paint, noise((W, H), 90, 2.5).point(lambda v: 0 if v > 172 else 255))
    shadow = paint.filter(ImageFilter.GaussianBlur(2 * k)).point(lambda v: int(v * 0.55))
    img.paste((20, 22, 12, 255), (2 * k, 3 * k), shadow)
    img.paste(cream, (0, 0), paint)

    cx, cy, sw_, sh_, sf, srot = L["stamp"]
    stamp = Image.new("L", (sw_ * k, sh_ * k), 0)
    sdr = ImageDraw.Draw(stamp)
    bw = max(5, sh_ // 12) * k
    sdr.rounded_rectangle([bw // 2, bw // 2, sw_ * k - bw // 2, sh_ * k - bw // 2], radius=sh_ * k // 10, outline=255,
                          width=bw)
    sdr.text((sw_ * k // 2, sh_ * k // 2 + k), "REBORN", font=font(sf * k), fill=255, anchor="mm")
    stamp = ImageChops.multiply(stamp, noise(stamp.size, 110, 1.6).point(lambda v: 0 if v > 168 else 255))
    stamp = stamp.rotate(srot, resample=Image.BICUBIC, expand=True)
    img.paste((186, 34, 30, 255), (cx * k - stamp.width // 2, cy * k - stamp.height // 2), stamp)
    return img.resize(L["size"], Image.LANCZOS).convert("RGB")


# ---------------------------------------------------------------- the preview: both at Discord's sizes

def preview(icons):
    bg, card, text, sub = (49, 51, 56), (43, 45, 49), (242, 243, 245), (181, 186, 193)
    W, H = 1500, 520 * len(icons) + 60
    sheet = Image.new("RGB", (W, H), bg)
    d = ImageDraw.Draw(sheet)
    for row, (name, im) in enumerate(icons):
        y0 = 40 + row * 520
        d.text((40, y0), name, font=font(40), fill=text)
        big = im.resize((400, 400), Image.LANCZOS)
        sheet.paste(big, (40, y0 + 70), big)
        # a profile card as Discord draws it: the large image in a rounded square, the logo small in a circle
        cx0, cy0 = 500, y0 + 70
        d.rounded_rectangle([cx0, cy0, cx0 + 960, cy0 + 200], radius=16, fill=card)
        d.text((cx0 + 24, cy0 + 16), "PLAYING A GAME", font=font(22), fill=sub)
        lg = im.resize((120, 120), Image.LANCZOS)
        lm = Image.new("L", (120, 120), 0)
        ImageDraw.Draw(lm).rounded_rectangle([0, 0, 119, 119], radius=10, fill=255)
        sheet.paste(lg, (cx0 + 24, cy0 + 56), ImageChops.multiply(lm, lg.split()[3]))
        d.text((cx0 + 168, cy0 + 58), "Battlefield 1943 Reborn", font=font(30), fill=text)
        d.text((cx0 + 168, cy0 + 100), "Main menu", font=font(26), fill=sub)
        d.text((cx0 + 168, cy0 + 136), "00:12 elapsed", font=font(26), fill=sub)
        # the sizes it shrinks to: 64, 40 and 24
        x = cx0
        for sz in (96, 64, 40, 24):
            s = im.resize((sz, sz), Image.LANCZOS)
            sheet.paste(s, (x, cy0 + 260 + (96 - sz)), s)
            d.text((x, cy0 + 370), f"{sz} px", font=font(22), fill=sub)
            x += sz + 60
    return sheet


def main():
    os.makedirs(OUT, exist_ok=True)
    made = []
    for name, fn, file in (("A  Island badge", icon_a, "icon_a_island.png"),
                           ("B  Stencil plate", icon_b, "icon_b_stencil.png")):
        im = fn().resize((1024, 1024), Image.LANCZOS)
        im.save(os.path.join(OUT, file))
        made.append((name, im))
        print("wrote", os.path.join(OUT, file))
    preview(made).save(os.path.join(OUT, "preview.png"))
    print("wrote", os.path.join(OUT, "preview.png"))
    for name, layout in (("banner.png", BANNER), ("cover.png", COVER)):
        plate_art(layout).save(os.path.join(OUT, name))
        print("wrote", os.path.join(OUT, name))


if __name__ == "__main__":
    main()
