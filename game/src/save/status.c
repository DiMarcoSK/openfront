/* status.c - the SaveStatus transition table (B24, spec-06 T018).
 * Every status change in save.c goes through save_status_next(); the
 * host test walks the same table. Rows = current status, columns = the
 * event. NO_CARD keeps play (R5), VERIFY_FAIL keeps both images until an
 * explicit Erase (R6), FULL stops retrying after 2 attempts (R7).
 */
#include <stdint.h>

#include "contracts/save.h"

#define OK SAVE_STATUS_OK
#define NC SAVE_STATUS_NO_CARD
#define VF SAVE_STATUS_VERIFY_FAIL
#define FU SAVE_STATUS_FULL
#define SV SAVE_STATUS_SAVING

static const uint8_t NEXT[5][SAVE_EV_COUNT] = {
	/*        NO_HW VERIF BLANK DAMAG BEGIN C_OK  FAIL  C_NOHW ERASE UNDO */
	/* OK */ {NC,   OK,   OK,   VF,   SV,   OK,   FU,   NC,    OK,   OK},
	/* NC */ {NC,   OK,   OK,   VF,   SV,   OK,   FU,   NC,    NC,   NC},
	/* VF */ {NC,   OK,   OK,   VF,   SV,   OK,   FU,   NC,    OK,   VF},
	/* FU */ {NC,   OK,   OK,   VF,   SV,   OK,   FU,   NC,    OK,   FU},
	/* SV */ {NC,   OK,   OK,   VF,   SV,   OK,   FU,   NC,    SV,   SV},
};

save_status_t save_status_next(save_status_t cur, save_event_t ev) {
	if ((unsigned)cur > (unsigned)SAVE_STATUS_SAVING ||
	    (unsigned)ev >= (unsigned)SAVE_EV_COUNT) {
		return cur;
	}
	return (save_status_t)NEXT[cur][ev];
}
