/*
 * Emerald PC: the process entry point (main_3ds.c's counterpart).
 *
 * The same order as the console: the platform, its hooks (the game's VBlank,
 * the video's present), then CtrGame_Init, which enters AgbMain and only
 * returns through the platform's exit path (aptMainLoop turning false).
 */
#include <3ds.h>

#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "3ds_platform.h"
#include "3ds_video.h"
#include "pc_crash.h"

bool PcSetup_Ensure(void);
bool PcGameData_External(void);

static void OnInterrupt(int sig)
{
    (void)sig;
    PcApt_RequestExit();
}

/* `--home DIR`: the user folder (saves, settings, the game data), as
 * EMERALD_PC_HOME (pc_paths.c); RecompLauncher passes its own
 * (recomplauncher.json). The arguments are read in UTF-8: on Windows from
 * the wide command line, not the ANSI argv. */
static void ReadArguments(int argc, char **argv)
{
#ifdef _WIN32
    int count = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);
    (void)argc;
    (void)argv;
    for (int i = 1; wide && i + 1 < count; ++i)
        if (wcscmp(wide[i], L"--home") == 0)
        {
            char home[1024];
            if (WideCharToMultiByte(CP_UTF8, 0, wide[i + 1], -1, home, sizeof(home), NULL, NULL) > 0)
                SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "EMERALD_PC_HOME", home, true);
        }
    LocalFree(wide);
#else
    for (int i = 1; i + 1 < argc; ++i)
        if (strcmp(argv[i], "--home") == 0)
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "EMERALD_PC_HOME", argv[i + 1], true);
#endif
}

int main(int argc, char **argv)
{
    setvbuf(stderr, NULL, _IONBF, 0);
    ReadArguments(argc, argv);
    PcCrash_Install();
    /* Checks the crash report: a deliberate fault. */
    const char *crashTest = getenv("EMERALD_PC_CRASH_TEST");
    if (crashTest && *crashTest == '1')
    {
        volatile int *volatile nowhere = (volatile int *)(uintptr_t)16;
        *nowhere = 0;
    }
    signal(SIGINT, OnInterrupt);
    fprintf(stderr, "Emerald PC%s - a port of Emerald 3DS by Zallax and contributors\n",
            PcGameData_External() ? "" : " (development build)");

    /* A release's first run: the game data from the player's ROM (pc_setup.c). */
    if (!PcSetup_Ensure())
        return 0;
    if (!CtrPlatform_Init())
        CtrPlatform_Fatal("platform initialization failed");
    CtrPlatformHooks hooks = {0};
    hooks.vblank = CtrGame_VBlank;
    hooks.videoPresent = CtrVideo_Present;
    CtrPlatform_SetHooks(&hooks);
    if (!CtrPlatform_BeginFrame())
        return 0;
    CtrGame_Init();

    CtrPlatform_Shutdown();
    return 0;
}
