#ifndef AUTH_H
#define AUTH_H

#include "api.h"

typedef struct {
    char uid[128];
    char cgi_key[32];
    char qr_url[1024];
    char qr_png_path[512];
} AuthSession;

typedef enum {
    AUTH_POLL_ERROR = -1,
    AUTH_POLL_WAITING = 0,
    AUTH_POLL_SCANNED = 1,
    AUTH_POLL_CONFIRMED = 2
} AuthPollStatus;

int auth_start(ApiContext *ctx, AuthSession *session, const char *qr_png_path);
int auth_poll_once(ApiContext *ctx, AuthSession *session, AuthPollStatus *status);
int auth_poll_until_done(ApiContext *ctx, AuthSession *session, int timeout_seconds);
int auth_check_session(ApiContext *ctx, cJSON **shelf_nuxt_out);

#endif
