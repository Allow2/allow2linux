/**
 * screen_selector.c -- Child selector screen
 *
 * Vertical list of children centered on screen, with "Parent" entry at end.
 * Controller-navigable (D-pad/arrows), mouse-clickable, wraps around.
 */

#include "screen_selector.h"

#include <stdio.h>
#include <string.h>

/* ---------- Constants ---------- */

#define SELECTOR_LIST_W    600

/* ---------- Helpers ---------- */

static int total_rows(const SelectorScreenState *state)
{
    return state->child_count + (state->show_parent ? 1 : 0);
}

/* ---------- Public API ---------- */

void screen_selector_set(SelectorScreenState *state,
                         const SelectorChildEntry *entries, int count)
{
    int i;
    if (count > SELECTOR_MAX_CHILDREN) count = SELECTOR_MAX_CHILDREN;
    state->child_count = count;
    state->selected_index = 0;
    state->show_parent = 1;
    for (i = 0; i < count; i++) {
        state->children[i] = entries[i];
    }
}

void screen_selector_render(SDL_Renderer *renderer, SelectorScreenState *state)
{
    int screen_w, screen_h;
    int rows, list_h, list_x, list_y;
    int i, row_y;
    SDL_Color white   = {COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255};
    SDL_Color gray    = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};
    SDL_Color div_col = {COLOR_DIV_R, COLOR_DIV_G, COLOR_DIV_B, 255};

    render_get_screen_size(&screen_w, &screen_h);
    render_background(renderer, COLOR_BG_A);

    /* Title */
    {
        TTF_Font *title_font = render_get_font(FONT_BOLD_36);
        if (title_font) {
            render_text_centered(renderer, title_font, "Who's playing?",
                                 screen_h / 2 - 300, white);
        }
    }

    rows = total_rows(state);
    list_h = rows * SELECTOR_ROW_H;
    list_x = (screen_w - SELECTOR_LIST_W) / 2;
    list_y = (screen_h - list_h) / 2;
    if (list_y < screen_h / 2 - 240) list_y = screen_h / 2 - 240;

    for (i = 0; i < rows; i++) {
        int is_parent_row = (state->show_parent && i == state->child_count);
        int is_selected   = (i == state->selected_index);

        row_y = list_y + i * SELECTOR_ROW_H;

        /* Divider before parent row */
        if (is_parent_row) {
            render_filled_rect(renderer, list_x, row_y,
                               SELECTOR_LIST_W, 2, div_col);
            row_y += 2;
        }

        /* Highlight background */
        if (is_selected) {
            SDL_Color hl = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 77};
            render_rounded_rect(renderer, list_x, row_y,
                                SELECTOR_LIST_W, SELECTOR_ROW_H,
                                BUTTON_RADIUS, hl);
        }

        /* Avatar */
        {
            int avatar_x = list_x + PADDING_MAJOR;
            int avatar_y = row_y + (SELECTOR_ROW_H - AVATAR_SIZE) / 2;

            if (is_parent_row) {
                render_avatar(renderer, 0, "Parent", NULL,
                              avatar_x, avatar_y, AVATAR_SIZE);
            } else {
                const char *path = state->children[i].avatar_path;
                if (path[0] == '\0') path = NULL;
                render_avatar(renderer, state->children[i].id,
                              state->children[i].name, path,
                              avatar_x, avatar_y, AVATAR_SIZE);
            }
        }

        /* Name */
        {
            TTF_Font *name_font = render_get_font(FONT_REGULAR_28);
            if (name_font) {
                int text_x = list_x + PADDING_MAJOR + AVATAR_SIZE + PADDING_MAJOR;
                int text_y = row_y + (SELECTOR_ROW_H - 28) / 2;
                const char *label = is_parent_row ? "Parent" : state->children[i].name;
                render_text(renderer, name_font, label, text_x, text_y, white);
            }
        }

        /* Arrow on selected row */
        if (is_selected) {
            TTF_Font *arrow_font = render_get_font(FONT_REGULAR_28);
            if (arrow_font) {
                int ax = list_x + SELECTOR_LIST_W - PADDING_MAJOR - 20;
                int ay = row_y + (SELECTOR_ROW_H - 28) / 2;
                render_text(renderer, arrow_font, "\xE2\x96\xBA", ax, ay, white);
            }
        }
    }

    /* Controller hints */
    {
        TTF_Font *hint_font = render_get_font(FONT_REGULAR_16);
        if (hint_font) {
            render_text_centered(renderer, hint_font,
                                 "D-pad: navigate    A: select",
                                 screen_h - 60, gray);
        }
    }
}

void screen_selector_input(SelectorScreenState *state, SDL_Event *event,
                            char *out_event_json, int max_len)
{
    int rows = total_rows(state);
    out_event_json[0] = '\0';
    if (rows == 0) return;

    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        if (key == SDLK_UP) {
            state->selected_index--;
            if (state->selected_index < 0) state->selected_index = rows - 1;
        } else if (key == SDLK_DOWN) {
            state->selected_index++;
            if (state->selected_index >= rows) state->selected_index = 0;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (state->show_parent && state->selected_index == state->child_count) {
                snprintf(out_event_json, max_len, "{\"event\":\"parent-selected\"}");
            } else {
                snprintf(out_event_json, max_len,
                         "{\"event\":\"child-selected\",\"childId\":%d}",
                         state->children[state->selected_index].id);
            }
        }
    }

    if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        Uint8 btn = event->cbutton.button;
        if (btn == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            state->selected_index--;
            if (state->selected_index < 0) state->selected_index = rows - 1;
        } else if (btn == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            state->selected_index++;
            if (state->selected_index >= rows) state->selected_index = 0;
        } else if (btn == SDL_CONTROLLER_BUTTON_A) {
            if (state->show_parent && state->selected_index == state->child_count) {
                snprintf(out_event_json, max_len, "{\"event\":\"parent-selected\"}");
            } else {
                snprintf(out_event_json, max_len,
                         "{\"event\":\"child-selected\",\"childId\":%d}",
                         state->children[state->selected_index].id);
            }
        }
    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int screen_w, screen_h;
        int list_x, list_y, list_h;
        int mx = event->button.x, my = event->button.y;
        int clicked_row;

        render_get_screen_size(&screen_w, &screen_h);
        list_h = rows * SELECTOR_ROW_H;
        list_x = (screen_w - SELECTOR_LIST_W) / 2;
        list_y = (screen_h - list_h) / 2;
        if (list_y < screen_h / 2 - 240) list_y = screen_h / 2 - 240;

        if (mx >= list_x && mx <= list_x + SELECTOR_LIST_W &&
            my >= list_y && my <= list_y + list_h) {
            clicked_row = (my - list_y) / SELECTOR_ROW_H;
            if (clicked_row >= 0 && clicked_row < rows) {
                state->selected_index = clicked_row;
                if (state->show_parent && clicked_row == state->child_count) {
                    snprintf(out_event_json, max_len,
                             "{\"event\":\"parent-selected\"}");
                } else {
                    snprintf(out_event_json, max_len,
                             "{\"event\":\"child-selected\",\"childId\":%d}",
                             state->children[clicked_row].id);
                }
            }
        }
    }
}
