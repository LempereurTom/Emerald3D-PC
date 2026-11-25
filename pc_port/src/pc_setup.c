/*
 * A release's first run: the game data, made from the player's ROM.
 *
 * A release's executable has no game data (pc_port/tools/pc_release.py;
 * pc_bundles.c reads it back as the game starts). Before the platform starts,
 * this checks the player's data (<home>/devdata, its marker naming the
 * engine ABI it was made for) against the executable's (romfs/engine/abi.bin).
 * Missing, or made for another release, it asks for the ROM (the system's
 * file dialog) and runs the builder beside the executable
 * (emerald-pc-builder: the recipe applied to the ROM, pc_port/tools/pc_builder.py),
 * showing its progress in a small window. A development build carries its
 * data: nothing to do.
 *
 * EMERALD_PC_BUILDER replaces the builder's command (arguments separated by
 * '|'), e.g. the script itself during development.
 */
#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "3ds_locale.h"
#include "pc_paths.h"

bool PcGameData_External(void);
bool PcSetup_Ensure(void);

#define LOOSE_MARKER "sdmc:/3ds/emerald3ds/devdata/.emerald3ds-dev"
#define ENGINE_ABI "romfs:/engine/abi.bin"

static bool ReadEngineAbi(unsigned long *abi)
{
    unsigned char b[4];
    FILE *f = fopen(ENGINE_ABI, "rb");
    bool ok = f && fread(b, 1, 4, f) == 4;

    if (f)
        fclose(f);
    *abi = (unsigned long)b[0] | (unsigned long)b[1] << 8 | (unsigned long)b[2] << 16 | (unsigned long)b[3] << 24;
    return ok;
}

static bool ReadDataAbi(unsigned long *abi)
{
    FILE *f = fopen(LOOSE_MARKER, "r");
    bool ok = f && fscanf(f, " abi %lx", abi) == 1;

    if (f)
        fclose(f);
    return ok;
}

/* ── The builder's run ──────────────────────────────────────────────────── */

static struct
{
    SDL_Mutex *lock;
    char line[256];     /* its last progress line */
    char error[512];
    int percent;
    bool done, ok;
} sRun;

static SDL_Process *sProcess;

static int SDLCALL ReadOutput(void *unused)
{
    SDL_IOStream *out = SDL_GetProcessOutput(sProcess);
    char buf[512];
    size_t used = 0;

    (void)unused;
    for (;;)
    {
        char c;
        if (SDL_ReadIO(out, &c, 1) != 1)
            break;
        if (c != '\n' && c != '\r')
        {
            if (used < sizeof(buf) - 1)
                buf[used++] = c;
            continue;
        }
        if (used == 0)
            continue;
        buf[used] = '\0';
        used = 0;
        SDL_LockMutex(sRun.lock);
        if (strncmp(buf, "ERROR: ", 7) == 0)
            SDL_strlcpy(sRun.error, buf + 7, sizeof(sRun.error));
        else if (buf[0] == '[')
        {
            sRun.percent = atoi(buf + 1);
            const char *message = strchr(buf, ']');
            SDL_strlcpy(sRun.line, message ? message + 2 : buf, sizeof(sRun.line));
        }
        SDL_UnlockMutex(sRun.lock);
    }
    return 0;
}

static bool StartBuilder(const char *rom)
{
    static char storage[8][1024];
    const char *args[12];
    int n = 0;
    const char *custom = SDL_getenv("EMERALD_PC_BUILDER");

    if (custom && *custom)
    {
        char *copy = SDL_strdup(custom), *save = NULL;
        for (char *part = SDL_strtok_r(copy, "|", &save); part && n < 8; part = SDL_strtok_r(NULL, "|", &save))
            SDL_strlcpy(storage[n], part, sizeof(storage[n])), args[n] = storage[n], ++n;
        SDL_free(copy);
    }
    else
    {
#ifdef _WIN32
        SDL_snprintf(storage[0], sizeof(storage[0]), "%semerald-pc-builder.exe", SDL_GetBasePath());
#else
        SDL_snprintf(storage[0], sizeof(storage[0]), "%semerald-pc-builder", SDL_GetBasePath());
#endif
        args[n] = storage[n];
        ++n;
    }
    args[n++] = "--rom";
    args[n++] = rom;
    args[n++] = "--home";
    args[n++] = PcPaths_Home();
    args[n] = NULL;

    memset(&sRun, 0, sizeof(sRun));
    sRun.lock = SDL_CreateMutex();
    SDL_strlcpy(sRun.line, "...", sizeof(sRun.line));
    sProcess = SDL_CreateProcess(args, true);
    if (sProcess == NULL)
    {
        SDL_snprintf(sRun.error, sizeof(sRun.error), "%s (%s)", SDL_GetError(), args[0]);
        return false;
    }
    SDL_DetachThread(SDL_CreateThread(ReadOutput, "builder output", NULL));
    return true;
}

/* ── The window ─────────────────────────────────────────────────────────── */

static void DrawProgress(SDL_Renderer *r, const char *title)
{
    char line[256];
    int percent;

    SDL_LockMutex(sRun.lock);
    SDL_strlcpy(line, sRun.line, sizeof(line));
    percent = sRun.percent;
    SDL_UnlockMutex(sRun.lock);
    SDL_SetRenderDrawColor(r, 20, 60, 45, 255);
    SDL_RenderClear(r);
    SDL_SetRenderScale(r, 2.0f, 2.0f);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDebugText(r, 16, 16, title);
    SDL_SetRenderDrawColor(r, 120, 220, 170, 255);
    SDL_RenderDebugText(r, 16, 40, line);
    SDL_SetRenderScale(r, 1.0f, 1.0f);
    SDL_FRect frame = { 32, 130, 576, 28 }, bar = { 36, 134, 568.0f * percent / 100.0f, 20 };
    SDL_SetRenderDrawColor(r, 120, 220, 170, 255);
    SDL_RenderRect(r, &frame);
    SDL_RenderFillRect(r, &bar);
    SDL_RenderPresent(r);
}

static struct
{
    SDL_AtomicInt state; /* 0 waiting, 1 chosen, 2 cancelled */
    char path[1024];
} sPick;

static void SDLCALL OnPicked(void *unused, const char *const *files, int filter)
{
    (void)unused;
    (void)filter;
    if (files && files[0])
    {
        SDL_strlcpy(sPick.path, files[0], sizeof(sPick.path));
        SDL_SetAtomicInt(&sPick.state, 1);
    }
    else
        SDL_SetAtomicInt(&sPick.state, 2);
}

static int Ask(SDL_Window *window, const char *title, const char *text, const char *yes, const char *no)
{
    const SDL_MessageBoxButtonData buttons[2] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yes },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, no },
    };
    const SDL_MessageBoxData data = { SDL_MESSAGEBOX_INFORMATION, window, title, text, 2, buttons, NULL };
    int chosen = 0;

    return SDL_ShowMessageBox(&data, &chosen) ? chosen : 0;
}

bool PcSetup_Ensure(void)
{
    unsigned long engine = 0, data = 0;
    const char *title = CTR_TEXT("Emerald PC - first run", "Emerald PC - primer inicio",
                                 "Emerald PC - première installation", "Emerald PC - primo avvio",
                                 "Emerald PC - primeira execução", "Emerald PC - erster Start",
                                 "Emerald PC - first run");
    const char *ask = CTR_TEXT(
        "Emerald PC contains no game data. It is made once, on this PC, from your own cartridge's ROM "
        "(Pokemon - Emerald Version).\n\nChoose your ROM file (.gba). It is only read, never copied.",
        "Emerald PC no contiene datos del juego. Se crean una vez, en este PC, a partir de la ROM de tu "
        "cartucho (Pokémon Edición Esmeralda).\n\nElige tu archivo ROM (.gba). Solo se lee, nunca se copia.",
        "Emerald PC ne contient aucune donnée du jeu. Elles sont créées une seule fois, sur ce PC, à partir "
        "de la ROM de ta cartouche (Pokémon Version Émeraude).\n\nChoisis ton fichier ROM (.gba). Il est "
        "seulement lu, jamais copié.",
        "Emerald PC non contiene dati del gioco. Vengono creati una volta, su questo PC, dalla ROM della tua "
        "cartuccia (Pokémon Versione Smeraldo).\n\nScegli il file ROM (.gba). Viene solo letto, mai copiato.",
        "Emerald PC não contém dados do jogo. Eles são criados uma vez, neste PC, a partir da ROM do seu "
        "cartucho (Pokémon Emerald).\n\nEscolha o arquivo ROM (.gba). Ele só é lido, nunca copiado.",
        "Emerald PC enthält keine Spieldaten. Sie werden einmal auf diesem PC aus der ROM deines Moduls "
        "erstellt (Pokémon Smaragd-Edition).\n\nWähle deine ROM-Datei (.gba). Sie wird nur gelesen, nie kopiert.",
        "Emerald PC contains no game data. It is made once, on this PC, from your own cartridge's ROM.\n\n"
        "Choose your ROM file (.gba). It is only read, never copied.");
    const char *choose = CTR_TEXT("Choose the ROM...", "Elegir la ROM...", "Choisir la ROM...", "Scegli la ROM...",
                                  "Escolher a ROM...", "ROM wählen...", "Choose the ROM...");
    const char *quit = CTR_TEXT("Quit", "Salir", "Quitter", "Esci", "Sair", "Beenden", "Quit");
    const char *making = CTR_TEXT("Making the game data from your ROM...", "Creating game data...",
                                  "Creation des donnees du jeu...", "Creating game data...",
                                  "Criando os dados do jogo...", "Spieldaten werden erstellt...",
                                  "Making the game data from your ROM...");
    const char *failed = CTR_TEXT("The game data could not be made", "No se pudieron crear los datos",
                                  "Les données du jeu n'ont pas pu être créées", "Impossibile creare i dati",
                                  "Não foi possível criar os dados", "Spieldaten konnten nicht erstellt werden",
                                  "The game data could not be made");
    const char *retry = CTR_TEXT("Try again", "Reintentar", "Réessayer", "Riprova", "Tentar de novo",
                                 "Erneut versuchen", "Try again");

    if (!PcGameData_External())
        return true;
    if (ReadEngineAbi(&engine) && ReadDataAbi(&data) && data == engine)
        return true;

    if (!SDL_Init(SDL_INIT_VIDEO))
        return false;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    if (!SDL_CreateWindowAndRenderer(title, 640, 200, 0, &window, &renderer))
        return false;
    SDL_SetRenderVSync(renderer, 1);
    bool ready = false;
    while (!ready)
    {
        SDL_SetRenderDrawColor(renderer, 20, 60, 45, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        if (!Ask(window, title, ask, choose, quit))
            break;
        static const SDL_DialogFileFilter filters[] = { { "Game Boy Advance ROM", "gba" } };
        SDL_SetAtomicInt(&sPick.state, 0);
        SDL_ShowOpenFileDialog(OnPicked, NULL, window, filters, 1, NULL, false);
        while (SDL_GetAtomicInt(&sPick.state) == 0)
        {
            SDL_Event e;
            while (SDL_PollEvent(&e))
                ;
            SDL_Delay(16);
        }
        if (SDL_GetAtomicInt(&sPick.state) != 1)
            continue;

        bool running = StartBuilder(sPick.path);
        int code = -1;
        while (running)
        {
            SDL_Event e;
            while (SDL_PollEvent(&e))
                ;
            DrawProgress(renderer, making);
            if (SDL_WaitProcess(sProcess, false, &code))
                running = false;
        }
        if (sProcess)
        {
            SDL_Delay(100); /* the output thread's last lines */
            SDL_DestroyProcess(sProcess);
            sProcess = NULL;
        }
        if (code == 0 && ReadEngineAbi(&engine) && ReadDataAbi(&data) && data == engine)
        {
            ready = true;
            break;
        }
        char text[768];
        SDL_snprintf(text, sizeof(text), "%s.\n\n%s", failed,
                     sRun.error[0] ? sRun.error
                                   : CTR_TEXT("Check that it is the right game, and its language.",
                                              "Comprueba que es el juego y el idioma correctos.",
                                              "Vérifie que c'est le bon jeu, dans la bonne langue.",
                                              "Controlla che sia il gioco e la lingua giusti.",
                                              "Verifique se é o jogo e o idioma certos.",
                                              "Prüfe, ob es das richtige Spiel in der richtigen Sprache ist.",
                                              "Check that it is the right game, and its language."));
        if (!Ask(window, title, text, retry, quit))
            break;
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return ready;
}
