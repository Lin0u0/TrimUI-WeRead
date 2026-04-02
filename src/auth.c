#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "auth.h"
#include "json.h"
#include "shelf.h"

static int b64_value(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

static int save_base64_png(const char *data_url, const char *path) {
    const char *marker = "base64,";
    const char *base64 = strstr(data_url, marker);
    FILE *fp;
    int val = 0;
    int valb = -8;

    if (!base64) {
        fprintf(stderr, "QR response did not contain base64 image data\n");
        return -1;
    }
    base64 += strlen(marker);
    fp = fopen(path, "wb");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    for (const char *p = base64; *p; p++) {
        int c;
        if (*p == '=') {
            break;
        }
        c = b64_value(*p);
        if (c < 0) {
            continue;
        }
        val = (val << 6) | c;
        valb += 6;
        if (valb >= 0) {
            unsigned char byte = (unsigned char)((val >> valb) & 0xFF);
            fwrite(&byte, 1, 1, fp);
            valb -= 8;
        }
    }

    fclose(fp);
    return 0;
}

static int json_item_to_text(cJSON *item, char *buf, size_t buf_size) {
    if (!item || !buf || buf_size == 0) {
        return -1;
    }
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(buf, buf_size, "%s", item->valuestring);
        return 0;
    }
    if (cJSON_IsNumber(item)) {
        snprintf(buf, buf_size, "%lld", (long long)item->valuedouble);
        return 0;
    }
    return -1;
}

int auth_start(ApiContext *ctx, AuthSession *session, const char *qr_png_path) {
    cJSON *uid_json = NULL;
    cJSON *qr_json = NULL;
    char *escaped = NULL;
    char *url = NULL;
    char confirm_url[1024];
    int rc = -1;

    memset(session, 0, sizeof(*session));
    snprintf(session->cgi_key, sizeof(session->cgi_key), "%u", (unsigned int)(rand() % 100000));
    snprintf(session->qr_png_path, sizeof(session->qr_png_path), "%s", qr_png_path);

    url = api_build_url(WEREAD_API_BASE_URL, "getuid");
    uid_json = api_get_json(ctx, url);
    free(url);
    url = NULL;
    if (!uid_json) {
        goto cleanup;
    }

    {
        const char *uid = json_get_string(uid_json, "uid");
        if (!uid) {
            fprintf(stderr, "Did not receive uid from getuid\n");
            goto cleanup;
        }
        snprintf(session->uid, sizeof(session->uid), "%s", uid);
    }

    snprintf(confirm_url, sizeof(confirm_url),
             "https://weread.qq.com/web/confirm?pf=2&uid=%s", session->uid);
    escaped = api_escape(ctx, confirm_url);
    if (!escaped) {
        goto cleanup;
    }

    url = malloc(strlen(WEREAD_API_BASE_URL) + strlen("/qrcode?url=") + strlen(escaped) + 1);
    if (!url) {
        goto cleanup;
    }
    snprintf(url, strlen(WEREAD_API_BASE_URL) + strlen("/qrcode?url=") + strlen(escaped) + 1,
             "%s/qrcode?url=%s", WEREAD_API_BASE_URL, escaped);
    qr_json = api_get_json(ctx, url);
    if (!qr_json) {
        goto cleanup;
    }

    {
        const char *qr_data = json_get_string(qr_json, "data");
        if (!qr_data || !json_is_truthy(json_get_path(qr_json, "succ"))) {
            fprintf(stderr, "Did not receive QR code data\n");
            goto cleanup;
        }
        snprintf(session->qr_url, sizeof(session->qr_url), "%s", confirm_url);
        if (save_base64_png(qr_data, session->qr_png_path) != 0) {
            fprintf(stderr, "Failed to save QR PNG to %s\n", session->qr_png_path);
            goto cleanup;
        }
    }

    printf("QR code written to %s\n", session->qr_png_path);
    printf("Scan it with WeChat and keep this process running.\n");
    rc = 0;

cleanup:
    cJSON_Delete(uid_json);
    cJSON_Delete(qr_json);
    free(escaped);
    free(url);
    return rc;
}

int auth_poll_once(ApiContext *ctx, AuthSession *session, AuthPollStatus *status) {
    Buffer status_buf = {0};
    Buffer login_buf = {0};
    cJSON *status_json = NULL;
    cJSON *login_json = NULL;
    char *status_url = api_build_url(WEREAD_API_BASE_URL, "getlogininfo");
    char *login_url = api_build_url(WEREAD_API_BASE_URL, "weblogin");
    char body[1024];
    cJSON *vid_item;
    char vid_text[64];
    const char *skey;
    const char *code;
    int is_auto_logout;
    cJSON *scan_value;
    int rc = -1;

    if (status) {
        *status = AUTH_POLL_WAITING;
    }
    if (!status_url || !login_url) {
        free(status_url);
        free(login_url);
        return -1;
    }

    snprintf(body, sizeof(body),
             "{\"uid\":\"%s\",\"cgiKey\":\"%s\"}",
             session->uid, session->cgi_key);
    if (api_post_timeout(ctx, status_url, body, &status_buf, 40L) != 0) {
        rc = 0;
        goto cleanup;
    }
    status_json = cJSON_Parse(status_buf.data);
    if (!status_json) {
        rc = 0;
        goto cleanup;
    }

    vid_item = json_get_path(status_json, "vid");
    skey = json_get_string(status_json, "skey");
    code = json_get_string(status_json, "code");
    is_auto_logout = json_is_truthy(json_get_path(status_json, "isAutoLogout"));
    scan_value = json_get_path(status_json, "scan");

    if (scan_value && cJSON_IsNumber(scan_value) && scan_value->valueint != 0 && status) {
        *status = AUTH_POLL_SCANNED;
    }

    if (json_item_to_text(vid_item, vid_text, sizeof(vid_text)) != 0 || !(skey && code)) {
        rc = 0;
        goto cleanup;
    }

    snprintf(body, sizeof(body),
             "{\"vid\":%s,\"skey\":\"%s\",\"code\":\"%s\","
             "\"isAutoLogout\":%s,\"pf\":2,\"cgiKey\":\"%s\",\"fp\":\"\"}",
             vid_text, skey, code, is_auto_logout ? "true" : "false", session->cgi_key);
    if (api_post_timeout(ctx, login_url, body, &login_buf, 15L) != 0) {
        rc = 0;
        goto cleanup;
    }
    login_json = cJSON_Parse(login_buf.data);
    if (login_json &&
        json_get_path(login_json, "vid") &&
        json_get_string(login_json, "accessToken")) {
        if (status) {
            *status = AUTH_POLL_CONFIRMED;
        }
        rc = 0;
        goto cleanup;
    }

    rc = -1;

cleanup:
    cJSON_Delete(status_json);
    cJSON_Delete(login_json);
    api_buffer_free(&status_buf);
    api_buffer_free(&login_buf);
    free(status_url);
    free(login_url);
    return rc;
}

int auth_poll_until_done(ApiContext *ctx, AuthSession *session, int timeout_seconds) {
    time_t deadline = time(NULL) + timeout_seconds;
    int rc = -1;

    while (time(NULL) < deadline) {
        AuthPollStatus status = AUTH_POLL_WAITING;
        if (auth_poll_once(ctx, session, &status) == 0) {
            if (status == AUTH_POLL_CONFIRMED) {
                printf("Login succeeded. Cookies saved to %s\n", ctx->cookie_file);
                rc = 0;
                break;
            }
            if (status == AUTH_POLL_SCANNED) {
                printf("QR scanned. Waiting for final confirmation...\n");
            } else {
                printf("Waiting for QR scan or confirmation...\n");
            }
        }
        sleep(2);
    }

    if (rc != 0) {
        fprintf(stderr, "Timed out waiting for login confirmation\n");
    }
    return rc;
}

int auth_check_session(ApiContext *ctx, cJSON **shelf_nuxt_out) {
    cJSON *nuxt;
    const char *route_path;
    cJSON *books;
    int valid = 0;

    if (shelf_nuxt_out) {
        *shelf_nuxt_out = NULL;
    }

    nuxt = shelf_load(ctx, 0, NULL);
    if (!nuxt) {
        return -1;
    }

    route_path = json_get_string(nuxt, "routePath");
    books = json_get_path(nuxt, "state.shelf.books");

    if (route_path && strstr(route_path, "/login")) {
        valid = 0;
    } else if (route_path && strstr(route_path, "/shelf")) {
        valid = 1;
    } else if (books && cJSON_IsArray(books)) {
        valid = 1;
    }

    if (valid && shelf_nuxt_out) {
        *shelf_nuxt_out = nuxt;
        return 1;
    }

    cJSON_Delete(nuxt);
    return valid;
}
