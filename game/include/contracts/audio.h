/* audio.h v1 - the facade everything goes through.
 * Owner: audio module (src/audio). Behind it: LibXM7 via BlocksDS, or the
 * raw ADPCM fallback. Nothing outside audio/ may see either (ADR-0003,
 * constitution 4).
 * Additive only: audio_v2.h + adapter is the breaking-change path.
 */
#ifndef WAR_TABLE_AUDIO_H_V1
#define WAR_TABLE_AUDIO_H_V1

#include <stdint.h>

#define AUDIO_ABI_VERSION 1u

typedef enum audio_cue {
	AUDIO_CUE_NONE = 0,
	AUDIO_CUE_CONFIRM,
	AUDIO_CUE_ALERT,
	AUDIO_CUE_BATTLE_END,
	AUDIO_CUE_FAILURE,
	AUDIO_CUE_SPEED, /* B20: speed request accept + honest clamp */
} audio_cue_t;

void audio_init(void);
void audio_cue(audio_cue_t cue);
void audio_boot(void);

#endif /* WAR_TABLE_AUDIO_H_V1 */
