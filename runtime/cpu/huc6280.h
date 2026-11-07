/* HuC6280 CPU interpreter.
 *
 * Executes one instruction per hu_step() against a 21-bit physical bus.
 * Logical addresses are translated through MPR0-7 (8 KiB pages). Interrupt
 * lines are raised by the system through irq_pending; masking via the
 * interrupt controller's disable register is the system's job.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    HU_FLAG_C = 0x01,
    HU_FLAG_Z = 0x02,
    HU_FLAG_I = 0x04,
    HU_FLAG_D = 0x08,
    HU_FLAG_B = 0x10,
    HU_FLAG_T = 0x20,
    HU_FLAG_V = 0x40,
    HU_FLAG_N = 0x80,
};

/* interrupt lines, in priority order (highest first: TIMER, IRQ1, IRQ2) */
enum {
    HU_IRQ2 = 0x01,   /* CD-ROM, external; shares vector with BRK */
    HU_IRQ1 = 0x02,   /* VDC */
    HU_TIRQ = 0x04,   /* internal timer */
};

enum {
    HU_VEC_IRQ2 = 0xFFF6,
    HU_VEC_IRQ1 = 0xFFF8,
    HU_VEC_TIMER = 0xFFFA,
    HU_VEC_NMI = 0xFFFC,
    HU_VEC_RESET = 0xFFFE,
};

typedef struct HuBus {
    void *ctx;
    uint8_t (*read)(void *ctx, uint32_t phys);
    void (*write)(void *ctx, uint32_t phys, uint8_t value);
} HuBus;

typedef struct HuC6280 {
    uint16_t pc;
    uint8_t a, x, y, s, p;
    uint8_t mpr[8];
    uint8_t mpr_buffer;     /* last value moved by TAM/TMA */
    uint8_t irq_pending;    /* HU_IRQ* lines asserted and not disabled */
    bool high_speed;        /* CSH: 7.16 MHz, CSL: 1.79 MHz */
    uint64_t cycles;
    HuBus bus;
} HuC6280;

void hu_init(HuC6280 *cpu, HuBus bus);
void hu_reset(HuC6280 *cpu);

/* Execute one instruction (or take one pending interrupt). Returns CPU cycles used. */
int hu_step(HuC6280 *cpu);

static inline uint32_t hu_phys(const HuC6280 *cpu, uint16_t addr)
{
    return ((uint32_t)cpu->mpr[addr >> 13] << 13) | (addr & 0x1FFFu);
}
