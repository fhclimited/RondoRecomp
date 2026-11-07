/* Identifying a disc image as a supported Rondo of Blood release. */
#pragma once

#include <stdint.h>

typedef enum {
    DISC_INVALID,       /* unreadable, or not a PC Engine CD */
    DISC_PCE_UNKNOWN,   /* a PC Engine CD, but not a known Rondo of Blood release */
    DISC_RONDO,         /* recognised Rondo of Blood */
} DiscKind;

/* Checks the PC Engine CD signature, then fingerprints the boot program sector.
 * If fingerprint is non-NULL it receives the fingerprint (0 when unreadable). */
DiscKind discid_identify(const char *cue_path, uint64_t *fingerprint);
