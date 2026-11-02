/* slots.h - internal to the save module (include-graph private). */
#ifndef WAR_TABLE_SAVE_SLOTS_H
#define WAR_TABLE_SAVE_SLOTS_H

#include "save_backend.h"

bool slot_decode(const uint8_t *image, save_slot_t *out);
bool slot_decode_index(const uint8_t block[512], int idx, save_slot_t *out);
bool slot_info(const uint8_t block[512], int idx, save_slot_info_t *out);
int slot_pick(const uint8_t block[512]);
void slot_commit(uint8_t block[512], int active, const save_aggregate_t *snap,
                 uint32_t seq, uint16_t day);
void slot_write_protocol(uint8_t block[512], int active,
                         const save_aggregate_t *snap, uint32_t seq,
                         uint16_t day, save_status_t *status);
void slot_boot_scan(uint8_t block[512], int *active_out,
                    save_aggregate_t *out, save_status_t *status);

#endif
