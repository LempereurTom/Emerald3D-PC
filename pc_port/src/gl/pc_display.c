/*
 * The window: the two screens, input, frame dumps.
 *
 * The 3DS's screens are portrait panels turned sideways: a framebuffer row
 * is a screen column (left to right), and inside a row pixels run from the
 * bottom of the screen to the top. The top screen's render target (240 x 400)
 * and the bottom screen's framebuffer (240 x 320) are both stored that way,
 * so one mapping shows both: screen point (s, t) in [0,1]^2, t down, is
 * texel (u, v) = (1 - t, s).
 *
 * Layout (J4, pc_layout.c): vertical (the 3DS's), side by side, inset or a
 * single screen, at the largest whole scale the window holds. Keys:
 *   F9 / Shift+F9   next / previous layout     Tab   swap the screens
 *   F11, Alt+Enter  fullscreen
 * The choice is kept in settings.txt (pc_layout, pc_swap). The mouse (or a
 * touch screen, through SDL's mouse events) is the stylus on the bottom
 * screen: a press there starts a touch, which follows the pointer, held to
 * the screen's edge, until the button is let go.
 *
 * Environment:
 *   EMERALD_PC_SCALE=n          initial window scale (default 2)
 *   EMERALD_PC_HIDDEN=1         no visible window (automated runs)
 *   EMERALD_PC_DUMP=dir         save frames as PNG into dir
 *   EMERALD_PC_DUMP_EVERY=n     every n frames (default 60), in the layout
 *                               shown, at 1x
 *   EMERALD_PC_DUMP_FROM=n      no frame dumped before frame n
 *   EMERALD_PC_DUMP_LAYOUTS=1   each dump in every layout (checks)
 *   EMERALD_PC_LAYOUT=name      vertical, side, inset, single (over settings)
 *   EMERALD_PC_SWAP=1           screens swapped (over settings)
 *   EMERALD_PC_NOPACE=1         no VBlank wait: as fast as the PC goes
 *                               (automated runs)
 */
#include "pc_c3d_internal.h"
#include "../pc_layout.h"
#include "../pc_input.h"
#include "../pc_ambience.h"
#include "pc_hud_gl.h"
#include "pc_options.h"
#include "3ds_platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GLuint PcC3d_TargetTexture(C3D_RenderTarget *target);

/* The largest layout at 1x (side by side is 720 wide, vertical 480 high). */
#define DUMP_W 1920
#define DUMP_H 1080

static SDL_Window *sWindow;
static SDL_GLContext sGl;
static GLuint sBlitProgram, sBlitVao, sBottomTex, sTopCpuTex, sDumpFbo, sDumpTex;
static GLint sBlitRect, sBlitTex, sBlitGpuRows, sBlitSrc, sBlitTint, sBlitSharp, sBlitGrade, sBlitAlpha, sBlitRound;
static u8 sTopFb[400 * 240 * 4], sBottomFb[320 * 240 * 4];
static GSPGPU_FramebufferFormat sFormat[2] = { GSP_BGR8_OES, GSP_BGR8_OES };
static bool sWide, s3d;
static const char *sDumpDir;
static unsigned sDumpEvery = 60;
static unsigned sFrames;

/* ── VBlank: the console's display clock ────────────────────────────────── *
 * On the console, C3D_FrameBegin(C3D_FRAME_SYNCDRAW) and gspWaitForVBlank()
 * wait for the LCD's next VBlank, and that wait is what holds the game to its
 * speed (3ds_platform.c's fast-forward and real-time pacing build on it).
 * The PC keeps the same clock, the LCD's 59.8261 Hz, whatever the monitor's
 * refresh: a deadline that advances one period per wait. Late by more than a
 * few periods (a window drag, a breakpoint), it starts over rather than
 * rushing to catch up, as the console's VBlank would. */
#define VBLANK_HZ 59.8261
#define VBLANK_LET_GO 4

static Uint64 sNextVBlank;
static int sPace = -1;
/* The LCD: on the console the screens show their framebuffers at every
 * VBlank, whether a frame was drawn or not, so the CPU's writes into the
 * bottom screen's framebuffer appear by themselves (the game's menus while
 * the top screen is held: 3ds_video.c waits for VBlank without a frame).
 * The window does the same: a VBlank with no frame presented since the last
 * one shows the screens again, from the last targets and the framebuffers. */
static bool sPresented;
static C3D_RenderTarget *sLastTop, *sLastBottom;

void PcDisplay_WaitVBlank(void)
{
    const Uint64 period = (Uint64)(1e9 / VBLANK_HZ + 0.5);
    Uint64 now;

    if (sPace < 0)
    {
        const char *env = getenv("EMERALD_PC_NOPACE");
        sPace = !(env && *env && *env != '0');
    }
    if (!sPace)
        return;
    now = SDL_GetTicksNS();
    if (sNextVBlank == 0 || now > sNextVBlank + VBLANK_LET_GO * period)
        sNextVBlank = now;
    else if (now < sNextVBlank)
        SDL_DelayPrecise(sNextVBlank - now);
    sNextVBlank += period;
}

void PcDisplay_ForgetTarget(C3D_RenderTarget *target)
{
    if (sLastTop == target)
        sLastTop = NULL;
    if (sLastBottom == target)
        sLastBottom = NULL;
}

void gspWaitForVBlank(void)
{
    if (!sPresented)
        PcDisplay_Present(sLastTop, sLastBottom);
    sPresented = false;
    PcDisplay_WaitVBlank();
}

/* ── gfx: the screens' framebuffers ─────────────────────────────────────── */

void gfxSetScreenFormat(gfxScreen_t screen, GSPGPU_FramebufferFormat format)
{
    sFormat[screen == GFX_BOTTOM] = format;
}

GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t screen) { return sFormat[screen == GFX_BOTTOM]; }

u8 *gfxGetFramebuffer(gfxScreen_t screen, gfx3dSide_t side, u16 *width, u16 *height)
{
    (void)side;
    if (width)
        *width = 240;
    if (height)
        *height = screen == GFX_BOTTOM ? 320 : 400;
    return screen == GFX_BOTTOM ? sBottomFb : sTopFb;
}

bool gfxIsWide(void) { return sWide; }
void gfxSetWide(bool enable) { sWide = enable; }
void gfxSet3D(bool enable) { s3d = enable; }
bool gfxIs3D(void) { return s3d; }

/* ── Setup ──────────────────────────────────────────────────────────────── */

static const char sBlitVs[] =
    "#version 330 core\n"
    "uniform vec4 uRect;\n" /* x, y, w, h in clip space */
    "out vec2 vSt;\n"
    "void main() {\n"
    "    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    vSt = c;\n"
    "    gl_Position = vec4(uRect.x + c.x * uRect.z, uRect.y - c.y * uRect.w, 0.0, 1.0);\n"
    "}\n";
/*
 * One screen into a rectangle of the window:
 *   uSrc    the part of the screen shown (s, t, width, height in 0..1; the
 *           FUSION layout's dock is a band of the bottom screen)
 *   uTint   multiplied in (the world dimmed under the bottom screen)
 *   uSharp  the scale, when the picture is sharp at a scale that is not
 *           whole: "sharp bilinear", each texel kept square and only its
 *           border blended, with the linear sampler; 0: plain sampling
 *   uGrade  contrast and saturation (COLOURS option), 0 for none
 *   uAlpha  the texture's coverage kept (FUSION's interface: premultiplied)
 *   uRound  the rectangle's size and its corners' radius, window pixels
 */
static const char sBlitFs[] =
    "#version 330 core\n"
    "in vec2 vSt;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D uTex;\n"
    "uniform int uGpuRows;\n" /* a render target: its GL rows run reversed (pc_c3d_mem.c) */
    "uniform vec4 uSrc;\n"
    "uniform vec4 uTint;\n"
    "uniform float uSharp;\n"
    "uniform vec2 uGrade;\n"
    "uniform int uAlpha;\n"
    "uniform vec3 uRound;\n"
    "void main() {\n"
    "    if (uRound.z > 0.0) {\n"
    "        vec2 p = vSt * uRound.xy;\n"
    "        vec2 q = min(p, uRound.xy - p);\n"
    "        if (q.x < uRound.z && q.y < uRound.z && length(vec2(uRound.z) - q) > uRound.z) discard;\n"
    "    }\n"
    "    vec2 st = uSrc.xy + vSt * uSrc.zw;\n"
    "    float v = uGpuRows != 0 ? 1.0 - st.x : st.x;\n"
    "    vec2 tc = vec2(1.0 - st.y, v);\n"
    "    if (uSharp > 0.0) {\n"
    "        vec2 size = vec2(textureSize(uTex, 0));\n"
    "        vec2 texel = tc * size;\n"
    "        vec2 range = vec2(0.5 - 0.5 / uSharp);\n"
    "        vec2 centre = fract(texel) - 0.5;\n"
    "        vec2 f = (centre - clamp(centre, -range, range)) * uSharp + 0.5;\n"
    "        tc = (floor(texel) + f) / size;\n"
    "    }\n"
    "    vec4 t = texture(uTex, tc);\n"
    "    vec3 c = t.rgb;\n"
    "    if (uGrade.x > 0.0 || uGrade.y > 0.0) {\n"
    "        c = (c - 0.5) * (1.0 + uGrade.x) + 0.5;\n"
    "        float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "        c = mix(vec3(l), c, 1.0 + uGrade.y);\n"
    "    }\n"
    "    fragColor = vec4(clamp(c, 0.0, 1.0) * uTint.rgb, uAlpha != 0 ? t.a : 1.0);\n"
    "}\n";

static GLuint MakeBlitProgram(void)
{
    const char *src[2] = { sBlitVs, sBlitFs };
    GLenum types[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    GLuint p = pglCreateProgram();
    for (int i = 0; i < 2; ++i)
    {
        GLuint s = pglCreateShader(types[i]);
        pglShaderSource(s, 1, &src[i], NULL);
        pglCompileShader(s);
        GLint ok = 0;
        pglGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[1024];
            pglGetShaderInfoLog(s, sizeof(log), NULL, log);
            fprintf(stderr, "[VIDEO] display shader: %s\n", log);
            return 0;
        }
        pglAttachShader(p, s);
    }
    pglBindFragDataLocation(p, 0, "fragColor");
    pglLinkProgram(p);
    sBlitRect = pglGetUniformLocation(p, "uRect");
    sBlitTex = pglGetUniformLocation(p, "uTex");
    sBlitGpuRows = pglGetUniformLocation(p, "uGpuRows");
    sBlitSrc = pglGetUniformLocation(p, "uSrc");
    sBlitAlpha = pglGetUniformLocation(p, "uAlpha");
    sBlitRound = pglGetUniformLocation(p, "uRound");
    sBlitTint = pglGetUniformLocation(p, "uTint");
    sBlitSharp = pglGetUniformLocation(p, "uSharp");
    sBlitGrade = pglGetUniformLocation(p, "uGrade");
    return p;
}

static GLuint MakeTexture(int w, int h)
{
    GLuint t;
    pglGenTextures(1, &t);
    pglBindTexture(GL_TEXTURE_2D, t);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

/* ── Layout ─────────────────────────────────────────────────────────────── */

static PcLayoutKind sLayoutKind = PC_LAYOUT_FUSION; /* the default: one screen */
static bool sSwap;
static PcLayout sLayout; /* as last drawn, for the mouse */

static void LayoutRead(void)
{
    const char *env = getenv("EMERALD_PC_LAYOUT");
    int kind = PcLayout_Parse(env);
    if (kind < 0)
        kind = CtrSettings_GetInt("pc_layout", PC_LAYOUT_FUSION);
    sLayoutKind = kind >= 0 && kind < PC_LAYOUT_COUNT ? (PcLayoutKind)kind : PC_LAYOUT_FUSION;
    env = getenv("EMERALD_PC_SWAP");
    sSwap = env && *env ? *env != '0' : CtrSettings_GetInt("pc_swap", 0) != 0;
}

static void LayoutLoad(void)
{
    LayoutRead();
    CtrLog_Write(CTR_LOG_VIDEO, "layout: %s%s (F9: next, Tab: swap, F11: fullscreen)", PcLayout_Name(sLayoutKind),
                 sSwap ? ", swapped" : "");
}

static int WindowScale(void);

/* A new layout: the window keeps its scale, if it is a plain window. */
static void LayoutChanged(void)
{
    CtrSettings_SetInt("pc_layout", (int)sLayoutKind);
    CtrSettings_SetInt("pc_swap", sSwap ? 1 : 0);
    CtrLog_Write(CTR_LOG_VIDEO, "layout: %s%s", PcLayout_Name(sLayoutKind), sSwap ? ", swapped" : "");
    SDL_WindowFlags flags = SDL_GetWindowFlags(sWindow);
    if (!(flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED)))
    {
        PcLayout next;
        int scale = WindowScale();
        float density = SDL_GetWindowPixelDensity(sWindow);
        PcLayout_Compute(sLayoutKind, sSwap, 1 << 16, 1 << 16, &next);
        if (density <= 0.0f)
            density = 1.0f;
        SDL_SetWindowSize(sWindow, (int)(next.logicalW * scale / density), (int)(next.logicalH * scale / density));
    }
}

/* ── Display options (OPTIONS > DISPLAY, pc_options.h) ─────────────────── */

static bool sStretch, sSmooth;
static int sColours; /* pc_colors: 0 off, 1 light, 2 strong */
/* The top screen's texture: this many times the screen (the 3D resolution). */
static int sTopScale = 1;
int PcC3d_TargetScale(C3D_RenderTarget *target);
void PcC3d_SetQuality(int scale, int samples);
static bool sParticles; /* pc_particles */
int PcGame_Ambience(void);
void PcAmbience_Draw(const PcRect *top, int fbW, int fbH, int ambience, float dt);
float PcAmbience_Weather(int ambience, bool enabled, float dt);
static bool sWeather; /* pc_weather */
static int sAmbience; /* this frame's (PcGame_Ambience, or EMERALD_PC_AMBIENCE) */
static float sRainNow;
/* FUSION: the bottom screen opened by hand (Tab), and the bottom UI's own
 * wish to be seen (3ds_bottom_ui.c, PORT_PC). */
static bool sFusionOpen;
/* FUSION's interface (pc_hud.c): drawn by this Compose, a tap it asked for
 * (a click or a key on one of its buttons, a few frames held), whether the
 * last input was a gamepad's (no key badges then). */
static bool sComposeHud;
bool PcGameData_External(void);
void PcMenu_Init(SDL_Window *window);
void PcMenu_Update(bool fullscreen);
static int sHudTapX, sHudTapY, sHudTapFrames;
static bool sPadLast;
/* The mouse's pointer: hidden in fullscreen FUSION, back for a moment when
 * it moves (to click the interface). */
static Uint64 sMouseMovedMs;
/* Escape (OnKey): B held a few frames, for a game screen open on the bottom
 * screen (the bag, the party...), which B closes. */
static int sEscapeBFrames;
unsigned char CtrBottom_PcEscape(void);
static bool sCursorHidden;

static void UpdateCursor(void)
{
    bool full = (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_FULLSCREEN) != 0;
    bool hide = full && sLayoutKind == PC_LAYOUT_FUSION && SDL_GetTicks() - sMouseMovedMs > 1500;

    if (hide == sCursorHidden)
        return;
    sCursorHidden = hide;
    if (hide)
        SDL_HideCursor();
    else
        SDL_ShowCursor();
}
unsigned char CtrBottom_PcWantsShown(void);
unsigned char CtrBottom_PcDockShown(void);
static int sAppliedScale = -1;
static GLuint sBlitSampler;

/* The window's scale: EMERALD_PC_SCALE, else pc_scale (0..5 = 1x..6x). */
static int WindowScale(void)
{
    const char *env = getenv("EMERALD_PC_SCALE");
    if (env && atoi(env) > 0)
        return atoi(env);
    int index = CtrSettings_GetInt("pc_scale", 1);
    return index >= 0 && index < 6 ? index + 1 : 2;
}

/*
 * GRAPHICS: the 3D world's resolution (1x-4x the console's 400x240) and its
 * anti-aliasing (multisampling), the look unchanged otherwise (pc_c3d_mem.c,
 * PcC3d_SetQuality). A preset sets both; changing either by hand makes the
 * preset CUSTOM. EMERALD_PC_QUALITY=scale,samples (checks) overrides.
 */
static const int sPresetValues[4][2] = { { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 } }; /* scale, msaa indexes */

static void ApplyGraphics(void)
{
    static int sLastPreset = -1;
    int preset = CtrSettings_GetInt("pc_gfx_preset", 2);
    int scale = CtrSettings_GetInt("pc_render_scale", -1), msaa = CtrSettings_GetInt("pc_msaa", -1);

    if (preset >= 0 && preset < 4)
    {
        if (sLastPreset >= 0 && preset != sLastPreset)
        {
            /* A preset chosen: its values. */
            scale = sPresetValues[preset][0];
            msaa = sPresetValues[preset][1];
            CtrSettings_SetInt("pc_render_scale", scale);
            CtrSettings_SetInt("pc_msaa", msaa);
        }
        else if (scale < 0 || msaa < 0)
        {
            /* Never set: the preset's, nothing written. */
            scale = sPresetValues[preset][0];
            msaa = sPresetValues[preset][1];
        }
        else if (scale != sPresetValues[preset][0] || msaa != sPresetValues[preset][1])
        {
            preset = 4; /* a value changed by hand */
            CtrSettings_SetInt("pc_gfx_preset", preset);
        }
    }
    sLastPreset = preset;
    if (scale < 0 || scale > 3)
        scale = 2;
    if (msaa < 0 || msaa > 3)
        msaa = 2;
    int k = scale + 1, samples = msaa == 0 ? 1 : 1 << msaa;
    const char *env = getenv("EMERALD_PC_QUALITY");
    if (env && *env)
        sscanf(env, "%d,%d", &k, &samples);
    PcC3d_SetQuality(k, samples);
}

void PcOptions_Apply(void)
{
    if (sWindow == NULL)
        return;
    PcLayoutKind kind = sLayoutKind;
    bool swap = sSwap;
    LayoutRead();
    sStretch = CtrSettings_GetInt("pc_stretch", 1) != 0;
    sColours = CtrSettings_GetInt("pc_colors", 1);
    sParticles = CtrSettings_GetInt("pc_particles", 1) != 0;
    sWeather = CtrSettings_GetInt("pc_weather", 1) != 0;
    sSmooth = CtrSettings_GetInt("pc_filter", 0) != 0;
    SDL_GL_SetSwapInterval(CtrSettings_GetInt("pc_vsync", 0) != 0 ? 1 : 0);
    ApplyGraphics();

    int mode = CtrSettings_GetInt("pc_window", 0);
    SDL_DisplayID display = SDL_GetDisplayForWindow(sWindow);
    if (mode == 2)
    {
        /* Exclusive: the chosen mode, or the desktop's. */
        const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(display);
        int res = CtrSettings_GetInt("pc_res", 0);
        int w = res > 0 ? res / 10000 : desktop ? desktop->w : 0;
        int h = res > 0 ? res % 10000 : desktop ? desktop->h : 0;
        SDL_DisplayMode chosen;
        if (w > 0 && SDL_GetClosestFullscreenDisplayMode(display, w, h, 0.0f, true, &chosen))
            SDL_SetWindowFullscreenMode(sWindow, &chosen);
        SDL_SetWindowFullscreen(sWindow, true);
    }
    else if (mode == 1)
    {
        SDL_SetWindowFullscreenMode(sWindow, NULL); /* borderless, the desktop's mode */
        SDL_SetWindowFullscreen(sWindow, true);
    }
    else
        SDL_SetWindowFullscreen(sWindow, false);

    int scale = WindowScale();
    if (scale != sAppliedScale || kind != sLayoutKind || swap != sSwap)
    {
        sAppliedScale = scale;
        LayoutChanged();
    }
}

/* The display's modes, one per size (the highest refresh first, as SDL
 * lists them), for the exclusive fullscreen's RESOLUTION. */
static int Modes(int *w, int *h, int max)
{
    int count = 0, n = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(sWindow), &count);
    for (int i = 0; modes != NULL && i < count && n < max; ++i)
    {
        bool seen = false;
        for (int j = 0; j < n; ++j)
            seen = seen || (w[j] == modes[i]->w && h[j] == modes[i]->h);
        if (!seen)
        {
            w[n] = modes[i]->w;
            h[n] = modes[i]->h;
            ++n;
        }
    }
    SDL_free(modes);
    return n;
}

void PcOptions_StepResolution(int direction)
{
    int w[64], h[64], n, current = -1, res = CtrSettings_GetInt("pc_res", 0);

    if (sWindow == NULL || (n = Modes(w, h, 64)) == 0)
        return;
    for (int i = 0; i < n; ++i)
        if (res == w[i] * 10000 + h[i])
            current = i;
    /* -1 is the desktop's mode, then the list. */
    int next = current + (direction < 0 ? -1 : 1);
    if (next < -1)
        next = n - 1;
    if (next >= n)
        next = -1;
    CtrSettings_SetInt("pc_res", next < 0 ? 0 : w[next] * 10000 + h[next]);
    if (CtrSettings_GetInt("pc_window", 0) == 2)
        PcOptions_Apply();
}

const char *PcOptions_ResolutionName(void)
{
    static char name[16];
    int res = CtrSettings_GetInt("pc_res", 0);
    if (res <= 0)
        return NULL;
    snprintf(name, sizeof(name), "%dX%d", res / 10000, res % 10000);
    return name;
}

bool PcDisplay_Init(void)
{
    if (sWindow != NULL)
        return true;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        fprintf(stderr, "[VIDEO] SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    int scale = WindowScale();
    sAppliedScale = scale;
    const char *hidden = getenv("EMERALD_PC_HIDDEN");
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (hidden && *hidden && *hidden != '0')
        flags |= SDL_WINDOW_HIDDEN;
    LayoutLoad();
    PcLayout first;
    PcLayout_Compute(sLayoutKind, sSwap, 1 << 16, 1 << 16, &first);
    /* A release's executable carries no game data (pc_bundles.c). */
    sWindow = SDL_CreateWindow(PcGameData_External() ? "Emerald PC" : "Emerald PC (development build)",
                               first.logicalW * scale, first.logicalH * scale, flags);
    if (sWindow == NULL)
    {
        fprintf(stderr, "[VIDEO] SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    PcMenu_Init(sWindow); /* the OPTIONS in a menu bar (pc_menu.c) */
    sGl = SDL_GL_CreateContext(sWindow);
    if (sGl == NULL)
    {
        fprintf(stderr, "[VIDEO] OpenGL 3.3 context: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(sWindow, sGl);
    SDL_GL_SetSwapInterval(0); /* PcDisplay_WaitVBlank paces frames (59.8261 Hz); pc_vsync below */
    if (!PcGl_Load())
        return false;
    fprintf(stderr, "[VIDEO] OpenGL %s on %s\n", (const char *)pglGetString(GL_VERSION),
            (const char *)pglGetString(GL_RENDERER));
    sBlitProgram = MakeBlitProgram();
    /* The screens' sampling for the window (sharp or smooth, pc_filter): a
     * sampler of its own, so the textures the game samples keep theirs. */
    pglGenSamplers(1, &sBlitSampler);
    pglSamplerParameteri(sBlitSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    pglSamplerParameteri(sBlitSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    PcInput_Init();
    PcOptions_Apply();
    PcOptions_ApplyAudio();
    pglGenVertexArrays(1, &sBlitVao);
    sBottomTex = MakeTexture(240, 320);
    sTopCpuTex = MakeTexture(240, 400);
    sDumpTex = MakeTexture(DUMP_W, DUMP_H);
    pglGenFramebuffers(1, &sDumpFbo);
    pglBindFramebuffer(GL_FRAMEBUFFER, sDumpFbo);
    pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sDumpTex, 0);
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    sDumpDir = getenv("EMERALD_PC_DUMP");
    const char *every = getenv("EMERALD_PC_DUMP_EVERY");
    if (every && atoi(every) > 0)
        sDumpEvery = (unsigned)atoi(every);
    return true;
}

void PcDisplay_Shutdown(void)
{
    if (sGl)
        SDL_GL_DestroyContext(sGl);
    if (sWindow)
        SDL_DestroyWindow(sWindow);
    sGl = NULL;
    sWindow = NULL;
}

/* ── Input ──────────────────────────────────────────────────────────────── */

static bool sTouching; /* a press that started on the bottom screen, still held */

static void OnKey(const SDL_KeyboardEvent *key)
{
    if (key->repeat)
        return;
    if (key->scancode == SDL_SCANCODE_F9)
    {
        int step = (key->mod & SDL_KMOD_SHIFT) ? PC_LAYOUT_COUNT - 1 : 1;
        sLayoutKind = (PcLayoutKind)((sLayoutKind + step) % PC_LAYOUT_COUNT);
        LayoutChanged();
    }
    else if (key->scancode == SDL_SCANCODE_ESCAPE)
    {
        /* Out of the bottom screen's menu: FUSION's bottom screen opened by
         * hand closes; the bottom UI leaves its own menus; a game screen
         * gets B. */
        if (sLayoutKind == PC_LAYOUT_FUSION && sFusionOpen && !CtrBottom_PcWantsShown())
        {
            sFusionOpen = false;
            CtrBottom_PcGoMap();
        }
        else if (!CtrBottom_PcEscape() && CtrBottom_PcWantsShown())
            sEscapeBFrames = 4;
        sFusionOpen = false;
    }
    else if (sLayoutKind == PC_LAYOUT_FUSION && !sLayout.bottomShown
             && PcHud_Key(key->scancode, &sHudTapX, &sHudTapY))
        sHudTapFrames = 6; /* P, I, F5, O, Shift: their button */
    else if (sLayoutKind == PC_LAYOUT_FUSION && key->scancode == SDL_SCANCODE_M
             && (sFusionOpen || PcHud_Visible()))
    {
        sFusionOpen = !sFusionOpen; /* M: the whole map, or away */
        if (!sFusionOpen)
            CtrBottom_PcGoMap();
    }
    else if (key->scancode == SDL_SCANCODE_TAB)
    {
        if (sLayoutKind == PC_LAYOUT_FUSION)
        {
            sFusionOpen = !sFusionOpen; /* the bottom screen over the world, or away */
            if (!sFusionOpen)
                CtrBottom_PcGoMap();
        }
        else
        {
            sSwap = !sSwap;
            LayoutChanged();
        }
    }
    else if (key->scancode == SDL_SCANCODE_F11
             || (key->scancode == SDL_SCANCODE_RETURN && (key->mod & SDL_KMOD_ALT)))
    {
        /* Window <-> fullscreen (the exclusive one stays exclusive). */
        int mode = CtrSettings_GetInt("pc_window", 0);
        CtrSettings_SetInt("pc_window", mode == 0 ? 1 : 0);
        PcOptions_Apply();
    }
}

static void PumpInput(void)
{
    SDL_Event e;
    float density = SDL_GetWindowPixelDensity(sWindow);
    int tx, ty;

    if (density <= 0.0f)
        density = 1.0f;
    while (SDL_PollEvent(&e))
    {
        if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            PcApt_RequestExit();
        else if (e.type == SDL_EVENT_KEY_DOWN)
        {
            sPadLast = false;
            /* A CONTROLS row waiting for its key takes this one. */
            if (!(e.key.repeat == false && PcInput_OnKeyDown(e.key.scancode)))
                OnKey(&e.key);
        }
        else if (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED)
            PcInput_OnEvent(&e);
        else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            float px = e.button.x * density, py = e.button.y * density;
            int hit = sLayoutKind == PC_LAYOUT_FUSION && !sLayout.bottomShown ? PcHud_Hit(px, py, &tx, &ty)
                                                                               : PC_HUD_HIT_NONE;
            sPadLast = false;
            sTouching = false;
            if (hit == PC_HUD_HIT_TAP)
            {
                sHudTapX = tx;
                sHudTapY = ty;
                sHudTapFrames = 6;
            }
            else if (hit == PC_HUD_HIT_MINIMAP)
                sFusionOpen = true;
            else
            {
                sTouching = PcLayout_ToBottom(&sLayout, px, py, false, &tx, &ty);
                /* FUSION's bottom screen opened by hand: a click beside it puts it away. */
                if (!sTouching && sLayoutKind == PC_LAYOUT_FUSION && sFusionOpen && !CtrBottom_PcWantsShown())
                {
                    sFusionOpen = false;
                    CtrBottom_PcGoMap();
                }
            }
        }
        else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
            sTouching = false;
        else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST)
            sTouching = false;
        else if (e.type == SDL_EVENT_MOUSE_MOTION)
            sMouseMovedMs = SDL_GetTicks();
    }

    /* The keyboard's keys (OPTIONS > CONTROLS) and every gamepad. */
    circlePosition circle = { 0, 0 };
    u32 pad = PcInput_Gamepads(&circle);
    u32 held = PcInput_Keyboard() | pad;
    if (sEscapeBFrames > 0)
    {
        --sEscapeBFrames;
        held |= KEY_B;
    }
    if (pad != 0 || circle.dx * circle.dx + circle.dy * circle.dy > 40 * 40)
        sPadLast = true;

    touchPosition touch = { 0, 0 };
    float mx, my;
    SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
    if (!(buttons & SDL_BUTTON_LMASK))
        sTouching = false;
    /* Window points -> pixels -> the bottom screen's 320x240, held to its
     * edges while the press lasts. */
    if (sTouching && PcLayout_ToBottom(&sLayout, mx * density, my * density, true, &tx, &ty))
    {
        held |= KEY_TOUCH;
        touch.px = (u16)tx;
        touch.py = (u16)ty;
    }
    else if (sHudTapFrames > 0)
    {
        /* A tap FUSION's interface asked for: held a few frames, then let go. */
        --sHudTapFrames;
        held |= KEY_TOUCH;
        touch.px = (u16)sHudTapX;
        touch.py = (u16)sHudTapY;
    }
    PcHid_SetState(held, touch, circle);
    UpdateCursor();
    PcMenu_Update((SDL_GetWindowFlags(sWindow) & SDL_WINDOW_FULLSCREEN) != 0);
}

/* ── Present ────────────────────────────────────────────────────────────── */

/* How the next blits sample and colour (Compose sets them per screen). */
static float sBlitSrcRect[4] = { 0, 0, 1, 1 }, sBlitTintValue = 1.0f, sBlitSharpValue, sBlitGradeValue[2];
static float sBlitTintRgb[3] = { 1.0f, 1.0f, 1.0f }; /* times sBlitTintValue: a shower's greyer world */
static int sBlitAlphaValue;   /* FUSION's interface: its canvas' coverage */
static float sBlitRoundValue; /* FUSION's minimap: its corners' radius */

static void Blit(GLuint tex, bool gpuRows, int x, int y, int w, int h, int fbW, int fbH)
{
    pglUniform1i(sBlitGpuRows, gpuRows);
    pglUniform4fv(sBlitSrc, 1, sBlitSrcRect);
    pglUniform4fv(sBlitTint, 1, (GLfloat[]){ sBlitTintValue * sBlitTintRgb[0], sBlitTintValue * sBlitTintRgb[1],
                                          sBlitTintValue * sBlitTintRgb[2], 1.0f });
    pglUniform1f(sBlitSharp, sBlitSharpValue);
    pglUniform2f(sBlitGrade, sBlitGradeValue[0], sBlitGradeValue[1]);
    pglUniform1i(sBlitAlpha, sBlitAlphaValue);
    pglUniform3f(sBlitRound, (float)w, (float)h, sBlitRoundValue);
    /* Logical rectangle (top-left origin) -> clip space. */
    float cx = (float)x / fbW * 2.0f - 1.0f, cy = 1.0f - (float)y / fbH * 2.0f;
    float cw = (float)w / fbW * 2.0f, ch = (float)h / fbH * 2.0f;
    pglUniform4fv(sBlitRect, 1, (GLfloat[]){ cx, cy, cw, ch });
    pglActiveTexture(GL_TEXTURE0);
    pglBindTexture(GL_TEXTURE_2D, tex);
    pglUniform1i(sBlitTex, 0);
    pglDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void UploadCpuScreen(GLuint tex, const u8 *fb, int w, int h, GSPGPU_FramebufferFormat fmt)
{
    static u8 rgba[400 * 240 * 4];
    for (int i = 0; i < w * h; ++i)
    {
        u8 *o = rgba + i * 4;
        if (fmt == GSP_RGB565_OES)
        {
            u16 v = (u16)(fb[i * 2] | (fb[i * 2 + 1] << 8));
            o[0] = (u8)(((v >> 11) & 31) << 3);
            o[1] = (u8)(((v >> 5) & 63) << 2);
            o[2] = (u8)((v & 31) << 3);
        }
        else if (fmt == GSP_RGBA8_OES)
        {
            o[0] = fb[i * 4 + 3];
            o[1] = fb[i * 4 + 2];
            o[2] = fb[i * 4 + 1];
        }
        else /* BGR8 */
        {
            o[0] = fb[i * 3 + 2];
            o[1] = fb[i * 3 + 1];
            o[2] = fb[i * 3];
        }
        o[3] = 255;
    }
    pglBindTexture(GL_TEXTURE_2D, tex);
    pglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    pglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static bool sBlitForWindow; /* the window's compose (the dumps keep plain 1x) */

/* FUSION's interface's canvas (CtrBottom_PcHudCanvas): its plates on black
 * and on white make one picture with its coverage, premultiplied - the
 * black one's colour, the coverage where white and black agree. */
static GLuint sHudTex;
static unsigned sHudTexVersion;

static void UploadHudCanvas(void)
{
    static u8 rgba[240 * 320 * 4];
    const u16 *black = CtrBottom_PcHudCanvas(0), *white = CtrBottom_PcHudCanvas(1);

    if (sHudTex == 0)
        sHudTex = MakeTexture(240, 320);
    for (int i = 0; i < 240 * 320; ++i)
    {
        u16 b = black[i], w = white[i];
        int br = ((b >> 11) & 31) << 3, bg = ((b >> 5) & 63) << 2, bb = (b & 31) << 3;
        int wr = ((w >> 11) & 31) << 3, wg = ((w >> 5) & 63) << 2, wb = (w & 31) << 3;
        int gap = ((wr - br) + (wg - bg) + (wb - bb)) / 3, a = 255 - (gap * 255) / 248;
        u8 *o = rgba + i * 4;

        a = a < 0 ? 0 : a > 255 ? 255 : a;
        o[0] = (u8)(br < a ? br : a);
        o[1] = (u8)(bg < a ? bg : a);
        o[2] = (u8)(bb < a ? bb : a);
        o[3] = (u8)a;
    }
    pglBindTexture(GL_TEXTURE_2D, sHudTex);
    pglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    pglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 240, 320, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

/* The sampling of a screen `width` texels wide drawn into `r`: sharp at a
 * whole scale is nearest; sharp at any other, the sharp bilinear; smooth,
 * linear. */
static void SamplingFor(const PcRect *r, float width)
{
    float scale = r->w / width;
    bool whole = fabsf(scale - roundf(scale)) < 0.01f;
    bool linear = sBlitForWindow && (sSmooth || !whole);

    /* A picture drawn bigger than the window (a high 3D resolution in a
     * small window) is averaged down, never made "sharp". */
    sBlitSharpValue = sBlitForWindow && !sSmooth && !whole && scale > 1.0f ? scale : 0.0f;
    pglSamplerParameteri(sBlitSampler, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    pglSamplerParameteri(sBlitSampler, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
}

static void BlitRect(GLuint tex, bool gpuRows, const PcRect *r, int fbW, int fbH)
{
    Blit(tex, gpuRows, (int)r->x, (int)r->y, (int)r->w, (int)r->h, fbW, fbH);
}

static void BlitTop(GLuint top, bool topGpu, const PcLayout *layout, int fbW, int fbH)
{
    static const float sGrades[3][2] = { { 0.0f, 0.0f }, { 0.06f, 0.14f }, { 0.13f, 0.28f } };
    int grade = sBlitForWindow && sColours >= 0 && sColours <= 2 ? sColours : 0;

    SamplingFor(&layout->top, 400.0f * sTopScale); /* the 3D resolution's texels */
    sBlitTintValue = layout->topDimmed ? 0.4f : 1.0f;
    if (sBlitForWindow && sRainNow > 0.0f)
    {
        sBlitTintRgb[0] = 1.0f - 0.20f * sRainNow;
        sBlitTintRgb[1] = 1.0f - 0.16f * sRainNow;
        sBlitTintRgb[2] = 1.0f - 0.08f * sRainNow;
    }
    sBlitGradeValue[0] = sGrades[grade][0];
    sBlitGradeValue[1] = sGrades[grade][1];
    BlitRect(top, topGpu, &layout->top, fbW, fbH);
    sBlitTintValue = 1.0f;
    sBlitTintRgb[0] = sBlitTintRgb[1] = sBlitTintRgb[2] = 1.0f;
    sBlitGradeValue[0] = sBlitGradeValue[1] = 0.0f;
}

static void BlitBottom(GLuint bottom, bool bottomGpu, const PcLayout *layout, int fbW, int fbH)
{
    SamplingFor(&layout->bottom, 320.0f);
    BlitRect(bottom, bottomGpu, &layout->bottom, fbW, fbH);
}

static void Compose(GLuint top, bool topGpu, GLuint bottom, bool bottomGpu, const PcLayout *layout, int fbW, int fbH)
{
    pglUseProgram(sBlitProgram);
    pglBindVertexArray(sBlitVao);
    pglDisable(GL_BLEND);
    pglDisable(GL_DEPTH_TEST);
    pglDisable(GL_STENCIL_TEST);
    pglDisable(GL_SCISSOR_TEST);
    pglDisable(GL_CULL_FACE);
    pglDisable(GL_COLOR_LOGIC_OP);
    pglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    pglClearColor(0, 0, 0, 1);
    pglClear(GL_COLOR_BUFFER_BIT);
    pglBindSampler(0, sBlitSampler);
    if (layout->bottomFirst && layout->bottomShown)
        BlitBottom(bottom, bottomGpu, layout, fbW, fbH);
    if (sBlitForWindow)
    {
        /* EMERALD_PC_AMBIENCE=leaves|petals|dust|night|rain (checks): over the place's. */
        static int sForced = -2;
        if (sForced == -2)
        {
            const char *env = getenv("EMERALD_PC_AMBIENCE");
            sForced = !env || !*env ? -1 : strcmp(env, "leaves") == 0 ? PC_AMBIENCE_LEAVES
                    : strcmp(env, "petals") == 0 ? PC_AMBIENCE_PETALS : strcmp(env, "dust") == 0 ? PC_AMBIENCE_DUST
                    : strcmp(env, "night") == 0 ? (PC_AMBIENCE_LEAVES | PC_AMBIENCE_NIGHT)
                    : strcmp(env, "rain") == 0 ? PC_AMBIENCE_ROUTE : -1;
            if (sForced == PC_AMBIENCE_ROUTE)
                PcAmbience_Weather(sForced, true, 400.0f); /* straight into a shower */
        }
        sAmbience = sForced >= 0 ? sForced : PcGame_Ambience();
        sRainNow = PcAmbience_Weather(sAmbience, sWeather, 1.0f / 59.8261f);
    }
    if (layout->topShown)
        BlitTop(top, topGpu, layout, fbW, fbH);
    if (sBlitForWindow && (sParticles || sRainNow > 0.0f) && layout->topShown && !layout->topDimmed)
    {
        /* Over the world, under the bottom screen and the dock. */
        PcAmbience_Draw(&layout->top, fbW, fbH, sParticles ? sAmbience : (sAmbience & PC_AMBIENCE_ROUTE),
                        1.0f / 59.8261f);
        pglUseProgram(sBlitProgram);
        pglBindVertexArray(sBlitVao);
        pglBindSampler(0, sBlitSampler);
    }
    if (!layout->bottomFirst && layout->bottomShown)
        BlitBottom(bottom, bottomGpu, layout, fbW, fbH);
    if (sComposeHud && !layout->bottomShown && layout->topShown)
    {
        /* FUSION's interface: its frames, the bottom screen's pieces, its letters. */
        PcHud_Build(&layout->top);
        PcHud_DrawShapes(0, fbW, fbH);
        pglUseProgram(sBlitProgram);
        pglBindVertexArray(sBlitVao);
        pglBindSampler(0, sBlitSampler);
        if (sHudTex == 0 || sHudTexVersion != PcHud_CanvasVersion())
        {
            UploadHudCanvas();
            sHudTexVersion = PcHud_CanvasVersion();
        }
        for (int i = 0; i < PcHud_SpriteCount(); ++i)
        {
            const PcHudSprite *sp = PcHud_Sprite(i);
            bool canvas = sp->from == PC_HUD_FROM_CANVAS;
            SamplingFor(&sp->dst, (float)sp->src.w);
            sBlitSrcRect[0] = sp->src.x / 320.0f;
            sBlitSrcRect[1] = sp->src.y / 240.0f;
            sBlitSrcRect[2] = sp->src.w / 320.0f;
            sBlitSrcRect[3] = sp->src.h / 240.0f;
            sBlitAlphaValue = canvas;
            sBlitRoundValue = sp->round;
            if (canvas)
            {
                pglEnable(GL_BLEND);
                pglBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            }
            BlitRect(canvas ? sHudTex : bottom, canvas ? false : bottomGpu, &sp->dst, fbW, fbH);
            pglDisable(GL_BLEND);
        }
        sBlitAlphaValue = 0;
        sBlitRoundValue = 0.0f;
        sBlitSrcRect[0] = sBlitSrcRect[1] = 0.0f;
        sBlitSrcRect[2] = sBlitSrcRect[3] = 1.0f;
        PcHud_DrawShapes(1, fbW, fbH);
        pglUseProgram(sBlitProgram);
        pglBindVertexArray(sBlitVao);
        pglBindSampler(0, sBlitSampler);
    }
    if (layout->stripShown)
    {
        /* FUSION's bar: the dock's rows of the bottom screen. */
        SamplingFor(&layout->strip, 320.0f);
        sBlitSrcRect[1] = PC_DOCK_Y / 240.0f;
        sBlitSrcRect[3] = PC_DOCK_H / 240.0f;
        BlitRect(bottom, bottomGpu, &layout->strip, fbW, fbH);
        sBlitSrcRect[1] = 0.0f;
        sBlitSrcRect[3] = 1.0f;
    }
    pglBindSampler(0, 0);
}

static void DumpOne(GLuint top, bool topGpu, GLuint bottom, bool bottomGpu, PcLayoutKind kind, bool swap,
                    const char *suffix)
{
    char path[1024];
    static u8 pixels[DUMP_W * DUMP_H * 4];
    PcLayout one;
    PcLayout_Compute(kind, swap, 1 << 16, 1 << 16, &one);
    int w = one.logicalW, h = one.logicalH;
    bool open = kind == PC_LAYOUT_FUSION && (sFusionOpen || CtrBottom_PcWantsShown());
    /* EMERALD_PC_DUMP_EFFECTS=1: composed as the window is (colours,
     * particles, sharp scaling, FUSION's interface at 1920x1080), to check them. */
    const char *effects = getenv("EMERALD_PC_DUMP_EFFECTS");
    bool asWindow = effects && *effects == '1';
    if (asWindow && kind == PC_LAYOUT_FUSION && !open)
    {
        w = 1920;
        h = 1080;
    }
    PcLayout_ComputeFull(kind, swap, w == one.logicalW, open, w, h, &one); /* at 1x */
    if (kind == PC_LAYOUT_FUSION && !open)
        one.stripShown = false; /* the interface instead */
    if (one.stripShown && !CtrBottom_PcDockShown())
        one.stripShown = false;
    pglBindFramebuffer(GL_FRAMEBUFFER, sDumpFbo);
    pglViewport(0, 0, w, h);
    sBlitForWindow = asWindow;
    sComposeHud = kind == PC_LAYOUT_FUSION && !open && PcHud_Visible();
    Compose(top, topGpu, bottom, bottomGpu, &one, w, h);
    sComposeHud = false;
    sBlitForWindow = false;
    pglPixelStorei(GL_PACK_ALIGNMENT, 1);
    pglReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    /* GL rows run bottom-up: flip into the image. */
    SDL_Surface *surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (surface == NULL)
        return;
    for (int y = 0; y < h; ++y)
        memcpy((u8 *)surface->pixels + y * surface->pitch, pixels + (h - 1 - y) * w * 4, w * 4);
    snprintf(path, sizeof(path), "%s/frame_%06u%s.png", sDumpDir, sFrames, suffix);
    if (!SDL_SavePNG(surface, path))
        fprintf(stderr, "[VIDEO] dump %s: %s\n", path, SDL_GetError());
    SDL_DestroySurface(surface);
}

/* The layout shown; with EMERALD_PC_DUMP_LAYOUTS=1, every layout, swapped or
 * not, each in its own file (frame_N_side_swap.png): one run checks them all. */
static void Dump(GLuint top, bool topGpu, GLuint bottom, bool bottomGpu)
{
    const char *all = getenv("EMERALD_PC_DUMP_LAYOUTS");
    char suffix[32];

    if (!(all && *all && *all != '0'))
    {
        DumpOne(top, topGpu, bottom, bottomGpu, sLayoutKind, sSwap, "");
        return;
    }
    for (int k = 0; k < PC_LAYOUT_COUNT; ++k)
        for (int swap = 0; swap < 2; ++swap)
        {
            snprintf(suffix, sizeof(suffix), "_%s%s", PcLayout_Name((PcLayoutKind)k), swap ? "_swap" : "");
            DumpOne(top, topGpu, bottom, bottomGpu, (PcLayoutKind)k, swap != 0, suffix);
        }
}

void PcDisplay_Present(C3D_RenderTarget *top, C3D_RenderTarget *bottom)
{
    if (sWindow == NULL)
        return;
    ++sFrames;
    sPresented = true;
    sLastTop = top;
    sLastBottom = bottom;
    GLuint topTex = PcC3d_TargetTexture(top);
    bool topGpu = topTex != 0;
    sTopScale = topGpu ? PcC3d_TargetScale(top) : 1;
    if (topTex == 0)
    {
        UploadCpuScreen(sTopCpuTex, sTopFb, 240, 400, sFormat[0]);
        topTex = sTopCpuTex;
    }
    GLuint bottomTex = PcC3d_TargetTexture(bottom);
    bool bottomGpu = bottomTex != 0;
    if (bottomTex == 0)
    {
        UploadCpuScreen(sBottomTex, sBottomFb, 240, 320, sFormat[1]);
        bottomTex = sBottomTex;
    }

    static long sDumpFrom = -1;
    if (sDumpFrom < 0)
    {
        const char *from = getenv("EMERALD_PC_DUMP_FROM");
        sDumpFrom = from && *from ? strtol(from, NULL, 10) : 0;
    }
    /* FUSION's interface: up while the bottom screen is away and the player free. */
    PcHud_Update(sLayoutKind == PC_LAYOUT_FUSION && !(sFusionOpen || CtrBottom_PcWantsShown()) && CtrBottom_PcDockShown(),
                 sPadLast);
    if (sDumpDir && sFrames % sDumpEvery == 0 && (long)sFrames >= sDumpFrom)
    {
        Dump(topTex, topGpu, bottomTex, bottomGpu);
        const char *surfaces = getenv("EMERALD_PC_DUMP_SURFACES");
        if (surfaces && *surfaces && *surfaces != '0')
            PcSurface_DumpAll(sDumpDir, sFrames);
    }
    static long sFrameLimit = -2;
    if (sFrameLimit == -2)
    {
        const char *limit = getenv("EMERALD_PC_FRAMES");
        sFrameLimit = limit != NULL && *limit ? strtol(limit, NULL, 10) : -1;
    }
    if (sFrameLimit > 0 && (long)sFrames >= sFrameLimit)
        PcApt_RequestExit();

    int w, h;
    SDL_GetWindowSizeInPixels(sWindow, &w, &h);
    bool open = sLayoutKind == PC_LAYOUT_FUSION && (sFusionOpen || CtrBottom_PcWantsShown());
    PcLayout_ComputeFull(sLayoutKind, sSwap, !sStretch, open, w, h, &sLayout);
    if (sLayoutKind == PC_LAYOUT_FUSION && !open)
        sLayout.stripShown = false; /* the interface instead of the dock's bar */
    if (sLayout.stripShown && !CtrBottom_PcDockShown())
        sLayout.stripShown = false; /* no dock to show (a battle, the title) */
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    pglViewport(0, 0, w, h);
    sBlitForWindow = true;
    sComposeHud = sLayoutKind == PC_LAYOUT_FUSION && !open && PcHud_Visible();
    Compose(topTex, topGpu, bottomTex, bottomGpu, &sLayout, w, h);
    sComposeHud = false;
    sBlitForWindow = false;
    SDL_GL_SwapWindow(sWindow);
    PcGl_CheckError("present");
    PumpInput();
}
