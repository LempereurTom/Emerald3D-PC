#!/usr/bin/env python3
"""Size, CRC32 and SHA-1 of Pokémon Emerald ROMs, checked against the clean
dumps Emerald 3DS supports (for recomplauncher.json, J10).

    python pc_port/tools/rom_info.py "Pokemon Emeraude.gba" [more.gba ...]

Prints one line a ROM and, for a supported one, the row to paste into
docs/PC_PORT_STATUS.md ("Dumps vérifiés"). Reads the files, writes nothing.
"""

from __future__ import annotations

import hashlib
import sys
import zlib
from pathlib import Path

# The clean dumps the port's localizers accept (tools/localize_*.py, docs/*.md).
SUPPORTED = {
    "f3ae088181bf583e55daf962a92bb46f4f1d07b7": ("BPEE", "English (USA, Europe); also Brazilian Portuguese"),
    "ca666651374d89ca439007bed54d839eb7bd14d0": ("BPEF", "French"),
    "fe1558a3dcb0360ab558969e09b690888b846dd9": ("BPES", "Spanish"),
    "1692db322400c3141c5de2db38469913ceb1f4d4": ("BPEI", "Italian"),
    "61c2eb2b380b1a75f0c94b767a2d4c26cd7ce4e3": ("BPED", "German"),
    "d7cf8f156ba9c455d164e1ea780a6bf1945465c2": ("BPEJ", "Japanese"),
}


def describe(path: Path) -> int:
    data = path.read_bytes()
    sha1 = hashlib.sha1(data).hexdigest()
    crc = zlib.crc32(data) & 0xFFFFFFFF
    code = data[0xAC:0xB0].decode("ascii", errors="replace") if len(data) >= 0xB0 else "?"
    known = SUPPORTED.get(sha1)
    print(f"{path.name}: {len(data)} bytes, CRC32 {crc:08x}, SHA-1 {sha1}, header code {code}")
    if known is None:
        print("  not a supported clean dump (a modified, trimmed or other ROM)")
        return 1
    print(f"  supported: {known[0]}, {known[1]}")
    print(f"  | {known[1]} | {known[0]} | {len(data)} | `{sha1}` | `{crc:08x}` |")
    return 0


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    return max(describe(Path(p)) for p in sys.argv[1:])


if __name__ == "__main__":
    sys.exit(main())
