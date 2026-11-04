/* war.h - the endless campaign (module-private to sim, ADR-0009).
 * war.c owns the map state - which fronts are held, lost or threatened,
 * the relics, the battles won - and the threat clock. sim.c calls it on
 * each day rollover, at deploy and when a sealed outcome is applied.
 */
#ifndef WAR_TABLE_SIM_WAR_H
#define WAR_TABLE_SIM_WAR_H

#include <stdbool.h>
#include <stdint.h>

#include "contracts/save.h"

/* what a deploy on a front fights (resolved once, kept for Retry)       */
typedef struct war_battle {
	uint8_t kind;          /* SIM_WAR_*                                 */
	uint8_t boss;          /* SIM_BOSS_* or SIM_BOSS_NONE               */
	uint8_t terrain;       /* SIM_TERRAIN_*                             */
	bool ambush;           /* the march is struck before it forms       */
	uint16_t foes;         /* companies the foe fields                  */
	uint16_t hold_in_pct;  /* damage taken while holding (siege walls)  */
	uint32_t reward;       /* plunder on victory                        */
} war_battle_t;

void war_reset(uint32_t seed);
void war_ingest(const save_aggregate_t *agg, uint32_t seed);
void war_export(save_aggregate_t *out);
/* one call per day rollover; a battle freezes the threat's deadline    */
void war_new_day(uint16_t day, bool in_battle, uint32_t *treasury);
/* false = nothing to fight there (a held front with no threat)         */
bool war_battle(uint8_t province, uint16_t army, uint32_t seed, war_battle_t *out);
void war_settle(uint8_t province, const war_battle_t *b, bool victory);
uint8_t war_taken(void);

#endif /* WAR_TABLE_SIM_WAR_H */
