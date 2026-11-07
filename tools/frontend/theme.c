/* See theme.h. */
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wextra"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* ---- colours ------------------------------------------------------------------------ */

SDL_Color th_alpha(SDL_Color c, int a)
{
    c.a = (Uint8)(a < 0 ? 0 : a > 255 ? 255 : a);
    return c;
}

SDL_Color th_fade(SDL_Color c, float f)
{
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    c.a = (Uint8)((float)c.a * f + 0.5f);
    return c;
}

static SDL_FColor fc(SDL_Color c)
{
    return (SDL_FColor){c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}

static SDL_FColor lerp_c(SDL_Color a, SDL_Color b, float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    SDL_FColor x = fc(a), y = fc(b);
    return (SDL_FColor){x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
}

float th_unit(int w, int h)
{
    float a = (float)w / 16.0f, b = (float)h / 9.0f;
    return (a < b ? a : b) / 60.0f;
}

/* ---- fonts --------------------------------------------------------------------------- */

static uint8_t *font_data[TH_FONT_COUNT];
static stbtt_fontinfo font_info[TH_FONT_COUNT];
static int font_state[TH_FONT_COUNT];   /* 0 not tried, 1 loaded, -1 none found */

typedef struct Atlas {
    SDL_Renderer *ren;
    SDL_Texture *tex;
    int font, px, side;
    float scale, ascent;
    stbtt_packedchar ch[95];
    Uint64 used;
} Atlas;
static Atlas atlases[24];
static Uint64 use_clock;

static bool load_font(int font)
{
    if (font_state[font]) return font_state[font] > 0;
    static const char *const ui[] = {"bahnschrift.ttf", "segoeui.ttf", "arial.ttf", NULL};
    static const char *const title[] = {"palab.ttf", "georgiab.ttf", "timesbd.ttf", "bahnschrift.ttf", NULL};
    const char *const *names = font == TH_FONT_TITLE ? title : ui;
    const char *win = SDL_getenv("WINDIR");
    char path[600];
    font_state[font] = -1;
    for (int i = 0; names[i]; i++) {
        snprintf(path, sizeof path, "%s\\Fonts\\%s", win ? win : "C:\\Windows", names[i]);
        size_t len = 0;
        uint8_t *data = SDL_LoadFile(path, &len);
        if (!data) continue;
        int off = stbtt_GetFontOffsetForIndex(data, 0);
        if (off >= 0 && stbtt_InitFont(&font_info[font], data, off)) {
            font_data[font] = data;
            font_state[font] = 1;
            return true;
        }
        SDL_free(data);
    }
    return false;
}

static Atlas *atlas_for(SDL_Renderer *ren, int font, float px)
{
    if (!load_font(font)) return NULL;
    int pxi = (int)(px + 0.5f);
    if (pxi < 6) pxi = 6;
    if (pxi > 400) pxi = 400;
    Atlas *free_slot = NULL, *oldest = &atlases[0];
    for (int i = 0; i < (int)(sizeof atlases / sizeof atlases[0]); i++) {
        Atlas *a = &atlases[i];
        if (a->tex && a->ren == ren && a->font == font && a->px == pxi) {
            a->used = ++use_clock;
            return a;
        }
        if (!a->tex && !free_slot) free_slot = a;
        if (a->used < oldest->used) oldest = a;
    }
    Atlas *a = free_slot ? free_slot : oldest;
    if (a->tex) SDL_DestroyTexture(a->tex);
    memset(a, 0, sizeof *a);
    int over = pxi < 48 ? 2 : 1;
    for (int side = 256; side <= 4096; side *= 2) {
        uint8_t *bits = calloc((size_t)side * (size_t)side, 1);
        if (!bits) return NULL;
        stbtt_pack_context pc;
        bool ok = stbtt_PackBegin(&pc, bits, side, side, 0, 1, NULL) != 0;
        if (ok) {
            stbtt_PackSetOversampling(&pc, (unsigned)over, 1);
            ok = stbtt_PackFontRange(&pc, font_data[font], 0, (float)pxi, 32, 95, a->ch) != 0;
            stbtt_PackEnd(&pc);
        }
        if (ok) {
            uint8_t *rgba = malloc((size_t)side * (size_t)side * 4);
            SDL_Texture *tex = rgba ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, side, side)
                                    : NULL;
            if (tex) {
                for (int k = 0; k < side * side; k++) {
                    rgba[k * 4 + 0] = rgba[k * 4 + 1] = rgba[k * 4 + 2] = 255;
                    rgba[k * 4 + 3] = bits[k];
                }
                SDL_UpdateTexture(tex, NULL, rgba, side * 4);
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
            }
            free(rgba);
            free(bits);
            if (!tex) return NULL;
            int asc = 0, desc = 0, gap = 0;
            stbtt_GetFontVMetrics(&font_info[font], &asc, &desc, &gap);
            a->ren = ren;
            a->tex = tex;
            a->font = font;
            a->px = pxi;
            a->side = side;
            a->scale = stbtt_ScaleForPixelHeight(&font_info[font], (float)pxi);
            a->ascent = (float)asc * a->scale;
            a->used = ++use_clock;
            return a;
        }
        free(bits);
    }
    return NULL;
}

void th_free(void)
{
    for (int i = 0; i < (int)(sizeof atlases / sizeof atlases[0]); i++) {
        if (atlases[i].tex) SDL_DestroyTexture(atlases[i].tex);
        memset(&atlases[i], 0, sizeof atlases[i]);
    }
    for (int f = 0; f < TH_FONT_COUNT; f++) {
        SDL_free(font_data[f]);
        font_data[f] = NULL;
        font_state[f] = 0;
    }
}

static int glyph(unsigned char c) { return c >= 32 && c < 127 ? c - 32 : '?' - 32; }

float th_measure(SDL_Renderer *ren, int font, float px, const char *text, float tracking)
{
    if (!text || !*text) return 0;
    size_t n = strlen(text);
    Atlas *a = atlas_for(ren, font, px);
    if (!a) return (float)n * px + tracking * (float)(n - 1);       /* debug font: 8x8 cells */
    float k = px / (float)a->px, w = 0;
    for (size_t i = 0; i < n; i++) {
        w += a->ch[glyph((unsigned char)text[i])].xadvance * k;
        if (i + 1 < n)
            w += (float)stbtt_GetCodepointKernAdvance(&font_info[font], (unsigned char)text[i],
                                                      (unsigned char)text[i + 1]) * a->scale * k + tracking;
    }
    return w;
}

float th_fit(SDL_Renderer *ren, int font, float px, const char *text, float max_w, float tracking)
{
    float w = th_measure(ren, font, px, text, tracking);
    if (w <= max_w || w <= 0) return px;
    /* the letters shrink; the spacing between them stays */
    float gaps = text && *text ? tracking * (float)(strlen(text) - 1) : 0, glyphs = w - gaps;
    return glyphs > 0 && max_w > gaps ? px * (max_w - gaps) / glyphs : px * max_w / w;
}

static void draw_run(SDL_Renderer *ren, Atlas *a, int font, float px, float x, float y, SDL_Color col,
                     const char *text, float tracking)
{
    enum { MAX = 160 };
    SDL_Vertex v[MAX * 4];
    int idx[MAX * 6];
    int nv = 0, ni = 0;
    float k = px / (float)a->px;
    float cx = 0, cy = 0;                     /* pen at the atlas's size, scaled below */
    SDL_FColor c = fc(col);
    size_t n = strlen(text);
    for (size_t i = 0; i < n && nv + 4 <= MAX * 4; i++) {
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(a->ch, a->side, a->side, glyph((unsigned char)text[i]), &cx, &cy, &q, 0);
        float x0 = x + q.x0 * k, x1 = x + q.x1 * k, y0 = y + (a->ascent + q.y0) * k, y1 = y + (a->ascent + q.y1) * k;
        if (q.x1 > q.x0) {
            v[nv + 0] = (SDL_Vertex){{x0, y0}, c, {q.s0, q.t0}};
            v[nv + 1] = (SDL_Vertex){{x1, y0}, c, {q.s1, q.t0}};
            v[nv + 2] = (SDL_Vertex){{x1, y1}, c, {q.s1, q.t1}};
            v[nv + 3] = (SDL_Vertex){{x0, y1}, c, {q.s0, q.t1}};
            int b = nv;
            idx[ni++] = b; idx[ni++] = b + 1; idx[ni++] = b + 2;
            idx[ni++] = b; idx[ni++] = b + 2; idx[ni++] = b + 3;
            nv += 4;
        }
        if (i + 1 < n)
            cx += (float)stbtt_GetCodepointKernAdvance(&font_info[font], (unsigned char)text[i],
                                                       (unsigned char)text[i + 1]) * a->scale + tracking / k;
    }
    if (ni) SDL_RenderGeometry(ren, a->tex, v, nv, idx, ni);
}

static void draw_debug(SDL_Renderer *ren, float px, float x, float y, SDL_Color col, const char *text, float tracking)
{
    float s = px / 8.0f;
    SDL_SetRenderScale(ren, s, s);
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    char one[2] = {0, 0};
    for (const char *p = text; *p; p++) {
        one[0] = *p;
        SDL_RenderDebugText(ren, x / s, y / s, one);
        x += px + tracking;
    }
    SDL_SetRenderScale(ren, 1.0f, 1.0f);
}

void th_text(SDL_Renderer *ren, int font, float px, float x, float y, SDL_Color col, const char *text, float ax,
             float ay, float tracking, SDL_Color glow)
{
    if (!text || !*text || px < 1.0f || !col.a) return;
    float w = th_measure(ren, font, px, text, tracking);
    x = floorf(x - w * ax + 0.5f);
    y = floorf(y - px * ay + 0.5f);
    Atlas *a = atlas_for(ren, font, px);
    if (glow.a) {
        float r = px * 0.07f < 1.0f ? 1.0f : px * 0.07f;
        for (int i = 0; i < 8; i++) {
            float an = (float)i * 0.7854f;
            if (a) draw_run(ren, a, font, px, x + cosf(an) * r, y + sinf(an) * r, glow, text, tracking);
            else draw_debug(ren, px, x + cosf(an) * r, y + sinf(an) * r, glow, text, tracking);
        }
    }
    if (a) draw_run(ren, a, font, px, x, y, col, text, tracking);
    else draw_debug(ren, px, x, y, col, text, tracking);
}

/* ---- shapes -------------------------------------------------------------------------- */

void th_poly(SDL_Renderer *ren, const SDL_FPoint *pts, int n, SDL_Color top, SDL_Color bottom, float y0, float y1)
{
    enum { MAX = 128 };
    if (n < 3 || n > MAX) return;
    SDL_Vertex v[MAX];
    int idx[(MAX - 2) * 3], ni = 0;
    float span = y1 - y0;
    for (int i = 0; i < n; i++)
        v[i] = (SDL_Vertex){pts[i], lerp_c(top, bottom, span != 0 ? (pts[i].y - y0) / span : 0), {0, 0}};
    for (int i = 1; i + 1 < n; i++) {
        idx[ni++] = 0;
        idx[ni++] = i;
        idx[ni++] = i + 1;
    }
    SDL_RenderGeometry(ren, NULL, v, n, idx, ni);
}

static void quad(SDL_Renderer *ren, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_FColor ca,
                 SDL_FColor cb, SDL_FColor cc, SDL_FColor cd)
{
    SDL_Vertex v[4] = {{a, ca, {0, 0}}, {b, cb, {0, 0}}, {c, cc, {0, 0}}, {d, cd, {0, 0}}};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(ren, NULL, v, 4, idx, 6);
}

static void segment(SDL_Renderer *ren, float x0, float y0, float x1, float y1, SDL_Color c, float thick, float extend)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f || !c.a) return;
    dx /= len;
    dy /= len;
    x0 -= dx * extend;
    y0 -= dy * extend;
    x1 += dx * extend;
    y1 += dy * extend;
    float h = (thick < 1.0f ? 1.0f : thick) * 0.5f, nx = -dy * h, ny = dx * h;
    SDL_FColor f = fc(c);
    quad(ren, (SDL_FPoint){x0 + nx, y0 + ny}, (SDL_FPoint){x1 + nx, y1 + ny}, (SDL_FPoint){x1 - nx, y1 - ny},
         (SDL_FPoint){x0 - nx, y0 - ny}, f, f, f, f);
}

void th_line(SDL_Renderer *ren, float x0, float y0, float x1, float y1, SDL_Color c, float thick)
{
    segment(ren, x0, y0, x1, y1, c, thick, 0);
}

void th_polyline(SDL_Renderer *ren, const SDL_FPoint *pts, int n, bool closed, SDL_Color c, float thick)
{
    for (int i = 0; i + 1 < n; i++) segment(ren, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, c, thick, thick * 0.5f);
    if (closed && n > 2) segment(ren, pts[n - 1].x, pts[n - 1].y, pts[0].x, pts[0].y, c, thick, thick * 0.5f);
}

void th_rect(SDL_Renderer *ren, float x0, float y0, float x1, float y1, SDL_Color c)
{
    if (!c.a) return;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_FRect r = {x0, y0, x1 - x0, y1 - y0};
    SDL_RenderFillRect(ren, &r);
}

void th_rect_h(SDL_Renderer *ren, float x0, float y0, float x1, float y1, SDL_Color left, SDL_Color right)
{
    SDL_FColor l = fc(left), r = fc(right);
    quad(ren, (SDL_FPoint){x0, y0}, (SDL_FPoint){x1, y0}, (SDL_FPoint){x1, y1}, (SDL_FPoint){x0, y1}, l, r, r, l);
}

void th_rect_v(SDL_Renderer *ren, float x0, float y0, float x1, float y1, SDL_Color top, SDL_Color bottom)
{
    SDL_FColor t = fc(top), b = fc(bottom);
    quad(ren, (SDL_FPoint){x0, y0}, (SDL_FPoint){x1, y0}, (SDL_FPoint){x1, y1}, (SDL_FPoint){x0, y1}, t, t, b, b);
}

void th_arc(SDL_Renderer *ren, float cx, float cy, float r, float a0, float a1, SDL_Color c, float thick)
{
    int n = (int)(fabsf(a1 - a0) * r / 3.0f);
    if (n < 6) n = 6;
    if (n > 256) n = 256;
    float h = thick * 0.5f;
    SDL_FColor f = fc(c);
    for (int i = 0; i < n; i++) {
        float t0 = a0 + (a1 - a0) * (float)i / (float)n, t1 = a0 + (a1 - a0) * (float)(i + 1) / (float)n;
        float c0 = cosf(t0), s0 = sinf(t0), c1 = cosf(t1), s1 = sinf(t1);
        quad(ren, (SDL_FPoint){cx + c0 * (r - h), cy + s0 * (r - h)}, (SDL_FPoint){cx + c1 * (r - h), cy + s1 * (r - h)},
             (SDL_FPoint){cx + c1 * (r + h), cy + s1 * (r + h)}, (SDL_FPoint){cx + c0 * (r + h), cy + s0 * (r + h)}, f, f,
             f, f);
    }
}

static void chamfer(float x0, float y0, float x1, float y1, float cut, SDL_FPoint out[6])
{
    float m = (x1 - x0 < y1 - y0 ? x1 - x0 : y1 - y0) * 0.5f;
    if (cut > m) cut = m;
    out[0] = (SDL_FPoint){x0 + cut, y0};
    out[1] = (SDL_FPoint){x1, y0};
    out[2] = (SDL_FPoint){x1, y1 - cut};
    out[3] = (SDL_FPoint){x1 - cut, y1};
    out[4] = (SDL_FPoint){x0, y1};
    out[5] = (SDL_FPoint){x0, y0 + cut};
}

void th_panel(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float cut, SDL_Color top, SDL_Color bottom,
              SDL_Color border, float thick)
{
    if (x1 - x0 < 2 || y1 - y0 < 2) return;
    SDL_FPoint p[6];
    chamfer(x0, y0, x1, y1, cut, p);
    th_poly(ren, p, 6, top, bottom, y0, y1);
    if (border.a && thick > 0) {
        th_polyline(ren, p, 6, true, th_alpha(border, border.a * 40 / 255), thick * 4.0f);   /* glow */
        th_polyline(ren, p, 6, true, border, thick);
    }
}

void th_brackets(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float len, SDL_Color c, float thick)
{
    float o = thick * 2.5f;
    SDL_FPoint tr[3] = {{x1 - len, y0 - o}, {x1 + o, y0 - o}, {x1 + o, y0 + len}};
    SDL_FPoint bl[3] = {{x0 - o, y1 - len}, {x0 - o, y1 + o}, {x0 + len, y1 + o}};
    th_polyline(ren, tr, 3, false, c, thick);
    th_polyline(ren, bl, 3, false, c, thick);
}

void th_scanlines(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float step, SDL_Color c)
{
    if (step < 2) step = 2;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    for (float y = floorf(y0); y < y1; y += step) {
        SDL_FRect r = {x0, y, x1 - x0, 1.0f};
        SDL_RenderFillRect(ren, &r);
    }
}

void th_chevron(SDL_Renderer *ren, float x, float y, float size, bool right, SDL_Color c, float thick)
{
    float d = right ? size * 0.5f : -size * 0.5f;
    SDL_FPoint p[3] = {{x - d, y - size * 0.6f}, {x + d, y}, {x - d, y + size * 0.6f}};
    th_polyline(ren, p, 3, false, c, thick);
}

void th_plate(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float slant, SDL_Color fill,
              SDL_Color border, float thick)
{
    SDL_FPoint p[4] = {{x0 + slant, y0}, {x1, y0}, {x1 - slant, y1}, {x0, y1}};
    th_poly(ren, p, 4, fill, fill, y0, y1);
    if (border.a) th_polyline(ren, p, 4, true, border, thick);
}

/* ---- the emblem and the backdrop ------------------------------------------------------ */

static void disc(SDL_Renderer *ren, float cx, float cy, float r, SDL_Color top, SDL_Color bottom)
{
    SDL_FPoint p[64];
    for (int i = 0; i < 64; i++) {
        float a = (float)i * 6.2831853f / 64.0f;
        p[i] = (SDL_FPoint){cx + cosf(a) * r, cy + sinf(a) * r};
    }
    th_poly(ren, p, 64, top, bottom, cy - r, cy + r);
}

/* A cross whose arms widen towards their ends, centred on its crossing at (cx, cy). */
static void cross(SDL_Renderer *ren, float cx, float cy, float r, SDL_Color top, SDL_Color bottom, SDL_Color edge,
                  float edge_w)
{
    float w = r * 0.13f, f = r * 0.21f;     /* half widths: at the crossing, at the ends */
    float up = r * 0.70f, down = r * 1.05f, side = r * 0.62f;
    SDL_FPoint arms[4][4] = {
        {{cx - w, cy}, {cx - f, cy - up}, {cx + f, cy - up}, {cx + w, cy}},           /* up */
        {{cx - w, cy}, {cx + w, cy}, {cx + f, cy + down}, {cx - f, cy + down}},       /* down */
        {{cx, cy - w}, {cx, cy + w}, {cx - side, cy + f}, {cx - side, cy - f}},       /* left */
        {{cx, cy - w}, {cx + side, cy - f}, {cx + side, cy + f}, {cx, cy + w}},       /* right */
    };
    for (int i = 0; i < 4; i++) th_poly(ren, arms[i], 4, top, bottom, cy - up, cy + down);
    SDL_FPoint mid[4] = {{cx - w, cy - w}, {cx + w, cy - w}, {cx + w, cy + w}, {cx - w, cy + w}};
    th_poly(ren, mid, 4, top, bottom, cy - up, cy + down);
    if (edge.a) {
        SDL_FPoint outline[12] = {
            {cx - w, cy - w}, {cx - f, cy - up}, {cx + f, cy - up}, {cx + w, cy - w},
            {cx + side, cy - f}, {cx + side, cy + f}, {cx + w, cy + w}, {cx + f, cy + down},
            {cx - f, cy + down}, {cx - w, cy + w}, {cx - side, cy + f}, {cx - side, cy - f},
        };
        th_polyline(ren, outline, 12, true, edge, edge_w);
    }
}

void th_emblem(SDL_Renderer *ren, float cx, float cy, float r, float t, float alpha)
{
    float k = r / 70.0f;
    SDL_Color gold = TH_GOLD, crimson = TH_CRIMSON;
    /* the rings: a dim circle, two arcs turning opposite ways, tick marks */
    th_arc(ren, cx, cy, r * 1.30f, 0, 6.2831853f, th_fade(th_alpha(gold, 50), alpha), 1.2f * k);
    th_arc(ren, cx, cy, r * 1.30f, t * 0.6f, t * 0.6f + 1.9f, th_fade(th_alpha(gold, 220), alpha), 2.6f * k);
    th_arc(ren, cx, cy, r * 1.43f, -t * 0.4f + 3.4f, -t * 0.4f + 4.4f, th_fade(th_alpha(crimson, 220), alpha),
           2.2f * k);
    for (int i = 0; i < 48; i++) {
        float a = (float)i * 0.1309f, in = r * (i % 4 == 0 ? 1.11f : 1.16f);
        th_line(ren, cx + cosf(a) * in, cy + sinf(a) * in, cx + cosf(a) * r * 1.21f, cy + sinf(a) * r * 1.21f,
                th_fade(th_alpha(gold, 70), alpha), 1.0f * k);
    }
    /* the seal: a dark disc with a gold rim, a pulsing crimson glow, the cross */
    disc(ren, cx, cy, r, th_fade((SDL_Color){52, 12, 22, 235}, alpha), th_fade((SDL_Color){12, 3, 7, 245}, alpha));
    th_arc(ren, cx, cy, r, 0, 6.2831853f, th_fade(th_alpha(gold, 150), alpha), 1.6f * k);
    float pulse = 0.5f + 0.5f * sinf(t * 2.0f);
    cross(ren, cx, cy - r * 0.12f, r * 0.80f, th_fade(th_alpha(crimson, (int)(40 + 40 * pulse)), alpha),
          th_fade(th_alpha(crimson, 10), alpha), TH_NONE, 0);
    cross(ren, cx, cy - r * 0.12f, r * 0.72f, th_fade((SDL_Color){255, 226, 150, 255}, alpha),
          th_fade((SDL_Color){176, 116, 38, 255}, alpha), th_fade((SDL_Color){70, 24, 12, 255}, alpha), 1.4f * k);
}

void th_backdrop(SDL_Renderer *ren, int w, int h, float t)
{
    (void)t;
    float fw = (float)w, fh = (float)h, u = th_unit(w, h);
    th_rect_v(ren, 0, 0, fw, fh * 0.55f, (SDL_Color){44, 9, 18, 255}, (SDL_Color){20, 4, 9, 255});
    th_rect_v(ren, 0, fh * 0.55f, fw, fh, (SDL_Color){20, 4, 9, 255}, (SDL_Color){4, 1, 3, 255});
    /* a faint grid centred on the screen */
    float grid = 48.0f * u;
    SDL_Color g = th_alpha(TH_GOLD, 9);
    for (float x = fmodf(fw * 0.5f, grid); x < fw; x += grid) th_rect(ren, x, 0, x + 1, fh, g);
    for (float y = fmodf(fh * 0.5f, grid); y < fh; y += grid) th_rect(ren, 0, y, fw, y + 1, g);
    th_scanlines(ren, 0, 0, fw, fh, 3.0f * u < 3 ? 3 : 3.0f * u, (SDL_Color){0, 0, 0, 60});
    /* darker edges */
    SDL_Color dark = {0, 0, 0, 170}, clear = {0, 0, 0, 0};
    th_rect_h(ren, 0, 0, fw * 0.22f, fh, dark, clear);
    th_rect_h(ren, fw * 0.78f, 0, fw, fh, clear, dark);
    th_rect_v(ren, 0, 0, fw, fh * 0.18f, th_alpha(dark, 120), clear);
    th_rect_v(ren, 0, fh * 0.82f, fw, fh, clear, th_alpha(dark, 140));
}
