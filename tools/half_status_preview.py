#!/usr/bin/env python3
"""Render the left half's nice!view screen behind the dongle on this computer.

The drawing (src/half_status_draw.c) is plain LVGL, so this compiles it with
LVGL and tools/half_status_preview/main.c and saves what the panel would show,
for a few made-up states, without flashing anything:

    tools/half_status_preview.py          # -> half_status_preview_out/half_status.png

LVGL is fetched once into half_status_preview_out/ (a few MB) and built there.
It is the version Zephyr 4.1 ships, which is what the pinned ZMK uses; if ZMK
moves on, change LVGL_VERSION. The panel is one bit per pixel, so the picture is
thresholded at half grey (as the display driver does, as far as I know) and
drawn upright, as the screen is read, 4x.

Needs gcc, git and Pillow (pip install pillow).
"""

import argparse
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parent.parent
TOOL_DIR = REPO / "tools" / "half_status_preview"
LVGL_URL = "https://github.com/lvgl/lvgl"
LVGL_VERSION = "v9.2.2"
CASES = ["bluetooth", "paired", "free", "usb", "charging", "right_charging", "waiting", "no_dongle"]
SCALE = 4


def lvgl_library(cache: Path) -> tuple[Path, Path]:
    """LVGL checked out and built as a static library: (source dir, library)."""
    src = cache / f"lvgl-{LVGL_VERSION}"
    lib = cache / f"liblvgl-{LVGL_VERSION}.a"
    if not src.is_dir():
        print(f"fetching LVGL {LVGL_VERSION}")
        subprocess.run(["git", "clone", "-q", "--depth", "1", "-b", LVGL_VERSION, LVGL_URL, str(src)],
                       check=True)
    if not lib.exists():
        print("building LVGL (once)")
        objs = cache / "obj"
        objs.mkdir(exist_ok=True)
        sources = sorted((src / "src").rglob("*.c"))
        for i, c in enumerate(sources):
            o = objs / (str(c.relative_to(src)).replace("/", "_") + ".o")
            subprocess.run(["gcc", "-c", "-O1", "-w", "-DLV_CONF_INCLUDE_SIMPLE", f"-I{TOOL_DIR}",
                            f"-I{src}", str(c), "-o", str(o)], check=True)
        subprocess.run(["ar", "rcs", str(lib), *map(str, objs.glob("*.o"))], check=True)
    return src, lib


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", type=Path, default=REPO / "half_status_preview_out")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(exist_ok=True)

    src, lib = lvgl_library(out)
    exe = out / "half_status_preview"
    subprocess.run(["gcc", "-O1", "-DLV_CONF_INCLUDE_SIMPLE", f"-I{TOOL_DIR}", f"-I{src}",
                    f"-I{REPO / 'src'}", str(TOOL_DIR / "main.c"), str(REPO / "src" / "half_status_draw.c"),
                    str(lib), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe), str(out), *CASES], check=True)

    tiles = []
    for case in CASES:
        panel = Image.open(out / f"{case}.pgm")
        mono = panel.point(lambda v: 255 if v >= 128 else 0)
        upright = mono.rotate(90, expand=True)  # back to how the screen is read
        tiles.append(upright.resize((upright.width * SCALE, upright.height * SCALE), Image.NEAREST))

    gap, label_h = 16, 20
    w, h = tiles[0].size
    sheet = Image.new("RGB", (len(tiles) * (w + gap) + gap, h + 2 * gap + label_h), (90, 90, 90))
    draw = ImageDraw.Draw(sheet)
    for i, (case, tile) in enumerate(zip(CASES, tiles)):
        x = gap + i * (w + gap)
        draw.text((x, gap // 2), case, fill=(255, 255, 255))
        sheet.paste(tile, (x, gap + label_h))
    path = out / "half_status.png"
    sheet.save(path)
    print(path)


if __name__ == "__main__":
    main()
