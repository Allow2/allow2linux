/**
 * render.c -- Common rendering utilities for allow2-lock-overlay
 *
 * Implements font loading, text rendering, rounded rectangles,
 * buttons, avatars, and background fill.
 */

#include "render.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---------- Internal state ---------- */

static TTF_Font *fonts[FONT_COUNT];
static SDL_Renderer *g_renderer = NULL;

/* ---------- Helpers ---------- */

static void hsl_to_rgb(float h, float s, float l,
                       Uint8 *r, Uint8 *g, Uint8 *b) {
    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float hp = h / 60.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float r1 = 0, g1 = 0, b1 = 0;

    if (hp < 1)      { r1 = c; g1 = x; }
    else if (hp < 2) { r1 = x; g1 = c; }
    else if (hp < 3) { g1 = c; b1 = x; }
    else if (hp < 4) { g1 = x; b1 = c; }
    else if (hp < 5) { r1 = x; b1 = c; }
    else              { r1 = c; b1 = x; }

    float m = l - c / 2.0f;
    *r = (Uint8)((r1 + m) * 255);
    *g = (Uint8)((g1 + m) * 255);
    *b = (Uint8)((b1 + m) * 255);
}

static TTF_Font *load_font(const char *path, int size) {
    TTF_Font *font = TTF_OpenFont(path, size);
    if (!font) {
        fprintf(stderr, "[overlay] TTF_OpenFont(%s, %d): %s\n",
                path, size, TTF_GetError());
    }
    return font;
}

/* ---------- Public: Init / Cleanup ---------- */

int render_init(SDL_Renderer *renderer, const char *assetsPath) {
    char regular_path[512];
    char bold_path[512];
    int i, loaded = 0;

    g_renderer = renderer;

    snprintf(regular_path, sizeof(regular_path), "%s/Inter-Regular.ttf", assetsPath);
    snprintf(bold_path, sizeof(bold_path), "%s/Inter-Bold.ttf", assetsPath);

    SDL_RenderSetLogicalSize(renderer, LOGICAL_W, LOGICAL_H);

    if (TTF_Init() < 0) {
        fprintf(stderr, "[overlay] TTF_Init: %s\n", TTF_GetError());
        return -1;
    }

    (void)0; /* SDL_image removed — avatars use colored-circle fallback */

    /* Load regular fonts */
    {
        static const int sizes[] = {16, 20, 22, 28, 32, 36, 48};
        static const FontIndex idx[] = {
            FONT_REGULAR_16, FONT_REGULAR_20, FONT_REGULAR_22, FONT_REGULAR_28,
            FONT_REGULAR_32, FONT_REGULAR_36, FONT_REGULAR_48
        };
        for (i = 0; i < 7; i++) {
            fonts[idx[i]] = load_font(regular_path, sizes[i]);
            if (fonts[idx[i]]) loaded++;
        }
    }

    /* Load bold fonts */
    {
        static const int sizes[] = {16, 20, 22, 28, 32, 36, 48};
        static const FontIndex idx[] = {
            FONT_BOLD_16, FONT_BOLD_20, FONT_BOLD_22, FONT_BOLD_28,
            FONT_BOLD_32, FONT_BOLD_36, FONT_BOLD_48
        };
        for (i = 0; i < 7; i++) {
            fonts[idx[i]] = load_font(bold_path, sizes[i]);
            if (fonts[idx[i]]) loaded++;
        }
    }

    if (loaded == 0) {
        fprintf(stderr, "[overlay] WARNING: no fonts loaded from %s\n", assetsPath);
        return -1;
    }

    fprintf(stderr, "[overlay] loaded %d/%d fonts\n", loaded, FONT_COUNT);
    return 0;
}

void render_cleanup(void) {
    int i;
    for (i = 0; i < FONT_COUNT; i++) {
        if (fonts[i]) {
            TTF_CloseFont(fonts[i]);
            fonts[i] = NULL;
        }
    }
    g_renderer = NULL;
    TTF_Quit();
}

TTF_Font *render_get_font(FontIndex idx) {
    if (idx < 0 || idx >= FONT_COUNT) return NULL;
    return fonts[idx];
}

/* ---------- Public: Background ---------- */

void render_background(SDL_Renderer *renderer, int alpha) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, COLOR_BG_R, COLOR_BG_G, COLOR_BG_B,
                           (Uint8)alpha);
    SDL_Rect full = {0, 0, LOGICAL_W, LOGICAL_H};
    SDL_RenderFillRect(renderer, &full);
}

void render_get_screen_size(int *w, int *h) {
    if (w) *w = LOGICAL_W;
    if (h) *h = LOGICAL_H;
}

/* ---------- Public: Text ---------- */

void render_text(SDL_Renderer *renderer, TTF_Font *font,
                 const char *text, int x, int y, SDL_Color color) {
    SDL_Surface *surface;
    SDL_Texture *texture;

    if (!font || !text || !text[0]) return;

    surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface) return;

    texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect dst = {x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

void render_text_centered(SDL_Renderer *renderer, TTF_Font *font,
                          const char *text, int y, SDL_Color color) {
    int tw = 0, th = 0;

    if (!font || !text || !text[0]) return;

    TTF_SizeUTF8(font, text, &tw, &th);
    render_text(renderer, font, text, (LOGICAL_W - tw) / 2, y, color);
}

int render_text_width(TTF_Font *font, const char *text) {
    int tw = 0, th = 0;
    if (!font || !text || !text[0]) return 0;
    TTF_SizeUTF8(font, text, &tw, &th);
    return tw;
}

/* ---------- Public: Rectangles ---------- */

void render_filled_rect(SDL_Renderer *renderer, int x, int y, int w, int h,
                        SDL_Color color) {
    SDL_Rect rect = {x, y, w, h};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

void render_rounded_rect(SDL_Renderer *renderer, int x, int y, int w, int h,
                         int radius, SDL_Color color) {
    int dx, dy, r2;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    if (radius <= 0) {
        SDL_Rect rect = {x, y, w, h};
        SDL_RenderFillRect(renderer, &rect);
        return;
    }

    /* Fill center + side strips */
    {
        SDL_Rect rects[3];
        rects[0].x = x + radius; rects[0].y = y;
        rects[0].w = w - 2 * radius; rects[0].h = h;
        rects[1].x = x; rects[1].y = y + radius;
        rects[1].w = radius; rects[1].h = h - 2 * radius;
        rects[2].x = x + w - radius; rects[2].y = y + radius;
        rects[2].w = radius; rects[2].h = h - 2 * radius;
        SDL_RenderFillRects(renderer, rects, 3);
    }

    /* Fill corners with quarter-circles */
    r2 = radius * radius;

    /* Top-left */
    {
        int cx = x + radius, cy = y + radius;
        for (dy = -radius; dy <= 0; dy++) {
            for (dx = -radius; dx <= 0; dx++) {
                if (dx * dx + dy * dy <= r2) {
                    SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
                }
            }
        }
    }
    /* Top-right */
    {
        int cx = x + w - radius - 1, cy = y + radius;
        for (dy = -radius; dy <= 0; dy++) {
            for (dx = 0; dx <= radius; dx++) {
                if (dx * dx + dy * dy <= r2) {
                    SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
                }
            }
        }
    }
    /* Bottom-left */
    {
        int cx = x + radius, cy = y + h - radius - 1;
        for (dy = 0; dy <= radius; dy++) {
            for (dx = -radius; dx <= 0; dx++) {
                if (dx * dx + dy * dy <= r2) {
                    SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
                }
            }
        }
    }
    /* Bottom-right */
    {
        int cx = x + w - radius - 1, cy = y + h - radius - 1;
        for (dy = 0; dy <= radius; dy++) {
            for (dx = 0; dx <= radius; dx++) {
                if (dx * dx + dy * dy <= r2) {
                    SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
                }
            }
        }
    }
}

/* ---------- Public: Button ---------- */

void render_button(SDL_Renderer *renderer, const char *label,
                   int x, int y, int w, int h,
                   int highlighted, SDL_Color accentColor) {
    SDL_Color bg;
    TTF_Font *font;

    if (highlighted) {
        bg = accentColor;
    } else {
        bg.r = COLOR_BTN_R;
        bg.g = COLOR_BTN_G;
        bg.b = COLOR_BTN_B;
        bg.a = 255;
    }

    render_rounded_rect(renderer, x, y, w, h, BUTTON_RADIUS, bg);

    font = render_get_font(FONT_BOLD_22);
    if (font && label) {
        int tw = 0, th = 0;
        int tx, ty;
        SDL_Color textColor = {COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255};
        TTF_SizeUTF8(font, label, &tw, &th);
        tx = x + (w - tw) / 2;
        ty = y + (h - th) / 2;
        render_text(renderer, font, label, tx, ty, textColor);
    }
}

/* ---------- Public: Avatar ---------- */

void render_avatar(SDL_Renderer *renderer, int childId, const char *name,
                   const char *avatarPath, int x, int y, int size) {
    /* Try loading avatar image (BMP only without SDL_image) */
    if (avatarPath && avatarPath[0]) {
        SDL_Surface *surface = SDL_LoadBMP(avatarPath);
        if (surface) {
            SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
            if (texture) {
                SDL_Rect dst = {x, y, size, size};
                SDL_RenderCopy(renderer, texture, NULL, &dst);
                SDL_DestroyTexture(texture);
            }
            SDL_FreeSurface(surface);
            return;
        }
    }

    /* Colored circle with initial */
    {
        float hue = (float)((childId * 137) % 360);
        Uint8 cr, cg, cb;
        int cx, cy, r, r2, dx, dy;

        hsl_to_rgb(hue, 0.6f, 0.5f, &cr, &cg, &cb);

        cx = x + size / 2;
        cy = y + size / 2;
        r = size / 2;
        r2 = r * r;

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, cr, cg, cb, 255);

        for (dy = -r; dy <= r; dy++) {
            for (dx = -r; dx <= r; dx++) {
                if (dx * dx + dy * dy <= r2) {
                    SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
                }
            }
        }

        if (name && name[0]) {
            TTF_Font *font = render_get_font(FONT_BOLD_36);
            if (font) {
                char initial[2];
                int tw = 0, th = 0;
                SDL_Color white = {255, 255, 255, 255};

                initial[0] = name[0];
                initial[1] = '\0';
                if (initial[0] >= 'a' && initial[0] <= 'z') initial[0] -= 32;

                TTF_SizeUTF8(font, initial, &tw, &th);
                render_text(renderer, font, initial,
                            cx - tw / 2, cy - th / 2, white);
            }
        }
    }
}
