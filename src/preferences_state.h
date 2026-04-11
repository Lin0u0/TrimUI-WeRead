#ifndef PREFERENCES_STATE_H
#define PREFERENCES_STATE_H

#include "api.h"

/*
 * UI preference persistence lives here so dark-mode and brightness semantics do
 * not leak through the generic persistence layer.
 */
int preferences_state_save_dark_mode(ApiContext *ctx, int dark_mode);
int preferences_state_load_dark_mode(ApiContext *ctx);
int preferences_state_save_brightness_level(ApiContext *ctx, int brightness_level);
int preferences_state_load_brightness_level(ApiContext *ctx, int *brightness_level);
int preferences_state_save_reader_font_size(ApiContext *ctx, int reader_font_size);
int preferences_state_load_reader_font_size(ApiContext *ctx, int *reader_font_size);
int preferences_state_save_rotation(ApiContext *ctx, int rotation);
int preferences_state_load_rotation(ApiContext *ctx, int *rotation);

#endif
