/* Headless runner: boots the disc on the interpreter, saves screenshots, prints progress.
 *
 * usage: rondo_headless <game.cue> [--frames N] [--shot-every N] [--out DIR]
 *                       [--log-bios] [--press FRAME:BUTTONS[:LEN]]...
 *   BUTTONS: comma list of i,ii,select,run,up,down,left,right
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pce/pce.h"
#include "pce/state.h"
#include "../frontend/game.h"
#include "recomp/recomp.h"
#include "../recomp/capture_script.h"
#include "util/png.h"

typedef struct { long frame; int len; uint8_t mask; } Press;

/* PC histogram (bank:addr), excluding the idle loop, for --profile */
static uint32_t *prof;
/* --watch BANK:ADDR: print A/X/Y whenever that instruction runs with different X than last time */
static int n_watch;
static struct { uint8_t bank; uint16_t addr; int last_x; } watch[8];

/* --trace FRAME:COUNT: log COUNT instructions from the start of FRAME to work/trace.txt */
static long trace_frame = -1, trace_left;
static FILE *trace_f;

static void step_hook(Pce *p, void *ctx)
{
    (void)ctx;
    uint16_t pc = p->cpu.pc;
    uint8_t bank = p->cpu.mpr[pc >> 13];
    if (prof) prof[(uint32_t)bank << 16 | pc]++;
    if (trace_left > 0 && (long)p->frame >= trace_frame) {
        if (!trace_f) trace_f = fopen("work/trace.txt", "w");
        if (trace_f) {
            HuC6280 *c = &p->cpu;
            fprintf(trace_f, "f%llu l%03d %02X:%04X  A=%02X X=%02X Y=%02X S=%02X P=%02X  bytes %02X %02X %02X\n",
                    (unsigned long long)p->frame, p->line, bank, pc, c->a, c->x, c->y, c->s, c->p,
                    pce_read8(p, pc), pce_read8(p, (uint16_t)(pc + 1)), pce_read8(p, (uint16_t)(pc + 2)));
        }
        if (--trace_left == 0 && trace_f) fclose(trace_f);
    }
    for (int i = 0; i < n_watch; i++) {
        if (watch[i].addr == pc && watch[i].bank == bank && watch[i].last_x != p->cpu.x) {
            printf("  [watch %02X:%04X] frame %llu line %d  A=%02X X=%02X Y=%02X\n", bank, pc,
                   (unsigned long long)p->frame, p->line, p->cpu.a, p->cpu.x, p->cpu.y);
            watch[i].last_x = p->cpu.x;
        }
    }
}

static uint8_t parse_buttons(const char *s)
{
    static const struct { const char *n; uint8_t m; } map[] = {
        {"i", PAD_I}, {"ii", PAD_II}, {"select", PAD_SELECT}, {"run", PAD_RUN},
        {"up", PAD_UP}, {"down", PAD_DOWN}, {"left", PAD_LEFT}, {"right", PAD_RIGHT},
    };
    uint8_t m = 0;
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s", s);
    for (char *tok = strtok(tmp, ","); tok; tok = strtok(NULL, ","))
        for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
            if (!strcmp(tok, map[i].n)) m |= map[i].m;
    return m;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <game.cue> [--frames N] [--shot-every N] [--out DIR] [--log-bios] [--press F:BUTTONS[:LEN]]\n", argv[0]);
        return 2;
    }
    long frames = 600, shot_every = 60;
    const char *out = "work/shots";
    bool log_bios = false, log_all = false;
    const char *bios_reset = NULL, *wav_path = NULL, *seeds_path = NULL, *misses_path = NULL;
    int verify = 0;
    bool no_native = false, use_script = false, mute_cd = false;
    Press presses[64];
    int n_press = 0;
    /* investigation helpers: WRAM watch, pokes, save states, WRAM dumps */
    int n_ram = 0, n_poke = 0, n_dump = 0;
    unsigned ram_addr[32];
    struct { unsigned addr, val; } poke[16];
    struct { long frame; char path[256]; } dump[16], save_at[4];
    int n_save_at = 0;
    const char *load_state = NULL;
    GameCheats cheats = {0};
    GameWatch gw;
    bool skip_cine = false;
    long skipped = 0;
    FILE *ram_trace = NULL;   /* --ram-trace FILE: WRAM after every frame */
    long start_frame = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--shot-every") && i + 1 < argc) shot_every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--log-bios")) log_bios = true;
        else if (!strcmp(argv[i], "--log-bios-all")) log_bios = log_all = true;
        else if (!strcmp(argv[i], "--profile")) prof = calloc(1u << 24, sizeof *prof);
        else if (!strcmp(argv[i], "--bios-reset") && i + 1 < argc) bios_reset = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--record-seeds") && i + 1 < argc) seeds_path = argv[++i];
        else if (!strcmp(argv[i], "--misses") && i + 1 < argc) misses_path = argv[++i];
        else if (!strcmp(argv[i], "--verify") && i + 1 < argc) verify = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-native")) no_native = true;
        else if (!strcmp(argv[i], "--script")) use_script = true;
        else if (!strcmp(argv[i], "--mute-cd")) mute_cd = true;
        else if (!strcmp(argv[i], "--ram") && i + 1 < argc) {
            char tmp[512];
            snprintf(tmp, sizeof tmp, "%s", argv[++i]);
            for (char *t = strtok(tmp, ","); t && n_ram < 32; t = strtok(NULL, ","))
                ram_addr[n_ram++] = (unsigned)strtoul(t, NULL, 16) & 0x1FFF;
        }
        else if (!strcmp(argv[i], "--poke") && i + 1 < argc && n_poke < 16) {
            unsigned a, v;
            if (sscanf(argv[++i], "%x=%x", &a, &v) == 2) { poke[n_poke].addr = a & 0x1FFF; poke[n_poke++].val = v; }
        }
        else if (!strcmp(argv[i], "--dump-ram") && i + 1 < argc && n_dump < 16) {
            char *c = argv[++i];
            dump[n_dump].frame = atol(c);
            char *colon = strchr(c, ':');
            snprintf(dump[n_dump++].path, sizeof dump[0].path, "%s", colon ? colon + 1 : "work/ram.bin");
        }
        else if (!strcmp(argv[i], "--save-state") && i + 1 < argc && n_save_at < 4) {
            char *c = argv[++i];
            save_at[n_save_at].frame = atol(c);
            char *colon = strchr(c, ':');
            snprintf(save_at[n_save_at++].path, sizeof save_at[0].path, "%s", colon ? colon + 1 : "work/s.state");
        }
        else if (!strcmp(argv[i], "--load-state") && i + 1 < argc) load_state = argv[++i];
        else if (!strcmp(argv[i], "--cheats") && i + 1 < argc) {
            const char *c = argv[++i];
            cheats.health = strstr(c, "health") != NULL;
            cheats.hearts = strstr(c, "hearts") != NULL;
            cheats.lives = strstr(c, "lives") != NULL;
        }
        else if (!strcmp(argv[i], "--skip-cinematics")) skip_cine = true;
        else if (!strcmp(argv[i], "--ram-trace") && i + 1 < argc) ram_trace = fopen(argv[++i], "wb");
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) {
            sscanf(argv[++i], "%ld:%ld", &trace_frame, &trace_left);
        }
        else if (!strcmp(argv[i], "--watch") && i + 1 < argc && n_watch < 8) {
            unsigned b, a;
            if (sscanf(argv[++i], "%x:%x", &b, &a) == 2)
                watch[n_watch++] = (typeof(watch[0])){(uint8_t)b, (uint16_t)a, -1};
        }
        else if (!strcmp(argv[i], "--press") && i + 1 < argc && n_press < 64) {
            char spec[128], *a, *b;
            snprintf(spec, sizeof spec, "%s", argv[++i]);
            a = strchr(spec, ':');
            if (!a) continue;
            *a++ = 0;
            b = strchr(a, ':');
            if (b) *b++ = 0;
            presses[n_press++] = (Press){atol(spec), b ? atoi(b) : 6, parse_buttons(a)};
        }
    }

    static Pce pce;
    char err[256];
    if (bios_reset) {
        /* Run the original System Card reset and report the hardware state it sets up. */
        if (!pce_init(&pce, argv[1], err, sizeof err) || !pce_map_real_bios(&pce, bios_reset, err, sizeof err)) {
            fprintf(stderr, "error: %s\n", err);
            return 1;
        }
        hu_reset(&pce.cpu);
        for (long f = 0; f < frames; f++) {
            pce_run_frame(&pce);
            if ((f + 1) % 30 == 0 || f + 1 == frames) {
                printf("frame %3ld PC=%02X:%04X timer %s reload=$%02X irq_disable=%X irq_m=%02X VCE=%02X  "
                       "VDC CR=%04X MWR=%04X HSR=%04X HDR=%04X VSR=%04X VDR=%04X VCR=%04X  irqs %u/%u/%u\n",
                       f + 1, pce.cpu.mpr[pce.cpu.pc >> 13], pce.cpu.pc, pce.timer_on ? "on" : "off",
                       pce.timer_reload, pce.irq_disable, pce.wram[0xF5], pce.vce.ctrl,
                       pce.vdc.reg[5], pce.vdc.reg[9], pce.vdc.reg[10], pce.vdc.reg[11],
                       pce.vdc.reg[12], pce.vdc.reg[13], pce.vdc.reg[14],
                       pce.irq_taken[0], pce.irq_taken[1], pce.irq_taken[2]);
            }
        }
        return 0;
    }
    if (!pce_init(&pce, argv[1], err, sizeof err) || !pce_boot(&pce, err, sizeof err)) {
        fprintf(stderr, "error: %s\n", err);
        return 1;
    }
    pce.log_bios = log_bios;
    pce.log_bios_all = log_all;
    pce.mute_cdda = mute_cd;
    recomp_set_verify(verify);
    if (no_native || seeds_path) recomp_set_enabled(false);   /* seeds: record every entry point */
    if (seeds_path) recomp_load_misses(seeds_path);
    if (prof || n_watch || trace_left) pce.on_step = step_hook;
    if (load_state) {
        if (!pce_state_load(&pce, load_state, 0, err, sizeof err)) { fprintf(stderr, "load state: %s\n", err); return 1; }
        start_frame = (long)pce.frame;
        printf("state loaded: frame %ld\n", start_frame);
    }
    game_watch_reset(&gw, &pce);
    uint8_t ram_last[32];
    for (int i = 0; i < n_ram; i++) ram_last[i] = pce.wram[ram_addr[i]];

    /* --wav: 44.1 kHz stereo 16-bit output; header sizes patched at the end */
    FILE *wav = wav_path ? fopen(wav_path, "wb") : NULL;
    uint32_t wav_frames = 0;
    double audio_frac = 0;
    if (wav) { uint8_t hdr[44] = {0}; fwrite(hdr, 1, 44, wav); }

    for (long f = start_frame; f < start_frame + frames; f++) {
        if (wav) {
            static int16_t s[2048 * 2];
            audio_frac += 44100.0 / (7159090.0 / (455.0 * 263.0));
            int n = (int)audio_frac;
            audio_frac -= n;
            pce_audio_render(&pce, s, n);
            fwrite(s, 4, (size_t)n, wav);
            wav_frames += (uint32_t)n;
        }
        pce.pad = use_script ? capture_pad((int)f) : 0;
        for (int i = 0; i < n_press; i++)
            if (f >= presses[i].frame && f < presses[i].frame + presses[i].len) pce.pad |= presses[i].mask;
        for (int i = 0; i < n_poke; i++) pce.wram[poke[i].addr] = (uint8_t)poke[i].val;
        if (skip_cine && game_in_cinematic(&pce)) { pce.pad = 0; skipped++; }
        pce_run_frame(&pce);
        game_apply_cheats(&pce, &cheats, &gw);
        if (n_ram) {
            bool changed = false;
            for (int i = 0; i < n_ram; i++) changed |= pce.wram[ram_addr[i]] != ram_last[i];
            if (changed) {
                printf("frame %5ld pad %02X:", f + 1, pce.pad);
                for (int i = 0; i < n_ram; i++) {
                    printf(" %03X=%02X%s", ram_addr[i], pce.wram[ram_addr[i]], pce.wram[ram_addr[i]] != ram_last[i] ? "*" : " ");
                    ram_last[i] = pce.wram[ram_addr[i]];
                }
                printf("\n");
            }
        }
        if (ram_trace) { fwrite(pce.wram, 1, 0x2000, ram_trace); fputc(pce.pad, ram_trace); }
        for (int i = 0; i < n_dump; i++)
            if (dump[i].frame == f + 1) {
                FILE *df = fopen(dump[i].path, "wb");
                if (df) { fwrite(pce.wram, 1, 0x2000, df); fclose(df); }
            }
        for (int i = 0; i < n_save_at; i++)
            if (save_at[i].frame == f + 1 && pce_state_save(&pce, save_at[i].path, 0, err, sizeof err))
                printf("state saved at frame %ld: %s\n", f + 1, save_at[i].path);
        if ((shot_every > 0 && (f + 1) % shot_every == 0) || f + 1 == start_frame + frames) {
            char path[512];
            snprintf(path, sizeof path, "%s/frame_%05ld.png", out, f + 1);
            if (!png_write_argb(path, pce.vdc.fb, pce.vdc.fb_w, pce.vdc.fb_h, PCE_FB_MAX_W))
                fprintf(stderr, "cannot write %s (does the directory exist?)\n", path);
            HuC6280 *c = &pce.cpu;
            printf("frame %5ld  PC=%02X:%04X  CR=%04X  %dx%d  irq_m=%02X  vec1=%02X%02X  io?=%u\n",
                   f + 1, c->mpr[c->pc >> 13], c->pc, pce.vdc.reg[5], pce.vdc.fb_w, pce.vdc.fb_h,
                   pce.wram[0xF5], pce.wram[0x203], pce.wram[0x202], pce.unhandled_io);
            fflush(stdout);
        }
    }
    if (wav) {
        uint32_t data = wav_frames * 4;
        uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                         16, 0, 0, 0, 1, 0, 2, 0, 0x44, 0xAC, 0, 0, 0x10, 0xB1, 2, 0, 4, 0, 16, 0,
                         'd', 'a', 't', 'a', 0, 0, 0, 0};
        uint32_t riff = data + 36;
        memcpy(h + 4, &riff, 4);
        memcpy(h + 40, &data, 4);
        fseek(wav, 0, SEEK_SET);
        fwrite(h, 1, 44, wav);
        fclose(wav);
    }
    {
        const RecompStats *rs = recomp_stats();
        uint64_t tot = rs->native_cycles + rs->interp_cycles;
        printf("\nRecompiled code: %s, %d regions; %.2f%% of CPU cycles native; %llu region calls; "
               "%u entry points without a region",
               recomp_build_id, recomp_chunk_count, tot ? 100.0 * (double)rs->native_cycles / (double)tot : 0.0,
               (unsigned long long)rs->calls, rs->miss_entries);
        if (verify) printf("; verified %llu, failures %llu", (unsigned long long)rs->verify_runs,
                           (unsigned long long)rs->verify_failures);
        printf("\n");
        if (seeds_path && recomp_write_misses(seeds_path)) printf("Seeds written to %s\n", seeds_path);
        if (misses_path && recomp_write_misses(misses_path)) printf("Misses written to %s\n", misses_path);
    }
    if (skip_cine) printf("Cinematic frames run with input ignored: %ld\n", skipped);
    printf("Sound: %llu PSG register writes; PSG audible for %.1f s, ADPCM for %.1f s\n",
           (unsigned long long)pce.snd.psg_writes, (double)pce.snd.psg_samples / SND_RATE,
           (double)pce.snd.adpcm_samples / SND_RATE);
    printf("\nBIOS calls:");
    for (int i = 0; i < PCE_BIOS_ENTRIES; i++)
        if (pce.bios_calls[i]) printf(" %s x%u", bios_entry_name(i), pce.bios_calls[i]);
    printf("\nInterrupts taken: IRQ2 %u, IRQ1 %u, timer %u; timer %s reload %u; irq_disable %X\n",
           pce.irq_taken[0], pce.irq_taken[1], pce.irq_taken[2], pce.timer_on ? "on" : "off",
           pce.timer_reload, pce.irq_disable);
    if (prof) {
        printf("Hottest PCs outside the idle loop:\n");
        for (int k = 0; k < 16; k++) {
            uint32_t best = 0, at = 0;
            for (uint32_t i = 0; i < (1u << 24); i++)
                if (prof[i] > best && (i & 0xFFFF) != 0x4174) { best = prof[i]; at = i; }
            if (!best) break;
            printf("  %02X:%04X  %u\n", at >> 16, at & 0xFFFF, best);
            prof[at] = 0;
        }
    }
    return 0;
}
