/* strings.c - the ui's ASCII string table (B27). See strings.h.        */
#include "strings.h"

static const char *const STRINGS[STR_COUNT] = {
	[STR_KEY_SELECT] = "Select",
	[STR_KEY_BACK] = "Back",
	[STR_KEY_TABS] = "Tabs",
	[STR_KEY_MENU] = "Menu",
	[STR_KEY_PICK] = "Pick",
	[STR_KEY_ATTACK] = "March",
	[STR_KEY_SWAP] = "Swap",

	[STR_ACT_ATTACK] = "Attack",
	[STR_ACT_RETAKE] = "Retake",
	[STR_ACT_TAKEN] = "Taken",

	[STR_TAB_RECRUIT] = "Recruit",
	[STR_TAB_FORMATION] = "Formation",
	[STR_BASE_TO_MAP] = "Map",
	[STR_BASE_RECRUIT_TITLE] = "Recruit companies",
	[STR_BASE_SWAP_HELP] = "A on a slot, A on another: swap.",
	[STR_BASE_KIND_HELP] = "A on the formation: next kind.",

	[STR_NEXT] = "NEXT",
	[STR_NEXT_PICK_FRONT] = "Pick a front, press A to attack.",
	[STR_NEXT_NEED_GOLD] = "Gold grows +2 a day. Wait.",
	[STR_NEXT_DEFEND] = "A front is attacked: Defend it!",
	[STR_NEXT_RECRUIT] = "Recruit 5 companies (A).",
	[STR_NEXT_MUSTER] = "The muster is training them.",
	[STR_NEXT_DEPLOY] = "Ready: Deploy to the front.",
	[STR_NEXT_BACK_TO_MAP] = "B: back to the map.",

	[STR_TIP_BATTLE] = "D-pad/tap: stance. A: pause.",
	[STR_FALLEN] = "Fallen",
};

const char *str(str_id_t id) {
	return (id < STR_COUNT && STRINGS[id] != 0) ? STRINGS[id] : "";
}
