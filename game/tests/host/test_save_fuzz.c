/* test_save_fuzz.c - B25 tamper-proof and durable save path
 * (spec-06 T010, T012, T016-T017; ADR-0002).
 *  1. 1000 power cuts: a commit torn after k bytes (k random over the
 *     170 B slot) then a cold boot -> always a verified slot, 0 unplayable.
 *  2. 10k fuzz images: random blocks, and CRC-VALID slots with hostile
 *     payloads -> boot + ingest never hang (< 2 s each) and the range
 *     clamps always hold (treasury, roster, chest, muster, selection).
 *  3. DoS caps: a 10,000-unit record stays <= 300k cycles/frame, and a
 *     1000-command flood keeps the 32-slot ring.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/queue.h"
#include "contracts/ui.h"
#include "crc32.h"
#include "slots.h"

static uint8_t rom_block[512];
static int cut_after = -1;      /* >= 0: the next write tears there    */
static bool powered = true;

save_backend_kind_t backend_probe(void) { return SAVE_BACKEND_EEPROM; }
void backend_read(uint32_t off, uint8_t *dst, uint32_t len) {
	memmove(dst, rom_block + off, len);
}
void backend_write(uint32_t off, const uint8_t *src, uint32_t len) {
	if (!powered) {
		return;
	}
	uint32_t n = len;
	if (cut_after >= 0 && (uint32_t)cut_after < len) {
		n = (uint32_t)cut_after;
		powered = false; /* the cart is pulled mid-write              */
	}
	memmove(rom_block + off, src, n);
}
bool backend_detect_detail(void) { return false; }
void backend_write_detail(uint32_t o, const uint8_t *s, uint32_t l) { (void)o; (void)s; (void)l; }
void backend_read_detail(uint32_t o, uint8_t *d, uint32_t l) { (void)o; (void)d; (void)l; }

ui_screen_t ui_screen(void) { return UI_SCREEN_CAMPAIGN; }
uint8_t ui_selected_row(void) { return 0xFFu; }
void ui_input(uint8_t kind, uint32_t value) { (void)kind; (void)value; }

static uint32_t lcg = 12345u;
static uint32_t rnd(void) {
	lcg = lcg * 1664525u + 1013904223u;
	return lcg >> 8;
}

static save_aggregate_t agg_of(uint32_t treasury, uint16_t units) {
	save_aggregate_t a;
	memset(&a, 0, sizeof a);
	a.treasury = treasury;
	a.seed = 42u;
	a.rng_state = 9u;
	a.unit_count = units;
	return a;
}

/* a CRC-valid v2 slot around an arbitrary payload (the tamper case)    */
static void forge_slot(int idx, const save_aggregate_t *payload, uint32_t seq) {
	save_slot_t s;
	memset(&s, 0, sizeof s);
	memcpy(s.header.magic, "TYCN", 4);
	s.header.format = SAVE_FORMAT_V2;
	s.header.slot = (uint8_t)idx;
	s.header.seq = seq;
	s.header.day = (uint16_t)rnd();
	s.header.payload_len = (uint16_t)sizeof(save_aggregate_t);
	s.payload = *payload;
	s.crc32 = crc32_calc(0u, (const uint8_t *)&s,
	                     (uint32_t)(16u + sizeof(save_aggregate_t)));
	memcpy(rom_block + SLOT_REGION_OFF + (uint32_t)idx * 170u, &s, 170u);
}

static void check_clamps(void) {
	assert(sim_snapshot()->treasury <= SIM_TREASURY_CAP);
	assert(sim_roster() <= SIM_CHEST_CAP);
	assert(sim_chest() <= SIM_CHEST_CAP);
	assert(sim_chest() >= sim_roster());
	assert(sim_muster_total() <= SIM_PENDING_CAP);
	assert(sim_selected_province() < SIM_FRONT_COUNT);
	assert(sim_snapshot()->day >= 1u);
	assert(!sim_battle_active());
}

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);

	/* 1. 1000 torn writes -> 0 unplayable boots.                         */
	{
		int restored = 0, older = 0;
		for (int cut = 0; cut < 1000; ++cut) {
			memset(rom_block, 0, sizeof rom_block);
			powered = true;
			cut_after = -1;
			save_init();
			save_aggregate_t a = agg_of(100u, 3u);
			save_set_day(4u);
			assert(save_commit(&a) == SAVE_STATUS_OK); /* the last good */
			const uint32_t want_new = 200u + (uint32_t)cut;
			save_aggregate_t b = agg_of(want_new, 5u);
			cut_after = (int)(rnd() % 171u); /* 0..170 bytes land     */
			(void)save_commit(&b);
			/* cold boot on the torn card                              */
			powered = true;
			cut_after = -1;
			save_init();
			save_slot_info_t s[2];
			save_slots(s);
			const int best = (s[0].valid && (!s[1].valid || s[0].seq > s[1].seq)) ? 0
			                 : s[1].valid ? 1 : -1;
			assert(best >= 0); /* never unplayable                    */
			save_aggregate_t out;
			assert(save_load_slot(best, &out) == SAVE_STATUS_OK);
			assert(out.treasury == 100u || out.treasury == want_new);
			if (out.treasury == want_new) {
				restored++;
			} else {
				older++;
			}
			sim_ingest_save(&out, s[best].day);
			check_clamps();
		}
		printf("save_fuzz: 1000 pulled-cart cuts -> 0 unplayable "
		       "(%d landed whole, %d fell back to the last good slot)\n",
		       restored, older);
	}

	/* 2. 10k fuzz images, half of them CRC-valid hostile payloads.       */
	{
		double worst = 0.0;
		int verified = 0;
		for (int i = 0; i < 10000; ++i) {
			if (i & 1) {
				for (int k = 0; k < 512; ++k) {
					rom_block[k] = (uint8_t)rnd();
				}
			} else {
				memset(rom_block, 0, sizeof rom_block);
				save_aggregate_t h;
				uint8_t *p = (uint8_t *)&h;
				for (size_t k = 0; k < sizeof h; ++k) {
					p[k] = (uint8_t)rnd();
				}
				forge_slot((int)(rnd() & 1u), &h, rnd());
			}
			const clock_t t0 = clock();
			save_init();
			save_slot_info_t s[2];
			save_slots(s);
			sim_init(1u);
			for (int k = 0; k < 2; ++k) {
				save_aggregate_t out;
				if (save_load_slot(k, &out) == SAVE_STATUS_OK) {
					verified++;
					sim_ingest_save(&out, s[k].day);
					check_clamps();
				}
			}
			const double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
			if (ms > worst) {
				worst = ms;
			}
		}
		assert(worst < 2000.0);
		printf("save_fuzz: 10000 images (%d CRC-valid hostile) -> 0 hangs, "
		       "worst %.3f ms, clamps held on every load\n", verified, worst);
	}

	/* 3a. a 10,000-unit record stays inside the 300k cycles/frame.       */
	{
		save_aggregate_t a = agg_of(4000000000u, 10000u);
		a.pending_reward = 250u;
		a.reserved[0] = 0xFFu;
		a.reserved[1] = 0xFFu; /* chest 65535                           */
		a.selected_province = 200u;
		sim_init(1u);
		sim_ingest_save(&a, 1u);
		check_clamps();
		assert(sim_roster() == SIM_CHEST_CAP);
		sim_cmd_t d;
		memset(&d, 0, sizeof d);
		d.cmd = 4u;
		d.arg = 2u;
		assert(sim_apply(&d) == SIM_OK);
		assert(sim_battle_units() <= SIM_BATTLE_UNITS);
		uint32_t worst_ops = 0u, before = sim_ops_last_step();
		for (int t = 0; t < 9 && sim_battle_active(); ++t) {
			sim_battle_step();
			const uint32_t ops = sim_ops_last_step() - before;
			before = sim_ops_last_step();
			if (ops > worst_ops) {
				worst_ops = ops;
			}
		}
		/* x4 = 4 steps per frame at <= 30 cycles per op (B05 model)  */
		assert(worst_ops * 4u * 30u <= 300000u);
		printf("save_fuzz: 10000-unit record -> roster %u, %u chips, "
		       "worst %u ops/step (%u cycles/frame at x4) <= 300k\n",
		       (unsigned)sim_roster(), (unsigned)SIM_BATTLE_UNITS,
		       (unsigned)worst_ops, (unsigned)(worst_ops * 4u * 30u));
	}

	/* 3b. a 1000-command flood keeps the 32-slot ring (DoS cap).          */
	{
		unsigned accepted = 0u;
		for (uint32_t i = 0; i < 1000u; ++i) {
			sim_cmd_t c;
			memset(&c, 0, sizeof c);
			c.seq = 100000u + i;
			c.cmd = 0u;
			if (sim_enqueue(&c)) {
				accepted++;
			}
		}
		assert(accepted == 32u);
		assert(queue_diag()->cmd_depth == 32u);
		printf("save_fuzz: 1000-command flood -> 32 kept, 968 rejected inline\n");
	}

	printf("B25 save fuzz / cut / cap host tests passed.\n");
	return 0;
}
