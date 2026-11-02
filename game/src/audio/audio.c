/* audio.c - the thin facade around LibXM7 / raw ADPCM (module: audio).
 * License bar: maxmod is excluded (ADR-0003, R9); LibXM7 ships via
 * BlocksDS inside this module only; every other module uses audio.h.
 * B01 scaffold: no mixer yet.
 */
#include <nds.h>
#include <calico.h>

#include "contracts/audio.h"

void audio_init(void) {
	/* No mixer channels yet; boot timing honours spec-01 R8 (300 ms cap). */
}

void audio_cue(audio_cue_t cue) {
	(void)cue;
}

void audio_boot(void) {
}
