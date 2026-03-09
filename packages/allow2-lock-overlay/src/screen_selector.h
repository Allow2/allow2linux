/**
 * screen_selector.h -- Child selector screen for allow2-lock-overlay
 *
 * Vertical list of children with "Parent" entry pinned at the bottom.
 * Controller-navigable (D-pad), keyboard, mouse click.
 * Supports search filtering, scrolling, and last-used time display.
 */

#ifndef SCREEN_SELECTOR_H
#define SCREEN_SELECTOR_H

#include "render.h"

#define SELECTOR_MAX_CHILDREN 16

typedef struct {
    int  id;
    char name[64];
    char avatar_path[256];
    char last_used_at[32];  /* ISO timestamp or empty string */
} SelectorChildEntry;

typedef struct {
    SelectorChildEntry children[SELECTOR_MAX_CHILDREN];
    int  child_count;
    int  selected_index;   /* Index into filtered list (0..filtered_count or parent) */
    int  show_parent;      /* Always 1 */
    int  scroll_offset;    /* First visible row index */
    int  visible_rows;     /* How many rows fit on screen (calculated during render) */
    char search_text[64];  /* Current search/filter text */
    int  search_len;       /* Length of search text */
} SelectorScreenState;

/* Set children from daemon message. Resets selection to 0. */
void screen_selector_set(SelectorScreenState *state,
                         const SelectorChildEntry *entries, int count);

/* Render one frame. */
void screen_selector_render(SDL_Renderer *renderer, SelectorScreenState *state);

/* Process input event. Writes IPC JSON to out_event_json if action taken. */
void screen_selector_input(SelectorScreenState *state, SDL_Event *event,
                           char *out_event_json, int max_len);

#endif /* SCREEN_SELECTOR_H */
