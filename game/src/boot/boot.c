/* boot.c - ROM entry point (module: boot; B01 scaffold + B07 fs boot).
 * Order (spec 01 plan section 3): filesystem up (argv boot / DLDI), then
 * render -> audio -> input -> sim, then the title screen; the save card
 * scan runs inside the first title frames so the Loading skeleton draws
 * first (spec-01 R8: scan capped at 2 x 250 ms, boot p95 <= 2 s:
 * 250 + 250 + 500 load + 300 audio = 1.3 s, margin 0.7 s).
 */
#include <nds.h>
#include <stdio.h>
#include <fat.h>

#include "contracts/render.h"
#include "contracts/save.h"
#include "contracts/audio.h"
#include "contracts/input.h"
#include "contracts/sim_api.h"
#include "contracts/ui.h"
#include "contracts/loop.h"

/* fs_boot(): argv boot (DeSmuME --nds-path and hbmenu hand the ROM path) or
 * the plain DLDI probe. A missing or unreadable card is never fatal: the
 * title degrades inline to NO_CARD and keeps every action live
 * (spec-01 R4). No NitroFS asset is read at boot; the art pipeline is
 * compiled in by grit (ADR-0006), the argv path is recorded per T004.   */
static void fs_boot(int argc, char **argv) {
	const bool have_argv = (argc >= 1 && argv[0] != NULL && argv[0][0] != '\0');
	if (!fatInitDefault()) {
		/* NO_CARD: kept inline, play never blocks (spec-01 R4).       */
		iprintf("t=title ev=fs args=%u fat=0 status=NO_CARD\n",
		        (unsigned)have_argv);
		return;
	}
	iprintf("t=title ev=fs args=%u fat=1\n", (unsigned)have_argv);
}

int main(int argc, char **argv) {
	consoleDemoInit();
	iprintf("WAR TABLE\n");
	iprintf("t=title ev=boot begin\n");

	fs_boot(argc, argv);

	render_init();
	audio_init();
	input_init();

	/* sim seeds the New Campaign defaults (Day 1, treasury 300); a
	 * verified slot overwrites both through sim_ingest_save later.      */
	sim_init(1u);

	ui_set_screen(UI_SCREEN_TITLE); /* enters in LOADING: title_init(). */

	iprintf("t=title ev=boot loop\n");
	loop_start();

	iprintf("t=title ev=boot done\n");
	return 0;
}
