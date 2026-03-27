#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "html_strip.h"

static int append_char(char **out, size_t *len, size_t *cap, char ch) {
    char *tmp;
    if (*len + 2 > *cap) {
        *cap *= 2;
        tmp = realloc(*out, *cap);
        if (!tmp) {
            return -1;
        }
        *out = tmp;
    }
    (*out)[(*len)++] = ch;
    (*out)[*len] = '\0';
    return 0;
}

static int append_text(char **out, size_t *len, size_t *cap, const char *text) {
    while (*text) {
        if (append_char(out, len, cap, *text++) != 0) {
            return -1;
        }
    }
    return 0;
}

static int is_block_tag(const char *tag) {
    return strstr(tag, "p") == tag ||
           strstr(tag, "/p") == tag ||
           strstr(tag, "div") == tag ||
           strstr(tag, "/div") == tag ||
           strstr(tag, "section") == tag ||
           strstr(tag, "/section") == tag ||
           strstr(tag, "br") == tag ||
           strstr(tag, "/li") == tag ||
           strstr(tag, "li") == tag ||
           strstr(tag, "/h") == tag ||
           strstr(tag, "h1") == tag ||
           strstr(tag, "h2") == tag ||
           strstr(tag, "h3") == tag;
}

char *html_strip_to_text(const char *html) {
    char *out;
    size_t len = 0;
    size_t cap = 4096;
    int in_tag = 0;
    char tag[64];
    size_t tag_len = 0;

    if (!html) {
        return NULL;
    }

    out = malloc(cap);
    if (!out) {
        return NULL;
    }
    out[0] = '\0';

    for (; *html; html++) {
        if (in_tag) {
            if (*html == '>') {
                tag[tag_len] = '\0';
                if (is_block_tag(tag) && len > 0 && out[len - 1] != '\n') {
                    if (append_char(&out, &len, &cap, '\n') != 0) {
                        free(out);
                        return NULL;
                    }
                }
                in_tag = 0;
                tag_len = 0;
            } else if (tag_len + 1 < sizeof(tag)) {
                tag[tag_len++] = (char)tolower((unsigned char)*html);
            }
            continue;
        }

        if (*html == '<') {
            in_tag = 1;
            tag_len = 0;
            continue;
        }

        if (*html == '&') {
            if (strncmp(html, "&nbsp;", 6) == 0) {
                if (append_char(&out, &len, &cap, ' ') != 0) {
                    free(out);
                    return NULL;
                }
                html += 5;
                continue;
            }
            if (strncmp(html, "&lt;", 4) == 0) {
                if (append_char(&out, &len, &cap, '<') != 0) {
                    free(out);
                    return NULL;
                }
                html += 3;
                continue;
            }
            if (strncmp(html, "&gt;", 4) == 0) {
                if (append_char(&out, &len, &cap, '>') != 0) {
                    free(out);
                    return NULL;
                }
                html += 3;
                continue;
            }
            if (strncmp(html, "&amp;", 5) == 0) {
                if (append_char(&out, &len, &cap, '&') != 0) {
                    free(out);
                    return NULL;
                }
                html += 4;
                continue;
            }
            if (strncmp(html, "&quot;", 6) == 0) {
                if (append_char(&out, &len, &cap, '"') != 0) {
                    free(out);
                    return NULL;
                }
                html += 5;
                continue;
            }
            if (strncmp(html, "&#39;", 5) == 0) {
                if (append_char(&out, &len, &cap, '\'') != 0) {
                    free(out);
                    return NULL;
                }
                html += 4;
                continue;
            }
        }

        if (*html == '\r') {
            continue;
        }

        if (*html == '\n') {
            if (len == 0 || out[len - 1] == '\n') {
                continue;
            }
            if (append_char(&out, &len, &cap, '\n') != 0) {
                free(out);
                return NULL;
            }
            continue;
        }

        if (append_char(&out, &len, &cap, *html) != 0) {
            free(out);
            return NULL;
        }
    }

    while (len > 0 && isspace((unsigned char)out[len - 1])) {
        out[--len] = '\0';
    }

    if (append_text(&out, &len, &cap, "\n") != 0) {
        free(out);
        return NULL;
    }

    return out;
}
