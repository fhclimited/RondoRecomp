/* Interface between the runtime and the C code that Setup generates from the
 * player's disc. The generated translation unit defines these symbols; developer
 * builds link recomp_none.c instead (interpreter only). */
#pragma once

#include <stdint.h>

struct Pce;
typedef void (*RecompFn)(struct Pce *p);

typedef struct RecompEntry {
    uint8_t bank;           /* physical bank the code was found in */
    uint16_t addr;          /* logical entry address */
    uint32_t code_hash;     /* hash of the code bytes, checked before use (overlays) */
    RecompFn fn;
} RecompEntry;

extern const RecompEntry recomp_table[];
extern const int recomp_table_len;
extern const char recomp_build_id[];    /* generator version and disc fingerprint */
