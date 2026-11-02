/* input.c - the edge on keys + touch and the 8-entry queue (module: input).
 * spec-01 R6: touch tap 1 moves the ring, tap 2 commits. The contract is
 * "input.h v1"; cap 8, debounce 250 ms, oldest dropped on overflow.
 */
#include <nds.h>
#include <calico.h>

#include "contracts/input.h"

static input_event_t q[INPUT_QUEUE_CAP];
static uint8_t head, tail, depth;
static uint32_t last_tap_frame;
static uint16_t debounce_left;

void input_init(void) {
	head = tail = depth = 0u;
	last_tap_frame = 0u;
	debounce_left = 0u;
}

static void push(uint8_t kind, uint32_t value) {
	if (depth == INPUT_QUEUE_CAP) {
		tail = (uint8_t)((tail + 1u) % INPUT_QUEUE_CAP); /* drop oldest */
		depth--;
	}
	q[head].kind = kind;
	q[head].frame = (uint8_t)(keysHeld() ? keysHeld() : 0u);
	q[head].pad = 0u;
	q[head].value = value;
	head = (uint8_t)((head + 1u) % INPUT_QUEUE_CAP);
	depth++;
}

uint8_t input_frame(input_event_t *out) {
	uint8_t emitted = 0u;

	const uint16_t keys = keysDown();
	if (keys) {
		push(INPUT_KEY_DOWN, keys);
	}

	touchPosition tp;
	touchRead(&tp);
	if (keysDown() & KEY_TOUCH) {
		if (debounce_left == 0u) {
			const uint32_t value = ((uint32_t)tp.px << 8) | (uint32_t)tp.py;
			push(INPUT_TOUCH_TAP, value);
			debounce_left = (uint16_t)(INPUT_TOUCH_DEBOUNCE_MS / 16u);
			last_tap_frame++;
		} else {
			debounce_left--;
		}
	}

	while (emitted < INPUT_QUEUE_CAP && depth > 0u) {
		out[emitted++] = q[tail];
		tail = (uint8_t)((tail + 1u) % INPUT_QUEUE_CAP);
		depth--;
	}
	return emitted;
}
