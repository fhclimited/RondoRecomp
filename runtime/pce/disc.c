/* CUE sheet parsing and sector reads. See disc.h. */
#include "disc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int msf_frames(const char *s)
{
    int m = 0, sec = 0, f = 0;
    sscanf(s, "%d:%d:%d", &m, &sec, &f);
    return (m * 60 + sec) * 75 + f;
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

/* Returns offset of the PCM data chunk, or -1. */
static long wav_data(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return -1; }
    for (;;) {
        uint8_t c[8];
        if (fread(c, 1, 8, f) != 8) { fclose(f); return -1; }
        uint32_t n = (uint32_t)c[4] | (uint32_t)c[5] << 8 | (uint32_t)c[6] << 16 | (uint32_t)c[7] << 24;
        if (!memcmp(c, "data", 4)) {
            long off = ftell(f);
            fclose(f);
            *size = (long)n;
            return off;
        }
        fseek(f, (long)(n + (n & 1)), SEEK_CUR);
    }
}

/* Next token from *p: a quoted string or a run of non-space characters. */
static int token(const char **p, char *out, int max)
{
    const char *s = *p;
    while (*s && isspace((unsigned char)*s)) s++;
    int n = 0;
    if (*s == '"') {
        s++;
        while (*s && *s != '"' && n < max - 1) out[n++] = *s++;
        if (*s == '"') s++;
    } else {
        while (*s && !isspace((unsigned char)*s) && n < max - 1) out[n++] = *s++;
    }
    out[n] = 0;
    *p = s;
    return n;
}

bool disc_open(Disc *disc, const char *cue_path, char *err, int err_len)
{
    memset(disc, 0, sizeof *disc);
    FILE *f = fopen(cue_path, "r");
    if (!f) { snprintf(err, (size_t)err_len, "cannot open %s", cue_path); return false; }

    char base[1024];
    snprintf(base, sizeof base, "%s", cue_path);
    char *slash = strrchr(base, '/');
    char *bslash = strrchr(base, '\\');
    if (bslash > slash) slash = bslash;
    if (slash) slash[1] = 0; else base[0] = 0;

    char cur_file[1024] = "", cur_type[16] = "";
    int file_first[DISC_MAX_TRACKS], file_of[DISC_MAX_TRACKS], pregap[DISC_MAX_TRACKS], idx00[DISC_MAX_TRACKS];
    char files[DISC_MAX_TRACKS][1024], types[DISC_MAX_TRACKS][16];
    int n_files = 0;
    char line[2048];
    DiscTrack *t = NULL;
    while (fgets(line, sizeof line, f)) {
        const char *p = line;
        char cmd[32], a[1024], b[64];
        if (!token(&p, cmd, sizeof cmd)) continue;
        for (char *c = cmd; *c; c++) *c = (char)toupper((unsigned char)*c);
        if (!strcmp(cmd, "FILE")) {
            token(&p, a, sizeof a);
            token(&p, b, sizeof b);
            snprintf(cur_file, sizeof cur_file, "%s%s", base, a);
            snprintf(cur_type, sizeof cur_type, "%s", b);
            snprintf(files[n_files], sizeof files[0], "%s", cur_file);
            snprintf(types[n_files], sizeof types[0], "%s", cur_type);
            file_first[n_files] = disc->n_tracks;
            n_files++;
        } else if (!strcmp(cmd, "TRACK") && n_files) {
            if (disc->n_tracks >= DISC_MAX_TRACKS) break;
            token(&p, a, sizeof a);
            token(&p, b, sizeof b);
            int i = disc->n_tracks++;
            t = &disc->tracks[i];
            t->number = atoi(a);
            snprintf(t->path, sizeof t->path, "%s", cur_file);
            t->data = strcmp(b, "AUDIO") != 0;
            t->sector_size = !strcmp(b, "MODE1/2048") ? 2048 : 2352;
            t->user_offset = !strcmp(b, "MODE1/2352") ? 16 : 0;
            file_of[i] = n_files - 1;
            pregap[i] = 0;
            idx00[i] = -1;
        } else if (!strcmp(cmd, "PREGAP") && t) {
            token(&p, a, sizeof a);
            pregap[t - disc->tracks] = msf_frames(a);
        } else if (!strcmp(cmd, "INDEX") && t) {
            token(&p, a, sizeof a);
            token(&p, b, sizeof b);
            if (atoi(a) == 1) t->file_index = msf_frames(b);
            else if (atoi(a) == 0) idx00[t - disc->tracks] = msf_frames(b);
        }
    }
    fclose(f);
    if (!disc->n_tracks) { snprintf(err, (size_t)err_len, "no tracks in %s", cue_path); return false; }

    int cursor = 0;
    for (int fi = 0; fi < n_files; fi++) {
        long size, off = 0;
        if (!strcmp(types[fi], "WAVE")) {
            off = wav_data(files[fi], &size);
            if (off < 0) { snprintf(err, (size_t)err_len, "bad WAV %s", files[fi]); return false; }
        } else {
            size = file_size(files[fi]);
            if (size < 0) { snprintf(err, (size_t)err_len, "cannot open %s", files[fi]); return false; }
        }
        int first = file_first[fi];
        int end = fi + 1 < n_files ? file_first[fi + 1] : disc->n_tracks;
        int gaps = 0, file_sectors = 0;
        for (int i = first; i < end; i++) {
            DiscTrack *tr = &disc->tracks[i];
            tr->data_offset = off;
            file_sectors = (int)(size / tr->sector_size);
            gaps += pregap[i];
            tr->lba = cursor + gaps + tr->file_index;
            int stop = file_sectors;
            if (i + 1 < end) stop = idx00[i + 1] >= 0 ? idx00[i + 1] : disc->tracks[i + 1].file_index;
            tr->sectors = stop - tr->file_index;
        }
        cursor += gaps + file_sectors;
    }
    return true;
}

const DiscTrack *disc_track(const Disc *disc, int number)
{
    for (int i = 0; i < disc->n_tracks; i++)
        if (disc->tracks[i].number == number) return &disc->tracks[i];
    return NULL;
}

const DiscTrack *disc_track_at(const Disc *disc, int lba)
{
    for (int i = 0; i < disc->n_tracks; i++) {
        const DiscTrack *t = &disc->tracks[i];
        if (lba >= t->lba && lba < t->lba + t->sectors) return t;
    }
    return NULL;
}

static bool read_sectors(const DiscTrack *t, int lba, int count, int bytes, int skip, uint8_t *out)
{
    FILE *f = fopen(t->path, "rb");
    if (!f) return false;
    bool ok = true;
    for (int i = 0; i < count && ok; i++) {
        long pos = t->data_offset + (long)(t->file_index + lba - t->lba + i) * t->sector_size + skip;
        ok = fseek(f, pos, SEEK_SET) == 0 && fread(out + (size_t)i * (size_t)bytes, 1, (size_t)bytes, f) == (size_t)bytes;
    }
    fclose(f);
    return ok;
}

bool disc_read_data(const Disc *disc, int lba, int count, uint8_t *out)
{
    const DiscTrack *t = disc_track_at(disc, lba);
    if (!t || !t->data || lba + count > t->lba + t->sectors) return false;
    return read_sectors(t, lba, count, 2048, t->user_offset, out);
}

bool disc_read_audio(const Disc *disc, int lba, int count, uint8_t *out)
{
    const DiscTrack *t = disc_track_at(disc, lba);
    if (!t || t->data || lba + count > t->lba + t->sectors) return false;
    return read_sectors(t, lba, count, 2352, 0, out);
}
