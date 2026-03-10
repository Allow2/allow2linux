/**
 * screen_pairing.h -- Pairing screen for allow2-lock-overlay
 *
 * Shows QR code + 6-digit PIN while waiting for the parent to confirm
 * pairing via the Allow2 app.
 */

#ifndef SCREEN_PAIRING_H
#define SCREEN_PAIRING_H

#include "render.h"

/* Max QR module grid: version 10 = 57x57 = 3249 modules */
#define QR_MAX_SIZE  64
#define QR_MAX_MODULES (QR_MAX_SIZE * QR_MAX_SIZE)

/* Pairing screen state */
typedef struct {
    char  pin[8];          /* 6-digit PIN string (null-terminated) */
    char  qr_data[512];   /* URL for QR code */
    int   qr_size;         /* QR grid dimension (e.g. 29 for version 3) */
    char  qr_modules[QR_MAX_MODULES + 1]; /* Flat '0'/'1' grid, row-major */
    float pulse_time;      /* Accumulated time for pulse animation */
    int   connected;       /* 1 = server reachable, 0 = disconnected */
} PairingScreenState;

/* Initialize / update state from daemon JSON message fields.
 * qr_size = grid dimension (e.g. 29), qr_modules = flat '0'/'1' string.
 * Resets pulse_time to 0. */
void screen_pairing_set(PairingScreenState *state,
                        const char *pin, const char *qr_data,
                        int qr_size, const char *qr_modules);

/* Update only the connection status (without resetting PIN/QR). */
void screen_pairing_set_connected(PairingScreenState *state, int connected);

/* Render one frame of the pairing screen.
 * dt = seconds elapsed since last frame (e.g., 0.016 for 60fps). */
void screen_pairing_render(SDL_Renderer *renderer,
                           PairingScreenState *state, float dt);

#endif /* SCREEN_PAIRING_H */
