/* Code generator: turns the game's code (from the player's disc) into C regions. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct GenOptions {
    const char *cue;            /* player's disc image */
    const char *seed_files[4];  /* seed lists (shipped seeds, player's misses); NULL-terminated */
    const char *out_dir;        /* receives regions_*.c, regions.h, table.c */
    const char *build_id;       /* stored in the generated code (recomp_build_id) */
    int max_region_instrs;      /* default 256 */
    int max_regions;            /* default 120000 */
    bool verbose;
} GenOptions;

typedef struct GenStats {
    int images;                 /* distinct RAM bank images collected */
    int groups_loaded;          /* overlay groups run through the game's loader */
    int capture_seeds;          /* entry points found by the headless capture run */
    int seeds, seeds_matched;
    int regions, entries, instructions;   /* regions = distinct chunks */
    int files;
    size_t c_bytes;
} GenStats;

typedef void (*GenLog)(const char *line);

bool gen_run(const GenOptions *o, GenStats *st, GenLog log, char *err, int err_len);
