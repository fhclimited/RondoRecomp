/* What each control does: the actions the player can rebind (Controls page), each with a
 * controller button and a key. Moving (d-pad, left stick, arrows), the menus' own keys,
 * F6 / F7 (state slot), F11 (fullscreen) and Esc (menu) are fixed. */
#pragma once

#include <stdbool.h>

enum {
    ACT_JUMP, ACT_ATTACK, ACT_SUB, ACT_CRASH, ACT_FLIP, ACT_RUN, ACT_PAUSE, ACT_MENU, ACT_REWIND,
    ACT_SAVE, ACT_LOAD, ACT_COUNT
};

/* A controller binding: an SDL_GamepadButton, BIND_LT / BIND_RT for the triggers, or
 * BIND_NONE. A key binding: an SDL_Scancode, 0 for none. */
#define BIND_NONE (-1)
#define BIND_LT 100
#define BIND_RT 101

typedef struct ActionInfo {
    const char *name;      /* as on the Controls page */
    const char *key;       /* in settings.ini: bind_pad_<key>, bind_key_<key> */
    int pad, kbd;          /* the defaults */
    bool clearable;        /* may be left without a control (the game can be played without it) */
} ActionInfo;

const ActionInfo *bind_action(int act);
void bind_defaults(int pad[ACT_COUNT], int key[ACT_COUNT]);
const char *bind_pad_name(int pad);   /* "A", "LB", "LT", ... ; "-" for none */
const char *bind_key_name(int key);   /* "Enter", "F5", "X", ... ; "-" for none */
/* Controls kept for something else: the d-pad (moving), the Guide button; the arrows
 * (moving), Esc (menu), F6 / F7 (state slot), F11 (fullscreen). */
bool bind_pad_reserved(int pad);
bool bind_key_reserved(int key);
/* Give action `act` a controller button (is_pad) or key. The action that had it gets this
 * one's old control (they swap); pressing an action's own control again clears it where it
 * may be cleared. Returns a line for the menu to show, or NULL. */
const char *bind_assign(int pad[ACT_COUNT], int key[ACT_COUNT], int act, bool is_pad, int value);
/* Test: assignments, swaps, clearing and names behave. 0 if all well, else the problems
 * (the first described in err). */
int bind_check(char *err, int errlen);
