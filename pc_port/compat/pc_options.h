/*
 * The PC port's options as the game's OPTIONS screen shows them (J8): the
 * DISPLAY and CONTROLS tabs and ABOUT (pc_extras.inc, pc_options.inc, included
 * by 3ds_extras.c and 3ds_bottom_options.inc under PORT_PC). Values live in
 * settings.txt like every option; these are the PC side's hooks. Plain C
 * types only: the game's units include this.
 */
#ifndef PC_OPTIONS_H
#define PC_OPTIONS_H

/* Settings keys (settings.txt), with their values:
 *   pc_window      0 window, 1 fullscreen (desktop), 2 fullscreen exclusive
 *   pc_res         exclusive fullscreen's mode, width * 10000 + height; 0: the desktop's
 *   pc_scale       window size, 0..5 = 1x..6x the layout
 *   pc_vsync       0/1
 *   pc_stretch     0 whole scales only (sharp), 1 fill the window
 *   pc_filter      0 sharp (nearest), 1 smooth (linear)
 *   pc_layout      0 vertical, 1 side, 2 inset, 3 single (F9)
 *   pc_swap        0/1 (Tab)
 *   pc_pad_layout  0 confirm on the bottom button (Xbox), 1 Nintendo positions
 *   pc_vol_master, pc_vol_music, pc_vol_sfx   0..10 (0 to 100 %)
 *   pc_kb_<button> an SDL scancode (a, b, x, y, l, r, start, select, up, down,
 *                  left, right, zl, zr)
 */

enum
{
    PC_BUTTON_A, PC_BUTTON_B, PC_BUTTON_X, PC_BUTTON_Y, PC_BUTTON_L, PC_BUTTON_R,
    PC_BUTTON_START, PC_BUTTON_SELECT, PC_BUTTON_UP, PC_BUTTON_DOWN, PC_BUTTON_LEFT,
    PC_BUTTON_RIGHT, PC_BUTTON_ZL, PC_BUTTON_ZR, PC_BUTTON_COUNT
};

/* The display settings read again and applied (an option changed). */
void PcOptions_Apply(void);
/* Exclusive fullscreen: the next or previous mode of the display. */
void PcOptions_StepResolution(int direction);
/* "1920X1080", or NULL for the desktop's mode. */
const char *PcOptions_ResolutionName(void);
/* The next key pressed becomes this button's (Escape: unchanged). */
void PcOptions_BindStart(int button);
/* The key's name ("X", "RETURN"), or "..." while one is awaited. */
const char *PcOptions_BindName(int button);
/* Every key back to its default. */
void PcOptions_ResetKeys(void);
/* The volumes read again and applied (pc_vol_master, pc_vol_music, pc_vol_sfx). */
void PcOptions_ApplyAudio(void);
/* The game side's (pc_sound_volume.inc): music and effects, 0..100. */
void PcSound_SetVolumes(int music, int effects);
/* The PC output's (pc_ndsp.c): 0..100. */
void PcNdsp_SetVolume(int percent);

#endif
