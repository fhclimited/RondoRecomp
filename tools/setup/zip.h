/* Minimal zip reader for importing the player's disc image (stored + Deflate, Zip64). */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct ZipEntry {
    char name[512];         /* path inside the archive, '/' separated */
    uint64_t comp_size;
    uint64_t size;
    uint64_t local_offset;
    uint32_t crc;
    uint16_t method;        /* 0 stored, 8 deflate */
    uint16_t flags;
} ZipEntry;

typedef struct Zip {
    FILE *f;
    ZipEntry *entries;
    int n;
} Zip;

typedef void (*ZipProgress)(void *ctx, uint64_t done, uint64_t total);

bool zip_open(Zip *z, const char *path, char *err, int err_len);
void zip_close(Zip *z);
/* Case-insensitive lookup; '\\' and '/' are treated alike. */
const ZipEntry *zip_find(const Zip *z, const char *name);
/* Decompress an entry to a file (CRC checked). */
bool zip_extract(const Zip *z, const ZipEntry *e, const char *dest, ZipProgress cb, void *ctx,
                 char *err, int err_len);
/* Decompress a small entry into a NUL-terminated heap buffer (free() it). */
char *zip_read_text(const Zip *z, const ZipEntry *e, size_t max, char *err, int err_len);
