/**
 * screen_pin.c -- PIN entry screen with number pad
 *
 * Digit display boxes and 3x4 navigable keypad.
 * Keyboard, D-pad/controller, and mouse input.
 */

#include "screen_pin.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---------- Constants ---------- */

#define PINPAD_ROWS        4
#define PINPAD_COLS        3
#define PINPAD_GAP         12
#define PINPAD_BTN_SZ      80
#define DIGIT_BOX_W        56
#define DIGIT_BOX_H        68
#define DIGIT_BOX_GAP      16
#define DIGIT_BOX_RADIUS   12

/* Keypad labels */
static const char *PAD_LABELS[PINPAD_ROWS][PINPAD_COLS] = {
    { "1", "2", "3" },
    { "4", "5", "6" },
    { "7", "8", "9" },
    { "\xE2\x86\x90", "0", "\xE2\x9C\x93" }
};

/* Digit values: -1 = backspace, -2 = confirm */
static const int PAD_DIGITS[PINPAD_ROWS][PINPAD_COLS] = {
    { 1, 2, 3 },
    { 4, 5, 6 },
    { 7, 8, 9 },
    { -1, 0, -2 }
};

/* ---------- Internal ---------- */

static void pin_add_digit(PinScreenState *state, char digit)
{
    if (state->locked_out) return;
    if (state->entered_count >= state->max_digits) return;
    if (state->entered_count >= PIN_MAX_DIGITS) return;
    state->entered[state->entered_count++] = digit;
}

static void pin_backspace(PinScreenState *state)
{
    if (state->locked_out) return;
    if (state->entered_count > 0) {
        state->entered_count--;
        state->entered[state->entered_count] = '\0';
    }
}

static void pin_confirm(PinScreenState *state, char *out, int max_len)
{
    char pin_str[PIN_MAX_DIGITS + 1];
    int i;
    if (state->locked_out || state->entered_count == 0) return;

    for (i = 0; i < state->entered_count; i++) pin_str[i] = state->entered[i];
    pin_str[state->entered_count] = '\0';

    if (state->is_parent) {
        snprintf(out, max_len,
                 "{\"event\":\"parent-pin-entered\",\"pin\":\"%s\"}", pin_str);
    } else {
        snprintf(out, max_len,
                 "{\"event\":\"pin-entered\",\"childId\":%d,\"pin\":\"%s\"}",
                 state->child_id, pin_str);
    }
}

static void pin_handle_pad_press(PinScreenState *state, char *out, int max_len)
{
    int val = PAD_DIGITS[state->pad_selected_row][state->pad_selected_col];
    out[0] = '\0';
    if (val >= 0)       pin_add_digit(state, (char)('0' + val));
    else if (val == -1) pin_backspace(state);
    else if (val == -2) pin_confirm(state, out, max_len);
}

/* ---------- Public API ---------- */

void screen_pin_set(PinScreenState *state, int child_id, const char *child_name,
                    int is_parent, int max_digits)
{
    memset(state, 0, sizeof(PinScreenState));
    state->child_id = child_id;
    if (child_name) {
        strncpy(state->child_name, child_name, sizeof(state->child_name) - 1);
    }
    state->is_parent = is_parent;
    state->max_digits = (max_digits > 0 && max_digits <= PIN_MAX_DIGITS) ? max_digits : 4;
    state->attempts_remaining = 5;
    state->show_app_approval = 1;
}

void screen_pin_set_result(PinScreenState *state, int success,
                           int attempts_remaining, int locked_out,
                           int lockout_seconds)
{
    if (success) return;
    state->entered_count = 0;
    memset(state->entered, 0, sizeof(state->entered));
    state->attempts_remaining = attempts_remaining;
    state->locked_out = locked_out;
    state->lockout_seconds = lockout_seconds;
}

void screen_pin_render(SDL_Renderer *renderer, PinScreenState *state, float dt)
{
    int screen_w, screen_h;
    int i;
    SDL_Color white = {COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255};
    SDL_Color gray  = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};
    SDL_Color red   = {252, 129, 129, 255};
    SDL_Color accent = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255};

    state->pulse_time += dt;
    render_get_screen_size(&screen_w, &screen_h);
    render_background(renderer, COLOR_BG_A);

    /* Title */
    {
        TTF_Font *title_font = render_get_font(FONT_BOLD_36);
        if (title_font) {
            char title[128];
            if (state->is_parent) {
                snprintf(title, sizeof(title), "Enter Parent PIN");
            } else {
                snprintf(title, sizeof(title), "Enter PIN for %s", state->child_name);
            }
            render_text_centered(renderer, title_font, title,
                                 screen_h / 2 - 260, white);
        }
    }

    /* Digit display */
    {
        int total_w = state->max_digits * DIGIT_BOX_W +
                      (state->max_digits - 1) * DIGIT_BOX_GAP;
        int box_x = (screen_w - total_w) / 2;
        int box_y = screen_h / 2 - 180;
        TTF_Font *digit_font = render_get_font(FONT_BOLD_48);

        for (i = 0; i < state->max_digits; i++) {
            int bx = box_x + i * (DIGIT_BOX_W + DIGIT_BOX_GAP);
            int filled = (i < state->entered_count);

            if (filled) {
                SDL_Color fill_bg = {40, 60, 120, 255};
                render_rounded_rect(renderer, bx, box_y,
                                    DIGIT_BOX_W, DIGIT_BOX_H,
                                    DIGIT_BOX_RADIUS, fill_bg);
                if (digit_font) {
                    if (state->is_parent) {
                        int tw = render_text_width(digit_font, "\xE2\x80\xA2");
                        render_text(renderer, digit_font, "\xE2\x80\xA2",
                                    bx + (DIGIT_BOX_W - tw) / 2,
                                    box_y + (DIGIT_BOX_H - 48) / 2, white);
                    } else {
                        char ch[2] = {state->entered[i], '\0'};
                        int tw = render_text_width(digit_font, ch);
                        render_text(renderer, digit_font, ch,
                                    bx + (DIGIT_BOX_W - tw) / 2,
                                    box_y + (DIGIT_BOX_H - 48) / 2, white);
                    }
                }
            } else {
                SDL_Color outline = {80, 80, 100, 255};
                render_rounded_rect(renderer, bx, box_y,
                                    DIGIT_BOX_W, DIGIT_BOX_H,
                                    DIGIT_BOX_RADIUS, outline);
                {
                    SDL_Color inner = {COLOR_BG_R, COLOR_BG_G, COLOR_BG_B, 255};
                    render_rounded_rect(renderer, bx + 2, box_y + 2,
                                        DIGIT_BOX_W - 4, DIGIT_BOX_H - 4,
                                        DIGIT_BOX_RADIUS - 1, inner);
                }
            }
        }
    }

    /* Attempts / lockout */
    {
        TTF_Font *info_font = render_get_font(FONT_REGULAR_20);
        int info_y = screen_h / 2 - 100;
        if (info_font) {
            if (state->locked_out) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Try again in %d:%02d",
                         state->lockout_seconds / 60, state->lockout_seconds % 60);
                render_text_centered(renderer, info_font, msg, info_y, red);
            } else if (state->attempts_remaining > 0 && state->attempts_remaining < 5) {
                char msg[64];
                snprintf(msg, sizeof(msg), "%d of 5 attempts remaining",
                         state->attempts_remaining);
                render_text_centered(renderer, info_font, msg, info_y, gray);
            }
        }
    }

    /* Number pad */
    {
        int pad_total_w = PINPAD_COLS * PINPAD_BTN_SZ + (PINPAD_COLS - 1) * PINPAD_GAP;
        int pad_x = (screen_w - pad_total_w) / 2 - 100;
        int pad_y = screen_h / 2 - 60;
        int row, col;

        for (row = 0; row < PINPAD_ROWS; row++) {
            for (col = 0; col < PINPAD_COLS; col++) {
                int bx = pad_x + col * (PINPAD_BTN_SZ + PINPAD_GAP);
                int by = pad_y + row * (PINPAD_BTN_SZ + PINPAD_GAP);
                int hl = (row == state->pad_selected_row &&
                          col == state->pad_selected_col);

                if (state->locked_out) {
                    SDL_Color dim = {COLOR_BTN_R, COLOR_BTN_G, COLOR_BTN_B, 100};
                    render_rounded_rect(renderer, bx, by,
                                        PINPAD_BTN_SZ, PINPAD_BTN_SZ, 8, dim);
                    {
                        TTF_Font *pf = render_get_font(FONT_REGULAR_32);
                        if (pf) {
                            SDL_Color dc = {COLOR_TEXT2_R, COLOR_TEXT2_G,
                                            COLOR_TEXT2_B, 100};
                            int tw = render_text_width(pf, PAD_LABELS[row][col]);
                            render_text(renderer, pf, PAD_LABELS[row][col],
                                        bx + (PINPAD_BTN_SZ - tw) / 2,
                                        by + (PINPAD_BTN_SZ - 32) / 2, dc);
                        }
                    }
                } else {
                    render_button(renderer, PAD_LABELS[row][col],
                                  bx, by, PINPAD_BTN_SZ, PINPAD_BTN_SZ,
                                  hl, accent);
                }
            }
        }

        /* "Or approve on the Allow2 app" */
        if (state->show_app_approval) {
            int pad_total_h = PINPAD_ROWS * PINPAD_BTN_SZ + (PINPAD_ROWS - 1) * PINPAD_GAP;
            int app_x = pad_x + pad_total_w + 60;
            int app_y = pad_y + pad_total_h / 2 - 40;
            TTF_Font *app_font = render_get_font(FONT_REGULAR_20);
            (void)pad_total_h;

            if (app_font) {
                float pulse;
                int alpha;

                render_text(renderer, app_font, "Or approve on the",
                            app_x, app_y, gray);
                render_text(renderer, app_font, "Allow2 app",
                            app_x, app_y + 28, white);

                pulse = (float)(sin((double)state->pulse_time * 3.0) * 0.5 + 0.5);
                alpha = (int)(80 + pulse * 175);
                {
                    SDL_Color dot = {COLOR_ACCENT_R, COLOR_ACCENT_G,
                                     COLOR_ACCENT_B, (Uint8)alpha};
                    render_filled_rect(renderer, app_x - 20, app_y + 68, 10, 10, dot);
                }
                {
                    SDL_Color wc = {COLOR_TEXT2_R, COLOR_TEXT2_G,
                                    COLOR_TEXT2_B, (Uint8)alpha};
                    render_text(renderer, app_font, "Waiting...",
                                app_x - 2, app_y + 64, wc);
                }
            }
        }
    }

    /* Controller hints */
    {
        TTF_Font *hint_font = render_get_font(FONT_REGULAR_16);
        if (hint_font) {
            render_text_centered(renderer, hint_font,
                                 "D-pad: navigate    A: press digit",
                                 screen_h - 60, gray);
        }
    }
}

void screen_pin_input(PinScreenState *state, SDL_Event *event,
                      char *out_event_json, int max_len)
{
    out_event_json[0] = '\0';
    if (state->locked_out) return;

    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;

        if (key >= SDLK_0 && key <= SDLK_9) {
            pin_add_digit(state, (char)('0' + (key - SDLK_0)));
            return;
        }
        if (key >= SDLK_KP_0 && key <= SDLK_KP_9) {
            pin_add_digit(state, (char)('0' + (key - SDLK_KP_0)));
            return;
        }
        if (key == SDLK_BACKSPACE) { pin_backspace(state); return; }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            pin_confirm(state, out_event_json, max_len);
            return;
        }

        if (key == SDLK_UP) {
            state->pad_selected_row--;
            if (state->pad_selected_row < 0) state->pad_selected_row = PINPAD_ROWS - 1;
        } else if (key == SDLK_DOWN) {
            state->pad_selected_row++;
            if (state->pad_selected_row >= PINPAD_ROWS) state->pad_selected_row = 0;
        } else if (key == SDLK_LEFT) {
            state->pad_selected_col--;
            if (state->pad_selected_col < 0) state->pad_selected_col = PINPAD_COLS - 1;
        } else if (key == SDLK_RIGHT) {
            state->pad_selected_col++;
            if (state->pad_selected_col >= PINPAD_COLS) state->pad_selected_col = 0;
        }
    }

    if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        Uint8 btn = event->cbutton.button;
        if (btn == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            state->pad_selected_row--;
            if (state->pad_selected_row < 0) state->pad_selected_row = PINPAD_ROWS - 1;
        } else if (btn == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            state->pad_selected_row++;
            if (state->pad_selected_row >= PINPAD_ROWS) state->pad_selected_row = 0;
        } else if (btn == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
            state->pad_selected_col--;
            if (state->pad_selected_col < 0) state->pad_selected_col = PINPAD_COLS - 1;
        } else if (btn == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            state->pad_selected_col++;
            if (state->pad_selected_col >= PINPAD_COLS) state->pad_selected_col = 0;
        } else if (btn == SDL_CONTROLLER_BUTTON_A) {
            pin_handle_pad_press(state, out_event_json, max_len);
        }
    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int screen_w, screen_h;
        int pad_total_w, pad_x, pad_y;
        int mx = event->button.x, my = event->button.y;
        int row, col;

        render_get_screen_size(&screen_w, &screen_h);
        pad_total_w = PINPAD_COLS * PINPAD_BTN_SZ + (PINPAD_COLS - 1) * PINPAD_GAP;
        pad_x = (screen_w - pad_total_w) / 2 - 100;
        pad_y = screen_h / 2 - 60;

        for (row = 0; row < PINPAD_ROWS; row++) {
            for (col = 0; col < PINPAD_COLS; col++) {
                int bx = pad_x + col * (PINPAD_BTN_SZ + PINPAD_GAP);
                int by = pad_y + row * (PINPAD_BTN_SZ + PINPAD_GAP);
                if (mx >= bx && mx <= bx + PINPAD_BTN_SZ &&
                    my >= by && my <= by + PINPAD_BTN_SZ) {
                    state->pad_selected_row = row;
                    state->pad_selected_col = col;
                    pin_handle_pad_press(state, out_event_json, max_len);
                    return;
                }
            }
        }
    }
}
