/*
 * Keyboard and gamepads to the 3DS's buttons (J8).
 *
 * The keyboard's keys are settings (pc_kb_<button>, an SDL scancode), set
 * from OPTIONS > CONTROLS: a row asked to change waits for the next key
 * (Escape leaves it as it was). Gamepads (Xbox, PlayStation, Switch Pro, any
 * SDL knows) are opened as they are plugged in and all read together:
 * the face buttons by position, the shoulders L and R, the triggers ZL and
 * ZR (the game speed, as on a New 3DS), Start and Back/Select, the D-pad,
 * and the left stick as the circle pad. pc_pad_layout picks which face
 * button is A: the bottom one (0, an Xbox pad's A, confirm where PC games
 * confirm) or the right one (1, the Nintendo position).
 */
#include <3ds.h>
#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

#include "3ds_log.h"
#include "3ds_platform.h"
#include "pc_input.h"
#include "pc_options.h"

static const struct
{
    const char *key;
    SDL_Scancode fallback;
    u32 bits;
} sButtons[PC_BUTTON_COUNT] = {
    [PC_BUTTON_A] = { "pc_kb_a", SDL_SCANCODE_X, KEY_A },
    [PC_BUTTON_B] = { "pc_kb_b", SDL_SCANCODE_Z, KEY_B },
    [PC_BUTTON_X] = { "pc_kb_x", SDL_SCANCODE_S, KEY_X },
    [PC_BUTTON_Y] = { "pc_kb_y", SDL_SCANCODE_A, KEY_Y },
    [PC_BUTTON_L] = { "pc_kb_l", SDL_SCANCODE_Q, KEY_L },
    [PC_BUTTON_R] = { "pc_kb_r", SDL_SCANCODE_W, KEY_R },
    [PC_BUTTON_START] = { "pc_kb_start", SDL_SCANCODE_RETURN, KEY_START },
    [PC_BUTTON_SELECT] = { "pc_kb_select", SDL_SCANCODE_BACKSPACE, KEY_SELECT },
    [PC_BUTTON_UP] = { "pc_kb_up", SDL_SCANCODE_UP, KEY_DUP },
    [PC_BUTTON_DOWN] = { "pc_kb_down", SDL_SCANCODE_DOWN, KEY_DDOWN },
    [PC_BUTTON_LEFT] = { "pc_kb_left", SDL_SCANCODE_LEFT, KEY_DLEFT },
    [PC_BUTTON_RIGHT] = { "pc_kb_right", SDL_SCANCODE_RIGHT, KEY_DRIGHT },
    [PC_BUTTON_ZL] = { "pc_kb_zl", SDL_SCANCODE_1, KEY_ZL },
    [PC_BUTTON_ZR] = { "pc_kb_zr", SDL_SCANCODE_2, KEY_ZR },
};

static int sCapture = -1; /* the button waiting for its key */
/* A wait left alone ends by itself: the next key, pressed to play once the
 * row is forgotten, must not be taken. */
#define CAPTURE_MS 5000
static Uint64 sCaptureStart;

static SDL_Scancode Binding(int button)
{
    int code = CtrSettings_GetInt(sButtons[button].key, (int)sButtons[button].fallback);
    return code > SDL_SCANCODE_UNKNOWN && code < SDL_SCANCODE_COUNT ? (SDL_Scancode)code : sButtons[button].fallback;
}

void PcOptions_BindStart(int button)
{
    if (button >= 0 && button < PC_BUTTON_COUNT)
    {
        sCapture = button;
        sCaptureStart = SDL_GetTicks();
    }
}

const char *PcOptions_BindName(int button)
{
    static char name[16];
    const char *sdl;

    if (button < 0 || button >= PC_BUTTON_COUNT)
        return "";
    if (button == sCapture)
        return "...";
    sdl = SDL_GetScancodeName(Binding(button));
    snprintf(name, sizeof(name), "%s", sdl && *sdl ? sdl : "?");
    for (char *c = name; *c; ++c)
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 'a' + 'A');
    return name;
}

void PcOptions_ResetKeys(void)
{
    sCapture = -1;
    for (int i = 0; i < PC_BUTTON_COUNT; ++i)
        CtrSettings_SetInt(sButtons[i].key, (int)sButtons[i].fallback);
}

bool PcInput_Capturing(void)
{
    return sCapture >= 0;
}

bool PcInput_OnKeyDown(SDL_Scancode code)
{
    if (sCapture < 0)
        return false;
    if (code != SDL_SCANCODE_ESCAPE)
    {
        CtrSettings_SetInt(sButtons[sCapture].key, (int)code);
        CtrLog_Write(CTR_LOG_INPUT, "controls: %s = %s", sButtons[sCapture].key, SDL_GetScancodeName(code));
    }
    sCapture = -1;
    return true;
}

u32 PcInput_Keyboard(void)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    u32 held = 0;

    if (sCapture >= 0 && SDL_GetTicks() - sCaptureStart > CAPTURE_MS)
        sCapture = -1;
    if (sCapture >= 0)
        return 0; /* the key being chosen is not a press */
    for (int i = 0; i < PC_BUTTON_COUNT; ++i)
        if (k[Binding(i)])
            held |= sButtons[i].bits;
    /* Alt+Enter is fullscreen, not START. */
    if (k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT])
        held &= ~KEY_START;
    return held;
}

/* ── Gamepads ───────────────────────────────────────────────────────────── */

#define MAX_PADS 4
static SDL_Gamepad *sPads[MAX_PADS];

void PcInput_Init(void)
{
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        CtrLog_Write(CTR_LOG_ERROR, "gamepads unavailable: %s", SDL_GetError());
}

void PcInput_OnEvent(const SDL_Event *e)
{
    if (e->type == SDL_EVENT_GAMEPAD_ADDED)
    {
        for (int i = 0; i < MAX_PADS; ++i)
            if (sPads[i] == NULL)
            {
                sPads[i] = SDL_OpenGamepad(e->gdevice.which);
                if (sPads[i] != NULL)
                    CtrLog_Write(CTR_LOG_INPUT, "gamepad connected: %s", SDL_GetGamepadName(sPads[i]));
                break;
            }
    }
    else if (e->type == SDL_EVENT_GAMEPAD_REMOVED)
    {
        for (int i = 0; i < MAX_PADS; ++i)
            if (sPads[i] != NULL && SDL_GetGamepadID(sPads[i]) == e->gdevice.which)
            {
                CtrLog_Write(CTR_LOG_INPUT, "gamepad disconnected: %s", SDL_GetGamepadName(sPads[i]));
                SDL_CloseGamepad(sPads[i]);
                sPads[i] = NULL;
            }
    }
}

/* The circle pad's range on the console, as hidCircleRead gives it. */
#define CIRCLE_MAX 156

u32 PcInput_Gamepads(circlePosition *circle)
{
    bool nintendo = CtrSettings_GetInt("pc_pad_layout", 0) != 0;
    u32 held = 0;
    int bestX = 0, bestY = 0;

    for (int i = 0; i < MAX_PADS; ++i)
    {
        SDL_Gamepad *p = sPads[i];
        if (p == NULL)
            continue;
        bool south = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH);
        bool east = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_EAST);
        bool west = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_WEST);
        bool north = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_NORTH);
        if (nintendo)
        {
            if (east) held |= KEY_A;
            if (south) held |= KEY_B;
            if (north) held |= KEY_X;
            if (west) held |= KEY_Y;
        }
        else
        {
            if (south) held |= KEY_A;
            if (east) held |= KEY_B;
            if (north) held |= KEY_X;
            if (west) held |= KEY_Y;
        }
        if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) held |= KEY_L;
        if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) held |= KEY_R;
        if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_START)) held |= KEY_START;
        if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_BACK)) held |= KEY_SELECT;
        if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_UP)) held |= KEY_DUP;
        if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) held |= KEY_DDOWN;
        if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) held |= KEY_DLEFT;
        if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) held |= KEY_DRIGHT;
        if (SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) held |= KEY_ZL;
        if (SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) held |= KEY_ZR;
        /* The stick: SDL's -32768..32767, y down; the console's +-156, y up.
         * The port applies its own dead zone (3ds_input_decode.c). */
        int x = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX) * CIRCLE_MAX / 32767;
        int y = -SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY) * CIRCLE_MAX / 32767;
        if (x * x + y * y > bestX * bestX + bestY * bestY)
        {
            bestX = x;
            bestY = y;
        }
    }
    circle->dx = (s16)SDL_clamp(bestX, -CIRCLE_MAX, CIRCLE_MAX);
    circle->dy = (s16)SDL_clamp(bestY, -CIRCLE_MAX, CIRCLE_MAX);
    return held;
}
