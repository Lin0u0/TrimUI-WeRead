#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "html_strip.h"

static int utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static int utf8_is_continuation(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

static uint32_t utf8_decode(const char *s, int *out_len) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char c = p[0];
    int len = utf8_char_len(c);

    if (out_len) *out_len = len;

    if (len == 1) return c;
    if (len == 2) {
        if (!p[1] || !utf8_is_continuation(p[1])) {
            if (out_len) *out_len = 1;
            return c;
        }
        return ((c & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if (len == 3) {
        if (!p[1] || !p[2] || !utf8_is_continuation(p[1]) || !utf8_is_continuation(p[2])) {
            if (out_len) *out_len = 1;
            return c;
        }
        return ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    if (len == 4) {
        if (!p[1] || !p[2] || !p[3] ||
            !utf8_is_continuation(p[1]) || !utf8_is_continuation(p[2]) ||
            !utf8_is_continuation(p[3])) {
            if (out_len) *out_len = 1;
            return c;
        }
        return ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    }
    return c;
}

static int is_cjk_codepoint(uint32_t cp) {
    return (cp >= 0x4E00 && cp <= 0x9FFF) ||   /* CJK Unified Ideographs */
           (cp >= 0x3400 && cp <= 0x4DBF) ||   /* CJK Extension A */
           (cp >= 0x20000 && cp <= 0x2A6DF) || /* CJK Extension B */
           (cp >= 0x2A700 && cp <= 0x2B73F) || /* CJK Extension C */
           (cp >= 0x2B740 && cp <= 0x2B81F) || /* CJK Extension D */
           (cp >= 0xF900 && cp <= 0xFAFF) ||   /* CJK Compatibility Ideographs */
           (cp >= 0x3100 && cp <= 0x312F) ||   /* Bopomofo */
           (cp >= 0x31A0 && cp <= 0x31BF);     /* Bopomofo Extended */
}

static int is_latin_codepoint(uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') ||
           (cp >= 'a' && cp <= 'z') ||
           (cp >= '0' && cp <= '9');
}

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

static int append_utf8(char **out, size_t *len, size_t *cap, const char *src, int char_len) {
    char *tmp;
    size_t needed = *len + (size_t)char_len + 1;
    if (needed > *cap) {
        while (*cap < needed) {
            *cap *= 2;
        }
        tmp = realloc(*out, *cap);
        if (!tmp) {
            return -1;
        }
        *out = tmp;
    }
    memcpy(*out + *len, src, (size_t)char_len);
    *len += (size_t)char_len;
    (*out)[*len] = '\0';
    return 0;
}

static int append_text(char **out, size_t *len, size_t *cap, const char *text) {
    size_t text_len = strlen(text);
    size_t needed = *len + text_len + 1;
    char *tmp;
    if (needed > *cap) {
        while (*cap < needed) {
            *cap *= 2;
        }
        tmp = realloc(*out, *cap);
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

static int tag_match(const char *tag, const char *name) {
    size_t len = strlen(name);
    return strncmp(tag, name, len) == 0 &&
           (tag[len] == '\0' || tag[len] == ' ' || tag[len] == '/');
}

static int is_block_tag(const char *tag) {
    return tag_match(tag, "p") ||
           tag_match(tag, "/p") ||
           tag_match(tag, "div") ||
           tag_match(tag, "/div") ||
           tag_match(tag, "section") ||
           tag_match(tag, "/section") ||
           tag_match(tag, "figure") ||
           tag_match(tag, "/figure") ||
           tag_match(tag, "img") ||
           tag_match(tag, "br") ||
           tag_match(tag, "/li") ||
           tag_match(tag, "li") ||
           tag_match(tag, "/h") ||
           tag_match(tag, "h1") ||
           tag_match(tag, "h2") ||
           tag_match(tag, "h3");
}

/* 段首缩进：一个全角空格 U+3000 */
static int append_paragraph_indent(char **out, size_t *len, size_t *cap) {
    /* U+3000 = E3 80 80 in UTF-8 */
    static const char indent[] = "\xE3\x80\x80";
    return append_text(out, len, cap, indent);
}

char *html_strip_to_text(const char *html) {
    char *out;
    size_t len = 0;
    size_t cap = 4096;
    int in_tag = 0;
    char tag[64];
    size_t tag_len = 0;
    uint32_t last_cp = 0;  /* 上一个字符的 codepoint，用于 CJK/Latin 边界检测 */
    int need_indent = 1;   /* 段首需要缩进 */

    if (!html) {
        return NULL;
    }

    out = malloc(cap);
    if (!out) {
        return NULL;
    }
    out[0] = '\0';

    for (; *html; ) {
        int ch_len;
        uint32_t cp;

        if (in_tag) {
            if (*html == '>') {
                tag[tag_len] = '\0';
                if (is_block_tag(tag) && len > 0 && out[len - 1] != '\n') {
                    if (append_char(&out, &len, &cap, '\n') != 0) {
                        free(out);
                        return NULL;
                    }
                    last_cp = '\n';
                    need_indent = 1;  /* 下一段需要缩进 */
                }
                in_tag = 0;
                tag_len = 0;
            } else if (tag_len + 1 < sizeof(tag)) {
                tag[tag_len++] = (char)tolower((unsigned char)*html);
            }
            html++;
            continue;
        }

        if (*html == '<') {
            in_tag = 1;
            tag_len = 0;
            html++;
            continue;
        }

        if (*html == '&') {
            if (strncmp(html, "&nbsp;", 6) == 0) {
                if (append_char(&out, &len, &cap, ' ') != 0) {
                    free(out);
                    return NULL;
                }
                last_cp = ' ';
                html += 6;
                continue;
            }
            if (strncmp(html, "&lt;", 4) == 0) {
                if (append_char(&out, &len, &cap, '<') != 0) {
                    free(out);
                    return NULL;
                }
                last_cp = '<';
                html += 4;
                continue;
            }
            if (strncmp(html, "&gt;", 4) == 0) {
                if (append_char(&out, &len, &cap, '>') != 0) {
                    free(out);
                    return NULL;
                }
                last_cp = '>';
                html += 4;
                continue;
            }
            if (strncmp(html, "&amp;", 5) == 0) {
                if (append_char(&out, &len, &cap, '&') != 0) {
                    free(out);
                    return NULL;
                }
                last_cp = '&';
                html += 5;
                continue;
            }
            if (strncmp(html, "&quot;", 6) == 0) {
                if (append_char(&out, &len, &cap, '"') != 0) {
                    free(out);
                    return NULL;
                }
                last_cp = '"';
                html += 6;
                continue;
            }
            if (strncmp(html, "&#39;", 5) == 0) {
                if (append_char(&out, &len, &cap, '\'') != 0) {
                    free(out);
                    return NULL;
                }
                last_cp = '\'';
                html += 5;
                continue;
            }
        }

        if (*html == '\r') {
            html++;
            continue;
        }

        if (*html == '\n') {
            if (len == 0 || out[len - 1] == '\n') {
                html++;
                continue;
            }
            if (append_char(&out, &len, &cap, '\n') != 0) {
                free(out);
                return NULL;
            }
            last_cp = '\n';
            need_indent = 1;  /* 下一段需要缩进 */
            html++;
            continue;
        }

        /* 解码当前 UTF-8 字符 */
        cp = utf8_decode(html, &ch_len);

        /* 跳过行首空白（缩进由我们统一添加） */
        if (need_indent && (cp == ' ' || cp == '\t' || cp == 0x3000)) {
            html += ch_len;
            continue;
        }

        /* 段首缩进：在非空白内容前添加两个全角空格 */
        if (need_indent) {
            if (append_paragraph_indent(&out, &len, &cap) != 0) {
                free(out);
                return NULL;
            }
            need_indent = 0;
            last_cp = 0x3000;  /* 全角空格 */
        }

        /* CJK/Latin 边界检测：在边界处插入空格 */
        if (last_cp != 0 && last_cp != ' ' && last_cp != '\n' && last_cp != 0x3000) {
            int last_is_cjk = is_cjk_codepoint(last_cp);
            int last_is_latin = is_latin_codepoint(last_cp);
            int curr_is_cjk = is_cjk_codepoint(cp);
            int curr_is_latin = is_latin_codepoint(cp);

            if ((last_is_cjk && curr_is_latin) || (last_is_latin && curr_is_cjk)) {
                if (append_char(&out, &len, &cap, ' ') != 0) {
                    free(out);
                    return NULL;
                }
            }
        }

        if (append_utf8(&out, &len, &cap, html, ch_len) != 0) {
            free(out);
            return NULL;
        }
        last_cp = cp;
        html += ch_len;
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
