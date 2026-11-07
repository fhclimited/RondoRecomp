/* Runs the SingleStepTests HuC6280 suite (binary v1 format) against the interpreter.
 *
 * usage: sst_huc6280 <dir-with-XX.json.bin> [opcode-hex ...] [-v N] [--known hex,hex,...]
 *   -v N      print details for the first N failures per opcode (default 1)
 *   --known   opcodes with documented, accepted differences: still reported,
 *             but they do not fail the run
 *
 * Checks A X Y S P PC, MPR0-7, every final RAM pair, and the total cycle count.
 *
 * Known differences (see docs/runtime.md): TII/TDD/TAI tests where a 64 KiB
 * transfer wraps around and overwrites its own source mid-copy; the suite's
 * 500-pair/500-cycle cap hides the generator state needed to model them.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu/huc6280.h"

#define RAM_SIZE (1u << 21)

static uint8_t ram[RAM_SIZE];
static uint32_t *touched;
static size_t n_touched, cap_touched;

static void touch(uint32_t a)
{
    if (n_touched == cap_touched) {
        cap_touched = cap_touched ? cap_touched * 2 : 4096;
        touched = realloc(touched, cap_touched * sizeof *touched);
    }
    touched[n_touched++] = a;
}

/* Block-transfer tests only record 500 RAM pairs; the generator's other memory is
 * random. Track which bytes are known so values copied from unrecorded addresses
 * (and registers popped back from such bytes) are excluded from comparison. */
static uint8_t known[RAM_SIZE];
static int last_read_known = 1;

static uint8_t bus_read(void *ctx, uint32_t a)
{
    (void)ctx;
    a &= RAM_SIZE - 1;
    last_read_known = known[a];
    return ram[a];
}

static void bus_write(void *ctx, uint32_t a, uint8_t v)
{
    (void)ctx;
    a &= RAM_SIZE - 1;
    ram[a] = v;
    known[a] = (uint8_t)last_read_known;
    touch(a);
}

static int is_block_op(uint32_t op) { return op == 0x73 || op == 0xC3 || op == 0xD3 || op == 0xE3 || op == 0xF3; }

typedef struct {
    uint8_t a, x, y, s, p;
    uint16_t pc;
    uint8_t mpr[8];
    uint32_t n_ram;
    const uint8_t *ram;   /* n_ram * (u32 addr, u8 val) */
} State;

typedef struct {
    char name[51];
    uint32_t opcode;
    State init, fin;
    uint32_t num_cycles;
} Test;

static uint32_t u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t u16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

static const uint8_t *parse_state(const uint8_t *p, State *st)
{
    uint32_t size = u32(p);
    const uint8_t *q = p + 4;
    st->a = q[0]; st->x = q[1]; st->y = q[2]; st->s = q[3]; st->p = q[4];
    st->pc = u16(q + 5);
    memcpy(st->mpr, q + 7, 8);
    st->n_ram = u32(q + 15);
    st->ram = q + 19;
    return p + size;
}

static const uint8_t *parse_test(const uint8_t *p, Test *t)
{
    uint32_t size = u32(p);
    memcpy(t->name, p + 4, 50);
    t->name[50] = 0;
    t->opcode = u32(p + 54);
    const uint8_t *q = parse_state(p + 58, &t->init);
    q = parse_state(q, &t->fin);
    t->num_cycles = u32(q);
    return p + size;
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static void print_flags(uint8_t p)
{
    const char *names = "NVTBDIZC";
    for (int i = 0; i < 8; i++) putchar(p & (0x80 >> i) ? names[i] : '.');
}

enum { F_REG = 1, F_P = 2, F_MPR = 4, F_RAM = 8, F_CYC = 16 };

static int run_test(const Test *t, HuC6280 *cpu, int verbose)
{
    n_touched = 0;
    for (uint32_t i = 0; i < t->init.n_ram; i++) {
        uint32_t a = u32(t->init.ram + i * 5) & (RAM_SIZE - 1);
        ram[a] = t->init.ram[i * 5 + 4];
        known[a] = 1;
        touch(a);
    }
    last_read_known = 1;
    cpu->a = t->init.a; cpu->x = t->init.x; cpu->y = t->init.y;
    cpu->s = t->init.s; cpu->p = t->init.p; cpu->pc = t->init.pc;
    memcpy(cpu->mpr, t->init.mpr, 8);
    cpu->irq_pending = 0;
    cpu->cycles = 0;

    int cyc = hu_step(cpu);

    int fail = 0;
    const State *f = &t->fin;
    int block = is_block_op(t->opcode);
    /* block ops pop X, A, Y from S-2, S-1, S (relative to the unchanged final S) */
    int kx = 1, ka = 1, ky = 1;
    if (block) {
        kx = known[hu_phys(cpu, (uint16_t)(0x2100 | (uint8_t)(cpu->s - 2)))];
        ka = known[hu_phys(cpu, (uint16_t)(0x2100 | (uint8_t)(cpu->s - 1)))];
        ky = known[hu_phys(cpu, (uint16_t)(0x2100 | cpu->s))];
    }
    if ((ka && cpu->a != f->a) || (kx && cpu->x != f->x) || (ky && cpu->y != f->y) ||
        cpu->s != f->s || cpu->pc != f->pc) fail |= F_REG;
    if (cpu->p != f->p) fail |= F_P;
    if (memcmp(cpu->mpr, f->mpr, 8)) fail |= F_MPR;
    uint32_t bad_ram = 0;
    for (uint32_t i = 0; i < f->n_ram; i++) {
        uint32_t a = u32(f->ram + i * 5) & (RAM_SIZE - 1);
        if (block && !known[a]) continue;
        if (ram[a] != f->ram[i * 5 + 4]) { fail |= F_RAM; bad_ram++; }
    }
    if ((uint32_t)cyc != t->num_cycles) fail |= F_CYC;

    if (fail && verbose) {
        printf("  FAIL %s\n", t->name);
        printf("    init  A=%02X X=%02X Y=%02X S=%02X PC=%04X P=", t->init.a, t->init.x, t->init.y, t->init.s, t->init.pc);
        print_flags(t->init.p);
        printf("\n    want  A=%02X X=%02X Y=%02X S=%02X PC=%04X P=", f->a, f->x, f->y, f->s, f->pc);
        print_flags(f->p);
        printf("  cycles %u\n    got   A=%02X X=%02X Y=%02X S=%02X PC=%04X P=", t->num_cycles, cpu->a, cpu->x, cpu->y, cpu->s, cpu->pc);
        print_flags(cpu->p);
        printf("  cycles %d\n", cyc);
        if (fail & F_MPR) {
            printf("    MPR want");
            for (int i = 0; i < 8; i++) printf(" %02X", f->mpr[i]);
            printf("  got");
            for (int i = 0; i < 8; i++) printf(" %02X", cpu->mpr[i]);
            printf("\n");
        }
        if (fail & F_RAM) {
            int shown = 0;
            for (uint32_t i = 0; i < f->n_ram && shown < 6; i++) {
                uint32_t a = u32(f->ram + i * 5) & (RAM_SIZE - 1);
                if (ram[a] != f->ram[i * 5 + 4]) {
                    printf("    RAM[%06X] want %02X got %02X\n", a, f->ram[i * 5 + 4], ram[a]);
                    shown++;
                }
            }
            if (bad_ram > 6) printf("    ... %u RAM mismatches\n", bad_ram);
        }
    }

    for (size_t i = 0; i < n_touched; i++) ram[touched[i]] = known[touched[i]] = 0;
    return fail;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <dir> [opcode-hex ...] [-v N]\n", argv[0]);
        return 2;
    }
    const char *dir = argv[1];
    int verbose = 1;
    int only[256], n_only = 0;
    int known_op[256] = {0};
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-v") && i + 1 < argc) {
            verbose = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--known") && i + 1 < argc) {
            for (char *s = argv[++i]; *s;) {
                char *end;
                long v = strtol(s, &end, 16);
                if (end == s) break;
                known_op[v & 0xFF] = 1;
                s = *end ? end + 1 : end;
            }
        } else {
            only[n_only++] = (int)strtol(argv[i], NULL, 16);
        }
    }
    int ops_known = 0;

    HuC6280 cpu;
    hu_init(&cpu, (HuBus){NULL, bus_read, bus_write});

    int total_pass = 0, total = 0, ops_clean = 0, ops_run = 0;
    for (int k = 0; k < (n_only ? n_only : 256); k++) {
        int op = n_only ? only[k] : k;
        char path[1024];
        snprintf(path, sizeof path, "%s/%02x.json.bin", dir, op);
        size_t len;
        uint8_t *buf = read_file(path, &len);
        if (!buf) { printf("op %02X: missing %s\n", op, path); continue; }

        uint32_t n = u32(buf);
        const uint8_t *p = buf + 4;
        int pass = 0, shown = 0, kinds = 0;
        int count[5] = {0};
        for (uint32_t i = 0; i < n; i++) {
            Test t;
            p = parse_test(p, &t);
            int fail = run_test(&t, &cpu, shown < verbose);
            if (!fail) { pass++; continue; }
            if (shown < verbose) shown++;
            kinds |= fail;
            for (int b = 0; b < 5; b++) if (fail >> b & 1) count[b]++;
        }
        free(buf);
        ops_run++;
        total += (int)n;
        total_pass += pass;
        if (pass == (int)n) {
            ops_clean++;
            if (known_op[op]) printf("op %02X: listed as known difference but now passes fully\n", op);
        } else {
            if (known_op[op]) ops_known++;
            printf("op %02X: %4d/%u pass   reg:%d P:%d MPR:%d RAM:%d cycles:%d%s\n",
                   op, pass, n, count[0], count[1], count[2], count[3], count[4],
                   known_op[op] ? "   (known difference)" : "");
        }
        (void)kinds;
        fflush(stdout);
    }
    printf("\n%d/%d opcodes fully passing, %d/%d tests (%.2f%%)",
           ops_clean, ops_run, total_pass, total, total ? 100.0 * total_pass / total : 0.0);
    if (ops_known) printf(", %d opcode(s) with known differences", ops_known);
    printf("\n");
    return ops_clean + ops_known == ops_run ? 0 : 1;
}
