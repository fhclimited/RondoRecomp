/* RondoRecomp Setup, windowed: choose your zipped disc image, watch it being turned into
 * rondo.exe, then play. Runs the same steps as rondo_setup (tools/setup/main.c) in a worker
 * thread; text is drawn with SDL3's built-in debug font, so no font files are needed. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>

int setup_main(int argc, char **argv);
extern void (*setup_line_hook)(const char *text);
extern void (*setup_progress_hook)(const char *what, int pct);

enum { IDLE, RUNNING, DONE, FAILED };

#define MAX_LINES 300
#define COLS 76
static SDL_Mutex *mtx;
static char lines[MAX_LINES][COLS + 1];
static int n_lines;
static char partial[1024];
static char prog_what[128];
static int prog_pct = -1;
static SDL_AtomicInt state;
static char input_path[1024];
static bool rebuild_mode;
static char base_dir[1024];

static void push_line(const char *text)
{
    /* wrap long lines */
    size_t len = strlen(text);
    size_t off = 0;
    do {
        if (n_lines == MAX_LINES) {
            memmove(lines[0], lines[1], sizeof lines[0] * (MAX_LINES - 1));
            n_lines--;
        }
        size_t n = len - off > COLS ? COLS : len - off;
        memcpy(lines[n_lines], text + off, n);
        lines[n_lines][n] = 0;
        n_lines++;
        off += n;
    } while (off < len);
}

static void gui_line(const char *text)
{
    SDL_LockMutex(mtx);
    for (const char *c = text; *c; c++) {
        size_t l = strlen(partial);
        if (*c == '\n') { push_line(partial); partial[0] = 0; }
        else if (*c != '\r' && l + 1 < sizeof partial) { partial[l] = *c; partial[l + 1] = 0; }
    }
    SDL_UnlockMutex(mtx);
}

static void gui_progress(const char *what, int pct)
{
    SDL_LockMutex(mtx);
    snprintf(prog_what, sizeof prog_what, "%s", what);
    prog_pct = pct;
    SDL_UnlockMutex(mtx);
}

static int SDLCALL worker(void *arg)
{
    (void)arg;
    char *argv[8];
    int argc = 0;
    argv[argc++] = "RondoRecomp Setup";
    if (rebuild_mode) argv[argc++] = "--rebuild";
    else argv[argc++] = input_path;
    argv[argc++] = "--out";
    argv[argc++] = base_dir;
    argv[argc] = NULL;
    int r = setup_main(argc, argv);
    SDL_LockMutex(mtx);
    if (partial[0]) { push_line(partial); partial[0] = 0; }
    prog_pct = -1;
    SDL_UnlockMutex(mtx);
    SDL_SetAtomicInt(&state, r == 0 ? DONE : FAILED);
    return r;
}

static void start(bool rebuild)
{
    rebuild_mode = rebuild;
    SDL_LockMutex(mtx);
    n_lines = 0;
    partial[0] = 0;
    prog_pct = -1;
    SDL_UnlockMutex(mtx);
    SDL_SetAtomicInt(&state, RUNNING);
    SDL_Thread *t = SDL_CreateThread(worker, "setup", NULL);
    if (t) SDL_DetachThread(t);
    else SDL_SetAtomicInt(&state, FAILED);
}

static void SDLCALL on_pick(void *userdata, const char *const *files, int filter)
{
    (void)userdata;
    (void)filter;
    if (!files || !files[0]) return;                 /* cancelled */
    snprintf(input_path, sizeof input_path, "%s", files[0]);
    start(false);
}

static bool exists(const char *name)
{
    char path[1100];
    snprintf(path, sizeof path, "%s%s", base_dir, name);
    return SDL_GetPathInfo(path, NULL);
}

typedef struct Button { float x, y, w, h; const char *label; int id; } Button;
enum { B_CHOOSE = 1, B_PLAY, B_REBUILD, B_QUIT };

static bool hit(const Button *b, float mx, float my)
{
    return mx >= b->x && mx < b->x + b->w && my >= b->y && my < b->y + b->h;
}

static void draw_button(SDL_Renderer *r, const Button *b, bool hover)
{
    SDL_FRect rc = {b->x, b->y, b->w, b->h};
    SDL_SetRenderDrawColor(r, hover ? 120 : 70, hover ? 40 : 24, hover ? 40 : 30, 255);
    SDL_RenderFillRect(r, &rc);
    SDL_SetRenderDrawColor(r, 200, 170, 120, 255);
    SDL_RenderRect(r, &rc);
    float tx = b->x + (b->w - 8.0f * (float)strlen(b->label)) / 2;
    SDL_RenderDebugText(r, tx, b->y + (b->h - 8) / 2, b->label);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    const char *base = SDL_GetBasePath();
    snprintf(base_dir, sizeof base_dir, "%s", base ? base : "./");
    mtx = SDL_CreateMutex();
    setup_line_hook = gui_line;
    setup_progress_hook = gui_progress;

    SDL_Window *win = SDL_CreateWindow("RondoRecomp Setup", 1280, 800, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, NULL) : NULL;
    if (!ren) return 1;
    SDL_SetRenderLogicalPresentation(ren, 640, 400, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    bool running = true;
    float mx = 0, my = 0;
    Uint64 t0 = SDL_GetTicks();
    while (running) {
        int st = SDL_GetAtomicInt(&state);
        bool have_game = exists("game"), have_exe = exists("rondo.exe");
        Button buttons[4];
        int nb = 0;
        if (st == IDLE || st == DONE || st == FAILED) {
            buttons[nb++] = (Button){24, 340, 200, 32, st == DONE ? "Choose another zip" : "Choose disc zip...", B_CHOOSE};
            if (have_exe && st != FAILED) buttons[nb++] = (Button){240, 340, 120, 32, "Play", B_PLAY};
            if (have_game && st != DONE) buttons[nb++] = (Button){376, 340, 120, 32, "Rebuild", B_REBUILD};
            buttons[nb++] = (Button){512, 340, 104, 32, "Quit", B_QUIT};
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            SDL_ConvertEventToRenderCoordinates(ren, &ev);
            if (ev.type == SDL_EVENT_QUIT) {
                if (st != RUNNING) running = false;
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                mx = ev.motion.x;
                my = ev.motion.y;
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                for (int i = 0; i < nb; i++) {
                    if (!hit(&buttons[i], ev.button.x, ev.button.y)) continue;
                    if (buttons[i].id == B_CHOOSE) {
                        static const SDL_DialogFileFilter filters[] = {
                            {"Disc image (zip or cue)", "zip;cue"}, {"All files", "*"}};
                        SDL_ShowOpenFileDialog(on_pick, NULL, win, filters, 2, NULL, false);
                    } else if (buttons[i].id == B_REBUILD) {
                        start(true);
                    } else if (buttons[i].id == B_PLAY) {
                        char exe[1100];
                        snprintf(exe, sizeof exe, "%srondo.exe", base_dir);
                        const char *args[] = {exe, NULL};
                        SDL_Process *p = SDL_CreateProcess(args, false);
                        if (p) { SDL_DestroyProcess(p); running = false; }
                    } else if (buttons[i].id == B_QUIT) {
                        running = false;
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 18, 10, 24, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawColor(ren, 230, 200, 140, 255);
        SDL_RenderDebugText(ren, 24, 16, "RondoRecomp Setup");
        SDL_SetRenderDrawColor(ren, 170, 160, 180, 255);
        SDL_RenderDebugText(ren, 24, 30, "Turns your own copy of Castlevania: Rondo of Blood into a native PC game.");

        SDL_LockMutex(mtx);
        if (st == IDLE) {
            SDL_RenderDebugText(ren, 24, 60, "1. Click \"Choose disc zip...\" and pick a zip of your disc image");
            SDL_RenderDebugText(ren, 24, 72, "   (a .cue file with its .bin/.iso/.wav tracks; a .cue also works).");
            SDL_RenderDebugText(ren, 24, 88, "2. Setup checks the disc, extracts it, recompiles the game code");
            SDL_RenderDebugText(ren, 24, 100, "   and builds rondo.exe in this folder. This takes about a minute.");
            SDL_RenderDebugText(ren, 24, 116, "3. Click Play.");
            SDL_RenderDebugText(ren, 24, 140, "Rebuild: recompile again, including code rondo.exe recorded");
            SDL_RenderDebugText(ren, 24, 152, "while you played (misses.txt).");
            SDL_RenderDebugText(ren, 24, 176, "No game files are included with RondoRecomp.");
        } else {
            int first = n_lines > 30 ? n_lines - 30 : 0;
            float y = 52;
            SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
            for (int i = first; i < n_lines; i++, y += 9) SDL_RenderDebugText(ren, 24, y, lines[i]);
            if (st == RUNNING) {
                static const char spin[] = "|/-\\";
                char busy[160];
                Uint64 t = (SDL_GetTicks() - t0) / 150;
                if (prog_pct >= 0 && prog_pct < 100) {
                    snprintf(busy, sizeof busy, "%c %s", spin[t % 4], prog_what);
                    SDL_FRect bar = {24, 322, 592, 8}, fill = {24, 322, 5.92f * (float)prog_pct, 8};
                    SDL_SetRenderDrawColor(ren, 60, 50, 70, 255);
                    SDL_RenderFillRect(ren, &bar);
                    SDL_SetRenderDrawColor(ren, 200, 150, 80, 255);
                    SDL_RenderFillRect(ren, &fill);
                } else {
                    snprintf(busy, sizeof busy, "%c working...", spin[t % 4]);
                }
                SDL_SetRenderDrawColor(ren, 230, 200, 140, 255);
                SDL_RenderDebugText(ren, 24, 344, busy);
            }
        }
        SDL_UnlockMutex(mtx);
        if (st == DONE) { SDL_SetRenderDrawColor(ren, 140, 220, 140, 255); SDL_RenderDebugText(ren, 24, 324, "Done. rondo.exe is ready."); }
        if (st == FAILED) { SDL_SetRenderDrawColor(ren, 240, 120, 110, 255); SDL_RenderDebugText(ren, 24, 324, "Setup did not finish; see the messages above."); }
        for (int i = 0; i < nb; i++) draw_button(ren, &buttons[i], hit(&buttons[i], mx, my));
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    SDL_Quit();
    return 0;
}
