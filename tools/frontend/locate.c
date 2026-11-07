/* See locate.h. */
#include "locate.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pce/disc.h"

/* FNV-1a 64 of the boot program sector (track 1st data track, record 2) of known releases */
static const uint64_t known_boot[] = {
    0xDB4D9415EC501573ull,   /* Castlevania: Rondo of Blood (English) */
};

static uint64_t fnv1a(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    while (n--) { h ^= *p++; h *= 0x100000001b3ull; }
    return h;
}

DiscKind locate_identify(const char *cue_path)
{
    static Disc disc;
    char err[256];
    if (!disc_open(&disc, cue_path, err, sizeof err)) return DISC_INVALID;
    const DiscTrack *t = NULL;
    for (int i = 0; i < disc.n_tracks; i++)
        if (disc.tracks[i].data) { t = &disc.tracks[i]; break; }
    uint8_t sec[2048];
    if (!t || !disc_read_data(&disc, t->lba + 1, 1, sec) || memcmp(sec + 0x20, "PC Engine CD-ROM SYSTEM", 23))
        return DISC_INVALID;
    if (!disc_read_data(&disc, t->lba + 2, 1, sec)) return DISC_INVALID;
    uint64_t h = fnv1a(sec, sizeof sec);
    for (size_t i = 0; i < sizeof known_boot / sizeof known_boot[0]; i++)
        if (h == known_boot[i]) return DISC_RONDO;
    return DISC_PCE_UNKNOWN;
}

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
    return out[0] && locate_identify(out) != DISC_INVALID;
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
        if (locate_identify(path) == DISC_RONDO) {
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
    if (remembered(out, out_len)) return true;
    const char *base = SDL_GetBasePath();
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
