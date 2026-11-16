#!/usr/bin/env python3
"""Render the Prospector dongle's screen animations to GIFs on this computer.

The animations (src/screen_fx/fx.c) are plain C, so this compiles them with
the made-up typing in tools/fx_preview/main.c and saves what the dongle would
draw, without flashing anything:

    tools/fx_preview.py              # every animation -> fx_preview_out/*.gif
    tools/fx_preview.py --fps 30     # what a faster screen link would give
    tools/fx_preview.py --saver thermal  # the screensaver coming over Thermal, and going
    tools/fx_preview.py --power keys # the panel going off and coming on, on Keys
    tools/fx_preview.py --themes     # every colour theme -> fx_preview_out/themes.png

The battery levels and the output are drawn over them as on the dongle
(src/screen_fx/status_overlay.c), in fonts from the Prospector module. Those
are fetched once, at the commit config/west.yml pins, into fx_preview_out/;
or point --prospector at a checkout of the module.

Needs gcc, git and Pillow (pip install pillow). Colours are the panel's
16-bit ones; speed and tearing on the real screen will differ.
"""

import argparse
import re
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent
FX_DIR = REPO / "src" / "screen_fx"
TOOL_DIR = REPO / "tools" / "fx_preview"
WIDTH, HEIGHT = 280, 240
PROSPECTOR_URL = "https://github.com/carrefinho/prospector-zmk-module"
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
    # the names in fx.c's `schemes` table, in order
    text = (FX_DIR / "fx.c").read_text()
    table = text[text.index("} schemes[] = {"):text.index("#define SCHEMES")]
    return re.findall(r'\{"(\w+)"', table)


def themed() -> list[str]:
    # the animations drawn in the theme's colours: the table gives them a colours function
    text = (FX_DIR / "fx.c").read_text()
    table = text[text.index("} effects[] = {"):text.index("#define FX_COUNT")]
    return [name for name, colors in re.findall(r'\{"(\w+)", \w+, \w+, (\w+)', table) if colors != "NULL"]


def save(images: list[Image.Image], path: Path, fps: float) -> None:
    images[0].save(path, save_all=True, append_images=images[1:], duration=round(1000 / fps), loop=0)
    print(f"wrote {path.relative_to(REPO) if path.is_relative_to(REPO) else path}")


def render(exe: Path, *arguments: object) -> list[Image.Image]:
    raw = subprocess.run([str(exe), *map(str, arguments)], check=True, capture_output=True).stdout
    size = WIDTH * HEIGHT * 3
    return [Image.frombytes("RGB", (WIDTH, HEIGHT), raw[i:i + size]) for i in range(0, len(raw), size)]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--fps", type=float, default=15, help="frames per second (default 15)")
    parser.add_argument("--seconds", type=float, default=12, help="length of each clip")
    parser.add_argument("--out", type=Path, default=REPO / "fx_preview_out")
    parser.add_argument("--saver", metavar="ANIMATION",
                        help="instead: each screensaver coming over this animation and going again")
    parser.add_argument("--power", metavar="ANIMATION",
                        help="instead: the panel going off and coming back on, with this animation showing")
    parser.add_argument("--themes", action="store_true",
                        help="instead: one picture of every colour theme, on each screen drawn in them")
    parser.add_argument("--prospector", type=Path,
                        help="a checkout of prospector-zmk-module (default: fetch the pinned commit)")
    args = parser.parse_args()

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
