/**
 * screen_feedback.h -- Feedback/Report Issue screen for allow2-lock-overlay
 *
 * Allows children (or parents) to report issues directly from the overlay.
 * Categories: Bypass, Missing Feature, Not Working, Question, Other.
 */

#ifndef SCREEN_FEEDBACK_H
#define SCREEN_FEEDBACK_H

#include "render.h"
#include <SDL2/SDL.h>

#define FEEDBACK_MAX_MESSAGE   512
#define FEEDBACK_NUM_CATEGORIES  5

/* Focus areas within the feedback screen */
#define FEEDBACK_FOCUS_CATEGORY  0
#define FEEDBACK_FOCUS_MESSAGE   1
#define FEEDBACK_FOCUS_SUBMIT    2
#define FEEDBACK_FOCUS_CANCEL    3

typedef struct {
    int selected_category;              /* 0-4 */
    char message_text[FEEDBACK_MAX_MESSAGE];
    int message_len;
    int focus;                          /* FEEDBACK_FOCUS_* */
    int submitted;                      /* 1 after submit */
} FeedbackScreenState;

/* Reset the feedback screen state. */
void screen_feedback_reset(FeedbackScreenState *state);

/* Render one frame of the feedback screen. */
void screen_feedback_render(SDL_Renderer *renderer, FeedbackScreenState *state);

/* Process an SDL event. If the event triggers an IPC message,
 * the JSON string is written to out_event_json (up to max_len).
 * Otherwise out_event_json[0] is set to '\0'. */
void screen_feedback_input(FeedbackScreenState *state, SDL_Event *event,
                           char *out_event_json, int max_len);

#endif /* SCREEN_FEEDBACK_H */
