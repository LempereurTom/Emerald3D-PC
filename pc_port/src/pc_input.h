#ifndef PC_INPUT_H
#define PC_INPUT_H

#include <3ds.h>
#include <SDL3/SDL.h>
#include <stdbool.h>

/* Keyboard and gamepads (pc_input.c); the window (pc_display.c) calls these. */
void PcInput_Init(void);
void PcInput_OnEvent(const SDL_Event *e);
/* A key press for the row waiting for its key: true when it took it. */
bool PcInput_OnKeyDown(SDL_Scancode code);
bool PcInput_Capturing(void);
u32 PcInput_Keyboard(void);
u32 PcInput_Gamepads(circlePosition *circle);

#endif
