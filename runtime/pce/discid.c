/* See discid.h. */
#include "discid.h"

#include <string.h>

#include "disc.h"

/* FNV-1a 64 of the boot program sector (first data track, record 2) of known releases */
static const uint64_t known_boot[] = {
    0xDB4D9415EC501573ull,   /* Castlevania: Rondo of Blood (English) */
};

static uint64_t fnv1a(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    while (n--) { h ^= *p++; h *= 0x100000001b3ull; }
    return h;
}

DiscKind discid_identify(const char *cue_path, uint64_t *fingerprint)
{
    static Disc disc;
    char err[256];
    uint8_t sec[2048];
    if (fingerprint) *fingerprint = 0;
    if (!disc_open(&disc, cue_path, err, sizeof err)) return DISC_INVALID;
    const DiscTrack *t = NULL;
    for (int i = 0; i < disc.n_tracks; i++)
        if (disc.tracks[i].data) { t = &disc.tracks[i]; break; }
    if (!t || !disc_read_data(&disc, t->lba + 1, 1, sec) || memcmp(sec + 0x20, "PC Engine CD-ROM SYSTEM", 23))
        return DISC_INVALID;
    if (!disc_read_data(&disc, t->lba + 2, 1, sec)) return DISC_INVALID;
    uint64_t h = fnv1a(sec, sizeof sec);
    if (fingerprint) *fingerprint = h;
    for (size_t i = 0; i < sizeof known_boot / sizeof known_boot[0]; i++)
        if (h == known_boot[i]) return DISC_RONDO;
    return DISC_PCE_UNKNOWN;
}
