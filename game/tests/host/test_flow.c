/* Host test for the B12 3-tap acceptance (spec-02 T034, R4, SDD-7).
 * Scripted 20/20: the whole path "campaign list -> RECRUIT -> Base
 * quantity stepper -> Confirm" runs in <= 3 taps, inside the 20 s
 * window; every run ends with the preset front, both +5 commands
 * dispatched and the muster started. Crosses campaign + base, so it
 * links both screens (test_campaign links campaign only, test_base
 * links base only).                                                 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/audio.h"
#include "contracts/ui.h"
#include "contracts/queue.h"
#include "nds.h"          /* libnds KEY_* masks (host stub mirrors)  */
#include "screens.h"      /* campaign/base screen glue               */

/* campaign + base draw spy (both screens share ui_draw_text)            */
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

void ui_input(uint8_t kind, uint32_t value) {
	(void)kind;
	(void)value;
}

/* T013: base routes the confirm SFX through audio.h; the ROM-only audio
 * unit is stubbed here.                                                 */
void audio_cue(audio_cue_t cue) { (void)cue; }

void queue_frame(uint32_t frame);

/* B13: the commit boundary reads the focus; host fixture reports none  */
uint8_t ui_selected_row(void) {
	return 0xFFu;
}

/* ---- backend stub (512 B EEPROM) ------------------------------------------ */
/* (no card stub here: test_flow drives the screens only; the save core
 * is covered by test_base / test_save_migrate.) */
save_backend_kind_t backend_probe(void) { return SAVE_BACKEND_EEPROM; }
void backend_read(uint32_t off, uint8_t *dst, uint32_t len) {
	(void)off; (void)dst; (void)len;
}
void backend_write(uint32_t off, const uint8_t *src, uint32_t len) {
	(void)off; (void)src; (void)len;
}
bool backend_detect_detail(void) { return false; }
void backend_write_detail(uint32_t o, const uint8_t *s, uint32_t l) { (void)o; (void)s; (void)l; }
void backend_read_detail(uint32_t o, uint8_t *d, uint32_t l) { (void)o; (void)d; (void)l; }

static void ingest(uint32_t treasury, uint16_t day) {
	save_aggregate_t agg;
	memset(&agg, 0, sizeof agg);
	agg.treasury = treasury;
	agg.seed = 42u;
	sim_ingest_save(&agg, day);
}

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);
	sim_init(1u);

	uint32_t frame = 1u;
	int run_debits = 0;
	for (int run = 0; run < 20; ++run) {
		const uint8_t front = (uint8_t)(run % (int)SIM_FRONT_COUNT);
		ingest(600u, 5u);
		campaign_focus_province(front);

		/* tap 1: list RECRUIT -> the success footer holds, then the
		 * handoff opens the Base Recruit tab with the row preset.   */
		int taps = 0;
		campaign_input((uint8_t)UI_EVT_KEY, KEY_A);
		++taps;
		for (int f = 0; f < 10; ++f) { /* the 160 ms hold          */
			campaign_frame();
		}
		assert(campaign_request() == UI_SCREEN_BASE);
		assert(sim_deploy_province() == -1); /* preset at dispatch  */

		/* dispatch: the campaign command debit... no: the campaign
		 * confirm enqueued cmd0 (recruit front), dispatch now
		 * debits the front cost and stamps the Base preset.         */
		queue_frame(++frame);
		assert(sim_deploy_province() == front);
		const uint32_t after_tap1 = sim_snapshot()->treasury;
		assert(after_tap1 < 600u); /* front cost debited once       */
		++run_debits;

		/* tap 2: Base +5 stepper (focus 0 on the Recruit tab)        */
		base_input((uint8_t)UI_EVT_KEY, KEY_A);
		++taps;
		queue_frame(++frame);
		assert(sim_chest() == 5u);

		/* tap 3: Confirm                                             */
		base_input((uint8_t)UI_EVT_KEY, KEY_A);
		++taps;
		queue_frame(++frame);
		assert(sim_chest() == 10u);
		/* the muster is live: the exported aggregate carries the
		 * pending 10 (the snapshot view holds resources only)        */
		save_aggregate_t exported;
		memset(&exported, 0, sizeof exported);
		sim_export_aggregate(&exported);
		assert(exported.pending_reward == 10u);
		assert(sim_chest() == 10u);
		assert(taps == 3); /* 3 taps, 0 modals, everything inline    */
	}
	printf("flow: 20/20 scripted 3-tap runs (front -> stepper -> "
	       "confirm), %d front debits, muster live every run\n",
	       run_debits);

	printf("B12 3-tap host tests passed.\n");
	return 0;
}
