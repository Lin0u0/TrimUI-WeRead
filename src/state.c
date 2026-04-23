#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

static int fsync_parent_dir(const char *path) {
    const char *slash;
    char *dir_path;
    int dir_fd;
    int rc = -1;

    if (!path) {
        return -1;
    }

    slash = strrchr(path, '/');
    if (!slash) {
        return -1;
    }

    dir_path = strndup(path, (size_t)(slash - path));
    if (!dir_path) {
        return -1;
    }

    dir_fd = open(dir_path, O_RDONLY);
    free(dir_path);
    if (dir_fd < 0) {
        return -1;
    }

    if (fsync(dir_fd) == 0) {
        rc = 0;
    }
    close(dir_fd);
    return rc;
}

static int write_text_atomic(const char *path, const char *text) {
    char *tmp_path;
    size_t tmp_len;
    size_t text_len;
    FILE *fp = NULL;
    int fd = -1;
    int rc = -1;

    if (!path || !text) {
        return -1;
    }

    text_len = strlen(text);
    tmp_len = strlen(path) + sizeof(".XXXXXX");
    tmp_path = malloc(tmp_len);
    if (!tmp_path) {
        return -1;
    }
    snprintf(tmp_path, tmp_len, "%s.XXXXXX", path);

    fd = mkstemp(tmp_path);
    if (fd < 0) {
        free(tmp_path);
        return -1;
    }

    fp = fdopen(fd, "wb");
    if (!fp) {
        close(fd);
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }
    fd = -1;

    if (fwrite(text, 1, text_len, fp) != text_len) {
        goto cleanup;
    }
    if (fflush(fp) != 0) {
        goto cleanup;
    }
    if (fsync(fileno(fp)) != 0) {
        goto cleanup;
    }
    if (fclose(fp) != 0) {
        fp = NULL;
        goto cleanup;
    }
    fp = NULL;

    if (rename(tmp_path, path) != 0) {
        goto cleanup;
    }
    if (fsync_parent_dir(path) != 0) {
        goto cleanup;
    }

    rc = 0;

cleanup:
    if (fp) {
        fclose(fp);
    }
    if (fd >= 0) {
        close(fd);
    }
    if (rc != 0) {
        unlink(tmp_path);
    }
    free(tmp_path);
    return rc;
}

int state_write_json(ApiContext *ctx, const char *name, cJSON *json) {
    char *path = state_path(ctx, name);
    char *text;
    int rc;

    if (!path || !json) {
        free(path);
        return -1;
    }

    text = cJSON_PrintUnformatted(json);
    if (!text) {
        free(path);
        return -1;
    }

    rc = write_text_atomic(path, text);
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
