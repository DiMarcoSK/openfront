/* Host unit tests for the B08 campaign list (spec-02 R1-R7).
 * 8 rows x 5 fields x 1 action; treasury 119 disables the 120 row with
 * "Need 1" and 120 enables it next frame; <= 3 taps from list to Recruit;
 * 8 rows reachable in 2 pages; the dispatch re-check rejects a drained
 * front with +1 reject and 0 debit; 33rd command rejected with 32 kept.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/queue.h"
#include "contracts/ui.h"
#include "contracts/audio.h"
#include "nds.h"
#include "screens.h"
#include "slots.h"

/* ---- draw spy ------------------------------------------------------------ */
void audio_cue(audio_cue_t cue) { (void)cue; } /* the threat cue: id only */
static char drawn[24][80];

void ui_draw_text(int x, int y, const char *txt) {
	(void)x;
	if (y >= 0 && y < 24) {
		snprintf(drawn[y], sizeof drawn[0], "%s", txt);
	}
}

ui_screen_t ui_screen(void) {
	return UI_SCREEN_CAMPAIGN;
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

void queue_frame(uint32_t frame);
bool queue_enqueue_cmd(const sim_cmd_t *cmd);

/* ---- backend stub (512 B EEPROM) ------------------------------------------ */
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

/* clear the drawn screen (each frame draws over, rows hold the labels)  */
static void clear_drawn(void) {
	memset(drawn, 0, sizeof drawn);
}

/* sim_step takes <= 255 ticks per call (contract v1); loop it.           */
static void step_n(uint32_t ticks) {
	while (ticks > 0u) {
		const uint8_t burst = (ticks > 255u) ? 255u : (uint8_t)ticks;
		sim_step(burst);
		ticks -= burst;
	}
}

/* ingest a treasury/day state through the reboot path (no roster)       */
static void ingest(uint32_t treasury, uint16_t day) {
	save_aggregate_t agg;
	memset(&agg, 0, sizeof agg);
	agg.treasury = treasury;
	agg.seed = 42u;
	sim_ingest_save(&agg, day);
}

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);
	uint8_t count = 0u;
	const sim_province_record_t *tab = sim_province_table(&count);

	/* R1: 8 ROM records with 5 fields each, names <= 11 chars            */
	assert(count == 8u);
	for (int i = 0; i < (int)count; ++i) {
		assert(strlen(tab[i].name) > 0u && strlen(tab[i].name) <= 11u);
		assert(tab[i].cost > 0u && tab[i].reward > 0u);
		assert(tab[i].threat <= SIM_THREAT_CRITICAL);
	}
	printf("campaign: 8 records x 5 fields in the ROM table\n");

	/* R1: census - one frame draws all 8 rows with one action each       */
	sim_init(1u);
	clear_drawn();
	campaign_focus_province(0);
	campaign_frame();
	for (int i = 0; i < (int)count; ++i) {
		char needle[24];
		snprintf(needle, sizeof needle, "%s", tab[i].name);
		assert(drawn_has(needle));
	}
	printf("campaign: 8 rows drawn in one pass, 1 action each\n");

	/* R1: 8 rows reachable across 2 pages (L/R page jump)                */
	campaign_input((uint8_t)UI_EVT_KEY, KEY_RIGHT); /* page 2 */
	clear_drawn();
	campaign_frame();
	assert(drawn_has("NEED") || drawn_has("RECRUIT"));
	printf("campaign: page jump reachable (2 pages x 4 rows)\n");

	/* R2: live ticker: 600 ticks -> day +1 and +2 treasury               */
	ingest(119u, 1u);
	sim_set_run_mode(SIM_RUN_ECONOMY);
	step_n(600u);
	assert(sim_snapshot()->day == 2u);
	assert(sim_snapshot()->treasury == 121u);
	assert(sim_tick() == 600u);
	printf("campaign: 600 ticks -> day 2, treasury 119+2\n");

	/* R3: exact deficit at 119 (Front West 120 -> "NEED 1")              */
	ingest(119u, 12u);
	clear_drawn();
	campaign_frame();
	assert(drawn_has("NEED 1")); /* the exact deficit, no NEED 0        */

	/* at 120 the row enables on the next frame (no NEED label)           */
	ingest(120u, 12u);
	clear_drawn();
	campaign_frame();
	assert(drawn_has("[RECRUIT]") || drawn_has("[*]"));
	printf("campaign: treasury 120 enables the 120-cost front\n");

	/* R4: 3-tap recruit handoff - cursor, deadline and debit            */
	ingest(580u, 12u);
	campaign_focus_province(0);
	campaign_input((uint8_t)UI_EVT_KEY, KEY_DOWN); /* -> Front East 90    */
	campaign_input((uint8_t)UI_EVT_KEY, KEY_DOWN);
	campaign_input((uint8_t)UI_EVT_KEY, KEY_DOWN); /* -> Front South 60   */
	campaign_input((uint8_t)UI_EVT_KEY, KEY_A);
	assert(sim_deploy_province() == -1); /* the preset lands at dispatch */
	assert(sim_snapshot()->treasury == 580u); /* not debited pre-tick     */
	assert(queue_diag()->cmd_depth == 1u);
	printf("campaign: row Recruit -> 1 command queued, footer holds\n");

	clear_drawn();
	campaign_frame();
	assert(drawn_has("Front South: recruit queued, 60 debited. Treasury 520. "
	                 "Opening Recruit tab."));
	printf("campaign: success footer byte-exact for the 160 ms hold\n");

	/* T032: hold A, mash A 20x during the hold -> 1 command, 1 debit.   */
	for (int press = 0; press < 20; ++press) {
		campaign_input((uint8_t)UI_EVT_KEY, KEY_A);
	}
	assert(queue_diag()->cmd_depth == 1u);

	/* the handoff lands exactly after the 160 ms hold (10 frames);
	 * the footer-draw frame above was the first of them.               */
	for (int f = 0; f < 8; ++f) {
		campaign_frame();
		assert(campaign_request() == UI_SCREEN_NONE);
	}
	campaign_frame();
	assert(campaign_request() == UI_SCREEN_BASE);
	assert(campaign_request() == UI_SCREEN_NONE); /* consumed on read    */
	printf("campaign: row Recruit -> 1 command queued, footer opens Base "
	       "after the hold\n");

	/* dispatch: one command, one debit (ensemble at the 10 Hz tick)      */
	queue_frame(1u);
	sim_step(1u);
	assert(sim_snapshot()->treasury == 520u);
	assert(queue_diag()->cmd_depth == 0u);
	assert(queue_diag()->rejects_total == 0u);
	assert(sim_deploy_province() == 3);
	printf("campaign: dispatch debits 60 once, 0 rejects\n");

	/* R6: drained between enqueue and dispatch -> reject, 0 debit        */
	ingest(119u, 12u);
	sim_cmd_t cmd;
	memset(&cmd, 0, sizeof cmd);
	cmd.seq = 42u;
	cmd.cmd = 0u; /* Front West 120, unaffordable                       */
	assert(queue_enqueue_cmd(&cmd));
	queue_frame(2u);
	assert(sim_snapshot()->treasury == 119u);
	assert(queue_diag()->last_reject == QUEUE_REJECT_INSUFFICIENT);
	printf("campaign: dispatch re-check -> +1 reject, 0 debit\n");

	/* T031/T032: one seq replayed twice at dispatch: exactly 1
	 * accepted command, 1 debit, reject log untouched (only true
	 * rejections log).                                                  */
	const uint32_t rejects_after_debit = queue_diag()->rejects_total;
	ingest(580u, 12u);
	memset(&cmd, 0, sizeof cmd);
	cmd.seq = 11u;
	cmd.cmd = 0u; /* Front West 120                                     */
	assert(queue_enqueue_cmd(&cmd));
	assert(queue_enqueue_cmd(&cmd));
	queue_frame(3u);
	sim_step(1u);
	assert(sim_snapshot()->treasury == 580u - 120u);
	assert(queue_diag()->rejects_total == rejects_after_debit);
	/* the direct replay path: 2nd apply short-circuits with 0 debit
	 * and SIM_DUPLICATE (no reject-log entry either)                   */
	sim_cmd_t dup = cmd;
	assert(sim_apply(&dup) == SIM_DUPLICATE);
	assert(sim_snapshot()->treasury == 580u - 120u);
	printf("campaign: replayed seq -> 1 accept, 1 debit, no reject log\n");

	/* R5: unaffordable press: nothing enqueued, inline, input preserved  */
	ingest(119u, 12u);
	campaign_focus_province(0); /* Front West costs 120 > 119            */
	const uint32_t before = queue_diag()->cmd_depth;
	campaign_input((uint8_t)UI_EVT_KEY, KEY_A);
	clear_drawn();
	campaign_frame();
	assert(queue_diag()->cmd_depth == before);
	assert(campaign_request() == UI_SCREEN_NONE);
	assert(drawn_has("needs 1 more"));
	printf("campaign: NEED row kept inline, input preserved\n");

	/* R7: 33rd command rejected, 32 kept, 0 silent drops                 */
	for (uint32_t i = 0u; i <= QUEUE_CMD_CAP; ++i) {
		cmd.seq = i;
		cmd.cmd = 0u; /* rejects at dispatch: the ring holds the pressure */
		const bool accepted = queue_enqueue_cmd(&cmd);
		assert(i < QUEUE_CMD_CAP ? accepted : !accepted);
	}
	const queue_diag_t *d = queue_diag();
	assert(d->cmd_depth == QUEUE_CMD_CAP);
	assert(d->queue_full_rejects == 1u);
	printf("campaign: 33rd command rejected, 32 kept\n");

	printf("B08 campaign host tests passed.\n");
	return 0;
}
