/* Windowed frontend (SDL3): start screen, video, input, audio, menu, save states, rewind,
 * cheats.
 *
 * usage: rondo [game.cue] [--play] [--autotest] [--autotest-features DIR] [--autotest-start]
 *   With no disc argument it uses the disc Setup installed, then the remembered disc, then
 *   searches the folders around the executable, then asks once with a file picker and
 *   remembers the choice. It opens on the start screen (Play, language, options; the game
 *   waits until Play) unless --play is given or the option is off.
 *
 * Controls: see input.h (also on the menu's Controls page). F1 / Esc / the controller's
 * Back button open the menu; options are kept in settings.ini in the pref folder
 * (%APPDATA%\RondoRecomp\RondoRecomp on Windows) with bram.bin and states\.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "capture_script.h"
#include "game.h"
#include "input.h"
#include "locate.h"
#include "menu.h"
#include "pce/pce.h"
#include "pce/state.h"
#include "recomp/recomp.h"
#include "settings.h"
#include "theme.h"
#include "video.h"
#include "wide.h"

#define FRAME_RATE (7159090.0 / (455.0 * 263.0))   /* 59.826 Hz */
#define AUDIO_RATE 44100

/* ---- on-screen messages ------------------------------------------------------------ */

static char osd_text[160];
static Uint64 osd_until;

static void show_osd(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(osd_text, sizeof osd_text, fmt, ap);
    va_end(ap);
    osd_until = SDL_GetTicks() + 2500;
}

/* A one-line notice on a small glass plate (theme.h): top centre (clear of the HUD on the
 * left), or bottom right. */
static void draw_banner(SDL_Renderer *ren, const char *text, bool bottom_right)
{
    int ww, wh;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    float u = th_unit(ww, wh), px = 15 * u, track = 1.5f * u;
    float w = th_measure(ren, TH_FONT_UI, px, text, track) + 44 * u, h = 30 * u, m = 14 * u;
    float x = bottom_right ? (float)ww - w - m : ((float)ww - w) * 0.5f, y = bottom_right ? (float)wh - h - m : m;
    th_panel(ren, x, y, x + w, y + h, 9 * u, TH_PANEL_TOP, TH_PANEL_BOTTOM, th_alpha(TH_GOLD, 190), 1.3f * u);
    th_rect(ren, x + 8 * u, y + 8 * u, x + 11 * u, y + h - 8 * u, TH_CRIMSON);
    th_text(ren, TH_FONT_UI, px, x + 22 * u, y + h * 0.5f, TH_TEXT, text, 0, 0.5f, track, TH_NONE);
}

/* ---- choosing the disc image ------------------------------------------------------- */

typedef struct {
    SDL_AtomicInt done;
    char path[1024];
} Pick;

static void SDLCALL on_pick(void *userdata, const char *const *files, int filter)
{
    (void)filter;
    Pick *p = userdata;
    if (files && files[0]) snprintf(p->path, sizeof p->path, "%s", files[0]);
    else p->path[0] = 0;
    SDL_SetAtomicInt(&p->done, 1);
}

static bool pick_file(SDL_Window *win, SDL_Renderer *ren, char *out, size_t n)
{
    static Pick pick;
    static const SDL_DialogFileFilter filters[] = {{"PC Engine CD image (*.cue)", "cue"}};
    SDL_SetAtomicInt(&pick.done, 0);
    pick.path[0] = 0;
    SDL_ShowOpenFileDialog(on_pick, &pick, win, filters, 1, NULL, false);
    while (!SDL_GetAtomicInt(&pick.done)) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_EVENT_QUIT) return false;
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    if (!pick.path[0]) return false;
    snprintf(out, n, "%s", pick.path);
    return true;
}

/* Command line, then the remembered path, then a search near the executable, then ask. */
static bool choose_disc(SDL_Window *win, SDL_Renderer *ren, const char *arg, char *out, size_t n)
{
    if (arg) {
        snprintf(out, n, "%s", arg);
        if (discid_identify(out, NULL) != DISC_INVALID) { locate_remember(out); return true; }
    } else if (locate_disc(out, n)) {
        locate_remember(out);
        return true;
    }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Rondo Recomp",
                             "Select the .cue file of your Castlevania: Rondo of Blood disc image.\n"
                             "It will be remembered for next time.", win);
    for (;;) {
        if (!pick_file(win, ren, out, n)) return false;
        DiscKind kind = discid_identify(out, NULL);
        if (kind == DISC_RONDO) { locate_remember(out); return true; }
        if (kind == DISC_PCE_UNKNOWN) {
            const SDL_MessageBoxButtonData buttons[] = {
                {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Use it"},
                {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Choose another"},
            };
            const SDL_MessageBoxData box = {
                SDL_MESSAGEBOX_WARNING, win, "Rondo Recomp",
                "This is a PC Engine CD image, but not a release of Rondo of Blood this build "
                "recognises. Use it anyway?", 2, buttons, NULL};
            int id = 0;
            SDL_ShowMessageBox(&box, &id);
            if (id == 1) { locate_remember(out); return true; }
            continue;
        }
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Rondo Recomp",
                                 "That file could not be read as a PC Engine CD image.\n"
                                 "Choose the .cue file of the disc image.", win);
    }
}

/* ---- the running game ---------------------------------------------------------------- */

static Pce pce;
static Rewind rw;
static bool have_rewind;
static GameWatch gw;
static char pref_dir[1024];
static bool no_save_files;   /* test runs: fresh backup RAM, nothing written to bram.bin */
static Wide wide;            /* widescreen: decided before every frame */

static void run_frame(void)
{
    wide_frame(&wide, &pce);
    pce_run_frame(&pce);
}

typedef struct Session {
    char disc[1024];
    uint64_t disc_id;
    int language;
    char bram_path[1100];
} Session;

static void flush_bram(Session *ss)
{
    if (pce.bram_dirty && ss->bram_path[0]) SDL_SaveFile(ss->bram_path, pce.bram, 0x800);
    pce.bram_dirty = false;
}

/* LANG_JA for the original Japanese disc, LANG_EN for a translated one. */
static int disc_language(const char *cue) { return discid_language(cue) == DISC_LANG_JA ? LANG_JA : LANG_EN; }

/* The disc for a language: installed by Setup (game/en, game/ja), chosen for it earlier in
 * the menu, or the main disc if it is in that language. */
static bool disc_for(int lang, const Settings *set, const char *main_disc, char *out, size_t n)
{
    if (locate_installed(lang == LANG_JA, out, n)) return true;
    const char *chosen = lang == LANG_JA ? set->disc_ja : set->disc_en;
    if (chosen[0] && discid_identify(chosen, NULL) != DISC_INVALID && disc_language(chosen) == lang) {
        snprintf(out, n, "%s", chosen);
        return true;
    }
    if (main_disc[0] && disc_language(main_disc) == lang) {
        snprintf(out, n, "%s", main_disc);
        return true;
    }
    return false;
}

/* (Re)start the machine with a disc; the session's language is the disc's. The disc is
 * checked first, so a failure leaves the running game alone. */
static bool start_game(Session *ss, const char *disc, char *err, int err_len)
{
    if (discid_identify(disc, NULL) == DISC_INVALID) {
        snprintf(err, (size_t)err_len, "%s could not be read as a PC Engine CD image", disc);
        return false;
    }
    flush_bram(ss);
    if (!pce_init(&pce, disc, err, err_len) || !pce_boot(&pce, err, err_len)) return false;
    snprintf(ss->disc, sizeof ss->disc, "%s", disc);
    int language = disc_language(disc);
    ss->language = language;
    ss->disc_id = 0;
    discid_identify(disc, &ss->disc_id);
    /* in-game saves (backup RAM), one file per language */
    snprintf(ss->bram_path, sizeof ss->bram_path, "%sbram%s.bin", pref_dir, language == LANG_JA ? "-ja" : "");
    if (no_save_files) ss->bram_path[0] = 0;
    size_t n = 0;
    void *data = ss->bram_path[0] ? SDL_LoadFile(ss->bram_path, &n) : NULL;
    if (data && n >= 0x800) memcpy(pce.bram, data, 0x800);
    SDL_free(data);
    pce.bram_dirty = false;
    if (have_rewind) rewind_reset(&rw, &pce);
    game_watch_reset(&gw, &pce);
    return true;
}

/* The Language option: restart with set->language's disc (each language is its own disc: the
 * original Japanese one, or the English translation). Without one installed or chosen
 * before, `ask` lets the player pick its .cue. The outcome goes to status; set->language
 * ends up as the running disc's. */
static void switch_language(Session *ss, Settings *set, const char *main_disc, bool ask, SDL_Window *win,
                            SDL_Renderer *ren, char *status, size_t status_len)
{
    int want = set->language;
    char disc[1024], err[256];
    bool have = disc_for(want, set, main_disc, disc, sizeof disc), picked = false;
    const char *problem = want == LANG_JA ? "No Japanese disc installed" : "No English disc installed";
    if (!have && ask) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Rondo Recomp",
            want == LANG_JA
                ? "Japanese needs the original Japanese disc image (Akumajou Dracula X: Chi no Rondo).\n"
                  "Select its .cue file; it will be remembered.\n"
                  "(Running Setup with its zip installs it next to the English one.)"
                : "English needs the English (translated) disc image.\nSelect its .cue file; it will be "
                  "remembered.\n(Running Setup with its zip installs it next to the Japanese one.)", win);
        if (!pick_file(win, ren, disc, sizeof disc)) problem = "No disc chosen";
        else if (discid_identify(disc, NULL) == DISC_INVALID) problem = "That is not a PC Engine CD image";
        else if (disc_language(disc) != want)
            problem = want == LANG_JA ? "That is the English disc" : "That is the Japanese disc";
        else have = picked = true;
    }
    if (have && start_game(ss, disc, err, sizeof err)) {
        if (picked && want == LANG_JA) snprintf(set->disc_ja, sizeof set->disc_ja, "%s", disc);
        if (picked && want == LANG_EN) snprintf(set->disc_en, sizeof set->disc_en, "%s", disc);
        snprintf(status, status_len, "Restarted in %s", want == LANG_JA ? "Japanese" : "English");
    } else {
        snprintf(status, status_len, "%.100s", have ? err : problem);
    }
    set->language = ss->language;
}

/* The start screen's lines under the title: the disc being played, the other language. */
static void start_screen_info(Menu *m, const Session *ss, const Settings *set, const char *main_disc)
{
    char other[1024];
    int o = ss->language == LANG_JA ? LANG_EN : LANG_JA;
    bool have = disc_for(o, set, main_disc, other, sizeof other);
    snprintf(m->info[0], sizeof m->info[0], "%s disc - %s %s", ss->language == LANG_JA ? "Japanese" : "English",
             o == LANG_JA ? "Japanese" : "English", have ? "also installed" : "not installed");
    snprintf(m->info[1], sizeof m->info[1], "%s", recomp_build_id);
}

/* Start screen: RondoRecomp Setup (next to rondo.exe) adds a disc or rebuilds. */
static bool open_setup(char *status, size_t n)
{
    const char *base = SDL_GetBasePath();
    char exe[1100];
    snprintf(exe, sizeof exe, "%sRondoRecomp Setup.exe", base ? base : "");
    if (!SDL_GetPathInfo(exe, NULL)) {
        snprintf(status, n, "RondoRecomp Setup.exe is not next to rondo.exe");
        return false;
    }
    const char *args[] = {exe, NULL};
    SDL_Process *proc = SDL_CreateProcess(args, false);
    if (!proc) {
        snprintf(status, n, "Cannot start Setup: %.80s", SDL_GetError());
        return false;
    }
    SDL_DestroyProcess(proc);      /* it keeps running; rondo.exe quits so it can be rebuilt */
    return true;
}

static void state_path(char *out, size_t n, const Session *ss, int slot)
{
    snprintf(out, n, "%sstates/slot%d%s.state", pref_dir, slot, ss->language == LANG_JA ? "-ja" : "");
}

/* Input events a test run sends itself carry this device id; test runs drop all other input
 * events (keyboard, mouse, controller), whatever window has the focus. */
#define TEST_INPUT 0x52524543u

static bool test_input(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
        return ev->key.which == TEST_INPUT;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        return ev->button.which == TEST_INPUT;
    case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_WHEEL: case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        return false;
    default:
        return true;   /* window, device and other events */
    }
}

static void screenshot(SDL_Renderer *ren, const char *path)
{
    SDL_Surface *s = SDL_RenderReadPixels(ren, NULL);
    if (s) {
        SDL_SaveBMP(s, path);
        SDL_DestroySurface(s);
    }
}

int main(int argc, char **argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    /* --autotest: save/load a state and rewind without user input, then quit.
     * --autotest-features DIR: play the capture script with cheats and automatic cinematic
     * skipping, take screenshots of the video modes and menu into DIR, then quit. */
    bool autotest = false, play_now = false, autotest_start = false;
    const char *features_dir = NULL, *disc_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--autotest")) autotest = true;
        else if (!strcmp(argv[i], "--play")) play_now = true;
        else if (!strcmp(argv[i], "--autotest-features") && i + 1 < argc) features_dir = argv[++i];
        else if (!strcmp(argv[i], "--autotest-start")) autotest_start = true;
        else disc_arg = argv[i];
    }
    char autotest_log[2048] = "";
#define TLOG(...) snprintf(autotest_log + strlen(autotest_log), sizeof autotest_log - strlen(autotest_log), __VA_ARGS__)

    locate_profile_dir(pref_dir, sizeof pref_dir);
    char settings_path[1100], states_dir[1100];
    snprintf(settings_path, sizeof settings_path, "%ssettings.ini", pref_dir);
    if (autotest || features_dir || autotest_start) {
        /* test runs keep their state files apart from the player's */
        no_save_files = true;
        size_t l = strlen(pref_dir);
        snprintf(pref_dir + l, sizeof pref_dir - l, "test/");
        SDL_CreateDirectory(pref_dir);
    }
    snprintf(states_dir, sizeof states_dir, "%sstates", pref_dir);
    SDL_CreateDirectory(states_dir);
    Settings set;
    settings_load(&set, settings_path);
    bool test_mode = autotest || features_dir || autotest_start;
    if (test_mode) {   /* fixed options; nothing is saved */
        settings_default(&set);
        if (features_dir) {
            set.skip = SKIP_AUTO;
            set.cheat_health = set.cheat_hearts = set.cheat_lives = true;
        }
    }

    char title[256];
    snprintf(title, sizeof title, "Rondo Recomp (%s)", recomp_build_id);
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (set.fullscreen && !test_mode) flags |= SDL_WINDOW_FULLSCREEN;
    if (test_mode) flags |= SDL_WINDOW_NOT_FOCUSABLE;   /* never takes the keyboard from the player */
    SDL_Window *win = SDL_CreateWindow(title, features_dir ? 1280 : 960, 720, flags);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, NULL) : NULL;
    Video video;
    if (!ren || !video_init(&video, ren)) {
        fprintf(stderr, "SDL video setup: %s\n", SDL_GetError());
        return 1;
    }

    char disc_path[1024];
    if (!choose_disc(win, ren, disc_arg, disc_path, sizeof disc_path)) {
        SDL_Quit();
        return 0;
    }
    have_rewind = rewind_init(&rw, 256u << 20, (int)(FRAME_RATE * 60 * 30));   /* ~1 KB a frame, up to 30 min */
    Session ss = {0};
    char err[256];
    bool started = false;
    {   /* the chosen language's disc if there is one (a disc on the command line wins) */
        char want[1024];
        if (!disc_arg && disc_for(set.language, &set, disc_path, want, sizeof want))
            started = start_game(&ss, want, err, sizeof err);
    }
    if (!started && !start_game(&ss, disc_path, err, sizeof err)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Rondo Recomp", err, win);
        SDL_Quit();
        return 1;
    }
    set.language = ss.language;
    /* entry points that run on the interpreter are kept next to rondo.exe; running Setup with
     * --rebuild recompiles them too */
    char misses_path[1024] = "";
    {
        const char *base = SDL_GetBasePath();
        if (base) {
            snprintf(misses_path, sizeof misses_path, "%smisses.txt", base);
            recomp_load_misses(misses_path);
        }
    }

    SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, AUDIO_RATE};
    SDL_AudioStream *audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio) SDL_ResumeAudioStreamDevice(audio);
    else fprintf(stderr, "no audio: %s\n", SDL_GetError());

    Input in;
    input_init(&in);
    Menu menu = {0};
    /* --autotest-start: the start screen for 600 frames, a mouse click on Play (a real event
     * through the queue), 300 frames later the title screen (Play skips the logo and intro);
     * then from power-on again, START (Enter) during the intro's story text skips it too */
    int t_start_loops = 0, t_phase = 0, t_phase_at = 0;
    if ((!test_mode && !play_now && set.start_screen) || autotest_start) {
        menu_open_start_screen(&menu);
        start_screen_info(&menu, &ss, &set, disc_path);
    }
    const double frame_ns = 1e9 / FRAME_RATE;
    double audio_frac = 0.0;
    static int16_t samples[2048 * 2];
    Uint64 next = SDL_GetTicksNS();
    bool running = true, cursor_shown = true;
    bool was_rewinding = false, skipping = false, hold_input = false;
    bool skip_intro = false;       /* the scene being skipped is the power-on intro */
    bool skip_to_title = false;    /* Play on the start screen: on from power-on to the title screen */
    int rewind_held_frames = 0;
    /* autotest bookkeeping */
    int t_rewound = 0;
    bool t_saved = false, t_loaded = false, t_shots = false;
    long t_cine_start = -1, t_cine_end = -1;
    Uint64 t_cine_ns = 0;
    int t_min_health = 0x100;

    while (running) {
        if (autotest_start) {          /* phases wait for the skips to finish, at any speed */
            t_start_loops++;
            int since = t_start_loops - t_phase_at;
            bool title = pce.wram[RONDO_STATE] == RONDO_OPENING && pce.wram[RONDO_SUB] >= 2;
            if (t_phase == 0 && t_start_loops == 600) {
                TLOG("autotest-start: start screen up for %d frames, game frame %llu (%s)\n", t_start_loops - 1,
                     (unsigned long long)pce.frame,
                     pce.frame == 0 ? "the game waits for Play" : "THE GAME RAN BEHIND IT");
                float px = 0, py = 0, wx = 0, wy = 0;
                menu_item_center(&menu, ren, 0, &px, &py);             /* Play */
                SDL_RenderCoordinatesToWindow(ren, px, py, &wx, &wy);
                SDL_Event click = {0};
                click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                click.button.windowID = SDL_GetWindowID(win);
                click.button.button = SDL_BUTTON_LEFT;
                click.button.down = true;
                click.button.clicks = 1;
                click.button.which = TEST_INPUT;
                click.button.x = wx;
                click.button.y = wy;
                SDL_PushEvent(&click);
                t_phase = 1;
                t_phase_at = t_start_loops;
            } else if (t_phase == 1 && ((since >= 5 && !skipping) || since > 3000)) {
                TLOG("autotest-start: after clicking Play: %s, %d screen frames later game frame %llu, state %02X/%02X (%s)\n",
                     menu.open ? "STILL ON THE START SCREEN" : "playing", since, (unsigned long long)pce.frame,
                     pce.wram[RONDO_STATE], pce.wram[RONDO_SUB],
                     title ? "the title screen: logo and intro skipped" : "NOT AT THE TITLE SCREEN");
                start_game(&ss, ss.disc, err, sizeof err);      /* power-on, no Play */
                skip_to_title = skipping = false;
                t_phase = 2;
                t_phase_at = t_start_loops;
            } else if (t_phase == 2 && pce.frame >= 1000) {
                TLOG("autotest-start: power-on again, frame %llu: state %02X/%02X (the intro), pressing START\n",
                     (unsigned long long)pce.frame, pce.wram[RONDO_STATE], pce.wram[RONDO_SUB]);
                SDL_Event key = {0};
                key.type = SDL_EVENT_KEY_DOWN;
                key.key.windowID = SDL_GetWindowID(win);
                key.key.scancode = SDL_SCANCODE_RETURN;
                key.key.key = SDLK_RETURN;
                key.key.down = true;
                key.key.which = TEST_INPUT;
                SDL_PushEvent(&key);
                t_phase = 3;
                t_phase_at = t_start_loops;
            } else if (t_phase == 3 && ((since >= 5 && !skipping) || since > 3000)) {
                TLOG("autotest-start: %d screen frames after START: game frame %llu, state %02X/%02X (%s)\n", since,
                     (unsigned long long)pce.frame, pce.wram[RONDO_STATE], pce.wram[RONDO_SUB],
                     title ? "the title screen: intro skipped" : "INTRO NOT SKIPPED");
                running = false;
            }
        }
        bool start_pressed = false;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) { running = false; continue; }
            /* test runs only take the input they send themselves (marked TEST_INPUT), so a key
             * pressed on the desktop while one runs cannot change it */
            if (test_mode && !test_input(&ev)) continue;
            input_device_event(&in, &ev);
            if (menu.open) {
                MenuResult r = MENU_NOTHING;
                if (ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    SDL_Event e = ev;
                    SDL_ConvertEventToRenderCoordinates(ren, &e);
                    bool motion = ev.type == SDL_EVENT_MOUSE_MOTION;
                    int item = menu_item_at(&menu, ren, motion ? e.motion.x : e.button.x, motion ? e.motion.y : e.button.y);
                    if (motion) menu_hover(&menu, item);
                    else if (e.button.button == SDL_BUTTON_LEFT) r = menu_click(&menu, &set, item);
                    else if (e.button.button == SDL_BUTTON_RIGHT) r = menu_nav(&menu, &set, NAV_BACK);
                } else {
                    Nav nav = input_nav_event(&ev);
                    if (nav == NAV_NONE) continue;
                    r = menu_nav(&menu, &set, nav);
                }
                switch (r) {
                case MENU_CLOSE:
                    menu.open = false;
                    hold_input = true;     /* the button that closed the menu is not a game press */
                    break;
                case MENU_SAVE_STATE:
                case MENU_LOAD_STATE: {
                    char path[1200];
                    state_path(path, sizeof path, &ss, set.slot);
                    if (r == MENU_SAVE_STATE) {
                        if (pce_state_save(&pce, path, ss.disc_id, err, sizeof err))
                            snprintf(menu.status, sizeof menu.status, "State %d saved", set.slot);
                        else snprintf(menu.status, sizeof menu.status, "Save failed: %.60s", err);
                    } else if (pce_state_load(&pce, path, ss.disc_id, err, sizeof err)) {
                        if (have_rewind) rewind_reset(&rw, &pce);
                        game_watch_reset(&gw, &pce);
                        pce.bram_dirty = false;
                        menu.open = false;
                        hold_input = true;
                        show_osd("State %d loaded", set.slot);
                    } else {
                        snprintf(menu.status, sizeof menu.status, "Slot %d: %.60s", set.slot, err);
                    }
                    break;
                }
                case MENU_RESET:
                    if (start_game(&ss, ss.disc, err, sizeof err)) {
                        menu.open = false;
                        hold_input = true;
                        show_osd("Game reset");
                    } else {
                        snprintf(menu.status, sizeof menu.status, "%.100s", err);
                    }
                    break;
                case MENU_QUIT:
                    running = false;
                    break;
                case MENU_PLAY:                /* start screen: the game from power-on */
                    if (start_game(&ss, ss.disc, err, sizeof err)) {
                        menu.open = false;
                        hold_input = true;
                        skipping = was_rewinding = false;
                        /* Play was the player's start: on to the title screen, unless story
                         * scenes are never skipped */
                        skip_to_title = set.skip != SKIP_OFF;
                    } else {
                        snprintf(menu.status, sizeof menu.status, "%.100s", err);
                    }
                    break;
                case MENU_START_SCREEN:
                    if (start_game(&ss, ss.disc, err, sizeof err)) {
                        menu_open_start_screen(&menu);
                        start_screen_info(&menu, &ss, &set, disc_path);
                        skipping = was_rewinding = skip_to_title = false;
                    } else {
                        snprintf(menu.status, sizeof menu.status, "%.100s", err);
                    }
                    break;
                case MENU_SETUP:
                    if (open_setup(menu.status, sizeof menu.status)) running = false;
                    break;
                case MENU_LANGUAGE:
                    switch_language(&ss, &set, disc_path, true, win, ren, menu.status, sizeof menu.status);
                    if (menu_on_start_screen(&menu)) start_screen_info(&menu, &ss, &set, disc_path);
                    if (!test_mode) settings_save(&set, settings_path);
                    break;
                case MENU_VIDEO_CHANGED:
                    SDL_SetWindowFullscreen(win, set.fullscreen);
                    if (!test_mode) settings_save(&set, settings_path);
                    break;
                case MENU_SETTINGS_CHANGED:
                    if (!test_mode) settings_save(&set, settings_path);
                    break;
                default:
                    break;
                }
                continue;
            }
            /* playing */
            bool open_menu = false;
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                SDL_Scancode sc = ev.key.scancode;
                if (sc == SDL_SCANCODE_F1 || sc == SDL_SCANCODE_ESCAPE) open_menu = true;
                else if (sc == SDL_SCANCODE_F11) {
                    set.fullscreen = !set.fullscreen;
                    SDL_SetWindowFullscreen(win, set.fullscreen);
                    if (!test_mode) settings_save(&set, settings_path);
                } else if (sc == SDL_SCANCODE_F6 || sc == SDL_SCANCODE_F7) {
                    set.slot = (set.slot + (sc == SDL_SCANCODE_F7 ? 1 : 8) - 1) % 9 + 1;
                    show_osd("State slot %d", set.slot);
                    if (!test_mode) settings_save(&set, settings_path);
                } else if (sc == SDL_SCANCODE_F5 || sc == SDL_SCANCODE_F8) {
                    char path[1200];
                    state_path(path, sizeof path, &ss, set.slot);
                    if (sc == SDL_SCANCODE_F5) {
                        if (pce_state_save(&pce, path, ss.disc_id, err, sizeof err)) show_osd("State %d saved", set.slot);
                        else show_osd("Save failed: %s", err);
                    } else if (pce_state_load(&pce, path, ss.disc_id, err, sizeof err)) {
                        show_osd("State %d loaded", set.slot);
                        if (have_rewind) rewind_reset(&rw, &pce);
                        game_watch_reset(&gw, &pce);
                        input_cancel_macros(&in);
                        pce.bram_dirty = false;
                    } else {
                        show_osd("Slot %d: %s", set.slot, err);
                    }
                }
            }
            if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && ev.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) open_menu = true;
            if (input_is_start(&ev)) start_pressed = true;
            if (open_menu && !test_mode) {
                menu_open(&menu);
                input_cancel_macros(&in);
            }
        }
        if (menu.open) {
            Nav nav = test_mode ? NAV_NONE : input_nav_stick(&in);
            if (nav != NAV_NONE) {
                MenuResult r = menu_nav(&menu, &set, nav);    /* the stick only steps and changes values */
                if (r == MENU_LANGUAGE) {
                    switch_language(&ss, &set, disc_path, true, win, ren, menu.status, sizeof menu.status);
                    if (menu_on_start_screen(&menu)) start_screen_info(&menu, &ss, &set, disc_path);
                }
                if (r == MENU_SETTINGS_CHANGED || r == MENU_VIDEO_CHANGED || r == MENU_LANGUAGE) {
                    if (r == MENU_VIDEO_CHANGED) SDL_SetWindowFullscreen(win, set.fullscreen);
                    if (!test_mode) settings_save(&set, settings_path);
                }
            }
        }

        GameCheats cheats = {set.cheat_health, set.cheat_hearts, set.cheat_lives, set.maria_unlocked, set.play_maria};
        {   /* widescreen fills the window's shape */
            int ww, wh;
            SDL_GetRenderOutputSize(ren, &ww, &wh);
            wide.want = set.widescreen && wh > 0
                            ? wide_extent((double)ww / (double)wh, video_picture_aspect(&video, &set)) : 0;
        }
        bool rewinding = false, ran_frame = false;
        if (menu_on_start_screen(&menu)) {
            /* the start screen: the game waits for Play (nothing runs, nothing sounds) */
        } else if (!menu.open) {
            /* Play on the start screen goes on to the title screen: from power-on (the logo,
             * state $00) through the intro */
            bool to_title = skip_to_title && (game_state(&pce) == 0x00 || game_in_intro(&pce));
            if (skip_to_title && !to_title) skip_to_title = false;
            if (!skipping && (to_title || (game_in_skippable(&pce) &&
                              (set.skip == SKIP_AUTO || (set.skip == SKIP_START && start_pressed))))) {
                skipping = true;
                skip_intro = !game_in_cinematic(&pce);
                if (features_dir) { t_cine_start = (long)pce.frame; t_cine_ns = SDL_GetTicksNS(); }
            }
            rewinding = set.rewind && have_rewind && !skipping && !test_mode && input_rewind_held(&in);
            if (autotest) {
                uint64_t f = pce.frame;
                char path[1200];
                state_path(path, sizeof path, &ss, 9);
                if (f == 600 && !t_saved) {
                    t_saved = true;
                    bool ok = pce_state_save(&pce, path, ss.disc_id, err, sizeof err);
                    TLOG("autotest: save at frame 600: %s\n", ok ? "ok" : err);
                } else if (f == 700 && !t_loaded) {
                    t_loaded = true;
                    bool ok = pce_state_load(&pce, path, ss.disc_id, err, sizeof err);
                    if (ok) rewind_reset(&rw, &pce);
                    TLOG("autotest: load at frame 700: %s, now at frame %llu\n", ok ? "ok" : err, (unsigned long long)pce.frame);
                }
                if (t_loaded && t_rewound < 200 && (t_rewound > 0 || f >= 800)) { rewinding = true; t_rewound++; }
                else if (t_rewound == 200 && was_rewinding) {
                    uint64_t back;
                    memcpy(&back, rw.cur + offsetof(Pce, frame), sizeof back);
                    TLOG("autotest: rewound for 200 frames, resuming at frame %llu\n", (unsigned long long)back);
                }
                if (t_rewound == 200 && f >= 1100 && !rewinding) running = false;
            }

            if (skipping) {
                /* run the scene as fast as possible, silently, ~12 ms per displayed frame (input:
                 * in a story scene an occasional I press, in case it waits for a button; none in
                 * the intro, which ignores buttons, so the title screen is not passed by) */
                Uint64 t0 = SDL_GetTicksNS();
                while ((skip_intro ? game_in_intro(&pce) || (skip_to_title && game_state(&pce) == 0x00)
                                   : game_in_cinematic(&pce)) &&
                       SDL_GetTicksNS() - t0 < 12000000) {
                    pce.pad = !skip_intro && (pce.frame & 15) < 4 ? PAD_I : 0;
                    run_frame();
                    pce_audio_render(&pce, samples, 735);
                    game_apply_cheats(&pce, &cheats, &gw);
                    flush_bram(&ss);
                }
                ran_frame = true;
                bool still = skip_intro ? game_in_intro(&pce) || (skip_to_title && game_state(&pce) == 0x00)
                                        : game_in_cinematic(&pce);
                if (!still) {
                    skipping = false;
                    if (skip_intro) skip_to_title = false;
                    hold_input = true;
                    if (have_rewind) rewind_record(&rw, &pce, 0);   /* one step back over the whole scene */
                    if (features_dir) {
                        t_cine_end = (long)pce.frame;
                        TLOG("features: %s frames %ld-%ld skipped in %.2f s\n", skip_intro ? "intro" : "cinematic",
                             t_cine_start, t_cine_end, (double)(SDL_GetTicksNS() - t_cine_ns) / 1e9);
                    }
                }
            } else if (rewinding) {
                /* step back (faster after holding for 3 s), then replay the frame to show it */
                int steps = rewind_held_frames > 180 ? 2 : 1;
                uint8_t pad = 0;
                bool moved = false;
                for (int i = 0; i < steps; i++) moved |= rewind_step(&rw, &pad);
                if (moved) {
                    pce_state_restore(&pce, rw.cur);
                    pce.pad = pad;
                    run_frame();
                    ran_frame = true;
                }
                pce.bram_dirty = false;            /* rewinding never overwrites the save file */
                rewind_held_frames++;
                was_rewinding = true;
            } else {
                if (was_rewinding) {               /* continue from the exact rewound state */
                    pce_state_restore(&pce, rw.cur);
                    pce.bram_dirty = false;
                    game_watch_reset(&gw, &pce);
                    was_rewinding = false;
                    rewind_held_frames = 0;
                    input_cancel_macros(&in);
                }
                uint8_t pad = features_dir ? capture_pad((int)pce.frame) : test_mode ? 0 : input_game(&in);
                if (hold_input && !features_dir) { /* until everything is let go */
                    if (pad) pad = 0;
                    else hold_input = false;
                }
                pce.pad = pad;
                run_frame();
                game_apply_cheats(&pce, &cheats, &gw);
                game_apply_run(&pce, !test_mode && input_run_held(&in), &gw);
                ran_frame = true;
                if (have_rewind && set.rewind) rewind_record(&rw, &pce, pce.pad);
                flush_bram(&ss);
                if (features_dir && game_playing(&pce) && pce.frame > 16200 && pce.wram[RONDO_HEALTH] < t_min_health)
                    t_min_health = pce.wram[RONDO_HEALTH];
            }
        }

        /* audio: one frame's worth, kept within ~100 ms of queued data (silent while skipping,
         * rewinding or in the menu) */
        if (ran_frame) {
            audio_frac += AUDIO_RATE / FRAME_RATE;
            int n = (int)audio_frac;
            audio_frac -= n;
            if (!skipping) {
                pce_audio_render(&pce, samples, n);
                if (rewinding) memset(samples, 0, (size_t)n * 4);
                else if (set.volume < 10)
                    for (int i = 0; i < n * 2; i++) samples[i] = (int16_t)(samples[i] * set.volume / 10);
                if (audio && SDL_GetAudioStreamQueued(audio) < AUDIO_RATE * 4 / 10)
                    SDL_PutAudioStreamData(audio, samples, n * 4);
            }
        }

        /* video */
        video_upload(&video, pce.vdc.fb, pce.vdc.fb_w, pce.vdc.fb_h, PCE_FB_MAX_W, pce.vdc.fb_wide);
        if (features_dir && !t_shots && pce.frame >= 17880) {
            /* screenshots: 16:9 window with each picture mode, then the menu pages */
            t_shots = true;
            TLOG("features: frame %llu state %02X/%02X drawn %d px wider (want %d, game changes %s, "
                 "scenery %d, map %04X)\n", (unsigned long long)pce.frame, pce.wram[0x30], pce.wram[0x31],
                 pce.vdc.fb_wide, wide.want, wide.patched ? "in" : "out", wide.scenery_moving, pce.vdc.reg[9]);
            static const struct { int aspect, integer, filter, sides; const char *name; } modes[] = {
                {ASPECT_4_3, 0, FILTER_SHARP, SIDES_BLUR, "wide_4x3_blur"},
                {ASPECT_STRETCH, 0, FILTER_SHARP, SIDES_BLUR, "wide_stretch"},
                {ASPECT_SQUARE, 1, FILTER_NEAREST, SIDES_BLACK, "wide_square_integer"},
            };
            Settings keep = set;
            char path[1200];
            for (size_t i = 0; i < sizeof modes / sizeof modes[0]; i++) {
                set.aspect = modes[i].aspect;
                set.scaling = modes[i].integer ? SCALE_WHOLE : SCALE_FIT;
                set.filter = modes[i].filter;
                set.sides = modes[i].sides;
                video_draw(&video, &set);
                snprintf(path, sizeof path, "%s/%s.bmp", features_dir, modes[i].name);
                screenshot(ren, path);
            }
            set = keep;
            Menu m = {0};
            menu_open(&m);
            m.opened_ms = 0;                   /* shown fully open */
            const char *names[] = {"menu_main", "menu_video", "menu_controls"};
            for (int i = 0; i < 3; i++) {
                if (i == 1) { for (int d = 0; d < 4; d++) menu_nav(&m, &set, NAV_DOWN); menu_nav(&m, &set, NAV_OK); }
                if (i == 2) { menu_nav(&m, &set, NAV_BACK); for (int d = 0; d < 3; d++) menu_nav(&m, &set, NAV_DOWN); menu_nav(&m, &set, NAV_OK); }
                video_draw(&video, &set);
                menu_draw(&m, &set, ren);
                snprintf(path, sizeof path, "%s/%s.bmp", features_dir, names[i]);
                screenshot(ren, path);
            }
            /* the notices (top left; START: skip, bottom right) */
            video_draw(&video, &set);
            draw_banner(ren, "State 1 saved", false);
            draw_banner(ren, "START: skip", true);
            snprintf(path, sizeof path, "%s/notices.bmp", features_dir);
            screenshot(ren, path);
            {   /* the start screen: a screenshot; START plays, Back stays, an option page returns
                 * to it, and the mouse finds each item in order */
                Menu sm = {0};
                menu_open_start_screen(&sm);
                sm.opened_ms = 0;
                start_screen_info(&sm, &ss, &set, disc_path);
                video_draw(&video, &set);
                menu_draw(&sm, &set, ren);
                snprintf(path, sizeof path, "%s/start_screen.bmp", features_dir);
                screenshot(ren, path);
                MenuResult on_start = menu_nav(&sm, &set, NAV_MENU), on_back = menu_nav(&sm, &set, NAV_BACK);
                int ww, wh, found = 0, last = -1;
                SDL_GetRenderOutputSize(ren, &ww, &wh);
                bool in_order = true;
                float col_x = 0, col_y = 0;
                menu_item_center(&sm, ren, 0, &col_x, &col_y);        /* down the panel's middle */
                for (int y = 0; y < wh; y++) {
                    int it = menu_item_at(&sm, ren, col_x, (float)y);
                    if (it < 0 || it == last) continue;
                    if (it != last + 1) in_order = false;
                    last = it;
                    found++;
                }
                menu_click(&sm, &set, 4);                        /* Video */
                menu_nav(&sm, &set, NAV_BACK);
                MenuResult after = menu_nav(&sm, &set, NAV_MENU);
                TLOG("features: start screen: START %s, Back %s, %d items under the mouse%s, Video then Back %s\n",
                     on_start == MENU_PLAY ? "plays" : "DOES NOT PLAY", on_back == MENU_NOTHING ? "stays" : "LEAVES",
                     found, in_order ? " in order" : " OUT OF ORDER",
                     after == MENU_PLAY ? "returns to it" : "DOES NOT RETURN");
            }
            char merr[200];
            int mbad = menu_check(&set, ren, merr, sizeof merr);
            TLOG("features: menu check: %s%s\n", mbad ? "PROBLEM at " : "every value of every option ok",
                 mbad ? merr : "");
        }
        static bool t_stage1_shot;
        if (features_dir && !t_stage1_shot && pce.frame >= 20060) {
            /* Stage 1 with the window's widescreen */
            t_stage1_shot = true;
            char path[1200];
            video_draw(&video, &set);
            snprintf(path, sizeof path, "%s/stage1_widescreen.bmp", features_dir);
            screenshot(ren, path);
            TLOG("features: Stage 1 at frame %llu drawn %d px wider on each side (game changes %s)\n",
                 (unsigned long long)pce.frame, pce.vdc.fb_wide, wide.patched ? "in place" : "missing");
        }
        if (menu.open != cursor_shown) {           /* the mouse pointer only over menus */
            cursor_shown = menu.open;
            if (cursor_shown) SDL_ShowCursor();
            else SDL_HideCursor();
        }
        if (!menu_on_start_screen(&menu)) video_draw(&video, &set);   /* the start screen draws it all */
        if (skipping) {
            draw_banner(ren, skip_intro ? "Skipping the intro..." : "Skipping cinematic...", false);
        } else if (rewinding) {
            char line[64];
            float secs = (float)rewind_frames(&rw) / (float)FRAME_RATE;
            snprintf(line, sizeof line, "<< REWIND  %d:%02d", (int)secs / 60, (int)secs % 60);
            draw_banner(ren, line, false);
        } else if (SDL_GetTicks() < osd_until) {
            draw_banner(ren, osd_text, false);
        }
        if (!menu.open && !skipping && set.skip == SKIP_START && game_in_skippable(&pce))
            draw_banner(ren, "START: skip", true);
        if (menu.open) menu_draw(&menu, &set, ren);
        SDL_RenderPresent(ren);

        if (features_dir && pce.frame >= 20100) {
            TLOG("features: lowest health while playing (cheat on): %d of 92; hearts %02X; lives %02X\n",
                 t_min_health, pce.wram[RONDO_HEARTS], pce.wram[RONDO_LIVES]);
            /* the Language option, without asking: the other language's disc if installed */
            int from = ss.language;
            set.language = from == LANG_JA ? LANG_EN : LANG_JA;
            char status[160];
            switch_language(&ss, &set, disc_path, false, win, ren, status, sizeof status);
            if (ss.language != from) {
                for (int i = 0; i < 900; i++) {
                    pce.pad = 0;
                    run_frame();
                }
            }
            TLOG("features: played the %s disc; Language -> %s: %s%s\n", from == LANG_JA ? "Japanese" : "English",
                 from == LANG_JA ? "English" : "Japanese", status,
                 ss.language != from ? (pce.wram[RONDO_STATE] == 0x02 ? " (attract mode after 900 frames)" : " (not running?)") : "");
            running = false;
        }
        if (features_dir) continue;   /* run uncapped */
        next += (Uint64)frame_ns;
        Uint64 now = SDL_GetTicksNS();
        if (next > now) SDL_DelayNS(next - now);
        else if (now - next > (Uint64)(frame_ns * 5)) next = now;   /* fell behind: resync */
    }

    flush_bram(&ss);
    if (!test_mode) settings_save(&set, settings_path);
    if (misses_path[0] && recomp_stats()->miss_entries) recomp_write_misses(misses_path);
    /* rondo.log next to the executable: what ran natively this session */
    {
        const char *base = SDL_GetBasePath();
        char log_path[1024];
        if (base) {
            snprintf(log_path, sizeof log_path, "%srondo.log", base);
            FILE *lf = fopen(log_path, "w");
            if (lf) {
                const RecompStats *rs = recomp_stats();
                uint64_t tot = rs->native_cycles + rs->interp_cycles;
                fputs(autotest_log, lf);
                fprintf(lf, "build: %s\nframes: %llu\nrecompiled chunks: %d, entry points: %d\n"
                            "CPU cycles native: %.2f%%\nchunk calls: %llu\nentry points without native code: %u\n",
                        recomp_build_id, (unsigned long long)pce.frame, recomp_chunk_count, recomp_entry_count,
                        tot ? 100.0 * (double)rs->native_cycles / (double)tot : 0.0,
                        (unsigned long long)rs->calls, rs->miss_entries);
                fclose(lf);
            }
        }
    }
    input_close(&in);
    video_free(&video);
    if (audio) SDL_DestroyAudioStream(audio);
    SDL_Quit();
    return 0;
}
