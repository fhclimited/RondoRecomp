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
            fprintf(trace_f, "f%llu l%03d %02X:%04X  A=%02X X=%02X Y=%02X S=%02X P=%02X  op=%02X  zp33=%02X\n",
                    (unsigned long long)p->frame, p->line, bank, pc, c->a, c->x, c->y, c->s, c->p,
                    pce_read8(p, pc), p->wram[0x33]);
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
    const char *bios_reset = NULL;
    Press presses[64];
    int n_press = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--shot-every") && i + 1 < argc) shot_every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--log-bios")) log_bios = true;
        else if (!strcmp(argv[i], "--log-bios-all")) log_bios = log_all = true;
        else if (!strcmp(argv[i], "--profile")) prof = calloc(1u << 24, sizeof *prof);
        else if (!strcmp(argv[i], "--bios-reset") && i + 1 < argc) bios_reset = argv[++i];
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
    if (prof || n_watch || trace_left) pce.on_step = step_hook;

    for (long f = 0; f < frames; f++) {
        pce.pad = 0;
        for (int i = 0; i < n_press; i++)
            if (f >= presses[i].frame && f < presses[i].frame + presses[i].len) pce.pad |= presses[i].mask;
        pce_run_frame(&pce);
        if ((shot_every > 0 && (f + 1) % shot_every == 0) || f + 1 == frames) {
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
