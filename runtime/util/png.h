#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Write 0xAARRGGBB pixels (stride in pixels) as an RGB PNG. */
bool png_write_argb(const char *path, const uint32_t *px, int w, int h, int stride);
