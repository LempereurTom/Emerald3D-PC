/*
 * The game's variables grouped, as the console's linker script groups them
 * (emerald3ds.ld.in): 3ds_game_full.c copies [__ctr_game_data, _end) and
 * [__ctr_game_bss, _end) as AgbMain first starts, and puts the copy back for
 * a soft reset (L+R+START+SELECT), as the GBA clears its RAM.
 *
 * A PE link has no such script, but sorts the sections named name$suffix
 * by suffix inside `name`. pc.mk renames the game objects' variables'
 * sections (build/root/src except platform/, build/root/data: the console's
 * selection) to .data$zzzgm1...; these markers come before
 * and after them ("zzz" sorts after the names the rest of the program uses).
 *
 * MinGW puts every variable, zero-initialised ones included, in its own
 * .data$name section with -fdata-sections: the game objects have nothing in
 * .bss, so the game's range is all in .data and the .bss range is empty
 * (both symbols at one place: never a negative size).
 *
 * As on the console, gDisplayedStringBattle leads the game's variables with
 * 1 KiB of room behind it: Juan's defeat text is longer than its 320 bytes
 * (more so in French and German) and ran into live variables, crashing after
 * the battle. pc.mk renames its section to .data$zzzgm0a; the room is
 * .data$zzzgm0b.
 */
#include <stdint.h>

__attribute__((section(".data$zzzgm0b"), aligned(4))) static char sJuanRoom[1024] = {1};
/* The start, which 3ds_game_full.c refers to; it points at the room so that
 * --gc-sections keeps the room's section (nothing else refers to it). */
__attribute__((section(".data$zzzgm0"), aligned(32))) char *__ctr_game_data[2] = {0, sJuanRoom};
__attribute__((section(".data$zzzgm2"), aligned(4))) char __ctr_game_data_end[4] = {1, 2, 3, 4};

__asm__(".section .bss$zzzgm,\"bw\"\n"
        ".globl ___ctr_game_bss\n___ctr_game_bss:\n"
        ".globl ___ctr_game_bss_end\n___ctr_game_bss_end:\n"
        ".space 4\n"
        ".text\n");

/*
 * The game's read-only data (the console's .gamedata, 3ds_port/emerald3ds.ld.in:
 * the tables and texts of build/root/src and build/root/data; on the PC the
 * scripts and songs too), between these two markers: pc.mk renames the game
 * objects' .rdata sections .rdata$zzzgd1... A release takes the bytes between
 * them out of the executable (pc_port/tools/pc_release.py) and the game puts
 * them back from the player's data as it starts (pc_gamedata.c, which refers
 * to both, so --gc-sections keeps them).
 */
__attribute__((section(".rdata$zzzgd0"), aligned(32))) const char __ctr_gamedata[32] = "EMERALD-PC GAMEDATA START";
__attribute__((section(".rdata$zzzgd9"), aligned(32))) const char __ctr_gamedata_end[32] = "EMERALD-PC GAMEDATA END";
