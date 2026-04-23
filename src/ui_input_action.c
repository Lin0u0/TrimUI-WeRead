#include "ui_input_action.h"

UiInputCommand ui_input_command_for_mask(UiInputMask mask) {
    if (mask == 0) {
        return UI_INPUT_COMMAND_NONE;
    }
    if ((mask & UI_INPUT_MASK_LOCK_BUTTON) != 0) {
        return UI_INPUT_COMMAND_LOCK;
    }
    if ((mask & UI_INPUT_MASK_SETTINGS_OPEN) != 0) {
        return UI_INPUT_COMMAND_SETTINGS;
    }
    if ((mask & UI_INPUT_BIT(KEY_R)) != 0) {
        return UI_INPUT_COMMAND_RESUME;
    }
    if ((mask & UI_INPUT_BIT(KEY_C)) != 0) {
        return UI_INPUT_COMMAND_CATALOG;
    }
    if ((mask & UI_INPUT_BIT(JOY_X)) != 0) {
        return UI_INPUT_COMMAND_AUX;
    }
    if ((mask & UI_INPUT_MASK_BACK) != 0) {
        return UI_INPUT_COMMAND_BACK;
    }
    if ((mask & UI_INPUT_MASK_CONFIRM) != 0) {
        return UI_INPUT_COMMAND_CONFIRM;
    }
    if ((mask & UI_INPUT_BIT(JOY_L1)) != 0) {
        return UI_INPUT_COMMAND_PAGE_PREV;
    }
    if ((mask & UI_INPUT_BIT(JOY_R1)) != 0) {
        return UI_INPUT_COMMAND_PAGE_NEXT;
    }
    if ((mask & UI_INPUT_BIT(KEY_PAGEUP)) != 0) {
        return UI_INPUT_COMMAND_PAGE_UP;
    }
    if ((mask & UI_INPUT_BIT(KEY_PAGEDOWN)) != 0) {
        return UI_INPUT_COMMAND_PAGE_DOWN;
    }
    if ((mask & UI_INPUT_MASK_UP) != 0) {
        return UI_INPUT_COMMAND_UP;
    }
    if ((mask & UI_INPUT_MASK_DOWN) != 0) {
        return UI_INPUT_COMMAND_DOWN;
    }
    if ((mask & UI_INPUT_MASK_LEFT) != 0) {
        return UI_INPUT_COMMAND_LEFT;
    }
    if ((mask & UI_INPUT_MASK_RIGHT) != 0) {
        return UI_INPUT_COMMAND_RIGHT;
    }
    return UI_INPUT_COMMAND_NONE;
}

UiInputAction ui_input_action_for_command(UiInputScope scope, UiInputCommand command) {
    if (command == UI_INPUT_COMMAND_NONE) {
        return UI_INPUT_ACTION_NONE;
    }
    if (command == UI_INPUT_COMMAND_LOCK) {
        return UI_INPUT_ACTION_GLOBAL_LOCK;
    }

    switch (scope) {
    case UI_INPUT_SCOPE_SHELF:
        switch (command) {
        case UI_INPUT_COMMAND_BACK:
            return UI_INPUT_ACTION_GLOBAL_BACK;
        case UI_INPUT_COMMAND_SETTINGS:
            return UI_INPUT_ACTION_SHELF_SETTINGS_OPEN;
        case UI_INPUT_COMMAND_DOWN:
        case UI_INPUT_COMMAND_RIGHT:
            return UI_INPUT_ACTION_SHELF_NEXT;
        case UI_INPUT_COMMAND_UP:
        case UI_INPUT_COMMAND_LEFT:
            return UI_INPUT_ACTION_SHELF_PREV;
        case UI_INPUT_COMMAND_RESUME:
        case UI_INPUT_COMMAND_AUX:
            return UI_INPUT_ACTION_SHELF_RESUME;
        case UI_INPUT_COMMAND_CONFIRM:
            return UI_INPUT_ACTION_SHELF_OPEN_SELECTED;
        default:
            return UI_INPUT_ACTION_NONE;
        }
    case UI_INPUT_SCOPE_LOGIN:
        switch (command) {
        case UI_INPUT_COMMAND_BACK:
            return UI_INPUT_ACTION_GLOBAL_BACK;
        case UI_INPUT_COMMAND_CONFIRM:
            return UI_INPUT_ACTION_LOGIN_CONFIRM;
        default:
            return UI_INPUT_ACTION_NONE;
        }
    case UI_INPUT_SCOPE_BOOTSTRAP:
        switch (command) {
        case UI_INPUT_COMMAND_BACK:
            return UI_INPUT_ACTION_GLOBAL_BACK;
        case UI_INPUT_COMMAND_CONFIRM:
            return UI_INPUT_ACTION_BOOTSTRAP_RETRY;
        default:
            return UI_INPUT_ACTION_NONE;
        }
    case UI_INPUT_SCOPE_OPENING:
        switch (command) {
        case UI_INPUT_COMMAND_BACK:
            return UI_INPUT_ACTION_GLOBAL_BACK;
        case UI_INPUT_COMMAND_CONFIRM:
            return UI_INPUT_ACTION_OPENING_RETRY;
        default:
            return UI_INPUT_ACTION_NONE;
        }
    case UI_INPUT_SCOPE_SETTINGS:
        switch (command) {
        case UI_INPUT_COMMAND_BACK:
        case UI_INPUT_COMMAND_SETTINGS:
            return UI_INPUT_ACTION_SETTINGS_CLOSE;
        case UI_INPUT_COMMAND_UP:
            return UI_INPUT_ACTION_SETTINGS_PREV;
        case UI_INPUT_COMMAND_DOWN:
            return UI_INPUT_ACTION_SETTINGS_NEXT;
        case UI_INPUT_COMMAND_LEFT:
            return UI_INPUT_ACTION_SETTINGS_ADJUST_PREV;
        case UI_INPUT_COMMAND_RIGHT:
            return UI_INPUT_ACTION_SETTINGS_ADJUST_NEXT;
        case UI_INPUT_COMMAND_CONFIRM:
            return UI_INPUT_ACTION_SETTINGS_CONFIRM;
        default:
            return UI_INPUT_ACTION_NONE;
        }
    case UI_INPUT_SCOPE_READER_CATALOG:
        switch (command) {
        case UI_INPUT_COMMAND_BACK:
            return UI_INPUT_ACTION_GLOBAL_BACK;
        case UI_INPUT_COMMAND_CATALOG:
        case UI_INPUT_COMMAND_AUX:
            return UI_INPUT_ACTION_READER_CATALOG_CLOSE;
        case UI_INPUT_COMMAND_UP:
            return UI_INPUT_ACTION_READER_CATALOG_UP;
        case UI_INPUT_COMMAND_DOWN:
            return UI_INPUT_ACTION_READER_CATALOG_DOWN;
        case UI_INPUT_COMMAND_LEFT:
            return UI_INPUT_ACTION_READER_CATALOG_PAGE_PREV;
        case UI_INPUT_COMMAND_RIGHT:
            return UI_INPUT_ACTION_READER_CATALOG_PAGE_NEXT;
        case UI_INPUT_COMMAND_CONFIRM:
            return UI_INPUT_ACTION_READER_CATALOG_CONFIRM;
        default:
            return UI_INPUT_ACTION_NONE;
        }
    case UI_INPUT_SCOPE_READER:
        switch (command) {
        case UI_INPUT_COMMAND_BACK:
            return UI_INPUT_ACTION_GLOBAL_BACK;
        case UI_INPUT_COMMAND_SETTINGS:
            return UI_INPUT_ACTION_READER_SETTINGS_OPEN;
        case UI_INPUT_COMMAND_CATALOG:
        case UI_INPUT_COMMAND_AUX:
            return UI_INPUT_ACTION_READER_CATALOG_TOGGLE;
        case UI_INPUT_COMMAND_LEFT:
        case UI_INPUT_COMMAND_PAGE_PREV:
            return UI_INPUT_ACTION_READER_PAGE_PREV;
        case UI_INPUT_COMMAND_RIGHT:
        case UI_INPUT_COMMAND_CONFIRM:
        case UI_INPUT_COMMAND_PAGE_NEXT:
            return UI_INPUT_ACTION_READER_PAGE_NEXT;
        case UI_INPUT_COMMAND_UP:
        case UI_INPUT_COMMAND_PAGE_UP:
            return UI_INPUT_ACTION_READER_CHAPTER_PREV;
        case UI_INPUT_COMMAND_DOWN:
        case UI_INPUT_COMMAND_PAGE_DOWN:
            return UI_INPUT_ACTION_READER_CHAPTER_NEXT;
        default:
            return UI_INPUT_ACTION_NONE;
        }
    case UI_INPUT_SCOPE_NONE:
    default:
        if (command == UI_INPUT_COMMAND_BACK) {
            return UI_INPUT_ACTION_GLOBAL_BACK;
        }
        return UI_INPUT_ACTION_NONE;
    }
}

UiInputAction ui_input_action_for_mask(UiInputScope scope, UiInputMask mask) {
    return ui_input_action_for_command(scope, ui_input_command_for_mask(mask));
}

UiInputAction ui_input_repeat_action_for_mask(UiInputScope scope, UiInputMask mask) {
    if (scope == UI_INPUT_SCOPE_SHELF) {
        if ((mask & (UI_INPUT_MASK_DOWN | UI_INPUT_MASK_RIGHT)) != 0) {
            return UI_INPUT_ACTION_SHELF_NEXT;
        }
        if ((mask & (UI_INPUT_MASK_UP | UI_INPUT_MASK_LEFT)) != 0) {
            return UI_INPUT_ACTION_SHELF_PREV;
        }
        return UI_INPUT_ACTION_NONE;
    }
    if (scope == UI_INPUT_SCOPE_READER_CATALOG) {
        if ((mask & UI_INPUT_MASK_UP) != 0) {
            return UI_INPUT_ACTION_READER_CATALOG_UP;
        }
        if ((mask & UI_INPUT_MASK_DOWN) != 0) {
            return UI_INPUT_ACTION_READER_CATALOG_DOWN;
        }
        if ((mask & UI_INPUT_MASK_LEFT) != 0) {
            return UI_INPUT_ACTION_READER_CATALOG_PAGE_PREV;
        }
        if ((mask & UI_INPUT_MASK_RIGHT) != 0) {
            return UI_INPUT_ACTION_READER_CATALOG_PAGE_NEXT;
        }
        return UI_INPUT_ACTION_NONE;
    }
    if (scope == UI_INPUT_SCOPE_READER) {
        if ((mask & UI_INPUT_MASK_PAGE_PREV) != 0) {
            return UI_INPUT_ACTION_READER_PAGE_PREV;
        }
        if ((mask & UI_INPUT_MASK_PAGE_NEXT) != 0) {
            return UI_INPUT_ACTION_READER_PAGE_NEXT;
        }
    }
    return UI_INPUT_ACTION_NONE;
}

void ui_input_state_reset(UiInputState *state, UiInputMask current_mask,
                          const UiInputSuppression *suppression) {
    if (!state) {
        return;
    }
    if (suppression) {
        current_mask &= ~suppression->blocked_mask;
    }
    state->current_mask = current_mask;
    state->pressed_mask = 0;
}

UiInputMask ui_input_state_update(UiInputState *state, UiInputMask current_mask,
                                  UiInputMask event_press_mask,
                                  const UiInputSuppression *suppression) {
    UiInputMask unblocked_current = current_mask;
    UiInputMask unblocked_events = event_press_mask;

    if (!state) {
        return 0;
    }
    if (suppression) {
        unblocked_current &= ~suppression->blocked_mask;
        unblocked_events &= ~suppression->blocked_mask;
    }
    state->pressed_mask = (unblocked_current & ~state->current_mask) |
                          (unblocked_events & ~state->current_mask);
    state->current_mask = unblocked_current;
    return state->pressed_mask;
}

void ui_input_suppression_reset(UiInputSuppression *suppression) {
    if (!suppression) {
        return;
    }
    suppression->blocked_mask = 0;
    suppression->released_since_block_mask = 0;
}

void ui_input_suppression_begin(UiInputSuppression *suppression, UiInputMask active_mask) {
    if (!suppression) {
        return;
    }
    suppression->blocked_mask = active_mask;
    suppression->released_since_block_mask = ~active_mask;
}

void ui_input_suppression_refresh(UiInputSuppression *suppression, UiInputMask active_mask) {
    if (!suppression) {
        return;
    }
    suppression->released_since_block_mask |= ~active_mask;
    suppression->blocked_mask &= active_mask | ~suppression->released_since_block_mask;
}
