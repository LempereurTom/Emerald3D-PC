#!/usr/bin/env python3
"""Make a PC release's executable and its data staging from a development build.

A development executable (pc.mk's `pc`) carries the game's read-only data -
tables, texts, scripts, songs - gathered between two markers
(pc_port/src/pc_game_group.c: __ctr_gamedata, __ctr_gamedata_end). This tool:

* takes the bytes between them out: `gamedata/gamedata.bin` of the staging,
  with `gamedata/gamedata.rel` (the console's bundle header and the payload's
  pointer sites, read from the PE's base relocations: those bytes are
  addresses of this executable, literals for the recipe, never game content);
* writes the release executable: the same file with that range zeroed and the
  start marker rewritten EXTERNAL, so the game reads the range back from the
  player's data as it starts (pc_port/src/pc_bundles.c);
* writes engine/, the engine's own files beside the executable (romfs:/ on
  the PC: the asset index, relocation table, shaders, the port's art);
* stages the data as the console's RomFS is staged (the development romfs
  plus the game data, without the embedded marker) and writes its ABI, so
  tools/gen_recipe.py, staging.py and release_audit.py work on it unchanged.

The executable loads where it was linked (no ASLR, pc.mk), so the payload's
pointers are final: nothing is relocated at run time.

    python pc_port/tools/pc_release.py --exe build/upstream-pc/3ds_port/emerald-pc.exe \\
        --romfs build/upstream-pc/3ds_port/build/pc-romfs --out build/pc-release
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "port_common"))
import staging  # noqa: E402

START_MARK = b"EMERALD-PC GAMEDATA START"
EXTERNAL_MARK = b"EMERALD-PC GAMEDATA EXTERNAL"
BUNDLE_MAGIC = b"C3GD"
BUNDLE_VERSION = 1


class Pe:
    def __init__(self, data: bytes):
        self.data = data
        if data[:2] != b"MZ":
            raise SystemExit("pc_release: not a PE file")
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise SystemExit("pc_release: no PE signature")
        coff = pe + 4
        self.sections_count, = struct.unpack_from("<H", data, coff + 2)
        opt_size, = struct.unpack_from("<H", data, coff + 16)
        opt = coff + 20
        magic, = struct.unpack_from("<H", data, opt)
        if magic != 0x10B:
            raise SystemExit("pc_release: only 32-bit PE executables are handled")
        self.image_base, = struct.unpack_from("<I", data, opt + 28)
        dirs = opt + 96
        self.reloc_rva, self.reloc_size = struct.unpack_from("<II", data, dirs + 5 * 8)
        self.sections = []
        sec = opt + opt_size
        for i in range(self.sections_count):
            name = data[sec:sec + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, va, raw_size, raw_ptr = struct.unpack_from("<IIII", data, sec + 8)
            self.sections.append((name, va, vsize, raw_ptr, raw_size))
            sec += 40

    def file_offset(self, rva: int) -> tuple[int, str]:
        for name, va, vsize, raw_ptr, raw_size in self.sections:
            if va <= rva < va + max(vsize, raw_size):
                if rva - va >= raw_size:
                    raise SystemExit("pc_release: RVA %#x is not in %s's file data" % (rva, name))
                return raw_ptr + rva - va, name
        raise SystemExit("pc_release: RVA %#x is in no section" % rva)

    def relocation_sites(self) -> list[int]:
        """Every 32-bit absolute address of the image (IMAGE_REL_BASED_HIGHLOW), as RVAs."""
        if self.reloc_rva == 0:
            raise SystemExit("pc_release: no base relocations (pc.mk links with --enable-reloc-section)")
        at, _ = self.file_offset(self.reloc_rva)
        end = at + self.reloc_size
        sites = []
        while at + 8 <= end:
            page, size = struct.unpack_from("<II", self.data, at)
            if size < 8:
                break
            for k in range((size - 8) // 2):
                entry, = struct.unpack_from("<H", self.data, at + 8 + 2 * k)
                kind, offset = entry >> 12, entry & 0xFFF
                if kind == 3:
                    sites.append(page + offset)
                elif kind != 0:
                    raise SystemExit("pc_release: base relocation type %d not handled" % kind)
            at += size
        return sites


def symbols(nm: str, exe: Path) -> dict[str, int]:
    out = subprocess.check_output([nm, "--defined-only", str(exe)], text=True, errors="ignore")
    result = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            result[parts[2]] = int(parts[0], 16)
    return result


def main(argv=None) -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, required=True, help="the development executable")
    ap.add_argument("--romfs", type=Path, required=True, help="its development data (build/pc-romfs)")
    ap.add_argument("--out", type=Path, required=True,
                    help="writes OUT/emerald-pc.exe, OUT/staging/ (all data) and OUT/engine/ (engine files only)")
    ap.add_argument("--nm", default="nm")
    args = ap.parse_args(argv)

    data = bytearray(args.exe.read_bytes())
    pe = Pe(bytes(data))
    syms = symbols(args.nm, args.exe)
    start = syms.get("___ctr_gamedata", syms.get("__ctr_gamedata"))
    end = syms.get("___ctr_gamedata_end", syms.get("__ctr_gamedata_end"))
    if start is None or end is None:
        raise SystemExit("pc_release: the game-data markers are not in %s" % args.exe)
    mark_rva = start - pe.image_base
    lo, hi = mark_rva + 32, end - pe.image_base
    mark_at, mark_section = pe.file_offset(mark_rva)
    lo_at, lo_section = pe.file_offset(lo)
    hi_at, hi_section = pe.file_offset(hi)
    if data[mark_at:mark_at + len(START_MARK)] != START_MARK:
        raise SystemExit("pc_release: %s is already a release (or its marker moved)" % args.exe)
    if not (mark_section == lo_section == hi_section) or hi_at - lo_at != hi - lo:
        raise SystemExit("pc_release: the game data is not one range of one section")
    payload = bytes(data[lo_at:hi_at])
    sites = sorted(rva - lo for rva in pe.relocation_sites() if lo <= rva and rva + 4 <= hi)
    print("pc_release: game data %d KiB in %s at %#x, %d pointer sites"
          % (len(payload) >> 10, lo_section, lo + pe.image_base, len(sites)))

    # The staging: the development data, the game data, no embedded marker.
    stage = args.out / "staging"
    if stage.exists():
        shutil.rmtree(stage)
    shutil.copytree(args.romfs, stage)
    (stage / "data.embedded").unlink(missing_ok=True)
    gd = stage / "gamedata"
    gd.mkdir(parents=True, exist_ok=True)
    (gd / "gamedata.bin").write_bytes(payload)
    header = struct.pack("<4sIIIII", BUNDLE_MAGIC, BUNDLE_VERSION, len(payload), lo + pe.image_base, 0, len(sites))
    (gd / "gamedata.rel").write_bytes(header + struct.pack("<%dI" % len(sites), *sites))
    staging.main(["abi", "--romfs", str(stage)])

    # The engine's own files, beside the executable (no game data).
    staging.main(["engine", "--romfs", str(stage), "--out", str(args.out / "engine")])

    # The executable: the range zeroed, the marker EXTERNAL.
    data[lo_at:hi_at] = bytes(hi_at - lo_at)
    data[mark_at:mark_at + 32] = EXTERNAL_MARK.ljust(32, b"\0")
    exe = args.out / "emerald-pc.exe"
    exe.write_bytes(bytes(data))
    # No debug information in a release: the DWARF carries the game's
    # identifiers and constants, and the audit reads every byte.
    strip = str(Path(args.nm).with_name(Path(args.nm).name.replace("nm", "strip")))
    subprocess.run([strip, "--strip-debug", str(exe)], check=True)
    print("pc_release: %s (game data taken out, no debug information), %s, %s"
          % (exe, stage, args.out / "engine"))


if __name__ == "__main__":
    main()
