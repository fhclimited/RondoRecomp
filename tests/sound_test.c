/* Sound tests.
 *
 *   sound_test               LFO: channel 1 modulates channel 0's pitch between two rates
 *   sound_test <game.cue>    also: AD_CPLAY streaming from the CD produces exactly the same
 *                            output as AD_PLAY of the same data from ADPCM RAM
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pce/pce.h"

#define MASTER_PER_SAMPLE (21477270.0 / SND_RATE)

/* render in pieces smaller than the output ring */
static int render(Sound *s, uint64_t *t, int16_t *out, int frames)
{
    memset(out, 0, (size_t)frames * 4);
    int done = 0;
    while (done < frames) {
        int n = frames - done < 4096 ? frames - done : 4096;
        double target = (double)(done + n) * MASTER_PER_SAMPLE;
        uint64_t start = *t - (uint64_t)((double)done * MASTER_PER_SAMPLE);
        sound_advance(s, start + (uint64_t)target + 1);
        done += sound_take(s, out + done * 2, n);
        *t = start + (uint64_t)target + 1;
        if (!n) break;
    }
    return done;
}

static int crossings(const int16_t *pcm, int frames)
{
    int n = 0;
    for (int i = 1; i < frames; i++) n += (pcm[i * 2] >= 0) != (pcm[(i - 1) * 2] >= 0);
    return n;
}

static int test_lfo(void)
{
    static Sound s;
    static uint8_t ram[0x10000];
    sound_reset(&s, ram, NULL);
    uint64_t t = 0;
    sound_psg_write(&s, t, 1, 0xFF);                      /* global balance */
    for (int ch = 0; ch < 2; ch++) {
        sound_psg_write(&s, t, 0, (uint8_t)ch);
        sound_psg_write(&s, t, 4, 0x00);                  /* off: write the waveform */
        for (int i = 0; i < 32; i++) sound_psg_write(&s, t, 6, i < 16 ? 0x1F : 0x00);
        sound_psg_write(&s, t, 2, ch == 0 ? 0x00 : 0x00);
        sound_psg_write(&s, t, 3, ch == 0 ? 0x02 : 0x01); /* ch0 period $200, ch1 $100 */
        sound_psg_write(&s, t, 5, 0xFF);
        sound_psg_write(&s, t, 4, 0x9F);                  /* on, full volume */
    }
    sound_psg_write(&s, t, 8, 0xFF);                      /* LFO clocked 255x slower than ch1 */
    sound_psg_write(&s, t, 9, 0x02);                      /* x16: period $200 +/- 240 */

    static int16_t pcm[SND_RATE * 2];
    int frames = render(&s, &t, pcm, SND_RATE / 2);      /* 0.5 s */
    /* LFO cycle ~0.58 s (high half, then low half): compare 0.1 s windows inside each half */
    int a = crossings(pcm + 2 * (int)(0.05 * SND_RATE), (int)(0.1 * SND_RATE));
    int b = crossings(pcm + 2 * (int)(0.38 * SND_RATE), (int)(0.1 * SND_RATE));
    int lo = a < b ? a : b, hi = a < b ? b : a;
    printf("LFO: %d frames, zero crossings per 0.1 s: %d and %d\n", frames, a, b);
    if (lo == 0 || hi < 2 * lo) { printf("FAIL: pitch does not alternate\n"); return 1; }

    /* LFO off: channel 0 alone at a steady pitch, channel 1 audible again */
    sound_psg_write(&s, t, 9, 0x80);
    render(&s, &t, pcm, (int)(0.2 * SND_RATE));
    int c = crossings(pcm, (int)(0.1 * SND_RATE)), d = crossings(pcm + 2 * (int)(0.1 * SND_RATE), (int)(0.1 * SND_RATE));
    printf("LFO off: zero crossings %d and %d\n", c, d);
    if (abs(c - d) > c / 10 + 2) { printf("FAIL: pitch not steady with the LFO off\n"); return 1; }
    return 0;
}

static int test_stream(const char *cue)
{
    static Pce p;
    char err[256];
    if (!pce_init(&p, cue, err, sizeof err) || !pce_boot(&p, err, sizeof err)) { printf("%s\n", err); return 1; }
    int lba = p.cd_base[0] + 237;                         /* the ADPCM block the game loads at boot */
    if (!disc_read_data(&p.disc, lba, 32, p.adpcm)) { printf("cannot read ADPCM sectors\n"); return 1; }

    static int16_t a[SND_RATE * 4], b[SND_RATE * 4];
    int frames = SND_RATE * 2;                            /* 2 s at rate 12 (8 kHz) */
    uint64_t t = 0;
    sound_reset(&p.snd, p.adpcm, &p.disc);
    sound_adpcm_play(&p.snd, t, 0, 0xFFFF, 12, false);
    render(&p.snd, &t, a, frames);
    t = 0;
    sound_reset(&p.snd, p.adpcm, &p.disc);
    sound_adpcm_stream(&p.snd, t, lba, 0xFFFF, 12);
    render(&p.snd, &t, b, frames);
    long diff = 0, energy = 0;
    for (int i = 0; i < frames * 2; i++) { diff += a[i] != b[i]; energy += abs(a[i]); }
    printf("ADPCM stream vs RAM: %d frames, %ld differing samples, mean level %ld\n", frames, diff, energy / (frames * 2));
    if (diff || !energy) { printf("FAIL\n"); return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    int fail = test_lfo();
    if (argc > 1) fail |= test_stream(argv[1]);
    printf(fail ? "sound tests FAILED\n" : "sound tests passed\n");
    return fail;
}
