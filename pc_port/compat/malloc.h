/*
 * newlib's heap statistics, as 3ds_platform.c's memory report reads them.
 *
 * A PC's heap has no fixed size to report: mallinfo() answers zeros and
 * sbrk() fails (the report then shows "heap room=0"). The system header is
 * included first, so <malloc.h> keeps everything else it declares.
 */
#ifndef PC_PORT_MALLOC_H
#define PC_PORT_MALLOC_H

#include_next <malloc.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mallinfo
{
    size_t arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost;
};
struct mallinfo mallinfo(void);
void *sbrk(ptrdiff_t increment);
extern char *fake_heap_start, *fake_heap_end;

#ifdef __cplusplus
}
#endif

#endif
