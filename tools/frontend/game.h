/* What the frontend knows about Rondo of Blood itself: the game's main state, the
 * player's lives/hearts/health, and the cheats built on them. No SDL here, so the
 * headless tool can test it.
 *
 * Work RAM ($F8 bank, $2000-$3FFF in the game's address space), found by watching the
 * game run (see docs/findings.md, "Game state"):
 *   $30  main state: 02 opening, 03 title, 04 save menu, 05 playing, 06 area change,
 *        0A story cinematic (e.g. before Stage 0; RUN does not skip it)
 *   $8D  lives (BCD)      $94  hearts (BCD)      $98  health (92 = full bar)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pce/pce.h"

#define RONDO_STATE 0x30
#define RONDO_LIVES 0x8D
#define RONDO_HEARTS 0x94
#define RONDO_HEALTH 0x98

enum {
    RONDO_OPENING = 0x02,
    RONDO_TITLE = 0x03,
    RONDO_SAVE_MENU = 0x04,
    RONDO_PLAYING = 0x05,
    RONDO_AREA_CHANGE = 0x06,
    RONDO_CINEMATIC = 0x0A,
};

typedef struct GameCheats {
    bool health, hearts, lives;
} GameCheats;

/* Per-session bookkeeping for the cheats. */
typedef struct GameWatch {
    uint8_t last_state, last_health, last_lives, last_hearts;
    uint8_t heart_drops[60];   /* hearts went down on frame (mod 60): tally guard */
    int quiet_frames;          /* frames since hearts last went down */
    bool hearts_suspended;
} GameWatch;

static inline uint8_t game_state(const Pce *p) { return p->wram[RONDO_STATE]; }
static inline bool game_in_cinematic(const Pce *p) { return game_state(p) == RONDO_CINEMATIC; }
static inline bool game_playing(const Pce *p) { return game_state(p) == RONDO_PLAYING; }

void game_watch_reset(GameWatch *w, const Pce *p);
/* Call after every emulated frame. */
void game_apply_cheats(Pce *p, const GameCheats *c, GameWatch *w);
