/* Machine state snapshots (save states, rewind) and the XOR-delta codec they share. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pce.h"

size_t pce_state_size(void);
/* Copy the machine state into out (pce_state_size() bytes). Host-side fields (pointers,
 * disc handles, framebuffer, audio output queue, diagnostics, input) are zeroed, so
 * snapshots are comparable across sessions and differ little from frame to frame. */
void pce_state_capture(const Pce *p, uint8_t *out);
/* Restore a captured state into p, keeping p's host-side fields. */
void pce_state_restore(Pce *p, const uint8_t *in);

/* Save-state files: header (format, state size, disc id) + delta-coded snapshot. */
bool pce_state_save(const Pce *p, const char *path, uint64_t disc_id, char *err, int err_len);
bool pce_state_load(Pce *p, const char *path, uint64_t disc_id, char *err, int err_len);

/* XOR-delta codec: delta_encode writes a ^ b as (zero run, literal run) tokens and returns
 * the encoded length (out needs delta_bound(n) bytes); delta_apply XORs it into dst. */
size_t delta_bound(size_t n);
size_t delta_encode(const uint8_t *a, const uint8_t *b, size_t n, uint8_t *out);
void delta_apply(uint8_t *dst, size_t n, const uint8_t *code, size_t len);

/* ---- rewind ------------------------------------------------------------------------- */

typedef struct RewindFrame RewindFrame;

typedef struct Rewind {
    size_t size;                 /* snapshot size */
    uint8_t *cur, *tmp, *code;   /* cur: snapshot of the newest recorded state */
    RewindFrame **frames;        /* ring of per-frame deltas, newest at head - 1 */
    int cap, head, count;
    size_t bytes, budget;
} Rewind;

bool rewind_init(Rewind *r, size_t budget_bytes, int max_frames);
void rewind_free(Rewind *r);
/* Forget the history; the current state becomes the starting point. */
void rewind_reset(Rewind *r, const Pce *p);
/* Call after each frame, with the input that was used during it. */
void rewind_record(Rewind *r, const Pce *p, uint8_t pad);
/* Step r->cur one frame back. Returns false when the history is used up; *pad receives
 * the input of the frame that was undone. */
bool rewind_step(Rewind *r, uint8_t *pad);
int rewind_frames(const Rewind *r);
