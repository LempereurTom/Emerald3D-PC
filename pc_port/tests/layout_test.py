#!/usr/bin/env python3
"""Do the PC build's save structures have the console's layout?

The save is the 3DS's 128 KiB flash image, written as the game's structures
are in memory (SaveBlock1, SaveBlock2, PokemonStorage...). A save moves from
the 3DS to the PC and back only if every structure has the same size and
every field the same offset with the PC compiler as with the ARM one: GCC for
ARM EABI and GCC for i686 can disagree on bitfields (MinGW's MSVC layout) and
on 64-bit alignment.

This generates one C file that puts sizeof() of each saved structure and
offsetof() of every top-level member of SaveBlock1/SaveBlock2 in a constant
table, compiles it to assembly for both targets (no link, nothing runs) and
compares the two tables value by value.

usage: layout_test.py --tree build/upstream-pc
                      [--arm-cc arm-none-eabi-gcc] [--pc-cc i686-w64-mingw32-gcc]
Exit status 0 when every value matches.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

STRUCTS = [
    "SaveBlock1", "SaveBlock2", "PokemonStorage", "Pokemon", "BoxPokemon",
    "PokemonSubstruct0", "PokemonSubstruct1", "PokemonSubstruct2", "PokemonSubstruct3",
    "SaveSector", "Time", "Pokedex", "PokemonJumpRecords", "BerryPickingResults",
    "PyramidBag", "BerryCrush", "Apprentice", "BattleFrontier", "PlayersApprentice",
    "RankingHall2P", "SecretBase", "WarpData", "ItemSlot", "Roamer", "RamScript",
    "DewfordTrend", "LinkBattleRecords", "RecordMixingGift", "ContestWinner", "Mail",
    "DayCare", "WaldaPhrase", "TrainerHillSave", "MysteryGiftSave", "ExternalEventData",
    "EnigmaBerry", "BerryTree", "ObjectEvent", "ObjectEventTemplate", "GabbyAndTyData",
]

ARM_FLAGS = ["-march=armv6k+fp", "-mtune=mpcore", "-mfloat-abi=hard", "-mtp=soft"]
PC_FLAGS = ["-march=i686", "-msse2", "-mfpmath=sse", "-mno-ms-bitfields", "-D_TIME_BITS=64"]
COMMON = ["-std=gnu99", "-O2", "-S", "-fsigned-char", "-fno-strict-aliasing", "-w",
          "-DPORTABLE", "-DPLATFORM_3DS", "-D__3DS__", "-DMODERN=1", "-DUBFIX",
          "-D__INTELLISENSE__", "-DPORT_BRIDGE", "-DCTR_VOXEL_LIGHTING=1", "-DCTR_SHOW_FPS=1",
          "-DCTR_LANG_PT_BR=0"]


def members(header: str, name: str) -> list[str]:
    """Top-level member names of `struct name` (bitfields and #if blocks skipped)."""
    m = re.search(r"^struct %s\s*\n\{(.*?)^\};" % re.escape(name), header, re.S | re.M)
    if not m:
        sys.exit(f"layout_test: struct {name} not found")
    out, depth, skip = [], 0, 0
    for raw in m.group(1).splitlines():
        line = re.sub(r"//.*|/\*.*?\*/", "", raw).strip()
        if line.startswith("#if"):
            skip += 1
            continue
        if line.startswith("#endif"):
            skip = max(0, skip - 1)
            continue
        if not line or line.startswith("#") or skip:
            continue
        depth += line.count("{") - line.count("}")
        if depth > 0 or line.startswith("}") and depth > 0:
            continue
        if ":" in line and not line.startswith("}"):
            continue  # bitfield: no offsetof
        found = re.search(r"([A-Za-z_]\w*)\s*(\[[^\]]*\]\s*)*;\s*$", line)
        if found:
            out.append(found.group(1))
    return out


def source(tree: Path) -> tuple[str, list[str]]:
    header = (tree / "include" / "global.h").read_text(encoding="utf-8", errors="ignore")
    labels, rows = [], []
    for s in STRUCTS:
        labels.append(f"sizeof(struct {s})")
        rows.append(f"sizeof(struct {s})")
    for s in ("SaveBlock1", "SaveBlock2"):
        for field in members(header, s):
            labels.append(f"offsetof({s}, {field})")
            rows.append(f"offsetof(struct {s}, {field})")
    # Canary: bitfields of mixed types, laid out differently by GCC (4 bytes)
    # and by MSVC (-mms-bitfields: 8). It proves the comparison can see a
    # layout difference, even if no saved structure happens to have one.
    labels.append("sizeof(struct LayoutCanary) [mixed-type bitfields]")
    rows.append("sizeof(struct LayoutCanary)")
    body = ",\n    ".join(f"(unsigned){r}" for r in rows)
    code = ('#include <stddef.h>\n#include "global.h"\n#include "pokemon.h"\n'
            '#include "pokemon_storage_system.h"\n#include "save.h"\n#include "global.berry.h"\n'
            '#include "global.fieldmap.h"\n#include "global.tv.h"\n'
            "struct LayoutCanary { u8 a:4; u16 b:12; u32 c:3; };\n"
            f"const unsigned gLayoutTable[] = {{\n    {body}\n}};\n")
    return code, labels


def table(asm: str) -> list[int]:
    """The gLayoutTable values from GCC's assembly (.word on ARM, .long on x86)."""
    m = re.search(r"_?gLayoutTable:\s*\n(.*?)(?:\n\s*\.(?:size|section|text|ident|globl)|\Z)", asm, re.S)
    if not m:
        sys.exit("layout_test: table not found in the assembly")
    return [int(v, 0) for v in re.findall(r"\.(?:word|long|4byte)\s+(-?\w+)", m.group(1))]


def compile_table(cc: str, flags: list[str], tree: Path, src: Path) -> list[int]:
    includes = ["-iquote", str(tree / "3ds_port" / "compat"), "-iquote", str(tree / "3ds_port" / "include"),
                "-iquote", str(tree / "include"), "-iquote", str(tree / "include" / "constants"),
                "-iquote", str(tree)]
    out = src.with_suffix(f".{Path(cc).stem}.s")
    subprocess.run([cc, *flags, *COMMON, *includes, str(src), "-o", str(out)], check=True)
    return table(out.read_text(encoding="utf-8", errors="ignore"))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tree", type=Path, required=True)
    ap.add_argument("--arm-cc", default="arm-none-eabi-gcc")
    ap.add_argument("--pc-cc", default="i686-w64-mingw32-gcc")
    args = ap.parse_args()
    code, labels = source(args.tree)
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "layout.c"
        src.write_text(code)
        arm = compile_table(args.arm_cc, ARM_FLAGS, args.tree, src)
        pc = compile_table(args.pc_cc, PC_FLAGS, args.tree, src)
    if len(arm) != len(labels) or len(pc) != len(labels):
        print(f"layout_test: table sizes differ (labels {len(labels)}, arm {len(arm)}, pc {len(pc)})")
        return 2
    bad = [(l, a, p) for l, a, p in zip(labels, arm, pc) if a != p]
    for label, a, p in bad:
        print(f"MISMATCH {label}: arm={a} pc={p}")
    print(f"layout_test: {len(labels) - len(bad)}/{len(labels)} values match "
          f"(SaveBlock1 {arm[0]} bytes, SaveBlock2 {arm[1]} bytes, PokemonStorage {arm[2]} bytes)")
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
