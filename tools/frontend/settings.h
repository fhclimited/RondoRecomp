/* Player options, kept in settings.ini in the user's pref folder. */
#pragma once

#include <stdbool.h>

enum { ASPECT_4_3, ASPECT_SQUARE, ASPECT_STRETCH, ASPECT_COUNT };
enum { FILTER_SHARP, FILTER_NEAREST, FILTER_SMOOTH, FILTER_COUNT };
enum { SIDES_BLUR, SIDES_BLACK, SIDES_COUNT };
enum { SCALE_FIT, SCALE_WHOLE, SCALE_COUNT };
enum { SKIP_START, SKIP_AUTO, SKIP_OFF, SKIP_COUNT };
enum { LANG_EN, LANG_JA, LANG_COUNT };

typedef struct Settings {
    /* video */
    bool fullscreen;
    bool widescreen;       /* show more of the level on wide screens */
    int aspect;            /* ASPECT_* */
    int scaling;           /* SCALE_*: fit the screen, or whole multiples only */
    int filter;            /* FILTER_* */
    int sides;             /* SIDES_*: what fills a wide screen beside the picture */
    /* gameplay */
    int skip;              /* SKIP_*: story cinematics */
    bool rewind;
    int volume;            /* 0-10 */
    bool start_screen;     /* show the start screen before the game (rondo.exe --play skips it once) */
    int slot;              /* save-state slot 1-9 */
    /* cheats */
    bool cheat_health, cheat_hearts, cheat_lives;
    bool maria_unlocked;   /* the game offers Maria (its own player change) */
    bool play_maria;       /* every stage, the Prologue too, played as Maria */
    /* language: which disc image to run */
    int language;          /* LANG_* */
    char disc_ja[1024];    /* the Japanese disc image (.cue) the player chose, if not installed by Setup */
    char disc_en[1024];    /* the same for the English (translated) disc */
} Settings;

void settings_default(Settings *s);
void settings_load(Settings *s, const char *path);
bool settings_save(const Settings *s, const char *path);
