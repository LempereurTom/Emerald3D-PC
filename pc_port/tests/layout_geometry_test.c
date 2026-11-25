/*
 * pc_layout.c alone: every layout, swapped or not, at several window sizes.
 *   gcc -I pc_port/src pc_port/tests/layout_geometry_test.c pc_port/src/pc_layout.c -lm
 * Exit status 0 when every check passes.
 */
#include "pc_layout.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int sFailed, sChecks;

#define CHECK(cond, ...)                                                                                       \
    do                                                                                                         \
    {                                                                                                          \
        ++sChecks;                                                                                             \
        if (!(cond))                                                                                           \
        {                                                                                                      \
            ++sFailed;                                                                                         \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                        \
            printf(__VA_ARGS__);                                                                               \
            printf("\n");                                                                                      \
        }                                                                                                      \
    } while (0)

static bool Inside(const PcRect *r, float x, float y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static void CheckLayout(PcLayoutKind kind, bool swap, int w, int h)
{
    PcLayout l;
    int tx, ty;
    const char *name = PcLayout_Name(kind);

    PcLayout_Compute(kind, swap, w, h, &l);
    /* Inside the window, centred, whole scale when it fits. */
    if (l.topShown)
        CHECK(l.top.x >= 0 && l.top.y >= 0 && l.top.x + l.top.w <= w + 0.01f && l.top.y + l.top.h <= h + 0.01f,
              "%s swap=%d %dx%d: top outside the window", name, swap, w, h);
    if (l.bottomShown)
        CHECK(l.bottom.x >= 0 && l.bottom.y >= 0 && l.bottom.x + l.bottom.w <= w + 0.01f
                  && l.bottom.y + l.bottom.h <= h + 0.01f,
              "%s swap=%d %dx%d: bottom outside the window", name, swap, w, h);
    if (w >= l.logicalW && h >= l.logicalH)
        CHECK(l.scale == floorf(l.scale) && l.scale >= 1.0f, "%s %dx%d: scale %.2f not whole", name, w, h, l.scale);
    /* Aspect kept: 5:3 top, 4:3 bottom. */
    if (l.topShown)
        CHECK(fabsf(l.top.w / l.top.h - 400.0f / 240.0f) < 0.01f, "%s: top aspect", name);
    if (l.bottomShown)
        CHECK(fabsf(l.bottom.w / l.bottom.h - 320.0f / 240.0f) < 0.01f, "%s: bottom aspect", name);

    if (l.stripShown && !l.bottomShown)
    {
        /* FUSION closed: the dock is the bottom screen's band of rows. */
        CHECK(PcLayout_ToBottom(&l, l.strip.x + 0.1f, l.strip.y + 0.1f, false, &tx, &ty) && tx == 0 && ty == PC_DOCK_Y,
              "%s %dx%d: dock top-left -> %d,%d", name, w, h, tx, ty);
        CHECK(PcLayout_ToBottom(&l, l.strip.x + l.strip.w - 0.1f, l.strip.y + l.strip.h - 0.1f, false, &tx, &ty)
                  && tx == 319 && ty == PC_DOCK_Y + PC_DOCK_H - 1,
              "%s %dx%d: dock bottom-right -> %d,%d", name, w, h, tx, ty);
        CHECK(!PcLayout_ToBottom(&l, l.top.x + 1, l.top.y + 1, false, &tx, &ty), "%s: world press became a touch",
              name);
        CHECK(l.strip.x >= l.top.x && l.strip.x + l.strip.w <= l.top.x + l.top.w + 0.01f, "%s: dock outside", name);
        return;
    }
    if (!l.bottomShown)
    {
        CHECK(!PcLayout_ToBottom(&l, w / 2.0f, h / 2.0f, false, &tx, &ty), "%s: touch with no bottom screen", name);
        CHECK(!PcLayout_ToBottom(&l, w / 2.0f, h / 2.0f, true, &tx, &ty), "%s: drag with no bottom screen", name);
        return;
    }
    /* The bottom screen's corners and middle. */
    CHECK(PcLayout_ToBottom(&l, l.bottom.x + 0.1f, l.bottom.y + 0.1f, false, &tx, &ty) && tx == 0 && ty == 0,
          "%s swap=%d %dx%d: top-left -> %d,%d", name, swap, w, h, tx, ty);
    CHECK(PcLayout_ToBottom(&l, l.bottom.x + l.bottom.w - 0.1f, l.bottom.y + l.bottom.h - 0.1f, false, &tx, &ty)
              && tx == 319 && ty == 239,
          "%s swap=%d %dx%d: bottom-right -> %d,%d", name, swap, w, h, tx, ty);
    float mx = l.bottom.x + l.bottom.w / 2, my = l.bottom.y + l.bottom.h / 2;
    bool coveredMiddle = l.topShown && l.bottomFirst && Inside(&l.top, mx, my);
    if (!coveredMiddle)
        /* Below 1x a window pixel spans more than one touch unit: +-1. */
        CHECK(PcLayout_ToBottom(&l, mx, my, false, &tx, &ty) && abs(tx - 160) <= (l.scale < 1 ? 1 : 0)
                  && abs(ty - 120) <= (l.scale < 1 ? 1 : 0),
              "%s swap=%d %dx%d: middle -> %d,%d", name, swap, w, h, tx, ty);
    /* Outside the bottom screen: refused for a press, held to the edge for a drag. */
    CHECK(!PcLayout_ToBottom(&l, l.bottom.x - 5, l.bottom.y - 5, false, &tx, &ty) || l.bottom.x < 5,
          "%s: press outside accepted", name);
    CHECK(PcLayout_ToBottom(&l, -1000, -1000, true, &tx, &ty) && tx == 0 && ty == 0, "%s: drag not clamped", name);
    CHECK(PcLayout_ToBottom(&l, 1e6f, 1e6f, true, &tx, &ty) && tx == 319 && ty == 239, "%s: drag not clamped", name);
    /* A press on the top screen is never a touch (unless the bottom is over it). */
    if (l.topShown)
    {
        float cx = l.top.x + l.top.w / 2, cy = l.top.y + l.top.h / 2;
        bool bottomOver = !l.bottomFirst && Inside(&l.bottom, cx, cy);
        if (!bottomOver)
            CHECK(!PcLayout_ToBottom(&l, cx, cy, false, &tx, &ty), "%s swap=%d %dx%d: top press became a touch", name,
                  swap, w, h);
    }
}

int main(void)
{
    static const int sizes[][2] = { { 800, 960 }, { 1920, 1080 }, { 2560, 1440 }, { 1440, 480 },
                                    { 333, 333 }, { 400, 480 },  { 3840, 2160 } };

    for (int k = 0; k < PC_LAYOUT_COUNT; ++k)
        for (int s = 0; s < 2; ++s)
            for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
                CheckLayout((PcLayoutKind)k, s != 0, sizes[i][0], sizes[i][1]);
    /* FUSION open: the whole bottom screen over the world (swap stands for open here). */
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        PcLayout o;
        int tx, ty;
        PcLayout_ComputeFull(PC_LAYOUT_FUSION, false, true, true, sizes[i][0], sizes[i][1], &o);
        CHECK(o.bottomShown && o.topShown && o.topDimmed && !o.bottomFirst, "fusion open %d: shown", i);
        CHECK(o.bottom.x >= o.top.x && o.bottom.y >= o.top.y && o.bottom.x + o.bottom.w <= o.top.x + o.top.w + 0.01f
                  && o.bottom.y + o.bottom.h <= o.top.y + o.top.h + 0.01f, "fusion open %d: outside the world", i);
        CHECK(PcLayout_ToBottom(&o, o.bottom.x + 0.1f, o.bottom.y + 0.1f, false, &tx, &ty) && tx == 0 && ty == 0,
              "fusion open %d: corner -> %d,%d", i, tx, ty);
        CHECK(!PcLayout_ToBottom(&o, o.top.x + 1, o.top.y + 1, false, &tx, &ty), "fusion open %d: world press", i);
    }

    /* Known values: the 3DS layout at 2x in an 800x960 window. */
    PcLayout l;
    PcLayout_Compute(PC_LAYOUT_VERTICAL, false, 800, 960, &l);
    CHECK(l.scale == 2 && l.top.x == 0 && l.top.y == 0 && l.bottom.x == 80 && l.bottom.y == 480,
          "vertical 2x: scale %.1f top %.0f,%.0f bottom %.0f,%.0f", l.scale, l.top.x, l.top.y, l.bottom.x,
          l.bottom.y);
    /* 1920x1080: side by side at 2x (1440x480), centred. */
    PcLayout_Compute(PC_LAYOUT_SIDE, false, 1920, 1080, &l);
    CHECK(l.scale == 2 && l.top.x == 240 && l.bottom.x == 1040 && l.top.y == 300, "side 1080p: scale %.1f x %.0f/%.0f",
          l.scale, l.top.x, l.bottom.x);
    /* Names. */
    CHECK(PcLayout_Parse("inset") == PC_LAYOUT_INSET && PcLayout_Parse("3") == PC_LAYOUT_SINGLE
              && PcLayout_Parse("x") == -1 && PcLayout_Parse("5") == -1 && PcLayout_Parse("fusion") == PC_LAYOUT_FUSION,
          "parse");

    printf("%d/%d checks passed\n", sChecks - sFailed, sChecks);
    return sFailed != 0;
}
