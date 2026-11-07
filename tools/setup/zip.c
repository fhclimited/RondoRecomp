/* See zip.h. */
#include "zip.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#ifdef _WIN32
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko
#define ftell64 ftello
#endif

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32; }

static bool read_at(FILE *f, uint64_t off, void *buf, size_t n)
{
    return fseek64(f, (int64_t)off, SEEK_SET) == 0 && fread(buf, 1, n, f) == n;
}

bool zip_open(Zip *z, const char *path, char *err, int err_len)
{
    memset(z, 0, sizeof *z);
    z->f = fopen(path, "rb");
    if (!z->f) { snprintf(err, (size_t)err_len, "cannot open %s", path); return false; }
    fseek64(z->f, 0, SEEK_END);
    uint64_t size = (uint64_t)ftell64(z->f);

    /* end of central directory: last 22 bytes + up to 64 KiB comment */
    size_t tail = size < 65557 ? (size_t)size : 65557;
    uint8_t *buf = malloc(tail);
    if (!buf || !read_at(z->f, size - tail, buf, tail)) {
        free(buf);
        snprintf(err, (size_t)err_len, "cannot read %s", path);
        zip_close(z);
        return false;
    }
    long eocd = -1;
    for (long i = (long)tail - 22; i >= 0; i--)
        if (le32(buf + i) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) {
        free(buf);
        snprintf(err, (size_t)err_len, "%s is not a zip archive", path);
        zip_close(z);
        return false;
    }
    uint64_t count = le16(buf + eocd + 10);
    uint64_t cd_size = le32(buf + eocd + 12);
    uint64_t cd_off = le32(buf + eocd + 16);
    /* Zip64: locator sits right before the EOCD record */
    if ((count == 0xFFFF || cd_off == 0xFFFFFFFF || cd_size == 0xFFFFFFFF) && eocd >= 20 &&
        le32(buf + eocd - 20) == 0x07064b50) {
        uint64_t rec = le64(buf + eocd - 20 + 8);
        uint8_t z64[56];
        if (read_at(z->f, rec, z64, sizeof z64) && le32(z64) == 0x06064b50) {
            count = le64(z64 + 32);
            cd_size = le64(z64 + 40);
            cd_off = le64(z64 + 48);
        }
    }
    free(buf);

    uint8_t *cd = malloc(cd_size ? cd_size : 1);
    z->entries = calloc(count ? count : 1, sizeof *z->entries);
    if (!cd || !z->entries || !read_at(z->f, cd_off, cd, cd_size)) {
        free(cd);
        snprintf(err, (size_t)err_len, "cannot read the zip directory");
        zip_close(z);
        return false;
    }
    uint64_t p = 0;
    for (uint64_t i = 0; i < count && p + 46 <= cd_size; i++) {
        const uint8_t *h = cd + p;
        if (le32(h) != 0x02014b50) break;
        ZipEntry *e = &z->entries[z->n];
        e->flags = le16(h + 8);
        e->method = le16(h + 10);
        e->crc = le32(h + 16);
        e->comp_size = le32(h + 20);
        e->size = le32(h + 24);
        uint16_t nlen = le16(h + 28), xlen = le16(h + 30), clen = le16(h + 32);
        e->local_offset = le32(h + 42);
        size_t copy = nlen < sizeof e->name - 1 ? nlen : sizeof e->name - 1;
        memcpy(e->name, h + 46, copy);
        e->name[copy] = 0;
        /* Zip64 extra field: values present only for fields set to 0xFFFFFFFF, in this order */
        const uint8_t *x = h + 46 + nlen, *xend = x + xlen;
        while (x + 4 <= xend) {
            uint16_t id = le16(x), len = le16(x + 2);
            if (id == 0x0001) {
                const uint8_t *v = x + 4;
                if (e->size == 0xFFFFFFFF) { e->size = le64(v); v += 8; }
                if (e->comp_size == 0xFFFFFFFF) { e->comp_size = le64(v); v += 8; }
                if (e->local_offset == 0xFFFFFFFF) { e->local_offset = le64(v); }
            }
            x += 4 + len;
        }
        z->n++;
        p += 46u + nlen + xlen + clen;
    }
    free(cd);
    return true;
}

void zip_close(Zip *z)
{
    if (z->f) fclose(z->f);
    free(z->entries);
    memset(z, 0, sizeof *z);
}

static bool name_eq(const char *a, const char *b)
{
    for (;; a++, b++) {
        char ca = *a == '\\' ? '/' : (char)tolower((unsigned char)*a);
        char cb = *b == '\\' ? '/' : (char)tolower((unsigned char)*b);
        if (ca != cb) return false;
        if (!ca) return true;
    }
}

const ZipEntry *zip_find(const Zip *z, const char *name)
{
    for (int i = 0; i < z->n; i++)
        if (name_eq(z->entries[i].name, name)) return &z->entries[i];
    return NULL;
}

/* Streams an entry's data through `sink`. */
typedef bool (*Sink)(void *ctx, const uint8_t *data, size_t n);

static bool stream(const Zip *z, const ZipEntry *e, Sink sink, void *sctx, ZipProgress cb, void *cctx,
                   char *err, int err_len)
{
    if (e->flags & 1) { snprintf(err, (size_t)err_len, "%s is encrypted", e->name); return false; }
    if (e->method != 0 && e->method != 8) {
        snprintf(err, (size_t)err_len, "%s uses compression method %u; re-create the zip with standard Deflate",
                 e->name, e->method);
        return false;
    }
    uint8_t lh[30];
    if (!read_at(z->f, e->local_offset, lh, sizeof lh) || le32(lh) != 0x04034b50) {
        snprintf(err, (size_t)err_len, "%s: bad local header", e->name);
        return false;
    }
    uint64_t data = e->local_offset + 30 + le16(lh + 26) + le16(lh + 28);
    if (fseek64(z->f, (int64_t)data, SEEK_SET) != 0) { snprintf(err, (size_t)err_len, "seek failed"); return false; }

    enum { CHUNK = 1 << 18 };
    uint8_t *in = malloc(CHUNK), *out = malloc(CHUNK);
    if (!in || !out) { free(in); free(out); snprintf(err, (size_t)err_len, "out of memory"); return false; }
    uLong crc = crc32(0, Z_NULL, 0);
    uint64_t left = e->comp_size, produced = 0;
    bool ok = true;
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (e->method == 8 && inflateInit2(&zs, -MAX_WBITS) != Z_OK) ok = false;

    int zr = Z_OK;
    while (ok && left > 0 && zr != Z_STREAM_END) {
        size_t n = left < CHUNK ? (size_t)left : CHUNK;
        if (fread(in, 1, n, z->f) != n) { ok = false; break; }
        left -= n;
        if (e->method == 0) {
            crc = crc32(crc, in, (uInt)n);
            produced += n;
            ok = sink(sctx, in, n);
        } else {
            zs.next_in = in;
            zs.avail_in = (uInt)n;
            do {
                zs.next_out = out;
                zs.avail_out = CHUNK;
                zr = inflate(&zs, Z_NO_FLUSH);
                if (zr != Z_OK && zr != Z_STREAM_END && zr != Z_BUF_ERROR) { ok = false; break; }
                size_t got = CHUNK - zs.avail_out;
                crc = crc32(crc, out, (uInt)got);
                produced += got;
                if (got && !sink(sctx, out, got)) { ok = false; break; }
            } while (zs.avail_out == 0 && zr != Z_STREAM_END);
        }
        if (cb) cb(cctx, e->comp_size - left, e->comp_size);
    }
    if (e->method == 8) inflateEnd(&zs);
    free(in);
    free(out);
    if (!ok) { snprintf(err, (size_t)err_len, "%s: decompression or write failed", e->name); return false; }
    if (produced != e->size || (uint32_t)crc != e->crc) {
        snprintf(err, (size_t)err_len, "%s is damaged (size or checksum mismatch)", e->name);
        return false;
    }
    return true;
}

static bool sink_file(void *ctx, const uint8_t *d, size_t n) { return fwrite(d, 1, n, (FILE *)ctx) == n; }

bool zip_extract(const Zip *z, const ZipEntry *e, const char *dest, ZipProgress cb, void *ctx,
                 char *err, int err_len)
{
    FILE *f = fopen(dest, "wb");
    if (!f) { snprintf(err, (size_t)err_len, "cannot create %s", dest); return false; }
    bool ok = stream(z, e, sink_file, f, cb, ctx, err, err_len);
    if (fclose(f) != 0 && ok) { snprintf(err, (size_t)err_len, "cannot write %s (disk full?)", dest); ok = false; }
    if (!ok) remove(dest);
    return ok;
}

typedef struct { char *buf; size_t len, max; } TextSink;

static bool sink_text(void *ctx, const uint8_t *d, size_t n)
{
    TextSink *t = ctx;
    if (t->len + n > t->max) return false;
    memcpy(t->buf + t->len, d, n);
    t->len += n;
    return true;
}

char *zip_read_text(const Zip *z, const ZipEntry *e, size_t max, char *err, int err_len)
{
    if (e->size > max) { snprintf(err, (size_t)err_len, "%s is too large", e->name); return NULL; }
    TextSink t = {malloc((size_t)e->size + 1), 0, (size_t)e->size};
    if (!t.buf) return NULL;
    if (!stream(z, e, sink_text, &t, NULL, NULL, err, err_len)) { free(t.buf); return NULL; }
    t.buf[t.len] = 0;
    return t.buf;
}
