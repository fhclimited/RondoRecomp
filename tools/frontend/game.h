/* What the frontend knows about Rondo of Blood itself: the game's main state, the
 * player's lives/hearts/health, and the cheats built on them. No SDL here, so the
 * headless tool can test it.
 *
 * Work RAM ($F8 bank, $2000-$3FFF in the game's address space), found by watching the
 * game run (see docs/findings.md, "Game state"):
 *   $30  main state: 02 opening, 03 title, 04 save menu, 05 playing, 06 area change,
 *        0A story cinematic (e.g. before Stage 0; RUN does not skip it)
 *   $8D  lives (BCD)      $94  hearts (BCD)      $98  health (92 = full bar)
 *   $9F  character: 0 Richter, 1 Maria (each stage loads that character's code)
 *   $1F40-$1F56  the save record of the current game; $1F40 bit 0: Maria can be chosen
 *   objects: 88 slots in parallel arrays, slot 0 is the player; X velocity $0D80 (high)
 *        / $0DD8 (low), 8.8 pixels a frame; facing $0AC0 (0 right, 1 left); animation
 *        frame timer $0F90 (counts down to the next frame)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pce/pce.h"

#define RONDO_STATE 0x30
#define RONDO_LIVES 0x8D
#define RONDO_HEARTS 0x94
#define RONDO_HEALTH 0x98
#define RONDO_SUB 0x31
#define RONDO_CHARACTER 0x9F
#define RONDO_UNLOCKS 0x1F40
#define RONDO_PLAYER_VX_HI 0x0D80
#define RONDO_PLAYER_VX_LO 0x0DD8
#define RONDO_PLAYER_FACING 0x0AC0
#define RONDO_PLAYER_ANIM_TIMER 0x0F90

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
    bool maria_unlocked;       /* the game offers Maria (its own player change) */
    bool play_maria;           /* every stage, the Prologue too, is played as Maria */
} GameCheats;

/* Per-session bookkeeping for the cheats. */
typedef struct GameWatch {
    uint8_t last_state, last_health, last_lives, last_hearts;
    uint8_t heart_drops[60];   /* hearts went down on frame (mod 60): tally guard */
    int quiet_frames;          /* frames since hearts last went down */
    bool hearts_suspended;
    bool maria_forced;         /* play_maria set the character; undo it when turned off */
    int run_from;              /* run: the walking/jumping speed the run replaced (0 = none) */
} GameWatch;

static inline uint8_t game_state(const Pce *p) { return p->wram[RONDO_STATE]; }
static inline bool game_in_cinematic(const Pce *p) { return game_state(p) == RONDO_CINEMATIC; }
/* The power-on intro: the story narration and the animated opening (state $02, sub-states
 * $00-$01, about 1:55 on both discs). The game ignores START until the title screen
 * (sub-state $02); later returns to the title skip it ($00 -> $02/$03). */
static inline bool game_in_intro(const Pce *p) { return game_state(p) == RONDO_OPENING && p->wram[RONDO_SUB] <= 0x01; }
/* What the frontend's cinematic skipping covers: story scenes and the power-on intro. */
static inline bool game_in_skippable(const Pce *p) { return game_in_cinematic(p) || game_in_intro(p); }
static inline bool game_playing(const Pce *p) { return game_state(p) == RONDO_PLAYING; }

void game_watch_reset(GameWatch *w, const Pce *p);
/* Call after every emulated frame. */
void game_apply_cheats(Pce *p, const GameCheats *c, GameWatch *w);

/* Richter's run from Symphony of the Night: while `held`, walking (and jumps that keep the
 * walking speed) go 2.3 / 1.25 times as fast, as Richter's run does there; Maria's walk is
 * sped up the same way. Only the exact walking / jumping speeds in the facing direction are
 * changed (not backflips or knockback), so the game's own movement code still handles
 * walls, stairs and the camera. Call after every emulated frame. */
void game_apply_run(Pce *p, bool held, GameWatch *w);
