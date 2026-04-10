#include "test_support.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void host_test_build_path(char *dst, size_t dst_size, const char *base, const char *name) {
    int written;

    if (!dst || dst_size == 0 || !base || !name) {
        host_test_fail(__FILE__, __LINE__, "path arguments", "invalid path arguments");
    }

    written = snprintf(dst, dst_size, "%s/%s", base, name);
    if (written < 0 || (size_t)written >= dst_size) {
        host_test_fail(__FILE__, __LINE__, "snprintf", "path truncated");
    }
}

static void host_test_vfail(const char *file, int line, const char *expr, const char *message) {
    fprintf(stderr, "host test failed at %s:%d: %s", file, line, expr ? expr : "(no expression)");
    if (message) {
        fprintf(stderr, " - %s", message);
    }
    fputc('\n', stderr);
    exit(1);
}

void host_test_fail(const char *file, int line, const char *expr, const char *message) {
    host_test_vfail(file, line, expr, message);
}

static int host_test_remove_tree(const char *path) {
    DIR *dir;
    struct dirent *entry;

    dir = opendir(path);
    if (!dir) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char child_path[PATH_MAX];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        host_test_build_path(child_path, sizeof(child_path), path, entry->d_name);
        if (lstat(child_path, &st) != 0) {
            closedir(dir);
            return -1;
        }
        if (S_ISDIR(st.st_mode)) {
            if (host_test_remove_tree(child_path) != 0) {
                closedir(dir);
                return -1;
            }
        } else if (unlink(child_path) != 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    return rmdir(path);
}

char *host_test_read_file(const char *path) {
    FILE *fp;
    char *buffer;
    long size;

    if (!path) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    buffer = calloc((size_t)size + 1, 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    if (size > 0 && fread(buffer, 1, (size_t)size, fp) != (size_t)size) {
        free(buffer);
        fclose(fp);
        return NULL;
    }

    fclose(fp);
    return buffer;
}

char *host_test_load_fixture(const char *relative_path) {
    char path[PATH_MAX];

    host_test_build_path(path, sizeof(path), "tests/fixtures", relative_path);
    return host_test_read_file(path);
}

int host_test_seed_state_file(ApiContext *ctx, const char *name, const char *contents) {
    char path[PATH_MAX];
    FILE *fp;
    size_t len;

    if (!ctx || !name || !contents) {
        return -1;
    }

    host_test_build_path(path, sizeof(path), ctx->state_dir, name);
    fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }

    len = strlen(contents);
    if (len > 0 && fwrite(contents, 1, len, fp) != len) {
        fclose(fp);
        return -1;
    }

    if (fclose(fp) != 0) {
        return -1;
    }
    return 0;
}

int host_test_init_api_context(ApiContext *ctx, char *temp_dir, size_t temp_dir_size) {
    char template_path[] = "/tmp/weread-host-XXXXXX";
    char *created_path;

    if (!ctx || !temp_dir || temp_dir_size == 0) {
        return -1;
    }

    created_path = mkdtemp(template_path);
    if (!created_path) {
        return -1;
    }
    if (snprintf(temp_dir, temp_dir_size, "%s", created_path) >= (int)temp_dir_size) {
        host_test_remove_tree(created_path);
        return -1;
    }

    if (api_init(ctx, temp_dir) != 0) {
        host_test_remove_tree(temp_dir);
        temp_dir[0] = '\0';
        return -1;
    }
    return 0;
}

void host_test_cleanup_api_context(ApiContext *ctx, const char *temp_dir) {
    if (ctx) {
        api_cleanup(ctx);
    }
    if (temp_dir && *temp_dir) {
        (void)host_test_remove_tree(temp_dir);
    }
}
