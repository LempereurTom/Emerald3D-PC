/*
 * The 3DS's paths on a PC.
 *
 * The port names its files the console's way: "sdmc:/3ds/emerald3ds/..." for
 * saves, settings, logs and the data pack, "romfs:/..." for the files that
 * ship with the executable. This header, force-included in every unit of the
 * PC build (game and native), sends the C library's file calls through
 * pc_port/src/pc_paths.c, which maps those prefixes to real folders:
 *
 *   sdmc:/3ds/emerald3ds/X  ->  <home>/X
 *   sdmc:/3ds               ->  <home>
 *   sdmc:/X                 ->  <home>/sdmc/X
 *   romfs:/X                ->  <romfs>/X
 *
 * <home> is EMERALD_PC_HOME, else %APPDATA%\EmeraldPC (Windows) or
 * $XDG_DATA_HOME/emerald-pc; <romfs> is EMERALD_PC_ROMFS, else engine/ next to
 * the executable (a release's engine files; a development run names its
 * build/pc-romfs). Paths are UTF-8 (wide calls on Windows).
 *
 * Any other path is passed through. mkdir takes the POSIX two arguments on
 * every host. fopencookie, a glibc extension the data pack uses, is provided
 * where the C library lacks it (MinGW).
 *
 * Nothing in the shared tree changes: the redirection is a macro layer.
 */
#ifndef PC_PORT_PATHS_H
#define PC_PORT_PATHS_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
/* Declared here first, before the macros below rename the calls: MinGW's
 * one-argument mkdir lives in these. */
#include <direct.h>
#include <io.h>
#include <unistd.h>
#include <time.h>
/* POSIX's, which the port's clock calls (3ds_game_bridge.c); MinGW only
 * declares it with _POSIX_C_SOURCE. pc_misc.c defines it. */
struct tm *localtime_r(const time_t *timer, struct tm *result);
#endif

#ifdef __cplusplus
extern "C" {
#endif

FILE *PcPaths_fopen(const char *path, const char *mode);
int PcPaths_remove(const char *path);
int PcPaths_rename(const char *from, const char *to);
int PcPaths_mkdir(const char *path, int mode);
int PcPaths_stat(const char *path, struct stat *st);
/* The real path of a 3DS path (into out, size bytes); out is returned. */
const char *PcPaths_Resolve(const char *path, char *out, size_t size);
/* The user folder (saves, settings, logs), UTF-8, no trailing slash. */
const char *PcPaths_Home(void);

#ifndef PC_PATHS_IMPLEMENTATION
#define fopen(path, mode) PcPaths_fopen((path), (mode))
#define remove(path) PcPaths_remove(path)
#define rename(from, to) PcPaths_rename((from), (to))
#define mkdir(path, mode) PcPaths_mkdir((path), (mode))
/* stat is both a function and a struct: only the call form is redirected. */
#define stat(path, st) PcPaths_stat((path), (st))
#endif

#ifdef _WIN32
typedef long long off64_t;
typedef ssize_t cookie_read_function_t(void *cookie, char *buf, size_t size);
typedef ssize_t cookie_write_function_t(void *cookie, const char *buf, size_t size);
/* off_t, as 3ds_data.c's seek callback is written (newlib's signature). */
typedef int cookie_seek_function_t(void *cookie, off_t *offset, int whence);
typedef int cookie_close_function_t(void *cookie);
typedef struct
{
    cookie_read_function_t *read;
    cookie_write_function_t *write;
    cookie_seek_function_t *seek;
    cookie_close_function_t *close;
} cookie_io_functions_t;
/* Read-only: the stream is read through to its end into a temporary file
 * that is deleted when closed (good enough for the pack's entries). */
FILE *fopencookie(void *cookie, const char *mode, cookie_io_functions_t io);
#endif

#ifdef __cplusplus
}
#endif

#endif
