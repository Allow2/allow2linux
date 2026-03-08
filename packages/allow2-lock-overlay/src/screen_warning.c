/**
 * screen_warning.c -- Semi-transparent warning bar overlay
 *
 * Renders a 60px top bar with activity name, remaining time, and
 * optional "Request More Time" button. The rest of the screen is
 * transparent so the game/app remains visible and playable.
 */

#include "screen_warning.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---- Constants ---- */

#define BAR_HEIGHT      60
#define ACCENT_WIDTH     4
#define BAR_PAD_LEFT    20
#define BAR_PAD_RIGHT   20

#define REQ_BTN_W      140
#define REQ_BTN_H       40

/* Warning bar background: #14141E at 85% */
#define BAR_BG_A    217

/* Hit-test rectangle for request button */
static SDL_Rect request_btn_rect;

/* ---- Helpers ---- */

static SDL_Color accent_for_level(WarningLevel level, float pulse_alpha) {
    SDL_Color info    = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255};
    SDL_Color urgent  = {246, 173, 85, 255};
    SDL_Color final_c = {252, 129, 129, 255};

    switch (level) {
    case WARNING_LEVEL_INFO:    return info;
    case WARNING_LEVEL_URGENT:  return urgent;
    case WARNING_LEVEL_FINAL:   return final_c;
    case WARNING_LEVEL_COUNTDOWN:
        final_c.a = (Uint8)(pulse_alpha * 255.0f);
        return final_c;
    }
    return info;
}

static void format_remaining(int seconds, char *buf, int buflen) {
    if (seconds >= 60) {
        int mins = seconds / 60;
        snprintf(buf, buflen, "%d minute%s remaining", mins, mins == 1 ? "" : "s");
    } else {
        snprintf(buf, buflen, "%d second%s remaining", seconds, seconds == 1 ? "" : "s");
    }
}

/* ---- Public functions ---- */

void screen_warning_set(WarningScreenState *state, const char *activity,
                        int activity_id, int remaining, const char *level_str) {
    if (!state) return;
    memset(state, 0, sizeof(*state));

    if (activity) strncpy(state->activity, activity, sizeof(state->activity) - 1);
    state->activity_id = activity_id;
    state->remaining = remaining;

    if (!level_str || strcmp(level_str, "info") == 0) {
        state->level = WARNING_LEVEL_INFO;
    } else if (strcmp(level_str, "urgent") == 0) {
        state->level = WARNING_LEVEL_URGENT;
    } else if (strcmp(level_str, "final") == 0) {
        state->level = WARNING_LEVEL_FINAL;
    } else if (strcmp(level_str, "countdown") == 0) {
        state->level = WARNING_LEVEL_COUNTDOWN;
    } else {
        state->level = WARNING_LEVEL_INFO;
    }

    state->show_request_btn = (state->level >= WARNING_LEVEL_URGENT) ? 1 : 0;
}

void screen_warning_render(SDL_Renderer *renderer, WarningScreenState *state,
                           float dt) {
    SDL_Color accent;
    char time_str[64];
    char display_text[192];
    SDL_Color white = {COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255};
    TTF_Font *body_font = render_get_font(FONT_BOLD_20);
    int text_x, text_y;
    float pulse_alpha;

    if (!renderer || !state) return;

    /* Pulse animation */
    state->pulse_phase += dt * 4.0f;
    if (state->pulse_phase > 6.2831853f) state->pulse_phase -= 6.2831853f;
    pulse_alpha = 0.4f + 0.6f * (0.5f + 0.5f * sinf(state->pulse_phase));

    /* Clear to transparent */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    /* Bar background */
    render_filled_rect(renderer, 0, 0, LOGICAL_W, BAR_HEIGHT,
                       (SDL_Color){COLOR_BG_R, COLOR_BG_G, COLOR_BG_B, BAR_BG_A});

    /* Left accent strip */
    accent = accent_for_level(state->level, pulse_alpha);
    render_filled_rect(renderer, 0, 0, ACCENT_WIDTH, BAR_HEIGHT, accent);

    /* Activity + time text */
    format_remaining(state->remaining, time_str, sizeof(time_str));
    snprintf(display_text, sizeof(display_text), "%s: %s",
             state->activity, time_str);

    text_x = ACCENT_WIDTH + BAR_PAD_LEFT;
    text_y = (BAR_HEIGHT - 20) / 2;
    if (body_font) {
        render_text(renderer, body_font, display_text, text_x, text_y, white);
    }

    /* Request button (urgent and above) */
    if (state->show_request_btn) {
        SDL_Color btn_accent = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255};
        int btn_x = LOGICAL_W - BAR_PAD_RIGHT - REQ_BTN_W;
        int btn_y_pos = (BAR_HEIGHT - REQ_BTN_H) / 2;
        render_button(renderer, "Request More Time",
                      btn_x, btn_y_pos, REQ_BTN_W, REQ_BTN_H,
                      state->btn_highlighted, btn_accent);
        request_btn_rect = (SDL_Rect){btn_x, btn_y_pos, REQ_BTN_W, REQ_BTN_H};
    } else {
        memset(&request_btn_rect, 0, sizeof(request_btn_rect));
    }
}

/* ---- Input ---- */

static int point_in_rect(int px, int py, SDL_Rect r) {
    return (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h);
}

void screen_warning_input(WarningScreenState *state, SDL_Event *event,
                          char *out_event_json, int max_len) {
    if (!state || !event || !out_event_json || max_len < 2) return;
    out_event_json[0] = '\0';

    if (!state->show_request_btn) return;

    if (event->type == SDL_MOUSEMOTION) {
        state->btn_highlighted = point_in_rect(event->motion.x, event->motion.y,
                                               request_btn_rect);
    } else if (event->type == SDL_MOUSEBUTTONDOWN &&
               event->button.button == SDL_BUTTON_LEFT) {
        if (point_in_rect(event->button.x, event->button.y, request_btn_rect)) {
            snprintf(out_event_json, max_len,
                     "{\"event\":\"request-more-time\",\"activityId\":%d,\"duration\":15}",
                     state->activity_id);
        }
    } else if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F12) {
        snprintf(out_event_json, max_len,
                 "{\"event\":\"request-more-time\",\"activityId\":%d,\"duration\":15}",
                 state->activity_id);
    }
}
