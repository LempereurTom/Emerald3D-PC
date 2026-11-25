#!/usr/bin/env python3
"""Write a playable scenario (3ds_port/src/3ds_scenario.c) from a text script.

The console records scenarios by playing; for automated PC checks this
writes one from a script instead, frame by frame (60 frames a second):

    # comment
    wait 600                 # nothing pressed for 600 frames
    press START              # held 6 frames, then 12 frames released
    press A 10               # held 10 frames (then 12 released)
    hold RIGHT 90            # held 90 frames, nothing after
    mash A 1200 20           # A pressed once every 20 frames, for 1200 frames
    touch 160 120 6          # the bottom screen touched at (160, 120)
    repeat 3                 # the lines up to `end` three times
      press DOWN
    end

Buttons: A B X Y L R START SELECT UP DOWN LEFT RIGHT (combine with +).

Writes <home>/scenarios/<name>/{input.bin,meta.txt} and, with --activate,
<home>/scenario.txt so the next run plays it (it is removed by nothing:
delete it to go back to normal play). No save.sav: the game starts without a
save, i.e. a new game - unless --save copies one in.

usage: make_scenario.py SCRIPT --home DIR [--name NAME] [--activate] [--save FILE]
"""
from __future__ import annotations

import argparse
import shutil
import struct
import sys
import time
from pathlib import Path

INPUT_MAGIC = 0x4E493345  # "E3IN"
KEYS = {
    "A": 1 << 0, "B": 1 << 1, "SELECT": 1 << 2, "START": 1 << 3, "RIGHT": 1 << 4, "LEFT": 1 << 5,
    "UP": 1 << 6, "DOWN": 1 << 7, "R": 1 << 8, "L": 1 << 9, "X": 1 << 10, "Y": 1 << 11,
}


def buttons(spec: str) -> int:
    value = 0
    for name in spec.upper().split("+"):
        if name not in KEYS:
            sys.exit(f"make_scenario: unknown button {name!r}")
        value |= KEYS[name]
    return value


def expand(lines: list[str]) -> list[list[str]]:
    """Script lines as token lists, repeat blocks unrolled."""
    out: list[list[str]] = []
    i = 0
    while i < len(lines):
        tokens = lines[i].split("#", 1)[0].split()
        i += 1
        if not tokens:
            continue
        if tokens[0] == "repeat":
            depth, body = 1, []
            while i < len(lines) and depth:
                t = lines[i].split("#", 1)[0].split()
                i += 1
                if t and t[0] == "repeat":
                    depth += 1
                elif t and t[0] == "end":
                    depth -= 1
                    if depth == 0:
                        break
                body.append(lines[i - 1])
            out += expand(body) * int(tokens[1])
        else:
            out.append(tokens)
    return out


def samples(script: str) -> list[tuple[int, int, int, int, int, int]]:
    frames: list[tuple[int, int, int, int, int, int]] = []

    def add(n: int, b: int = 0, touch: tuple[int, int] | None = None) -> None:
        for _ in range(n):
            frames.append((b, 1 if touch else 0, 0, 0, touch[0] if touch else 0, touch[1] if touch else 0))

    for t in expand(script.splitlines()):
        op = t[0]
        if op == "wait":
            add(int(t[1]))
        elif op == "press":
            add(int(t[2]) if len(t) > 2 else 6, buttons(t[1]))
            add(12)
        elif op == "hold":
            add(int(t[2]), buttons(t[1]))
        elif op == "mash":
            total, every = int(t[2]), int(t[3]) if len(t) > 3 else 20
            b = buttons(t[1])
            for f in range(total):
                add(1, b if f % every < every // 2 else 0)
        elif op == "touch":
            add(int(t[3]) if len(t) > 3 else 6, 0, (int(t[1]), int(t[2])))
            add(12)
        else:
            sys.exit(f"make_scenario: unknown command {op!r}")
    return frames


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("script", type=Path)
    ap.add_argument("--home", type=Path, required=True, help="EMERALD_PC_HOME (sdmc:/3ds/emerald3ds)")
    ap.add_argument("--name", default=None)
    ap.add_argument("--activate", action="store_true")
    ap.add_argument("--save", type=Path)
    args = ap.parse_args()
    name = args.name or args.script.stem
    frames = samples(args.script.read_text(encoding="utf-8"))
    out = args.home / "scenarios" / name
    out.mkdir(parents=True, exist_ok=True)
    with (out / "input.bin").open("wb") as f:
        f.write(struct.pack("<4I", INPUT_MAGIC, 1, 12, 0))
        for s in frames:
            f.write(struct.pack("<HHhhHH", *s))
    (out / "meta.txt").write_text(f"start {int(time.time())}\nframes {len(frames)}\n")
    save = out / "save.sav"
    if args.save:
        shutil.copyfile(args.save, save)
    elif save.exists():
        save.unlink()
    if args.activate:
        (args.home / "scenario.txt").write_text(f"mode=play\nname={name}\nexit=1\n")
    print(f"make_scenario: {name}: {len(frames)} frames ({len(frames) / 60:.1f} s) -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
