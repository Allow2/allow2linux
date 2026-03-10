/**
 * allow2-lock-overlay -- main.c
 *
 * Fullscreen SDL2 overlay for Allow2 Parental Freedom on Linux.
 * Communicates with the allow2linux daemon via Unix domain socket (JSON).
 *
 * Build: make
 * Usage: allow2-lock-overlay --socket /tmp/allow2-overlay.sock
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_syswm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include "socket.h"
#include "render.h"
#include "json.h"
#include "screen_pairing.h"
#include "screen_selector.h"
#include "screen_pin.h"
#include "screen_lock.h"
#include "screen_warning.h"
#include "screen_status.h"
#include "screen_feedback.h"

/* ---- Screen states ---- */

typedef enum {
    SCREEN_NONE = 0,
    SCREEN_PAIRING,
    SCREEN_SELECTOR,
    SCREEN_PIN_ENTRY,
    SCREEN_LOCK,
    SCREEN_WARNING,
    SCREEN_DENIED,
    SCREEN_STATUS,
    SCREEN_FEEDBACK
} ScreenState;

/* ---- Application state ---- */

typedef struct {
    ScreenState screen;
    int running;
    int window_visible;
    const char *socket_path;

    /* Per-screen state */
    PairingScreenState   pairing;
    SelectorScreenState  selector;
    PinScreenState       pin;
    LockScreenState      lock;
    WarningScreenState   warning;
    StatusScreenState    status;
    FeedbackScreenState  feedback;

    /* Denied flash */
    Uint32 denied_show_time;

    /* Frame timing */
    Uint32 last_frame_time;

    /* Watchdog: last time we received a message from daemon */
    Uint32 last_message_time;

    /* Game controller */
    SDL_GameController *controller;

    /* Mode: 0 = overlay (default), 1 = app (normal windowed) */
    int app_mode;

    /* Overlay mode display info (for deferred window creation) */
    int is_gamescope;
    int display_w;
    int display_h;
} AppState;

static AppState state;

/* ---- Gamescope overlay integration ---- */

/**
 * Tell gamescope to composite this window as an overlay.
 * Sets GAMESCOPE_EXTERNAL_OVERLAY and STEAM_OVERLAY X11 atoms.
 * Without this, gamescope ignores windows from non-Steam processes.
 */
static void set_gamescope_overlay(SDL_Window *window) {
    SDL_SysWMinfo wminfo;
    SDL_VERSION(&wminfo.version);
    if (!SDL_GetWindowWMInfo(window, &wminfo)) {
        fprintf(stderr, "[overlay] SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        return;
    }

#ifdef SDL_VIDEO_DRIVER_X11
    if (wminfo.subsystem == SDL_SYSWM_X11) {
        Display *dpy = wminfo.info.x11.display;
        Window xwin = wminfo.info.x11.window;
        uint32_t val;

        /* Mark as Steam overlay on :1 (inner Xwayland) */
        val = 1;
        Atom steam_overlay = XInternAtom(dpy, "STEAM_OVERLAY", False);
        XChangeProperty(dpy, xwin, steam_overlay, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)&val, 1);

        Atom steam_input = XInternAtom(dpy, "STEAM_INPUT_FOCUS", False);
        XChangeProperty(dpy, xwin, steam_input, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)&val, 1);

        /* Override-redirect to bypass WM */
        XSetWindowAttributes attrs;
        attrs.override_redirect = True;
        XChangeWindowAttributes(dpy, xwin, CWOverrideRedirect, &attrs);
        XFlush(dpy);

        fprintf(stderr, "[overlay] atoms set on :1 window 0x%lx\n",
                (unsigned long)xwin);

        /* Now open :0 (gamescope control display) and set baselayer order
         * to include our window in the composite stack */
        {
            Display *ctrl = XOpenDisplay(":0");
            if (ctrl) {
                Window root = DefaultRootWindow(ctrl);

                /* Set our window as an overlay on :0 control display.
                 * GAMESCOPECTRL_BASELAYER_WINDOW with our :1 window ID
                 * tells gamescope to overlay this window. */
                Atom overlay_window = XInternAtom(ctrl, "GAMESCOPECTRL_BASELAYER_WINDOW", False);
                uint32_t wid = (uint32_t)xwin;
                XChangeProperty(ctrl, root, overlay_window, XA_CARDINAL, 32,
                                PropModeReplace, (unsigned char *)&wid, 1);

                XFlush(ctrl);
                fprintf(stderr, "[overlay] set GAMESCOPECTRL_BASELAYER_WINDOW=0x%x on :0\n", wid);
                XCloseDisplay(ctrl);
            } else {
                fprintf(stderr, "[overlay] could not open :0 for gamescope control\n");
            }
        }
    } else {
        fprintf(stderr, "[overlay] not X11 subsystem\n");
    }
#else
    fprintf(stderr, "[overlay] X11 not compiled in\n");
    (void)window;
#endif
}

/* ---- Helpers ---- */

static void send_event(const char *json_str) {
    if (!socket_is_connected()) return;
    socket_write(json_str, (int)strlen(json_str));
    socket_write("\n", 1);
}

/* Forward declarations for deferred window creation */
static SDL_Window *g_window;
static SDL_Renderer *g_renderer;

static void show_window(SDL_Window *window) {
    /* Overlay mode: deferred window creation on first show */
    if (!window && !state.app_mode) {
        Uint32 win_flags;
        if (state.is_gamescope) {
            win_flags = SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS |
                        SDL_WINDOW_ALWAYS_ON_TOP;
            g_window = SDL_CreateWindow("Allow2",
                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                0, 0, win_flags);
        } else {
            win_flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP;
            g_window = SDL_CreateWindow("Allow2",
                0, 0, state.display_w, state.display_h, win_flags);
        }
        if (!g_window) {
            fprintf(stderr, "[overlay] deferred SDL_CreateWindow failed: %s\n", SDL_GetError());
            return;
        }
        /* Create renderer for the new window */
        g_renderer = SDL_CreateRenderer(g_window, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!g_renderer) {
            g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
        }
        if (g_renderer) {
            SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        }
        if (!state.app_mode) {
            set_gamescope_overlay(g_window);
        }
        fprintf(stderr, "[overlay] deferred window created (screen=%d)\n", state.screen);
        state.window_visible = 1;
        return;
    }

    if (window && !state.window_visible) {
        SDL_ShowWindow(window);
        SDL_RaiseWindow(window);
        state.window_visible = 1;
        fprintf(stderr, "[overlay] window shown (screen=%d)\n", state.screen);
    }
}

static void hide_window(SDL_Window *window) {
    /* Overlay mode with deferred window: destroy it entirely to free the screen */
    if (!state.app_mode && g_window) {
        if (g_renderer) {
            SDL_DestroyRenderer(g_renderer);
            g_renderer = NULL;
        }
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        state.window_visible = 0;
        fprintf(stderr, "[overlay] deferred window destroyed\n");
        return;
    }
    if (window && state.window_visible) {
        SDL_HideWindow(window);
        state.window_visible = 0;
    }
}

static void resolve_assets_path(char *out, int maxlen) {
    /* Flatpak: fonts are at /app/share/allow2/assets (symlinked from /app/bin/assets) */
    if (access("/app/share/allow2/assets/Inter-Regular.ttf", R_OK) == 0) {
        snprintf(out, maxlen, "/app/share/allow2/assets");
        return;
    }

    /* Non-Flatpak: resolve relative to binary location */
    char exe_dir[512];
    int len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
    if (len > 0) {
        exe_dir[len] = '\0';
        char *dir = dirname(exe_dir);
        snprintf(out, maxlen, "%s/assets", dir);
    } else {
        snprintf(out, maxlen, "assets");
    }
}

/* ---- Message handling ---- */

static void handle_message(const char *json_str) {
    JsonValue msg;
    const char *screen;

    if (json_parse(json_str, &msg) != 0) {
        fprintf(stderr, "[overlay] failed to parse JSON: %.80s\n", json_str);
        return;
    }

    screen = json_get_string(&msg, "screen");
    if (!screen) {
        json_free(&msg);
        return;
    }
    fprintf(stderr, "[overlay] received screen=%s\n", screen);

    if (strcmp(screen, "dismiss") == 0) {
        state.screen = SCREEN_NONE;
    }
    else if (strcmp(screen, "pairing") == 0) {
        state.screen = SCREEN_PAIRING;
        screen_pairing_set(&state.pairing,
                           json_get_string(&msg, "pin"),
                           json_get_string(&msg, "qrData"),
                           json_get_int(&msg, "qrSize", 0),
                           json_get_string(&msg, "qrModules"));
        state.pairing.connected = json_get_int(&msg, "connected", 1);
    }
    else if (strcmp(screen, "pairing-connection") == 0) {
        /* Update only connection status without resetting pairing state */
        screen_pairing_set_connected(&state.pairing,
                                     json_get_int(&msg, "connected", 1));
    }
    else if (strcmp(screen, "selector") == 0) {
        state.screen = SCREEN_SELECTOR;
        /* Parse children array into SelectorChildEntry array */
        {
            const JsonValue *arr = json_get_array(&msg, "children");
            SelectorChildEntry entries[SELECTOR_MAX_CHILDREN];
            int count = 0;
            if (arr) {
                int i;
                for (i = 0; i < arr->array_len && i < SELECTOR_MAX_CHILDREN; i++) {
                    JsonValue *child = &arr->array_items[i];
                    const char *n, *ap, *lu;
                    memset(&entries[count], 0, sizeof(entries[count]));
                    entries[count].id = json_get_int(child, "id", 0);
                    n = json_get_string(child, "name");
                    if (n) strncpy(entries[count].name, n, sizeof(entries[count].name) - 1);
                    ap = json_get_string(child, "avatarPath");
                    if (ap) strncpy(entries[count].avatar_path, ap, sizeof(entries[count].avatar_path) - 1);
                    lu = json_get_string(child, "lastUsedAt");
                    if (lu) strncpy(entries[count].last_used_at, lu, sizeof(entries[count].last_used_at) - 1);
                    count++;
                }
            }
            screen_selector_set(&state.selector, entries, count);
        }
    }
    else if (strcmp(screen, "pin-entry") == 0) {
        state.screen = SCREEN_PIN_ENTRY;
        screen_pin_set(&state.pin,
                       json_get_int(&msg, "childId", 0),
                       json_get_string(&msg, "childName"),
                       json_get_int(&msg, "isParent", 0),
                       json_get_int(&msg, "maxDigits", 4));
    }
    else if (strcmp(screen, "pin-result") == 0) {
        int success = json_get_int(&msg, "success", 0);
        screen_pin_set_result(&state.pin, success,
                              json_get_int(&msg, "attemptsRemaining", 0),
                              json_get_int(&msg, "lockedOut", 0),
                              json_get_int(&msg, "lockoutSeconds", 0));
        /* On success, daemon will send dismiss next */
    }
    else if (strcmp(screen, "lock") == 0) {
        state.screen = SCREEN_LOCK;
        screen_lock_set(&state.lock,
                        json_get_string(&msg, "reason"),
                        json_get_string(&msg, "childName"),
                        json_get_int(&msg, "childId", 0),
                        json_get_int(&msg, "activityId", 0));
    }
    else if (strcmp(screen, "warning") == 0) {
        state.screen = SCREEN_WARNING;
        screen_warning_set(&state.warning,
                           json_get_string(&msg, "activity"),
                           json_get_int(&msg, "activityId", 0),
                           json_get_int(&msg, "remaining", 0),
                           json_get_string(&msg, "level"));
    }
    else if (strcmp(screen, "request-status") == 0) {
        const char *st = json_get_string(&msg, "status");
        if (st) {
            screen_lock_set_request_status(&state.lock, st);
        }
    }
    else if (strcmp(screen, "status") == 0) {
        state.screen = SCREEN_STATUS;
        memset(&state.status, 0, sizeof(state.status));
        screen_status_set(&state.status,
                          json_get_string(&msg, "family"),
                          json_get_string(&msg, "childName"),
                          json_get_int(&msg, "childId", 0),
                          json_get_int(&msg, "isParent", 0));
        state.status.can_submit_feedback = json_get_int(&msg, "canSubmitFeedback", 0);
        /* Parse activities array */
        {
            const JsonValue *arr = json_get_array(&msg, "activities");
            if (arr) {
                int ai;
                for (ai = 0; ai < arr->array_len && ai < STATUS_MAX_ACTIVITIES; ai++) {
                    JsonValue *act = &arr->array_items[ai];
                    screen_status_add_activity(&state.status,
                        json_get_string(act, "name"),
                        json_get_int(act, "remaining", 0));
                }
            }
        }
    }
    else if (strcmp(screen, "feedback") == 0) {
        state.screen = SCREEN_FEEDBACK;
        screen_feedback_reset(&state.feedback);
    }
    else if (strcmp(screen, "denied") == 0) {
        state.screen = SCREEN_DENIED;
        state.denied_show_time = SDL_GetTicks();
    }

    json_free(&msg);
}

/* ---- Event dispatch ---- */

static void dispatch_event(SDL_Event *event, SDL_Window *window) {
    char out_json[512];
    out_json[0] = '\0';

    switch (state.screen) {
    case SCREEN_SELECTOR:
        screen_selector_input(&state.selector, event, out_json, sizeof(out_json));
        break;
    case SCREEN_PIN_ENTRY:
        screen_pin_input(&state.pin, event, out_json, sizeof(out_json));
        break;
    case SCREEN_LOCK:
        screen_lock_input(&state.lock, event, out_json, sizeof(out_json));
        break;
    case SCREEN_WARNING:
        screen_warning_input(&state.warning, event, out_json, sizeof(out_json));
        break;
    case SCREEN_STATUS:
        screen_status_input(&state.status, event, out_json, sizeof(out_json));
        break;
    case SCREEN_FEEDBACK:
        screen_feedback_input(&state.feedback, event, out_json, sizeof(out_json));
        break;
    default:
        break;
    }

    if (out_json[0]) {
        send_event(out_json);
    }

    (void)window;
}

/* ---- Main ---- */

int main(int argc, char *argv[]) {
    const char *socket_path = "/tmp/allow2-overlay.sock";
    char assets_path[512];
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Event event;
    char msg_buf[4096];
    int i;
    int app_mode = 0;  /* 0 = overlay, 1 = app (normal windowed) */
    int render_initialized = 0;

    /* Parse args */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            if (strcmp(argv[++i], "app") == 0) app_mode = 1;
        }
    }

    /* Initialize state */
    memset(&state, 0, sizeof(state));
    state.screen = SCREEN_NONE;
    state.running = 1;
    state.socket_path = socket_path;
    state.app_mode = app_mode;

    /* Init SDL */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "[overlay] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Create window.
     * App mode: normal resizable window, shown immediately.
     * Overlay mode: window is DEFERRED — not created until daemon sends a screen.
     *   This prevents a fullscreen window from blocking the desktop when idle.
     *   Window will be created on first show_window() call. */
    if (state.app_mode) {
        Uint32 win_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
        window = SDL_CreateWindow("Allow2",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            900, 600, win_flags);
        fprintf(stderr, "[app] windowed mode 900x600\n");
    } else {
        /* Overlay mode: detect display info but DON'T create window yet */
        const char *video_driver = SDL_GetCurrentVideoDriver();
        state.is_gamescope = (video_driver && strcmp(video_driver, "x11") == 0) ? 1 : 0;
        fprintf(stderr, "[overlay] video driver: %s (gamescope=%d)\n",
                video_driver ? video_driver : "?", state.is_gamescope);

        if (!state.is_gamescope) {
            SDL_DisplayMode dm;
            if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
                fprintf(stderr, "[overlay] display mode: %dx%d\n", dm.w, dm.h);
                state.display_w = dm.w;
                state.display_h = dm.h;
            } else {
                state.display_w = 1280;
                state.display_h = 800;
            }
        }

        /* Window stays NULL until needed — created in show_window() */
        window = NULL;
    }
    /* App mode: verify window was created. Overlay mode: window is NULL (deferred). */
    if (state.app_mode && !window) {
        fprintf(stderr, "[overlay] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* Register as gamescope overlay (required for visibility in Game Mode).
     * Skip in app mode -- not an overlay, just a regular window.
     * In overlay mode, this is done later when window is created in show_window(). */
    if (state.app_mode) {
        /* no-op: app mode doesn't need gamescope overlay */
    }

    /* Create renderer — only if window exists (app mode).
     * Overlay mode: renderer created alongside deferred window in show_window(). */
    if (window) {
        renderer = SDL_CreateRenderer(
            window, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
        );
        if (!renderer) {
            fprintf(stderr, "[overlay] accelerated renderer failed, trying software\n");
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        }
        if (!renderer) {
            fprintf(stderr, "[overlay] SDL_CreateRenderer failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    }

    /* Load assets (fonts, sets up logical resolution).
     * In overlay mode, renderer is NULL here (deferred) — init later. */
    resolve_assets_path(assets_path, sizeof(assets_path));
    if (renderer) {
        if (render_init(renderer, assets_path) < 0) {
            fprintf(stderr, "[overlay] WARNING: render_init failed -- "
                    "text won't render\n");
        }
        render_initialized = 1;
    }

    /* Open game controller if present */
    {
        int num_joy = SDL_NumJoysticks();
        int j;
        for (j = 0; j < num_joy; j++) {
            if (SDL_IsGameController(j)) {
                state.controller = SDL_GameControllerOpen(j);
                if (state.controller) {
                    fprintf(stderr, "[overlay] controller: %s\n",
                            SDL_GameControllerName(state.controller));
                    break;
                }
            }
        }
    }

    /* Connect to daemon */
    if (socket_connect(socket_path) == 0) {
        if (state.app_mode) {
            send_event("{\"event\":\"app-opened\"}");
        } else {
            send_event("{\"event\":\"ready\"}");
        }
    }

    state.last_frame_time = SDL_GetTicks();
    state.last_message_time = SDL_GetTicks();

    /* In app mode, window is already shown at creation */
    if (state.app_mode) {
        state.window_visible = 1;
    }

    /* ---- Main event loop ---- */

    while (state.running) {
        Uint32 now;
        float dt;

        /* Reconnect if needed */
        if (!socket_is_connected()) {
            if (socket_try_reconnect(socket_path)) {
                if (state.app_mode) {
                    send_event("{\"event\":\"app-opened\"}");
                } else {
                    send_event("{\"event\":\"ready\"}");
                }
            }
        }

        /* Read incoming messages */
        while (socket_is_connected()) {
            int n = socket_read_line(msg_buf, sizeof(msg_buf));
            if (n > 0) {
                handle_message(msg_buf);
                state.last_message_time = SDL_GetTicks();
            } else {
                break;
            }
        }

        /* Watchdog: in overlay mode, exit if no message from daemon for 60s.
         * This prevents a stale overlay from locking the screen. */
        if (!state.app_mode && !socket_is_connected()) {
            Uint32 silence = SDL_GetTicks() - state.last_message_time;
            if (silence > 60000) {
                fprintf(stderr, "[overlay] watchdog: no daemon contact for %ums, exiting\n", silence);
                state.running = 0;
                continue;
            }
        }

        /* Hidden state: block on events, zero CPU.
         * In app mode, keep window visible but idle (waiting for daemon). */
        if (state.screen == SCREEN_NONE) {
            if (!state.app_mode) {
                hide_window(window);
                /* Sync locals — deferred window was destroyed */
                if (!g_window) {
                    window = NULL;
                    renderer = NULL;
                }
            }

            if (SDL_WaitEventTimeout(&event, 500)) {
                if (event.type == SDL_QUIT && state.app_mode) {
                    state.running = 0;
                }
                if (event.type == SDL_KEYDOWN &&
                    event.key.keysym.sym == SDLK_ESCAPE) {
                    state.running = 0;
                }
            }

            /* In app mode, render an empty background while idle */
            if (state.app_mode) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                SDL_RenderClear(renderer);
                render_background(renderer, COLOR_BG_A);
                SDL_RenderPresent(renderer);
            }

            state.last_frame_time = SDL_GetTicks();
            continue;
        }

        show_window(window);

        /* Sync local pointers after deferred window creation */
        if (!window && g_window) {
            window = g_window;
            renderer = g_renderer;
            if (!render_initialized && renderer) {
                if (render_init(renderer, assets_path) < 0) {
                    fprintf(stderr, "[overlay] WARNING: deferred render_init failed\n");
                }
                render_initialized = 1;
            }
        }

        /* If still no renderer, skip frame (deferred creation failed) */
        if (!renderer) {
            state.last_frame_time = SDL_GetTicks();
            SDL_Delay(100);
            continue;
        }

        /* Compute delta time */
        now = SDL_GetTicks();
        dt = (float)(now - state.last_frame_time) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;  /* Clamp to avoid big jumps */
        state.last_frame_time = now;

        /* Poll events */
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                /* App mode: close button always exits.
                 * Overlay mode: SDL_QUIT is blocked (no close button). */
                if (state.app_mode) {
                    state.running = 0;
                }
                break;

            case SDL_KEYDOWN:
                /* App mode: ESC always exits.
                 * Overlay mode: ESC on non-lock screens dismisses (returns
                 * to SCREEN_NONE which destroys the deferred window).
                 * On lock screen: ESC is blocked entirely. */
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    if (state.app_mode) {
                        state.running = 0;
                    } else if (state.screen != SCREEN_LOCK) {
                        state.screen = SCREEN_NONE;
                        send_event("{\"event\":\"dismissed\"}");
                    }
                }
                dispatch_event(&event, window);
                break;

            case SDL_CONTROLLERBUTTONDOWN:
                dispatch_event(&event, window);
                break;

            case SDL_CONTROLLERDEVICEADDED:
                if (!state.controller) {
                    int idx = event.cdevice.which;
                    if (SDL_IsGameController(idx)) {
                        state.controller = SDL_GameControllerOpen(idx);
                    }
                }
                break;

            case SDL_CONTROLLERDEVICEREMOVED:
                if (state.controller) {
                    SDL_GameControllerClose(state.controller);
                    state.controller = NULL;
                }
                break;

            case SDL_TEXTINPUT:
                dispatch_event(&event, window);
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEMOTION:
            case SDL_MOUSEWHEEL:
                dispatch_event(&event, window);
                break;

            default:
                break;
            }
        }

        /* Render */
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);

        switch (state.screen) {
        case SCREEN_PAIRING:
            screen_pairing_render(renderer, &state.pairing, dt);
            break;
        case SCREEN_SELECTOR:
            screen_selector_render(renderer, &state.selector);
            break;
        case SCREEN_PIN_ENTRY:
            screen_pin_render(renderer, &state.pin, dt);
            break;
        case SCREEN_LOCK:
            screen_lock_render(renderer, &state.lock, dt);
            break;
        case SCREEN_WARNING:
            screen_warning_render(renderer, &state.warning, dt);
            break;
        case SCREEN_STATUS:
            screen_status_render(renderer, &state.status);
            break;
        case SCREEN_FEEDBACK:
            screen_feedback_render(renderer, &state.feedback);
            break;
        case SCREEN_DENIED: {
            TTF_Font *tf = render_get_font(FONT_BOLD_36);
            render_background(renderer, COLOR_BG_A);
            if (tf) {
                SDL_Color red = {252, 129, 129, 255};
                render_text_centered(renderer, tf, "Request denied",
                                     LOGICAL_H / 2 - 20, red);
            }
            if (SDL_GetTicks() - state.denied_show_time > 2000) {
                state.screen = SCREEN_NONE;
            }
            break;
        }
        default:
            break;
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }

    /* Cleanup */
    socket_disconnect();
    if (render_initialized) render_cleanup();
    if (state.controller) SDL_GameControllerClose(state.controller);
    /* Destroy whichever window/renderer exists (local or deferred global) */
    if (renderer) SDL_DestroyRenderer(renderer);
    else if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (window) SDL_DestroyWindow(window);
    else if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();

    return 0;
}
