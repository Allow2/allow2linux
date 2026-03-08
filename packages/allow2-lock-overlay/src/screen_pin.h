/**
 * screen_pin.h -- PIN entry screen for allow2-lock-overlay
 *
 * Shows digit display boxes and a 3x4 navigable keypad.
 * Supports keyboard, D-pad/controller, and mouse.
 */

#ifndef SCREEN_PIN_H
#define SCREEN_PIN_H

#include "render.h"

#define PIN_MAX_DIGITS 8

typedef struct {
    int  child_id;
    char child_name[64];
    int  is_parent;         /* 1 = show dots instead of digits */
    int  max_digits;        /* 4 or 6 */
    char entered[PIN_MAX_DIGITS];
    int  entered_count;
    int  pad_selected_row;  /* 0-3 */
    int  pad_selected_col;  /* 0-2 */
    int  attempts_remaining;
    int  locked_out;
    int  lockout_seconds;
    int  show_app_approval;
    float pulse_time;
} PinScreenState;

/* Initialize state from daemon message. */
void screen_pin_set(PinScreenState *state, int child_id, const char *child_name,
                    int is_parent, int max_digits);

/* Update with PIN result from daemon. */
void screen_pin_set_result(PinScreenState *state, int success,
                           int attempts_remaining, int locked_out,
                           int lockout_seconds);

/* Render one frame. dt = seconds since last frame. */
void screen_pin_render(SDL_Renderer *renderer, PinScreenState *state, float dt);

/* Process input event. Writes IPC JSON to out_event_json if action taken. */
void screen_pin_input(PinScreenState *state, SDL_Event *event,
                      char *out_event_json, int max_len);

#endif /* SCREEN_PIN_H */
