/* See input.h. */
#include "input.h"

#include <string.h>

#include "pce/pce.h"

#define STICK 16000

void input_init(Input *in)
{
    memset(in, 0, sizeof *in);
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);   /* controllers already connected */
    for (int i = 0; i < n && i < 8; i++) in->pads[i] = SDL_OpenGamepad(ids[i]);
    SDL_free(ids);
    in->pad_last = n > 0;
}

bool input_pad_last(const Input *in) { return in->pad_last; }

void input_use_binds(Input *in, const int *pad, const int *key)
{
    in->bind_pad = pad;
    in->bind_key = key;
}

/* a controller binding held on any controller (triggers past a light press) */
static bool pad_held(const Input *in, int b)
{
    if (b == BIND_NONE) return false;
    for (int i = 0; i < 8; i++) {
        SDL_Gamepad *g = in->pads[i];
        if (!g) continue;
        if (b == BIND_LT || b == BIND_RT) {
            if (SDL_GetGamepadAxis(g, b == BIND_LT ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000)
                return true;
        } else if (SDL_GetGamepadButton(g, (SDL_GamepadButton)b)) {
            return true;
        }
    }
    return false;
}

bool input_held(const Input *in, int act)
{
    if (!in->bind_pad || act < 0 || act >= ACT_COUNT) return false;
    int k = in->bind_key[act];
    if (k > 0 && k < SDL_SCANCODE_COUNT && SDL_GetKeyboardState(NULL)[k]) return true;
    return pad_held(in, in->bind_pad[act]);
}

bool input_control_of(const Input *in, const SDL_Event *ev, bool *is_pad, int *value)
{
    if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat) {
        *is_pad = false;
        *value = ev->key.scancode;
        return true;
    }
    if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        *is_pad = true;
        *value = ev->gbutton.button;
        return true;
    }
    if (ev->type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
        (ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || ev->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        int t = ev->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        if (!in->trig_pressed[t]) return false;
        *is_pad = true;
        *value = t ? BIND_RT : BIND_LT;
        return true;
    }
    return false;
}

bool input_pressed(const Input *in, const SDL_Event *ev, int act)
{
    if (!in->bind_pad || act < 0 || act >= ACT_COUNT) return false;
    bool is_pad;
    int v;
    if (!input_control_of(in, ev, &is_pad, &v)) return false;
    return is_pad ? v == in->bind_pad[act] && v != BIND_NONE : v == in->bind_key[act] && v != 0;
}

void input_close(Input *in)
{
    for (int i = 0; i < 8; i++)
        if (in->pads[i]) SDL_CloseGamepad(in->pads[i]);
    memset(in, 0, sizeof *in);
}

void input_device_event(Input *in, const SDL_Event *ev)
{
    in->trig_pressed[0] = in->trig_pressed[1] = false;   /* only for the event that pressed it */
    if (ev->type == SDL_EVENT_GAMEPAD_ADDED) {
        in->pad_last = true;
        if (SDL_GetGamepadFromID(ev->gdevice.which)) return;
        for (int i = 0; i < 8; i++)
            if (!in->pads[i]) { in->pads[i] = SDL_OpenGamepad(ev->gdevice.which); break; }
    } else if (ev->type == SDL_EVENT_GAMEPAD_REMOVED) {
        bool any = false;
        for (int i = 0; i < 8; i++) {
            if (in->pads[i] && SDL_GetGamepadID(in->pads[i]) == ev->gdevice.which) {
                SDL_CloseGamepad(in->pads[i]);
                in->pads[i] = NULL;
            }
            any |= in->pads[i] != NULL;
        }
        if (!any) in->pad_last = false;
    } else if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        in->pad_last = true;
    } else if (ev->type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        if (ev->gaxis.value > STICK || ev->gaxis.value < -STICK) in->pad_last = true;
        if (ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || ev->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            /* a trigger as a button: pressed past half way, let go below a quarter */
            int t = ev->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
            bool down = in->trig_held[t] ? ev->gaxis.value > 8000 : ev->gaxis.value > STICK;
            in->trig_pressed[t] = down && !in->trig_held[t];
            in->trig_held[t] = down;
        }
    } else if (ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        in->pad_last = false;
    }
}

void input_cancel_macros(Input *in) { in->q_len = in->q_pos = in->q_left = 0; }

static void push(Input *in, uint8_t set, uint8_t clear, int frames)
{
    if (in->q_len < 6) in->q[in->q_len++] = (MacroStep){set, clear, frames};
}

uint8_t input_game(Input *in)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    uint8_t m = 0;
    bool sub = false, flip = false;
    if (k[SDL_SCANCODE_UP]) m |= PAD_UP;
    if (k[SDL_SCANCODE_DOWN]) m |= PAD_DOWN;
    if (k[SDL_SCANCODE_LEFT]) m |= PAD_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) m |= PAD_RIGHT;
    /* the game's buttons and the two macros, from the controls (keys and controller alike) */
    if (input_held(in, ACT_JUMP)) m |= PAD_I;
    if (input_held(in, ACT_ATTACK)) m |= PAD_II;
    if (input_held(in, ACT_CRASH)) m |= PAD_SELECT;
    if (input_held(in, ACT_PAUSE)) m |= PAD_RUN;
    sub = input_held(in, ACT_SUB);
    flip = input_held(in, ACT_FLIP);
    for (int i = 0; i < 8; i++) {   /* moving: the d-pad and the left stick */
        SDL_Gamepad *g = in->pads[i];
        if (!g) continue;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP)) m |= PAD_UP;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) m |= PAD_DOWN;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) m |= PAD_LEFT;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) m |= PAD_RIGHT;
        int x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
        int y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
        if (x < -STICK) m |= PAD_LEFT;
        if (x > STICK) m |= PAD_RIGHT;
        if (y < -STICK) m |= PAD_UP;
        if (y > STICK) m |= PAD_DOWN;
    }

    /* start a macro on a new press (not while another one runs) */
    if (!in->q_len) {
        if (sub && !in->prev_sub) {
            /* Up + II; II has to be seen as a new press, so let go of it first if held */
            if (in->last_out & PAD_II) push(in, 0, PAD_II, 3);
            push(in, PAD_UP | PAD_II, PAD_DOWN, 4);
        } else if (flip && !in->prev_flip) {
            /* I, let go, I again while still rising */
            if (in->last_out & PAD_I) push(in, 0, PAD_I, 3);
            push(in, PAD_I, 0, 4);
            push(in, 0, PAD_I, 3);
            push(in, PAD_I, 0, 4);
        }
        if (in->q_len) { in->q_pos = 0; in->q_left = in->q[0].frames; }
    }
    in->prev_sub = sub;
    in->prev_flip = flip;
    if (in->q_len) {
        const MacroStep *st = &in->q[in->q_pos];
        m = (uint8_t)((m & ~st->clear) | st->set);
        if (--in->q_left <= 0) {
            if (++in->q_pos < in->q_len) in->q_left = in->q[in->q_pos].frames;
            else input_cancel_macros(in);
        }
    }

    /* opposite directions cannot be pressed together on a real pad */
    if ((m & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT)) m &= (uint8_t)~(PAD_LEFT | PAD_RIGHT);
    if ((m & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN)) m &= (uint8_t)~(PAD_UP | PAD_DOWN);
    in->last_out = m;
    return m;
}

bool input_run_held(const Input *in) { return input_held(in, ACT_RUN); }

bool input_rewind_held(const Input *in) { return input_held(in, ACT_REWIND); }

Nav input_nav_event(const SDL_Event *ev)
{
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        switch (ev->key.scancode) {
        case SDL_SCANCODE_UP: return NAV_UP;
        case SDL_SCANCODE_DOWN: return NAV_DOWN;
        case SDL_SCANCODE_LEFT: return NAV_LEFT;
        case SDL_SCANCODE_RIGHT: return NAV_RIGHT;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_X: case SDL_SCANCODE_SPACE: return ev->key.repeat ? NAV_NONE : NAV_OK;
        case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_Z: case SDL_SCANCODE_BACKSPACE: return ev->key.repeat ? NAV_NONE : NAV_BACK;
        case SDL_SCANCODE_F1: return ev->key.repeat ? NAV_NONE : NAV_MENU;
        default: return NAV_NONE;
        }
    }
    if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (ev->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return NAV_UP;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return NAV_DOWN;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return NAV_LEFT;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return NAV_RIGHT;
        case SDL_GAMEPAD_BUTTON_SOUTH: return NAV_OK;
        case SDL_GAMEPAD_BUTTON_EAST: return NAV_BACK;
        case SDL_GAMEPAD_BUTTON_BACK: case SDL_GAMEPAD_BUTTON_START: return NAV_MENU;
        default: return NAV_NONE;
        }
    }
    return NAV_NONE;
}

Nav input_nav_stick(Input *in)
{
    int dir = 0;
    for (int i = 0; i < 8; i++) {
        SDL_Gamepad *g = in->pads[i];
        if (!g) continue;
        int x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
        if (y < -24000) dir = NAV_UP;
        else if (y > 24000) dir = NAV_DOWN;
        else if (x < -24000) dir = NAV_LEFT;
        else if (x > 24000) dir = NAV_RIGHT;
    }
    Uint64 now = SDL_GetTicks();
    if (dir == 0) { in->stick_dir = 0; return NAV_NONE; }
    if (dir != in->stick_dir) { in->stick_dir = dir; in->stick_next = now + 350; return (Nav)dir; }
    if (now >= in->stick_next) { in->stick_next = now + 110; return (Nav)dir; }
    return NAV_NONE;
}
