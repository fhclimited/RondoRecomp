/* Finding the user's disc image so rondo.exe can start without arguments. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    DISC_INVALID,       /* unreadable, or not a PC Engine CD */
    DISC_PCE_UNKNOWN,   /* a PC Engine CD, but not a known Rondo of Blood release */
    DISC_RONDO,         /* recognised Rondo of Blood */
} DiscKind;

DiscKind locate_identify(const char *cue_path);

/* Remembered path (if it still identifies as a PC Engine CD), else a search of the
 * folders around the executable for a recognised disc. */
bool locate_disc(char *out, size_t out_len);

void locate_remember(const char *cue_path);
