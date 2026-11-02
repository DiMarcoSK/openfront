/* backend.c - the transport behind save_backend.h (ADR-0002).
 * Primary: the real EEPROM over the SPI (works on every card, size probe
 * gives the actual byte budget). Fallback: one 512 B save.bin on the FAT
 * root when the loader exposes only a DLDI file handle.
 */
#include <nds.h>
#include <calico.h>
#include <fat.h>
#include <stdio.h>

#include "save_backend.h"
#include "contracts/save.h"

static save_backend_kind_t kind = SAVE_BACKEND_NONE;
static FILE *fat_handle;
static uint32_t eeprom_size;

save_backend_kind_t backend_probe(void) {
	if (kind != SAVE_BACKEND_NONE) {
		return kind;
	}
	const int type = cardEepromGetType();
	uint32_t size = cardEepromGetSize();
	eeprom_size = size;

	if (type > 0 && size >= 512u) {
		kind = SAVE_BACKEND_EEPROM;
		return kind;
	}
	/* DLDI / FAT fallback: save.bin in the card root.                       */
	if (fatInitDefault() != 0) {
		fat_handle = fopen("war_table_save.bin", "r+b");
		if (fat_handle == NULL) {
			fat_handle = fopen("war_table_save.bin", "w+b");
		}
		if (fat_handle != NULL) {
			fseek(fat_handle, 0, SEEK_END);
			const long fs = ftell(fat_handle);
			if (fs >= 0 && (uint32_t)fs >= 512u) {
				kind = SAVE_BACKEND_DLDI;
			}
		}
	}
	return kind;
}

void backend_read(uint32_t off, uint8_t *dst, uint32_t len) {
	if (kind == SAVE_BACKEND_EEPROM) {
		cardReadEeprom((u32)off, (u8 *)dst, (u32)len, 1);
		return;
	}
	if (kind == SAVE_BACKEND_DLDI && fat_handle != NULL) {
		fseek(fat_handle, (long)off, SEEK_SET);
		if (fread(dst, 1, (size_t)len, fat_handle) != (size_t)len) {
			memset(dst, 0, len);
		}
	}
}

void backend_write(uint32_t off, const uint8_t *src, uint32_t len) {
	if (kind == SAVE_BACKEND_EEPROM) {
		cardWriteEeprom((u32)off, (u8 *)src, (u32)len, 1);
		return;
	}
	if (kind == SAVE_BACKEND_DLDI && fat_handle != NULL) {
		fseek(fat_handle, (long)off, SEEK_SET);
		fwrite(src, 1, (size_t)len, fat_handle);
		fflush(fat_handle);
	}
}

bool backend_detail_available(void) {
	return kind == SAVE_BACKEND_EEPROM && eeprom_size >= 8192u;
}
