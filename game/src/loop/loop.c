/* loop.c - the frame cycle (module: loop).
 * Owns VBlank, the screen switch order and the per-frame sequence:
 * input -> sim step -> ui draw -> save coalesced commit (ADR-0001).
 */
#include <nds.h>
#include <calico.h>

#include "contracts/loop.h"
#include "contracts/input.h"
#include "contracts/sim_api.h"
#include "contracts/ui.h"

static uint32_t frame_counter;

void loop_init(void) {
	frame_counter = 0u;
}

static void loop_frame(void) {
	(void)frame_counter++;
	input_event_t events[INPUT_QUEUE_CAP];
	const uint8_t count = input_frame(events);
	for (uint8_t i = 0; i < count && i < INPUT_QUEUE_CAP; ++i) {
		/* Command plumbing ships with B03/B12; B01 just drains the queue. */
	}
	sim_step(1u);
	ui_frame();
}

void loop_start(void) {
	while (pmMainLoop()) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_START) {
			break;
		}
		loop_frame();
	}
}
