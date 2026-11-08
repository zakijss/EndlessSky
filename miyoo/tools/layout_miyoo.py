#!/usr/bin/env python3
"""Write the Miyoo Mini Plus versions of the game's interfaces.

The Miyoo build lays the game out for an 800x600 screen (drawn at 0.8 on the
640x480 panel) instead of the desktop's 1024x768 minimum. This reads upstream's
data/_ui/interfaces.txt, applies a rule to each interface that needs one, and
writes the results to one file the Miyoo package loads after the original; in
the software build a later definition replaces the earlier one. Interfaces with
no rule are left out and keep their upstream layout.

    layout_miyoo.py <data/_ui/interfaces.txt> <out file> [images folder]
"""
import re
import struct
import sys

HALF_WIDTH = 400   # the 800x600 logical screen spans x = -400 .. 400
HALF_HEIGHT = 300
COORD = re.compile(r'^(\t+)(center|from|to) (-?[\d.]+) (-?[\d.]+)(.*)$')
DIMS = re.compile(r'^(\t+)dimensions (-?[\d.]+) (-?[\d.]+)(.*)$')
FROM_TO = re.compile(r'^(\t+)from (-?[\d.]+) (-?[\d.]+) to (-?[\d.]+) (-?[\d.]+)(.*)$')
IMAGES = None  # set in main: the images folder, for native sprite sizes

# Images an interface shows by a placeholder name, not a file: their native size.
PLACEHOLDER_SIZES = {"land": (720, 360)}


def image_size(name):
    """Native size of a sprite in logical units, from its image file's header."""
    if name in PLACEHOLDER_SIZES:
        return PLACEHOLDER_SIZES[name]
    for suffix in ("@1x.png", ".png", "@1x.jpg", ".jpg"):
        path = f"{IMAGES}/{name}{suffix}"
        try:
            with open(path, "rb") as f:
                head = f.read(26)
                if head[:8] == b"\x89PNG\r\n\x1a\n":
                    return struct.unpack(">II", head[16:24])
                f.seek(2)
                while True:
                    marker = f.read(2)
                    length = struct.unpack(">H", f.read(2))[0]
                    if marker[1] in (0xC0, 0xC1, 0xC2):
                        f.read(1)
                        h, w = struct.unpack(">HH", f.read(4))
                        return w, h
                    f.seek(length - 2, 1)
        except FileNotFoundError:
            continue
    raise FileNotFoundError(f"no image for sprite {name!r}")


def num(v):
    return str(int(v)) if v == int(v) else str(v)


def toward_center(shift, beyond):
    """Move elements at |x| >= beyond toward the centre by shift; sizes stay the same."""
    def rule(x, y):
        if abs(x) >= beyond:
            x -= shift if x > 0 else -shift
        return x, y
    return rule


class Scale:
    """Scale a whole layout by s around its centre (cx, cy) and centre it on the screen:
    positions, sizes, and the native size of any image that gives none. Fonts keep their size."""
    def __init__(self, s, cx=0., cy=0.):
        self.s, self.cx, self.cy = s, cx, cy

    def point(self, x, y):
        return (x - self.cx) * self.s, (y - self.cy) * self.s


class Move:
    """Move every position with fn(x, y) -> (x, y); sizes stay the same."""
    s = 1.

    def __init__(self, fn):
        self.point = fn


class Fit(Move):
    """Move with fn, give the named sprites these sizes, then run post(lines) if given."""
    def __init__(self, fn, sizes, post=None):
        super().__init__(fn)
        self.sizes = sizes
        self.post = post


def stack_info_panel(lines):
    """Interface sprites scale evenly, so 800 wide makes the 1020x570 panel 447 tall. Its
    art has borders only on the sides, so two copies overlapping cover the 502 needed."""
    old = ['\tsprite "ui/info panel"', '\t\tcenter 0 -4.4', '\t\tdimensions 800 502']
    at = next(i for i in range(len(lines)) if lines[i:i + 3] == old)
    half = 447 / 2
    new = []
    for cy in (-255.2 + half, 246.4 - half):
        new += ['\tsprite "ui/info panel"', f'\t\tcenter 0 {num(round(cy, 1))}', '\t\tdimensions 800 447']
    return lines[:at] + new + lines[at + 3:]


def info_panel(x, y):
    """The info panel is 1020x660 with its tabs and buttons. Its 250-wide side columns
    keep their width and move in by 100, the middle (the ship diagram) shrinks from 500
    to 300 (ShipInfoPanel and PlayerInfoPanel fit their tables to the boxes), heights
    shrink to 0.88, and the tab strip and the button row close in to meet the panel."""
    if y <= -300:
        return x + 80, y + 35
    if y >= 290:
        return x - 100, y - 34
    nx = x + 100 if x <= -250 else x - 100 if x >= 250 else x * .6
    return nx, round(y * .88, 1)


class Edit:
    """Replace whole lines of a block, matched without their indentation: {old: new}."""
    def __init__(self, lines):
        self.lines = lines


def planet_move(x, y):
    """The small-screen planet dialog is 840x680 around x = -60. Centre it, raise it 75 (the
    landscape's top 95 rows go off screen) so the trade, bank and hiring tables and their button
    rows fit inside its text panel, and bring the two button columns 20 in so they stay on screen."""
    nx, ny = x + 60, y - 75
    if x <= -400:
        nx += 20
    elif x >= 280:
        nx -= 20
    return nx, ny


def follow_planet(x, y):
    """Panels shown inside the planet dialog move with it."""
    return x + 60, y - 75



# One rule per interface. The main menu's side panels sit at x = +-360 for a 1024-wide
# screen; 90 closer keeps them clear of the compass art (about +-120) and the edges.
def port_title(lines):
    """The title reads "ENDLESS SKY" (300x40 at y = -200): mark it as this port, small, at the
    foot of the Y, with the porter's name under that, clear of the side panel (top y = -171)."""
    return lines + [
        '\tlabel "Mini Plus"', '\t\tfrom 153 -205', '\t\talign top left', '\t\tcolor "bright"',
        '\tlabel "by ZJSS"', '\t\tfrom 153 -189', '\t\talign top left', '\t\tcolor "medium"',
    ]


def port_info_button(lines):
    """A Port info button under Preferences, as Gamerules sits under Enter Ship (MenuPanel 'i')."""
    return lines + [
        '\tvisible', '\tactive',
        '\tsprite "ui/wide button"', '\t\tcenter -345 195',
        '\tbutton i "Port _info"', '\t\tcenter -345 195', '\t\tdimensions 90 30',
    ]


def planet_backing(lines):
    """The flight HUD shows through the gaps between the landing screen's button columns
    (its "no target" label, for one): back both columns with the panel colour."""
    backing = []
    for left, right in ((-400, -240), (240, 400)):
        backing += ['\tfill', f'\t\tfrom {left} -15 to {right} 265', '\t\tcolor "panel background"']
    return lines[:1] + backing + lines[1:]


RULES = {
    "menu background": Fit(lambda x, y: (x, y), {}, post=port_title),
    "main menu": Fit(toward_center(90, 200), {}, post=port_info_button),
    "menu player info": Move(toward_center(90, 200)),
    "menu start info": Move(toward_center(90, 200)),
    # The same three side panels as the main menu, with their buttons and list boxes.
    "load menu": Move(toward_center(90, 200)),
    "start conditions menu": Move(toward_center(90, 200)),
    # Text keeps its size, so the landing screens move to fit instead of shrinking.
    "planet (small screen)": Fit(planet_move, {}, post=planet_backing),
    "spaceport (small screen)": Move(follow_planet),
    "news (small screen)": Move(follow_planet),
    "trade (small screen)": Move(follow_planet),
    "bank (small screen)": Move(follow_planet),
    "hiring (small screen)": Move(follow_planet),
    # The job board's lists stop above the mission box on narrow screens (MissionPanel.cpp),
    # so it can rise above the map buttons; the key keeps the bottom-left corner.
    "mission": Move(lambda x, y: (x, y - 90)),
    # The log's controls reach x = 810 from the left edge; a 15 narrower log brings them on screen.
    "message log": Edit({
        'value "width" 570': 'value "width" 555',
        'from 570 0 top left': 'from 555 0 top left',
        'center 710 -30 bottom left': 'center 695 -30 bottom left',
        'center 640 -30 bottom left': 'center 625 -30 bottom left',
        'center 706 -20 bottom left': 'center 691 -20 bottom left',
        'center 714 -16 bottom left': 'center 699 -16 bottom left',
        'center 654 -80 bottom left': 'center 639 -80 bottom left',
    }),
    # The status bar (time, Wi-Fi, battery) takes the top 18 units of the top-right corner:
    # the system and date lines move down a little, closer to the credits line.
    "hud": Edit({
        'from -160 25': 'from -160 29',
        'from -20 45': 'from -20 47',
    }),
    "info panel": Fit(info_panel, {"ui/info panel": (800, 502)}, post=stack_info_panel),
    # The controls tables are placed by code around the centre; only the note on the left
    # moves, into the part of its box the settings panel doesn't cover.
    "controls": Edit({
        'center -350 102': 'center -305 102',
        'from -400 48 to -270 49': 'from -390 48 to -280 49',
        'from -420 70 to -270 130': 'from -390 70 to -280 190',
        # No keyboard to unbind with: say how the button layers work instead.
        '"wrapped label" "Press \'_x\' over controls to unbind them. Controls can share the same keybind."':
            '"wrapped label" "Tap Select: pointer or flying. Hold Select or Start with a button for more commands."',
    }),
}


def edit_block(name, body, rule):
    out, used = [], set()
    for line in body:
        key = line.strip()
        if key in rule.lines:
            indent = line[:len(line) - len(line.lstrip('\t'))]
            line = indent + rule.lines[key]
            used.add(key)
        out.append(line)
    assert used == set(rule.lines), (name, set(rule.lines) - used)
    return out


def scale_block(body, rule, anchored=False):
    """Move (and for Scale, resize) every element; scaled images without dimensions get
    their native size, scaled."""
    out = []
    image, has_dims, indent = None, False, '\t\t'
    element_start, positioned = None, True

    def close():
        # An element placed only by its alignment sits at the origin; give it the moved origin.
        if element_start is not None and not positioned:
            x, y = rule.point(0., 0.)
            if (x, y) != (0., 0.):
                out.insert(element_start + 1, f'\t\tcenter {num(round(x, 1))} {num(round(y, 1))}')
        if image and not has_dims and image in getattr(rule, 'sizes', {}):
            w, h = rule.sizes[image]
            out.append(f'{indent}dimensions {w} {h}')
        elif image and not has_dims and rule.s != 1:
            w, h = image_size(image)
            out.append(f'{indent}dimensions {num(round(w * rule.s, 1))} {num(round(h * rule.s, 1))}')

    for line in body:
        depth = len(line) - len(line.lstrip('\t'))
        if depth == 1:
            close()
            m = re.match(r'^\t(sprite|image) "([^"]+)"', line)
            image, has_dims = (m.group(2), False) if m else (None, False)
            element_start, positioned = (len(out), False) if m else (None, True)
        elif FROM_TO.match(line) or COORD.match(line):
            positioned = True
        if (m := FROM_TO.match(line)):
            x1, y1 = rule.point(float(m.group(2)), float(m.group(3)))
            x2, y2 = rule.point(float(m.group(4)), float(m.group(5)))
            line = f'{m.group(1)}from {num(round(x1, 1))} {num(round(y1, 1))} to {num(round(x2, 1))} {num(round(y2, 1))}{m.group(6)}'
            if not anchored:
                check(x1, y1, line); check(x2, y2, line)
        elif (m := COORD.match(line)):
            x, y = rule.point(float(m.group(3)), float(m.group(4)))
            line = f'{m.group(1)}{m.group(2)} {num(round(x, 1))} {num(round(y, 1))}{m.group(5)}'
            if not anchored:
                check(x, y, line)
        elif (m := DIMS.match(line)):
            has_dims = True
            line = line if rule.s == 1 else f'{m.group(1)}dimensions {num(round(float(m.group(2)) * rule.s, 1))} {num(round(float(m.group(3)) * rule.s, 1))}{m.group(4)}'
        out.append(line)
    close()
    return out


def check(x, y, line):
    # Element anchors must land on the 800x600 screen.
    assert abs(x) <= HALF_WIDTH and abs(y) <= HALF_HEIGHT, (line, x, y)


def blocks(lines):
    """Yield (name, lines) for each top-level interface block."""
    name, body = None, []
    for line in lines:
        if line and not line[0].isspace():
            if name:
                yield name, body
            m = re.match(r'^interface "([^"]+)"', line)
            name, body = (m.group(1), [line]) if m else (None, [])
        elif name:
            body.append(line)
    if name:
        yield name, body


def main():
    global IMAGES
    src, out = sys.argv[1], sys.argv[2]
    IMAGES = sys.argv[3] if len(sys.argv) > 3 else src.rsplit('/data/', 1)[0] + '/images'
    with open(src) as f:
        lines = f.read().split('\n')
    result = ['# Miyoo Mini Plus layouts, generated by miyoo/tools/layout_miyoo.py from interfaces.txt.', '']
    done = set()
    for name, body in blocks(lines):
        rule = RULES.get(name)
        if not rule:
            continue
        # Positions in an interface anchored to a screen edge or corner are relative to it.
        anchored = len(body[0].split('"')[-1].split()) > 0 or any(l.strip().startswith('anchor ') for l in body)
        block = edit_block(name, body, rule) if isinstance(rule, Edit) else scale_block(body, rule, anchored)
        # Loaded after interfaces.txt: replace the upstream definition instead of adding to it.
        result.append('overwrite')
        result.extend(rule.post(block) if getattr(rule, 'post', None) else block)
        done.add(name)
    missing = set(RULES) - done
    assert not missing, f'interfaces not found upstream: {missing}'
    with open(out, 'w') as f:
        f.write('\n'.join(result).rstrip('\n') + '\n')
    print(f'{len(done)} interfaces written to {out}')


if __name__ == '__main__':
    main()
