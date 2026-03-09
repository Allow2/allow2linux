/**
 * screen_feedback.c -- Feedback/Report Issue screen
 *
 * Allows users to report issues from the overlay. Supports keyboard,
 * mouse, and gamepad navigation. Categories are radio-button style,
 * message is a free-text input.
 */

#include "screen_feedback.h"

#include <stdio.h>
#include <string.h>

/* ---- Constants ---- */

#define TITLE_Y             60
#define CATEGORY_X         340
#define CATEGORY_Y         180
#define CATEGORY_ROW_H      40
#define RADIO_SIZE           18

#define MSG_BOX_X          340
#define MSG_BOX_W          600
#define MSG_BOX_H          120
#define MSG_PAD             10

#define BTN_W              160
#define BTN_H               50
#define BTN_GAP             40

/* Category labels (display) and keys (JSON) */
static const char *category_labels[FEEDBACK_NUM_CATEGORIES] = {
    "Bypass / Circumvention",
    "Missing Feature",
    "Not Working",
    "Question",
    "Other",
};

static const char *category_keys[FEEDBACK_NUM_CATEGORIES] = {
    "bypass",
    "missing_feature",
    "not_working",
    "question",
    "other",
};

/* ---- Hit-test rectangles ---- */

static SDL_Rect category_rects[FEEDBACK_NUM_CATEGORIES];
static SDL_Rect msg_box_rect;
static SDL_Rect submit_btn_rect;
static SDL_Rect cancel_btn_rect;

/* ---- Helpers ---- */

static int point_in_rect(int px, int py, SDL_Rect r) {
    return (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h);
}

static void write_submit_event(FeedbackScreenState *state,
                                char *out, int max_len) {
    /* Escape message text for JSON: replace " and \ with space,
     * and replace newlines with space. Simple approach sufficient
     * for short user messages. */
    char safe_msg[FEEDBACK_MAX_MESSAGE];
    int i;
    strncpy(safe_msg, state->message_text, sizeof(safe_msg) - 1);
    safe_msg[sizeof(safe_msg) - 1] = '\0';
    for (i = 0; safe_msg[i]; i++) {
        if (safe_msg[i] == '"' || safe_msg[i] == '\\' ||
            safe_msg[i] == '\n' || safe_msg[i] == '\r') {
            safe_msg[i] = ' ';
        }
    }

    snprintf(out, max_len,
             "{\"event\":\"submit-feedback\","
             "\"category\":\"%s\","
             "\"message\":\"%s\"}",
             category_keys[state->selected_category],
             safe_msg);
}

static void write_cancel_event(char *out, int max_len) {
    snprintf(out, max_len, "{\"event\":\"feedback-cancel\"}");
}

/* ---- Public functions ---- */

void screen_feedback_reset(FeedbackScreenState *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->selected_category = 0;
    state->focus = FEEDBACK_FOCUS_CATEGORY;
}

/* ---- Render ---- */

void screen_feedback_render(SDL_Renderer *renderer, FeedbackScreenState *state) {
    SDL_Color white  = {COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255};
    SDL_Color gray   = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};
    SDL_Color accent = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255};
    SDL_Color hint_c = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 180};
    SDL_Color box_bg = {COLOR_BTN_R, COLOR_BTN_G, COLOR_BTN_B, 200};

    TTF_Font *title_font = render_get_font(FONT_BOLD_36);
    TTF_Font *label_font = render_get_font(FONT_BOLD_22);
    TTF_Font *body_font  = render_get_font(FONT_REGULAR_20);
    TTF_Font *hint_font  = render_get_font(FONT_REGULAR_16);
    int y;
    int i;
    int msg_y;

    if (!renderer || !state) return;

    render_background(renderer, COLOR_BG_A);

    /* Title */
    y = TITLE_Y;
    if (title_font) {
        render_text_centered(renderer, title_font, "Report an Issue", y, white);
    }

    /* "Category:" label */
    y = CATEGORY_Y - 36;
    if (label_font) {
        render_text(renderer, label_font, "Category:", CATEGORY_X, y, gray);
    }

    /* Category radio buttons */
    y = CATEGORY_Y;
    for (i = 0; i < FEEDBACK_NUM_CATEGORIES; i++) {
        int is_selected = (state->selected_category == i);
        int is_focused = (state->focus == FEEDBACK_FOCUS_CATEGORY);
        int radio_x = CATEGORY_X;
        int radio_y = y + (CATEGORY_ROW_H - RADIO_SIZE) / 2;
        SDL_Color radio_col;

        /* Store hit rect */
        category_rects[i] = (SDL_Rect){CATEGORY_X, y, MSG_BOX_W, CATEGORY_ROW_H};

        /* Highlight row if this category is focused and selected */
        if (is_focused && is_selected) {
            SDL_Color hl = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 40};
            render_rounded_rect(renderer, CATEGORY_X - 8, y,
                                MSG_BOX_W + 16, CATEGORY_ROW_H,
                                BUTTON_RADIUS, hl);
        }

        /* Radio circle outline */
        radio_col = is_selected ? accent : gray;
        {
            SDL_Rect outer = {radio_x, radio_y, RADIO_SIZE, RADIO_SIZE};
            SDL_SetRenderDrawColor(renderer, radio_col.r, radio_col.g,
                                   radio_col.b, radio_col.a);
            SDL_RenderDrawRect(renderer, &outer);

            /* Filled inner circle for selected */
            if (is_selected) {
                SDL_Rect inner = {radio_x + 4, radio_y + 4,
                                  RADIO_SIZE - 8, RADIO_SIZE - 8};
                render_filled_rect(renderer, inner.x, inner.y,
                                   inner.w, inner.h, accent);
            }
        }

        /* Category label text */
        if (body_font) {
            render_text(renderer, body_font, category_labels[i],
                        radio_x + RADIO_SIZE + 12, y + (CATEGORY_ROW_H - 20) / 2,
                        is_selected ? white : gray);
        }

        y += CATEGORY_ROW_H;
    }

    /* "Message:" label */
    msg_y = y + 20;
    if (label_font) {
        render_text(renderer, label_font, "Message:", CATEGORY_X, msg_y, gray);
    }
    msg_y += 32;

    /* Message input box */
    {
        int is_msg_focused = (state->focus == FEEDBACK_FOCUS_MESSAGE);
        SDL_Color border_col = is_msg_focused ? accent : gray;

        msg_box_rect = (SDL_Rect){MSG_BOX_X, msg_y, MSG_BOX_W, MSG_BOX_H};

        /* Background */
        render_rounded_rect(renderer, MSG_BOX_X, msg_y,
                            MSG_BOX_W, MSG_BOX_H,
                            BUTTON_RADIUS, box_bg);

        /* Border */
        {
            SDL_Rect border = {MSG_BOX_X, msg_y, MSG_BOX_W, MSG_BOX_H};
            SDL_SetRenderDrawColor(renderer, border_col.r, border_col.g,
                                   border_col.b, border_col.a);
            SDL_RenderDrawRect(renderer, &border);
        }

        /* Message text or placeholder */
        if (body_font) {
            int tx = MSG_BOX_X + MSG_PAD;
            int ty = msg_y + MSG_PAD;

            if (state->message_len == 0) {
                SDL_Color placeholder = {COLOR_TEXT2_R, COLOR_TEXT2_G,
                                         COLOR_TEXT2_B, 128};
                render_text(renderer, body_font, "Type your message here...",
                            tx, ty, placeholder);
            } else {
                render_text(renderer, body_font, state->message_text,
                            tx, ty, white);
            }

            /* Blinking cursor when focused */
            if (is_msg_focused) {
                int blink = ((SDL_GetTicks() / 500) % 2 == 0);
                if (blink) {
                    int cursor_x;
                    if (state->message_len > 0) {
                        cursor_x = tx + render_text_width(body_font,
                                                          state->message_text) + 2;
                    } else {
                        cursor_x = tx;
                    }
                    render_filled_rect(renderer, cursor_x, ty, 2, 20, accent);
                }
            }
        }
    }

    /* Submit / Cancel buttons */
    {
        int btn_y = msg_y + MSG_BOX_H + 30;
        int total_w = BTN_W * 2 + BTN_GAP;
        int btn_x = (LOGICAL_W - total_w) / 2;

        render_button(renderer, "Submit",
                      btn_x, btn_y, BTN_W, BTN_H,
                      (state->focus == FEEDBACK_FOCUS_SUBMIT), accent);
        submit_btn_rect = (SDL_Rect){btn_x, btn_y, BTN_W, BTN_H};

        render_button(renderer, "Cancel",
                      btn_x + BTN_W + BTN_GAP, btn_y, BTN_W, BTN_H,
                      (state->focus == FEEDBACK_FOCUS_CANCEL), accent);
        cancel_btn_rect = (SDL_Rect){btn_x + BTN_W + BTN_GAP, btn_y,
                                      BTN_W, BTN_H};
    }

    /* Gamepad hints at bottom */
    if (hint_font) {
        render_text_centered(renderer, hint_font,
                             "Tab: next field    A: Submit    B: Cancel",
                             LOGICAL_H - 50, hint_c);
    }
}

/* ---- Input ---- */

void screen_feedback_input(FeedbackScreenState *state, SDL_Event *event,
                           char *out_event_json, int max_len) {
    if (!state || !event || !out_event_json || max_len < 2) return;
    out_event_json[0] = '\0';

    /* --- Text input (only when message field focused) --- */
    if (event->type == SDL_TEXTINPUT && state->focus == FEEDBACK_FOCUS_MESSAGE) {
        int add_len = (int)strlen(event->text.text);
        if (state->message_len + add_len < FEEDBACK_MAX_MESSAGE - 1) {
            memcpy(state->message_text + state->message_len,
                   event->text.text, add_len);
            state->message_len += add_len;
            state->message_text[state->message_len] = '\0';
        }
        return;
    }

    /* --- Keyboard --- */
    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;

        /* Escape / B = cancel */
        if (key == SDLK_ESCAPE) {
            write_cancel_event(out_event_json, max_len);
            return;
        }

        /* Tab = cycle focus forward */
        if (key == SDLK_TAB) {
            state->focus++;
            if (state->focus > FEEDBACK_FOCUS_CANCEL) {
                state->focus = FEEDBACK_FOCUS_CATEGORY;
            }
            return;
        }

        /* Backspace in message field */
        if (key == SDLK_BACKSPACE && state->focus == FEEDBACK_FOCUS_MESSAGE) {
            if (state->message_len > 0) {
                state->message_len--;
                state->message_text[state->message_len] = '\0';
            }
            return;
        }

        /* Up/Down navigation */
        if (key == SDLK_UP) {
            if (state->focus == FEEDBACK_FOCUS_CATEGORY) {
                state->selected_category--;
                if (state->selected_category < 0) {
                    state->selected_category = FEEDBACK_NUM_CATEGORIES - 1;
                }
            } else if (state->focus == FEEDBACK_FOCUS_MESSAGE) {
                state->focus = FEEDBACK_FOCUS_CATEGORY;
            } else if (state->focus == FEEDBACK_FOCUS_SUBMIT ||
                       state->focus == FEEDBACK_FOCUS_CANCEL) {
                state->focus = FEEDBACK_FOCUS_MESSAGE;
            }
            return;
        }

        if (key == SDLK_DOWN) {
            if (state->focus == FEEDBACK_FOCUS_CATEGORY) {
                state->selected_category++;
                if (state->selected_category >= FEEDBACK_NUM_CATEGORIES) {
                    state->selected_category = 0;
                }
            } else if (state->focus == FEEDBACK_FOCUS_MESSAGE) {
                state->focus = FEEDBACK_FOCUS_SUBMIT;
            }
            return;
        }

        /* Left/Right for submit/cancel buttons */
        if (key == SDLK_LEFT &&
            state->focus == FEEDBACK_FOCUS_CANCEL) {
            state->focus = FEEDBACK_FOCUS_SUBMIT;
            return;
        }
        if (key == SDLK_RIGHT &&
            state->focus == FEEDBACK_FOCUS_SUBMIT) {
            state->focus = FEEDBACK_FOCUS_CANCEL;
            return;
        }

        /* Enter on submit or cancel */
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (state->focus == FEEDBACK_FOCUS_SUBMIT) {
                state->submitted = 1;
                write_submit_event(state, out_event_json, max_len);
            } else if (state->focus == FEEDBACK_FOCUS_CANCEL) {
                write_cancel_event(out_event_json, max_len);
            } else if (state->focus == FEEDBACK_FOCUS_CATEGORY) {
                /* Enter on category moves to message */
                state->focus = FEEDBACK_FOCUS_MESSAGE;
            }
            return;
        }
    }

    /* --- Controller --- */
    if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        Uint8 btn = event->cbutton.button;

        /* B = cancel */
        if (btn == SDL_CONTROLLER_BUTTON_B) {
            write_cancel_event(out_event_json, max_len);
            return;
        }

        /* A = submit (from submit button) or select */
        if (btn == SDL_CONTROLLER_BUTTON_A) {
            if (state->focus == FEEDBACK_FOCUS_SUBMIT) {
                state->submitted = 1;
                write_submit_event(state, out_event_json, max_len);
            } else if (state->focus == FEEDBACK_FOCUS_CANCEL) {
                write_cancel_event(out_event_json, max_len);
            } else if (state->focus == FEEDBACK_FOCUS_CATEGORY) {
                /* A on category moves to message */
                state->focus = FEEDBACK_FOCUS_MESSAGE;
            }
            return;
        }

        /* D-pad navigation */
        if (btn == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            if (state->focus == FEEDBACK_FOCUS_CATEGORY) {
                state->selected_category--;
                if (state->selected_category < 0) {
                    state->selected_category = FEEDBACK_NUM_CATEGORIES - 1;
                }
            } else if (state->focus == FEEDBACK_FOCUS_MESSAGE) {
                state->focus = FEEDBACK_FOCUS_CATEGORY;
            } else if (state->focus == FEEDBACK_FOCUS_SUBMIT ||
                       state->focus == FEEDBACK_FOCUS_CANCEL) {
                state->focus = FEEDBACK_FOCUS_MESSAGE;
            }
            return;
        }

        if (btn == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            if (state->focus == FEEDBACK_FOCUS_CATEGORY) {
                state->selected_category++;
                if (state->selected_category >= FEEDBACK_NUM_CATEGORIES) {
                    state->selected_category = 0;
                }
            } else if (state->focus == FEEDBACK_FOCUS_MESSAGE) {
                state->focus = FEEDBACK_FOCUS_SUBMIT;
            }
            return;
        }

        if (btn == SDL_CONTROLLER_BUTTON_DPAD_LEFT &&
            state->focus == FEEDBACK_FOCUS_CANCEL) {
            state->focus = FEEDBACK_FOCUS_SUBMIT;
            return;
        }
        if (btn == SDL_CONTROLLER_BUTTON_DPAD_RIGHT &&
            state->focus == FEEDBACK_FOCUS_SUBMIT) {
            state->focus = FEEDBACK_FOCUS_CANCEL;
            return;
        }

        /* X button = cycle focus (like Tab) */
        if (btn == SDL_CONTROLLER_BUTTON_X) {
            state->focus++;
            if (state->focus > FEEDBACK_FOCUS_CANCEL) {
                state->focus = FEEDBACK_FOCUS_CATEGORY;
            }
            return;
        }
    }

    /* --- Mouse click --- */
    if (event->type == SDL_MOUSEBUTTONDOWN &&
        event->button.button == SDL_BUTTON_LEFT) {
        int mx = event->button.x, my = event->button.y;
        int i;

        /* Check category clicks */
        for (i = 0; i < FEEDBACK_NUM_CATEGORIES; i++) {
            if (point_in_rect(mx, my, category_rects[i])) {
                state->selected_category = i;
                state->focus = FEEDBACK_FOCUS_CATEGORY;
                return;
            }
        }

        /* Check message box click */
        if (point_in_rect(mx, my, msg_box_rect)) {
            state->focus = FEEDBACK_FOCUS_MESSAGE;
            return;
        }

        /* Check submit button click */
        if (point_in_rect(mx, my, submit_btn_rect)) {
            state->submitted = 1;
            write_submit_event(state, out_event_json, max_len);
            return;
        }

        /* Check cancel button click */
        if (point_in_rect(mx, my, cancel_btn_rect)) {
            write_cancel_event(out_event_json, max_len);
            return;
        }
    }
}
