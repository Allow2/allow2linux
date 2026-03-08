/**
 * render.h -- Common rendering utilities for allow2-lock-overlay
 *
 * Provides font management, text rendering, styled buttons, avatars,
 * and background fills. All coordinates are in logical 1280x800 space.
 */

#ifndef RENDER_H
#define RENDER_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/* ---- Color constants (from design spec) ---- */

#define COLOR_BG_R       20
#define COLOR_BG_G       20
#define COLOR_BG_B       30
#define COLOR_BG_A      242   /* 0.95 * 255 */

#define COLOR_TEXT_R     255   /* Primary text (white) */
#define COLOR_TEXT_G     255
#define COLOR_TEXT_B     255

#define COLOR_TEXT2_R    160   /* Secondary text (light gray) */
#define COLOR_TEXT2_G    160
#define COLOR_TEXT2_B    176

#define COLOR_ACCENT_R   102  /* Accent blue */
#define COLOR_ACCENT_G   126
#define COLOR_ACCENT_B   234

#define COLOR_BTN_R       45  /* Button background (dark blue) */
#define COLOR_BTN_G       55
#define COLOR_BTN_B       72

#define COLOR_DIV_R       45  /* Divider (subtle gray) */
#define COLOR_DIV_G       45
#define COLOR_DIV_B       61

/* ---- Logical screen dimensions ---- */

#define LOGICAL_W  1280
#define LOGICAL_H   800

/* ---- Layout constants ---- */

#define PADDING_MAJOR    24
#define PADDING_MINOR    12
#define AVATAR_SIZE      64
#define BUTTON_RADIUS     8
#define SELECTOR_ROW_H  100

/* ---- Font index enum ---- */

typedef enum {
    FONT_REGULAR_16 = 0,
    FONT_REGULAR_20,
    FONT_REGULAR_22,
    FONT_REGULAR_28,
    FONT_REGULAR_32,
    FONT_REGULAR_36,
    FONT_REGULAR_48,
    FONT_BOLD_16,
    FONT_BOLD_20,
    FONT_BOLD_22,
    FONT_BOLD_28,
    FONT_BOLD_32,
    FONT_BOLD_36,
    FONT_BOLD_48,
    FONT_COUNT
} FontIndex;

/* ---- Initialization / Cleanup ---- */

/* Load fonts from assets directory. Sets up logical rendering.
 * Returns 0 on success, -1 on error. */
int render_init(SDL_Renderer *renderer, const char *assetsPath);

/* Free all font and image resources. */
void render_cleanup(void);

/* ---- Font access ---- */

/* Get a preloaded font by index. Returns NULL if not loaded. */
TTF_Font *render_get_font(FontIndex idx);

/* ---- Drawing primitives ---- */

/* Fill the entire screen with the standard dark background at given alpha. */
void render_background(SDL_Renderer *renderer, int alpha);

/* Get logical screen size (always 1280x800). */
void render_get_screen_size(int *w, int *h);

/* Render text at (x, y) top-left corner. */
void render_text(SDL_Renderer *renderer, TTF_Font *font,
                 const char *text, int x, int y, SDL_Color color);

/* Render text horizontally centered at the given y coordinate. */
void render_text_centered(SDL_Renderer *renderer, TTF_Font *font,
                          const char *text, int y, SDL_Color color);

/* Measure text width without rendering. */
int render_text_width(TTF_Font *font, const char *text);

/* Draw a filled rectangle. */
void render_filled_rect(SDL_Renderer *renderer, int x, int y, int w, int h,
                        SDL_Color color);

/* Draw a filled rounded rectangle. */
void render_rounded_rect(SDL_Renderer *renderer, int x, int y, int w, int h,
                         int radius, SDL_Color color);

/* Draw a styled button with centered text label.
 * Uses accent color when highlighted, otherwise dark button color. */
void render_button(SDL_Renderer *renderer, const char *label,
                   int x, int y, int w, int h,
                   int highlighted, SDL_Color accentColor);

/* Draw an avatar: tries PNG at avatarPath first, falls back to
 * colored circle with initial letter derived from name.
 * Color derived from childId via HSL. */
void render_avatar(SDL_Renderer *renderer, int childId, const char *name,
                   const char *avatarPath, int x, int y, int size);

#endif /* RENDER_H */
