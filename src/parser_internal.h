/*
 * parser_internal.h - Internal shared parser primitives
 *
 * Owns low-level string/JS slice helpers reused across parser modules.
 * Reader-domain extraction entrypoints stay declared in reader_internal.h.
 */
#ifndef PARSER_INTERNAL_H
#define PARSER_INTERNAL_H

/* Skip ASCII whitespace within a bounded slice. */
const char *parser_skip_ws(const char *p, const char *end);

/* Find the matching closer while respecting nested pairs and quoted strings. */
const char *parser_find_matching_pair(const char *start, const char *end,
                                      char open_ch, char close_ch);

/* Duplicate a bounded slice as a NUL-terminated string. */
char *parser_dup_range(const char *start, const char *end);

#endif /* PARSER_INTERNAL_H */
