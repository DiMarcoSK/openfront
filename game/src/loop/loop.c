/* loop.c - the frame cycle (module: loop).
 * Owns VBlank, the screen switch order and the per-frame sequence:
 * input -> sim step -> ui draw -> coalesced save (ADR-0001).
 * loop pushes a monotonic cmd seq and calls queue_frame() once per frame.
 * B14: loop also owns the diag/trace clock - wall-time frame ms and
 * missed ticks (tickGetCount), the 10 Hz window sample, the L+R+START
 * overlay chord and the 1-line-per-hop trace pump (0 network syscalls).
 */
#include <nds.h>
#include <calico.h>

#include "contracts/loop.h"
#include "contracts/audio.h"
#include "contracts/queue.h"
#include "contracts/render.h"
#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/trace.h"
#include "contracts/ui.h"
#include "diag.h"

void queue_init(void);
void queue_frame(uint32_t frame);

/* calico system tick clock: TICK_FREQ = SYSTEM_CLOCK/64 Hz(tick.h).     */
#define WALL_MS(ticks) ((uint32_t)((ticks) * 1000ull / TICK_FREQ))
#define WALL_US(ticks) ((uint32_t)((ticks) * 1000000ull / TICK_FREQ))

static uint32_t frame_counter;
static uint64_t wall_tick; /* last 10 Hz wall-clock stamp                */

void loop_init(void) {
	frame_counter = 0u;
	wall_tick = 0u;
	diag_init();
}

void loop_start(void) {
	bool running = true;
	while (running && pmMainLoop()) {
		swiWaitForVBlank();
		render_vblank(); /* last frame: overlay, OAM, palettes, tear-free */
		scanKeys();
		/* B14: the L+R+START dev overlay chord (all three edges in
		 * the same keysDown frame). The chord consumes START so the
		 * exit path below does not fire in the same frame.            */
		if ((keysDown() & (KEY_L | KEY_R | KEY_START)) ==
		    (KEY_L | KEY_R | KEY_START)) {
			diag_overlay_cycle();
		}
		/* START is the Settings entry on every screen (B24, spec-06
		 * 6.4), delivered through the conduit like any key.           */
		trace_set_frame(frame_counter);

		const uint64_t t0 = tickGetCount();
		/* One conduit call per frame; ADR-0001: menus tick nothing.    */
		queue_frame(frame_counter);
		if ((frame_counter % 6u) == 0u) {
			/* Missed tick = the 10 Hz boundary ran later than the
			 * wall clock allows (VBlank overrun, save jam) - a
			 * catch-up would lie about the economy, so it counts
			 * (SDD-7: 0 missed ticks/min).                          */
			if (wall_tick != 0u) {
				if (WALL_MS(t0 - wall_tick) > 120u) {
					diag_add_missed(1u);
				}
			}
			wall_tick = t0;
			/* 10 Hz boundary: the economy runs on Campaign only,
			 * muster also runs in Base (spec-03 R1/R4), battle
			 * runs its own resolve next to the economy ticker. */
			const ui_screen_t scr = ui_screen();
			/* B22 (spec-04 T026): an open order panel, the pause or
			 * the boss intro holds the tick AND the field - 0
			 * catch-up when it closes (10 s open = 0 ticks).       */
			const bool hold = ui_sim_hold();
			uint8_t modes = 0u;
			if (scr == UI_SCREEN_CAMPAIGN || scr == UI_SCREEN_BATTLE ||
			    scr == UI_SCREEN_SETTINGS) {
				modes |= SIM_RUN_ECONOMY;
			}
			if (scr == UI_SCREEN_CAMPAIGN || scr == UI_SCREEN_BASE ||
			    scr == UI_SCREEN_BATTLE || scr == UI_SCREEN_SETTINGS) {
				modes |= SIM_RUN_MUSTER;
			}
			if (hold) {
				modes = 0u;
			}
			sim_set_run_mode(modes);
			const uint8_t subs = sim_speed();
			const uint32_t sim_t0 = tickGetCount();
			/* B20: n sub-steps per 10 Hz frame at the ACTIVE rate,
			 * same field rules (spec-04 R4, T017). The economy
			 * accumulates n ticks; each battle sub-step is one
			 * real-time field tick (ADR-0008), stopping at the
			 * seal: x4 is the same battle, four times as fast.   */
			sim_step(subs);
			if (scr == UI_SCREEN_BATTLE && !hold) {
				for (uint8_t s = 0u; s < subs; ++s) {
					if (!sim_battle_active() ||
					    sim_battle_sealed()) {
						break;
					}
					sim_battle_step();
				}
			}
			/* B20: honest clamp (R5) - the measured sim slice
			 * decides; no silent slowdown, the strip copies.      */
			const uint8_t active_before = sim_speed();
			sim_speed_slice(WALL_US(tickGetCount() - sim_t0));
			if (sim_speed() != active_before) {
				audio_cue(AUDIO_CUE_SPEED); /* id only (T019) */
			}
			diag_push(DIAG_KEY_TICK, sim_tick());
			diag_push(DIAG_KEY_QUEUE_HIGH,
			          queue_diag()->cmd_high_water);
			diag_push(DIAG_KEY_REJECT_LAST,
			          queue_diag()->rejects_total);
			diag_push(DIAG_KEY_SAVE_STATUS,
			          (uint32_t)save_status());
			diag_frame_sample(); /* one sample -> the 10 s window */
		}
		frame_counter++;
		render_begin();
		ui_frame();
		render_frame();
		diag_push(DIAG_KEY_FRAME_MS, WALL_MS(tickGetCount() - t0));
		/* The pump: 1 trace line per hop to the emulator console.      */
		trace_sync();
		if (diag_overlay_page() != 0u && (frame_counter % 60u) == 0u) {
			diag_mirror(); /* stdout mirror while the overlay is up */
		}
	}
}
