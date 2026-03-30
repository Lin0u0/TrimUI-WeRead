#ifndef API_H
#define API_H

#include <curl/curl.h>
#include "cJSON.h"

#define WEREAD_BASE_URL "https://weread.qq.com/wrwebsimplenjlogic"
#define WEREAD_API_BASE_URL WEREAD_BASE_URL "/api"
#define KINDLE_USER_AGENT \
    "Mozilla/5.0 (X11; U; Linux armv7l like Android; en-us) " \
    "AppleWebKit/531.2+ (KHTML, like Gecko) Version/5.0 " \
    "Safari/533.2+ Kindle/3.0+"

/* Dynamic buffer for HTTP responses */
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} Buffer;

/* Shared API context */
typedef struct {
    CURL *curl;
    CURLcode last_curl_code;
    char data_dir[512];
    char state_dir[512];
    char cookie_file[512];
    char ca_file[512];
    char error_buf[CURL_ERROR_SIZE];
    int poor_network;  /* set to 1 when a request needed retries */
} ApiContext;

/* Initialize/cleanup */
int api_init(ApiContext *ctx, const char *data_dir);
void api_cleanup(ApiContext *ctx);

/* Fetch a URL, return response in buf. Caller must free buf->data. */
int api_get(ApiContext *ctx, const char *url, Buffer *buf);
int api_get_with_ua(ApiContext *ctx, const char *url, const char *user_agent, Buffer *buf);
int api_post(ApiContext *ctx, const char *url, const char *body, Buffer *buf);
int api_post_timeout(ApiContext *ctx, const char *url, const char *body, Buffer *buf, long timeout_seconds);
void api_buffer_free(Buffer *buf);

/* Extract window.__NUXT__ JSON from SSR HTML. Returns cJSON* (caller frees). */
cJSON *api_extract_nuxt(const char *html, size_t html_len);

/* Fetch a URL and return the extracted NUXT state. */
cJSON *api_fetch_nuxt(ApiContext *ctx, const char *url);

/* Download binary data (e.g., QR code image) */
int api_download(ApiContext *ctx, const char *url, Buffer *buf);

/* Helpers for API endpoints and JSON payloads. */
char *api_build_url(const char *base, const char *path);
char *api_escape(ApiContext *ctx, const char *value);
cJSON *api_get_json(ApiContext *ctx, const char *url);
cJSON *api_post_json(ApiContext *ctx, const char *url, const char *body);
cJSON *api_post_json_timeout(ApiContext *ctx, const char *url, const char *body, long timeout_seconds);

#endif
