/* Interface between the runtime and the C code that Setup generates from the
 * player's disc. The generated code defines the chunk and entry tables and
 * recomp_build_id; developer builds link recomp_none.c instead (interpreter only). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cpu/huc6280.h"

/* Runs natively from c->pc (an entry of the chunk) until an exit; sets c->pc to
 * where execution continues and returns the CPU cycles used, or -1 if c->pc is
 * not an instruction of this chunk. Backward branches return early once `budget`
 * cycles are used. */
typedef int (*RecompFn)(HuC6280 *c, int budget);

/* All code found in one bank image (one 8 KiB window), compiled as one function. */
typedef struct RecompChunk {
    uint8_t bank;
    uint16_t n_ranges;          /* code byte ranges the chunk was compiled from */
    const uint16_t *ranges;     /* n_ranges pairs: offset in bank, length */
    uint64_t hash;              /* recomp_code_hash() of those bytes */
    RecompFn fn;
} RecompChunk;

typedef struct RecompEntry {
    uint32_t key;               /* bank << 16 | logical address */
    uint32_t chunk;             /* index into recomp_chunks */
} RecompEntry;

extern const RecompChunk recomp_chunks[];
extern const int recomp_chunk_count;
extern const RecompEntry recomp_entries[];  /* sorted by key; several chunks may share a key */
extern const int recomp_entry_count;
extern const char recomp_build_id[];

/* FNV-1a 64 over code byte ranges (generator and runtime use the same function). */
static inline uint64_t recomp_code_hash(const uint8_t *page, const uint16_t *ranges, int n_ranges)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (int i = 0; i < n_ranges; i++) {
        const uint8_t *p = page + ranges[2 * i];
        for (uint16_t k = 0; k < ranges[2 * i + 1]; k++) { h ^= p[k]; h *= 0x100000001b3ull; }
    }
    return h;
}

/* Is `off` inside one of a chunk's code ranges (sorted, non-overlapping)? Generated code
 * uses this after stores that may patch its own instructions. */
static inline int recomp_hits(const uint16_t *r, int n, uint16_t off)
{
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if ((int)r[2 * mid] + r[2 * mid + 1] <= off) lo = mid + 1; else hi = mid;
    }
    return lo < n && r[2 * lo] <= off;
}

/* ---- runtime dispatch (dispatch.c) ---------------------------------------------- */

struct Pce;

typedef struct RecompStats {
    uint64_t native_cycles, interp_cycles;
    uint64_t calls, verify_runs, verify_failures;
    uint32_t miss_entries;                 /* distinct entry points recorded without a chunk */
} RecompStats;

void recomp_init(void);
/* Forget cached code checks (after memory was replaced, e.g. a save state was loaded). */
void recomp_invalidate(void);
bool recomp_enabled(void);
void recomp_set_enabled(bool on);
/* Finds a chunk whose code matches memory and that has the CPU's PC as an entry. */
const RecompChunk *recomp_lookup(HuC6280 *c);
/* Runs a chunk (see RecompFn); returns CPU cycles, or -1 if it could not be entered.
 * With verification on, every Nth call is replayed on the interpreter and compared. */
int recomp_run(struct Pce *p, const RecompChunk *k, int budget);

void recomp_set_verify(int every_n);
const RecompStats *recomp_stats(void);

/* Entry points that ran on the interpreter: written as seeds for the next build. */
void recomp_note_miss(HuC6280 *c);
void recomp_note_interp(int cycles);
bool recomp_write_misses(const char *path);
bool recomp_load_misses(const char *path);
