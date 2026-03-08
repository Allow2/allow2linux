/**
 * screen_lock.c -- Lock screen with "Request More Time" and "Switch Child"
 *
 * Full opaque overlay. Blocks all interaction with the device underneath.
 * Sub-views: main, duration picker, pending, denied.
 */

#include "screen_lock.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---- Constants ---- */

#define BTN_W         200
#define BTN_H          60
#define BTN_GAP        24

#define DUR_BTN_W     160
#define DUR_BTN_H      60
#define DUR_BTN_GAP    24

#define DENIED_TIMEOUT  3.0f

static const int DURATION_OPTIONS[] = { 15, 30, 60 };
static const char *DURATION_LABELS[] = { "15 min", "30 min", "1 hour" };
#define NUM_DURATIONS 3

#define MAIN_BTN_COUNT     2
#define DURATION_BTN_COUNT 3

/* Hit-test rectangles (computed each frame) */
static SDL_Rect main_btn_rects[MAIN_BTN_COUNT];
static SDL_Rect dur_btn_rects[DURATION_BTN_COUNT];
static SDL_Rect back_link_rect;

/* ---- Public functions ---- */

void screen_lock_set(LockScreenState *state, const char *reason,
                     const char *child_name, int child_id, int activity_id) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    if (reason) strncpy(state->reason, reason, sizeof(state->reason) - 1);
    if (child_name) strncpy(state->child_name, child_name, sizeof(state->child_name) - 1);
    state->child_id = child_id;
    state->activity_id = activity_id;
    state->view = LOCK_VIEW_MAIN;
}

void screen_lock_set_request_status(LockScreenState *state, const char *status) {
    if (!state || !status) return;

    if (strcmp(status, "pending") == 0) {
        state->view = LOCK_VIEW_PENDING;
        state->pulse_phase = 0.0f;
    } else if (strcmp(status, "denied") == 0) {
        state->view = LOCK_VIEW_DENIED;
        state->denied_timer = DENIED_TIMEOUT;
    }
    /* "approved" is handled by caller (dismiss) */
}

/* ---- Render helpers ---- */

static void render_main_view(SDL_Renderer *renderer, LockScreenState *state) {
    SDL_Color white = {COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255};
    SDL_Color gray  = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};
    SDL_Color accent = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255};
    TTF_Font *title_font = render_get_font(FONT_BOLD_36);
    TTF_Font *body_font  = render_get_font(FONT_REGULAR_20);
    TTF_Font *hint_font  = render_get_font(FONT_REGULAR_16);
    int title_y = LOGICAL_H / 2 - 120;

    /* Reason title */
    if (title_font) {
        render_text_centered(renderer, title_font, state->reason, title_y, white);
    }

    /* Personalized message */
    if (body_font) {
        char message[256];
        snprintf(message, sizeof(message),
                 "%s, your daily screen time has been used up.", state->child_name);
        render_text_centered(renderer, body_font, message, title_y + 56, gray);
    }

    /* Two buttons side by side */
    {
        int total_w = BTN_W * 2 + BTN_GAP;
        int left_x = (LOGICAL_W - total_w) / 2;
        int right_x = left_x + BTN_W + BTN_GAP;
        int btn_y = LOGICAL_H / 2 + 40;

        render_button(renderer, "Request More Time",
                      left_x, btn_y, BTN_W, BTN_H,
                      state->selected_button == 0, accent);
        main_btn_rects[0] = (SDL_Rect){left_x, btn_y, BTN_W, BTN_H};

        render_button(renderer, "Switch Child",
                      right_x, btn_y, BTN_W, BTN_H,
                      state->selected_button == 1, accent);
        main_btn_rects[1] = (SDL_Rect){right_x, btn_y, BTN_W, BTN_H};
    }

    /* Controller hints */
    if (hint_font) {
        SDL_Color hint = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 180};
        render_text_centered(renderer, hint_font,
                             "D-pad: navigate    A: select",
                             LOGICAL_H - 60, hint);
    }
}

static void render_duration_view(SDL_Renderer *renderer, LockScreenState *state) {
    SDL_Color white = {COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255};
    SDL_Color gray  = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};
    SDL_Color accent = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255};
    TTF_Font *title_font = render_get_font(FONT_BOLD_36);
    TTF_Font *body_font  = render_get_font(FONT_REGULAR_20);
    int title_y = LOGICAL_H / 2 - 80;
    int i;

    if (title_font) {
        render_text_centered(renderer, title_font, "Request More Time",
                             title_y, white);
    }

    /* Three duration buttons */
    {
        int total_w = DUR_BTN_W * NUM_DURATIONS + DUR_BTN_GAP * (NUM_DURATIONS - 1);
        int start_x = (LOGICAL_W - total_w) / 2;
        int btn_y = LOGICAL_H / 2 + 10;

        for (i = 0; i < NUM_DURATIONS; i++) {
            int bx = start_x + i * (DUR_BTN_W + DUR_BTN_GAP);
            render_button(renderer, DURATION_LABELS[i],
                          bx, btn_y, DUR_BTN_W, DUR_BTN_H,
                          state->selected_button == i, accent);
            dur_btn_rects[i] = (SDL_Rect){bx, btn_y, DUR_BTN_W, DUR_BTN_H};
        }
    }

    /* Back link */
    if (body_font) {
        int back_y = LOGICAL_H / 2 + 10 + DUR_BTN_H + 36;
        render_text_centered(renderer, body_font, "\xE2\x86\x90 Back",
                             back_y, gray);
        int tw = render_text_width(body_font, "\xE2\x86\x90 Back");
        back_link_rect = (SDL_Rect){(LOGICAL_W - tw) / 2, back_y, tw, 28};
    }
}

static void render_pending_view(SDL_Renderer *renderer, LockScreenState *state,
                                float dt) {
    SDL_Color white = {COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255};
    TTF_Font *body_font = render_get_font(FONT_REGULAR_20);
    int text_y = LOGICAL_H / 2 - 20;
    float alpha;
    Uint8 a;
    SDL_Color dot_color;

    if (body_font) {
        render_text_centered(renderer, body_font,
                             "Waiting for parent approval...", text_y, white);
    }

    /* Pulsing dot */
    state->pulse_phase += dt * 3.0f;
    if (state->pulse_phase > 6.2831853f) state->pulse_phase -= 6.2831853f;
    alpha = 0.5f + 0.5f * sinf(state->pulse_phase);
    a = (Uint8)(alpha * 255.0f);
    dot_color.r = COLOR_ACCENT_R;
    dot_color.g = COLOR_ACCENT_G;
    dot_color.b = COLOR_ACCENT_B;
    dot_color.a = a;
    render_filled_rect(renderer, LOGICAL_W / 2 - 5, text_y + 45, 10, 10, dot_color);
}

static void render_denied_view(SDL_Renderer *renderer) {
    SDL_Color red = {252, 129, 129, 255};
    TTF_Font *title_font = render_get_font(FONT_BOLD_36);
    if (title_font) {
        render_text_centered(renderer, title_font, "Request denied",
                             LOGICAL_H / 2 - 20, red);
    }
}

/* ---- Main render ---- */

void screen_lock_render(SDL_Renderer *renderer, LockScreenState *state,
                        float dt) {
    if (!renderer || !state) return;

    render_background(renderer, COLOR_BG_A);

    switch (state->view) {
    case LOCK_VIEW_MAIN:
        render_main_view(renderer, state);
        break;
    case LOCK_VIEW_DURATION:
        render_duration_view(renderer, state);
        break;
    case LOCK_VIEW_PENDING:
        render_pending_view(renderer, state, dt);
        break;
    case LOCK_VIEW_DENIED:
        render_denied_view(renderer);
        state->denied_timer -= dt;
        if (state->denied_timer <= 0.0f) {
            state->view = LOCK_VIEW_MAIN;
            state->selected_button = 0;
        }
        break;
    }
}

/* ---- Input ---- */

static int point_in_rect(int px, int py, SDL_Rect r) {
    return (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h);
}

static void write_request_event(LockScreenState *state, int duration,
                                char *out, int max_len) {
    snprintf(out, max_len,
             "{\"event\":\"request-more-time\",\"activityId\":%d,\"duration\":%d}",
             state->activity_id, duration);
}

static void write_switch_event(char *out, int max_len) {
    snprintf(out, max_len, "{\"event\":\"switch-child\"}");
}

static void handle_main_input(LockScreenState *state, SDL_Event *event,
                              char *out, int max_len) {
    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        if (key == SDLK_LEFT || key == SDLK_RIGHT) {
            state->selected_button = (state->selected_button + 1) % MAIN_BTN_COUNT;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (state->selected_button == 0) {
                state->view = LOCK_VIEW_DURATION;
                state->selected_button = 0;
            } else {
                write_switch_event(out, max_len);
            }
        }
    } else if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        Uint8 btn = event->cbutton.button;
        if (btn == SDL_CONTROLLER_BUTTON_DPAD_LEFT ||
            btn == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            state->selected_button = (state->selected_button + 1) % MAIN_BTN_COUNT;
        } else if (btn == SDL_CONTROLLER_BUTTON_A) {
            if (state->selected_button == 0) {
                state->view = LOCK_VIEW_DURATION;
                state->selected_button = 0;
            } else {
                write_switch_event(out, max_len);
            }
        }
    } else if (event->type == SDL_MOUSEBUTTONDOWN &&
               event->button.button == SDL_BUTTON_LEFT) {
        int mx = event->button.x, my = event->button.y;
        if (point_in_rect(mx, my, main_btn_rects[0])) {
            state->view = LOCK_VIEW_DURATION;
            state->selected_button = 0;
        } else if (point_in_rect(mx, my, main_btn_rects[1])) {
            write_switch_event(out, max_len);
        }
    }
}

static void handle_duration_input(LockScreenState *state, SDL_Event *event,
                                  char *out, int max_len) {
    int i;

    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        if (key == SDLK_LEFT) {
            state->selected_button--;
            if (state->selected_button < 0) state->selected_button = DURATION_BTN_COUNT - 1;
        } else if (key == SDLK_RIGHT) {
            state->selected_button++;
            if (state->selected_button >= DURATION_BTN_COUNT) state->selected_button = 0;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            write_request_event(state, DURATION_OPTIONS[state->selected_button],
                                out, max_len);
        } else if (key == SDLK_ESCAPE) {
            state->view = LOCK_VIEW_MAIN;
            state->selected_button = 0;
        }
    } else if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        Uint8 btn = event->cbutton.button;
        if (btn == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
            state->selected_button--;
            if (state->selected_button < 0) state->selected_button = DURATION_BTN_COUNT - 1;
        } else if (btn == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            state->selected_button++;
            if (state->selected_button >= DURATION_BTN_COUNT) state->selected_button = 0;
        } else if (btn == SDL_CONTROLLER_BUTTON_A) {
            write_request_event(state, DURATION_OPTIONS[state->selected_button],
                                out, max_len);
        } else if (btn == SDL_CONTROLLER_BUTTON_B) {
            state->view = LOCK_VIEW_MAIN;
            state->selected_button = 0;
        }
    } else if (event->type == SDL_MOUSEBUTTONDOWN &&
               event->button.button == SDL_BUTTON_LEFT) {
        int mx = event->button.x, my = event->button.y;
        for (i = 0; i < DURATION_BTN_COUNT; i++) {
            if (point_in_rect(mx, my, dur_btn_rects[i])) {
                write_request_event(state, DURATION_OPTIONS[i], out, max_len);
                return;
            }
        }
        if (point_in_rect(mx, my, back_link_rect)) {
            state->view = LOCK_VIEW_MAIN;
            state->selected_button = 0;
        }
    }
}

void screen_lock_input(LockScreenState *state, SDL_Event *event,
                       char *out_event_json, int max_len) {
    if (!state || !event || !out_event_json || max_len < 2) return;
    out_event_json[0] = '\0';

    switch (state->view) {
    case LOCK_VIEW_MAIN:
        handle_main_input(state, event, out_event_json, max_len);
        break;
    case LOCK_VIEW_DURATION:
        handle_duration_input(state, event, out_event_json, max_len);
        break;
    case LOCK_VIEW_PENDING:
    case LOCK_VIEW_DENIED:
        break;
    }
}
