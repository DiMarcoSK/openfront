/* crc32.h - private to the save module. */
#ifndef WAR_TABLE_SAVE_CRC32_H
#define WAR_TABLE_SAVE_CRC32_H

#include <stdint.h>

uint32_t crc32_calc(uint32_t seed, const uint8_t *buf, uint32_t len);

#endif
