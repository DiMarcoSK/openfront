/* overlay.h - the L+R+START diagnostics overlay (module: ui, intra).
 * Page 1 = the six counters fed by the 10 s diag ring; page 2 = the SLO
 * table with a probe, threshold and owner per line (SDD-7, B14). Drawn
 * last so it wins the console rows; no network syscalls behind it.       */
#ifndef WAR_TABLE_UI_OVERLAY_H
#define WAR_TABLE_UI_OVERLAY_H

/* overlay_frame(): draw the current overlay page; no-op when closed.    */
void overlay_frame(void);

#endif /* WAR_TABLE_UI_OVERLAY_H */
