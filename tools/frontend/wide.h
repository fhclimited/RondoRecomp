/* Widescreen for Rondo of Blood: the VDC draws extra columns on both sides of the
 * 256-pixel picture (vdc.wide), and small changes to the game make it fill them:
 * background columns and rows are streamed further out, and objects are spawned, kept
 * moving, kept alive and drawn further out. No SDL here, so the headless tool uses it too.
 *
 * Every change is a byte patch checked against the original bytes first (the game loads
 * code from the disc as it goes). Patches are put in or taken out only at safe points
 * (outside gameplay, or while the game changes rooms), so a room is never half drawn the
 * old way. A room is shown wide when every patch is in place and it is wider than the
 * screen (or its scenery scrolls sideways, like the Stage 0 carriage chase); a room one
 * screen wide (vertical shafts, boss rooms) keeps the original 4:3 picture.
 * See docs/findings.md, "Widescreen". */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pce/pce.h"

/* Largest extension per side the game changes support (pixels). 21:9 needs about 100. */
#define WIDE_MAX 112

typedef struct Wide {
    int want;              /* requested extension per side, pixels (0 = off) */
    /* results of the last wide_frame(), for the frame that follows it */
    int active;            /* extension drawn (0 = original 4:3 picture) */
    bool patched;          /* every game change is in place */
    /* bookkeeping */
    uint8_t layer_x[8];    /* scroll layers' X, to notice scenery that scrolls */
    uint64_t move_history; /* bit per recent frame: some scenery layer moved */
    int scenery_moving;    /* how many of the last 60 frames it did */
    int hud_x;             /* where the HUD was moved to (8 = original) */
    int subweapon_x;       /* where the sub-weapon icon was moved to (28 = original) */
    /* rows of the picture: X scroll last frame, frames it has not changed, how a band
     * holding a parked object is cut off (VDC_CLIP_*; see wide_frame) */
    uint16_t row_x[PCE_FB_MAX_H];
    uint8_t row_still[PCE_FB_MAX_H];
    uint8_t row_clip[PCE_FB_MAX_H];
    /* objects: screen X last frame, frames it has not changed */
    int16_t obj_x[0x58];
    uint8_t obj_still[0x58];
} Wide;

/* frames a band's scroll must stay put to count as parked (the sky moves every few frames,
 * the carriage band not at all while it waits) */
#define PARKED_FRAMES 20

/* Pixels per side that fill a window of aspect window_aspect (width / height) when the
 * game's 256-pixel picture is shown at picture_aspect (4:3, or 256:224 with square
 * pixels); rounded down to even, capped at WIDE_MAX. */
int wide_extent(double window_aspect, double picture_aspect);

/* Call before every emulated frame. Puts game changes in or takes them out (at safe
 * points), and sets p->vdc.wide and the room mask for the frame about to run. */
void wide_frame(Wide *w, Pce *p);

/* Take every game change out now (e.g. before a state is saved for a build without
 * widescreen). Leaves the next frame at 4:3. */
void wide_off(Wide *w, Pce *p);

/* Diagnostics: one line per game change (bank:offset, in / out / other code). */
int wide_report(Pce *p, char *out, int n);

/* Diagnostics: bit i set = leave patch i out (rondo_headless --wide-skip). */
extern uint32_t wide_debug_skip;
