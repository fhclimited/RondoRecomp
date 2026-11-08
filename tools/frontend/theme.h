/* The look of the frontend's own screens (the start screen, the menu, notices): gothic, in
 * Castlevania's colours, laid out after the Metroid: Other M port's menus. Panels are dark
 * burgundy frames with a pointed-arch top, double gold borders, pinnacles and a cross finial;
 * dividers carry diamonds and a cross; the emblem is a rose window; the start screen is a
 * moonlit castle skyline with bats. Text is drawn with stb_truetype in fonts built into
 * rondo.exe (ui_fonts.c, SIL OFL): Cinzel for menu text, Cinzel Decorative for capitals,
 * UnifrakturMaguntia (blackletter) for titles; Windows' fonts, then SDL's debug font, if
 * one cannot be read. Everything draws in render-output pixels. */
#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

enum { TH_FONT_UI, TH_FONT_TITLE, TH_FONT_CAPS, TH_FONT_COUNT };

#define TH_GOLD ((SDL_Color){230, 184, 92, 255})
#define TH_CRIMSON ((SDL_Color){222, 44, 58, 255})
#define TH_TEXT ((SDL_Color){246, 236, 218, 255})
#define TH_TEXT_DIM ((SDL_Color){184, 162, 140, 255})
#define TH_ACTION ((SDL_Color){255, 214, 150, 255})
#define TH_PANEL_TOP ((SDL_Color){42, 11, 20, 240})
#define TH_PANEL_BOTTOM ((SDL_Color){12, 3, 8, 244})
#define TH_NONE ((SDL_Color){0, 0, 0, 0})

/* `c` with its alpha replaced (0-255) or scaled (0-1). */
SDL_Color th_alpha(SDL_Color c, int a);
SDL_Color th_fade(SDL_Color c, float f);

/* Size of `text` at `px` pixels (the font's height) with `tracking` extra pixels between
 * letters. */
float th_measure(SDL_Renderer *ren, int font, float px, const char *text, float tracking);
/* `text` placed so that its box's point (ax, ay) (0 = left / top, 0.5 = middle, 1 = right /
 * bottom) is at (x, y); a glow colour with alpha > 0 draws a soft halo behind it. */
void th_text(SDL_Renderer *ren, int font, float px, float x, float y, SDL_Color col, const char *text, float ax,
             float ay, float tracking, SDL_Color glow);
/* The font size (no more than `px`) at which `text` fits in `max_w` pixels. */
float th_fit(SDL_Renderer *ren, int font, float px, const char *text, float max_w, float tracking);

/* Shapes. A convex polygon shaded from `top` (at y0) to `bottom` (at y1). */
void th_poly(SDL_Renderer *ren, const SDL_FPoint *pts, int n, SDL_Color top, SDL_Color bottom, float y0, float y1);
void th_line(SDL_Renderer *ren, float x0, float y0, float x1, float y1, SDL_Color c, float thick);
void th_polyline(SDL_Renderer *ren, const SDL_FPoint *pts, int n, bool closed, SDL_Color c, float thick);
void th_rect(SDL_Renderer *ren, float x0, float y0, float x1, float y1, SDL_Color c);
/* A rectangle shaded left to right (h) or top to bottom (v). */
void th_rect_h(SDL_Renderer *ren, float x0, float y0, float x1, float y1, SDL_Color left, SDL_Color right);
void th_rect_v(SDL_Renderer *ren, float x0, float y0, float x1, float y1, SDL_Color top, SDL_Color bottom);
void th_arc(SDL_Renderer *ren, float cx, float cy, float r, float a0, float a1, SDL_Color c, float thick);
/* A panel with its top-left and bottom-right corners cut by `cut`: a vertical gradient, a
 * `border` line and a faint outer glow. */
void th_panel(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float cut, SDL_Color top, SDL_Color bottom,
              SDL_Color border, float thick);
/* L-shaped brackets just outside the uncut corners (top right, bottom left) of a panel. */
void th_brackets(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float len, SDL_Color c, float thick);
/* Horizontal lines every `step` pixels. */
void th_scanlines(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float step, SDL_Color c);
/* A chevron pointing right (or left), `size` pixels tall, centred on (x, y). */
void th_chevron(SDL_Renderer *ren, float x, float y, float size, bool right, SDL_Color c, float thick);
/* A slanted plate (a parallelogram leaning `slant` pixels). */
void th_plate(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float slant, SDL_Color fill,
              SDL_Color border, float thick);

/* A filled disc shaded top to bottom; a cross whose arms widen towards their ends, centred
 * on its crossing (r: its size), with an optional edge line. */
void th_disc(SDL_Renderer *ren, float cx, float cy, float r, SDL_Color top, SDL_Color bottom);
void th_cross(SDL_Renderer *ren, float cx, float cy, float r, SDL_Color top, SDL_Color bottom, SDL_Color edge,
              float edge_w);
/* A small filled arrowhead pointing right (or left), `size` pixels tall. */
void th_arrow(SDL_Renderer *ren, float x, float y, float size, bool right, SDL_Color c);
/* A gothic panel: x0..x1, its pointed-arch top `rise` pixels high at y0 (the apex), its
 * bottom at y1; dark burgundy, a gold border with an inner line, pinnacles at the arch's
 * feet, a cross on the apex. `u` is the layout unit, `alpha` 0-1. */
void th_frame(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float rise, float u, float alpha);
/* A divider from x0 to x1 at y: gold lines either side of diamonds and a small cross. */
void th_divider(SDL_Renderer *ren, float x0, float x1, float y, float u, float alpha);
/* A plaque with pointed ends (notices). */
void th_plaque(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float u, float alpha);

/* The emblem: a rose window, `r` pixels across to its rim, with a gold cross in stained
 * glass; `t` (seconds) makes the glass glow; `alpha` 0-1. */
void th_emblem(SDL_Renderer *ren, float cx, float cy, float r, float t, float alpha);
/* The start screen's backdrop over the whole w x h output: a night sky, a blood moon, the
 * castle's skyline with lit windows, mist, bats crossing (t: seconds). */
void th_backdrop(SDL_Renderer *ren, int w, int h, float t);

/* Layout unit: 1 at 960 x 540, scaled with the output (the smaller of its width / 16 and
 * height / 9). */
float th_unit(int w, int h);

/* Fonts are loaded on first use; this frees them (with the renderer still alive). */
void th_free(void);
