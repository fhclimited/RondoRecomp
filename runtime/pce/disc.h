/* CD image access (CUE sheet + ISO/BIN/WAV tracks), addressed by absolute LBA. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DISC_MAX_TRACKS 99

typedef struct DiscTrack {
    int number;
    bool data;
    char path[1024];
    long data_offset;     /* byte offset of the file's first sector */
    int sector_size;      /* 2048 or 2352 */
    int user_offset;      /* 16 for MODE1/2352 */
    int file_index;       /* INDEX 01 position in the file, sectors */
    int sectors;
    int lba;              /* absolute LBA of INDEX 01 */
} DiscTrack;

typedef struct Disc {
    DiscTrack tracks[DISC_MAX_TRACKS];
    int n_tracks;
} Disc;

bool disc_open(Disc *disc, const char *cue_path, char *err, int err_len);
const DiscTrack *disc_track(const Disc *disc, int number);
const DiscTrack *disc_track_at(const Disc *disc, int lba);
/* Read `count` 2048-byte data sectors starting at absolute `lba`. */
bool disc_read_data(const Disc *disc, int lba, int count, uint8_t *out);
/* Read `count` 2352-byte audio sectors (16-bit stereo PCM) starting at absolute `lba`. */
bool disc_read_audio(const Disc *disc, int lba, int count, uint8_t *out);
