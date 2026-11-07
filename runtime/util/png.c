/* Minimal PNG writer (RGB8, uncompressed deflate blocks). Debug screenshots only. */
#include "png.h"

#include <stdio.h>
#include <stdlib.h>

static uint32_t crc_table[256];

static void crc_init(void)
{
    if (crc_table[1]) return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    c = ~c;
    while (n--) c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t h[8];
    be32(h, len);
    for (int i = 0; i < 4; i++) h[4 + i] = (uint8_t)type[i];
    fwrite(h, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t c = crc(0, h + 4, 4);
    c = crc(c, data, len);
    be32(h, c);
    fwrite(h, 1, 4, f);
}

bool png_write_argb(const char *path, const uint32_t *px, int w, int h, int stride)
{
    crc_init();
    size_t raw_len = (size_t)h * (size_t)(w * 3 + 1);
    uint8_t *raw = malloc(raw_len);
    if (!raw) return false;
    uint8_t *r = raw;
    for (int y = 0; y < h; y++) {
        *r++ = 0;
        for (int x = 0; x < w; x++) {
            uint32_t c = px[y * stride + x];
            *r++ = (uint8_t)(c >> 16);
            *r++ = (uint8_t)(c >> 8);
            *r++ = (uint8_t)c;
        }
    }
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(z_len);
    if (!z) { free(raw); return false; }
    uint8_t *o = z;
    *o++ = 0x78; *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_len; off += 65535) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        *o++ = off + n == raw_len ? 1 : 0;
        *o++ = (uint8_t)n; *o++ = (uint8_t)(n >> 8);
        *o++ = (uint8_t)~n; *o++ = (uint8_t)(~n >> 8);
        for (size_t i = 0; i < n; i++) {
            uint8_t v = raw[off + i];
            *o++ = v;
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
    }
    be32(o, b << 16 | a);
    o += 4;

    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return false; }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)(o - z));
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return true;
}
