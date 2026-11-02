/* boot.c - ROM entry point (module: boot).
 * Boots the 8 modules in the fixed order render -> save -> audio -> input
 * -> sim -> ui, then hands control to loop (spec 01 plan section 3).
 */
#include <nds.h>
#include <calico.h>
#include <stdio.h>

#include "contracts/render.h"
#include "contracts/save.h"
#include "contracts/audio.h"
#include "contracts/input.h"
#include "contracts/sim_api.h"
#include "contracts/ui.h"
#include "contracts/loop.h"

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	consoleDemoInit();
	iprintf("WAR TABLE\n");
	iprintf("boot: modules linking\n");

	render_init();
	save_init();
	audio_init();
	input_init();
	sim_init(1u);
	ui_set_screen(UI_SCREEN_TITLE);

	iprintf("boot: entering loop\n");
	loop_start();

	iprintf("boot: done\n");
	return 0;
}
