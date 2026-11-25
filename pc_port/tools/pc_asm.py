#!/usr/bin/env python3
"""Turn the decompilation's ARM GNU as data sources into i686 (PE/COFF) ones.

Used by pc_port/pc.mk in place of full.mk's ASM_PSEUDO_OP_CONV (a sed). The
data files hold no ARM code, only directives, but a few of those differ:

  .4byte / .2byte          -> .int / .short (as the 3DS build does)
  .align N                 -> .p2align N   (ARM: 2^N bytes; i386 PE: N bytes)
  .pushsection/.popsection -> .section     (not supported by the COFF target)
  .type .size .thumb .arm .thumb_func .syntax .code   -> dropped (ELF/ARM only)

The rewrite has to reach files pulled in with `.include` too (map layouts,
macros, MPlayDef.s), or an `.align 2` there would silently become a 2-byte
alignment: so every `.include` is expanded in place, searched like GNU as
does (the including file's folder, the current folder, then -I folders).

usage: pc_asm.py [-I DIR]... [FILE]     (reads stdin without FILE)
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

INCLUDE = re.compile(r'^\s*\.include\s+"([^"]+)"\s*$')
ALIGN = re.compile(r'^(\s*(?:[A-Za-z_.$][\w.$]*:+\s*)?)\.align(\s)')
DROP = re.compile(r'^\s*\.(type|size|thumb|arm|thumb_func|syntax|code)(\s|$)')
PUSH = re.compile(r'^\s*\.pushsection\s+([^,\s]+)(.*)$')
POP = re.compile(r'^\s*\.popsection\s*$')
SECTION = re.compile(r'^\s*\.section\s+([^,\s]+)')
SIMPLE_SECTION = re.compile(r'^\s*\.(text|data|bss)\s*$')
WORD4 = re.compile(r'\.4byte\b')
WORD2 = re.compile(r'\.2byte\b')


class Converter:
    def __init__(self, search: list[Path]):
        self.search = search
        self.section = ".text"
        self.stack: list[str] = []
        self.out: list[str] = []
        self.depth = 0

    def find(self, name: str, base: Path | None) -> Path | None:
        candidates = ([base] if base else []) + [Path.cwd()] + self.search
        for folder in candidates:
            path = folder / name
            if path.is_file():
                return path
        return None

    def feed(self, text: str, base: Path | None) -> None:
        for line in text.splitlines():
            self.line(line, base)

    def line(self, line: str, base: Path | None) -> None:
        m = INCLUDE.match(line)
        if m:
            path = self.find(m.group(1), base)
            if path is None:
                # Leave it to the assembler, which reports it with its name.
                self.out.append(line)
                return
            if self.depth > 32:
                sys.exit(f"pc_asm: .include nested too deep at {path}")
            self.depth += 1
            self.feed(path.read_text(encoding="latin-1"), path.parent)
            self.depth -= 1
            return
        if DROP.match(line):
            return
        m = PUSH.match(line)
        if m:
            self.stack.append(self.section)
            self.section = m.group(1)
            self.out.append(f"\t.section {self.section}")
            return
        if POP.match(line):
            self.section = self.stack.pop() if self.stack else ".text"
            self.out.append(f"\t.section {self.section}")
            return
        m = SECTION.match(line) or SIMPLE_SECTION.match(line)
        if m:
            name = m.group(1)
            self.section = name if name.startswith(".") or SECTION.match(line) else "." + name
        line = WORD4.sub(".int", line)
        line = WORD2.sub(".short", line)
        line = ALIGN.sub(r"\1.p2align\2", line)
        self.out.append(line)


def main(argv: list[str]) -> int:
    search: list[Path] = []
    files: list[str] = []
    i = 0
    while i < len(argv):
        if argv[i] == "-I":
            search.append(Path(argv[i + 1]))
            i += 2
        elif argv[i].startswith("-I"):
            search.append(Path(argv[i][2:]))
            i += 1
        else:
            files.append(argv[i])
            i += 1
    conv = Converter(search)
    if files:
        for name in files:
            path = Path(name)
            conv.feed(path.read_text(encoding="latin-1"), path.parent)
    else:
        conv.feed(sys.stdin.buffer.read().decode("latin-1"), None)
    sys.stdout.buffer.write(("\n".join(conv.out) + "\n").encode("latin-1"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
