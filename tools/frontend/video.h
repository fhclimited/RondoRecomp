/* Putting the PC Engine picture on any screen: 4:3 / square-pixel / stretched, optional
 * integer scaling, sharp (crisp but even) / nearest / smooth filtering, and a blurred copy
 * of the picture to fill the sides of 16:9 and ultrawide screens. Also on-screen text. */
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
} Video;

bool video_init(Video *v, SDL_Renderer *ren);
void video_free(Video *v);
/* Upload a new picture (ARGB8888, pitch in pixels). */
void video_upload(Video *v, const uint32_t *fb, int w, int h, int pitch);
/* Clear and draw the picture for the current window size; returns where it went. */
SDL_FRect video_draw(Video *v, const Settings *s);

/* Text with SDL's built-in 8x8 font. scale = pixels per font pixel. */
int ui_scale(SDL_Renderer *ren);
void ui_text(SDL_Renderer *ren, float x, float y, float scale, SDL_Color c, const char *text);
void ui_box(SDL_Renderer *ren, SDL_FRect r, SDL_Color c);
