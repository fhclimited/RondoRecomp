/* Remaster mode on screen (world.h has the rooms): the picture as a wider canvas at the
 * game's resolution, with the rooms around the one being played drawn in under the live
 * picture, which then goes through the usual video options (video.h); and the map, over
 * the paused game. */
#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "world.h"

typedef struct WorldView {
    SDL_Renderer *ren;
    SDL_Texture *canvas;         /* the remaster picture, game pixels */
    int cw, ch;
    SDL_Texture *blur;           /* the map's background: the picture at half size */
    int bw, bh;
} WorldView;

void worldview_init(WorldView *wv, SDL_Renderer *ren);
void worldview_free(WorldView *wv);
/* World.free_tex for tiles' textures. */
void worldview_free_tex(void *tex);

/* Draw the remaster picture: `ext` game pixels each side of the original 256 (as wide as
 * the window's shape needs, more than the live picture's own widescreen if need be), the
 * view and rooms from `w`, the live picture (`frame`, the emulated picture as uploaded:
 * frame_w x frame_h, `frame_wide` pixels added each side, clear beyond the room's walls)
 * where the camera is. Returns the canvas (*w_out x *h_out), or NULL when there is no room
 * to show (then show the picture as usual). */
SDL_Texture *worldview_compose(WorldView *wv, World *w, SDL_Texture *frame, int frame_w, int frame_h,
                               int frame_wide, bool frame_live, int ext, int *w_out, int *h_out);

/* The map over the window: the picture last drawn at `dst` (window pixels) blurred behind
 * it; `t` seconds since it opened (it fades in; the player's dot blinks, lit at first). */
void worldview_draw_map(WorldView *wv, const World *w, const Pce *p, SDL_FRect dst, float t);
