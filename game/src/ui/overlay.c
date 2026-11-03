/* overlay.c - the L+R+START diagnostics overlay (module: ui; B14).
 * Six counters readable on device (spec-01 T011, spec-02 T014, spec-04
 * T031): frame ms, ticks, missed ticks, queue high-water, last reject,
 * save status. Page 2 is the SLO screen with one probe, threshold and
 * owner per SLO (SDD-7): loop <= 300 s, frame p95 <= 13.3 ms,
 * 0 missed ticks/min, boot <= 2 s, restore <= 2 s. Data comes from the
 * diag contract only; ui never writes run state (constitution 6).        */
#include <stdio.h>

#include "contracts/queue.h"
#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/trace.h"
#include "overlay.h"
#include "ui_draw.h"

/* Page-2 data: threshold + probe + owner per SLO row (SDD-7 / spec-06
 * plan). The keys are the diag keys the probes write.                    */
static const char *const slo_rows[] = {
	"SLO      PROBE      OWNER",
	"LOOP<300s  flow/20   pm-arch",
	"FRM<13.3ms cyc05/19  arch",
	"MISSED0/m  diagring  loop",
	"BOOT<2s    scan250   title",
	"REST<2s    bootscan  save",
};
#define SLO_ROWS (sizeof slo_rows / sizeof slo_rows[0])
_Static_assert(SLO_ROWS == 6, "SLO page pins to 6 rows (header + 5)");

static const char *reject_word(uint8_t reason) {
	/* queue.h v1 reasons, one short word each.                           */
	static const char *words[] = {
		"-", "FULL", "TREY", "DUP", "DEB", "ARG", "CHEST",
	};
	return words[reason < 7u ? reason : 0u];
}

void overlay_frame(void) {
	char line[40];
	const uint8_t page = diag_overlay_page();
	if (page == 0u) {
		return;
	}
	const diag_stat_t *st = diag_stats();
	const queue_diag_t *qd = queue_diag();

	if (page == 1u) {
		save_slot_info_t slots[2];
		save_slots(slots);
		snprintf(line, sizeof line, "DIAG  boot:%u", (unsigned)trace_boot());
		ui_draw_text(0, 20, line);
		snprintf(line, sizeof line,
		         "FRM %lu/%lu  TK %lu  MI %lu",
		         (unsigned long)st[DIAG_KEY_FRAME_MS].last,
		         (unsigned long)st[DIAG_KEY_FRAME_MS].max,
		         (unsigned long)sim_tick(),
		         (unsigned long)st[DIAG_KEY_MISSED_TIC].last);
		ui_draw_text(0, 21, line);
		snprintf(line, sizeof line, "Q %u/%u  RJ %s/%lu",
		         (unsigned)qd->cmd_high_water, (unsigned)QUEUE_CMD_CAP,
		         reject_word((uint8_t)qd->last_reject),
		         (unsigned long)qd->rejects_total);
		ui_draw_text(0, 22, line);
		/* Save diagnostics (spec-06 T027): commit seq, slot seqs,
		 * last status - the trace id joins header, status and log.   */
		snprintf(line, sizeof line, "SAVE %s %lu [%lu|%lu]",
		         save_status_text(save_status()),
		         (unsigned long)save_commit_seq(),
		         (unsigned long)slots[0].seq,
		         (unsigned long)slots[1].seq);
		ui_draw_text(0, 23, line);
	} else {
		for (unsigned i = 0u; i < SLO_ROWS; ++i) {
			ui_draw_text(0, (int)(18 + i), slo_rows[i]);
		}
	}
}
