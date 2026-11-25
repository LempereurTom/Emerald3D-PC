/*
 * The citro shim's memory: GPU allocations, surfaces, PICA pixel formats
 * and the GX transfer engine (display transfer, texture copy, memory fill).
 * See pc_c3d_internal.h for the model.
 */
#include "pc_c3d_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Allocations ────────────────────────────────────────────────────────── *
 * A small header before each block keeps its size and how it was made. The
 * process is 32-bit: a pointer is its own "physical address".
 *
 * Blocks of a page or more are made with VirtualAlloc(MEM_WRITE_WATCH) on
 * Windows: the system then tells which pages the CPU wrote since the last
 * look (GetWriteWatch), which is how the shim learns that the port wrote
 * texels without hashing megabytes per check. Smaller blocks are malloc'd and
 * hashed. */

#ifdef _WIN32
#include <windows.h>
#endif

typedef struct
{
    void *base;
    size_t size;
    u32 magic;
    u32 watched; /* VirtualAlloc'd with MEM_WRITE_WATCH */
} BlockHeader;

#define BLOCK_MAGIC 0x50434D45u /* "PCME" */
#define WATCH_MIN 4096u

static size_t sLinearUsed, sVramUsed;

static void *AlignedAlloc(size_t size, size_t align, size_t *counter)
{
    if (align < 16)
        align = 16;
    u8 *base = NULL;
    u32 watched = 0;
#ifdef _WIN32
    if (size >= WATCH_MIN && align <= 4096)
    {
        /* The header in a page of its own before the data: the data starts
         * on a page boundary, its pages are its own. */
        base = VirtualAlloc(NULL, size + 4096, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
        watched = base != NULL;
    }
#endif
    if (base == NULL)
        base = malloc(size + align + sizeof(BlockHeader));
    if (base == NULL)
        return NULL;
    uintptr_t start = watched ? (uintptr_t)base + 4096
                              : ((uintptr_t)base + sizeof(BlockHeader) + align - 1) & ~(uintptr_t)(align - 1);
    BlockHeader *h = (BlockHeader *)start - 1;
    h->base = base;
    h->size = size;
    h->magic = BLOCK_MAGIC;
    h->watched = watched;
    *counter += size;
    if (!watched)
        memset((void *)start, 0, size); /* VirtualAlloc's pages are zero already */
    return (void *)start;
}

static BlockHeader *HeaderOf(const void *mem)
{
    BlockHeader *h = (BlockHeader *)mem - 1;
    return h->magic == BLOCK_MAGIC ? h : NULL;
}

void PcC3d_FlushPending(void);

static void AlignedFree(void *mem, size_t *counter)
{
    BlockHeader *h;
    if (mem == NULL || (h = HeaderOf(mem)) == NULL)
        return;
    /* Draws not yet run may read this block (vertices, textures). */
    PcC3d_FlushPending();
    PcSurface *s = PcSurface_Find(mem);
    if (s != NULL)
        PcSurface_Destroy(s);
    *counter -= h->size;
    h->magic = 0;
#ifdef _WIN32
    if (h->watched)
    {
        VirtualFree(h->base, 0, MEM_RELEASE);
        return;
    }
#endif
    free(h->base);
}

/* Whether the CPU wrote [mem, mem+bytes) since the last call (and forget
 * it). -1: not known for this block (not watched). */
static int WriteWatchConsume(const void *mem, u32 bytes)
{
#ifdef _WIN32
    BlockHeader *h = HeaderOf(mem);
    if (h == NULL || !h->watched || (const u8 *)mem != (const u8 *)h->base + 4096)
        return -1;
    static PVOID pages[4096];
    ULONG_PTR count = sizeof(pages) / sizeof(pages[0]);
    DWORD granularity;
    if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, (PVOID)mem, bytes, pages, &count, &granularity) != 0)
        return -1;
    return count > 0;
#else
    (void)mem;
    (void)bytes;
    return -1;
#endif
}

static bool IsWatched(const void *mem)
{
    BlockHeader *h = HeaderOf(mem);
    return h != NULL && h->watched && (const u8 *)mem == (const u8 *)h->base + 4096;
}

/* Forget writes made so far (the shim's own, when it encodes into memory). */
static void WriteWatchReset(const void *mem, u32 bytes)
{
#ifdef _WIN32
    BlockHeader *h = HeaderOf(mem);
    if (h != NULL && h->watched && (const u8 *)mem == (const u8 *)h->base + 4096)
        ResetWriteWatch((PVOID)mem, bytes);
#else
    (void)mem;
    (void)bytes;
#endif
}

static void *AlignedRealloc(void *mem, size_t size, size_t *counter)
{
    BlockHeader *h;
    if (mem == NULL)
        return AlignedAlloc(size, 16, counter);
    if ((h = HeaderOf(mem)) == NULL)
        return NULL;
    void *fresh = AlignedAlloc(size, 16, counter);
    if (fresh != NULL)
    {
        memcpy(fresh, mem, h->size < size ? h->size : size);
        AlignedFree(mem, counter);
    }
    return fresh;
}

void *linearAlloc(size_t size) { return AlignedAlloc(size, 0x80, &sLinearUsed); }
void *linearMemAlign(size_t size, size_t alignment) { return AlignedAlloc(size, alignment, &sLinearUsed); }
void *linearRealloc(void *mem, size_t size) { return AlignedRealloc(mem, size, &sLinearUsed); }
size_t linearGetSize(void *mem) { BlockHeader *h = mem ? HeaderOf(mem) : NULL; return h ? h->size : 0; }
void linearFree(void *mem) { AlignedFree(mem, &sLinearUsed); }
u32 linearSpaceFree(void) { return 64u * 1024 * 1024; }

void *vramAlloc(size_t size) { return AlignedAlloc(size, 0x80, &sVramUsed); }
void *vramAllocAt(size_t size, vramAllocPos pos) { (void)pos; return AlignedAlloc(size, 0x80, &sVramUsed); }
void *vramMemAlign(size_t size, size_t alignment) { return AlignedAlloc(size, alignment, &sVramUsed); }
void *vramMemAlignAt(size_t size, size_t alignment, vramAllocPos pos) { (void)pos; return AlignedAlloc(size, alignment, &sVramUsed); }
void *vramRealloc(void *mem, size_t size) { return AlignedRealloc(mem, size, &sVramUsed); }
size_t vramGetSize(void *mem) { BlockHeader *h = mem ? HeaderOf(mem) : NULL; return h ? h->size : 0; }
void vramFree(void *mem) { AlignedFree(mem, &sVramUsed); }
u32 vramSpaceFree(void) { return 6u * 1024 * 1024; }

u32 osConvertVirtToPhys(const void *vaddr) { return (u32)(uintptr_t)vaddr; }

/* The linear heap's bounds (libctru): the video flushes "the whole heap"
 * with them, which here is every surface. */
u32 __ctru_linear_heap = 0x1000;
u32 __ctru_linear_heap_size = 0xFFFFE000u;

/* The largest free VRAM block (3ds_gpumem.c on the console). */
u32 CtrGpuMem_LargestVram(void) { return 6u * 1024 * 1024; }

/* Raw PICA register writes: the video only writes texture-cache and frame
 * buffer flushes (GPUREG_TEXUNIT_CONFIG bit 16, FRAMEBUFFER_FLUSH and
 * _INVALIDATE), which GL needs not: every draw sees the ones before it. */
void GPUCMD_Add(u32 header, const u32 *param, u32 paramlength)
{
    (void)header; (void)param; (void)paramlength;
}

Result APT_CheckNew3DS(bool *out)
{
    if (out)
        *out = true;
    return 0;
}

/* ── Pixel formats ──────────────────────────────────────────────────────── */

u32 PcFormat_TexBpp4(int fmt)
{
    switch (fmt)
    {
    case GPU_RGBA8: return 32;
    case GPU_RGB8: return 24;
    case GPU_RGBA5551: case GPU_RGB565: case GPU_RGBA4: case GPU_LA8: case GPU_HILO8: return 16;
    case GPU_L8: case GPU_A8: case GPU_LA4: case GPU_ETC1A4: return 8;
    default: return 4; /* L4, A4, ETC1 */
    }
}

u32 PcFormat_ColorBytes(int fmt)
{
    switch (fmt)
    {
    case GPU_RB_RGBA8: return 4;
    case GPU_RB_RGB8: return 3;
    default: return 2;
    }
}

/* A colour buffer format as the texture format with the same encoding. */
static int ColorToTex(int colorFmt)
{
    switch (colorFmt)
    {
    case GPU_RB_RGBA8: return GPU_RGBA8;
    case GPU_RB_RGB8: return GPU_RGB8;
    case GPU_RB_RGBA5551: return GPU_RGBA5551;
    case GPU_RB_RGB565: return GPU_RGB565;
    default: return GPU_RGBA4;
    }
}

static inline u32 Morton(u32 x, u32 y)
{
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

u32 PcFormat_TiledOffset(u32 x, u32 y, u32 width)
{
    return ((y >> 3) * (width >> 3) + (x >> 3)) * 64 + Morton(x & 7, y & 7);
}

static inline u8 Expand5(u32 v) { return (u8)((v << 3) | (v >> 2)); }
static inline u8 Expand6(u32 v) { return (u8)((v << 2) | (v >> 4)); }
static inline u8 Expand4(u32 v) { return (u8)((v << 4) | v); }

static void DecodeTexel(const u8 *mem, u32 index, int fmt, u8 *out)
{
    u32 v;
    switch (fmt)
    {
    case GPU_RGBA8:
        mem += index * 4;
        out[0] = mem[3]; out[1] = mem[2]; out[2] = mem[1]; out[3] = mem[0];
        break;
    case GPU_RGB8:
        mem += index * 3;
        out[0] = mem[2]; out[1] = mem[1]; out[2] = mem[0]; out[3] = 255;
        break;
    case GPU_RGBA5551:
        v = mem[index * 2] | (mem[index * 2 + 1] << 8);
        out[0] = Expand5((v >> 11) & 31); out[1] = Expand5((v >> 6) & 31); out[2] = Expand5((v >> 1) & 31);
        out[3] = (v & 1) ? 255 : 0;
        break;
    case GPU_RGB565:
        v = mem[index * 2] | (mem[index * 2 + 1] << 8);
        out[0] = Expand5((v >> 11) & 31); out[1] = Expand6((v >> 5) & 63); out[2] = Expand5(v & 31); out[3] = 255;
        break;
    case GPU_RGBA4:
        v = mem[index * 2] | (mem[index * 2 + 1] << 8);
        out[0] = Expand4((v >> 12) & 15); out[1] = Expand4((v >> 8) & 15); out[2] = Expand4((v >> 4) & 15);
        out[3] = Expand4(v & 15);
        break;
    case GPU_LA8:
        out[0] = out[1] = out[2] = mem[index * 2 + 1]; out[3] = mem[index * 2];
        break;
    case GPU_HILO8:
        out[0] = mem[index * 2 + 1]; out[1] = mem[index * 2]; out[2] = 0; out[3] = 255;
        break;
    case GPU_L8:
        out[0] = out[1] = out[2] = mem[index]; out[3] = 255;
        break;
    case GPU_A8:
        out[0] = out[1] = out[2] = 0; out[3] = mem[index];
        break;
    case GPU_LA4:
        v = mem[index];
        out[0] = out[1] = out[2] = Expand4(v >> 4); out[3] = Expand4(v & 15);
        break;
    case GPU_L4:
        v = (mem[index / 2] >> ((index & 1) * 4)) & 15;
        out[0] = out[1] = out[2] = Expand4(v); out[3] = 255;
        break;
    case GPU_A4:
        v = (mem[index / 2] >> ((index & 1) * 4)) & 15;
        out[0] = out[1] = out[2] = 0; out[3] = Expand4(v);
        break;
    default: /* ETC1: not used by the port; shown magenta so it is noticed */
        out[0] = 255; out[1] = 0; out[2] = 255; out[3] = 255;
        break;
    }
}

static void EncodeTexel(const u8 *in, u32 index, int fmt, u8 *mem)
{
    u32 v;
    switch (fmt)
    {
    case GPU_RGBA8:
        mem += index * 4;
        mem[0] = in[3]; mem[1] = in[2]; mem[2] = in[1]; mem[3] = in[0];
        break;
    case GPU_RGB8:
        mem += index * 3;
        mem[0] = in[2]; mem[1] = in[1]; mem[2] = in[0];
        break;
    case GPU_RGBA5551:
        v = ((in[0] >> 3) << 11) | ((in[1] >> 3) << 6) | ((in[2] >> 3) << 1) | (in[3] >= 128 ? 1 : 0);
        mem[index * 2] = (u8)v; mem[index * 2 + 1] = (u8)(v >> 8);
        break;
    case GPU_RGB565:
        v = ((in[0] >> 3) << 11) | ((in[1] >> 2) << 5) | (in[2] >> 3);
        mem[index * 2] = (u8)v; mem[index * 2 + 1] = (u8)(v >> 8);
        break;
    default: /* RGBA4 */
        v = ((in[0] >> 4) << 12) | ((in[1] >> 4) << 8) | ((in[2] >> 4) << 4) | (in[3] >> 4);
        mem[index * 2] = (u8)v; mem[index * 2 + 1] = (u8)(v >> 8);
        break;
    }
}

/* The PICA stores a surface top row first: memory row y is texture
 * coordinate t = 1 - y/h, which is GL row h-1-y (GL's row 0 is t = 0). The
 * rasteriser agrees (window y up, as GL's), so drawing into a GL texture and
 * sampling it behave as on the console once rows are flipped here, where
 * memory and GL meet. Everything that works on memory (GX transfers, the
 * port's texel writes) keeps the console's row order. */
void PcFormat_DecodeTexture(const u8 *mem, u32 w, u32 h, int fmt, u8 *rgba)
{
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x)
            DecodeTexel(mem, PcFormat_TiledOffset(x, y, w), fmt, rgba + ((h - 1 - y) * w + x) * 4);
}

void PcFormat_DecodeColor(const u8 *mem, u32 w, u32 h, int fmt, u8 *rgba)
{
    PcFormat_DecodeTexture(mem, w, h, ColorToTex(fmt), rgba);
}

void PcFormat_EncodeColor(const u8 *rgba, u32 w, u32 h, int fmt, u8 *mem)
{
    int tfmt = ColorToTex(fmt);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x)
            EncodeTexel(rgba + ((h - 1 - y) * w + x) * 4, PcFormat_TiledOffset(x, y, w), tfmt, mem);
}

/* ── Surfaces ───────────────────────────────────────────────────────────── */

static PcSurface *sSurfaces;
static u8 *sScratch;
static size_t sScratchSize;

static u8 *Scratch(size_t size)
{
    if (size > sScratchSize)
    {
        free(sScratch);
        sScratch = malloc(size);
        sScratchSize = sScratch ? size : 0;
    }
    return sScratch;
}

PcSurface *PcSurface_Create(void *mem, u32 bytes, u16 width, u16 height, int kind, int fmt)
{
    PcSurface *s = PcSurface_Find(mem);
    if (s != NULL)
    {
        if (s->kind == kind && s->width == width && s->height == height && s->fmt == fmt)
            return s;
        PcSurface_Destroy(s);
    }
    s = calloc(1, sizeof(*s));
    if (s == NULL)
        return NULL;
    s->mem = mem;
    s->bytes = bytes;
    s->width = width;
    s->height = height;
    s->kind = (u8)kind;
    s->fmt = (u8)fmt;
    s->cpuNewer = true;
    if (kind != PC_SURFACE_DEPTH)
    {
        pglGenTextures(1, &s->tex);
        pglBindTexture(GL_TEXTURE_2D, s->tex);
        pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    }
    s->next = sSurfaces;
    sSurfaces = s;
    return s;
}

void PcSurface_Destroy(PcSurface *s)
{
    for (PcSurface **p = &sSurfaces; *p != NULL; p = &(*p)->next)
        if (*p == s)
        {
            *p = s->next;
            break;
        }
    for (PcSurface *o = sSurfaces; o != NULL; o = o->next)
        if (o->depth == s)
        {
            o->depth = NULL;
            if (o->fbo)
            {
                pglDeleteFramebuffers(1, &o->fbo);
                o->fbo = 0;
            }
        }
    if (s->fbo)
        pglDeleteFramebuffers(1, &s->fbo);
    if (s->depthRb)
        pglDeleteRenderbuffers(1, &s->depthRb);
    if (s->tex)
        pglDeleteTextures(1, &s->tex);
    free(s);
}

PcSurface *PcSurface_Find(const void *mem)
{
    for (PcSurface *s = sSurfaces; s != NULL; s = s->next)
        if (s->mem == mem)
            return s;
    return NULL;
}

PcSurface *PcSurface_Containing(const void *addr)
{
    const u8 *a = addr;
    for (PcSurface *s = sSurfaces; s != NULL; s = s->next)
        if (a >= s->mem && a < s->mem + s->bytes)
            return s;
    return NULL;
}

static u64 Hash(const u8 *mem, u32 bytes)
{
    u64 h = 0xcbf29ce484222325ull;
    const u32 *w = (const u32 *)mem;
    u32 words = bytes / 4;
    for (u32 i = 0; i < words; ++i)
        h = (h ^ w[i]) * 0x100000001b3ull;
    for (u32 i = words * 4; i < bytes; ++i)
        h = (h ^ mem[i]) * 0x100000001b3ull;
    return h;
}

void PcSurface_InvalidateHashes(void)
{
    for (PcSurface *s = sSurfaces; s != NULL; s = s->next)
        s->hashChecked = false;
}

void PcSurface_MarkCpuWrite(const void *addr, u32 size)
{
    const u8 *a = addr;
    for (PcSurface *s = sSurfaces; s != NULL; s = s->next)
        if (a < s->mem + s->bytes && a + size > s->mem)
        {
            s->cpuNewer = true;
            s->gpuNewer = false;
        }
}

static void FlushRange(const void *addr, u32 size)
{
    const u8 *a = addr;
    for (PcSurface *s = sSurfaces; s != NULL; s = s->next)
        if (a < s->mem + s->bytes && a + size > s->mem)
            s->hashChecked = false;
}

void PcSurface_SyncToGl(PcSurface *s)
{
    if (s == NULL || s->kind == PC_SURFACE_DEPTH)
        return;
    if (!s->cpuNewer && !s->gpuNewer && !s->hashChecked)
    {
        /* Texel writes the console's code leaves to the GPU's reading the
         * memory when it runs: the write watch says so, or a hash. */
        int written = WriteWatchConsume(s->mem, s->bytes);
        if (written < 0 && s->kind == PC_SURFACE_TEXTURE)
            written = Hash(s->mem, s->bytes) != s->hash;
        s->hashChecked = true;
        if (written > 0)
            s->cpuNewer = true;
    }
    if (!s->cpuNewer)
        return;
    WriteWatchReset(s->mem, s->bytes);
    u8 *rgba = Scratch((size_t)s->width * s->height * 4);
    if (rgba == NULL)
        return;
    if (s->kind == PC_SURFACE_TEXTURE)
        PcFormat_DecodeTexture(s->mem, s->width, s->height, s->fmt, rgba);
    else
        PcFormat_DecodeColor(s->mem, s->width, s->height, s->fmt, rgba);
    pglBindTexture(GL_TEXTURE_2D, s->tex);
    pglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    pglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s->width, s->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    s->cpuNewer = false;
    if (!IsWatched(s->mem))
        s->hash = Hash(s->mem, s->bytes);
    s->hashChecked = true;
}

void PcSurface_SyncToMemory(PcSurface *s)
{
    if (s == NULL || !s->gpuNewer || s->kind == PC_SURFACE_DEPTH)
        return;
    u8 *rgba = Scratch((size_t)s->width * s->height * 4);
    if (rgba == NULL)
        return;
    GLuint fbo = PcSurface_Framebuffer(s, s->depth);
    pglBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    pglPixelStorei(GL_PACK_ALIGNMENT, 1);
    pglReadPixels(0, 0, s->width, s->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    pglBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    if (s->kind == PC_SURFACE_COLOR)
        PcFormat_EncodeColor(rgba, s->width, s->height, s->fmt, s->mem);
    else
    {
        int fmt = s->fmt;
        /* A texture drawn into: its colour-buffer twin encodes it. */
        int colorFmt = fmt == GPU_RGBA8 ? GPU_RB_RGBA8 : fmt == GPU_RGB8 ? GPU_RB_RGB8
                     : fmt == GPU_RGBA5551 ? GPU_RB_RGBA5551 : fmt == GPU_RGB565 ? GPU_RB_RGB565 : GPU_RB_RGBA4;
        PcFormat_EncodeColor(rgba, s->width, s->height, colorFmt, s->mem);
    }
    s->gpuNewer = false;
    WriteWatchReset(s->mem, s->bytes);
    if (!IsWatched(s->mem))
        s->hash = Hash(s->mem, s->bytes);
    s->hashChecked = true;
}

GLuint PcSurface_Framebuffer(PcSurface *color, PcSurface *depth)
{
    if (color->fbo && color->depth == depth)
        return color->fbo;
    if (color->fbo)
        pglDeleteFramebuffers(1, &color->fbo);
    pglGenFramebuffers(1, &color->fbo);
    pglBindFramebuffer(GL_FRAMEBUFFER, color->fbo);
    pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color->tex, 0);
    color->depth = depth;
    if (depth != NULL)
    {
        if (!depth->depthRb)
        {
            pglGenRenderbuffers(1, &depth->depthRb);
            pglBindRenderbuffer(GL_RENDERBUFFER, depth->depthRb);
            pglRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, color->width, color->height);
        }
        pglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth->depthRb);
    }
    GLenum status = pglCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "[VIDEO] framebuffer %ux%u incomplete: 0x%04X\n", color->width, color->height, status);
    return color->fbo;
}

/* Debug: every colour/texture surface as it is in GL, as PNG files
 * (dir/surface_<frame>_<address>_<w>x<h>_<kind><fmt>.png, image rows top
 * down = GL rows bottom up, so a surface looks as a texture is sampled). */
void PcSurface_DumpAll(const char *dir, unsigned frame)
{
    PcC3d_FlushPending();
    char path[1024];
    for (PcSurface *s = sSurfaces; s != NULL; s = s->next)
    {
        if (s->kind == PC_SURFACE_DEPTH || s->width * s->height < 32 * 32)
            continue;
        PcSurface_SyncToGl(s);
        u8 *rgba = Scratch((size_t)s->width * s->height * 4);
        if (rgba == NULL)
            return;
        GLuint fbo = PcSurface_Framebuffer(s, s->depth);
        pglBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        pglPixelStorei(GL_PACK_ALIGNMENT, 1);
        pglReadPixels(0, 0, s->width, s->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        pglBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        SDL_Surface *img = SDL_CreateSurface(s->width, s->height, SDL_PIXELFORMAT_RGBA32);
        if (img == NULL)
            continue;
        for (u32 y = 0; y < s->height; ++y)
        {
            u8 *row = (u8 *)img->pixels + y * img->pitch;
            memcpy(row, rgba + (s->height - 1 - y) * s->width * 4, s->width * 4);
            for (u32 x = 0; x < s->width; ++x)
                row[x * 4 + 3] = 255; /* alpha shown as opaque */
        }
        snprintf(path, sizeof(path), "%s/surface_%06u_%08x_%ux%u_%c%u%s%s.png", dir, frame, (u32)(uintptr_t)s->mem,
                 s->width, s->height, s->kind == PC_SURFACE_TEXTURE ? 't' : 'c', s->fmt, s->gpuNewer ? "_gpu" : "",
                 s->cpuNewer ? "_cpu" : "");
        SDL_SavePNG(img, path);
        SDL_DestroySurface(img);
    }
    PcC3d_ApplyTargetState();
}

/* ── GX: the transfer engine ────────────────────────────────────────────── */

/* GX_TRANSFER_FMT_* -> GPU_COLORBUF (the two enumerations differ). */
static int GxToColor(u32 gx)
{
    static const int map[] = { GPU_RB_RGBA8, GPU_RB_RGB8, GPU_RB_RGB565, GPU_RB_RGBA5551, GPU_RB_RGBA4 };
    return gx < 5 ? map[gx] : GPU_RB_RGBA8;
}

static void ReadBack(const void *addr)
{
    PcSurface *s = PcSurface_Containing(addr);
    if (s != NULL)
        PcSurface_SyncToMemory(s);
}

/* Before the CPU side writes [addr, addr+size): a surface the GPU drew into
 * and that is only partly overwritten gets its drawing into memory first. */
static void PrepareWrite(const void *addr, u32 size)
{
    PcSurface *s = PcSurface_Containing(addr);
    if (s != NULL && s->gpuNewer && !((const u8 *)addr == s->mem && size >= s->bytes))
        PcSurface_SyncToMemory(s);
}

Result GX_DisplayTransfer(u32 *inadr, u32 indim, u32 *outadr, u32 outdim, u32 flags)
{
    PcC3d_FlushPending();
    u32 inW = indim & 0xFFFF, inH = indim >> 16, outW = outdim & 0xFFFF, outH = outdim >> 16;
    int inFmt = GxToColor((flags >> 8) & 7), outFmt = GxToColor((flags >> 12) & 7);
    bool flip = flags & 1, toTiled = (flags >> 1) & 1, raw = (flags >> 3) & 1;
    u32 scale = (flags >> 24) & 3;
    u32 inBpp = PcFormat_ColorBytes(inFmt), outBpp = PcFormat_ColorBytes(outFmt);

    if (inW == 0 || inH == 0 || outW == 0 || outH == 0)
        return 0;
    ReadBack(inadr);
    PrepareWrite(outadr, outW * outH * PcFormat_ColorBytes(outFmt));
    if (raw)
    {
        u32 bytes = inW * inH * inBpp;
        memmove(outadr, inadr, bytes);
        PcSurface_MarkCpuWrite(outadr, bytes);
        return 0;
    }
    u8 *rgba = malloc((size_t)inW * inH * 4);
    if (rgba == NULL)
        return -1;
    const u8 *in = (const u8 *)inadr;
    int tfmt = inFmt == GPU_RB_RGBA8 ? GPU_RGBA8 : inFmt == GPU_RB_RGB8 ? GPU_RGB8
             : inFmt == GPU_RB_RGB565 ? GPU_RGB565 : inFmt == GPU_RB_RGBA5551 ? GPU_RGBA5551 : GPU_RGBA4;
    for (u32 y = 0; y < inH; ++y)
        for (u32 x = 0; x < inW; ++x)
        {
            u32 index = toTiled ? y * inW + x : PcFormat_TiledOffset(x, y, inW);
            DecodeTexel(in, index, tfmt, rgba + (y * inW + x) * 4);
        }
    /* Scaling: 1 = 2x1 (horizontal), 2 = 2x2 downscale, averaged. */
    u32 sx = scale >= 1 ? 2 : 1, sy = scale == 2 ? 2 : 1;
    int ofmt = outFmt == GPU_RB_RGBA8 ? GPU_RGBA8 : outFmt == GPU_RB_RGB8 ? GPU_RGB8
             : outFmt == GPU_RB_RGB565 ? GPU_RGB565 : outFmt == GPU_RB_RGBA5551 ? GPU_RGBA5551 : GPU_RGBA4;
    u8 *out = (u8 *)outadr;
    for (u32 y = 0; y < outH; ++y)
        for (u32 x = 0; x < outW; ++x)
        {
            u32 acc[4] = { 0, 0, 0, 0 }, n = 0;
            for (u32 dy = 0; dy < sy; ++dy)
                for (u32 dx = 0; dx < sx; ++dx)
                {
                    u32 ix = x * sx + dx, iy = y * sy + dy;
                    if (ix >= inW || iy >= inH)
                        continue;
                    if (flip)
                        iy = inH - 1 - iy;
                    const u8 *p = rgba + (iy * inW + ix) * 4;
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; acc[3] += p[3];
                    ++n;
                }
            u8 px[4] = { 0, 0, 0, 0 };
            if (n)
                for (int c = 0; c < 4; ++c)
                    px[c] = (u8)(acc[c] / n);
            u32 index = toTiled ? PcFormat_TiledOffset(x, y, outW) : y * outW + x;
            EncodeTexel(px, index, ofmt, out);
        }
    free(rgba);
    PcSurface_MarkCpuWrite(outadr, outW * outH * outBpp);
    return 0;
}

Result GX_TextureCopy(u32 *inadr, u32 indim, u32 *outadr, u32 outdim, u32 size, u32 flags)
{
    PcC3d_FlushPending();
    PrepareWrite(outadr, size);
    (void)flags;
    ReadBack(inadr);
    const u8 *in = (const u8 *)inadr;
    u8 *out = (u8 *)outadr;
    u32 inLine = (indim & 0xFFFF) * 16, inGap = (indim >> 16) * 16;
    u32 outLine = (outdim & 0xFFFF) * 16, outGap = (outdim >> 16) * 16;
    if (inLine == 0 && outLine == 0)
        memmove(out, in, size);
    else
    {
        /* Lines of inLine bytes separated by inGap, into lines of outLine. */
        if (inLine == 0) inLine = size;
        if (outLine == 0) outLine = size;
        u32 done = 0, inPos = 0, outPos = 0, inLeft = inLine, outLeft = outLine;
        while (done < size)
        {
            u32 n = inLeft < outLeft ? inLeft : outLeft;
            if (n > size - done) n = size - done;
            memmove(out + outPos, in + inPos, n);
            done += n; inPos += n; outPos += n; inLeft -= n; outLeft -= n;
            if (inLeft == 0) { inPos += inGap; inLeft = inLine; }
            if (outLeft == 0) { outPos += outGap; outLeft = outLine; }
        }
    }
    PcSurface_MarkCpuWrite(outadr, size + (outLine ? (size / outLine) * outGap : 0));
    return 0;
}

static void ClearSurfaceGl(PcSurface *color, PcSurface *depth, bool clearColor, u32 colorValue, bool clearDepth,
                           u32 depthValue, int depthFmt)
{
    PcSurface *target = color;
    if (target == NULL)
    {
        /* A depth surface alone: its colour surface holds the FBO. */
        for (PcSurface *s = sSurfaces; s != NULL; s = s->next)
            if (s->depth == depth)
            {
                target = s;
                break;
            }
        if (target == NULL)
            return;
    }
    GLuint fbo = PcSurface_Framebuffer(target, target->depth);
    pglBindFramebuffer(GL_FRAMEBUFFER, fbo);
    pglDisable(GL_SCISSOR_TEST);
    pglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    pglDepthMask(GL_TRUE);
    pglStencilMask(0xFF);
    GLbitfield mask = 0;
    if (clearColor)
    {
        u8 bytes[4] = { (u8)colorValue, (u8)(colorValue >> 8), (u8)(colorValue >> 16), (u8)(colorValue >> 24) };
        u8 rgba[4];
        int tfmt = color->fmt == GPU_RB_RGBA8 ? GPU_RGBA8 : color->fmt == GPU_RB_RGB8 ? GPU_RGB8
                 : color->fmt == GPU_RB_RGB565 ? GPU_RGB565 : color->fmt == GPU_RB_RGBA5551 ? GPU_RGBA5551 : GPU_RGBA4;
        if (color->kind == PC_SURFACE_TEXTURE)
            tfmt = color->fmt;
        DecodeTexel(bytes, 0, tfmt, rgba);
        pglClearColor(rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, rgba[3] / 255.0f);
        mask |= GL_COLOR_BUFFER_BIT;
        color->gpuNewer = true;
        color->cpuNewer = false;
    }
    if (clearDepth && target->depth != NULL)
    {
        double d = depthFmt == GPU_RB_DEPTH16 ? (depthValue & 0xFFFF) / 65535.0 : (depthValue & 0xFFFFFF) / 16777215.0;
        pglClearDepth(d);
        pglClearStencil(depthFmt == GPU_RB_DEPTH24_STENCIL8 ? (int)(depthValue >> 24) : 0);
        mask |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    }
    if (mask)
        pglClear(mask);
    PcC3d_ApplyTargetState();
}

static void FillOne(u32 *start, u32 value, u32 *end, u16 control, u32 *otherStart, u32 otherValue, u32 *otherEnd,
                    u16 otherControl)
{
    (void)otherStart; (void)otherValue; (void)otherEnd; (void)otherControl;
    if (start == NULL || !(control & 1) || end <= start)
        return;
    u32 width = (control >> 8) & 3; /* 0: 16-bit, 1: 24-bit, 2: 32-bit pattern */
    u32 bytes = (u32)((u8 *)end - (u8 *)start);
    PcSurface *s = PcSurface_Containing(start);
    if (s != NULL && (u8 *)start == s->mem && bytes >= s->bytes)
    {
        if (s->kind == PC_SURFACE_DEPTH)
            ClearSurfaceGl(NULL, s, false, 0, true, value, s->fmt);
        else
            ClearSurfaceGl(s, NULL, true, value, false, 0, 0);
        return;
    }
    if (s != NULL)
        PcSurface_SyncToMemory(s);
    u8 *p = (u8 *)start;
    u32 step = width == 0 ? 2 : width == 1 ? 3 : 4;
    for (u32 i = 0; i + step <= bytes; i += step)
        for (u32 b = 0; b < step; ++b)
            p[i + b] = (u8)(value >> (8 * b));
    PcSurface_MarkCpuWrite(start, bytes);
}

Result GX_MemoryFill(u32 *buf0a, u32 buf0v, u32 *buf0e, u16 control0, u32 *buf1a, u32 buf1v, u32 *buf1e,
                     u16 control1)
{
    PcC3d_FlushPending();
    FillOne(buf0a, buf0v, buf0e, control0, NULL, 0, NULL, 0);
    FillOne(buf1a, buf1v, buf1e, control1, NULL, 0, NULL, 0);
    return 0;
}

Result GX_RequestDma(u32 *src, u32 *dst, u32 length)
{
    PcC3d_FlushPending();
    PrepareWrite(dst, length);
    ReadBack(src);
    memmove(dst, src, length);
    PcSurface_MarkCpuWrite(dst, length);
    return 0;
}

Result GX_ProcessCommandList(u32 *buf0a, u32 buf0s, u8 flags)
{
    (void)buf0a; (void)buf0s; (void)flags;
    return 0;
}

Result GX_FlushCacheRegions(u32 *buf0a, u32 buf0s, u32 *buf1a, u32 buf1s, u32 *buf2a, u32 buf2s)
{
    if (buf0a) FlushRange(buf0a, buf0s);
    if (buf1a) FlushRange(buf1a, buf1s);
    if (buf2a) FlushRange(buf2a, buf2s);
    return 0;
}

void GX_BindQueue(gxCmdQueue_s *queue) { (void)queue; }
void gxCmdQueueClear(gxCmdQueue_s *queue) { if (queue) queue->numEntries = queue->curEntry = 0; }
void gxCmdQueueAdd(gxCmdQueue_s *queue, const gxCmdEntry_s *entry) { (void)queue; (void)entry; }
void gxCmdQueueRun(gxCmdQueue_s *queue) { (void)queue; }
void gxCmdQueueStop(gxCmdQueue_s *queue) { (void)queue; }
bool gxCmdQueueWait(gxCmdQueue_s *queue, s64 timeout) { (void)queue; (void)timeout; return true; }

/* 3ds_video.c wraps these (-Wl,--wrap on the console); on a PC its wrappers
 * are not linked in, but they name the real functions. */
Result __real_GX_DisplayTransfer(u32 *inadr, u32 indim, u32 *outadr, u32 outdim, u32 flags)
{
    return GX_DisplayTransfer(inadr, indim, outadr, outdim, flags);
}
Result __real_GX_TextureCopy(u32 *inadr, u32 indim, u32 *outadr, u32 outdim, u32 size, u32 flags)
{
    return GX_TextureCopy(inadr, indim, outadr, outdim, size, flags);
}
Result __real_GX_RequestDma(u32 *src, u32 *dst, u32 length) { return GX_RequestDma(src, dst, length); }
Result __real_GX_ProcessCommandList(u32 *buf0a, u32 buf0s, u8 flags) { return GX_ProcessCommandList(buf0a, buf0s, flags); }
void __real_GX_BindQueue(gxCmdQueue_s *queue) { GX_BindQueue(queue); }

Result GSPGPU_FlushDataCache(const void *adr, u32 size)
{
    FlushRange(adr, size);
    return 0;
}
Result __real_GSPGPU_FlushDataCache(const void *adr, u32 size) { return GSPGPU_FlushDataCache(adr, size); }
Result svcFlushProcessDataCache(Handle process, u32 addr, u32 size)
{
    (void)process;
    FlushRange((const void *)(uintptr_t)addr, size);
    return 0;
}

Result svcInvalidateProcessDataCache(Handle process, u32 addr, u32 size)
{
    (void)process;
    (void)size;
    ReadBack((const void *)(uintptr_t)addr);
    return 0;
}

Result GSPGPU_InvalidateDataCache(const void *adr, u32 size)
{
    ReadBack(adr);
    (void)size;
    return 0;
}

void C3D_SyncDisplayTransfer(u32 *inadr, u32 indim, u32 *outadr, u32 outdim, u32 flags)
{
    GX_DisplayTransfer(inadr, indim, outadr, outdim, flags);
}

void C3D_SyncTextureCopy(u32 *inadr, u32 indim, u32 *outadr, u32 outdim, u32 size, u32 flags)
{
    GX_TextureCopy(inadr, indim, outadr, outdim, size, flags);
}

void C3D_SyncMemoryFill(u32 *buf0a, u32 buf0v, u32 *buf0e, u16 control0, u32 *buf1a, u32 buf1v, u32 *buf1e,
                        u16 control1)
{
    GX_MemoryFill(buf0a, buf0v, buf0e, control0, buf1a, buf1v, buf1e, control1);
}
