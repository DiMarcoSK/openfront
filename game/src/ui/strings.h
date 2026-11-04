/* strings.h - the ui's ASCII string table (B27, module-private to ui).
 * One id per piece of player-facing copy that is not a live number, so a
 * sweep (length, tone, a future language) reads one file. Every entry
 * fits 30 chars or is drawn wrapped. Screens still format live values
 * with snprintf; the words around them come from here. The pre-B27
 * byte-exact spec copy stays where its host tests pin it.
 */
#ifndef WAR_TABLE_UI_STRINGS_H
#define WAR_TABLE_UI_STRINGS_H

typedef enum str_id {
	/* the control legend (guide_legend)                                 */
	STR_KEY_SELECT = 0,   /* "Select"                                    */
	STR_KEY_BACK,
	STR_KEY_TABS,
	STR_KEY_MENU,
	STR_KEY_PICK,
	STR_KEY_ATTACK,
	STR_KEY_SWAP,
	/* campaign row actions                                               */
	STR_ACT_ATTACK,
	STR_ACT_RETAKE,
	STR_ACT_TAKEN,
	/* base                                                              */
	STR_TAB_RECRUIT,
	STR_TAB_FORMATION,
	STR_BASE_TO_MAP,
	STR_BASE_RECRUIT_TITLE,
	STR_BASE_SWAP_HELP,
	STR_BASE_KIND_HELP,
	/* the next step (onboarding: the blank card names what to do)       */
	STR_NEXT,
	STR_NEXT_PICK_FRONT,
	STR_NEXT_NEED_GOLD,
	STR_NEXT_DEFEND,
	STR_NEXT_RECRUIT,
	STR_NEXT_MUSTER,
	STR_NEXT_DEPLOY,
	STR_NEXT_BACK_TO_MAP,
	/* battle: the first seconds of the first battle                     */
	STR_TIP_BATTLE,
	STR_FALLEN,
	STR_COUNT
} str_id_t;

const char *str(str_id_t id);

#endif /* WAR_TABLE_UI_STRINGS_H */
