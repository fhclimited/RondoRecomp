/* RondoRecomp Setup (command line): turns the player's own disc image into rondo.exe.
 *
 * usage: rondo_setup <disc.zip | disc.cue> [--out DIR] [--sdk DIR] [--zig ZIG] [--allow-unknown]
 *                    [--no-build]
 *
 *   1. import    read the zip (or .cue), find a CUE sheet whose files are all present
 *   2. identify  PC Engine CD signature + boot-sector fingerprint (refuses unknown discs
 *                unless --allow-unknown)
 *   3. extract   to DIR/game (staged in DIR/game.tmp, renamed only after the check passes)
 *   4. generate  DIR/build/gen/game_gen.c (recompiled code; empty table for now)
 *   5. build     zig cc: SDK runtime sources + generated code + SDL3 -> DIR/rondo.exe
 *
 * Defaults: DIR = folder of this program, SDK = DIR/sdk, ZIG = DIR/zig/zig.exe.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pce/discid.h"
#include "zip.h"

#define SETUP_VERSION "0.1"
#define MAX_FILES 128

static char g_err[512];

static bool ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && SDL_strcasecmp(s + a - b, suffix) == 0;
}

static void join(char *out, size_t n, const char *a, const char *b)
{
    size_t l = strlen(a);
    bool sep = l && (a[l - 1] == '/' || a[l - 1] == '\\');
    const char *native = strchr(a, '\\') ? "\\" : "/";   /* match the base path's separators */
    snprintf(out, n, "%s%s%s", a, (l && !sep) ? native : "", b);
}

static void dirname_of(char *out, size_t n, const char *path)
{
    snprintf(out, n, "%s", path);
    char *s = strrchr(out, '/'), *b = strrchr(out, '\\');
    if (b > s) s = b;
    if (s) s[1] = 0; else out[0] = 0;
}

static const char *basename_of(const char *path)
{
    const char *s = strrchr(path, '/'), *b = strrchr(path, '\\');
    if (b > s) s = b;
    return s ? s + 1 : path;
}

/* Absolute path with native separators, so child processes resolve it reliably. */
static void absolute(char *path, size_t n)
{
#ifdef _WIN32
    char full[1024];
    if (_fullpath(full, path, sizeof full)) snprintf(path, n, "%s", full);
    for (char *c = path; *c; c++) if (*c == '/') *c = '\\';
#else
    (void)path; (void)n;
#endif
}

/* ---- file system helpers ------------------------------------------------------ */

static SDL_EnumerationResult SDLCALL remove_visit(void *ctx, const char *dir, const char *name)
{
    (void)ctx;
    char path[1024];
    join(path, sizeof path, dir, name);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY)
        SDL_EnumerateDirectory(path, remove_visit, NULL);
    SDL_RemovePath(path);
    return SDL_ENUM_CONTINUE;
}

static void remove_tree(const char *path)
{
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return;
    if (info.type == SDL_PATHTYPE_DIRECTORY) SDL_EnumerateDirectory(path, remove_visit, NULL);
    SDL_RemovePath(path);
}

static bool make_dirs_for(const char *file)
{
    char dir[1024];
    dirname_of(dir, sizeof dir, file);
    return !dir[0] || SDL_CreateDirectory(dir);
}

/* ---- CUE sheets ---------------------------------------------------------------- */

/* Collect the file names a CUE sheet references (FILE "name" TYPE). */
static int cue_files(const char *text, char names[][512], int max)
{
    int n = 0;
    const char *p = text;
    while (*p && n < max) {
        const char *line = p;
        while (*p && *p != '\n') p++;
        const char *end = p;
        if (*p) p++;
        while (line < end && isspace((unsigned char)*line)) line++;
        if (end - line < 5 || SDL_strncasecmp(line, "FILE", 4) || !isspace((unsigned char)line[4])) continue;
        line += 4;
        while (line < end && isspace((unsigned char)*line)) line++;
        const char *s = line, *e;
        if (*s == '"') {
            s++;
            e = s;
            while (e < end && *e != '"') e++;
        } else {
            e = s;
            while (e < end && !isspace((unsigned char)*e)) e++;
        }
        size_t len = (size_t)(e - s);
        if (len == 0 || len >= 512) continue;
        memcpy(names[n], s, len);
        names[n][len] = 0;
        for (char *c = names[n]; *c; c++) if (*c == '\\') *c = '/';
        n++;
    }
    return n;
}

/* ---- import --------------------------------------------------------------------- */

static void progress(void *ctx, uint64_t done, uint64_t total)
{
    static int last = -1;
    int pct = total ? (int)(done * 100 / total) : 100;
    if (pct != last && (pct % 10 == 0)) {
        printf("\r    %s %3d%%", (const char *)ctx, pct);
        fflush(stdout);
        last = pct;
    }
    if (done == total) { printf("\n"); last = -1; }
}

/* Extract one candidate CUE (and its files) from the zip into `stage`; returns the staged cue path. */
static bool import_zip_cue(Zip *z, const ZipEntry *cue, const char *stage, char *cue_out, size_t cue_out_len)
{
    char *text = zip_read_text(z, cue, 1 << 16, g_err, sizeof g_err);
    if (!text) return false;
    static char names[MAX_FILES][512];
    int n = cue_files(text, names, MAX_FILES);
    char cdir[512];
    dirname_of(cdir, sizeof cdir, cue->name);
    const ZipEntry *files[MAX_FILES];
    for (int i = 0; i < n; i++) {
        char full[1100];
        snprintf(full, sizeof full, "%s%s", cdir, names[i]);
        files[i] = zip_find(z, full);
        if (!files[i]) {
            snprintf(g_err, sizeof g_err, "%s refers to \"%s\", which is not in the zip", cue->name, names[i]);
            free(text);
            return false;
        }
    }
    if (n == 0) { snprintf(g_err, sizeof g_err, "%s lists no track files", cue->name); free(text); return false; }

    remove_tree(stage);
    if (!SDL_CreateDirectory(stage)) { snprintf(g_err, sizeof g_err, "cannot create %s", stage); free(text); return false; }
    join(cue_out, cue_out_len, stage, basename_of(cue->name));
    FILE *f = fopen(cue_out, "wb");
    if (!f || fwrite(text, 1, strlen(text), f) != strlen(text)) {
        if (f) fclose(f);
        snprintf(g_err, sizeof g_err, "cannot write %s", cue_out);
        free(text);
        return false;
    }
    fclose(f);
    free(text);
    for (int i = 0; i < n; i++) {
        char dest[1100];
        join(dest, sizeof dest, stage, names[i]);
        if (!make_dirs_for(dest)) { snprintf(g_err, sizeof g_err, "cannot create folder for %s", dest); return false; }
        if (!zip_extract(z, files[i], dest, progress, (void *)names[i], g_err, sizeof g_err)) return false;
    }
    return true;
}

static bool import_cue(const char *cue_path, const char *stage, char *cue_out, size_t cue_out_len)
{
    size_t len = 0;
    char *text = SDL_LoadFile(cue_path, &len);
    if (!text) { snprintf(g_err, sizeof g_err, "cannot read %s", cue_path); return false; }
    static char names[MAX_FILES][512];
    int n = cue_files(text, names, MAX_FILES);
    SDL_free(text);
    char cdir[1024];
    dirname_of(cdir, sizeof cdir, cue_path);
    remove_tree(stage);
    if (!SDL_CreateDirectory(stage)) { snprintf(g_err, sizeof g_err, "cannot create %s", stage); return false; }
    join(cue_out, cue_out_len, stage, basename_of(cue_path));
    if (!SDL_CopyFile(cue_path, cue_out)) { snprintf(g_err, sizeof g_err, "cannot copy %s", cue_path); return false; }
    for (int i = 0; i < n; i++) {
        char src[1100], dest[1100];
        join(src, sizeof src, cdir, names[i]);
        join(dest, sizeof dest, stage, names[i]);
        printf("    %s\n", names[i]);
        if (!make_dirs_for(dest) || !SDL_CopyFile(src, dest)) {
            snprintf(g_err, sizeof g_err, "cannot copy %s", src);
            return false;
        }
    }
    return true;
}

/* ---- generate + build ------------------------------------------------------------ */

static bool write_generated(const char *gen_dir, uint64_t fingerprint)
{
    char path[1024];
    if (!SDL_CreateDirectory(gen_dir)) { snprintf(g_err, sizeof g_err, "cannot create %s", gen_dir); return false; }
    join(path, sizeof path, gen_dir, "game_gen.c");
    FILE *f = fopen(path, "w");
    if (!f) { snprintf(g_err, sizeof g_err, "cannot write %s", path); return false; }
    fprintf(f,
            "/* Generated by RondoRecomp Setup " SETUP_VERSION " from the player's disc. Do not distribute. */\n"
            "#include \"recomp/recomp.h\"\n\n"
            "/* No functions are recompiled yet: the runtime interprets all game code. */\n"
            "const RecompEntry recomp_table[1] = {{0}};\n"
            "const int recomp_table_len = 0;\n"
            "const char recomp_build_id[] = \"setup " SETUP_VERSION ", disc %016llX\";\n",
            (unsigned long long)fingerprint);
    fclose(f);
    return true;
}

static bool run(const char *const *args)
{
    SDL_Process *proc = SDL_CreateProcess(args, false);
    if (!proc) { snprintf(g_err, sizeof g_err, "cannot start %s: %s", args[0], SDL_GetError()); return false; }
    int code = -1;
    SDL_WaitProcess(proc, true, &code);
    SDL_DestroyProcess(proc);
    if (code != 0) { snprintf(g_err, sizeof g_err, "%s failed (exit code %d)", basename_of(args[0]), code); return false; }
    return true;
}

static bool build(const char *sdk, const char *zig, const char *out, const char *gen_dir)
{
    /* the list of runtime sources ships with the SDK */
    char list_path[1024];
    join(list_path, sizeof list_path, sdk, "sources.txt");
    size_t len = 0;
    char *list = SDL_LoadFile(list_path, &len);
    if (!list) { snprintf(g_err, sizeof g_err, "SDK not found (%s)", list_path); return false; }

    static char storage[64][1024];
    const char *args[96];
    int n = 0, s = 0;
    static char inc_rt[1100], inc_sdl[1100], libdir[1100], gen_c[1100], exe[1100];
    join(inc_rt, sizeof inc_rt, sdk, "src/runtime");
    join(inc_sdl, sizeof inc_sdl, sdk, "include");
    join(libdir, sizeof libdir, sdk, "lib");
    join(gen_c, sizeof gen_c, gen_dir, "game_gen.c");
    join(exe, sizeof exe, out, "rondo.exe");
    static char opt_i1[1200], opt_i2[1200], opt_l[1200];
    snprintf(opt_i1, sizeof opt_i1, "-I%s", inc_rt);
    snprintf(opt_i2, sizeof opt_i2, "-I%s", inc_sdl);
    join(opt_l, sizeof opt_l, libdir, "libSDL3.dll.a");   /* import library, passed by path */

    args[n++] = zig;
    args[n++] = "cc";
    args[n++] = "-target";
    args[n++] = "x86_64-windows-gnu";
    args[n++] = "-O2";
    args[n++] = "-std=c11";
    args[n++] = "-w";
    args[n++] = opt_i1;
    args[n++] = opt_i2;
    for (char *line = strtok(list, "\r\n"); line && s < 64; line = strtok(NULL, "\r\n")) {
        if (!*line || *line == '#') continue;
        join(storage[s], sizeof storage[s], sdk, line);
        args[n++] = storage[s++];
    }
    args[n++] = gen_c;
    args[n++] = opt_l;
    args[n++] = "-mwindows";
    args[n++] = "-o";
    args[n++] = exe;
    args[n] = NULL;
    SDL_free(list);

    /* keep the compiler's cache next to the build instead of in the user profile */
    char cache[1100];
    join(cache, sizeof cache, out, "build/zig-cache");
    SDL_CreateDirectory(cache);
    SDL_Environment *env = SDL_GetEnvironment();
    SDL_SetEnvironmentVariable(env, "ZIG_GLOBAL_CACHE_DIR", cache, true);
    SDL_SetEnvironmentVariable(env, "ZIG_LOCAL_CACHE_DIR", cache, true);
    printf("    compiling %d runtime sources + generated code (the first build also prepares the C library)\n", s);
    if (!run(args)) return false;

    /* runtime DLLs */
    static const char *dlls[] = {"SDL3.dll", "libiconv-2.dll"};
    for (size_t i = 0; i < sizeof dlls / sizeof dlls[0]; i++) {
        char src[1100], dst[1100];
        join(src, sizeof src, sdk, "bin");
        join(src, sizeof src, src, dlls[i]);
        join(dst, sizeof dst, out, dlls[i]);
        if (SDL_GetPathInfo(src, NULL) && !SDL_CopyFile(src, dst)) {
            snprintf(g_err, sizeof g_err, "cannot copy %s", src);
            return false;
        }
    }
    return true;
}

/* ---- main --------------------------------------------------------------------- */

static int fail(const char *step)
{
    fprintf(stderr, "\nSetup failed while %s:\n  %s\n", step, g_err);
    return 1;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* keep progress and errors in order */
    const char *input = NULL, *out_arg = NULL, *sdk_arg = NULL, *zig_arg = NULL;
    bool allow_unknown = false, do_build = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) out_arg = argv[++i];
        else if (!strcmp(argv[i], "--sdk") && i + 1 < argc) sdk_arg = argv[++i];
        else if (!strcmp(argv[i], "--zig") && i + 1 < argc) zig_arg = argv[++i];
        else if (!strcmp(argv[i], "--allow-unknown")) allow_unknown = true;
        else if (!strcmp(argv[i], "--no-build")) do_build = false;
        else input = argv[i];
    }
    if (!input) {
        fprintf(stderr, "usage: rondo_setup <disc.zip | disc.cue> [--out DIR] [--sdk DIR] [--zig ZIG] "
                        "[--allow-unknown] [--no-build]\n");
        return 2;
    }
    const char *base = SDL_GetBasePath();
    char out[1024], sdk[1024], zig[1024], stage[1100], game[1100], gen_dir[1100], cue[1100];
    snprintf(out, sizeof out, "%s", out_arg ? out_arg : (base ? base : "."));
    if (sdk_arg) snprintf(sdk, sizeof sdk, "%s", sdk_arg); else join(sdk, sizeof sdk, out, "sdk");
    if (zig_arg) snprintf(zig, sizeof zig, "%s", zig_arg); else join(zig, sizeof zig, out, "zig/zig.exe");
    SDL_CreateDirectory(out);
    absolute(out, sizeof out);
    absolute(sdk, sizeof sdk);
    absolute(zig, sizeof zig);
    join(stage, sizeof stage, out, "game.tmp");
    join(game, sizeof game, out, "game");
    join(gen_dir, sizeof gen_dir, out, "build/gen");

    printf("RondoRecomp Setup " SETUP_VERSION "\n\n[1/4] Importing %s\n", input);
    DiscKind kind = DISC_INVALID;
    uint64_t fp = 0;
    if (ends_with(input, ".zip")) {
        Zip z;
        if (!zip_open(&z, input, g_err, sizeof g_err)) return fail("opening the zip");
        int cues = 0;
        for (int i = 0; i < z.n && kind != DISC_RONDO; i++) {
            if (!ends_with(z.entries[i].name, ".cue")) continue;
            cues++;
            printf("  found %s\n", z.entries[i].name);
            if (!import_zip_cue(&z, &z.entries[i], stage, cue, sizeof cue)) { printf("  skipped: %s\n", g_err); continue; }
            kind = discid_identify(cue, &fp);
            if (kind == DISC_PCE_UNKNOWN && allow_unknown) break;
            if (kind != DISC_RONDO) printf("  skipped: not a recognised Rondo of Blood disc\n");
        }
        zip_close(&z);
        if (!cues) { snprintf(g_err, sizeof g_err, "the zip contains no .cue file"); remove_tree(stage); return fail("importing"); }
    } else if (ends_with(input, ".cue")) {
        if (!import_cue(input, stage, cue, sizeof cue)) { remove_tree(stage); return fail("importing"); }
        kind = discid_identify(cue, &fp);
    } else {
        snprintf(g_err, sizeof g_err, "expected a .zip or .cue file");
        return fail("importing");
    }

    printf("\n[2/4] Checking the disc\n");
    if (kind == DISC_INVALID || (kind == DISC_PCE_UNKNOWN && !allow_unknown)) {
        remove_tree(stage);
        snprintf(g_err, sizeof g_err, kind == DISC_INVALID
                 ? "no readable PC Engine CD image found"
                 : "this PC Engine CD is not a recognised Rondo of Blood release (fingerprint %016llX); "
                   "use --allow-unknown to try it anyway", (unsigned long long)fp);
        return fail("checking the disc");
    }
    printf("  %s (fingerprint %016llX)\n", kind == DISC_RONDO ? "Castlevania: Rondo of Blood" : "unknown release",
           (unsigned long long)fp);
    remove_tree(game);
    if (!SDL_RenamePath(stage, game)) {
        snprintf(g_err, sizeof g_err, "cannot move %s to %s: %s", stage, game, SDL_GetError());
        return fail("extracting");
    }
    printf("  extracted to %s\n", game);

    printf("\n[3/4] Generating code\n");
    if (!write_generated(gen_dir, fp)) return fail("generating code");
    printf("  %s/game_gen.c (no functions recompiled yet: interpreter only)\n", gen_dir);

    if (!do_build) { printf("\nDone (--no-build).\n"); return 0; }
    printf("\n[4/4] Building rondo.exe\n");
    if (!SDL_GetPathInfo(zig, NULL)) {
        snprintf(g_err, sizeof g_err, "compiler not found at %s", zig);
        return fail("building");
    }
    if (!build(sdk, zig, out, gen_dir)) return fail("building");
    printf("\nDone: %s/rondo.exe\n", out);
    return 0;
}
