/* See game.h. */
#include "game.h"

#include <string.h>

void game_watch_reset(GameWatch *w, const Pce *p)
{
    memset(w, 0, sizeof *w);
    w->last_state = game_state(p);
    w->last_health = p->wram[RONDO_HEALTH];
    w->last_lives = p->wram[RONDO_LIVES];
    w->last_hearts = p->wram[RONDO_HEARTS];
}

static bool menu_state(uint8_t s) { return s <= RONDO_SAVE_MENU || s == 0x08; }

void game_apply_cheats(Pce *p, const GameCheats *c, GameWatch *w)
{
    uint8_t s = game_state(p);
    uint8_t *ram = p->wram;

    /* Health: undo any loss while playing (the game's own refills still work, and a new
     * area or character keeps the maximum the game sets). */
    if (c->health && s == RONDO_PLAYING && w->last_state == RONDO_PLAYING && ram[RONDO_HEALTH] < w->last_health)
        ram[RONDO_HEALTH] = w->last_health;

    /* Lives: undo any loss outside the title and menus. */
    if (c->lives && !menu_state(s) && !menu_state(w->last_state) && ram[RONDO_LIVES] < w->last_lives)
        ram[RONDO_LIVES] = w->last_lives;

    /* Hearts: 99 while playing. If the game counts hearts down steadily (more than 12 times
     * a second, like a hearts-to-score tally), stop topping them up until it has been quiet
     * for 3 seconds or the state changes, so the count can finish. */
    if (c->hearts) {
        if (s != w->last_state) {
            w->hearts_suspended = false;
            memset(w->heart_drops, 0, sizeof w->heart_drops);
        }
        int slot = (int)(p->frame % 60);
        bool dropped = s == RONDO_PLAYING && ram[RONDO_HEARTS] < w->last_hearts;
        w->heart_drops[slot] = dropped;
        w->quiet_frames = dropped ? 0 : w->quiet_frames + 1;
        int drops = 0;
        for (int i = 0; i < 60; i++) drops += w->heart_drops[i];
        if (drops > 12) w->hearts_suspended = true;
        else if (w->hearts_suspended && w->quiet_frames > 180) w->hearts_suspended = false;
        if (s == RONDO_PLAYING && !w->hearts_suspended && ram[RONDO_HEARTS] != 0x99) ram[RONDO_HEARTS] = 0x99;
    }

    w->last_state = s;
    w->last_health = ram[RONDO_HEALTH];
    w->last_lives = ram[RONDO_LIVES];
    w->last_hearts = ram[RONDO_HEARTS];
}
