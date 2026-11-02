/* title.h - internal to ui (module-private; spec-01 T008).
 * Title state machine: EMPTY / LOADING / READY / PARTIAL (one damaged) /
 * NO_CARD / BOTH_DAMAGED, plus the handoff marker. The title is a menu:
 * it contributes no run state, reads save snapshots and never writes
 * EEPROM (card truth is `contracts/save.h`, ADR-0001, ADR-0002).
 */
#ifndef WAR_TABLE_UI_TITLE_H
#define WAR_TABLE_UI_TITLE_H

#include <stdint.h>

typedef enum title_state {
	TITLE_LOADING = 0,     /* skeleton while the card scan runs          */
	TITLE_READY,           /* 1 or 2 verified slots, both clean          */
	TITLE_PARTIAL,         /* 1 verified slot + 1 damaged                */
	TITLE_EMPTY,           /* blank card, no campaign yet (first boot)   */
	TITLE_NO_CARD,         /* absent or unreadable card, inline, live    */
	TITLE_BOTH_DAMAGED,    /* VERIFY_FAIL on both slots                  */
	TITLE_HANDOFF,         /* confirmed; ui switches to Campaign         */
} title_state_t;

typedef enum title_action {
	TITLE_ACT_NONE = 0,
	TITLE_ACT_CONFIRM_CARD,   /* A / tap 2 on a valid card              */
	TITLE_ACT_NEW_ICON,       /* primary when primary == NEW CAMPAIGN   */
	TITLE_ACT_SETTINGS,       /* START / secondary (routed in B11)      */
} title_action_t;

/* title_init(): enter the title in LOADING; the scan runs inside the
 * first frames of the loop so the skeleton is drawn first (spec-01 R8).  */
void title_init(void);

title_state_t title_state(void);

/* title_entered_slot(): the slot the last confirmed load came from
 * (0..1), -1 when the entry was New Campaign; ui prints the handoff
 * copy on the Campaign header from it.                                   */
int title_entered_slot(void);

/* title_input(): one conduit-delivered event; returns the action the
 * frame resolved (spec-01 T009: 8-entry queue, 1 entry per frame).       */
title_action_t title_input(uint8_t kind, uint32_t value);

/* title_frame(): draw the current state once; called by ui_frame.        */
void title_frame(void);

/* title_default_slot(): ring legend - highest seq with CRC OK, tie ->
 * slot 1 (index 0); -1 when no slot is verified. Never a damaged card.  */
int title_default_slot(void);

/* title_handoff(): load one validated slot into sim with tick 0; returns
 * false for a damaged/out-of-range slot, so Continue can never load it
 * (spec-01 R5). ui owns the actual screen switch.                        */
bool title_handoff(int slot);

#endif /* WAR_TABLE_UI_TITLE_H */
