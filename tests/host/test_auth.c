#include "test_support.h"

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

extern int save_base64_png(const char *data_url, const char *path);

static void assert_qr_png_write_failure_propagates(void) {
    ApiContext ctx;
    char temp_dir[PATH_MAX];
    char qr_path[PATH_MAX];
    struct rlimit old_limit;
    struct rlimit limited;
    void (*old_sigxfsz)(int);
    int rc;

    HOST_TEST_ASSERT_INT_EQ(host_test_init_api_context(&ctx, temp_dir, sizeof(temp_dir)), 0);
    HOST_TEST_ASSERT(
        snprintf(qr_path, sizeof(qr_path), "%s/%s", temp_dir, "qr.png") < (int)sizeof(qr_path)
    );

    HOST_TEST_ASSERT_INT_EQ(getrlimit(RLIMIT_FSIZE, &old_limit), 0);
    limited = old_limit;
    limited.rlim_cur = 1;
    old_sigxfsz = signal(SIGXFSZ, SIG_IGN);
    HOST_TEST_ASSERT(old_sigxfsz != SIG_ERR);
    HOST_TEST_ASSERT_INT_EQ(setrlimit(RLIMIT_FSIZE, &limited), 0);

    rc = save_base64_png("data:image/png;base64,AAAA", qr_path);

    HOST_TEST_ASSERT_INT_EQ(setrlimit(RLIMIT_FSIZE, &old_limit), 0);
    HOST_TEST_ASSERT(signal(SIGXFSZ, old_sigxfsz) != SIG_ERR);
    HOST_TEST_ASSERT_INT_EQ(rc, -1);

    host_test_cleanup_api_context(&ctx, temp_dir);
}

int main(void) {
    assert_qr_png_write_failure_propagates();
    return 0;
}
