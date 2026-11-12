#!/usr/bin/env python3
"""Render the Prospector dongle's screen animations to GIFs on this computer.

The animations (src/screen_fx/fx.c) are plain C, so this compiles them with
the made-up typing in tools/fx_preview/main.c and saves what the dongle would
draw, without flashing anything:

    tools/fx_preview.py              # every animation -> fx_preview_out/*.gif
    tools/fx_preview.py --fps 30     # what a faster screen link would give

Needs gcc and Pillow (pip install pillow). Colours are the panel's 16-bit
ones; speed and tearing on the real screen will differ.
"""

import argparse
import re
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent
FX_DIR = REPO / "src" / "screen_fx"
WIDTH, HEIGHT = 280, 240


def build(workdir: Path) -> Path:
    exe = workdir / "fx_preview"
    subprocess.run(
        ["gcc", "-O2", "-Wall", "-Wextra", "-I", str(FX_DIR), str(FX_DIR / "fx.c"),
         str(REPO / "tools" / "fx_preview" / "main.c"), "-lm", "-o", str(exe)],
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
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        exe = build(Path(tmp))
        frames = int(args.seconds * args.fps)
        sheet = []
        for index, name in enumerate(names()):
            images = render(exe, index, frames, args.fps)
            path = args.out / f"{index + 1}_{name.lower()}.gif"
            images[0].save(path, save_all=True, append_images=images[1:],
                           duration=round(1000 / args.fps), loop=0)
            print(f"wrote {path.relative_to(REPO) if path.is_relative_to(REPO) else path}")
            # idle, speeding up, flat out
            sheet.append([images[int(frames * at)] for at in (0.1, 0.4, 0.7)])

    contact = Image.new("RGB", (WIDTH * 3 + 8, (HEIGHT + 4) * len(sheet) - 4), "#202020")
    for row, shots in enumerate(sheet):
        for col, shot in enumerate(shots):
            contact.paste(shot, (col * (WIDTH + 4), row * (HEIGHT + 4)))
    contact.save(args.out / "contact_sheet.png")
    print("wrote contact_sheet.png (columns: idle, speeding up, flat out)")


if __name__ == "__main__":
    main()
