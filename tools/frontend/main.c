/* Windowed frontend (SDL3): video, keyboard/gamepad input, CD audio.
 *
 * usage: rondo [game.cue]
 *   With no argument it uses the remembered disc, then searches the folders around
 *   the executable, then asks once with a file picker and remembers the choice.
 *
 * Keyboard: arrows = d-pad, X = I, Z = II, Enter = RUN, Right Shift / Backspace = SELECT,
 *           F11 = fullscreen, Esc = quit.
 *           F5 = save state, F8 = load state, F6 / F7 = previous / next slot (1-9),
 *           hold R = rewind (also the controller's left trigger or left shoulder).
 * Gamepad:  d-pad/left stick, East (B) = I, South (A) = II, Start = RUN, Back = SELECT.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "locate.h"
#include "pce/pce.h"
#include "pce/state.h"
#include "recomp/recomp.h"

#define FRAME_RATE (7159090.0 / (455.0 * 263.0))   /* 59.826 Hz */
#define AUDIO_RATE 44100

static uint8_t read_input(SDL_Gamepad **pads, int n_pads)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    uint8_t m = 0;
    if (k[SDL_SCANCODE_UP]) m |= PAD_UP;
    if (k[SDL_SCANCODE_DOWN]) m |= PAD_DOWN;
    if (k[SDL_SCANCODE_LEFT]) m |= PAD_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) m |= PAD_RIGHT;
    if (k[SDL_SCANCODE_X]) m |= PAD_I;
    if (k[SDL_SCANCODE_Z]) m |= PAD_II;
    if (k[SDL_SCANCODE_RETURN]) m |= PAD_RUN;
    if (k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_BACKSPACE]) m |= PAD_SELECT;
    for (int i = 0; i < n_pads; i++) {
        SDL_Gamepad *g = pads[i];
        if (!g) continue;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP)) m |= PAD_UP;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) m |= PAD_DOWN;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) m |= PAD_LEFT;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) m |= PAD_RIGHT;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_EAST)) m |= PAD_I;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_SOUTH)) m |= PAD_II;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START)) m |= PAD_RUN;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK)) m |= PAD_SELECT;
        int x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
        int y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
        if (x < -16000) m |= PAD_LEFT;
        if (x > 16000) m |= PAD_RIGHT;
        if (y < -16000) m |= PAD_UP;
        if (y > 16000) m |= PAD_DOWN;
    }
    /* opposite directions cannot be pressed together on a real pad */
    if ((m & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT)) m &= (uint8_t)~(PAD_LEFT | PAD_RIGHT);
    if ((m & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN)) m &= (uint8_t)~(PAD_UP | PAD_DOWN);
    return m;
}

/* Rewind is held on R, the left trigger or the left shoulder button. */
static bool rewind_pressed(SDL_Gamepad **pads, int n_pads)
{
    if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_R]) return true;
    for (int i = 0; i < n_pads; i++) {
        SDL_Gamepad *g = pads[i];
        if (!g) continue;
        if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000) return true;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) return true;
    }
    return false;
}

/* ---- on-screen messages (SDL's built-in 8x8 font) ------------------------------ */

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

static void draw_osd(SDL_Renderer *ren, const char *text, int window_w)
{
    float scale = window_w >= 1600 ? 4.0f : window_w >= 800 ? 3.0f : 2.0f;
    SDL_SetRenderScale(ren, scale, scale);
    float w = 8.0f * (float)strlen(text) + 8, h = 16;
    SDL_FRect bg = {4, 4, w, h};
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 170);
    SDL_RenderFillRect(ren, &bg);
    SDL_SetRenderDrawColor(ren, 255, 230, 160, 255);
    SDL_RenderDebugText(ren, 8, 8, text);
    SDL_SetRenderScale(ren, 1.0f, 1.0f);
}

/* ---- choosing the disc image ------------------------------------------------- */

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

int main(int argc, char **argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    char title[256];
    snprintf(title, sizeof title, "Rondo Recomp (%s)", recomp_build_id);
    SDL_Window *win = SDL_CreateWindow(title, 960, 720, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, NULL) : NULL;
    SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                               PCE_FB_MAX_W, PCE_FB_MAX_H) : NULL;
    if (!tex) {
        fprintf(stderr, "SDL video setup: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    /* --autotest: save/load a state and rewind without user input, then quit (for checks) */
    bool autotest = false;
    const char *disc_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--autotest")) autotest = true;
        else disc_arg = argv[i];
    }
    char autotest_log[512] = "";
    char disc_path[1024];
    if (!choose_disc(win, ren, disc_arg, disc_path, sizeof disc_path)) {
        SDL_Quit();
        return 0;
    }
    static Pce pce;
    char err[256];
    if (!pce_init(&pce, disc_path, err, sizeof err) || !pce_boot(&pce, err, sizeof err)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Rondo Recomp", err, win);
        SDL_Quit();
        return 1;
    }
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
    /* save states: pref folder/states/slotN.state, tagged with the disc fingerprint */
    uint64_t disc_id = 0;
    discid_identify(disc_path, &disc_id);
    char states_dir[1024] = "";
    {
        char *dir = SDL_GetPrefPath("RondoRecomp", "RondoRecomp");
        if (dir) { snprintf(states_dir, sizeof states_dir, "%sstates", dir); SDL_free(dir); SDL_CreateDirectory(states_dir); }
    }
    int slot = 1;
    /* rewind: every frame's change is kept (about 1 KB each), up to 256 MB / 30 minutes */
    static Rewind rw;
    bool have_rewind = rewind_init(&rw, 256u << 20, (int)(FRAME_RATE * 60 * 30));
    if (have_rewind) rewind_reset(&rw, &pce);
    bool was_rewinding = false;
    int rewind_held_frames = 0;

    /* save data (backup RAM) lives in the user's pref folder */
    char save_path[1024] = "";
    {
        char *dir = SDL_GetPrefPath("RondoRecomp", "RondoRecomp");
        if (dir) { snprintf(save_path, sizeof save_path, "%sbram.bin", dir); SDL_free(dir); }
        size_t n = 0;
        void *data = save_path[0] ? SDL_LoadFile(save_path, &n) : NULL;
        if (data && n >= 0x800) memcpy(pce.bram, data, 0x800);
        SDL_free(data);
        pce.bram_dirty = false;
    }

    SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, AUDIO_RATE};
    SDL_AudioStream *audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio) SDL_ResumeAudioStreamDevice(audio);
    else fprintf(stderr, "no audio: %s\n", SDL_GetError());

    SDL_Gamepad *pads[8] = {0};
    int n_pads = 8;   /* slots; NULL = free */
    {
        int n = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&n);   /* controllers already connected */
        for (int i = 0; i < n && i < 8; i++) pads[i] = SDL_OpenGamepad(ids[i]);
        SDL_free(ids);
    }
    const double frame_ns = 1e9 / FRAME_RATE;
    double audio_frac = 0.0;
    static int16_t samples[2048 * 2];
    Uint64 next = SDL_GetTicksNS();
    bool running = true;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (ev.key.scancode == SDL_SCANCODE_ESCAPE) running = false;
                else if (ev.key.scancode == SDL_SCANCODE_F11 && !ev.key.repeat)
                    SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
                else if ((ev.key.scancode == SDL_SCANCODE_F6 || ev.key.scancode == SDL_SCANCODE_F7) && !ev.key.repeat) {
                    slot = (slot + (ev.key.scancode == SDL_SCANCODE_F7 ? 1 : 8) - 1) % 9 + 1;
                    show_osd("State slot %d", slot);
                } else if (ev.key.scancode == SDL_SCANCODE_F5 && !ev.key.repeat && states_dir[0]) {
                    char path[1100];
                    snprintf(path, sizeof path, "%s/slot%d.state", states_dir, slot);
                    if (pce_state_save(&pce, path, disc_id, err, sizeof err)) show_osd("State %d saved", slot);
                    else show_osd("Save failed: %s", err);
                } else if (ev.key.scancode == SDL_SCANCODE_F8 && !ev.key.repeat && states_dir[0]) {
                    char path[1100];
                    snprintf(path, sizeof path, "%s/slot%d.state", states_dir, slot);
                    if (pce_state_load(&pce, path, disc_id, err, sizeof err)) {
                        show_osd("State %d loaded", slot);
                        if (have_rewind) rewind_reset(&rw, &pce);
                        pce.bram_dirty = false;
                    } else {
                        show_osd("Slot %d: %s", slot, err);
                    }
                }
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (!SDL_GetGamepadFromID(ev.gdevice.which)) {
                    for (int i = 0; i < 8; i++)
                        if (!pads[i]) { pads[i] = SDL_OpenGamepad(ev.gdevice.which); break; }
                }
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                for (int i = 0; i < 8; i++)
                    if (pads[i] && SDL_GetGamepadID(pads[i]) == ev.gdevice.which) {
                        SDL_CloseGamepad(pads[i]);
                        pads[i] = NULL;
                    }
                break;
            default:
                break;
            }
        }

        bool rewinding = have_rewind && rewind_pressed(pads, n_pads);
        if (autotest) {
            uint64_t f = pce.frame;
            size_t used = strlen(autotest_log);
            char path[1100];
            snprintf(path, sizeof path, "%s/slot9.state", states_dir);
            static bool saved, loaded;
            if (f == 600 && !saved) {
                saved = true;
                bool ok = pce_state_save(&pce, path, disc_id, err, sizeof err);
                snprintf(autotest_log + used, sizeof autotest_log - used, "autotest: save at frame 600: %s\n",
                         ok ? "ok" : err);
            } else if (f == 700 && !loaded) {
                loaded = true;
                bool ok = pce_state_load(&pce, path, disc_id, err, sizeof err);
                if (ok) rewind_reset(&rw, &pce);
                snprintf(autotest_log + used, sizeof autotest_log - used,
                         "autotest: load at frame 700: %s, now at frame %llu\n", ok ? "ok" : err,
                         (unsigned long long)pce.frame);
            }
            static int rewound;
            if (loaded && rewound < 200 && (rewound > 0 || f >= 800)) { rewinding = true; rewound++; }
            else if (rewound == 200 && was_rewinding) {
                uint64_t back;
                memcpy(&back, rw.cur + offsetof(Pce, frame), sizeof back);
                snprintf(autotest_log + used, sizeof autotest_log - used,
                         "autotest: rewound for 200 frames, resuming at frame %llu\n", (unsigned long long)back);
            }
            if (rewound == 200 && f >= 1100 && !rewinding) running = false;
        }
        if (rewinding) {
            /* step back (faster after holding for 3 s), then replay the frame to show it */
            int steps = rewind_held_frames > 180 ? 2 : 1;
            uint8_t pad = 0;
            bool moved = false;
            for (int i = 0; i < steps; i++) moved |= rewind_step(&rw, &pad);
            if (moved) {
                pce_state_restore(&pce, rw.cur);
                pce.pad = pad;
                pce_run_frame(&pce);
            }
            pce.bram_dirty = false;            /* rewinding never overwrites the save file */
            rewind_held_frames++;
            was_rewinding = true;
        } else {
            if (was_rewinding) {               /* continue from the exact rewound state */
                pce_state_restore(&pce, rw.cur);
                pce.bram_dirty = false;
                was_rewinding = false;
                rewind_held_frames = 0;
            }
            pce.pad = read_input(pads, n_pads);
            pce_run_frame(&pce);
            if (have_rewind) rewind_record(&rw, &pce, pce.pad);
            if (pce.bram_dirty && save_path[0]) {
                SDL_SaveFile(save_path, pce.bram, 0x800);
                pce.bram_dirty = false;
            }
        }

        /* audio: one frame's worth, kept within ~100 ms of queued data (silent while rewinding) */
        audio_frac += AUDIO_RATE / FRAME_RATE;
        int n = (int)audio_frac;
        audio_frac -= n;
        pce_audio_render(&pce, samples, n);
        if (rewinding) memset(samples, 0, (size_t)n * 4);
        if (audio && SDL_GetAudioStreamQueued(audio) < AUDIO_RATE * 4 / 10)
            SDL_PutAudioStreamData(audio, samples, n * 4);

        /* video: 4:3 picture, letterboxed in the window */
        SDL_Rect src = {0, 0, pce.vdc.fb_w, pce.vdc.fb_h};
        if (src.w > 0 && src.h > 0)
            SDL_UpdateTexture(tex, &src, pce.vdc.fb, PCE_FB_MAX_W * 4);
        int ww, wh;
        SDL_GetRenderOutputSize(ren, &ww, &wh);
        float dh = (float)wh, dw = dh * 4.0f / 3.0f;
        if (dw > (float)ww) { dw = (float)ww; dh = dw * 3.0f / 4.0f; }
        SDL_FRect fsrc = {0, 0, (float)src.w, (float)src.h};
        SDL_FRect dst = {((float)ww - dw) / 2, ((float)wh - dh) / 2, dw, dh};
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, &fsrc, &dst);
        if (rewinding) {
            char line[64];
            float secs = (float)rewind_frames(&rw) / (float)FRAME_RATE;
            snprintf(line, sizeof line, "<< REWIND  %d:%02d", (int)secs / 60, (int)secs % 60);
            draw_osd(ren, line, ww);
        } else if (SDL_GetTicks() < osd_until) {
            draw_osd(ren, osd_text, ww);
        }
        SDL_RenderPresent(ren);

        next += (Uint64)frame_ns;
        Uint64 now = SDL_GetTicksNS();
        if (next > now) SDL_DelayNS(next - now);
        else if (now - next > (Uint64)(frame_ns * 5)) next = now;   /* fell behind: resync */
    }

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
    if (audio) SDL_DestroyAudioStream(audio);
    SDL_Quit();
    return 0;
}
