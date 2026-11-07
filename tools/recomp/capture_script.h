/* Input script for the headless capture run (used by the code generator inside Setup, and
 * by rondo_headless --script to reproduce it). Frame numbers count from power-on.
 *
 * Path: intro and attract sequence -> title (RUN) -> save menu -> NAME ENTRY -> slot 1 ->
 * name "AA" -> END -> back to the save menu -> slot 1 -> GAME START -> Stage 0, played by
 * holding right, attacking every 45 frames and jumping every 120 frames.
 *
 * The menus ignore input while they animate, so presses are spaced out. */
#pragma once

#include <stdint.h>

#include "pce/pce.h"

typedef struct CaptureInput {
    int first, last;     /* frames: first press, last frame a repeat may start */
    int period;          /* 0 = single press */
    int len;             /* frames held */
    uint8_t pad;
} CaptureInput;

static const CaptureInput capture_script[] = {
    {8500, 8500, 0, 6, PAD_RUN}, {8800, 8800, 0, 6, PAD_RUN}, {9100, 9100, 0, 6, PAD_RUN}, {9400, 9400, 0, 6, PAD_RUN},
    /* save menu -> NAME ENTRY -> slot 1 */
    {9750, 9750, 0, 6, PAD_DOWN}, {9775, 9775, 0, 6, PAD_DOWN}, {9800, 9800, 0, 6, PAD_DOWN},
    {9830, 9830, 0, 8, PAD_I}, {10050, 10050, 0, 8, PAD_I},
    /* name "AA", then END */
    {10200, 10200, 0, 6, PAD_I}, {10230, 10230, 0, 6, PAD_I},
    {10300, 10300, 0, 6, PAD_DOWN}, {10320, 10320, 0, 6, PAD_DOWN}, {10340, 10340, 0, 6, PAD_DOWN},
    {10360, 10360, 0, 6, PAD_DOWN}, {10400, 10400, 0, 8, PAD_I},
    /* NAME ENTRY screen: END */
    {10760, 10760, 0, 6, PAD_DOWN}, {10820, 10820, 0, 6, PAD_DOWN}, {10880, 10880, 0, 6, PAD_DOWN},
    {10950, 10950, 0, 8, PAD_I},
    /* save menu: slot 1, then GAME START */
    {11250, 11250, 0, 6, PAD_UP}, {11310, 11310, 0, 6, PAD_UP}, {11370, 11370, 0, 6, PAD_UP},
    {11440, 11440, 0, 8, PAD_I}, {12450, 12450, 0, 8, PAD_I},
    /* Stage 0 */
    {14200, 14200, 0, 8000, PAD_RIGHT},
    {14230, 22000, 45, 4, PAD_II},
    {14260, 22000, 120, 10, PAD_I},
};

#define CAPTURE_FRAMES 20000

static inline uint8_t capture_pad(int frame)
{
    uint8_t pad = 0;
    for (unsigned i = 0; i < sizeof capture_script / sizeof capture_script[0]; i++) {
        const CaptureInput *s = &capture_script[i];
        if (frame < s->first) continue;
        if (s->period == 0) {
            if (frame < s->first + s->len) pad |= s->pad;
        } else if (frame <= s->last + s->len) {
            int k = (frame - s->first) % s->period;
            int start = frame - k;
            if (start <= s->last && k < s->len) pad |= s->pad;
        }
    }
    return pad;
}
