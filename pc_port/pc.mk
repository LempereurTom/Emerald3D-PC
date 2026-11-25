# Emerald PC: the game tree built for a PC (i686), on top of 3ds_port's own
# Makefile and full.mk, which stay as they are.
#
# Run from a bootstrapped tree's 3ds_port directory (its own build/ folder):
#   python tools/bootstrap.py --dir build/upstream-pc
#   make -C build/upstream-pc tools generated
#   make -C build/upstream-pc/3ds_port -f $PWD/pc_port/pc.mk pc -j16
# in an MSYS2 MINGW32 shell (Windows) or with an i686 gcc (Linux, later).
#
# Every tool and flag of the 3DS build is a variable of its Makefile; the
# `override` assignments below win over its own, so its per-file flags,
# source lists, wraps and generators are all reused. What differs:
#   - compiler and assembler: i686 GCC and GNU as;
#   - libctru: pc_port/compat/3ds.h (libctru-lite) answers <3ds.h>;
#   - paths: pc_port/compat/pc_paths.h is force-included everywhere;
#   - the native units that drive 3DS hardware are replaced (pc_port/src);
#   - development build: scripts, songs and the game's read-only data are
#     linked into the executable instead of being moved to bundles (a PE
#     executable takes their unaligned pointers). Such an executable carries
#     game content and is never distributed; the release path comes later.

PC_PORT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

# ── Tools ────────────────────────────────────────────────────────────────
PC_CC ?= i686-w64-mingw32-gcc
PC_AS ?= /mingw32/bin/as.exe
PC_CPP ?= /mingw32/bin/cpp.exe
PC_PYTHON ?= /ucrt64/bin/python.exe

# The decomp's host tools (preproc, scaninc, mid2agb...) are built with the
# UCRT64 compiler: their DLLs come after the i686 toolchain on PATH.
export PATH := $(PATH):/ucrt64/bin

override CC := $(PC_CC)
override AS := $(PC_AS)
override CPPTOOL := $(PC_CPP)
override PYTHON := $(PC_PYTHON)
override HOSTCC := gcc

# i686 with SSE2 maths. -mno-ms-bitfields: MinGW lays bitfields out the MSVC
# way by default, which would make the save structures differ from the
# console's (docs/PC_PORT_INVENTORY.md §1.5).
# -D_TIME_BITS=64: msvcrt's 32-bit time_t is MinGW i686's default; the
# console's is 64-bit, and a 32-bit one stops the game's clock in January
# 2038 (time() wraps). Everything is built with it, so layouts agree.
# -malign-data=abi: data aligned as the ABI asks, not x86's 32 bytes for any
# array of 32 bytes or more; each object's size in the link map is then its
# own, which the release's recipe matches against the GBA game's tables
# (pc_port/tools/pc_nm.py). Layouts inside structures do not change.
override ARCH := -march=i686 -msse2 -mfpmath=sse -mno-ms-bitfields -D_TIME_BITS=64 -malign-data=abi
override CTRULIB := $(PC_PORT)/ctru-unused
# SDL3 (window, input, audio): the official MinGW development package.
PC_SDL ?= /opt/SDL3-3.4.20/i686-w64-mingw32
# libctru-lite first, then the unmodified libctru, citro3d and citro2d headers
# (pc_port/third_party), then SDL.
PC_TP := $(PC_PORT)/third_party
PC_INCLUDES := -I$(PC_PORT)/compat -I$(PC_TP)/libctru/include -I$(PC_TP)/citro3d/include \
	-I$(PC_TP)/citro2d/include -I$(PC_SDL)/include
# The path redirections (pc_paths.h) are force-included only where files are
# opened: the port's own units, the voxel units, the natives and pc_port.
# The decompilation's units are compiled in two stages (cpp | preproc | cc):
# a forced include would be declared twice there, and they open no files.
PC_PATHS_INCLUDE := -include pc_paths.h
override CFLAGS := $(ARCH) -O2 -g -std=gnu99 -Wall -Wextra -ffunction-sections -fdata-sections \
	-DPORT_PC $(PC_INCLUDES)
override ASFLAGS = --32 --divide --defsym MODERN=1 --defsym PORTABLE=1 --defsym UBFIX=1 --defsym PORT_PC=1 -I$(ROOT)

# The data sources are written for ARM GNU as. On i386 PE `.align N` means N
# bytes, not 2^N; .pushsection and ELF/ARM-only directives are refused. The
# rewrite (pc_port/tools/pc_asm.py) expands `.include`s so it reaches them too.
# Search folders: the repository root (data rules run there) and sound/
# (MPlayDef.s, for the song rule that runs in 3ds_port).
override ASM_PSEUDO_OP_CONV = "$(PC_PYTHON)" "$(PC_PORT)/tools/pc_asm.py" -I "$(abspath $(ROOT))" -I "$(abspath $(ROOT))/sound"

include Makefile

# ── What the PC build links ──────────────────────────────────────────────
# Native units of 3ds_port/src that compile as they are against libctru-lite
# or need nothing from the SDK.
PC_NATIVE_REUSED := src/3ds_log.c src/3ds_settings.c src/3ds_data.c src/3ds_prof.c src/3ds_scenario.c \
	src/3ds_fs.c src/3ds_pak.c src/3ds_icons_ptbr.c src/3ds_input_decode.c src/3ds_video_decode.c \
	src/3ds_corner_art.c src/3ds_daynight.c src/3ds_clock_store.c src/3ds_nuzlocke_core.c \
	src/3ds_nuzlocke_store.c src/3ds_encounters.c src/3ds_field_view.c \
	src/3ds_platform.c src/3ds_input.c src/3ds_audio.c \
	src/3ds_video.c src/3ds_bottom_screen.c src/3ds_dex_top.c
# + the build's identity (version, commit, defines), as on the console. The
# link id is pc_misc.c's: the console's build/linkid.c depends on every 3DS
# native object.
PC_NATIVE_OBJS := $(patsubst src/%.c,build/%.o,$(PC_NATIVE_REUSED)) build/buildinfo.o

# The PC's own units (platform, stubs, libctru-lite, paths; gl/: the citro
# shim over OpenGL and the window).
PC_SRCS := $(wildcard $(PC_PORT)/src/*.c $(PC_PORT)/src/gl/*.c)
PC_OBJS := $(patsubst $(PC_PORT)/src/%.c,build/pc/%.o,$(PC_SRCS))

# Third-party code compiled as it is: citro3d's maths, citro2d.
PC_TP_SRCS := $(wildcard $(PC_TP)/citro3d/source/maths/*.c) $(PC_TP)/citro2d/source/base.c
PC_TP_OBJS := $(patsubst $(PC_TP)/%.c,build/pc/tp/%.o,$(PC_TP_SRCS))
PC_OBJS += $(PC_TP_OBJS)

build/pc/tp/%.o: $(PC_TP)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(PC_PORT)/src/gl -w -c $< -o $@

# Game side: everything full.mk builds, except the bundle loader and the
# reserved regions (replaced by linking the data itself) and the hardware
# cycle test. The voxel renderer (ctr_voxel.c) runs on the citro shim (J5).
PC_BACKEND_EXCLUDED := build/bridge/3ds_script_loader.o build/bridge/3ds_cycle_test.o
PC_DATA_EXCLUDED := build/root/3ds_script_blob.o build/root/3ds_script_keep.o build/root/3ds_song_blob.o
PC_GAME_OBJS := $(FULL_C_OBJS) $(filter-out $(PC_DATA_EXCLUDED),$(FULL_DATA_OBJS)) \
	$(filter-out $(PC_BACKEND_EXCLUDED),$(BACKEND_OBJS)) $(SCRIPT_DATA_OBJS) $(ROOT_SONG_OBJS)

# 32-bit Windows names C symbols with a leading underscore (gMain is _gMain);
# the assembled data names them bare. Each assembled object is copied with
# its global symbols, defined and undefined, given the underscore. Linux ELF
# has no prefix: there this pass is the identity.
PC_ASM_OBJS := $(filter build/root/data/% build/root/sound/%,$(PC_GAME_OBJS))
ifneq ($(findstring mingw,$(PC_CC)),)
PC_LINK_GAME_OBJS := $(filter-out $(PC_ASM_OBJS),$(PC_GAME_OBJS)) $(patsubst build/%,build/pcus/%,$(PC_ASM_OBJS))
else
PC_LINK_GAME_OBJS := $(PC_GAME_OBJS)
endif
PC_OBJCOPY ?= /mingw32/bin/objcopy.exe
PC_NM ?= /mingw32/bin/nm.exe

build/pcus/%.o: build/%.o
	@mkdir -p $(dir $@)
	@"$(PC_NM)" -g $< | awk '$$NF !~ /^[._]/ { print $$NF " _" $$NF }' | sort -u > $@.syms
	"$(PC_OBJCOPY)" --redefine-syms=$@.syms $< $@

build/bridge/%.o build/voxel/%.o: FULLCFLAGS += $(PC_PATHS_INCLUDE)
build/ctr_voxel.o: CFLAGS += $(PC_PATHS_INCLUDE)
$(PC_NATIVE_OBJS) $(PC_OBJS): CFLAGS += $(PC_PATHS_INCLUDE)

# The game's variables grouped for the soft reset (pc_game_group.c): the
# console's selection (emerald3ds.ld.in), build/root/src except platform/ and
# build/root/data, copied with their .data sections renamed .data$zzzgm1...
# The songs join them for their read-only data (game data a release takes
# out); they have no variables.
PC_GROUPED := $(filter build/root/src/% build/pcus/root/data/% build/pcus/root/sound/%,$(filter-out build/root/src/platform/%,$(PC_LINK_GAME_OBJS)))
PC_LINK_GAME_OBJS := $(filter-out $(PC_GROUPED),$(PC_LINK_GAME_OBJS)) $(patsubst build/%,build/pcgame/%,$(PC_GROUPED))
PC_OBJDUMP ?= /mingw32/bin/objdump.exe

build/pcgame/%.o: build/%.o $(PC_PORT)/tools/pc_group_sections.sh
	@mkdir -p $(dir $@)
	@sh "$(PC_PORT)/tools/pc_group_sections.sh" "$(PC_OBJDUMP)" $< > $@.args
	"$(PC_OBJCOPY)" @$@.args $< $@

# PC code that reads the game's state (pc_port/game: the ambient particles'
# place), built as the bridge's units are, with the game's headers.
PC_GAMESIDE_OBJS := $(patsubst $(PC_PORT)/game/%.c,build/pcgameside/%.o,$(wildcard $(PC_PORT)/game/*.c))
build/pcgameside/%.o: $(PC_PORT)/game/%.c
	@mkdir -p $(dir $@)
	"$(CC)" $(FULLFLAGS) $(FULLCFLAGS) $(PC_PATHS_INCLUDE) -MMD -MP -c $< -o $@

PC_ALL_OBJS := $(PC_LINK_GAME_OBJS) $(PC_NATIVE_OBJS) $(PC_OBJS) $(PC_GAMESIDE_OBJS)

build/pc/%.o: $(PC_PORT)/src/%.c $(wildcard $(PC_PORT)/compat/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iinclude -Isrc -iquote compat -MMD -MP -c $< -o $@

-include $(PC_OBJS:.o=.d)

# No ASLR: the asset index (romfs/assets/asset_index.bin) names each INCBIN
# stub by its absolute address in the image, so the image must load where it
# was linked. The .reloc section is kept for the release tooling (J7/J9).
PC_LDFLAGS := $(ARCH) -Wl,--gc-sections -Wl,--disable-dynamicbase -Wl,--enable-reloc-section \
	-Wl,-Map,build/emerald-pc.map -Wl,--stack,8388608
# The game-side wraps full.mk links with (profiling, sound lock, map events).
PC_LDFLAGS += $(foreach f,$(PROF_WRAPPED) $(SOUND_WRAPPED) $(MAP_WRAPPED),-Wl,--wrap=$(f))
# The console's GX wraps (3ds_port/Makefile): the port watches the queue
# citro3d binds (upload admission: the voxel atlas, chunk uploads) and flushes
# before each GX operation. Not the GPU memory accounting (3ds_gpumem.c is not
# on the PC).
PC_LDFLAGS += -Wl,--wrap=GX_BindQueue -Wl,--wrap=GX_ProcessCommandList -Wl,--wrap=GX_TextureCopy 	-Wl,--wrap=GX_DisplayTransfer -Wl,--wrap=GX_RequestDma -Wl,--wrap=GSPGPU_FlushDataCache
# A backup of the save before its first overwrite in a session (pc_save.c).
PC_LDFLAGS += -Wl,--wrap=Port_WriteFlash
# The MUSIC and EFFECTS volumes (pc_sound_volume.inc, in 3ds_sound.c).
PC_LDFLAGS += -Wl,--wrap=TrkVolPitSet
# PC tools: a scenario playback may write its own save.sav (pc_misc.c).
PC_LDFLAGS += -Wl,--wrap=CtrScenario_SaveWrites
PC_LIBS := -L$(PC_SDL)/lib -lSDL3 -lm -lwinpthread

.PHONY: pc pc-objects pc-game-objects
pc-game-objects: $(PC_GAME_OBJS)
pc-objects: $(PC_ALL_OBJS)

emerald-pc.exe: $(PC_ALL_OBJS)
	$(file >build/pc-link.rsp,$(PC_ALL_OBJS))
	$(CC) $(PC_LDFLAGS) @build/pc-link.rsp $(PC_LIBS) -o $@

# The executable's DLLs beside it: SDL3 and MinGW's runtime.
PC_DLLS := $(PC_SDL)/bin/SDL3.dll /mingw32/bin/libwinpthread-1.dll /mingw32/bin/libgcc_s_dw2-1.dll
pc-dlls: $(PC_DLLS)
	cp -u $(PC_DLLS) .

# The data goes with the executable: the asset index holds absolute addresses
# into it (no ASLR), so an index from an older link reads the wrong bytes
# (black screens). `pc` therefore always rebuilds pc-data too.
pc: emerald-pc.exe pc-dlls pc-data

# ── Development data (romfs/, read through the "romfs" backend) ─────────
# The same generators as the console's build; the asset index reads the
# link map, whose PE section names (.rdata$sym) are given the ELF spelling
# (.rodata.sym) its parser knows, in a copy. The Citro3D shader (picasso) is
# not made here: the PC's shaders come with the citro shim.
# Everything goes to its own folder, build/pc-romfs (EMERALD_PC_ROMFS), so
# none of the console's romfs rules (tied to its ELF) is involved.
PC_ROMFS := build/pc-romfs
PC_ROMFS_ART := $(filter-out romfs/shaders/voxel.shbin,$(ROMFS_SHADER_OUTS))

# The objects' paths as the console's map has them: the game's go through
# copies (build/pcgame/: the soft reset's grouping), which the generator does
# not recognise; it then finds no asset at all.
build/emerald-pc.rodata.map: emerald-pc.exe
	sed -E -e 's/^ \.rdata\$$zzzgd1( |$$)/ .rdata\1/' -e 's/^ \.rdata\$$zzzgd1/ .rdata$$/' \
		-e 's/^ \.rdata\$$/ .rodata./' -e 's#build/pcgame/#build/#g' build/emerald-pc.map > $@

$(PC_ROMFS)/assets/asset_index.bin: build/emerald-pc.rodata.map $(SCRIPTS)/gen_asset_table.py
	@mkdir -p $(PC_ROMFS)/assets
	"$(PYTHON)" $(SCRIPTS)/gen_asset_table.py --port-dir . --map $< --fs-dir $(PC_ROMFS) --uri-prefix ""
	"$(PYTHON)" scripts/verify_assets.py --fs-dir $(PC_ROMFS)
	@test -s $@ || { echo 'pc-data: empty asset index (no asset found in the link map)'; rm -f $@; exit 1; }

.PHONY: pc-data
# The voxel shaders: on the PC a marker naming the GLSL programs
# (pc_port/src/gl/pc_shaders.c), the world's first and the weather's second,
# as the console's voxel.shbin has them (DVLE 0 and 1).
$(PC_ROMFS)/shaders/voxel.shbin:
	@mkdir -p $(dir $@)
	printf 'PCSH:voxel,weather' > $@

pc-data: $(PC_ROMFS)/shaders/voxel.shbin $(PC_ROMFS)/assets/asset_index.bin $(ROMFS_MAP_OUT) $(PC_ROMFS_ART) romfs/boot.txt
	@for f in $(ROMFS_MAP_OUT) $(PC_ROMFS_ART) romfs/boot.txt; do \
		mkdir -p $(PC_ROMFS)/$$(dirname $${f#romfs/}) && cp -u $$f $(PC_ROMFS)/$${f#romfs/}; done
	printf 'embedded\n' > $(PC_ROMFS)/data.embedded
