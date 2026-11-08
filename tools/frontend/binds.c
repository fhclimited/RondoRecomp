/* See binds.h. */
#include "binds.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static const ActionInfo ACTIONS[ACT_COUNT] = {
    [ACT_JUMP] = {"Jump", "jump", SDL_GAMEPAD_BUTTON_SOUTH, SDL_SCANCODE_X, false},
    [ACT_ATTACK] = {"Attack", "attack", SDL_GAMEPAD_BUTTON_WEST, SDL_SCANCODE_Z, false},
    [ACT_SUB] = {"Sub-weapon", "subweapon", SDL_GAMEPAD_BUTTON_EAST, SDL_SCANCODE_C, true},
    [ACT_CRASH] = {"Item Crash", "item_crash", SDL_GAMEPAD_BUTTON_NORTH, SDL_SCANCODE_V, false},
    [ACT_FLIP] = {"Backflip", "backflip", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_SCANCODE_A, true},
    [ACT_RUN] = {"Run (hold)", "run", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_SCANCODE_S, true},
    [ACT_PAUSE] = {"Pause", "pause", SDL_GAMEPAD_BUTTON_START, SDL_SCANCODE_RETURN, false},
    [ACT_MENU] = {"Menu", "menu", SDL_GAMEPAD_BUTTON_BACK, SDL_SCANCODE_F1, false},
    [ACT_REWIND] = {"Rewind (hold)", "rewind", BIND_LT, SDL_SCANCODE_R, true},
    [ACT_SAVE] = {"Save state", "save_state", BIND_NONE, SDL_SCANCODE_F5, true},
    [ACT_LOAD] = {"Load state", "load_state", BIND_NONE, SDL_SCANCODE_F8, true},
};

const ActionInfo *bind_action(int act) { return act >= 0 && act < ACT_COUNT ? &ACTIONS[act] : NULL; }

void bind_defaults(int pad[ACT_COUNT], int key[ACT_COUNT])
{
    for (int i = 0; i < ACT_COUNT; i++) {
        pad[i] = ACTIONS[i].pad;
        key[i] = ACTIONS[i].kbd;
    }
}

const char *bind_pad_name(int pad)
{
    switch (pad) {
    case BIND_NONE: return "-";
    case BIND_LT: return "LT";
    case BIND_RT: return "RT";
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A";
    case SDL_GAMEPAD_BUTTON_EAST: return "B";
    case SDL_GAMEPAD_BUTTON_WEST: return "X";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Y";
    case SDL_GAMEPAD_BUTTON_BACK: return "Back";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide";
    case SDL_GAMEPAD_BUTTON_START: return "Start";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L3 (left stick)";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3 (right stick)";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad right";
    case SDL_GAMEPAD_BUTTON_MISC1: return "Share";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return "Paddle 1";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return "Paddle 2";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return "Paddle 3";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "Paddle 4";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
    default: return pad >= 0 && pad < SDL_GAMEPAD_BUTTON_COUNT ? "Extra button" : "-";
    }
}

const char *bind_key_name(int key)
{
    if (key <= 0 || key >= SDL_SCANCODE_COUNT) return "-";
    if (key == SDL_SCANCODE_RETURN) return "Enter";
    if (key == SDL_SCANCODE_ESCAPE) return "Esc";
    const char *n = SDL_GetScancodeName((SDL_Scancode)key);
    return n && n[0] ? n : "?";
}

bool bind_pad_reserved(int pad)
{
    return pad == SDL_GAMEPAD_BUTTON_DPAD_UP || pad == SDL_GAMEPAD_BUTTON_DPAD_DOWN ||
           pad == SDL_GAMEPAD_BUTTON_DPAD_LEFT || pad == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ||
           pad == SDL_GAMEPAD_BUTTON_GUIDE;
}

bool bind_key_reserved(int key)
{
    return key == SDL_SCANCODE_UP || key == SDL_SCANCODE_DOWN || key == SDL_SCANCODE_LEFT ||
           key == SDL_SCANCODE_RIGHT || key == SDL_SCANCODE_ESCAPE || key == SDL_SCANCODE_F6 ||
           key == SDL_SCANCODE_F7 || key == SDL_SCANCODE_F11;
}

const char *bind_assign(int pad[ACT_COUNT], int key[ACT_COUNT], int act, bool is_pad, int value)
{
    static char msg[160];
    if (act < 0 || act >= ACT_COUNT) return NULL;
    int *set = is_pad ? pad : key, none = is_pad ? BIND_NONE : 0;
    if (is_pad ? bind_pad_reserved(value) : bind_key_reserved(value)) {
        snprintf(msg, sizeof msg, "%s is kept for %s.", is_pad ? bind_pad_name(value) : bind_key_name(value),
                 is_pad ? (value == SDL_GAMEPAD_BUTTON_GUIDE ? "the system" : "moving")
                 : value == SDL_SCANCODE_ESCAPE ? "the menu"
                 : value == SDL_SCANCODE_F6 || value == SDL_SCANCODE_F7 ? "the state slot"
                 : value == SDL_SCANCODE_F11 ? "fullscreen" : "moving");
        return msg;
    }
    if (set[act] == value) {                      /* its own control again: clear it */
        if (!ACTIONS[act].clearable) {
            snprintf(msg, sizeof msg, "%s needs a %s.", ACTIONS[act].name, is_pad ? "button" : "key");
            return msg;
        }
        set[act] = none;
        snprintf(msg, sizeof msg, "%s: no %s.", ACTIONS[act].name, is_pad ? "button" : "key");
        return msg;
    }
    int old = set[act];
    set[act] = value;
    for (int i = 0; i < ACT_COUNT; i++)
        if (i != act && set[i] == value) {
            set[i] = old;                          /* the action that had it gets ours */
            if (old == none && !ACTIONS[i].clearable) {   /* it cannot be left without: undo */
                set[i] = value;
                set[act] = old;
                snprintf(msg, sizeof msg, "%s is %s's; give %s another first.", is_pad ? bind_pad_name(value) : bind_key_name(value),
                         ACTIONS[i].name, ACTIONS[i].name);
                return msg;
            }
            snprintf(msg, sizeof msg, "%s: %s. %s now has %s.", ACTIONS[act].name,
                     is_pad ? bind_pad_name(value) : bind_key_name(value), ACTIONS[i].name,
                     old == none ? "none" : is_pad ? bind_pad_name(old) : bind_key_name(old));
            return msg;
        }
    snprintf(msg, sizeof msg, "%s: %s.", ACTIONS[act].name, is_pad ? bind_pad_name(value) : bind_key_name(value));
    return msg;
}

int bind_check(char *err, int errlen)
{
    int pad[ACT_COUNT], key[ACT_COUNT], bad = 0;
    err[0] = 0;
#define FAIL(...) do { if (!bad++) snprintf(err, (size_t)errlen, __VA_ARGS__); } while (0)
    bind_defaults(pad, key);
    for (int i = 0; i < ACT_COUNT; i++)
        for (int j = i + 1; j < ACT_COUNT; j++)
            if ((pad[i] != BIND_NONE && pad[i] == pad[j]) || (key[i] && key[i] == key[j]))
                FAIL("defaults: %s and %s share a control", ACTIONS[i].name, ACTIONS[j].name);
    /* separate save and load buttons */
    bind_assign(pad, key, ACT_SAVE, true, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    bind_assign(pad, key, ACT_LOAD, true, SDL_GAMEPAD_BUTTON_LEFT_STICK);
    if (pad[ACT_SAVE] != SDL_GAMEPAD_BUTTON_RIGHT_STICK || pad[ACT_LOAD] != SDL_GAMEPAD_BUTTON_LEFT_STICK)
        FAIL("save / load state did not take R3 / L3");
    /* a button another action has: they swap */
    bind_assign(pad, key, ACT_SAVE, true, SDL_GAMEPAD_BUTTON_NORTH);
    if (pad[ACT_SAVE] != SDL_GAMEPAD_BUTTON_NORTH || pad[ACT_CRASH] != SDL_GAMEPAD_BUTTON_RIGHT_STICK)
        FAIL("Y to save state: Item Crash should have taken R3");
    /* its own button again clears it where it may */
    bind_assign(pad, key, ACT_LOAD, true, SDL_GAMEPAD_BUTTON_LEFT_STICK);
    if (pad[ACT_LOAD] != BIND_NONE) FAIL("load state's own button again did not clear it");
    bind_assign(pad, key, ACT_JUMP, true, SDL_GAMEPAD_BUTTON_SOUTH);
    if (pad[ACT_JUMP] != SDL_GAMEPAD_BUTTON_SOUTH) FAIL("jump was cleared");
    /* a key for an action that may not go without, from one that has none: refused */
    bind_assign(pad, key, ACT_SAVE, false, SDL_SCANCODE_F5);   /* clears save's key */
    bind_assign(pad, key, ACT_SAVE, false, SDL_SCANCODE_X);    /* jump's X: jump would get none */
    if (key[ACT_JUMP] != SDL_SCANCODE_X) FAIL("jump was left without a key");
    /* reserved controls and triggers */
    bind_assign(pad, key, ACT_ATTACK, true, SDL_GAMEPAD_BUTTON_DPAD_UP);
    bind_assign(pad, key, ACT_ATTACK, false, SDL_SCANCODE_F11);
    if (pad[ACT_ATTACK] != SDL_GAMEPAD_BUTTON_WEST || key[ACT_ATTACK] != SDL_SCANCODE_Z) FAIL("a reserved control was taken");
    bind_assign(pad, key, ACT_MENU, true, BIND_RT);
    if (pad[ACT_MENU] != BIND_RT || strcmp(bind_pad_name(pad[ACT_MENU]), "RT")) FAIL("RT for the menu");
    if (strcmp(bind_key_name(SDL_SCANCODE_RETURN), "Enter") || strcmp(bind_key_name(SDL_SCANCODE_F5), "F5"))
        FAIL("key names");
#undef FAIL
    return bad;
}
