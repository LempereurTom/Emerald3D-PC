#!/bin/sh
# The objcopy arguments that move a game object's variables into the group
# the soft reset copies (pc_port/src/pc_game_group.c): every .data and
# .data$X section renamed .data$zzzgm1X, and gDisplayedStringBattle's to
# .data$zzzgm0a, first in the group with the room behind it. (MinGW puts every
# variable in .data$X with -fdata-sections: the game has nothing in .bss.)
# The game's read-only data (the console's .gamedata: tables, texts; on the
# PC the scripts and songs too) is gathered the same way: every .rdata and
# .rdata$X (and the assembled data's .rodata and script_data) renamed
# .rdata$zzzgd1X, between pc_game_group.c's .rdata$zzzgd0
# and .rdata$zzzgd9, so a release can take it out of the executable
# (pc_port/tools/pc_release.py).
# Usage: pc_group_sections.sh OBJDUMP object.o > args
"$1" -h "$2" | awk '
$2 ~ /^\.data(\$|$)/ {
    name = $2
    kind = name; sub(/\$.*/, "", kind)
    suffix = ""
    if (index(name, "$") > 0) suffix = substr(name, index(name, "$") + 1)
    if (name == ".data$gDisplayedStringBattle") target = ".data$zzzgm0a"
    else target = kind "$zzzgm1" suffix
    printf "--rename-section %s=%s\n", name, target
}
$2 ~ /^(\.rdata|\.rodata|script_data)(\$|$)/ {
    name = $2
    suffix = ""
    if (index(name, "$") > 0) suffix = substr(name, index(name, "$") + 1)
    printf "--rename-section %s=.rdata$zzzgd1%s\n", name, suffix
}'
