/*
 * The 3DS's paths on a PC (see pc_port/compat/pc_paths.h).
 */
#define PC_PATHS_IMPLEMENTATION
#include "pc_paths.h"
/* The header is also force-included ahead of this file (-include): drop the
 * redirections here, where the real calls are made. */
#undef fopen
#undef remove
#undef rename
#undef mkdir
#undef stat

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#endif

/*
 * The two folders (J6). Paths are UTF-8 throughout (SDL's, the user's name
 * in %APPDATA% included); Windows' narrow calls would read them in the ANSI
 * code page, so there the wide ones are used (Wide, below).
 *
 *   saves, settings, logs  EMERALD_PC_HOME, else the user's data folder:
 *                          %APPDATA%\EmeraldPC (Windows),
 *                          $XDG_DATA_HOME/emerald-pc or ~/.local/share/emerald-pc
 *   the game's data        EMERALD_PC_ROMFS, else romfs/ next to the executable
 */
static char sHome[1024], sRomfs[1024];

static void TrimSlash(char *path)
{
    size_t n = strlen(path);
    while (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\'))
        path[--n] = 0;
}

static const char *Home(void)
{
    if (sHome[0] == 0)
    {
        const char *env = SDL_getenv("EMERALD_PC_HOME"); /* UTF-8, unlike getenv on Windows */
        if (env != NULL && *env)
            snprintf(sHome, sizeof(sHome), "%s", env);
        else
        {
#ifdef _WIN32
            char *pref = SDL_GetPrefPath(NULL, "EmeraldPC");
#else
            char *pref = SDL_GetPrefPath(NULL, "emerald-pc");
#endif
            snprintf(sHome, sizeof(sHome), "%s", pref != NULL ? pref : "userdata");
            SDL_free(pref);
        }
        TrimSlash(sHome);
    }
    return sHome;
}

static const char *Romfs(void)
{
    if (sRomfs[0] == 0)
    {
        const char *env = SDL_getenv("EMERALD_PC_ROMFS");
        const char *base = SDL_GetBasePath();
        if (env != NULL && *env)
            snprintf(sRomfs, sizeof(sRomfs), "%s", env);
        else
            snprintf(sRomfs, sizeof(sRomfs), "%sengine", base != NULL ? base : "");
        TrimSlash(sRomfs);
    }
    return sRomfs;
}

const char *PcPaths_Home(void) { return Home(); }

#ifdef _WIN32
/* UTF-8 to UTF-16 for the wide calls; NULL if it does not fit. */
static wchar_t *Wide(const char *utf8, wchar_t *out, int count)
{
    return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, count) > 0 ? out : NULL;
}
#endif

static int StartsWith(const char *s, const char *prefix, const char **rest)
{
    size_t n = strlen(prefix);
    if (strncmp(s, prefix, n) != 0)
        return 0;
    *rest = s + n;
    return 1;
}

const char *PcPaths_Resolve(const char *path, char *out, size_t size)
{
    const char *rest;

    if (path == NULL)
        return NULL;
    if (StartsWith(path, "sdmc:/3ds/emerald3ds", &rest))
        snprintf(out, size, "%s%s", Home(), rest);
    else if (StartsWith(path, "sdmc:/3ds", &rest))
        snprintf(out, size, "%s%s", Home(), rest);
    else if (StartsWith(path, "sdmc:/", &rest))
        snprintf(out, size, "%s/sdmc/%s", Home(), rest);
    else if (StartsWith(path, "romfs:/", &rest))
        snprintf(out, size, "%s/%s", Romfs(), rest);
    else
        snprintf(out, size, "%s", path);
    return out;
}

FILE *PcPaths_fopen(const char *path, const char *mode)
{
    char real[1024];
    PcPaths_Resolve(path, real, sizeof(real));
#ifdef _WIN32
    wchar_t wpath[1024], wmode[16];
    if (Wide(real, wpath, 1024) && Wide(mode, wmode, 16))
        return _wfopen(wpath, wmode);
    errno = ENAMETOOLONG;
    return NULL;
#else
    return fopen(real, mode);
#endif
}

int PcPaths_remove(const char *path)
{
    char real[1024];
    PcPaths_Resolve(path, real, sizeof(real));
#ifdef _WIN32
    wchar_t wpath[1024];
    return Wide(real, wpath, 1024) ? _wremove(wpath) : -1;
#else
    return remove(real);
#endif
}

int PcPaths_rename(const char *from, const char *to)
{
    char a[1024], b[1024];
    PcPaths_Resolve(from, a, sizeof(a));
    PcPaths_Resolve(to, b, sizeof(b));
#ifdef _WIN32
    /* POSIX rename replaces the destination; msvcrt's refuses to. */
    wchar_t wa[1024], wb[1024];
    if (!Wide(a, wa, 1024) || !Wide(b, wb, 1024))
        return -1;
    return MoveFileExW(wa, wb, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
#else
    return rename(a, b);
#endif
}

int PcPaths_mkdir(const char *path, int mode)
{
    char real[1024];
    PcPaths_Resolve(path, real, sizeof(real));
#ifdef _WIN32
    (void)mode;
    wchar_t wpath[1024];
    return Wide(real, wpath, 1024) ? _wmkdir(wpath) : -1;
#else
    return mkdir(real, (mode_t)mode);
#endif
}

int PcPaths_stat(const char *path, struct stat *st)
{
    char real[1024];
    PcPaths_Resolve(path, real, sizeof(real));
#ifdef _WIN32
    wchar_t wpath[1024];
    struct _stat64 wide;
    if (!Wide(real, wpath, 1024) || _wstat64(wpath, &wide) != 0)
        return -1;
    memset(st, 0, sizeof(*st));
    st->st_mode = wide.st_mode;
    st->st_size = (off_t)wide.st_size;
    st->st_mtime = (time_t)wide.st_mtime;
    return 0;
#else
    return stat(real, st);
#endif
}

#ifdef _WIN32
FILE *fopencookie(void *cookie, const char *mode, cookie_io_functions_t io)
{
    char dir[1024], name[1100];
    static volatile long sCounter;
    long n;
    FILE *file;
    char buffer[64 * 1024];
    ssize_t got;

    if (io.read == NULL || strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL)
    {
        errno = EINVAL;
        return NULL;
    }
    snprintf(dir, sizeof(dir), "%s/tmp", Home());
    mkdir(Home());
    mkdir(dir);
    n = InterlockedIncrement(&sCounter);
    snprintf(name, sizeof(name), "%s/stream-%lu-%ld.bin", dir, (unsigned long)GetCurrentProcessId(), n);
    /* "D": deleted when closed (msvcrt). */
    file = fopen(name, "w+bD");
    if (file == NULL)
        return NULL;
    while ((got = io.read(cookie, buffer, sizeof(buffer))) > 0)
    {
        if (fwrite(buffer, 1, (size_t)got, file) != (size_t)got)
        {
            fclose(file);
            if (io.close)
                io.close(cookie);
            return NULL;
        }
    }
    if (io.close)
        io.close(cookie);
    rewind(file);
    return file;
}
#endif
