#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "api.h"

static int is_transient_error(CURLcode code) {
    return code == CURLE_OPERATION_TIMEDOUT ||
           code == CURLE_COULDNT_CONNECT ||
           code == CURLE_COULDNT_RESOLVE_HOST ||
           code == CURLE_COULDNT_RESOLVE_PROXY ||
           code == CURLE_GOT_NOTHING ||
           code == CURLE_RECV_ERROR ||
           code == CURLE_SEND_ERROR;
}

static void buf_init(Buffer *buf) {
    buf->capacity = 4096;
    buf->size = 0;
    buf->data = malloc(buf->capacity);
    if (buf->data) {
        buf->data[0] = '\0';
    }
}

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    Buffer *buf = (Buffer *)userdata;
    size_t total = size * nmemb;
    size_t needed = buf->size + total + 1;

    if (needed > buf->capacity) {
        while (buf->capacity < needed) {
            buf->capacity *= 2;
        }
        char *tmp = realloc(buf->data, buf->capacity);
        if (!tmp) {
            return 0;
        }
        buf->data = tmp;
    }

    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

void api_buffer_free(Buffer *buf) {
    if (!buf) {
        return;
    }
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

int api_init(ApiContext *ctx, const char *data_dir) {
    memset(ctx, 0, sizeof(*ctx));
    snprintf(ctx->data_dir, sizeof(ctx->data_dir), "%s", data_dir);
    snprintf(ctx->state_dir, sizeof(ctx->state_dir), "%s/state", data_dir);
    snprintf(ctx->cookie_file, sizeof(ctx->cookie_file), "%s/cookies.txt", data_dir);
    if (mkdir(ctx->data_dir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir data_dir");
        return -1;
    }
    if (mkdir(ctx->state_dir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir state_dir");
        return -1;
    }
    ctx->curl = curl_easy_init();
    if (!ctx->curl) {
        return -1;
    }
    return 0;
}

void api_cleanup(ApiContext *ctx) {
    if (ctx->curl) {
        curl_easy_cleanup(ctx->curl);
        ctx->curl = NULL;
    }
}

static void setup_curl(ApiContext *ctx, const char *url, const char *user_agent, Buffer *buf,
                       long timeout_seconds) {
    CURL *c = ctx->curl;
    curl_easy_reset(c);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, user_agent ? user_agent : KINDLE_USER_AGENT);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, buf);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_COOKIEFILE, ctx->cookie_file);
    curl_easy_setopt(c, CURLOPT_COOKIEJAR, ctx->cookie_file);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, ctx->error_buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    if (ctx->ca_file[0]) {
        curl_easy_setopt(c, CURLOPT_CAINFO, ctx->ca_file);
    }
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
}

static int api_get_ua_internal(ApiContext *ctx, const char *url, const char *user_agent,
                               Buffer *buf, long timeout_seconds) {
    long retry_timeout = timeout_seconds < 10 ? timeout_seconds : 10;

    for (int attempt = 0; attempt <= 1; attempt++) {
        buf_init(buf);
        setup_curl(ctx, url, user_agent, buf, attempt == 0 ? timeout_seconds : retry_timeout);
        ctx->last_curl_code = curl_easy_perform(ctx->curl);
        CURLcode res = ctx->last_curl_code;

        if (res == CURLE_OK) {
            long code;
            curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &code);
            if (code >= 200 && code < 400) {
                return 0;
            }
            fprintf(stderr, "GET %s returned %ld\n", url, code);
            api_buffer_free(buf);
            return -1;
        }

        if (attempt == 0 && is_transient_error(res)) {
            api_buffer_free(buf);
            sleep(1);
            continue;
        }

        if (is_transient_error(res) || res == CURLE_OPERATION_TIMEDOUT) {
            ctx->poor_network = 1;
        }
        if (res != CURLE_OPERATION_TIMEDOUT) {
            fprintf(stderr, "GET %s failed: %s\n", url, ctx->error_buf);
        }
        api_buffer_free(buf);
        return -1;
    }
    return -1;
}

int api_get(ApiContext *ctx, const char *url, Buffer *buf) {
    return api_get_ua_internal(ctx, url, KINDLE_USER_AGENT, buf, 30L);
}

int api_get_with_ua(ApiContext *ctx, const char *url, const char *user_agent, Buffer *buf) {
    return api_get_ua_internal(ctx, url, user_agent, buf, 30L);
}

static int api_post_internal(ApiContext *ctx, const char *url, const char *body, Buffer *buf,
                             long timeout_seconds) {
    long retry_timeout = timeout_seconds < 10 ? timeout_seconds : 10;

    for (int attempt = 0; attempt <= 1; attempt++) {
        struct curl_slist *headers = NULL;

        buf_init(buf);
        setup_curl(ctx, url, KINDLE_USER_AGENT, buf, attempt == 0 ? timeout_seconds : retry_timeout);
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Referer: " WEREAD_BASE_URL "/");
        headers = curl_slist_append(headers, "Origin: https://weread.qq.com");
        curl_easy_setopt(ctx->curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(ctx->curl, CURLOPT_POSTFIELDS, body);
        ctx->last_curl_code = curl_easy_perform(ctx->curl);
        CURLcode res = ctx->last_curl_code;
        curl_slist_free_all(headers);

        if (res == CURLE_OK) {
            long code;
            curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &code);
            if (code >= 200 && code < 400) {
                return 0;
            }
            fprintf(stderr, "POST %s returned %ld\n", url, code);
            api_buffer_free(buf);
            return -1;
        }

        if (attempt == 0 && is_transient_error(res)) {
            api_buffer_free(buf);
            sleep(1);
            continue;
        }

        if (is_transient_error(res) || res == CURLE_OPERATION_TIMEDOUT) {
            ctx->poor_network = 1;
        }
        if (res != CURLE_OPERATION_TIMEDOUT) {
            fprintf(stderr, "POST %s failed: %s\n", url, ctx->error_buf);
        }
        api_buffer_free(buf);
        return -1;
    }
    return -1;
}

int api_post(ApiContext *ctx, const char *url, const char *body, Buffer *buf) {
    return api_post_internal(ctx, url, body, buf, 30L);
}

int api_post_timeout(ApiContext *ctx, const char *url, const char *body, Buffer *buf, long timeout_seconds) {
    return api_post_internal(ctx, url, body, buf, timeout_seconds);
}

int api_download(ApiContext *ctx, const char *url, Buffer *buf) {
    for (int attempt = 0; attempt <= 1; attempt++) {
        buf_init(buf);
        setup_curl(ctx, url, KINDLE_USER_AGENT, buf, attempt == 0 ? 30L : 10L);
        ctx->last_curl_code = curl_easy_perform(ctx->curl);
        CURLcode res = ctx->last_curl_code;

        if (res == CURLE_OK) {
            return 0;
        }

        if (attempt == 0 && is_transient_error(res)) {
            api_buffer_free(buf);
            sleep(1);
            continue;
        }

        if (is_transient_error(res) || res == CURLE_OPERATION_TIMEDOUT) {
            ctx->poor_network = 1;
        }
        fprintf(stderr, "Download %s failed: %s\n", url, ctx->error_buf);
        api_buffer_free(buf);
        return -1;
    }
    return -1;
}

/* api_extract_nuxt is implemented in js_parser.c */

cJSON *api_fetch_nuxt(ApiContext *ctx, const char *url) {
    Buffer buf;
    cJSON *nuxt;

    if (api_get(ctx, url, &buf) != 0) {
        return NULL;
    }
    nuxt = api_extract_nuxt(buf.data, buf.size);
    api_buffer_free(&buf);
    return nuxt;
}

char *api_build_url(const char *base, const char *path) {
    size_t base_len;
    size_t path_len;
    char *url;

    if (!base || !path) {
        return NULL;
    }

    base_len = strlen(base);
    path_len = strlen(path);
    url = malloc(base_len + path_len + 2);
    if (!url) {
        return NULL;
    }
    strcpy(url, base);
    if (path[0] != '/') {
        strcat(url, "/");
    }
    strcat(url, path);
    return url;
}

char *api_escape(ApiContext *ctx, const char *value) {
    char *encoded;
    char *copy;

    if (!ctx || !ctx->curl || !value) {
        return NULL;
    }
    encoded = curl_easy_escape(ctx->curl, value, 0);
    if (!encoded) {
        return NULL;
    }
    copy = strdup(encoded);
    curl_free(encoded);
    return copy;
}

cJSON *api_get_json(ApiContext *ctx, const char *url) {
    Buffer buf;
    cJSON *json;

    if (api_get(ctx, url, &buf) != 0) {
        return NULL;
    }
    json = cJSON_Parse(buf.data);
    if (!json) {
        fprintf(stderr, "Failed to parse JSON from %s\n", url);
    }
    api_buffer_free(&buf);
    return json;
}

cJSON *api_post_json(ApiContext *ctx, const char *url, const char *body) {
    return api_post_json_timeout(ctx, url, body, 30L);
}

cJSON *api_post_json_timeout(ApiContext *ctx, const char *url, const char *body, long timeout_seconds) {
    Buffer buf;
    cJSON *json;

    if (api_post_internal(ctx, url, body, &buf, timeout_seconds) != 0) {
        return NULL;
    }
    json = cJSON_Parse(buf.data);
    if (!json) {
        fprintf(stderr, "Failed to parse JSON from %s\n", url);
    }
    api_buffer_free(&buf);
    return json;
}
