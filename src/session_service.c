#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include "reader.h"
#include "session_service.h"
#include "state.h"

static SessionServiceValidateSessionFn session_service_validate_session_override = NULL;
static SessionServiceRemoteLogoutFn session_service_remote_logout_override = NULL;

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

void session_service_set_remote_logout_override(SessionServiceRemoteLogoutFn fn) {
    session_service_remote_logout_override = fn;
}

static void session_service_init_logout_result(SessionLogoutResult *result) {
    if (!result) {
        return;
    }

    result->outcome = SESSION_LOGOUT_LOCAL_FAILED;
    result->local_cleanup_ok = 0;
    result->remote_attempted = 0;
    result->remote_logout_ok = 0;
}

static int session_service_delete_file(const char *path) {
    if (!path || !*path) {
        return -1;
    }
    if (unlink(path) == 0 || access(path, F_OK) != 0) {
        return 0;
    }
    return -1;
}

static int session_service_delete_state_file(ApiContext *ctx, const char *name) {
    char path[1024];

    if (!ctx || !name) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/%s", ctx->state_dir, name) >= (int)sizeof(path)) {
        return -1;
    }
    return session_service_delete_file(path);
}

typedef struct {
    char path[1024];
    char *contents;
    size_t size;
    mode_t mode;
    int existed;
} SessionServiceFileBackup;

static void session_service_free_backup(SessionServiceFileBackup *backup) {
    if (!backup) {
        return;
    }

    free(backup->contents);
    backup->contents = NULL;
    backup->size = 0;
    backup->mode = 0;
    backup->existed = 0;
    backup->path[0] = '\0';
}

static int session_service_capture_file_backup(SessionServiceFileBackup *backup, const char *path) {
    struct stat st;
    FILE *fp = NULL;

    if (!backup || !path) {
        return -1;
    }

    memset(backup, 0, sizeof(*backup));
    if (snprintf(backup->path, sizeof(backup->path), "%s", path) >= (int)sizeof(backup->path)) {
        return -1;
    }

    if (lstat(path, &st) != 0) {
        if (access(path, F_OK) != 0) {
            return 0;
        }
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        return -1;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    if (st.st_size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }

    backup->size = (size_t)st.st_size;
    backup->contents = calloc(backup->size + 1, 1);
    if (!backup->contents) {
        fclose(fp);
        return -1;
    }
    if (backup->size > 0 &&
        fread(backup->contents, 1, backup->size, fp) != backup->size) {
        fclose(fp);
        session_service_free_backup(backup);
        return -1;
    }
    fclose(fp);

    backup->mode = st.st_mode & 0777;
    backup->existed = 1;
    return 0;
}

static int session_service_restore_backup(const SessionServiceFileBackup *backup) {
    FILE *fp;

    if (!backup || !backup->existed || !backup->path[0]) {
        return 0;
    }

    fp = fopen(backup->path, "wb");
    if (!fp) {
        return -1;
    }
    if (backup->size > 0 &&
        fwrite(backup->contents, 1, backup->size, fp) != backup->size) {
        fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0) {
        return -1;
    }
    if (backup->mode != 0) {
        (void)chmod(backup->path, backup->mode);
    }
    return 0;
}

static int session_service_capture_state_backup(ApiContext *ctx, const char *name,
                                                SessionServiceFileBackup *backup) {
    char path[1024];

    if (!ctx || !name || !backup) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/%s", ctx->state_dir, name) >= (int)sizeof(path)) {
        return -1;
    }
    return session_service_capture_file_backup(backup, path);
}

static int session_service_clear_local_session(ApiContext *ctx) {
    SessionServiceFileBackup backups[4];
    int i;

    if (!ctx) {
        return -1;
    }

    memset(backups, 0, sizeof(backups));
    if (session_service_capture_file_backup(&backups[0], ctx->cookie_file) != 0 ||
        session_service_capture_state_backup(ctx, STATE_FILE_SHELF, &backups[1]) != 0 ||
        session_service_capture_state_backup(ctx, STATE_FILE_LAST_READER, &backups[2]) != 0 ||
        session_service_capture_state_backup(ctx, STATE_FILE_READER_POSITIONS, &backups[3]) != 0) {
        for (i = 0; i < (int)(sizeof(backups) / sizeof(backups[0])); i++) {
            session_service_free_backup(&backups[i]);
        }
        return -1;
    }

    if (session_service_delete_file(ctx->cookie_file) != 0 ||
        session_service_delete_state_file(ctx, STATE_FILE_SHELF) != 0 ||
        session_service_delete_state_file(ctx, STATE_FILE_LAST_READER) != 0 ||
        session_service_delete_state_file(ctx, STATE_FILE_READER_POSITIONS) != 0) {
        for (i = 0; i < (int)(sizeof(backups) / sizeof(backups[0])); i++) {
            if (session_service_restore_backup(&backups[i]) != 0) {
                break;
            }
        }
        for (i = 0; i < (int)(sizeof(backups) / sizeof(backups[0])); i++) {
            session_service_free_backup(&backups[i]);
        }
        return -1;
    }

    for (i = 0; i < (int)(sizeof(backups) / sizeof(backups[0])); i++) {
        session_service_free_backup(&backups[i]);
    }
    return 0;
}

static int session_service_remote_logout(ApiContext *ctx) {
    Buffer response = {0};
    char *url;
    int rc;

    if (session_service_remote_logout_override) {
        return session_service_remote_logout_override(ctx);
    }
    if (!ctx) {
        return -1;
    }

    url = api_build_url(WEREAD_BASE_URL, "api/logout");
    if (!url) {
        return -1;
    }
    rc = api_get(ctx, url, &response);
    api_buffer_free(&response);
    free(url);
    return rc;
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

int session_service_logout(ApiContext *ctx, SessionLogoutResult *result) {
    int remote_logout_rc;

    session_service_init_logout_result(result);
    if (session_service_clear_local_session(ctx) != 0) {
        return -1;
    }
    if (result) {
        result->local_cleanup_ok = 1;
    }

    remote_logout_rc = session_service_remote_logout(ctx);
    if (result) {
        result->remote_attempted = 1;
        result->remote_logout_ok = remote_logout_rc == 0 ? 1 : 0;
        result->outcome = remote_logout_rc == 0 ?
            SESSION_LOGOUT_SUCCESS :
            SESSION_LOGOUT_REMOTE_FAILED;
    }
    return 0;
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
