# The Discord Developer Portal's Rich Presence art (docs/DISCORD-PRESENCE.md) from the game's own menu art, as the
# game extracts it into a user folder's cache (never shipped or committed: the output goes under runs/).
#   python -I tools/make_discord_art.py [<user>/cache/extracted] [out folder]
# Needs Pillow and runs/ui-designs/gamefont.py (the game's fonts) for the REBORN lettering.
import os
import sys
sys.path.insert(0, 'F:/BF-1942-MP-Decomp/runs/ui-designs')
import gamefont
from PIL import Image, ImageFilter, ImageEnhance
SRC = sys.argv[1] if len(sys.argv) > 1 else 'F:/BF-1942-MP-Decomp/runs/aimtoggle1/user/cache/extracted'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'F:/BF-1942-MP-Decomp/runs/discord-art'
os.makedirs(OUT, exist_ok=True)
S = 1024
# logo: the menu backdrop, darkened and blurred, the game's logo with REBORN in the launcher's gold beneath.
bd = Image.open(f'{SRC}/menu/backdrop.png').convert('RGB')
side = min(bd.size)
bd = bd.crop(((bd.width - side) // 2, 0, (bd.width + side) // 2, side)).resize((S, S), Image.LANCZOS)
bd = ImageEnhance.Brightness(bd.filter(ImageFilter.GaussianBlur(6))).enhance(0.35).convert('RGBA')
logo = Image.open(f'{SRC}/menu/logo.png').convert('RGBA')
lw = int(S * 0.86)
logo = logo.resize((lw, int(logo.height * lw / logo.width)), Image.LANCZOS)
word = gamefont.render('headline_medium', 'REBORN', 1.0, 6, (201, 183, 122))
ww = int(S * 0.50)
word = word.resize((ww, int(word.height * ww / word.width)), Image.LANCZOS)
total = logo.height + 40 + word.height
y = (S - total) // 2
bd.alpha_composite(logo, ((S - logo.width) // 2, y))
bd.alpha_composite(word, ((S - word.width) // 2, y + logo.height + 40))
bd.convert('RGB').save(f'{OUT}/logo.png')
# the maps: each loading picture's middle square, at 512 (Discord's smallest for an asset).
for m in ['wake', 'guadalcanal', 'iwo', 'coral']:
    im = Image.open(f'{SRC}/maps/{m}.png').convert('RGB')
    h = im.height
    im = im.crop(((im.width - h) // 2, 0, (im.width + h) // 2, h)).resize((512, 512), Image.LANCZOS)
    im.save(f'{OUT}/map_{m}.png')
print('done')
