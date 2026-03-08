/**
 * screen_warning.h -- Warning bar overlay for allow2-lock-overlay
 *
 * Semi-transparent top bar showing activity name, remaining time,
 * and optional "Request More Time" button. Game/app remains visible
 * and playable underneath.
 */

#ifndef SCREEN_WARNING_H
#define SCREEN_WARNING_H

#include "render.h"

typedef enum {
    WARNING_LEVEL_INFO,     /* Blue accent bar */
    WARNING_LEVEL_URGENT,   /* Amber accent bar */
    WARNING_LEVEL_FINAL,    /* Red accent bar */
    WARNING_LEVEL_COUNTDOWN /* Red pulsing accent bar */
} WarningLevel;

typedef struct {
    char activity[64];      /* "Gaming", "Internet", etc. */
    int activity_id;
    int remaining;          /* Seconds remaining */
    WarningLevel level;
    int show_request_btn;   /* 1 when level >= URGENT */
    int btn_highlighted;    /* Mouse hover on request button */
    float pulse_phase;      /* Animation phase for pulsing accent */
} WarningScreenState;

/* Initialize / update state from daemon JSON message fields.
 * level_str is one of "info", "urgent", "final", "countdown". */
void screen_warning_set(WarningScreenState *state, const char *activity,
                        int activity_id, int remaining, const char *level_str);

/* Render one frame of the warning bar.
 * dt = seconds elapsed since last frame. */
void screen_warning_render(SDL_Renderer *renderer,
                           WarningScreenState *state, float dt);

/* Process an SDL event. If the event triggers an IPC message,
 * the JSON string is written to out_event_json (up to max_len).
 * Otherwise out_event_json[0] is set to '\0'. */
void screen_warning_input(WarningScreenState *state, SDL_Event *event,
                          char *out_event_json, int max_len);

#endif /* SCREEN_WARNING_H */
