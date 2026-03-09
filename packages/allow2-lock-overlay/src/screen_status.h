/**
 * screen_status.h -- Status screen for allow2-lock-overlay
 *
 * Shows device pairing status, current child, and activity remaining times.
 * Used when the user opens the Allow2 app on an already-paired device.
 */

#ifndef SCREEN_STATUS_H
#define SCREEN_STATUS_H

#include "render.h"
#include <SDL2/SDL.h>

#define STATUS_MAX_ACTIVITIES 8

typedef struct {
    char family_name[128];
    char child_name[128];
    int child_id;
    int is_parent;
    struct {
        char name[64];
        int remaining_seconds;
    } activities[STATUS_MAX_ACTIVITIES];
    int activity_count;
} StatusScreenState;

/* Set the core fields of the status screen. */
void screen_status_set(StatusScreenState *state, const char *family,
                       const char *child, int child_id, int is_parent);

/* Add an activity with remaining seconds. */
void screen_status_add_activity(StatusScreenState *state, const char *name,
                                int remaining);

/* Render one frame of the status screen. */
void screen_status_render(SDL_Renderer *renderer, StatusScreenState *state);

/* Process an SDL event. If the event triggers an IPC message,
 * the JSON string is written to out_event_json (up to max_len).
 * Otherwise out_event_json[0] is set to '\0'. */
void screen_status_input(StatusScreenState *state, SDL_Event *event,
                         char *out_event_json, int max_len);

#endif /* SCREEN_STATUS_H */
