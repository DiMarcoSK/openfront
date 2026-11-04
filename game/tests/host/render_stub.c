/* render_stub.c - host stand-in for the render module and the ui layout
 * seam. The ui screens paint the art through contracts/render.h; on the
 * host the paint is a no-op and the tests keep reading the copy channel
 * (ui_draw_text, spied per test). Counters let a test prove a paint ran.
 */
#include <stdint.h>
#include <string.h>

#include "contracts/render.h"
#include "ui_draw.h"

uint32_t render_stub_backdrop[2];
uint32_t render_stub_sprites;
uint32_t render_stub_god_frames_drawn;

void ui_draw_layout(const ui_row_t *rows, uint8_t n) { (void)rows; (void)n; }
static uint32_t anim;
uint32_t ui_anim_step(void) { return anim / 8u; }
void ui_anim_tick(void) { anim++; }

void render_vblank(void) {}
void render_begin(void) {}
void render_backdrop(render_screen_t s, uint8_t backdrop, uint8_t god) {
	(void)god;
	render_stub_backdrop[s & 1] = backdrop;
}
void render_camera(int x, int y) { (void)x; (void)y; }
void render_flash(uint8_t level) { (void)level; }
int render_text_width(const char *t) {
	const int n = (int)strlen(t);
	return n ? n * 6 - 1 : 0;
}
int render_text(render_screen_t s, int x, int y, uint8_t c, const char *t) {
	(void)s; (void)y; (void)c;
	return x + (int)strlen(t) * 6;
}
int render_text_wrap(render_screen_t s, int x, int y, int w, int lines,
                     uint8_t c, const char *t) {
	(void)s; (void)x; (void)y; (void)c;
	const int cols = w / 6 ? w / 6 : 1;
	const int n = ((int)strlen(t) + cols - 1) / cols;
	return n < lines ? n : lines;
}
int render_text_big(render_screen_t s, int x, int y, uint8_t c, const char *t) {
	(void)s; (void)y; (void)c;
	return x + (int)strlen(t) * 12;
}
void render_rect(render_screen_t s, int x, int y, int w, int h, uint8_t c) {
	(void)s; (void)x; (void)y; (void)w; (void)h; (void)c;
}
void render_box(render_screen_t s, int x, int y, int w, int h, uint8_t c) {
	(void)s; (void)x; (void)y; (void)w; (void)h; (void)c;
}
void render_ring(render_screen_t s, int x, int y, int w, int h) {
	(void)s; (void)x; (void)y; (void)w; (void)h;
}
void render_panel(render_screen_t s, int x, int y, int w, int h, uint8_t k) {
	(void)s; (void)x; (void)y; (void)w; (void)h; (void)k;
}
void render_button(render_screen_t s, int x, int y, int w, int h,
                   const char *l, uint8_t st) {
	(void)s; (void)x; (void)y; (void)w; (void)h; (void)l; (void)st;
}
void render_bar(render_screen_t s, int x, int y, int w, uint32_t n,
                uint32_t d, uint8_t k) {
	(void)s; (void)x; (void)y; (void)w; (void)n; (void)d; (void)k;
}
void render_squad(uint8_t civ, uint8_t cls, uint8_t st, uint8_t anim_,
                  uint32_t step, int x, int y, uint8_t f) {
	(void)civ; (void)cls; (void)st; (void)anim_; (void)step; (void)x;
	(void)y; (void)f;
	render_stub_sprites++;
}
void render_portrait(render_screen_t s, uint8_t civ, uint8_t cls,
                     uint8_t anim_, uint32_t step, int x, int y, uint8_t f) {
	(void)s; (void)civ; (void)cls; (void)anim_; (void)step; (void)x;
	(void)y; (void)f;
	render_stub_sprites++;
}
void render_icon(render_screen_t s, uint8_t i, int x, int y, uint8_t f) {
	(void)s; (void)i; (void)x; (void)y; (void)f;
	render_stub_sprites++;
}
void render_pin(render_screen_t s, uint8_t t, int x, int y, uint8_t f) {
	(void)s; (void)t; (void)x; (void)y; (void)f;
	render_stub_sprites++;
}
void render_relic(render_screen_t s, uint8_t g, int x, int y, uint8_t f) {
	(void)s; (void)g; (void)x; (void)y; (void)f;
	render_stub_sprites++;
}
void render_god(uint8_t g, uint8_t a, uint32_t step, int x, int y, uint8_t f) {
	(void)g; (void)a; (void)step; (void)x; (void)y; (void)f;
	render_stub_god_frames_drawn++;
}
uint8_t render_god_frames(uint8_t a) {
	static const uint8_t N[6] = {4, 6, 6, 6, 2, 6};
	return N[a % 6u];
}
void render_fx(uint8_t g, uint8_t fr, int x, int y, uint8_t f) {
	(void)g; (void)fr; (void)x; (void)y; (void)f;
}
uint32_t render_overlay_hash(render_screen_t s) { (void)s; return 0u; }
