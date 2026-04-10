#ifndef STATE_H
#define STATE_H

#include "api.h"

/*
 * Protected runtime-contract filenames for persisted state.
 * Phase 1 may clarify code ownership around these names, but must keep the
 * concrete on-disk values stable for existing installs and launchers.
 */
#define STATE_FILE_SHELF "shelf.json"
#define STATE_FILE_LAST_READER "last-reader.json"
#define STATE_FILE_READER_POSITIONS "reader-positions.json"
#define STATE_FILE_PREFERENCES "preferences.json"

int state_write_json(ApiContext *ctx, const char *name, cJSON *json);
cJSON *state_read_json(ApiContext *ctx, const char *name);

#endif
