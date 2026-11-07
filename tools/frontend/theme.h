/* The look of the frontend's own screens (the start screen, the menu, notices), after the
 * Metroid: Other M port's visor menus but in Castlevania's colours: dark glass panels with
 * cut corners, gold lines with a soft glow, crimson accents, scan lines; a cross in turning
 * rings for an emblem. Text is drawn in Windows' own fonts (Bahnschrift for the menus,
 * Palatino for titles) with stb_truetype; without them, in SDL's built-in debug font.
 * Everything draws in render-output pixels. */
#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

enum { TH_FONT_UI, TH_FONT_TITLE, TH_FONT_COUNT };

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

/* The emblem: a cross in rings, `r` pixels to the inner ring, turning with time `t`
 * (seconds); `alpha` 0-1. */
void th_emblem(SDL_Renderer *ren, float cx, float cy, float r, float t, float alpha);
/* The start screen's backdrop over the whole w x h output: night-red gradient, a faint grid,
 * scan lines, darkened edges. */
void th_backdrop(SDL_Renderer *ren, int w, int h, float t);

/* Layout unit: 1 at 960 x 540, scaled with the output (the smaller of its width / 16 and
 * height / 9). */
float th_unit(int w, int h);

/* Fonts are loaded on first use; this frees them (with the renderer still alive). */
void th_free(void);
