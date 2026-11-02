/* crc32.c - CRC-32 over header + payload (ADR-0002).
 * 256-entry table, 1 KB ROM; the 4 B CRC replaces the 2 B checksum so a
 * burst error on a clone cart is caught instead of silently copied.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

static uint32_t crc_table[256];
static bool crc_ready;

static void crc_build(void) {
	if (crc_ready) {
		return;
	}
	for (uint32_t i = 0; i < 256u; ++i) {
		uint32_t c = i;
		for (int k = 0; k < 8; ++k) {
			c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
		}
		crc_table[i] = c;
	}
	crc_ready = true;
}

uint32_t crc32_calc(uint32_t seed, const uint8_t *buf, uint32_t len) {
	crc_build();
	uint32_t c = seed ^ 0xFFFFFFFFu;
	for (uint32_t i = 0; i < len; ++i) {
		c = crc_table[(c ^ buf[i]) & 0xFFu] ^ (c >> 8);
	}
	return c ^ 0xFFFFFFFFu;
}
