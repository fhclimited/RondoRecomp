/* See worldview.h. */
#include "worldview.h"

#include <math.h>

#include "theme.h"

void worldview_init(WorldView *wv, SDL_Renderer *ren)
{
    SDL_zerop(wv);
    wv->ren = ren;
}

void worldview_free(WorldView *wv)
{
    if (wv->canvas) SDL_DestroyTexture(wv->canvas);
    if (wv->blur) SDL_DestroyTexture(wv->blur);
    wv->canvas = wv->blur = NULL;
}

void worldview_free_tex(void *tex)
{
    if (tex) SDL_DestroyTexture((SDL_Texture *)tex);
}

/* A tile's texture, uploaded if the tile changed. */
static SDL_Texture *tile_tex(SDL_Renderer *ren, WorldTile *t)
{
    if (!t->tex) {
        SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WORLD_PAGE,
                                             WORLD_PAGE);
        if (!tex) return NULL;
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        t->tex = tex;
        t->tex_gen = 0;
    }
    if (t->tex_gen != t->gen) {
        SDL_UpdateTexture((SDL_Texture *)t->tex, NULL, t->px, WORLD_PAGE * 4);
        t->tex_gen = t->gen;
    }
    return (SDL_Texture *)t->tex;
}

static void draw_tiles(SDL_Renderer *ren, WorldTile *const *set, int ox, int oy, int vx, int vy, int cw, int ch)
{
    for (int page = 0; page < 256; page++) {
        WorldTile *t = set[page];
        if (!t) continue;
        int x = ox + (page & 15) * WORLD_PAGE - vx, y = oy + (page >> 4) * WORLD_PAGE - vy;
        if (x >= cw || y >= ch || x + WORLD_PAGE <= 0 || y + WORLD_PAGE <= 0) continue;
        SDL_Texture *tex = tile_tex(ren, t);
        if (!tex) continue;
        SDL_FRect d = {(float)x, (float)y, (float)WORLD_PAGE, (float)WORLD_PAGE};
        SDL_RenderTexture(ren, tex, NULL, &d);
    }
}

SDL_Texture *worldview_compose(WorldView *wv, World *w, SDL_Texture *frame, int frame_w, int frame_h,
                               int frame_wide, bool frame_live, int ext, int *w_out, int *h_out)
{
    SDL_Renderer *ren = wv->ren;
    WorldRoom *cur = world_room(w);
    if (!w->view_set || w->area == 0xFF) return NULL;
    int cw = 256 + 2 * ext, ch = frame_h > 0 ? frame_h : 224;
    if (!wv->canvas || wv->cw != cw || wv->ch != ch) {
        if (wv->canvas) SDL_DestroyTexture(wv->canvas);
        wv->canvas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, cw, ch);
        if (!wv->canvas) return NULL;
        SDL_SetTextureScaleMode(wv->canvas, SDL_SCALEMODE_NEAREST);
        wv->cw = cw;
        wv->ch = ch;
    }
    int vx = (int)lroundf(w->view_x) - ext, vy = (int)lroundf(w->view_y);
    SDL_SetRenderTarget(ren, wv->canvas);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    /* a room that could not be placed (a state loaded into it) is shown on its own */
    bool alone = cur && !cur->placed && w->settled;
    /* drawn from the data under seen; the room being played over the others */
    for (int pass = 0; pass < 4; pass++) {
        bool data = pass < 2, mine = pass & 1;
        for (int i = 0; i < w->n; i++) {
            WorldRoom *r = &w->rooms[i];
            bool is_cur = r == cur;
            if (is_cur != mine || r->area != w->area) continue;
            if (alone ? !is_cur : !r->placed) continue;
            int ox = alone ? 0 : r->ox, oy = alone ? 0 : r->oy;
            draw_tiles(ren, data ? r->data : r->pic, ox, oy, vx, vy, cw, ch);
        }
    }
    /* the live picture where the camera is (clear beyond the room's walls) */
    if (frame_live && cur && (cur->placed || alone)) {
        int ox = alone ? 0 : cur->ox, oy = alone ? 0 : cur->oy;
        SDL_FRect src = {0, 0, (float)frame_w, (float)frame_h};
        SDL_FRect d = {(float)(ox + w->cam_x - frame_wide - vx), (float)(oy + w->cam_y - vy), (float)frame_w,
                       (float)frame_h};
        SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(frame, SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(ren, frame, &src, &d);
        SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
    }
    SDL_SetRenderTarget(ren, NULL);
    *w_out = cw;
    *h_out = ch;
    return wv->canvas;
}

/* ---- the map ----------------------------------------------------------------------------- */

static bool cell(const WorldRoom *r, int x, int y)
{
    if (x < 0 || y < 0 || x > 15 || y > 15) return false;
    return ((r->seen[y] | r->known[y]) >> x) & 1;
}

void worldview_draw_map(WorldView *wv, const World *w, const Pce *p, SDL_FRect dst, float t)
{
    SDL_Renderer *ren = wv->ren;
    int ww, wh;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    float fw = (float)ww, fh = (float)wh, u = th_unit(ww, wh);
    float open = t / 0.16f;
    if (open < 0) open = 0;
    if (open > 1) open = 1;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    /* the paused game behind, softly blurred (half its resolution, smoothly scaled back) */
    if (wv->canvas) {
        int bw = wv->cw / 2 > 0 ? wv->cw / 2 : 1, bh = wv->ch / 2 > 0 ? wv->ch / 2 : 1;
        if (!wv->blur || wv->bw != bw || wv->bh != bh) {
            if (wv->blur) SDL_DestroyTexture(wv->blur);
            wv->blur = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, bw, bh);
            wv->bw = bw;
            wv->bh = bh;
        }
        if (wv->blur) {
            SDL_SetTextureScaleMode(wv->canvas, SDL_SCALEMODE_LINEAR);
            SDL_SetRenderTarget(ren, wv->blur);
            SDL_RenderTexture(ren, wv->canvas, NULL, NULL);
            SDL_SetRenderTarget(ren, NULL);
            SDL_SetTextureScaleMode(wv->canvas, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureScaleMode(wv->blur, SDL_SCALEMODE_LINEAR);
            SDL_SetTextureAlphaModFloat(wv->blur, open);
            SDL_SetTextureBlendMode(wv->blur, SDL_BLENDMODE_BLEND);
            SDL_RenderTexture(ren, wv->blur, NULL, &dst);
        }
    }
    th_rect(ren, 0, 0, fw, fh, (SDL_Color){8, 6, 12, (Uint8)(110 * open)});

    /* the stage's rooms: pages seen (light) and known from the room's data (darker) */
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
    for (int i = 0; i < w->n; i++) {
        const WorldRoom *r = &w->rooms[i];
        if (r->area != w->area || !r->placed) continue;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                if (!cell(r, x, y)) continue;
                int cx = r->ox + x * WORLD_PAGE, cy = r->oy + y * WORLD_PAGE;
                if (cx < x0) x0 = cx;
                if (cy < y0) y0 = cy;
                if (cx + WORLD_PAGE > x1) x1 = cx + WORLD_PAGE;
                if (cy + WORLD_PAGE > y1) y1 = cy + WORLD_PAGE;
            }
    }
    float a = open;
    th_text(ren, TH_FONT_TITLE, 40 * u, fw * 0.5f, fh * 0.1f, th_fade(TH_TEXT, a), "Map", 0.5f, 0.5f, 1 * u,
            th_fade(th_alpha(TH_CRIMSON, 120), a));
    const char *hint = "RT / M  CLOSE";
    th_text(ren, TH_FONT_UI, 11 * u, fw * 0.5f, fh * 0.93f, th_fade(TH_TEXT_DIM, a), hint, 0.5f, 0.5f, 2 * u, TH_NONE);
    if (x1 <= x0) {
        th_text(ren, TH_FONT_UI, 16 * u, fw * 0.5f, fh * 0.5f, th_fade(TH_TEXT, a), "Nothing mapped yet", 0.5f, 0.5f,
                1 * u, TH_NONE);
        return;
    }
    float aw = fw * 0.86f, ah = fh * 0.68f;
    float s = fminf(aw / (float)(x1 - x0), ah / (float)(y1 - y0));
    float smax = fh / 9.0f / (float)WORLD_PAGE;      /* a screen at most a ninth of the height */
    if (s > smax) s = smax;
    float mx = fw * 0.5f - (float)(x1 - x0) * s * 0.5f, my = fh * 0.53f - (float)(y1 - y0) * s * 0.5f;
    float cs = (float)WORLD_PAGE * s, lw = fmaxf(1.5f, cs / 22.0f);
    SDL_Color seen = th_fade((SDL_Color){150, 150, 156, 235}, a), known = th_fade((SDL_Color){78, 78, 86, 220}, a);
    SDL_Color line = th_fade((SDL_Color){250, 250, 250, 255}, a);
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < w->n; i++) {
            const WorldRoom *r = &w->rooms[i];
            if (r->area != w->area || !r->placed) continue;
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) {
                    if (!cell(r, x, y)) continue;
                    float cx = mx + (float)(r->ox + x * WORLD_PAGE - x0) * s;
                    float cy = my + (float)(r->oy + y * WORLD_PAGE - y0) * s;
                    if (pass == 0) {
                        th_rect(ren, cx, cy, cx + cs, cy + cs, (r->seen[y] >> x) & 1 ? seen : known);
                        continue;
                    }
                    /* white along the room's outline */
                    if (!cell(r, x - 1, y)) th_rect(ren, cx, cy, cx + lw, cy + cs, line);
                    if (!cell(r, x + 1, y)) th_rect(ren, cx + cs - lw, cy, cx + cs, cy + cs, line);
                    if (!cell(r, x, y - 1)) th_rect(ren, cx, cy, cx + cs, cy + lw, line);
                    if (!cell(r, x, y + 1)) th_rect(ren, cx, cy + cs - lw, cx + cs, cy + cs, line);
                }
        }
    /* the player: a blinking yellow dot */
    float px, py;
    if (world_player(w, p, &px, &py)) {
        float sx = mx + (px - (float)x0) * s, sy = my + (py - 16.0f - (float)y0) * s;
        float r = fmaxf(5.0f * u, cs * 0.16f);
        bool lit = fmodf(t, 0.7f) < 0.46f;
        if (lit) {
            th_disc(ren, sx, sy, r * 2.0f, th_fade((SDL_Color){255, 220, 60, 50}, a), th_fade((SDL_Color){255, 200, 40, 30}, a));
            th_disc(ren, sx, sy, r, th_fade((SDL_Color){255, 240, 120, 255}, a), th_fade((SDL_Color){250, 200, 30, 255}, a));
        }
    }
}
