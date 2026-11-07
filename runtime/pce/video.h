/* PC Engine video: HuC6260 VCE (palette, dot clock) and HuC6270 VDC
 * (VRAM, background, sprites, DMA, raster/vblank interrupts). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PCE_FB_MAX_W 512
#define PCE_FB_MAX_H 256
#define PCE_LINES_PER_FRAME 263

typedef struct Vce {
    uint16_t pal[512];      /* 9-bit GRB entries */
    uint32_t argb[512];     /* cached 0xAARRGGBB */
    uint16_t addr;
    uint8_t ctrl;           /* bits 0-1 dot clock, bit 7 grayscale */
} Vce;

void vce_reset(Vce *v);
void vce_write(Vce *v, uint16_t off, uint8_t val);
uint8_t vce_read(Vce *v, uint16_t off);

enum {
    VDC_ST_CR = 0x01,   /* sprite 0 collision */
    VDC_ST_OR = 0x02,   /* sprite overflow */
    VDC_ST_RR = 0x04,   /* raster compare */
    VDC_ST_DS = 0x08,   /* VRAM -> SATB DMA done */
    VDC_ST_DV = 0x10,   /* VRAM -> VRAM DMA done */
    VDC_ST_VD = 0x20,   /* vertical blank */
};

typedef struct Vdc {
    uint16_t vram[0x8000];
    uint16_t satb[256];
    uint16_t reg[32];
    uint8_t ar;
    uint8_t status;
    uint16_t read_latch;
    bool irq;               /* IRQ1 line */
    bool satb_pending;      /* DVSSR written: transfer at next vblank */

    /* per-frame state, latched at frame start */
    int display_start, display_end;
    int bg_y;               /* y scroll of the previously rendered line */
    bool first_line;

    /* framebuffer of the active display */
    uint32_t fb[PCE_FB_MAX_W * PCE_FB_MAX_H];
    int fb_w, fb_h;

    /* Widescreen (host setting, not machine state): draw this many extra pixels on each
     * side of a 256-pixel picture, from the background map and the sprites the game
     * places there. 0 = off, at most VDC_WIDE_MAX. fb_wide is the amount used for the
     * current frame (latched at its start); the original picture starts at fb[fb_wide]. */
    int wide;
    int fb_wide;
    /* Also host-set, used only while widescreen is drawn: background columns of the
     * framebuffer outside [wide_x0, wide_x1) are black (beyond the room's walls);
     * sprites are still drawn there. */
    int wide_x0, wide_x1;
} Vdc;

#define VDC_WIDE_MAX 128

void vdc_reset(Vdc *d);
void vdc_write(Vdc *d, uint16_t off, uint8_t val);
uint8_t vdc_read(Vdc *d, uint16_t off);

/* Called at the start of each of the 263 lines (before the CPU runs that line). */
void vdc_line_start(Vdc *d, int line);
/* Called after the CPU has run the line: renders it if it is an active display line. */
void vdc_line_end(Vdc *d, const Vce *vce, int line);

/* Host-side rendering aids for widescreen, kept outside the machine state (save states are
 * unchanged; one picture is drawn at a time per process):
 * - vdc_row_scroll: the background X scroll (BXR) each displayed row of the last frame was
 *   drawn with; raster splits give bands of rows their own scroll.
 * - vdc_row_bg_blank: rows of the last frame whose background showed nothing (colour 0)
 *   across the original 256 pixels; vdc_row_bg_edge: rows of a widened picture whose
 *   background showed something at its left edge (first 4 pixels).
 * - vdc_set_row_clip: per displayed row, where its background is cut off beyond the
 *   original 256 pixels, as beyond a room's walls (sprites or black there): VDC_CLIP_BOTH
 *   both sides, VDC_CLIP_RIGHT the right side only, 0 neither. NULL for none; the array
 *   (PCE_FB_MAX_H bytes) must stay valid while frames are drawn. */
enum { VDC_CLIP_BOTH = 1, VDC_CLIP_RIGHT = 2 };
const uint16_t *vdc_row_scroll(void);
const uint8_t *vdc_row_bg_blank(void);
const uint8_t *vdc_row_bg_edge(void);
void vdc_set_row_clip(const uint8_t *rows);
