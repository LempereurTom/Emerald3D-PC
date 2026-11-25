/*
 * Odds and ends a PE executable lacks.
 */
#include <time.h>

/*
 * __ctr_text_start / _end: the code the profiler hashes (3ds_prof.c), which
 * the 3DS linker script (emerald3ds.ld.in) defines; the PE link does not
 * group the game's code, so the hash is of nothing. The game's variables
 * (__ctr_game_data/bss, the soft reset) are grouped: pc_game_group.c.
 * Symbols are spelled with the leading underscore of 32-bit Windows.
 */
__asm__(".data\n"
        ".globl ___ctr_text_start\n___ctr_text_start:\n"
        ".globl ___ctr_text_end\n___ctr_text_end:\n"
        ".text\n");

/* The link's id (3ds_prof.c logs it): fixed for a development build. */
const char gCtrLinkId[] = "EPID-development";

#ifdef _WIN32
/* POSIX's thread-safe localtime (3ds_game_bridge.c); MSVCRT has localtime_s. */
struct tm *localtime_r(const time_t *timer, struct tm *result)
{
    return localtime_s(result, timer) == 0 ? result : NULL;
}
#endif

/*
 * A scenario playback never writes saves (3ds_scenario.c): a measurement must
 * not touch them. EMERALD_PC_SCENARIO_SAVES=1 lets one write: the game then
 * saves into the home folder's emerald3ds.sav (the scenario's own save.sav is
 * only read), to prepare a save at a given spot (a scenario that ends with
 * SAVE -> YES). Development aid only; linked with --wrap.
 */
#include <stdbool.h>
#include <stdlib.h>

bool __real_CtrScenario_SaveWrites(void);
bool __wrap_CtrScenario_SaveWrites(void)
{
    static int allowed = -1;

    if (allowed < 0)
    {
        const char *env = getenv("EMERALD_PC_SCENARIO_SAVES");
        allowed = env && *env && *env != '0';
    }
    return allowed || __real_CtrScenario_SaveWrites();
}
