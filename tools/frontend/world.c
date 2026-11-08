/* See world.h. */
#include "world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Work RAM (offsets; $2000+ in the game's address space). See docs/findings.md. */
#define R_STATE 0x30
#define R_SUB 0x31
#define R_ROOM 0x82            /* room of the stage; its entry (where the player comes in) is $83 */
#define R_AREA 0xA2            /* stage; bit 0 = the character */
#define R_BANK 0xDB            /* the stage's first bank */
#define R_GRID 0x1D00          /* the room: 16 x 16 pages, a screen number each (0 = none) */
#define R_CAM_XL 0x1E08        /* scroll layer 0 (the playfield): X low, page; Y low, page */
#define R_CAM_XP 0x1E96
#define R_CAM_YL 0x1E20
#define R_CAM_YP 0x1E8E
#define R_CAM_XL_COPY 0x1E39   /* the copies the picture is drawn from (see cam_from_copy) */
#define R_CAM_XB_COPY 0x1E42
#define R_CAM_YL_COPY 0x1E54
#define R_CAM_YB_COPY 0x1E5D
#define R_LIM_TOP 0x1ECE       /* the camera's top and bottom page rows */
#define R_LIM_BOTTOM 0x1ED6
#define R_LIM_LEFT 0x1EBE      /* leftmost page, rightmost X (page, low byte) */
#define R_LIM_RIGHT 0x1EC6
#define R_PLAYER_XL 0x0BC8     /* object slot 0, screen coordinates */
#define R_PLAYER_XH 0x0B70
#define R_PLAYER_YL 0x0CD0
#define R_PLAYER_YH 0x0C78

#define SETTLE 4               /* frames into a room before its camera and picture count */
#define SPAN (16 * WORLD_PAGE) /* a room's coordinates: 4096 pixels both ways */

/* ---- tiles -------------------------------------------------------------------------- */

static WorldTile *tile_get(WorldTile **set, int page)
{
    if (!set[page]) {
        set[page] = calloc(1, sizeof(WorldTile));
        if (set[page]) set[page]->gen = 1;
    }
    return set[page];
}

static void tiles_free(World *w, WorldTile **set)
{
    for (int i = 0; i < 256; i++) {
        if (!set[i]) continue;
        if (set[i]->tex && w->free_tex) w->free_tex(set[i]->tex);
        free(set[i]);
        set[i] = NULL;
    }
}

/* ---- the saved map ------------------------------------------------------------------ */

static void map_load(World *w)
{
    FILE *f = w->path[0] ? fopen(w->path, "r") : NULL;
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f) && w->n < WORLD_ROOMS) {
        unsigned area, room, placed, s[16], k[16];
        int ox, oy;
        if (sscanf(line, "room %x %x %u %d %d %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x"
                         " %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x",
                   &area, &room, &placed, &ox, &oy, &s[0], &s[1], &s[2], &s[3], &s[4], &s[5], &s[6], &s[7],
                   &s[8], &s[9], &s[10], &s[11], &s[12], &s[13], &s[14], &s[15], &k[0], &k[1], &k[2], &k[3],
                   &k[4], &k[5], &k[6], &k[7], &k[8], &k[9], &k[10], &k[11], &k[12], &k[13], &k[14],
                   &k[15]) != 37)
            continue;
        WorldRoom *r = &w->rooms[w->n++];
        r->area = (uint8_t)area;
        r->room = (uint8_t)room;
        r->placed = placed != 0;
        r->ox = ox;
        r->oy = oy;
        for (int i = 0; i < 16; i++) {
            r->seen[i] = (uint16_t)s[i];
            r->known[i] = (uint16_t)k[i];
        }
    }
    fclose(f);
}

/* ---- the stages' pictures (saved next to the map, a file per stage) ----------------------
 * "RRWT", version, then per tile: room, 0 seen / 1 data, page, byte count, run-length
 * encoded pixels (a byte: bit 7 set = the next pixel 1-128 times, else 1-128 pixels as
 * they are). */

#define TILES_MAGIC 0x54575252u   /* "RRWT" */

static void tiles_path(const World *w, uint8_t area, char *out, size_t n)
{
    snprintf(out, n, "%s", w->path);
    char *slash = strrchr(out, '/'), *back = strrchr(out, '\\');
    if (back > slash) slash = back;
    size_t at = slash ? (size_t)(slash - out + 1) : 0;
    snprintf(out + at, n - at, "map_%02X.pic", area);
}

static size_t rle(const uint32_t *px, int n, uint8_t *out)
{
    size_t o = 0;
    for (int i = 0; i < n;) {
        int run = 1;
        while (i + run < n && run < 128 && px[i + run] == px[i]) run++;
        if (run >= 3) {
            out[o++] = (uint8_t)(0x80 | (run - 1));
            memcpy(out + o, &px[i], 4);
            o += 4;
            i += run;
            continue;
        }
        int lit = 0;
        while (i + lit < n && lit < 128 &&
               !(i + lit + 2 < n && px[i + lit] == px[i + lit + 1] && px[i + lit] == px[i + lit + 2]))
            lit++;
        if (!lit) lit = 1;
        out[o++] = (uint8_t)(lit - 1);
        memcpy(out + o, &px[i], (size_t)lit * 4);
        o += (size_t)lit * 4;
        i += lit;
    }
    return o;
}

static bool unrle(const uint8_t *in, size_t len, uint32_t *px, int n)
{
    size_t o = 0;
    int i = 0;
    while (o < len && i < n) {
        int c = (in[o] & 0x7F) + 1;
        if (in[o++] & 0x80) {
            if (o + 4 > len || i + c > n) return false;
            uint32_t v;
            memcpy(&v, in + o, 4);
            o += 4;
            for (int k = 0; k < c; k++) px[i++] = v;
        } else {
            if (o + (size_t)c * 4 > len || i + c > n) return false;
            memcpy(&px[i], in + o, (size_t)c * 4);
            o += (size_t)c * 4;
            i += c;
        }
    }
    return i == n;
}

static void pictures_save(World *w, uint8_t area)
{
    if (!w->path[0] || area == 0xFF || !w->pics_dirty) return;
    char path[1100], tmp[1110];
    tiles_path(w, area, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    uint8_t *buf = malloc(WORLD_PAGE * WORLD_PAGE * 4 + WORLD_PAGE * WORLD_PAGE / 128 + 16);
    if (!f || !buf) {
        if (f) fclose(f);
        free(buf);
        return;
    }
    uint32_t head[2] = {TILES_MAGIC, 1};
    fwrite(head, 4, 2, f);
    for (int i = 0; i < w->n; i++) {
        const WorldRoom *r = &w->rooms[i];
        if (r->area != area) continue;
        for (int kind = 0; kind < 2; kind++)
            for (int page = 0; page < 256; page++) {
                const WorldTile *t = (kind ? r->data : r->pic)[page];
                if (!t) continue;
                uint32_t n = (uint32_t)rle(t->px, WORLD_PAGE * WORLD_PAGE, buf);
                uint8_t key[3] = {r->room, (uint8_t)kind, (uint8_t)page};
                fwrite(key, 1, 3, f);
                fwrite(&n, 4, 1, f);
                fwrite(buf, 1, n, f);
            }
    }
    free(buf);
    bool ok = fclose(f) == 0;
    if (ok) {
        remove(path);
        ok = rename(tmp, path) == 0;
    }
    if (ok) w->pics_dirty = false;
}

static void pictures_load(World *w, uint8_t area)
{
    if (!w->path[0]) return;
    char path[1100];
    tiles_path(w, area, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint32_t head[2];
    uint8_t *buf = malloc(WORLD_PAGE * WORLD_PAGE * 4 + WORLD_PAGE * WORLD_PAGE / 128 + 16);
    if (buf && fread(head, 4, 2, f) == 2 && head[0] == TILES_MAGIC && head[1] == 1) {
        uint8_t key[3];
        uint32_t n;
        while (fread(key, 1, 3, f) == 3 && fread(&n, 4, 1, f) == 1 &&
               n <= WORLD_PAGE * WORLD_PAGE * 4 + WORLD_PAGE * WORLD_PAGE / 128 + 16 && fread(buf, 1, n, f) == n) {
            int ri = -1;
            for (int i = 0; i < w->n; i++)
                if (w->rooms[i].area == area && w->rooms[i].room == key[0]) ri = i;
            if (ri < 0) continue;
            WorldTile **set = key[1] ? w->rooms[ri].data : w->rooms[ri].pic;
            if (set[key[2]]) continue;            /* already drawn this session */
            WorldTile *t = tile_get(set, key[2]);
            if (t && !unrle(buf, n, t->px, WORLD_PAGE * WORLD_PAGE)) memset(t->px, 0, sizeof t->px);
        }
    }
    free(buf);
    fclose(f);
}

void world_save(World *w)
{
    pictures_save(w, w->area);
    if (!w->dirty || !w->path[0]) return;
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", w->path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "# Rondo Recomp map: rooms placed and pages seen, per stage\n");
    for (int i = 0; i < w->n; i++) {
        const WorldRoom *r = &w->rooms[i];
        fprintf(f, "room %02X %02X %d %d %d", r->area, r->room, r->placed ? 1 : 0, r->ox, r->oy);
        for (int y = 0; y < 16; y++) fprintf(f, " %X", r->seen[y]);
        for (int y = 0; y < 16; y++) fprintf(f, " %X", r->known[y]);
        fputc('\n', f);
    }
    bool ok = fclose(f) == 0;
    if (ok) {
        remove(w->path);
        ok = rename(tmp, w->path) == 0;
    }
    if (ok) w->dirty = false;
}

void world_init(World *w, const char *map_path)
{
    void (*keep)(void *) = w->free_tex;
    memset(w, 0, sizeof *w);
    w->free_tex = keep;
    snprintf(w->path, sizeof w->path, "%s", map_path ? map_path : "");
    w->cur = -1;
    w->from = -1;
    w->last_room = -1;
    w->area = 0xFF;
    map_load(w);
}

void world_free(World *w)
{
    for (int i = 0; i < w->n; i++) {
        tiles_free(w, w->rooms[i].pic);
        tiles_free(w, w->rooms[i].data);
    }
}

void world_reset_play(World *w)
{
    w->settled = false;
    w->cur = -1;
    w->from = -1;
    w->last_room = -1;
    w->steady = 0;
    w->live = false;
}

static int find_room(World *w, uint8_t area, uint8_t room)
{
    for (int i = 0; i < w->n; i++)
        if (w->rooms[i].area == area && w->rooms[i].room == room) return i;
    if (w->n == WORLD_ROOMS) return -1;
    WorldRoom *r = &w->rooms[w->n];
    memset(r, 0, sizeof *r);
    r->area = area;
    r->room = room;
    w->dirty = true;
    return w->n++;
}

static bool any_placed(const World *w, uint8_t area)
{
    for (int i = 0; i < w->n; i++)
        if (w->rooms[i].area == area && w->rooms[i].placed) return true;
    return false;
}

/* ---- reading the game ----------------------------------------------------------------- */

static int player_x(const Pce *p) { return (int16_t)(p->wram[R_PLAYER_XL] | p->wram[R_PLAYER_XH] << 8); }
static int player_y(const Pce *p) { return (int16_t)(p->wram[R_PLAYER_YL] | p->wram[R_PLAYER_YH] << 8); }

/* The camera the coming frame shows. The game's frame logic runs across the frame
 * boundary (from vblank into the next frame's first lines), so the camera itself can be
 * caught half-updated (e.g. Y set 64 above and clamped back two lines later); the frame
 * is drawn from the copy the logic takes as it starts (69:41FD: low byte and the page's
 * bit 0, 512-pixel map coordinates). The full value is the one that matches the copy
 * nearest the camera now. */
static int cam_from_copy(int low, int bit0, int page, int now_low)
{
    int now = (page & 0x0F) << 8 | now_low, copy = (bit0 & 1) << 8 | low;
    int d = (copy - now) & 0x1FF;
    if (d >= 0x100) d -= 0x200;
    return (now + d) & 0xFFF;
}

void world_before_frame(World *w, const Pce *p)
{
    const uint8_t *r = p->wram;
    w->cam_x = cam_from_copy(r[R_CAM_XL_COPY], r[R_CAM_XB_COPY], r[R_CAM_XP], r[R_CAM_XL]);
    w->cam_y = cam_from_copy(r[R_CAM_YL_COPY], r[R_CAM_YB_COPY], r[R_CAM_YP], r[R_CAM_YL]);
    bool same = w->cur >= 0 && w->rooms[w->cur].area == (r[R_AREA] & 0xFE) && w->rooms[w->cur].room == r[R_ROOM];
    w->same = same && r[R_STATE] == 0x05;
    w->live = w->same && r[R_SUB] == 0x01 && w->steady >= SETTLE - 1;
}

/* B.origin - A.origin for a move from room A (its last settled frame: from_*) to room B
 * (now: camera bx, by; player px, py). By the edge of the screen the player left A at, the
 * rooms meet there; anywhere else (a door) the player stands at the same place in both. */
static void move_offset(const World *w, int bcx, int bcy, int bpx, int bpy, int *dx, int *dy, const char **how)
{
    int sx = w->from_px - w->from_cam_x, sy = w->from_py - w->from_cam_y;
    if (sx >= 208) {
        *dx = w->from_cam_x + 256 - bcx;
        *dy = w->from_py - bpy;
        *how = "right edge";
    } else if (sx < 48) {
        *dx = w->from_cam_x - (bcx + 256);
        *dy = w->from_py - bpy;
        *how = "left edge";
    } else if (sy < 40) {
        *dx = w->from_px - bpx;
        *dy = w->from_cam_y - (bcy + 224);
        *how = "top edge";
    } else if (sy >= 176) {
        *dx = w->from_px - bpx;
        *dy = w->from_cam_y + 224 - bcy;
        *how = "bottom edge";
    } else {
        *dx = w->from_px - bpx;
        *dy = w->from_py - bpy;
        *how = "door";
    }
}

static void place(World *w, WorldRoom *b, const Pce *p)
{
    int bpx = w->cam_x + player_x(p), bpy = w->cam_y + player_y(p);
    if (w->from >= 0 && w->from != w->cur) {
        WorldRoom *a = &w->rooms[w->from];
        int dx, dy;
        const char *how;
        move_offset(w, w->cam_x, w->cam_y, bpx, bpy, &dx, &dy, &how);
        if (!a->placed && !b->placed && !any_placed(w, b->area)) {
            a->placed = true;
            a->ox = a->oy = 0;
        }
        if (a->placed && !b->placed) {
            b->ox = a->ox + dx;
            b->oy = a->oy + dy;
            b->placed = true;
            w->dirty = true;
            snprintf(w->last_event, sizeof w->last_event, "room %02X:%02X placed at %d,%d (%s of %02X, offset %d,%d)",
                     b->area, b->room, b->ox, b->oy, how, a->room, dx, dy);
        } else if (!a->placed && b->placed) {
            a->ox = b->ox - dx;
            a->oy = b->oy - dy;
            a->placed = true;
            w->dirty = true;
            snprintf(w->last_event, sizeof w->last_event, "room %02X:%02X placed at %d,%d (behind %02X)", a->area,
                     a->room, a->ox, a->oy, b->room);
        } else {
            snprintf(w->last_event, sizeof w->last_event, "room %02X:%02X entered (%s of %02X)", b->area, b->room,
                     how, a->room);
        }
    } else if (!b->placed && !any_placed(w, b->area)) {
        b->placed = true;
        b->ox = b->oy = 0;
        w->dirty = true;
        snprintf(w->last_event, sizeof w->last_event, "room %02X:%02X starts the stage's map", b->area, b->room);
    }
    w->from = -1;
}

/* ---- the seen picture ----------------------------------------------------------------- */

static void capture(World *w, const Pce *p, WorldRoom *rm)
{
    const uint32_t *bg = vdc_fb_bg();
    const uint8_t *on = vdc_row_bg_on();
    int fw = p->vdc.fb_w, fh = p->vdc.fb_h, ext = p->vdc.fb_wide;
    if (fh > 256) fh = 256;
    uint32_t bumped[8] = {0};   /* tiles touched this frame: gen bumped once */
    for (int y = 0; y < fh; y++) {
        if (!on[y]) continue;
        int ry = w->cam_y + y;
        if (ry < 0 || ry >= SPAN) continue;
        const uint32_t *src = &bg[y * PCE_FB_MAX_W];
        for (int x = 0; x < fw;) {
            int rx = w->cam_x - ext + x;
            if (rx < 0) { x += -rx; continue; }
            if (rx >= SPAN) break;
            int page = (ry >> 8) * 16 + (rx >> 8);
            int run = WORLD_PAGE - (rx & 255);
            if (run > fw - x) run = fw - x;
            WorldTile *t = tile_get(rm->pic, page);
            if (!t) return;
            uint32_t *dst = &t->px[(ry & 255) * WORLD_PAGE + (rx & 255)];
            bool any = false;
            for (int k = 0; k < run; k++)
                if (src[x + k] >> 24) { dst[k] = src[x + k]; any = true; }
            if (any && !(bumped[page >> 5] & 1u << (page & 31))) {
                bumped[page >> 5] |= 1u << (page & 31);
                t->gen++;
                w->pics_dirty = true;
            }
            x += run;
        }
    }
    /* the map: pages the screen covered at least a quarter of each way */
    for (int py = 0; py < 16; py++)
        for (int px = 0; px < 16; px++) {
            int ox = (w->cam_x + 256 < (px + 1) * 256 ? w->cam_x + 256 : (px + 1) * 256) -
                     (w->cam_x > px * 256 ? w->cam_x : px * 256);
            int oy = (w->cam_y + 224 < (py + 1) * 256 ? w->cam_y + 224 : (py + 1) * 256) -
                     (w->cam_y > py * 256 ? w->cam_y : py * 256);
            if (ox >= 64 && oy >= 56 && !(rm->seen[py] & 1u << px)) {
                rm->seen[py] |= (uint16_t)(1u << px);
                w->dirty = true;
            }
        }
}

/* ---- drawing a room from the game's data ------------------------------------------------
 * The scroll code (bank $6F) draws 32-pixel blocks of a page: the page's screen number
 * gives 64 block numbers (8 x 8, at S + screen * 64) and 8 bytes of their ninth bits (at
 * B + screen * 8, a bit per column); a block is 4 x 4 background map words (32 bytes at
 * C + block * 32 in bank $DB + K + ninth bit). S, B, K and C are read from the code
 * itself, which is recognised by its bytes (-1: any). */

typedef struct DataCode { uint16_t screens, bits, blocks; uint8_t bank_add; } DataCode;

static const int PAT_SCREEN[] = {0x48, 0x64, 0x0D, 0x0A, 0x26, 0x0D, 0x0A, 0x26, 0x0D, 0x0A, 0x26, 0x0D, 0x18, 0x69, -1,
                                 0x85, 0x0C, 0xA5, 0x0D, 0x69, -1,   0x85, 0x0D, 0x68, 0x64, 0x05, 0xA0, 0x06, 0x0A, 0x26,
                                 0x05, 0x88, 0xD0, 0xFA, 0x18, 0x69, -1,   0x85, 0x04, 0xA5, 0x05, 0x69, -1,   0x85, 0x05, 0x60};
static const int PAT_BLOCK[] = {0x48, 0xA9, -1,   0x69, 0x00, 0x18, 0x65, 0xDB, 0x53, 0x40, 0x68, 0x64, 0x13, 0x0A, 0x26,
                                0x13, 0x0A, 0x26, 0x13, 0x0A, 0x26, 0x13, 0x0A, 0x26, 0x13, 0x0A, 0x26, 0x13, 0x18, 0x69,
                                -1,   0x85, 0x12, 0xA5, 0x13, 0x69, -1,   0x85, 0x13};

static int find(const uint8_t *bank, const int *pat, int n)
{
    for (int i = 0; i + n <= 0x2000; i++) {
        int k = 0;
        while (k < n && (pat[k] < 0 || bank[i + k] == pat[k])) k++;
        if (k == n) return i;
    }
    return -1;
}

static bool data_code(const Pce *p, DataCode *dc)
{
    const uint8_t *b = p->page[0x6F];
    if (!b) return false;
    int s = find(b, PAT_SCREEN, (int)(sizeof PAT_SCREEN / sizeof PAT_SCREEN[0]));
    int k = find(b, PAT_BLOCK, (int)(sizeof PAT_BLOCK / sizeof PAT_BLOCK[0]));
    if (s < 0 || k < 0) return false;
    dc->bits = (uint16_t)(b[s + 14] | b[s + 20] << 8);
    dc->screens = (uint16_t)(b[s + 36] | b[s + 42] << 8);
    dc->bank_add = b[k + 2];
    dc->blocks = (uint16_t)(b[k + 30] | b[k + 36] << 8);
    /* the tables are read through $A000-$BFFF (bank $6F), the blocks through $C000-$DFFF */
    return dc->screens >= 0xA000 && dc->screens < 0xC000 && dc->bits >= 0xA000 && dc->bits < 0xC000 &&
           dc->blocks >= 0xC000 && dc->blocks < 0xE000;
}

static void draw_tile8(const Pce *p, uint16_t word, uint32_t *dst)
{
    const uint16_t *vram = p->vdc.vram;
    const uint32_t *argb = p->vce.argb;
    uint32_t base = (uint32_t)(word & 0x7FF) * 16;
    int pal = word >> 12;
    for (int row = 0; row < 8; row++) {
        uint16_t a = vram[(base + row) & 0x7FFF], b = vram[(base + 8 + row) & 0x7FFF];
        for (int col = 0; col < 8; col++) {
            int bit = 7 - col;
            int c = (a >> bit & 1) | (a >> (8 + bit) & 1) << 1 | (b >> bit & 1) << 2 | (b >> (8 + bit) & 1) << 3;
            dst[row * WORLD_PAGE + col] = c ? argb[pal * 16 + c] : argb[0];
        }
    }
}

/* What the camera can show within its limits now (room pixels): the game's walls. Its
 * grid also holds screens it never scrolls to here (another room's, a boss arena shown
 * later through another entry), which are left out. */
static void reach(const uint8_t *r, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = (r[R_LIM_LEFT] & 15) * WORLD_PAGE;
    *x1 = (r[R_LIM_RIGHT] & 15) * WORLD_PAGE + r[R_LIM_RIGHT + 1] + 256;
    int top = r[R_LIM_TOP] & 15, bottom = r[R_LIM_BOTTOM] & 15;
    if (bottom < top) { int t = top; top = bottom; bottom = t; }
    *y0 = top * WORLD_PAGE;
    *y1 = bottom * WORLD_PAGE + 224;
    if (*x1 > SPAN) *x1 = SPAN;
    if (*y1 > SPAN) *y1 = SPAN;
}

static void draw_data(World *w, const Pce *p, WorldRoom *rm, const DataCode *dc)
{
    const uint8_t *r = p->wram, *code = p->page[0x6F];
    int rx0, ry0, rx1, ry1;
    reach(r, &rx0, &ry0, &rx1, &ry1);
    for (int py = ry0 / WORLD_PAGE; py * WORLD_PAGE < ry1; py++)
        for (int px = rx0 / WORLD_PAGE; px * WORLD_PAGE < rx1; px++) {
            int screen = r[R_GRID + py * 16 + px] & 0x7F;
            if (!screen) continue;
            WorldTile *t = tile_get(rm->data, py * 16 + px);
            if (!t) return;
            for (int by = 0; by < 8; by++) {
                uint32_t bits_at = (uint32_t)(dc->bits - 0xA000) + (uint32_t)screen * 8 + (uint32_t)by;
                uint8_t bits = bits_at < 0x2000 ? code[bits_at] : 0;
                for (int bx = 0; bx < 8; bx++) {
                    uint32_t at = (uint32_t)(dc->screens - 0xA000) + (uint32_t)screen * 64 + (uint32_t)(by * 8 + bx);
                    if (at >= 0x2000) continue;
                    int block = code[at];
                    int bank = r[R_BANK] + dc->bank_add + (bits >> bx & 1);
                    const uint8_t *bk = bank < 256 ? p->page[bank] : NULL;
                    uint32_t off = (uint32_t)(dc->blocks - 0xC000) + (uint32_t)block * 32;
                    if (!bk || off + 32 > 0x2000) continue;
                    for (int ty = 0; ty < 4; ty++)
                        for (int tx = 0; tx < 4; tx++) {
                            uint16_t word = (uint16_t)(bk[off + (ty * 4 + tx) * 2] | bk[off + (ty * 4 + tx) * 2 + 1] << 8);
                            draw_tile8(p, word, &t->px[(by * 32 + ty * 8) * WORLD_PAGE + bx * 32 + tx * 8]);
                        }
                }
            }
            /* clear what lies past the walls */
            for (int y = 0; y < WORLD_PAGE; y++) {
                int ry = py * WORLD_PAGE + y;
                for (int x = 0; x < WORLD_PAGE; x++) {
                    int rx = px * WORLD_PAGE + x;
                    if (rx < rx0 || rx >= rx1 || ry < ry0 || ry >= ry1) t->px[y * WORLD_PAGE + x] = 0;
                }
            }
            t->gen++;
            w->pics_dirty = true;
            if (!(rm->known[py] & 1u << px)) {
                rm->known[py] |= (uint16_t)(1u << px);
                w->dirty = true;
            }
        }
}

/* ---- every frame ------------------------------------------------------------------------ */

void world_after_frame(World *w, const Pce *p)
{
    const uint8_t *r = p->wram;
    w->shown = false;
    if (r[R_STATE] != 0x05) {          /* not playing (a story scene, the stage changing) */
        w->cur = -1;
        w->steady = 0;
        w->settled = false;
        return;
    }
    if (r[0x98] == 0) w->last_room = -1;   /* the player died: the next room is a restart */
    uint8_t area = r[R_AREA] & 0xFE, room = r[R_ROOM];
    if (w->cur < 0 || w->rooms[w->cur].area != area || w->rooms[w->cur].room != room) {
        int prev = w->last_room;
        bool from_prev = prev >= 0 && w->rooms[prev].area == area &&
                         !(w->rooms[prev].room == room);
        if (area != w->area) {
            /* a new stage: the last one's pictures saved and let go, this one's loaded */
            world_save(w);
            for (int i = 0; i < w->n; i++)
                if (w->rooms[i].area != area) {
                    tiles_free(w, w->rooms[i].pic);
                    tiles_free(w, w->rooms[i].data);
                }
            w->area = area;
            pictures_load(w, area);
        }
        w->from = from_prev ? prev : -1;
        if (from_prev) {
            w->from_cam_x = w->last_cam_x;
            w->from_cam_y = w->last_cam_y;
            w->from_px = w->last_px;
            w->from_py = w->last_py;
        }
        w->cur = find_room(w, area, room);
        w->steady = 0;
        w->settled = false;
        w->data_age = 0;
        memset(w->data_limits, 0xFF, sizeof w->data_limits);
        if (w->cur < 0) return;
    }
    /* the picture is the room's once it has settled (also through its other sub-states: a
     * stage's title, the player dying), unless the room changed during the frame */
    w->shown = w->settled && w->same;
    if (r[R_SUB] != 0x01) {
        w->steady = 0;
        return;
    }
    WorldRoom *rm = &w->rooms[w->cur];
    /* a new room counts once its picture is up: for a few frames the game shows only the
     * backdrop colour while it draws the room (the background is switched off) */
    if (w->steady < SETTLE) {
        const uint8_t *on = vdc_row_bg_on();
        int rows = p->vdc.fb_h < 256 ? p->vdc.fb_h : 256, lit = 0;
        for (int y = 0; y < rows; y++) lit += on[y] != 0;
        if (lit * 4 < rows * 3) {
            w->steady = 0;
            return;
        }
    }
    int was = w->steady++;
    if (w->steady == SETTLE && !w->settled) {
        place(w, rm, p);
        w->settled = true;
        w->shown = w->same;
    }
    /* the frame just run showed the camera read before it, in this room, settled */
    if (was >= SETTLE - 1 && w->live) {
        if (r[0x98]) capture(w, p, rm);   /* not while dying (the screen fades) */
        w->last_room = w->cur;
        w->last_cam_x = w->cam_x;
        w->last_cam_y = w->cam_y;
        w->last_px = w->cam_x + player_x(p);
        w->last_py = w->cam_y + player_y(p);
    }
    /* the room from its data: once settled, again when the camera's limits change, and
     * every few seconds (colours change) */
    uint8_t lim[4] = {r[R_LIM_LEFT], r[R_LIM_RIGHT], r[R_LIM_TOP], r[R_LIM_BOTTOM]};
    w->data_age++;
    if (w->steady >= 30 && ((memcmp(lim, w->data_limits, 4) && w->data_age >= 30) || w->data_age >= 600)) {
        DataCode dc;
        w->data_ok = data_code(p, &dc);
        if (w->data_ok) draw_data(w, p, rm, &dc);
        memcpy(w->data_limits, lim, 4);
        w->data_age = 0;
    }
}

void world_step_view(World *w)
{
    const WorldRoom *rm = w->cur >= 0 ? &w->rooms[w->cur] : NULL;
    if (!rm || !w->settled) return;                  /* between rooms: hold still */
    float tx = (float)(rm->placed ? rm->ox : 0) + (float)w->cam_x;
    float ty = (float)(rm->placed ? rm->oy : 0) + (float)w->cam_y;
    float dx = tx - w->view_x, dy = ty - w->view_y;
    if (!w->view_set || !rm->placed) {
        w->view_x = tx;
        w->view_y = ty;
        w->view_set = true;
        return;
    }
    /* follow the camera exactly, or glide to it after a jump (a room change) */
    if (fabsf(dx) <= 24 && fabsf(dy) <= 24) {
        w->view_x = tx;
        w->view_y = ty;
        return;
    }
    float k = 0.16f;
    float sx = dx * k, sy = dy * k;
    if (fabsf(sx) < 2) sx = dx > 0 ? 2.0f : -2.0f;
    if (fabsf(sy) < 2 && fabsf(dy) > 24) sy = dy > 0 ? 2.0f : -2.0f;
    w->view_x += fabsf(sx) > fabsf(dx) ? dx : sx;
    w->view_y += fabsf(sy) > fabsf(dy) ? dy : sy;
}

bool world_player(const World *w, const Pce *p, float *x, float *y)
{
    if (w->cur < 0 || !w->rooms[w->cur].placed || !w->settled) return false;
    const WorldRoom *rm = &w->rooms[w->cur];
    *x = (float)(rm->ox + w->cam_x + player_x(p));
    *y = (float)(rm->oy + w->cam_y + player_y(p));
    return true;
}

/* ---- diagnostics ------------------------------------------------------------------------ */

static void blit_tiles(uint32_t *img, int iw, int ih, int x0, int y0, WorldTile *const *set)
{
    for (int page = 0; page < 256; page++) {
        const WorldTile *t = set[page];
        if (!t) continue;
        int tx = x0 + (page & 15) * WORLD_PAGE, ty = y0 + (page >> 4) * WORLD_PAGE;
        for (int y = 0; y < WORLD_PAGE; y++) {
            int iy = ty + y;
            if (iy < 0 || iy >= ih) continue;
            for (int x = 0; x < WORLD_PAGE; x++) {
                int ix = tx + x;
                uint32_t c = t->px[y * WORLD_PAGE + x];
                if (ix >= 0 && ix < iw && c >> 24) img[iy * iw + ix] = c;
            }
        }
    }
}

static bool room_box(const WorldRoom *r, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = *y0 = 16;
    *x1 = *y1 = -1;
    for (int page = 0; page < 256; page++)
        if (r->pic[page] || r->data[page]) {
            int x = page & 15, y = page >> 4;
            if (x < *x0) *x0 = x;
            if (y < *y0) *y0 = y;
            if (x > *x1) *x1 = x;
            if (y > *y1) *y1 = y;
        }
    return *x1 >= 0;
}

uint32_t *world_room_image(const World *w, int i, int *out_w, int *out_h)
{
    if (i < 0 || i >= w->n) return NULL;
    const WorldRoom *r = &w->rooms[i];
    int x0, y0, x1, y1;
    if (!room_box(r, &x0, &y0, &x1, &y1)) return NULL;
    int iw = (x1 - x0 + 1) * WORLD_PAGE, ih = (y1 - y0 + 1) * WORLD_PAGE;
    uint32_t *img = malloc((size_t)iw * (size_t)ih * 4);
    if (!img) return NULL;
    for (int k = 0; k < iw * ih; k++) img[k] = 0xFF200020u;
    blit_tiles(img, iw, ih, -x0 * WORLD_PAGE, -y0 * WORLD_PAGE, r->data);
    blit_tiles(img, iw, ih, -x0 * WORLD_PAGE, -y0 * WORLD_PAGE, r->pic);
    *out_w = iw;
    *out_h = ih;
    return img;
}

uint32_t *world_stage_image(const World *w, uint8_t area, int *out_w, int *out_h)
{
    int bx0 = 1 << 30, by0 = 1 << 30, bx1 = -(1 << 30), by1 = -(1 << 30);
    for (int i = 0; i < w->n; i++) {
        const WorldRoom *r = &w->rooms[i];
        int x0, y0, x1, y1;
        if (r->area != area || !r->placed || !room_box(r, &x0, &y0, &x1, &y1)) continue;
        if (r->ox + x0 * WORLD_PAGE < bx0) bx0 = r->ox + x0 * WORLD_PAGE;
        if (r->oy + y0 * WORLD_PAGE < by0) by0 = r->oy + y0 * WORLD_PAGE;
        if (r->ox + (x1 + 1) * WORLD_PAGE > bx1) bx1 = r->ox + (x1 + 1) * WORLD_PAGE;
        if (r->oy + (y1 + 1) * WORLD_PAGE > by1) by1 = r->oy + (y1 + 1) * WORLD_PAGE;
    }
    if (bx1 <= bx0) return NULL;
    int iw = bx1 - bx0, ih = by1 - by0;
    if ((long long)iw * ih > 64ll << 20) return NULL;
    uint32_t *img = malloc((size_t)iw * (size_t)ih * 4);
    if (!img) return NULL;
    for (int k = 0; k < iw * ih; k++) img[k] = 0xFF200020u;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < w->n; i++) {
            const WorldRoom *r = &w->rooms[i];
            if (r->area != area || !r->placed) continue;
            blit_tiles(img, iw, ih, r->ox - bx0, r->oy - by0, pass ? r->pic : r->data);
        }
    *out_w = iw;
    *out_h = ih;
    return img;
}
