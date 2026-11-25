#!/usr/bin/env python3
"""nm as tools/gen_recipe.py and tools/vtree_manifest.py expect it, for a PC build.

Called as `pc_nm.py [--defined-only] [-S] FILE` (through pc_nm.cmd on
Windows, which subprocess can start). FILE is either

* a PE executable of the PC build: MinGW's nm, with the leading underscore of
  32-bit Windows C names taken off (so the names match the GBA game's), and
  `__ctr_gamedata` moved to where the game data's payload starts (32 bytes
  after its marker, pc_port/src/pc_game_group.c), so that symbol addresses
  minus it are payload offsets, as on the console;
* a localized game's reference symbols (`build/<language>-reference.s`, the
  `.equ` / `.size` / `.type` list its localization script writes): printed as
  nm prints an ARM object, without assembling it (no ARM toolchain here).
"""

from __future__ import annotations

import os
import re
import subprocess
import sys

MINGW_NM = os.environ.get("PC_NM", "nm")


def reference(path: str, sizes: bool) -> None:
    values, lengths, kinds = {}, {}, {}
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = re.match(r"\s*\.equ\s+([^,\s]+)\s*,\s*(\S+)", line)
            if m:
                values[m.group(1)] = int(m.group(2), 0)
                continue
            m = re.match(r"\s*\.size\s+([^,\s]+)\s*,\s*(\S+)", line)
            if m:
                lengths[m.group(1)] = int(m.group(2), 0)
                continue
            m = re.match(r"\s*\.type\s+([^,\s]+)\s*,\s*%(\w+)", line)
            if m:
                kinds[m.group(1)] = "T" if m.group(2) == "function" else "D"
    for name, value in values.items():
        kind = kinds.get(name, "D")
        if sizes:
            # Sizeless entries (labels, some functions) give nothing to match
            # a table against: left out, as a sized nm would show no size.
            if lengths.get(name, 0) > 0:
                print("%08x %08x %s %s" % (value, lengths[name], kind, name))
        else:
            print("%08x %s %s" % (value, kind, name))


def map_sizes(exe: str) -> dict[int, int]:
    """Each data object's size by its address: PE symbols carry none, but
    -fdata-sections gives every object its own input section, which the
    link map lists with its size (build/emerald-pc.map beside the build)."""
    path = os.path.join(os.path.dirname(os.path.abspath(exe)), "build", "emerald-pc.map")
    sizes: dict[int, int] = {}
    if not os.path.exists(path):
        return sizes
    pending = False
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = re.match(r"^ \.(?:rdata|data)\$\S+(?:\s+0x([0-9a-f]+)\s+0x([0-9a-f]+))?", line)
            if m:
                if m.group(1):
                    sizes.setdefault(int(m.group(1), 16), int(m.group(2), 16))
                    pending = False
                else:
                    pending = True
                continue
            if pending:
                m = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+\S", line)
                if m:
                    sizes.setdefault(int(m.group(1), 16), int(m.group(2), 16))
            pending = False
    return sizes


def pe(args: list[str], path: str) -> None:
    with_sizes = "-S" in args
    out = subprocess.check_output([MINGW_NM] + [a for a in args if a != "-S"] + [path], text=True,
                                  errors="ignore")
    sizes = map_sizes(path) if with_sizes else {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        name = parts[-1]
        if name.startswith("_"):
            name = name[1:]
        if name.startswith("."):
            continue
        address = int(parts[0], 16)
        if name == "__ctr_gamedata":
            address += 32
        if with_sizes:
            size = sizes.get(address)
            if size is None:
                continue  # code, or a label inside an object: no size to give
            print("%08x %08x %s %s" % (address, size, parts[1], name))
        else:
            print("%08x %s %s" % (address, parts[1], name))


def main() -> None:
    args = sys.argv[1:]
    path = args[-1]
    flags = args[:-1]
    if path.endswith(".s"):
        reference(path, "-S" in flags)
    else:
        pe(flags, path)


if __name__ == "__main__":
    main()
