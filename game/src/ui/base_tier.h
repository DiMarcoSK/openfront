/* base_tier.h - the 24 px tier band pixel model (module: ui, intra; B17).
 * Geometry is spec-03 R9/T022: exactly 24 px tall, 256 px wide, 4bpp
 * (~3 KB), numeral + pin badge, no backdrop/shadow. The ROM renders
 * console text today; the blit lands with the render-frame art sweep
 * (B27/B31) - the model, glyphs, pin shapes, repaint discipline and the
 * grayscale fixture are tested here.
 */
#ifndef WAR_TABLE_UI_BASE_TIER_H
#define WAR_TABLE_UI_BASE_TIER_H

#include <stdint.h>

#define TIER_BAND_W      256u
#define TIER_BAND_H      24u
#define TIER_BAND_BYTES  ((TIER_BAND_W * TIER_BAND_H) / 2u) /* 3072 = ~3 KB */
#define TIER_BASE_MAX    9u   /* the numeral range 0..9 (numeral clamps)   */

/* 4bpp palette slots (tokens.h maps indices to ink/paper/etc).           */
enum {
	TIER_PIX_PAPER = 0,
	TIER_PIX_INK,
	TIER_PIX_INK2,
	TIER_PIX_OK,
	TIER_PIX_WARN,
	TIER_PIX_CRIT,
};

/* Hand-coded 3x5 digit glyphs, 1 bit per pixel, rows low to high.        */
extern const uint8_t tier_digits[10][5];

/* Numerals 0..9; a tier above 9 cannot exist (sim caps front count).     */
typedef enum tier_band_state {
	TIER_STATE_OK = 0,   /* tier 0..4                                      */
	TIER_STATE_WARN,     /* tier 5..7                                      */
	TIER_STATE_CRIT,     /* tier 8..9                                      */
} tier_band_state_t;

tier_band_state_t tier_state_for(uint8_t tier);

/* tier_band_render(): paint one band frame into out (>= 3072 B):
 * paper field, ink-2 edge rows, the tier numeral at x 4..15, and the
 * 16x16 status pin at x 232..247 - the pin carries its state by SHAPE
 * (solid / dash / cross) so ok/warn/crit survive grayscale.              */
void tier_band_render(uint8_t tier, tier_band_state_t state,
                      uint8_t *out, uint32_t cap);

/* tier_band_paint(): the frame hook used by the Base screen - repaints
 * only when the tier questions the band, counted once per change.        */
void tier_band_paint(uint8_t tier);
uint32_t tier_band_repaints(void);   /* 1 per change (pixel test gate)     */
uint32_t tier_band_bytes(void);

#endif /* WAR_TABLE_UI_BASE_TIER_H */
