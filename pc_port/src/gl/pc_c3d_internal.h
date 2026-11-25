/*
 * The citro shim's internals: citro3d on OpenGL 3.3.
 *
 * Model. The PICA200 reads and writes memory: textures, colour and depth
 * buffers are blocks of "linear"/"VRAM" memory in the GPU's own layouts
 * (8x8 tiles in Morton order, PICA pixel formats). The port's code relies on
 * that (it writes texels, copies render buffers with GX transfers, fills them
 * with GX_MemoryFill), so the shim keeps those blocks in CPU memory, in the
 * console's layouts, and treats OpenGL as the rasteriser only.
 *
 * Every such block is a surface with a GL texture mirroring it. Two flags
 * say which copy is newer:
 *   cpuNewer: the memory changed (texel writes, copies, fills) - decoded and
 *             uploaded to GL before GL reads it;
 *   gpuNewer: GL drew into it - read back and encoded into memory before
 *             anything on the CPU side (a transfer, a copy) reads it.
 * Texture memory written by the CPU is also detected by a hash, checked once
 * a frame and after cache flushes, as the console's code writes texels and
 * relies on the frame's flush.
 *
 * Coordinates. A surface's memory row y (tiles counted from the start of the
 * block) is GL row y of its texture; draws into a render buffer use GL's
 * window coordinates on the same rows. The screens' orientation is handled
 * once, when the frame is shown (pc_display.c).
 */
#ifndef PC_C3D_INTERNAL_H
#define PC_C3D_INTERNAL_H

#include <citro3d.h>

#include "pc_gl.h"

enum PcSurfaceKind
{
    PC_SURFACE_TEXTURE, /* fmt: GPU_TEXCOLOR */
    PC_SURFACE_COLOR,   /* fmt: GPU_COLORBUF */
    PC_SURFACE_DEPTH,   /* fmt: GPU_DEPTHBUF */
};

typedef struct PcSurface
{
    u8 *mem;
    u32 bytes;
    u16 width, height;
    u8 kind;
    u8 fmt;
    bool cpuNewer, gpuNewer;
    bool hashChecked; /* this frame / since the last flush */
    u64 hash;
    GLuint tex;  /* RGBA8 mirror (colour and texture surfaces) */
    GLuint fbo;  /* when drawn into */
    GLuint depthRb;
    struct PcSurface *depth; /* the depth surface attached to this colour one */
    struct PcSurface *next;
} PcSurface;

/* Surfaces: registered when the shim allocates GPU memory for them. */
PcSurface *PcSurface_Create(void *mem, u32 bytes, u16 width, u16 height, int kind, int fmt);
void PcSurface_Destroy(PcSurface *s);
PcSurface *PcSurface_Find(const void *mem);       /* exact start */
PcSurface *PcSurface_Containing(const void *addr); /* any byte inside */
/* GL's copy up to date (decode + upload when the memory is newer). */
void PcSurface_SyncToGl(PcSurface *s);
/* Memory up to date (read back + encode when GL is newer). */
void PcSurface_SyncToMemory(PcSurface *s);
/* The memory range [addr, addr+size) was written by the CPU side. */
void PcSurface_MarkCpuWrite(const void *addr, u32 size);
/* Every texture's hash is checked again at its next use. */
void PcSurface_InvalidateHashes(void);
/* Debug: every colour/texture surface saved as PNG into dir. */
void PcSurface_DumpAll(const char *dir, unsigned frame);
/* An FBO for drawing into a colour surface (+ its depth surface). */
GLuint PcSurface_Framebuffer(PcSurface *color, PcSurface *depth);

/* Pixel formats: bytes per pixel, decode to / encode from RGBA8 (R,G,B,A
 * bytes), Morton index of a texel inside a tiled block. */
u32 PcFormat_TexBpp4(int texFmt);   /* bits per pixel */
u32 PcFormat_ColorBytes(int colorFmt);
u32 PcFormat_TiledOffset(u32 x, u32 y, u32 width); /* in pixels */
void PcFormat_DecodeTexture(const u8 *mem, u32 w, u32 h, int fmt, u8 *rgba);
void PcFormat_DecodeColor(const u8 *mem, u32 w, u32 h, int fmt, u8 *rgba);
void PcFormat_EncodeColor(const u8 *rgba, u32 w, u32 h, int fmt, u8 *mem);

/* Programs: the 3DS shaders the port uses, each rewritten in GLSL. */
enum PcProgramKind
{
    PC_PROGRAM_NONE,
    PC_PROGRAM_RENDER2D, /* citro2d's render2d.v.pica */
    PC_PROGRAM_VOXEL,    /* 3ds_port voxel.v.pica (J5) */
    PC_PROGRAM_WEATHER,  /* 3ds_port voxel_weather.v.pica (J5) */
    PC_PROGRAM_COUNT,
};
int PcProgram_KindOf(const shaderProgram_s *program);
GLuint PcProgram_Gl(int kind);
int PcProgram_UniformRegister(int kind, const char *name);

/* The window and the screens (pc_display.c). */
bool PcDisplay_Init(void);
void PcDisplay_Shutdown(void);
/* Waits for the next VBlank of the console's display clock (59.8261 Hz). */
void PcDisplay_WaitVBlank(void);
/* A render target about to be freed: no longer shown again by the LCD. */
void PcDisplay_ForgetTarget(C3D_RenderTarget *target);
/* Shows the frame: the top screen's linked render targets and the bottom
 * screen (its target, or the CPU framebuffer). Pumps input. */
void PcDisplay_Present(C3D_RenderTarget *top, C3D_RenderTarget *bottom);

/* Draw-state application (pc_c3d.c). */
void PcC3d_ApplyTargetState(void);

#endif
