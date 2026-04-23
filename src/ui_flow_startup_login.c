/*
 * ui_flow_startup_login.c - Startup refresh and QR login flow ownership
 *
 * Keeps startup/login worker lifecycle with the startup/login flow while
 * leaving ui_run() as the top-level scheduler and view owner.
 */
#include "ui_internal.h"

#if HAVE_SDL

#include <string.h>
#include "session_service.h"

static int ui_startup_login_start_thread(void *userdata) {
    LoginStartState *state = (LoginStartState *)userdata;

    if (!state) {
        return -1;
    }
    if (session_service_login_start_background(state->data_dir, state->ca_file,
                                               &state->session, state->qr_path) == 0) {
        atomic_store(&state->success, 1);
    } else {
        atomic_store(&state->failed, 1);
    }
    atomic_store(&state->running, 0);
    return atomic_load(&state->success) ? 0 : -1;
}

static int ui_startup_login_poll_thread(void *userdata) {
    LoginPollState *state = (LoginPollState *)userdata;

    if (!state) {
        return -1;
    }
    if (session_service_login_poll_background(state->data_dir, state->ca_file, &state->session,
                                              &state->stop, &state->completed,
                                              &state->last_status) != 0 &&
        !atomic_load(&state->completed)) {
        state->last_status = AUTH_POLL_ERROR;
    }
    atomic_store(&state->running, 0);
    return atomic_load(&state->completed) ? 0 : -1;
}

static int ui_startup_login_startup_thread(void *userdata) {
    StartupState *state = (StartupState *)userdata;

    if (!state) {
        return -1;
    }
    state->session_ok =
        session_service_startup_refresh_background(state->data_dir, state->ca_file,
                                                   &state->shelf_nuxt, &state->poor_network);
    if (state->session_ok < 0 && !state->poor_network) {
        state->poor_network = 0;
    }
    atomic_store(&state->running, 0);
    atomic_store(&state->completed, 1);
    return state->session_ok == 1 ? 0 : -1;
}

void ui_startup_login_startup_state_reset(StartupState *state) {
    if (!state) {
        return;
    }
    cJSON_Delete(state->shelf_nuxt);
    memset(state, 0, sizeof(*state));
}

void ui_startup_login_begin_startup_refresh(ApiContext *ctx, StartupState *startup_state,
                                            SDL_Thread **startup_thread_handle) {
    if (!ctx || !startup_state || !startup_thread_handle || *startup_thread_handle ||
        atomic_load(&startup_state->running)) {
        return;
    }

    ui_startup_login_startup_state_reset(startup_state);
    snprintf(startup_state->data_dir, sizeof(startup_state->data_dir), "%s", ctx->data_dir);
    snprintf(startup_state->ca_file, sizeof(startup_state->ca_file), "%s", ctx->ca_file);
    atomic_store(&startup_state->running, 1);
    *startup_thread_handle =
        SDL_CreateThread(ui_startup_login_startup_thread, "weread-startup", startup_state);
    if (!*startup_thread_handle) {
        atomic_store(&startup_state->running, 0);
        atomic_store(&startup_state->completed, 1);
        startup_state->session_ok = -1;
    }
}

int ui_startup_login_finish_startup(SDL_Thread **startup_thread_handle,
                                    StartupState *startup_state) {
    if (!startup_thread_handle || !*startup_thread_handle || !startup_state ||
        atomic_load(&startup_state->running)) {
        return 0;
    }

    SDL_WaitThread(*startup_thread_handle, NULL);
    *startup_thread_handle = NULL;
    return 1;
}

void ui_startup_login_begin_login_flow(ApiContext *ctx, LoginStartState *login_start,
                                       SDL_Thread **login_thread, UiView *view,
                                       char *status, size_t status_size,
                                       const char *qr_path) {
    if (atomic_load(&login_start->running) || *login_thread) {
        return;
    }

    memset(login_start, 0, sizeof(*login_start));
    snprintf(login_start->data_dir, sizeof(login_start->data_dir), "%s", ctx->data_dir);
    snprintf(login_start->ca_file, sizeof(login_start->ca_file), "%s", ctx->ca_file);
    snprintf(login_start->qr_path, sizeof(login_start->qr_path), "%s", qr_path);
    atomic_store(&login_start->running, 1);
    snprintf(status, status_size, "\xE6\xAD\xA3\xE5\x9C\xA8\xE7\x94\x9F\xE6\x88\x90\xE4\xBA\x8C\xE7\xBB\xB4\xE7\xA0\x81...");
    *view = VIEW_LOGIN;
    *login_thread =
        SDL_CreateThread(ui_startup_login_start_thread, "weread-login-start", login_start);
    if (!*login_thread) {
        atomic_store(&login_start->running, 0);
        atomic_store(&login_start->failed, 1);
        snprintf(status, status_size,
                 "\xE6\x97\xA0\xE6\xB3\x95\xE5\x88\x9B\xE5\xBB\xBA\xE7\x99\xBB\xE5\xBD\x95\xE7\xBA\xBF\xE7\xA8\x8B");
    }
}

int ui_startup_login_finish_login_start(ApiContext *ctx, LoginStartState *login_start,
                                        SDL_Thread **login_thread, LoginPollState *login_poll,
                                        SDL_Thread **login_poll_thread_handle,
                                        AuthSession *session, Uint32 *last_poll,
                                        int *login_active, char *status,
                                        size_t status_size, int *render_requested) {
    if (!ctx || !login_start || !login_thread || !*login_thread ||
        atomic_load(&login_start->running) ||
        !login_poll || !login_poll_thread_handle || !session || !last_poll ||
        !login_active || !status || !render_requested) {
        return 0;
    }

    SDL_WaitThread(*login_thread, NULL);
    *login_thread = NULL;
    *render_requested = 1;

    if (atomic_load(&login_start->success)) {
        *session = login_start->session;
        snprintf(status, status_size,
                 "\xE4\xBA\x8C\xE7\xBB\xB4\xE7\xA0\x81\xE5\xB7\xB2\xE7\x94\x9F\xE6\x88\x90\xEF\xBC\x8C\xE7\xAD\x89\xE5\xBE\x85\xE6\x89\xAB\xE7\xA0\x81\xE7\xA1\xAE\xE8\xAE\xA4...");
        *login_active = 1;
        *last_poll = SDL_GetTicks();
        memset(login_poll, 0, sizeof(*login_poll));
        snprintf(login_poll->data_dir, sizeof(login_poll->data_dir), "%s", ctx->data_dir);
        snprintf(login_poll->ca_file, sizeof(login_poll->ca_file), "%s", ctx->ca_file);
        login_poll->session = *session;
        atomic_store(&login_poll->running, 1);
        *login_poll_thread_handle =
            SDL_CreateThread(ui_startup_login_poll_thread, "weread-login-poll", login_poll);
        if (!*login_poll_thread_handle) {
            atomic_store(&login_poll->running, 0);
            *login_active = 0;
            snprintf(status, status_size,
                     "\xE6\x97\xA0\xE6\xB3\x95\xE5\x88\x9B\xE5\xBB\xBA\xE7\x99\xBB\xE5\xBD\x95\xE8\xBD\xAE\xE8\xAF\xA2\xE7\xBA\xBF\xE7\xA8\x8B");
        }
    } else if (atomic_load(&login_start->failed)) {
        snprintf(status, status_size,
                 "\xE4\xBA\x8C\xE7\xBB\xB4\xE7\xA0\x81\xE7\x94\x9F\xE6\x88\x90\xE5\xA4\xB1\xE8\xB4\xA5");
    }

    return 1;
}

int ui_startup_login_poll_login(LoginPollState *login_poll,
                                SDL_Thread **login_poll_thread_handle, Uint32 *last_poll,
                                char *status, size_t status_size, int *render_requested) {
    if (!login_poll || !login_poll_thread_handle || !last_poll || !status ||
        !render_requested) {
        return 0;
    }

    if (atomic_load(&login_poll->running)) {
        if (SDL_GetTicks() - *last_poll > 1200) {
            *render_requested = 1;
            if (login_poll->last_status == AUTH_POLL_SCANNED) {
                snprintf(status, status_size,
                         "\xE5\xB7\xB2\xE6\x89\xAB\xE7\xA0\x81\xEF\xBC\x8C\xE7\xAD\x89\xE5\xBE\x85\xE7\xA1\xAE\xE8\xAE\xA4...");
            } else {
                snprintf(status, status_size,
                         "\xE7\xAD\x89\xE5\xBE\x85\xE6\x89\xAB\xE7\xA0\x81\xE6\x88\x96\xE7\xA1\xAE\xE8\xAE\xA4...");
            }
            *last_poll = SDL_GetTicks();
        }
        return 0;
    }

    if (*login_poll_thread_handle) {
        SDL_WaitThread(*login_poll_thread_handle, NULL);
        *login_poll_thread_handle = NULL;
        *render_requested = 1;
        if (atomic_load(&login_poll->completed)) {
            return 1;
        }

        snprintf(status, status_size,
                 "\xE7\x99\xBB\xE5\xBD\x95\xE7\xAD\x89\xE5\xBE\x85\xE5\xB7\xB2\xE5\x81\x9C\xE6\xAD\xA2");
        return -1;
    }

    return 0;
}

void ui_startup_login_shutdown(LoginPollState *login_poll, SDL_Thread **login_thread,
                               SDL_Thread **startup_thread_handle,
                               SDL_Thread **login_poll_thread_handle) {
    if (login_thread && *login_thread) {
        SDL_WaitThread(*login_thread, NULL);
        *login_thread = NULL;
    }
    if (startup_thread_handle && *startup_thread_handle) {
        SDL_WaitThread(*startup_thread_handle, NULL);
        *startup_thread_handle = NULL;
    }
    if (login_poll_thread_handle && *login_poll_thread_handle) {
        if (login_poll) {
            atomic_store(&login_poll->stop, 1);
        }
        SDL_WaitThread(*login_poll_thread_handle, NULL);
        *login_poll_thread_handle = NULL;
    }
}

#endif
