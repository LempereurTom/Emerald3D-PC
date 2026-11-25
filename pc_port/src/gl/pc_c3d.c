/*
 * citro3d's API on OpenGL 3.3: state, textures, render targets, programs,
 * draws and the frame. See pc_c3d_internal.h for the model.
 *
 * The public headers are citro3d's own (pc_port/third_party/citro3d): the
 * structures the port's code reads and writes (C3D_Tex, C3D_RenderTarget,
 * C3D_TexEnv...) keep their layout, and their inline helpers run unchanged.
 * What citro3d would encode into PICA registers is kept here as state and
 * applied to GL when something is drawn.
 */
#include "pc_c3d_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Uniform registers (uniforms.h names them) ──────────────────────────── */
C3D_FVec C3D_FVUnif[2][C3D_FVUNIF_COUNT];
C3D_IVec C3D_IVUnif[2][C3D_IVUNIF_COUNT];
u16 C3D_BoolUnifs[2];
bool C3D_FVUnifDirty[2][C3D_FVUNIF_COUNT];
bool C3D_IVUnifDirty[2][C3D_IVUNIF_COUNT];
bool C3D_BoolUnifsDirty[2];

void C3D_UpdateUniforms(GPU_SHADER_TYPE type)
{
    (void)type; /* every draw sends the whole register file */
}

/* ── The context ────────────────────────────────────────────────────────── */

/*
 * citro3d's own context, as bytes: ctr_voxel.c reaches into it to unbind a
 * texture unit behind citro3d's back (ReleaseDappleUnit: the bound textures at
 * 0x118, the dirty flags at 0x20). The 3DS and this build are both 32-bit, so
 * the offsets hold: the shim keeps its bound textures right there, and a
 * write there is a real unbind. The flags are not read (every draw takes the
 * units as they are).
 */
u8 __C3D_Context[0x200] __attribute__((aligned(16)));
#define C3DI_CONTEXT_TEX 0x118u
#define BOUND_TEX ((C3D_Tex **)(void *)(__C3D_Context + C3DI_CONTEXT_TEX))
_Static_assert(sizeof(C3D_Tex *) == 4, "the console's context layout needs 32-bit pointers");

typedef struct
{
    bool active, inFrame;
    shaderProgram_s *program;
    C3D_AttrInfo attrInfo;
    C3D_BufInfo bufInfo;
    C3D_FVec fixedAttribs[12];
    C3D_TexEnv texEnv[6];
    u32 texEnvBuf, texEnvBufClr;
    C3D_ProcTex *procTex;
    int procTexCoord;
    C3D_ProcTexLut *procTexLut[3];
    /* Effect */
    float zScale, zOffset;
    GPU_CULLMODE cullMode;
    bool depthEnable;
    GPU_TESTFUNC depthFunc;
    u32 writeMask;
    bool alphaTestEnable;
    GPU_TESTFUNC alphaFunc;
    int alphaRef;
    bool stencilEnable;
    GPU_TESTFUNC stencilFunc;
    int stencilRef, stencilInMask, stencilWriteMask;
    GPU_STENCILOP stencilOps[3];
    u32 blendColor;
    GPU_BLENDEQUATION colorEq, alphaEq;
    GPU_BLENDFACTOR srcClr, dstClr, srcAlpha, dstAlpha;
    bool logicOpMode;
    GPU_LOGICOP logicOp;
    /* Frame buffer, viewport, scissor */
    C3D_FrameBuf fb;
    u32 viewport[4];
    GPU_SCISSORMODE scissorMode;
    u32 scissor[4];
} PcC3dContext;

static PcC3dContext sCtx;
static GLuint sVao, sVbo[12], sIbo;

static C3D_RenderTarget *sTargets;
static C3D_RenderTarget *sLinked[3]; /* top left, top right, bottom */
static void (*sFrameEndHook)(void *);
static void *sFrameEndHookParam;
static u32 sFrameCounter;

bool C3D_Init(size_t cmdBufSize)
{
    (void)cmdBufSize;
    if (sCtx.active)
        return false;
    if (!PcDisplay_Init())
        return false;
    memset(&sCtx, 0, sizeof(sCtx));
    sCtx.active = true;
    /* citro3d binds its GX queue here, and the port watches that call (the
     * link wraps GX_BindQueue, 3ds_video.c) to know the queue and lengthen it:
     * without one it never finds room to upload, and the voxel world's atlas
     * waited forever. The shim runs GX operations at once, so the queue
     * stays empty; only its capacity is read. */
    static gxCmdQueue_s sQueue;
    if (sQueue.entries == NULL)
    {
        sQueue.entries = calloc(32, sizeof(gxCmdEntry_s));
        sQueue.maxEntries = sQueue.entries ? 32 : 0;
    }
    GX_BindQueue(&sQueue);
    pglGenVertexArrays(1, &sVao);
    pglBindVertexArray(sVao);
    pglGenBuffers(12, sVbo);
    pglGenBuffers(1, &sIbo);
    C3D_DepthMap(true, -1.0f, 0.0f);
    C3D_CullFace(GPU_CULL_BACK_CCW);
    C3D_StencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0x00);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
    C3D_BlendingColor(0);
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    C3D_AlphaTest(false, GPU_ALWAYS, 0x00);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
                   GPU_ONE_MINUS_SRC_ALPHA);
    sCtx.texEnvBuf = 0;
    sCtx.texEnvBufClr = 0xFFFFFFFF;
    for (int i = 0; i < 6; ++i)
        C3D_TexEnvInit(&sCtx.texEnv[i]);
    AttrInfo_Init(&sCtx.attrInfo);
    BufInfo_Init(&sCtx.bufInfo);
    return true;
}

void C3D_Fini(void)
{
    if (!sCtx.active)
        return;
    sCtx.active = false;
    PcDisplay_Shutdown();
}

float C3D_GetCmdBufUsage(void) { return 0.0f; }

/* ── Attributes and buffers (same encodings as citro3d's) ───────────────── */

void AttrInfo_Init(C3D_AttrInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->flags[1] = 0xFFF << 16;
}

int AttrInfo_AddLoader(C3D_AttrInfo *info, int regId, GPU_FORMATS format, int count)
{
    if (info->attrCount == 12)
        return -1;
    int id = info->attrCount++;
    if (regId < 0)
        regId = id;
    if (id < 8)
        info->flags[0] |= GPU_ATTRIBFMT(id, count, format);
    else
        info->flags[1] |= GPU_ATTRIBFMT(id - 8, count, format);
    info->flags[1] = (info->flags[1] & ~(0xF0000000 | BIT(id + 16))) | (id << 28);
    info->permutation |= (u64)regId << (id * 4);
    return id;
}

int AttrInfo_AddFixed(C3D_AttrInfo *info, int regId)
{
    if (info->attrCount == 12)
        return -1;
    int id = info->attrCount++;
    if (regId < 0)
        regId = id;
    info->flags[1] = (info->flags[1] & ~0xF0000000) | (id << 28);
    info->permutation |= (u64)regId << (id * 4);
    return id;
}

C3D_AttrInfo *C3D_GetAttrInfo(void) { return sCtx.active ? &sCtx.attrInfo : NULL; }
void C3D_SetAttrInfo(C3D_AttrInfo *info)
{
    if (sCtx.active && info != &sCtx.attrInfo)
        memcpy(&sCtx.attrInfo, info, sizeof(*info));
}

/* The base is 0 on a PC (citro3d: 0x18000000), so any address fits. */
void BufInfo_Init(C3D_BufInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->base_paddr = 0;
}

int BufInfo_Add(C3D_BufInfo *info, const void *data, ptrdiff_t stride, int attribCount, u64 permutation)
{
    if (info->bufCount == 12)
        return -1;
    int id = info->bufCount++;
    u32 pa = osConvertVirtToPhys(data);
    if (pa < info->base_paddr)
        return -2;
    C3D_BufCfg *buf = &info->buffers[id];
    buf->offset = pa - info->base_paddr;
    buf->flags[0] = permutation & 0xFFFFFFFF;
    buf->flags[1] = (permutation >> 32) | (stride << 16) | (attribCount << 28);
    return id;
}

C3D_BufInfo *C3D_GetBufInfo(void) { return sCtx.active ? &sCtx.bufInfo : NULL; }
void C3D_SetBufInfo(C3D_BufInfo *info)
{
    if (sCtx.active && info != &sCtx.bufInfo)
        memcpy(&sCtx.bufInfo, info, sizeof(*info));
}

C3D_FVec *C3D_FixedAttribGetWritePtr(int id)
{
    return (id >= 0 && id < 12) ? &sCtx.fixedAttribs[id] : NULL;
}

/* ── Effect ─────────────────────────────────────────────────────────────── */

void C3D_DepthMap(bool bIsZBuffer, float zScale, float zOffset)
{
    (void)bIsZBuffer;
    sCtx.zScale = zScale;
    sCtx.zOffset = zOffset;
}
void C3D_CullFace(GPU_CULLMODE mode) { sCtx.cullMode = mode; }
void C3D_StencilTest(bool enable, GPU_TESTFUNC function, int ref, int inputMask, int writeMask)
{
    sCtx.stencilEnable = enable;
    sCtx.stencilFunc = function;
    sCtx.stencilRef = ref;
    sCtx.stencilInMask = inputMask;
    sCtx.stencilWriteMask = writeMask;
}
void C3D_StencilOp(GPU_STENCILOP sfail, GPU_STENCILOP dfail, GPU_STENCILOP pass)
{
    sCtx.stencilOps[0] = sfail;
    sCtx.stencilOps[1] = dfail;
    sCtx.stencilOps[2] = pass;
}
void C3D_BlendingColor(u32 color) { sCtx.blendColor = color; }
void C3D_EarlyDepthTest(bool enable, GPU_EARLYDEPTHFUNC function, u32 ref) { (void)enable; (void)function; (void)ref; }
void C3D_DepthTest(bool enable, GPU_TESTFUNC function, GPU_WRITEMASK writemask)
{
    sCtx.depthEnable = enable;
    sCtx.depthFunc = function;
    sCtx.writeMask = writemask;
}
void C3D_AlphaTest(bool enable, GPU_TESTFUNC function, int ref)
{
    sCtx.alphaTestEnable = enable;
    sCtx.alphaFunc = function;
    sCtx.alphaRef = ref;
}
void C3D_AlphaBlend(GPU_BLENDEQUATION colorEq, GPU_BLENDEQUATION alphaEq, GPU_BLENDFACTOR srcClr,
                    GPU_BLENDFACTOR dstClr, GPU_BLENDFACTOR srcAlpha, GPU_BLENDFACTOR dstAlpha)
{
    sCtx.colorEq = colorEq;
    sCtx.alphaEq = alphaEq;
    sCtx.srcClr = srcClr;
    sCtx.dstClr = dstClr;
    sCtx.srcAlpha = srcAlpha;
    sCtx.dstAlpha = dstAlpha;
    sCtx.logicOpMode = false;
}
void C3D_ColorLogicOp(GPU_LOGICOP op)
{
    sCtx.logicOp = op;
    sCtx.logicOpMode = true;
}
void C3D_FragOpMode(GPU_FRAGOPMODE mode) { (void)mode; }
void C3D_FragOpShadow(float scale, float bias) { (void)scale; (void)bias; }

/* ── Texture combiners ──────────────────────────────────────────────────── */

C3D_TexEnv *C3D_GetTexEnv(int id) { return (id >= 0 && id < 6) ? &sCtx.texEnv[id] : NULL; }
void C3D_SetTexEnv(int id, C3D_TexEnv *env)
{
    if (id >= 0 && id < 6 && env != &sCtx.texEnv[id])
        memcpy(&sCtx.texEnv[id], env, sizeof(*env));
}
void C3D_DirtyTexEnv(C3D_TexEnv *env) { (void)env; }
void C3D_TexEnvBufUpdate(int mode, int mask)
{
    u32 val = sCtx.texEnvBuf;
    mask &= 0xF;
    if (mode & C3D_RGB)
        val = (val & ~(0xF << 8)) | (mask << 8);
    if (mode & C3D_Alpha)
        val = (val & ~(0xF << 12)) | (mask << 12);
    sCtx.texEnvBuf = val;
}
void C3D_TexEnvBufColor(u32 color) { sCtx.texEnvBufClr = color; }

/* ── Procedural texture ─────────────────────────────────────────────────── */

void C3D_ProcTexInit(C3D_ProcTex *pt, int offset, int width)
{
    memset(pt, 0, sizeof(*pt));
    pt->offset = offset;
    pt->width = width;
    pt->unknown1 = 0x60;
    pt->unknown2 = 0xE0C080;
}
void C3D_ProcTexNoiseCoefs(C3D_ProcTex *pt, int mode, float amplitude, float frequency, float phase)
{
    (void)pt; (void)mode; (void)amplitude; (void)frequency; (void)phase;
}
void C3D_ProcTexLodBias(C3D_ProcTex *pt, float bias) { (void)pt; (void)bias; }
void C3D_ProcTexBind(int texCoordId, C3D_ProcTex *pt)
{
    sCtx.procTex = pt;
    sCtx.procTexCoord = texCoordId & 3;
}
void C3D_ProcTexLutBind(GPU_PROCTEX_LUTID id, C3D_ProcTexLut *lut)
{
    int slot = id == GPU_LUT_ALPHAMAP ? 1 : id == GPU_LUT_RGBMAP ? 0 : 2;
    sCtx.procTexLut[slot] = lut;
}
void C3D_ProcTexColorLutBind(C3D_ProcTexColorLut *lut) { (void)lut; }

static float ClampLut(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
void ProcTexLut_FromArray(C3D_ProcTexLut *lut, const float in[129])
{
    for (int i = 0; i < 128; ++i)
    {
        u32 cur = (u32)(0xFFF * ClampLut(in[i]));
        u32 next = (u32)(0xFFF * ClampLut(in[i + 1]));
        u32 diff = (next - cur) & 0xFFF;
        (*lut)[i] = cur | (diff << 12);
    }
}

/* ── Textures ───────────────────────────────────────────────────────────── */

bool C3D_TexInitWithParams(C3D_Tex *tex, C3D_TexCube *cube, C3D_TexInitParams p)
{
    if ((p.width & 7) || (p.height & 7) || p.width < 8 || p.height < 8 || p.width > 1024 || p.height > 1024)
        return false;
    u32 size = (u32)p.width * p.height * PcFormat_TexBpp4(p.format) / 8;
    u32 total = p.maxLevel ? C3D_TexCalcTotalSize(size, p.maxLevel) : size;
    memset(tex, 0, sizeof(*tex));
    tex->width = p.width;
    tex->height = p.height;
    tex->fmt = p.format;
    tex->size = size;
    tex->maxLevel = p.maxLevel;
    tex->param = GPU_TEXTURE_MODE(p.type);
    if (p.format == GPU_ETC1)
        tex->param |= GPU_TEXTURE_ETC1_PARAM;
    if (cube != NULL)
    {
        /* Cube maps: not used by the port; refused rather than half made. */
        return false;
    }
    tex->data = p.onVram ? vramAlloc(total) : linearAlloc(total);
    if (tex->data == NULL)
        return false;
    PcSurface_Create(tex->data, size, p.width, p.height, PC_SURFACE_TEXTURE, p.format);
    return true;
}

void C3D_TexLoadImage(C3D_Tex *tex, const void *data, GPU_TEXFACE face, int level)
{
    (void)face;
    u32 size = 0;
    void *out = C3D_Tex2DGetImagePtr(tex, level, &size);
    if (out == NULL)
        return;
    memcpy(out, data, size);
    PcSurface_MarkCpuWrite(out, size);
}

void C3D_TexGenerateMipmap(C3D_Tex *tex, GPU_TEXFACE face) { (void)tex; (void)face; }

void C3D_TexBind(int unitId, C3D_Tex *tex)
{
    if (unitId >= 0 && unitId < 3)
        BOUND_TEX[unitId] = tex;
}

void C3D_TexFlush(C3D_Tex *tex)
{
    if (tex != NULL && tex->data != NULL)
        PcSurface_MarkCpuWrite(tex->data, tex->size);
}

void C3D_TexDelete(C3D_Tex *tex)
{
    if (tex == NULL || tex->data == NULL)
        return;
    for (int i = 0; i < 3; ++i)
        if (BOUND_TEX[i] == tex)
            BOUND_TEX[i] = NULL;
    /* VRAM and linear blocks are told apart by nothing on a PC: both are
     * the shim's heap blocks, which free their surface. */
    vramFree(tex->data);
    tex->data = NULL;
}

void C3D_TexShadowParams(bool perspective, float bias) { (void)perspective; (void)bias; }

/* The surface of a texture, made on first use when the texture was not
 * made by C3D_TexInit (data set by hand). */
static PcSurface *TexSurface(C3D_Tex *tex)
{
    if (tex == NULL || tex->data == NULL)
        return NULL;
    PcSurface *s = PcSurface_Find(tex->data);
    if (s == NULL || s->width != tex->width || s->height != tex->height)
        s = PcSurface_Create(tex->data, tex->width * tex->height * PcFormat_TexBpp4(tex->fmt) / 8, tex->width,
                             tex->height, PC_SURFACE_TEXTURE, tex->fmt);
    return s;
}

/* ── Frame buffers and render targets ───────────────────────────────────── */

static const u8 sColorFmtSizes[] = { 2, 1, 0, 0, 0 };
static const u8 sDepthFmtSizes[] = { 0, 0, 1, 2 };

u32 C3D_CalcColorBufSize(u32 width, u32 height, GPU_COLORBUF fmt) { return width * height * (2 + sColorFmtSizes[fmt]); }
u32 C3D_CalcDepthBufSize(u32 width, u32 height, GPU_DEPTHBUF fmt) { return width * height * (2 + sDepthFmtSizes[fmt]); }

C3D_FrameBuf *C3D_GetFrameBuf(void) { return &sCtx.fb; }
void C3D_SetFrameBuf(C3D_FrameBuf *fb)
{
    if (fb != &sCtx.fb)
        memcpy(&sCtx.fb, fb, sizeof(*fb));
}

void C3D_FrameBufTex(C3D_FrameBuf *fb, C3D_Tex *tex, GPU_TEXFACE face, int level)
{
    (void)face;
    C3D_FrameBufAttrib(fb, tex->width, tex->height, false);
    C3D_FrameBufColor(fb, C3D_Tex2DGetImagePtr(tex, level, NULL), (GPU_COLORBUF)tex->fmt);
}

void C3D_FrameBufClear(C3D_FrameBuf *fb, C3D_ClearBits clearBits, u32 clearColor, u32 clearDepth)
{
    u32 size = (u32)fb->width * fb->height;
    u32 cfs = sColorFmtSizes[fb->colorFmt], dfs = sDepthFmtSizes[fb->depthFmt];
    u8 *colorEnd = (u8 *)fb->colorBuf + size * (2 + cfs);
    u8 *depthEnd = (u8 *)fb->depthBuf + size * (2 + dfs);
    if ((clearBits & C3D_CLEAR_COLOR) && fb->colorBuf)
        GX_MemoryFill(fb->colorBuf, clearColor, (u32 *)colorEnd, BIT(0) | (cfs << 8), NULL, 0, NULL, 0);
    if ((clearBits & C3D_CLEAR_DEPTH) && fb->depthBuf)
        GX_MemoryFill(fb->depthBuf, clearDepth, (u32 *)depthEnd, BIT(0) | (dfs << 8), NULL, 0, NULL, 0);
}

void C3D_FrameBufTransfer(C3D_FrameBuf *fb, gfxScreen_t screen, gfx3dSide_t side, u32 transferFlags)
{
    (void)fb; (void)screen; (void)side; (void)transferFlags;
}

static void SurfacesFor(C3D_FrameBuf *fb, PcSurface **color, PcSurface **depth)
{
    *color = fb->colorBuf ? PcSurface_Find(fb->colorBuf) : NULL;
    *depth = fb->depthBuf ? PcSurface_Find(fb->depthBuf) : NULL;
    if (*color == NULL && fb->colorBuf)
        *color = PcSurface_Create(fb->colorBuf, C3D_CalcColorBufSize(fb->width, fb->height, fb->colorFmt),
                                  fb->width, fb->height, PC_SURFACE_COLOR, fb->colorFmt);
    if (*depth == NULL && fb->depthBuf)
        *depth = PcSurface_Create(fb->depthBuf, C3D_CalcDepthBufSize(fb->width, fb->height, fb->depthFmt),
                                  fb->width, fb->height, PC_SURFACE_DEPTH, fb->depthFmt);
}

static C3D_RenderTarget *TargetNew(void)
{
    C3D_RenderTarget *t = calloc(1, sizeof(*t));
    if (t == NULL)
        return NULL;
    t->next = sTargets;
    if (sTargets)
        sTargets->prev = t;
    sTargets = t;
    return t;
}

C3D_RenderTarget *C3D_RenderTargetCreate(int width, int height, GPU_COLORBUF colorFmt, C3D_DEPTHTYPE depthFmt)
{
    void *colorBuf = vramAlloc(C3D_CalcColorBufSize(width, height, colorFmt));
    void *depthBuf = NULL;
    GPU_DEPTHBUF depthReal = GPU_RB_DEPTH16;
    if (colorBuf == NULL)
        return NULL;
    if (C3D_DEPTHTYPE_OK(depthFmt))
    {
        depthReal = C3D_DEPTHTYPE_VAL(depthFmt);
        depthBuf = vramAlloc(C3D_CalcDepthBufSize(width, height, depthReal));
        if (depthBuf == NULL)
        {
            vramFree(colorBuf);
            return NULL;
        }
    }
    C3D_RenderTarget *t = TargetNew();
    if (t == NULL)
    {
        vramFree(depthBuf);
        vramFree(colorBuf);
        return NULL;
    }
    C3D_FrameBufAttrib(&t->frameBuf, width, height, false);
    C3D_FrameBufColor(&t->frameBuf, colorBuf, colorFmt);
    t->ownsColor = true;
    if (depthBuf)
    {
        C3D_FrameBufDepth(&t->frameBuf, depthBuf, depthReal);
        t->ownsDepth = true;
    }
    PcSurface *color, *depth;
    SurfacesFor(&t->frameBuf, &color, &depth);
    if (color)
        PcSurface_Framebuffer(color, depth);
    return t;
}

C3D_RenderTarget *C3D_RenderTargetCreateFromTex(C3D_Tex *tex, GPU_TEXFACE face, int level, C3D_DEPTHTYPE depthFmt)
{
    C3D_RenderTarget *t = TargetNew();
    if (t == NULL)
        return NULL;
    C3D_FrameBufTex(&t->frameBuf, tex, face, level);
    if (C3D_DEPTHTYPE_OK(depthFmt))
    {
        GPU_DEPTHBUF depthReal = C3D_DEPTHTYPE_VAL(depthFmt);
        void *depthBuf = vramAlloc(C3D_CalcDepthBufSize(t->frameBuf.width, t->frameBuf.height, depthReal));
        if (depthBuf == NULL)
        {
            free(t);
            return NULL;
        }
        C3D_FrameBufDepth(&t->frameBuf, depthBuf, depthReal);
        t->ownsDepth = true;
    }
    PcSurface *color = TexSurface(tex), *depth = NULL;
    if (t->frameBuf.depthBuf)
        depth = PcSurface_Create(t->frameBuf.depthBuf,
                                 C3D_CalcDepthBufSize(t->frameBuf.width, t->frameBuf.height, t->frameBuf.depthFmt),
                                 t->frameBuf.width, t->frameBuf.height, PC_SURFACE_DEPTH, t->frameBuf.depthFmt);
    if (color)
        PcSurface_Framebuffer(color, depth);
    return t;
}

void C3D_RenderTargetDelete(C3D_RenderTarget *target)
{
    if (target == NULL)
        return;
    PcDisplay_ForgetTarget(target);
    for (int i = 0; i < 3; ++i)
        if (sLinked[i] == target)
            sLinked[i] = NULL;
    if (target->prev)
        target->prev->next = target->next;
    else
        sTargets = target->next;
    if (target->next)
        target->next->prev = target->prev;
    if (target->ownsColor)
        vramFree(target->frameBuf.colorBuf);
    if (target->ownsDepth)
        vramFree(target->frameBuf.depthBuf);
    free(target);
}

void C3D_RenderTargetSetOutput(C3D_RenderTarget *target, gfxScreen_t screen, gfx3dSide_t side, u32 transferFlags)
{
    int id = screen == GFX_BOTTOM ? 2 : side == GFX_RIGHT ? 1 : 0;
    if (sLinked[id])
        sLinked[id]->linked = false;
    sLinked[id] = target;
    if (target)
    {
        target->linked = true;
        target->transferFlags = transferFlags;
        target->screen = screen;
        target->side = side;
    }
}

/* ── Frames ─────────────────────────────────────────────────────────────── */

float C3D_FrameRate(float fps) { (void)fps; return 60.0f; }
void C3D_FrameSync(void) {}
u32 C3D_FrameCounter(int id) { (void)id; return sFrameCounter; }

bool C3D_FrameBegin(u8 flags)
{
    if (sCtx.inFrame)
        return false;
    /* SYNCDRAW: the console waits for the previous frame and its VBlank. */
    if (flags & C3D_FRAME_SYNCDRAW)
        PcDisplay_WaitVBlank();
    sCtx.inFrame = true;
    for (C3D_RenderTarget *t = sTargets; t != NULL; t = t->next)
        t->used = false;
    PcSurface_InvalidateHashes();
    return true;
}

bool C3D_FrameDrawOn(C3D_RenderTarget *target)
{
    if (!sCtx.inFrame || target == NULL)
        return false;
    target->used = true;
    C3D_SetFrameBuf(&target->frameBuf);
    C3D_SetViewport(0, 0, target->frameBuf.width, target->frameBuf.height);
    return true;
}

void PcC3d_FlushPending(void);

void C3D_FrameSplit(u8 flags)
{
    (void)flags;
    PcC3d_FlushPending();
}

void C3D_FrameEndHook(void (*hook)(void *), void *param)
{
    sFrameEndHook = hook;
    sFrameEndHookParam = param;
}

void C3D_FrameEnd(u8 flags)
{
    (void)flags;
    if (!sCtx.inFrame)
        return;
    PcC3d_FlushPending();
    if (sFrameEndHook)
        sFrameEndHook(sFrameEndHookParam);
    sCtx.inFrame = false;
    ++sFrameCounter;
    PcDisplay_Present(sLinked[0] && sLinked[0]->used ? sLinked[0] : sLinked[0],
                      sLinked[2] && sLinked[2]->used ? sLinked[2] : NULL);
}

float C3D_GetDrawingTime(void) { return 0.0f; }
float C3D_GetProcessingTime(void) { return 0.0f; }

/* ── Viewport and scissor ───────────────────────────────────────────────── */

void C3D_SetViewport(u32 x, u32 y, u32 w, u32 h)
{
    sCtx.viewport[0] = x;
    sCtx.viewport[1] = y;
    sCtx.viewport[2] = w;
    sCtx.viewport[3] = h;
}

void C3D_SetScissor(GPU_SCISSORMODE mode, u32 left, u32 top, u32 right, u32 bottom)
{
    sCtx.scissorMode = mode;
    sCtx.scissor[0] = left;
    sCtx.scissor[1] = top;
    sCtx.scissor[2] = right;
    sCtx.scissor[3] = bottom;
}

/* ── Programs ───────────────────────────────────────────────────────────── */

void C3D_BindProgram(shaderProgram_s *program) { sCtx.program = program; }

/* ── Drawing ────────────────────────────────────────────────────────────── *
 * The PICA runs a frame's draws when the command list is submitted
 * (C3D_FrameSplit, C3D_FrameEnd) and reads vertices and texels then: a draw
 * sees the texels the CPU wrote after it was issued, until the submission.
 * The port relies on that (it decodes tiles into the atlas while it issues
 * the frame), so draws are recorded here with a snapshot of the state they
 * were issued with, and run at the submission points - and before any GX
 * operation or free, which on the console come after the submitted work. */

typedef struct
{
    PcC3dContext ctx;
    int programKind;
    C3D_Tex tex[3];
    bool texOn[3];
    C3D_ProcTex procTex;
    C3D_ProcTexLut procLut[2];
    bool procOn, procLutOn[2];
    C3D_FVec fv[C3D_FVUNIF_COUNT];
    C3D_IVec iv[C3D_IVUNIF_COUNT];
    u16 bools;
    u8 op; /* 0 arrays, 1 elements, 2 immediate */
    GPU_Primitive_t prim;
    int first, count, type;
    const void *indices;
    float *imm;
    u32 immFloats;
} PendingDraw;

static PendingDraw *sPending;
static u32 sPendingCount, sPendingCap;
static bool sFlushing;
static PcSurface *sDrawColor;
static GLint sDrawScale = 1; /* the quality's scale of the surface drawn into */

static PendingDraw *Record(u8 op)
{
    if (!sCtx.inFrame)
        return NULL;
    if (sPendingCount == sPendingCap)
    {
        u32 cap = sPendingCap ? sPendingCap * 2 : 256;
        PendingDraw *p = realloc(sPending, cap * sizeof(*p));
        if (p == NULL)
            return NULL;
        sPending = p;
        sPendingCap = cap;
    }
    PendingDraw *d = &sPending[sPendingCount++];
    d->ctx = sCtx;
    d->programKind = PcProgram_KindOf(sCtx.program);
    for (int i = 0; i < 3; ++i)
    {
        d->texOn[i] = BOUND_TEX[i] != NULL && BOUND_TEX[i]->data != NULL;
        if (d->texOn[i])
            d->tex[i] = *BOUND_TEX[i];
    }
    d->procOn = sCtx.procTex != NULL;
    if (d->procOn)
    {
        d->procTex = *sCtx.procTex;
        for (int i = 0; i < 2; ++i)
        {
            d->procLutOn[i] = sCtx.procTexLut[i] != NULL;
            if (d->procLutOn[i])
                memcpy(d->procLut[i], *sCtx.procTexLut[i], sizeof(C3D_ProcTexLut));
        }
    }
    memcpy(d->fv, C3D_FVUnif[GPU_VERTEX_SHADER], sizeof(d->fv));
    memcpy(d->iv, C3D_IVUnif[GPU_VERTEX_SHADER], sizeof(d->iv));
    d->bools = C3D_BoolUnifs[GPU_VERTEX_SHADER];
    d->op = op;
    d->imm = NULL;
    d->immFloats = 0;
    return d;
}

/* Binds a draw's frame buffer (uploading what the CPU wrote into it first),
 * with its viewport. */
static bool BindTarget(const PendingDraw *d)
{
    PcSurface *color, *depth;
    C3D_FrameBuf fb = d->ctx.fb;
    SurfacesFor(&fb, &color, &depth);
    if (color == NULL)
        return false;
    PcSurface_SyncToGl(color);
    GLuint fbo = PcSurface_DrawFbo(color, depth);
    PcSurface_PrepareDraw(color);
    pglBindFramebuffer(GL_FRAMEBUFFER, fbo);
    /* The quality's scale (PcC3d_SetQuality): the same picture, finer. */
    GLint k = color->scale;
    pglViewport((GLint)d->ctx.viewport[0] * k, (GLint)d->ctx.viewport[1] * k, (GLsizei)d->ctx.viewport[2] * k,
                (GLsizei)d->ctx.viewport[3] * k);
    sDrawColor = color;
    sDrawScale = k;
    if (color->samples > 1)
        color->msaaDirty = true;
    return true;
}

void PcC3d_ApplyTargetState(void)
{
    /* A clear rebinds frame buffers and masks: the next draw restores its
     * own state completely, so nothing is needed here beyond the binding. */
    pglBindFramebuffer(GL_FRAMEBUFFER, sDrawColor ? (sDrawColor->msaaFbo && sDrawColor->samples > 1
                                                     ? sDrawColor->msaaFbo : sDrawColor->fbo) : 0);
}

static const GLenum sTestFunc[] = { GL_NEVER, GL_ALWAYS, GL_EQUAL, GL_NOTEQUAL, GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL };
static const GLenum sBlendEq[] = { GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX };
static const GLenum sBlendFactor[] = {
    GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
    GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_CONSTANT_COLOR,
    GL_ONE_MINUS_CONSTANT_COLOR, GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA, GL_SRC_ALPHA_SATURATE,
};
static const GLenum sLogicOp[] = {
    GL_CLEAR, GL_AND, GL_AND_REVERSE, GL_COPY, GL_SET, GL_COPY_INVERTED, GL_NOOP, GL_INVERT,
    GL_NAND, GL_OR, GL_NOR, GL_XOR, GL_EQUIV, GL_AND_INVERTED, GL_OR_REVERSE, GL_OR_INVERTED,
};
static const GLenum sStencilOp[] = { GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP };

static void ApplyFixedFunction(const PcC3dContext *c)
{
    /* Depth: the PICA writes depth only where the test runs. */
    if (c->depthEnable)
    {
        pglEnable(GL_DEPTH_TEST);
        pglDepthFunc(sTestFunc[c->depthFunc & 7]);
    }
    else
        pglDisable(GL_DEPTH_TEST);
    pglDepthMask((c->writeMask & GPU_WRITE_DEPTH) ? GL_TRUE : GL_FALSE);
    pglColorMask((c->writeMask & GPU_WRITE_RED) != 0, (c->writeMask & GPU_WRITE_GREEN) != 0,
                 (c->writeMask & GPU_WRITE_BLUE) != 0, (c->writeMask & GPU_WRITE_ALPHA) != 0);

    if (c->logicOpMode)
    {
        pglDisable(GL_BLEND);
        pglEnable(GL_COLOR_LOGIC_OP);
        pglLogicOp(sLogicOp[c->logicOp & 15]);
    }
    else
    {
        pglDisable(GL_COLOR_LOGIC_OP);
        pglEnable(GL_BLEND);
        pglBlendEquationSeparate(sBlendEq[c->colorEq % 5], sBlendEq[c->alphaEq % 5]);
        pglBlendFuncSeparate(sBlendFactor[c->srcClr % 15], sBlendFactor[c->dstClr % 15],
                             sBlendFactor[c->srcAlpha % 15], sBlendFactor[c->dstAlpha % 15]);
        u32 bc = c->blendColor;
        pglBlendColor((bc & 0xFF) / 255.0f, ((bc >> 8) & 0xFF) / 255.0f, ((bc >> 16) & 0xFF) / 255.0f,
                      ((bc >> 24) & 0xFF) / 255.0f);
    }

    if (c->stencilEnable)
    {
        pglEnable(GL_STENCIL_TEST);
        pglStencilFunc(sTestFunc[c->stencilFunc & 7], c->stencilRef, (GLuint)c->stencilInMask);
        pglStencilOp(sStencilOp[c->stencilOps[0] & 7], sStencilOp[c->stencilOps[1] & 7],
                     sStencilOp[c->stencilOps[2] & 7]);
        pglStencilMask((GLuint)c->stencilWriteMask);
    }
    else
        pglDisable(GL_STENCIL_TEST);

    /* Culling: the PICA's front faces are counter-clockwise in window
     * coordinates, which run the same way as GL's. */
    if (c->cullMode == GPU_CULL_NONE)
        pglDisable(GL_CULL_FACE);
    else
    {
        pglEnable(GL_CULL_FACE);
        pglFrontFace(GL_CCW);
        pglCullFace(c->cullMode == GPU_CULL_FRONT_CCW ? GL_FRONT : GL_BACK);
    }

    if (c->scissorMode == GPU_SCISSOR_NORMAL)
    {
        pglEnable(GL_SCISSOR_TEST);
        u32 l = c->scissor[0], t = c->scissor[1], r = c->scissor[2], b = c->scissor[3];
        GLint k = sDrawScale;
        pglScissor((GLint)l * k, (GLint)t * k, r > l ? (GLsizei)(r - l) * k : 0, b > t ? (GLsizei)(b - t) * k : 0);
    }
    else
        pglDisable(GL_SCISSOR_TEST);
}

static GLint U(GLuint program, const char *name) { return pglGetUniformLocation(program, name); }

static void LutToFloats(const C3D_ProcTexLut *lut, float *out)
{
    for (int i = 0; i < 128; ++i)
    {
        u32 e = lut ? (*lut)[i] : (u32)(i * 0xFFF / 127);
        u32 cur = e & 0xFFF;
        int diff = (int)((e >> 12) & 0xFFF);
        if (diff & 0x800)
            diff -= 0x1000;
        out[i * 2] = cur / 4095.0f;
        out[i * 2 + 1] = diff / 4095.0f;
    }
}

static void ApplyProgramUniforms(GLuint program, const PendingDraw *d)
{
    const PcC3dContext *c = &d->ctx;
    static GLfloat regs[C3D_FVUNIF_COUNT * 4];
    for (int i = 0; i < C3D_FVUNIF_COUNT; ++i)
    {
        /* C3D_FVec keeps w, z, y, x in memory: by name. */
        regs[i * 4 + 0] = d->fv[i].x;
        regs[i * 4 + 1] = d->fv[i].y;
        regs[i * 4 + 2] = d->fv[i].z;
        regs[i * 4 + 3] = d->fv[i].w;
    }
    pglUniform4fv(U(program, "uC"), C3D_FVUNIF_COUNT, regs);
    GLint ivals[C3D_IVUNIF_COUNT * 4];
    for (int i = 0; i < C3D_IVUNIF_COUNT; ++i)
        for (int k = 0; k < 4; ++k) /* C3D_IVec: x | y << 8 | z << 16 | w << 24 (IVec_Pack) */
            ivals[i * 4 + k] = (GLint)((d->iv[i] >> (8 * k)) & 0xFF);
    pglUniform4iv(U(program, "uI"), C3D_IVUNIF_COUNT, ivals);
    pglUniform1i(U(program, "uB"), d->bools);
    pglUniform2f(U(program, "uDepthMap"), c->zScale, c->zOffset);

    /* Texture combiners */
    GLint stage[6 * 4];
    GLfloat stageColor[6 * 4];
    for (int i = 0; i < 6; ++i)
    {
        const C3D_TexEnv *e = &c->texEnv[i];
        stage[i * 4 + 0] = e->srcRgb;
        stage[i * 4 + 1] = e->srcAlpha;
        stage[i * 4 + 2] = (GLint)(e->opRgb | (e->opAlpha << 12));
        stage[i * 4 + 3] = e->funcRgb | (e->funcAlpha << 4) | (e->scaleRgb << 8) | (e->scaleAlpha << 10);
        stageColor[i * 4 + 0] = (e->color & 0xFF) / 255.0f;
        stageColor[i * 4 + 1] = ((e->color >> 8) & 0xFF) / 255.0f;
        stageColor[i * 4 + 2] = ((e->color >> 16) & 0xFF) / 255.0f;
        stageColor[i * 4 + 3] = ((e->color >> 24) & 0xFF) / 255.0f;
    }
    pglUniform4iv(U(program, "uStage"), 6, stage);
    pglUniform4fv(U(program, "uStageColor"), 6, stageColor);
    u32 b = c->texEnvBufClr;
    GLfloat bufColor[4] = { (b & 0xFF) / 255.0f, ((b >> 8) & 0xFF) / 255.0f, ((b >> 16) & 0xFF) / 255.0f,
                            ((b >> 24) & 0xFF) / 255.0f };
    pglUniform1i(U(program, "uBufUpdate"), (GLint)c->texEnvBuf);
    pglUniform4fv(U(program, "uBufColor"), 1, bufColor);
    pglUniform1i(U(program, "uAlphaFunc"), c->alphaTestEnable ? (GLint)c->alphaFunc : -1);
    pglUniform1i(U(program, "uAlphaRef"), c->alphaRef);
    pglUniform1i(U(program, "uScissorInvert"), c->scissorMode == GPU_SCISSOR_INVERT);
    GLint sc[4] = { (GLint)c->scissor[0] * sDrawScale, (GLint)c->scissor[1] * sDrawScale,
                    (GLint)c->scissor[2] * sDrawScale, (GLint)c->scissor[3] * sDrawScale };
    pglUniform4iv(U(program, "uScissor"), 1, sc);

    /* Procedural texture */
    pglUniform1i(U(program, "uPtOn"), d->procOn);
    if (d->procOn)
    {
        const C3D_ProcTex *pt = &d->procTex;
        GLint ptCfg[4] = { (GLint)pt->uClamp, (GLint)pt->vClamp, (GLint)pt->rgbFunc,
                           (GLint)(pt->alphaSeparate ? pt->alphaFunc : pt->rgbFunc) };
        pglUniform4iv(U(program, "uPtCfg"), 1, ptCfg);
        pglUniform1i(U(program, "uPtCoord"), c->procTexCoord);
        static GLfloat lutRgb[256], lutA[256];
        LutToFloats(d->procLutOn[0] ? &d->procLut[0] : NULL, lutRgb);
        bool alphaOwn = pt->alphaSeparate && d->procLutOn[1];
        LutToFloats(alphaOwn ? &d->procLut[1] : d->procLutOn[0] ? &d->procLut[0] : NULL, lutA);
        pglUniform4fv(U(program, "uPtLutRgb"), 64, lutRgb);
        pglUniform4fv(U(program, "uPtLutA"), 64, lutA);
    }
}

static void BindTextures(GLuint program, const PendingDraw *d)
{
    static const char *names[3] = { "uTex0", "uTex1", "uTex2" };
    static const GLenum wraps[4] = { GL_CLAMP_TO_EDGE, GL_CLAMP_TO_BORDER, GL_REPEAT, GL_MIRRORED_REPEAT };
    GLint on[3] = { 0, 0, 0 };
    for (int unit = 0; unit < 3; ++unit)
    {
        pglActiveTexture(GL_TEXTURE0 + unit);
        if (!d->texOn[unit])
        {
            pglBindTexture(GL_TEXTURE_2D, 0);
            continue;
        }
        C3D_Tex tex = d->tex[unit];
        PcSurface *s = TexSurface(&tex);
        if (s == NULL)
        {
            pglBindTexture(GL_TEXTURE_2D, 0);
            continue;
        }
        PcSurface_SyncToGl(s);
        PcSurface_Resolve(s); /* a picture drawn multisampled, read as a texture */
        pglBindTexture(GL_TEXTURE_2D, s->tex);
        u32 p = tex.param;
        pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (p >> 1) & 1 ? GL_LINEAR : GL_NEAREST);
        pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (p >> 2) & 1 ? GL_LINEAR : GL_NEAREST);
        pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wraps[(p >> 12) & 3]);
        pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wraps[(p >> 8) & 3]);
        u32 bc = tex.border;
        GLfloat border[4] = { (bc & 0xFF) / 255.0f, ((bc >> 8) & 0xFF) / 255.0f, ((bc >> 16) & 0xFF) / 255.0f,
                              ((bc >> 24) & 0xFF) / 255.0f };
        pglTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        pglUniform1i(U(program, names[unit]), unit);
        on[unit] = 1;
    }
    pglUniform1iv(U(program, "uTexOn"), 3, on);
    pglActiveTexture(GL_TEXTURE0);
}

static const GLenum sAttribType[] = { GL_BYTE, GL_UNSIGNED_BYTE, GL_SHORT, GL_FLOAT };
static const u32 sAttribSize[] = { 1, 1, 2, 4 };

/* Sets up the vertex attributes from the attribute and buffer info, with
 * each buffer's bytes [0, vertices * stride) uploaded. */
static void SetupAttributes(const PcC3dContext *c, u32 vertices)
{
    const C3D_AttrInfo *ai = &c->attrInfo;
    bool fromBuffer[12] = { false };

    for (int i = 0; i < 16; ++i)
        pglDisableVertexAttribArray((GLuint)i);
    for (int b = 0; b < c->bufInfo.bufCount; ++b)
    {
        const C3D_BufCfg *cfg = &c->bufInfo.buffers[b];
        const u8 *base = (const u8 *)(uintptr_t)(c->bufInfo.base_paddr + cfg->offset);
        u32 stride = (cfg->flags[1] >> 16) & 0xFFF;
        u32 count = cfg->flags[1] >> 28;
        u64 perm = cfg->flags[0] | ((u64)(cfg->flags[1] & 0xFFFF) << 32);
        u32 bytes = vertices * stride;
        if (bytes == 0)
            continue;
        pglBindBuffer(GL_ARRAY_BUFFER, sVbo[b]);
        pglBufferData(GL_ARRAY_BUFFER, bytes, base, GL_STREAM_DRAW);
        u32 offset = 0;
        for (u32 j = 0; j < count; ++j)
        {
            u32 id = (u32)(perm >> (j * 4)) & 0xF;
            if (id >= 12)
            {
                offset += 4 * (id - 11); /* padding: 4, 8, 12, 16 bytes */
                continue;
            }
            u32 fmtBits = id < 8 ? (ai->flags[0] >> (id * 4)) & 0xF : (ai->flags[1] >> ((id - 8) * 4)) & 0xF;
            u32 type = fmtBits & 3, n = (fmtBits >> 2) + 1;
            u32 reg = (u32)(ai->permutation >> (id * 4)) & 0xF;
            pglEnableVertexAttribArray(reg);
            pglVertexAttribPointer(reg, (GLint)n, sAttribType[type], GL_FALSE, (GLsizei)stride,
                                   (const void *)(uintptr_t)offset);
            offset += n * sAttribSize[type];
            fromBuffer[id] = true;
        }
    }
    /* Fixed attributes: the value written for the attribute's index. */
    for (int id = 0; id < ai->attrCount; ++id)
    {
        if (fromBuffer[id])
            continue;
        u32 reg = (u32)(ai->permutation >> (id * 4)) & 0xF;
        const C3D_FVec *v = &c->fixedAttribs[id];
        pglVertexAttrib4f(reg, v->x, v->y, v->z, v->w);
    }
}

static GLenum Primitive(GPU_Primitive_t p)
{
    switch (p)
    {
    case GPU_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
    case GPU_TRIANGLE_FAN: return GL_TRIANGLE_FAN;
    default: return GL_TRIANGLES;
    }
}

static void Execute(const PendingDraw *d)
{
    GLuint program = PcProgram_Gl(d->programKind);
    if (program == 0 || !BindTarget(d))
        return;
    pglUseProgram(program);
    pglBindVertexArray(sVao);
    ApplyFixedFunction(&d->ctx);
    ApplyProgramUniforms(program, d);
    BindTextures(program, d);
    if (d->op == 0)
    {
        SetupAttributes(&d->ctx, (u32)(d->first + d->count));
        pglDrawArrays(Primitive(d->prim), d->first, d->count);
    }
    else if (d->op == 1)
    {
        u32 maxIndex = 0;
        if (d->type == C3D_UNSIGNED_SHORT)
        {
            const u16 *idx = d->indices;
            for (int i = 0; i < d->count; ++i)
                if (idx[i] > maxIndex) maxIndex = idx[i];
        }
        else
        {
            const u8 *idx = d->indices;
            for (int i = 0; i < d->count; ++i)
                if (idx[i] > maxIndex) maxIndex = idx[i];
        }
        SetupAttributes(&d->ctx, maxIndex + 1);
        u32 bytes = (u32)d->count * (d->type == C3D_UNSIGNED_SHORT ? 2 : 1);
        pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sIbo);
        pglBufferData(GL_ELEMENT_ARRAY_BUFFER, bytes, d->indices, GL_STREAM_DRAW);
        pglDrawElements(Primitive(d->prim), d->count,
                        d->type == C3D_UNSIGNED_SHORT ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, NULL);
    }
    else
    {
        int attrs = d->ctx.attrInfo.attrCount;
        u32 vertices = d->immFloats / (4 * (u32)attrs);
        for (int i = 0; i < 16; ++i)
            pglDisableVertexAttribArray((GLuint)i);
        pglBindBuffer(GL_ARRAY_BUFFER, sVbo[0]);
        pglBufferData(GL_ARRAY_BUFFER, d->immFloats * sizeof(float), d->imm, GL_STREAM_DRAW);
        for (int id = 0; id < attrs; ++id)
        {
            u32 reg = (u32)(d->ctx.attrInfo.permutation >> (id * 4)) & 0xF;
            pglEnableVertexAttribArray(reg);
            pglVertexAttribPointer(reg, 4, GL_FLOAT, GL_FALSE, attrs * 16, (const void *)(uintptr_t)(id * 16));
        }
        pglDrawArrays(Primitive(d->prim), 0, (GLsizei)vertices);
    }
    sDrawColor->gpuNewer = true;
}

void PcC3d_FlushPending(void)
{
    if (sFlushing || sPendingCount == 0)
        return;
    sFlushing = true;
    /* What the CPU wrote since the last run is looked for once per run. */
    PcSurface_InvalidateHashes();
    for (u32 i = 0; i < sPendingCount; ++i)
    {
        Execute(&sPending[i]);
        free(sPending[i].imm);
    }
    sPendingCount = 0;
    sFlushing = false;
}

void C3D_DrawArrays(GPU_Primitive_t primitive, int first, int size)
{
    if (size <= 0)
        return;
    PendingDraw *d = Record(0);
    if (d == NULL)
        return;
    d->prim = primitive;
    d->first = first;
    d->count = size;
}

void C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void *indices)
{
    if (count <= 0 || indices == NULL)
        return;
    PendingDraw *d = Record(1);
    if (d == NULL)
        return;
    d->prim = primitive;
    d->count = count;
    d->type = type;
    d->indices = indices;
}

/* Immediate mode: vertices sent attribute by attribute, drawn at the end. */
static float *sImm;
static u32 sImmFloats, sImmCap;
static GPU_Primitive_t sImmPrim;

void C3D_ImmDrawBegin(GPU_Primitive_t primitive)
{
    sImmPrim = primitive;
    sImmFloats = 0;
}

void C3D_ImmSendAttrib(float x, float y, float z, float w)
{
    if (sImmFloats + 4 > sImmCap)
    {
        u32 cap = sImmCap ? sImmCap * 2 : 1024;
        float *p = realloc(sImm, cap * sizeof(float));
        if (p == NULL)
            return;
        sImm = p;
        sImmCap = cap;
    }
    sImm[sImmFloats++] = x;
    sImm[sImmFloats++] = y;
    sImm[sImmFloats++] = z;
    sImm[sImmFloats++] = w;
}

void C3D_ImmDrawEnd(void)
{
    if (sCtx.attrInfo.attrCount <= 0 || sImmFloats == 0)
        return;
    PendingDraw *d = Record(2);
    if (d == NULL)
        return;
    d->prim = sImmPrim;
    d->imm = malloc(sImmFloats * sizeof(float));
    if (d->imm == NULL)
    {
        --sPendingCount;
        return;
    }
    memcpy(d->imm, sImm, sImmFloats * sizeof(float));
    d->immFloats = sImmFloats;
}

/* ── For the display: a render target's GL texture, up to date ──────────── */

GLuint PcC3d_TargetTexture(C3D_RenderTarget *target)
{
    if (target == NULL || target->frameBuf.colorBuf == NULL)
        return 0;
    PcSurface *s = PcSurface_Find(target->frameBuf.colorBuf);
    if (s == NULL)
        return 0;
    PcSurface_SyncToGl(s);
    PcSurface_Resolve(s);
    return s->tex;
}

/* Its scale (the quality's): the texture is that many times the screen. */
int PcC3d_TargetScale(C3D_RenderTarget *target)
{
    PcSurface *s = target && target->frameBuf.colorBuf ? PcSurface_Find(target->frameBuf.colorBuf) : NULL;
    return s ? s->scale : 1;
}
