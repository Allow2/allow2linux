/**
 * screen_pairing.c -- Pairing screen for allow2-lock-overlay
 *
 * Shows a QR code placeholder on the left, a 6-digit PIN in styled
 * digit boxes on the right, and a pulsing "Waiting for confirmation..."
 * indicator.
 */

#include "screen_pairing.h"

#include <string.h>
#include <math.h>

/* ---- Layout constants ---- */

#define QR_SIZE       200
#define QR_CENTER_X   350
#define QR_CENTER_Y   300

#define PIN_CENTER_X  750
#define PIN_CENTER_Y  300
#define PIN_BOX_W      56
#define PIN_BOX_H      68
#define PIN_BOX_GAP    10
#define PIN_BOX_RADIUS 12
#define PIN_DIGIT_COUNT 6

#define WAIT_Y        500

/* ---- Public API ---- */

void screen_pairing_set(PairingScreenState *state, const char *pin,
                        const char *qr_data)
{
    if (!state) return;
    memset(state->pin, 0, sizeof(state->pin));
    memset(state->qr_data, 0, sizeof(state->qr_data));
    state->pulse_time = 0.0f;
    if (pin) strncpy(state->pin, pin, sizeof(state->pin) - 1);
    if (qr_data) strncpy(state->qr_data, qr_data, sizeof(state->qr_data) - 1);
}

/* ---- Render helpers ---- */

static void render_qr_placeholder(SDL_Renderer *renderer, const char *qr_data)
{
    int x = QR_CENTER_X - QR_SIZE / 2;
    int y = QR_CENTER_Y - QR_SIZE / 2;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color border = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};
    SDL_Color finder = {51, 51, 51, 180};
    int sq = 24;
    TTF_Font *label_font;
    TTF_Font *url_font;

    /* White background */
    render_rounded_rect(renderer, x, y, QR_SIZE, QR_SIZE, 8, white);

    /* Border */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    {
        SDL_Rect outline = {x, y, QR_SIZE, QR_SIZE};
        SDL_RenderDrawRect(renderer, &outline);
    }

    /* Corner squares */
    render_filled_rect(renderer, x + 12, y + 12, sq, sq, finder);
    render_filled_rect(renderer, x + QR_SIZE - 12 - sq, y + 12, sq, sq, finder);
    render_filled_rect(renderer, x + 12, y + QR_SIZE - 12 - sq, sq, sq, finder);

    /* "Scan QR Code" label */
    label_font = render_get_font(FONT_BOLD_20);
    if (label_font) {
        SDL_Color dark = {51, 51, 51, 255};
        int tw = render_text_width(label_font, "Scan QR Code");
        render_text(renderer, label_font, "Scan QR Code",
                    x + (QR_SIZE - tw) / 2, y + QR_SIZE / 2 - 10, dark);
    }

    /* URL below QR box */
    if (qr_data && qr_data[0]) {
        url_font = render_get_font(FONT_REGULAR_16);
        if (url_font) {
            char url_display[64];
            size_t len = strlen(qr_data);
            SDL_Color gray = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};
            int tw;

            if (len > 40) {
                memcpy(url_display, qr_data, 37);
                url_display[37] = '.';
                url_display[38] = '.';
                url_display[39] = '.';
                url_display[40] = '\0';
            } else {
                strncpy(url_display, qr_data, sizeof(url_display) - 1);
                url_display[sizeof(url_display) - 1] = '\0';
            }

            tw = render_text_width(url_font, url_display);
            render_text(renderer, url_font, url_display,
                        QR_CENTER_X - tw / 2, y + QR_SIZE + 12, gray);
        }
    }
}

static void render_pin_section(SDL_Renderer *renderer, const char *pin)
{
    int total_w = PIN_DIGIT_COUNT * PIN_BOX_W + (PIN_DIGIT_COUNT - 1) * PIN_BOX_GAP;
    int start_x = PIN_CENTER_X - total_w / 2;
    int start_y = PIN_CENTER_Y - PIN_BOX_H / 2;
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

    /* "Enter this PIN" label above digits */
    if (title_font) {
        int tw = render_text_width(title_font, "Enter this PIN");
        render_text(renderer, title_font, "Enter this PIN",
                    PIN_CENTER_X - tw / 2, start_y - 50, white);
    }

    /* Subtitle */
    if (sub_font) {
        int tw = render_text_width(sub_font, "in the Allow2 app");
        render_text(renderer, sub_font, "in the Allow2 app",
                    PIN_CENTER_X - tw / 2, start_y - 16, gray);
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

    render_qr_placeholder(renderer, state->qr_data);
    render_pin_section(renderer, state->pin);

    /* Instruction text */
    body_font = render_get_font(FONT_REGULAR_20);
    if (body_font) {
        render_text_centered(renderer, body_font,
            "Open the Allow2 app on your phone and enter this PIN",
            450, gray);
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
