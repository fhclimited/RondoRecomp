/* HuC6280 interpreter: fetch/decode around the shared semantics in huc6280_ops.h. */
#include "huc6280.h"

#include <string.h>

#include "huc6280_ops.h"

/* Base cycle counts. Dynamic extras: taken branch +2, T-mode ALU +3,
 * decimal ADC/SBC +1, block transfers +6 per byte. */
const uint8_t hu_base_cycles[256] = {
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

static inline uint8_t fetch(HuC6280 *c) { return hu_rd(c, c->pc++); }
static inline uint16_t fetch16(HuC6280 *c)
{
    uint8_t lo = fetch(c);
    return (uint16_t)(lo | fetch(c) << 8);
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

static int take_interrupt(HuC6280 *c, uint16_t vector)
{
    hu_push(c, (uint8_t)(c->pc >> 8));
    hu_push(c, (uint8_t)c->pc);
    hu_push(c, (uint8_t)(c->p & ~HU_FB));
    c->p = (uint8_t)((c->p | HU_FI) & ~(HU_FD | HU_FT));
    c->pc = hu_rd16(c, vector);
    return 8;
}

void hu_init(HuC6280 *c, HuBus bus)
{
    memset(c, 0, sizeof *c);
    c->bus = bus;
}

void hu_reset(HuC6280 *c)
{
    c->mpr[7] = 0x00;
    c->p = (uint8_t)((c->p | HU_FI) & ~(HU_FD | HU_FT));
    c->high_speed = false;
    c->pc = hu_rd16(c, HU_VEC_RESET);
}

int hu_step(HuC6280 *c)
{
    if (c->irq_pending && !(c->p & HU_FI)) {
        uint16_t vec = (c->irq_pending & HU_TIRQ) ? HU_VEC_TIMER
                     : (c->irq_pending & HU_IRQ1) ? HU_VEC_IRQ1 : HU_VEC_IRQ2;
        int n = take_interrupt(c, vec);
        c->cycles += (uint64_t)n;
        return n;
    }

    uint8_t op = fetch(c);
    bool t = (c->p & HU_FT) != 0;
    c->p &= (uint8_t)~HU_FT;
    int extra = 0;
    uint16_t a;
    uint8_t v;

    switch (op) {
    /* --- loads / stores ---------------------------------------------- */
    case 0xA9: c->a = fetch(c); hu_set_nz(c, c->a); break;
    case 0xA5: c->a = hu_rd(c, hu_zp(fetch(c))); hu_set_nz(c, c->a); break;
    case 0xB5: c->a = hu_rd(c, hu_ea_zpx(c, fetch(c))); hu_set_nz(c, c->a); break;
    case 0xAD: c->a = hu_rd(c, fetch16(c)); hu_set_nz(c, c->a); break;
    case 0xBD: c->a = hu_rd(c, hu_ea_abx(c, fetch16(c))); hu_set_nz(c, c->a); break;
    case 0xB9: c->a = hu_rd(c, hu_ea_aby(c, fetch16(c))); hu_set_nz(c, c->a); break;
    case 0xA1: c->a = hu_rd(c, hu_ea_izx(c, fetch(c))); hu_set_nz(c, c->a); break;
    case 0xB1: c->a = hu_rd(c, hu_ea_izy(c, fetch(c))); hu_set_nz(c, c->a); break;
    case 0xB2: c->a = hu_rd(c, hu_ea_izp(c, fetch(c))); hu_set_nz(c, c->a); break;

    case 0xA2: c->x = fetch(c); hu_set_nz(c, c->x); break;
    case 0xA6: c->x = hu_rd(c, hu_zp(fetch(c))); hu_set_nz(c, c->x); break;
    case 0xB6: c->x = hu_rd(c, hu_ea_zpy(c, fetch(c))); hu_set_nz(c, c->x); break;
    case 0xAE: c->x = hu_rd(c, fetch16(c)); hu_set_nz(c, c->x); break;
    case 0xBE: c->x = hu_rd(c, hu_ea_aby(c, fetch16(c))); hu_set_nz(c, c->x); break;

    case 0xA0: c->y = fetch(c); hu_set_nz(c, c->y); break;
    case 0xA4: c->y = hu_rd(c, hu_zp(fetch(c))); hu_set_nz(c, c->y); break;
    case 0xB4: c->y = hu_rd(c, hu_ea_zpx(c, fetch(c))); hu_set_nz(c, c->y); break;
    case 0xAC: c->y = hu_rd(c, fetch16(c)); hu_set_nz(c, c->y); break;
    case 0xBC: c->y = hu_rd(c, hu_ea_abx(c, fetch16(c))); hu_set_nz(c, c->y); break;

    case 0x85: hu_wr(c, hu_zp(fetch(c)), c->a); break;
    case 0x95: hu_wr(c, hu_ea_zpx(c, fetch(c)), c->a); break;
    case 0x8D: hu_wr(c, fetch16(c), c->a); break;
    case 0x9D: hu_wr(c, hu_ea_abx(c, fetch16(c)), c->a); break;
    case 0x99: hu_wr(c, hu_ea_aby(c, fetch16(c)), c->a); break;
    case 0x81: hu_wr(c, hu_ea_izx(c, fetch(c)), c->a); break;
    case 0x91: hu_wr(c, hu_ea_izy(c, fetch(c)), c->a); break;
    case 0x92: hu_wr(c, hu_ea_izp(c, fetch(c)), c->a); break;

    case 0x86: hu_wr(c, hu_zp(fetch(c)), c->x); break;
    case 0x96: hu_wr(c, hu_ea_zpy(c, fetch(c)), c->x); break;
    case 0x8E: hu_wr(c, fetch16(c), c->x); break;
    case 0x84: hu_wr(c, hu_zp(fetch(c)), c->y); break;
    case 0x94: hu_wr(c, hu_ea_zpx(c, fetch(c)), c->y); break;
    case 0x8C: hu_wr(c, fetch16(c), c->y); break;

    case 0x64: hu_wr(c, hu_zp(fetch(c)), 0); break;
    case 0x74: hu_wr(c, hu_ea_zpx(c, fetch(c)), 0); break;
    case 0x9C: hu_wr(c, fetch16(c), 0); break;
    case 0x9E: hu_wr(c, hu_ea_abx(c, fetch16(c)), 0); break;

    /* --- ALU: ORA AND EOR ADC SBC ---------------------------------------- */
#define ALU_CASES(base, kind)                                                              \
    case base + 0x09: extra += hu_alu(c, kind, fetch(c), t); break;                        \
    case base + 0x05: extra += hu_alu(c, kind, hu_rd(c, hu_zp(fetch(c))), t); break;       \
    case base + 0x15: extra += hu_alu(c, kind, hu_rd(c, hu_ea_zpx(c, fetch(c))), t); break;\
    case base + 0x0D: extra += hu_alu(c, kind, hu_rd(c, fetch16(c)), t); break;            \
    case base + 0x1D: extra += hu_alu(c, kind, hu_rd(c, hu_ea_abx(c, fetch16(c))), t); break; \
    case base + 0x19: extra += hu_alu(c, kind, hu_rd(c, hu_ea_aby(c, fetch16(c))), t); break; \
    case base + 0x01: extra += hu_alu(c, kind, hu_rd(c, hu_ea_izx(c, fetch(c))), t); break;\
    case base + 0x11: extra += hu_alu(c, kind, hu_rd(c, hu_ea_izy(c, fetch(c))), t); break;\
    case base + 0x12: extra += hu_alu(c, kind, hu_rd(c, hu_ea_izp(c, fetch(c))), t); break;
    ALU_CASES(0x00, HU_ORA)
    ALU_CASES(0x20, HU_AND)
    ALU_CASES(0x40, HU_EOR)
    ALU_CASES(0x60, HU_ADC)
    ALU_CASES(0xE0, HU_SBC)
#undef ALU_CASES

    /* --- compares ------------------------------------------------------ */
    case 0xC9: hu_cmp(c, c->a, fetch(c)); break;
    case 0xC5: hu_cmp(c, c->a, hu_rd(c, hu_zp(fetch(c)))); break;
    case 0xD5: hu_cmp(c, c->a, hu_rd(c, hu_ea_zpx(c, fetch(c)))); break;
    case 0xCD: hu_cmp(c, c->a, hu_rd(c, fetch16(c))); break;
    case 0xDD: hu_cmp(c, c->a, hu_rd(c, hu_ea_abx(c, fetch16(c)))); break;
    case 0xD9: hu_cmp(c, c->a, hu_rd(c, hu_ea_aby(c, fetch16(c)))); break;
    case 0xC1: hu_cmp(c, c->a, hu_rd(c, hu_ea_izx(c, fetch(c)))); break;
    case 0xD1: hu_cmp(c, c->a, hu_rd(c, hu_ea_izy(c, fetch(c)))); break;
    case 0xD2: hu_cmp(c, c->a, hu_rd(c, hu_ea_izp(c, fetch(c)))); break;
    case 0xE0: hu_cmp(c, c->x, fetch(c)); break;
    case 0xE4: hu_cmp(c, c->x, hu_rd(c, hu_zp(fetch(c)))); break;
    case 0xEC: hu_cmp(c, c->x, hu_rd(c, fetch16(c))); break;
    case 0xC0: hu_cmp(c, c->y, fetch(c)); break;
    case 0xC4: hu_cmp(c, c->y, hu_rd(c, hu_zp(fetch(c)))); break;
    case 0xCC: hu_cmp(c, c->y, hu_rd(c, fetch16(c))); break;

    /* --- bit tests ----------------------------------------------------- */
    case 0x89: hu_bit(c, fetch(c)); break;
    case 0x24: hu_bit(c, hu_rd(c, hu_zp(fetch(c)))); break;
    case 0x34: hu_bit(c, hu_rd(c, hu_ea_zpx(c, fetch(c)))); break;
    case 0x2C: hu_bit(c, hu_rd(c, fetch16(c))); break;
    case 0x3C: hu_bit(c, hu_rd(c, hu_ea_abx(c, fetch16(c)))); break;
    case 0x83: v = fetch(c); hu_tst(c, v, hu_rd(c, hu_zp(fetch(c)))); break;
    case 0xA3: v = fetch(c); hu_tst(c, v, hu_rd(c, hu_ea_zpx(c, fetch(c)))); break;
    case 0x93: v = fetch(c); hu_tst(c, v, hu_rd(c, fetch16(c))); break;
    case 0xB3: v = fetch(c); hu_tst(c, v, hu_rd(c, hu_ea_abx(c, fetch16(c)))); break;
    case 0x04: hu_tsb_trb(c, hu_zp(fetch(c)), true); break;
    case 0x0C: hu_tsb_trb(c, fetch16(c), true); break;
    case 0x14: hu_tsb_trb(c, hu_zp(fetch(c)), false); break;
    case 0x1C: hu_tsb_trb(c, fetch16(c), false); break;

    /* --- shifts / inc / dec -------------------------------------------- */
    case 0x0A: c->a = hu_shift(c, HU_ASL, c->a); break;
    case 0x2A: c->a = hu_shift(c, HU_ROL, c->a); break;
    case 0x4A: c->a = hu_shift(c, HU_LSR, c->a); break;
    case 0x6A: c->a = hu_shift(c, HU_ROR, c->a); break;
    case 0x06: hu_rmw_shift(c, HU_ASL, hu_zp(fetch(c))); break;
    case 0x16: hu_rmw_shift(c, HU_ASL, hu_ea_zpx(c, fetch(c))); break;
    case 0x0E: hu_rmw_shift(c, HU_ASL, fetch16(c)); break;
    case 0x1E: hu_rmw_shift(c, HU_ASL, hu_ea_abx(c, fetch16(c))); break;
    case 0x26: hu_rmw_shift(c, HU_ROL, hu_zp(fetch(c))); break;
    case 0x36: hu_rmw_shift(c, HU_ROL, hu_ea_zpx(c, fetch(c))); break;
    case 0x2E: hu_rmw_shift(c, HU_ROL, fetch16(c)); break;
    case 0x3E: hu_rmw_shift(c, HU_ROL, hu_ea_abx(c, fetch16(c))); break;
    case 0x46: hu_rmw_shift(c, HU_LSR, hu_zp(fetch(c))); break;
    case 0x56: hu_rmw_shift(c, HU_LSR, hu_ea_zpx(c, fetch(c))); break;
    case 0x4E: hu_rmw_shift(c, HU_LSR, fetch16(c)); break;
    case 0x5E: hu_rmw_shift(c, HU_LSR, hu_ea_abx(c, fetch16(c))); break;
    case 0x66: hu_rmw_shift(c, HU_ROR, hu_zp(fetch(c))); break;
    case 0x76: hu_rmw_shift(c, HU_ROR, hu_ea_zpx(c, fetch(c))); break;
    case 0x6E: hu_rmw_shift(c, HU_ROR, fetch16(c)); break;
    case 0x7E: hu_rmw_shift(c, HU_ROR, hu_ea_abx(c, fetch16(c))); break;

    case 0x1A: c->a++; hu_set_nz(c, c->a); break;
    case 0x3A: c->a--; hu_set_nz(c, c->a); break;
    case 0xE6: hu_rmw_inc(c, hu_zp(fetch(c)), 1); break;
    case 0xF6: hu_rmw_inc(c, hu_ea_zpx(c, fetch(c)), 1); break;
    case 0xEE: hu_rmw_inc(c, fetch16(c), 1); break;
    case 0xFE: hu_rmw_inc(c, hu_ea_abx(c, fetch16(c)), 1); break;
    case 0xC6: hu_rmw_inc(c, hu_zp(fetch(c)), -1); break;
    case 0xD6: hu_rmw_inc(c, hu_ea_zpx(c, fetch(c)), -1); break;
    case 0xCE: hu_rmw_inc(c, fetch16(c), -1); break;
    case 0xDE: hu_rmw_inc(c, hu_ea_abx(c, fetch16(c)), -1); break;
    case 0xE8: c->x++; hu_set_nz(c, c->x); break;
    case 0xCA: c->x--; hu_set_nz(c, c->x); break;
    case 0xC8: c->y++; hu_set_nz(c, c->y); break;
    case 0x88: c->y--; hu_set_nz(c, c->y); break;

    /* --- register transfers -------------------------------------------- */
    case 0xAA: c->x = c->a; hu_set_nz(c, c->x); break;
    case 0xA8: c->y = c->a; hu_set_nz(c, c->y); break;
    case 0x8A: c->a = c->x; hu_set_nz(c, c->a); break;
    case 0x98: c->a = c->y; hu_set_nz(c, c->a); break;
    case 0xBA: c->x = c->s; hu_set_nz(c, c->x); break;
    case 0x9A: c->s = c->x; break;
    case 0x22: v = c->a; c->a = c->x; c->x = v; break;   /* SAX */
    case 0x42: v = c->a; c->a = c->y; c->y = v; break;   /* SAY */
    case 0x02: v = c->x; c->x = c->y; c->y = v; break;   /* SXY */
    case 0x62: c->a = 0; break;                          /* CLA */
    case 0x82: c->x = 0; break;                          /* CLX */
    case 0xC2: c->y = 0; break;                          /* CLY */

    /* --- stack ---------------------------------------------------------- */
    case 0x48: hu_push(c, c->a); break;
    case 0xDA: hu_push(c, c->x); break;
    case 0x5A: hu_push(c, c->y); break;
    case 0x08: hu_push(c, (uint8_t)(c->p | HU_FB)); break;
    case 0x68: c->a = hu_pull(c); hu_set_nz(c, c->a); break;
    case 0xFA: c->x = hu_pull(c); hu_set_nz(c, c->x); break;
    case 0x7A: c->y = hu_pull(c); hu_set_nz(c, c->y); break;
    case 0x28: c->p = (uint8_t)(hu_pull(c) & ~HU_FB); break;   /* B only exists on the stack */

    /* --- flags ----------------------------------------------------------- */
    case 0x18: c->p &= (uint8_t)~HU_FC; break;
    case 0x38: c->p |= HU_FC; break;
    case 0x58: c->p &= (uint8_t)~HU_FI; break;
    case 0x78: c->p |= HU_FI; break;
    case 0xD8: c->p &= (uint8_t)~HU_FD; break;
    case 0xF8: c->p |= HU_FD; break;
    case 0xB8: c->p &= (uint8_t)~HU_FV; break;
    case 0xF4: c->p |= HU_FT; break;                      /* SET */

    /* --- control flow ---------------------------------------------------- */
    case 0x4C: c->pc = fetch16(c); break;
    case 0x6C: c->pc = hu_rd16(c, fetch16(c)); break;
    case 0x7C: c->pc = hu_rd16(c, hu_ea_abx(c, fetch16(c))); break;
    case 0x20:
        a = fetch16(c);
        hu_push_return(c, (uint16_t)(c->pc - 1));
        c->pc = a;
        break;
    case 0x44: {
        int8_t off = (int8_t)fetch(c);
        hu_push_return(c, (uint16_t)(c->pc - 1));
        c->pc = (uint16_t)(c->pc + off);
        break;
    }
    case 0x60: c->pc = hu_rts(c); break;
    case 0x40: c->pc = hu_rti(c); break;
    case 0x00: c->pc = hu_brk(c, (uint16_t)(c->pc + 1)); break;

    case 0x10: extra += branch(c, !(c->p & HU_FN)); break;
    case 0x30: extra += branch(c, (c->p & HU_FN) != 0); break;
    case 0x50: extra += branch(c, !(c->p & HU_FV)); break;
    case 0x70: extra += branch(c, (c->p & HU_FV) != 0); break;
    case 0x90: extra += branch(c, !(c->p & HU_FC)); break;
    case 0xB0: extra += branch(c, (c->p & HU_FC) != 0); break;
    case 0xD0: extra += branch(c, !(c->p & HU_FZ)); break;
    case 0xF0: extra += branch(c, (c->p & HU_FZ) != 0); break;
    case 0x80: branch(c, true); break;

    /* --- bit ops on zero page --------------------------------------------- */
    case 0x07: case 0x17: case 0x27: case 0x37: case 0x47: case 0x57: case 0x67: case 0x77:
        hu_rmb(c, hu_zp(fetch(c)), op >> 4); break;
    case 0x87: case 0x97: case 0xA7: case 0xB7: case 0xC7: case 0xD7: case 0xE7: case 0xF7:
        hu_smb(c, hu_zp(fetch(c)), (op >> 4) - 8); break;
    case 0x0F: case 0x1F: case 0x2F: case 0x3F: case 0x4F: case 0x5F: case 0x6F: case 0x7F:
        v = hu_rd(c, hu_zp(fetch(c))); extra += branch(c, !(v & (1u << (op >> 4)))); break;
    case 0x8F: case 0x9F: case 0xAF: case 0xBF: case 0xCF: case 0xDF: case 0xEF: case 0xFF:
        v = hu_rd(c, hu_zp(fetch(c))); extra += branch(c, (v & (1u << ((op >> 4) - 8))) != 0); break;

    /* --- HuC6280 specific -------------------------------------------------- */
    case 0x53: hu_tam(c, fetch(c)); break;
    case 0x43: hu_tma(c, fetch(c)); break;
    case 0x03: hu_st(c, 0, fetch(c)); break;
    case 0x13: hu_st(c, 1, fetch(c)); break;
    case 0x23: hu_st(c, 2, fetch(c)); break;
    case 0x54: c->high_speed = false; break;              /* CSL */
    case 0xD4: c->high_speed = true; break;               /* CSH */
    case 0x73: case 0xC3: case 0xD3: case 0xE3: case 0xF3: {
        hu_block_push(c);
        uint16_t src = fetch16(c), dst = fetch16(c), len = fetch16(c);
        extra += hu_block_run(c, op, src, dst, len);
        break;
    }

    default:  /* undefined opcodes and NOP */
        break;
    }

    int n = hu_base_cycles[op] + extra;
    c->cycles += (uint64_t)n;
    return n;
}
