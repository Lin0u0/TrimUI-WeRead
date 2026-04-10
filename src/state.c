#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "state.h"
#include "json.h"

/*
 * Phase 4 maintainer note: state.c is the generic JSON persistence boundary.
 * It owns raw file I/O plus the protected STATE_FILE_* names, while typed
 * semantics stay in reader_state, shelf_state, and preferences_state.
 */

static char *state_path(ApiContext *ctx, const char *name) {
    char *path;
    size_t len;

    if (!ctx || !name) {
        return NULL;
    }
    len = strlen(ctx->state_dir) + strlen(name) + 2;
    path = malloc(len);
    if (!path) {
        return NULL;
    }
    snprintf(path, len, "%s/%s", ctx->state_dir, name);
    return path;
}

int state_write_json(ApiContext *ctx, const char *name, cJSON *json) {
    char *path = state_path(ctx, name);
    char *text;
    FILE *fp;
    int rc = -1;

    if (!path || !json) {
        free(path);
        return -1;
    }

    text = cJSON_PrintUnformatted(json);
    if (!text) {
        free(path);
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp) {
        size_t len = strlen(text);
        rc = fwrite(text, 1, len, fp) == len ? 0 : -1;
        fclose(fp);
    }

    free(text);
    free(path);
    return rc;
}

cJSON *state_read_json(ApiContext *ctx, const char *name) {
    char *path = state_path(ctx, name);
    FILE *fp;
    long size;
    char *buf;
    cJSON *json = NULL;

    if (!path) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        free(path);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        free(path);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        free(path);
        return NULL;
    }

    buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        free(path);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, fp) == (size_t)size) {
        buf[size] = '\0';
        json = cJSON_Parse(buf);
    }

    free(buf);
    fclose(fp);
    free(path);
    return json;
}
