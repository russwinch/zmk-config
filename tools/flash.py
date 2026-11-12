#!/usr/bin/env python3
"""Flash a keyboard's halves (and dongle) in order, with or without a settings reset.

Copies each .uf2 onto the bootloader drive as soon as one appears, so the
only manual step is putting each device into its bootloader (double-tap
reset). Devices are done in a fixed order; for the dongle setup that is
dongle, left, right, which is also the order the Prospector wants them paired.

    python3 tools/flash.py waterfowl-dongle --firmware ~/Downloads/firmware.zip
    python3 tools/flash.py waterfowl-dongle --reset --download
    python3 tools/flash.py waterfowl-dongle --only dongle --firmware firmware.zip
    python3 tools/flash.py --list

--reset flashes settings_reset first on each device (clears its Bluetooth
pairings), then that device's firmware: two bootloader trips per device.

Standard library only (Python 3.9+). macOS, Linux and Windows. It never asks
for input: it prints what to do next and waits for the drive, so it can also
be run by an agent while a person handles the hardware (see CLAUDE.md).
"""

from __future__ import annotations

import argparse
import os
import shutil
import string
import subprocess
import sys
import tempfile
import time
import zipfile
from dataclasses import dataclass
from pathlib import Path

REPO = "russwinch/zmk-config"
SETTINGS_RESET = "settings_reset"


@dataclass(frozen=True)
class Device:
    name: str  # used with --only
    what: str  # shown to the person
    firmware: str  # first shield in build.yaml, or the entry's artifact-name
    board: str  # "nice_nano" or "xiao_ble" (picks the settings_reset file, checks the drive)


# Keep in step with build.yaml. Order matters: it is the flashing (and so the
# pairing) order, dongle first, then left, then right.
TARGETS: dict[str, list[Device]] = {
    "waterfowl-dongle": [
        Device("dongle", "Prospector dongle", "waterfowl_dongle", "xiao_ble"),
        Device("left", "Waterfowl LEFT half", "waterfowl_left_peripheral_for_dongle", "nice_nano"),
        Device("right", "Waterfowl RIGHT half", "waterfowl_right", "nice_nano"),
    ],
    "waterfowl": [
        Device("left", "Waterfowl LEFT half", "waterfowl_left", "nice_nano"),
        Device("right", "Waterfowl RIGHT half", "waterfowl_right", "nice_nano"),
    ],
    "urchin-dongle": [
        Device("dongle", "Prospector dongle", "urchin_dongle", "xiao_ble"),
        Device("left", "Urchin LEFT half", "urchin_left_peripheral_for_dongle", "nice_nano"),
        Device("right", "Urchin RIGHT half", "urchin_right", "nice_nano"),
    ],
    "urchin": [
        Device("left", "Urchin LEFT half", "urchin_left", "nice_nano"),
        Device("right", "Urchin RIGHT half", "urchin_right", "nice_nano"),
    ],
    "aurora-dongle": [
        Device("dongle", "Prospector dongle", "splitkb_aurora_corne_dongle", "xiao_ble"),
        Device("left", "Aurora Corne LEFT half", "splitkb_aurora_corne_left_peripheral_for_dongle", "nice_nano"),
        Device("right", "Aurora Corne RIGHT half", "splitkb_aurora_corne_right", "nice_nano"),
    ],
    "aurora": [
        Device("left", "Aurora Corne LEFT half", "splitkb_aurora_corne_left", "nice_nano"),
        Device("right", "Aurora Corne RIGHT half", "splitkb_aurora_corne_right", "nice_nano"),
    ],
    "forager-dongle": [
        Device("dongle", "Prospector dongle", "forager_dongle", "xiao_ble"),
        Device("left", "Forager LEFT half", "forager_left_peripheral_for_dongle", "xiao_ble"),
        Device("right", "Forager RIGHT half", "forager_right", "xiao_ble"),
    ],
    "forager": [
        Device("left", "Forager LEFT half", "forager_left", "xiao_ble"),
        Device("right", "Forager RIGHT half", "forager_right", "xiao_ble"),
    ],
}

# Words looked for in a bootloader drive's INFO_UF2.TXT to tell the boards apart.
BOARD_HINTS = {"nice_nano": ("nicenano", "nice!nano", "nice_nano"), "xiao_ble": ("xiao",)}


def fail(msg: str) -> None:
    print(f"\nERROR: {msg}", file=sys.stderr)
    sys.exit(1)


# --------------------------------------------------------------------------
# Finding the firmware files


def uf2_key(path: Path) -> tuple[str, str | None]:
    """(first shield or artifact-name, board) for a CI-built .uf2.

    ZMK's workflow names files "<shield list>-<board with / as _>-zmk.uf2",
    e.g. "waterfowl_left nice_view_adapter nice_view-nice_nano__zmk-zmk.uf2",
    or "<artifact-name>.uf2" when build.yaml sets one.
    """
    parts = path.stem.rsplit("-", 2)
    if len(parts) == 3 and parts[2] == "zmk":
        return parts[0].split()[0], parts[1].split("__")[0]
    return path.stem, None


def find_uf2(files: list[Path], firmware: str, board: str) -> Path:
    matches = [
        f for f in files
        if uf2_key(f)[0] == firmware and uf2_key(f)[1] in (None, board)
    ]
    if len(matches) != 1:
        names = "\n  ".join(sorted(f.name for f in files)) or "(none)"
        what = "no" if not matches else "more than one"
        fail(f"{what} .uf2 for {firmware} ({board}). Files found:\n  {names}")
    return matches[0]


def unpack(firmware: Path) -> list[Path]:
    if firmware.is_dir():
        return sorted(firmware.rglob("*.uf2"))
    if firmware.suffix == ".zip":
        out = Path(tempfile.mkdtemp(prefix="zmk-firmware-"))
        with zipfile.ZipFile(firmware) as z:
            z.extractall(out)
        return sorted(out.rglob("*.uf2"))
    fail(f"{firmware} is not a .zip or a folder")
    return []


def download(branch: str) -> Path:
    """Latest successful build of `branch` via the GitHub CLI (gh)."""
    if not shutil.which("gh"):
        fail("--download needs the GitHub CLI (gh), logged in. Or pass --firmware.")
    run_id = subprocess.run(
        ["gh", "run", "list", "-R", REPO, "-w", "build.yml", "-b", branch,
         "-s", "success", "-L", "1", "--json", "databaseId", "-q", ".[0].databaseId"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    if not run_id:
        fail(f"no successful build of {branch} found")
    out = Path(tempfile.mkdtemp(prefix="zmk-firmware-"))
    print(f"Downloading firmware from build run {run_id} ({branch})...")
    subprocess.run(["gh", "run", "download", run_id, "-R", REPO, "-n", "firmware", "-D", str(out)],
                   check=True)
    return out


def default_firmware() -> Path:
    zips = sorted((Path.home() / "Downloads").glob("firmware*.zip"), key=lambda p: p.stat().st_mtime)
    if not zips:
        fail("no --firmware given and no firmware*.zip in ~/Downloads (or use --download)")
    print(f"Using the newest download: {zips[-1]}")
    return zips[-1]


# --------------------------------------------------------------------------
# Bootloader drives


def mount_roots() -> list[Path]:
    if sys.platform == "darwin":
        return [Path("/Volumes")]
    user = os.environ.get("USER", "")
    return [Path("/media") / user, Path("/run/media") / user, Path("/media"), Path("/mnt")]


def windows_drives() -> list[Path]:
    from ctypes import windll  # type: ignore[attr-defined]

    mask = windll.kernel32.GetLogicalDrives()
    return [Path(f"{letter}:\\") for i, letter in enumerate(string.ascii_uppercase) if mask >> i & 1]


def uf2_drives(roots: list[Path] | None) -> list[Path]:
    """Mounted drives that look like a UF2 bootloader (they have INFO_UF2.TXT)."""
    if roots is None:
        candidates = windows_drives() if sys.platform == "win32" else [
            d for r in mount_roots() if r.is_dir() for d in r.iterdir()
        ]
    else:
        candidates = [d for r in roots if r.is_dir() for d in r.iterdir()]
    found = []
    for d in candidates:
        try:
            if (d / "INFO_UF2.TXT").is_file():
                found.append(d)
        except OSError:
            pass  # e.g. an empty card reader on Windows
    return found


def drive_board(drive: Path) -> str | None:
    try:
        info = (drive / "INFO_UF2.TXT").read_text(errors="replace").lower()
    except OSError:
        return None
    for board, hints in BOARD_HINTS.items():
        if any(h in info for h in hints):
            return board
    return None


def wait_for_drive(roots: list[Path] | None, timeout: float) -> Path:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        drives = uf2_drives(roots)
        if len(drives) == 1:
            time.sleep(1)  # let the OS finish mounting it
            return drives[0]
        if len(drives) > 1:
            fail("more than one bootloader drive is mounted: unplug or eject the extras and rerun")
        time.sleep(0.5)
    fail("timed out waiting for a bootloader drive")
    return Path()


def wait_until_gone(drive: Path, timeout: float = 30) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not drive.exists():
            return True
        time.sleep(0.5)
    return False


def flash(uf2: Path, device: Device, step: str, roots: list[Path] | None, timeout: float,
          force: bool) -> None:
    print(f"\n>>> {step}: {device.what}")
    print(f"    Double-tap reset on the {device.what} (plug it in by USB first).")
    print(f"    Waiting for its bootloader drive... (file: {uf2.name})", flush=True)
    drive = wait_for_drive(roots, timeout)

    board = drive_board(drive)
    if board is not None and board != device.board:
        msg = f"the drive at {drive} looks like a {board}, but {device.what} should be a {device.board}"
        if not force:
            fail(msg + ". Nothing was copied. (--force skips this check.)")
        print(f"    WARNING: {msg}; copying anyway (--force)")

    print(f"    Found {drive}, copying...", flush=True)
    try:
        shutil.copyfile(uf2, drive / uf2.name)
    except OSError as e:
        # The board reboots as soon as it has the whole file, which can make
        # the copy itself report an error (common on macOS). Judge by whether
        # the drive goes away.
        print(f"    (copy reported: {e}; checking the board rebooted)")
    if not wait_until_gone(drive):
        fail(f"{drive} is still mounted 30s after the copy: the flash probably didn't take")
    print("    Done: it rebooted.")


# --------------------------------------------------------------------------


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("target", nargs="?", choices=sorted(TARGETS), help="keyboard (see --list)")
    src = p.add_mutually_exclusive_group()
    src.add_argument("--firmware", type=Path, help="CI firmware .zip or a folder of .uf2 files "
                     "(default: newest ~/Downloads/firmware*.zip)")
    src.add_argument("--download", action="store_true",
                     help="fetch the latest successful build with the GitHub CLI (gh)")
    p.add_argument("--branch", default="main", help="branch for --download (default: main)")
    p.add_argument("--reset", action="store_true",
                   help="flash settings_reset first on each device (clears its pairings)")
    p.add_argument("--only", help="comma-separated devices to do, e.g. 'dongle' or 'left,right'")
    p.add_argument("--dry-run", action="store_true", help="show the plan and check the files, flash nothing")
    p.add_argument("--timeout", type=float, default=300, help="seconds to wait for each drive (default 300)")
    p.add_argument("--force", action="store_true", help="copy even if the drive looks like the wrong board")
    p.add_argument("--volumes", type=Path, action="append", help=argparse.SUPPRESS)  # testing
    p.add_argument("--list", action="store_true", help="list keyboards and their devices")
    args = p.parse_args()

    if args.list or not args.target:
        for name, devices in TARGETS.items():
            print(f"{name}: " + ", ".join(f"{d.name} ({d.firmware}, {d.board})" for d in devices))
        if not args.list:
            p.error("a keyboard is required")
        return

    devices = TARGETS[args.target]
    if args.only:
        wanted = [w.strip() for w in args.only.split(",")]
        unknown = set(wanted) - {d.name for d in devices}
        if unknown:
            fail(f"--only: unknown device(s) {', '.join(sorted(unknown))} for {args.target}")
        devices = [d for d in devices if d.name in wanted]

    source = download(args.branch) if args.download else (args.firmware or default_firmware())
    files = unpack(source.expanduser())

    plan: list[tuple[Path, Device, str]] = []
    for d in devices:
        if args.reset:
            plan.append((find_uf2(files, SETTINGS_RESET, d.board), d, "settings reset"))
        plan.append((find_uf2(files, d.firmware, d.board), d, "firmware"))

    print(f"\nPlan ({len(plan)} flash{'es' if len(plan) != 1 else ''}, in this order):")
    for i, (uf2, d, step) in enumerate(plan, 1):
        print(f"  {i}. {d.what}: {step}  [{uf2.name}]")
    if args.dry_run:
        return
    if args.reset and args.target.endswith("-dongle"):
        print("\nKeep the dongle powered throughout, so each half pairs with it as soon as it "
              "has its firmware (left before right sets the Prospector's battery order).")

    for i, (uf2, d, step) in enumerate(plan, 1):
        flash(uf2, d, f"[{i}/{len(plan)}] {step}", args.volumes, args.timeout, args.force)

    print("\nAll done.")
    if args.reset:
        print("Pairings were cleared: re-pair the computer (forget the keyboard there first).")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
        sys.exit(130)
