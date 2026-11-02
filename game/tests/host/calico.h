/* Host-side stdlib shim for the contract-driven unit tests.
 * Objects compiled with `-include tests/host/nds_stub.h` build without
 * devkitARM: this file defines the primitives the modules touch.
 */
#ifndef WAR_TABLE_NDS_STUB_H
#define WAR_TABLE_NDS_STUB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>

/* Input state machinery is emulated by the test driver. */
typedef struct { unsigned int px, py; } touchPosition;

static inline void consoleDemoInit(void) {}
static inline void swiWaitForVBlank(void) {}
static inline unsigned int keysDown(void) { return 0u; }
#define KEY_TOUCH 0x0200u
static inline unsigned int keysHeld(void) { return 0u; }
static inline void touchRead(touchPosition *tp) { tp->px = tp->py = 0u; }
static inline bool pmMainLoop(void) { return false; }
static inline void setBrightness(int a, int b) { (void)a; (void)b; }

#define KEY_START  0x0001u

/* libnds/free lib stylings used by save/render stubs. */
typedef enum { VRAM_A_MAIN_BG, VRAM_C_SUB_BG } VRAM_A_TYPE;
static inline void vramSetBankA(int a) { (void)a; }
static inline void vramSetBankC(int c) { (void)c; }
static inline void iprintf(const char *fmt, ...) { (void)fmt; }

#endif
