/* HuC6280 CPU interpreter. See huc6280.h. */
#include "huc6280.h"

#include <string.h>

#define FC HU_FLAG_C
#define FZ HU_FLAG_Z
#define FI HU_FLAG_I
#define FD HU_FLAG_D
#define FB HU_FLAG_B
#define FT HU_FLAG_T
#define FV HU_FLAG_V
#define FN HU_FLAG_N

/* Base cycle counts. Dynamic extras: taken branch +2, T-mode ALU +3,
 * decimal ADC/SBC +1, block transfers +6 per byte. */
static const uint8_t base_cycles[256] = {
/*        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/* 0 */   8, 7, 3, 4, 6, 4, 6, 7, 3, 2, 2, 2, 7, 5, 7, 6,
/* 1 */   2, 7, 7, 4, 6, 4, 6, 7, 2, 5, 2, 2, 7, 5, 7, 6,
/* 2 */   7, 7, 3, 4, 4, 4, 6, 7, 4, 2, 2, 2, 5, 5, 7, 6,
/* 3 */   2, 7, 7, 2, 4, 4, 6, 7, 2, 5, 2, 2, 5, 5, 7, 6,
/* 4 */   7, 7, 3, 4, 8, 4, 6, 7, 3, 2, 2, 2, 4, 5, 7, 6,
/* 5 */   2, 7, 7, 5, 3, 4, 6, 7, 2, 5, 3, 2, 2, 5, 7, 6,
/* 6 */   7, 7, 2, 2, 4, 4, 6, 7, 4, 2, 2, 2, 7, 5, 7, 6,
/* 7 */   2, 7, 7,17, 4, 4, 6, 7, 2, 5, 4, 2, 7, 5, 7, 6,
/* 8 */   4, 7, 2, 7, 4, 4, 4, 7, 2, 2, 2, 2, 5, 5, 5, 6,
/* 9 */   2, 7, 7, 8, 4, 4, 4, 7, 2, 5, 2, 2, 5, 5, 5, 6,
/* A */   2, 7, 2, 7, 4, 4, 4, 7, 2, 2, 2, 2, 5, 5, 5, 6,
/* B */   2, 7, 7, 8, 4, 4, 4, 7, 2, 5, 2, 2, 5, 5, 5, 6,
/* C */   2, 7, 2,17, 4, 4, 6, 7, 2, 2, 2, 2, 5, 5, 7, 6,
/* D */   2, 7, 7,17, 3, 4, 6, 7, 2, 5, 3, 2, 2, 5, 7, 6,
/* E */   2, 7, 2,17, 4, 4, 6, 7, 2, 2, 2, 2, 5, 5, 7, 6,
/* F */   2, 7, 7,17, 2, 4, 6, 7, 2, 5, 4, 2, 2, 5, 7, 6,
};

/* --- bus helpers ------------------------------------------------------ */

static inline uint8_t rd(HuC6280 *c, uint16_t a) { return c->bus.read(c->bus.ctx, hu_phys(c, a)); }
static inline void wr(HuC6280 *c, uint16_t a, uint8_t v) { c->bus.write(c->bus.ctx, hu_phys(c, a), v); }
static inline uint16_t zp(uint8_t a) { return (uint16_t)(0x2000 | a); }

static inline uint8_t fetch(HuC6280 *c) { return rd(c, c->pc++); }
static inline uint16_t fetch16(HuC6280 *c)
{
    uint8_t lo = fetch(c);
    return (uint16_t)(lo | fetch(c) << 8);
}

static inline void push(HuC6280 *c, uint8_t v) { wr(c, (uint16_t)(0x2100 | c->s), v); c->s--; }
static inline uint8_t pull(HuC6280 *c) { c->s++; return rd(c, (uint16_t)(0x2100 | c->s)); }

static inline void set_nz(HuC6280 *c, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~(FN | FZ)) | (v & FN) | (v ? 0 : FZ));
}

static inline uint16_t zp_ptr(HuC6280 *c, uint8_t z)
{
    uint8_t lo = rd(c, zp(z));
    return (uint16_t)(lo | rd(c, zp((uint8_t)(z + 1))) << 8);
}

/* --- addressing modes (return logical address) ----------------------------- */

static inline uint16_t am_zp(HuC6280 *c)  { return zp(fetch(c)); }
static inline uint16_t am_zpx(HuC6280 *c) { return zp((uint8_t)(fetch(c) + c->x)); }
static inline uint16_t am_zpy(HuC6280 *c) { return zp((uint8_t)(fetch(c) + c->y)); }
static inline uint16_t am_abs(HuC6280 *c) { return fetch16(c); }
static inline uint16_t am_abx(HuC6280 *c) { return (uint16_t)(fetch16(c) + c->x); }
static inline uint16_t am_aby(HuC6280 *c) { return (uint16_t)(fetch16(c) + c->y); }
static inline uint16_t am_izx(HuC6280 *c) { return zp_ptr(c, (uint8_t)(fetch(c) + c->x)); }
static inline uint16_t am_izy(HuC6280 *c) { return (uint16_t)(zp_ptr(c, fetch(c)) + c->y); }
static inline uint16_t am_izp(HuC6280 *c) { return zp_ptr(c, fetch(c)); }

/* --- ALU ---------------------------------------------------------------- */

enum { ALU_ORA, ALU_AND, ALU_EOR, ALU_ADC, ALU_SBC };

static uint8_t do_adc(HuC6280 *c, uint8_t acc, uint8_t v, int *extra)
{
    int carry = c->p & FC;
    uint8_t res;
    if (c->p & FD) {
        int lo = (acc & 0x0F) + (v & 0x0F) + carry;
        int hi = (acc & 0xF0) + (v & 0xF0);
        if (lo > 0x09) { lo += 0x06; hi += 0x10; }
        if (hi > 0x90) hi += 0x60;
        res = (uint8_t)((lo & 0x0F) | (hi & 0xF0));
        c->p = (uint8_t)((c->p & ~FC) | (hi > 0xFF ? FC : 0));
        (*extra)++;
    } else {
        unsigned r = (unsigned)acc + v + carry;
        res = (uint8_t)r;
        c->p = (uint8_t)((c->p & ~(FC | FV)) | (r > 0xFF ? FC : 0) |
                         ((~(acc ^ v) & (acc ^ res) & 0x80) ? FV : 0));
    }
    set_nz(c, res);
    return res;
}

static uint8_t do_sbc(HuC6280 *c, uint8_t acc, uint8_t v, int *extra)
{
    int borrow = (c->p & FC) ? 0 : 1;
    uint8_t res;
    if (c->p & FD) {
        int lo = (acc & 0x0F) - (v & 0x0F) - borrow;
        int hi = (acc & 0xF0) - (v & 0xF0);
        if (lo < 0) { lo -= 0x06; hi -= 0x10; }
        if (hi < 0) hi -= 0x60;
        res = (uint8_t)((lo & 0x0F) | (hi & 0xF0));
        int bin = acc - v - borrow;
        c->p = (uint8_t)((c->p & ~FC) | (bin >= 0 ? FC : 0));
        (*extra)++;
    } else {
        int r = acc - v - borrow;
        res = (uint8_t)r;
        c->p = (uint8_t)((c->p & ~(FC | FV)) | (r >= 0 ? FC : 0) |
                         (((acc ^ v) & (acc ^ res) & 0x80) ? FV : 0));
    }
    set_nz(c, res);
    return res;
}

static uint8_t alu(HuC6280 *c, int op, uint8_t acc, uint8_t v, int *extra)
{
    switch (op) {
    case ALU_ORA: acc |= v; set_nz(c, acc); return acc;
    case ALU_AND: acc &= v; set_nz(c, acc); return acc;
    case ALU_EOR: acc ^= v; set_nz(c, acc); return acc;
    case ALU_ADC: return do_adc(c, acc, v, extra);
    default:      return do_sbc(c, acc, v, extra);
    }
}

/* ORA/AND/EOR/ADC operate on memory at zero page [X] instead of A when T is set. */
static void alu_op(HuC6280 *c, int op, uint8_t v, bool t, int *extra)
{
    if (t && op != ALU_SBC) {
        uint16_t ma = zp(c->x);
        uint8_t r = alu(c, op, rd(c, ma), v, extra);
        wr(c, ma, r);
        *extra += 3;
    } else {
        c->a = alu(c, op, c->a, v, extra);
    }
}

static inline void compare(HuC6280 *c, uint8_t reg, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~FC) | (reg >= v ? FC : 0));
    set_nz(c, (uint8_t)(reg - v));
}

static uint8_t shift(HuC6280 *c, int kind, uint8_t v)
{
    uint8_t carry_in = c->p & FC;
    uint8_t out;
    switch (kind) {
    case 0: c->p = (uint8_t)((c->p & ~FC) | (v >> 7)); out = (uint8_t)(v << 1); break;               /* ASL */
    case 1: c->p = (uint8_t)((c->p & ~FC) | (v >> 7)); out = (uint8_t)(v << 1 | carry_in); break;    /* ROL */
    case 2: c->p = (uint8_t)((c->p & ~FC) | (v & 1)); out = (uint8_t)(v >> 1); break;                /* LSR */
    default: c->p = (uint8_t)((c->p & ~FC) | (v & 1)); out = (uint8_t)(v >> 1 | carry_in << 7); break; /* ROR */
    }
    set_nz(c, out);
    return out;
}

static inline void rmw_shift(HuC6280 *c, int kind, uint16_t a) { wr(c, a, shift(c, kind, rd(c, a))); }
static inline void rmw_inc(HuC6280 *c, uint16_t a, int d)
{
    uint8_t v = (uint8_t)(rd(c, a) + d);
    wr(c, a, v);
    set_nz(c, v);
}

static inline void bit_test(HuC6280 *c, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~(FN | FV | FZ)) | (v & (FN | FV)) | ((c->a & v) ? 0 : FZ));
}

static inline void tst(HuC6280 *c, uint8_t imm, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~(FN | FV | FZ)) | (v & (FN | FV)) | ((imm & v) ? 0 : FZ));
}

static inline void tsb_trb(HuC6280 *c, uint16_t a, bool set)
{
    uint8_t v = rd(c, a);
    uint8_t r = set ? (uint8_t)(v | c->a) : (uint8_t)(v & ~c->a);
    /* N and V come from the operand before modification */
    c->p = (uint8_t)((c->p & ~(FN | FV | FZ)) | (v & (FN | FV)) | ((c->a & v) ? 0 : FZ));
    wr(c, a, r);
}

static inline int branch(HuC6280 *c, bool cond)
{
    int8_t off = (int8_t)fetch(c);
    if (cond) {
        c->pc = (uint16_t)(c->pc + off);
        return 2;
    }
    return 0;
}

static int block_transfer(HuC6280 *c, uint8_t op)
{
    /* Hardware order (per bus trace): push Y, A, X first, then fetch the operands.
     * Matters when the instruction itself sits in the stack page. */
    push(c, c->y);
    push(c, c->a);
    push(c, c->x);
    uint16_t src = fetch16(c), dst = fetch16(c), len = fetch16(c);
    uint32_t n = len ? len : 0x10000u;
    bool alt = false;
    for (uint32_t i = 0; i < n; i++) {
        wr(c, dst, rd(c, src));
        switch (op) {
        case 0x73: src++; dst++; break;                                   /* TII */
        case 0xC3: src--; dst--; break;                                   /* TDD */
        case 0xD3: src++; break;                                          /* TIN */
        case 0xE3: src++; dst = (uint16_t)(dst + (alt ? -1 : 1)); alt = !alt; break; /* TIA */
        default:   dst++; src = (uint16_t)(src + (alt ? -1 : 1)); alt = !alt; break; /* TAI */
        }
    }
    c->x = pull(c);
    c->a = pull(c);
    c->y = pull(c);
    return (int)(6 * n);
}

static int take_interrupt(HuC6280 *c, uint16_t vector)
{
    push(c, (uint8_t)(c->pc >> 8));
    push(c, (uint8_t)c->pc);
    push(c, (uint8_t)(c->p & ~FB));
    c->p = (uint8_t)((c->p | FI) & ~(FD | FT));
    uint8_t lo = rd(c, vector);
    c->pc = (uint16_t)(lo | rd(c, (uint16_t)(vector + 1)) << 8);
    return 8;
}

/* --- public ------------------------------------------------------------- */

void hu_init(HuC6280 *c, HuBus bus)
{
    memset(c, 0, sizeof *c);
    c->bus = bus;
}

void hu_reset(HuC6280 *c)
{
    c->mpr[7] = 0x00;
    c->p = (uint8_t)((c->p | FI) & ~(FD | FT));
    c->high_speed = false;
    uint8_t lo = rd(c, HU_VEC_RESET);
    c->pc = (uint16_t)(lo | rd(c, HU_VEC_RESET + 1) << 8);
}

int hu_step(HuC6280 *c)
{
    if (c->irq_pending && !(c->p & FI)) {
        uint16_t vec = (c->irq_pending & HU_TIRQ) ? HU_VEC_TIMER
                     : (c->irq_pending & HU_IRQ1) ? HU_VEC_IRQ1 : HU_VEC_IRQ2;
        int n = take_interrupt(c, vec);
        c->cycles += (uint64_t)n;
        return n;
    }

    uint8_t op = fetch(c);
    bool t = (c->p & FT) != 0;
    c->p &= (uint8_t)~FT;
    int extra = 0;
    uint16_t a;
    uint8_t v;

    switch (op) {
    /* --- loads / stores ---------------------------------------------- */
    case 0xA9: c->a = fetch(c); set_nz(c, c->a); break;
    case 0xA5: c->a = rd(c, am_zp(c));  set_nz(c, c->a); break;
    case 0xB5: c->a = rd(c, am_zpx(c)); set_nz(c, c->a); break;
    case 0xAD: c->a = rd(c, am_abs(c)); set_nz(c, c->a); break;
    case 0xBD: c->a = rd(c, am_abx(c)); set_nz(c, c->a); break;
    case 0xB9: c->a = rd(c, am_aby(c)); set_nz(c, c->a); break;
    case 0xA1: c->a = rd(c, am_izx(c)); set_nz(c, c->a); break;
    case 0xB1: c->a = rd(c, am_izy(c)); set_nz(c, c->a); break;
    case 0xB2: c->a = rd(c, am_izp(c)); set_nz(c, c->a); break;

    case 0xA2: c->x = fetch(c); set_nz(c, c->x); break;
    case 0xA6: c->x = rd(c, am_zp(c));  set_nz(c, c->x); break;
    case 0xB6: c->x = rd(c, am_zpy(c)); set_nz(c, c->x); break;
    case 0xAE: c->x = rd(c, am_abs(c)); set_nz(c, c->x); break;
    case 0xBE: c->x = rd(c, am_aby(c)); set_nz(c, c->x); break;

    case 0xA0: c->y = fetch(c); set_nz(c, c->y); break;
    case 0xA4: c->y = rd(c, am_zp(c));  set_nz(c, c->y); break;
    case 0xB4: c->y = rd(c, am_zpx(c)); set_nz(c, c->y); break;
    case 0xAC: c->y = rd(c, am_abs(c)); set_nz(c, c->y); break;
    case 0xBC: c->y = rd(c, am_abx(c)); set_nz(c, c->y); break;

    case 0x85: wr(c, am_zp(c),  c->a); break;
    case 0x95: wr(c, am_zpx(c), c->a); break;
    case 0x8D: wr(c, am_abs(c), c->a); break;
    case 0x9D: wr(c, am_abx(c), c->a); break;
    case 0x99: wr(c, am_aby(c), c->a); break;
    case 0x81: wr(c, am_izx(c), c->a); break;
    case 0x91: wr(c, am_izy(c), c->a); break;
    case 0x92: wr(c, am_izp(c), c->a); break;

    case 0x86: wr(c, am_zp(c),  c->x); break;
    case 0x96: wr(c, am_zpy(c), c->x); break;
    case 0x8E: wr(c, am_abs(c), c->x); break;
    case 0x84: wr(c, am_zp(c),  c->y); break;
    case 0x94: wr(c, am_zpx(c), c->y); break;
    case 0x8C: wr(c, am_abs(c), c->y); break;

    case 0x64: wr(c, am_zp(c),  0); break;
    case 0x74: wr(c, am_zpx(c), 0); break;
    case 0x9C: wr(c, am_abs(c), 0); break;
    case 0x9E: wr(c, am_abx(c), 0); break;

    /* --- ALU: ORA AND EOR ADC SBC ---------------------------------------- */
#define ALU_CASES(base, kind)                                                 \
    case base + 0x09: alu_op(c, kind, fetch(c), t, &extra); break;            \
    case base + 0x05: alu_op(c, kind, rd(c, am_zp(c)), t, &extra); break;     \
    case base + 0x15: alu_op(c, kind, rd(c, am_zpx(c)), t, &extra); break;    \
    case base + 0x0D: alu_op(c, kind, rd(c, am_abs(c)), t, &extra); break;    \
    case base + 0x1D: alu_op(c, kind, rd(c, am_abx(c)), t, &extra); break;    \
    case base + 0x19: alu_op(c, kind, rd(c, am_aby(c)), t, &extra); break;    \
    case base + 0x01: alu_op(c, kind, rd(c, am_izx(c)), t, &extra); break;    \
    case base + 0x11: alu_op(c, kind, rd(c, am_izy(c)), t, &extra); break;    \
    case base + 0x12: alu_op(c, kind, rd(c, am_izp(c)), t, &extra); break;
    ALU_CASES(0x00, ALU_ORA)
    ALU_CASES(0x20, ALU_AND)
    ALU_CASES(0x40, ALU_EOR)
    ALU_CASES(0x60, ALU_ADC)
    ALU_CASES(0xE0, ALU_SBC)
#undef ALU_CASES

    /* --- compares ------------------------------------------------------ */
    case 0xC9: compare(c, c->a, fetch(c)); break;
    case 0xC5: compare(c, c->a, rd(c, am_zp(c))); break;
    case 0xD5: compare(c, c->a, rd(c, am_zpx(c))); break;
    case 0xCD: compare(c, c->a, rd(c, am_abs(c))); break;
    case 0xDD: compare(c, c->a, rd(c, am_abx(c))); break;
    case 0xD9: compare(c, c->a, rd(c, am_aby(c))); break;
    case 0xC1: compare(c, c->a, rd(c, am_izx(c))); break;
    case 0xD1: compare(c, c->a, rd(c, am_izy(c))); break;
    case 0xD2: compare(c, c->a, rd(c, am_izp(c))); break;
    case 0xE0: compare(c, c->x, fetch(c)); break;
    case 0xE4: compare(c, c->x, rd(c, am_zp(c))); break;
    case 0xEC: compare(c, c->x, rd(c, am_abs(c))); break;
    case 0xC0: compare(c, c->y, fetch(c)); break;
    case 0xC4: compare(c, c->y, rd(c, am_zp(c))); break;
    case 0xCC: compare(c, c->y, rd(c, am_abs(c))); break;

    /* --- bit tests ----------------------------------------------------- */
    case 0x89: bit_test(c, fetch(c)); break;
    case 0x24: bit_test(c, rd(c, am_zp(c))); break;
    case 0x34: bit_test(c, rd(c, am_zpx(c))); break;
    case 0x2C: bit_test(c, rd(c, am_abs(c))); break;
    case 0x3C: bit_test(c, rd(c, am_abx(c))); break;
    case 0x83: v = fetch(c); tst(c, v, rd(c, am_zp(c))); break;
    case 0xA3: v = fetch(c); tst(c, v, rd(c, am_zpx(c))); break;
    case 0x93: v = fetch(c); tst(c, v, rd(c, am_abs(c))); break;
    case 0xB3: v = fetch(c); tst(c, v, rd(c, am_abx(c))); break;
    case 0x04: tsb_trb(c, am_zp(c), true); break;
    case 0x0C: tsb_trb(c, am_abs(c), true); break;
    case 0x14: tsb_trb(c, am_zp(c), false); break;
    case 0x1C: tsb_trb(c, am_abs(c), false); break;

    /* --- shifts / inc / dec -------------------------------------------- */
    case 0x0A: c->a = shift(c, 0, c->a); break;
    case 0x2A: c->a = shift(c, 1, c->a); break;
    case 0x4A: c->a = shift(c, 2, c->a); break;
    case 0x6A: c->a = shift(c, 3, c->a); break;
    case 0x06: rmw_shift(c, 0, am_zp(c)); break;
    case 0x16: rmw_shift(c, 0, am_zpx(c)); break;
    case 0x0E: rmw_shift(c, 0, am_abs(c)); break;
    case 0x1E: rmw_shift(c, 0, am_abx(c)); break;
    case 0x26: rmw_shift(c, 1, am_zp(c)); break;
    case 0x36: rmw_shift(c, 1, am_zpx(c)); break;
    case 0x2E: rmw_shift(c, 1, am_abs(c)); break;
    case 0x3E: rmw_shift(c, 1, am_abx(c)); break;
    case 0x46: rmw_shift(c, 2, am_zp(c)); break;
    case 0x56: rmw_shift(c, 2, am_zpx(c)); break;
    case 0x4E: rmw_shift(c, 2, am_abs(c)); break;
    case 0x5E: rmw_shift(c, 2, am_abx(c)); break;
    case 0x66: rmw_shift(c, 3, am_zp(c)); break;
    case 0x76: rmw_shift(c, 3, am_zpx(c)); break;
    case 0x6E: rmw_shift(c, 3, am_abs(c)); break;
    case 0x7E: rmw_shift(c, 3, am_abx(c)); break;

    case 0x1A: c->a++; set_nz(c, c->a); break;
    case 0x3A: c->a--; set_nz(c, c->a); break;
    case 0xE6: rmw_inc(c, am_zp(c), 1); break;
    case 0xF6: rmw_inc(c, am_zpx(c), 1); break;
    case 0xEE: rmw_inc(c, am_abs(c), 1); break;
    case 0xFE: rmw_inc(c, am_abx(c), 1); break;
    case 0xC6: rmw_inc(c, am_zp(c), -1); break;
    case 0xD6: rmw_inc(c, am_zpx(c), -1); break;
    case 0xCE: rmw_inc(c, am_abs(c), -1); break;
    case 0xDE: rmw_inc(c, am_abx(c), -1); break;
    case 0xE8: c->x++; set_nz(c, c->x); break;
    case 0xCA: c->x--; set_nz(c, c->x); break;
    case 0xC8: c->y++; set_nz(c, c->y); break;
    case 0x88: c->y--; set_nz(c, c->y); break;

    /* --- register transfers -------------------------------------------- */
    case 0xAA: c->x = c->a; set_nz(c, c->x); break;
    case 0xA8: c->y = c->a; set_nz(c, c->y); break;
    case 0x8A: c->a = c->x; set_nz(c, c->a); break;
    case 0x98: c->a = c->y; set_nz(c, c->a); break;
    case 0xBA: c->x = c->s; set_nz(c, c->x); break;
    case 0x9A: c->s = c->x; break;
    case 0x22: v = c->a; c->a = c->x; c->x = v; break;   /* SAX */
    case 0x42: v = c->a; c->a = c->y; c->y = v; break;   /* SAY */
    case 0x02: v = c->x; c->x = c->y; c->y = v; break;   /* SXY */
    case 0x62: c->a = 0; break;                          /* CLA */
    case 0x82: c->x = 0; break;                          /* CLX */
    case 0xC2: c->y = 0; break;                          /* CLY */

    /* --- stack ---------------------------------------------------------- */
    case 0x48: push(c, c->a); break;
    case 0xDA: push(c, c->x); break;
    case 0x5A: push(c, c->y); break;
    case 0x08: push(c, (uint8_t)(c->p | FB)); break;
    case 0x68: c->a = pull(c); set_nz(c, c->a); break;
    case 0xFA: c->x = pull(c); set_nz(c, c->x); break;
    case 0x7A: c->y = pull(c); set_nz(c, c->y); break;
    case 0x28: c->p = (uint8_t)(pull(c) & ~FB); break;   /* B only exists on the stack */

    /* --- flags ----------------------------------------------------------- */
    case 0x18: c->p &= (uint8_t)~FC; break;
    case 0x38: c->p |= FC; break;
    case 0x58: c->p &= (uint8_t)~FI; break;
    case 0x78: c->p |= FI; break;
    case 0xD8: c->p &= (uint8_t)~FD; break;
    case 0xF8: c->p |= FD; break;
    case 0xB8: c->p &= (uint8_t)~FV; break;
    case 0xF4: c->p |= FT; break;                         /* SET */

    /* --- control flow ---------------------------------------------------- */
    case 0x4C: c->pc = fetch16(c); break;
    case 0x6C: a = fetch16(c); c->pc = (uint16_t)(rd(c, a) | rd(c, (uint16_t)(a + 1)) << 8); break;
    case 0x7C: a = (uint16_t)(fetch16(c) + c->x); c->pc = (uint16_t)(rd(c, a) | rd(c, (uint16_t)(a + 1)) << 8); break;
    case 0x20:
        a = fetch16(c);
        push(c, (uint8_t)((c->pc - 1) >> 8));
        push(c, (uint8_t)(c->pc - 1));
        c->pc = a;
        break;
    case 0x44: {
        int8_t off = (int8_t)fetch(c);
        push(c, (uint8_t)((c->pc - 1) >> 8));
        push(c, (uint8_t)(c->pc - 1));
        c->pc = (uint16_t)(c->pc + off);
        break;
    }
    case 0x60: v = pull(c); c->pc = (uint16_t)((v | pull(c) << 8) + 1); break;
    case 0x40: c->p = (uint8_t)(pull(c) & ~FB); v = pull(c); c->pc = (uint16_t)(v | pull(c) << 8); break;
    case 0x00:
        c->pc++;
        push(c, (uint8_t)(c->pc >> 8));
        push(c, (uint8_t)c->pc);
        push(c, (uint8_t)(c->p | FB));
        c->p = (uint8_t)((c->p | FI) & ~(FD | FT));
        c->pc = (uint16_t)(rd(c, HU_VEC_IRQ2) | rd(c, HU_VEC_IRQ2 + 1) << 8);
        break;

    case 0x10: extra += branch(c, !(c->p & FN)); break;
    case 0x30: extra += branch(c, (c->p & FN) != 0); break;
    case 0x50: extra += branch(c, !(c->p & FV)); break;
    case 0x70: extra += branch(c, (c->p & FV) != 0); break;
    case 0x90: extra += branch(c, !(c->p & FC)); break;
    case 0xB0: extra += branch(c, (c->p & FC) != 0); break;
    case 0xD0: extra += branch(c, !(c->p & FZ)); break;
    case 0xF0: extra += branch(c, (c->p & FZ) != 0); break;
    case 0x80: branch(c, true); break;

    /* --- bit ops on zero page --------------------------------------------- */
    case 0x07: case 0x17: case 0x27: case 0x37: case 0x47: case 0x57: case 0x67: case 0x77:
        a = am_zp(c); wr(c, a, (uint8_t)(rd(c, a) & ~(1u << (op >> 4)))); break;
    case 0x87: case 0x97: case 0xA7: case 0xB7: case 0xC7: case 0xD7: case 0xE7: case 0xF7:
        a = am_zp(c); wr(c, a, (uint8_t)(rd(c, a) | (1u << ((op >> 4) - 8)))); break;
    case 0x0F: case 0x1F: case 0x2F: case 0x3F: case 0x4F: case 0x5F: case 0x6F: case 0x7F:
        v = rd(c, am_zp(c)); extra += branch(c, !(v & (1u << (op >> 4)))); break;
    case 0x8F: case 0x9F: case 0xAF: case 0xBF: case 0xCF: case 0xDF: case 0xEF: case 0xFF:
        v = rd(c, am_zp(c)); extra += branch(c, (v & (1u << ((op >> 4) - 8))) != 0); break;

    /* --- HuC6280 specific -------------------------------------------------- */
    case 0x53:                                            /* TAM */
        v = fetch(c);
        for (int i = 0; i < 8; i++)
            if (v >> i & 1) c->mpr[i] = c->a;
        c->mpr_buffer = c->a;
        break;
    case 0x43:                                            /* TMA */
        v = fetch(c);
        if (v == 0) {
            c->a = c->mpr_buffer;
        } else {          /* several selected registers are ORed together */
            uint8_t r = 0;
            for (int i = 0; i < 8; i++)
                if (v >> i & 1) r |= c->mpr[i];
            c->a = c->mpr_buffer = r;
        }
        break;
    case 0x03: c->bus.write(c->bus.ctx, 0x1FE000, fetch(c)); break;   /* ST0 */
    case 0x13: c->bus.write(c->bus.ctx, 0x1FE002, fetch(c)); break;   /* ST1 */
    case 0x23: c->bus.write(c->bus.ctx, 0x1FE003, fetch(c)); break;   /* ST2 */
    case 0x54: c->high_speed = false; break;                          /* CSL */
    case 0xD4: c->high_speed = true; break;                           /* CSH */
    case 0x73: case 0xC3: case 0xD3: case 0xE3: case 0xF3:
        extra += block_transfer(c, op);
        break;

    default:  /* undefined opcodes and NOP */
        break;
    }

    int n = base_cycles[op] + extra;
    c->cycles += (uint64_t)n;
    return n;
}
