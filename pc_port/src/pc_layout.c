/*
 * Where the two screens go in the window: see pc_layout.h.
 */
#include "pc_layout.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TOP_W 400
#define TOP_H 240
#define BOTTOM_W 320
#define BOTTOM_H 240
/* The small screen of the inset layout, against the big one's scale. */
#define INSET_FACTOR 0.5f
#define INSET_MARGIN 4.0f /* at 1x */

static const char *const sNames[PC_LAYOUT_COUNT] = { "vertical", "side", "inset", "single", "fusion" };

const char *PcLayout_Name(PcLayoutKind kind)
{
    return kind >= 0 && kind < PC_LAYOUT_COUNT ? sNames[kind] : "?";
}

int PcLayout_Parse(const char *text)
{
    char *end;
    long n;

    if (text == NULL || *text == '\0')
        return -1;
    for (int i = 0; i < PC_LAYOUT_COUNT; ++i)
        if (strcmp(text, sNames[i]) == 0)
            return i;
    n = strtol(text, &end, 10);
    return *end == '\0' && n >= 0 && n < PC_LAYOUT_COUNT ? (int)n : -1;
}

static PcRect Rect(float x, float y, float w, float h)
{
    PcRect r = { x, y, w, h };
    return r;
}

void PcLayout_Compute(PcLayoutKind kind, bool swap, int windowW, int windowH, PcLayout *out)
{
    PcLayout_ComputeFit(kind, swap, true, windowW, windowH, out);
}

void PcLayout_ComputeFit(PcLayoutKind kind, bool swap, bool integer, int windowW, int windowH, PcLayout *out)
{
    PcLayout_ComputeFull(kind, swap, integer, false, windowW, windowH, out);
}

/* FUSION, in window pixels: the top screen as `single` places it, then the
 * bottom screen over it (open) or its dock alone (closed). */
#define FUSION_OPEN 0.62f   /* the open bottom screen's height, of the top's */
#define FUSION_DOCK 0.46f   /* the dock's width, of the top's */
#define FUSION_MARGIN 0.025f

static void Fusion(PcLayout *out, bool open)
{
    const PcRect *t = &out->top;
    float margin = t->h * FUSION_MARGIN;

    out->bottomFirst = false;
    if (open)
    {
        float scale = fminf(t->h * FUSION_OPEN / BOTTOM_H, t->w * FUSION_OPEN / BOTTOM_W);
        float w = BOTTOM_W * scale, h = BOTTOM_H * scale;
        out->bottom = Rect(floorf(t->x + (t->w - w) / 2.0f), floorf(t->y + t->h - h - margin), w, h);
        out->bottomShown = true;
        out->topDimmed = true;
        return;
    }
    float scale = t->w * FUSION_DOCK / BOTTOM_W;
    float w = BOTTOM_W * scale, h = PC_DOCK_H * scale;
    out->bottomShown = false;
    out->stripShown = true;
    out->strip = Rect(floorf(t->x + (t->w - w) / 2.0f), floorf(t->y + t->h - h - margin), w, h);
}

void PcLayout_ComputeFull(PcLayoutKind kind, bool swap, bool integer, bool open, int windowW, int windowH,
                          PcLayout *out)
{
    bool fusion = kind == PC_LAYOUT_FUSION;
    if (fusion)
    {
        kind = PC_LAYOUT_SINGLE;
        swap = false;
    }
    /* The layout at 1x, top-left origin. */
    PcRect top = Rect(0, 0, 0, 0), bottom = Rect(0, 0, 0, 0);
    int lw, lh;

    if (kind < 0 || kind >= PC_LAYOUT_COUNT)
        kind = PC_LAYOUT_VERTICAL;
    memset(out, 0, sizeof(*out));
    out->kind = kind;
    out->swap = swap;
    out->topShown = out->bottomShown = true;
    switch (kind)
    {
    case PC_LAYOUT_SIDE:
        lw = TOP_W + BOTTOM_W;
        lh = TOP_H;
        top = Rect(swap ? BOTTOM_W : 0, 0, TOP_W, TOP_H);
        bottom = Rect(swap ? 0 : TOP_W, 0, BOTTOM_W, BOTTOM_H);
        break;
    case PC_LAYOUT_INSET:
    case PC_LAYOUT_SINGLE:
    {
        /* The big screen fills the layout; the small one, if any, in the
         * bottom-right corner over it. */
        bool bigTop = !swap;
        float sw = (bigTop ? BOTTOM_W : TOP_W) * INSET_FACTOR, sh = (bigTop ? BOTTOM_H : TOP_H) * INSET_FACTOR;
        lw = bigTop ? TOP_W : BOTTOM_W;
        lh = bigTop ? TOP_H : BOTTOM_H;
        PcRect big = Rect(0, 0, (float)lw, (float)lh);
        PcRect small = Rect(lw - sw - INSET_MARGIN, lh - sh - INSET_MARGIN, sw, sh);
        /* The big bottom screen is all buttons (its dock, its header): the
         * small top screen goes beside it, not over it. */
        if (!bigTop && kind == PC_LAYOUT_INSET)
        {
            small = Rect(lw + INSET_MARGIN, 0, sw, sh);
            lw += (int)(INSET_MARGIN + sw);
        }
        top = bigTop ? big : small;
        bottom = bigTop ? small : big;
        if (kind == PC_LAYOUT_SINGLE)
        {
            out->topShown = bigTop;
            out->bottomShown = !bigTop;
        }
        break;
    }
    case PC_LAYOUT_VERTICAL:
    default:
        lw = TOP_W;
        lh = TOP_H + BOTTOM_H;
        top = Rect(0, swap ? BOTTOM_H : 0, TOP_W, TOP_H);
        bottom = Rect((TOP_W - BOTTOM_W) / 2.0f, swap ? 0 : TOP_H, BOTTOM_W, BOTTOM_H);
        break;
    }
    out->logicalW = lw;
    out->logicalH = lh;

    /* The largest whole scale the window holds; below 1x, what fits. */
    float fit = fminf((float)windowW / lw, (float)windowH / lh);
    float scale = fit >= 1.0f && integer ? floorf(fit) : fit;
    if (scale <= 0.0f)
        scale = 1.0f;
    /* Rounded, and never negative: lw * scale may come out a hair above the
     * window when the scale is a fraction. */
    float ox = fmaxf(0.0f, floorf((windowW - lw * scale) / 2.0f + 0.5f));
    float oy = fmaxf(0.0f, floorf((windowH - lh * scale) / 2.0f + 0.5f));
    out->scale = scale;
    out->top = Rect(ox + top.x * scale, oy + top.y * scale, top.w * scale, top.h * scale);
    out->bottom = Rect(ox + bottom.x * scale, oy + bottom.y * scale, bottom.w * scale, bottom.h * scale);
    if (fusion)
    {
        out->kind = PC_LAYOUT_FUSION;
        Fusion(out, open);
    }
}

bool PcLayout_ToBottom(const PcLayout *layout, float x, float y, bool clamp, int *touchX, int *touchY)
{
    const PcRect *b = &layout->bottom;
    float u, v;

    /* FUSION's dock alone: its band of the bottom screen. */
    if (layout->stripShown && !layout->bottomShown)
    {
        const PcRect *s = &layout->strip;
        u = (x - s->x) * BOTTOM_W / s->w;
        v = (y - s->y) * PC_DOCK_H / s->h;
        if (u < 0.0f || u >= BOTTOM_W || v < 0.0f || v >= PC_DOCK_H)
        {
            if (!clamp)
                return false;
            u = fminf(fmaxf(u, 0.0f), BOTTOM_W - 1);
            v = fminf(fmaxf(v, 0.0f), PC_DOCK_H - 1);
        }
        *touchX = (int)u;
        *touchY = PC_DOCK_Y + (int)v;
        return true;
    }

    if (!layout->bottomShown || b->w <= 0.0f || b->h <= 0.0f)
        return false;
    /* With the inset's small top screen over the big bottom one, a click on
     * the top screen is not a touch. */
    if (!clamp && layout->topShown && layout->bottomFirst)
    {
        const PcRect *t = &layout->top;
        if (x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h)
            return false;
    }
    u = (x - b->x) * BOTTOM_W / b->w;
    v = (y - b->y) * BOTTOM_H / b->h;
    if (u < 0.0f || u >= BOTTOM_W || v < 0.0f || v >= BOTTOM_H)
    {
        if (!clamp)
            return false;
        u = fminf(fmaxf(u, 0.0f), BOTTOM_W - 1);
        v = fminf(fmaxf(v, 0.0f), BOTTOM_H - 1);
    }
    *touchX = (int)u;
    *touchY = (int)v;
    return true;
}
