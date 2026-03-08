/**
 * screen_selector.h -- Child selector screen for allow2-lock-overlay
 *
 * Vertical list of children with "Parent" entry at the end.
 * Controller-navigable (D-pad), keyboard, mouse click.
 */

#ifndef SCREEN_SELECTOR_H
#define SCREEN_SELECTOR_H

#include "render.h"

#define SELECTOR_MAX_CHILDREN 16

typedef struct {
    int  id;
    char name[64];
    char avatar_path[256];
} SelectorChildEntry;

typedef struct {
    SelectorChildEntry children[SELECTOR_MAX_CHILDREN];
    int  child_count;
    int  selected_index;   /* 0..child_count (last = parent) */
    int  show_parent;      /* Always 1 */
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
