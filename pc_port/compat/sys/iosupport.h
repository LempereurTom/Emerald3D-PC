/*
 * newlib's device table, as 3ds_log.c uses it to route stdout/stderr into the
 * log. On a PC stdout and stderr stay the terminal's: the table exists so the
 * assignment compiles, and nothing reads it.
 */
#ifndef PC_PORT_SYS_IOSUPPORT_H
#define PC_PORT_SYS_IOSUPPORT_H

#include <stddef.h>
#include <sys/types.h>

struct _reent;

enum { STD_IN = 0, STD_OUT = 1, STD_ERR = 2, STD_MAX = 35 };

typedef struct
{
    const char *name;
    size_t structSize;
    void *open_r;
    void *close_r;
    ssize_t (*write_r)(struct _reent *r, void *fd, const char *ptr, size_t len);
} devoptab_t;

extern const devoptab_t *devoptab_list[STD_MAX];

#endif
