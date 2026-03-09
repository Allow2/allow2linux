/**
 * screen_status.c -- Status screen showing pairing info and activity times
 *
 * Displayed when the user launches the Allow2 app on an already-paired device.
 * Shows child name, remaining time per activity, and a "Request More Time" button.
 * In parent mode, shows "Parent Mode -- No Restrictions" with no request button.
 */

#include "screen_status.h"

#include <stdio.h>
#include <string.h>

/* ---- Constants ---- */

#define BTN_W         220
#define BTN_H          50
#define ROW_H          36
#define ACTIVITY_X    400
#define TIME_X        780

/* Hit-test rectangles */
static SDL_Rect request_btn_rect;
static SDL_Rect report_btn_rect;

/* ---- Helpers ---- */

static void format_time(int seconds, char *out, int maxlen) {
    if (seconds <= 0) {
        snprintf(out, maxlen, "0 min");
    } else if (seconds >= 3600) {
        int h = seconds / 3600;
        int m = (seconds % 3600) / 60;
        if (m > 0) {
            snprintf(out, maxlen, "%dh %dm", h, m);
        } else {
            snprintf(out, maxlen, "%dh", h);
        }
    } else {
        int m = seconds / 60;
        if (m < 1) m = 1;  /* Show at least "1 min" if nonzero seconds */
        snprintf(out, maxlen, "%d min", m);
    }
}

/* ---- Public functions ---- */

void screen_status_set(StatusScreenState *state, const char *family,
                       const char *child, int child_id, int is_parent) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    if (family) strncpy(state->family_name, family, sizeof(state->family_name) - 1);
    if (child) strncpy(state->child_name, child, sizeof(state->child_name) - 1);
    state->child_id = child_id;
    state->is_parent = is_parent;
}

void screen_status_add_activity(StatusScreenState *state, const char *name,
                                int remaining) {
    if (!state || state->activity_count >= STATUS_MAX_ACTIVITIES) return;
    if (name) {
        strncpy(state->activities[state->activity_count].name, name,
                sizeof(state->activities[0].name) - 1);
    }
    state->activities[state->activity_count].remaining_seconds = remaining;
    state->activity_count++;
}

/* ---- Render ---- */

void screen_status_render(SDL_Renderer *renderer, StatusScreenState *state) {
    SDL_Color white  = {COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255};
    SDL_Color gray   = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};
    SDL_Color accent = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255};
    SDL_Color green  = {100, 220, 120, 255};
    SDL_Color hint_c = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 180};

    TTF_Font *title_font = render_get_font(FONT_BOLD_48);
    TTF_Font *sub_font   = render_get_font(FONT_REGULAR_22);
    TTF_Font *body_font  = render_get_font(FONT_REGULAR_20);
    TTF_Font *bold_font  = render_get_font(FONT_BOLD_22);
    TTF_Font *hint_font  = render_get_font(FONT_REGULAR_16);
    int y;
    int i;

    if (!renderer || !state) return;

    render_background(renderer, COLOR_BG_A);

    /* Title */
    y = 60;
    if (title_font) {
        render_text_centered(renderer, title_font, "Allow2", y, white);
    }

    /* Subtitle: paired status */
    y += 70;
    if (sub_font) {
        render_text_centered(renderer, sub_font,
                             "This device is paired", y, green);
    }

    /* Family name (if provided) */
    if (state->family_name[0] && sub_font) {
        y += 36;
        render_text_centered(renderer, sub_font, state->family_name, y, gray);
    }

    /* Divider */
    y += 50;
    {
        SDL_Color div_color = {COLOR_DIV_R, COLOR_DIV_G, COLOR_DIV_B, 255};
        render_filled_rect(renderer, LOGICAL_W / 4, y, LOGICAL_W / 2, 1, div_color);
    }
    y += 24;

    /* Parent mode */
    if (state->is_parent) {
        if (bold_font) {
            render_text_centered(renderer, bold_font,
                                 "Parent Mode \xe2\x80\x94 No Restrictions",
                                 y + 40, white);
        }
    }
    /* No child selected */
    else if (state->child_id == 0) {
        if (body_font) {
            render_text_centered(renderer, body_font,
                                 "No child signed in", y + 40, gray);
        }
    }
    /* Child signed in */
    else {
        /* Child name + avatar */
        if (bold_font) {
            char label[192];
            snprintf(label, sizeof(label), "Signed in as: %s", state->child_name);
            render_text_centered(renderer, bold_font, label, y, white);
        }
        y += 44;

        /* Activity list */
        if (state->activity_count > 0 && body_font) {
            for (i = 0; i < state->activity_count; i++) {
                char time_str[32];
                format_time(state->activities[i].remaining_seconds,
                            time_str, sizeof(time_str));

                render_text(renderer, body_font,
                            state->activities[i].name,
                            ACTIVITY_X, y, gray);
                render_text(renderer, body_font,
                            time_str,
                            TIME_X, y, white);
                y += ROW_H;
            }
        } else if (body_font) {
            render_text_centered(renderer, body_font,
                                 "No activity limits active", y, gray);
            y += ROW_H;
        }

        /* Request More Time button */
        y += 20;
        {
            int bx = (LOGICAL_W - BTN_W) / 2;
            render_button(renderer, "Request More Time",
                          bx, y, BTN_W, BTN_H, 1, accent);
            request_btn_rect = (SDL_Rect){bx, y, BTN_W, BTN_H};
        }

        /* Report Issue button (only when feedback is available) */
        if (state->can_submit_feedback) {
            y += BTN_H + 12;
            {
                int bx = (LOGICAL_W - BTN_W) / 2;
                render_button(renderer, "Report Issue",
                              bx, y, BTN_W, BTN_H, 0, accent);
                report_btn_rect = (SDL_Rect){bx, y, BTN_W, BTN_H};
            }
        }
    }

    /* Gamepad hints at bottom */
    if (hint_font) {
        const char *hint_text;
        if (!state->is_parent && state->child_id > 0) {
            if (state->can_submit_feedback) {
                hint_text = "B: Close    A: Request More Time    Y: Report Issue";
            } else {
                hint_text = "B: Close    A: Request More Time";
            }
        } else {
            hint_text = "B: Close";
        }
        render_text_centered(renderer, hint_font, hint_text,
                             LOGICAL_H - 50, hint_c);
    }
}

/* ---- Input ---- */

static int point_in_rect(int px, int py, SDL_Rect r) {
    return (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h);
}

static void write_request_event(StatusScreenState *state, char *out, int max_len) {
    snprintf(out, max_len,
             "{\"event\":\"request-more-time\",\"childId\":%d}",
             state->child_id);
}

static void write_close_event(char *out, int max_len) {
    snprintf(out, max_len, "{\"event\":\"app-close\"}");
}

static void write_report_issue_event(char *out, int max_len) {
    snprintf(out, max_len, "{\"event\":\"report-issue\"}");
}

void screen_status_input(StatusScreenState *state, SDL_Event *event,
                         char *out_event_json, int max_len) {
    if (!state || !event || !out_event_json || max_len < 2) return;
    out_event_json[0] = '\0';

    /* A / Enter = request more time (only when child is signed in, not parent) */
    /* B / ESC = close */

    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        if (key == SDLK_ESCAPE) {
            write_close_event(out_event_json, max_len);
        } else if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) &&
                   !state->is_parent && state->child_id > 0) {
            write_request_event(state, out_event_json, max_len);
        } else if (key == SDLK_r && state->can_submit_feedback) {
            write_report_issue_event(out_event_json, max_len);
        }
    } else if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        Uint8 btn = event->cbutton.button;
        if (btn == SDL_CONTROLLER_BUTTON_B) {
            write_close_event(out_event_json, max_len);
        } else if (btn == SDL_CONTROLLER_BUTTON_A &&
                   !state->is_parent && state->child_id > 0) {
            write_request_event(state, out_event_json, max_len);
        } else if (btn == SDL_CONTROLLER_BUTTON_Y &&
                   state->can_submit_feedback) {
            write_report_issue_event(out_event_json, max_len);
        }
    } else if (event->type == SDL_MOUSEBUTTONDOWN &&
               event->button.button == SDL_BUTTON_LEFT) {
        int mx = event->button.x, my = event->button.y;
        if (!state->is_parent && state->child_id > 0 &&
            point_in_rect(mx, my, request_btn_rect)) {
            write_request_event(state, out_event_json, max_len);
        }
        if (state->can_submit_feedback &&
            point_in_rect(mx, my, report_btn_rect)) {
            write_report_issue_event(out_event_json, max_len);
        }
    }
}
