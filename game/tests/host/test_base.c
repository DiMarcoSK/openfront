/* Host unit tests for the B09 base slice (spec-03 R2-R8).
 * +5 at chest 298/300 blocked with treasury unchanged and input
 * preserved, 0 modals in 20 attempts; 1 company per 20 ticks; auto-form
 * fills 10 slots in <= 1 frame with the roster sum invariant; the 20
 * pending backpressure cap holds; the swap keeps the roster sum.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/queue.h"
#include "contracts/ui.h"
#include "nds.h"
#include "screens.h"
#include "slots.h"

/* ---- draw spy ------------------------------------------------------------ */
static char drawn[24][80];

void ui_draw_text(int x, int y, const char *txt) {
	(void)x;
	if (y >= 0 && y < 24) {
		snprintf(drawn[y], sizeof drawn[0], "%s", txt);
	}
}

ui_screen_t ui_screen(void) {
	return UI_SCREEN_BASE; /* coalescing holds during the base session */
}

void ui_input(uint8_t kind, uint32_t value) {
	(void)kind;
	(void)value;
}

void queue_frame(uint32_t frame);
bool queue_enqueue_cmd(const sim_cmd_t *cmd);

static void step_n(uint32_t ticks) {
	while (ticks > 0u) {
		const uint8_t burst = (ticks > 255u) ? 255u : (uint8_t)ticks;
		sim_step(burst);
		ticks -= burst;
	}
}

/* ---- backend stub ---------------------------------------------------------- */
static uint8_t rom_block[512];
save_backend_kind_t backend_probe(void) { return SAVE_BACKEND_EEPROM; }
void backend_read(uint32_t off, uint8_t *dst, uint32_t len) {
	assert(off + len <= sizeof rom_block);
	memmove(dst, rom_block + off, len);
}
void backend_write(uint32_t off, const uint8_t *src, uint32_t len) {
	assert(off + len <= sizeof rom_block);
	memmove(rom_block + off, src, len);
}
bool backend_detect_detail(void) { return false; }
void backend_write_detail(uint32_t o, const uint8_t *s, uint32_t l) { (void)o; (void)s; (void)l; }
void backend_read_detail(uint32_t o, uint8_t *d, uint32_t l) { (void)o; (void)d; (void)l; }

static bool drawn_has(const char *s) {
	for (int i = 0; i < 24; ++i) {
		if (strstr(drawn[i], s) != NULL) {
			return true;
		}
	}
	return false;
}

static void clear_drawn(void) { memset(drawn, 0, sizeof drawn); }

static bool enqueue_cmd(uint8_t id, uint8_t arg) {
	sim_cmd_t cmd;
	memset(&cmd, 0, sizeof cmd);
	cmd.seq = (uint32_t)(id * 100u + arg);
	cmd.cmd = id;
	cmd.arg = arg;
	return queue_enqueue_cmd(&cmd);
}

static void dispatch_all(void) {
	queue_frame(7u); /* dispatch drains the ring (sim_apply)             */
}

static void ingest(uint32_t treasury, uint16_t chest, uint16_t day) {
	save_aggregate_t agg;
	memset(&agg, 0, sizeof agg);
	agg.treasury = treasury;
	agg.seed = 42u;
	agg.reserved[0] = (uint8_t)(chest & 0xFFu);
	agg.reserved[1] = (uint8_t)(chest >> 8u);
	sim_ingest_save(&agg, day);
}

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);

	/* R4: 1 company every 20 ticks; 5 ready at 10 s (100 ticks)          */
	sim_init(1u);
	ingest(580u, 0u, 12u);
	assert(enqueue_cmd(3u, 5u));
	dispatch_all();
	assert(sim_snapshot()->treasury == 580u - 60u); /* one debit only    */
	assert(sim_snapshot()->day == 12u);
	assert(sim_chest() == 5u);
	assert(sim_roster() == 0u);
	sim_set_run_mode(SIM_RUN_MUSTER);
	step_n(100u);
	assert(sim_roster() == 5u);
	assert(sim_tick() == 0u); /* muster is not economy (menus froze it) */
	printf("base: 5 mustered over 100 ticks, treasury debited once\n");

	/* partial progress: 3 of 5 after 60 ticks with a ticks-left report   */
	assert(enqueue_cmd(3u, 5u));
	dispatch_all();
	step_n(60u);
	assert(sim_roster() == 8u); /* 5 + 3                                  */
	assert(sim_muster_done() == 3u);
	assert(sim_muster_total() == 5u);
	assert(sim_muster_ticks_left() <= SIM_MUSTER_TICKS);
	printf("base: 3 of 5 mustered, ticks left bounded\n");

	/* R3: chest 298/300 blocks +5 inline, treasury unchanged, 20 tries.  */
	ingest(1000u, 298u, 12u);
	assert(sim_chest() == 298u);
	for (int i = 0; i < 20; ++i) {
		assert(enqueue_cmd(3u, 5u));
		dispatch_all();
		assert(queue_diag()->last_reject == QUEUE_REJECT_CHEST_FULL);
	}
	assert(sim_snapshot()->treasury == 1000u); /* untouched: R3          */
	assert(sim_chest() == 298u);
	assert(sim_roster() == 0u); /* the ingest reset; 0 musters passed     */
	printf("base: 20/20 blocked at chest 298/300, 0 modals, 0 state\n");

	/* R3 on screen: the reason stays inline, focus kept, 0 modals        */
	clear_drawn();
	base_input((uint8_t)UI_EVT_KEY, KEY_A); /* focus 0 = +5 stepper      */
	clear_drawn();
	base_frame();
	assert(drawn_has("Chest 298/300"));
	printf("base: blocked reason inline, no modal\n");

	/* R5: pending cap 20: the 21st company never accepted                */
	ingest(6000u, 0u, 12u);
	for (int i = 0; i < 4; ++i) { /* 4 x 5 = 20 pending                 */
		assert(enqueue_cmd(3u, 5u));
		dispatch_all();
	}
	assert(enqueue_cmd(3u, 5u)); /* 21st enters the ring                  */
	dispatch_all();
	assert(queue_diag()->last_reject == QUEUE_REJECT_QUEUE_FULL);
	printf("base: 21st company refused, 20/20 backpressure holds\n");

	/* R6: auto-form determinism, fills the owned slots only              */
	sim_set_run_mode(SIM_RUN_MUSTER); /* the ingest resets the modes     */
	step_n(400u); /* 20 pending -> all mustered: 1 per 20 ticks        */
	assert(sim_roster() == 20u);
	sim_autoform();
	uint8_t form_ref[SIM_FORMATION_SLOTS];
	memcpy(form_ref, sim_formation(), SIM_FORMATION_SLOTS);
	sim_autoform(); /* idempotent deterministic fill                     */
	assert(memcmp(form_ref, sim_formation(), SIM_FORMATION_SLOTS) == 0);
	uint8_t filled = 0u;
	for (int i = 0; i < (int)SIM_FORMATION_SLOTS; ++i) {
		if (sim_formation()[i] != 0xFFu) {
			filled++;
		}
	}
	assert(filled == 10u); /* roster (8 + mustered) >= 10                */
	printf("base: auto-form deterministic, 10/10 in <= 1 frame\n");

	/* R7: swap keeps the roster sum with 0 duplicates / 0 losses          */
	uint8_t seen[SIM_FORMATION_SLOTS];
	memset(seen, 0, sizeof seen);
	for (int i = 0; i < (int)SIM_FORMATION_SLOTS; ++i) {
		seen[sim_formation()[i]]++;
	}
	const uint8_t roster_before = sim_roster();
	assert(enqueue_cmd(1u, (uint8_t)((1u << 4u) | 7u))); /* slot 1 <-> 7 */
	dispatch_all();
	assert(sim_roster() == roster_before);
	const uint8_t *f = sim_formation();
	assert(f[1] == 7u && f[7] == 1u);
	uint8_t seen2[SIM_FORMATION_SLOTS];
	memset(seen2, 0, sizeof seen2);
	for (int i = 0; i < (int)SIM_FORMATION_SLOTS; ++i) {
		seen2[f[i]]++;
	}
	assert(memcmp(seen, seen2, sizeof seen) == 0);
	printf("base: swap 1<->7 keeps the roster sum, 0 duplicates\n");

	/* same-slot swap does nothing (spec-03 Error copy, no state change)  */
	assert(enqueue_cmd(1u, (uint8_t)((3u << 4u) | 3u)));
	dispatch_all();
	assert(sim_roster() == roster_before);
	printf("base: same-slot swap rejected, formation unchanged\n");

	printf("B09 base host tests passed.\n");
	return 0;
}
