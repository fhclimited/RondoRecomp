/* See prompt.h. */
#include "prompt.h"

#include <string.h>

#include "pce/video.h"

#define LINE_Y 136             /* the sprites' screen Y */
#define LINE_X 64              /* the original line's left edge, 18 characters of 8 pixels;
                                * the new one starts there too (left of it is the logo's dagger) */

/* The original line as the game lays it out, and the six sprites that draw it (screen X,
 * pattern, attributes: palette 7 in front, 16 or 32 pixels wide, 16 high). */
static const char ORIGINAL[] = "PUSH  RUN  BUTTON!";
static const struct { int x; uint16_t pattern, attr; } SPRITES[6] = {
    {64, 0x1AA, 0x0187}, {112, 0x1A9, 0x0087}, {128, 0x1AC, 0x0087},
    {152, 0x1AD, 0x0087}, {168, 0x1AE, 0x0187}, {200, 0x1EB, 0x0087},
};

/* Letters the original line does not have, in its style: 7 rows of 2-pixel strokes, the
 * top three rows a pixel to the right of the bottom three (italic). Drawn on rows 8-14 of
 * the character, as the game's are. */
static const struct { char c; const char *rows[7]; } EXTRA[] = {
    {'A', {"..#####.", ".##...##", ".##...##", ".######.", "##...##.", "##...##.", "##...##."}},
    {'E', {".#######", ".##.....", ".##.....", ".#####..", "##......", "##......", "#######."}},
    {'K', {".##...##", ".##..##.", ".##.##..", ".####...", "##.##...", "##..##..", "##...##."}},
    {'Y', {".##...##", ".##...##", "..##.##.", "...###..", "..##....", "..##....", "..##...."}},
};

static const char *new_line(bool pad) { return pad ? "PUSH  START  BUTTON!" : "PUSH  ENTER  KEY!"; }

/* sprite pattern pixel (16 x 16 cell, 4 planes of 16 words) */
static int pattern_pixel(const Vdc *d, int cell, int x, int y)
{
    uint32_t a = ((uint32_t)cell * 64 + (uint32_t)y) & 0x7FFF;
    int bit = 15 - x;
    return ((d->vram[a] >> bit) & 1) | ((d->vram[(a + 16) & 0x7FFF] >> bit) & 1) << 1 |
           ((d->vram[(a + 32) & 0x7FFF] >> bit) & 1) << 2 | ((d->vram[(a + 48) & 0x7FFF] >> bit) & 1) << 3;
}

void prompt_before_frame(TitlePrompt *t, Pce *p)
{
    t->hidden = false;
    vdc_set_sprite_hide(0);
    if (p->wram[0x30] != 0x02) return;          /* the opening and title screen only */
    const Vdc *d = &p->vdc;
    uint64_t mask = 0;
    int found = 0;
    for (int k = 0; k < 6; k++)
        for (int i = 0; i < 64; i++) {
            const uint16_t *s = &d->satb[i * 4];
            if ((s[0] & 0x3FF) - 64 == LINE_Y && (s[1] & 0x3FF) - 32 == SPRITES[k].x &&
                ((s[2] >> 1) & 0x3FF) == SPRITES[k].pattern && s[3] == SPRITES[k].attr && !(mask >> i & 1)) {
                mask |= 1ull << i;
                found++;
                break;
            }
        }
    if (found != 6) return;

    /* the line as the sprites draw it, cut into characters */
    uint8_t line[16][18 * 8];
    memset(line, 0, sizeof line);
    for (int k = 0; k < 6; k++) {
        int w = (SPRITES[k].attr & 0x0100) ? 32 : 16;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < w; x++) {
                int lx = SPRITES[k].x - LINE_X + x;
                if (lx < 0 || lx >= 18 * 8) continue;
                int c = pattern_pixel(d, (SPRITES[k].pattern & (w == 32 ? ~1 : ~0)) + (x >> 4), x & 15, y);
                if (c) line[y][lx] = (uint8_t)c;
            }
    }
    int count[16] = {0};
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 18 * 8; x++) {
            t->cells[x >> 3][y][x & 7] = line[y][x];
            count[line[y][x]]++;
        }
    t->ink = 1;
    for (int c = 1; c < 16; c++)
        if (count[c] > count[t->ink]) t->ink = (uint8_t)c;
    if (!count[t->ink]) return;                 /* nothing there: leave the game's line */

    t->palette = SPRITES[0].attr & 0xF;
    t->hidden = true;
    vdc_set_sprite_hide(mask);
}

/* one character of the new line into the picture */
static void draw_char(const TitlePrompt *t, Pce *p, char ch, int x0)
{
    if (ch == ' ') return;
    Vdc *d = &p->vdc;
    const uint32_t *argb = &p->vce.argb[0x100 | t->palette << 4];
    for (int i = 0; ORIGINAL[i]; i++)
        if (ORIGINAL[i] == ch) {                 /* the game's own letter */
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 8; x++) {
                    int c = t->cells[i][y][x], fx = d->fb_wide + x0 + x, fy = LINE_Y + y;
                    if (c && fx >= 0 && fx < d->fb_w && fy < d->fb_h) d->fb[fy * PCE_FB_MAX_W + fx] = argb[c];
                }
            return;
        }
    for (size_t e = 0; e < sizeof EXTRA / sizeof EXTRA[0]; e++)
        if (EXTRA[e].c == ch) {
            for (int y = 0; y < 7; y++)
                for (int x = 0; x < 8; x++) {
                    int fx = d->fb_wide + x0 + x, fy = LINE_Y + 8 + y;
                    if (EXTRA[e].rows[y][x] == '#' && fx >= 0 && fx < d->fb_w && fy < d->fb_h)
                        d->fb[fy * PCE_FB_MAX_W + fx] = argb[t->ink];
                }
            return;
        }
}

void prompt_after_frame(TitlePrompt *t, Pce *p)
{
    vdc_set_sprite_hide(0);
    if (!t->hidden) return;
    const char *s = t->line ? t->line : new_line(t->pad);
    int n = (int)strlen(s);
    for (int i = 0; i < n; i++) draw_char(t, p, s[i], LINE_X + i * 8);
    t->frames++;
}
