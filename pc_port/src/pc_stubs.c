/*
 * Stubs: the native units that are not on the PC yet.
 *
 *   GPU memory accounting     -> nothing to account on a PC
 *   local wireless (UDS)      -> no PC equivalent in v1: always absent
 *
 * Each stub keeps the contract its header states (valid pointers, sane
 * values). Video, the bottom screen and the Pokédex's top screen are the
 * console's own units over the citro shim (pc_port/src/gl); the voxel
 * renderer is the console's ctr_voxel.c on the shim (J5). Audio is the
 * console's 3ds_audio.c over NDSP on SDL3 (pc_ndsp.c).
 */
#include <3ds.h>

#include <stdlib.h>
#include <string.h>

#include "3ds_gpumem.h"
#include "3ds_local.h"
#include "3ds_log.h"
#include "3ds_platform.h"

/* ── GPU memory accounting (3ds_gpumem.c) ───────────────────────────────── */

void CtrGpuMem_Report(const char *why) { (void)why; }

/* ── Local wireless: absent on a PC (v1) ────────────────────────────────── */

uint8_t LocalLink_Enabled(void) { return 0; }
uint32_t LocalLink_Generation(void) { return 0; }
int LocalLink_GetPeers(struct LocalPeer *out, int max) { (void)out; (void)max; return 0; }
uint8_t LocalLink_GetSession(void) { return 0; }
uint8_t LocalLink_GetSessionKind(void) { return 0; }
void LocalLink_LeaveSession(void) {}
uint16_t LocalLink_MyId(void) { return 0; }
uint8_t LocalLink_PollEvent(struct LocalEvent *ev) { (void)ev; return 0; }
void LocalLink_Send(uint16_t dest, const void *data, uint32_t len) { (void)dest; (void)data; (void)len; }
void LocalLink_SetAdvert(uint8_t hosting, uint8_t nextSlot, const uint8_t *data) { (void)hosting; (void)nextSlot; (void)data; }
void LocalLink_StartSession(uint8_t kind) { (void)kind; }
uint8_t LocalLink_InSession(void) { return 0; }
void LocalLink_Init(void) {}
void LocalLink_Shutdown(void) {}
void LocalLink_AfterWake(void) {}
void LocalLink_LeaveBeforeSleep(void) {}

/* ── The hardware cycle test (3ds_cycle_test.c is not built) ────────────── */

void CtrCycle_Frame(void) {}
void CtrCycle_Input(CtrInputSample *sample) { (void)sample; }

/* ── Mystery Gift ───────────────────────────────────────────────────────── *
 * full.mk leaves mystery_gift_menu.c out; on the console --gc-sections drops
 * the code that names it (union_room.c, mystery_gift_view.c), which nothing
 * reaches. The PE link keeps those sections, so the names need a body: one
 * that stops loudly, since reaching it would be a difference from the 3DS. */
#define PC_UNREACHABLE(name) CtrPlatform_Fatal("unreachable on the 3DS port: " name)
unsigned char gGiftIsFromEReader;
signed char DoMysteryGiftYesNo(uint8_t *textState, uint16_t *windowId, unsigned char placement, const uint8_t *str)
{
    (void)textState; (void)windowId; (void)placement; (void)str;
    PC_UNREACHABLE("DoMysteryGiftYesNo");
}
uint16_t GetMysteryGiftBaseBlock(void) { PC_UNREACHABLE("GetMysteryGiftBaseBlock"); }
void MG_AddMessageTextPrinter(const uint8_t *str) { (void)str; PC_UNREACHABLE("MG_AddMessageTextPrinter"); }
void MG_DrawCheckerboardPattern(uint32_t bg) { (void)bg; PC_UNREACHABLE("MG_DrawCheckerboardPattern"); }
void MG_DrawTextBorder(uint8_t windowId) { (void)windowId; PC_UNREACHABLE("MG_DrawTextBorder"); }
uint32_t PrintMysteryGiftMenuMessage(uint8_t *textState, const uint8_t *str)
{
    (void)textState; (void)str;
    PC_UNREACHABLE("PrintMysteryGiftMenuMessage");
}
void PrintMysteryGiftOrEReaderHeader(unsigned char isEReader, uint32_t useCancel)
{
    (void)isEReader; (void)useCancel;
    PC_UNREACHABLE("PrintMysteryGiftOrEReaderHeader");
}
