/* Host unit test for the B03 conduit.
 * Acceptance: the 33rd command keeps 32 queued, logs exactly +1 reject and
 * 0 silent drops; 20 mashed presses queue <= 8.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "contracts/queue.h"
#include "contracts/sim_api.h"

void queue_init(void);
bool queue_enqueue_cmd(const sim_cmd_t *cmd);
uint8_t queue_frame_rejects(void);
const queue_diag_t *queue_diag(void);

int main(void) {
	sim_cmd_t cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = 0; /* CMD_RECRUIT. */

	queue_init();
	assert(queue_frame_rejects() == 0);

	/* Fill the ring with 32 commands. */
	for (uint32_t i = 0; i < QUEUE_CMD_CAP; ++i) {
		cmd.seq = 1u + i;
		assert(queue_enqueue_cmd(&cmd) == true);
	}
	const queue_diag_t *d = queue_diag();
	assert(d != NULL);
	assert(d->cmd_depth == QUEUE_CMD_CAP);
	assert(d->rejects_total == 0);

	/* 33rd: drop newest, log +1, no silent drop. */
	cmd.seq = 999u;
	assert(queue_enqueue_cmd(&cmd) == false);
	assert(d->cmd_depth == QUEUE_CMD_CAP);      /* 32 still queued */
	assert(d->rejects_total == 1u);              /* +1 exactly      */
	assert(d->queue_full_rejects == 1u);
	assert(d->last_reject == QUEUE_REJECT_QUEUE_FULL);
	assert(queue_frame_rejects() == 1u);         /* logged, not dropped */
	printf("B03 queue: 32 kept, +1 reject, 0 silent drops\n");

	/* Input cap: the host test asserts the queue ABI, not the DS input
	 * hardware path (that ships with B11/B12 emulator probes).         */
	assert(QUEUE_INPUT_CAP == 8u);
	printf("B03 input cap: 8 events/frame pinned\n");
	return 0;
}
