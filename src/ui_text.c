/*
 * ui_text.c - UTF-8 text handling and typesetting utilities
 *
 * Handles: UTF-8 encoding, CJK detection, character width caching, line wrapping
 */
#include "ui_internal.h"

#if HAVE_SDL

#include <ctype.h>
#include <string.h>

/* Character width cache for faster text measurement */
typedef struct {
    uint32_t codepoint;
    int width;
} CharWidthEntry;

#define CHAR_WIDTH_CACHE_SIZE 512
static CharWidthEntry g_char_width_cache[CHAR_WIDTH_CACHE_SIZE];
static int g_char_width_cache_count = 0;
static TTF_Font *g_char_width_cache_font = NULL;

int utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

uint32_t utf8_decode(const char *s, int *out_len) {
    unsigned char c = (unsigned char)s[0];
    int len = utf8_char_len(c);
    if (out_len) *out_len = len;
    if (len == 1) return c;
    if (len == 2) return ((c & 0x1F) << 6) | (s[1] & 0x3F);
    if (len == 3) return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    if (len == 4) return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    return c;
}

int is_cjk_codepoint(uint32_t cp) {
    return (cp >= 0x4E00 && cp <= 0x9FFF) ||   /* CJK Unified Ideographs */
           (cp >= 0x3400 && cp <= 0x4DBF) ||   /* CJK Extension A */
           (cp >= 0x20000 && cp <= 0x2A6DF) || /* CJK Extension B */
           (cp >= 0x2A700 && cp <= 0x2B73F) || /* CJK Extension C */
           (cp >= 0x2B740 && cp <= 0x2B81F) || /* CJK Extension D */
           (cp >= 0xF900 && cp <= 0xFAFF) ||   /* CJK Compatibility Ideographs */
           (cp >= 0x3100 && cp <= 0x312F) ||   /* Bopomofo */
           (cp >= 0x31A0 && cp <= 0x31BF) ||   /* Bopomofo Extended */
           (cp >= 0x3000 && cp <= 0x303F) ||   /* CJK Symbols and Punctuation */
           (cp >= 0xFF00 && cp <= 0xFFEF);     /* Halfwidth and Fullwidth Forms */
}

int is_cjk_char(const char *s) {
    uint32_t cp = utf8_decode(s, NULL);
    return is_cjk_codepoint(cp);
}

int is_latin_or_digit(const char *s) {
    unsigned char c = (unsigned char)*s;
    if ((c & 0x80) != 0) return 0;
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

void char_width_cache_reset(void) {
    g_char_width_cache_count = 0;
    g_char_width_cache_font = NULL;
}

static int char_width_cache_lookup(uint32_t cp, int *width) {
    for (int i = 0; i < g_char_width_cache_count; i++) {
        if (g_char_width_cache[i].codepoint == cp) {
            *width = g_char_width_cache[i].width;
            return 1;
        }
    }
    return 0;
}

static void char_width_cache_insert(uint32_t cp, int width) {
    if (g_char_width_cache_count >= CHAR_WIDTH_CACHE_SIZE) {
        /* Evict oldest entries by shifting */
        memmove(&g_char_width_cache[0], &g_char_width_cache[CHAR_WIDTH_CACHE_SIZE / 4],
                sizeof(CharWidthEntry) * (CHAR_WIDTH_CACHE_SIZE * 3 / 4));
        g_char_width_cache_count = CHAR_WIDTH_CACHE_SIZE * 3 / 4;
    }
    g_char_width_cache[g_char_width_cache_count].codepoint = cp;
    g_char_width_cache[g_char_width_cache_count].width = width;
    g_char_width_cache_count++;
}

int get_char_width_fast(TTF_Font *font, const char *s, int char_len) {
    uint32_t cp;
    int width = 0;
    char buf[8];

    if (!font || !s || char_len <= 0 || char_len > 4) {
        return 0;
    }

    /* Reset cache if font changed */
    if (font != g_char_width_cache_font) {
        char_width_cache_reset();
        g_char_width_cache_font = font;
    }

    cp = utf8_decode(s, NULL);
    if (char_width_cache_lookup(cp, &width)) {
        return width;
    }

    memcpy(buf, s, (size_t)char_len);
    buf[char_len] = '\0';
    TTF_SizeUTF8(font, buf, &width, NULL);
    char_width_cache_insert(cp, width);
    return width;
}

int is_forbidden_line_start_punct(const char *text) {
    static const char *tokens[] = {
        ",", ".", "!", "?", ":", ";", ")", "]", "}", "%",
        "\xE3\x80\x81",  /* 、 */
        "\xE3\x80\x82",  /* 。 */
        "\xEF\xBC\x8C",  /* ， */
        "\xEF\xBC\x8E",  /* ． */
        "\xEF\xBC\x81",  /* ！ */
        "\xEF\xBC\x9F",  /* ？ */
        "\xEF\xBC\x9A",  /* ： */
        "\xEF\xBC\x9B",  /* ； */
        "\xEF\xBC\x89",  /* ） */
        "\xE3\x80\x91",  /* 】 */
        "\xE3\x80\x8D",  /* 」 */
        "\xE3\x80\x8F",  /* 』 */
        "\xE2\x80\x99",  /* ' */
        "\xE2\x80\x9D",  /* " */
        "\xE3\x80\x8B",  /* 》 */
        "\xE3\x80\x89",  /* 〉 */
        "\xE3\x80\xBE",  /* 〾 */
        NULL
    };

    if (!text || !*text) {
        return 0;
    }
    for (int i = 0; tokens[i]; i++) {
        size_t len = strlen(tokens[i]);
        if (strncmp(text, tokens[i], len) == 0) {
            return 1;
        }
    }
    return 0;
}

const char *skip_line_start_spacing(const char *text, const char *end) {
    const char *p = text;

    while (p && p < end && *p) {
        int ch_len = utf8_char_len((unsigned char)*p);
        if (!isspace((unsigned char)*p) &&
            strncmp(p, "\xE3\x80\x80", 3) != 0) { /* full-width space */
            break;
        }
        p += ch_len;
    }
    return p;
}

#endif /* HAVE_SDL */
