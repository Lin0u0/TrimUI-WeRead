#include "test_support.h"

#include "ui_input_action.h"

static void assert_scope_actions(void) {
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_SHELF, UI_INPUT_BIT(KEY_DOWN)),
        UI_INPUT_ACTION_SHELF_NEXT);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_SHELF, UI_INPUT_BIT(HAT_RIGHT)),
        UI_INPUT_ACTION_SHELF_NEXT);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_SHELF, UI_INPUT_BIT(KEY_UP)),
        UI_INPUT_ACTION_SHELF_PREV);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_SHELF, UI_INPUT_BIT(KEY_R)),
        UI_INPUT_ACTION_SHELF_RESUME);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_SHELF, UI_INPUT_BIT(JOY_X)),
        UI_INPUT_ACTION_SHELF_RESUME);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_SHELF, UI_INPUT_BIT(JOY_Y)),
        UI_INPUT_ACTION_SHELF_SETTINGS_OPEN);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_SHELF, UI_INPUT_BIT(JOY_A)),
        UI_INPUT_ACTION_SHELF_OPEN_SELECTED);

    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_READER, UI_INPUT_BIT(JOY_R1)),
        UI_INPUT_ACTION_READER_PAGE_NEXT);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_READER, UI_INPUT_BIT(KEY_RIGHT)),
        UI_INPUT_ACTION_READER_PAGE_NEXT);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_READER, UI_INPUT_BIT(JOY_L1)),
        UI_INPUT_ACTION_READER_PAGE_PREV);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_READER, UI_INPUT_BIT(KEY_PAGEUP)),
        UI_INPUT_ACTION_READER_CHAPTER_PREV);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_READER, UI_INPUT_BIT(KEY_PAGEDOWN)),
        UI_INPUT_ACTION_READER_CHAPTER_NEXT);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_READER, UI_INPUT_BIT(KEY_C)),
        UI_INPUT_ACTION_READER_CATALOG_TOGGLE);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_READER, UI_INPUT_BIT(JOY_Y)),
        UI_INPUT_ACTION_READER_SETTINGS_OPEN);

    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_READER_CATALOG, UI_INPUT_BIT(KEY_UP)),
        UI_INPUT_ACTION_READER_CATALOG_UP);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_READER_CATALOG, UI_INPUT_BIT(KEY_LEFT)),
        UI_INPUT_ACTION_READER_CATALOG_PAGE_PREV);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_READER_CATALOG, UI_INPUT_BIT(JOY_X)),
        UI_INPUT_ACTION_READER_CATALOG_CLOSE);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_READER_CATALOG, UI_INPUT_BIT(JOY_A)),
        UI_INPUT_ACTION_READER_CATALOG_CONFIRM);

    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_SETTINGS, UI_INPUT_BIT(KEY_UP)),
        UI_INPUT_ACTION_SETTINGS_PREV);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_SETTINGS, UI_INPUT_BIT(KEY_RIGHT)),
        UI_INPUT_ACTION_SETTINGS_ADJUST_NEXT);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_SETTINGS, UI_INPUT_BIT(JOY_Y)),
        UI_INPUT_ACTION_SETTINGS_CLOSE);

    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_LOGIN, UI_INPUT_BIT(KEY_RETURN)),
        UI_INPUT_ACTION_LOGIN_CONFIRM);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_LOGIN, UI_INPUT_BIT(KEY_SPACE)),
        UI_INPUT_ACTION_LOGIN_CONFIRM);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_LOGIN, UI_INPUT_BIT(KEY_B)),
        UI_INPUT_ACTION_GLOBAL_BACK);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_LOGIN, UI_INPUT_BIT(KEY_POWER)),
        UI_INPUT_ACTION_GLOBAL_LOCK);
}

static void assert_no_login_l_shortcut(void) {
    /* Login starts from confirm only; there is no separate letter shortcut. */
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_action_for_mask(UI_INPUT_SCOPE_LOGIN, UI_INPUT_BIT(KEY_LEFT)),
        UI_INPUT_ACTION_NONE);
}

static void assert_repeat_actions(void) {
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_repeat_action_for_mask(UI_INPUT_SCOPE_SHELF, UI_INPUT_BIT(KEY_DOWN)),
        UI_INPUT_ACTION_SHELF_NEXT);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_repeat_action_for_mask(UI_INPUT_SCOPE_READER_CATALOG, UI_INPUT_BIT(KEY_RIGHT)),
        UI_INPUT_ACTION_READER_CATALOG_PAGE_NEXT);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_repeat_action_for_mask(UI_INPUT_SCOPE_READER, UI_INPUT_BIT(JOY_R1)),
        UI_INPUT_ACTION_READER_PAGE_NEXT);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_repeat_action_for_mask(UI_INPUT_SCOPE_LOGIN, UI_INPUT_BIT(JOY_A)),
        UI_INPUT_ACTION_NONE);
}

static void assert_input_state_edges(void) {
    UiInputState state;
    UiInputSuppression suppression;

    ui_input_suppression_reset(&suppression);
    ui_input_state_reset(&state, 0, &suppression);

    HOST_TEST_ASSERT_INT_EQ(
        ui_input_state_update(&state, UI_INPUT_BIT(JOY_A), 0, &suppression),
        UI_INPUT_BIT(JOY_A));
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_state_update(&state, UI_INPUT_BIT(JOY_A), 0, &suppression),
        0);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_state_update(&state, 0, 0, &suppression),
        0);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_state_update(&state, 0, UI_INPUT_BIT(JOY_A), &suppression),
        UI_INPUT_BIT(JOY_A));

    ui_input_suppression_begin(&suppression, UI_INPUT_BIT(JOY_A));
    ui_input_state_reset(&state, UI_INPUT_BIT(JOY_A), &suppression);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_state_update(&state, UI_INPUT_BIT(JOY_A), UI_INPUT_BIT(JOY_A), &suppression),
        0);
    ui_input_suppression_refresh(&suppression, 0);
    HOST_TEST_ASSERT_INT_EQ(suppression.blocked_mask, 0);
    HOST_TEST_ASSERT_INT_EQ(
        ui_input_state_update(&state, UI_INPUT_BIT(JOY_A), 0, &suppression),
        UI_INPUT_BIT(JOY_A));
}

int main(void) {
    assert_scope_actions();
    assert_no_login_l_shortcut();
    assert_repeat_actions();
    assert_input_state_edges();
    return 0;
}
