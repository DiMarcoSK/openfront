/* diag.h - loop-internal seam for diag.c (module: loop).
 * Must stay additive-free against contracts/queue.h, which carries the
 * public diag API; this header only shares the internal state shape.
 */
#ifndef WAR_TABLE_LOOP_DIAG_H
#define WAR_TABLE_LOOP_DIAG_H

#include "contracts/queue.h"

/* diag_init(): zero the live keys, the window and the overlay state.    */
void diag_init(void);

/* diag_add_missed(): loop records a missed 10 Hz tick (wall-clock
 * overrun) here so both sources feed the same key.                      */
void diag_add_missed(uint32_t n);

#endif /* WAR_TABLE_LOOP_DIAG_H */
