/*
 * The bundle loader (3ds_script_loader.c's place on the PC).
 *
 * On the console, scripts, songs and the game's read-only data are linked as
 * reserved regions and filled from bundles before AgbMain. The PC links them
 * in place, all together between pc_game_group.c's two markers
 * (__ctr_gamedata, __ctr_gamedata_end; pc.mk gathers the sections), at fixed
 * addresses (no ASLR), so their pointers are final.
 *
 * A development build carries them. A release does not
 * (pc_port/tools/pc_release.py): the bytes between the markers are zeroes,
 * and the start marker says EXTERNAL. They are then read back, as they were
 * linked, from the player's data (gamedata/gamedata.bin, rebuilt from their
 * ROM by the builder) into that region before anything reads a game table.
 * The scripts and songs are inside it: their own bundles are nothing to do.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "3ds_data.h"
#include "3ds_log.h"

bool CtrGameData_Init(void);
uint32_t CtrGameData_Bytes(void);
bool CtrScripts_Init(void);
uint32_t CtrScripts_Bytes(void);
bool CtrSongs_Init(void);
uint32_t CtrSongs_Bytes(void);
const uint8_t *Port_ResolveScriptPointer(const uint8_t *ptr);
const uint8_t *Port_ResolveTextPointer(const uint8_t *ptr);
const void *Port_ResolveEventPointer(const void *ptr);

extern const char __ctr_gamedata[32], __ctr_gamedata_end[32];

#define GAMEDATA_PATH "gamedata/gamedata.bin"
#define EXTERNAL_MARK "EMERALD-PC GAMEDATA EXTERNAL"

static uint32_t sGameDataBytes;

/* The region writable for the copy, then read-only again. */
static bool Unprotect(void *start, size_t size, bool writable)
{
#ifdef _WIN32
    DWORD old;
    return VirtualProtect(start, size, writable ? PAGE_READWRITE : PAGE_READONLY, &old) != 0;
#else
    long page = sysconf(_SC_PAGESIZE);
    uintptr_t from = (uintptr_t)start & ~(uintptr_t)(page - 1);
    uintptr_t to = ((uintptr_t)start + size + page - 1) & ~(uintptr_t)(page - 1);
    return mprotect((void *)from, to - from, writable ? PROT_READ | PROT_WRITE : PROT_READ) == 0;
#endif
}

bool PcGameData_External(void)
{
    /* Read through a volatile pointer: the release rewrites the marker in
     * the file, after the compiler saw its contents. */
    const volatile char *mark = __ctr_gamedata;
    char text[sizeof(EXTERNAL_MARK)];

    for (size_t i = 0; i < sizeof(text); ++i)
        text[i] = mark[i];
    return memcmp(text, EXTERNAL_MARK, sizeof(EXTERNAL_MARK) - 1) == 0;
}

bool CtrGameData_Init(void)
{
    uint8_t *region = (uint8_t *)(uintptr_t)(__ctr_gamedata + sizeof(__ctr_gamedata));
    uint32_t size = (uint32_t)((uintptr_t)__ctr_gamedata_end - (uintptr_t)region), stored = 0;
    FILE *file;
    bool ok;

    if (!PcGameData_External())
    {
        CtrLog_Write(CTR_LOG_BOOT, "game data: linked in place (development build, %lu KiB)",
                     (unsigned long)(size >> 10));
        return true;
    }
    if (!CtrData_Size(GAMEDATA_PATH, &stored) || stored != size)
    {
        CtrLog_Write(CTR_LOG_ERROR, "game data: %s is %lu bytes, this executable needs %lu", GAMEDATA_PATH,
                     (unsigned long)stored, (unsigned long)size);
        return false;
    }
    file = CtrData_Open(GAMEDATA_PATH);
    if (file == NULL || !Unprotect(region, size, true))
    {
        if (file)
            fclose(file);
        CtrLog_Write(CTR_LOG_ERROR, "game data: cannot open %s or write its region", GAMEDATA_PATH);
        return false;
    }
    ok = fread(region, 1, size, file) == size;
    fclose(file);
    Unprotect(region, size, false);
    if (!ok)
    {
        CtrLog_Write(CTR_LOG_ERROR, "game data: %s could not be read", GAMEDATA_PATH);
        return false;
    }
    sGameDataBytes = size;
    CtrLog_Write(CTR_LOG_BOOT, "game data: %lu KiB in place, from the player's data", (unsigned long)(size >> 10));
    return true;
}

uint32_t CtrGameData_Bytes(void)
{
    return sGameDataBytes;
}

/* The scripts and songs are inside the game data's region. */
bool CtrScripts_Init(void)
{
    return true;
}

uint32_t CtrScripts_Bytes(void)
{
    return 0;
}

bool CtrSongs_Init(void)
{
    return true;
}

uint32_t CtrSongs_Bytes(void)
{
    return 0;
}

/* As on the console: script, text and event pointers are already final. */
const uint8_t *Port_ResolveScriptPointer(const uint8_t *ptr) { return ptr; }
const uint8_t *Port_ResolveTextPointer(const uint8_t *ptr) { return ptr; }
const void *Port_ResolveEventPointer(const void *ptr) { return ptr; }
