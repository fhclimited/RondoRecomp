/* PC Engine PSG + ADPCM. See sound.h. */
#include "sound.h"

#include "disc.h"

#include <math.h>
#include <string.h>

#define MASTER_HZ 21477270.0
#define PSG_HZ (MASTER_HZ / 6.0)              /* 3.58 MHz */
#define PSG_PER_SAMPLE (PSG_HZ / SND_RATE)
#define PSG_GAIN 300.0f                        /* per channel, per step of the 5-bit wave */
#define ADPCM_GAIN 6                           /* 12-bit signal -> 16-bit output */

/* ---- PSG ------------------------------------------------------------------------- */

/* Volume steps are 1.5 dB (5-bit channel volume) and 3 dB (4-bit balances); a 0 mutes. */
static float level(int vol, int bal, int global_bal)
{
    if (!vol || !bal || !global_bal) return 0.0f;
    double db = (31 - vol) * 1.5 + (15 - bal) * 3.0 + (15 - global_bal) * 3.0;
    return (float)pow(10.0, -db / 20.0);
}

static void update_amp(Sound *s, PsgChannel *c)
{
    int vol = c->control & 0x1F;
    c->amp_l = level(vol, c->balance >> 4, s->global_balance >> 4);
    c->amp_r = level(vol, c->balance & 15, s->global_balance & 15);
}

void sound_reset(Sound *s, const uint8_t *adpcm_ram, const struct Disc *disc)
{
    memset(s, 0, sizeof *s);
    s->ram = adpcm_ram;
    s->disc = disc;
    s->ad_sec_lba = -1;
    for (int i = 0; i < 6; i++) s->ch[i].lfsr = 1;
    s->ad_volume = 256;
}

void sound_psg_write(Sound *s, uint64_t now, uint8_t reg, uint8_t v)
{
    sound_advance(s, now);
    s->psg_writes++;
    PsgChannel *c = s->select < 6 ? &s->ch[s->select] : NULL;
    switch (reg & 15) {
    case 0: s->select = v & 7; break;
    case 1:
        s->global_balance = v;
        for (int i = 0; i < 6; i++) update_amp(s, &s->ch[i]);
        break;
    case 2: if (c) c->freq = (uint16_t)((c->freq & 0xF00) | v); break;
    case 3: if (c) c->freq = (uint16_t)((c->freq & 0x0FF) | (v & 0x0F) << 8); break;
    case 4:
        if (!c) break;
        if ((v & 0xC0) == 0x40) c->wave_index = 0;       /* off + DDA: reset the wave write index */
        c->control = v;
        update_amp(s, c);
        break;
    case 5: if (c) { c->balance = v; update_amp(s, c); } break;
    case 6:
        if (!c) break;
        if (c->control & 0x40) {
            c->dda = v & 0x1F;
        } else {
            c->wave[c->wave_index] = v & 0x1F;
            c->wave_index = (c->wave_index + 1) & 31;
        }
        break;
    case 7: if (c && s->select >= 4) c->noise = v; break;
    case 8: s->lfo_freq = v; break;
    case 9:
        s->lfo_ctrl = v;
        if (v & 3) s->lfo_used = true;
        if (v & 0x80) s->ch[1].phase = 0;                /* LFO reset */
        break;
    }
}

static float psg_channel(PsgChannel *c, int index, double period_override)
{
    if (!(c->control & 0x80)) return 0.0f;
    int sample;
    if (c->control & 0x40) {
        sample = c->dda;
    } else if (index >= 4 && (c->noise & 0x80)) {
        double period = (double)(((c->noise & 0x1F) ^ 0x1F) + 1) * 64.0;
        c->noise_clock += PSG_PER_SAMPLE;
        while (c->noise_clock >= period) {
            c->noise_clock -= period;
            uint32_t bit = ((c->lfsr >> 0) ^ (c->lfsr >> 1) ^ (c->lfsr >> 11) ^ (c->lfsr >> 12) ^ (c->lfsr >> 17)) & 1;
            c->lfsr = (c->lfsr >> 1) | bit << 17;
        }
        sample = (c->lfsr & 1) ? 0x1F : 0x00;
    } else {
        double period = period_override > 0 ? period_override : (c->freq ? c->freq : 4096);
        if (period < 8) return 0.0f;                     /* above audible range: silent */
        c->phase += PSG_PER_SAMPLE / period;
        if (c->phase >= 32.0) c->phase = fmod(c->phase, 32.0);
        sample = c->wave[(int)c->phase & 31];
    }
    return (float)(sample - 16);
}

/* ---- ADPCM (OKI MSM5205 style, low nibble first) ---------------------------------- */

static const int16_t step_table[49] = {
    16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157,
    173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
    1282, 1411, 1552,
};
static const int8_t index_adjust[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static uint8_t adpcm_byte(Sound *s)
{
    if (!s->ad_stream) return s->ram[s->ad_addr];
    if (s->ad_sec_lba != s->ad_stream_lba) {
        if (!s->disc || !disc_read_data(s->disc, s->ad_stream_lba, 1, s->ad_sec)) memset(s->ad_sec, 0x88, sizeof s->ad_sec);
        s->ad_sec_lba = s->ad_stream_lba;
    }
    return s->ad_sec[s->ad_stream_off];
}

static void adpcm_advance_byte(Sound *s)
{
    s->ad_left--;
    if (!s->ad_stream) { s->ad_addr++; return; }
    if (++s->ad_stream_off == 2048) { s->ad_stream_off = 0; s->ad_stream_lba++; }
}

static int adpcm_next(Sound *s)
{
    uint8_t b = adpcm_byte(s);
    int n = s->ad_high ? b >> 4 : b & 15;
    if (s->ad_high) adpcm_advance_byte(s);
    s->ad_high = !s->ad_high;
    int st = step_table[s->ad_index];
    int diff = st >> 3;
    if (n & 1) diff += st >> 2;
    if (n & 2) diff += st >> 1;
    if (n & 4) diff += st;
    if (n & 8) diff = -diff;
    s->ad_signal += diff;
    if (s->ad_signal > 2047) s->ad_signal = 2047;
    if (s->ad_signal < -2048) s->ad_signal = -2048;
    s->ad_index += index_adjust[n & 7];
    if (s->ad_index < 0) s->ad_index = 0;
    if (s->ad_index > 48) s->ad_index = 48;
    return s->ad_signal;
}

static void adpcm_restart(Sound *s)
{
    s->ad_addr = s->ad_start;
    s->ad_left = s->ad_len;
    s->ad_high = false;
    s->ad_signal = 0;
    s->ad_index = 0;
    s->ad_prev = s->ad_cur = 0;
    s->ad_phase = 0;
}

void sound_adpcm_play(Sound *s, uint64_t now, uint16_t start, uint32_t len, uint8_t rate, bool repeat)
{
    sound_advance(s, now);
    s->ad_stream = false;
    s->ad_start = start;
    s->ad_len = len;
    s->ad_rate = rate & 15;
    s->ad_repeat = repeat;
    adpcm_restart(s);
    s->ad_playing = len > 0;
}

void sound_adpcm_stop(Sound *s, uint64_t now)
{
    sound_advance(s, now);
    s->ad_playing = false;
}

void sound_adpcm_stream(Sound *s, uint64_t now, int lba, uint32_t bytes, uint8_t rate)
{
    sound_advance(s, now);
    s->ad_stream = true;
    s->ad_stream_lba = lba;
    s->ad_stream_off = 0;
    s->ad_sec_lba = -1;
    s->ad_len = bytes;
    s->ad_rate = rate & 15;
    s->ad_repeat = false;
    s->ad_left = bytes;
    s->ad_high = false;
    s->ad_signal = 0;
    s->ad_index = 0;
    s->ad_prev = s->ad_cur = 0;
    s->ad_phase = 0;
    s->ad_playing = bytes > 0;
}

void sound_adpcm_fade(Sound *s, int step)
{
    s->ad_fade_step = step;
    if (!step) s->ad_volume = 256;
}

static int adpcm_sample(Sound *s)
{
    if (!s->ad_playing) return 0;
    double hz = 32000.0 / (16 - s->ad_rate);
    s->ad_phase += hz / SND_RATE;
    while (s->ad_phase >= 1.0) {
        s->ad_phase -= 1.0;
        if (s->ad_left == 0) {
            if (!s->ad_repeat) { s->ad_playing = false; return 0; }
            adpcm_restart(s);
        }
        s->ad_prev = s->ad_cur;
        s->ad_cur = adpcm_next(s);
    }
    return (int)(s->ad_prev + (s->ad_cur - s->ad_prev) * s->ad_phase);
}

/* ---- output ---------------------------------------------------------------------- */

static int16_t clamp16(int v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

void sound_advance(Sound *s, uint64_t now)
{
    if (now <= s->last_master) return;
    s->frac += (double)(now - s->last_master) * SND_RATE / MASTER_HZ;
    s->last_master = now;
    int n = (int)s->frac;
    s->frac -= n;
    for (int i = 0; i < n; i++) {
        float l = 0, r = 0;
        double ch0_period = 0;
        bool lfo = (s->lfo_ctrl & 3) && !(s->lfo_ctrl & 0x80);
        if (lfo) {
            /* LFO: channel 1's waveform, clocked lfo_freq times slower, is added to channel 0's
             * period (x1, x16 or x256); channel 1 is not heard */
            static const int shift[4] = {0, 0, 4, 8};
            PsgChannel *m = &s->ch[1];
            double mp = (double)(m->freq ? m->freq : 4096) * (s->lfo_freq ? s->lfo_freq : 256);
            m->phase += PSG_PER_SAMPLE / mp;
            if (m->phase >= 32.0) m->phase = fmod(m->phase, 32.0);
            int mod = (int)m->wave[(int)m->phase & 31] - 16;
            ch0_period = (double)(s->ch[0].freq ? s->ch[0].freq : 4096) + (double)(mod * (1 << shift[s->lfo_ctrl & 3]));
            if (ch0_period < 1) ch0_period = 1;
        }
        for (int k = 0; k < 6; k++) {
            if (lfo && k == 1) continue;
            PsgChannel *c = &s->ch[k];
            float v = psg_channel(c, k, k == 0 ? ch0_period : 0);
            l += v * c->amp_l;
            r += v * c->amp_r;
        }
        int ad = adpcm_sample(s) * ADPCM_GAIN * s->ad_volume / 256;
        if (l != 0 || r != 0) s->psg_samples++;
        if (s->ad_playing) s->adpcm_samples++;
        if (s->ad_fade_step && (i & 63) == 0) {
            s->ad_volume += s->ad_fade_step;
            if (s->ad_volume <= 0) { s->ad_volume = 0; s->ad_fade_step = 0; }
        }
        uint32_t at = (s->head + s->count) % SND_RING;
        if (s->count == SND_RING) {                  /* nobody is reading: drop the oldest */
            s->head = (s->head + 1) % SND_RING;
            s->count--;
            at = (s->head + s->count) % SND_RING;
        }
        s->ring[at * 2] = clamp16((int)(l * PSG_GAIN) + ad);
        s->ring[at * 2 + 1] = clamp16((int)(r * PSG_GAIN) + ad);
        s->count++;
    }
}

int sound_take(Sound *s, int16_t *out, int n)
{
    int taken = 0;
    while (taken < n && s->count) {
        out[taken * 2] = clamp16(out[taken * 2] + s->ring[s->head * 2]);
        out[taken * 2 + 1] = clamp16(out[taken * 2 + 1] + s->ring[s->head * 2 + 1]);
        s->head = (s->head + 1) % SND_RING;
        s->count--;
        taken++;
    }
    return taken;
}
