/* See wide.h. */
#include "wide.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game.h"

/* A change to the game: `len` bytes at physical bank:offset, applied only when the bytes
 * there are `orig` (so other code loaded into the same RAM is left alone). */
typedef struct Patch {
    const char *what;
    uint8_t bank;
    uint16_t off;          /* offset within the 8 KiB bank */
    uint8_t len;
    uint8_t orig[64];
    uint8_t wide[64];
} Patch;

#define FILL60 /* 52 bytes of our bank $00's RTS filler */ \
    {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, \
     0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, \
     0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, \
     0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60}

#define E WIDE_MAX         /* every patch is made for the widest supported view */

static const Patch patches[] = {
    /* Background streaming (bank $6F, the scroll-layer code; every layer and both axes
     * use it): when the camera crosses a 32-pixel column, the game draws the column 64
     * pixels past the edge it is moving towards. The table holds that distance (low
     * byte; the page byte after it stays +1/-1). 0x70/0x90 draws 112 pixels out, which
     * still fits the 512-pixel background map (256 + 2 * 112 + 32 = 512). */
    {"background columns streamed 112 px out", 0x6F, 0x0780, 2, {0x40, 0xC0}, {0x70, 0x90}},
    /* Some layers (the scenery of the Stage 0 carriage chase) are first drawn by stage
     * code out to the old 64 px only, which would leave a 48 px gap the new distance
     * never fills. So each streamed column (6F:A7A5) is drawn twice: at the old distance,
     * then at the new one, by code in free space in our own bank $00 (mapped at $E000
     * throughout) that switches the table above between the two. */
    {"streamed columns drawn at the old and the new distance", 0x6F, 0x07A5, 3, {0xBD, 0x66, 0x3E}, {0x4C, 0x10, 0xFE}},
    {"column drawing at both distances", 0x00, 0x1E10, 52, FILL60,
     {0x5A,                     /* PHY                              */
      0xDA,                     /* PHX                              */
      0xA9, 0x40,               /* LDA #$40      the old distances  */
      0x8D, 0x80, 0xA7,         /* STA $A780                        */
      0xA9, 0xC0,               /* LDA #$C0                         */
      0x8D, 0x81, 0xA7,         /* STA $A781                        */
      0x20, 0x2E, 0xFE,         /* JSR draw                         */
      0xFA,                     /* PLX                              */
      0x7A,                     /* PLY                              */
      0xA9, 0x70,               /* LDA #$70      the new distances  */
      0x8D, 0x80, 0xA7,         /* STA $A780                        */
      0xA9, 0x90,               /* LDA #$90                         */
      0x8D, 0x81, 0xA7,         /* STA $A781                        */
      0x4C, 0x2E, 0xFE,         /* JMP draw (returns to the caller) */
      0xBD, 0x66, 0x3E,         /* draw: LDA $3E66,X  (as at A7A5)  */
      0x4C, 0xA8, 0xA7,         /* JMP $A7A8                        */
      0x40, 0xC0,               /* $FE34: the old distances, kept for vertical use */
      0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60}},
    /* The table is also read for vertical distances: where full-height columns start
     * (64 px above the camera, 6F:A7C4) and how far rows are streamed above and below
     * (6F:A8A4). Those stay as they were, read from the copy at $FE34; scenery layers
     * shorter than the map would otherwise be drawn into each other's rows. */
    {"columns start 64 px above the camera, as before", 0x6F, 0x07C5, 2, {0x80, 0xA7}, {0x34, 0xFE}},
    {"rows streamed 64 px above and below, as before", 0x6F, 0x08A5, 2, {0x80, 0xA7}, {0x34, 0xFE}},
    /* Room start (6F:A94C): the game draws 13 columns from 64 px left of the camera by
     * moving a temporary camera 384 px left and stepping it right 32 px at a time. With
     * the table above each column lands 48 px further right, so start 480 px left
     * (camera + 0x20 - 0x200) and draw all 16 columns. */
    {"room start draws 16 columns", 0x6F, 0x0963, 7,
     {0xA9, 0x0C, 0x85, 0x16, 0xA9, 0x80, 0x44}, {0xA9, 0x0F, 0x85, 0x16, 0xA9, 0x20, 0x44}},
    /* Rows streamed while the camera moves up or down (6F:A8D5) span the original 256
     * pixels from the camera; draw 16 blocks from 112 px left instead (the whole map). */
    {"rows drawn 512 px wide", 0x6F, 0x08D5, 7,
     {0xAD, 0x66, 0x3E, 0x29, 0x7F, 0x85, 0x08}, {0xA9, 0x10, 0x85, 0x08, 0xEA, 0xEA, 0xEA}},
    {"rows start 112 px left", 0x6F, 0x08EC, 2, {0x10, 0x15}, {0xEA, 0xEA}},

    /* Objects live in screen coordinates. Spawning (bank $69): objects in the room's list
     * (sorted by world X) start when they come within camera + $140 (64 px past the right
     * edge) or camera - $40; start them 128 px out instead (camera + $180 / - $80). */
    {"objects spawn 128 px right of the screen", 0x69, 0x1E68, 2, {0x69, 0x40}, {0x69, 0x80}},
    {"objects spawn 128 px left of the screen", 0x69, 0x1E7F, 2, {0x69, 0xC0}, {0x69, 0x80}},
    /* Object update (bank $70, 70:A000): objects more than 128 px off either side are
     * removed (screen X < -128 or >= 384); keep them to -192 / 448 so ones that just
     * spawned at 384 are not removed at once... */
    {"objects removed 192 px left of the screen", 0x70, 0x0070, 1, {0x80}, {0x40}},
    {"objects removed 192 px right of the screen", 0x70, 0x007D, 1, {0x80}, {0xC0}},
    /* ...and objects that only move near the screen wait at -64 / 320; let them move
     * from -128 / 384, where they now spawn. */
    {"objects move from 128 px left of the screen", 0x70, 0x00B0, 1, {0xC0}, {0x80}},
    {"objects move from 128 px right of the screen", 0x70, 0x00BD, 1, {0x40}, {0x80}},

    /* ---- drawing: follows each frame's choice of wide or 4:3 (sprites past the 256
     * pixels would only take up the 64 hardware sprites in a 4:3 picture) ---- */

    /* Sprite building (bank $72, 72:8000): each sprite piece is dropped unless its X
     * (screen + 32) is 0..$11F; let pieces through from -E to $11F + E. */
    {"sprites drawn up to 112 px right of the screen", 0x72, 0x0133, 1, {0x20}, {0x20 + E}},
    {"sprites with X 0..15 drawn", 0x72, 0x013A, 3, {0xDD, 0xA1, 0x81}, {0xC9, 0x00, 0xEA}},
    {"negative sprite X goes to the check below", 0x72, 0x0127, 3, {0x4C, 0x8C, 0x81}, {0x4C, 0x00, 0xFE}},
    /* ...in free space in our own bank $00 (RTS filler; mapped at $E000 throughout):
     * A = X high byte (negative). Keep the piece if it is $FF and the low byte >= -E. */
    {"negative sprite X check", 0x00, 0x1E00, 16,
     {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60},
     {0xC9, 0xFF,               /* CMP #$FF          */
      0xD0, 0x09,               /* BNE drop          */
      0xA5, 0x05,               /* LDA <$05          */
      0xC9, 0x100 - E,          /* CMP #-E           */
      0x90, 0x03,               /* BCC drop          */
      0x4C, 0x3F, 0x81,         /* JMP $813F (keep)  */
      0x4C, 0x8C, 0x81}},       /* drop: JMP $818C   */
};
#define N_PATCHES (int)(sizeof patches / sizeof patches[0])
#define N_WORLD 14         /* patches[0..13] change the game world, the rest only drawing */
#define WORLD_PATCHES ((1u << N_WORLD) - 1)
#define DRAW_PATCHES (((1u << N_PATCHES) - 1) & ~WORLD_PATCHES)

static uint8_t *at(Pce *p, const Patch *q) { return p->page[q->bank] ? p->page[q->bank] + q->off : NULL; }

/* Diagnostics: patches to leave out (bit i = patches[i]), counted as in place. */
uint32_t wide_debug_skip;

/* Which patches are in place. */
static uint32_t patches_in(Pce *p)
{
    uint32_t mask = wide_debug_skip;
    for (int i = 0; i < N_PATCHES; i++) {
        const uint8_t *m = at(p, &patches[i]);
        if (m && !memcmp(m, patches[i].wide, patches[i].len)) mask |= 1u << i;
    }
    return mask;
}

/* Put the patches in `mask` in (or take them out) where the original (or patched) bytes
 * are found. */
static void set_patches(Pce *p, uint32_t mask, bool on)
{
    for (int i = 0; i < N_PATCHES; i++) {
        if (!(mask & 1u << i) || (wide_debug_skip & 1u << i)) continue;
        const Patch *q = &patches[i];
        uint8_t *m = at(p, q);
        if (!m) continue;
        const uint8_t *from = on ? q->orig : q->wide, *to = on ? q->wide : q->orig;
        if (!memcmp(m, from, q->len)) {
            memcpy(m, to, q->len);
            p->cpu.page_gen[q->bank]++;   /* recompiled code in this bank is re-checked */
        }
    }
}

int wide_extent(double window_aspect, double picture_aspect)
{
    double w = 256.0 * window_aspect / picture_aspect;
    int e = (int)floor((w - 256.0) / 2.0);
    if (e < 0) e = 0;
    if (e > WIDE_MAX) e = WIDE_MAX;
    return e & ~1;
}

/* ---- objects ------------------------------------------------------------------------ */

/* Object arrays (88 slots, work RAM offsets): kind, flags, screen X low/high. */
#define OBJ_SLOTS 0x58
#define OBJ_KIND 0x0750
#define OBJ_FLAGS 0x10F0     /* bit 0: fixed to the screen (does not scroll) */
#define OBJ_XL 0x0BC8
#define OBJ_XH 0x0B70
#define OBJ_TYPE 0x0A10
#define HUD_SLOT 0x57        /* the HUD (health, hearts, score): type 5, screen X 8 */

/* Some objects are generators fixed to the screen at the old spawn edge (e.g. Stage 1's
 * skeleton generator sits at screen X 320 and drops skeletons there). Move ones at
 * exactly 320 / -64 to the new edge, 384 / -128 (and back when widescreen is off). */
static void move_generators(Pce *p, bool on)
{
    uint8_t *r = p->wram;
    for (int i = 0; i < OBJ_SLOTS; i++) {
        if (!r[OBJ_KIND + i] || !(r[OBJ_FLAGS + i] & 1)) continue;
        int x = r[OBJ_XL + i] | r[OBJ_XH + i] << 8;
        int to = -1;
        if (on && x == 0x0140) to = 0x0180;
        else if (on && x == 0xFFC0) to = 0xFF80;
        else if (!on && x == 0x0180) to = 0x0140;
        else if (!on && x == 0xFF80) to = 0xFFC0;
        if (to >= 0) {
            r[OBJ_XL + i] = (uint8_t)to;
            r[OBJ_XH + i] = (uint8_t)(to >> 8);
        }
    }
}

/* Keep the HUD at the left edge of the picture: screen X 8 - extension. */
static void place_hud(Wide *w, Pce *p, int ext)
{
    uint8_t *r = p->wram;
    if (r[OBJ_TYPE + HUD_SLOT] != 0x05) return;
    int x = (int16_t)(r[OBJ_XL + HUD_SLOT] | r[OBJ_XH + HUD_SLOT] << 8);
    if (x != 8 && x != w->hud_x) return;      /* not where we or the game put it */
    int to = 8 - ext;
    r[OBJ_XL + HUD_SLOT] = (uint8_t)to;
    r[OBJ_XH + HUD_SLOT] = (uint8_t)(to >> 8);
    w->hud_x = to;
}

/* ---- the room ----------------------------------------------------------------------- */

/* Scroll layers (work RAM): layer 0 is the playfield camera. */
#define CAM_XL 0x1E08        /* X low byte, layers 0-7 */
#define CAM_PAGE 0x1E96      /* X high byte mod 16, layers 0-7 */
#define LIMIT_LEFT 0x1EBE    /* camera's leftmost page */
#define LIMIT_RIGHT 0x1EC6   /* camera's rightmost X: page, low byte */

/* signed distance a - b in the camera's 4096-pixel space */
static int wrap(int d) { d &= 0xFFF; return d >= 0x800 ? d - 0x1000 : d; }

/* the camera can move sideways in this room */
static bool room_scrolls(const uint8_t *r)
{
    return r[LIMIT_RIGHT] * 256 + r[LIMIT_RIGHT + 1] > r[LIMIT_LEFT] * 256;
}

/* The two routines that draw a room when it starts (6F:A94C, 6F:A9B1; checked by their
 * first bytes). The game has just loaded the room's header, camera limits included. */
static const uint8_t ROOM_START_A[] = {0x82, 0xA9, 0x90, 0x9D, 0x66, 0x3E};   /* A94C */
static const uint8_t ROOM_START_B[] = {0xA4, 0x45, 0xB9, 0x94, 0xA9, 0x82};   /* A9B1 */

/* Called at every dispatch point (Pce.on_entry): when a room starts, put the world
 * changes in for rooms the camera scrolls sideways through, and take them out for every
 * other room, whose background map may be shared out between layers in ways the wider
 * drawing would overwrite (the Stage 0 carriage chase is one). */
static void on_entry(Pce *p, void *ctx)
{
    uint16_t pc = p->cpu.pc;
    if ((pc != 0xA94C && pc != 0xA9B1) || p->cpu.mpr[5] != 0x6F || !p->page[0x6F]) return;
    const uint8_t *code = p->page[0x6F] + (pc & 0x1FFF);
    if (memcmp(code, pc == 0xA94C ? ROOM_START_A : ROOM_START_B, 6)) return;
    Wide *w = ctx;
    set_patches(p, WORLD_PATCHES, w->want > 0 && room_scrolls(p->wram));
}

void wide_frame(Wide *w, Pce *p)
{
    uint8_t *r = p->wram;
    uint8_t s = game_state(p);
    bool want = w->want > 0;

    if (!p->on_entry) {
        p->on_entry = on_entry;
        p->on_entry_ctx = w;
    }
    /* outside gameplay the game is as it was; rooms put the changes back as they start */
    if (s != RONDO_PLAYING) set_patches(p, WORLD_PATCHES, false);
    w->patched = (patches_in(p) & WORLD_PATCHES) == WORLD_PATCHES;
    if (s == RONDO_PLAYING) move_generators(p, w->patched);

    /* scenery that keeps scrolling sideways (the carriage chase): a layer moved on at
     * least 45 of the last 60 frames; changing rooms starts the count again */
    bool moved = false;
    for (int i = 1; i < 8; i++) {
        if (r[CAM_XL + i] != w->layer_x[i]) moved = true;
        w->layer_x[i] = r[CAM_XL + i];
    }
    if (s != RONDO_PLAYING || r[0x31] == 0x03 || r[0x31] == 0x00) w->move_history = 0;
    else w->move_history = (w->move_history << 1 | moved) & ((1ull << 60) - 1);
    int moving = 0;
    for (uint64_t h = w->move_history; h; h &= h - 1) moving++;
    w->scenery_moving = moving;

    /* this frame shows the camera as it is now (it moves during the frame and is shown
     * from the next one) */
    int ext = w->want;
    int cam = (r[CAM_PAGE] & 0x0F) << 8 | r[CAM_XL];
    int left = wrap(r[LIMIT_LEFT] * 256 - cam);
    int right = wrap((r[LIMIT_RIGHT] * 256 + r[LIMIT_RIGHT + 1]) - cam) + 256;
    bool scrolls = room_scrolls(r);
    int mw = (p->vdc.reg[9] >> 4) & 3;         /* background map width: 64+ tiles needed */
    /* wide: rooms that scroll sideways, drawn with the changes; and rooms one screen wide
     * whose scenery scrolls past (their layers loop round the whole map), drawn as the game
     * draws them. Others (vertical shafts, boss rooms, rooms entered before widescreen was
     * turned on) keep the 4:3 picture. */
    bool show = want && s == RONDO_PLAYING && mw >= 1 &&
                (scrolls ? w->patched : (!w->patched && w->scenery_moving >= 45));
    /* sprites past the 256 pixels are built only while the picture is wide */
    set_patches(p, DRAW_PATCHES, show);
    if (show && (patches_in(p) & DRAW_PATCHES) != DRAW_PATCHES) {
        set_patches(p, DRAW_PATCHES, false);
        show = false;
    }
    if (!show) ext = 0;

    p->vdc.wide = ext;
    if (scrolls) {
        p->vdc.wide_x0 = left + ext;
        p->vdc.wide_x1 = right + ext;
    } else {
        p->vdc.wide_x0 = 0;
        p->vdc.wide_x1 = 256 + 2 * ext;
    }
    if (s == RONDO_PLAYING) place_hud(w, p, ext);
    w->active = ext;
}

void wide_off(Wide *w, Pce *p)
{
    set_patches(p, WORLD_PATCHES | DRAW_PATCHES, false);
    move_generators(p, false);
    place_hud(w, p, 0);
    w->patched = false;
    w->active = 0;
    p->vdc.wide = 0;
}

int wide_report(Pce *p, char *out, int n)
{
    int used = 0;
    for (int i = 0; i < N_PATCHES && used < n; i++) {
        const Patch *q = &patches[i];
        const uint8_t *m = at(p, q);
        const char *st = !m ? "unmapped" : !memcmp(m, q->wide, q->len) ? "in" : !memcmp(m, q->orig, q->len) ? "out" : "other code";
        used += snprintf(out + used, (size_t)(n - used), "  %02X:%04X %-10s %s\n", q->bank, q->off, st, q->what);
    }
    return used;
}
