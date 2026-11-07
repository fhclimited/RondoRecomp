/* The in-game menu (F1 / Esc / the controller's Back button). The game is paused while it
 * is open. */
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
} MenuResult;

typedef struct Menu {
    bool open;
    int page;
    int sel[8];
    char status[160];      /* one line at the bottom (e.g. "State 3 saved") */
} Menu;

void menu_open(Menu *m);
MenuResult menu_nav(Menu *m, Settings *s, Nav nav);
void menu_draw(const Menu *m, const Settings *s, SDL_Renderer *ren);

/* Test: draws every page with every value of every option, checking that each value reads
 * back and that changing an option leaves the rest of *s alone. Returns the number of
 * problems (the first is described in err); *s is restored. */
int menu_check(Settings *s, SDL_Renderer *ren, char *err, int errlen);
