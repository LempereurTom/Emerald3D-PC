/*
 * The window's menu bar (Windows): the game's OPTIONS, every row, under the
 * title bar.
 *
 * Built from the bottom screen's own table (pc_menu.h: the categories of the
 * OPTIONS screen, their groups, their rows), so it always shows what the
 * screen shows: a category per menu, a group per submenu, then each row -
 *   a toggle         a checked item,
 *   a choice         a submenu of its values (radio), its value at the right,
 *   a stepped value  a submenu with ◀ and ▶, its value at the right,
 *   an action        an item,
 *   a note           a greyed line (ABOUT's credits).
 * Rows the game greys (the 3D world's, without it) are greyed. States and
 * values are read again each time a menu opens; a choice goes through the
 * same steps a tap on the screen takes, so the game applies it as always.
 *
 * Shown in a window, hidden in fullscreen. Linux: no menu bar (the screen's
 * OPTIONS are the same).
 */
#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pc_menu.h"

void PcMenu_Init(SDL_Window *window);
void PcMenu_Update(bool fullscreen);

#ifdef _WIN32
#include <windows.h>

#define CMD_BASE 40000
#define MAX_CMDS 2048
#define MAX_POPUPS 512
#define MAX_ROWS 512

enum { CMD_TOGGLE, CMD_SET, CMD_NEXT, CMD_BACK, CMD_ACTION };
enum { POP_GROUP, POP_CHOICE, POP_STEP };

typedef struct
{
    int type, cat, group, row, value;
} Cmd;

typedef struct
{
    HMENU menu;
    int type, cat, group, row; /* POP_GROUP: cat, group; the others: a row */
} Popup;

/* A row's place in its group's menu, to update its text and state. */
typedef struct
{
    HMENU parent;
    int position, cat, group, row, kind, id; /* id: its command, for a toggle or an action */
    HMENU sub;
} RowItem;

static HWND sHwnd;
static SDL_Window *sWindow;
static WNDPROC sOldProc;
static HMENU sBar;
static bool sAttached, sBuilt;
static Cmd sCmds[MAX_CMDS];
static int sCmdCount;
static Popup sPopups[MAX_POPUPS];
static int sPopupCount;
static RowItem sRows[MAX_ROWS];
static int sRowCount;

/* Words a sentence keeps as they are spelt here: acronyms, names. */
static const char *const sWords[] = {
    "3D", "FPS", "PC", "ZL", "ZR", "EXP", "EV", "EVS", "IV", "IVS", "TM", "TMS", "HM", "HMS", "HP", "PV",
    "CT", "CS", "PP", "V-SYNC", "CC", "BY-NC-SA", "LV.", "NV.", "ROM", "README", "L", "R", "A", "B", "X", "Y",
    "Zallax", "Emerald", "Hoenn", "Nuzlocke", "Pokédex", "Pokémon", "github.com/ZallaxDev", "F9:", "F11:",
    "TAB:", "ESC:", "START:", "STICK:", "LR", "L=A", "ZL:", "ZR:",
};

static bool SameWord(const char *a, size_t len, const char *b)
{
    if (strlen(b) != len)
        return false;
    for (size_t i = 0; i < len; ++i)
    {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y && !(x >= 0x80 && y >= 0x80))
            return false;
    }
    return true;
}

/* The table's labels are the screen's capitals: a menu reads them as a
 * sentence (TEXT SPEED -> Text speed), keeping the words above as they are
 * spelt there and any word with a digit (1X, 1/4096, TYPE1). */
static void Sentence(const char *in, char *out, size_t size)
{
    size_t n = 0;
    bool first = true;

    while (*in && n + 4 < size)
    {
        const char *word = in;
        size_t len = 0;
        bool digit = false;
        const char *as = NULL;
        while (word[len] && word[len] != ' ')
        {
            unsigned char c = (unsigned char)word[len];
            if (c >= '0' && c <= '9')
                digit = true;
            ++len;
        }
        for (size_t k = 0; k < sizeof(sWords) / sizeof(sWords[0]) && !as; ++k)
            if (SameWord(word, len, sWords[k]))
                as = sWords[k];
        if (as)
        {
            for (size_t i = 0; as[i] && n + 4 < size; ++i)
                out[n++] = as[i];
            first = false;
            in += len;
            while (*in == ' ' && n + 1 < size)
                out[n++] = *in++;
            continue;
        }
        bool keep = digit;
        for (size_t i = 0; i < len && n + 4 < size; ++i)
        {
            unsigned char c = (unsigned char)word[i];
            if (keep || (first && i == 0))
                out[n++] = (char)c;
            else if (c >= 'A' && c <= 'Z')
                out[n++] = (char)(c + 32);
            else if (c == 0xC3 && i + 1 < len && (unsigned char)word[i + 1] >= 0x80
                     && (unsigned char)word[i + 1] <= 0x9E && (unsigned char)word[i + 1] != 0x97)
            {
                out[n++] = (char)c; /* À..Þ -> à..þ */
                out[n++] = (char)(word[i + 1] + 0x20);
                ++i;
            }
            else
                out[n++] = (char)c;
        }
        first = false;
        in += len;
        while (*in == ' ' && n + 1 < size)
            out[n++] = *in++;
    }
    out[n] = '\0';
}

static void Wide(const char *utf8, wchar_t *out, int size)
{
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, size) == 0)
        out[0] = 0;
}

static int AddCmd(int type, int cat, int group, int row, int value)
{
    if (sCmdCount >= MAX_CMDS)
        return 0;
    sCmds[sCmdCount] = (Cmd){ type, cat, group, row, value };
    return CMD_BASE + sCmdCount++;
}

static HMENU AddPopup(int type, int cat, int group, int row)
{
    HMENU menu = CreatePopupMenu();
    if (sPopupCount < MAX_POPUPS)
        sPopups[sPopupCount++] = (Popup){ menu, type, cat, group, row };
    return menu;
}

static void ItemText(const PcOptRow *r, int kind, char *out, size_t size)
{
    char label[96];
    Sentence(r->label, label, sizeof(label));
    /* A choice's value is added as its menu opens (RefreshGroup). */
    if (kind == PC_OPT_STEP || kind == PC_OPT_ACTION)
        snprintf(out, size, "%s\t%s", label, r->value);
    else
        snprintf(out, size, "%s", label);
}

static void BuildGroup(HMENU menu, int cat, int group)
{
    int rows = CtrBottom_PcOptRows(cat, group);
    for (int r = 0; r < rows; ++r)
    {
        PcOptRow row;
        char text[160];
        wchar_t wide[160];
        if (!CtrBottom_PcOptRow(cat, group, r, &row))
            continue;
        RowItem item = { menu, GetMenuItemCount(menu), cat, group, r, row.kind, 0, NULL };
        ItemText(&row, row.kind, text, sizeof(text));
        Wide(text, wide, 160);
        switch (row.kind)
        {
        case PC_OPT_INFO:
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, wide);
            break;
        case PC_OPT_TOGGLE:
            item.id = AddCmd(CMD_TOGGLE, cat, group, r, 0);
            AppendMenuW(menu, MF_STRING, item.id, wide);
            break;
        case PC_OPT_ACTION:
            item.id = AddCmd(CMD_ACTION, cat, group, r, 0);
            AppendMenuW(menu, MF_STRING, item.id, wide);
            break;
        case PC_OPT_CHOICE:
        {
            HMENU sub = AddPopup(POP_CHOICE, cat, group, r);
            for (int v = 0; v < row.count; ++v)
            {
                char name[64], shown[64];
                wchar_t w[64];
                CtrBottom_PcOptChoiceName(cat, group, r, v, name, sizeof(name));
                Sentence(name, shown, sizeof(shown));
                Wide(shown, w, 64);
                AppendMenuW(sub, MF_STRING, AddCmd(CMD_SET, cat, group, r, v), w);
            }
            item.sub = sub;
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, wide);
            break;
        }
        case PC_OPT_STEP:
        {
            /* A key of CONTROLS: one click, then the key (the screen's capture). */
            if (strncmp(row.key, "pc_kb_", 6) == 0)
            {
                item.id = AddCmd(CMD_NEXT, cat, group, r, 0);
                AppendMenuW(menu, MF_STRING, item.id, wide);
                break;
            }
            HMENU sub = AddPopup(POP_STEP, cat, group, r);
            AppendMenuW(sub, MF_STRING, AddCmd(CMD_BACK, cat, group, r, 0), L"\x25C0");
            AppendMenuW(sub, MF_STRING, AddCmd(CMD_NEXT, cat, group, r, 0), L"\x25B6");
            item.sub = sub;
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, wide);
            break;
        }
        }
        if (sRowCount < MAX_ROWS)
            sRows[sRowCount++] = item;
    }
}

static void Build(void)
{
    sBar = CreateMenu();
    for (int c = 0; c < CtrBottom_PcOptCats(); ++c)
    {
        const char *name = CtrBottom_PcOptCatName(c);
        char shown[96];
        wchar_t wide[96];
        if (name == NULL)
            continue;
        HMENU cat = CreatePopupMenu();
        for (int g = 0; g < CtrBottom_PcOptGroups(c); ++g)
        {
            const char *gname = CtrBottom_PcOptGroupName(c, g);
            if (gname == NULL)
                continue;
            HMENU group = AddPopup(POP_GROUP, c, g, 0);
            BuildGroup(group, c, g);
            Sentence(gname, shown, sizeof(shown));
            Wide(shown, wide, 96);
            AppendMenuW(cat, MF_POPUP, (UINT_PTR)group, wide);
        }
        Sentence(name, shown, sizeof(shown));
        Wide(shown, wide, 96);
        AppendMenuW(sBar, MF_POPUP, (UINT_PTR)cat, wide);
    }
    sBuilt = true;
}

/* A group's menu as it opens: each row's state, value and greying. */
static void RefreshGroup(HMENU menu)
{
    for (int i = 0; i < sRowCount; ++i)
    {
        RowItem *it = &sRows[i];
        PcOptRow row;
        char label[96], text[192];
        wchar_t wide[192];
        if (it->parent != menu || !CtrBottom_PcOptRow(it->cat, it->group, it->row, &row))
            continue;
        Sentence(row.label, label, sizeof(label));
        if (row.kind == PC_OPT_CHOICE)
        {
            char value[64], shown[64];
            CtrBottom_PcOptChoiceName(it->cat, it->group, it->row, row.current, value, sizeof(value));
            Sentence(value, shown, sizeof(shown));
            snprintf(text, sizeof(text), "%s\t%s", label, shown);
        }
        else if (row.kind == PC_OPT_STEP || row.kind == PC_OPT_ACTION)
        {
            char value[64];
            Sentence(row.value, value, sizeof(value));
            snprintf(text, sizeof(text), "%s\t%s", label, value);
        }
        else
            snprintf(text, sizeof(text), "%s", label);
        Wide(text, wide, 192);
        UINT state = row.kind == PC_OPT_INFO || !row.live ? MF_GRAYED : MF_ENABLED;
        if (it->sub)
            ModifyMenuW(menu, it->position, MF_BYPOSITION | MF_POPUP | state, (UINT_PTR)it->sub, wide);
        else
            ModifyMenuW(menu, it->position, MF_BYPOSITION | MF_STRING | state
                        | (row.kind == PC_OPT_TOGGLE && row.current ? MF_CHECKED : MF_UNCHECKED),
                        it->id, wide);
    }
}

static void RefreshChoice(const Popup *p)
{
    PcOptRow row;
    if (!CtrBottom_PcOptRow(p->cat, p->group, p->row, &row) || row.count <= 0)
        return;
    CheckMenuRadioItem(p->menu, 0, row.count - 1, row.current, MF_BYPOSITION);
}

static void Run(int id)
{
    const Cmd *c;
    if (id < CMD_BASE || id >= CMD_BASE + sCmdCount)
        return;
    c = &sCmds[id - CMD_BASE];
    switch (c->type)
    {
    case CMD_TOGGLE:
    {
        PcOptRow row;
        if (CtrBottom_PcOptRow(c->cat, c->group, c->row, &row))
            CtrBottom_PcOptSet(c->cat, c->group, c->row, !row.current);
        break;
    }
    case CMD_SET: CtrBottom_PcOptSet(c->cat, c->group, c->row, c->value); break;
    case CMD_NEXT:
    case CMD_ACTION: CtrBottom_PcOptStep(c->cat, c->group, c->row, 1); break;
    case CMD_BACK: CtrBottom_PcOptStep(c->cat, c->group, c->row, -1); break;
    }
}

static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_INITMENUPOPUP)
    {
        HMENU menu = (HMENU)wParam;
        for (int i = 0; i < sPopupCount; ++i)
            if (sPopups[i].menu == menu)
            {
                if (sPopups[i].type == POP_GROUP)
                    RefreshGroup(menu);
                else if (sPopups[i].type == POP_CHOICE)
                    RefreshChoice(&sPopups[i]);
                break;
            }
    }
    else if (msg == WM_COMMAND && HIWORD(wParam) == 0 && LOWORD(wParam) >= CMD_BASE)
    {
        Run(LOWORD(wParam));
        return 0;
    }
    return CallWindowProcW(sOldProc, hwnd, msg, wParam, lParam);
}

/* EMERALD_PC_MENU_DUMP=file (checks): the menu as it would open, in text. */
static void DumpMenu(FILE *f, HMENU menu, int depth)
{
    for (int i = 0; i < GetMenuItemCount(menu); ++i)
    {
        wchar_t wide[192];
        char text[400];
        MENUITEMINFOW info = { sizeof(info), MIIM_STATE | MIIM_SUBMENU | MIIM_STRING };
        info.dwTypeData = wide;
        info.cch = 192;
        if (!GetMenuItemInfoW(menu, i, TRUE, &info))
            continue;
        if (info.hSubMenu)
        {
            for (int p = 0; p < sPopupCount; ++p)
                if (sPopups[p].menu == info.hSubMenu)
                {
                    if (sPopups[p].type == POP_GROUP)
                        RefreshGroup(info.hSubMenu);
                    else if (sPopups[p].type == POP_CHOICE)
                        RefreshChoice(&sPopups[p]);
                }
        }
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, sizeof(text), NULL, NULL);
        for (char *t = strchr(text, '\t'); t; t = strchr(t, '\t'))
            *t = '|';
        fprintf(f, "%*s%s%s%s\n", depth * 2, "", (info.fState & MFS_CHECKED) ? "[x] " : "",
                (info.fState & MFS_GRAYED) ? "(grey) " : "", text);
        if (info.hSubMenu)
            DumpMenu(f, info.hSubMenu, depth + 1);
    }
}

void PcMenu_Init(SDL_Window *window)
{
    sWindow = window;
    sHwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (sHwnd == NULL)
        return;
    sOldProc = (WNDPROC)SetWindowLongPtrW(sHwnd, GWLP_WNDPROC, (LONG_PTR)Proc);
}

void PcMenu_Update(bool fullscreen)
{
    bool want = !fullscreen && sHwnd != NULL;
    const char *off = SDL_getenv("EMERALD_PC_NO_MENU");
    if (off && *off == '1')
        want = false;
    if (want == sAttached)
        return;
    if (want && !sBuilt)
    {
        Build();
        const char *dump = SDL_getenv("EMERALD_PC_MENU_DUMP");
        FILE *f = dump && *dump ? fopen(dump, "w") : NULL;
        if (f)
        {
            DumpMenu(f, sBar, 0);
            fclose(f);
        }
    }
    /* The client area keeps its size: the bar goes above it. */
    int w = 0, h = 0;
    SDL_GetWindowSize(sWindow, &w, &h);
    SetMenu(sHwnd, want ? sBar : NULL);
    sAttached = want;
    if (!fullscreen && !(SDL_GetWindowFlags(sWindow) & SDL_WINDOW_MAXIMIZED))
    {
        RECT rect = { 0, 0, w, h };
        DWORD style = (DWORD)GetWindowLongPtrW(sHwnd, GWL_STYLE), ex = (DWORD)GetWindowLongPtrW(sHwnd, GWL_EXSTYLE);
        AdjustWindowRectEx(&rect, style, want, ex);
        SetWindowPos(sHwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

#else

void PcMenu_Init(SDL_Window *window)
{
    (void)window;
}

void PcMenu_Update(bool fullscreen)
{
    (void)fullscreen;
}

#endif
