/* field.h - the real-time battlefield (module-private to sim, ADR-0008).
 * Divisions on a 512 px field in 3 lanes: they march, wheel round an open
 * wing, clash, shoot and rout. Pure integer math; its only luck is the
 * seed at open and the roll per tick, both from sim.c's LCG, so the same
 * inputs at the same ticks give the same field (ADR-0007). sim.c
 * owns the rules on top (morale, the general, the gods, the seal).
 */
#ifndef WAR_TABLE_SIM_FIELD_H
#define WAR_TABLE_SIM_FIELD_H

#include <stdbool.h>
#include <stdint.h>

#include "contracts/sim_api.h"

/* one company = FIELD_COMPANY_HP; a foot company strikes 6 hp a tick,
 * so one company alone on one company is ~4.5 min of melee at x1: whole
 * armies decide in 2-5 min, mostly by morale (balance_probe gates it). */
#define FIELD_COMPANY_HP 16384u
#define FIELD_SUB        16    /* x is map px * 16                     */
#define FIELD_W          512

typedef struct field_div {
	int32_t x;          /* map px * FIELD_SUB                          */
	uint32_t hp, hp0;
	uint8_t lane, cls, state, target;
	uint8_t wheel;      /* ticks left of a wheel onto another lane      */
	uint8_t impact;     /* ticks left of the first-contact shock       */
	uint8_t flags;      /* SIM_DIV_FLANKING | SIM_DIV_FLANKED (tick)    */
	uint8_t engaged;    /* has met the foe at least once               */
	uint8_t side;       /* ticks left of a side charge after a wheel    */
	uint8_t home;       /* the lane it mustered in: a wing stays a wing */
	uint8_t pad[2];
} field_div_t;

typedef struct field {
	field_div_t d[2][SIM_DIVISIONS];
	uint8_t n[2];
	bool clashed;
} field_t;

/* what sim.c sets every tick                                          */
typedef struct field_ctl {
	uint8_t stance;       /* our SIM_ORDER_*                            */
	uint8_t form;         /* our SIM_FORM_*                             */
	bool charge;          /* the general's charge drives the line       */
	bool ward;            /* the rally holds the wings: no side blows   */
	bool foe_rout;        /* their army has broken                      */
	uint16_t out_pct[2];  /* damage dealt, % per side                   */
	uint16_t in_pct[2];   /* damage taken, % per side                   */
	uint32_t roll;        /* this tick's luck (sim.c's LCG)             */
} field_ctl_t;

typedef struct field_report {
	uint16_t lost[2];     /* companies that fell this tick, per side    */
	uint8_t flanked[2];   /* divisions struck in a side/rear this tick  */
	uint8_t blunted;      /* side blows on our ring that the ring took  */
	uint8_t wheeling;     /* foe riders that began a wheel this tick    */
	bool clash;           /* the first melee contact of the battle      */
} field_report_t;

void field_open(field_t *f, uint16_t ours, uint8_t form, uint16_t foes,
                uint8_t terrain, uint32_t seed);
void field_tick(field_t *f, const field_ctl_t *c, field_report_t *r);
uint16_t field_companies(const field_t *f, uint8_t side);
/* the gods and the general act on the field through these three        */
uint16_t field_strike(field_t *f, uint8_t side, uint16_t companies);
uint16_t field_raise(field_t *f, uint8_t side, uint16_t companies);
uint16_t field_rout_weakest(field_t *f, uint8_t side);
void field_view(const field_t *f, uint8_t side, sim_division_t *out);
uint32_t field_ops(void);  /* unit ops of the last tick (cycle harness) */

#endif /* WAR_TABLE_SIM_FIELD_H */
