#!/usr/bin/env python3
"""Render the Prospector dongle's screen animations to GIFs on this computer.

The animations (src/screen_fx/fx.c) are plain C, so this compiles them with
the made-up typing in tools/fx_preview/main.c and saves what the dongle would
draw, without flashing anything:

    tools/fx_preview.py              # every animation -> fx_preview_out/*.gif
    tools/fx_preview.py --fps 30     # what a faster screen link would give

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


def render(exe: Path, index: int, frames: int, fps: float) -> list[Image.Image]:
    raw = subprocess.run([str(exe), str(index), str(frames), str(fps)], check=True,
                         capture_output=True).stdout
    size = WIDTH * HEIGHT * 3
    return [Image.frombytes("RGB", (WIDTH, HEIGHT), raw[i:i + size]) for i in range(0, len(raw), size)]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--fps", type=float, default=15, help="frames per second (default 15)")
    parser.add_argument("--seconds", type=float, default=12, help="length of each clip")
    parser.add_argument("--out", type=Path, default=REPO / "fx_preview_out")
    parser.add_argument("--prospector", type=Path,
                        help="a checkout of prospector-zmk-module (default: fetch the pinned commit)")
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    module = args.prospector or prospector(REPO / "fx_preview_out")
    with tempfile.TemporaryDirectory() as tmp:
        exe = build(Path(tmp), module)
        frames = int(args.seconds * args.fps)
        sheet = []
        for index, name in enumerate(names()):
            images = render(exe, index, frames, args.fps)
            path = args.out / f"{index + 1}_{name.lower()}.gif"
            images[0].save(path, save_all=True, append_images=images[1:],
                           duration=round(1000 / args.fps), loop=0)
            print(f"wrote {path.relative_to(REPO) if path.is_relative_to(REPO) else path}")
            # idle, speeding up, flat out and not connected, idle on USB
            sheet.append([images[int(frames * at)] for at in (0.1, 0.4, 0.7, 0.95)])

    contact = Image.new("RGB", (WIDTH * 4 + 12, (HEIGHT + 4) * len(sheet) - 4), "#202020")
    for row, shots in enumerate(sheet):
        for col, shot in enumerate(shots):
            contact.paste(shot, (col * (WIDTH + 4), row * (HEIGHT + 4)))
    contact.save(args.out / "contact_sheet.png")
    print("wrote contact_sheet.png (columns: idle, speeding up, flat out and not connected, idle on USB)")


if __name__ == "__main__":
    main()
