#ifndef TESTS_HOST_TEST_SUPPORT_H
#define TESTS_HOST_TEST_SUPPORT_H

#include <stddef.h>
#include "api.h"

void host_test_fail(const char *file, int line, const char *expr, const char *message);

#define HOST_TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            host_test_fail(__FILE__, __LINE__, #expr, NULL); \
        } \
    } while (0)

#define HOST_TEST_ASSERT_MSG(expr, message) \
    do { \
        if (!(expr)) { \
            host_test_fail(__FILE__, __LINE__, #expr, (message)); \
        } \
    } while (0)

#define HOST_TEST_ASSERT_INT_EQ(actual, expected) \
    do { \
        int host_test_actual__ = (actual); \
        int host_test_expected__ = (expected); \
        if (host_test_actual__ != host_test_expected__) { \
            host_test_fail(__FILE__, __LINE__, #actual " == " #expected, NULL); \
        } \
    } while (0)

int host_test_init_api_context(ApiContext *ctx, char *temp_dir, size_t temp_dir_size);
void host_test_cleanup_api_context(ApiContext *ctx, const char *temp_dir);

char *host_test_load_fixture(const char *relative_path);
int host_test_seed_state_file(ApiContext *ctx, const char *name, const char *contents);
char *host_test_read_file(const char *path);

#endif
