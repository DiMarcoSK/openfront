/* diag.c - the 10 s diagnostics ring (module: loop; spec-02 T014, B14).
 * Loop owns the ring: six keys sampled once per 10 Hz tick into a
 * 100-sample window; the L+R+START overlay shows the window stats and
 * the SLO table (2 pages), and one mirror line goes to emulator stdout.
 * 0 network syscalls: stdout only, nothing shipped (constitution 4).
 * spec-01 T011 / spec-02 T014 / spec-04 T031: frame ms, tick count,
 * missed ticks, queue high-water, last reject, save status.
 */
#include <nds.h>
#include <stdio.h>

#include "contracts/queue.h"
#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "diag.h"

/* window[key][sample] - 6 keys x 100 u32 x 4 B = 2.4 KB RAM.            */
static uint32_t window[DIAG_KEY_COUNT][DIAG_WINDOW_TICKS];
static diag_stat_t stats[DIAG_KEY_COUNT];
static uint8_t wn;   /* samples accumulated, <= DIAG_WINDOW_TICKS       */
static uint8_t wold; /* evicted-index when the ring is full             */
static uint8_t page; /* 0 closed, 1 counters, 2 SLO (overlay state)     */

void diag_push(diag_key_t key, uint32_t value) {
	if (key < DIAG_KEY_COUNT) {
		/* Writing to a key overwrites (queue.h v1).                    */
		stats[key].last = value;
	}
}

uint32_t diag_read(diag_key_t key) {
	return (key < DIAG_KEY_COUNT) ? stats[key].last : 0u;
}

void diag_frame_sample(void) {
	for (uint8_t k = 0u; k < (uint8_t)DIAG_KEY_COUNT; ++k) {
		window[k][wold] = stats[k].last;
	}
	wold = (uint8_t)((wold + 1u) % DIAG_WINDOW_TICKS);
	if (wn < DIAG_WINDOW_TICKS) {
		wn++; /* fill first, evict after (true 10 s window)          */
	}
	/* 640 reads @ 10 Hz: the stats are window-true up to a 10 s lag.     */
	for (uint8_t k = 0u; k < (uint8_t)DIAG_KEY_COUNT; ++k) {
		uint32_t min = 0xFFFFFFFFu, max = 0u, sum = 0u;
		for (uint8_t i = 0u; i < wn; ++i) {
			const uint32_t v = window[k][i];
			if (v < min) min = v;
			if (v > max) max = v;
			sum += v;
		}
		stats[k].min = min;
		stats[k].max = max;
		stats[k].sum = sum;
		stats[k].n = wn;
	}
}

const diag_stat_t *diag_stats(void) {
	return stats;
}

void diag_add_missed(uint32_t n) {
	stats[DIAG_KEY_MISSED_TIC].last += n;
}

void diag_overlay_cycle(void) {
	page = (uint8_t)((page + 1u) % 3u);
	if (page != 0u) {
		diag_mirror(); /* the L+R+START view mirrors to stdout */
	}
}

uint8_t diag_overlay_page(void) {
	return page;
}

/* The one-line mirror: all six counters readable in stdout (B14
 * acceptance: "mirrored to emulator stdout").                            */
void diag_mirror(void) {
	const queue_diag_t *qd = queue_diag();
	static const char *rejects[] = {
		"none", "full", "trey", "dup", "deb", "arg", "chest",
	};
	const char *save = save_status_text(save_status());
	iprintf(
	    "t=diag f=%lu frame_ms=%lu ticks=%lu missed=%lu "
	    "q_hi=%lu rej=%s:%lu save=%s\n",
	    (unsigned long)stats[DIAG_KEY_TICK].last,
	    (unsigned long)stats[DIAG_KEY_FRAME_MS].last,
	    (unsigned long)sim_tick(),
	    (unsigned long)stats[DIAG_KEY_MISSED_TIC].last,
	    (unsigned long)qd->cmd_high_water,
	    rejects[qd->last_reject < 7u ? qd->last_reject : 0u],
	    (unsigned long)qd->rejects_total,
	    save);
	(void)window;
}
