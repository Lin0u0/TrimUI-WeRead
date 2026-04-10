#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "reader.h"
#include "session_service.h"

static SessionServiceValidateSessionFn session_service_validate_session_override = NULL;

static int session_service_init_worker_context(ApiContext *ctx, const char *data_dir,
                                               const char *ca_file) {
    if (!ctx || !data_dir) {
        return -1;
    }
    if (api_init(ctx, data_dir) != 0) {
        return -1;
    }
    if (ca_file && *ca_file) {
        snprintf(ctx->ca_file, sizeof(ctx->ca_file), "%s", ca_file);
    }
    return 0;
}

int session_service_validate_session(ApiContext *ctx, cJSON **shelf_nuxt_out) {
#if !HAVE_SDL
    /*
     * Phase 4 maintainer note: this host-only verification seam keeps
     * WEREAD_TEST_SESSION_STATUS and the override hook deterministic for tests,
     * while production callers still rely on auth_check_session() semantics.
     */
    const char *override_value = getenv("WEREAD_TEST_SESSION_STATUS");

    if (override_value && *override_value) {
        return atoi(override_value);
    }
#endif
    if (session_service_validate_session_override) {
        return session_service_validate_session_override(ctx, shelf_nuxt_out);
    }
    return auth_check_session(ctx, shelf_nuxt_out);
}

void session_service_set_validate_session_override(SessionServiceValidateSessionFn fn) {
    session_service_validate_session_override = fn;
}

int session_service_require_valid_session(ApiContext *ctx, const char **error_message) {
    int session_ok = session_service_validate_session(ctx, NULL);

    if (error_message) {
        if (session_ok == 0) {
            *error_message = "Session expired or not logged in. Run `weread login` first.";
        } else if (session_ok < 0) {
            *error_message = "Unable to verify login status. Check your network and try again.";
        } else {
            *error_message = NULL;
        }
    }
    return session_ok;
}

int session_service_startup_refresh(ApiContext *ctx, cJSON **shelf_nuxt_out, int *poor_network) {
    int session_ok = session_service_validate_session(ctx, shelf_nuxt_out);

    if (poor_network) {
        *poor_network = ctx ? ctx->poor_network : 0;
    }
    if (ctx) {
        ctx->poor_network = 0;
    }
    if (session_ok == 1) {
        (void)reader_warmup_font(ctx);
    }
    return session_ok;
}

int session_service_login_start(ApiContext *ctx, AuthSession *session, const char *qr_png_path) {
    return auth_start(ctx, session, qr_png_path);
}

int session_service_login_poll_once(ApiContext *ctx, AuthSession *session, AuthPollStatus *status) {
    return auth_poll_once(ctx, session, status);
}

int session_service_login_wait(ApiContext *ctx, AuthSession *session, int timeout_seconds) {
    return auth_poll_until_done(ctx, session, timeout_seconds);
}

int session_service_startup_refresh_background(const char *data_dir, const char *ca_file,
                                               cJSON **shelf_nuxt_out, int *poor_network) {
    ApiContext ctx;
    int session_ok;

    if (session_service_init_worker_context(&ctx, data_dir, ca_file) != 0) {
        return -1;
    }
    session_ok = session_service_startup_refresh(&ctx, shelf_nuxt_out, poor_network);
    api_cleanup(&ctx);
    return session_ok;
}

int session_service_login_start_background(const char *data_dir, const char *ca_file,
                                           AuthSession *session, const char *qr_png_path) {
    ApiContext ctx;
    int rc;

    if (session_service_init_worker_context(&ctx, data_dir, ca_file) != 0) {
        return -1;
    }
    rc = session_service_login_start(&ctx, session, qr_png_path);
    api_cleanup(&ctx);
    return rc;
}

int session_service_login_poll_background(const char *data_dir, const char *ca_file,
                                          AuthSession *session, const int *stop,
                                          int *completed, AuthPollStatus *last_status) {
    ApiContext ctx;
    int rc = -1;

    if (session_service_init_worker_context(&ctx, data_dir, ca_file) != 0) {
        return -1;
    }

    while (!(stop && *stop) && !(completed && *completed)) {
        AuthPollStatus status = AUTH_POLL_WAITING;

        if (session_service_login_poll_once(&ctx, session, &status) == 0) {
            if (last_status) {
                *last_status = status;
            }
            if (status == AUTH_POLL_CONFIRMED) {
                if (completed) {
                    *completed = 1;
                }
                rc = 0;
                break;
            }
        } else if (last_status) {
            *last_status = AUTH_POLL_ERROR;
        }
        usleep(700000);
    }

    api_cleanup(&ctx);
    return rc;
}
