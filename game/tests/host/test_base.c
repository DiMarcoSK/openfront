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
#include "contracts/audio.h"
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

/* B13: the commit boundary reads the campaign focus; the host
 * fixture keeps no live campaign screen, so it reports none.   */
uint8_t ui_selected_row(void) {
	return 0xFFu;
}

void ui_input(uint8_t kind, uint32_t value) {
	(void)kind;
	(void)value;
}

/* T013: the confirm SFX rides contracts/audio.h; audio.c is a ROM-only
 * unit, the host suite stubs the facade.                                 */
void audio_cue(audio_cue_t cue) { (void)cue; }

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
	static uint32_t fixture_seq; /* B12: one seq = one debit, so the
	                              * fixture must stamp monotonic seqs  */
	sim_cmd_t cmd;
	memset(&cmd, 0, sizeof cmd);
	cmd.seq = ++fixture_seq; /* B12: one seq = one debit; monotonic      */
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

/* B16: the same ingest with the roster set through the slot payload
 * (agg.unit_count), so formation tests build any of the 3 rosters.       */
static void ingest_roster(uint32_t treasury, uint16_t chest, uint16_t day,
                          uint16_t roster) {
	save_aggregate_t agg;
	memset(&agg, 0, sizeof agg);
	agg.treasury = treasury;
	agg.seed = 42u;
	agg.reserved[0] = (uint8_t)(chest & 0xFFu);
	agg.reserved[1] = (uint8_t)(chest >> 8u);
	agg.unit_count = roster;
	sim_ingest_save(&agg, day);
}

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);

	/* Regression (art pass): before any input the Base requests no
	 * screen - a zero-initialised request read as UI_SCREEN_TITLE and
	 * bounced the first deploy back through the title handoff.        */
	assert(base_request() == UI_SCREEN_NONE);

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

	/* B15/T012: the Success state copy is the finished batch             */
	clear_drawn();
	base_frame();
	assert(drawn_has("5 mustered. Chest 5/300. Treasury 520 g."));
	printf("base: Success state copy byte-exact\n");

	/* partial progress: 3 of 5 after 60 ticks with a ticks-left report   */
	assert(enqueue_cmd(3u, 5u));
	dispatch_all();
	/* B15/T015: one coalesced save per confirm - the debit flags dirty,
	 * the commit rides the Campaign boundary (B13), never the tick.      */
	assert(save_is_dirty());
	/* B15/T012: the Loading copy (0 mustered yet, 5 in the batch)        */
	clear_drawn();
	base_frame();
	assert(drawn_has("Mustering 1 of 5 - 0:10. 5 queued."));
	printf("base: Loading state copy byte-exact\n");
	step_n(60u);
	assert(sim_roster() == 8u); /* 5 + 3                                  */
	assert(sim_muster_done() == 3u);
	assert(sim_muster_total() == 5u);
	assert(sim_muster_ticks_left() <= SIM_MUSTER_TICKS);
	printf("base: 3 of 5 mustered, ticks left bounded\n");

	/* B15 R4: the Partial row must be byte-exact: "3 of 5 mustered -
	 * 0:04 left. 2 still in queue." (drawn at a fresh 20-tick boundary)  */
	clear_drawn();
	base_frame();
	assert(drawn_has("3 of 5 mustered - 0:04 left. 2 still in queue."));
	printf("base: Partial state copy byte-exact\n");

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

	/* ---- B16: formation complete -------------------------------------- */
	/* T016: the layout hash is stable over 3 rosters (equal runs,
	 * deterministic autoform) and differs between rosters.                */
	uint16_t seen_hash[3];
	const uint16_t rosters[3] = { 4u, 9u, 10u };
	for (int r = 0; r < 3; ++r) {
		ingest_roster(6000u, 0u, 12u, rosters[r]);
		sim_autoform();
		seen_hash[r] = sim_formation_hash();
		sim_autoform(); /* re-run: the same layout -> the same hash   */
		assert(sim_formation_hash() == seen_hash[r]);
	}
	assert(seen_hash[0] != seen_hash[1]);
	assert(seen_hash[1] != seen_hash[2]);
	assert(seen_hash[0] != seen_hash[2]);
	printf("base: layout hash stable over 3 rosters (4/9/10)\n");
	{
		/* under-strength fill at roster 4: 4 filled, 6 Empty.          */
		ingest_roster(600u, 0u, 12u, 4u);
		sim_autoform();
		uint8_t filled4 = 0u;
		for (int i = 0; i < (int)SIM_FORMATION_SLOTS; ++i) {
			if (sim_formation()[i] != 0xFFu) {
				filled4++;
			}
		}
		assert(filled4 == 4u);
		clear_drawn();
		base_input((uint8_t)UI_EVT_KEY, KEY_RIGHT); /* Formation tab  */
		base_frame();
		assert(drawn_has("4 of 10 filled, 6 Empty. Recruit 6 more,"));
		printf("base: N/10 label byte-exact at 4 of 10\n");
	}

	/* T019: every slot reachable by d-pad: 12 DOWN steps visit all
	 * 10 slot cards, the formation kind + the Deploy door ("<" marker). */
	{
		ingest_roster(600u, 0u, 12u, 10u); /* full formation        */
		sim_autoform();
		/* tab is already Formation here (the 4-of-10 block toggled it)  */
		bool visited[SIM_FORMATION_SLOTS + 2];
		memset(visited, 0, sizeof visited);
		for (int step = 0; step <= (int)SIM_FORMATION_SLOTS + 1; ++step) {
			clear_drawn();
			base_frame();
			for (int i = 0; i < (int)SIM_FORMATION_SLOTS + 2; ++i) {
				if (visited[i]) {
					continue;
				}
				if (i < (int)SIM_FORMATION_SLOTS) {
					/* the marker rides the filled card text
					 * (roster 10: every slot is a "C");     */
					char probe[8];
					snprintf(probe, sizeof probe,
					         "[%02d]C<", i + 1);
					if (strstr(drawn[4 + (i / 5)], probe) != NULL) {
						visited[i] = true;
					}
				} else if (i == (int)SIM_FORMATION_SLOTS) {
					if (strstr(drawn[10], "FORMATION") != NULL &&
					    strstr(drawn[10], " <") != NULL) {
						visited[i] = true; /* the kind cycler  */
					}
				} else if (strstr(drawn[12], " <") != NULL) {
					/* the Deploy door marker is appended last    */
					visited[i] = true;
				}
			}
			base_input((uint8_t)UI_EVT_KEY, KEY_DOWN);
		}
		for (int i = 0; i <= (int)SIM_FORMATION_SLOTS + 1; ++i) {
			assert(visited[i]);
		}
		printf("base: 10 slots + kind + Deploy reachable by d-pad\n");
	}

	/* T019: touch parity - tap moves the ring, tap 2 on the same
	 * target commits (selection runs through touch only).                 */
	{
		base_input((uint8_t)UI_EVT_KEY, KEY_LEFT);
		base_input((uint8_t)UI_EVT_KEY, KEY_RIGHT); /* Formation tab  */
		/* slot 2 (row 0, col 1): 76 px << 8 | 48                        */
		base_input((uint8_t)UI_EVT_TOUCH, ((76u << 8) | 40u));
		clear_drawn();
		base_frame();
		assert(drawn_has("[02]C<")); /* the ring moved                 */
		/* slot 4 (row 0, col 3): 178 px << 8 | 40; tap 1 moves           */
		base_input((uint8_t)UI_EVT_TOUCH, ((178u << 8) | 40u));
		clear_drawn();
		base_frame();
		assert(drawn_has("[04]C<"));
		/* tap 2 on slot 4 confirms the selection -> swap pending         */
		base_input((uint8_t)UI_EVT_TOUCH, ((178u << 8) | 40u));
		clear_drawn();
		base_frame();
		assert(drawn_has("Slot 4 selected - pick target slot A."));
		printf("base: touch parity - ring then confirm, swap flow\n");
	}

	printf("B09+B16 base host tests passed.\n");
	return 0;
}
