/*
 * The PC port's options (J8): the values and texts its lines in gCtrExtras
 * use (pc_extras.inc). Included by 3ds_port/src/3ds_extras.c ahead of the
 * table, under PORT_PC; the hooks are pc_port's (pc_options.h).
 */
#include "pc_options.h"

static const char *const sPcWindowModes[] = {
    CTR_TEXT("WINDOW", "VENTANA", "FENÊTRE", "FINESTRA", "JANELA", "FENSTER", "ウインドウ"),
    CTR_TEXT("FULLSCREEN", "P. COMPLETA", "PLEIN ÉCRAN", "SCHERMO INT.", "TELA CHEIA", "VOLLBILD", "ぜんがめん"),
    CTR_TEXT("EXCLUSIVE", "EXCLUSIVO", "EXCLUSIF", "ESCLUSIVO", "EXCLUSIVO", "EXKLUSIV", "せんよう"),
};
static const char *const sPcScales[] = {"1X", "2X", "3X", "4X", "5X", "6X"};
static const char *const sPcStretch[] = {
    CTR_TEXT("WHOLE", "ENTERO", "ENTIÈRE", "INTERA", "INTEIRA", "GANZZAHLIG", "せいすう"),
    CTR_TEXT("STRETCH", "ESTIRAR", "ÉTIRÉE", "ADATTA", "ESTICAR", "STRECKEN", "ひきのばし"),
};
static const char *const sPcFilters[] = {
    CTR_TEXT("SHARP", "NÍTIDO", "NET", "NITIDO", "NÍTIDO", "SCHARF", "くっきり"),
    CTR_TEXT("SMOOTH", "SUAVE", "LISSÉ", "MORBIDO", "SUAVE", "WEICH", "なめらか"),
};
static const char *const sPcLayouts[] = {
    CTR_TEXT("VERTICAL", "VERTICAL", "VERTICALE", "VERTICALE", "VERTICAL", "VERTIKAL", "たて"),
    CTR_TEXT("SIDE BY SIDE", "LADO A LADO", "CÔTE À CÔTE", "AFFIANCATI", "LADO A LADO", "NEBENEIN.", "よこ"),
    CTR_TEXT("INSET", "INSERTADA", "INCRUSTÉE", "RIQUADRO", "EMBUTIDA", "EINGEBETTET", "こがめん"),
    CTR_TEXT("ONE SCREEN", "UNA PANT.", "UN ÉCRAN", "UNO SCHERMO", "UMA TELA", "EIN BILD", "ひとつ"),
    "FUSION",
};
static const char *const sPcColours[] = {
    CTR_TEXT("ORIGINAL", "ORIGINAL", "D'ORIGINE", "ORIGINALE", "ORIGINAL", "ORIGINAL", "そのまま"),
    CTR_TEXT("VIVID", "VIVO", "VIVES", "VIVACI", "VIVAS", "LEBHAFT", "あざやか"),
    CTR_TEXT("VERY VIVID", "MUY VIVO", "TRÈS VIVES", "MOLTO VIVACI", "MUITO VIVAS", "SEHR LEBHAFT", "とても あざやか"),
};
static const char *const sPcPadLayouts[] = {"XBOX", "NINTENDO"};
/* GRAPHICS (pc_display.c's PcOptions_Apply): a preset sets the 3D resolution
 * and the anti-aliasing; changing either makes it CUSTOM. */
static const char *const sPcPresets[] = {
    CTR_TEXT("LOW", "BAJO", "BAS", "BASSO", "BAIXO", "NIEDRIG", "ひくい"),
    CTR_TEXT("MEDIUM", "MEDIO", "MOYEN", "MEDIO", "MÉDIO", "MITTEL", "ふつう"),
    CTR_TEXT("HIGH", "ALTO", "ÉLEVÉ", "ALTO", "ALTO", "HOCH", "たかい"),
    "ULTRA",
    CTR_TEXT("CUSTOM", "PERSONAL.", "PERSO.", "PERSONAL.", "PERSONAL.", "EIGENE", "カスタム"),
};
static const char *const sPcRenderScales[] = {"1X", "2X", "3X", "4X"};
static const char *const sPcMsaa[] = {
    CTR_TEXT("OFF", "NO", "NON", "NO", "NÃO", "AUS", "オフ"),
    "2X", "4X", "8X",
};
static const char *const sPcVolumes[] = {"0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"};
static const char *const sPcReset[] = {
    CTR_TEXT("RESET", "RESTABLECER", "RÉTABLIR", "RIPRISTINA", "RESTAURAR", "ZURÜCKSETZEN", "もどす"),
};

/* ASCII from the PC side in the game's characters: letters, digits and a few
 * signs; anything else is a space. */
static const u8 *PcGameText(const char *ascii, u8 *out, size_t size)
{
    size_t n = 0;

    for (; ascii != NULL && *ascii && n + 1 < size; ++ascii, ++n)
    {
        char c = *ascii;
        out[n] = c >= 'A' && c <= 'Z' ? CHAR_A + (c - 'A')
               : c >= 'a' && c <= 'z' ? CHAR_a + (c - 'a')
               : c >= '0' && c <= '9' ? CHAR_0 + (c - '0')
               : c == '.' ? CHAR_PERIOD : c == '-' ? CHAR_HYPHEN : c == '/' ? CHAR_SLASH
               : CHAR_SPACE;
    }
    out[n] = EOS;
    return out;
}

static void PcApply(void) { PcOptions_Apply(); }
static void PcApplyAudio(void) { PcOptions_ApplyAudio(); }

static void PcResolutionStep(int direction) { PcOptions_StepResolution(direction); }
static const u8 *PcResolutionText(void)
{
    static u8 text[16];
    const char *name = PcOptions_ResolutionName();
    return PcGameText(name ? name : CTR_TEXT("DESKTOP", "ESCRITORIO", "BUREAU", "DESKTOP", "AREA DE TRAB.",
                                             "DESKTOP", "DESKTOP"), text, sizeof(text));
}

/* A key row: either arrow waits for the next key; the text is the key's. */
#define PC_KEY_ROW(button, suffix)                                                       \
    static void PcBindStep_##suffix(int direction) { (void)direction; PcOptions_BindStart(button); } \
    static const u8 *PcBindText_##suffix(void)                                           \
    {                                                                                    \
        static u8 text[16];                                                              \
        return PcGameText(PcOptions_BindName(button), text, sizeof(text));               \
    }
PC_KEY_ROW(PC_BUTTON_A, a)
PC_KEY_ROW(PC_BUTTON_B, b)
PC_KEY_ROW(PC_BUTTON_X, x)
PC_KEY_ROW(PC_BUTTON_Y, y)
PC_KEY_ROW(PC_BUTTON_L, l)
PC_KEY_ROW(PC_BUTTON_R, r)
PC_KEY_ROW(PC_BUTTON_START, start)
PC_KEY_ROW(PC_BUTTON_SELECT, select)
PC_KEY_ROW(PC_BUTTON_UP, up)
PC_KEY_ROW(PC_BUTTON_DOWN, down)
PC_KEY_ROW(PC_BUTTON_LEFT, left)
PC_KEY_ROW(PC_BUTTON_RIGHT, right)
PC_KEY_ROW(PC_BUTTON_ZL, zl)
PC_KEY_ROW(PC_BUTTON_ZR, zr)

static void PcResetKeys(void) { PcOptions_ResetKeys(); }
