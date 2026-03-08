/**
 * screen_lock.h -- Lock screen for allow2-lock-overlay
 *
 * Full opaque overlay. Blocks all interaction with the device underneath.
 * Sub-views: main, duration picker, pending, denied.
 */

#ifndef SCREEN_LOCK_H
#define SCREEN_LOCK_H

#include "render.h"

typedef enum {
    LOCK_VIEW_MAIN,         /* Main lock screen */
    LOCK_VIEW_DURATION,     /* Duration picker (15min, 30min, 1hr) */
    LOCK_VIEW_PENDING,      /* Waiting for parent approval */
    LOCK_VIEW_DENIED        /* Request was denied (brief, then back to main) */
} LockView;

typedef struct {
    char reason[128];       /* "Screen time is up", "Gaming time is up", etc. */
    char child_name[64];
    int child_id;
    int activity_id;        /* Activity ID for request-more-time events */
    LockView view;
    int selected_button;    /* 0 = Request, 1 = Switch Child (main view) */
                            /* 0 = 15min, 1 = 30min, 2 = 1hr (duration view) */
    float denied_timer;     /* Countdown to dismiss denied message */
    float pulse_phase;      /* Animation phase for pulsing dot */
} LockScreenState;

/* Initialize state from daemon JSON message fields. */
void screen_lock_set(LockScreenState *state, const char *reason,
                     const char *child_name, int child_id, int activity_id);

/* Update request status ("pending", "approved", "denied"). */
void screen_lock_set_request_status(LockScreenState *state, const char *status);

/* Render one frame of the lock screen.
 * dt = seconds elapsed since last frame. */
void screen_lock_render(SDL_Renderer *renderer, LockScreenState *state, float dt);

/* Process an SDL event. If the event triggers an IPC message,
 * the JSON string is written to out_event_json (up to max_len).
 * Otherwise out_event_json[0] is set to '\0'. */
void screen_lock_input(LockScreenState *state, SDL_Event *event,
                       char *out_event_json, int max_len);

#endif /* SCREEN_LOCK_H */
