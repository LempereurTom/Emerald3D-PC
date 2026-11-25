# pc_port — Emerald PC backend

Native PC backend for [Emerald 3DS](https://github.com/ZallaxDev/emerald-3ds)
(Emerald 3DS by Zallax and contributors), next to `3ds_port/`, which it reuses
without changing it. License: CC BY-NC-SA 4.0, like the rest of the port.

Status, decisions and known bugs: [docs/PC_PORT_STATUS.md](../docs/PC_PORT_STATUS.md).
Inventory and design: [docs/PC_PORT_INVENTORY.md](../docs/PC_PORT_INVENTORY.md).

## How it fits

| Piece | Role |
|---|---|
| `pc.mk` | Overlay on `3ds_port/Makefile` + `full.mk`: overrides compiler, assembler and flags, swaps the native units, links `emerald-pc.exe`. Every per-file flag, wrap and generator of the 3DS build is reused. |
| `compat/3ds.h` | libctru-lite: the libctru calls the reused native units make (locks, events, threads, ticks, APT, HID, a few gfx), on POSIX threads. |
| `compat/pc_paths.h` | Maps `sdmc:/3ds/emerald3ds/` and `romfs:/` to real folders (macro layer, force-included where files are opened). |
| `src/gl/` | The citro shim: citro3d's API on OpenGL 3.3 (`pc_c3d*.c`), the PICA's shaders in GLSL (`pc_shaders.c`), the window, input and the console's VBlank clock (`pc_display.c`). |
| `third_party/` | Unmodified libctru/citro3d/citro2d headers (GPU, NDSP), citro3d's maths, citro2d's `base.c` (zlib). |
| `src/pc_main.c` | Entry point. |
| `src/pc_bundles.c` | Development build: scripts, songs and game data are linked in place. |
| `src/pc_ndsp.c` | NDSP (the DSP's sound channels) on an SDL3 audio stream: the console's `3ds_audio.c` and `3ds_sound.c` run on it unchanged. |
| `src/pc_stubs.c` | Local wireless, GPU memory report: stubs. |
| `tools/pc_asm.py` | ARM GNU as data → i686 PE (`.align`, sections, includes). |
| `tools/make_scenario.py` | A playable input scenario from a text script (automated checks). |
| `src/pc_input.c` | Keyboard keys (OPTIONS > CONTROLS, settings `pc_kb_*`) and gamepads (SDL3). |
| `compat/pc_options.h`, `compat/pc_extras*.inc`, `compat/pc_options.inc` | The DISPLAY, CONTROLS, SOUND and ABOUT options, included by `3ds_extras.c` and `3ds_bottom_options.inc` under `PORT_PC`. |
| `src/pc_layout.c` | Where the two screens go in the window (vertical, side by side, inset, single, FUSION: the top screen alone with the dock's bar, the bottom screen opened over it) and the mouse's point on the bottom screen. |
| `src/gl/pc_ambience.c`, `game/pc_game_ambience.c` | Ambient particles (leaves, petals, fireflies, cave dust; outdoors only) and the visual-only showers on routes (DYNAMIC WEATHER); the game side says what kind of place it is. |
| `src/gl/pc_hud.c`, `compat/pc_hud.h` | FUSION's single-screen interface: the place, clock and RUN top left, a minimap bottom left, PARTY/BAG/SAVE/OPTIONS bottom right, each the bottom screen's own pixels; a click or key (P, I, F5, O, Shift, M) is a tap there. |
| `src/gl/pc_menu.c`, `compat/pc_menu.h` | The window's menu bar (Windows): every row of OPTIONS, built from the bottom screen's own table. |
| `src/pc_setup.c`, `tools/pc_builder.py` | A release's first run: the ROM asked for, the game data rebuilt from it. |
| `compat/pc_sound_volume.inc` | Music and effects volumes (OPTIONS > GENERAL > SOUND), included by `3ds_sound.c` under `PORT_PC`. |
| `tests/layout_test.py` | Save structures: same layout as the console (ARM vs i686). |
| `tests/layout_geometry_test.c` | `pc_layout.c` alone: every layout, swapped or not, many window sizes. |
| `tests/scenarios/` | Scenario scripts: `new_game.txt` (intro → Littleroot), `touch_dock.txt` (the dock by touch), `save_in_house.txt` (a save in Mom's house). |
| `run-dev.ps1` | Runs the development build in a window. |

## Building a language

One executable a language, as on the console (docs/PC_PORT_STATUS.md, D3), in
a tree of its own; the English tree, build/upstream-pc, is the development one.

```
# UCRT64 shell, repository root: the game's texts and graphics from the
# player's clean ROM (French here; --spanish-rom, --italian-rom, --german-rom,
# --japanese-rom alike), no 3DS toolchain needed
git clone -q --no-checkout build/upstream-pc build/upstream-pc-fr   # or let bootstrap fetch
python tools/bootstrap.py --dir build/upstream-pc-fr --french-rom "Pokemon Emeraude.gba"
# Brazilian Portuguese goes over the English game, from the repository's texts:
python tools/bootstrap.py --dir build/upstream-pc-pt_br --port-lang pt_br
make -C build/upstream-pc-pt_br tools generated -j16

# MINGW32 shell
make -C build/upstream-pc-fr/3ds_port -f $PWD/pc_port/pc.mk pc -j16
make -C build/upstream-pc-pt_br/3ds_port -f $PWD/pc_port/pc.mk pc PORT_LANG=pt_br -j16

# play (PowerShell): powershell -ExecutionPolicy Bypass -File pc_port
un-dev.ps1 -Lang fr
```

A language's executable and romfs hold that language's texts, taken from
the ROM: like every development build, never distributed.
`python pc_port/tools/rom_info.py <rom>` gives a ROM's size, CRC32 and SHA-1
and whether it is one of the six supported clean dumps.

## The user folder

`<home>` holds the save (`emerald3ds.sav`, the console's own format: a 3DS
save works as it is, and back), its companions (`.clock`, `.nuzlocke`),
`settings.txt` (the PC's keys `pc_layout`, `pc_swap` included; the console
keeps keys it does not know), and:

- `backup/<YYYYMMDD-HHMMSS>/`: the save and its companions as they were
  before the game first wrote them in a session (`src/pc_save.c`, the
  builder's layout); never deleted;
- `crash.txt`: written if the game crashes (`src/pc_crash.c`): the fault and
  the code addresses on the stack, for
  `addr2line -f -i -e emerald-pc.exe <addresses>`;
- `port.log` when `debug.txt` is there, as on the console.

## Building (Windows, MSYS2)

Tools (portable MSYS2 in `A:\msys64`): `mingw-w64-i686-gcc`, `mingw-w64-i686-binutils`
(MINGW32, the game), `mingw-w64-ucrt-x86_64-gcc`, `-libpng`, `-python`,
`-python-pillow`, `-arm-none-eabi-gcc` (UCRT64: decomp tools, generators,
layout test), `make`, `git`.

```
# UCRT64 shell, repository root
python tools/bootstrap.py --dir build/upstream-pc
make -C build/upstream-pc tools generated -j16

# MINGW32 shell, repository root
# (`pc` also rebuilds pc-data: the asset index holds addresses into the exe)
make -C build/upstream-pc/3ds_port -f $PWD/pc_port/pc.mk pc -j16

# play, from PowerShell: powershell -ExecutionPolicy Bypass -File pc_port\run-dev.ps1
# FUSION: P party, I bag, F5 save, O options, Shift run, M map (or click them); Escape closes the bottom screen's menu
# F9: next layout (Shift+F9: previous), Tab: swap the screens (FUSION: open/close the bottom screen), F11 or Alt+Enter: fullscreen

# layout geometry check (UCRT64 shell)
gcc -I pc_port/src pc_port/tests/layout_geometry_test.c pc_port/src/pc_layout.c -lm -o lgt && ./lgt

# save layout check (MINGW32 shell, UCRT64 on PATH after it)
PATH=$PATH:/ucrt64/bin python pc_port/tests/layout_test.py --tree build/upstream-pc \
    --arm-cc /ucrt64/bin/arm-none-eabi-gcc.exe --pc-cc i686-w64-mingw32-gcc
```

`emerald-pc.exe` from this build **contains game content** (development
build: data linked in place, built from the decompilation's sources). It is
never distributed.

## Releases (no game content)

```
# UCRT64 python, MINGW32 binutils (a built language tree and its clean ROM)
PC_NM=/mingw32/bin/nm.exe python pc_port/tools/pc_build_release.py --version 0.1.0     --tree build/upstream-pc-fr --rom <ROM> --language fr --pyinstaller --nm /mingw32/bin/nm.exe
```

`tools/pc_release.py` takes the game's read-only data out of the executable
(zeroed; the game reads it back from the player's data), `tools/gen_recipe.py`
(with `tools/pc_nm.py`) and `tools/voxel_precompile.py` write the recipe,
`tools/pc_builder.py` (frozen: `emerald-pc-builder.exe`) rebuilds the data
from the player's ROM into `<home>/devdata` on the first run
(`src/pc_setup.c`), and `tools/release_audit.py --rom` checks the ZIP
(`release_audit_allow.toml` adds the PC's files to the repository's list).


| Variable | Default | Meaning |
|---|---|---|
| `EMERALD_PC_ROMFS` | `romfs/` next to the exe | the files that ship with the executable (`romfs:/`) |
| `EMERALD_PC_HOME` | `%APPDATA%\EmeraldPC`, `$XDG_DATA_HOME/emerald-pc` | saves, settings, logs, data pack (`sdmc:/3ds/emerald3ds/`) |
| `EMERALD_PC_FRAMES` | — | stop after N frames (headless checks) |
| `EMERALD_PC_HIDDEN` | — | `1`: no visible window |
| `EMERALD_PC_SCALE` | 2 | initial window scale |
| `EMERALD_PC_AMBIENCE` | (the place's) | `leaves`, `petals`, `dust`, `night` or `rain`: forces an ambience (checks) |
| `EMERALD_PC_LAYOUT` | (settings) | `vertical`, `side`, `inset` or `single`, over `settings.txt`'s `pc_layout` |
| `EMERALD_PC_SWAP` | (settings) | `1`: the screens swapped, over `pc_swap` |
| `EMERALD_PC_DUMP_FROM` | — | no frame dumped before this one |
| `EMERALD_PC_DUMP_LAYOUTS` | — | `1`: each dump in every layout (`frame_N_side_swap.png`…) |
| `EMERALD_PC_DUMP` | — | save frames as PNG into this folder |
| `EMERALD_PC_DUMP_EVERY` | 60 | … every N frames |
| `EMERALD_PC_DUMP_SURFACES` | — | `1`: also every GPU surface (debugging the shim) |
| `EMERALD_PC_NOPACE` | — | `1`: no VBlank wait, as fast as the PC goes |
| `EMERALD_PC_MUTE` | — | `1`: no audio device (the sound engine still runs) |
| `EMERALD_PC_SCENARIO_SAVES` | — | `1`: a scenario playback may save, into the home folder's `emerald3ds.sav` (to prepare a save) |
| `EMERALD_PC_CRASH_TEST` | — | `1`: a deliberate crash at start (checks `crash.txt`) |
| `EMERALD_PC_VOLUME` | 100 | output volume, in percent |
| `EMERALD_PC_AUDIO_FILTER` | 14000 | treble filter cutoff in Hz (gentle low-pass against the mixer's grit); `0`: off |
| `EMERALD_PC_AUDIO_DUMP` | — | also write what is played to this WAV file |
| `EMERALD_PC_AUDIO_DUMP_OUT` | — | what the device receives, after SDL (float WAV) |
| `SDL_AUDIO_DRIVER` | — | SDL's own; `dummy` plays nothing (automated runs) |

Automated check (MINGW32 shell, repository root):

```
H=build/upstream-pc/3ds_port/build/pc-home
python pc_port/tools/make_scenario.py pc_port/tests/scenarios/new_game.txt --home $H --activate
cd build/upstream-pc/3ds_port
SDL_AUDIO_DRIVER=dummy EMERALD_PC_AUDIO_DUMP=build/check.wav \
  EMERALD_PC_HIDDEN=1 EMERALD_PC_DUMP=build/dump EMERALD_PC_DUMP_EVERY=120 EMERALD_PC_FRAMES=7400 \
  EMERALD_PC_ROMFS=build/pc-romfs EMERALD_PC_HOME=build/pc-home ./emerald-pc.exe
```

`port.log` ends with `NDSP: played … s; ran dry N times`. `EMERALD_PC_NOPACE=1` runs
faster than real time (the sound then falls behind the picture, by design).
