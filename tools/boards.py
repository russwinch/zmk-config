#!/usr/bin/env python3
"""The keyboards' shapes, for pictures of them.

Each board's left half as its maker's CAD files have it, in mm (y down, x
from the inner edge, so to the left is negative): where each key's switch
is, the outline of its PCB, and its encoders, screen and controller. The
right half is its mirror.

    tools/boards.py           # docs/boards/<board>.svg, for the README
    tools/boards.py --header  # also src/screen_fx/boards.h, for the dongle

Measured from:
  Urchin     duckyb/urchin main.kicad_pcb (SW1-SW17, U1, Edge.Cuts)
  Forager    carrefinho/forager forager-pcb.kicad_pcb (LCH1-LCH17, LU1)
  Aurora     splitkb/aurora "Aurora Corne" top plate (switches), bottom
  Corne      plate (outline) and OLED cover DXFs
  Waterfowl  JW2586/waterfowl-keyboard-info switch plate, PCB edge and
             OLED cover DXFs (the right half's, mirrored)
"""

import argparse
import math
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# rows: the three rows of keys, outer column first; thumbs: (x, y, degrees
# clockwise), outer first; the rest as named
BOARDS = {
    "waterfowl": dict(
        rows=[
            [(-123, 33.2), (-104, 19.1), (-84.9, 14.3), (-65.9, 21.5), (-46.8, 23.9)],
            [(-123, 52.3), (-104, 38.1), (-84.9, 33.4), (-65.9, 40.5), (-46.8, 42.9)],
            [(-123, 71.3), (-104, 57.2), (-84.9, 52.4), (-65.9, 59.6), (-46.8, 62)],
        ],
        thumbs=[(-58.4, 81.6, 15), (-38.9, 89.7, 30), (-22.2, 102.5, 45)],
        roller=(-84.9, 76.5),
        dial=(-17.3, 71.4),
        outline=[(0, 14.2), (-0.9, 11), (-3.3, 8.8), (-22.4, 4), (-40.8, 1.2), (-59.4, 0), (-78, 0.5), (-96.5, 2.6), (-114.7, 6.4), (-133.5, 12.1), (-135.7, 14.3), (-136.5, 17.4), (-136.5, 86.1), (-135.7, 89.1), (-133.5, 91.3), (-131.1, 92.1), (-80.1, 92.1), (-73.4, 92.5), (-66.8, 93.5), (-60.3, 95.1), (-54, 97.3), (-47.9, 100.2), (-42.2, 103.7), (-36.8, 107.6), (-25.2, 118.5), (-22.6, 119.2), (-19.4, 118.5), (-1.8, 101.2), (-0.8, 99.9), (-0.1, 98)],
        oled=[(0, 84.2), (-0.5, 85.1), (-1.5, 85.1), (-5.7, 80.6), (-5.7, 79.4), (-3.6, 74.3), (-3.4, 70.5), (-4.6, 65.7), (-7.4, 61.5), (-11.6, 58.7), (-16.4, 57.5), (-20.2, 57.7), (-25.3, 59.8), (-26.5, 59.8), (-34.7, 51.7), (-34.8, 4.2), (-34.4, 3), (-33.6, 2.4), (-32.5, 2.2), (-18.3, 4.8), (-4.3, 8.4), (-1.9, 9.8), (-0.4, 12), (0, 14.2)],
    ),
    "corne": dict(
        rows=[
            [(-124.7, 16.1), (-105.7, 16.1), (-86.7, 11.4), (-67.7, 9), (-48.7, 11.4), (-29.7, 13.7)],
            [(-124.7, 35.1), (-105.7, 35.1), (-86.7, 30.4), (-67.7, 28), (-48.7, 30.4), (-29.7, 32.7)],
            [(-124.7, 54.1), (-105.7, 54.1), (-86.7, 49.4), (-67.7, 47), (-48.7, 49.4), (-29.7, 51.7)],
        ],
        thumbs=[(-58.2, 69, 0), (-37.2, 71.7, 15), (-15, 75.5, 30)],
        outline=[(-21.2, 4.7), (-20.8, 5.1), (-20.7, 6.6), (-20.3, 7), (-0.5, 7), (0, 7.5), (0, 67.5), (-14.4, 92.1), (-15.2, 92), (-30, 83.5), (-67.2, 78), (-78.2, 63.3), (-133.2, 63.2), (-133.7, 62.9), (-133.8, 7.5), (-133.4, 7), (-96.2, 7), (-95.8, 6.6), (-95.7, 2.7), (-95.4, 2.3), (-77.2, 2.2), (-76.8, 1.8), (-76.7, 0.3), (-76.3, 0), (-59.2, 0), (-58.8, 0.4), (-58.7, 1.9), (-58.2, 2.2), (-40.2, 2.3), (-39.8, 2.7), (-39.7, 4.4), (-39.2, 4.7)],
        oled=[(-19.5, 7.5), (-19, 7), (-0.5, 7), (0, 7.3), (0, 65.9), (-0.4, 66.4), (-19.4, 55.4)],
    ),
    "urchin": dict(
        rows=[
            [(-100.2, 27.1), (-82.2, 15.1), (-64.2, 9.1), (-46.2, 15.6), (-28.2, 18.1)],
            [(-100.2, 44.1), (-82.2, 32.1), (-64.2, 26.1), (-46.2, 32.6), (-28.2, 35.1)],
            [(-100.2, 61.1), (-82.2, 49.1), (-64.2, 43.1), (-46.2, 49.6), (-28.2, 52.1)],
        ],
        thumbs=[(-36.3, 69.8, 15), (-17.7, 77.5, 30)],
        nano=(-9.4, 41.7),
        outline=[(-109.9, 54.7), (-109.5, 54.7), (-109.5, 69.5), (-103.8, 72.2), (-96, 74.5), (-88, 75.8), (-53.2, 76.2), (-42.9, 77.4), (-37.1, 78.8), (-30.1, 81.3), (-14.1, 89.6), (-1.9, 68.9), (-0.3, 64.4), (0, 61.4), (-0.1, 21.2), (-1, 20.2), (-2.3, 19.9), (-3.7, 20.9), (-4, 23.3), (-4.4, 23.6), (-14.5, 23.6), (-14.8, 21.4), (-15.4, 20.5), (-16.4, 19.9), (-18.3, 19.8), (-18.7, 19.4), (-19, 9.4), (-28, 5.8), (-38.9, 2.7), (-52, 0.5), (-63.4, 0), (-73.9, 0.8), (-82.1, 2.7), (-90.1, 5.6), (-97.6, 9.5), (-104.5, 14.3), (-109.4, 18.7), (-109.5, 32), (-109.9, 32)],
    ),
    "forager": dict(
        rows=[
            [(-103.4, 21.7), (-85.4, 15.7), (-67.4, 10.7), (-49.4, 13.7), (-31.4, 15.7)],
            [(-103.4, 38.7), (-85.4, 32.7), (-67.4, 27.7), (-49.4, 30.7), (-31.4, 32.7)],
            [(-103.4, 55.7), (-85.4, 49.7), (-67.4, 44.7), (-49.4, 47.7), (-31.4, 49.7)],
        ],
        thumbs=[(-28.6, 71.3, 25), (-14, 82.6, 25)],
        xiao=(-103.9, 9.7),
        outline=[(-14.3, 6.8), (-14.4, 1.5), (-15.2, 0.5), (-16.4, 0), (-112.4, 0), (-113.5, 0.3), (-114.3, 1.1), (-114.6, 2.2), (-114.6, 64.2), (-114.3, 65.4), (-113.5, 66.2), (-112.4, 66.5), (-54.8, 66.6), (-44.4, 71.6), (-43.3, 73), (-42.8, 75.8), (-41.9, 77), (-29.7, 82.8), (-28.7, 84.2), (-28.2, 87.1), (-27.3, 88.2), (-10.2, 96.2), (-8.3, 96), (-7.4, 95), (-0.2, 79.6), (0, 78.5), (-0.4, 77.4), (-1.2, 76.7), (-12.6, 71.3), (-13.7, 70.2), (-14.2, 68.9)],
    ),
}

# keycaps: Choc on the Urchin and Forager (18 x 17 mm apart), MX on the
# others (19.05 mm)
CAPS = {"urchin": (17.5, 16.5), "forager": (17.5, 16.5), "corne": (18, 18), "waterfowl": (18, 18)}
NAMES = {"urchin": "Urchin", "forager": "Forager", "corne": "Aurora Corne", "waterfowl": "Waterfowl"}


class Part:
    """One key position, or something else on a half, as the left half has it."""

    def __init__(self, kind, x, y, rot=0.0, w=0.0, h=0.0, core=True):
        self.kind, self.x, self.y, self.rot, self.w, self.h, self.core = kind, x, y, rot, w, h, core


def positions(name):
    """The board's key positions in ZMK's order: (part, right half?)."""
    b = BOARDS[name]
    w, h = CAPS[name]
    rows = [[Part("key", x, y, 0, w, h) for x, y in row] for row in b["rows"]]
    thumbs = [Part("key", x, y, r, w, h) for x, y, r in b["thumbs"]]
    if name == "corne":
        # the outer column is extra; the encoder is in the outer thumb's place
        for row in rows:
            row[0].core = False
        thumbs[0] = Part("dial", thumbs[0].x, thumbs[0].y, 0, 14, 14, core=False)
        thumbs[2].h = 27  # the inner thumb is 1.5u
    if name == "waterfowl":
        # the roller under the middle finger, the outer thumb repeats the one
        # beside it, the dial under the screen
        thumbs[0].core = False
        thumbs = [Part("roller", *b["roller"], 0, 8, 17, core=False), *thumbs,
                  Part("dial", *b["dial"], 0, 24, 24, core=False)]
    out = []
    for row in rows + [thumbs]:
        out += [(p, False) for p in row] + [(p, True) for p in reversed(row)]
    return out


def extras(name):
    """What else is on top: (part, on the right half too?)."""
    b = BOARDS[name]
    out = []
    if "oled" in b:  # the cover over the controller, and the left half's nice!view on it
        out.append((Part("cover", 0, 0), True))
    if name == "waterfowl":
        out.append((Part("screen", -17.4, 30, 0, 13, 27), False))
    if name == "corne":
        out.append((Part("screen", -9.6, 25, 0, 13, 27), False))
    if name == "urchin":  # the nice!nano, on top beside the inner column
        out.append((Part("controller", *b["nano"], 0, 18, 33), True))
    return out


# --- the README's pictures -----------------------------------------------------

GAP = 24  # mm between the halves
SCALE = 0.75  # pixels a mm, the same for every board so they compare
COLORS = dict(board="#3d434c", cover="#30353c", key="#c4cad3", extra="#e3a33c", knob="#e3a33c", knob_mark="#3d434c",
              screen="#9fb4a6", screen_edge="#1d2126", controller="#1c2a4a")


def svg(name):
    b = BOARDS[name]
    xs = [x for x, y in b["outline"]]
    ys = [y for x, y in b["outline"]]
    half_w, top, bottom = -min(xs), min(ys), max(ys)
    pad = 2
    width, height = 2 * half_w + GAP + 2 * pad, bottom - top + 2 * pad
    mid = width / 2

    def at(x, y, right):
        return (mid + GAP / 2 - x if right else mid - GAP / 2 + x), y - top + pad

    def poly(points, right, fill):
        pts = " ".join("%.1f,%.1f" % at(x, y, right) for x, y in points)
        return f'<polygon points="{pts}" fill="{fill}" stroke="{fill}" stroke-width="1" stroke-linejoin="round"/>'

    def rect(p, right, fill, rx, extra=""):
        cx, cy = at(p.x, p.y, right)
        rot = -p.rot if right else p.rot
        return (f'<rect x="{cx - p.w / 2:.2f}" y="{cy - p.h / 2:.2f}" width="{p.w}" height="{p.h}" rx="{rx}" '
                f'fill="{fill}" transform="rotate({rot:g} {cx:.2f} {cy:.2f})"{extra}/>')

    out = []
    for right in (False, True):
        out.append(poly(b["outline"], right, COLORS["board"]))
        if "oled" in b:
            out.append(poly(b["oled"], right, COLORS["cover"]))
    for p, both in extras(name):
        for right in (False, True) if both else (False,):
            if p.kind == "screen":
                out.append(rect(p, right, COLORS["screen_edge"], 1.5))
                inner = Part("screen", p.x, p.y + 1.5, 0, p.w - 3, p.h - 7)
                out.append(rect(inner, right, COLORS["screen"], 0.5))
            elif p.kind == "controller":
                out.append(rect(p, right, COLORS["controller"], 1))
    for p, right in positions(name):
        color = COLORS["key"] if p.core else COLORS["extra"]
        if p.kind == "dial":
            cx, cy = at(p.x, p.y, right)
            r = p.w / 2
            out.append(f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r}" fill="{COLORS["knob"]}"/>')
            out.append(f'<line x1="{cx:.2f}" y1="{cy - r * 0.85:.2f}" x2="{cx:.2f}" y2="{cy - r * 0.35:.2f}" '
                       f'stroke="{COLORS["knob_mark"]}" stroke-width="1.6" stroke-linecap="round"/>')
        elif p.kind == "roller":
            out.append(rect(p, right, color, 3))
            cx, cy = at(p.x, p.y, right)
            for i in range(-2, 3):
                y = cy + i * 3
                out.append(f'<line x1="{cx - p.w / 2 + 1.2:.2f}" y1="{y:.2f}" x2="{cx + p.w / 2 - 1.2:.2f}" y2="{y:.2f}" '
                           f'stroke="{COLORS["knob_mark"]}" stroke-width="0.8"/>')
        else:
            out.append(rect(p, right, color, 2))
    body = "\n  ".join(out)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width:.1f} {height:.1f}" '
            f'width="{width * SCALE:.0f}" height="{height * SCALE:.0f}">\n  <title>{NAMES[name]}</title>\n  {body}\n</svg>\n')


# --- the dongle's pictures -----------------------------------------------------

KINDS = {"key": "FX_PART_KEY", "dial": "FX_PART_DIAL", "roller": "FX_PART_ROLLER", "screen": "FX_PART_SCREEN",
         "controller": "FX_PART_CONTROLLER"}


def header():
    """src/screen_fx/boards.h: each board in quarter millimetres."""
    q = lambda v: int(round(v * 4))
    lines = ["/* The keyboards' shapes, for the Keys screen. Written by tools/boards.py from",
             " * its measurements of each board: run it again rather than editing this. */",
             "", "#pragma once", "", '#include "fx.h"', ""]
    names = []
    for name in BOARDS:
        b = BOARDS[name]
        names.append(name)
        parts = []
        for p, right in positions(name):
            flags = KINDS[p.kind] + (" | FX_PART_RIGHT" if right else "") + ("" if p.core else " | FX_PART_EXTRA")
            parts.append(f"    {{{q(p.x)}, {q(p.y)}, {int(round(p.rot))}, {q(p.w)}, {q(p.h)}, {flags}}},")
        lines += [f"static const struct fx_part {name}_parts[] = {{", *parts, "};"]
        parts = []
        for p, both in extras(name):
            if p.kind in KINDS:
                flags = KINDS[p.kind] + (" | FX_PART_BOTH" if both else "")
                parts.append(f"    {{{q(p.x)}, {q(p.y)}, {int(round(p.rot))}, {q(p.w)}, {q(p.h)}, {flags}}},")
        if parts:
            lines += [f"static const struct fx_part {name}_extras[] = {{", *parts, "};"]
        for poly in ("outline", "oled"):
            if poly in b:
                pts = ", ".join(f"{q(x)}, {q(y)}" for x, y in b[poly])
                lines += [f"static const int16_t {name}_{poly}[] = {{{pts}}};"]
        lines.append("")
    lines.append("static const struct fx_board fx_boards[] = {")
    for name in names:
        b = BOARDS[name]
        ex = f"{name}_extras, sizeof({name}_extras) / sizeof({name}_extras[0])" if extras(name) and any(
            p.kind in KINDS for p, _ in extras(name)) else "NULL, 0"
        cover = f"{name}_oled, sizeof({name}_oled) / sizeof({name}_oled[0]) / 2" if "oled" in b else "NULL, 0"
        lines.append(f"    [FX_BOARD_{name.upper()}] = {{{name}_parts, sizeof({name}_parts) / sizeof({name}_parts[0]), {ex},")
        lines.append(f"        {name}_outline, sizeof({name}_outline) / sizeof({name}_outline[0]) / 2, {cover}}},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--header", action="store_true", help="also write src/screen_fx/boards.h")
    args = ap.parse_args()
    out = REPO / "docs" / "boards"
    out.mkdir(parents=True, exist_ok=True)
    for name in BOARDS:
        (out / f"{name}.svg").write_text(svg(name))
        print(f"wrote docs/boards/{name}.svg")
    if args.header:
        (REPO / "src" / "screen_fx" / "boards.h").write_text(header())
        print("wrote src/screen_fx/boards.h")


if __name__ == "__main__":
    main()
