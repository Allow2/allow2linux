/**
 * screen_pairing.h -- Pairing screen for allow2-lock-overlay
 *
 * Shows QR code + 6-digit PIN while waiting for the parent to confirm
 * pairing via the Allow2 app.
 */

#ifndef SCREEN_PAIRING_H
#define SCREEN_PAIRING_H

#include "render.h"

/* Pairing screen state */
typedef struct {
    char  pin[8];          /* 6-digit PIN string (null-terminated) */
    char  qr_data[512];   /* URL for QR code */
    float pulse_time;      /* Accumulated time for pulse animation */
} PairingScreenState;

/* Initialize / update state from daemon JSON message fields.
 * Resets pulse_time to 0. */
void screen_pairing_set(PairingScreenState *state,
                        const char *pin, const char *qr_data);

/* Render one frame of the pairing screen.
 * dt = seconds elapsed since last frame (e.g., 0.016 for 60fps). */
void screen_pairing_render(SDL_Renderer *renderer,
                           PairingScreenState *state, float dt);

#endif /* SCREEN_PAIRING_H */
