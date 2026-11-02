/* sim.c - economy + command queue (module: sim).
 * B01 scaffold: a 1-command ring that reports the state; the real economy
 * engine ships with B03/B08/B09/B10. The snapshot is 8 B (B02 pinning),
 * the command ring is 32 (spec-02 plan section 4).
 */
#include <nds.h>
#include <calico.h>

#include "contracts/sim_api.h"

static sim_snapshot_t snap;
static sim_cmd_t ring[32];
static uint8_t ring_head, ring_tail, ring_depth;

#define CMD_RING_CAP 32u

static bool q_full(void) { return ring_depth == CMD_RING_CAP; }
static bool q_empty(void) { return ring_depth == 0u; }

void sim_init(uint32_t seed) {
	snap.treasury = 300u;  /* spec-01 Empty copy: Day 1, treasury 300     */
	snap.day = 1u;
	snap.selected = 0u;
	snap.flags = 0u;
	ring_head = ring_tail = ring_depth = 0u;
	(void)seed;
}

const sim_snapshot_t *sim_snapshot(void) {
	return &snap;
}

bool sim_enqueue(const sim_cmd_t *cmd) {
	if (cmd == NULL || q_full()) {
		snap.flags |= 0x2u; /* QUEUE_FULL bit (spec-02 R7)                 */
		return false;
	}
	ring[ring_head] = *cmd;
	ring_head = (uint8_t)((ring_head + 1u) % CMD_RING_CAP);
	ring_depth++;
	return true;
}

void sim_step(uint8_t ticks) {
	for (uint8_t i = 0; i < ticks; ++i) {
		if (!q_empty()) {
			const sim_cmd_t cmd = ring[ring_tail];
			ring_tail = (uint8_t)((ring_tail + 1u) % CMD_RING_CAP);
			ring_depth--;
			(void)cmd; /* application ships with B03/B08                  */
		}
		/* +2 treasury per 600-tick day lands with B08. */
	}
}

/* Sealed outcome storage: sim is the single writer. */
static battle_outcome_t last_outcome;
static bool outcome_sealed;

const battle_outcome_t *sim_seal_outcome(void) {
    if (!outcome_sealed) {
        last_outcome.version = 1u;
        last_outcome.battle_id = 0u;
        last_outcome.result = 0u;
        last_outcome.seed = 1u;
        last_outcome.treasury_delta = 0;
        last_outcome.day_delta = 0u;
        last_outcome.units_before = last_outcome.units_after = 0u;
        last_outcome.province_id = last_outcome.province_state = 0u;
        last_outcome.tier_before = last_outcome.tier_after = 0u;
        outcome_sealed = true;
    }
    return &last_outcome;
}
