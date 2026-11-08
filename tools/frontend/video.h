/* Putting the PC Engine picture on any screen: 4:3 / square-pixel / stretched, fit to the
 * screen or whole multiples only, sharp (crisp but even) / nearest / smooth filtering, and
 * a blurred copy of the picture beside it on 16:9 and ultrawide screens. Widescreen
 * pictures (wider than 256 pixels, see wide.h) keep the same pixel shape. Also on-screen
 * text. */
#pragma once

#include <SDL3/SDL.h>
#include <stdint.h>

#include "settings.h"

typedef struct Video {
    SDL_Renderer *ren;
    SDL_Texture *frame;          /* the emulated picture, streaming */
    SDL_Texture *sharp;          /* integer-upscaled copy (FILTER_SHARP) */
    int sharp_w, sharp_h;
    SDL_Texture *blur_a, *blur_b;
    int src_w, src_h;
    int src_wide;                /* extra pixels on each side of a 256-pixel picture */
    /* drawn instead of `frame` when set (remaster mode's canvas, worldview.h) */
    SDL_Texture *source;
    int source_w, source_h, source_wide;
} Video;

bool video_init(Video *v, SDL_Renderer *ren);
void video_free(Video *v);
/* Upload a new picture (ARGB8888, pitch in pixels); `wide` = widescreen extension per side. */
void video_upload(Video *v, const uint32_t *fb, int w, int h, int pitch, int wide);
/* Show `tex` (w x h, `wide` pixels added each side of 256) instead of the uploaded picture
 * from now on; NULL goes back to the picture. */
void video_set_source(Video *v, SDL_Texture *tex, int w, int h, int wide);
/* Width / height of the screen area a 256-pixel-wide picture is shown in (4:3, or square
 * pixels), for working out how much widescreen fills the window. */
double video_picture_aspect(const Video *v, const Settings *s);
/* Clear and draw the picture for the current window size; returns where it went. */
SDL_FRect video_draw(Video *v, const Settings *s);

/* Text with SDL's built-in 8x8 font. scale = pixels per font pixel. */
int ui_scale(SDL_Renderer *ren);
void ui_text(SDL_Renderer *ren, float x, float y, float scale, SDL_Color c, const char *text);
void ui_box(SDL_Renderer *ren, SDL_FRect r, SDL_Color c);
