/*
 * FUSION's single-screen interface: the world fills the window and the
 * bottom screen's pieces that matter while walking sit in its corners, as a
 * JRPG's HUD would have them:
 *   top left      the place and the clock, RUN beside them (a status toggle)
 *   bottom left   the minimap: the bottom screen's map around the player's
 *                 mark; a click (or M) opens the whole bottom screen
 *   bottom right  PARTY and BAG, then SAVE and OPTIONS, a little smaller
 * The plates are the bottom UI's own, drawn again on their own
 * (CtrBottom_PcHudCanvas: on black and on white, their coverage told from
 * the two), the minimap the bottom screen's map; all drawn sharp. A click
 * on one is a tap on the bottom screen there, so the bottom UI does what it
 * always does (and opens over the world).
 * Up only while the player is free to walk (not in a message, a battle, a
 * menu), a few frames after, so it does not flicker between two messages.
 * The letters are a 5x7 pixel font, the key badges shown until a gamepad is
 * used.
 */
#include "pc_hud_gl.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#define HUD_RECTS 4096
#define HUD_SPRITES 12
#define HUD_HITS 12
#define SHOW_DELAY 12 /* frames free to walk before it shows */

static PcHudInfo sInfo;
static int sFreeFrames;
static bool sVisible, sPad;

static float sShapes[2][HUD_RECTS * 8];
static int sShapeCount[2];
static PcHudSprite sSprites[HUD_SPRITES];
static int sSpriteCount;
static struct
{
    PcRect r;
    int kind, tapX, tapY;
} sHits[HUD_HITS];
static int sHitCount;

static const float sInk[4] = { 0.08f, 0.235f, 0.176f, 1.0f };
static const float sInkSoft[4] = { 0.08f, 0.235f, 0.176f, 0.92f };
static const float sMint[4] = { 0.47f, 0.86f, 0.67f, 1.0f };
static const float sWhite[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float sShadow[4] = { 0.0f, 0.0f, 0.0f, 0.30f };

/* ── Shapes ─────────────────────────────────────────────────────────────── */

static void Rect(int layer, float x, float y, float w, float h, const float c[4])
{
    if (sShapeCount[layer] >= HUD_RECTS || w <= 0.0f || h <= 0.0f)
        return;
    float *o = sShapes[layer] + sShapeCount[layer]++ * 8;
    o[0] = x;
    o[1] = y;
    o[2] = w;
    o[3] = h;
    memcpy(o + 4, c, 4 * sizeof(float));
}

/* A soft drop shadow: two offset layers. */
static void Shadow(float x, float y, float w, float h, float u)
{
    Rect(0, x + 3.0f * u, y + 4.0f * u, w, h, sShadow);
    Rect(0, x + 1.0f * u, y + 2.0f * u, w, h, sShadow);
}

/* ── Letters ────────────────────────────────────────────────────────────── */

/* A-Z then 0-9, rows top down, bit 4 the left column. */
static const unsigned char sGlyphs[36][7] = {
    { 14, 17, 17, 31, 17, 17, 17 }, { 30, 17, 17, 30, 17, 17, 30 }, { 14, 17, 16, 16, 16, 17, 14 },
    { 30, 17, 17, 17, 17, 17, 30 }, { 31, 16, 16, 30, 16, 16, 31 }, { 31, 16, 16, 30, 16, 16, 16 },
    { 14, 17, 16, 23, 17, 17, 15 }, { 17, 17, 17, 31, 17, 17, 17 }, { 14, 4, 4, 4, 4, 4, 14 },
    { 7, 2, 2, 2, 2, 18, 12 },      { 17, 18, 20, 24, 20, 18, 17 }, { 16, 16, 16, 16, 16, 16, 31 },
    { 17, 27, 21, 21, 17, 17, 17 }, { 17, 17, 25, 21, 19, 17, 17 }, { 14, 17, 17, 17, 17, 17, 14 },
    { 30, 17, 17, 30, 16, 16, 16 }, { 14, 17, 17, 17, 21, 18, 13 }, { 30, 17, 17, 30, 20, 18, 17 },
    { 15, 16, 16, 14, 1, 1, 30 },   { 31, 4, 4, 4, 4, 4, 4 },       { 17, 17, 17, 17, 17, 17, 14 },
    { 17, 17, 17, 17, 17, 10, 4 },  { 17, 17, 17, 21, 21, 21, 10 }, { 17, 17, 10, 4, 10, 17, 17 },
    { 17, 17, 10, 4, 4, 4, 4 },     { 31, 1, 2, 4, 8, 16, 31 },     { 14, 17, 19, 21, 25, 17, 14 },
    { 4, 12, 4, 4, 4, 4, 14 },      { 14, 17, 1, 2, 4, 8, 31 },     { 31, 2, 4, 2, 1, 17, 14 },
    { 2, 6, 10, 18, 31, 2, 2 },     { 31, 16, 30, 1, 1, 17, 14 },   { 6, 8, 16, 30, 17, 17, 14 },
    { 31, 1, 2, 4, 8, 8, 8 },       { 14, 17, 17, 14, 17, 17, 14 }, { 14, 17, 17, 15, 1, 2, 12 },
};

enum { MARK_NONE, MARK_ACUTE, MARK_GRAVE, MARK_CIRC, MARK_TILDE, MARK_UMLAUT, MARK_CEDILLA };

/* The next character of a UTF-8 string: its glyph (-1 a space, -2 none) and
 * its mark. */
static int NextGlyph(const char **text, int *mark)
{
    const unsigned char *p = (const unsigned char *)*text;
    unsigned cp = *p++;

    if (cp >= 0xC0 && (*p & 0xC0) == 0x80)
        cp = ((cp & 0x1F) << 6) | (*p++ & 0x3F);
    *text = (const char *)p;
    *mark = MARK_NONE;
    if (cp >= 'a' && cp <= 'z')
        cp -= 32;
    if (cp >= 0xE0 && cp <= 0xFE)
        cp -= 32; /* à..þ -> À..Þ */
    switch (cp)
    {
    case 0xC0: *mark = MARK_GRAVE; return 'A' - 'A';
    case 0xC1: *mark = MARK_ACUTE; return 'A' - 'A';
    case 0xC2: *mark = MARK_CIRC; return 'A' - 'A';
    case 0xC3: *mark = MARK_TILDE; return 'A' - 'A';
    case 0xC4: *mark = MARK_UMLAUT; return 'A' - 'A';
    case 0xC7: *mark = MARK_CEDILLA; return 'C' - 'A';
    case 0xC8: *mark = MARK_GRAVE; return 'E' - 'A';
    case 0xC9: *mark = MARK_ACUTE; return 'E' - 'A';
    case 0xCA: *mark = MARK_CIRC; return 'E' - 'A';
    case 0xCD: *mark = MARK_ACUTE; return 'I' - 'A';
    case 0xD3: *mark = MARK_ACUTE; return 'O' - 'A';
    case 0xD4: *mark = MARK_CIRC; return 'O' - 'A';
    case 0xD5: *mark = MARK_TILDE; return 'O' - 'A';
    case 0xD6: *mark = MARK_UMLAUT; return 'O' - 'A';
    case 0xDA: *mark = MARK_ACUTE; return 'U' - 'A';
    case 0xDC: *mark = MARK_UMLAUT; return 'U' - 'A';
    }
    if (cp >= 'A' && cp <= 'Z')
        return (int)(cp - 'A');
    if (cp >= '0' && cp <= '9')
        return 26 + (int)(cp - '0');
    return cp == ' ' ? -1 : -2;
}

static float TextWidth(const char *text, float px)
{
    int n = 0, mark;
    while (*text)
        if (NextGlyph(&text, &mark) != -2)
            ++n;
    return n > 0 ? n * 6.0f * px - px : 0.0f;
}

/* A mark's two rows over the letter (or under it, the cedilla). */
static const signed char sMarks[7][2][5] = {
    { { 0 } },
    { { 0, 0, 0, 1, 0 }, { 0, 0, 1, 0, 0 } },
    { { 0, 1, 0, 0, 0 }, { 0, 0, 1, 0, 0 } },
    { { 0, 0, 1, 0, 0 }, { 0, 1, 0, 1, 0 } },
    { { 0, 1, 1, 0, 1 }, { 1, 0, 1, 1, 0 } },
    { { 0, 0, 0, 0, 0 }, { 0, 1, 0, 1, 0 } },
    { { 0, 0, 1, 0, 0 }, { 0, 1, 0, 0, 0 } },
};

static void Glyph(int layer, float x, float y, float px, int g, int mark, const float c[4])
{
    for (int row = 0; row < 7; ++row)
    {
        unsigned bits = sGlyphs[g][row];
        for (int col = 0; col < 5;)
        {
            if (!(bits & (16u >> col)))
            {
                ++col;
                continue;
            }
            int run = col;
            while (run < 5 && (bits & (16u >> run)))
                ++run;
            Rect(layer, x + col * px, y + row * px, (run - col) * px, px, c);
            col = run;
        }
    }
    if (mark != MARK_NONE)
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 5; ++col)
                if (sMarks[mark][row][col])
                {
                    float my = mark == MARK_CEDILLA ? y + (7 + row) * px : y - (2 - row) * px - px * 0.5f;
                    Rect(layer, x + col * px, my, px, px, c);
                }
}

/* White letters with an ink outline, `px` window pixels a font pixel. */
static void Text(float x, float y, float px, const char *text, bool outline)
{
    static const float offsets[5][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { 0, 0 } };
    for (int pass = outline ? 0 : 4; pass < 5; ++pass)
    {
        const char *t = text;
        float cx = x + offsets[pass][0] * px, cy = y + offsets[pass][1] * px;
        int mark;
        while (*t)
        {
            int g = NextGlyph(&t, &mark);
            if (g == -2)
                continue;
            if (g >= 0)
                Glyph(1, cx, cy, px, g, mark, pass == 4 ? sWhite : sInk);
            cx += 6.0f * px;
        }
    }
}

/* A key's name on a small ink plate with a mint edge. */
static void Badge(float x, float y, float px, const char *key)
{
    float w = TextWidth(key, px) + 6.0f * px, h = 11.0f * px;
    Rect(1, x, y, w, h, sMint);
    Rect(1, x + px * 0.75f, y + px * 0.75f, w - px * 1.5f, h - px * 1.5f, sInkSoft);
    Text(x + 3.0f * px, y + 2.0f * px, px, key, false);
}

/* ── Layout ─────────────────────────────────────────────────────────────── */

static void Sprite(float x, float y, float scale, PcHudBox src, int from)
{
    if (sSpriteCount >= HUD_SPRITES)
        return;
    sSprites[sSpriteCount++] = (PcHudSprite){ { x, y, src.w * scale, src.h * scale }, src, from, 0.0f };
}

static void Hit(float x, float y, float w, float h, int kind, PcHudBox tap)
{
    if (sHitCount >= HUD_HITS)
        return;
    sHits[sHitCount].r = (PcRect){ x, y, w, h };
    sHits[sHitCount].kind = kind;
    sHits[sHitCount].tapX = tap.x + tap.w / 2;
    sHits[sHitCount].tapY = tap.y + tap.h / 2;
    ++sHitCount;
}

void PcHud_Update(bool active, bool pad)
{
    static bool sWasOpen;

    sPad = pad;
    if (!active)
    {
        /* The bottom screen over the world (a menu): back from it, at once. */
        sWasOpen = true;
        sFreeFrames = 0;
        sVisible = false;
        sHitCount = 0;
        return;
    }
    CtrBottom_PcHud(&sInfo);
    if (sInfo.shown && sWasOpen)
    {
        sFreeFrames = SHOW_DELAY;
        sWasOpen = false;
    }
    sFreeFrames = sInfo.shown ? sFreeFrames + 1 : 0;
    sVisible = sFreeFrames > SHOW_DELAY;
    if (!sVisible)
        sHitCount = 0;
}

bool PcHud_Visible(void)
{
    return sVisible;
}

unsigned PcHud_CanvasVersion(void)
{
    return sInfo.version;
}

/* A rectangle with its corners rounded `r`, a window pixel's row a strip. */
static void RoundRect(int layer, float x, float y, float w, float h, float r, const float c[4])
{
    int rows = (int)ceilf(r);

    for (int i = 0; i < rows; ++i)
    {
        float d = r - (i + 0.5f), inset = r - sqrtf(fmaxf(0.0f, r * r - d * d));
        Rect(layer, x + inset, y + i, w - 2.0f * inset, 1.0f, c);
        Rect(layer, x + inset, y + h - i - 1.0f, w - 2.0f * inset, 1.0f, c);
    }
    Rect(layer, x, y + rows, w, h - 2.0f * rows, c);
}

void PcHud_Build(const PcRect *top)
{
    static const char *const sKeys[PC_HUD_BUTTONS] = { "P", "I", "F5", "O" };
    static const float sScales[PC_HUD_BUTTONS] = { 1.86f, 1.86f, 1.55f, 1.55f };
    float u = top->h / 1080.0f, m = 22.0f * u;
    float px = fmaxf(1.0f, roundf(2.0f * u)); /* the font's pixel: whole window pixels */
    float x, y, k;

    sShapeCount[0] = sShapeCount[1] = 0;
    sSpriteCount = 0;
    sHitCount = 0;
    if (!sVisible)
        return;

    /* Top left: the place and the clock, RUN beside them. The plates' rings
     * and shadows are in their pieces (PLATE_M* pixels round each). */
    k = 1.86f * u;
    x = top->x + m - 6.0f * k;
    y = top->y + m - 6.0f * k;
    Sprite(x, y, k, sInfo.header, PC_HUD_FROM_CANVAS);
    x += sInfo.header.w * k - 4.0f * k;
    Sprite(x, y + (sInfo.header.h - sInfo.run.h) * k / 2.0f, k, sInfo.run, PC_HUD_FROM_CANVAS);
    Hit(x + 6.0f * k, y + 6.0f * k, (sInfo.run.w - 12) * k, (sInfo.run.h - 14) * k, PC_HUD_HIT_TAP, sInfo.runTap);
    if (!sPad && sInfo.runKey)
        Badge(x + (sInfo.run.w - 3) * k, y + (sInfo.header.h * k - 11.0f * px) / 2.0f - 1.0f * k, px, sInfo.runKey);

    /* Bottom left: the minimap, the map around the player's mark. */
    if (sInfo.map)
    {
        float mw = roundf(250.0f * u), mh = roundf(170.0f * u), mx = top->x + m, my = top->y + top->h - m - mh;
        PcHudBox a = sInfo.mapArea, src = { 0, 0, 104, 71 };
        src.x = (short)(sInfo.playerX - src.w / 2);
        src.y = (short)(sInfo.playerY - src.h / 2);
        if (src.x < a.x) src.x = a.x;
        if (src.y < a.y) src.y = a.y;
        if (src.x + src.w > a.x + a.w) src.x = (short)(a.x + a.w - src.w);
        if (src.y + src.h > a.y + a.h) src.y = (short)(a.y + a.h - src.h);
        RoundRect(0, mx - 2.0f * u, my + 1.0f * u, mw + 10.0f * u, mh + 10.0f * u, 16.0f * u, sShadow);
        RoundRect(0, mx - 5.0f * u, my - 5.0f * u, mw + 10.0f * u, mh + 10.0f * u, 16.0f * u, sMint);
        RoundRect(0, mx - 2.0f * u, my - 2.0f * u, mw + 4.0f * u, mh + 4.0f * u, 13.0f * u, sInk);
        Sprite(mx, my, mw / src.w, src, PC_HUD_FROM_BOTTOM);
        sSprites[sSpriteCount - 1].dst.h = mh;
        sSprites[sSpriteCount - 1].round = 11.0f * u;
        Hit(mx - 5.0f * u, my - 5.0f * u, mw + 10.0f * u, mh + 10.0f * u, PC_HUD_HIT_MINIMAP, src);
        if (!sPad)
            Badge(mx + mw - TextWidth("M", px) - 10.0f * px, my + mh - 13.0f * px, px, "M");
    }

    /* Bottom right: OPTIONS, SAVE, then BAG, PARTY, right to left. A
     * button's piece has its plate's margins: 6 pixels left, right and
     * above, 8 below (its shadow). */
    x = top->x + top->w - m - 8.0f * u;
    float base = top->y + top->h - m - 10.0f * px;
    for (int i = PC_HUD_BUTTONS - 1; i >= 0; --i)
    {
        if (!sInfo.buttonOn[i])
            continue;
        PcHudBox b = sInfo.button[i];
        float s = sScales[i] * u, w = (b.w - 12) * s, h = (b.h - 14) * s;
        /* A label wider than its button takes its own room. */
        float room = sInfo.label[i] ? fmaxf(0.0f, (TextWidth(sInfo.label[i], px) - w) / 2.0f) : 0.0f;
        x -= w + room;
        y = base - h - 4.0f * px;
        Sprite(x - 6.0f * s, y - 6.0f * s, s, b, PC_HUD_FROM_CANVAS);
        Hit(x, y, w, h + 14.0f * px, PC_HUD_HIT_TAP, sInfo.buttonTap[i]);
        if (sInfo.label[i])
            Text(x + (w - TextWidth(sInfo.label[i], px)) / 2.0f, base - 1.0f * px, px, sInfo.label[i], true);
        if (!sPad)
            Badge(x - 4.0f * u, y - 6.0f * u, px, sKeys[i]);
        x -= room + 16.0f * u + (i == PC_HUD_SAVE ? 20.0f * u : 0.0f);
    }
}

int PcHud_SpriteCount(void)
{
    return sSpriteCount;
}

const PcHudSprite *PcHud_Sprite(int i)
{
    return &sSprites[i];
}

void PcHud_DrawShapes(int layer, int fbW, int fbH)
{
    PcOverlay_Draw(sShapes[layer], sShapeCount[layer], fbW, fbH);
}

int PcHud_Hit(float x, float y, int *tapX, int *tapY)
{
    for (int i = 0; i < sHitCount; ++i)
    {
        const PcRect *r = &sHits[i].r;
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h)
        {
            *tapX = sHits[i].tapX;
            *tapY = sHits[i].tapY;
            return sHits[i].kind;
        }
    }
    return PC_HUD_HIT_NONE;
}

bool PcHud_Key(int scancode, int *tapX, int *tapY)
{
    int button = scancode == SDL_SCANCODE_P ? PC_HUD_PARTY : scancode == SDL_SCANCODE_I ? PC_HUD_BAG
               : scancode == SDL_SCANCODE_F5 ? PC_HUD_SAVE : scancode == SDL_SCANCODE_O ? PC_HUD_OPTIONS : -1;
    PcHudBox b;

    if (!sVisible)
        return false;
    if (scancode == SDL_SCANCODE_LSHIFT || scancode == SDL_SCANCODE_RSHIFT)
        b = sInfo.runTap;
    else if (button >= 0 && sInfo.buttonOn[button])
        b = sInfo.buttonTap[button];
    else
        return false;
    *tapX = b.x + b.w / 2;
    *tapY = b.y + b.h / 2;
    return true;
}
