/*
 * The PC port's place in OPTIONS (3ds_port/src/3ds_bottom_options.inc, under
 * PORT_PC; the lines: pc_extras.inc):
 *   GENERAL  + SOUND (master, music, effects) and ABOUT - the credit the
 *              licence asks for (CC BY-NC-SA 4.0: Emerald 3DS by Zallax and
 *              contributors, where it is from, and that this is a modified,
 *              unofficial PC port; the README says what changed)
 *   DISPLAY  SCREEN (the window), PICTURE (scaling, filter, colours),
 *            LAYOUT (the two screens), AMBIENCE (particles, weather)
 *   CONTROLS BUTTONS, D-PAD & MENUS, SPEED (and the keys' reset), GAMEPAD
 */
static const OptRowDef sRowsPcSound[] = {
    EXTRA_ROW("pc_vol_master"),
    EXTRA_ROW("pc_vol_music"),
    EXTRA_ROW("pc_vol_sfx"),
};
static const OptRowDef sRowsPcAbout[] = {
    {ROW_INFO, 0, NULL, CTR_TEXT("EMERALD 3DS BY ZALLAX", "EMERALD 3DS POR ZALLAX", "EMERALD 3DS PAR ZALLAX", "EMERALD 3DS DI ZALLAX", "EMERALD 3DS POR ZALLAX", "EMERALD 3DS VON ZALLAX", "EMERALD 3DS: ZALLAX")},
    {ROW_INFO, 0, NULL, CTR_TEXT("AND CONTRIBUTORS", "Y COLABORADORES", "ET CONTRIBUTEURS", "E CONTRIBUTORI", "E COLABORADORES", "UND MITWIRKENDE", "ほか きょうりょくしゃ")},
    {ROW_INFO, 0, NULL, "GITHUB.COM/ZALLAXDEV"},
    {ROW_INFO, 0, NULL, CTR_TEXT("UNOFFICIAL PC PORT", "PORT PC NO OFICIAL", "PORTAGE PC NON OFFICIEL", "PORT PC NON UFFICIALE", "PORT PC NÃO OFICIAL", "INOFFIZ. PC-PORT", "ひこうしき PCばん")},
    {ROW_INFO, 0, NULL, CTR_TEXT("MODIFIED: SEE README", "MODIFICADO: VER README", "MODIFIÉ : VOIR README", "MODIFICATO: VEDI README", "MODIFICADO: VER README", "GEÄNDERT: SIEHE README", "かいぞう: README")},
    {ROW_INFO, 0, NULL, "CC BY-NC-SA 4.0"},
};

static const OptRowDef sRowsPcScreen[] = {
    EXTRA_ROW("pc_window"),
    EXTRA_ROW("pc_res"),
    EXTRA_ROW("pc_scale"),
    EXTRA_ROW("pc_vsync"),
    {ROW_INFO, 0, NULL, CTR_TEXT("F11: FULLSCREEN", "F11: PANT. COMPLETA", "F11 : PLEIN ÉCRAN", "F11: SCHERMO INTERO", "F11: TELA CHEIA", "F11: VOLLBILD", "F11: ぜんがめん")},
};
static const OptRowDef sRowsPcPicture[] = {
    EXTRA_ROW("pc_stretch"),
    EXTRA_ROW("pc_filter"),
    EXTRA_ROW("pc_colors"),
};
static const OptRowDef sRowsPcLayout[] = {
    EXTRA_ROW("pc_layout"),
    EXTRA_ROW("pc_swap"),
    {ROW_INFO, 0, NULL, CTR_TEXT("F9: NEXT LAYOUT", "F9: SIG. DISPOSICIÓN", "F9 : DISPOSITION SUIV.", "F9: DISPOSIZIONE SUCC.", "F9: PRÓX. DISPOSIÇÃO", "F9: NÄCHSTE ANORDNUNG", "F9: つぎの はいち")},
    {ROW_INFO, 0, NULL, CTR_TEXT("TAB: SWAP, FUSION: OPEN", "TAB: INTERC., FUSION: ABRIR", "TAB : INVERSER, FUSION : OUVRIR", "TAB: SCAMBIA / APRI", "TAB: TROCA / ABRIR", "TAB: TAUSCH / ÖFFNEN", "TAB: いれかえ / ひらく")},
};
static const OptRowDef sRowsPcAmbience[] = {
    EXTRA_ROW("pc_particles"),
    EXTRA_ROW("pc_weather"),
    {ROW_INFO, 0, NULL, CTR_TEXT("OUTDOORS ONLY", "SOLO AL AIRE LIBRE", "EN EXTÉRIEUR SEULEMENT", "SOLO ALL'APERTO", "SÓ AO AR LIVRE", "NUR DRAUSSEN", "そとだけ")},
    {ROW_INFO, 0, NULL, CTR_TEXT("SHOWERS: ROUTES, LOOKS ONLY", "LLUVIA: RUTAS, SOLO VISUAL", "AVERSES : ROUTES, VISUEL", "PIOGGIA: PERCORSI, VISIVA", "CHUVA: ROTAS, SÓ VISUAL", "SCHAUER: ROUTEN, OPTISCH", "あめ: みちのみ みためだけ")},
};

static const OptRowDef sRowsPcButtons[] = {
    EXTRA_ROW("pc_kb_a"),
    EXTRA_ROW("pc_kb_b"),
    EXTRA_ROW("pc_kb_x"),
    EXTRA_ROW("pc_kb_y"),
    EXTRA_ROW("pc_kb_l"),
    EXTRA_ROW("pc_kb_r"),
};
static const OptRowDef sRowsPcDpad[] = {
    EXTRA_ROW("pc_kb_up"),
    EXTRA_ROW("pc_kb_down"),
    EXTRA_ROW("pc_kb_left"),
    EXTRA_ROW("pc_kb_right"),
    EXTRA_ROW("pc_kb_start"),
    EXTRA_ROW("pc_kb_select"),
};
static const OptRowDef sRowsPcSpeed[] = {
    EXTRA_ROW("pc_kb_zl"),
    EXTRA_ROW("pc_kb_zr"),
    EXTRA_ROW("pc_kb_reset"),
    {ROW_INFO, 0, NULL, CTR_TEXT("ARROW: PRESS A KEY", "FLECHA: PULSA UNA TECLA", "FLÈCHE : APPUIE", "FRECCIA: PREMI UN TASTO", "SETA: APERTE UMA TECLA", "PFEIL: TASTE DRÜCKEN", "やじるし: キーを おす")},
    {ROW_INFO, 0, NULL, CTR_TEXT("ESC: CANCEL", "ESC: CANCELAR", "ÉCHAP : ANNULER", "ESC: ANNULLA", "ESC: CANCELAR", "ESC: ABBRECHEN", "ESC: やめる")},
};
static const OptRowDef sRowsPcPad[] = {
    EXTRA_ROW("pc_pad_layout"),
    {ROW_INFO, 0, NULL, CTR_TEXT("START: MENU", "START: MENÚ", "START : MENU", "START: MENU", "START: MENU", "START: MENÜ", "START: メニュー")},
    {ROW_INFO, 0, NULL, CTR_TEXT("STICK: CIRCLE PAD", "STICK: PAD CIRCULAR", "STICK : PAD CIRCULAIRE", "STICK: PAD SCORREVOLE", "ANALÓGICO: DIRECIONAL", "STICK: SCHIEBEPAD", "スティック: スライドパッド")},
    {ROW_INFO, 0, NULL, CTR_TEXT("TRIGGERS: SPEED", "GATILLOS: VELOCIDAD", "GÂCHETTES : VITESSE", "GRILLETTI: VELOCITÀ", "GATILHOS: VELOCIDADE", "TRIGGER: TEMPO", "トリガー: スピード")},
};

static const OptGroupDef sGroupsPcDisplay[] = {
    GROUP(CTR_TEXT("SCREEN", "PANTALLA", "AFFICHAGE", "SCHERMO", "TELA", "BILDSCHIRM", "がめん"), sRowsPcScreen),
    GROUP(CTR_TEXT("PICTURE", "IMAGEN", "IMAGE", "IMMAGINE", "IMAGEM", "BILD", "がしつ"), sRowsPcPicture),
    GROUP(CTR_TEXT("LAYOUT", "DISPOSICIÓN", "DISPOSITION", "DISPOSIZIONE", "DISPOSIÇÃO", "ANORDNUNG", "はいち"), sRowsPcLayout),
    GROUP(CTR_TEXT("AMBIENCE", "AMBIENTE", "AMBIANCE", "ATMOSFERA", "AMBIENTE", "ATMOSPHÄRE", "ふんいき"), sRowsPcAmbience),
};
static const OptGroupDef sGroupsPcControls[] = {
    GROUP(CTR_TEXT("BUTTONS", "BOTONES", "BOUTONS", "TASTI", "BOTÕES", "TASTEN", "ボタン"), sRowsPcButtons),
    GROUP(CTR_TEXT("D-PAD & MENUS", "CRUCETA Y MENÚS", "CROIX ET MENUS", "CROCE E MENU", "DIRECIONAL", "STEUERKREUZ", "じゅうじキー"), sRowsPcDpad),
    GROUP(CTR_TEXT("SPEED", "VELOCIDAD", "VITESSE", "VELOCITÀ", "VELOCIDADE", "TEMPO", "スピード"), sRowsPcSpeed),
    GROUP(CTR_TEXT("GAMEPAD", "MANDO", "MANETTE", "PAD", "CONTROLE", "GAMEPAD", "パッド"), sRowsPcPad),
};
#define PC_GENERAL_GROUPS                                                                       \
    GROUP(CTR_TEXT("SOUND", "SONIDO", "SON", "AUDIO", "SOM", "TON", "サウンド"), sRowsPcSound), \
    GROUP(CTR_TEXT("ABOUT", "ACERCA DE", "À PROPOS", "INFO", "SOBRE", "INFO", "じょうほう"), sRowsPcAbout)
#define PC_CATS                                                                                                      \
    {CTR_TEXT("DISPLAY", "PANTALLA", "ÉCRAN", "SCHERMO", "TELA", "ANZEIGE", "がめん"), sGroupsPcDisplay, ARRAY_COUNT(sGroupsPcDisplay)}, \
    {CTR_TEXT("CONTROLS", "CONTROLES", "COMMANDES", "COMANDI", "CONTROLES", "STEUERUNG", "そうさ"), sGroupsPcControls, ARRAY_COUNT(sGroupsPcControls)},
