/*
 * The window's menu bar (pc_port/src/gl/pc_menu.c): every row of the game's
 * OPTIONS screen, read from the bottom screen's own table
 * (3ds_port/src/3ds_bottom_options.inc, under PORT_PC in 3ds_bottom_ui.c), so
 * the two always agree. Categories, their groups, each group's rows; a
 * row's kind, label, value and choices. Texts are UTF-8. Plain C: both
 * sides include it.
 */
#ifndef PC_MENU_H
#define PC_MENU_H

#include <stdbool.h>
#include <stddef.h>

enum { PC_OPT_TOGGLE, PC_OPT_CHOICE, PC_OPT_STEP, PC_OPT_ACTION, PC_OPT_INFO };

typedef struct
{
    int kind;          /* PC_OPT_* */
    bool live;         /* can be changed now (some need the 3D world) */
    int count;         /* PC_OPT_CHOICE: its values */
    int current;       /* PC_OPT_TOGGLE: 1 on; PC_OPT_CHOICE: the value's index */
    char label[64];
    char value[64];    /* PC_OPT_STEP: the value shown; PC_OPT_ACTION: the action */
    char key[32];      /* a port extra's settings key (pc_kb_a...), else empty */
} PcOptRow;

/* Game side. A category or group the build has nothing in is named NULL. */
int CtrBottom_PcOptCats(void);
const char *CtrBottom_PcOptCatName(int cat);
int CtrBottom_PcOptGroups(int cat);
const char *CtrBottom_PcOptGroupName(int cat, int group);
int CtrBottom_PcOptRows(int cat, int group);
bool CtrBottom_PcOptRow(int cat, int group, int row, PcOptRow *out);
void CtrBottom_PcOptChoiceName(int cat, int group, int row, int value, char *out, size_t size);
/* A step on (dir > 0) or back; an action run (dir > 0). */
void CtrBottom_PcOptStep(int cat, int group, int row, int dir);
/* A choice set to one of its values (or a toggle to 0/1). */
void CtrBottom_PcOptSet(int cat, int group, int row, int value);

#endif
