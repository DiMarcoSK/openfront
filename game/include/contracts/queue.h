/* queue.h v1 - the single conduit (spec-02 plan: B03).
 * Owned by loop (the frame cycle): input 8/frame, commands 32, saves 1.
 * Every other module enqueues through these but never drains directly.
 * Reject counters live here so a lost event is counted in exactly one
 * place ("0 silent drops").
 * Additive only: queue_v2.h + adapter is the breaking-change path.
 */
#ifndef WAR_TABLE_QUEUE_H_V1
#define WAR_TABLE_QUEUE_H_V1

#include <stdint.h>
#include <stdbool.h>

#define QUEUE_ABI_VERSION 1u

#define QUEUE_INPUT_CAP 8u   /* input events per frame               */
#define QUEUE_CMD_CAP   32u  /* pending sim commands                  */
#define QUEUE_SAVE_CAP  1u   /* outstanding save commit               */
#define QUEUE_REJECT_LOG_CAP 4u

typedef enum queue_reject_reason {
	QUEUE_REJECT_NONE = 0,
	QUEUE_REJECT_QUEUE_FULL,
	QUEUE_REJECT_INSUFFICIENT,
	QUEUE_REJECT_DUP_SEQ,
	QUEUE_REJECT_DEBOUNCE,
	QUEUE_REJECT_BAD_ARG,
	/* additive B08 (B10/B12): command-level re-check failure codes.    */
	QUEUE_REJECT_CHEST_FULL,
} queue_reject_reason_t;

typedef struct queue_diag {
	uint16_t input_high_water;   /* max events queued in one frame       */
	uint16_t cmd_high_water;     /* max commands queued                  */
	uint16_t cmd_depth;          /* current depth                        */
	uint16_t save_depth;         /* 0 or 1                               */
	uint32_t rejects_total;
	uint32_t queue_full_rejects;
	queue_reject_reason_t last_reject;
	uint16_t debounce_msleft;
} queue_diag_t;

/* ---- diag (B03) ------------------------------------------------------- */

typedef enum diag_key {
	DIAG_KEY_TICK,
	DIAG_KEY_MISSED_TIC,
	DIAG_KEY_FRAME_MS,
	DIAG_KEY_QUEUE_HIGH,
	DIAG_KEY_REJECT_LAST,
	DIAG_KEY_SAVE_STATUS,
	DIAG_KEY_COUNT,
} diag_key_t;

/* Loop owns the 10 s ring (spec-02 T014). Writing to a key overwrites. */
void diag_push(diag_key_t key, uint32_t value);
uint32_t diag_read(diag_key_t key);

/* ---- additive B14: the 10 s window + the L+R+START overlay ------------
 * Diag stays loop-owned; these add the window statistics the overlay
 * shows and the test/overlay seams. No telemetry egress: the mirror is
 * emulator stdout only (constitution 4).
 */
#define DIAG_WINDOW_TICKS 100u /* 10 s at 10 Hz                        */

typedef struct diag_stat {
	uint32_t last;       /* last value of the key                        */
	uint32_t min;        /* window min                                   */
	uint32_t max;        /* window max                                   */
	uint32_t sum;        /* window sum (same unit as the key)            */
	uint16_t n;          /* samples in the window (<= 100)               */
	uint16_t pad;
} diag_stat_t;

_Static_assert(sizeof(diag_stat_t) == 20, "diag stat pins to 20 B");

/* diag_frame_sample(): loop calls at each 10 Hz boundary - one sample
 * per key enters the ring, the window stats are recomputed.              */
void diag_frame_sample(void);

/* diag_stats(): the per-key window stats (const; loop is the writer).    */
const diag_stat_t *diag_stats(void);

/* The overlay flag lives with the ring (loop owns diag): cycle closed ->
 * counters -> SLO -> closed on each L+R+START chord.                     */
void diag_overlay_cycle(void);
uint8_t diag_overlay_page(void); /* 0 = closed, 1 = counters, 2 = SLO     */

/* diag_mirror(): dumps the six counters to emulator stdout (1 line),
 * on-device overlay mirror of the L+R+START view; 0 network syscalls.    */
void diag_mirror(void);

/* diag snapshot (const; loop is the writer).                          */
const queue_diag_t *queue_diag(void);

/* queue_frame_rejects(): rejects observed in the last frame, 0..N.      */
uint8_t queue_frame_rejects(void);

#endif /* WAR_TABLE_QUEUE_H_V1 */
