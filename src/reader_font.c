#include <stdio.h>
#include <sys/stat.h>
#include "reader_internal.h"

#define WEREAD_RANDOM_FONT_URL "https://cdn.weread.qq.com/app/assets/test-font/fzys_reversed.ttf"

static int ensure_random_font(ApiContext *ctx, char *path, size_t path_size) {
    struct stat st;
    Buffer buf = {0};
    FILE *fp;

    if (reader_join_path_checked(path, path_size, ctx->data_dir,
                                 "fzys_reversed.ttf") != 0) {
        return -1;
    }
    if (stat(path, &st) == 0 && st.st_size > 0) {
        return 0;
    }

    if (api_download(ctx, WEREAD_RANDOM_FONT_URL, &buf) != 0) {
        return -1;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        api_buffer_free(&buf);
        return -1;
    }
    fwrite(buf.data, 1, buf.size, fp);
    fclose(fp);
    api_buffer_free(&buf);
    return 0;
}

int reader_warmup_font(ApiContext *ctx) {
    char path[512];

    if (!ctx) {
        return -1;
    }
    return ensure_random_font(ctx, path, sizeof(path));
}

int reader_cached_random_font_path(ApiContext *ctx, char *path, size_t path_size) {
    struct stat st;

    if (!ctx || !path || path_size == 0) {
        return -1;
    }
    if (reader_join_path_checked(path, path_size, ctx->data_dir,
                                 "fzys_reversed.ttf") != 0) {
        return -1;
    }
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        path[0] = '\0';
        return -1;
    }
    return 0;
}
