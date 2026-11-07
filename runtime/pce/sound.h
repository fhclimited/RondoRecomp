/* PC Engine sound: the HuC6280 PSG (6 wavetable channels, DDA, noise) and the CD-ROM
 * ADPCM voice playback (MSM5205-style 4-bit ADPCM from 64 KiB of ADPCM RAM).
 *
 * Rendering catches up to the master clock on every register write, so changes land on
 * the right output sample; finished samples wait in a ring buffer until the frontend asks
 * for them. CD audio is mixed in separately (pce_audio_render). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct Disc;

#define SND_RATE 44100
#define SND_RING 16384                 /* stereo frames */

typedef struct PsgChannel {
    uint16_t freq;                     /* 12-bit period; 0 = 4096 */
    uint8_t control;                   /* bit 7 on, bit 6 DDA, bits 0-4 volume */
    uint8_t balance;                   /* left << 4 | right */
    uint8_t wave[32];                  /* 5-bit samples */
    uint8_t wave_index;                /* write index */
    uint8_t dda;
    uint8_t noise;                     /* channels 4/5: bit 7 on, bits 0-4 frequency */
    double phase;                      /* waveform position, 0..32 */
    double noise_clock;
    uint32_t lfsr;
    float amp_l, amp_r;
} PsgChannel;

typedef struct Sound {
    PsgChannel ch[6];
    uint8_t select, global_balance, lfo_freq, lfo_ctrl;

    /* ADPCM */
    const uint8_t *ram;                /* 64 KiB ADPCM RAM (owned by the system) */
    const struct Disc *disc;           /* for AD_CPLAY streaming (owned by the system) */
    bool ad_stream;                    /* data comes from the CD, not ADPCM RAM */
    int ad_stream_lba;                 /* next sector to read */
    uint16_t ad_stream_off;            /* byte offset in the cached sector */
    int ad_sec_lba;                    /* sector in ad_sec, -1 = none */
    uint8_t ad_sec[2048];
    bool ad_playing, ad_repeat;
    uint16_t ad_start, ad_addr;
    uint32_t ad_len, ad_left;          /* bytes */
    uint8_t ad_rate;
    bool ad_high;                      /* next nibble is the high one */
    int ad_signal, ad_index;
    int ad_prev, ad_cur;               /* last two decoded samples (12-bit) */
    double ad_phase;
    int ad_volume, ad_fade_step;       /* 0..256 */

    /* diagnostics */
    bool lfo_used;
    uint64_t psg_writes, psg_samples, adpcm_samples;   /* writes; samples with PSG / ADPCM output */

    /* output clock and buffer */
    uint64_t last_master;
    double frac;
    int16_t ring[SND_RING * 2];
    uint32_t head, count;
} Sound;

void sound_reset(Sound *s, const uint8_t *adpcm_ram, const struct Disc *disc);
/* Render output up to master clock `now`. */
void sound_advance(Sound *s, uint64_t now);
/* PSG register write at master clock `now` (reg = address & 15). */
void sound_psg_write(Sound *s, uint64_t now, uint8_t reg, uint8_t v);
/* ADPCM playback from ADPCM RAM (AD_PLAY): rate 0..15 = 32 kHz / (16 - rate). */
void sound_adpcm_play(Sound *s, uint64_t now, uint16_t start, uint32_t len, uint8_t rate, bool repeat);
void sound_adpcm_stop(Sound *s, uint64_t now);
/* AD_CPLAY: stream `bytes` of ADPCM straight from the CD starting at absolute `lba`. */
void sound_adpcm_stream(Sound *s, uint64_t now, int lba, uint32_t bytes, uint8_t rate);
/* Fade the ADPCM volume out (step per output block; 0 = cancel and restore). */
void sound_adpcm_fade(Sound *s, int step);
/* Take up to n frames of mixed PSG + ADPCM output (adds to out); returns frames taken. */
int sound_take(Sound *s, int16_t *out, int n);
