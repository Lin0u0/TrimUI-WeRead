#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reader.h"
#include "reader_internal.h"

#define READER_FETCH_TIMEOUT_SECONDS 8L

static ReaderFetchPageFn reader_fetch_override = NULL;

void reader_set_fetch_override(ReaderFetchPageFn fn) {
    reader_fetch_override = fn;
}

int reader_fetch_page(ApiContext *ctx, const char *url, Buffer *buf) {
    if (reader_fetch_override) {
        return reader_fetch_override(ctx, url, buf);
    }
    return api_get_once_timeout(ctx, url, buf, READER_FETCH_TIMEOUT_SECONDS);
}

int reader_target_is_article(const char *target) {
    if (!target || !*target) {
        return 0;
    }
    return strstr(target, "mpdetail?") != NULL || strstr(target, "/mpdetail") != NULL;
}

char *reader_extract_query_value(const char *target, const char *key) {
    const char *query;
    const char *p;
    size_t key_len;

    if (!target || !key || !key[0]) {
        return NULL;
    }

    query = strchr(target, '?');
    p = query ? query + 1 : target;
    key_len = strlen(key);
    while (p && *p) {
        const char *value_start;
        const char *value_end;

        if ((p == target || p[-1] == '?' || p[-1] == '&') &&
            strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            value_start = p + key_len + 1;
            value_end = value_start;
            while (*value_end && *value_end != '&' && *value_end != '#') {
                value_end++;
            }
            if (value_end > value_start) {
                return reader_dup_range(value_start, value_end);
            }
            return NULL;
        }
        p = strchr(p, '&');
        if (p) {
            p++;
        }
    }

    return NULL;
}

char *reader_build_url(ApiContext *ctx, const char *target, int font_size) {
    char *escaped;
    char *url;

    if (!target) {
        return NULL;
    }
    if (strncmp(target, "http://", 7) == 0 || strncmp(target, "https://", 8) == 0) {
        return strdup(target);
    }
    if (strncmp(target, "/web/reader/", 12) == 0) {
        escaped = api_escape(ctx, target + 12);
        if (!escaped) {
            return NULL;
        }
        url = malloc(strlen(WEREAD_BASE_URL) + strlen("/reader?bc=&fs=") + strlen(escaped) + 8);
        if (!url) {
            free(escaped);
            return NULL;
        }
        sprintf(url, "%s/reader?bc=%s&fs=%d", WEREAD_BASE_URL, escaped, font_size);
        free(escaped);
        return url;
    }
    if (strncmp(target, "/wrwebsimplenjlogic/mpdetail?", 28) == 0) {
        return api_build_url("https://weread.qq.com", target + 1);
    }
    if (strncmp(target, "/mpdetail?", 10) == 0) {
        return api_build_url(WEREAD_BASE_URL, target + 1);
    }
    if (strncmp(target, "/wrwebsimplenjlogic/reader?", 26) == 0) {
        return api_build_url("https://weread.qq.com", target + 1);
    }
    if (strncmp(target, "/reader?", 8) == 0) {
        return api_build_url(WEREAD_BASE_URL, target + 1);
    }

    escaped = api_escape(ctx, target);
    if (!escaped) {
        return NULL;
    }
    url = malloc(strlen(WEREAD_BASE_URL) + strlen("/reader?bc=&fs=") + strlen(escaped) + 8);
    if (!url) {
        free(escaped);
        return NULL;
    }
    sprintf(url, "%s/reader?bc=%s&fs=%d", WEREAD_BASE_URL, escaped, font_size);
    free(escaped);
    return url;
}
