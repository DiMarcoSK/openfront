/* loop.c - the frame cycle (module: loop).
 * Owns VBlank, the screen switch order and the per-frame sequence:
 * input -> sim step -> ui draw -> coalesced save (ADR-0001).
 * loop pushes a monotonic cmd seq and calls queue_frame() once per frame.
 */
#include <nds.h>
#include <calico.h>

#include "contracts/loop.h"
#include "contracts/queue.h"
#include "contracts/sim_api.h"
#include "contracts/ui.h"

void queue_init(void);
void queue_frame(uint32_t frame);
void diag_push(diag_key_t key, uint32_t value);

static uint32_t frame_counter;

/* The 10 s diag ring: loop owns it (spec-02 T014).                      */
#define DIAG_RING_SIZE 128u
static uint32_t diag_ring[DIAG_RING_SIZE];
static uint8_t diag_head;

void diag_push(diag_key_t key, uint32_t value) {
	(void)key; /* key indexing ships with B05/B14; a ring keeps the cost. */
	diag_ring[diag_head] = value;
	diag_head = (uint8_t)((diag_head + 1u) % DIAG_RING_SIZE);
}

uint32_t diag_read(diag_key_t key) {
	(void)key;
	return diag_ring[(uint8_t)((diag_head + DIAG_RING_SIZE - 1u) % DIAG_RING_SIZE)];
}

void loop_init(void) {
	frame_counter = 0u;
}

void loop_start(void) {
	bool running = true;
	while (running && pmMainLoop()) {
		swiWaitForVBlank();
		scanKeys();
		/* START exits outside the title only: on the title it is the
		 * Settings entry (spec-01 6.5), delivered through the conduit. */
		if (ui_screen() != UI_SCREEN_TITLE && (keysDown() & KEY_START)) {
			running = false;
		}
		/* One conduit call per frame; ADR-0001: menus tick nothing.    */
		queue_frame(frame_counter);
		if ((frame_counter % 6u) == 0u) {
			/* 10 Hz boundary: the economy runs on Campaign only,
			 * muster also runs in Base (spec-03 R1/R4), battle
			 * runs its own resolve next to the economy ticker. */
			const ui_screen_t scr = ui_screen();
			uint8_t modes = 0u;
			if (scr == UI_SCREEN_CAMPAIGN || scr == UI_SCREEN_BATTLE ||
			    scr == UI_SCREEN_SETTINGS) {
				modes |= SIM_RUN_ECONOMY;
			}
			if (scr == UI_SCREEN_CAMPAIGN || scr == UI_SCREEN_BASE ||
			    scr == UI_SCREEN_BATTLE || scr == UI_SCREEN_SETTINGS) {
				modes |= SIM_RUN_MUSTER;
			}
			sim_set_run_mode(modes);
			sim_step(1u);
			if (scr == UI_SCREEN_BATTLE) {
				sim_battle_step();
			}
			diag_push(DIAG_KEY_TICK, frame_counter / 6u);
		}
		frame_counter++;
		ui_frame();
	}
}
