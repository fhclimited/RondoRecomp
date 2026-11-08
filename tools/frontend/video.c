/* See video.h. */
#include "video.h"

#include <math.h>

#include "pce/pce.h"

bool video_init(Video *v, SDL_Renderer *ren)
{
    SDL_zerop(v);
    v->ren = ren;
    v->frame = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, PCE_FB_MAX_W, PCE_FB_MAX_H);
    v->blur_a = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 64, 56);
    v->blur_b = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 16, 14);
    if (v->blur_a) SDL_SetTextureScaleMode(v->blur_a, SDL_SCALEMODE_LINEAR);
    if (v->blur_b) SDL_SetTextureScaleMode(v->blur_b, SDL_SCALEMODE_LINEAR);
    v->src_w = 256;
    v->src_h = 224;
    return v->frame != NULL;
}

void video_free(Video *v)
{
    if (v->frame) SDL_DestroyTexture(v->frame);
    if (v->sharp) SDL_DestroyTexture(v->sharp);
    if (v->blur_a) SDL_DestroyTexture(v->blur_a);
    if (v->blur_b) SDL_DestroyTexture(v->blur_b);
    SDL_zerop(v);
}

void video_upload(Video *v, const uint32_t *fb, int w, int h, int pitch, int wide)
{
    if (w <= 0 || h <= 0) return;
    SDL_Rect r = {0, 0, w, h};
    SDL_UpdateTexture(v->frame, &r, fb, pitch * 4);
    v->src_w = w;
    v->src_h = h;
    v->src_wide = wide;
}

double video_picture_aspect(const Video *v, const Settings *s)
{
    return s->aspect == ASPECT_SQUARE ? 256.0 / (double)v->src_h : 4.0 / 3.0;
}

/* Where the picture goes in a ww x wh window. */
static SDL_FRect place(const Video *v, const Settings *s, int ww, int wh)
{
    float sw = (float)v->src_w, sh = (float)v->src_h;
    if (s->aspect == ASPECT_STRETCH) return (SDL_FRect){0, 0, (float)ww, (float)wh};
    /* aspect of the whole picture: a TV's 4:3 (for the 256 pixels the game normally
     * shows; widescreen adds to the width at the same pixel shape), or square pixels */
    float base = sw - 2.0f * (float)v->src_wide;
    float ar = s->aspect == ASPECT_SQUARE ? sw / sh : 4.0f / 3.0f * sw / base;
    float dh = (float)wh, dw = dh * ar;
    if (dw > (float)ww) { dw = (float)ww; dh = dw / ar; }
    if (s->scaling == SCALE_WHOLE) {
        /* whole multiples of the picture height (and of the width for square pixels) */
        int k = (int)floorf(dh / sh);
        if (k >= 1) {
            dh = sh * (float)k;
            dw = s->aspect == ASPECT_SQUARE ? sw * (float)k : dh * ar;
        }
    }
    dw = floorf(dw);
    dh = floorf(dh);
    return (SDL_FRect){floorf(((float)ww - dw) / 2), floorf(((float)wh - dh) / 2), dw, dh};
}

/* FILTER_SHARP: blow the picture up by a whole factor with hard edges, then scale the rest
 * of the way smoothly, so pixels stay crisp but all come out the same size. */
static SDL_Texture *sharp_copy(Video *v, float dw, float dh)
{
    int k = (int)ceilf(fmaxf(dw / (float)v->src_w, dh / (float)v->src_h));
    if (k < 1) k = 1;
    while (k > 1 && (v->src_w * k > 8192 || v->src_h * k > 8192)) k--;
    int tw = v->src_w * k, th = v->src_h * k;
    if (!v->sharp || v->sharp_w != tw || v->sharp_h != th) {
        if (v->sharp) SDL_DestroyTexture(v->sharp);
        v->sharp = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, tw, th);
        if (!v->sharp) return NULL;
        SDL_SetTextureScaleMode(v->sharp, SDL_SCALEMODE_LINEAR);
        v->sharp_w = tw;
        v->sharp_h = th;
    }
    SDL_SetTextureScaleMode(v->frame, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderTarget(v->ren, v->sharp);
    SDL_FRect src = {0, 0, (float)v->src_w, (float)v->src_h};
    SDL_RenderTexture(v->ren, v->frame, &src, NULL);
    SDL_SetRenderTarget(v->ren, NULL);
    return v->sharp;
}

SDL_FRect video_draw(Video *v, const Settings *s)
{
    SDL_Renderer *ren = v->ren;
    int ww, wh;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    SDL_FRect dst = place(v, s, ww, wh);
    SDL_FRect src = {0, 0, (float)v->src_w, (float)v->src_h};

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);

    /* the sides of a wide screen: the picture itself, blurred and dimmed */
    bool covers = dst.x <= 0 && dst.y <= 0 && dst.w >= (float)ww && dst.h >= (float)wh;
    if (s->sides == SIDES_BLUR && !covers && v->blur_a && v->blur_b) {
        SDL_SetTextureScaleMode(v->frame, SDL_SCALEMODE_LINEAR);
        SDL_SetRenderTarget(ren, v->blur_a);
        SDL_RenderTexture(ren, v->frame, &src, NULL);
        SDL_SetRenderTarget(ren, v->blur_b);
        SDL_RenderTexture(ren, v->blur_a, NULL, NULL);
        SDL_SetRenderTarget(ren, NULL);
        /* cover the window keeping the picture's shape, overflowing at the edges */
        float ar = dst.w / dst.h;
        float bw = (float)ww, bh = bw / ar;
        if (bh < (float)wh) { bh = (float)wh; bw = bh * ar; }
        SDL_FRect bg = {((float)ww - bw) / 2, ((float)wh - bh) / 2, bw, bh};
        SDL_RenderTexture(ren, v->blur_b, NULL, &bg);
        ui_box(ren, (SDL_FRect){0, 0, (float)ww, (float)wh}, (SDL_Color){0, 0, 0, 120});
    }

    if (s->filter == FILTER_SHARP) {
        SDL_Texture *t = sharp_copy(v, dst.w, dst.h);
        if (t) SDL_RenderTexture(ren, t, NULL, &dst);
        else SDL_RenderTexture(ren, v->frame, &src, &dst);
    } else {
        SDL_SetTextureScaleMode(v->frame, s->filter == FILTER_SMOOTH ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(ren, v->frame, &src, &dst);
    }
    return dst;
}

/* ---- text -------------------------------------------------------------------------- */

int ui_scale(SDL_Renderer *ren)
{
    int ww, wh;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    int s = wh / 270;
    if (ww / 360 < s) s = ww / 360;
    return s < 1 ? 1 : s;
}

void ui_text(SDL_Renderer *ren, float x, float y, float scale, SDL_Color c, const char *text)
{
    SDL_SetRenderScale(ren, scale, scale);
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderDebugText(ren, x / scale, y / scale, text);
    SDL_SetRenderScale(ren, 1.0f, 1.0f);
}

void ui_box(SDL_Renderer *ren, SDL_FRect r, SDL_Color c)
{
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren, &r);
}
