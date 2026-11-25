/*
 * FUSION's single-screen interface (the PC port): what the bottom screen's
 * UI tells (3ds_port/src/3ds_bottom_ui.c, under PORT_PC) and what the window
 * draws over the world (pc_port/src/gl/pc_hud.c). Boxes are in the bottom
 * screen's 320x240 pixels: the window shows those pixels of it, and a click
 * on one is a tap at its centre. Plain C: both sides include it.
 */
#ifndef PC_HUD_H
#define PC_HUD_H

#include <stdbool.h>

typedef struct
{
    short x, y, w, h;
} PcHudBox;

/* The action buttons, right to left from the corner's other end. */
enum { PC_HUD_PARTY, PC_HUD_BAG, PC_HUD_SAVE, PC_HUD_OPTIONS, PC_HUD_BUTTONS };

typedef struct
{
    bool shown;        /* the player is free to walk: the interface is up */
    bool map;          /* the bottom screen shows the map: the minimap can */
    /* Boxes of the interface's own canvas (CtrBottom_PcHudCanvas): the
     * header (the place, the Repel's steps, the clock), RUN, the buttons;
     * their taps, boxes of the bottom screen. */
    unsigned version;  /* the canvas redrawn: a new number */
    PcHudBox header;
    PcHudBox run, runTap;
    PcHudBox mapArea;  /* the map's picture */
    short playerX, playerY; /* the map and the player's mark: the bottom screen's */
    PcHudBox button[PC_HUD_BUTTONS], buttonTap[PC_HUD_BUTTONS];
    bool buttonOn[PC_HUD_BUTTONS];
    const char *label[PC_HUD_BUTTONS]; /* upper case, UTF-8 */
    const char *runKey;                /* the Shift key's name */
} PcHudInfo;

/* Game side. */
void CtrBottom_PcHud(PcHudInfo *out);
/* The interface's canvas, the screen's layout (RGB565, a column of 240 a
 * pixel across, bottom up): its plates drawn on black (0) or on white (1). */
const unsigned short *CtrBottom_PcHudCanvas(int onWhite);
/* The interface's bottom screen put away: OPTIONS, the card... back to the
 * map, so the minimap has it. */
void CtrBottom_PcGoMap(void);

#endif
