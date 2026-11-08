/* The title screen's "PUSH RUN BUTTON!" (RUN is the PC Engine's start button) shown as the
 * button to press on this PC: "PUSH START BUTTON!" with a controller, "PUSH ENTER KEY!" on
 * the keyboard ("PUSH PAUSE BUTTON!" / "KEY!" when the Pause control was changed).
 *
 * The game draws the line as six sprites (palette 7) over the title. Before a frame, if they
 * are in the sprite table as on the title screen, they are hidden for that frame
 * (vdc_set_sprite_hide: host side, the game and save states see no change); after it, the
 * new line is drawn into the picture in the game's own letters, taken from those sprites'
 * patterns in VRAM (P U S H R N B T O !), and its colours from the palette as it is, so it
 * fades with the title. The letters the line lacks (A E K Y) are drawn here in the same
 * italic style. Anything else in the sprite table is left alone. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pce/pce.h"

typedef struct TitlePrompt {
    bool pad;                  /* set by the caller: a controller is the device in use */
    const char *line;          /* set by the caller: the new line (letters of the game's line and
                                * A E K Y, at most 22); NULL: by `pad`, START or ENTER */
    bool hidden;               /* the line's sprites are hidden this frame: draw after it */
    int palette;               /* sprite palette of the line */
    uint8_t cells[18][16][8];  /* the original line, 8 x 16 colour indices per character */
    uint8_t ink;               /* its letters' main colour index */
    int frames;                /* frames the new line has been drawn (for tests) */
} TitlePrompt;

/* Before pce_run_frame: find the line, hide its sprites. */
void prompt_before_frame(TitlePrompt *t, Pce *p);
/* After pce_run_frame: draw the new line over the picture. */
void prompt_after_frame(TitlePrompt *t, Pce *p);
