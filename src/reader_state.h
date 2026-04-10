#ifndef READER_STATE_H
#define READER_STATE_H

#include "api.h"

/*
 * Reader-local persisted state lives behind this boundary. The concrete file
 * names remain defined in state.h so the on-disk runtime contract stays stable.
 */
int reader_state_save_last_reader(ApiContext *ctx, const char *target, int font_size,
                                  int content_font_size);
int reader_state_load_last_reader(ApiContext *ctx, char *target, size_t target_size,
                                  int *font_size, int *content_font_size);
int reader_state_save_position(ApiContext *ctx, const char *book_id,
                               const char *source_target, const char *target,
                               int font_size, int content_font_size,
                               int current_page, int current_offset);
int reader_state_load_position(ApiContext *ctx, const char *book_id,
                               const char *source_target, char *target,
                               size_t target_size, int *font_size,
                               int *content_font_size, int *current_page,
                               int *current_offset);
int reader_state_load_position_by_book_id(ApiContext *ctx, const char *book_id,
                                          char *source_target,
                                          size_t source_target_size,
                                          char *target, size_t target_size,
                                          int *font_size,
                                          int *content_font_size,
                                          int *current_page,
                                          int *current_offset);

#endif
