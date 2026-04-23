#include "test_support.h"

#include <stdlib.h>
#include <string.h>

#include "html_strip.h"

static void assert_html_strip_handles_truncated_utf8(void) {
    const char *input = "A\xE4\xB8";
    char *text = html_strip_to_text(input);

    HOST_TEST_ASSERT(text != NULL);
    HOST_TEST_ASSERT(strcmp(text, "\xE3\x80\x80" "A" "\xE4" "\xB8" "\n") == 0);
    free(text);
}

int main(void) {
    assert_html_strip_handles_truncated_utf8();
    return 0;
}
