/*
 * Ambient particles over the top screen (OPTIONS > DISPLAY > PICTURE >
 * PARTICLES, J8): falling leaves in the woods, petals on the flower routes,
 * fireflies outdoors at night, dust in the caves (pc_ambience.h; the place
 * comes from the game side, pc_port/game/pc_game_ambience.c).
 *
 * Drawn in the game's own pixels: every particle is a few solid blocks on the
 * top screen's pixel grid (its scale in the window), so it sits with the
 * pixel art rather than over it. A leaf flutters by changing between two
 * shapes; a firefly pulses. Screen-space and sparse: an ambience.
 *
 * Showers (DYNAMIC WEATHER): on the routes, under a clear sky, the PC's own
 * cycle of dry spells (6-14 minutes) and showers (2-4 minutes), faded in and
 * out, drawn as slanting streaks with the world a little greyer. Seen only:
 * the game's weather - which carries into battles - stays its own, and the
 * game's real rain, sand and ash are left alone (no shower then).
 */
#include "pc_c3d_internal.h"
#include "../pc_ambience.h"
#include "../pc_layout.h"

#include <math.h>
#include <stdlib.h>

#define MAX_PARTICLES 48
#define MAX_BLOCKS 6

typedef struct
{
    float x, y;       /* in top-screen pixels, 0..400 x 0..240 */
    float vx, vy;
    float phase, speed;
    int kind;         /* PC_AMBIENCE_* or PC_AMBIENCE_NIGHT for a firefly */
    int tint;
} Particle;

static Particle sParticles[MAX_PARTICLES];
static int sCount, sKind = -1;
static GLuint sProgram, sVao, sVbo;
static GLint sUniformRect;
static unsigned sSeed = 12345;

/* The showers: their cycle and their streaks. */
#define MAX_DROPS 90
static struct { float x, y, speed; } sDrops[MAX_DROPS];
static float sRain, sCycle = 300.0f; /* intensity 0..1; seconds left in this spell */
static bool sRaining, sDropsMade;

static float Random(void)
{
    sSeed = sSeed * 1664525u + 1013904223u;
    return (sSeed >> 8) / 16777216.0f;
}

static const char sVs[] =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"  /* window pixels */
    "layout(location = 1) in vec4 aCol;\n"
    "uniform vec2 uSize;\n"
    "out vec4 vCol;\n"
    "void main() {\n"
    "    vCol = aCol;\n"
    "    gl_Position = vec4(aPos.x / uSize.x * 2.0 - 1.0, 1.0 - aPos.y / uSize.y * 2.0, 0.0, 1.0);\n"
    "}\n";
static const char sFs[] =
    "#version 330 core\n"
    "in vec4 vCol;\n"
    "out vec4 fragColor;\n"
    "void main() { fragColor = vCol; }\n";

static bool Init(void)
{
    const char *src[2] = { sVs, sFs };
    GLenum types[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    GLint ok = 0;

    sProgram = pglCreateProgram();
    for (int i = 0; i < 2; ++i)
    {
        GLuint s = pglCreateShader(types[i]);
        pglShaderSource(s, 1, &src[i], NULL);
        pglCompileShader(s);
        pglGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
            return false;
        pglAttachShader(sProgram, s);
    }
    pglBindFragDataLocation(sProgram, 0, "fragColor");
    pglLinkProgram(sProgram);
    sUniformRect = pglGetUniformLocation(sProgram, "uSize");
    pglGenVertexArrays(1, &sVao);
    pglGenBuffers(1, &sVbo);
    pglBindVertexArray(sVao);
    pglBindBuffer(GL_ARRAY_BUFFER, sVbo);
    pglEnableVertexAttribArray(0);
    pglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (const void *)0);
    pglEnableVertexAttribArray(1);
    pglVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (const void *)(2 * sizeof(float)));
    pglBindVertexArray(0);
    return true;
}

static void Spawn(Particle *p, int kind, bool anywhere)
{
    p->kind = kind;
    p->phase = Random() * 6.2832f;
    p->tint = (int)(Random() * 3.0f);
    p->x = Random() * 400.0f;
    p->y = anywhere ? Random() * 240.0f : -4.0f;
    switch (kind)
    {
    case PC_AMBIENCE_LEAVES:
        p->vx = 6.0f + Random() * 8.0f;   /* a light wind to the right */
        p->vy = 14.0f + Random() * 10.0f;
        p->speed = 2.0f + Random() * 2.0f;
        break;
    case PC_AMBIENCE_PETALS:
        p->vx = 4.0f + Random() * 10.0f;
        p->vy = 8.0f + Random() * 6.0f;
        p->speed = 2.5f + Random() * 2.5f;
        break;
    case PC_AMBIENCE_DUST:
        p->vx = (Random() - 0.5f) * 3.0f;
        p->vy = (Random() - 0.3f) * 2.0f;
        p->speed = 0.5f + Random();
        p->y = Random() * 240.0f;
        break;
    default: /* a firefly */
        p->vx = (Random() - 0.5f) * 8.0f;
        p->vy = (Random() - 0.5f) * 6.0f;
        p->speed = 1.0f + Random() * 1.5f;
        p->y = 60.0f + Random() * 180.0f;
        break;
    }
}

/* The population for this place: a mix of its kind and, at night, fireflies. */
static void Populate(int ambience)
{
    int kind = ambience & PC_AMBIENCE_KIND;
    bool night = (ambience & PC_AMBIENCE_NIGHT) != 0;
    int main = kind == PC_AMBIENCE_LEAVES ? (night ? 8 : 16) : kind == PC_AMBIENCE_PETALS ? (night ? 8 : 18)
             : kind == PC_AMBIENCE_DUST ? 22 : 0;
    int flies = night && kind != PC_AMBIENCE_DUST ? 12 : 0;

    sCount = 0;
    for (int i = 0; i < main && sCount < MAX_PARTICLES; ++i)
        Spawn(&sParticles[sCount++], kind, true);
    for (int i = 0; i < flies && sCount < MAX_PARTICLES; ++i)
        Spawn(&sParticles[sCount++], PC_AMBIENCE_NIGHT, true);
}

static void Step(float dt)
{
    for (int i = 0; i < sCount; ++i)
    {
        Particle *p = &sParticles[i];
        p->phase += p->speed * dt;
        float sway = sinf(p->phase);
        if (p->kind == PC_AMBIENCE_NIGHT || p->kind == PC_AMBIENCE_DUST)
        {
            /* Wandering: a slow turn of the heading. */
            float turn = (Random() - 0.5f) * 0.6f;
            float c = cosf(turn), s = sinf(turn);
            float vx = p->vx * c - p->vy * s, vy = p->vx * s + p->vy * c;
            p->vx = vx;
            p->vy = vy;
            p->x += p->vx * dt;
            p->y += p->vy * dt;
            if (p->x < -4 || p->x > 404 || p->y < -4 || p->y > 244)
                Spawn(p, p->kind, true);
            continue;
        }
        p->x += (p->vx + sway * 10.0f) * dt;
        p->y += p->vy * dt * (0.7f + 0.3f * fabsf(sway));
        if (p->y > 244.0f || p->x > 404.0f)
            Spawn(p, p->kind, false);
    }
}

static int PutBlock(float *v, int n, float x, float y, float w, float h, const float col[4])
{
    const float quad[6][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    for (int k = 0; k < 6; ++k)
    {
        float *o = v + (n + k) * 6;
        o[0] = x + quad[k][0] * w;
        o[1] = y + quad[k][1] * h;
        o[2] = col[0];
        o[3] = col[1];
        o[4] = col[2];
        o[5] = col[3];
    }
    return n + 6;
}

/* The showers' cycle, once a frame: how much it rains (0..1). */
float PcAmbience_Weather(int ambience, bool enabled, float dt)
{
    bool route = enabled && (ambience & PC_AMBIENCE_ROUTE) != 0;

    sCycle -= dt;
    if (sCycle <= 0.0f)
    {
        sRaining = !sRaining;
        sCycle = sRaining ? 120.0f + Random() * 120.0f : 360.0f + Random() * 480.0f;
    }
    float target = route && sRaining ? 1.0f : 0.0f;
    float step = dt / 6.0f; /* six seconds to come or go */
    sRain = sRain < target ? fminf(target, sRain + step) : fmaxf(target, sRain - step * 2.0f);
    return sRain;
}

static int PutBlock(float *v, int n, float x, float y, float w, float h, const float col[4]);

/* The streaks: a slant of three blocks, falling to the right. */
static int Rain(float *v, int n, const PcRect *top, float px, float py, float dt)
{
    const float col[4] = { 0.78f, 0.85f, 1.0f, 0.30f + 0.25f * sRain };
    int count = (int)(MAX_DROPS * sRain);

    if (!sDropsMade)
    {
        for (int i = 0; i < MAX_DROPS; ++i)
        {
            sDrops[i].x = Random() * 440.0f - 40.0f;
            sDrops[i].y = Random() * 240.0f;
            sDrops[i].speed = 190.0f + Random() * 70.0f;
        }
        sDropsMade = true;
    }
    for (int i = 0; i < count; ++i)
    {
        sDrops[i].y += sDrops[i].speed * dt;
        sDrops[i].x += sDrops[i].speed * 0.25f * dt;
        if (sDrops[i].y > 240.0f)
        {
            sDrops[i].y -= 248.0f;
            sDrops[i].x = Random() * 440.0f - 40.0f;
        }
        if (sDrops[i].x < 0 || sDrops[i].x >= 396 || sDrops[i].y < 0 || sDrops[i].y >= 234)
            continue;
        float x = top->x + floorf(sDrops[i].x) * px, y = top->y + floorf(sDrops[i].y) * py;
        for (int k = 0; k < 3; ++k)
            n = PutBlock(v, n, x + k * px, y + k * 2 * py, px, 2 * py, col);
    }
    return n;
}

void PcAmbience_Draw(const PcRect *top, int fbW, int fbH, int ambience, float dt)
{
    static float verts[(MAX_PARTICLES * MAX_BLOCKS + MAX_DROPS * 3) * 6 * 6];
    static const float sLeaf[3][4] = { { 0.38f, 0.62f, 0.20f, 0.9f }, { 0.55f, 0.70f, 0.22f, 0.9f },
                                       { 0.72f, 0.55f, 0.18f, 0.9f } };
    static const float sPetal[3][4] = { { 1.00f, 0.72f, 0.82f, 0.9f }, { 1.00f, 0.95f, 0.97f, 0.9f },
                                        { 0.98f, 0.62f, 0.74f, 0.9f } };
    int n = 0;

    if ((ambience & ~PC_AMBIENCE_ROUTE) != sKind)
    {
        sKind = ambience & ~PC_AMBIENCE_ROUTE;
        Populate(sKind);
    }
    if ((sCount == 0 && sRain <= 0.0f) || top->w <= 0.0f)
        return;
    if (sProgram == 0 && !Init())
        return;
    Step(dt > 0.1f ? 0.1f : dt);

    float px = top->w / 400.0f, py = top->h / 240.0f; /* a game pixel */
    for (int i = 0; i < sCount; ++i)
    {
        const Particle *p = &sParticles[i];
        /* On the game's pixel grid. */
        float x = top->x + floorf(p->x) * px, y = top->y + floorf(p->y) * py;
        if (p->x < 0 || p->x >= 398 || p->y < 0 || p->y >= 238)
            continue;
        if (p->kind == PC_AMBIENCE_LEAVES || p->kind == PC_AMBIENCE_PETALS)
        {
            const float *c = p->kind == PC_AMBIENCE_LEAVES ? sLeaf[p->tint] : sPetal[p->tint];
            bool flat = sinf(p->phase) > 0.0f; /* the flutter: two shapes */
            if (p->kind == PC_AMBIENCE_LEAVES)
                n = flat ? PutBlock(verts, n, x, y, 4 * px, 2 * py, c) : PutBlock(verts, n, x + px, y, 2 * px, 4 * py, c);
            else
                n = flat ? PutBlock(verts, n, x, y, 3 * px, 2 * py, c) : PutBlock(verts, n, x, y, 2 * px, 3 * py, c);
        }
        else if (p->kind == PC_AMBIENCE_DUST)
        {
            const float c[4] = { 0.92f, 0.90f, 0.82f, 0.28f };
            n = PutBlock(verts, n, x, y, px, py, c);
        }
        else
        {
            /* A firefly: a bright pixel and its glow, pulsing. */
            float glow = 0.5f + 0.5f * sinf(p->phase * 2.0f);
            const float core[4] = { 1.0f, 0.97f, 0.55f, 0.35f + 0.65f * glow };
            const float halo[4] = { 0.85f, 1.0f, 0.45f, 0.18f * glow };
            n = PutBlock(verts, n, x - px, y - py, 3 * px, 3 * py, halo);
            n = PutBlock(verts, n, x, y, px, py, core);
        }
    }
    if (sRain > 0.0f)
        n = Rain(verts, n, top, px, py, dt > 0.1f ? 0.1f : dt);
    if (n == 0)
        return;
    pglUseProgram(sProgram);
    pglUniform2f(sUniformRect, (float)fbW, (float)fbH);
    pglBindVertexArray(sVao);
    pglBindBuffer(GL_ARRAY_BUFFER, sVbo);
    pglBufferData(GL_ARRAY_BUFFER, n * 6 * sizeof(float), verts, GL_STREAM_DRAW);
    pglEnable(GL_BLEND);
    pglBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    pglDrawArrays(GL_TRIANGLES, 0, n);
    pglDisable(GL_BLEND);
    pglBindVertexArray(0);
}

/* Flat shapes over everything (FUSION's interface, pc_hud.c): `count`
 * rectangles of 8 floats (x, y, w, h in window pixels, then r, g, b, a). */
#define PC_OVERLAY_MAX 4096
void PcOverlay_Draw(const float *rects, int count, int fbW, int fbH)
{
    static float verts[PC_OVERLAY_MAX * 6 * 6];
    int n = 0;

    if (count <= 0 || (sProgram == 0 && !Init()))
        return;
    if (count > PC_OVERLAY_MAX)
        count = PC_OVERLAY_MAX;
    for (int i = 0; i < count; ++i)
    {
        const float *r = rects + i * 8;
        n = PutBlock(verts, n, r[0], r[1], r[2], r[3], r + 4);
    }
    pglUseProgram(sProgram);
    pglUniform2f(sUniformRect, (float)fbW, (float)fbH);
    pglBindVertexArray(sVao);
    pglBindBuffer(GL_ARRAY_BUFFER, sVbo);
    pglBufferData(GL_ARRAY_BUFFER, n * 6 * sizeof(float), verts, GL_STREAM_DRAW);
    pglEnable(GL_BLEND);
    pglBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    pglDrawArrays(GL_TRIANGLES, 0, n);
    pglDisable(GL_BLEND);
    pglBindVertexArray(0);
}
