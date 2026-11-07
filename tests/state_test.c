/* Save state and rewind tests (need the disc): state_test <game.cue>
 *
 * 1. restore + same input reproduces the same machine state, byte for byte
 * 2. a state file loaded into a fresh instance (other addresses) continues identically
 * 3. rewinding N frames lands exactly on the state recorded N frames earlier, and returns
 *    the input of each undone frame
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pce/pce.h"
#include "pce/state.h"
#include "recomp/capture_script.h"

#define START 15000     /* in Stage 0 */
#define RUN 1500

static void run(Pce *p, int frames)
{
    for (int i = 0; i < frames; i++) {
        p->pad = capture_pad((int)p->frame);
        pce_run_frame(p);
    }
}

static int report(const char *what, const uint8_t *a, const uint8_t *b, size_t n)
{
    size_t i = 0;
    while (i < n && a[i] == b[i]) i++;
    if (i == n) { printf("  ok   %s\n", what); return 0; }
    printf("  FAIL %s: first difference at offset %zu\n", what, i);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: state_test <game.cue>\n"); return 2; }
    static Pce p, q;
    char err[256];
    if (!pce_init(&p, argv[1], err, sizeof err) || !pce_boot(&p, err, sizeof err)) { puts(err); return 1; }
    size_t n = pce_state_size();
    uint8_t *a = malloc(n), *b = malloc(n), *c = malloc(n), *mid = malloc(n);
    int fail = 0;

    run(&p, START);
    pce_state_capture(&p, a);
    run(&p, RUN);
    pce_state_capture(&p, b);
    printf("state size %zu bytes; frame %llu\n", n, (unsigned long long)p.frame);

    /* 1. restore and replay */
    pce_state_restore(&p, a);
    run(&p, RUN);
    pce_state_capture(&p, c);
    fail |= report("restore + replay reproduces the state", b, c, n);

    /* 2. file into a fresh instance */
    const char *path = "work/state_test.state";
    pce_state_restore(&p, a);
    if (!pce_state_save(&p, path, 0x1234, err, sizeof err)) { printf("  FAIL save: %s\n", err); fail = 1; }
    if (!pce_init(&q, argv[1], err, sizeof err) || !pce_boot(&q, err, sizeof err)) { puts(err); return 1; }
    char e2[256];
    if (pce_state_load(&q, path, 0x9999, e2, sizeof e2)) { printf("  FAIL wrong disc id accepted\n"); fail = 1; }
    else printf("  ok   wrong disc id refused (%s)\n", e2);
    if (!pce_state_load(&q, path, 0x1234, err, sizeof err)) { printf("  FAIL load: %s\n", err); fail = 1; }
    run(&q, RUN);
    pce_state_capture(&q, c);
    fail |= report("state file into a fresh instance continues identically", b, c, n);
    FILE *f = fopen(path, "rb");
    if (f) { fseek(f, 0, SEEK_END); printf("       state file: %ld bytes\n", ftell(f)); fclose(f); }

    /* 3. rewind */
    Rewind rw;
    if (!rewind_init(&rw, 256u << 20, 20000)) { puts("rewind_init failed"); return 1; }
    pce_state_restore(&p, a);
    rewind_reset(&rw, &p);
    for (int i = 0; i < RUN; i++) {
        uint8_t pad = capture_pad((int)p.frame);
        p.pad = pad;
        pce_run_frame(&p);
        rewind_record(&rw, &p, pad);
        if (i == RUN - 501) pce_state_capture(&p, mid);
    }
    printf("       rewind history: %d frames, %.1f KB per frame on average\n", rewind_frames(&rw),
           (double)rw.bytes / rewind_frames(&rw) / 1024.0);
    int pad_errors = 0;
    for (int i = 0; i < 500; i++) {
        uint8_t pad;
        uint64_t frame_before = 0;
        memcpy(&frame_before, rw.cur + offsetof(Pce, frame), sizeof frame_before);
        if (!rewind_step(&rw, &pad)) { puts("  FAIL history ended early"); fail = 1; break; }
        if (pad != capture_pad((int)frame_before - 1)) pad_errors++;
    }
    fail |= report("rewinding 500 frames lands on the recorded state", mid, rw.cur, n);
    if (pad_errors) { printf("  FAIL %d frames returned the wrong input\n", pad_errors); fail = 1; }
    else printf("  ok   each undone frame returned its input\n");
    pce_state_restore(&p, rw.cur);
    run(&p, 500);
    pce_state_capture(&p, c);
    fail |= report("playing on from the rewound state matches the original run", b, c, n);
    rewind_free(&rw);

    printf(fail ? "state tests FAILED\n" : "state tests passed\n");
    return fail;
}
