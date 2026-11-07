/* Runtime side of the recompiler: entry lookup, code checks, stats, verification, misses. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu/huc6280_info.h"
#include "pce/pce.h"
#include "recomp.h"

static bool enabled = true;
static int verify_every;
static RecompStats stats;
static uint32_t *verified_gen;     /* per chunk: page_gen + 1 when its bytes last matched */
static uint32_t *failed_gen;       /* per chunk: page_gen + 1 when its bytes last did not match */
static uint8_t *has_entry;         /* bit per (bank, offset): some entry starts there */

void recomp_init(void)
{
    free(verified_gen);
    free(failed_gen);
    free(has_entry);
    size_t n = (size_t)(recomp_chunk_count ? recomp_chunk_count : 1);
    verified_gen = calloc(n, sizeof *verified_gen);
    failed_gen = calloc(n, sizeof *failed_gen);
    has_entry = calloc(256 * 8192 / 8, 1);
    for (int i = 0; i < recomp_entry_count; i++) {
        uint32_t k = recomp_entries[i].key;
        uint32_t bit = (k >> 16) * 8192 + (k & 0x1FFF);
        has_entry[bit >> 3] |= (uint8_t)(1u << (bit & 7));
    }
    memset(&stats, 0, sizeof stats);
}

void recomp_invalidate(void)
{
    size_t n = (size_t)(recomp_chunk_count ? recomp_chunk_count : 1);
    if (verified_gen) memset(verified_gen, 0, n * sizeof *verified_gen);
    if (failed_gen) memset(failed_gen, 0, n * sizeof *failed_gen);
}

bool recomp_enabled(void) { return enabled && recomp_entry_count > 0; }
void recomp_set_enabled(bool on) { enabled = on; }
void recomp_set_verify(int every_n) { verify_every = every_n; }
const RecompStats *recomp_stats(void) { return &stats; }

const RecompChunk *recomp_lookup(HuC6280 *c)
{
    uint16_t pc = c->pc;
    uint8_t bank = c->mpr[pc >> 13];
    uint32_t bit = (uint32_t)bank * 8192 + (pc & 0x1FFF);
    if (!(has_entry[bit >> 3] & (1u << (bit & 7)))) return NULL;
    const uint8_t *page = c->page_ptr[bank];
    if (!page) return NULL;

    uint32_t key = (uint32_t)bank << 16 | pc;
    int lo = 0, hi = recomp_entry_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (recomp_entries[mid].key < key) lo = mid + 1; else hi = mid;
    }
    uint32_t gen = c->page_gen[bank] + 1;
    for (int i = lo; i < recomp_entry_count && recomp_entries[i].key == key; i++) {
        uint32_t ci = recomp_entries[i].chunk;
        if (verified_gen[ci] == gen) return &recomp_chunks[ci];
        if (failed_gen[ci] == gen) continue;
        const RecompChunk *k = &recomp_chunks[ci];
        if (recomp_code_hash(page, k->ranges, k->n_ranges) == k->hash) {
            verified_gen[ci] = gen;
            return k;
        }
        failed_gen[ci] = gen;
    }
    return NULL;
}

/* ---- running and verification ----------------------------------------------------- */

static Pce *snap_before, *snap_native;

static void report_difference(const Pce *want, const Pce *got, const RecompChunk *k, uint16_t entry)
{
    const HuC6280 *a = &want->cpu, *b = &got->cpu;
    fprintf(stderr, "[recomp] VERIFY FAILED chunk bank %02X entry %04X\n", k->bank, entry);
    fprintf(stderr, "  interpreter A=%02X X=%02X Y=%02X S=%02X P=%02X PC=%04X\n", a->a, a->x, a->y, a->s, a->p, a->pc);
    fprintf(stderr, "  native      A=%02X X=%02X Y=%02X S=%02X P=%02X PC=%04X\n", b->a, b->x, b->y, b->s, b->p, b->pc);
    const uint8_t *x = (const uint8_t *)want, *y = (const uint8_t *)got;
    for (size_t i = 0; i < sizeof(Pce); i++) {
        if (x[i] != y[i]) {
            fprintf(stderr, "  first differing byte at struct offset %zu (interp %02X, native %02X)\n", i, x[i], y[i]);
            break;
        }
    }
}

int recomp_run(Pce *p, const RecompChunk *k, int budget)
{
    HuC6280 *c = &p->cpu;
    stats.calls++;
    if (!verify_every || stats.calls % (uint64_t)verify_every) {
        int n = k->fn(c, budget);
        if (n < 0) return -1;
        stats.native_cycles += (uint64_t)n;
        c->cycles += (uint64_t)n;
        return n;
    }

    /* replay on the interpreter from the same state and compare everything */
    if (!snap_before) {
        snap_before = malloc(sizeof(Pce));
        snap_native = malloc(sizeof(Pce));
        if (!snap_before || !snap_native) { verify_every = 0; return recomp_run(p, k, budget); }
    }
    uint16_t entry = c->pc;
    memcpy(snap_before, p, sizeof(Pce));
    int n = k->fn(c, budget);
    if (n < 0) return -1;
    c->cycles += (uint64_t)n;
    memcpy(snap_native, p, sizeof(Pce));

    memcpy(p, snap_before, sizeof(Pce));
    uint8_t irq = c->irq_pending;
    c->irq_pending = 0;                      /* native code does not take interrupts */
    int done = 0;
    while (done < n) done += hu_step(c);
    c->irq_pending = irq;

    stats.verify_runs++;
    bool same = done == n && memcmp(p, snap_native, sizeof(Pce)) == 0;
    if (!same) {
        stats.verify_failures++;
        if (stats.verify_failures <= 20) {
            if (done != n) fprintf(stderr, "[recomp] cycle mismatch: native %d, interpreter %d\n", n, done);
            report_difference(p, snap_native, k, entry);
        }
    }
    memcpy(p, snap_native, sizeof(Pce));     /* continue from the native result */
    stats.native_cycles += (uint64_t)n;
    return n;
}

/* ---- misses (hash set of bank:addr + basic-block hash) ---------------------------- */

typedef struct { uint32_t key; uint32_t h; } Miss;
static Miss *miss_tab;            /* key 0 = empty slot (bank $00 is never recorded) */
static uint32_t miss_cap, n_misses;

static void miss_insert(uint32_t key, uint32_t h)
{
    if (n_misses * 2 >= miss_cap) {
        uint32_t old_cap = miss_cap;
        Miss *old = miss_tab;
        miss_cap = old_cap ? old_cap * 2 : 4096;
        miss_tab = calloc(miss_cap, sizeof *miss_tab);
        n_misses = 0;
        for (uint32_t i = 0; i < old_cap; i++)
            if (old[i].key) miss_insert(old[i].key, old[i].h);
        free(old);
    }
    uint32_t i = ((key ^ h) * 2654435761u) & (miss_cap - 1);
    while (miss_tab[i].key) {
        if (miss_tab[i].key == key && miss_tab[i].h == h) return;
        i = (i + 1) & (miss_cap - 1);
    }
    miss_tab[i] = (Miss){key, h};
    n_misses++;
    stats.miss_entries = n_misses;
}

void recomp_note_miss(HuC6280 *c)
{
    uint16_t pc = c->pc;
    uint8_t bank = c->mpr[pc >> 13];
    if (bank == 0x00 || !c->page_ram[bank]) return;   /* only code in RAM banks */
    miss_insert((uint32_t)bank << 16 | pc, hu_block_hash(c->page_ptr[bank], pc & 0x1FFF));
}

void recomp_note_interp(int cycles) { stats.interp_cycles += (uint64_t)cycles; }

static int cmp_miss(const void *a, const void *b)
{
    const Miss *x = a, *y = b;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return x->h < y->h ? -1 : x->h > y->h;
}

bool recomp_write_misses(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    Miss *list = malloc((n_misses ? n_misses : 1) * sizeof *list);
    uint32_t n = 0;
    for (uint32_t i = 0; i < miss_cap; i++)
        if (miss_tab[i].key) list[n++] = miss_tab[i];
    qsort(list, n, sizeof *list, cmp_miss);
    for (uint32_t i = 0; i < n; i++)
        fprintf(f, "%02X %04X %08X\n", list[i].key >> 16, list[i].key & 0xFFFF, list[i].h);
    free(list);
    fclose(f);
    return true;
}

bool recomp_load_misses(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    unsigned bank, addr, h;
    while (fscanf(f, "%x %x %x", &bank, &addr, &h) == 3)
        miss_insert(bank << 16 | (addr & 0xFFFF), h);
    fclose(f);
    return true;
}
