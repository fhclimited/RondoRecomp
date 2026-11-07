/* See locate.h. */
#include "locate.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


/* ---- remembered path ------------------------------------------------------- */

static bool pref_file(char *out, size_t n)
{
    char *dir = SDL_GetPrefPath("RondoRecomp", "RondoRecomp");
    if (!dir) return false;
    snprintf(out, n, "%sdisc.txt", dir);
    SDL_free(dir);
    return true;
}

void locate_remember(const char *cue_path)
{
    char file[1024];
    if (!pref_file(file, sizeof file)) return;
    FILE *f = fopen(file, "w");
    if (!f) return;
    fprintf(f, "%s\n", cue_path);
    fclose(f);
}

static bool remembered(char *out, size_t n)
{
    char file[1024];
    if (!pref_file(file, sizeof file)) return false;
    FILE *f = fopen(file, "r");
    if (!f) return false;
    bool ok = fgets(out, (int)n, f) != NULL;
    fclose(f);
    if (!ok) return false;
    out[strcspn(out, "\r\n")] = 0;
    return out[0] && discid_identify(out, NULL) != DISC_INVALID;
}

/* ---- search near the executable -------------------------------------------- */

typedef struct {
    char *out;
    size_t out_len;
    bool found;
    int depth;
} Search;

static void join(char *out, size_t n, const char *dir, const char *name)
{
    size_t l = strlen(dir);
    bool sep = l && (dir[l - 1] == '/' || dir[l - 1] == '\\');
    snprintf(out, n, "%s%s%s", dir, sep ? "" : "/", name);
}

static bool has_cue_ext(const char *name)
{
    size_t l = strlen(name);
    return l > 4 && SDL_strcasecmp(name + l - 4, ".cue") == 0;
}

static SDL_EnumerationResult SDLCALL visit(void *userdata, const char *dirname, const char *fname)
{
    Search *s = userdata;
    char path[1024];
    join(path, sizeof path, dirname, fname);
    if (has_cue_ext(fname)) {
        if (discid_identify(path, NULL) == DISC_RONDO) {
            snprintf(s->out, s->out_len, "%s", path);
            s->found = true;
            return SDL_ENUM_SUCCESS;
        }
        return SDL_ENUM_CONTINUE;
    }
    if (s->depth == 0 && fname[0] != '.') {
        SDL_PathInfo info;
        if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            s->depth = 1;
            SDL_EnumerateDirectory(path, visit, s);
            s->depth = 0;
            if (s->found) return SDL_ENUM_SUCCESS;
        }
    }
    return SDL_ENUM_CONTINUE;
}

static void parent(char *dir)
{
    size_t l = strlen(dir);
    while (l && (dir[l - 1] == '/' || dir[l - 1] == '\\')) dir[--l] = 0;
    while (l && dir[l - 1] != '/' && dir[l - 1] != '\\') dir[--l] = 0;
}

bool locate_disc(char *out, size_t out_len)
{
    const char *base = SDL_GetBasePath();
    /* an installed build: game/ next to the executable, as written by Setup */
    if (base) {
        char game[1024];
        join(game, sizeof game, base, "game");
        Search s = {out, out_len, false, 1};
        SDL_EnumerateDirectory(game, visit, &s);
        if (s.found) return true;
    }
    if (remembered(out, out_len)) return true;
    if (!base) return false;
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", base);
    /* the executable's folder, its parent and grandparent, each with one level of subfolders */
    for (int level = 0; level < 3 && dir[0]; level++) {
        Search s = {out, out_len, false, 0};
        SDL_EnumerateDirectory(dir, visit, &s);
        if (s.found) return true;
        parent(dir);
    }
    return false;
}
