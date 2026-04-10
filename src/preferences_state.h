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

#endif
