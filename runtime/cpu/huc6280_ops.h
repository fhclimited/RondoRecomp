/* HuC6280 instruction semantics, shared by the interpreter and recompiled code.
 *
 * Operands are passed in already decoded, so the interpreter fetches them from
 * memory and the recompiler emits them as constants. Functions returning int
 * return extra cycles beyond hu_base_cycles[] (decimal mode, T mode, taken
 * branches, block transfers).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "huc6280.h"

#if defined(__GNUC__) || defined(__clang__)
#define HU_INLINE static inline __attribute__((always_inline))
#else
#define HU_INLINE static inline
#endif

#define HU_FC HU_FLAG_C
#define HU_FZ HU_FLAG_Z
#define HU_FI HU_FLAG_I
#define HU_FD HU_FLAG_D
#define HU_FB HU_FLAG_B
#define HU_FT HU_FLAG_T
#define HU_FV HU_FLAG_V
#define HU_FN HU_FLAG_N

/* ---- memory ------------------------------------------------------------------ */

HU_INLINE uint8_t hu_rd(HuC6280 *c, uint16_t a)
{
    uint8_t bank = c->mpr[a >> 13];
    const uint8_t *pg = c->page_ptr[bank];
    if (pg) return pg[a & 0x1FFF];
    return c->bus.read(c->bus.ctx, (uint32_t)bank << 13 | (a & 0x1FFFu));
}

HU_INLINE void hu_wr(HuC6280 *c, uint16_t a, uint8_t v)
{
    uint8_t bank = c->mpr[a >> 13];
    if (c->page_ram[bank]) {
        c->page_ptr[bank][a & 0x1FFF] = v;
        c->page_gen[bank]++;
        return;
    }
    c->bus.write(c->bus.ctx, (uint32_t)bank << 13 | (a & 0x1FFFu), v);
}

HU_INLINE uint16_t hu_zp(uint8_t a) { return (uint16_t)(0x2000 | a); }

HU_INLINE void hu_push(HuC6280 *c, uint8_t v) { hu_wr(c, (uint16_t)(0x2100 | c->s), v); c->s--; }
HU_INLINE uint8_t hu_pull(HuC6280 *c) { c->s++; return hu_rd(c, (uint16_t)(0x2100 | c->s)); }

HU_INLINE void hu_set_nz(HuC6280 *c, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~(HU_FN | HU_FZ)) | (v & HU_FN) | (v ? 0 : HU_FZ));
}

HU_INLINE uint16_t hu_zp_ptr(HuC6280 *c, uint8_t z)
{
    uint8_t lo = hu_rd(c, hu_zp(z));
    return (uint16_t)(lo | hu_rd(c, hu_zp((uint8_t)(z + 1))) << 8);
}

/* ---- effective addresses from decoded operands ---------------------------------- */

HU_INLINE uint16_t hu_ea_zpx(HuC6280 *c, uint8_t z) { return hu_zp((uint8_t)(z + c->x)); }
HU_INLINE uint16_t hu_ea_zpy(HuC6280 *c, uint8_t z) { return hu_zp((uint8_t)(z + c->y)); }
HU_INLINE uint16_t hu_ea_abx(HuC6280 *c, uint16_t a) { return (uint16_t)(a + c->x); }
HU_INLINE uint16_t hu_ea_aby(HuC6280 *c, uint16_t a) { return (uint16_t)(a + c->y); }
HU_INLINE uint16_t hu_ea_izx(HuC6280 *c, uint8_t z) { return hu_zp_ptr(c, (uint8_t)(z + c->x)); }
HU_INLINE uint16_t hu_ea_izy(HuC6280 *c, uint8_t z) { return (uint16_t)(hu_zp_ptr(c, z) + c->y); }
HU_INLINE uint16_t hu_ea_izp(HuC6280 *c, uint8_t z) { return hu_zp_ptr(c, z); }

/* ---- ALU ---------------------------------------------------------------------- */

enum { HU_ORA, HU_AND, HU_EOR, HU_ADC, HU_SBC };

HU_INLINE uint8_t hu_adc_val(HuC6280 *c, uint8_t acc, uint8_t v, int *extra)
{
    int carry = c->p & HU_FC;
    uint8_t res;
    if (c->p & HU_FD) {
        int lo = (acc & 0x0F) + (v & 0x0F) + carry;
        int hi = (acc & 0xF0) + (v & 0xF0);
        if (lo > 0x09) { lo += 0x06; hi += 0x10; }
        if (hi > 0x90) hi += 0x60;
        res = (uint8_t)((lo & 0x0F) | (hi & 0xF0));
        c->p = (uint8_t)((c->p & ~HU_FC) | (hi > 0xFF ? HU_FC : 0));
        (*extra)++;
    } else {
        unsigned r = (unsigned)acc + v + (unsigned)carry;
        res = (uint8_t)r;
        c->p = (uint8_t)((c->p & ~(HU_FC | HU_FV)) | (r > 0xFF ? HU_FC : 0) |
                         ((~(acc ^ v) & (acc ^ res) & 0x80) ? HU_FV : 0));
    }
    hu_set_nz(c, res);
    return res;
}

HU_INLINE uint8_t hu_sbc_val(HuC6280 *c, uint8_t acc, uint8_t v, int *extra)
{
    int borrow = (c->p & HU_FC) ? 0 : 1;
    uint8_t res;
    if (c->p & HU_FD) {
        int lo = (acc & 0x0F) - (v & 0x0F) - borrow;
        int hi = (acc & 0xF0) - (v & 0xF0);
        if (lo < 0) { lo -= 0x06; hi -= 0x10; }
        if (hi < 0) hi -= 0x60;
        res = (uint8_t)((lo & 0x0F) | (hi & 0xF0));
        int bin = acc - v - borrow;
        c->p = (uint8_t)((c->p & ~HU_FC) | (bin >= 0 ? HU_FC : 0));
        (*extra)++;
    } else {
        int r = acc - v - borrow;
        res = (uint8_t)r;
        c->p = (uint8_t)((c->p & ~(HU_FC | HU_FV)) | (r >= 0 ? HU_FC : 0) |
                         (((acc ^ v) & (acc ^ res) & 0x80) ? HU_FV : 0));
    }
    hu_set_nz(c, res);
    return res;
}

HU_INLINE uint8_t hu_alu_val(HuC6280 *c, int op, uint8_t acc, uint8_t v, int *extra)
{
    switch (op) {
    case HU_ORA: acc |= v; hu_set_nz(c, acc); return acc;
    case HU_AND: acc &= v; hu_set_nz(c, acc); return acc;
    case HU_EOR: acc ^= v; hu_set_nz(c, acc); return acc;
    case HU_ADC: return hu_adc_val(c, acc, v, extra);
    default:     return hu_sbc_val(c, acc, v, extra);
    }
}

/* ORA/AND/EOR/ADC operate on zero page [X] instead of A when T is set (+3 cycles). */
HU_INLINE int hu_alu(HuC6280 *c, int op, uint8_t v, bool t)
{
    int extra = 0;
    if (t && op != HU_SBC) {
        uint16_t ma = hu_zp(c->x);
        uint8_t r = hu_alu_val(c, op, hu_rd(c, ma), v, &extra);
        hu_wr(c, ma, r);
        extra += 3;
    } else {
        c->a = hu_alu_val(c, op, c->a, v, &extra);
    }
    return extra;
}

HU_INLINE void hu_cmp(HuC6280 *c, uint8_t reg, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~HU_FC) | (reg >= v ? HU_FC : 0));
    hu_set_nz(c, (uint8_t)(reg - v));
}

enum { HU_ASL, HU_ROL, HU_LSR, HU_ROR };

HU_INLINE uint8_t hu_shift(HuC6280 *c, int kind, uint8_t v)
{
    uint8_t carry_in = c->p & HU_FC;
    uint8_t out;
    switch (kind) {
    case HU_ASL: c->p = (uint8_t)((c->p & ~HU_FC) | (v >> 7)); out = (uint8_t)(v << 1); break;
    case HU_ROL: c->p = (uint8_t)((c->p & ~HU_FC) | (v >> 7)); out = (uint8_t)(v << 1 | carry_in); break;
    case HU_LSR: c->p = (uint8_t)((c->p & ~HU_FC) | (v & 1)); out = (uint8_t)(v >> 1); break;
    default:     c->p = (uint8_t)((c->p & ~HU_FC) | (v & 1)); out = (uint8_t)(v >> 1 | carry_in << 7); break;
    }
    hu_set_nz(c, out);
    return out;
}

HU_INLINE void hu_rmw_shift(HuC6280 *c, int kind, uint16_t a) { hu_wr(c, a, hu_shift(c, kind, hu_rd(c, a))); }

HU_INLINE void hu_rmw_inc(HuC6280 *c, uint16_t a, int d)
{
    uint8_t v = (uint8_t)(hu_rd(c, a) + d);
    hu_wr(c, a, v);
    hu_set_nz(c, v);
}

HU_INLINE void hu_bit(HuC6280 *c, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~(HU_FN | HU_FV | HU_FZ)) | (v & (HU_FN | HU_FV)) | ((c->a & v) ? 0 : HU_FZ));
}

HU_INLINE void hu_tst(HuC6280 *c, uint8_t imm, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~(HU_FN | HU_FV | HU_FZ)) | (v & (HU_FN | HU_FV)) | ((imm & v) ? 0 : HU_FZ));
}

/* N and V come from the operand before modification */
HU_INLINE void hu_tsb_trb(HuC6280 *c, uint16_t a, bool set)
{
    uint8_t v = hu_rd(c, a);
    uint8_t r = set ? (uint8_t)(v | c->a) : (uint8_t)(v & ~c->a);
    c->p = (uint8_t)((c->p & ~(HU_FN | HU_FV | HU_FZ)) | (v & (HU_FN | HU_FV)) | ((c->a & v) ? 0 : HU_FZ));
    hu_wr(c, a, r);
}

HU_INLINE void hu_rmb(HuC6280 *c, uint16_t a, int bit) { hu_wr(c, a, (uint8_t)(hu_rd(c, a) & ~(1u << bit))); }
HU_INLINE void hu_smb(HuC6280 *c, uint16_t a, int bit) { hu_wr(c, a, (uint8_t)(hu_rd(c, a) | (1u << bit))); }

/* ---- HuC6280 specifics ---------------------------------------------------------- */

HU_INLINE void hu_tam(HuC6280 *c, uint8_t mask)
{
    for (int i = 0; i < 8; i++)
        if (mask >> i & 1) c->mpr[i] = c->a;
    c->mpr_buffer = c->a;
}

HU_INLINE void hu_tma(HuC6280 *c, uint8_t mask)
{
    if (mask == 0) {
        c->a = c->mpr_buffer;
    } else {                /* several selected registers are ORed together */
        uint8_t r = 0;
        for (int i = 0; i < 8; i++)
            if (mask >> i & 1) r |= c->mpr[i];
        c->a = c->mpr_buffer = r;
    }
}

/* ST0/ST1/ST2: write straight to the VDC ports, whatever MPR0 holds */
HU_INLINE void hu_st(HuC6280 *c, int port, uint8_t v)
{
    static const uint32_t addr[3] = {0x1FE000, 0x1FE002, 0x1FE003};
    c->bus.write(c->bus.ctx, addr[port], v);
}

/* Block transfers: the hardware pushes Y, A, X before reading the operands. */
HU_INLINE void hu_block_push(HuC6280 *c)
{
    hu_push(c, c->y);
    hu_push(c, c->a);
    hu_push(c, c->x);
}

HU_INLINE int hu_block_run(HuC6280 *c, uint8_t op, uint16_t src, uint16_t dst, uint16_t len)
{
    uint32_t n = len ? len : 0x10000u;
    bool alt = false;
    for (uint32_t i = 0; i < n; i++) {
        hu_wr(c, dst, hu_rd(c, src));
        switch (op) {
        case 0x73: src++; dst++; break;                                                 /* TII */
        case 0xC3: src--; dst--; break;                                                 /* TDD */
        case 0xD3: src++; break;                                                        /* TIN */
        case 0xE3: src++; dst = (uint16_t)(dst + (alt ? -1 : 1)); alt = !alt; break;    /* TIA */
        default:   dst++; src = (uint16_t)(src + (alt ? -1 : 1)); alt = !alt; break;    /* TAI */
        }
    }
    c->x = hu_pull(c);
    c->a = hu_pull(c);
    c->y = hu_pull(c);
    return (int)(6 * n);
}

/* ---- control flow --------------------------------------------------------------- */

/* JSR/BSR push the address of their last byte. */
HU_INLINE void hu_push_return(HuC6280 *c, uint16_t last_byte)
{
    hu_push(c, (uint8_t)(last_byte >> 8));
    hu_push(c, (uint8_t)last_byte);
}

HU_INLINE uint16_t hu_rts(HuC6280 *c)
{
    uint8_t lo = hu_pull(c);
    return (uint16_t)((lo | hu_pull(c) << 8) + 1);
}

HU_INLINE uint16_t hu_rti(HuC6280 *c)
{
    c->p = (uint8_t)(hu_pull(c) & ~HU_FB);   /* B only exists on the stack */
    uint8_t lo = hu_pull(c);
    return (uint16_t)(lo | hu_pull(c) << 8);
}

HU_INLINE uint16_t hu_rd16(HuC6280 *c, uint16_t a)
{
    uint8_t lo = hu_rd(c, a);
    return (uint16_t)(lo | hu_rd(c, (uint16_t)(a + 1)) << 8);
}

/* BRK: push the address after its padding byte, then vector like IRQ2. */
HU_INLINE uint16_t hu_brk(HuC6280 *c, uint16_t next)
{
    hu_push(c, (uint8_t)(next >> 8));
    hu_push(c, (uint8_t)next);
    hu_push(c, (uint8_t)(c->p | HU_FB));
    c->p = (uint8_t)((c->p | HU_FI) & ~(HU_FD | HU_FT));
    return hu_rd16(c, HU_VEC_IRQ2);
}
