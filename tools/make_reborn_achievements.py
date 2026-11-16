"""The pictures of Reborn's own achievements (bf/src/reborn_achievements.cpp): 64 x 64 RGBA PNGs, the size and format
of the game's own achievement pictures, as perforated postage stamps with a small printed scene of our own drawing.
Nothing here comes from the game: every shape is drawn below.

    python tools/make_reborn_achievements.py            writes bf/data/achievements/101.png, 102.png, 103.png
    python tools/make_reborn_achievements.py --sheet x.png   also a sheet of the three at 4x, for a look

Needs Pillow. Each picture is drawn at 8x and scaled down, so the edges are smooth.
"""

import argparse
import math
import os
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SIZE = 64
SS = 8                      # supersampling
S = SIZE * SS
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'bf', 'data', 'achievements')

PAPER = (243, 240, 230, 255)
FRAME = (52, 50, 44, 255)


def px(v):
    return int(round(v * SS))


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def vertical_gradient(w, h, top, bottom):
    g = Image.new('RGBA', (w, h))
    d = ImageDraw.Draw(g)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=lerp(top, bottom, y / max(1, h - 1)))
    return g


def radial_glow(w, h, cx, cy, r, colour):
    """A soft round light: colour at the centre, fading out by radius r."""
    mask = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(mask)
    steps = 40
    for i in range(steps, 0, -1):
        t = i / steps
        d.ellipse([cx - r * t, cy - r * t, cx + r * t, cy + r * t], fill=int(255 * (1 - t) ** 1.6))
    layer = Image.new('RGBA', (w, h), colour)
    layer.putalpha(mask)
    return layer


def stamp_mask():
    """The stamp's outline: a square with round bites out of its edges (the perforation)."""
    m = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(m)
    inset = px(1)
    d.rectangle([inset, inset, S - inset - 1, S - inset - 1], fill=255)
    holes = 9
    r = px(1.75)
    span = S - 2 * inset
    for i in range(holes + 1):
        c = inset + span * i / holes
        for x, y in ((c, inset), (c, S - inset - 1), (inset, c), (S - inset - 1, c)):
            d.ellipse([x - r, y - r, x + r, y + r], fill=0)
    return m


def grain(img, amount, seed):
    """Printed paper: a fine, faint noise over the scene."""
    rnd = random.Random(seed)
    w, h = img.size
    n = Image.new('L', (w // SS, h // SS))
    n.putdata([128 + int(rnd.gauss(0, amount)) for _ in range(n.size[0] * n.size[1])])
    n = n.resize((w, h), Image.BILINEAR)
    rgb = img.convert('RGB')
    noisy = ImageChops.add(rgb, Image.merge('RGB', (n, n, n)), 1.0, -128)
    out = noisy.convert('RGBA')
    out.putalpha(img.getchannel('A'))
    return out


def tone(img, sat):
    """The muted, slightly warm print of a wartime stamp: colour pulled toward a sepia grey."""
    rgb = img.convert('RGB')
    grey = rgb.convert('L')
    sepia = Image.merge('RGB', (grey.point(lambda v: min(255, int(v * 1.04 + 6))),
                                grey.point(lambda v: int(v * 0.98 + 2)),
                                grey.point(lambda v: int(v * 0.86))))
    out = Image.blend(sepia, rgb, sat).convert('RGBA')
    out.putalpha(img.getchannel('A'))
    return out


def vignette(img, strength):
    w, h = img.size
    mask = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(mask)
    steps = 30
    for i in range(steps):
        t = i / steps
        a = int(255 * strength * (1 - t) ** 2)
        d.rectangle([w * t / 2.6, h * t / 2.6, w - w * t / 2.6, h - h * t / 2.6], outline=a, width=max(1, int(w / 2.6 / steps) + 1))
    mask = mask.filter(ImageFilter.GaussianBlur(w / 30))
    dark = Image.new('RGBA', (w, h), (20, 18, 12, 255))
    dark.putalpha(mask)
    return Image.alpha_composite(img, dark)


# ---------------------------------------------------------------- the three scenes (drawn on the inner picture)

def feather(d, root, angle, length, width, fill):
    """One long feather: a pointed leaf from `root` along `angle` (radians)."""
    x, y = root
    ca, sa = math.cos(angle), math.sin(angle)
    nx, ny = -sa, ca
    pts = []
    for k in range(13):
        t = k / 12
        half = width * math.sin(math.pi * t) ** 0.8 * (1 - 0.35 * t)
        pts.append((x + ca * length * t + nx * half, y + sa * length * t + ny * half))
    for k in range(12, -1, -1):
        t = k / 12
        half = width * math.sin(math.pi * t) ** 0.8 * (1 - 0.35 * t)
        pts.append((x + ca * length * t - nx * half * 0.4, y + sa * length * t - ny * half * 0.4))
    d.polygon(pts, fill=fill)


def scene_reborn(w, h):
    """I am reborn: a phoenix rising out of its flames, wings up, on a deep red field."""
    img = vertical_gradient(w, h, (70, 34, 30, 255), (150, 64, 36, 255))
    img = Image.alpha_composite(img, radial_glow(w, h, w * 0.5, h * 0.78, w * 0.62, (246, 160, 72, 255)))
    d = ImageDraw.Draw(img)
    cx = w * 0.5
    # the flames at the foot: tongues of fire, dark to light
    for colour, scale in (((196, 74, 34, 255), 1.0), ((232, 136, 54, 255), 0.72), ((250, 206, 120, 255), 0.42)):
        pts = [(0, h)]
        tongues = 7
        for i in range(tongues * 2 + 1):
            t = i / (tongues * 2)
            x = w * t
            peak = (i % 2 == 1)
            hgt = h * (0.30 if peak else 0.12) * scale * (1.2 - abs(t - 0.5) * 1.2)
            pts.append((x, h - hgt))
        pts.append((w, h))
        d.polygon(pts, fill=colour)
    # the wings: feathers fanning up and out from the shoulders
    gold, light, deep = (238, 196, 110, 255), (252, 232, 170, 255), (204, 142, 66, 255)
    sy = h * 0.47
    for side in (-1, 1):
        root = (cx + side * w * 0.05, sy)
        for i, (deg, ln) in enumerate(((20, 0.40), (38, 0.44), (56, 0.43), (74, 0.38), (90, 0.30))):
            a = math.radians(-90 + side * (90 - deg))
            feather(d, root, a, w * ln, w * 0.075, deep if i % 2 else gold)
        for i, (deg, ln) in enumerate(((30, 0.26), (52, 0.28), (74, 0.24))):
            a = math.radians(-90 + side * (90 - deg))
            feather(d, root, a, w * ln, w * 0.05, light)
    # the body and the head, rising
    d.ellipse([cx - w * 0.07, sy - h * 0.08, cx + w * 0.07, sy + h * 0.16], fill=gold)
    d.ellipse([cx - w * 0.055, sy - h * 0.20, cx + w * 0.055, sy - h * 0.09], fill=light)
    d.polygon([(cx - w * 0.02, sy - h * 0.165), (cx + w * 0.02, sy - h * 0.165), (cx, sy - h * 0.235)], fill=light)  # the crest
    d.polygon([(cx + w * 0.045, sy - h * 0.15), (cx + w * 0.10, sy - h * 0.135), (cx + w * 0.045, sy - h * 0.12)], fill=deep)  # the beak
    # the tail, three long plumes falling into the fire
    for k, dx in enumerate((-0.07, 0.0, 0.07)):
        feather(d, (cx, sy + h * 0.12), math.radians(90 + dx * 260), h * (0.30 if k == 1 else 0.24), w * 0.05, deep if k == 1 else gold)
    return img


def scene_forever(w, h):
    """I am forever: an endless loop in gold between two laurel branches, on a deep green field."""
    img = vertical_gradient(w, h, (58, 82, 64, 255), (30, 46, 36, 255))
    img = Image.alpha_composite(img, radial_glow(w, h, w * 0.5, h * 0.45, w * 0.6, (120, 150, 104, 255)))
    d = ImageDraw.Draw(img)
    cx, cy = w * 0.5, h * 0.47
    # laurel: two arcs of leaves meeting at the foot
    leaf = (176, 182, 120, 255)
    leaf_dark = (120, 130, 82, 255)
    for side in (-1, 1):
        for i in range(7):
            t = i / 6
            a = math.radians(115 + 120 * t) if side < 0 else math.radians(65 - 120 * t)
            R = w * 0.40
            x, y = cx + R * math.cos(a), cy + h * 0.05 + R * math.sin(a) * 0.95
            ang = a + (math.pi / 2) * side
            lw, lh = w * 0.085, w * 0.036
            pts = []
            for k in range(16):
                u = 2 * math.pi * k / 16
                ex, ey = lw * math.cos(u), lh * math.sin(u)
                pts.append((x + ex * math.cos(ang) - ey * math.sin(ang), y + ex * math.sin(ang) + ey * math.cos(ang)))
            d.polygon(pts, fill=leaf if i % 2 else leaf_dark)
    # the loop: a lemniscate drawn as a thick stroke, a darker shadow under it
    def loop(scale, dx, dy):
        pts = []
        for k in range(240):
            t = 2 * math.pi * k / 240
            den = 1 + math.sin(t) ** 2
            x = math.cos(t) / den
            y = math.sin(t) * math.cos(t) / den
            pts.append((cx + dx + x * w * scale, cy + dy + y * w * scale * 1.15))
        return pts + pts[:1]
    d.line(loop(0.33, w * 0.012, h * 0.018), fill=(24, 30, 22, 255), width=int(w * 0.095), joint='curve')
    d.line(loop(0.33, 0, 0), fill=(214, 172, 84, 255), width=int(w * 0.085), joint='curve')
    d.line(loop(0.33, -w * 0.006, -h * 0.010), fill=(246, 214, 132, 255), width=int(w * 0.035), joint='curve')
    # three small stars over it: always on
    for i, sx in enumerate((0.34, 0.5, 0.66)):
        star(d, w * sx, h * (0.15 if i == 1 else 0.19), w * (0.045 if i == 1 else 0.035), (238, 226, 184, 255))
    return img


def star(d, x, y, r, fill, inner=0.42, rot=-math.pi / 2):
    pts = []
    for k in range(10):
        rr = r if k % 2 == 0 else r * inner
        a = rot + math.pi * k / 5
        pts.append((x + rr * math.cos(a), y + rr * math.sin(a)))
    d.polygon(pts, fill=fill)


def scene_inevitable(w, h):
    """I am inevitable: the host's own command flag, a white star on red, planted on a hill over the sea at dusk."""
    img = vertical_gradient(w, h, (104, 62, 58, 255), (214, 150, 108, 255))
    img = Image.alpha_composite(img, radial_glow(w, h, w * 0.30, h * 0.62, w * 0.5, (250, 196, 140, 255)))
    d = ImageDraw.Draw(img)
    # the sea
    d.rectangle([0, h * 0.70, w, h], fill=(70, 78, 86, 255))
    for i in range(5):
        y = h * (0.74 + 0.05 * i)
        d.line([(w * (0.05 + 0.13 * (i % 2)), y), (w * (0.32 + 0.1 * (i % 3)), y)], fill=(150, 140, 128, 255), width=max(1, int(h * 0.012)))
    # the hill
    d.polygon([(w * 0.30, h), (w * 0.52, h * 0.66), (w * 0.62, h * 0.62), (w * 0.74, h * 0.64), (w, h * 0.74), (w, h)], fill=(52, 50, 36, 255))
    # the pole
    px0, top, foot = w * 0.62, h * 0.12, h * 0.64
    d.rectangle([px0 - w * 0.014, top, px0 + w * 0.014, foot], fill=(36, 32, 26, 255))
    d.ellipse([px0 - w * 0.03, top - w * 0.03, px0 + w * 0.03, top + w * 0.03], fill=(206, 176, 96, 255))
    # the flag, waving: a strip whose edge follows a sine
    fw, fh = w * 0.34, h * 0.24
    x0, y0 = px0 + w * 0.012, top + h * 0.03
    upper, lower = [], []
    for k in range(25):
        t = k / 24
        wave = math.sin(t * math.pi * 1.6) * h * 0.03 * t
        upper.append((x0 + fw * t, y0 + wave))
        lower.append((x0 + fw * t, y0 + fh + wave * 0.8 - fh * 0.06 * t))
    d.polygon(upper + lower[::-1], fill=(168, 44, 38, 255))
    d.line(upper, fill=(204, 80, 64, 255), width=max(1, int(h * 0.015)))
    star(d, x0 + fw * 0.46, y0 + fh * 0.50 + math.sin(0.46 * math.pi * 1.6) * h * 0.03 * 0.46, fh * 0.34, (244, 236, 214, 255))
    return img


SCENES = [
    (101, scene_reborn, 0.80),
    (102, scene_forever, 0.78),
    (103, scene_inevitable, 0.80),
]


def make(scene, sat, seed):
    out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    paper = Image.new('RGBA', (S, S), PAPER)
    out.paste(paper, (0, 0), stamp_mask())
    # the picture inside a thin dark frame, as the game's stamps have
    m0, m1 = px(7), S - px(7)
    d = ImageDraw.Draw(out)
    d.rectangle([m0 - px(0.9), m0 - px(0.9), m1 + px(0.9) - 1, m1 + px(0.9) - 1], fill=FRAME)
    pic = scene(m1 - m0, m1 - m0)
    pic = vignette(pic, 0.45)
    pic = tone(pic, sat)
    out.paste(pic, (m0, m0))
    out = grain(out, 6, seed)
    # a faint shade at the paper's edge, so the stamp reads on light and dark rows alike
    small = out.resize((SIZE, SIZE), Image.LANCZOS)
    return small


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default=OUT, help='where the PNGs go (bf/data/achievements)')
    ap.add_argument('--sheet', default='', help='also write a sheet of the three at 4x here')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    made = []
    for ident, scene, sat in SCENES:
        img = make(scene, sat, ident)
        path = os.path.join(args.out, '%d.png' % ident)
        img.save(path, optimize=True)
        made.append(img)
        print('wrote %s (%dx%d RGBA)' % (os.path.normpath(path), img.size[0], img.size[1]))
    if args.sheet:
        sheet = Image.new('RGBA', (len(made) * 4 * SIZE + (len(made) + 1) * 24, 4 * SIZE + 48), (60, 60, 56, 255))
        for i, img in enumerate(made):
            sheet.alpha_composite(img.resize((4 * SIZE, 4 * SIZE), Image.NEAREST), (24 + i * (4 * SIZE + 24), 24))
        sheet.save(args.sheet)
        print('wrote', args.sheet)


if __name__ == '__main__':
    main()
