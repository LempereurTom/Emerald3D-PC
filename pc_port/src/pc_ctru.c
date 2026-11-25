/*
 * libctru-lite (pc_port/compat/3ds.h) on POSIX threads.
 *
 * Locks spin briefly and then yield: the port's locks guard short sections
 * (the log ring, the pack handle, settings) and are rarely contended.
 * Events keep libctru's semantics: ONESHOT clears on the wait that consumes
 * the signal, STICKY stays set until cleared, PULSE wakes current waiters.
 */
#include <3ds.h>

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ── Locks ──────────────────────────────────────────────────────────────── */

void LightLock_Init(LightLock *lock)
{
    __atomic_store_n(lock, 1, __ATOMIC_RELEASE);
}

int LightLock_TryLock(LightLock *lock)
{
    s32 expected = 1;
    /* A zero-initialised lock (static storage never passed to Init) counts as
     * free too, as on the console where Init only writes 1. */
    if (__atomic_compare_exchange_n(lock, &expected, 0, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return 0;
    expected = 0;
    (void)expected;
    return 1;
}

void LightLock_Lock(LightLock *lock)
{
    unsigned spins = 0;
    for (;;)
    {
        s32 expected = 1;
        if (__atomic_compare_exchange_n(lock, &expected, 0, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return;
        if (++spins < 64)
            continue;
        sched_yield();
    }
}

void LightLock_Unlock(LightLock *lock)
{
    __atomic_store_n(lock, 1, __ATOMIC_RELEASE);
}

static u32 CurrentTag(void)
{
#ifdef _WIN32
    return (u32)GetCurrentThreadId();
#else
    return (u32)(uintptr_t)pthread_self();
#endif
}

void RecursiveLock_Init(RecursiveLock *lock)
{
    LightLock_Init(&lock->lock);
    lock->thread_tag = 0;
    lock->counter = 0;
}

void RecursiveLock_Lock(RecursiveLock *lock)
{
    u32 tag = CurrentTag();
    if (lock->counter != 0 && lock->thread_tag == tag)
    {
        lock->counter++;
        return;
    }
    LightLock_Lock(&lock->lock);
    lock->thread_tag = tag;
    lock->counter = 1;
}

int RecursiveLock_TryLock(RecursiveLock *lock)
{
    u32 tag = CurrentTag();
    if (lock->counter != 0 && lock->thread_tag == tag)
    {
        lock->counter++;
        return 0;
    }
    if (LightLock_TryLock(&lock->lock) != 0)
        return 1;
    lock->thread_tag = tag;
    lock->counter = 1;
    return 0;
}

void RecursiveLock_Unlock(RecursiveLock *lock)
{
    if (--lock->counter == 0)
    {
        lock->thread_tag = 0;
        LightLock_Unlock(&lock->lock);
    }
}

/* ── Events ─────────────────────────────────────────────────────────────── */

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    ResetType type;
    int signalled;
    unsigned pulse;
} EventImpl;

void LightEvent_Init(LightEvent *event, ResetType reset_type)
{
    EventImpl *e = calloc(1, sizeof(*e));
    if (e == NULL)
        abort();
    pthread_mutex_init(&e->mutex, NULL);
    pthread_cond_init(&e->cond, NULL);
    e->type = reset_type;
    event->state = 0;
    event->lock = 1;
    event->impl = e;
}

void LightEvent_Clear(LightEvent *event)
{
    EventImpl *e = event->impl;
    pthread_mutex_lock(&e->mutex);
    e->signalled = 0;
    pthread_mutex_unlock(&e->mutex);
}

void LightEvent_Signal(LightEvent *event)
{
    EventImpl *e = event->impl;
    pthread_mutex_lock(&e->mutex);
    if (e->type == RESET_PULSE)
    {
        e->pulse++;
        pthread_cond_broadcast(&e->cond);
    }
    else
    {
        e->signalled = 1;
        if (e->type == RESET_ONESHOT)
            pthread_cond_signal(&e->cond);
        else
            pthread_cond_broadcast(&e->cond);
    }
    pthread_mutex_unlock(&e->mutex);
}

void LightEvent_Pulse(LightEvent *event)
{
    EventImpl *e = event->impl;
    pthread_mutex_lock(&e->mutex);
    e->pulse++;
    pthread_cond_broadcast(&e->cond);
    pthread_mutex_unlock(&e->mutex);
}

/* Called with the mutex held: consumes the signal when it is there. */
static int Consume(EventImpl *e)
{
    if (!e->signalled)
        return 0;
    if (e->type == RESET_ONESHOT)
        e->signalled = 0;
    return 1;
}

int LightEvent_TryWait(LightEvent *event)
{
    EventImpl *e = event->impl;
    int got;
    pthread_mutex_lock(&e->mutex);
    got = Consume(e);
    pthread_mutex_unlock(&e->mutex);
    return got;
}

void LightEvent_Wait(LightEvent *event)
{
    EventImpl *e = event->impl;
    unsigned pulse;
    pthread_mutex_lock(&e->mutex);
    pulse = e->pulse;
    while (!Consume(e) && e->pulse == pulse)
        pthread_cond_wait(&e->cond, &e->mutex);
    pthread_mutex_unlock(&e->mutex);
}

int LightEvent_WaitTimeout(LightEvent *event, s64 timeout_ns)
{
    EventImpl *e = event->impl;
    struct timespec deadline;
    unsigned pulse;
    int got = 0;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ns / 1000000000;
    deadline.tv_nsec += timeout_ns % 1000000000;
    if (deadline.tv_nsec >= 1000000000)
    {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }
    pthread_mutex_lock(&e->mutex);
    pulse = e->pulse;
    for (;;)
    {
        if (Consume(e) || e->pulse != pulse)
        {
            got = 1;
            break;
        }
        if (pthread_cond_timedwait(&e->cond, &e->mutex, &deadline) == ETIMEDOUT)
        {
            got = Consume(e);
            break;
        }
    }
    pthread_mutex_unlock(&e->mutex);
    /* libctru: 0 when signalled, 1 on timeout. */
    return got ? 0 : 1;
}

/* ── Threads ────────────────────────────────────────────────────────────── */

struct PcThread
{
    pthread_t handle;
    ThreadFunc entry;
    void *arg;
    bool detached;
    bool finished;
};

static void *ThreadMain(void *p)
{
    struct PcThread *t = p;
    t->entry(t->arg);
    __atomic_store_n(&t->finished, true, __ATOMIC_RELEASE);
    if (t->detached)
        free(t);
    return NULL;
}

Thread threadCreate(ThreadFunc entrypoint, void *arg, size_t stack_size, int prio, int core_id, bool detached)
{
    struct PcThread *t = calloc(1, sizeof(*t));
    pthread_attr_t attr;
    (void)prio;
    (void)core_id;
    if (t == NULL)
        return NULL;
    t->entry = entrypoint;
    t->arg = arg;
    t->detached = detached;
    pthread_attr_init(&attr);
    /* The console's stacks are small (8 KiB for the log writer): a PC gives
     * every thread at least 256 KiB. */
    pthread_attr_setstacksize(&attr, stack_size < 256 * 1024 ? 256 * 1024 : stack_size);
    if (detached)
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t->handle, &attr, ThreadMain, t) != 0)
    {
        pthread_attr_destroy(&attr);
        free(t);
        return NULL;
    }
    pthread_attr_destroy(&attr);
    return t;
}

Result threadJoin(Thread thread, u64 timeout_ns)
{
    if (thread == NULL)
        return -1;
    if (timeout_ns != U64_MAX)
    {
        u64 waited = 0;
        while (!__atomic_load_n(&thread->finished, __ATOMIC_ACQUIRE))
        {
            if (waited >= timeout_ns)
                return -1;
            svcSleepThread(1000000);
            waited += 1000000;
        }
    }
    pthread_join(thread->handle, NULL);
    return 0;
}

void threadFree(Thread thread)
{
    free(thread);
}

void threadDetach(Thread thread)
{
    if (thread != NULL)
    {
        thread->detached = true;
        pthread_detach(thread->handle);
    }
}

Thread threadGetCurrent(void)
{
    return NULL;
}

/* ── Kernel calls ───────────────────────────────────────────────────────── */

u64 svcGetSystemTick(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    /* ns * 268111856 / 1e9 without overflowing 64 bits for decades. */
    return (u64)now.tv_sec * SYSCLOCK_ARM11 + (u64)now.tv_nsec * SYSCLOCK_ARM11 / 1000000000ULL;
}

void svcSleepThread(s64 ns)
{
    struct timespec t;
    if (ns <= 0)
    {
        sched_yield();
        return;
    }
    t.tv_sec = ns / 1000000000;
    t.tv_nsec = ns % 1000000000;
    while (nanosleep(&t, &t) != 0 && errno == EINTR)
        ;
}

Result svcGetThreadPriority(s32 *out, Handle handle)
{
    (void)handle;
    if (out)
        *out = 0x30;
    return 0;
}

Result svcOutputDebugString(const char *str, s32 length)
{
    /* The log's lines, as an emulator would show them: on the terminal. */
    fwrite(str, 1, (size_t)length, stderr);
    if (length > 0 && str[length - 1] != '\n')
        fputc('\n', stderr);
    return 0;
}

void svcBreak(u32 breakReason)
{
    fprintf(stderr, "svcBreak(%u)\n", (unsigned)breakReason);
    abort();
}

u32 osGetFirmVersion(void)
{
    return SYSTEM_VERSION(2, 0, 0);
}

u32 osGetKernelVersion(void)
{
    return SYSTEM_VERSION(2, 0, 0);
}

float osGet3DSliderState(void)
{
    return 0.0f;
}

void osSetSpeedupEnable(bool enable)
{
    (void)enable;
}

Result cfguInit(void)
{
    return 0;
}

void cfguExit(void)
{
}

Result CFGU_GetSystemModel(u8 *model)
{
    if (model)
        *model = CFG_MODEL_N3DSXL;
    return 0;
}

Result romfsInit(void)
{
    return 0;
}

Result romfsExit(void)
{
    return 0;
}

/* Linear and VRAM memory (allocation, free space): pc_port/src/gl/pc_c3d_mem.c. */

/* newlib's heap statistics (pc_port/compat/malloc.h). */
#include <malloc.h>
char *fake_heap_start, *fake_heap_end;

struct mallinfo mallinfo(void)
{
    struct mallinfo info;
    memset(&info, 0, sizeof(info));
    return info;
}

void *sbrk(ptrdiff_t increment)
{
    (void)increment;
    return (void *)-1;
}

/* ── APT ────────────────────────────────────────────────────────────────── */

static volatile int sExitRequested;
static aptHookCookie *sHooks;
static bool sHomeAllowed = true;

bool aptMainLoop(void)
{
    return !__atomic_load_n(&sExitRequested, __ATOMIC_ACQUIRE);
}

void PcApt_RequestExit(void)
{
    if (__atomic_exchange_n(&sExitRequested, 1, __ATOMIC_ACQ_REL))
        return;
    for (aptHookCookie *c = sHooks; c != NULL; c = c->next)
        if (c->callback)
            c->callback(APTHOOK_ONEXIT, c->param);
}

void aptHook(aptHookCookie *cookie, aptHookFn callback, void *param)
{
    cookie->callback = callback;
    cookie->param = param;
    cookie->next = sHooks;
    sHooks = cookie;
}

void aptUnhook(aptHookCookie *cookie)
{
    for (aptHookCookie **p = &sHooks; *p != NULL; p = &(*p)->next)
        if (*p == cookie)
        {
            *p = cookie->next;
            return;
        }
}

void aptSetHomeAllowed(bool allowed)
{
    sHomeAllowed = allowed;
}

bool aptIsHomeAllowed(void)
{
    return sHomeAllowed;
}

bool aptCheckHomePressRejected(void)
{
    return false;
}

Result APT_SetAppCpuTimeLimit(u32 percent)
{
    (void)percent;
    return 0;
}

Result APT_GetAppCpuTimeLimit(u32 *percent)
{
    if (percent)
        *percent = 80;
    return 0;
}

/* ── HID ────────────────────────────────────────────────────────────────── */

static LightLock sHidLock = 1;
static u32 sHidPending, sHidHeld, sHidPrevious;
static touchPosition sTouchPending, sTouch;
static circlePosition sCirclePending, sCircle;

void PcHid_SetState(u32 held, touchPosition touch, circlePosition circle)
{
    LightLock_Lock(&sHidLock);
    sHidPending = held;
    sTouchPending = touch;
    sCirclePending = circle;
    LightLock_Unlock(&sHidLock);
}

void hidScanInput(void)
{
    LightLock_Lock(&sHidLock);
    sHidPrevious = sHidHeld;
    sHidHeld = sHidPending;
    sTouch = sTouchPending;
    sCircle = sCirclePending;
    LightLock_Unlock(&sHidLock);
}

u32 hidKeysHeld(void)
{
    return sHidHeld;
}

u32 hidKeysDown(void)
{
    return sHidHeld & ~sHidPrevious;
}

u32 hidKeysUp(void)
{
    return ~sHidHeld & sHidPrevious;
}

void hidTouchRead(touchPosition *pos)
{
    if (pos)
        *pos = sTouch;
}

void hidCircleRead(circlePosition *pos)
{
    if (pos)
        *pos = sCircle;
}

/* ── gfx / console ──────────────────────────────────────────────────────── *
 * The screens' framebuffers and formats belong to the display
 * (pc_port/src/gl/pc_display.c); what is left here are no-ops. */

void gfxInit(GSPGPU_FramebufferFormat topFormat, GSPGPU_FramebufferFormat bottomFormat, bool vrambuffers)
{
    (void)topFormat;
    (void)bottomFormat;
    (void)vrambuffers;
}

void gfxInitDefault(void)
{
}

void gfxExit(void)
{
}

void gfxSetDoubleBuffering(gfxScreen_t screen, bool enable)
{
    (void)screen;
    (void)enable;
}

void gfxFlushBuffers(void)
{
}

void gfxSwapBuffers(void)
{
}

PrintConsole *consoleInit(gfxScreen_t screen, PrintConsole *console)
{
    static PrintConsole sDefault;
    (void)screen;
    if (console == NULL)
        console = &sDefault;
    console->consoleWidth = 40;
    console->consoleHeight = 30;
    return console;
}

PrintConsole *consoleSelect(PrintConsole *console)
{
    return console;
}

void consoleSetWindow(PrintConsole *console, int x, int y, int width, int height)
{
    (void)console;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

void consoleClear(void)
{
}

/* newlib's device table (pc_port/compat/sys/iosupport.h). */
#include <sys/iosupport.h>
const devoptab_t *devoptab_list[STD_MAX];
