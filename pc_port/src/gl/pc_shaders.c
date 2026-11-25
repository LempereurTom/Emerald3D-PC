/*
 * The PICA200's programmable stages, in GLSL 3.30.
 *
 * Vertex shaders: each 3DS shader the port uses is rewritten by hand, reading
 * the same float uniform registers (uC[96], filled from citro3d's
 * C3D_FVUnif) and the same input registers (v0..v15, attribute locations), so
 * the code that sets them up runs unchanged. A shader binary is recognised by
 * a marker ("PCSH:name[,name...]") where the console has a picasso .shbin.
 *
 * Fragment stage: one shader emulates the PICA's fixed fragment pipeline as
 * configured by citro3d: six texture combiner stages (sources, operands,
 * functions, scales, the combiner buffer), the procedural texture as citro2d
 * uses it, the alpha test and the inverted scissor. Blending, depth, stencil,
 * logic ops and the normal scissor are GL's (pc_c3d.c).
 */
#include "pc_c3d_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render2d_shbin.h"

const u8 render2d_shbin[] = "PCSH:render2d";
const u32 render2d_shbin_size = sizeof(render2d_shbin);

/* ── GLSL ───────────────────────────────────────────────────────────────── */

#define GLSL_VERTEX_COMMON                                                           \
    "#version 330 core\n"                                                            \
    "uniform vec4 uC[96];\n"                                                         \
    "uniform ivec4 uI[4];\n"                                                         \
    "uniform int uB;\n"                                                              \
    "uniform vec2 uDepthMap;\n"                                                      \
    "out vec4 vColor;\n"                                                             \
    "out vec2 vTc0;\n"                                                               \
    "out vec2 vTc1;\n"                                                               \
    "out vec2 vTc2;\n"                                                               \
    /* PICA clip space -> GL: x, y and w as they are; the PICA's depth is */         \
    /* z/w * zScale + zOffset in [0, 1] (citro3d C3D_DepthMap), GL's is */           \
    /* (z/w + 1) / 2. */                                                             \
    "vec4 PicaPosition(vec4 p) {\n"                                                  \
    "    float depth = p.z * uDepthMap.x + p.w * uDepthMap.y;\n"                     \
    "    return vec4(p.x, p.y, 2.0 * depth - p.w, p.w);\n"                           \
    "}\n"

/* citro2d source/render2d.v.pica */
static const char sRender2dVs[] = GLSL_VERTEX_COMMON
    "layout(location = 0) in vec4 v0;\n" /* position */
    "layout(location = 1) in vec4 v1;\n" /* texcoord */
    "layout(location = 2) in vec4 v2;\n" /* proctex coord */
    "layout(location = 3) in vec4 v3;\n" /* colour, 0..255 */
    "void main() {\n"
    "    vec4 r0 = vec4(v0.xyz, 1.0);\n"
    "    vec4 r1 = vec4(dot(uC[0], r0), dot(uC[1], r0), dot(uC[2], r0), dot(uC[3], r0));\n"
    "    vec4 p = vec4(dot(uC[4], r1), dot(uC[5], r1), dot(uC[6], r1), dot(uC[7], r1));\n"
    "    gl_Position = PicaPosition(p);\n"
    "    vTc0 = v1.xy;\n"
    "    vTc1 = v2.xy;\n"
    "    vTc2 = vec2(0.0);\n"
    "    vColor = v3 * 0.003921568393707275390625;\n"
    "}\n";

/*
 * 3ds_port src/voxel/voxel.v.pica, instruction for instruction (see its
 * comments). Registers, the same in both voxel programs as on the console:
 * projection c0-c3, modelView c4-c7, origin c8, shadeTint c9, tintDiff c10,
 * grade c11, fog c12, dappleU c13, dappleV c14. The PICA clamps the colour it
 * hands the fragment stage to [0, 1]; GL does not, so it is clamped here.
 */
static const char sVoxelVs[] = GLSL_VERTEX_COMMON
    "layout(location = 0) in vec4 v0;\n" /* x, y, z, shade (shorts) */
    "layout(location = 1) in vec4 v1;\n" /* u, v */
    "void main() {\n"
    "    vec4 r0 = vec4(0.001953125 * v0.xyz, 1.0);\n"
    "    vec4 r7 = uC[8] + r0;\n"
    "    vec4 r1 = vec4(dot(uC[4], r7), dot(uC[5], r7), dot(uC[6], r7), dot(uC[7], r7));\n"
    "    gl_Position = PicaPosition(vec4(dot(uC[0], r1), dot(uC[1], r1), dot(uC[2], r1), dot(uC[3], r1)));\n"
    "    vTc0 = v1.xy;\n"
    "    vTc2 = v1.xy;\n"
    "    vTc1 = vec2(dot(uC[13], r0), dot(uC[14], r0));\n"
    "    float shade = 0.00006103515625 * v0.w;\n"
    "    float t = clamp(shade * uC[11].x + uC[11].y, 0.0, 1.0);\n"
    "    vec3 colour = shade * (uC[9].xyz + uC[10].xyz * t);\n"
    "    float haze = clamp(uC[12].x * r1.z + uC[12].y, 0.0, uC[12].z);\n"
    "    vColor = clamp(vec4(colour, 1.0 - haze), 0.0, 1.0);\n"
    "}\n";

/*
 * 3ds_port src/voxel/voxel_weather.v.pica (entry weatherMain): the rain's
 * streaks, the sand's and the ash's flakes. Its own uniforms follow the
 * world's: boxOffset c15, boxSize c16, boxBase c17, across c18, along c19.
 */
static const char sWeatherVs[] = GLSL_VERTEX_COMMON
    "layout(location = 0) in vec4 v0;\n" /* x, y, z in the box, alpha (shorts) */
    "layout(location = 1) in vec4 v1;\n" /* the corner: across, along (bytes) */
    "void main() {\n"
    "    vec4 r0 = 0.000030517578125 * v0 + uC[15];\n"
    "    r0 = r0 - floor(r0);\n"
    "    vec4 r2 = 0.5 - abs(r0 - 0.5);\n"
    "    float fade = min(min(uC[16].w * r2.x, uC[15].w * r2.z), uC[17].w * r2.y);\n"
    "    fade = min(1.0, fade);\n"
    "    vec4 r4 = uC[17] + uC[16] * r0;\n"
    "    vec4 r5 = 0.015625 * v1;\n"
    "    r4 = r4 + uC[18] * r5.x + uC[19] * r5.y;\n"
    "    r4.w = 1.0;\n"
    "    vec4 r1 = vec4(dot(uC[4], r4), dot(uC[5], r4), dot(uC[6], r4), dot(uC[7], r4));\n"
    "    gl_Position = PicaPosition(vec4(dot(uC[0], r1), dot(uC[1], r1), dot(uC[2], r1), dot(uC[3], r1)));\n"
    "    float clear = 1.0 - clamp(uC[12].x * r1.z + uC[12].y, 0.0, uC[12].z);\n"
    "    float lens = clamp(uC[19].w * (-uC[18].w - r1.z), 0.0, 1.0);\n"
    "    float a = 0.000030517578125 * v0.w * fade * lens * clear;\n"
    "    vTc0 = vec2(0.0);\n"
    "    vTc1 = vec2(0.0);\n"
    "    vTc2 = vec2(0.0);\n"
    "    vColor = clamp(vec4(a, a, a, clear), 0.0, 1.0);\n"
    "}\n";

static const char sFragment[] =
    "#version 330 core\n"
    "in vec4 vColor;\n"
    "in vec2 vTc0;\n"
    "in vec2 vTc1;\n"
    "in vec2 vTc2;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D uTex0;\n"
    "uniform sampler2D uTex1;\n"
    "uniform sampler2D uTex2;\n"
    "uniform int uTexOn[3];\n"
    "uniform ivec4 uStage[6];\n"      /* srcRgb, srcAlpha, opRgb|opAlpha<<12, funcs|scales */
    "uniform vec4 uStageColor[6];\n"
    "uniform int uBufUpdate;\n"
    "uniform vec4 uBufColor;\n"
    "uniform int uAlphaFunc;\n"
    "uniform int uAlphaRef;\n"
    "uniform int uScissorInvert;\n"
    "uniform ivec4 uScissor;\n"
    "uniform int uPtOn;\n"
    "uniform ivec4 uPtCfg;\n"         /* uClamp, vClamp, rgbFunc, alphaFunc */
    "uniform int uPtCoord;\n"
    "uniform vec4 uPtLutRgb[64];\n"   /* 128 x (value, step) */
    "uniform vec4 uPtLutA[64];\n"
    "\n"
    "float PtClamp(float x, int mode) {\n"
    "    if (mode == 0) return x > 1.0 ? 0.0 : x;\n"
    "    if (mode == 1) return min(x, 1.0);\n"
    "    if (mode == 2) return fract(x);\n"
    "    if (mode == 3) { float f = fract(x); return (int(floor(x)) % 2 == 0) ? f : 1.0 - f; }\n"
    "    return x > 0.5 ? 1.0 : 0.0;\n"
    "}\n"
    "float PtFunc(int f, float u, float v) {\n"
    "    if (f == 0) return u;\n"
    "    if (f == 1) return u * u;\n"
    "    if (f == 2) return v;\n"
    "    if (f == 3) return v * v;\n"
    "    if (f == 4) return (u + v) * 0.5;\n"
    "    if (f == 5) return (u * u + v * v) * 0.5;\n"
    "    if (f == 6) return min(sqrt(u * u + v * v), 1.0);\n"
    "    if (f == 7) return min(u, v);\n"
    "    if (f == 8) return max(u, v);\n"
    "    return min(((u + v) * 0.5 + sqrt(u * u + v * v)) * 0.5, 1.0);\n"
    "}\n"
    "float LutRgb(float x) {\n"
    "    float c = x * 128.0; float i = clamp(floor(c), 0.0, 127.0); float f = c - i;\n"
    "    int k = int(i) * 2; vec4 a = uPtLutRgb[k / 4]; int o = k % 4;\n"
    "    vec2 e = o == 0 ? a.xy : a.zw;\n"
    "    return clamp(e.x + e.y * f, 0.0, 1.0);\n"
    "}\n"
    "float LutA(float x) {\n"
    "    float c = x * 128.0; float i = clamp(floor(c), 0.0, 127.0); float f = c - i;\n"
    "    int k = int(i) * 2; vec4 a = uPtLutA[k / 4]; int o = k % 4;\n"
    "    vec2 e = o == 0 ? a.xy : a.zw;\n"
    "    return clamp(e.x + e.y * f, 0.0, 1.0);\n"
    "}\n"
    "vec4 ProcTex() {\n"
    "    vec2 tc = uPtCoord == 0 ? vTc0 : uPtCoord == 1 ? vTc1 : vTc2;\n"
    "    float u = PtClamp(abs(tc.x), uPtCfg.x), v = PtClamp(abs(tc.y), uPtCfg.y);\n"
    "    float rgb = LutRgb(PtFunc(uPtCfg.z, u, v));\n"
    "    float a = LutA(PtFunc(uPtCfg.w, u, v));\n"
    "    return vec4(vec3(rgb), a);\n"
    "}\n"
    "vec3 OpRgb(int op, vec4 c) {\n"
    "    if (op == 0) return c.rgb;\n"
    "    if (op == 1) return 1.0 - c.rgb;\n"
    "    if (op == 2) return vec3(c.a);\n"
    "    if (op == 3) return vec3(1.0 - c.a);\n"
    "    if (op == 4) return vec3(c.r);\n"
    "    if (op == 5) return vec3(1.0 - c.r);\n"
    "    if (op == 8) return vec3(c.g);\n"
    "    if (op == 9) return vec3(1.0 - c.g);\n"
    "    if (op == 12) return vec3(c.b);\n"
    "    if (op == 13) return vec3(1.0 - c.b);\n"
    "    return c.rgb;\n"
    "}\n"
    "float OpA(int op, vec4 c) {\n"
    "    if (op == 0) return c.a;\n"
    "    if (op == 1) return 1.0 - c.a;\n"
    "    if (op == 2) return c.r;\n"
    "    if (op == 3) return 1.0 - c.r;\n"
    "    if (op == 4) return c.g;\n"
    "    if (op == 5) return 1.0 - c.g;\n"
    "    if (op == 6) return c.b;\n"
    "    return 1.0 - c.b;\n"
    "}\n"
    "vec3 FuncRgb(int f, vec3 a, vec3 b, vec3 c) {\n"
    "    if (f == 0) return a;\n"
    "    if (f == 1) return a * b;\n"
    "    if (f == 2) return min(a + b, 1.0);\n"
    "    if (f == 3) return clamp(a + b - 0.5, 0.0, 1.0);\n"
    "    if (f == 4) return a * c + b * (1.0 - c);\n"
    "    if (f == 5) return max(a - b, 0.0);\n"
    "    if (f == 6 || f == 7) return vec3(clamp(4.0 * dot(a - 0.5, b - 0.5), 0.0, 1.0));\n"
    "    if (f == 8) return min(a * b + c, 1.0);\n"
    "    if (f == 9) return min(a + b, 1.0) * c;\n"
    "    return a;\n"
    "}\n"
    "float FuncA(int f, float a, float b, float c) {\n"
    "    if (f == 0) return a;\n"
    "    if (f == 1) return a * b;\n"
    "    if (f == 2) return min(a + b, 1.0);\n"
    "    if (f == 3) return clamp(a + b - 0.5, 0.0, 1.0);\n"
    "    if (f == 4) return a * c + b * (1.0 - c);\n"
    "    if (f == 5) return max(a - b, 0.0);\n"
    "    if (f == 8) return min(a * b + c, 1.0);\n"
    "    if (f == 9) return min(a + b, 1.0) * c;\n"
    "    return a;\n"
    "}\n"
    "void main() {\n"
    "    if (uScissorInvert != 0) {\n"
    "        vec2 p = gl_FragCoord.xy;\n"
    "        if (p.x >= float(uScissor.x) && p.x < float(uScissor.z) && p.y >= float(uScissor.y) && p.y < float(uScissor.w)) discard;\n"
    "    }\n"
    "    vec4 t0 = uTexOn[0] != 0 ? texture(uTex0, vTc0) : vec4(0.0);\n"
    "    vec4 t1 = uTexOn[1] != 0 ? texture(uTex1, vTc1) : vec4(0.0);\n"
    "    vec4 t2 = uTexOn[2] != 0 ? texture(uTex2, vTc2) : vec4(0.0);\n"
    "    vec4 t3 = uPtOn != 0 ? ProcTex() : vec4(0.0);\n"
    "    vec4 prev = vec4(0.0);\n"
    "    vec4 buf = uBufColor, nextBuf = uBufColor;\n"
    "    for (int i = 0; i < 6; ++i) {\n"
    "        ivec4 st = uStage[i];\n"
    "        vec4 src[3];\n"
    "        for (int k = 0; k < 3; ++k) {\n"
    "            for (int ch = 0; ch < 2; ++ch) {\n"
    "                int s = ((ch == 0 ? st.x : st.y) >> (4 * k)) & 15;\n"
    "                vec4 v = s == 0 ? vColor : s == 3 ? t0 : s == 4 ? t1 : s == 5 ? t2 : s == 6 ? t3\n"
    "                       : s == 13 ? buf : s == 14 ? uStageColor[i] : s == 15 ? prev : vec4(0.0);\n"
    "                if (ch == 0) src[k].rgb = OpRgb((st.z >> (4 * k)) & 15, v);\n"
    "                else src[k].a = OpA((st.z >> (12 + 4 * k)) & 15, v);\n"
    "            }\n"
    "        }\n"
    "        int fRgb = st.w & 15, fA = (st.w >> 4) & 15;\n"
    "        float sRgb = float(1 << ((st.w >> 8) & 3)), sA = float(1 << ((st.w >> 10) & 3));\n"
    "        vec3 rgb = clamp(FuncRgb(fRgb, src[0].rgb, src[1].rgb, src[2].rgb) * sRgb, 0.0, 1.0);\n"
    "        float a = fRgb == 7 ? rgb.r : clamp(FuncA(fA, src[0].a, src[1].a, src[2].a) * sA, 0.0, 1.0);\n"
    "        prev = vec4(rgb, a);\n"
    "        buf = nextBuf;\n"
    "        if (i < 4) {\n"
    "            if (((uBufUpdate >> (8 + i)) & 1) != 0) nextBuf.rgb = prev.rgb;\n"
    "            if (((uBufUpdate >> (12 + i)) & 1) != 0) nextBuf.a = prev.a;\n"
    "        }\n"
    "    }\n"
    "    if (uAlphaFunc >= 0) {\n"
    "        int a = int(prev.a * 255.0 + 0.5);\n"
    "        bool pass = uAlphaFunc == 1 || (uAlphaFunc == 2 && a == uAlphaRef) || (uAlphaFunc == 3 && a != uAlphaRef)\n"
    "            || (uAlphaFunc == 4 && a < uAlphaRef) || (uAlphaFunc == 5 && a <= uAlphaRef)\n"
    "            || (uAlphaFunc == 6 && a > uAlphaRef) || (uAlphaFunc == 7 && a >= uAlphaRef);\n"
    "        if (!pass) discard;\n"
    "    }\n"
    "    fragColor = prev;\n"
    "}\n";

/* ── Programs ───────────────────────────────────────────────────────────── */

typedef struct
{
    const char *name;
    const char *vertex;
    struct { const char *uniform; int reg; } uniforms[16];
} ProgramDef;

static const ProgramDef sDefs[PC_PROGRAM_COUNT] = {
    [PC_PROGRAM_RENDER2D] = { "render2d", sRender2dVs, { { "mdlvMtx", 0 }, { "projMtx", 4 } } },
#define VOXEL_UNIFORMS { "projection", 0 }, { "modelView", 4 }, { "origin", 8 }, { "shadeTint", 9 }, \
        { "tintDiff", 10 }, { "grade", 11 }, { "fog", 12 }, { "dappleU", 13 }, { "dappleV", 14 }
    [PC_PROGRAM_VOXEL] = { "voxel", sVoxelVs, { VOXEL_UNIFORMS } },
    [PC_PROGRAM_WEATHER] = { "weather", sWeatherVs,
                             { VOXEL_UNIFORMS, { "boxOffset", 15 }, { "boxSize", 16 }, { "boxBase", 17 },
                               { "across", 18 }, { "along", 19 } } },
};

static GLuint sPrograms[PC_PROGRAM_COUNT];

static GLuint Compile(GLenum type, const char *source, const char *what)
{
    GLuint s = pglCreateShader(type);
    pglShaderSource(s, 1, &source, NULL);
    pglCompileShader(s);
    GLint ok = 0;
    pglGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        pglGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[VIDEO] %s shader of %s does not compile:\n%s\n",
                type == GL_VERTEX_SHADER ? "vertex" : "fragment", what, log);
        pglDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint PcProgram_Gl(int kind)
{
    if (kind <= PC_PROGRAM_NONE || kind >= PC_PROGRAM_COUNT || sDefs[kind].vertex == NULL)
        return 0;
    if (sPrograms[kind])
        return sPrograms[kind];
    GLuint vs = Compile(GL_VERTEX_SHADER, sDefs[kind].vertex, sDefs[kind].name);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, sFragment, sDefs[kind].name);
    if (!vs || !fs)
        return 0;
    GLuint p = pglCreateProgram();
    pglAttachShader(p, vs);
    pglAttachShader(p, fs);
    pglBindFragDataLocation(p, 0, "fragColor");
    pglLinkProgram(p);
    GLint ok = 0;
    pglGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        pglGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "[VIDEO] program %s does not link:\n%s\n", sDefs[kind].name, log);
        return 0;
    }
    sPrograms[kind] = p;
    return p;
}

int PcProgram_UniformRegister(int kind, const char *name)
{
    if (kind <= PC_PROGRAM_NONE || kind >= PC_PROGRAM_COUNT || name == NULL)
        return -1;
    for (int i = 0; i < 16 && sDefs[kind].uniforms[i].uniform; ++i)
        if (strcmp(sDefs[kind].uniforms[i].uniform, name) == 0)
            return sDefs[kind].uniforms[i].reg;
    return -1;
}

/* ── DVLB / shader programs (libctru's API) ─────────────────────────────── *
 * A DVLE's program kind is kept in a field the PC never otherwise reads. */

static int KindOfName(const char *name, size_t len)
{
    for (int k = PC_PROGRAM_NONE + 1; k < PC_PROGRAM_COUNT; ++k)
        if (sDefs[k].name && strlen(sDefs[k].name) == len && strncmp(sDefs[k].name, name, len) == 0)
            return k;
    if (len == 5 && strncmp(name, "voxel", 5) == 0)
        return PC_PROGRAM_VOXEL;
    if (len == 7 && strncmp(name, "weather", 7) == 0)
        return PC_PROGRAM_WEATHER;
    return PC_PROGRAM_NONE;
}

DVLB_s *DVLB_ParseFile(u32 *shbinData, u32 shbinSize)
{
    const char *text = (const char *)shbinData;
    if (shbinData == NULL || shbinSize < 6 || strncmp(text, "PCSH:", 5) != 0)
    {
        fprintf(stderr, "[VIDEO] shader binary of %u bytes is not a PC shader marker\n", (unsigned)shbinSize);
        return NULL;
    }
    const char *names = text + 5;
    size_t len = strnlen(names, shbinSize - 5);
    u32 count = 1;
    for (size_t i = 0; i < len; ++i)
        if (names[i] == ',')
            ++count;
    DVLB_s *dvlb = calloc(1, sizeof(*dvlb));
    if (dvlb == NULL)
        return NULL;
    dvlb->numDVLE = count;
    dvlb->DVLE = calloc(count, sizeof(DVLE_s));
    if (dvlb->DVLE == NULL)
    {
        free(dvlb);
        return NULL;
    }
    const char *start = names;
    for (u32 i = 0; i < count; ++i)
    {
        const char *end = memchr(start, ',', len - (size_t)(start - names));
        size_t n = end ? (size_t)(end - start) : len - (size_t)(start - names);
        dvlb->DVLE[i].type = VERTEX_SHDR;
        dvlb->DVLE[i].dvlp = &dvlb->DVLP;
        dvlb->DVLE[i].outmapClock = (u32)KindOfName(start, n);
        if (dvlb->DVLE[i].outmapClock == PC_PROGRAM_NONE)
            fprintf(stderr, "[VIDEO] unknown PC shader \"%.*s\"\n", (int)n, start);
        start = end ? end + 1 : start + n;
    }
    return dvlb;
}

void DVLB_Free(DVLB_s *dvlb)
{
    if (dvlb == NULL)
        return;
    free(dvlb->DVLE);
    free(dvlb);
}

s8 DVLE_GetUniformRegister(DVLE_s *dvle, const char *name)
{
    return dvle ? (s8)PcProgram_UniformRegister((int)dvle->outmapClock, name) : -1;
}

void DVLE_GenerateOutmap(DVLE_s *dvle) { (void)dvle; }

Result shaderInstanceInit(shaderInstance_s *si, DVLE_s *dvle)
{
    memset(si, 0, sizeof(*si));
    si->dvle = dvle;
    return 0;
}

Result shaderInstanceFree(shaderInstance_s *si)
{
    free(si);
    return 0;
}

Result shaderInstanceSetBool(shaderInstance_s *si, int id, bool value)
{
    if (si == NULL || id < 0 || id > 15)
        return -1;
    si->boolUniforms = (u16)((si->boolUniforms & ~BIT(id)) | (value ? BIT(id) : 0));
    return 0;
}

Result shaderInstanceGetBool(shaderInstance_s *si, int id, bool *value)
{
    if (si == NULL || id < 0 || id > 15 || value == NULL)
        return -1;
    *value = (si->boolUniforms >> id) & 1;
    return 0;
}

s8 shaderInstanceGetUniformLocation(shaderInstance_s *si, const char *name)
{
    return si ? DVLE_GetUniformRegister(si->dvle, name) : -1;
}

Result shaderProgramInit(shaderProgram_s *sp)
{
    memset(sp, 0, sizeof(*sp));
    return 0;
}

Result shaderProgramFree(shaderProgram_s *sp)
{
    if (sp == NULL)
        return -1;
    shaderInstanceFree(sp->vertexShader);
    shaderInstanceFree(sp->geometryShader);
    sp->vertexShader = sp->geometryShader = NULL;
    return 0;
}

Result shaderProgramSetVsh(shaderProgram_s *sp, DVLE_s *dvle)
{
    if (sp == NULL || dvle == NULL)
        return -1;
    shaderInstanceFree(sp->vertexShader);
    sp->vertexShader = malloc(sizeof(shaderInstance_s));
    if (sp->vertexShader == NULL)
        return -1;
    return shaderInstanceInit(sp->vertexShader, dvle);
}

Result shaderProgramSetGsh(shaderProgram_s *sp, DVLE_s *dvle, u8 stride)
{
    (void)sp; (void)dvle; (void)stride;
    return -1; /* no geometry shaders in the port */
}

Result shaderProgramSetGshInputPermutation(shaderProgram_s *sp, u64 permutation)
{
    (void)sp; (void)permutation;
    return 0;
}

Result shaderProgramConfigure(shaderProgram_s *sp, bool sendVshCode, bool sendGshCode)
{
    (void)sp; (void)sendVshCode; (void)sendGshCode;
    return 0;
}

Result shaderProgramUse(shaderProgram_s *sp)
{
    C3D_BindProgram(sp);
    return 0;
}

int PcProgram_KindOf(const shaderProgram_s *program)
{
    if (program == NULL || program->vertexShader == NULL || program->vertexShader->dvle == NULL)
        return PC_PROGRAM_NONE;
    return (int)program->vertexShader->dvle->outmapClock;
}
