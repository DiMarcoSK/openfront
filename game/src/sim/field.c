/* field.c - the real-time battlefield (ADR-0008, GAME_DESIGN 7).
 * Up to 10 divisions a side in 3 lanes on a 512 px field. Every 10 Hz
 * tick each division picks a foe, marches or wheels to it, and strikes
 * when in reach; blows are summed and land together, so the order the
 * divisions are walked never changes the result. Flanks are geometry,
 * not dice: a lane left empty is a lane the foe rides through, and a
 * division that wheels in strikes the side (x1.5) or, past the line, the
 * rear (x2). Formations shape the ground they hold: the phalanx is
 * shields in front and bare sides, the ring has no sides at all.
 */
#include <string.h>

#include "field.h"

#define PX(v)      ((int32_t)(v) * FIELD_SUB)
#define MELEE      PX(14)
#define BOW        PX(88)
#define BOW_STAND  PX(72)
#define REAR_OFF   PX(10)   /* behind their line, inside melee reach      */
#define LANE_COST  PX(48)
#define WEST_EDGE  PX(16)
#define EAST_EDGE  PX(496)
#define SIDE_TICKS 50u     /* a side charge outlives the melee by 5 s    */

enum { INF = SIM_CLS_INFANTRY, ARCH = SIM_CLS_ARCHERS, CAV = SIM_CLS_CAVALRY };

static const uint8_t SPEED[3] = {8u, 7u, 16u};       /* px*16 per tick   */
static const uint8_t ATK[3] = {6u, 4u, 8u};          /* hp per company   */
static const uint8_t WHEEL_TICKS[3] = {40u, 40u, 20u};

/* our classes ride the formation slots; the art reads the same table   */
static const uint8_t OUR_CLS[SIM_DIVISIONS] = {
	INF, INF, CAV, ARCH, INF, INF, CAV, ARCH, INF, INF,
};
static const uint8_t OUR_LANE[SIM_FORM_COUNT][SIM_DIVISIONS] = {
	{1, 0, 2, 1, 2, 1, 0, 1, 0, 2},   /* Line: every lane held         */
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1},   /* Phalanx: one deep block       */
	{1, 1, 0, 1, 1, 1, 2, 1, 1, 1},   /* Wedge: a spearhead, riders out */
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1},   /* Circle: one ring              */
};
static const uint8_t FOE_CLS[SIM_DIVISIONS] = {
	INF, INF, INF, CAV, CAV, ARCH, INF, INF, ARCH, CAV,
};
static const uint8_t FOE_LANE[SIM_DIVISIONS] = {
	1, 0, 2, 0, 2, 1, 1, 2, 1, 0,
};

static uint32_t ops;

uint32_t field_ops(void) {
	return ops;
}

static uint16_t companies_of(uint32_t hp) {
	return (uint16_t)((hp + FIELD_COMPANY_HP - 1u) / FIELD_COMPANY_HP);
}

static bool alive(const field_div_t *d) {
	return d->state != SIM_DIV_DEAD && d->hp > 0u;
}

static int32_t iabs(int32_t v) {
	return v < 0 ? -v : v;
}

static void split(field_div_t *d, uint8_t n, uint16_t companies) {
	for (uint8_t i = 0; i < n; ++i) {
		const uint16_t c = (uint16_t)(companies / n + (i < companies % n ? 1u : 0u));
		d[i].hp = d[i].hp0 = (uint32_t)c * FIELD_COMPANY_HP;
	}
}

/* a cheap integer hash: the seed's say over jitter and luck            */
static uint32_t hash(uint32_t a, uint32_t b) {
	uint32_t h = (a ^ 0x9E3779B9u) * 0x85EBCA6Bu ^ b * 0xC2B2AE35u;
	h ^= h >> 13;
	h *= 0x27D4EB2Du;
	return h ^ (h >> 16);
}

void field_open(field_t *f, uint16_t ours, uint8_t form, uint16_t foes,
                uint8_t terrain, uint32_t seed) {
	memset(f, 0, sizeof *f);
	if (form >= SIM_FORM_COUNT) {
		form = SIM_FORM_LINE;
	}
	/* ours: one division per formation slot, ranks deepen per lane     */
	f->n[0] = (uint8_t)(ours < SIM_DIVISIONS ? ours : SIM_DIVISIONS);
	split(f->d[0], f->n[0], ours);
	uint8_t rank[3] = {0u, 0u, 0u};
	for (uint8_t i = 0; i < f->n[0]; ++i) {
		field_div_t *d = &f->d[0][i];
		d->cls = OUR_CLS[i];
		d->lane = d->home = OUR_LANE[form][i];
		d->target = 0xFFu;
		const int r = rank[d->lane]++;
		int x;
		if (d->cls == ARCH) {
			x = 128 - r * 6;
		} else if (d->cls == CAV) {
			x = 152 - r * 8;
		} else if (form == SIM_FORM_WEDGE) {
			x = 184 - r * 10;        /* the spearhead leads            */
		} else if (form == SIM_FORM_CIRCLE) {
			x = 160 - r * 6;         /* a tight ring                   */
		} else {
			x = 168 - r * (form == SIM_FORM_PHALANX ? 8 : 16);
		}
		/* no two musters stand quite alike: +-6 px                  */
		d->x = PX(x) + (int32_t)(hash(seed, i) % 13u) * FIELD_SUB - PX(6);
	}
	/* theirs: 2 companies a division (3..10), riders on the wings; the
	 * plains field more horse, the highlands fewer                     */
	uint8_t nf = (uint8_t)(foes < 3u ? foes : (foes / 2u > SIM_DIVISIONS ? SIM_DIVISIONS
	                                          : (foes / 2u < 3u ? 3u : foes / 2u)));
	f->n[1] = nf;
	split(f->d[1], nf, foes);
	uint8_t frank[3] = {0u, 0u, 0u};
	for (uint8_t i = 0; i < nf; ++i) {
		field_div_t *d = &f->d[1][i];
		d->cls = FOE_CLS[i];
		/* the rear divisions are whatever the foe could muster       */
		if (i >= 6u) {
			static const uint8_t MUSTER[4] = {INF, CAV, ARCH, INF};
			d->cls = MUSTER[hash(seed, 100u + i) & 3u];
		}
		if (terrain == SIM_TERRAIN_PLAINS && i == 1u) d->cls = CAV;
		if (terrain == SIM_TERRAIN_HIGHLANDS && i == 3u) d->cls = INF;
		d->lane = d->home = FOE_LANE[i];
		d->target = 0xFFu;
		const int r = frank[d->lane]++;
		d->x = PX(d->cls == ARCH ? 388 + r * 6 : d->cls == CAV ? 362 + r * 8
		                                                   : 344 + r * 16) +
		       (int32_t)(hash(seed, 50u + i) % 13u) * FIELD_SUB - PX(6);
	}
}

uint16_t field_companies(const field_t *f, uint8_t side) {
	uint16_t n = 0u;
	for (uint8_t i = 0; i < f->n[side & 1u]; ++i) {
		const field_div_t *d = &f->d[side & 1u][i];
		if (alive(d) && d->state != SIM_DIV_ROUT) {
			n = (uint16_t)(n + companies_of(d->hp));
		}
	}
	return n;
}

/* the nearest standing foe; a lane away costs LANE_COST of march, a
 * lane the division was not sent to costs a lot more                   */
static uint8_t pick(const field_t *f, uint8_t s, const field_div_t *a, int want) {
	const uint8_t o = (uint8_t)(s ^ 1u);
	uint8_t best = 0xFFu;
	int32_t bd = 0x7FFFFFFF;
	ops += f->n[o]; /* the scan is the unit work the harness counts    */
	for (uint8_t j = 0; j < f->n[o]; ++j) {
		const field_div_t *b = &f->d[o][j];
		if (!alive(b) || b->state == SIM_DIV_ROUT) {
			continue;
		}
		int32_t d = iabs(b->x - a->x) + LANE_COST * iabs((int32_t)b->lane - (int32_t)a->lane);
		if (want >= 0 && b->lane != (uint8_t)want) {
			d += PX(400);
		}
		if (d < bd) {
			bd = d;
			best = j;
		}
	}
	return best;
}

/* a blow already in reach in the division's own lane comes first       */
static uint8_t in_reach(const field_t *f, uint8_t s, const field_div_t *a) {
	const uint8_t o = (uint8_t)(s ^ 1u);
	uint8_t best = 0xFFu;
	int32_t bd = MELEE + 1;
	ops += f->n[o];
	for (uint8_t j = 0; j < f->n[o]; ++j) {
		const field_div_t *b = &f->d[o][j];
		if (alive(b) && b->lane == a->lane && iabs(b->x - a->x) < bd) {
			bd = iabs(b->x - a->x);
			best = j;
		}
	}
	return best;
}

/* 88..112%: the fortune of one division's blows this tick             */
static uint32_t luck(uint32_t roll, uint8_t s, uint8_t i) {
	return 88u + hash(roll, (uint32_t)s * 16u + i) % 25u;
}

static void step_x(field_div_t *a, int32_t goal, int32_t speed) {
	if (a->x < goal) {
		a->x = (a->x + speed > goal) ? goal : a->x + speed;
	} else if (a->x > goal) {
		a->x = (a->x - speed < goal) ? goal : a->x - speed;
	}
}

void field_tick(field_t *f, const field_ctl_t *c, field_report_t *r) {
	uint32_t dmg[2][SIM_DIVISIONS];
	memset(dmg, 0, sizeof dmg);
	memset(r, 0, sizeof *r);
	ops = 0u;
	for (uint8_t s = 0; s < 2u; ++s) {
		for (uint8_t i = 0; i < f->n[s]; ++i) {
			f->d[s][i].flags = 0u;
		}
	}
	for (uint8_t s = 0; s < 2u; ++s) {
		const uint8_t o = (uint8_t)(s ^ 1u);
		const int32_t dir = (s == 0u) ? 1 : -1;
		for (uint8_t i = 0; i < f->n[s]; ++i) {
			field_div_t *a = &f->d[s][i];
			if (!alive(a)) {
				continue;
			}
			ops++;
			if (a->impact > 0u) a->impact--;
			/* a division that wheeled in keeps the side while it fights */
			if (a->side > 0u && a->state != SIM_DIV_FIGHT) a->side--;
			int32_t speed = SPEED[a->cls];
			if (s == 0u && c->form == SIM_FORM_CIRCLE) speed = speed * 3 / 4;
			if (s == 0u && c->charge) speed = speed * 3 / 2;
			/* a broken army runs for its own edge and strikes nothing */
			if (s == 1u && c->foe_rout) {
				a->state = SIM_DIV_ROUT;
				a->x += speed;
				if (a->x > PX(FIELD_W + 8)) {
					a->state = SIM_DIV_DEAD; /* off the field: fled  */
				}
				continue;
			}
			const uint8_t stance = (s == 0u) ? (c->charge ? SIM_ORDER_ADVANCE : c->stance)
			                                 : SIM_ORDER_ADVANCE;
			/* ---- whom to fight -------------------------------------- */
			int want = -1;
			const bool wheeler = a->cls != ARCH &&
			                     (a->cls == CAV || a->home != SIM_LANE_CENTRE);
			if (s == 0u && stance == SIM_ORDER_FLANK && wheeler) {
				want = SIM_LANE_CENTRE; /* swing onto their centre      */
			}
			uint8_t t = (a->cls == ARCH) ? 0xFFu : in_reach(f, s, a);
			if (t == 0xFFu || a->wheel > 0u) {
				if (a->target >= f->n[o] || !alive(&f->d[o][a->target]) ||
				    f->d[o][a->target].state == SIM_DIV_ROUT ||
				    (want >= 0 && f->d[o][a->target].lane != (uint8_t)want)) {
					a->target = pick(f, s, a, want);
				}
				t = a->target;
			}
			a->target = t;
			if (t == 0xFFu) {
				a->state = SIM_DIV_HOLD;
				continue;
			}
			field_div_t *b = &f->d[o][t];
			const int32_t dx = b->x - a->x;
			const bool may_move = s == 1u || stance == SIM_ORDER_ADVANCE ||
			                      (stance == SIM_ORDER_FLANK && wheeler);
			/* ---- fall back: open the distance, bows still loose ---- */
			if (s == 0u && stance == SIM_ORDER_FALLBACK) {
				a->state = SIM_DIV_MARCH;
				a->x = (a->x - speed < WEST_EDGE) ? WEST_EDGE : a->x - speed;
				a->wheel = 0u;
				if (a->cls != ARCH) {
					continue;
				}
			}
			/* ---- archers: stand off and shoot ---------------------- */
			if (a->cls == ARCH) {
				const int32_t reach = iabs(dx) + LANE_COST / 2 *
				                      iabs((int32_t)b->lane - (int32_t)a->lane);
				if (reach <= BOW) {
					a->state = SIM_DIV_SHOOT;
					uint32_t hit = companies_of(a->hp) * (uint32_t)ATK[ARCH];
					hit = hit * c->out_pct[s] / 100u * luck(c->roll, s, i) / 100u;
					if (o == 0u && c->form == SIM_FORM_PHALANX) hit = hit * 3u / 4u;
					dmg[o][t] += hit * c->in_pct[o] / 100u;
					a->engaged = 1u;
				} else if (may_move) {
					a->state = SIM_DIV_MARCH;
					step_x(a, b->x - dir * BOW_STAND, speed);
				} else if (!(s == 0u && stance == SIM_ORDER_FALLBACK)) {
					a->state = SIM_DIV_HOLD;
				}
				continue;
			}
			/* ---- melee: wheel onto another lane first -------------- */
			if (b->lane != a->lane) {
				if (!may_move) {
					a->state = SIM_DIV_HOLD;
					continue;
				}
				/* a planned flank (our Flank stance, or riders through
				 * an open wing) rides down its own lane past their line,
				 * then turns in on the side or the rear; foot with an
				 * empty lane closes on the nearest fight at once       */
				const bool ride = (s == 0u && want >= 0) || a->cls == CAV;
				const int32_t goal = !ride ? b->x
				                     : (s == 0u) ? b->x + REAR_OFF : b->x - REAR_OFF;
				if (ride && a->wheel == 0u && iabs(goal - a->x) > PX(24)) {
					a->state = SIM_DIV_MARCH;
					step_x(a, goal, speed);
					continue;
				}
				if (a->wheel == 0u) {
					a->wheel = WHEEL_TICKS[a->cls];
					if (s == 1u && a->cls == CAV) {
						r->wheeling++;
					}
				}
				a->state = SIM_DIV_WHEEL;
				step_x(a, goal, speed);
				if (--a->wheel == 0u) {
					a->lane = b->lane;
					a->side = SIDE_TICKS;
				}
				continue;
			}
			if (iabs(dx) > MELEE) {
				if (may_move) {
					a->state = SIM_DIV_MARCH;
					const int32_t goal = (a->side > 0u && s == 0u && want >= 0)
					                         ? b->x + REAR_OFF
					                         : b->x - dir * (MELEE - PX(2));
					step_x(a, goal, speed);
				} else {
					a->state = SIM_DIV_HOLD;
				}
				continue;
			}
			/* ---- the blow ------------------------------------------- */
			a->state = SIM_DIV_FIGHT;
			if (!a->engaged) {
				a->engaged = 1u;
				if (a->cls == CAV) {
					a->impact = 30u; /* the riders' shock             */
				} else if (s == 0u && c->form == SIM_FORM_WEDGE) {
					a->impact = 40u; /* the spearhead's                */
				}
			}
			if (!f->clashed) {
				f->clashed = true;
				r->clash = true;
			}
			uint32_t hit = companies_of(a->hp) * (uint32_t)ATK[a->cls];
			hit = hit * c->out_pct[s] / 100u * luck(c->roll, s, i) / 100u;
			if (a->impact > 0u) hit = hit * (a->cls == CAV ? 150u : 130u) / 100u;
			/* behind = on the side the target turns its back to       */
			const int32_t bdir = (o == 0u) ? 1 : -1;
			bool rear = (a->x - b->x) * bdir < -PX(4);
			bool flank = !rear && a->side > 0u;
			if (o == 0u && (rear || flank)) {
				if (c->ward) {
					rear = flank = false;
				} else if (c->form == SIM_FORM_CIRCLE) {
					r->blunted++;     /* the ring has no sides         */
					rear = flank = false;
				}
			}
			uint32_t pct = 100u;
			const bool phalanx = o == 0u && c->form == SIM_FORM_PHALANX;
			if (rear) {
				pct = phalanx ? 250u : 200u;
			} else if (flank) {
				pct = phalanx ? 200u : 150u;
			} else if (phalanx) {
				pct = 70u;            /* the shield wall               */
			}
			if (rear || flank) {
				a->flags |= SIM_DIV_FLANKING;
				b->flags |= SIM_DIV_FLANKED;
			}
			hit = hit * pct / 100u;
			dmg[o][t] += hit * c->in_pct[o] / 100u;
		}
	}
	/* every blow lands at once: no division acts on a stale field      */
	for (uint8_t s = 0; s < 2u; ++s) {
		for (uint8_t i = 0; i < f->n[s]; ++i) {
			field_div_t *d = &f->d[s][i];
			if (!alive(d)) {
				continue;
			}
			if (d->flags & SIM_DIV_FLANKED) {
				r->flanked[s]++;
			}
			if (dmg[s][i] == 0u) {
				continue;
			}
			ops++;
			const uint16_t before = companies_of(d->hp);
			d->hp = (dmg[s][i] >= d->hp) ? 0u : d->hp - dmg[s][i];
			if (d->state != SIM_DIV_ROUT) {
				r->lost[s] = (uint16_t)(r->lost[s] + before - companies_of(d->hp));
			}
			if (d->hp == 0u) {
				d->state = SIM_DIV_DEAD;
			}
		}
	}
}

/* the strongest standing division takes the blow, a company at a time */
uint16_t field_strike(field_t *f, uint8_t side, uint16_t companies) {
	uint16_t done = 0u;
	side &= 1u;
	while (done < companies) {
		field_div_t *big = NULL;
		for (uint8_t i = 0; i < f->n[side]; ++i) {
			field_div_t *d = &f->d[side][i];
			if (alive(d) && d->state != SIM_DIV_ROUT && (big == NULL || d->hp > big->hp)) {
				big = d;
			}
		}
		if (big == NULL) {
			break;
		}
		big->hp = (big->hp > FIELD_COMPANY_HP) ? big->hp - FIELD_COMPANY_HP : 0u;
		if (big->hp == 0u) {
			big->state = SIM_DIV_DEAD;
		}
		done++;
	}
	return done;
}

/* the weakest standing division is made whole again, up to its muster */
uint16_t field_raise(field_t *f, uint8_t side, uint16_t companies) {
	uint16_t done = 0u;
	side &= 1u;
	while (done < companies) {
		field_div_t *low = NULL;
		for (uint8_t i = 0; i < f->n[side]; ++i) {
			field_div_t *d = &f->d[side][i];
			if (alive(d) && d->state != SIM_DIV_ROUT &&
			    d->hp + FIELD_COMPANY_HP <= d->hp0 && (low == NULL || d->hp < low->hp)) {
				low = d;
			}
		}
		if (low == NULL) {
			break;
		}
		low->hp += FIELD_COMPANY_HP;
		done++;
	}
	return done;
}

uint16_t field_rout_weakest(field_t *f, uint8_t side) {
	field_div_t *low = NULL;
	side &= 1u;
	for (uint8_t i = 0; i < f->n[side]; ++i) {
		field_div_t *d = &f->d[side][i];
		if (alive(d) && d->state != SIM_DIV_ROUT && (low == NULL || d->hp < low->hp)) {
			low = d;
		}
	}
	if (low == NULL) {
		return 0u;
	}
	const uint16_t n = companies_of(low->hp);
	low->hp = 0u;
	low->state = SIM_DIV_DEAD;
	return n;
}

void field_view(const field_t *f, uint8_t side, sim_division_t *out) {
	side &= 1u;
	for (uint8_t i = 0; i < f->n[side]; ++i) {
		const field_div_t *d = &f->d[side][i];
		const uint16_t now = companies_of(d->hp);
		const uint16_t start = companies_of(d->hp0);
		out[i].x = (int16_t)(d->x / FIELD_SUB);
		out[i].lane = d->lane;
		out[i].cls = d->cls;
		out[i].companies = (uint8_t)(now > 255u ? 255u : now);
		out[i].companies_start = (uint8_t)(start > 255u ? 255u : start);
		out[i].state = alive(d) ? d->state : (uint8_t)SIM_DIV_DEAD;
		out[i].flags = d->flags;
	}
}
