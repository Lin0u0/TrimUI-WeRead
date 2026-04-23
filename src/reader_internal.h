/*
 * reader_internal.h - Internal shared declarations for reader modules
 *
 * Keeps reader-domain parser entrypoints and reader module collaboration
 * surfaces. Generic parser primitives live in parser_internal.h.
 */
#ifndef READER_INTERNAL_H
#define READER_INTERNAL_H

#include "reader.h"
#include "api.h"
#include "parser_internal.h"

/*
 * Reader modules still expose these helper names to reader-side collaborators
 * such as catalog parsing, but the generic implementations now live in the
 * shared parser subsystem.
 */
#define reader_skip_ws parser_skip_ws
#define reader_find_matching_pair parser_find_matching_pair
#define reader_dup_range parser_dup_range

/* ====================== Reader Parser Functions ====================== */

/* Resolve a NUXT alias to a string value */
char *reader_resolve_nuxt_alias_string(const char *html, const char *alias);

/* Resolve a NUXT alias to a literal value (including objects) */
char *reader_resolve_nuxt_alias_literal(const char *html, const char *alias);

/* Reader/article target helpers */
int reader_target_is_article(const char *target);
char *reader_extract_query_value(const char *target, const char *key);
int reader_fetch_page(ApiContext *ctx, const char *url, Buffer *buf);
char *reader_build_url(ApiContext *ctx, const char *target, int font_size);

/* Find a top-level field in a JSON-like object */
const char *reader_find_top_level_field(const char *obj_start, const char *obj_end,
                                        const char *field_name, const char **value_start);

/* Extract a container (object or array) from a slice */
char *reader_extract_container_from_slice(const char *block_start, const char *block_end,
                                          const char *marker, char open_ch, char close_ch);

/* Extract resolved value after a marker */
char *reader_extract_resolved_value_after_marker(const char *html, const char *block_start,
                                                 const char *block_end, const char *marker);

/* Extract resolved value after a plain marker in the full HTML buffer */
char *reader_extract_resolved_value_after_global_marker(const char *html, const char *marker);

/* Reader-side slice/object extraction helpers */
int reader_find_named_object_block(const char *html, const char *marker,
                                   const char **block_start, const char **block_end);
char *reader_extract_resolved_from_slice(const char *html, const char *slice_start,
                                         const char *slice_end, const char *field_marker);
char *reader_extract_resolved_value_in_object_block(const char *html,
                                                    const char *block_marker,
                                                    const char *field_marker);
int reader_extract_int_in_object_block(const char *html, const char *block_marker,
                                       const char *field_marker, int fallback);
int reader_extract_int_from_slice_resolved(const char *html, const char *slice_start,
                                           const char *slice_end,
                                           const char *field_marker, int fallback);
int reader_parse_bool_from_slice_resolved(const char *html, const char *slice_start,
                                          const char *slice_end,
                                          const char *field_marker, int fallback);
char *reader_extract_attr_text(const char *html, const char *marker);
int reader_extract_max_wco(const char *html);
int reader_extract_wco_values(const char *html, int **values_out, int *count_out);

/* Article document helpers */
int reader_extract_article_review_id_from_reader_shell(const char *html,
                                                       char **review_id_out);
int reader_load_article_from_review_id(ApiContext *ctx, const char *review_id,
                                       int font_size, ReaderDocument *doc);
int reader_parse_article_document(ApiContext *ctx, const char *target, int font_size,
                                  const char *html, ReaderDocument *doc);
int reader_load_book_document(ApiContext *ctx, const char *target, int font_size,
                              const char *html, ReaderDocument *doc);

/* ====================== Catalog Functions (catalog.c) ====================== */

/* Free catalog items array */
void reader_catalog_items_free(ReaderCatalogItem *items, int count);

/* Comparator for sorting catalog items by chapter_idx */
int catalog_item_cmp_chapter_idx(const void *a, const void *b);

/* Find index of chapter_uid in catalog items array */
int reader_catalog_find_index(ReaderCatalogItem *items, int count, const char *chapter_uid);

/* Parse catalogloadmore API JSON response */
int reader_parse_catalogloadmore_json(cJSON *json, const char *current_chapter_uid,
                                      ReaderCatalogItem **items_out, int *count_out,
                                      int *first_idx_out, int *last_idx_out);

/* Fetch a catalog chunk from API */
int reader_fetch_catalog_chunk(ApiContext *ctx, const char *book_id, int type,
                               int range_start, int range_end, const char *current_chapter_uid,
                               ReaderCatalogItem **items_out, int *count_out,
                               int *first_idx_out, int *last_idx_out);

/* Parse catalog from reader block in HTML */
int reader_parse_catalog(const char *html, const char *reader_block_start,
                         const char *reader_block_end, ReaderCatalogItem **items_out,
                         int *count_out);

/* Hydrate full catalog from API */
int reader_hydrate_full_catalog(ApiContext *ctx, ReaderDocument *doc);

/* Focus catalog window around current chapter */
int reader_focus_catalog(ApiContext *ctx, ReaderDocument *doc);

/* Merge one catalog item into an existing catalog array */
int reader_catalog_merge_item(ReaderCatalogItem **items_inout, int *count_inout, int *cap_inout,
                              ReaderCatalogItem *item);

/* ====================== Utility Functions ====================== */

/* Safe path joining */
int reader_join_path_checked(char *dst, size_t dst_size, const char *dir, const char *name);

/* Duplicate string or return NULL */
char *reader_dup_or_null(const char *s);

/* Choose best summary between page and document */
const char *reader_choose_summary(const ReaderDocument *doc, const char *page_summary);

/* Add chapter UID to JSON payload (handles string vs number) */
void reader_add_chapter_uid_field(cJSON *payload, const char *chapter_uid);

/* Reader font cache helper for internal callers */
int reader_cached_random_font_path(ApiContext *ctx, char *path, size_t path_size);

#endif /* READER_INTERNAL_H */
