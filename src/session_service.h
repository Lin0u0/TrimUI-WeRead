#ifndef SESSION_SERVICE_H
#define SESSION_SERVICE_H

#include "auth.h"

typedef int (*SessionServiceValidateSessionFn)(ApiContext *ctx, cJSON **shelf_nuxt_out);

int session_service_validate_session(ApiContext *ctx, cJSON **shelf_nuxt_out);
void session_service_set_validate_session_override(SessionServiceValidateSessionFn fn);
int session_service_require_valid_session(ApiContext *ctx, const char **error_message);
int session_service_startup_refresh(ApiContext *ctx, cJSON **shelf_nuxt_out, int *poor_network);
int session_service_login_start(ApiContext *ctx, AuthSession *session, const char *qr_png_path);
int session_service_login_poll_once(ApiContext *ctx, AuthSession *session, AuthPollStatus *status);
int session_service_login_wait(ApiContext *ctx, AuthSession *session, int timeout_seconds);

int session_service_startup_refresh_background(const char *data_dir, const char *ca_file,
                                               cJSON **shelf_nuxt_out, int *poor_network);
int session_service_login_start_background(const char *data_dir, const char *ca_file,
                                           AuthSession *session, const char *qr_png_path);
int session_service_login_poll_background(const char *data_dir, const char *ca_file,
                                          AuthSession *session, const int *stop,
                                          int *completed, AuthPollStatus *last_status);

#endif
