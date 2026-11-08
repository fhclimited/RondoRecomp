/* See menu.h. */
#include "menu.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "theme.h"
#include "video.h"

enum { PAGE_MAIN, PAGE_VIDEO, PAGE_GAMEPLAY, PAGE_CHEATS, PAGE_CONTROLS, PAGE_START, PAGE_COUNT };

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
static const char *const MODES[] = {"Classic", "Remaster"};
static const char *const SCALINGS[] = {"Fit screen", "Whole multiples"};
static const char *const SKIPS[] = {"Press START", "Automatically", "Never"};
static const char *const LANGS[] = {"English patch", "Japanese original"};
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
    {IT_ACTION, "Start screen", MENU_START_SCREEN, 0, NULL, 0, 0, 0},
    {IT_ACTION, "Quit", MENU_QUIT, 0, NULL, 0, 0, 0},
};
static const Item START_ITEMS[] = {
    {IT_ACTION, "Play", MENU_PLAY, 0, NULL, 0, 0, 0},
    CHOICE("Mode", mode, MODES, 0, MENU_SETTINGS_CHANGED),
    CHOICE("Language", language, LANGS, 0, MENU_LANGUAGE),
    {IT_ACTION, "Load state", MENU_LOAD_STATE, 0, NULL, 0, 0, 0},
    CHOICE("State slot", slot, SLOTS, 1, MENU_SETTINGS_CHANGED),
    {IT_PAGE, "Video", PAGE_VIDEO, 0, NULL, 0, 0, 0},
    {IT_PAGE, "Gameplay", PAGE_GAMEPLAY, 0, NULL, 0, 0, 0},
    {IT_PAGE, "Cheats", PAGE_CHEATS, 0, NULL, 0, 0, 0},
    {IT_PAGE, "Controls", PAGE_CONTROLS, 0, NULL, 0, 0, 0},
    {IT_ACTION, "Setup (add a disc, rebuild)", MENU_SETUP, 0, NULL, 0, 0, 0},
    {IT_ACTION, "Quit", MENU_QUIT, 0, NULL, 0, 0, 0},
};
static const Item VIDEO_ITEMS[] = {
    TOGGLE("Fullscreen", fullscreen, MENU_VIDEO_CHANGED),
    CHOICE("Mode", mode, MODES, 0, MENU_SETTINGS_CHANGED),
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
    TOGGLE("Start screen at launch", start_screen, MENU_SETTINGS_CHANGED),
    {IT_BACK, "Back", 0, 0, NULL, 0, 0, 0},
};
static const Item CHEAT_ITEMS[] = {
    TOGGLE("Infinite health", cheat_health, MENU_SETTINGS_CHANGED),
    TOGGLE("Infinite hearts", cheat_hearts, MENU_SETTINGS_CHANGED),
    TOGGLE("Infinite lives", cheat_lives, MENU_SETTINGS_CHANGED),
    TOGGLE("Maria unlocked", maria_unlocked, MENU_SETTINGS_CHANGED),
    TOGGLE("Play as Maria", play_maria, MENU_SETTINGS_CHANGED),
    {IT_BACK, "Back", 0, 0, NULL, 0, 0, 0},
};
static const Item CONTROL_ITEMS[] = {
    {IT_TEXT, "\tController\tKeyboard", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Move\tD-pad / stick\tArrows", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Jump\tA\tX", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Attack\tX\tZ", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Sub-weapon\tB\tC", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Item Crash\tY\tV", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Backflip\tRB\tA", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Run (hold)\tLB\tS", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Pause\tSTART\tEnter", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Menu\tBACK\tF1 / Esc", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Rewind (hold)\tLT\tR", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Map (Remaster)\tRT\tM / Tab", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Save / load state\t\tF5 / F8", 0, 0, NULL, 0, 0, 0},
    {IT_TEXT, "Fullscreen\t\tF11", 0, 0, NULL, 0, 0, 0},
    {IT_BACK, "Back", 0, 0, NULL, 0, 0, 0},
};

/* parent -1: a root (the in-game menu, the start screen); the option pages return to
 * whichever root opened them */
static const struct { const char *title; const Item *items; int n; int parent; } PAGES[PAGE_COUNT] = {
    {"Paused", MAIN_ITEMS, (int)(sizeof MAIN_ITEMS / sizeof MAIN_ITEMS[0]), -1},
    {"Video", VIDEO_ITEMS, (int)(sizeof VIDEO_ITEMS / sizeof VIDEO_ITEMS[0]), PAGE_MAIN},
    {"Gameplay", GAMEPLAY_ITEMS, (int)(sizeof GAMEPLAY_ITEMS / sizeof GAMEPLAY_ITEMS[0]), PAGE_MAIN},
    {"Cheats", CHEAT_ITEMS, (int)(sizeof CHEAT_ITEMS / sizeof CHEAT_ITEMS[0]), PAGE_MAIN},
    {"Controls", CONTROL_ITEMS, (int)(sizeof CONTROL_ITEMS / sizeof CONTROL_ITEMS[0]), PAGE_MAIN},
    {"Main Menu", START_ITEMS, (int)(sizeof START_ITEMS / sizeof START_ITEMS[0]), -1},
};

/* What the selected item does: the footer's help line. */
static const struct { const char *label, *help; } HELP[] = {
    {"Play", "Start the game: on from power-on to its title screen."},
    {"Language", "The English patch (fan translation) or the Japanese original: each is its own disc."},
    {"Load state", "Continue from the save state in the slot below (F8 in the game)."},
    {"State slot", "Which of the nine save-state slots to use (F6 / F7 in the game)."},
    {"Video", "Fullscreen, Classic or Remaster, the picture's shape, size and filter."},
    {"Gameplay", "Story scenes, rewind, volume and the start screen."},
    {"Cheats", "Infinite health, hearts and lives; Maria."},
    {"Controls", "The buttons and keys."},
    {"Setup (add a disc, rebuild)", "Open Setup to add the other language's disc or rebuild; this closes."},
    {"Quit", "Close Rondo Recomp."},
    {"Resume", "Back to the game."},
    {"Save state", "Keep this moment in the slot below (F5 in the game)."},
    {"Reset game", "Restart the game from power-on."},
    {"Start screen", "Leave the game for the start screen."},
    {"Fullscreen", "Use the whole screen at its own resolution (F11)."},
    {"Mode", "Classic: the original 4:3 picture. Remaster: the rooms around you drawn in, stitched together, and a map."},
    {"Aspect ratio", "4:3 as on a TV, square pixels, or stretched to fill."},
    {"Size", "As big as the screen allows, or whole multiples only."},
    {"Filter", "Sharp: crisp, even pixels. Nearest: raw. Smooth: softened."},
    {"Beside the picture", "What fills the screen beside a narrower picture."},
    {"Skip cinematics", "Story scenes and the intro: START skips them, or they are skipped."},
    {"Rewind (hold LT / R)", "Hold LT or R to play the game backwards."},
    {"Volume", "Sound volume."},
    {"Start screen at launch", "Off: rondo.exe goes straight into the game."},
    {"Infinite health", "Health never goes down."},
    {"Infinite hearts", "Hearts stay at 99."},
    {"Infinite lives", "Lives never run out."},
    {"Maria unlocked", "The game offers Maria, as after rescuing her in Stage 2."},
    {"Play as Maria", "Every stage, the Prologue too, is played as Maria: from the next stage."},
    {"Back", "Back to the previous page."},
};

static int parent_of(const Menu *m, int page)
{
    int p = PAGES[page].parent;
    return p == PAGE_MAIN ? m->root : p;
}

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
    m->page = m->root = PAGE_MAIN;
    m->sel[PAGE_MAIN] = 0;
    m->status[0] = 0;
    m->opened_ms = SDL_GetTicks();
}

void menu_open_start_screen(Menu *m)
{
    m->open = true;
    m->page = m->root = PAGE_START;
    m->sel[PAGE_START] = 0;
    m->status[0] = 0;
    m->opened_ms = SDL_GetTicks();
}

bool menu_on_start_screen(const Menu *m) { return m->open && m->root == PAGE_START; }

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
        case IT_BACK: m->page = parent_of(m, m->page); return MENU_NOTHING;
        case IT_CHOICE:
        case IT_TOGGLE: set_value(s, it, get_value(s, it) + 1); return it->on_change;
        default: return MENU_NOTHING;
        }
    case NAV_BACK:
        if (PAGES[m->page].parent >= 0) m->page = parent_of(m, m->page);
        else if (m->root != PAGE_START) return MENU_CLOSE;    /* the start screen stays */
        return MENU_NOTHING;
    case NAV_MENU:            /* START / F1: on the start screen, play */
        if (m->root != PAGE_START) return MENU_CLOSE;
        if (m->page == PAGE_START) return MENU_PLAY;
        m->page = PAGE_START;
        return MENU_NOTHING;
    default:
        return MENU_NOTHING;
    }
}

static const char *item_help(int page, const Item *it)
{
    if (page == PAGE_CONTROLS && it->kind == IT_BACK)
        return "Sub-weapon is also Up + Attack, Backflip is Jump twice: the original inputs work too.";
    for (size_t i = 0; i < sizeof HELP / sizeof HELP[0]; i++)
        if (!strcmp(HELP[i].label, it->label)) return HELP[i].help;
    return "";
}

/* Where everything goes for a page (layout units u: 1 at 960 x 540), shared by drawing and
 * the mouse. The start screen's own page has its panel on the right, the rose window and the
 * title on the left; the others are one panel in the middle. */
typedef struct Frame {
    int w, h;
    float u, cx, cy;
    float x0, x1, y0, y1;        /* the panel: y0 is its arch's apex */
    float rise;                  /* the arch's height */
    float title_y, sub_y, div_y, foot_y;
    float rows_top, row_h, text_px;
    bool start;
} Frame;

static Frame frame_for(const Menu *m, SDL_Renderer *ren)
{
    Frame F;
    SDL_GetRenderOutputSize(ren, &F.w, &F.h);
    float u = F.u = th_unit(F.w, F.h);
    F.cx = (float)F.w * 0.5f;
    F.cy = (float)F.h * 0.5f;
    F.start = m->page == PAGE_START;
    /* as tall as the page needs (arch and title, rows of 30, footer), at most 500 */
    int rows = PAGES[m->page].n;
    float head = F.start ? 78 : 92;
    float height = (head + 30 + (float)(rows - 1) * 30 + 18 + 54 + 12) * u;
    if (height > 500 * u) height = 500 * u;
    F.x0 = F.start ? F.cx + 20 * u : F.cx - 300 * u;
    F.x1 = F.start ? F.cx + 440 * u : F.cx + 300 * u;
    F.y0 = F.cy - height * 0.5f + 6 * u;
    F.y1 = F.cy + height * 0.5f + 6 * u;
    F.rise = 26 * u;
    F.title_y = F.y0 + 50 * u;
    F.sub_y = F.y0 + 75 * u;
    F.div_y = F.y0 + head * u;
    F.foot_y = F.y1 - 54 * u;
    F.rows_top = F.div_y + 30 * u;
    float span = (F.foot_y - 18 * u) - F.rows_top;
    F.row_h = rows > 1 ? span / (float)(rows - 1) : 30 * u;
    if (F.row_h > 30 * u) F.row_h = 30 * u;
    F.text_px = F.row_h * 0.62f;
    if (F.text_px > 18 * u) F.text_px = 18 * u;
    return F;
}

static float row_y(const Frame *F, int i) { return F->rows_top + (float)i * F->row_h; }

int menu_item_at(const Menu *m, SDL_Renderer *ren, float x, float y)
{
    Frame F = frame_for(m, ren);
    if (x < F.x0 + 16 * F.u || x > F.x1 - 16 * F.u) return -1;
    for (int i = 0; i < PAGES[m->page].n; i++) {
        float yc = row_y(&F, i);
        if (y >= yc - F.row_h * 0.5f && y < yc + F.row_h * 0.5f)
            return selectable(&PAGES[m->page].items[i]) ? i : -1;
    }
    return -1;
}

bool menu_item_center(const Menu *m, SDL_Renderer *ren, int item, float *x, float *y)
{
    if (item < 0 || item >= PAGES[m->page].n) return false;
    Frame F = frame_for(m, ren);
    *x = (F.x0 + F.x1) * 0.5f;
    *y = row_y(&F, item);
    return true;
}

void menu_hover(Menu *m, int item)
{
    if (item >= 0 && item < PAGES[m->page].n && selectable(&PAGES[m->page].items[item])) m->sel[m->page] = item;
}

MenuResult menu_click(Menu *m, Settings *s, int item)
{
    if (item < 0 || item >= PAGES[m->page].n || !selectable(&PAGES[m->page].items[item])) return MENU_NOTHING;
    m->sel[m->page] = item;
    return menu_nav(m, s, NAV_OK);
}

/* The title (blackletter) under the arch's apex, a line naming the game on the in-game
 * pages, the ornamented divider. */
static void draw_header(SDL_Renderer *ren, const Frame *F, const char *title, bool game_line, float a)
{
    float u = F->u, mx = (F->x0 + F->x1) * 0.5f;
    th_text(ren, TH_FONT_TITLE, 34 * u, mx, F->title_y, th_fade(TH_TEXT, a), title, 0.5f, 0.5f, 1 * u,
            th_fade(th_alpha(TH_CRIMSON, 120), a));
    if (game_line)
        th_text(ren, TH_FONT_UI, 10.5f * u, mx, F->sub_y, th_fade(TH_GOLD, a), "CASTLEVANIA  -  RONDO OF BLOOD", 0.5f,
                0.5f, 3 * u, TH_NONE);
    th_divider(ren, F->x0 + 22 * u, F->x1 - 22 * u, F->div_y, u, a);
}

/* A row of the controls table: columns separated by tabs; a first column left empty makes
 * it the table's heading. */
static void draw_table_row(SDL_Renderer *ren, const Frame *F, float y, const char *text, float px, float a)
{
    float u = F->u, cols[3] = {F->x0 + 54 * u, F->x0 + 280 * u, F->x0 + 440 * u};
    bool heading = text[0] == '\t';
    const char *p = text;
    for (int c = 0; c < 3; c++) {
        const char *e = strchr(p, '\t');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char part[96];
        if (n >= sizeof part) n = sizeof part - 1;
        memcpy(part, p, n);
        part[n] = 0;
        SDL_Color col = heading ? TH_GOLD : c == 0 ? TH_TEXT : TH_ACTION;
        th_text(ren, heading ? TH_FONT_CAPS : TH_FONT_UI, heading ? px * 0.85f : px, cols[c], y, th_fade(col, a), part,
                0, 0.5f, heading ? 2.0f * u : 0, TH_NONE);
        if (!e) break;
        p = e + 1;
    }
}

static void draw_rows(SDL_Renderer *ren, const Frame *F, const Menu *m, const Settings *s, float t, float a)
{
    float u = F->u, half = F->row_h * 0.46f;
    const Item *items = PAGES[m->page].items;
    for (int i = 0; i < PAGES[m->page].n; i++) {
        const Item *it = &items[i];
        float y = row_y(F, i);
        bool sel = i == m->sel[m->page] && selectable(it);
        bool action = it->kind == IT_ACTION || it->kind == IT_PAGE || it->kind == IT_BACK;
        if (sel) {
            /* a bar of blood fading to the right between gold hairlines, a cross for a cursor */
            th_rect_h(ren, F->x0 + 16 * u, y - half, F->x1 - 16 * u, y + half, th_fade(th_alpha(TH_CRIMSON, 150), a),
                      th_fade(th_alpha(TH_CRIMSON, 8), a));
            th_rect_h(ren, F->x0 + 16 * u, y - half, F->x1 - 16 * u, y - half + 1.0f, th_fade(th_alpha(TH_GOLD, 150), a),
                      th_fade(th_alpha(TH_GOLD, 0), a));
            th_rect_h(ren, F->x0 + 16 * u, y + half - 1.0f, F->x1 - 16 * u, y + half, th_fade(th_alpha(TH_GOLD, 150), a),
                      th_fade(th_alpha(TH_GOLD, 0), a));
            float bob = sinf(t * 6.0f) * 2.0f * u;
            th_cross(ren, F->x0 + 34 * u + bob, y, F->row_h * 0.42f, th_fade((SDL_Color){255, 230, 156, 255}, a),
                     th_fade((SDL_Color){190, 128, 44, 255}, a), th_fade((SDL_Color){60, 20, 10, 255}, a), 1.0f * u);
        } else if (it->kind != IT_TEXT) {
            th_rect_h(ren, F->x0 + 30 * u, y + F->row_h * 0.48f, (F->x0 + F->x1) * 0.5f, y + F->row_h * 0.48f + 1.0f,
                      th_alpha(TH_GOLD, 0), th_fade(th_alpha(TH_GOLD, 26), a));
            th_rect_h(ren, (F->x0 + F->x1) * 0.5f, y + F->row_h * 0.48f, F->x1 - 30 * u, y + F->row_h * 0.48f + 1.0f,
                      th_fade(th_alpha(TH_GOLD, 26), a), th_alpha(TH_GOLD, 0));
        }
        if (it->kind == IT_TEXT) {
            draw_table_row(ren, F, y, it->label, F->text_px * 0.95f, a);
            continue;
        }
        char label[96];
        snprintf(label, sizeof label, "%s", it->label);
        if (it->kind == IT_ACTION && (it->arg == MENU_SAVE_STATE || it->arg == MENU_LOAD_STATE))
            snprintf(label, sizeof label, "%s  (slot %d)", it->label, s->slot);
        bool value = it->kind == IT_CHOICE || it->kind == IT_TOGGLE;
        float label_w = (F->x1 - F->x0) - (value ? 300 : 110) * u;
        SDL_Color lc = sel ? (SDL_Color){255, 248, 232, 255} : action ? TH_ACTION : TH_TEXT;
        th_text(ren, TH_FONT_UI, th_fit(ren, TH_FONT_UI, F->text_px, label, label_w, 0), F->x0 + 56 * u, y,
                th_fade(lc, a), label, 0, 0.5f, 0, sel ? th_fade(th_alpha(TH_CRIMSON, 140), a) : TH_NONE);
        if (value) {
            const char *v = it->choices[get_value(s, it)];
            th_text(ren, TH_FONT_UI, th_fit(ren, TH_FONT_UI, F->text_px, v, 168 * u, 0), F->x1 - 120 * u, y,
                    th_fade(sel ? (SDL_Color){255, 214, 120, 255} : th_alpha(TH_GOLD, 210), a), v, 0.5f, 0.5f, 0,
                    TH_NONE);
            SDL_Color arrow = th_fade(sel ? TH_GOLD : th_alpha(TH_GOLD, 80), a);
            th_arrow(ren, F->x1 - 214 * u, y, F->row_h * 0.34f, false, arrow);
            th_arrow(ren, F->x1 - 26 * u, y, F->row_h * 0.34f, true, arrow);
        } else if (it->kind == IT_PAGE) {
            th_arrow(ren, F->x1 - 26 * u, y, F->row_h * 0.34f, true, th_fade(sel ? TH_GOLD : th_alpha(TH_GOLD, 120), a));
        }
    }
}

/* What the selected item does (or the last message), then the buttons. */
static void draw_footer(SDL_Renderer *ren, const Frame *F, const Menu *m, float a)
{
    float u = F->u, w = F->x1 - F->x0 - 56 * u, mx = (F->x0 + F->x1) * 0.5f;
    th_rect_h(ren, F->x0 + 24 * u, F->foot_y, mx, F->foot_y + 1.0f, th_alpha(TH_GOLD, 0), th_fade(th_alpha(TH_GOLD, 110), a));
    th_rect_h(ren, mx, F->foot_y, F->x1 - 24 * u, F->foot_y + 1.0f, th_fade(th_alpha(TH_GOLD, 110), a), th_alpha(TH_GOLD, 0));
    const Item *it = &PAGES[m->page].items[m->sel[m->page]];
    const char *help = m->status[0] ? m->status : selectable(it) ? item_help(m->page, it) : "";
    th_text(ren, TH_FONT_UI, th_fit(ren, TH_FONT_UI, 13.5f * u, help, w, 0), mx, F->foot_y + 18 * u,
            th_fade(m->status[0] ? TH_ACTION : TH_TEXT, a), help, 0.5f, 0.5f, 0, TH_NONE);
    const char *hint = m->page == PAGE_START
                           ? "UP / DOWN  CHOOSE     LEFT / RIGHT  CHANGE     A / ENTER  SELECT     START  PLAY"
                       : m->page == PAGE_MAIN
                           ? "UP / DOWN  CHOOSE     LEFT / RIGHT  CHANGE     A / ENTER  SELECT     B / ESC  RESUME"
                           : "UP / DOWN  CHOOSE     LEFT / RIGHT  CHANGE     A / ENTER  SELECT     B / ESC  BACK";
    th_text(ren, TH_FONT_UI, th_fit(ren, TH_FONT_UI, 9.5f * u, hint, w, 1.2f * u), mx, F->foot_y + 38 * u,
            th_fade(TH_TEXT_DIM, a), hint, 0.5f, 0.5f, 1.2f * u, TH_NONE);
}

/* The start screen's left side: the rose window, the title, the disc. */
static void draw_start_title(SDL_Renderer *ren, const Frame *F, const Menu *m, float t, float a)
{
    float u = F->u, x = F->cx - 230 * u, y = F->cy;
    th_emblem(ren, x, y - 96 * u, 58 * u, t, a);
    th_text(ren, TH_FONT_CAPS, th_fit(ren, TH_FONT_CAPS, 24 * u, "CASTLEVANIA", 380 * u, 5 * u), x, y + 10 * u,
            th_fade(TH_GOLD, a), "CASTLEVANIA", 0.5f, 0.5f, 5 * u, th_fade(th_alpha((SDL_Color){0, 0, 0, 255}, 160), a));
    float tp = th_fit(ren, TH_FONT_TITLE, 66 * u, "Rondo of Blood", 420 * u, 1 * u);
    th_text(ren, TH_FONT_TITLE, tp, x, y + 58 * u, th_fade(TH_TEXT, a), "Rondo of Blood", 0.5f, 0.5f, 1 * u,
            th_fade(th_alpha(TH_CRIMSON, 150), a));
    th_divider(ren, x - 170 * u, x + 170 * u, y + 102 * u, u, a);
    th_text(ren, TH_FONT_UI, 11 * u, x, y + 122 * u, th_fade(TH_TEXT_DIM, a), "RONDO RECOMP", 0.5f, 0.5f, 5 * u, TH_NONE);
    if (m->info[0][0])
        th_text(ren, TH_FONT_UI, th_fit(ren, TH_FONT_UI, 12.5f * u, m->info[0], 420 * u, 0), x, y + 146 * u,
                th_fade(TH_GOLD, a), m->info[0], 0.5f, 0.5f, 0, th_fade(th_alpha((SDL_Color){0, 0, 0, 255}, 150), a));
    if (m->info[1][0])
        th_text(ren, TH_FONT_UI, 10 * u, x, y + 164 * u, th_fade(TH_TEXT_DIM, a), m->info[1], 0.5f, 0.5f, 0.5f * u,
                th_fade(th_alpha((SDL_Color){0, 0, 0, 255}, 150), a));
}

void menu_draw(const Menu *m, const Settings *s, SDL_Renderer *ren)
{
    Frame F = frame_for(m, ren);
    Uint64 now = SDL_GetTicks();
    float t = (float)now / 1000.0f, since = (float)(now - m->opened_ms) / 1000.0f;
    float pop = since * 6.0f;
    if (pop > 1) pop = 1;
    if (pop < 0) pop = 0;
    float a = pop * pop;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    if (m->root == PAGE_START) {
        th_backdrop(ren, F.w, F.h, t);              /* the game is not running behind it */
    } else {
        float fw = (float)F.w, fh = (float)F.h;
        th_rect(ren, 0, 0, fw, fh, (SDL_Color){10, 1, 5, 178});
        th_rect_h(ren, 0, 0, fw * 0.25f, fh, (SDL_Color){0, 0, 0, 120}, (SDL_Color){0, 0, 0, 0});
        th_rect_h(ren, fw * 0.75f, 0, fw, fh, (SDL_Color){0, 0, 0, 0}, (SDL_Color){0, 0, 0, 120});
    }
    if (F.start) {
        float b = since * 2.0f;
        draw_start_title(ren, &F, m, t, b > 1 ? 1 : b);
    }
    /* the frame opens out from its middle */
    float mid = (F.y0 + F.y1) * 0.5f;
    th_frame(ren, F.x0, mid - (mid - F.y0) * pop, F.x1, mid + (F.y1 - mid) * pop, F.rise * pop, F.u, pop);
    draw_header(ren, &F, PAGES[m->page].title, !F.start, a);
    draw_rows(ren, &F, m, s, t, a);
    draw_footer(ren, &F, m, a);
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
