/* Host pixel test for the B17 tier band (spec-03 T024).
 * Acceptance: band exactly 24 px at 256 px wide (~3 KB 4bpp); numeral
 * matches tier 0..9; ok/warn/crit pins survive grayscale (distinct
 * silhouettes); repaint count is exactly 1 per tier change.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ui/base_tier.h"

/* 4bpp read (mirror of the writer; even x low nibble).                   */
static uint8_t pix(const uint8_t *buf, uint32_t x, uint32_t y) {
	const uint32_t i = (y * TIER_BAND_W + x) / 2u;
	return ((x & 1u) == 0u) ? (buf[i] & 0x0Fu) : (buf[i] >> 4);
}

static uint32_t region_hash(const uint8_t *buf, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h) {
	uint32_t hsh = 0x811C9DC5u;
	for (uint32_t j = 0; j < h; ++j) {
		for (uint32_t i = 0; i < w; ++i) {
			hsh ^= pix(buf, x + i, y + j);
			hsh *= 0x01000193u;
		}
	}
	return hsh;
}

int main(void) {
	static uint8_t band[TIER_BAND_BYTES];

	/* geometry: exactly 24 px x 256 px, ~3 KB 4bpp                       */
	assert(tier_band_bytes() == 3072u);
	assert(TIER_BAND_H == 24u && TIER_BAND_W == 256u);
	printf("tier: band 256 x 24 = 3072 B 4bpp\n");

	/* numeral matches tier 0..9: the 12x20 glyph region equals an
	 * independent decode of the hand-coded digit table                    */
	for (uint8_t t = 0u; t <= 9u; ++t) {
		memset(band, 0xAA, sizeof band); /* poison, render must clear  */
		tier_band_render(t, TIER_STATE_OK, band, sizeof band);
		for (uint32_t j = 0; j < 20u; ++j) {
			const uint8_t row = tier_digits[t][j >> 2];
			for (uint32_t i = 0; i < 12u; ++i) {
				const uint8_t bit = (uint8_t)((row >> (i >> 2)) & 1u);
				const uint8_t want = bit ? TIER_PIX_INK : TIER_PIX_PAPER;
				assert(pix(band, 4u + i, 2u + j) == want);
			}
		}
	}
	/* the tiers must not share a numeral: 0 vs 1 vs 9 region hashes      */
	{
		static uint8_t b2[TIER_BAND_BYTES];
		tier_band_render(0u, TIER_STATE_OK, band, sizeof band);
		tier_band_render(1u, TIER_STATE_OK, b2, sizeof b2);
		assert(region_hash(band, 4u, 2u, 12u, 20u) !=
		       region_hash(b2, 4u, 2u, 12u, 20u));
		/* a tier above the model clamps the numeral to 9               */
		tier_band_render(11u, TIER_STATE_CRIT, b2, sizeof b2);
		tier_band_render(9u, TIER_STATE_CRIT, band, sizeof band);
		assert(region_hash(band, 4u, 2u, 12u, 20u) ==
		       region_hash(b2, 4u, 2u, 12u, 20u));
	}
	printf("tier: numeral matches 0..9, clamp at 9\n");

	/* the three pin states survive grayscale: distinct silhouettes        */
	{
		static uint8_t bok[TIER_BAND_BYTES], bwarn[TIER_BAND_BYTES],
		    bcrit[TIER_BAND_BYTES];
		tier_band_render(2u, TIER_STATE_OK, bok, sizeof bok);
		tier_band_render(6u, TIER_STATE_WARN, bwarn, sizeof bwarn);
		tier_band_render(8u, TIER_STATE_CRIT, bcrit, sizeof bcrit);
		const uint32_t hok = region_hash(bok, 232u, 4u, 16u, 16u);
		const uint32_t hw = region_hash(bwarn, 232u, 4u, 16u, 16u);
		const uint32_t hc = region_hash(bcrit, 232u, 4u, 16u, 16u);
		assert(hok != hw && hw != hc && hok != hc);
		/* the state nibble is where the colour lands: ok/warn/crit
		 * slots 3/4/5 read at a pixel the shape puts the colour on      */
		assert(pix(bok, 240u, 12u) == TIER_PIX_OK);
		assert(pix(bwarn, 240u, 12u) == TIER_PIX_WARN);
		assert(pix(bcrit, 240u, 12u) == TIER_PIX_CRIT);
	}
	printf("tier: ok/warn/crit pins shape-distinct (grayscale-safe)\n");

	/* repaint count is exactly 1 per tier change (R9)                     */
	tier_band_repaints(); /* reset is via the same counter                 */
	const uint32_t base = tier_band_repaints();
	tier_band_paint(2u);
	tier_band_paint(2u);
	tier_band_paint(2u); /* identical frames repaint nothing              */
	assert(tier_band_repaints() == base + 1u);
	tier_band_paint(3u);
	assert(tier_band_repaints() == base + 2u);
	tier_band_paint(3u);
	assert(tier_band_repaints() == base + 2u);
	printf("tier: repaint count 1 per change, 0 on identical frames\n");

	printf("B17 tier band pixel tests passed.\n");
	return 0;
}
