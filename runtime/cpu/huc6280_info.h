/* HuC6280 opcode facts shared by the runtime and the code generator. */
#pragma once

#include <stdint.h>

extern const uint8_t hu_op_len[256];        /* instruction length in bytes */
extern const uint8_t hu_op_ends_block[256]; /* control flow, or an instruction that ends a region */

/* FNV-1a 32 of the basic block starting at `off` in an 8 KiB page: the bytes of the
 * instructions up to and including the first one that ends a block (max 64 bytes,
 * stops at the page end). Used to match recorded entry points against bank images. */
uint32_t hu_block_hash(const uint8_t *page, uint32_t off);
