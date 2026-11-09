/* The in-game menu (F1 / Esc / the controller's Back button; the game is paused while it is
 * open) and the start screen shown before the game (the same pages, with Play at the top). */
#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "input.h"
#include "settings.h"

typedef enum {
    MENU_NOTHING,
    MENU_CLOSE,
    MENU_SAVE_STATE,
    MENU_LOAD_STATE,
    MENU_RESET,
    MENU_QUIT,
    MENU_LANGUAGE,         /* settings.language changed: restart with the other disc */
    MENU_VIDEO_CHANGED,    /* fullscreen etc. */
    MENU_SETTINGS_CHANGED, /* anything else: save settings.ini */
    MENU_PLAY,             /* start screen: start the game */
    MENU_START_SCREEN,     /* in-game menu: back to the start screen */
    MENU_SETUP,            /* start screen: open RondoRecomp Setup (add a disc, rebuild) */
    MENU_RESET_CONTROLS,   /* Controls page: every control back to its default (handled here) */
} MenuResult;

typedef struct Menu {
    bool open;
    int page;
    int root;              /* the page Back returns to: the in-game menu or the start screen */
    int sel[8];
    char status[160];      /* one line at the bottom (e.g. "State 3 saved") */
    char info[2][96];      /* start screen: lines under the title (the disc, the languages) */
    Uint64 opened_ms;      /* when it opened: the panel opens out */
    int capturing;         /* Controls page: 1 + the action counting down to its new control, 0 none */
    Uint64 capture_until;  /* ...when the count ends (ms) */
    int held[8], n_held;   /* controller buttons / triggers held since it began, in the order pressed */
    int last[8], n_last;   /* the last ones down together (for buttons let go before the end) */
    int held_key, last_key;    /* a key held now / the last one pressed, 0 none */
    bool key_after;        /* the last press was a key */
} Menu;

void menu_open(Menu *m);
void menu_open_start_screen(Menu *m);
bool menu_on_start_screen(const Menu *m);   /* open as the start screen (any of its pages) */
MenuResult menu_nav(Menu *m, Settings *s, Nav nav);
void menu_draw(const Menu *m, const Settings *s, SDL_Renderer *ren);
/* Controls page: choosing an action's row counts down from 5, and what is held when the count
 * ends becomes its control: one controller button or trigger, two (Back + RB: the first
 * pressed is the one held), or a key. With nothing held then, the last ones pressed together
 * (or the last key) count. While it counts, every event goes to menu_capture; Esc or a mouse
 * click leaves the control as it was. menu_update ends the count: MENU_SETTINGS_CHANGED when
 * a control changed. */
bool menu_capturing(const Menu *m);
MenuResult menu_capture(Menu *m, Settings *s, const Input *in, const SDL_Event *ev);
MenuResult menu_update(Menu *m, Settings *s);

/* Mouse, in render coordinates: the item under (x, y) or -1; moving over an item selects
 * it; clicking chooses it (a choice steps to its next value). */
int menu_item_at(const Menu *m, SDL_Renderer *ren, float x, float y);
void menu_hover(Menu *m, int item);
MenuResult menu_click(Menu *m, Settings *s, int item);
/* The middle of an item's row, in render coordinates (for tests that click it). */
bool menu_item_center(const Menu *m, SDL_Renderer *ren, int item, float *x, float *y);

/* Test: draws every page with every value of every option, checking that each value reads
 * back and that changing an option leaves the rest of *s alone. Returns the number of
 * problems (the first is described in err); *s is restored. */
int menu_check(Settings *s, SDL_Renderer *ren, char *err, int errlen);
