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
#include "ui_fonts.h"
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

static const uint8_t *font_data[TH_FONT_COUNT];
static bool font_owned[TH_FONT_COUNT];   /* loaded from a file (freed), not built in */
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
    /* built in */
    const unsigned char *built = font == TH_FONT_TITLE ? th_font_title : font == TH_FONT_CAPS ? th_font_caps : th_font_text;
    int off0 = stbtt_GetFontOffsetForIndex(built, 0);
    if (off0 >= 0 && stbtt_InitFont(&font_info[font], built, off0)) {
        font_data[font] = built;
        font_owned[font] = false;
        font_state[font] = 1;
        return true;
    }
    /* Windows' own */
    static const char *const ui[] = {"bahnschrift.ttf", "segoeui.ttf", "arial.ttf", NULL};
    static const char *const title[] = {"palab.ttf", "georgiab.ttf", "timesbd.ttf", "bahnschrift.ttf", NULL};
    const char *const *names = font == TH_FONT_UI ? ui : title;
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
            font_owned[font] = true;
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
        if (font_owned[f]) SDL_free((void *)font_data[f]);
        font_data[f] = NULL;
        font_owned[f] = false;
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

void th_disc(SDL_Renderer *ren, float cx, float cy, float r, SDL_Color top, SDL_Color bottom)
{
    SDL_FPoint p[64];
    for (int i = 0; i < 64; i++) {
        float a = (float)i * 6.2831853f / 64.0f;
        p[i] = (SDL_FPoint){cx + cosf(a) * r, cy + sinf(a) * r};
    }
    th_poly(ren, p, 64, top, bottom, cy - r, cy + r);
}

/* A cross whose arms widen towards their ends, centred on its crossing at (cx, cy). */
void th_cross(SDL_Renderer *ren, float cx, float cy, float r, SDL_Color top, SDL_Color bottom, SDL_Color edge,
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

void th_arrow(SDL_Renderer *ren, float x, float y, float size, bool right, SDL_Color c)
{
    float d = right ? size * 0.45f : -size * 0.45f;
    SDL_FPoint p[3] = {{x - d, y - size * 0.5f}, {x + d, y}, {x - d, y + size * 0.5f}};
    th_poly(ren, p, 3, c, c, y - size, y + size);
}

static void diamond(SDL_Renderer *ren, float cx, float cy, float r, SDL_Color fill, SDL_Color edge, float edge_w)
{
    SDL_FPoint p[4] = {{cx, cy - r}, {cx + r * 0.75f, cy}, {cx, cy + r}, {cx - r * 0.75f, cy}};
    th_poly(ren, p, 4, fill, fill, cy - r, cy + r);
    if (edge.a) th_polyline(ren, p, 4, true, edge, edge_w);
}

/* A pointed arch from the left foot (x0, y0 + rise) over the apex ((x0 + x1) / 2, y0) to the
 * right foot: each half bulges upward and stands vertical at its foot. 2n + 1 points. */
static int arch_points(float x0, float x1, float y0, float rise, SDL_FPoint *out, int n)
{
    float mid = (x0 + x1) * 0.5f, half = (x1 - x0) * 0.5f;
    int k = 0;
    for (int i = 0; i <= n; i++) {
        float t = 1.0f - (float)i / (float)n;          /* 1 at the foot, 0 at the apex */
        out[k++] = (SDL_FPoint){mid - half * t, y0 + rise * (1.0f - sqrtf(1.0f - t))};
    }
    for (int i = n - 1; i >= 0; i--) {
        float t = 1.0f - (float)i / (float)n;
        out[k++] = (SDL_FPoint){mid + half * t, y0 + rise * (1.0f - sqrtf(1.0f - t))};
    }
    return k;
}

/* A pinnacle standing on (x, y): a post with a pointed cap and a crimson jewel. */
static void pinnacle(SDL_Renderer *ren, float x, float y, float u, float alpha)
{
    SDL_Color gold = th_fade(TH_GOLD, alpha), dark = th_fade((SDL_Color){60, 30, 14, 255}, alpha);
    float w = 4.0f * u, h = 20.0f * u;
    SDL_FPoint post[4] = {{x - w, y - h}, {x + w, y - h}, {x + w, y + 4 * u}, {x - w, y + 4 * u}};
    th_poly(ren, post, 4, th_fade((SDL_Color){214, 166, 80, 255}, alpha), th_fade((SDL_Color){120, 76, 30, 255}, alpha),
            y - h, y + 4 * u);
    th_polyline(ren, post, 4, true, dark, 1.0f * u);
    SDL_FPoint cap[3] = {{x - w - 2 * u, y - h}, {x, y - h - 15 * u}, {x + w + 2 * u, y - h}};
    th_poly(ren, cap, 3, th_fade((SDL_Color){250, 214, 130, 255}, alpha), gold, y - h - 15 * u, y - h);
    th_polyline(ren, cap, 3, true, dark, 1.0f * u);
    diamond(ren, x, y - h * 0.45f, 2.6f * u, th_fade(TH_CRIMSON, alpha), TH_NONE, 0);
}

void th_frame(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float rise, float u, float alpha)
{
    enum { N = 24 };
    SDL_FPoint pts[2 * N + 3], in[2 * N + 3];
    int k = arch_points(x0, x1, y0, rise, pts, N);
    pts[k++] = (SDL_FPoint){x1, y1};
    pts[k++] = (SDL_FPoint){x0, y1};
    th_poly(ren, pts, k, th_fade((SDL_Color){48, 10, 20, 244}, alpha), th_fade((SDL_Color){12, 3, 7, 248}, alpha), y0,
            y1);
    /* light through the arch, falling into the panel */
    th_rect_v(ren, x0 + 7 * u, y0 + rise, x1 - 7 * u, y0 + rise + 90 * u, th_fade(th_alpha(TH_CRIMSON, 34), alpha),
              th_alpha(TH_CRIMSON, 0));
    th_polyline(ren, pts, k, true, th_fade(th_alpha(TH_GOLD, 36), alpha), 5.0f * u);    /* glow */
    th_polyline(ren, pts, k, true, th_fade(th_alpha(TH_GOLD, 215), alpha), 1.6f * u);
    int ki = arch_points(x0 + 6 * u, x1 - 6 * u, y0 + 8 * u, rise - 2 * u, in, N);
    in[ki++] = (SDL_FPoint){x1 - 6 * u, y1 - 6 * u};
    in[ki++] = (SDL_FPoint){x0 + 6 * u, y1 - 6 * u};
    th_polyline(ren, in, ki, true, th_fade(th_alpha(TH_GOLD, 85), alpha), 1.0f * u);
    /* pinnacles at the arch's feet, a cross on its apex, jewels at the bottom corners */
    pinnacle(ren, x0, y0 + rise, u, alpha);
    pinnacle(ren, x1, y0 + rise, u, alpha);
    th_cross(ren, (x0 + x1) * 0.5f, y0 - 11 * u, 13 * u, th_fade((SDL_Color){255, 226, 150, 255}, alpha),
             th_fade((SDL_Color){176, 116, 38, 255}, alpha), th_fade((SDL_Color){70, 24, 12, 255}, alpha), 1.0f * u);
    diamond(ren, x0, y1, 6 * u, th_fade(TH_CRIMSON, alpha), th_fade(TH_GOLD, alpha), 1.2f * u);
    diamond(ren, x1, y1, 6 * u, th_fade(TH_CRIMSON, alpha), th_fade(TH_GOLD, alpha), 1.2f * u);
}

void th_divider(SDL_Renderer *ren, float x0, float x1, float y, float u, float alpha)
{
    float mid = (x0 + x1) * 0.5f, t = 0.6f * u;
    SDL_Color gold = th_fade(th_alpha(TH_GOLD, 170), alpha), clear = th_alpha(TH_GOLD, 0);
    th_rect_h(ren, x0, y - t, mid - 40 * u, y + t, clear, gold);
    th_rect_h(ren, mid + 40 * u, y - t, x1, y + t, gold, clear);
    th_rect_h(ren, x0 + 30 * u, y + 3 * u - t * 0.5f, mid - 46 * u, y + 3 * u + t * 0.5f, clear,
              th_fade(th_alpha(TH_GOLD, 80), alpha));
    th_rect_h(ren, mid + 46 * u, y + 3 * u - t * 0.5f, x1 - 30 * u, y + 3 * u + t * 0.5f,
              th_fade(th_alpha(TH_GOLD, 80), alpha), clear);
    diamond(ren, mid - 30 * u, y, 3.2f * u, th_fade(TH_GOLD, alpha), TH_NONE, 0);
    diamond(ren, mid + 30 * u, y, 3.2f * u, th_fade(TH_GOLD, alpha), TH_NONE, 0);
    diamond(ren, mid - 19 * u, y, 2.2f * u, th_fade(th_alpha(TH_GOLD, 200), alpha), TH_NONE, 0);
    diamond(ren, mid + 19 * u, y, 2.2f * u, th_fade(th_alpha(TH_GOLD, 200), alpha), TH_NONE, 0);
    diamond(ren, mid, y, 8 * u, th_fade(TH_CRIMSON, alpha), th_fade(TH_GOLD, alpha), 1.2f * u);
    th_cross(ren, mid, y - 0.6f * u, 5.5f * u, th_fade((SDL_Color){255, 226, 150, 255}, alpha),
             th_fade((SDL_Color){200, 140, 50, 255}, alpha), TH_NONE, 0);
}

void th_plaque(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float u, float alpha)
{
    float my = (y0 + y1) * 0.5f, p = (y1 - y0) * 0.5f;
    SDL_FPoint pts[6] = {{x0, my}, {x0 + p, y0}, {x1 - p, y0}, {x1, my}, {x1 - p, y1}, {x0 + p, y1}};
    th_poly(ren, pts, 6, th_fade((SDL_Color){52, 12, 22, 240}, alpha), th_fade((SDL_Color){16, 4, 9, 244}, alpha), y0,
            y1);
    th_polyline(ren, pts, 6, true, th_fade(th_alpha(TH_GOLD, 36), alpha), 4.0f * u);
    th_polyline(ren, pts, 6, true, th_fade(th_alpha(TH_GOLD, 210), alpha), 1.3f * u);
    float d = 3.5f * u;
    SDL_FPoint in[6] = {{x0 + d * 1.6f, my}, {x0 + p + d * 0.6f, y0 + d}, {x1 - p - d * 0.6f, y0 + d},
                        {x1 - d * 1.6f, my}, {x1 - p - d * 0.6f, y1 - d}, {x0 + p + d * 0.6f, y1 - d}};
    th_polyline(ren, in, 6, true, th_fade(th_alpha(TH_GOLD, 80), alpha), 1.0f * u);
}

/* ---- the emblem and the backdrop ------------------------------------------------------ */

void th_emblem(SDL_Renderer *ren, float cx, float cy, float r, float t, float alpha)
{
    const float tau = 6.2831853f;
    float k = r / 70.0f, glow = 0.5f + 0.5f * sinf(t * 1.6f);
    /* a soft crimson halo */
    for (int i = 3; i >= 1; i--)
        th_disc(ren, cx, cy, r * (1.12f + 0.14f * (float)i), th_fade(th_alpha(TH_CRIMSON, (int)(9 + 7 * glow)), alpha),
                th_fade(th_alpha(TH_CRIMSON, 3), alpha));
    /* the stone ring with gold rims and studs */
    th_disc(ren, cx, cy, r * 1.14f, th_fade((SDL_Color){70, 28, 30, 255}, alpha), th_fade((SDL_Color){24, 7, 11, 255}, alpha));
    th_arc(ren, cx, cy, r * 1.14f, 0, tau, th_fade(TH_GOLD, alpha), 1.8f * k);
    for (int i = 0; i < 16; i++) {
        float a = (float)i * tau / 16.0f;
        th_disc(ren, cx + cosf(a) * r * 1.07f, cy + sinf(a) * r * 1.07f, 1.8f * k, th_fade(TH_GOLD, alpha),
                th_fade((SDL_Color){150, 100, 40, 255}, alpha));
    }
    /* the stained glass, glowing slowly */
    th_disc(ren, cx, cy, r, th_fade((SDL_Color){(Uint8)(150 + 50 * glow), 24, 42, 255}, alpha),
            th_fade((SDL_Color){46, 8, 40, 255}, alpha));
    /* tracery: eight lobes between eight spokes, an inner ring */
    SDL_Color lead = th_fade(th_alpha(TH_GOLD, 210), alpha);
    for (int i = 0; i < 8; i++) {
        float a = (float)i * tau / 8.0f, b = a + tau / 16.0f;
        th_arc(ren, cx + cosf(b) * r * 0.66f, cy + sinf(b) * r * 0.66f, r * 0.27f, 0, tau, lead, 1.4f * k);
        th_line(ren, cx + cosf(a) * r * 0.36f, cy + sinf(a) * r * 0.36f, cx + cosf(a) * r, cy + sinf(a) * r, lead,
                1.3f * k);
    }
    th_arc(ren, cx, cy, r, 0, tau, th_fade(TH_GOLD, alpha), 2.0f * k);
    th_disc(ren, cx, cy, r * 0.36f, th_fade((SDL_Color){120, 16, 30, 255}, alpha), th_fade((SDL_Color){52, 6, 18, 255}, alpha));
    th_arc(ren, cx, cy, r * 0.36f, 0, tau, lead, 1.4f * k);
    /* the cross */
    th_cross(ren, cx, cy - r * 0.10f, r * 0.70f, th_fade(th_alpha(TH_CRIMSON, (int)(70 + 60 * glow)), alpha),
             th_fade(th_alpha(TH_CRIMSON, 20), alpha), TH_NONE, 0);
    th_cross(ren, cx, cy - r * 0.10f, r * 0.62f, th_fade((SDL_Color){255, 230, 156, 255}, alpha),
             th_fade((SDL_Color){176, 116, 38, 255}, alpha), th_fade((SDL_Color){60, 20, 10, 255}, alpha), 1.4f * k);
}

static float hash01(unsigned n)
{
    n = (n << 13) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return (float)(n & 0x7fffffff) / 2147483648.0f;
}

/* The castle's skyline along the bottom of the screen (units from the centre). */
static void castle(SDL_Renderer *ren, float cx, float bottom, float u, float t)
{
    static const struct { float x, w, h; int roof; } towers[] = {
        {-690, 46, 150, 1}, {-620, 64, 110, 0}, {-555, 38, 178, 2}, {-480, 72, 132, 0}, {-405, 50, 212, 1},
        {-338, 34, 162, 2}, {-270, 84, 118, 0}, {-190, 56, 244, 1}, {-128, 32, 186, 2}, {-55, 112, 150, 0},
        {35, 44, 276, 2}, {96, 62, 204, 1}, {168, 92, 138, 0}, {246, 46, 232, 1}, {305, 34, 172, 2},
        {376, 72, 124, 0}, {446, 50, 196, 1}, {515, 40, 152, 2}, {585, 66, 114, 0}, {656, 46, 170, 1},
    };
    SDL_Color stone = {16, 4, 9, 255};
    /* the wall and its battlements */
    th_rect(ren, cx - 820 * u, bottom - 64 * u, cx + 820 * u, bottom, stone);
    for (float x = -820; x < 820; x += 16) th_rect(ren, cx + x * u, bottom - 72 * u, cx + (x + 8) * u, bottom - 63 * u, stone);
    for (size_t i = 0; i < sizeof towers / sizeof towers[0]; i++) {
        float x = cx + towers[i].x * u, w = towers[i].w * u, top = bottom - towers[i].h * u;
        th_rect(ren, x - w * 0.5f, top, x + w * 0.5f, bottom, stone);
        if (towers[i].roof == 0) {
            for (float m = -w * 0.5f; m < w * 0.5f - 2 * u; m += 12 * u)
                th_rect(ren, x + m, top - 8 * u, x + m + 7 * u, top + 1, stone);
        } else {
            float lean = towers[i].roof == 2 ? 2.4f : 1.5f;
            SDL_FPoint roof[3] = {{x - w * 0.5f - 4 * u, top + 1}, {x, top - w * lean}, {x + w * 0.5f + 4 * u, top + 1}};
            th_poly(ren, roof, 3, stone, stone, top - w * lean, top);
            th_line(ren, x, top - w * lean, x, top - w * lean - 12 * u, stone, 1.6f * u);   /* the finial */
        }
        /* lit windows, the candles inside flickering */
        int nwin = (int)(hash01((unsigned)i * 7u + 3u) * 3.0f);
        for (int j = 0; j < nwin; j++) {
            float wy = top + (towers[i].h * u) * (0.18f + 0.22f * (float)j);
            if (wy > bottom - 70 * u) break;
            float wx = x + (hash01((unsigned)(i * 13 + j)) - 0.5f) * (w - 14 * u);
            float f = 0.75f + 0.25f * sinf(t * (5.0f + (float)j) + (float)i * 2.1f);
            SDL_Color lit = {255, 176, 84, (Uint8)(170 * f)};
            th_rect(ren, wx - 2.5f * u, wy, wx + 2.5f * u, wy + 8 * u, lit);
            th_disc(ren, wx, wy, 2.5f * u, lit, lit);
            th_disc(ren, wx, wy + 4 * u, 9 * u, th_alpha(lit, (int)(26 * f)), th_alpha(lit, 0));
        }
    }
}

/* A bat at (x, y), `s` pixels per unit, wings at `flap` (-1..1). */
static void bat(SDL_Renderer *ren, float x, float y, float s, float flap, SDL_Color c)
{
    float f = flap * 6 * s;
    SDL_FPoint l[4] = {{x - 2 * s, y}, {x - 7 * s, y - 2 * s - f}, {x - 13 * s, y - f * 1.4f + 1 * s}, {x - 6 * s, y + 2 * s}};
    SDL_FPoint r[4] = {{x + 2 * s, y}, {x + 6 * s, y + 2 * s}, {x + 13 * s, y - f * 1.4f + 1 * s}, {x + 7 * s, y - 2 * s - f}};
    th_poly(ren, l, 4, c, c, y - 10 * s, y + 10 * s);
    th_poly(ren, r, 4, c, c, y - 10 * s, y + 10 * s);
    th_disc(ren, x, y, 2.4f * s, c, c);
}

void th_backdrop(SDL_Renderer *ren, int w, int h, float t)
{
    float fw = (float)w, fh = (float)h, u = th_unit(w, h), cx = fw * 0.5f;
    /* the night sky, red towards the horizon */
    th_rect_v(ren, 0, 0, fw, fh * 0.64f, (SDL_Color){14, 3, 10, 255}, (SDL_Color){70, 13, 26, 255});
    th_rect_v(ren, 0, fh * 0.64f, fw, fh, (SDL_Color){70, 13, 26, 255}, (SDL_Color){10, 2, 5, 255});
    /* stars */
    for (unsigned i = 0; i < 110; i++) {
        float x = hash01(i * 2u + 11u) * fw, y = hash01(i * 2u + 12u) * fh * 0.58f;
        float tw = 0.55f + 0.45f * sinf(t * (0.8f + hash01(i + 400u) * 1.6f) + (float)i);
        float r = (0.6f + hash01(i + 900u) * 1.1f) * u;
        th_rect(ren, x - r * 0.5f, y - r * 0.5f, x + r * 0.5f, y + r * 0.5f,
                (SDL_Color){255, 236, 214, (Uint8)(40 + 130 * tw * hash01(i + 77u))});
    }
    /* the blood moon */
    float mx = cx - 400 * u, my = fh * 0.2f, mr = 46 * u;
    for (int i = 4; i >= 1; i--)
        th_disc(ren, mx, my, mr * (1.0f + 0.32f * (float)i), (SDL_Color){200, 40, 40, (Uint8)(14 - 2 * i)},
                (SDL_Color){200, 40, 40, (Uint8)(10 - 2 * i)});
    th_disc(ren, mx, my, mr, (SDL_Color){255, 212, 180, 255}, (SDL_Color){214, 104, 86, 255});
    th_disc(ren, mx - mr * 0.3f, my - mr * 0.2f, mr * 0.22f, (SDL_Color){190, 90, 80, 60}, (SDL_Color){190, 90, 80, 40});
    th_disc(ren, mx + mr * 0.25f, my + mr * 0.3f, mr * 0.16f, (SDL_Color){190, 90, 80, 55}, (SDL_Color){190, 90, 80, 35});
    th_disc(ren, mx + mr * 0.35f, my - mr * 0.35f, mr * 0.1f, (SDL_Color){190, 90, 80, 50}, (SDL_Color){190, 90, 80, 30});
    /* clouds drifting by: soft puffs */
    for (unsigned c = 0; c < 3; c++) {
        float span = fw + 500 * u;
        float cl = fmodf(t * (5.0f + 2.0f * (float)c) * u + hash01(c + 300u) * span, span) - 250 * u;
        float cy = fh * (0.16f + 0.14f * (float)c) + mr * 0.2f;
        for (unsigned p = 0; p < 7; p++) {
            float px = cl + ((float)p - 3.0f) * 26 * u, pr = (16 + 14 * hash01(c * 10u + p + 500u)) * u;
            float py = cy + (hash01(c * 10u + p + 600u) - 0.5f) * 10 * u;
            th_disc(ren, px, py, pr, (SDL_Color){30, 6, 14, 40}, (SDL_Color){18, 4, 9, 70});
        }
    }
    /* bats crossing */
    for (unsigned i = 0; i < 5; i++) {
        float speed = (18.0f + 14.0f * hash01(i + 31u)) * u;
        float x = fmodf(t * speed + hash01(i + 51u) * fw, fw + 160 * u) - 80 * u;
        float y = fh * (0.12f + 0.32f * hash01(i + 71u)) + sinf(t * 0.9f + (float)i) * 10 * u;
        bat(ren, x, y, (0.7f + 0.5f * hash01(i + 91u)) * u, sinf(t * 9.0f + (float)i * 1.7f), (SDL_Color){10, 2, 6, 230});
    }
    /* mist rising from below, then the castle */
    th_rect_v(ren, 0, fh * 0.6f, fw, fh * 0.86f, (SDL_Color){120, 24, 40, 0}, (SDL_Color){120, 24, 40, 46});
    castle(ren, cx, fh, u, t);
    th_rect_v(ren, 0, fh * 0.9f, fw, fh, (SDL_Color){60, 12, 22, 0}, (SDL_Color){60, 12, 22, 60});
    /* darker edges */
    SDL_Color dark = {0, 0, 0, 150}, clear = {0, 0, 0, 0};
    th_rect_h(ren, 0, 0, fw * 0.18f, fh, dark, clear);
    th_rect_h(ren, fw * 0.82f, 0, fw, fh, clear, dark);
    th_rect_v(ren, 0, 0, fw, fh * 0.14f, th_alpha(dark, 110), clear);
}
