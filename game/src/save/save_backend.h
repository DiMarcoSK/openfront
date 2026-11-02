/* save_backend.h - EEPROM/DLDI transport front (ADR-0002, spec-06 R5/R11).
 * The save module is the only writer. A cardless card is never a blocker
 * for play: the game keeps running and NO_CARD is shown inline.
 * Spec-06 open question 3: hbmenu/AKAIO usually expose only a FAT file
 * handle; the file fallback (save.bin on the card root) is the primary
 * path never used by real hardware.
 */
#ifndef WAR_TABLE_SAVE_BACKEND_H
#define WAR_TABLE_SAVE_BACKEND_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "contracts/save.h"

/* Probe and classify the underlying save chip. Prefer a real EEPROM over
 * a FAT-file rim, because the FAT path depends on the loader's DLDI.       */
save_backend_kind_t backend_probe(void);

/* Raw block-level read/write. Length <= 512 B.                            */
void backend_read(uint32_t off, uint8_t *dst, uint32_t len);
void backend_write(uint32_t off, const uint8_t *src, uint32_t len);

/* 8 KB detail block: additive, only on cards that report >= 8 KB.         */
bool backend_detect_detail(void);
void backend_write_detail(uint32_t off, const uint8_t *src, uint32_t len);
void backend_read_detail(uint32_t off, uint8_t *dst, uint32_t len);

#endif
