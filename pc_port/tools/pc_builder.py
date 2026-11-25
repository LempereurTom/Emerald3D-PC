#!/usr/bin/env python3
"""Emerald PC's builder: the game data, rebuilt from the player's own ROM.

A release carries no game content (docs/PC_PORT_STATUS.md, J9): its
executable has the game's read-only data zeroed out, and its romfs/ holds the
engine's own files only. This builder runs Emerald 3DS's builder
(builder/emerald3ds_builder: the recipe applied to the ROM, every file
checked against its CRC, the voxel scenery made from the ROM's own art) and
writes the result as loose files where the game reads them: the user folder's
devdata/ (%APPDATA%/EmeraldPC/devdata, $XDG_DATA_HOME/emerald-pc/devdata),
with the marker naming the engine ABI it was made for.

    emerald-pc-builder --rom "Pokemon - Emerald Version.gba" [--payload DIR] [--home DIR]

The game starts it on its first run (or when its data no longer matches the
executable) with the ROM the player picks.
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
for candidate in (HERE / "builder", HERE.parents[1] / "builder" if len(HERE.parents) > 1 else None):
    if candidate and candidate.is_dir():
        sys.path.insert(0, str(candidate))

from emerald3ds_builder import build as builder_build  # noqa: E402
from emerald3ds_builder.build import Payload, build_pack  # noqa: E402
from emerald3ds_builder.errors import BuilderError  # noqa: E402

MARKER = ".emerald3ds-dev"


def default_home() -> Path:
    """The user folder the game uses (pc_port/src/pc_paths.c)."""
    if os.environ.get("EMERALD_PC_HOME"):
        return Path(os.environ["EMERALD_PC_HOME"])
    if sys.platform == "win32":
        return Path(os.environ.get("APPDATA", Path.home())) / "EmeraldPC"
    data = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(data) / "emerald-pc"


def default_payload() -> Path:
    """payload/ beside the builder (its executable's folder once frozen)."""
    base = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else HERE
    return base / "payload"


def progress(fraction: float, message: str) -> None:
    # One line a step: the game shows the last one while it waits; under
    # RecompLauncher ("prepare" in recomplauncher.json), its own form.
    if os.environ.get("RECOMPLAUNCHER") == "1":
        print("progress %.2f %s" % (fraction, message), flush=True)
    else:
        print("[%3d%%] %s" % (int(fraction * 100), message), flush=True)


def build(rom: Path, payload: Path, home: Path, followers: bool = True) -> dict:
    out = home / "devdata"
    work = Path(tempfile.mkdtemp(prefix="emerald-pc-"))
    try:
        pack = work / "emerald-pc.pak"
        # The pack names its files by hash only: keep their paths as the
        # builder hands them to the pack writer (which still writes and
        # verifies the pack, as for the console).
        written: list[tuple[str, bytes]] = []
        write_pak = builder_build.pak.write_pak

        def keep(out_pak, files, *rest, **kw):
            files = list(files)
            written.extend(files)
            return write_pak(out_pak, files, *rest, **kw)

        builder_build.pak.write_pak = keep
        try:
            summary = build_pack(rom, Payload(payload), pack, progress=progress, follower_fetcher=followers)
        finally:
            builder_build.pak.write_pak = write_pak
        # A new folder first, then swapped in: a build that fails half way
        # leaves the old data as it was.
        fresh = home / "devdata.new"
        if fresh.exists():
            shutil.rmtree(fresh)
        fresh.mkdir(parents=True)
        for rel, data in written:
            target = fresh / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        (fresh / MARKER).write_text("abi %08x\n" % summary["abi"], encoding="ascii")
        if out.exists():
            shutil.rmtree(out)
        fresh.rename(out)
        return summary
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--payload", type=Path, default=None)
    ap.add_argument("--home", type=Path, default=None)
    ap.add_argument("--no-followers", action="store_true", help="leave the optional follower sprites out")
    args = ap.parse_args(argv)
    try:
        summary = build(args.rom, args.payload or default_payload(), args.home or default_home(),
                        followers=not args.no_followers)
    except BuilderError as exc:
        print("ERROR: %s" % exc, flush=True)
        return 1
    print("DONE: %d files from %s, ABI %08x" % (summary["entries"], summary["rom"], summary["abi"]), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
