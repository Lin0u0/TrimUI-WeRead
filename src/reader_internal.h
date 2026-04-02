/*
 * reader_internal.h - Internal shared declarations for reader modules
 */
#ifndef READER_INTERNAL_H
#define READER_INTERNAL_H

#include "reader.h"
#include "api.h"

/* ====================== JS/HTML Parser Functions ====================== */

/* Skip whitespace characters */
const char *reader_skip_ws(const char *p, const char *end);

/* Find matching closing bracket/brace */
const char *reader_find_matching_pair(const char *start, const char *end, char open_ch, char close_ch);

/* Duplicate a range of characters */
char *reader_dup_range(const char *start, const char *end);

/* Resolve a NUXT alias to a string value */
char *reader_resolve_nuxt_alias_string(const char *html, const char *alias);

/* Resolve a NUXT alias to a literal value (including objects) */
char *reader_resolve_nuxt_alias_literal(const char *html, const char *alias);

/* Find a top-level field in a JSON-like object */
const char *reader_find_top_level_field(const char *obj_start, const char *obj_end,
                                        const char *field_name, const char **value_start);

/* Extract a container (object or array) from a slice */
char *reader_extract_container_from_slice(const char *block_start, const char *block_end,
                                          const char *marker, char open_ch, char close_ch);

/* Extract resolved value after a marker */
char *reader_extract_resolved_value_after_marker(const char *html, const char *block_start,
                                                 const char *block_end, const char *marker);

/* ====================== Catalog Functions ====================== */

/* Free catalog items array */
void reader_catalog_items_free(ReaderCatalogItem *items, int count);

/* Parse catalog from a page block */
int reader_parse_catalog_from_page_block(const char *html, const char *block_start,
                                         const char *block_end, ReaderCatalogItem **out_items,
                                         int *out_count);

/* ====================== Utility Functions ====================== */

/* Safe path joining */
int reader_join_path_checked(char *dst, size_t dst_size, const char *dir, const char *name);

/* Duplicate string or return NULL */
char *reader_dup_or_null(const char *s);

/* Choose best summary between page and document */
const char *reader_choose_summary(const ReaderDocument *doc, const char *page_summary);

/* Add chapter UID to JSON payload (handles string vs number) */
void reader_add_chapter_uid_field(cJSON *payload, const char *chapter_uid);

#endif /* READER_INTERNAL_H */
