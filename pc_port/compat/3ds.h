/*
 * libctru-lite: the part of libctru the Emerald 3DS native units use, on a PC.
 *
 * Only the native translation units of 3ds_port/src include <3ds.h>; on the
 * PC build this header answers instead (pc_port/compat comes first on the
 * include path). It covers types, locks, events, threads, the system tick,
 * version queries and the few gfx/console calls the logger's fatal screen
 * makes. GPU (citro2d/citro3d), NDSP, HID and UDS are not here: those units
 * are replaced (pc_port/src) or, for the renderers, served by the citro shim.
 *
 * Implementation: pc_port/src/pc_ctru.c, on POSIX threads (winpthreads under
 * MinGW). License: CC BY-NC-SA 4.0, like the rest of the port.
 */
#ifndef PC_PORT_3DS_H
#define PC_PORT_3DS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "pc_paths.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Types: libctru's own (pc_port/third_party/libctru) ─────────────────── */
#include <3ds/types.h>

#define R_SUCCEEDED(res) ((res) >= 0)
#define R_FAILED(res) ((res) < 0)
#define CUR_THREAD_HANDLE 0xFFFF8000

/* The ARM11 system tick: 268 111 856 Hz. svcGetSystemTick keeps that rate on
 * a PC so every "ticks * 1000 / SYSCLOCK_ARM11" in the port stays right. */
#define SYSCLOCK_ARM11 268111856ULL
#define SYSCLOCK_ARM9 134055928ULL

#define SYSTEM_VERSION(major, minor, revision) (((major) << 24) | ((minor) << 16) | ((revision) << 8))
#define GET_VERSION_MAJOR(version) ((version) >> 24)
#define GET_VERSION_MINOR(version) (((version) >> 16) & 0xFF)
#define GET_VERSION_REVISION(version) (((version) >> 8) & 0xFF)

/* ── Locks and events ───────────────────────────────────────────────────── *
 * LightLock keeps libctru's encoding (1 = free), so `LightLock x = 1;` as a
 * static initialiser stays valid. */
typedef s32 LightLock;
void LightLock_Init(LightLock *lock);
void LightLock_Lock(LightLock *lock);
int LightLock_TryLock(LightLock *lock);
void LightLock_Unlock(LightLock *lock);

typedef struct
{
    LightLock lock;
    u32 thread_tag;
    u32 counter;
} RecursiveLock;
void RecursiveLock_Init(RecursiveLock *lock);
void RecursiveLock_Lock(RecursiveLock *lock);
int RecursiveLock_TryLock(RecursiveLock *lock);
void RecursiveLock_Unlock(RecursiveLock *lock);

typedef enum
{
    RESET_ONESHOT = 0,
    RESET_STICKY = 1,
    RESET_PULSE = 2,
} ResetType;

typedef struct
{
    s32 state;
    LightLock lock;
    void *impl; /* pc_ctru.c: mutex + condition, made by LightEvent_Init */
} LightEvent;
void LightEvent_Init(LightEvent *event, ResetType reset_type);
void LightEvent_Clear(LightEvent *event);
void LightEvent_Signal(LightEvent *event);
void LightEvent_Pulse(LightEvent *event);
int LightEvent_TryWait(LightEvent *event);
void LightEvent_Wait(LightEvent *event);
int LightEvent_WaitTimeout(LightEvent *event, s64 timeout_ns);

/* ── Threads ────────────────────────────────────────────────────────────── */
typedef struct PcThread *Thread;
Thread threadCreate(ThreadFunc entrypoint, void *arg, size_t stack_size, int prio, int core_id, bool detached);
Result threadJoin(Thread thread, u64 timeout_ns);
void threadFree(Thread thread);
void threadDetach(Thread thread);
Thread threadGetCurrent(void);

/* ── Kernel calls kept by the port ──────────────────────────────────────── */
u64 svcGetSystemTick(void);
void svcSleepThread(s64 ns);
Result svcGetThreadPriority(s32 *out, Handle handle);
Result svcOutputDebugString(const char *str, s32 length);
void svcBreak(u32 breakReason);
#define USERBREAK_PANIC 0
#define USERBREAK_ASSERT 1
#define USERBREAK_USER 2

u32 osGetFirmVersion(void);
u32 osGetKernelVersion(void);
float osGet3DSliderState(void);
void osSetSpeedupEnable(bool enable);

Result cfguInit(void);
void cfguExit(void);
Result CFGU_GetSystemModel(u8 *model);
/* CFG_SystemModel: a PC reports itself as a New 3DS XL (the most capable
 * model), so every "New 3DS only" path is taken. */
#define CFG_MODEL_N3DSXL 4

Result romfsInit(void);
Result romfsExit(void);

/* ── GPU-addressable memory ─────────────────────────────────────────────── *
 * On a PC, "linear" and "VRAM" memory are ordinary heap blocks the citro
 * shim (pc_port/src/gl) treats as the PICA's memory: textures, render
 * buffers and screen framebuffers live there in the console's layouts. The
 * free amounts reported are the New 3DS's, so no low-memory path is taken. */
typedef enum
{
    VRAM_ALLOC_A = BIT(0),
    VRAM_ALLOC_B = BIT(1),
    VRAM_ALLOC_ANY = VRAM_ALLOC_A | VRAM_ALLOC_B,
} vramAllocPos;
void *linearAlloc(size_t size);
void *linearMemAlign(size_t size, size_t alignment);
void *linearRealloc(void *mem, size_t size);
size_t linearGetSize(void *mem);
void linearFree(void *mem);
u32 linearSpaceFree(void);
void *vramAlloc(size_t size);
void *vramAllocAt(size_t size, vramAllocPos pos);
void *vramMemAlign(size_t size, size_t alignment);
void *vramMemAlignAt(size_t size, size_t alignment, vramAllocPos pos);
void *vramRealloc(void *mem, size_t size);
size_t vramGetSize(void *mem);
void vramFree(void *mem);
u32 vramSpaceFree(void);
u32 osConvertVirtToPhys(const void *vaddr);
Result GSPGPU_FlushDataCache(const void *adr, u32 size);
Result GSPGPU_InvalidateDataCache(const void *adr, u32 size);
Result APT_CheckNew3DS(bool *out);

#define CUR_PROCESS_HANDLE 0xFFFF8001
Result svcFlushProcessDataCache(Handle process, u32 addr, u32 size);
Result svcInvalidateProcessDataCache(Handle process, u32 addr, u32 size);

/* Screen sizes (libctru gspgpu.h). */
#define GSP_SCREEN_TOP 0
#define GSP_SCREEN_BOTTOM 1
#define GSP_SCREEN_WIDTH 240
#define GSP_SCREEN_HEIGHT_TOP 400
#define GSP_SCREEN_HEIGHT_TOP_2X 800
#define GSP_SCREEN_HEIGHT_BOTTOM 320

/* Names citro3d's and citro2d's headers use in prototypes only: tex3ds.h's
 * decompression callback (libctru util/decompress.h) and the system font's
 * structures (libctru font.h, cfgu.h). The port uses neither. */
typedef ssize_t (*decompressCallback)(void *userdata, void *buffer, size_t size);
typedef struct tag_CFNT_s CFNT_s;
typedef struct tag_FINF_s FINF_s;
typedef struct tag_charWidthInfo_s charWidthInfo_s;
typedef struct tag_fontGlyphPos_s fontGlyphPos_s;
typedef enum { CFG_REGION_JPN = 0, CFG_REGION_USA = 1, CFG_REGION_EUR = 2 } CFG_Region;

/* The GPU's headers (libctru): enums, registers, GX transfer flags, shaders. */
#include <3ds/gpu/enums.h>
#include <3ds/gpu/gpu.h>
#include <3ds/gpu/gx.h>
#include <3ds/gpu/shaderProgram.h>

/* ── NDSP: the DSP's sound channels (libctru's headers; pc_ndsp.c on SDL3) ── */
#include <3ds/ndsp/ndsp.h>
#include <3ds/ndsp/channel.h>
Result DSP_FlushDataCache(const void *address, u32 size);
Result DSP_InvalidateDataCache(const void *address, u32 size);

/* ── APT: the application's life cycle ──────────────────────────────────── *
 * aptMainLoop() turns false once the PC asks the game to quit (window closed,
 * Ctrl+C): PcApt_RequestExit. HOME does not exist on a PC. */
typedef enum
{
    APTHOOK_ONSUSPEND = 0,
    APTHOOK_ONRESTORE,
    APTHOOK_ONSLEEP,
    APTHOOK_ONWAKEUP,
    APTHOOK_ONEXIT,
    APTHOOK_COUNT,
} APT_HookType;
typedef void (*aptHookFn)(APT_HookType hook, void *param);
typedef struct tag_aptHookCookie
{
    struct tag_aptHookCookie *next;
    aptHookFn callback;
    void *param;
} aptHookCookie;
bool aptMainLoop(void);
void aptHook(aptHookCookie *cookie, aptHookFn callback, void *param);
void aptUnhook(aptHookCookie *cookie);
void aptSetHomeAllowed(bool allowed);
bool aptIsHomeAllowed(void);
bool aptCheckHomePressRejected(void);
Result APT_SetAppCpuTimeLimit(u32 percent);
Result APT_GetAppCpuTimeLimit(u32 *percent);
/* PC side: ask aptMainLoop to end; runs the ONEXIT hooks once. */
void PcApt_RequestExit(void);

/* ── HID: buttons, touch, circle pad ────────────────────────────────────── *
 * libctru's bit values. The state comes from PcHid_SetState, which the PC
 * input layer (SDL, from J2) calls once per frame before hidScanInput. */
enum
{
    KEY_A = BIT(0),
    KEY_B = BIT(1),
    KEY_SELECT = BIT(2),
    KEY_START = BIT(3),
    KEY_DRIGHT = BIT(4),
    KEY_DLEFT = BIT(5),
    KEY_DUP = BIT(6),
    KEY_DDOWN = BIT(7),
    KEY_R = BIT(8),
    KEY_L = BIT(9),
    KEY_X = BIT(10),
    KEY_Y = BIT(11),
    KEY_ZL = BIT(14),
    KEY_ZR = BIT(15),
    KEY_TOUCH = BIT(20),
    KEY_CSTICK_RIGHT = BIT(24),
    KEY_CSTICK_LEFT = BIT(25),
    KEY_CSTICK_UP = BIT(26),
    KEY_CSTICK_DOWN = BIT(27),
    KEY_CPAD_RIGHT = BIT(28),
    KEY_CPAD_LEFT = BIT(29),
    KEY_CPAD_UP = BIT(30),
    KEY_CPAD_DOWN = BIT(31),
};
typedef struct
{
    u16 px, py;
} touchPosition;
typedef struct
{
    s16 dx, dy;
} circlePosition;
void hidScanInput(void);
u32 hidKeysHeld(void);
u32 hidKeysDown(void);
u32 hidKeysUp(void);
void hidTouchRead(touchPosition *pos);
void hidCircleRead(circlePosition *pos);
void PcHid_SetState(u32 held, touchPosition touch, circlePosition circle);

/* ── gfx / console: only the logger's fatal screen uses them ────────────── */
typedef enum { GFX_TOP = 0, GFX_BOTTOM = 1 } gfxScreen_t;
typedef enum { GFX_LEFT = 0, GFX_RIGHT = 1 } gfx3dSide_t;
typedef enum
{
    GSP_RGBA8_OES = 0,
    GSP_BGR8_OES = 1,
    GSP_RGB565_OES = 2,
    GSP_RGB5_A1_OES = 3,
    GSP_RGBA4_OES = 4,
} GSPGPU_FramebufferFormat;

GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t screen);
bool gfxIsWide(void);
void gfxSetWide(bool enable);
void gfxSet3D(bool enable);
bool gfxIs3D(void);
void gfxInit(GSPGPU_FramebufferFormat topFormat, GSPGPU_FramebufferFormat bottomFormat, bool vrambuffers);
void gfxInitDefault(void);
void gfxExit(void);
void gfxSetScreenFormat(gfxScreen_t screen, GSPGPU_FramebufferFormat format);
void gfxSetDoubleBuffering(gfxScreen_t screen, bool enable);
u8 *gfxGetFramebuffer(gfxScreen_t screen, gfx3dSide_t side, u16 *width, u16 *height);
void gfxFlushBuffers(void);
void gfxSwapBuffers(void);
void gspWaitForVBlank(void);

typedef struct
{
    int consoleWidth, consoleHeight;
} PrintConsole;
PrintConsole *consoleInit(gfxScreen_t screen, PrintConsole *console);
PrintConsole *consoleSelect(PrintConsole *console);
void consoleSetWindow(PrintConsole *console, int x, int y, int width, int height);
void consoleClear(void);

#ifdef __cplusplus
}
#endif

#endif
