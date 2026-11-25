/*
 * Where the two screens go in the window (J4).
 *
 * Pure geometry, no SDL or GL: pc_display.c draws with it and maps the mouse
 * through it, the tests check it alone. Sizes are in window pixels.
 *
 *   vertical  the 3DS: top screen above, bottom screen centred under it
 *   side      side by side, top screen on the left
 *   inset     one screen big, the other small: the bottom screen in the top's
 *             corner, or (swapped) the top screen beside the big bottom one,
 *             whose dock and header must stay reachable
 *   single    one screen only
 *   fusion    the top screen fills the window; of the bottom one, only its
 *             dock, as a bar of shortcut icons over the world - until it
 *             has something to show (a menu, a section, the focus) or is
 *             opened, then all of it over the dimmed world
 *
 * "Swap" exchanges the screens' places (in inset and single: which one is
 * the big one), for the menus the port shows on the bottom screen.
 * The layout is scaled by the largest whole number the window holds (sharp
 * pixels), centred; a window smaller than 1x is fitted as it is.
 */
#ifndef PC_LAYOUT_H
#define PC_LAYOUT_H

#include <stdbool.h>

typedef enum
{
    PC_LAYOUT_VERTICAL,
    PC_LAYOUT_SIDE,
    PC_LAYOUT_INSET,
    PC_LAYOUT_SINGLE,
    PC_LAYOUT_FUSION,
    PC_LAYOUT_COUNT
} PcLayoutKind;

typedef struct
{
    float x, y, w, h;
} PcRect;

typedef struct
{
    PcLayoutKind kind;
    bool swap;
    PcRect top, bottom;
    bool topShown, bottomShown;
    /* FUSION: the bottom screen's dock band (PC_DOCK_Y.., PC_DOCK_H high)
     * shown alone in `strip`; and the world dimmed under an open bottom. */
    PcRect strip;
    bool stripShown, topDimmed;
    bool bottomFirst;     /* draw the bottom screen first (the top is over it) */
    int logicalW, logicalH; /* the layout at 1x */
    float scale;
} PcLayout;

void PcLayout_Compute(PcLayoutKind kind, bool swap, int windowW, int windowH, PcLayout *out);
/* integer false: the largest scale the window holds, whole or not (the
 * picture fills the window as far as its proportions allow). */
void PcLayout_ComputeFit(PcLayoutKind kind, bool swap, bool integer, int windowW, int windowH, PcLayout *out);
/* As above; open: FUSION's bottom screen shown whole (ignored by the others). */
void PcLayout_ComputeFull(PcLayoutKind kind, bool swap, bool integer, bool open, int windowW, int windowH,
                          PcLayout *out);

/* The bottom screen's dock (3ds_bottom_ui.c): its band of rows. */
#define PC_DOCK_Y 204
#define PC_DOCK_H 36

/*
 * A point of the window on the bottom screen, in its 320x240 coordinates.
 * clamp: points outside are brought to the nearest edge (a stylus dragged
 * off the screen keeps touching it); otherwise they are refused.
 */
bool PcLayout_ToBottom(const PcLayout *layout, float x, float y, bool clamp, int *touchX, int *touchY);

const char *PcLayout_Name(PcLayoutKind kind);
/* "vertical", "side", "inset", "single" or a number; -1 if unknown. */
int PcLayout_Parse(const char *text);

#endif
