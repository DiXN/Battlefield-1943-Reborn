"""Our button prompt pictures (bf/glyphs) from Kenney's "Input Prompts" (CC0, https://kenney.nl/assets/input-prompts).

TekRantGaming, 16 Nov 2026: "replace all of our button prompts, glyphs, and game with the following ones. Only take
the ones we currently support". Every picture our launcher, menus and the game's HUD prompts ask for (launcher_kit.cpp
PadName and the keys' names, hud_glyphs.cpp Choose and KeyFor) is taken here from the pack, style by style and key by
key, and nothing else of it. Each is cut from the pack's 128 pixel ("Double") picture, trimmed to what it draws and
made at most 64 pixels on its long side, as the Xelu and Kenney pictures before them were (the menus and the HUD size
a prompt by its height).

The Xbox 360 style keeps the game's own buttons (the pack has no Xbox 360 buttons). The Xbox Series and PlayStation 3
styles use the pack's coloured face buttons, PlayStation 5 and Steam Deck its plain ones, as the pads print them.
WASD is drawn here from the pack's W, A, S and D keys (the pack has no WASD picture). Every picture gets a thin dark
rim in our tape's colour (rim below), so the white ones read on light paper too.

    python -I tools/make_kenney_glyphs.py <the unpacked pack's folder>      (writes bf/glyphs/<set>/*.png)

Run nothing from the pack: it is pictures only, read as data.
"""

import os
import sys

from PIL import Image, ImageFilter

MAX_SIDE = 64
RIM = 5                      # the dark rim, in the pack's 128 pixel pictures (about 2.5 at 64)
RIM_COLOUR = (20, 20, 18)    # our prompts' tape (launcher_kit.h kStrip)
GLYPHS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bf', 'glyphs')


def pads(prefix, face, back, start, dpad, lb, rb, lt, rt):
    """A controller set: our file name -> the pack's name."""
    m = {'a': face[0], 'b': face[1], 'x': face[2], 'y': face[3], 'back': back, 'start': start,
         'dpad': f'{dpad}_dpad', 'dpad_up': f'{dpad}_dpad_up', 'lb': lb, 'rb': rb, 'lt': lt, 'rt': rt}
    # The sticks: "stick" the stick seen from above, "stick_click" pressed, "stick_move" moved, the right stick's
    # own (stick_r*) and the moves' directions (_lr, _ud) where the HUD's prompts name them (hud_glyphs.cpp Choose).
    for side, ours in (('l', 'stick'), ('r', 'stick_r')):
        m[ours] = f'{prefix}_stick_top_{side}'
        m[f'{ours}_click'] = f'{prefix}_stick_{side}_press'
        m[f'{ours}_move'] = f'{prefix}_stick_{side}'
        m[f'{ours}_lr'] = f'{prefix}_stick_{side}_horizontal'
        m[f'{ours}_ud'] = f'{prefix}_stick_{side}_vertical'
    return m


def face(prefix, names, colour):
    return [f'{prefix}_button_{"color_" if colour else ""}{n}' for n in names]


SETS = {
    'xbox_series': ('Xbox Series', pads('xbox', face('xbox', 'abxy', True), 'xbox_button_view', 'xbox_button_menu',
                                        'xbox', 'xbox_lb', 'xbox_rb', 'xbox_lt', 'xbox_rt')),
    # The PlayStation 3 style (King Kong's set name, ps2): the DualShock 3's coloured symbols, Select and Start.
    'ps2': ('PlayStation Series', pads('playstation', face('playstation', ['cross', 'circle', 'square', 'triangle'], True),
                                       'playstation3_button_select', 'playstation3_button_start', 'playstation',
                                       'playstation_trigger_l1', 'playstation_trigger_r1', 'playstation_trigger_l2',
                                       'playstation_trigger_r2')),
    'ps5': ('PlayStation Series', pads('playstation', face('playstation', ['cross', 'circle', 'square', 'triangle'], False),
                                       'playstation5_button_create', 'playstation5_button_options', 'playstation',
                                       'playstation_trigger_l1', 'playstation_trigger_r1', 'playstation_trigger_l2',
                                       'playstation_trigger_r2')),
    'steam_deck': ('Steam Deck', pads('steamdeck', face('steamdeck', 'abxy', False), 'steamdeck_button_view',
                                      'steamdeck_button_options', 'steamdeck', 'steamdeck_button_l1',
                                      'steamdeck_button_r1', 'steamdeck_button_l2', 'steamdeck_button_r2')),
}


def keyboard():
    """The keyboard and mouse set: the SDK's key names (rex/ui keybinds.cpp) and the old set's other names."""
    m = {}
    for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
        m[c] = f'keyboard_{c.lower()}'
    for i in range(10):
        m[str(i)] = m[f'Numpad{i}'] = f'keyboard_{i}'  # the pack has no numpad digits: the row's, as before
    for i in range(1, 13):
        m[f'F{i}'] = f'keyboard_f{i}'
    for i in (10, 11, 12):
        m[str(i)] = f'keyboard_f{i}'  # the old set's 10, 11 and 12 were F10 to F12
    named = {
        'Alt': 'alt', 'Ctrl': 'ctrl', 'Control': 'ctrl', 'Shift': 'shift', 'Shift_Alt': 'shift_icon',
        'Space': 'space', 'Tab': 'tab', 'Backspace': 'backspace', 'Backspace_Alt': 'backspace_icon',
        'Enter': 'enter', 'Return': 'enter', 'Enter_Alt': 'return', 'Enter_Tall': 'numpad_enter',
        'NumpadEnter': 'numpad_enter', 'Esc': 'escape', 'Escape': 'escape', 'CapsLock': 'capslock',
        'Caps_Lock': 'capslock', 'Del': 'delete', 'Delete': 'delete', 'Insert': 'insert', 'Home': 'home', 'End': 'end',
        'PageUp': 'page_up', 'Page_Up': 'page_up', 'PageDown': 'page_down', 'Page_Down': 'page_down',
        'PrintScreen': 'printscreen', 'Print_Screen': 'printscreen', 'NumLock': 'numlock', 'Num_Lock': 'numlock',
        'Arrow_Up': 'arrow_up', 'Up': 'arrow_up', 'Arrow_Down': 'arrow_down', 'Down': 'arrow_down',
        'Arrow_Left': 'arrow_left', 'Left': 'arrow_left', 'Arrow_Right': 'arrow_right', 'Right': 'arrow_right',
        'Asterisk': 'asterisk', 'NumpadStar': 'asterisk', 'Minus': 'minus', 'NumpadMinus': 'minus', 'Plus': 'plus',
        'NumpadPlus': 'numpad_plus', 'Plus_Tall': 'numpad_plus', 'Slash': 'slash_forward',
        'NumpadSlash': 'slash_forward', 'Backslash': 'slash_back', 'Backtick': 'tilde', 'Tilda': 'tilde',
        'Bracket_Left': 'bracket_open', 'LBracket': 'bracket_open', 'Bracket_Right': 'bracket_close',
        'RBracket': 'bracket_close', 'Mark_Left': 'bracket_less', 'Mark_Right': 'bracket_greater', 'Comma': 'comma',
        'Period': 'period', 'Semicolon': 'semicolon', 'Quote': 'quote', 'Question': 'question', 'Command': 'command',
        'Win': 'win',
    }
    for ours, theirs in named.items():
        m[ours] = f'keyboard_{theirs}'
    mouse = {'LMB': 'mouse_left', 'Mouse_Left': 'mouse_left', 'RMB': 'mouse_right', 'Mouse_Right': 'mouse_right',
             'MMB': 'mouse_scroll', 'Mouse_Middle': 'mouse_scroll', 'Mouse_Simple': 'mouse',
             'MB4': 'mouse_side_back', 'MB5': 'mouse_side_forward'}
    m.update(mouse)
    return m


def load(pack, folder, name):
    path = os.path.join(pack, folder, 'Double', name + '.png')
    if not os.path.isfile(path):
        sys.exit(f'not in the pack: {folder}/Double/{name}.png')
    im = Image.open(path).convert('RGBA')
    box = im.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox()
    return im.crop(box)


def rim(im):
    """A dark rim round the picture: Kenney's pictures are white, and our menus put some prompts on light paper
    (Home's cards, the pause menu's map) and the HUD over bright sky; on our dark tape it does not show."""
    big = Image.new('RGBA', (im.width + 2 * RIM, im.height + 2 * RIM), (0, 0, 0, 0))
    big.alpha_composite(im, (RIM, RIM))
    a = big.getchannel('A').point(lambda v: 255 if v > 24 else 0)
    a = a.filter(ImageFilter.MaxFilter(2 * RIM + 1)).filter(ImageFilter.GaussianBlur(1.2))
    out = Image.new('RGBA', big.size, RIM_COLOUR + (0,))
    out.putalpha(a)
    out.alpha_composite(big)
    return out


def fit(im):
    k = MAX_SIDE / max(im.width, im.height)
    return im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)


def wasd(pack):
    keys = {c: load(pack, 'Keyboard & Mouse', f'keyboard_{c}') for c in 'wasd'}
    w, h = keys['w'].size
    gap = max(2, w // 12)
    out = Image.new('RGBA', (3 * w + 2 * gap, 2 * h + gap), (0, 0, 0, 0))
    out.alpha_composite(keys['w'], (w + gap, 0))
    for i, c in enumerate('asd'):
        out.alpha_composite(keys[c], (i * (w + gap), h + gap))
    return out


def save(im, set_name, name):
    os.makedirs(os.path.join(GLYPHS, set_name), exist_ok=True)
    fit(rim(im)).save(os.path.join(GLYPHS, set_name, name + '.png'), optimize=True)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    pack = sys.argv[1]
    if not os.path.isfile(os.path.join(pack, 'License.txt')):
        sys.exit('no License.txt: not the unpacked Input Prompts folder')
    written = 0
    for set_name, (folder, mapping) in SETS.items():
        for ours, theirs in mapping.items():
            save(load(pack, folder, theirs), set_name, ours)
            written += 1
    for ours, theirs in keyboard().items():
        save(load(pack, 'Keyboard & Mouse', theirs), 'keyboard', ours)
        written += 1
    save(wasd(pack), 'keyboard', 'WASD')
    print(f'{written + 1} pictures written to bf/glyphs')


if __name__ == '__main__':
    main()
