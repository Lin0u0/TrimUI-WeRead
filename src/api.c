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

static const char *skip_ws(const char *p, const char *end) {
    while (p < end && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static char *dup_range(const char *start, const char *end) {
    size_t len = (size_t)(end - start);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

static const char *find_matching(const char *start, const char *end, char open_ch, char close_ch) {
    int depth = 0;
    int in_string = 0;
    char string_char = 0;

    for (const char *p = start; p < end; p++) {
        char c = *p;
        if (in_string) {
            if (c == '\\' && p + 1 < end) {
                p++;
            } else if (c == string_char) {
                in_string = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            in_string = 1;
            string_char = c;
            continue;
        }
        if (c == open_ch) {
            depth++;
        } else if (c == close_ch) {
            depth--;
            if (depth == 0) {
                return p;
            }
        }
    }

    return NULL;
}

static int append_str(char **out, size_t *len, size_t *cap, const char *text, size_t text_len) {
    size_t needed = *len + text_len + 1;
    if (needed > *cap) {
        while (*cap < needed) {
            *cap *= 2;
        }
        char *tmp = realloc(*out, *cap);
        if (!tmp) {
            return -1;
        }
        *out = tmp;
    }
    memcpy(*out + *len, text, text_len);
    *len += text_len;
    (*out)[*len] = '\0';
    return 0;
}

static int parse_js_literal(const char **cursor, const char *end, char **out_literal) {
    const char *p = skip_ws(*cursor, end);
    const char *start;

    if (p >= end) {
        return -1;
    }

    if (*p == '"' || *p == '\'') {
        char quote = *p++;
        start = p - 1;
        while (p < end) {
            if (*p == '\\' && p + 1 < end) {
                p += 2;
                continue;
            }
            if (*p == quote) {
                p++;
                *out_literal = dup_range(start, p);
                *cursor = p;
                return *out_literal ? 0 : -1;
            }
            p++;
        }
        return -1;
    }

    if (strncmp(p, "void 0", 6) == 0) {
        *out_literal = strdup("null");
        *cursor = p + 6;
        return *out_literal ? 0 : -1;
    }
    if (strncmp(p, "!0", 2) == 0) {
        *out_literal = strdup("true");
        *cursor = p + 2;
        return *out_literal ? 0 : -1;
    }
    if (strncmp(p, "!1", 2) == 0) {
        *out_literal = strdup("false");
        *cursor = p + 2;
        return *out_literal ? 0 : -1;
    }
    if (strncmp(p, "true", 4) == 0 || strncmp(p, "null", 4) == 0) {
        *out_literal = dup_range(p, p + 4);
        *cursor = p + 4;
        return *out_literal ? 0 : -1;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out_literal = dup_range(p, p + 5);
        *cursor = p + 5;
        return *out_literal ? 0 : -1;
    }
    if (strncmp(p, "Array(", 6) == 0) {
        const char *close = p + 6;
        while (close < end && *close != ')') {
            close++;
        }
        if (close < end && *close == ')') {
            *out_literal = strdup("[]");
            *cursor = close + 1;
            return *out_literal ? 0 : -1;
        }
        return -1;
    }
    if (*p == '{') {
        const char *close = find_matching(p, end, '{', '}');
        if (!close) {
            return -1;
        }
        *out_literal = strdup("{}");
        *cursor = close + 1;
        return *out_literal ? 0 : -1;
    }
    if (*p == '[') {
        const char *close = find_matching(p, end, '[', ']');
        if (!close) {
            return -1;
        }
        *out_literal = strdup("[]");
        *cursor = close + 1;
        return *out_literal ? 0 : -1;
    }
    if (*p == '-' || *p == '.' || isdigit((unsigned char)*p)) {
        start = p;
        if (*p == '-') {
            p++;
        }
        if (p < end && *p == '.') {
            p++;
        }
        while (p < end && isdigit((unsigned char)*p)) {
            p++;
        }
        if (p < end && *p == '.') {
            p++;
            while (p < end && isdigit((unsigned char)*p)) {
                p++;
            }
        }
        *out_literal = dup_range(start, p);
        *cursor = p;
        return *out_literal ? 0 : -1;
    }
    return -1;
}

static char *substitute_params(const char *object_str, char **params, char **values, int count) {
    size_t cap = strlen(object_str) + 64;
    size_t len = 0;
    char *out = malloc(cap);
    int in_string = 0;
    char string_char = 0;

    if (!out) {
        return NULL;
    }
    out[0] = '\0';

    for (const char *p = object_str; *p; p++) {
        if (in_string) {
            if (append_str(&out, &len, &cap, p, 1) != 0) {
                free(out);
                return NULL;
            }
            if (*p == '\\' && p[1]) {
                if (append_str(&out, &len, &cap, p + 1, 1) != 0) {
                    free(out);
                    return NULL;
                }
                p++;
            } else if (*p == string_char) {
                in_string = 0;
            }
            continue;
        }

        if (*p == '"' || *p == '\'') {
            in_string = 1;
            string_char = *p;
            if (append_str(&out, &len, &cap, p, 1) != 0) {
                free(out);
                return NULL;
            }
            continue;
        }

        if (isalpha((unsigned char)*p) || *p == '_' || *p == '$') {
            const char *start = p;
            size_t ident_len = 1;
            int replaced = 0;

            while (isalnum((unsigned char)p[ident_len]) || p[ident_len] == '_' || p[ident_len] == '$') {
                ident_len++;
            }

            for (int i = 0; i < count; i++) {
                if (strlen(params[i]) == ident_len && strncmp(start, params[i], ident_len) == 0) {
                    if (append_str(&out, &len, &cap, values[i], strlen(values[i])) != 0) {
                        free(out);
                        return NULL;
                    }
                    p += ident_len - 1;
                    replaced = 1;
                    break;
                }
            }

            if (!replaced) {
                if (append_str(&out, &len, &cap, start, ident_len) != 0) {
                    free(out);
                    return NULL;
                }
                p += ident_len - 1;
            }
            continue;
        }

        if (append_str(&out, &len, &cap, p, 1) != 0) {
            free(out);
            return NULL;
        }
    }

    return out;
}

static char *quote_object_keys(const char *text) {
    size_t cap = strlen(text) * 2 + 1;
    size_t len = 0;
    char *out = malloc(cap);
    int in_string = 0;
    char string_char = 0;

    if (!out) {
        return NULL;
    }
    out[0] = '\0';

    for (const char *p = text; *p; p++) {
        if (in_string) {
            if (*p == '\\' && p[1]) {
                if (append_str(&out, &len, &cap, p, 1) != 0 ||
                    append_str(&out, &len, &cap, p + 1, 1) != 0) {
                    free(out);
                    return NULL;
                }
                p++;
            } else if (*p == string_char) {
                char quote = '"';
                if (append_str(&out, &len, &cap, &quote, 1) != 0) {
                    free(out);
                    return NULL;
                }
                in_string = 0;
            } else {
                if (*p == '"' && string_char == '\'') {
                    if (append_str(&out, &len, &cap, "\\\"", 2) != 0) {
                        free(out);
                        return NULL;
                    }
                } else if (append_str(&out, &len, &cap, p, 1) != 0) {
                    free(out);
                    return NULL;
                }
            }
            continue;
        }

        if (*p == '"' || *p == '\'') {
            in_string = 1;
            string_char = *p;
            if (append_str(&out, &len, &cap, "\"", 1) != 0) {
                free(out);
                return NULL;
            }
            continue;
        }

        if (isalpha((unsigned char)*p) || *p == '_' || *p == '$') {
            const char *start = p;
            const char *lookahead = p + 1;
            size_t ident_len = 1;
            while (isalnum((unsigned char)*lookahead) || *lookahead == '_' || *lookahead == '$') {
                ident_len++;
                lookahead++;
            }
            lookahead = skip_ws(lookahead, text + strlen(text));
            if (*lookahead == ':') {
                if (append_str(&out, &len, &cap, "\"", 1) != 0 ||
                    append_str(&out, &len, &cap, start, ident_len) != 0 ||
                    append_str(&out, &len, &cap, "\"", 1) != 0) {
                    free(out);
                    return NULL;
                }
                p += ident_len - 1;
                continue;
            }
        }

        if (append_str(&out, &len, &cap, p, 1) != 0) {
            free(out);
            return NULL;
        }
    }

    return out;
}

static void replace_literal(char *text, const char *from, const char *to) {
    size_t from_len = strlen(from);
    size_t to_len = strlen(to);
    char *p = text;

    while ((p = strstr(p, from)) != NULL) {
        memcpy(p, to, to_len);
        if (to_len < from_len) {
            memset(p + to_len, ' ', from_len - to_len);
        }
        p += from_len;
    }
}

static char *normalize_js_numbers_for_json(const char *text) {
    size_t cap = strlen(text) * 2 + 1;
    size_t len = 0;
    char *out = malloc(cap);
    int in_string = 0;
    char string_char = 0;

    if (!out) {
        return NULL;
    }
    out[0] = '\0';

    for (const char *p = text; *p; p++) {
        char prev = p > text ? p[-1] : '\0';
        if (in_string) {
            if (append_str(&out, &len, &cap, p, 1) != 0) {
                free(out);
                return NULL;
            }
            if (*p == '\\' && p[1]) {
                if (append_str(&out, &len, &cap, p + 1, 1) != 0) {
                    free(out);
                    return NULL;
                }
                p++;
            } else if (*p == string_char) {
                in_string = 0;
            }
            continue;
        }

        if (*p == '"' || *p == '\'') {
            in_string = 1;
            string_char = *p;
        }

        if (*p == '.' && isdigit((unsigned char)p[1]) &&
            (p == text || prev == ':' || prev == ',' || prev == '[' || prev == ' ' ||
             prev == '\n' || prev == '\r' || prev == '\t')) {
            if (append_str(&out, &len, &cap, "0", 1) != 0) {
                free(out);
                return NULL;
            }
        }

        if (append_str(&out, &len, &cap, p, 1) != 0) {
            free(out);
            return NULL;
        }
    }

    return out;
}

cJSON *api_extract_nuxt(const char *html, size_t html_len) {
    const char *marker = "window.__NUXT__=";
    const char *start = strstr(html, marker);
    const char *end = html + html_len;
    const char *fn_start;
    const char *params_start;
    const char *params_end;
    const char *ret;
    const char *obj_start;
    const char *obj_end;
    const char *call_start;
    const char *call_end;
    char **params = NULL;
    char **values = NULL;
    int param_count = 0;
    char *object_str = NULL;
    char *json_str = NULL;
    cJSON *root = NULL;

    if (!start) {
        fprintf(stderr, "NUXT state not found in HTML\n");
        return NULL;
    }

    fn_start = strstr(start, "(function(");
    if (!fn_start) {
        fprintf(stderr, "NUXT wrapper function not found\n");
        return NULL;
    }

    params_start = fn_start + strlen("(function(");
    params_end = strchr(params_start, ')');
    if (!params_end) {
        fprintf(stderr, "NUXT parameter list is incomplete\n");
        return NULL;
    }

    if (params_end > params_start) {
        char *params_copy = dup_range(params_start, params_end);
        char *cursor = params_copy;
        char *token;
        while ((token = strsep(&cursor, ",")) != NULL) {
            while (*token && isspace((unsigned char)*token)) {
                token++;
            }
            if (*token) {
                char **new_params = realloc(params, sizeof(char *) * (param_count + 1));
                if (!new_params) {
                    free(params_copy);
                    goto cleanup;
                }
                params = new_params;
                params[param_count] = strdup(token);
                if (!params[param_count]) {
                    free(params_copy);
                    goto cleanup;
                }
                param_count++;
            }
        }
        free(params_copy);
    }

    ret = strstr(params_end, "return ");
    if (!ret) {
        fprintf(stderr, "return statement not found in NUXT\n");
        goto cleanup;
    }
    ret += 7;

    obj_start = skip_ws(ret, end);
    if (obj_start >= end || *obj_start != '{') {
        fprintf(stderr, "Expected object after return\n");
        goto cleanup;
    }
    obj_end = find_matching(obj_start, end, '{', '}');
    if (!obj_end) {
        fprintf(stderr, "Unbalanced braces in NUXT object\n");
        goto cleanup;
    }

    call_start = skip_ws(obj_end + 1, end);
    while (call_start < end && *call_start == '}') {
        call_start = skip_ws(call_start + 1, end);
    }
    if (call_start >= end || *call_start != '(') {
        fprintf(stderr, "NUXT invocation arguments not found\n");
        goto cleanup;
    }
    call_end = find_matching(call_start, end, '(', ')');
    if (!call_end) {
        fprintf(stderr, "Unbalanced NUXT invocation arguments\n");
        goto cleanup;
    }

    object_str = dup_range(obj_start, obj_end + 1);
    if (!object_str) {
        goto cleanup;
    }

    if (param_count > 0) {
        const char *cursor = call_start + 1;
        values = calloc((size_t)param_count, sizeof(char *));
        if (!values) {
            goto cleanup;
        }
        for (int i = 0; i < param_count; i++) {
            cursor = skip_ws(cursor, call_end);
            if (cursor >= call_end || parse_js_literal(&cursor, call_end, &values[i]) != 0) {
                fprintf(stderr, "Failed to parse NUXT invocation argument %d\n", i);
                goto cleanup;
            }
            cursor = skip_ws(cursor, call_end);
            if (cursor < call_end && *cursor == ',') {
                cursor++;
            }
        }
        json_str = substitute_params(object_str, params, values, param_count);
    } else {
        json_str = strdup(object_str);
    }

    if (!json_str) {
        goto cleanup;
    }

    {
        char *quoted = quote_object_keys(json_str);
        if (!quoted) {
            goto cleanup;
        }
        free(json_str);
        json_str = quoted;
    }
    replace_literal(json_str, "void 0", "null");
    replace_literal(json_str, "!0", "true");
    replace_literal(json_str, "!1", "false");

    if (!json_str) {
        goto cleanup;
    }

    {
        char *normalized = normalize_js_numbers_for_json(json_str);
        if (!normalized) {
            goto cleanup;
        }
        free(json_str);
        json_str = normalized;
    }

    root = cJSON_Parse(json_str);
    if (!root) {
        const char *err = cJSON_GetErrorPtr();
        fprintf(stderr, "JSON parse error near: %.40s\n", err ? err : "unknown");
    }

cleanup:
    if (params) {
        for (int i = 0; i < param_count; i++) {
            free(params[i]);
        }
    }
    if (values) {
        for (int i = 0; i < param_count; i++) {
            free(values[i]);
        }
    }
    free(params);
    free(values);
    free(object_str);
    free(json_str);
    return root;
}

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
