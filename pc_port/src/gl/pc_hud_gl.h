/*
 * FUSION's single-screen interface, window side (pc_hud.c): laid out over
 * the world from what the bottom screen's UI tells (pc_hud.h), drawn by
 * pc_display.c (the pieces of the bottom screen) and by PcOverlay_Draw (the
 * frames, letters and key badges).
 */
#ifndef PC_HUD_GL_H
#define PC_HUD_GL_H

#include "../pc_layout.h"
#include "pc_hud.h"

enum { PC_HUD_FROM_BOTTOM, PC_HUD_FROM_CANVAS };

typedef struct
{
    PcRect dst;   /* window pixels */
    PcHudBox src; /* pixels of the bottom screen or of the interface's canvas */
    int from;     /* PC_HUD_FROM_* */
    float round;  /* its corners rounded, window pixels (0: square) */
} PcHudSprite;

enum { PC_HUD_HIT_NONE, PC_HUD_HIT_TAP, PC_HUD_HIT_MINIMAP };

/* Once a frame: whether the interface may be up (FUSION, the bottom screen
 * away) and whether the last input came from a gamepad (no key badges). */
void PcHud_Update(bool active, bool pad);
bool PcHud_Visible(void);
/* The interface's canvas redrawn: a new number (upload it again). */
unsigned PcHud_CanvasVersion(void);
/* Laid out over `top` (the world's rectangle). */
void PcHud_Build(const PcRect *top);
int PcHud_SpriteCount(void);
const PcHudSprite *PcHud_Sprite(int i);
/* Layer 0 under the pieces (shadows, frames), 1 over them (letters, badges). */
void PcHud_DrawShapes(int layer, int fbW, int fbH);
/* A click at a window pixel: a tap on the bottom screen at (tapX, tapY), the
 * minimap, or nothing. */
int PcHud_Hit(float x, float y, int *tapX, int *tapY);
/* A key (SDL scancode): P, I, F5, O and Shift tap their button. */
bool PcHud_Key(int scancode, int *tapX, int *tapY);

void PcOverlay_Draw(const float *rects, int count, int fbW, int fbH);

#endif
