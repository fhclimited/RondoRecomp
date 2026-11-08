/* Remaster mode's world: the rooms of a stage laid out side by side, the way the game
 * moves between them, so the picture can show the rooms next door around the room being
 * played, room changes can glide instead of cut, and a map can show where everything is.
 *
 * The game keeps each room as a grid of up to 16 x 16 screens ("pages") of 256 x 256
 * pixels, and goes from room to room by its own stage code (an edge of the screen, a
 * stair, a door), with no shared map: so a room is placed against the one the player left
 * by where they left it (an edge of the screen: the rooms meet there; anywhere else, a
 * door: the player stands at the same place in both). The first placement of a room
 * holds; placements are saved per stage, with the pages the player has seen (the map).
 *
 * Each room has two pictures of its background, in 256 x 256 tiles:
 *  - seen: what the camera has shown (each frame's background without its sprites);
 *  - data: the room drawn from the game's own data for the rows of pages its camera can
 *    reach, for the parts not seen yet (with the tiles and colours in video memory, so
 *    only while the room is being played; kept afterwards).
 * Both are saved with the map (a file per stage, loaded when the stage starts), so rooms
 * seen in earlier sessions are drawn in from the start.
 *
 * No SDL here, so the headless tool tests it. See docs/findings.md, "Rooms". */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pce/pce.h"

#define WORLD_PAGE 256           /* pixels per page, both ways */
#define WORLD_ROOMS 128          /* rooms remembered (all stages) */

/* A 256 x 256 tile of a room's picture (ARGB; alpha 0 = nothing there). `tex` belongs to
 * the frontend (a texture of it, uploaded when `gen` moved on from `tex_gen`). */
typedef struct WorldTile {
    uint32_t px[WORLD_PAGE * WORLD_PAGE];
    uint32_t gen, tex_gen;
    void *tex;
} WorldTile;

typedef struct WorldRoom {
    uint8_t area, room;          /* $A2 without the character bit, $82 */
    bool placed;
    int ox, oy;                  /* where the room's pixel (0, 0) is in the stage, pixels */
    uint16_t seen[16];           /* map: bit x of seen[y]: page (x, y) has been on screen */
    uint16_t known[16];          /* pages drawn from the room's data */
    WorldTile *pic[256];         /* seen, by page (y * 16 + x) */
    WorldTile *data[256];        /* drawn from the data */
} WorldRoom;

typedef struct World {
    char path[1024];             /* the saved map ("" = not saved) */
    WorldRoom rooms[WORLD_ROOMS];
    int n;
    int cur;                     /* room being played, -1 = none */
    uint8_t area;                /* its area, 0xFF = none */
    /* the frame being run (world_before_frame): camera in room pixels; whether its picture
     * is the room being played (state $05, sub-state $01, the room settled) */
    int cam_x, cam_y;
    bool live;
    bool same;                   /* the frame starts in the current room (state $05) */
    bool shown;                  /* after it: the frame run is a picture of the settled room */
    int steady;                  /* frames in a row the current room has been playing */
    bool settled;                /* the current room has settled (camera and picture count) */
    /* the last settled frame of the room just left, for placing the next one */
    int from;                    /* room left (-1 = none) */
    int last_room;               /* the room last_* below belong to (-1 = none) */
    int from_cam_x, from_cam_y, from_px, from_py;
    int last_cam_x, last_cam_y, last_px, last_py;
    /* drawing from the data: the camera limits it was done for, frames since */
    uint8_t data_limits[4];
    int data_age;
    bool data_ok;                /* the game's room-drawing code was recognised */
    /* the view: world pixels of the top left of the original 256 pixels, following the
     * camera and gliding across room changes */
    float view_x, view_y;
    bool view_set;
    bool dirty;                  /* placements or seen pages changed since saved */
    bool pics_dirty;             /* the current stage's pictures changed since saved */
    void (*free_tex)(void *tex); /* frontend: frees a tile's texture */
    /* diagnostics */
    char last_event[160];
} World;

void world_init(World *w, const char *map_path);
void world_free(World *w);
/* Forget the room being played (a state loaded, the game reset): the next frame starts
 * over without placing anything. */
void world_reset_play(World *w);
/* Call before every emulated frame (reads the camera the frame will show). */
void world_before_frame(World *w, const Pce *p);
/* Call after it: picks up the frame's background, places rooms, draws from the data. */
void world_after_frame(World *w, const Pce *p);
/* Step the view towards the camera (once per frame shown). */
void world_step_view(World *w);
/* Save the map, and the current stage's pictures (map_XX.pic beside it), if they changed. */
void world_save(World *w);

/* The room being played, or NULL. */
static inline WorldRoom *world_room(World *w) { return w->cur >= 0 ? &w->rooms[w->cur] : NULL; }

/* Player position in the world (pixels), if a placed room is being played. */
bool world_player(const World *w, const Pce *p, float *x, float *y);

/* Diagnostics (headless): a room's pictures as one image (seen over data), and a stage's
 * placed rooms as one image; ARGB, *iw x *ih, to free(). NULL if there is nothing. */
uint32_t *world_room_image(const World *w, int room_index, int *iw, int *ih);
uint32_t *world_stage_image(const World *w, uint8_t area, int *iw, int *ih);
