/* Backup RAM (save data): the System Card's 2 KiB file store, reimplemented.
 *
 * Layout, as the BIOS sees it with bank $F7 mapped at $8000:
 *   +0  "HUBM"
 *   +4  pointer to the end of backup RAM   ($8800 for 2 KiB)
 *   +6  pointer to the first unused byte   ($8010 when empty)
 *   +16 files, back to back:
 *        +0 size of the whole entry (header + data), +2 checksum,
 *        +4 name (12 bytes), +16 data
 * The checksum makes the sum of a file's data bytes and the checksum word zero.
 */
#include <string.h>

#include "pce.h"

#define BRAM_SIZE 0x800
#define BASE 0x8000
#define HDR 16

static uint16_t rd16(const uint8_t *b, int o) { return (uint16_t)(b[o] | b[o + 1] << 8); }
static void wr16(uint8_t *b, int o, uint16_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); }

bool bram_formatted(const Pce *p)
{
    return memcmp(p->bram, "HUBM", 4) == 0;
}

void bram_format(Pce *p)
{
    memset(p->bram, 0, BRAM_SIZE);
    memcpy(p->bram, "HUBM", 4);
    wr16(p->bram, 4, BASE + BRAM_SIZE);
    wr16(p->bram, 6, BASE + HDR);
    p->bram_dirty = true;
}

static int used_end(const Pce *p) { return rd16(p->bram, 6) - BASE; }

/* offset of the entry whose name matches, or -1 */
static int find(const Pce *p, const uint8_t name[12])
{
    int off = HDR, end = used_end(p);
    while (off + HDR <= end) {
        int size = rd16(p->bram, off);
        if (size < HDR || off + size > end) break;
        if (!memcmp(p->bram + off + 4, name, 12)) return off;
        off += size;
    }
    return -1;
}

static uint16_t checksum(const uint8_t *data, int n)
{
    unsigned sum = 0;
    for (int i = 0; i < n; i++) sum += data[i];
    return (uint16_t)(0x10000u - (sum & 0xFFFF));
}

int bram_free(const Pce *p)
{
    int free_bytes = (rd16(p->bram, 4) - rd16(p->bram, 6)) - 18;
    return free_bytes < 0 ? 0 : free_bytes;
}

int bram_read(Pce *p, const uint8_t name[12], uint8_t *out, int len, int offset, int *done)
{
    *done = 0;
    if (!bram_formatted(p)) return 0xFF;
    int e = find(p, name);
    if (e < 0) return 1;
    int size = rd16(p->bram, e) - HDR;
    if (offset >= size) return 2;
    const uint8_t *data = p->bram + e + HDR;
    if (checksum(data, size) != rd16(p->bram, e + 2)) return 2;
    int n = len < size - offset ? len : size - offset;
    memcpy(out, data + offset, (size_t)n);
    *done = n;
    return 0;
}

int bram_delete(Pce *p, const uint8_t name[12])
{
    if (!bram_formatted(p)) return 0xFF;
    int e = find(p, name);
    if (e < 0) return 1;
    int size = rd16(p->bram, e), end = used_end(p);
    memmove(p->bram + e, p->bram + e + size, (size_t)(end - e - size));
    memset(p->bram + end - size, 0, (size_t)size);
    wr16(p->bram, 6, (uint16_t)(BASE + end - size));
    p->bram_dirty = true;
    return 0;
}

int bram_write(Pce *p, const uint8_t name[12], const uint8_t *in, int len, int offset)
{
    if (!bram_formatted(p)) return 0xFF;
    int e = find(p, name);
    if (e < 0) {                                     /* create the file */
        int size = HDR + offset + len, end = used_end(p);
        if (end + size + 2 > rd16(p->bram, 4) - BASE) return 4;
        e = end;
        memset(p->bram + e, 0, (size_t)size);
        wr16(p->bram, e, (uint16_t)size);
        memcpy(p->bram + e + 4, name, 12);
        wr16(p->bram, 6, (uint16_t)(BASE + end + size));
    }
    int size = rd16(p->bram, e) - HDR;
    if (offset + len > size) return 2;
    uint8_t *data = p->bram + e + HDR;
    memcpy(data + offset, in, (size_t)len);
    wr16(p->bram, e + 2, checksum(data, size));
    p->bram_dirty = true;
    return 0;
}

int bram_file_name(const Pce *p, int index, uint8_t name[12])
{
    if (!bram_formatted(p)) return 0xFF;
    int off = HDR, end = used_end(p), i = 0;
    while (off + HDR <= end) {
        int size = rd16(p->bram, off);
        if (size < HDR || off + size > end) break;
        if (i++ == index) { memcpy(name, p->bram + off + 4, 12); return 0; }
        off += size;
    }
    return 1;
}
