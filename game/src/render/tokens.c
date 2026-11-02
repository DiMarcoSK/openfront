/* tokens.c - the NDS palette compiled from include/contracts/tokens.h.
 * Only the VRAM-facing 565 constants live here. The hex string stays
 * banned outside tokens.h (B02 palette gate, constitution 1).
 */
#include <nds.h>
#include <calico.h>

#include "contracts/tokens.h"

/* Calico exposes palette banks as raw u16 windows; libnds' BG_PALETTE
 * macros are not part of the v2 header set (nds.h -> calico.h).          */
#define WAR_TABLE_PALRAM        ((volatile u16 *)0x05000000u)
#define WAR_TABLE_PALRAM_SUB    ((volatile u16 *)0x05000400u)

void tokens_load_main(void) {
	WAR_TABLE_PALRAM[0] = TOKEN_COLOR_PAPER;
	WAR_TABLE_PALRAM[1] = TOKEN_COLOR_SURFACE;
	WAR_TABLE_PALRAM[2] = TOKEN_COLOR_INK;
	WAR_TABLE_PALRAM[3] = TOKEN_COLOR_INK_2;
	WAR_TABLE_PALRAM[4] = TOKEN_COLOR_LINE;
	WAR_TABLE_PALRAM[5] = TOKEN_COLOR_ACCENT;
	WAR_TABLE_PALRAM[6] = TOKEN_COLOR_ON_ACCENT;
	WAR_TABLE_PALRAM[7] = TOKEN_COLOR_CRIT;
}

void tokens_load_sub(void) {
	WAR_TABLE_PALRAM_SUB[0] = TOKEN_COLOR_PAPER;
	WAR_TABLE_PALRAM_SUB[1] = TOKEN_COLOR_SURFACE;
	WAR_TABLE_PALRAM_SUB[2] = TOKEN_COLOR_INK;
	WAR_TABLE_PALRAM_SUB[3] = TOKEN_COLOR_INK_2;
	WAR_TABLE_PALRAM_SUB[4] = TOKEN_COLOR_LINE;
	WAR_TABLE_PALRAM_SUB[5] = TOKEN_COLOR_ACCENT;
	WAR_TABLE_PALRAM_SUB[6] = TOKEN_COLOR_ON_ACCENT;
	WAR_TABLE_PALRAM_SUB[7] = TOKEN_COLOR_CRIT;
}
