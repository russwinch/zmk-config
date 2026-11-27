#!/usr/bin/env python3
"""Render the Prospector dongle's screen animations to GIFs on this computer.

The animations (src/screen_fx/fx.c) are plain C, so this compiles them with
the made-up typing in tools/fx_preview/main.c and saves what the dongle would
draw, without flashing anything:

    tools/fx_preview.py              # every animation -> fx_preview_out/*.gif
    tools/fx_preview.py --fps 30     # what a faster screen link would give
    tools/fx_preview.py --saver thermal  # the screensaver coming over Thermal, and going
    tools/fx_preview.py --power typist # the panel going off and coming on, on Typist
    tools/fx_preview.py --themes     # every colour theme -> fx_preview_out/themes.png
    tools/fx_preview.py --stills     # a still of each screen -> fx_preview_out/<n>_<name>.png,
                                     # those that draw the board once per board (<n>_<name>_<board>.png)
    tools/fx_preview.py --contrast   # every theme's contrast ratios, the low ones flagged
    tools/fx_preview.py --board waterfowl  # on the Waterfowl's keys and encoders (or corne)

The battery levels and the output are drawn over them as on the dongle
(src/screen_fx/status_overlay.c), in fonts from the Prospector module. Those
are fetched once, at the commit config/west.yml pins, into fx_preview_out/;
or point --prospector at a checkout of the module.

The panel's rounded corners (fx.h's FX_CORNER_R) are blacked out, as the
glass hides them; --square shows the whole picture.

Needs gcc, git and Pillow (pip install pillow). Colours are the panel's
16-bit ones; speed and tearing on the real screen will differ.
"""

import argparse
import os
import re
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parent.parent
FX_DIR = REPO / "src" / "screen_fx"
TOOL_DIR = REPO / "tools" / "fx_preview"
WIDTH, HEIGHT = 280, 240
PROSPECTOR_URL = "https://github.com/carrefinho/prospector-zmk-module"
ENV = dict(os.environ)  # for main.c: FX_PREVIEW_BOARD
BOARDS = ["urchin", "forager", "corne", "waterfowl"]  # --board's choices, in the README's order
BOARD_SCREENS = {"DEBUG", "STATUS", "TYPIST", "SHOWROOM"}  # the screens that draw the board itself (--stills)
CORNERS = [True]  # black out the corners the panel's glass hides (--square: don't)


def corner_mask() -> Image.Image:
    """White where the panel shows, with its corners rounded as fx.h has them."""
    radius = 2 * float(re.search(r"#define FX_CORNER_R ([\d.]+)f", (FX_DIR / "fx.h").read_text()).group(1))
    mask = Image.new("L", (WIDTH, HEIGHT), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, WIDTH - 1, HEIGHT - 1], radius, fill=255)
    return mask
FONT_DIR = "boards/shields/prospector_adapter/src/fonts"


def prospector(cache: Path) -> Path:
    """A checkout of the Prospector module at the commit config/west.yml pins."""
    west = (REPO / "config" / "west.yml").read_text()
    revision = re.search(r"name: prospector-zmk-module.*?revision: (\w+)", west, re.S).group(1)
    checkout = cache / f"prospector-{revision[:12]}"
    if not (checkout / FONT_DIR).is_dir():
        print(f"fetching the Prospector module's fonts ({revision[:12]})")
        checkout.mkdir(parents=True, exist_ok=True)
        for command in (["init", "-q"], ["fetch", "-q", "--depth", "1", PROSPECTOR_URL, revision],
                        ["checkout", "-q", "FETCH_HEAD"]):
            subprocess.run(["git", "-C", str(checkout), *command], check=True)
    return checkout


def build(workdir: Path, module: Path) -> Path:
    exe = workdir / "fx_preview"
    # the fonts status_overlay.c declares
    overlay = (FX_DIR / "status_overlay.c").read_text()
    fonts = [module / FONT_DIR / f"{name}.c" for name in re.findall(r"LV_FONT_DECLARE\((\w+)\)", overlay)]
    subprocess.run(
        ["gcc", "-O2", "-Wall", "-Wextra", "-I", str(FX_DIR), "-I", str(TOOL_DIR), str(FX_DIR / "fx.c"),
         str(FX_DIR / "status_overlay.c"), str(TOOL_DIR / "lvgl_shim.c"), str(TOOL_DIR / "main.c"),
         *map(str, fonts), "-lm", "-o", str(exe)],
        check=True,
    )
    return exe


def names() -> list[str]:
    # the names in fx.c's `effects` table, in order
    text = (FX_DIR / "fx.c").read_text()
    table = text[text.index("} effects[] = {"):text.index("#define FX_COUNT")]
    return re.findall(r'\{"(\w+)"', table)


def savers() -> list[str]:
    # the ones the table gives a way in to (.saver = ...)
    text = (FX_DIR / "fx.c").read_text()
    table = text[text.index("} effects[] = {"):text.index("#define FX_COUNT")]
    entries = re.findall(r'\{"(\w+)"(.*?)(?=\n    \{"|\n\};)', table, re.S)
    return [name for name, entry in entries if ".saver" in entry]


def themes() -> list[str]:
    # the names in colors.h's `schemes` table, in order
    text = (FX_DIR / "colors.h").read_text()
    table = text[text.index("} schemes[] = {"):text.index("#define SCHEMES")]
    return re.findall(r'\{"([\w ]+)"', table)


# --contrast: each theme colour's WCAG 2 contrast ratio against what it is
# drawn on (1 the same, 21 black on white), and the least each should have.
# 4.5 is the usual bar for text, 7 the strict one; dim only has to show.
SLOTS = "bg dim label text bright glow good warn bad mod layer".split()
LEAST = {"dim": 1.4, "label": 4.5, "text": 7, "bright": 7, "glow": 4.5,
         "good": 4.5, "warn": 4.5, "bad": 4.5, "mod": 4.5, "layer": 4.5}


def scheme_colors() -> dict[str, dict[str, tuple[int, int, int]]]:
    text = (FX_DIR / "colors.h").read_text()
    table = text[text.index("} schemes[] = {"):text.index("#define SCHEMES")]
    return {name: dict(zip(SLOTS, [tuple(map(int, c)) for c in re.findall(r"\{(\d+), (\d+), (\d+)\}", body)]))
            for name, body in re.findall(r'\{"([\w ]+)",(.*?\}\})', table, re.S)}


def contrast(a: tuple[int, int, int], b: tuple[int, int, int]) -> float:
    def luminance(c):
        lin = [v / 255 / 12.92 if v <= 10 else ((v / 255 + 0.055) / 1.055) ** 2.4 for v in c]
        return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]
    la, lb = sorted((luminance(a), luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def contrast_table() -> bool:
    """Print every theme's contrast ratios; False if any is under its LEAST."""
    ok = True
    print(f"{'':15} " + " ".join(f"{s:>6}" for s in SLOTS[1:]) + "  held key on dim")
    for name, c in scheme_colors().items():
        cells = []
        for slot in SLOTS[1:]:
            ratio = contrast(c[slot], c["bg"])
            low = ratio < LEAST[slot]
            ok &= not low
            cells.append(f"{ratio:5.1f}{'!' if low else ' '}")
        held = contrast(c["bright"], c["dim"])
        ok &= held >= 4.5
        print(f"{name:15} " + " ".join(cells) + f"  {held:5.1f}{'!' if held < 4.5 else ''}")
    print(f"against the background (! under {', '.join(f'{k} {v}' for k, v in LEAST.items())};"
          " a held key on an unpressed one 4.5)")
    return ok


def themed() -> list[str]:
    # the animations drawn in the theme's colours: the table gives them a colours function
    text = (FX_DIR / "fx.c").read_text()
    table = text[text.index("} effects[] = {"):text.index("#define FX_COUNT")]
    return [name for name, colors in re.findall(r'\{"(\w+)", \w+, \w+, (\w+)', table) if colors != "NULL"]


def save(images: list[Image.Image], path: Path, fps: float) -> None:
    images[0].save(path, save_all=True, append_images=images[1:], duration=round(1000 / fps), loop=0)
    print(f"wrote {path.relative_to(REPO) if path.is_relative_to(REPO) else path}")


def render(exe: Path, *arguments: object) -> list[Image.Image]:
    raw = subprocess.run([str(exe), *map(str, arguments)], check=True, capture_output=True, env=ENV).stdout
    size = WIDTH * HEIGHT * 3
    images = [Image.frombytes("RGB", (WIDTH, HEIGHT), raw[i:i + size]) for i in range(0, len(raw), size)]
    if CORNERS[0]:
        mask, black = corner_mask(), Image.new("RGB", (WIDTH, HEIGHT))
        images = [Image.composite(image, black, mask) for image in images]
    return images


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--fps", type=float, default=15, help="frames per second (default 15)")
    parser.add_argument("--seconds", type=float, default=12, help="length of each clip")
    parser.add_argument("--out", type=Path, default=REPO / "fx_preview_out")
    parser.add_argument("--saver", metavar="ANIMATION",
                        help="instead: each screensaver coming over this animation and going again")
    parser.add_argument("--power", metavar="ANIMATION",
                        help="instead: the panel going off and coming back on, with this animation showing")
    parser.add_argument("--contrast", action="store_true",
                        help="instead: print each theme's contrast ratios and flag the low ones (no build)")
    parser.add_argument("--themes", action="store_true",
                        help="instead: one picture of every colour theme, on each screen drawn in them")
    parser.add_argument("--prospector", type=Path,
                        help="a checkout of prospector-zmk-module (default: fetch the pinned commit)")
    parser.add_argument("--board", choices=BOARDS, default="forager",
                        help="the keyboard the screens draw (default forager; --stills draws each)")
    parser.add_argument("--stills", action="store_true",
                        help="instead: a PNG of each screen mid-typing (Status over USB), 4x the panel's size "
                             "(to draw over)")
    parser.add_argument("--square", action="store_true",
                        help="show the corners too, which the panel's rounded glass hides")
    args = parser.parse_args()
    if args.contrast:
        raise SystemExit(0 if contrast_table() else 1)
    ENV["FX_PREVIEW_BOARD"] = args.board
    CORNERS[0] = not args.square

    args.out.mkdir(parents=True, exist_ok=True)
    module = args.prospector or prospector(REPO / "fx_preview_out")
    with tempfile.TemporaryDirectory() as tmp:
        exe = build(Path(tmp), module)
        if args.saver:
            showing = [name.lower() for name in names()].index(args.saver.lower())
            for saver in savers():
                if saver.lower() != args.saver.lower():
                    images = render(exe, "saver", showing, names().index(saver), args.fps)
                    save(images, args.out / f"saver_{args.saver.lower()}_{saver.lower()}.gif", args.fps)
            return
        if args.power:
            showing = [name.lower() for name in names()].index(args.power.lower())
            save(render(exe, "power", showing, args.fps), args.out / f"power_{args.power.lower()}.gif", args.fps)
            return
        frames = int(args.seconds * args.fps)
        if args.stills:
            # mid-typing over Bluetooth; Status over USB, where its middle has no computer's link.
            # The screens that draw the board itself once per board, as <n>_<name>_<board>.png
            for index, name in enumerate(names()):
                at = 0.85 if name == "STATUS" else 0.4
                for board in BOARDS if name in BOARD_SCREENS else [None]:
                    ENV["FX_PREVIEW_BOARD"] = board or args.board
                    path = args.out / f"{index + 1}_{name.lower()}{'_' + board if board else ''}.png"
                    render(exe, index, frames, args.fps)[int(frames * at)].resize(
                        (WIDTH * 4, HEIGHT * 4), Image.NEAREST).save(path)
                    print(f"wrote {path.relative_to(REPO) if path.is_relative_to(REPO) else path}")
            ENV["FX_PREVIEW_BOARD"] = args.board
            return
        if args.themes:
            # a row a theme: the first screen as it is turned to (its name up), then each typing flat out
            screens = [names().index(name) for name in themed()]
            still = int(frames * 0.7)
            sheet = [[render(exe, screens[0], still + 1, args.fps, theme, "name")[still]] +
                     [render(exe, screen, still + 1, args.fps, theme)[still] for screen in screens]
                     for theme in range(len(themes()))]
            contact = Image.new("RGB", ((WIDTH + 4) * len(sheet[0]) - 4, (HEIGHT + 4) * len(sheet) - 4), "#202020")
            for row, shots in enumerate(sheet):
                for col, shot in enumerate(shots):
                    contact.paste(shot, (col * (WIDTH + 4), row * (HEIGHT + 4)))
            contact.save(args.out / "themes.png")
            print(f"wrote themes.png (rows: {', '.join(themes())}; columns: the name, {', '.join(themed())})")
            return
        sheet = []
        for index, name in enumerate(names()):
            images = render(exe, index, frames, args.fps)
            save(images, args.out / f"{index + 1}_{name.lower()}.gif", args.fps)
            # idle, speeding up (the profile just changed), flat out and not connected, idle on USB
            sheet.append([images[int(frames * at)] for at in (0.1, 0.4, 0.7, 0.95)])

    contact = Image.new("RGB", (WIDTH * 4 + 12, (HEIGHT + 4) * len(sheet) - 4), "#202020")
    for row, shots in enumerate(sheet):
        for col, shot in enumerate(shots):
            contact.paste(shot, (col * (WIDTH + 4), row * (HEIGHT + 4)))
    contact.save(args.out / "contact_sheet.png")
    print("wrote contact_sheet.png (columns: idle, speeding up, flat out and not connected, idle on USB)")


if __name__ == "__main__":
    main()
