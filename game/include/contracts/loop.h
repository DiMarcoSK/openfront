/* loop.h v1 - the module that owns the frame cycle.
 * loop owns the VBlank frame, the SCREEN_DETB switches and the ordering
 * between input -> sim -> ui -> save; it pushes no state of its own.
 * Additive only: loop_v2.h + adapter is the breaking-change path.
 */
#ifndef WAR_TABLE_LOOP_H_V1
#define WAR_TABLE_LOOP_H_V1

#include <stdint.h>

#define LOOP_ABI_VERSION 1u

/* loop_init(): seed the frame counter + screen state; run once.           */
void loop_init(void);

/* loop_start(): the ROM's main pmMainLoop, returns when START is hit.     */
void loop_start(void);

#endif /* WAR_TABLE_LOOP_H_V1 */
