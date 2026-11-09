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

/* ---- ornaments ---------------------------------------------------------------------- */

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

/* A pinnacle standing on (x, y): a post with a pointed cap and a ruby. */
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
    diamond(ren, x, y - h * 0.45f, 2.6f * u, th_fade(TH_RUBY, alpha), TH_NONE, 0);
}

void th_frame(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float rise, float u, float alpha)
{
    enum { N = 24 };
    SDL_FPoint pts[2 * N + 3], in[2 * N + 3];
    int k = arch_points(x0, x1, y0, rise, pts, N);
    pts[k++] = (SDL_FPoint){x1, y1};
    pts[k++] = (SDL_FPoint){x0, y1};
    th_poly(ren, pts, k, th_fade(TH_PANEL_TOP, alpha), th_fade(TH_PANEL_BOTTOM, alpha), y0, y1);
    /* pale moonlight through the arch, falling into the panel; candlelight warming it from below */
    th_poly(ren, pts, 2 * N + 1, th_fade(th_alpha(TH_MOON, 30), alpha), th_fade(th_alpha(TH_MOON, 22), alpha), y0,
            y0 + rise);
    th_rect_v(ren, x0 + 1 * u, y0 + rise, x1 - 1 * u, y0 + rise + 120 * u, th_fade(th_alpha(TH_MOON, 22), alpha),
              th_alpha(TH_MOON, 0));
    th_rect_v(ren, x0 + 1 * u, y1 - 110 * u, x1 - 1 * u, y1, th_alpha(TH_CANDLE, 0), th_fade(th_alpha(TH_CANDLE, 18), alpha));
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
    diamond(ren, x0, y1, 6 * u, th_fade(TH_RUBY, alpha), th_fade(TH_GOLD, alpha), 1.2f * u);
    diamond(ren, x1, y1, 6 * u, th_fade(TH_RUBY, alpha), th_fade(TH_GOLD, alpha), 1.2f * u);
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
    diamond(ren, mid, y, 8 * u, th_fade(TH_RUBY, alpha), th_fade(TH_GOLD, alpha), 1.2f * u);
    th_cross(ren, mid, y - 0.6f * u, 5.5f * u, th_fade((SDL_Color){255, 226, 150, 255}, alpha),
             th_fade((SDL_Color){200, 140, 50, 255}, alpha), TH_NONE, 0);
}

void th_plaque(SDL_Renderer *ren, float x0, float y0, float x1, float y1, float u, float alpha)
{
    float my = (y0 + y1) * 0.5f, p = (y1 - y0) * 0.5f;
    SDL_FPoint pts[6] = {{x0, my}, {x0 + p, y0}, {x1 - p, y0}, {x1, my}, {x1 - p, y1}, {x0 + p, y1}};
    th_poly(ren, pts, 6, th_fade(TH_PANEL_TOP, alpha), th_fade(TH_PANEL_BOTTOM, alpha), y0, y1);
    th_polyline(ren, pts, 6, true, th_fade(th_alpha(TH_GOLD, 36), alpha), 4.0f * u);
    th_polyline(ren, pts, 6, true, th_fade(th_alpha(TH_GOLD, 210), alpha), 1.3f * u);
    float d = 3.5f * u;
    SDL_FPoint in[6] = {{x0 + d * 1.6f, my}, {x0 + p + d * 0.6f, y0 + d}, {x1 - p - d * 0.6f, y0 + d},
                        {x1 - d * 1.6f, my}, {x1 - p - d * 0.6f, y1 - d}, {x0 + p + d * 0.6f, y1 - d}};
    th_polyline(ren, in, 6, true, th_fade(th_alpha(TH_GOLD, 80), alpha), 1.0f * u);
}

/* ---- the emblem and the backdrop ------------------------------------------------------ */

static float hash01(unsigned n)
{
    n = (n << 13) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return (float)(n & 0x7fffffff) / 2147483648.0f;
}

/* A candle's flicker, 0.6-1, different for every seed. */
static float flicker(float t, unsigned seed)
{
    float a = 4.0f + 3.0f * hash01(seed * 3u + 1u), b = 9.0f + 5.0f * hash01(seed * 3u + 2u);
    return 0.8f + 0.12f * sinf(t * a + (float)seed) + 0.08f * sinf(t * b + 1.7f * (float)seed);
}

/* A soft round light (rx by ry): `inner` at the centre, fading to nothing at the rim, about
 * as light falls off. Added to what is there (`add`: light) or blended over it (haze). */
static void glow(SDL_Renderer *ren, float cx, float cy, float rx, float ry, SDL_Color inner, bool add)
{
    enum { N = 36 };
    if (!inner.a || rx < 0.5f || ry < 0.5f) return;
    SDL_Vertex v[1 + 2 * N];
    int idx[N * 9], ni = 0;
    SDL_FColor c = fc(inner), m = c, e = c;
    m.a *= 0.42f;
    e.a = 0;
    v[0] = (SDL_Vertex){{cx, cy}, c, {0, 0}};
    for (int i = 0; i < N; i++) {
        float a = (float)i * 6.2831853f / (float)N, ca = cosf(a), sa = sinf(a);
        v[1 + i] = (SDL_Vertex){{cx + ca * rx * 0.3f, cy + sa * ry * 0.3f}, m, {0, 0}};
        v[1 + N + i] = (SDL_Vertex){{cx + ca * rx, cy + sa * ry}, e, {0, 0}};
    }
    for (int i = 0; i < N; i++) {
        int j = (i + 1) % N;
        idx[ni++] = 0; idx[ni++] = 1 + i; idx[ni++] = 1 + j;
        idx[ni++] = 1 + i; idx[ni++] = 1 + N + i; idx[ni++] = 1 + N + j;
        idx[ni++] = 1 + i; idx[ni++] = 1 + N + j; idx[ni++] = 1 + j;
    }
    SDL_SetRenderDrawBlendMode(ren, add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(ren, NULL, v, 1 + 2 * N, idx, ni);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
}

void th_emblem(SDL_Renderer *ren, float cx, float cy, float r, float t, float alpha)
{
    const float tau = 6.2831853f;
    float k = r / 70.0f, f = flicker(t * 0.6f, 5u);
    /* candlelight through the glass, warming the wall around it */
    glow(ren, cx, cy, r * 2.1f, r * 2.1f, th_fade((SDL_Color){255, 160, 70, (Uint8)(34 * f)}, alpha), true);
    /* the stone ring with gold rims and studs */
    th_disc(ren, cx, cy, r * 1.14f, th_fade((SDL_Color){70, 62, 60, 255}, alpha), th_fade((SDL_Color){24, 21, 23, 255}, alpha));
    th_arc(ren, cx, cy, r * 1.14f, 0, tau, th_fade(TH_GOLD, alpha), 1.8f * k);
    for (int i = 0; i < 16; i++) {
        float a = (float)i * tau / 16.0f;
        th_disc(ren, cx + cosf(a) * r * 1.07f, cy + sinf(a) * r * 1.07f, 1.8f * k, th_fade(TH_GOLD, alpha),
                th_fade((SDL_Color){150, 100, 40, 255}, alpha));
    }
    /* the stained glass: blues and indigos, lit from behind by the candle, warmest at the middle */
    th_disc(ren, cx, cy, r, th_fade((SDL_Color){30, 44, 92, 255}, alpha), th_fade((SDL_Color){12, 16, 42, 255}, alpha));
    for (int i = 0; i < 8; i++) {
        float b = (float)i * tau / 8.0f + tau / 16.0f, lx = cx + cosf(b) * r * 0.66f, ly = cy + sinf(b) * r * 0.66f;
        if (i & 1)
            th_disc(ren, lx, ly, r * 0.27f, th_fade((SDL_Color){62, 92, 158, 255}, alpha),
                    th_fade((SDL_Color){20, 30, 78, 255}, alpha));
        else
            th_disc(ren, lx, ly, r * 0.27f, th_fade((SDL_Color){76, 70, 150, 255}, alpha),
                    th_fade((SDL_Color){26, 22, 74, 255}, alpha));
    }
    glow(ren, cx, cy, r * 1.05f, r * 1.05f, th_fade((SDL_Color){255, 180, 96, (Uint8)(96 * f)}, alpha), true);
    /* tracery: eight lobes between eight spokes, an inner ring */
    SDL_Color lead = th_fade(th_alpha(TH_GOLD, 210), alpha);
    for (int i = 0; i < 8; i++) {
        float a = (float)i * tau / 8.0f, b = a + tau / 16.0f;
        th_arc(ren, cx + cosf(b) * r * 0.66f, cy + sinf(b) * r * 0.66f, r * 0.27f, 0, tau, lead, 1.4f * k);
        th_line(ren, cx + cosf(a) * r * 0.36f, cy + sinf(a) * r * 0.36f, cx + cosf(a) * r, cy + sinf(a) * r, lead,
                1.3f * k);
    }
    th_arc(ren, cx, cy, r, 0, tau, th_fade(TH_GOLD, alpha), 2.0f * k);
    th_disc(ren, cx, cy, r * 0.36f, th_fade((SDL_Color){(Uint8)(214 + 30 * f), 150, 70, 255}, alpha),
            th_fade((SDL_Color){140, 72, 22, 255}, alpha));
    th_arc(ren, cx, cy, r * 0.36f, 0, tau, lead, 1.4f * k);
    /* the cross, a warm light about it */
    glow(ren, cx, cy - r * 0.10f, r * 0.75f, r * 0.75f, th_fade((SDL_Color){255, 200, 120, (Uint8)(60 * f)}, alpha), true);
    th_cross(ren, cx, cy - r * 0.10f, r * 0.62f, th_fade((SDL_Color){255, 232, 164, 255}, alpha),
             th_fade((SDL_Color){176, 116, 38, 255}, alpha), th_fade((SDL_Color){60, 30, 10, 255}, alpha), 1.4f * k);
}

/* The moon lights the town from the upper left. */
#define MOONLIT ((SDL_Color){168, 184, 220, 255})

/* A lit window (w x h) at (x, y), its top left: candlelight with a cross of glazing bars and
 * a glow about it; unlit, a faint pane. `bright` scales the light (the far row is dimmer). */
static void window_at(SDL_Renderer *ren, float x, float y, float w, float h, bool lit, float f, float bright, float u)
{
    if (!lit) {
        th_rect(ren, x, y, x + w, y + h, (SDL_Color){30, 32, 48, (Uint8)(200 * bright)});
        return;
    }
    glow(ren, x + w * 0.5f, y + h * 0.5f, w * 2.6f, h * 1.9f, (SDL_Color){255, 150, 60, (Uint8)(52 * f * bright)}, true);
    th_rect_v(ren, x, y, x + w, y + h, (SDL_Color){255, 214, 140, (Uint8)(235 * f * bright)},
              (SDL_Color){240, 140, 56, (Uint8)(235 * f * bright)});
    if (w > 4 * u) {
        SDL_Color bar = {40, 22, 12, (Uint8)(220 * bright)};
        th_rect(ren, x + w * 0.5f - 0.5f * u, y, x + w * 0.5f + 0.5f * u, y + h, bar);
        th_rect(ren, x, y + h * 0.45f - 0.5f * u, x + w, y + h * 0.45f + 0.5f * u, bar);
    }
}

/* A house of the town from x0 to x1, its walls from `top` down to `ground`: a gable roof
 * (roof 0), a steep one (1) or a flat one behind a parapet (2), perhaps a chimney, and
 * windows, some lit by candles. far: the paler row behind, with fewer, dimmer lights. */
static void house(SDL_Renderer *ren, float x0, float x1, float top, float ground, int roof, unsigned seed, float u,
                  float t, bool far)
{
    SDL_Color wall = far ? (SDL_Color){24, 27, 46, 255} : (SDL_Color){10, 10, 18, 255};
    SDL_Color rim = th_alpha(MOONLIT, far ? 60 : 110);
    float w = x1 - x0, mid = (x0 + x1) * 0.5f, pitch = 0;
    th_rect(ren, x0, top, x1, ground, wall);
    if (roof == 2) {
        th_rect(ren, x0 - 2 * u, top - 5 * u, x1 + 2 * u, top + 1, wall);
        th_line(ren, x0 - 2 * u, top - 5 * u, x1 + 2 * u, top - 5 * u, th_alpha(rim, rim.a / 2), 1.0f * u);
    } else {
        pitch = roof == 1 ? 1.3f : 0.55f + 0.3f * hash01(seed + 5u);
        float apex = top - (w * 0.5f + 3 * u) * pitch;
        SDL_FPoint p[3] = {{x0 - 3 * u, top + 1}, {mid, apex}, {x1 + 3 * u, top + 1}};
        th_poly(ren, p, 3, wall, wall, apex, top);
        th_line(ren, x0 - 3 * u, top, mid, apex, rim, 1.0f * u);          /* the moonlit slope */
    }
    if (hash01(seed + 9u) < 0.55f) {                                     /* a chimney */
        float c = x0 + w * (0.22f + 0.56f * hash01(seed + 10u)), cw = (far ? 4.0f : 6.0f) * u;
        float at = roof == 2 ? top - 5 * u : top - (w * 0.5f + 3 * u - fabsf(c - mid)) * pitch;
        float ct = at - (8 + 8 * hash01(seed + 11u)) * u;
        th_rect(ren, c - cw * 0.5f, ct, c + cw * 0.5f, at + 2 * u, wall);
        th_rect(ren, c - cw * 0.5f - 1 * u, ct - 2 * u, c + cw * 0.5f + 1 * u, ct, wall);
        th_line(ren, c - cw * 0.5f - 1 * u, ct - 2 * u, c - cw * 0.5f - 1 * u, at, th_alpha(rim, rim.a / 2), 1.0f * u);
    }
    /* windows */
    float ww = (far ? 3.5f : 6.0f) * u, wh = (far ? 5.0f : 9.0f) * u, gx = (far ? 13.0f : 20.0f) * u,
          gy = (far ? 15.0f : 24.0f) * u;
    int cols = (int)((w - 6 * u) / gx), rows = (int)((ground - top - 8 * u) / gy);
    if (cols > 4) cols = 4;
    if (rows > 4) rows = 4;
    float left = mid - (float)(cols - 1) * gx * 0.5f - ww * 0.5f;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            unsigned s = seed * 17u + (unsigned)(r * 5 + c);
            bool lit = hash01(s + 101u) < (far ? 0.32f : 0.5f);
            window_at(ren, left + (float)c * gx, top + 8 * u + (float)r * gy, ww, wh, lit, flicker(t, s),
                      far ? 0.55f : 1.0f, u);
        }
}

/* A street lantern on a post standing on `ground` at x, with its pool of warm light. */
static void lantern(SDL_Renderer *ren, float x, float ground, float u, float t, unsigned seed)
{
    SDL_Color iron = {8, 8, 14, 255};
    float f = flicker(t, seed + 900u), top = ground - 46 * u;
    glow(ren, x, top - 4 * u, 34 * u, 30 * u, (SDL_Color){255, 150, 60, (Uint8)(70 * f)}, true);
    th_rect(ren, x - 1.0f * u, top, x + 1.0f * u, ground, iron);
    th_rect(ren, x - 2.5f * u, top - 1 * u, x + 2.5f * u, top + 1 * u, iron);
    th_rect_v(ren, x - 3 * u, top - 9 * u, x + 3 * u, top - 1 * u, (SDL_Color){255, 222, 150, (Uint8)(250 * f)},
              (SDL_Color){250, 150, 60, (Uint8)(250 * f)});
    SDL_FPoint cap[3] = {{x - 4.5f * u, top - 9 * u}, {x, top - 14 * u}, {x + 4.5f * u, top - 9 * u}};
    th_poly(ren, cap, 3, iron, iron, top - 14 * u, top - 9 * u);
    glow(ren, x, top - 5 * u, 8 * u, 8 * u, (SDL_Color){255, 230, 170, (Uint8)(120 * f)}, true);
}

/* The church on the left: a tower with a spire and a cross, a lit lancet window, a nave. */
static void church(SDL_Renderer *ren, float x, float ground, float u, float t)
{
    SDL_Color wall = {10, 10, 18, 255}, rim = th_alpha(MOONLIT, 110);
    float tw = 34 * u, top = ground - 150 * u, spire = top - 104 * u;
    th_rect(ren, x + tw * 0.5f - 2 * u, ground - 86 * u, x + tw * 0.5f + 70 * u, ground, wall);          /* nave */
    SDL_FPoint nave[3] = {{x + tw * 0.5f - 2 * u, ground - 86 * u}, {x + tw * 0.5f + 34 * u, ground - 112 * u},
                          {x + tw * 0.5f + 70 * u, ground - 86 * u}};
    th_poly(ren, nave, 3, wall, wall, ground - 112 * u, ground - 86 * u);
    th_line(ren, nave[0].x, nave[0].y, nave[1].x, nave[1].y, rim, 1.0f * u);
    th_rect(ren, x - tw * 0.5f, top, x + tw * 0.5f, ground, wall);                                       /* tower */
    th_rect(ren, x - tw * 0.5f - 3 * u, top - 4 * u, x + tw * 0.5f + 3 * u, top + 1, wall);
    SDL_FPoint sp[3] = {{x - tw * 0.5f + 1 * u, top - 3 * u}, {x, spire}, {x + tw * 0.5f - 1 * u, top - 3 * u}};
    th_poly(ren, sp, 3, wall, wall, spire, top);
    th_line(ren, sp[0].x, sp[0].y, x, spire, rim, 1.0f * u);
    th_line(ren, x - tw * 0.5f - 3 * u, top - 4 * u, x - tw * 0.5f - 3 * u, ground, th_alpha(rim, 50), 1.0f * u);
    th_rect(ren, x - 0.8f * u, spire - 14 * u, x + 0.8f * u, spire + 1, wall);                           /* cross */
    th_rect(ren, x - 4.5f * u, spire - 10.5f * u, x + 4.5f * u, spire - 9 * u, wall);
    /* the belfry's lancet, lit, and the nave's windows */
    float f = flicker(t * 0.7f, 77u);
    glow(ren, x, top + 30 * u, 26 * u, 34 * u, (SDL_Color){255, 150, 60, (Uint8)(60 * f)}, true);
    SDL_FPoint lancet[5] = {{x - 5 * u, top + 42 * u}, {x - 5 * u, top + 24 * u}, {x, top + 15 * u},
                            {x + 5 * u, top + 24 * u}, {x + 5 * u, top + 42 * u}};
    th_poly(ren, lancet, 5, (SDL_Color){255, 216, 140, (Uint8)(240 * f)}, (SDL_Color){236, 130, 50, (Uint8)(240 * f)},
            top + 15 * u, top + 42 * u);
    th_rect(ren, x - 0.5f * u, top + 18 * u, x + 0.5f * u, top + 42 * u, (SDL_Color){40, 22, 12, 220});
    for (int i = 0; i < 3; i++)
        window_at(ren, x + tw * 0.5f + (12 + 20 * (float)i) * u, ground - 70 * u, 6 * u, 16 * u, true,
                  flicker(t, 80u + (unsigned)i), 0.9f, u);
}

/* The castle on its hill, far off behind the town (pale with distance). */
static void castle(SDL_Renderer *ren, float cx, float ground, float u, float t)
{
    static const struct { float x, w, h; int roof; } towers[] = {
        {-120, 30, 150, 1}, {-86, 52, 112, 0}, {-46, 26, 186, 2}, {-6, 70, 128, 0},
        {38, 34, 214, 1}, {78, 46, 140, 0}, {116, 24, 170, 2}, {150, 40, 104, 0},
    };
    SDL_Color stone = {26, 30, 52, 255}, rim = th_alpha(MOONLIT, 50);
    /* the hill */
    SDL_FPoint hill[34];
    int n = 0;
    for (int i = 0; i <= 31; i++) {
        float x = -420.0f + 840.0f * (float)i / 31.0f, d = x / 260.0f;
        hill[n++] = (SDL_FPoint){cx + x * u, ground - (14.0f + 92.0f * expf(-d * d)) * u};
    }
    hill[n++] = (SDL_FPoint){cx + 420 * u, ground};
    hill[n++] = (SDL_FPoint){cx - 420 * u, ground};
    th_poly(ren, hill, n, (SDL_Color){30, 34, 58, 255}, stone, ground - 106 * u, ground);
    float base = ground - 96 * u;
    th_rect(ren, cx - 140 * u, base - 40 * u, cx + 176 * u, base + 10 * u, stone);
    for (size_t i = 0; i < sizeof towers / sizeof towers[0]; i++) {
        float x = cx + towers[i].x * u, w = towers[i].w * u, top = base - towers[i].h * u * 0.62f;
        th_rect(ren, x - w * 0.5f, top, x + w * 0.5f, base, stone);
        if (towers[i].roof == 0) {
            for (float m = -w * 0.5f; m < w * 0.5f - 2 * u; m += 9 * u)
                th_rect(ren, x + m, top - 5 * u, x + m + 5 * u, top + 1, stone);
        } else {
            float lean = towers[i].roof == 2 ? 2.2f : 1.4f;
            SDL_FPoint roof[3] = {{x - w * 0.5f - 3 * u, top + 1}, {x, top - w * lean}, {x + w * 0.5f + 3 * u, top + 1}};
            th_poly(ren, roof, 3, stone, stone, top - w * lean, top);
            th_line(ren, roof[0].x, roof[0].y, x, top - w * lean, rim, 1.0f * u);
        }
        if (hash01((unsigned)i + 40u) < 0.5f)
            window_at(ren, x - 1.5f * u, top + 12 * u, 3 * u, 5 * u, true, flicker(t, 60u + (unsigned)i), 0.5f, u);
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
    /* the night sky: deep blue, a little warmer low down where the town's lights reach */
    th_rect_v(ren, 0, 0, fw, fh * 0.5f, (SDL_Color){4, 6, 16, 255}, (SDL_Color){14, 20, 42, 255});
    th_rect_v(ren, 0, fh * 0.5f, fw, fh * 0.8f, (SDL_Color){14, 20, 42, 255}, (SDL_Color){38, 34, 52, 255});
    th_rect_v(ren, 0, fh * 0.8f, fw, fh, (SDL_Color){38, 34, 52, 255}, (SDL_Color){12, 11, 18, 255});
    /* the pale moon, high on the left */
    float mx = cx - 390 * u, my = fh * 0.15f, mr = 36 * u;
    /* stars, fewer near the moon */
    for (unsigned i = 0; i < 140; i++) {
        float x = hash01(i * 2u + 11u) * fw, y = hash01(i * 2u + 12u) * fh * 0.6f;
        float d = sqrtf((x - mx) * (x - mx) + (y - my) * (y - my)) / mr;
        if (d < 4.0f) continue;
        float tw = 0.55f + 0.45f * sinf(t * (0.8f + hash01(i + 400u) * 1.6f) + (float)i);
        float r = (0.6f + hash01(i + 900u) * 1.1f) * u, fade = d < 8.0f ? (d - 4.0f) / 4.0f : 1.0f;
        th_rect(ren, x - r * 0.5f, y - r * 0.5f, x + r * 0.5f, y + r * 0.5f,
                (SDL_Color){222, 230, 255, (Uint8)((30 + 140 * tw * hash01(i + 77u)) * fade)});
    }
    glow(ren, mx, my, mr * 10, mr * 10, (SDL_Color){120, 140, 200, 64}, true);
    glow(ren, mx, my, mr * 3.6f, mr * 3.6f, (SDL_Color){200, 214, 250, 120}, true);
    th_disc(ren, mx, my, mr, (SDL_Color){250, 250, 242, 255}, (SDL_Color){206, 214, 232, 255});
    th_disc(ren, mx - mr * 0.3f, my - mr * 0.2f, mr * 0.24f, (SDL_Color){168, 176, 200, 70}, (SDL_Color){168, 176, 200, 50});
    th_disc(ren, mx + mr * 0.22f, my + mr * 0.32f, mr * 0.17f, (SDL_Color){168, 176, 200, 64}, (SDL_Color){168, 176, 200, 44});
    th_disc(ren, mx + mr * 0.38f, my - mr * 0.3f, mr * 0.11f, (SDL_Color){168, 176, 200, 58}, (SDL_Color){168, 176, 200, 38});
    th_disc(ren, mx - mr * 0.05f, my + mr * 0.05f, mr * 0.08f, (SDL_Color){168, 176, 200, 50}, (SDL_Color){168, 176, 200, 30});
    /* moonbeams slanting down onto the town: soft shafts, feathered at their sides, coming
     * in out of the moon's glow and fading as they fall, slowly brightening and dimming */
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
    for (unsigned i = 0; i < 5; i++) {
        float ang = 0.92f + 0.11f * (float)i + 0.05f * (hash01(i + 150u) - 0.5f), dx = cosf(ang), dy = sinf(ang);
        float off = ((float)i - 2.0f) * mr * 0.7f, len = fh * (0.95f + 0.3f * hash01(i + 190u)), in = mr * 3.0f;
        float s0 = mr * (0.6f + 0.4f * hash01(i + 160u)), s1 = (60 + 70 * hash01(i + 170u)) * u;
        float sm = s0 + (s1 - s0) * in / len;
        float a0 = (22 + 12 * hash01(i + 180u)) * (0.55f + 0.45f * sinf(t * 0.21f + (float)i * 1.9f));
        float ox = mx - dy * off, oy = my + dx * off, nx = -dy, ny = dx;
        float px = ox + dx * in, py = oy + dy * in, ex = ox + dx * len, ey = oy + dy * len;
        SDL_FColor lit = fc((SDL_Color){170, 190, 236, (Uint8)a0}), none = lit;
        none.a = 0;
        /* three cross-sections (where it starts, at its brightest, where it ends), each dark
         * at the sides and lit in the middle; lit only at the brightest one */
        SDL_FPoint s[3][3] = {
            {{ox + nx * s0, oy + ny * s0}, {ox, oy}, {ox - nx * s0, oy - ny * s0}},
            {{px + nx * sm, py + ny * sm}, {px, py}, {px - nx * sm, py - ny * sm}},
            {{ex + nx * s1, ey + ny * s1}, {ex, ey}, {ex - nx * s1, ey - ny * s1}},
        };
        for (int k = 0; k < 2; k++) {
            SDL_FColor c0 = k == 1 ? lit : none, c1 = k == 0 ? lit : none;
            quad(ren, s[k][0], s[k][1], s[k + 1][1], s[k + 1][0], none, c0, c1, none);
            quad(ren, s[k][1], s[k][2], s[k + 1][2], s[k + 1][1], c0, none, none, c1);
        }
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    /* thin clouds drifting past, silvered near the moon */
    for (unsigned c = 0; c < 4; c++) {
        float span = fw + 600 * u;
        float cl = fmodf(t * (3.0f + 1.6f * (float)c) * u + hash01(c + 300u) * span, span) - 300 * u;
        float cy = fh * (0.1f + 0.1f * (float)c) + mr * 0.3f;
        for (unsigned p = 0; p < 6; p++) {
            float px = cl + ((float)p - 2.5f) * 46 * u, py = cy + (hash01(c * 10u + p + 600u) - 0.5f) * 12 * u;
            float rx = (50 + 50 * hash01(c * 10u + p + 500u)) * u, ry = (7 + 6 * hash01(c * 10u + p + 550u)) * u;
            float d = sqrtf((px - mx) * (px - mx) + (py - my) * (py - my)) / (mr * 6), lit = d < 1 ? 1 - d : 0;
            SDL_Color col = {(Uint8)(34 + 120 * lit), (Uint8)(40 + 124 * lit), (Uint8)(66 + 130 * lit), (Uint8)(70 + 40 * lit)};
            glow(ren, px, py, rx, ry, col, false);
        }
    }
    /* bats crossing */
    for (unsigned i = 0; i < 3; i++) {
        float speed = (16.0f + 12.0f * hash01(i + 31u)) * u;
        float x = fmodf(t * speed + hash01(i + 51u) * fw, fw + 160 * u) - 80 * u;
        float y = fh * (0.1f + 0.28f * hash01(i + 71u)) + sinf(t * 0.9f + (float)i) * 10 * u;
        bat(ren, x, y, (0.6f + 0.4f * hash01(i + 91u)) * u, sinf(t * 9.0f + (float)i * 1.7f), (SDL_Color){6, 7, 16, 230});
    }
    /* far off: the castle on its hill and the town's rear rows, in a cool mist */
    castle(ren, cx - 150 * u, fh - 40 * u, u, t);
    {
        float x = -900;
        for (unsigned i = 0; x < 900; i++) {
            float bw = 30 + 34 * hash01(i * 3u + 1001u), bh = 58 + 46 * hash01(i * 3u + 1002u);
            int roof = hash01(i * 3u + 1003u) < 0.85f ? 0 : 2;
            house(ren, cx + x * u, cx + (x + bw) * u, fh - bh * u, fh, roof, i * 31u + 2000u, u, t, true);
            x += bw + 3 + 10 * hash01(i + 1300u);
        }
    }
    th_rect_v(ren, 0, fh * 0.66f, fw, fh * 0.86f, (SDL_Color){110, 126, 170, 0}, (SDL_Color){110, 126, 170, 34});
    /* the town's warm glow rising into the night */
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
    th_rect_v(ren, 0, fh * 0.6f, fw, fh * 0.9f, (SDL_Color){255, 140, 50, 0}, (SDL_Color){255, 140, 50, 34});
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    /* the near row of houses, the church, lanterns along the street */
    {
        float x = -900;
        for (unsigned i = 0; x < 900; i++) {
            float bw = 42 + 46 * hash01(i * 3u + 1u), bh = 40 + 52 * hash01(i * 3u + 2u);
            float r = hash01(i * 3u + 3u);
            if (x + bw > -490 && x < -370) {                     /* room for the church */
                church(ren, cx - 430 * u, fh, u, t);
                x = -370 + 22;
                continue;
            }
            house(ren, cx + x * u, cx + (x + bw) * u, fh - bh * u, fh, r < 0.6f ? 0 : r < 0.82f ? 1 : 2, i * 31u + 7u, u,
                  t, false);
            if (hash01(i + 700u) < 0.45f) lantern(ren, cx + (x + bw + 6) * u, fh, u, t, i);
            x += bw + 12 + 8 * hash01(i + 300u);
        }
    }
    /* the street in shadow, darker edges */
    th_rect_v(ren, 0, fh * 0.93f, fw, fh, (SDL_Color){4, 4, 10, 0}, (SDL_Color){4, 4, 10, 170});
    SDL_Color dark = {0, 0, 6, 150}, clear = {0, 0, 6, 0};
    th_rect_h(ren, 0, 0, fw * 0.16f, fh, dark, clear);
    th_rect_h(ren, fw * 0.84f, 0, fw, fh, clear, dark);
    th_rect_v(ren, 0, 0, fw, fh * 0.12f, th_alpha(dark, 90), clear);
}
