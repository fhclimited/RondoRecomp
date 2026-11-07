/* Finding the user's disc image so rondo.exe can start without arguments. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "pce/discid.h"

/* Remembered path (if it still identifies as a PC Engine CD), else a search of the
 * folders around the executable (game/ next to it first) for a recognised disc. */
bool locate_disc(char *out, size_t out_len);

void locate_remember(const char *cue_path);
