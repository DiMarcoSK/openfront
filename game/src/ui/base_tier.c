/* base_tier.c - the 24 px tier band pixel model (module: ui; B17).
 * Exactly 24 px tall, 256 px wide, 4bpp: ~3 KB (3072 B). The numeral is
 * a 3x5 glyph scaled 4x (12x20) at the left; the status pin is a
 * 16x16 badge whose SHAPE encodes ok/warn/crit so the fixture survives
 * grayscale (info is never colour-alone - constitution 7). Repaints
 * are counted: one paint per tier change, 0 on identical frames.
 */
#include <string.h>

#include "base_tier.h"
#include "ui_draw.h" /* the ROM keeps a text row beside the model now */

const uint8_t tier_digits[10][5] = {
	{ 0x7u, 0x5u, 0x5u, 0x5u, 0x7u }, /* 0 */
	{ 0x3u, 0x1u, 0x1u, 0x1u, 0x1u }, /* 1 */
	{ 0x7u, 0x1u, 0x7u, 0x4u, 0x7u }, /* 2 */
	{ 0x7u, 0x1u, 0x7u, 0x1u, 0x7u }, /* 3 */
	{ 0x5u, 0x5u, 0x7u, 0x1u, 0x1u }, /* 4 */
	{ 0x7u, 0x4u, 0x7u, 0x1u, 0x7u }, /* 5 */
	{ 0x7u, 0x4u, 0x7u, 0x5u, 0x7u }, /* 6 */
	{ 0x7u, 0x1u, 0x1u, 0x1u, 0x1u }, /* 7 */
	{ 0x7u, 0x5u, 0x7u, 0x5u, 0x7u }, /* 8 */
	{ 0x7u, 0x5u, 0x7u, 0x1u, 0x7u }, /* 9 */
};

static uint8_t last_tier = 0xFFu;
static uint32_t repaint_count;

tier_band_state_t tier_state_for(uint8_t tier) {
	if (tier >= 8u) {
		return TIER_STATE_CRIT;
	}
	if (tier >= 5u) {
		return TIER_STATE_WARN;
	}
	return TIER_STATE_OK;
}

/* pixel(): 4bpp write, even x -> low nibble, odd x -> high nibble.       */
static void pixel(uint8_t *out, uint32_t x, uint32_t y, uint8_t col) {
	uint8_t *b = &out[(y * TIER_BAND_W + x) / 2u];
	if ((x & 1u) == 0u) {
		*b = (uint8_t)((*b & 0xF0u) | (col & 0x0Fu));
	} else {
		*b = (uint8_t)((*b & 0x0Fu) | ((col & 0x0Fu) << 4));
	}
}

static void rect_fill(uint8_t *out, uint32_t x, uint32_t y, uint32_t w,
                      uint32_t h, uint8_t col) {
	for (uint32_t j = 0; j < h; ++j) {
		for (uint32_t i = 0; i < w; ++i) {
			pixel(out, x + i, y + j, col);
		}
	}
}

/* glyph(): the 3x5 digit at scale 4 -> 12x20 block from (x, y).          */
static void glyph(uint8_t *out, uint8_t d, uint32_t x, uint32_t y) {
	const uint8_t *g = tier_digits[d];
	for (uint32_t j = 0; j < 5u * 4u; ++j) {
		const uint8_t row = g[j >> 2];
		for (uint32_t i = 0; i < 3u * 4u; ++i) {
			const uint8_t bit = (uint8_t)(row >> (i >> 2)) & 1u;
			pixel(out, x + i, y + j, bit ? TIER_PIX_INK : TIER_PIX_PAPER);
		}
	}
}

void tier_band_render(uint8_t tier, tier_band_state_t state,
                      uint8_t *out, uint32_t cap) {
	if (out == NULL || cap < TIER_BAND_BYTES) {
		return;
	}
	memset(out, 0, TIER_BAND_BYTES); /* paper everywhere                */
	rect_fill(out, 0u, 0u, TIER_BAND_W, 1u, TIER_PIX_INK2);
	rect_fill(out, 0u, TIER_BAND_H - 1u, TIER_BAND_W, 1u, TIER_PIX_INK2);
	/* numeral: every tier digit 0..9 + "FORTRESS (max)" clamps to 9      */
	if (tier > TIER_BASE_MAX) {
		tier = (uint8_t)TIER_BASE_MAX;
	}
	glyph(out, tier, 4u, 2u);
	/* status pin (16x16 at x 232): outline + state SHAPE (grayscale-safe)*/
	rect_fill(out, 232u, 4u, 16u, 16u, TIER_PIX_INK);
	rect_fill(out, 233u, 5u, 14u, 14u, TIER_PIX_PAPER);
	const uint8_t status_col = (state == TIER_STATE_OK)
	    ? TIER_PIX_OK
	    : (state == TIER_STATE_WARN) ? TIER_PIX_WARN : TIER_PIX_CRIT;
	if (state == TIER_STATE_OK) {
		rect_fill(out, 235u, 7u, 10u, 10u, status_col); /* solid    */
	} else if (state == TIER_STATE_WARN) {
		/* dash: a horizontal bar leaves a clear silhouette             */
		rect_fill(out, 235u, 11u, 10u, 2u, status_col);
	} else {
		/* cross: two diagonals read even in pure luminance            */
		for (uint32_t i = 0u; i < 10u; ++i) {
			pixel(out, 235u + i, 7u + i, status_col);
			pixel(out, 244u - i, 7u + i, status_col);
		}
	}
}

void tier_band_paint(uint8_t tier) {
	if (tier > TIER_BASE_MAX) {
		tier = (uint8_t)TIER_BASE_MAX;
	}
	if (tier != last_tier) {
		/* B17 R9: the band repaints exactly once per change; the
		 * blit lands with B27/B31 - here the counter is the gate.     */
		repaint_count++;
		last_tier = tier;
	}
}

uint32_t tier_band_repaints(void) {
	return repaint_count;
}

uint32_t tier_band_bytes(void) {
	return TIER_BAND_BYTES;
}
