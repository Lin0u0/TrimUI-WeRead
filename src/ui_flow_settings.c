/*
 * ui_flow_settings.c - Shared settings-flow boundary for shelf and reader callers
 */
#include "ui_internal.h"

#if HAVE_SDL

static const UiSettingsItemSpec UI_SETTINGS_ITEMS[UI_SETTINGS_ITEM_COUNT] = {
    { UI_SETTINGS_ITEM_READER_FONT_SIZE, "Font Size" },
    { UI_SETTINGS_ITEM_DARK_MODE, "Dark Mode" },
    { UI_SETTINGS_ITEM_BRIGHTNESS, "Brightness" },
    { UI_SETTINGS_ITEM_ROTATION, "Rotation" },
    { UI_SETTINGS_ITEM_LOGOUT, "Logout" }
};

void ui_settings_flow_state_reset(SettingsFlowState *state) {
    if (!state) {
        return;
    }

    state->open = 0;
    state->quick_open = 0;
    state->selected = 0;
    state->logout_confirm_armed = 0;
    state->shelf_selected = 0;
    state->origin = UI_SETTINGS_ORIGIN_NONE;
    state->return_view = VIEW_SHELF;
}

int ui_settings_flow_open(SettingsFlowState *state, UiView *view,
                          UiSettingsOrigin origin, int quick_open, int shelf_selected) {
    if (!state || !view) {
        return -1;
    }

    state->open = 1;
    state->quick_open = quick_open ? 1 : 0;
    state->selected = 0;
    state->origin = origin;
    state->shelf_selected = shelf_selected >= 0 ? shelf_selected : 0;
    switch (origin) {
        case UI_SETTINGS_ORIGIN_READER:
            state->return_view = VIEW_READER;
            break;
        case UI_SETTINGS_ORIGIN_SHELF:
        default:
            state->return_view = VIEW_SHELF;
            break;
    }

    *view = VIEW_SETTINGS;
    return 0;
}

int ui_settings_flow_open_from_shelf(SettingsFlowState *state, UiView *view,
                                     int quick_open, int shelf_selected) {
    return ui_settings_flow_open(state, view, UI_SETTINGS_ORIGIN_SHELF, quick_open,
                                 shelf_selected);
}

int ui_settings_flow_open_from_reader(SettingsFlowState *state, UiView *view, int quick_open) {
    return ui_settings_flow_open(state, view, UI_SETTINGS_ORIGIN_READER, quick_open, 0);
}

int ui_settings_flow_begin_close(SettingsFlowState *state) {
    if (!state || !state->open) {
        return -1;
    }

    state->open = 0;
    state->quick_open = 0;
    state->logout_confirm_armed = 0;
    return 0;
}

int ui_settings_flow_finish_close(SettingsFlowState *state, UiView *view, int *shelf_selected) {
    UiView return_view;
    int saved_shelf_selected;

    if (!state || !view) {
        return -1;
    }

    return_view = state->return_view;
    saved_shelf_selected = state->shelf_selected;
    ui_settings_flow_state_reset(state);
    if (shelf_selected && return_view == VIEW_SHELF) {
        *shelf_selected = saved_shelf_selected >= 0 ? saved_shelf_selected : 0;
    }
    *view = return_view;
    return 0;
}

const UiSettingsItemSpec *ui_settings_flow_items(int *count_out) {
    if (count_out) {
        *count_out = UI_SETTINGS_ITEM_COUNT;
    }
    return UI_SETTINGS_ITEMS;
}

#endif
