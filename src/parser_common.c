/*
 * parser_common.c - Shared low-level parser helper implementations
 *
 * Owns generic string/JS slice helpers reused by js_parser.c and
 * reader_parser.c. Reader-domain extraction entrypoints stay in the
 * reader subsystem.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "parser_internal.h"

const char *parser_skip_ws(const char *p, const char *end) {
    while (p < end && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

const char *parser_find_matching_pair(const char *start, const char *end,
                                      char open_ch, char close_ch) {
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

char *parser_dup_range(const char *start, const char *end) {
    size_t len = (size_t)(end - start);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}
