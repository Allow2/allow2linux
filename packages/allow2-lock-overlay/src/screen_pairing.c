/**
 * screen_pairing.c -- Pairing screen for allow2-lock-overlay
 *
 * Shows the Allow2 Parental Freedom branding at top, a QR code on
 * the left, a 6-digit PIN in styled digit boxes on the right, and
 * a pulsing "Waiting for confirmation..." indicator.
 */

#include "screen_pairing.h"

#include <string.h>
#include <math.h>

/* ---- Layout constants ---- */

#define TITLE_Y        80
#define SUBTITLE_Y    130

#define QR_SIZE       200
#define QR_CENTER_X   380
#define QR_CENTER_Y   360

#define PIN_CENTER_X  780
#define PIN_CENTER_Y  360
#define PIN_BOX_W      56
#define PIN_BOX_H      68
#define PIN_BOX_GAP    10
#define PIN_BOX_RADIUS 12
#define PIN_DIGIT_COUNT 6

#define INSTRUCTION_Y 520
#define WAIT_Y        580

/* Logo circle dimensions */
#define LOGO_SIZE      56
#define LOGO_X        (LOGICAL_W / 2 - LOGO_SIZE / 2)
#define LOGO_Y         16

/* ---- Public API ---- */

void screen_pairing_set(PairingScreenState *state, const char *pin,
                        const char *qr_data, int qr_size,
                        const char *qr_modules)
{
    if (!state) return;
    memset(state->pin, 0, sizeof(state->pin));
    memset(state->qr_data, 0, sizeof(state->qr_data));
    memset(state->qr_modules, 0, sizeof(state->qr_modules));
    state->qr_size = 0;
    state->pulse_time = 0.0f;
    if (pin) strncpy(state->pin, pin, sizeof(state->pin) - 1);
    if (qr_data) strncpy(state->qr_data, qr_data, sizeof(state->qr_data) - 1);
    if (qr_size > 0 && qr_size <= QR_MAX_SIZE && qr_modules) {
        state->qr_size = qr_size;
        strncpy(state->qr_modules, qr_modules, sizeof(state->qr_modules) - 1);
    }
}

/* ---- Render helpers ---- */

static void render_logo(SDL_Renderer *renderer)
{
    /* Draw a circular accent-colored logo with "A2" text */
    SDL_Color accent = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255};
    SDL_Color white = {255, 255, 255, 255};
    TTF_Font *logo_font = render_get_font(FONT_BOLD_28);
    int cx = LOGO_X + LOGO_SIZE / 2;
    int cy = LOGO_Y + LOGO_SIZE / 2;
    int r;

    /* Filled circle (approximated with concentric rects for simplicity) */
    for (r = LOGO_SIZE / 2; r > 0; r--) {
        /* Use parametric circle scan */
        int y;
        for (y = -r; y <= r; y++) {
            int half_w = (int)sqrt((double)(r * r - y * y));
            SDL_Rect row = {cx - half_w, cy + y, half_w * 2, 1};
            SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, accent.a);
            SDL_RenderFillRect(renderer, &row);
        }
        break; /* Only need one pass for a filled circle */
    }

    /* "A2" text centered in the circle */
    if (logo_font) {
        int tw = render_text_width(logo_font, "A2");
        render_text(renderer, logo_font, "A2",
                    cx - tw / 2, cy - 14, white);
    }
}

static void render_title(SDL_Renderer *renderer)
{
    TTF_Font *title_font = render_get_font(FONT_BOLD_36);
    TTF_Font *sub_font   = render_get_font(FONT_REGULAR_20);
    SDL_Color accent = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255};
    SDL_Color gray   = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};

    if (title_font) {
        render_text_centered(renderer, title_font,
            "Allow2 Parental Freedom", TITLE_Y, accent);
    }
    if (sub_font) {
        render_text_centered(renderer, sub_font,
            "Device Pairing", SUBTITLE_Y, gray);
    }
}

static void render_qr_code(SDL_Renderer *renderer,
                           int qr_size, const char *qr_modules)
{
    int x = QR_CENTER_X - QR_SIZE / 2;
    int y = QR_CENTER_Y - QR_SIZE / 2;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color black = {0, 0, 0, 255};

    /* White background with quiet zone */
    render_rounded_rect(renderer, x, y, QR_SIZE, QR_SIZE, 8, white);

    if (qr_size > 0 && qr_modules && qr_modules[0]) {
        /* Calculate module pixel size with quiet zone (2 modules each side) */
        int total_modules = qr_size + 4; /* 2-module quiet zone on each side */
        int mod_px = QR_SIZE / total_modules;
        int grid_px = mod_px * total_modules;
        int offset_x = x + (QR_SIZE - grid_px) / 2;
        int offset_y = y + (QR_SIZE - grid_px) / 2;
        int quiet = 2 * mod_px;
        int r, c;

        for (r = 0; r < qr_size; r++) {
            for (c = 0; c < qr_size; c++) {
                int idx = r * qr_size + c;
                if (qr_modules[idx] == '1') {
                    SDL_Rect mod = {
                        offset_x + quiet + c * mod_px,
                        offset_y + quiet + r * mod_px,
                        mod_px, mod_px
                    };
                    SDL_SetRenderDrawColor(renderer, black.r, black.g,
                                           black.b, black.a);
                    SDL_RenderFillRect(renderer, &mod);
                }
            }
        }
    } else {
        /* Fallback: "Scan QR Code" placeholder */
        TTF_Font *label_font = render_get_font(FONT_BOLD_20);
        if (label_font) {
            SDL_Color dark = {51, 51, 51, 255};
            int tw = render_text_width(label_font, "Scan QR Code");
            render_text(renderer, label_font, "Scan QR Code",
                        x + (QR_SIZE - tw) / 2, y + QR_SIZE / 2 - 10, dark);
        }
    }

    /* "Scan with your phone" label below QR */
    {
        TTF_Font *sub_font = render_get_font(FONT_REGULAR_16);
        if (sub_font) {
            SDL_Color gray = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};
            int tw = render_text_width(sub_font, "Scan with your phone");
            render_text(renderer, sub_font, "Scan with your phone",
                        QR_CENTER_X - tw / 2, y + QR_SIZE + 12, gray);
        }
    }
}

static void render_pin_section(SDL_Renderer *renderer, const char *pin)
{
    int total_w = PIN_DIGIT_COUNT * PIN_BOX_W + (PIN_DIGIT_COUNT - 1) * PIN_BOX_GAP;
    int start_x = PIN_CENTER_X - total_w / 2;
    int start_y = PIN_CENTER_Y - PIN_BOX_H / 2 + 30; /* shifted down for label space */
    SDL_Color box_bg = {240, 242, 255, 255};
    SDL_Color box_border = {221, 224, 245, 255};
    SDL_Color digit_color = {51, 51, 51, 255};
    SDL_Color white = {COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255};
    SDL_Color gray  = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};
    int pin_len = pin ? (int)strlen(pin) : 0;
    TTF_Font *digit_font = render_get_font(FONT_BOLD_36);
    TTF_Font *title_font = render_get_font(FONT_BOLD_28);
    TTF_Font *sub_font   = render_get_font(FONT_REGULAR_20);
    int i;

    /* "...or enter this PIN" label above digits */
    if (title_font) {
        int tw = render_text_width(title_font, "...or enter this PIN");
        render_text(renderer, title_font, "...or enter this PIN",
                    PIN_CENTER_X - tw / 2, start_y - 70, white);
    }

    /* Subtitle */
    if (sub_font) {
        int tw = render_text_width(sub_font, "in the Allow2 app");
        render_text(renderer, sub_font, "in the Allow2 app",
                    PIN_CENTER_X - tw / 2, start_y - 32, gray);
    }

    /* Digit boxes */
    for (i = 0; i < PIN_DIGIT_COUNT; i++) {
        int bx = start_x + i * (PIN_BOX_W + PIN_BOX_GAP);

        render_rounded_rect(renderer, bx, start_y, PIN_BOX_W, PIN_BOX_H,
                            PIN_BOX_RADIUS, box_bg);

        /* Border */
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, box_border.r, box_border.g,
                               box_border.b, box_border.a);
        {
            SDL_Rect br = {bx, start_y, PIN_BOX_W, PIN_BOX_H};
            SDL_RenderDrawRect(renderer, &br);
        }

        if (digit_font && i < pin_len && pin[i] >= '0' && pin[i] <= '9') {
            char ds[2] = {pin[i], '\0'};
            int tw = render_text_width(digit_font, ds);
            render_text(renderer, digit_font, ds,
                        bx + (PIN_BOX_W - tw) / 2,
                        start_y + (PIN_BOX_H - 36) / 2,
                        digit_color);
        }
    }
}

/* ---- Divider between QR and PIN sections ---- */

static void render_divider(SDL_Renderer *renderer)
{
    SDL_Color div_color = {COLOR_DIV_R, COLOR_DIV_G, COLOR_DIV_B, 255};
    int cx = (QR_CENTER_X + PIN_CENTER_X) / 2;
    int top = QR_CENTER_Y - QR_SIZE / 2 + 20;
    int bot = QR_CENTER_Y + QR_SIZE / 2 - 20;
    TTF_Font *or_font = render_get_font(FONT_REGULAR_16);
    SDL_Color gray = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};

    /* Vertical line */
    render_filled_rect(renderer, cx, top, 1, bot - top, div_color);

    /* "or" label in the middle */
    if (or_font) {
        int tw = render_text_width(or_font, "or");
        /* Background pill behind "or" to break the line */
        {
            SDL_Color bg = {COLOR_BG_R, COLOR_BG_G, COLOR_BG_B, 255};
            render_filled_rect(renderer, cx - tw / 2 - 8,
                               (top + bot) / 2 - 12, tw + 16, 24, bg);
        }
        render_text(renderer, or_font, "or",
                    cx - tw / 2, (top + bot) / 2 - 8, gray);
    }
}

/* ---- Main render ---- */

void screen_pairing_render(SDL_Renderer *renderer,
                           PairingScreenState *state, float dt)
{
    float pulse;
    int alpha;
    SDL_Color gray = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};
    TTF_Font *body_font;

    if (!renderer || !state) return;

    state->pulse_time += dt;

    render_background(renderer, COLOR_BG_A);

    /* Logo and title */
    render_logo(renderer);
    render_title(renderer);

    /* QR code and PIN side by side with divider */
    render_qr_code(renderer, state->qr_size, state->qr_modules);
    render_divider(renderer);
    render_pin_section(renderer, state->pin);

    /* Instruction text */
    body_font = render_get_font(FONT_REGULAR_20);
    if (body_font) {
        render_text_centered(renderer, body_font,
            "Open the Allow2 app on your phone to pair this device",
            INSTRUCTION_Y, gray);
    }

    /* Pulsing waiting indicator */
    pulse = (float)(sin((double)state->pulse_time * 3.0) * 0.5 + 0.5);
    alpha = (int)(80 + pulse * 175);
    {
        SDL_Color dot = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, (Uint8)alpha};
        render_filled_rect(renderer, LOGICAL_W / 2 - 100, WAIT_Y, 10, 10, dot);
    }
    if (body_font) {
        SDL_Color txt = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, (Uint8)alpha};
        render_text(renderer, body_font, "Waiting for confirmation...",
                    LOGICAL_W / 2 - 82, WAIT_Y - 4, txt);
    }
}
