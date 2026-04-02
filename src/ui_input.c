/*
 * ui_input.c - Input event handling and key mappings
 *
 * Handles: keyboard events, joystick events, button mappings, repeat actions
 */
#include "ui_internal.h"

#if HAVE_SDL

int ui_event_is_keydown(const SDL_Event *event, SDL_Keycode key) {
    return event->type == SDL_KEYDOWN && event->key.keysym.sym == key;
}

int ui_event_is_tg5040_button_down(const SDL_Event *event, int enabled, Uint8 button) {
    return enabled && event->type == SDL_JOYBUTTONDOWN && event->jbutton.button == button;
}

int ui_event_is_tg5040_axis(const SDL_Event *event, int enabled, Uint8 axis, int negative) {
    const Sint16 threshold = 16000;

    if (!enabled || event->type != SDL_JOYAXISMOTION || event->jaxis.axis != axis) {
        return 0;
    }
    return negative ? event->jaxis.value <= -threshold : event->jaxis.value >= threshold;
}

int ui_event_is_up(const SDL_Event *event, int tg5040_input) {
    if (ui_event_is_keydown(event, SDLK_UP) || ui_event_is_keydown(event, SDLK_w)) {
        return 1;
    }
    if (tg5040_input && event->type == SDL_JOYHATMOTION) {
        return (event->jhat.value & SDL_HAT_UP) != 0;
    }
    return ui_event_is_tg5040_axis(event, tg5040_input, 1, 1);
}

int ui_event_is_down(const SDL_Event *event, int tg5040_input) {
    if (ui_event_is_keydown(event, SDLK_DOWN) || ui_event_is_keydown(event, SDLK_s)) {
        return 1;
    }
    if (tg5040_input && event->type == SDL_JOYHATMOTION) {
        return (event->jhat.value & SDL_HAT_DOWN) != 0;
    }
    return ui_event_is_tg5040_axis(event, tg5040_input, 1, 0);
}

int ui_event_is_left(const SDL_Event *event, int tg5040_input) {
    if (ui_event_is_keydown(event, SDLK_LEFT) || ui_event_is_keydown(event, SDLK_a)) {
        return 1;
    }
    if (tg5040_input && event->type == SDL_JOYHATMOTION) {
        return (event->jhat.value & SDL_HAT_LEFT) != 0;
    }
    return ui_event_is_tg5040_axis(event, tg5040_input, 0, 1);
}

int ui_event_is_right(const SDL_Event *event, int tg5040_input) {
    if (ui_event_is_keydown(event, SDLK_RIGHT) || ui_event_is_keydown(event, SDLK_d)) {
        return 1;
    }
    if (tg5040_input && event->type == SDL_JOYHATMOTION) {
        return (event->jhat.value & SDL_HAT_RIGHT) != 0;
    }
    return ui_event_is_tg5040_axis(event, tg5040_input, 0, 0);
}

int ui_event_is_back(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_keydown(event, SDLK_ESCAPE) ||
           ui_event_is_keydown(event, SDLK_b) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_B);
}

int ui_event_is_confirm(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_keydown(event, SDLK_RETURN) ||
           ui_event_is_keydown(event, SDLK_SPACE) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_A);
}

int ui_event_is_shelf_resume(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_keydown(event, SDLK_r) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_X);
}

int ui_event_is_catalog_toggle(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_keydown(event, SDLK_c) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_X);
}

int ui_event_is_rotate(const SDL_Event *event, int tg5040_input) {
    (void)tg5040_input;
    return ui_event_is_keydown(event, SDLK_TAB);
}

int ui_event_is_rotate_combo(const SDL_Event *event, int tg5040_input,
                             int select_pressed, int start_pressed) {
    (void)select_pressed;
    (void)start_pressed;
    return ui_event_is_rotate(event, tg5040_input) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_R2);
}

int ui_event_is_dark_mode_toggle(const SDL_Event *event, int tg5040_input, int select_pressed) {
    (void)select_pressed;
    if (ui_event_is_keydown(event, SDLK_t)) {
        return 1;
    }
    return ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_L2);
}

int ui_event_is_brightness_up(const SDL_Event *event, int tg5040_input, int start_pressed) {
    (void)tg5040_input;
    (void)start_pressed;
    return ui_event_is_keydown(event, SDLK_RIGHTBRACKET);
}

int ui_event_is_brightness_down(const SDL_Event *event, int tg5040_input, int start_pressed) {
    (void)tg5040_input;
    (void)start_pressed;
    return ui_event_is_keydown(event, SDLK_LEFTBRACKET);
}

int ui_event_is_lock_button(const SDL_Event *event) {
    return ui_event_is_keydown(event, SDLK_POWER) ||
           ui_event_is_keydown(event, SDLK_p);
}

int ui_event_is_chapter_prev(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_up(event, tg5040_input) ||
           ui_event_is_keydown(event, SDLK_PAGEUP);
}

int ui_event_is_chapter_next(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_down(event, tg5040_input) ||
           ui_event_is_keydown(event, SDLK_PAGEDOWN);
}

int ui_event_is_page_prev(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_left(event, tg5040_input) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_L1);
}

int ui_event_is_page_next(const SDL_Event *event, int tg5040_input) {
    return ui_event_is_right(event, tg5040_input) ||
           ui_event_is_confirm(event, tg5040_input) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_R1);
}

int ui_any_joystick_button_pressed(SDL_Joystick **joysticks, int joystick_count, Uint8 button) {
    int i;

    for (i = 0; i < joystick_count; i++) {
        if (joysticks[i] && SDL_JoystickGetButton(joysticks[i], button)) {
            return 1;
        }
    }
    return 0;
}

int ui_any_joystick_hat_pressed(SDL_Joystick **joysticks, int joystick_count, Uint8 mask) {
    int i;

    for (i = 0; i < joystick_count; i++) {
        if (joysticks[i] && (SDL_JoystickGetHat(joysticks[i], 0) & mask) != 0) {
            return 1;
        }
    }
    return 0;
}

int ui_any_joystick_axis_pressed(SDL_Joystick **joysticks, int joystick_count,
                                 Uint8 axis, int negative) {
    const Sint16 threshold = 16000;
    int i;

    for (i = 0; i < joystick_count; i++) {
        Sint16 value;

        if (!joysticks[i]) {
            continue;
        }
        value = SDL_JoystickGetAxis(joysticks[i], axis);
        if ((negative && value <= -threshold) || (!negative && value >= threshold)) {
            return 1;
        }
    }
    return 0;
}

int ui_input_is_up_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    return (keys && (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W])) ||
           (tg5040_input &&
            (ui_any_joystick_hat_pressed(joysticks, joystick_count, SDL_HAT_UP) ||
             ui_any_joystick_axis_pressed(joysticks, joystick_count, 1, 1)));
}

int ui_input_is_down_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    return (keys && (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S])) ||
           (tg5040_input &&
            (ui_any_joystick_hat_pressed(joysticks, joystick_count, SDL_HAT_DOWN) ||
             ui_any_joystick_axis_pressed(joysticks, joystick_count, 1, 0)));
}

int ui_input_is_left_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    return (keys && (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A])) ||
           (tg5040_input &&
            (ui_any_joystick_hat_pressed(joysticks, joystick_count, SDL_HAT_LEFT) ||
             ui_any_joystick_axis_pressed(joysticks, joystick_count, 0, 1)));
}

int ui_input_is_right_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    return (keys && (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D])) ||
           (tg5040_input &&
            (ui_any_joystick_hat_pressed(joysticks, joystick_count, SDL_HAT_RIGHT) ||
             ui_any_joystick_axis_pressed(joysticks, joystick_count, 0, 0)));
}

int ui_input_is_page_prev_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count) {
    return ui_input_is_left_held(tg5040_input, joysticks, joystick_count) ||
           (tg5040_input &&
            ui_any_joystick_button_pressed(joysticks, joystick_count, TG5040_JOY_L1));
}

int ui_input_is_page_next_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    return ui_input_is_right_held(tg5040_input, joysticks, joystick_count) ||
           (keys && (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE])) ||
           (tg5040_input &&
            (ui_any_joystick_button_pressed(joysticks, joystick_count, TG5040_JOY_A) ||
             ui_any_joystick_button_pressed(joysticks, joystick_count, TG5040_JOY_R1)));
}

UiRepeatAction ui_repeat_action_current(UiView view, const ReaderViewState *reader_state,
                                        int tg5040_input, SDL_Joystick **joysticks,
                                        int joystick_count) {
    if (view == VIEW_SHELF) {
        if (ui_input_is_down_held(tg5040_input, joysticks, joystick_count) ||
            ui_input_is_right_held(tg5040_input, joysticks, joystick_count)) {
            return UI_REPEAT_SHELF_NEXT;
        }
        if (ui_input_is_up_held(tg5040_input, joysticks, joystick_count) ||
            ui_input_is_left_held(tg5040_input, joysticks, joystick_count)) {
            return UI_REPEAT_SHELF_PREV;
        }
        return UI_REPEAT_NONE;
    }
    if (view != VIEW_READER || !reader_state) {
        return UI_REPEAT_NONE;
    }
    if (reader_state->catalog_open) {
        if (ui_input_is_up_held(tg5040_input, joysticks, joystick_count)) {
            return UI_REPEAT_CATALOG_UP;
        }
        if (ui_input_is_down_held(tg5040_input, joysticks, joystick_count)) {
            return UI_REPEAT_CATALOG_DOWN;
        }
        if (ui_input_is_page_prev_held(tg5040_input, joysticks, joystick_count)) {
            return UI_REPEAT_CATALOG_PAGE_PREV;
        }
        if (ui_input_is_page_next_held(tg5040_input, joysticks, joystick_count)) {
            return UI_REPEAT_CATALOG_PAGE_NEXT;
        }
        return UI_REPEAT_NONE;
    }
    if (ui_input_is_page_prev_held(tg5040_input, joysticks, joystick_count)) {
        return UI_REPEAT_PAGE_PREV;
    }
    if (ui_input_is_page_next_held(tg5040_input, joysticks, joystick_count)) {
        return UI_REPEAT_PAGE_NEXT;
    }
    return UI_REPEAT_NONE;
}

#endif /* HAVE_SDL */
