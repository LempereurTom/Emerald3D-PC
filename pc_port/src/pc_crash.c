/*
 * A crash report (Windows): when the process faults, wherever, write
 * crash.txt in the user folder (EMERALD_PC_HOME, else ./userdata) and on
 * stderr - the fault, the thread, the registers, and every return address
 * found on that thread's stack, to be resolved against the executable's
 * debug information:
 *   addr2line -f -i -e emerald-pc.exe <addresses>
 * (no ASLR, --disable-dynamicbase: the addresses are the link's).
 *
 * The stack is scanned rather than walked: the game is built without frame
 * pointers, so every word on the stack that points into the code is listed,
 * the true return addresses among a few stale ones. Nothing here allocates
 * or takes a lock: the faulting thread may hold the heap's or the log's.
 */
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_crash.h"

static DWORD sMainThread;
static char sPath[1024];
static uintptr_t sCodeStart, sCodeEnd;

static void Put(HANDLE out, const char *text)
{
    DWORD written;
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (out != INVALID_HANDLE_VALUE)
        WriteFile(out, text, (DWORD)strlen(text), &written, NULL);
    if (err != NULL && err != INVALID_HANDLE_VALUE)
        WriteFile(err, text, (DWORD)strlen(text), &written, NULL);
}

static LONG WINAPI OnCrash(EXCEPTION_POINTERS *info)
{
    static volatile LONG sOnce;
    char line[256];
    const EXCEPTION_RECORD *rec = info->ExceptionRecord;
    const CONTEXT *ctx = info->ContextRecord;
    HANDLE out;

    if (InterlockedExchange(&sOnce, 1) != 0)
        return EXCEPTION_CONTINUE_SEARCH;
    out = CreateFileA(sPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    Put(out, "Emerald PC crash report\n");
    snprintf(line, sizeof(line), "exception %08lx at %p, thread %lu%s\n", (unsigned long)rec->ExceptionCode,
             rec->ExceptionAddress, (unsigned long)GetCurrentThreadId(),
             GetCurrentThreadId() == sMainThread ? " (the game's)" : "");
    Put(out, line);
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
    {
        snprintf(line, sizeof(line), "%s of address %08lx\n",
                 rec->ExceptionInformation[0] == 0 ? "read" : rec->ExceptionInformation[0] == 1 ? "write" : "execute",
                 (unsigned long)rec->ExceptionInformation[1]);
        Put(out, line);
    }
#ifdef _M_IX86
    snprintf(line, sizeof(line), "eip=%08lx esp=%08lx ebp=%08lx eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx\n",
             ctx->Eip, ctx->Esp, ctx->Ebp, ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi);
    Put(out, line);
    /* The stack, from the fault up to its base. */
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    const uintptr_t *sp = (const uintptr_t *)(uintptr_t)ctx->Esp;
    const uintptr_t *base = (const uintptr_t *)tib->StackBase;
    unsigned found = 0;
    Put(out, "code addresses on the stack (innermost first):\n");
    snprintf(line, sizeof(line), "  %08lx\n", (unsigned long)ctx->Eip);
    Put(out, line);
    for (; sp < base && found < 96; ++sp)
    {
        uintptr_t v = *sp;
        if (v >= sCodeStart && v < sCodeEnd)
        {
            snprintf(line, sizeof(line), "  %08lx\n", (unsigned long)v);
            Put(out, line);
            ++found;
        }
    }
#else
    (void)ctx;
#endif
    snprintf(line, sizeof(line), "resolve with: addr2line -f -i -e emerald-pc.exe <addresses>\n");
    Put(out, line);
    if (out != INVALID_HANDLE_VALUE)
        CloseHandle(out);
    return EXCEPTION_CONTINUE_SEARCH; /* Windows ends the process as before */
}

void PcCrash_Install(void)
{
    const char *home = getenv("EMERALD_PC_HOME");
    HMODULE self = GetModuleHandleA(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)self;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)self + dos->e_lfanew);

    sMainThread = GetCurrentThreadId();
    sCodeStart = (uintptr_t)self + nt->OptionalHeader.BaseOfCode;
    sCodeEnd = sCodeStart + nt->OptionalHeader.SizeOfCode;
    snprintf(sPath, sizeof(sPath), "%s/crash.txt", home && *home ? home : "userdata");
    SetUnhandledExceptionFilter(OnCrash);
}
#else
#include "pc_crash.h"
void PcCrash_Install(void) {}
#endif
