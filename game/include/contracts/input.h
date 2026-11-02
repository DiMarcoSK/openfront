/* input.h v1 - the edge on keys + touch and the 8-entry queue.
 * Owner: input module (src/input). It delivers an edge (keysDown) plus
 * direct-select touch into the 8-entry queue; nothing else reads raw
 * NDS input state.
 * Additive only: input_v2.h + adapter is the breaking-change path.
 */
#ifndef WAR_TABLE_INPUT_H_V1
#define WAR_TABLE_INPUT_H_V1

#include <stdint.h>

#define INPUT_ABI_VERSION 1u

/* spec-01 R6: touch tap 1 moves the ring, tap 2 on the same target acts. */
#define INPUT_QUEUE_CAP 8u
#define INPUT_TOUCH_DEBOUNCE_MS 250u

typedef enum input_event_kind {
	INPUT_KEY_DOWN = 0,
	INPUT_TOUCH_TAP,
	INPUT_TICK_READY,
} input_event_kind_t;

typedef struct input_event {
	uint8_t       kind;
	uint8_t       frame;     /* VBlank frame that produced it            */
	uint16_t      pad;
	uint32_t      value;     /* bit mask for keys, x/y for touch          */
} input_event_t;

_Static_assert(sizeof(input_event_t) == 8, "input event is 8 B");

/* input_init(): registers the IRQ + queue; run once from boot.            */
void input_init(void);

/* input_frame(input_event_t *out): pops up to INPUT_QUEUE_CAP entries.
 * Returns the number popped. The cap is enforced by the queue.           */
uint8_t input_frame(input_event_t *out);

#endif /* WAR_TABLE_INPUT_H_V1 */
