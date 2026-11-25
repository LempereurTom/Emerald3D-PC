/*
 * A backup of the save before the game first overwrites it (J6).
 *
 * The console's Port_WriteFlash (3ds_compat.c) writes each sector into
 * emerald3ds.sav as the game saves. On the PC its first call in a session is
 * preceded by a copy of the save and its companions - the files the builder
 * backs up before an install (builder/emerald3ds_builder/install.py,
 * SAVE_FILES) - into <home>/backup/<YYYYMMDD-HHMMSS>[-N]/, the builder's own
 * layout. A copy being made is <stamp>.partial and renamed when complete, so
 * one cut short stays marked. Nothing is ever deleted: a backup is 128 KiB.
 * A failed backup is logged and the save goes ahead (refusing to save would
 * lose the player's progress instead).
 *
 * Linked with --wrap=Port_WriteFlash (pc.mk); the callers are the game's
 * flash driver (agb_flash_dummy.c).
 */
#include <3ds.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "3ds_log.h"
#include "3ds_platform.h"

#define DIR "sdmc:/3ds/emerald3ds/"
static const char *const sSaveFiles[] = {
    "emerald3ds.sav",      "emerald3ds.clock",    "emerald3ds.clock.tmp",    "emerald3ds.clock.bak",
    "emerald3ds.nuzlocke", "emerald3ds.nuzlocke.tmp", "emerald3ds.nuzlocke.bak",
};

static bool Exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    fclose(f);
    return true;
}

static bool Copy(const char *from, const char *to)
{
    char buffer[16384];
    size_t n;
    bool ok = true;
    FILE *in = fopen(from, "rb"), *out;

    if (in == NULL)
        return false;
    out = fopen(to, "wb");
    if (out == NULL)
    {
        fclose(in);
        return false;
    }
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0)
        if (fwrite(buffer, 1, n, out) != n)
        {
            ok = false;
            break;
        }
    ok = ok && !ferror(in);
    fclose(in);
    if (fclose(out) != 0)
        ok = false;
    return ok;
}

static void BackUp(void)
{
    char stamp[32], folder[160], partial[176], from[160], to[256];
    time_t now = time(NULL);
    struct tm local;
    unsigned copied = 0;

    if (!Exists(DIR "emerald3ds.sav"))
        return; /* the first save: nothing to keep */
    localtime_r(&now, &local);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    mkdir(DIR "backup", 0777);
    /* The first free name: neither the final folder nor its .partial. */
    for (int n = 1;; ++n)
    {
        struct stat st;

        if (n >= 1000)
        {
            CtrLog_Write(CTR_LOG_ERROR, "save backup: no free folder name in backup/");
            return;
        }
        if (n == 1)
            snprintf(folder, sizeof(folder), DIR "backup/%s", stamp);
        else
            snprintf(folder, sizeof(folder), DIR "backup/%s-%d", stamp, n);
        snprintf(partial, sizeof(partial), "%s.partial", folder);
        if (stat(folder, &st) == 0)
            continue;
        if (mkdir(partial, 0777) == 0)
            break;
    }
    for (size_t i = 0; i < sizeof(sSaveFiles) / sizeof(sSaveFiles[0]); ++i)
    {
        snprintf(from, sizeof(from), DIR "%s", sSaveFiles[i]);
        if (!Exists(from))
            continue;
        snprintf(to, sizeof(to), "%s/%s", partial, sSaveFiles[i]);
        if (!Copy(from, to))
        {
            CtrLog_Write(CTR_LOG_ERROR, "save backup: copying %s failed (errno=%d); %s left as it is",
                         sSaveFiles[i], errno, partial);
            return;
        }
        ++copied;
    }
    if (rename(partial, folder) != 0)
    {
        CtrLog_Write(CTR_LOG_ERROR, "save backup: %s not renamed (errno=%d)", partial, errno);
        return;
    }
    CtrLog_Write(CTR_LOG_FS, "save backup: %u files in backup/%s before the first write", copied,
                 folder + strlen(DIR "backup/"));
}

u16 __real_Port_WriteFlash(u32 offset, const void *data, u32 size);
u16 __wrap_Port_WriteFlash(u32 offset, const void *data, u32 size)
{
    static bool sDone;

    /* Only when the write really goes to the file (not a scenario played). */
    if (!sDone && CtrScenario_SaveWrites())
    {
        sDone = true;
        BackUp();
    }
    return __real_Port_WriteFlash(offset, data, size);
}
