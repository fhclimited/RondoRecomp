/* See menu.h. */
#include "menu.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "video.h"

enum { PAGE_MAIN, PAGE_VIDEO, PAGE_GAMEPLAY, PAGE_CHEATS, PAGE_CONTROLS, PAGE_COUNT };

typedef enum { IT_ACTION, IT_PAGE, IT_CHOICE, IT_TOGGLE, IT_TEXT, IT_BACK } ItemKind;

typedef struct Item {
    ItemKind kind;
    const char *label;
    int arg;                   /* IT_ACTION: MenuResult; IT_PAGE: page */
    size_t field;              /* IT_CHOICE / IT_TOGGLE: offsetof(Settings, ...) */
    const char *const *choices;
    int n_choices;
    int base;                  /* value of choice 0 (slot numbers start at 1) */
    MenuResult on_change;
} Item;

static const char *const OFF_ON[] = {"Off", "On"};
static const char *const ASPECTS[] = {"4:3 (TV)", "Square pixels", "Stretch to fill"};
static const char *const FILTERS[] = {"Sharp", "Nearest", "Smooth"};
static const char *const SIDES[] = {"Blurred picture", "Black"};
static const char *const WIDES[] = {"Off (4:3)", "More of the level"};
static const char *const SCALINGS[] = {"Fit screen", "Whole multiples"};
static const char *const SKIPS[] = {"Press START", "Automatically", "Never"};
static const char *const LANGS[] = {"English", "Japanese"};
static const char *const SLOTS[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
static const char *const VOLUMES[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};

/* a setting's offset, checked against its type at compile time (a bool read as an int
 * picks up the bytes after it) */
#define F_INT(x) _Generic(((Settings *)0)->x, int: offsetof(Settings, x))
#define F_BOOL(x) _Generic(((Settings *)0)->x, bool: offsetof(Settings, x))
#define CHOICE(lbl, fld, arr, base, chg) {IT_CHOICE, lbl, 0, F_INT(fld), arr, (int)(sizeof arr / sizeof arr[0]), base, chg}
#define TOGGLE(lbl, fld, chg) {IT_TOGGLE, lbl, 0, F_BOOL(fld), OFF_ON, 2, 0, chg}
#define TOGGLE_NAMED(lbl, fld, arr, chg) {IT_TOGGLE, lbl, 0, F_BOOL(fld), arr, 2, 0, chg}

static const Item MAIN_ITEMS[] = {
    {IT_ACTION, "Resume", MENU_CLOSE, 0, NULL, 0, 0, 0},
    {IT_ACTION, "Save state", MENU_SAVE_STATE, 0, NULL, 0, 0, 0},
    {IT_ACTION, "Load state", MENU_LOAD_STATE, 0, NULL, 0, 0, 0},
    CHOICE("State slot", slot, SLOTS, 1, MENU_SETTINGS_CHANGED),
    {IT_PAGE, "Video", PAGE_VIDEO, 0, NULL, 0, 0, 0},
    {IT_PAGE, "Gameplay", PAGE_GAMEPLAY, 0, NULL, 0, 0, 0},
    {IT_PAGE, "Cheats", PAGE_CHEATS, 0, NULL, 0, 0, 0},
    {IT_PAGE, "Controls", PAGE_CONTROLS, 0, NULL, 0, 0, 0},
    CHOICE("Language", language, LANGS, 0, MENU_LANGUAGE),
    {IT_ACTION, "Reset game", MENU_RESET, 0, NULL, 0, 0, 0},
    {IT_ACTION, "Quit", MENU_QUIT, 0, NULL, 0, 0, 0},
};
static const Item VIDEO_ITEMS[] = {
    TOGGLE("Fullscreen", fullscreen, MENU_VIDEO_CHANGED),
    TOGGLE_NAMED("Widescreen", widescreen, WIDES, MENU_SETTINGS_CHANGED),
    CHOICE("Aspect ratio", aspect, ASPECTS, 0, MENU_SETTINGS_CHANGED),
    CHOICE("Size", scaling, SCALINGS, 0, MENU_SETTINGS_CHANGED),
    CHOICE("Filter", filter, FILTERS, 0, MENU_SETTINGS_CHANGED),
    CHOICE("Beside the picture", sides, SIDES, 0, MENU_SETTINGS_CHANGED),
    {IT_BACK, "Back", 0, 0, NULL, 0, 0, 0},
};
static const Item GAMEPLAY_ITEMS[] = {
    CHOICE("Skip cinematics", skip, SKIPS, 0, MENU_SETTINGS_CHANGED),
    TOGGLE("Rewind (hold LT / R)", rewind, MENU_SETTINGS_CHANGED),
    CHOICE("Volume", volume, VOLUMES, 0, MENU_SETTINGS_CHANGED),
    {IT_BACK, "Back", 0, 0, NULL, 0, 0, 0},
};
static const Item CHEAT_ITEMS[] = {
    TOGGLE("Infinite health", cheat_health, MENU_SETTINGS_CHANGED),
    TOGGLE("Infinite hearts", cheat_hearts, MENU_SETTINGS_CHANGED),
    TOGGLE("Infinite lives", cheat_lives, MENU_SETTINGS_CHANGED),
    TOGGLE("Maria unlocked", maria_unlocked, MENU_SETTINGS_CHANGED),
    TOGGLE("Play as Maria", play_maria, MENU_SETTINGS_CHANGED),
    {IT_TEXT, "", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Play as Maria starts at the next stage", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "(the Prologue too). Maria unlocked lets", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "the game offer her, as after Stage 2.", 0, 0, NULL, 0, 0, 0},
    {IT_BACK, "Back", 0, 0, NULL, 0, 0, 0},
};
static const Item CONTROL_ITEMS[] = {
    {IT_TEXT, "              Controller   Keyboard", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Move          D-pad/stick  Arrows", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Jump          A            X", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Attack        X            Z", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Sub-weapon    B            C", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Item Crash    Y            V", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Backflip      RB           A", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Run (hold)    LB           S", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Pause         START        Enter", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Menu          BACK         F1 / Esc", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Rewind (hold) LT           R", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Save / load state          F5 / F8", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Fullscreen                 F11", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Sub-weapon is Up+Attack, Backflip is", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Jump twice (Maria: double jump); the", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "original inputs work too.", 0, 0, NULL, 0, 0, 0},
    {IT_BACK, "Back", 0, 0, NULL, 0, 0, 0},
};

static const struct { const char *title; const Item *items; int n; int parent; } PAGES[PAGE_COUNT] = {
    {"RONDO RECOMP", MAIN_ITEMS, (int)(sizeof MAIN_ITEMS / sizeof MAIN_ITEMS[0]), -1},
    {"VIDEO", VIDEO_ITEMS, (int)(sizeof VIDEO_ITEMS / sizeof VIDEO_ITEMS[0]), PAGE_MAIN},
    {"GAMEPLAY", GAMEPLAY_ITEMS, (int)(sizeof GAMEPLAY_ITEMS / sizeof GAMEPLAY_ITEMS[0]), PAGE_MAIN},
    {"CHEATS", CHEAT_ITEMS, (int)(sizeof CHEAT_ITEMS / sizeof CHEAT_ITEMS[0]), PAGE_MAIN},
    {"CONTROLS", CONTROL_ITEMS, (int)(sizeof CONTROL_ITEMS / sizeof CONTROL_ITEMS[0]), PAGE_MAIN},
};

static int get_value(const Settings *s, const Item *it)
{
    const char *p = (const char *)s + it->field;
    if (it->kind == IT_TOGGLE) return *(const bool *)p ? 1 : 0;
    int v = *(const int *)p - it->base;
    return v < 0 ? 0 : v >= it->n_choices ? it->n_choices - 1 : v;
}

static void set_value(Settings *s, const Item *it, int v)
{
    char *p = (char *)s + it->field;
    v = (v % it->n_choices + it->n_choices) % it->n_choices;
    if (it->kind == IT_TOGGLE) *(bool *)p = v != 0;
    else *(int *)p = v + it->base;
}

static bool selectable(const Item *it) { return it->kind != IT_TEXT; }

static int first_selectable(int page)
{
    for (int i = 0; i < PAGES[page].n; i++)
        if (selectable(&PAGES[page].items[i])) return i;
    return 0;
}

void menu_open(Menu *m)
{
    m->open = true;
    m->page = PAGE_MAIN;
    m->sel[PAGE_MAIN] = 0;
    m->status[0] = 0;
}

MenuResult menu_nav(Menu *m, Settings *s, Nav nav)
{
    const Item *items = PAGES[m->page].items;
    int n = PAGES[m->page].n;
    int *sel = &m->sel[m->page];
    if (!selectable(&items[*sel])) {
        while (*sel < n - 1 && !selectable(&items[*sel])) (*sel)++;
    }
    const Item *it = &items[*sel];
    switch (nav) {
    case NAV_UP:
    case NAV_DOWN: {
        int step = nav == NAV_UP ? -1 : 1, i = *sel;
        do { i = (i + step + n) % n; } while (!selectable(&items[i]) && i != *sel);
        *sel = i;
        return MENU_NOTHING;
    }
    case NAV_LEFT:
    case NAV_RIGHT:
        if (it->kind == IT_CHOICE || it->kind == IT_TOGGLE) {
            set_value(s, it, get_value(s, it) + (nav == NAV_LEFT ? -1 : 1));
            return it->on_change;
        }
        return MENU_NOTHING;
    case NAV_OK:
        switch (it->kind) {
        case IT_ACTION: return (MenuResult)it->arg;
        case IT_PAGE: m->page = it->arg; m->sel[m->page] = first_selectable(m->page); return MENU_NOTHING;
        case IT_BACK: m->page = PAGES[m->page].parent; return MENU_NOTHING;
        case IT_CHOICE:
        case IT_TOGGLE: set_value(s, it, get_value(s, it) + 1); return it->on_change;
        default: return MENU_NOTHING;
        }
    case NAV_BACK:
        if (PAGES[m->page].parent < 0) return MENU_CLOSE;
        m->page = PAGES[m->page].parent;
        return MENU_NOTHING;
    case NAV_MENU:
        return MENU_CLOSE;
    default:
        return MENU_NOTHING;
    }
}

void menu_draw(const Menu *m, const Settings *s, SDL_Renderer *ren)
{
    int ww, wh;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    const Item *items = PAGES[m->page].items;
    int n = PAGES[m->page].n;
    const int cols = 40;
    /* biggest whole scale at which the page fits in 95% of the window */
    int ks = ui_scale(ren);
    while (ks > 1 && ((float)((n + 6) * 12 * ks) > (float)wh * 0.95f || (float)((cols + 4) * 8 * ks) > (float)ww * 0.95f)) ks--;
    float k = (float)ks;
    float ch = 8 * k, line = 12 * k;

    ui_box(ren, (SDL_FRect){0, 0, (float)ww, (float)wh}, (SDL_Color){0, 0, 0, 160});
    float pw = (cols + 4) * ch, ph = (float)(n + 6) * line;
    SDL_FRect panel = {((float)ww - pw) / 2, ((float)wh - ph) / 2, pw, ph};
    ui_box(ren, panel, (SDL_Color){16, 10, 24, 235});
    ui_box(ren, (SDL_FRect){panel.x, panel.y, pw, k * 2}, (SDL_Color){170, 30, 40, 255});
    ui_box(ren, (SDL_FRect){panel.x, panel.y + ph - k * 2, pw, k * 2}, (SDL_Color){170, 30, 40, 255});

    const SDL_Color title = {235, 200, 120, 255}, text = {225, 225, 225, 255}, dim = {150, 150, 160, 255},
                    hi = {255, 240, 190, 255}, val = {150, 210, 255, 255};
    float x = panel.x + 2 * ch, y = panel.y + line;
    const char *t = PAGES[m->page].title;
    ui_text(ren, panel.x + (pw - (float)strlen(t) * ch) / 2, y, k, title, t);
    y += line * 2;

    for (int i = 0; i < n; i++, y += line) {
        const Item *it = &items[i];
        bool on = i == m->sel[m->page] && selectable(it);
        if (on) ui_box(ren, (SDL_FRect){panel.x + ch, y - 2 * k, pw - 2 * ch, line}, (SDL_Color){120, 24, 36, 220});
        char label[96];
        snprintf(label, sizeof label, "%s", it->label);
        if (it->kind == IT_ACTION && (it->arg == MENU_SAVE_STATE || it->arg == MENU_LOAD_STATE))
            snprintf(label, sizeof label, "%s (slot %d)", it->label, s->slot);
        if (it->kind == IT_PAGE) snprintf(label, sizeof label, "%s  >", it->label);
        ui_text(ren, x, y, k, it->kind == IT_TEXT ? dim : on ? hi : text, label);
        if (it->kind == IT_CHOICE || it->kind == IT_TOGGLE) {
            char v[64];
            const char *c = it->choices[get_value(s, it)];
            if (on) snprintf(v, sizeof v, "< %s >", c);
            else snprintf(v, sizeof v, "%s", c);
            ui_text(ren, panel.x + pw - 2 * ch - (float)strlen(v) * ch, y, k, val, v);
        }
    }
    y += line;
    const char *hint = m->status[0] ? m->status : "A: choose   B: back   Left/Right: change";
    ui_text(ren, panel.x + (pw - (float)strlen(hint) * ch) / 2, y, k, m->status[0] ? hi : dim, hint);
}

int menu_check(Settings *s, SDL_Renderer *ren, char *err, int errlen)
{
    Settings keep = *s;
    int bad = 0;
    err[0] = 0;
    for (int pg = 0; pg < PAGE_COUNT; pg++) {
        Menu m = {.open = true, .page = pg};
        for (int i = 0; i < PAGES[pg].n; i++) {
            const Item *it = &PAGES[pg].items[i];
            if (it->kind != IT_CHOICE && it->kind != IT_TOGGLE) continue;
            size_t at = it->field, size = it->kind == IT_TOGGLE ? sizeof(bool) : sizeof(int);
            m.sel[pg] = i;
            for (int v = 0; v < it->n_choices; v++) {
                Settings before;
                memcpy(&before, s, sizeof before);
                set_value(s, it, v);
                if (get_value(s, it) != v || memcmp(s, &before, at) ||
                    memcmp((const char *)s + at + size, (const char *)&before + at + size, sizeof *s - at - size)) {
                    if (!bad) snprintf(err, (size_t)errlen, "%s / %s, value %d", PAGES[pg].title, it->label, v);
                    bad++;
                }
                menu_draw(&m, s, ren);
            }
        }
    }
    *s = keep;
    return bad;
}
