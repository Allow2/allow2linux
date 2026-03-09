/**
 * screen_selector.c -- Child selector screen
 *
 * Vertical list of children centered on screen, with search bar at top,
 * scrolling support, last-used time display, and "Parent" entry pinned
 * at the bottom.
 *
 * Controller-navigable (D-pad/arrows), mouse-clickable, keyboard search.
 */

#include "screen_selector.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---------- Constants ---------- */

#define SELECTOR_LIST_W    600
#define SEARCH_BAR_H       48
#define SEARCH_BAR_W       500
#define SCROLL_IND_W        8
#define SCROLL_IND_MARGIN   6

/* ---------- Helpers ---------- */

/**
 * Case-insensitive substring search.
 * Returns 1 if needle is found in haystack, 0 otherwise.
 */
static int strcasestr_match(const char *haystack, const char *needle)
{
    int hlen, nlen, i, j;
    if (needle[0] == '\0') return 1;
    hlen = (int)strlen(haystack);
    nlen = (int)strlen(needle);
    if (nlen > hlen) return 0;
    for (i = 0; i <= hlen - nlen; i++) {
        for (j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j]))
                break;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

/**
 * Build a filtered index array of children matching search_text.
 * Returns the count of matched children.
 * filtered_indices must have room for SELECTOR_MAX_CHILDREN entries.
 */
static int build_filtered_list(const SelectorScreenState *state,
                               int *filtered_indices)
{
    int i, count = 0;
    for (i = 0; i < state->child_count; i++) {
        if (strcasestr_match(state->children[i].name, state->search_text)) {
            filtered_indices[count++] = i;
        }
    }
    return count;
}

/**
 * Total navigable rows: filtered children + parent (if shown).
 */
static int total_filtered_rows(const SelectorScreenState *state,
                               int filtered_count)
{
    return filtered_count + (state->show_parent ? 1 : 0);
}

/**
 * Parse an ISO 8601 timestamp (YYYY-MM-DDTHH:MM:SS or YYYY-MM-DDTHH:MM:SSZ)
 * and format a human-readable relative time string.
 * Output examples: "2h ago", "yesterday", "3d ago", "1w ago", "2w ago"
 * If the timestamp is empty/null, out is set to "".
 */
static void format_relative_time(const char *iso_timestamp, char *out, int max_len)
{
    struct tm tm_parsed;
    time_t then, now;
    double diff_sec;
    int diff_min, diff_hr, diff_day, diff_wk;

    out[0] = '\0';
    if (iso_timestamp == NULL || iso_timestamp[0] == '\0') return;
    if (max_len < 2) return;

    memset(&tm_parsed, 0, sizeof(tm_parsed));

    /* Parse YYYY-MM-DDTHH:MM:SS */
    if (sscanf(iso_timestamp, "%d-%d-%dT%d:%d:%d",
               &tm_parsed.tm_year, &tm_parsed.tm_mon, &tm_parsed.tm_mday,
               &tm_parsed.tm_hour, &tm_parsed.tm_min, &tm_parsed.tm_sec) < 6) {
        /* Try date-only: YYYY-MM-DD */
        if (sscanf(iso_timestamp, "%d-%d-%d",
                   &tm_parsed.tm_year, &tm_parsed.tm_mon, &tm_parsed.tm_mday) < 3) {
            return;
        }
        tm_parsed.tm_hour = 0;
        tm_parsed.tm_min = 0;
        tm_parsed.tm_sec = 0;
    }

    tm_parsed.tm_year -= 1900;
    tm_parsed.tm_mon -= 1;
    tm_parsed.tm_isdst = -1;

    then = mktime(&tm_parsed);
    if (then == (time_t)-1) return;

    now = time(NULL);
    diff_sec = difftime(now, then);
    if (diff_sec < 0) {
        snprintf(out, max_len, "now");
        return;
    }

    diff_min = (int)(diff_sec / 60.0);
    diff_hr  = diff_min / 60;
    diff_day = diff_hr / 24;
    diff_wk  = diff_day / 7;

    if (diff_min < 1) {
        snprintf(out, max_len, "now");
    } else if (diff_min < 60) {
        snprintf(out, max_len, "%dm ago", diff_min);
    } else if (diff_hr < 24) {
        snprintf(out, max_len, "%dh ago", diff_hr);
    } else if (diff_day == 1) {
        snprintf(out, max_len, "yesterday");
    } else if (diff_day < 7) {
        snprintf(out, max_len, "%dd ago", diff_day);
    } else {
        snprintf(out, max_len, "%dw ago", diff_wk);
    }
}

/**
 * Clamp scroll_offset so selected_index is visible.
 */
static void clamp_scroll(SelectorScreenState *state, int filtered_count)
{
    int nav_rows;

    /* visible_rows only counts child rows (parent is pinned, not scrolled) */
    if (state->visible_rows <= 0) return;

    nav_rows = filtered_count;  /* only children scroll */

    /* If selected_index points to parent (== filtered_count), keep children
       scroll in range but don't force scroll for parent. */
    if (state->selected_index < nav_rows) {
        if (state->selected_index < state->scroll_offset) {
            state->scroll_offset = state->selected_index;
        }
        if (state->selected_index >= state->scroll_offset + state->visible_rows) {
            state->scroll_offset = state->selected_index - state->visible_rows + 1;
        }
    }

    /* General bounds */
    if (state->scroll_offset > nav_rows - state->visible_rows) {
        state->scroll_offset = nav_rows - state->visible_rows;
    }
    if (state->scroll_offset < 0) {
        state->scroll_offset = 0;
    }
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
    state->scroll_offset = 0;
    state->visible_rows = 0;
    state->search_text[0] = '\0';
    state->search_len = 0;
    for (i = 0; i < count; i++) {
        state->children[i] = entries[i];
    }
}

void screen_selector_render(SDL_Renderer *renderer, SelectorScreenState *state)
{
    int screen_w, screen_h;
    int filtered_indices[SELECTOR_MAX_CHILDREN];
    int filtered_count;
    int list_x, list_y, list_top;
    int child_area_h, parent_area_y;
    int row_y, vis_idx;
    int children_scrollable;
    char rel_time[32];
    SDL_Color white   = {COLOR_TEXT_R, COLOR_TEXT_G, COLOR_TEXT_B, 255};
    SDL_Color gray    = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 255};
    SDL_Color div_col = {COLOR_DIV_R, COLOR_DIV_G, COLOR_DIV_B, 255};

    render_get_screen_size(&screen_w, &screen_h);
    render_background(renderer, COLOR_BG_A);

    /* Build filtered list */
    filtered_count = build_filtered_list(state, filtered_indices);

    /* Title */
    {
        TTF_Font *title_font = render_get_font(FONT_BOLD_36);
        if (title_font) {
            render_text_centered(renderer, title_font, "Who's playing?",
                                 PADDING_MAJOR * 2, white);
        }
    }

    /* Search bar */
    {
        int bar_x = (screen_w - SEARCH_BAR_W) / 2;
        int bar_y = PADDING_MAJOR * 2 + 44 + PADDING_MINOR;
        SDL_Color bar_bg = {COLOR_BTN_R, COLOR_BTN_G, COLOR_BTN_B, 200};
        SDL_Color cursor_col = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 255};
        TTF_Font *search_font = render_get_font(FONT_REGULAR_22);

        render_rounded_rect(renderer, bar_x, bar_y,
                            SEARCH_BAR_W, SEARCH_BAR_H,
                            BUTTON_RADIUS, bar_bg);

        if (search_font) {
            int text_x = bar_x + PADDING_MINOR;
            int text_y = bar_y + (SEARCH_BAR_H - 22) / 2;

            if (state->search_len == 0) {
                /* Placeholder text */
                SDL_Color placeholder = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 128};
                render_text(renderer, search_font, "Type to search...",
                            text_x, text_y, placeholder);
            } else {
                render_text(renderer, search_font, state->search_text,
                            text_x, text_y, white);
            }

            /* Blinking cursor */
            {
                int blink = ((SDL_GetTicks() / 500) % 2 == 0);
                if (blink) {
                    int cursor_x;
                    if (state->search_len > 0) {
                        cursor_x = text_x + render_text_width(search_font,
                                                              state->search_text) + 2;
                    } else {
                        cursor_x = text_x;
                    }
                    render_filled_rect(renderer, cursor_x, text_y, 2, 22, cursor_col);
                }
            }
        }

        list_top = bar_y + SEARCH_BAR_H + PADDING_MINOR;
    }

    /* Calculate layout: children area + pinned parent area */
    list_x = (screen_w - SELECTOR_LIST_W) / 2;

    /* Reserve space for parent row at bottom (+ divider) */
    parent_area_y = screen_h - SELECTOR_ROW_H - 60 - 2;
    /* 60 = hint area height, 2 = divider */

    child_area_h = parent_area_y - list_top - PADDING_MINOR;
    if (child_area_h < SELECTOR_ROW_H) child_area_h = SELECTOR_ROW_H;

    state->visible_rows = child_area_h / SELECTOR_ROW_H;
    if (state->visible_rows < 1) state->visible_rows = 1;

    /* Clamp scroll */
    clamp_scroll(state, filtered_count);

    list_y = list_top;

    /* Render visible children rows */
    children_scrollable = (filtered_count > state->visible_rows);

    for (vis_idx = 0; vis_idx < state->visible_rows && vis_idx + state->scroll_offset < filtered_count; vis_idx++) {
        int fi = vis_idx + state->scroll_offset;  /* index in filtered list */
        int ci = filtered_indices[fi];             /* index in children array */
        int is_selected = (fi == state->selected_index);

        row_y = list_y + vis_idx * SELECTOR_ROW_H;

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
            const char *path = state->children[ci].avatar_path;
            if (path[0] == '\0') path = NULL;
            render_avatar(renderer, state->children[ci].id,
                          state->children[ci].name, path,
                          avatar_x, avatar_y, AVATAR_SIZE);
        }

        /* Name */
        {
            TTF_Font *name_font = render_get_font(FONT_REGULAR_28);
            if (name_font) {
                int text_x = list_x + PADDING_MAJOR + AVATAR_SIZE + PADDING_MAJOR;
                int text_y = row_y + (SELECTOR_ROW_H - 28) / 2;
                render_text(renderer, name_font, state->children[ci].name,
                            text_x, text_y, white);
            }
        }

        /* Last used time (right side) */
        format_relative_time(state->children[ci].last_used_at, rel_time, sizeof(rel_time));
        if (rel_time[0] != '\0') {
            TTF_Font *time_font = render_get_font(FONT_REGULAR_16);
            if (time_font) {
                int tw = render_text_width(time_font, rel_time);
                int tx = list_x + SELECTOR_LIST_W - PADDING_MAJOR - tw;
                int ty;
                if (is_selected) {
                    /* Move left to not overlap arrow */
                    tx -= 28;
                }
                ty = row_y + (SELECTOR_ROW_H - 16) / 2;
                render_text(renderer, time_font, rel_time, tx, ty, gray);
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

    /* Scroll indicators on right side */
    if (children_scrollable) {
        int track_x = list_x + SELECTOR_LIST_W + SCROLL_IND_MARGIN;
        int track_y = list_y;
        int track_h = state->visible_rows * SELECTOR_ROW_H;
        int thumb_h, thumb_y;
        SDL_Color track_col = {COLOR_DIV_R, COLOR_DIV_G, COLOR_DIV_B, 100};
        SDL_Color thumb_col = {COLOR_TEXT2_R, COLOR_TEXT2_G, COLOR_TEXT2_B, 180};

        /* Track background */
        render_rounded_rect(renderer, track_x, track_y,
                            SCROLL_IND_W, track_h,
                            SCROLL_IND_W / 2, track_col);

        /* Thumb */
        if (filtered_count > 0) {
            thumb_h = (state->visible_rows * track_h) / filtered_count;
            if (thumb_h < 20) thumb_h = 20;
            if (thumb_h > track_h) thumb_h = track_h;
            thumb_y = track_y;
            if (filtered_count > state->visible_rows) {
                thumb_y = track_y + (state->scroll_offset * (track_h - thumb_h))
                          / (filtered_count - state->visible_rows);
            }
            render_rounded_rect(renderer, track_x, thumb_y,
                                SCROLL_IND_W, thumb_h,
                                SCROLL_IND_W / 2, thumb_col);
        }

        /* Up arrow indicator */
        if (state->scroll_offset > 0) {
            TTF_Font *ind_font = render_get_font(FONT_REGULAR_16);
            if (ind_font) {
                render_text(renderer, ind_font, "\xE2\x96\xB2",
                            track_x - 4, track_y - 20, gray);
            }
        }

        /* Down arrow indicator */
        if (state->scroll_offset + state->visible_rows < filtered_count) {
            TTF_Font *ind_font = render_get_font(FONT_REGULAR_16);
            if (ind_font) {
                render_text(renderer, ind_font, "\xE2\x96\xBC",
                            track_x - 4, track_y + track_h + 4, gray);
            }
        }
    }

    /* Pinned parent row at bottom */
    if (state->show_parent) {
        int is_parent_selected = (state->selected_index == filtered_count);

        /* Divider line above parent */
        render_filled_rect(renderer, list_x, parent_area_y,
                           SELECTOR_LIST_W, 2, div_col);

        row_y = parent_area_y + 2;

        /* Highlight background */
        if (is_parent_selected) {
            SDL_Color hl = {COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B, 77};
            render_rounded_rect(renderer, list_x, row_y,
                                SELECTOR_LIST_W, SELECTOR_ROW_H,
                                BUTTON_RADIUS, hl);
        }

        /* Avatar */
        {
            int avatar_x = list_x + PADDING_MAJOR;
            int avatar_y = row_y + (SELECTOR_ROW_H - AVATAR_SIZE) / 2;
            render_avatar(renderer, 0, "Parent", NULL,
                          avatar_x, avatar_y, AVATAR_SIZE);
        }

        /* Name */
        {
            TTF_Font *name_font = render_get_font(FONT_REGULAR_28);
            if (name_font) {
                int text_x = list_x + PADDING_MAJOR + AVATAR_SIZE + PADDING_MAJOR;
                int text_y = row_y + (SELECTOR_ROW_H - 28) / 2;
                render_text(renderer, name_font, "Parent", text_x, text_y, white);
            }
        }

        /* Arrow on selected row */
        if (is_parent_selected) {
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
            const char *hint = state->search_len > 0
                ? "D-pad: navigate    A: select    Backspace: clear search"
                : "D-pad: navigate    A: select    Type: search";
            render_text_centered(renderer, hint_font, hint,
                                 screen_h - 40, gray);
        }
    }
}

void screen_selector_input(SelectorScreenState *state, SDL_Event *event,
                            char *out_event_json, int max_len)
{
    int filtered_indices[SELECTOR_MAX_CHILDREN];
    int filtered_count;
    int nav_rows;

    out_event_json[0] = '\0';

    filtered_count = build_filtered_list(state, filtered_indices);
    nav_rows = total_filtered_rows(state, filtered_count);
    if (nav_rows == 0) return;

    /* --- Text input for search --- */
    if (event->type == SDL_TEXTINPUT) {
        int add_len = (int)strlen(event->text.text);
        if (state->search_len + add_len < (int)sizeof(state->search_text) - 1) {
            memcpy(state->search_text + state->search_len, event->text.text, add_len);
            state->search_len += add_len;
            state->search_text[state->search_len] = '\0';
            /* Reset navigation on search change */
            state->selected_index = 0;
            state->scroll_offset = 0;
        }
        return;
    }

    /* --- Keyboard --- */
    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;

        if (key == SDLK_BACKSPACE) {
            if (state->search_len > 0) {
                state->search_len--;
                state->search_text[state->search_len] = '\0';
                /* Reset navigation on search change */
                state->selected_index = 0;
                state->scroll_offset = 0;
                /* Rebuild filtered count after change */
                filtered_count = build_filtered_list(state, filtered_indices);
                nav_rows = total_filtered_rows(state, filtered_count);
            }
            return;
        }

        if (key == SDLK_ESCAPE && state->search_len > 0) {
            /* Clear search */
            state->search_text[0] = '\0';
            state->search_len = 0;
            state->selected_index = 0;
            state->scroll_offset = 0;
            return;
        }

        if (key == SDLK_UP) {
            state->selected_index--;
            if (state->selected_index < 0) state->selected_index = nav_rows - 1;
            clamp_scroll(state, filtered_count);
        } else if (key == SDLK_DOWN) {
            state->selected_index++;
            if (state->selected_index >= nav_rows) state->selected_index = 0;
            clamp_scroll(state, filtered_count);
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (state->show_parent && state->selected_index == filtered_count) {
                snprintf(out_event_json, max_len, "{\"event\":\"parent-selected\"}");
            } else if (state->selected_index < filtered_count) {
                int ci = filtered_indices[state->selected_index];
                snprintf(out_event_json, max_len,
                         "{\"event\":\"child-selected\",\"childId\":%d}",
                         state->children[ci].id);
            }
        }
    }

    /* --- Controller --- */
    if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        Uint8 btn = event->cbutton.button;
        if (btn == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            state->selected_index--;
            if (state->selected_index < 0) state->selected_index = nav_rows - 1;
            clamp_scroll(state, filtered_count);
        } else if (btn == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            state->selected_index++;
            if (state->selected_index >= nav_rows) state->selected_index = 0;
            clamp_scroll(state, filtered_count);
        } else if (btn == SDL_CONTROLLER_BUTTON_A) {
            if (state->show_parent && state->selected_index == filtered_count) {
                snprintf(out_event_json, max_len, "{\"event\":\"parent-selected\"}");
            } else if (state->selected_index < filtered_count) {
                int ci = filtered_indices[state->selected_index];
                snprintf(out_event_json, max_len,
                         "{\"event\":\"child-selected\",\"childId\":%d}",
                         state->children[ci].id);
            }
        }
    }

    /* --- Mouse click --- */
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int screen_w, screen_h;
        int list_x, list_top, parent_area_y_click;
        int mx = event->button.x, my = event->button.y;

        render_get_screen_size(&screen_w, &screen_h);
        list_x = (screen_w - SELECTOR_LIST_W) / 2;

        /* Reconstruct list_top from render layout */
        list_top = PADDING_MAJOR * 2 + 44 + PADDING_MINOR + SEARCH_BAR_H + PADDING_MINOR;
        parent_area_y_click = screen_h - SELECTOR_ROW_H - 60 - 2;

        /* Check parent row click (pinned at bottom) */
        if (state->show_parent &&
            mx >= list_x && mx <= list_x + SELECTOR_LIST_W &&
            my >= parent_area_y_click && my < parent_area_y_click + SELECTOR_ROW_H + 2) {
            state->selected_index = filtered_count;
            snprintf(out_event_json, max_len, "{\"event\":\"parent-selected\"}");
            return;
        }

        /* Check child rows */
        if (mx >= list_x && mx <= list_x + SELECTOR_LIST_W &&
            my >= list_top) {
            int clicked_vis = (my - list_top) / SELECTOR_ROW_H;
            int clicked_fi = clicked_vis + state->scroll_offset;
            if (clicked_fi >= 0 && clicked_fi < filtered_count &&
                clicked_vis < state->visible_rows) {
                int ci = filtered_indices[clicked_fi];
                state->selected_index = clicked_fi;
                snprintf(out_event_json, max_len,
                         "{\"event\":\"child-selected\",\"childId\":%d}",
                         state->children[ci].id);
            }
        }
    }

    /* --- Mouse wheel for scrolling --- */
    if (event->type == SDL_MOUSEWHEEL) {
        if (event->wheel.y > 0) {
            /* Scroll up */
            state->scroll_offset -= 2;
            if (state->scroll_offset < 0) state->scroll_offset = 0;
        } else if (event->wheel.y < 0) {
            /* Scroll down */
            state->scroll_offset += 2;
            if (state->scroll_offset > filtered_count - state->visible_rows) {
                state->scroll_offset = filtered_count - state->visible_rows;
            }
            if (state->scroll_offset < 0) state->scroll_offset = 0;
        }
    }
}
