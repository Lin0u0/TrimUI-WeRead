/*
 * ui_input.c - Input event handling and key mappings
 *
 * The UI suppresses only the physical inputs that were already held at the
 * moment of a transition, so newly pressed keys/buttons keep working
 * immediately on the next page or view.
 */
#include "ui_internal.h"

#if HAVE_SDL

static UiInputMask ui_input_scancode_bit(SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_UP: return UI_INPUT_BIT(KEY_UP);
        case SDL_SCANCODE_W: return UI_INPUT_BIT(KEY_W);
        case SDL_SCANCODE_DOWN: return UI_INPUT_BIT(KEY_DOWN);
        case SDL_SCANCODE_S: return UI_INPUT_BIT(KEY_S);
        case SDL_SCANCODE_LEFT: return UI_INPUT_BIT(KEY_LEFT);
        case SDL_SCANCODE_A: return UI_INPUT_BIT(KEY_A);
        case SDL_SCANCODE_RIGHT: return UI_INPUT_BIT(KEY_RIGHT);
        case SDL_SCANCODE_D: return UI_INPUT_BIT(KEY_D);
        case SDL_SCANCODE_ESCAPE: return UI_INPUT_BIT(KEY_ESCAPE);
        case SDL_SCANCODE_B: return UI_INPUT_BIT(KEY_B);
        case SDL_SCANCODE_BACKSPACE: return UI_INPUT_BIT(KEY_BACKSPACE);
        case SDL_SCANCODE_RETURN: return UI_INPUT_BIT(KEY_RETURN);
        case SDL_SCANCODE_SPACE: return UI_INPUT_BIT(KEY_SPACE);
        case SDL_SCANCODE_R: return UI_INPUT_BIT(KEY_R);
        case SDL_SCANCODE_C: return UI_INPUT_BIT(KEY_C);
        case SDL_SCANCODE_Y: return UI_INPUT_BIT(KEY_Y);
        case SDL_SCANCODE_TAB: return UI_INPUT_BIT(KEY_TAB);
        case SDL_SCANCODE_T: return UI_INPUT_BIT(KEY_T);
        case SDL_SCANCODE_LEFTBRACKET: return UI_INPUT_BIT(KEY_LEFTBRACKET);
        case SDL_SCANCODE_RIGHTBRACKET: return UI_INPUT_BIT(KEY_RIGHTBRACKET);
        case SDL_SCANCODE_PAGEUP: return UI_INPUT_BIT(KEY_PAGEUP);
        case SDL_SCANCODE_PAGEDOWN: return UI_INPUT_BIT(KEY_PAGEDOWN);
        case SDL_SCANCODE_P: return UI_INPUT_BIT(KEY_P);
        case SDL_SCANCODE_POWER: return UI_INPUT_BIT(KEY_POWER);
        default:
            return 0;
    }
}

static UiInputMask ui_input_button_bit(Uint8 button) {
    switch (button) {
        case TG5040_JOY_A: return UI_INPUT_BIT(JOY_A);
        case TG5040_JOY_B: return UI_INPUT_BIT(JOY_B);
        case TG5040_JOY_X: return UI_INPUT_BIT(JOY_X);
        case TG5040_JOY_Y: return UI_INPUT_BIT(JOY_Y);
        case TG5040_JOY_L1: return UI_INPUT_BIT(JOY_L1);
        case TG5040_JOY_R1: return UI_INPUT_BIT(JOY_R1);
        case TG5040_JOY_L2: return UI_INPUT_BIT(JOY_L2);
        case TG5040_JOY_R2: return UI_INPUT_BIT(JOY_R2);
        case TG5040_JOY_SELECT: return UI_INPUT_BIT(JOY_SELECT);
        case TG5040_JOY_START: return UI_INPUT_BIT(JOY_START);
        default:
            return 0;
    }
}

static UiInputMask ui_input_hat_mask(Uint8 value) {
    UiInputMask mask = 0;

    if ((value & SDL_HAT_UP) != 0) {
        mask |= UI_INPUT_BIT(HAT_UP);
    }
    if ((value & SDL_HAT_DOWN) != 0) {
        mask |= UI_INPUT_BIT(HAT_DOWN);
    }
    if ((value & SDL_HAT_LEFT) != 0) {
        mask |= UI_INPUT_BIT(HAT_LEFT);
    }
    if ((value & SDL_HAT_RIGHT) != 0) {
        mask |= UI_INPUT_BIT(HAT_RIGHT);
    }
    return mask;
}

static UiInputMask ui_input_axis_mask(Uint8 axis, Sint16 value) {
    const Sint16 threshold = 16000;

    if (axis == 0) {
        if (value <= -threshold) {
            return UI_INPUT_BIT(AXIS_X_NEG);
        }
        if (value >= threshold) {
            return UI_INPUT_BIT(AXIS_X_POS);
        }
        return 0;
    }
    if (axis == 1) {
        if (value <= -threshold) {
            return UI_INPUT_BIT(AXIS_Y_NEG);
        }
        if (value >= threshold) {
            return UI_INPUT_BIT(AXIS_Y_POS);
        }
    }
    return 0;
}

static int ui_input_mask_blocked(const UiInputSuppression *suppression, UiInputMask mask) {
    return suppression && (suppression->blocked_mask & mask) != 0;
}

static int ui_event_is_tg5040_button_down(const SDL_Event *event, int enabled, Uint8 button,
                                          const UiInputSuppression *suppression) {
    UiInputMask mask = ui_input_button_bit(button);

    return enabled &&
           event->type == SDL_JOYBUTTONDOWN &&
           event->jbutton.button == button &&
           !ui_input_mask_blocked(suppression, mask);
}

static int ui_event_is_tg5040_axis(const SDL_Event *event, int enabled, Uint8 axis, int negative,
                                   const UiInputSuppression *suppression) {
    UiInputMask mask;

    if (!enabled || event->type != SDL_JOYAXISMOTION || event->jaxis.axis != axis) {
        return 0;
    }
    mask = ui_input_axis_mask(axis, event->jaxis.value);
    if (negative) {
        return mask == UI_INPUT_BIT(AXIS_X_NEG) ||
               mask == UI_INPUT_BIT(AXIS_Y_NEG) ?
            !ui_input_mask_blocked(suppression, mask) : 0;
    }
    return mask == UI_INPUT_BIT(AXIS_X_POS) ||
           mask == UI_INPUT_BIT(AXIS_Y_POS) ?
        !ui_input_mask_blocked(suppression, mask) : 0;
}

int ui_event_is_keydown(const SDL_Event *event, SDL_Keycode key, SDL_Scancode scancode,
                        const UiInputSuppression *suppression) {
    UiInputMask mask = ui_input_scancode_bit(scancode);

    return event->type == SDL_KEYDOWN &&
           event->key.repeat == 0 &&
           event->key.keysym.sym == key &&
           !ui_input_mask_blocked(suppression, mask);
}

int ui_event_is_up(const SDL_Event *event, int tg5040_input,
                   const UiInputSuppression *suppression) {
    if (ui_event_is_keydown(event, SDLK_UP, SDL_SCANCODE_UP, suppression) ||
        ui_event_is_keydown(event, SDLK_w, SDL_SCANCODE_W, suppression)) {
        return 1;
    }
    if (tg5040_input && event->type == SDL_JOYHATMOTION) {
        UiInputMask mask = ui_input_hat_mask(event->jhat.value) & UI_INPUT_BIT(HAT_UP);

        return mask != 0 && !ui_input_mask_blocked(suppression, mask);
    }
    return ui_event_is_tg5040_axis(event, tg5040_input, 1, 1, suppression);
}

int ui_event_is_down(const SDL_Event *event, int tg5040_input,
                     const UiInputSuppression *suppression) {
    if (ui_event_is_keydown(event, SDLK_DOWN, SDL_SCANCODE_DOWN, suppression) ||
        ui_event_is_keydown(event, SDLK_s, SDL_SCANCODE_S, suppression)) {
        return 1;
    }
    if (tg5040_input && event->type == SDL_JOYHATMOTION) {
        UiInputMask mask = ui_input_hat_mask(event->jhat.value) & UI_INPUT_BIT(HAT_DOWN);

        return mask != 0 && !ui_input_mask_blocked(suppression, mask);
    }
    return ui_event_is_tg5040_axis(event, tg5040_input, 1, 0, suppression);
}

int ui_event_is_left(const SDL_Event *event, int tg5040_input,
                     const UiInputSuppression *suppression) {
    if (ui_event_is_keydown(event, SDLK_LEFT, SDL_SCANCODE_LEFT, suppression) ||
        ui_event_is_keydown(event, SDLK_a, SDL_SCANCODE_A, suppression)) {
        return 1;
    }
    if (tg5040_input && event->type == SDL_JOYHATMOTION) {
        UiInputMask mask = ui_input_hat_mask(event->jhat.value) & UI_INPUT_BIT(HAT_LEFT);

        return mask != 0 && !ui_input_mask_blocked(suppression, mask);
    }
    return ui_event_is_tg5040_axis(event, tg5040_input, 0, 1, suppression);
}

int ui_event_is_right(const SDL_Event *event, int tg5040_input,
                      const UiInputSuppression *suppression) {
    if (ui_event_is_keydown(event, SDLK_RIGHT, SDL_SCANCODE_RIGHT, suppression) ||
        ui_event_is_keydown(event, SDLK_d, SDL_SCANCODE_D, suppression)) {
        return 1;
    }
    if (tg5040_input && event->type == SDL_JOYHATMOTION) {
        UiInputMask mask = ui_input_hat_mask(event->jhat.value) & UI_INPUT_BIT(HAT_RIGHT);

        return mask != 0 && !ui_input_mask_blocked(suppression, mask);
    }
    return ui_event_is_tg5040_axis(event, tg5040_input, 0, 0, suppression);
}

int ui_event_is_back(const SDL_Event *event, int tg5040_input,
                     const UiInputSuppression *suppression) {
    return ui_event_is_keydown(event, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, suppression) ||
           ui_event_is_keydown(event, SDLK_b, SDL_SCANCODE_B, suppression) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_B, suppression);
}

int ui_event_is_confirm(const SDL_Event *event, int tg5040_input,
                        const UiInputSuppression *suppression) {
    return ui_event_is_keydown(event, SDLK_RETURN, SDL_SCANCODE_RETURN, suppression) ||
           ui_event_is_keydown(event, SDLK_SPACE, SDL_SCANCODE_SPACE, suppression) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_A, suppression);
}

int ui_event_is_shelf_resume(const SDL_Event *event, int tg5040_input,
                             const UiInputSuppression *suppression) {
    return ui_event_is_keydown(event, SDLK_r, SDL_SCANCODE_R, suppression) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_X, suppression);
}

int ui_event_is_catalog_toggle(const SDL_Event *event, int tg5040_input,
                               const UiInputSuppression *suppression) {
    return ui_event_is_keydown(event, SDLK_c, SDL_SCANCODE_C, suppression) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_X, suppression);
}

int ui_event_is_lock_button(const SDL_Event *event, const UiInputSuppression *suppression) {
    return ui_event_is_keydown(event, SDLK_POWER, SDL_SCANCODE_POWER, suppression) ||
           ui_event_is_keydown(event, SDLK_p, SDL_SCANCODE_P, suppression);
}

int ui_event_is_chapter_prev(const SDL_Event *event, int tg5040_input,
                             const UiInputSuppression *suppression) {
    return ui_event_is_up(event, tg5040_input, suppression) ||
           ui_event_is_keydown(event, SDLK_PAGEUP, SDL_SCANCODE_PAGEUP, suppression);
}

int ui_event_is_chapter_next(const SDL_Event *event, int tg5040_input,
                             const UiInputSuppression *suppression) {
    return ui_event_is_down(event, tg5040_input, suppression) ||
           ui_event_is_keydown(event, SDLK_PAGEDOWN, SDL_SCANCODE_PAGEDOWN, suppression);
}

int ui_event_is_page_prev(const SDL_Event *event, int tg5040_input,
                          const UiInputSuppression *suppression) {
    return ui_event_is_left(event, tg5040_input, suppression) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_L1, suppression);
}

int ui_event_is_page_next(const SDL_Event *event, int tg5040_input,
                          const UiInputSuppression *suppression) {
    return ui_event_is_right(event, tg5040_input, suppression) ||
           ui_event_is_confirm(event, tg5040_input, suppression) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_R1, suppression);
}

int ui_event_is_settings_open(const SDL_Event *event, int tg5040_input,
                              const UiInputSuppression *suppression) {
    return ui_event_is_keydown(event, SDLK_y, SDL_SCANCODE_Y, suppression) ||
           ui_event_is_tg5040_button_down(event, tg5040_input, TG5040_JOY_Y, suppression);
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

UiInputMask ui_input_current_mask(int tg5040_input, SDL_Joystick **joysticks, int joystick_count) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    UiInputMask mask = 0;

    if (keys) {
        if (keys[SDL_SCANCODE_UP]) {
            mask |= UI_INPUT_BIT(KEY_UP);
        }
        if (keys[SDL_SCANCODE_W]) {
            mask |= UI_INPUT_BIT(KEY_W);
        }
        if (keys[SDL_SCANCODE_DOWN]) {
            mask |= UI_INPUT_BIT(KEY_DOWN);
        }
        if (keys[SDL_SCANCODE_S]) {
            mask |= UI_INPUT_BIT(KEY_S);
        }
        if (keys[SDL_SCANCODE_LEFT]) {
            mask |= UI_INPUT_BIT(KEY_LEFT);
        }
        if (keys[SDL_SCANCODE_A]) {
            mask |= UI_INPUT_BIT(KEY_A);
        }
        if (keys[SDL_SCANCODE_RIGHT]) {
            mask |= UI_INPUT_BIT(KEY_RIGHT);
        }
        if (keys[SDL_SCANCODE_D]) {
            mask |= UI_INPUT_BIT(KEY_D);
        }
        if (keys[SDL_SCANCODE_ESCAPE]) {
            mask |= UI_INPUT_BIT(KEY_ESCAPE);
        }
        if (keys[SDL_SCANCODE_B]) {
            mask |= UI_INPUT_BIT(KEY_B);
        }
        if (keys[SDL_SCANCODE_BACKSPACE]) {
            mask |= UI_INPUT_BIT(KEY_BACKSPACE);
        }
        if (keys[SDL_SCANCODE_RETURN]) {
            mask |= UI_INPUT_BIT(KEY_RETURN);
        }
        if (keys[SDL_SCANCODE_SPACE]) {
            mask |= UI_INPUT_BIT(KEY_SPACE);
        }
        if (keys[SDL_SCANCODE_R]) {
            mask |= UI_INPUT_BIT(KEY_R);
        }
        if (keys[SDL_SCANCODE_C]) {
            mask |= UI_INPUT_BIT(KEY_C);
        }
        if (keys[SDL_SCANCODE_Y]) {
            mask |= UI_INPUT_BIT(KEY_Y);
        }
        if (keys[SDL_SCANCODE_TAB]) {
            mask |= UI_INPUT_BIT(KEY_TAB);
        }
        if (keys[SDL_SCANCODE_T]) {
            mask |= UI_INPUT_BIT(KEY_T);
        }
        if (keys[SDL_SCANCODE_LEFTBRACKET]) {
            mask |= UI_INPUT_BIT(KEY_LEFTBRACKET);
        }
        if (keys[SDL_SCANCODE_RIGHTBRACKET]) {
            mask |= UI_INPUT_BIT(KEY_RIGHTBRACKET);
        }
        if (keys[SDL_SCANCODE_PAGEUP]) {
            mask |= UI_INPUT_BIT(KEY_PAGEUP);
        }
        if (keys[SDL_SCANCODE_PAGEDOWN]) {
            mask |= UI_INPUT_BIT(KEY_PAGEDOWN);
        }
        if (keys[SDL_SCANCODE_P]) {
            mask |= UI_INPUT_BIT(KEY_P);
        }
        if (keys[SDL_SCANCODE_POWER]) {
            mask |= UI_INPUT_BIT(KEY_POWER);
        }
    }

    if (!tg5040_input) {
        return mask;
    }

    if (ui_any_joystick_button_pressed(joysticks, joystick_count, TG5040_JOY_A)) {
        mask |= UI_INPUT_BIT(JOY_A);
    }
    if (ui_any_joystick_button_pressed(joysticks, joystick_count, TG5040_JOY_B)) {
        mask |= UI_INPUT_BIT(JOY_B);
    }
    if (ui_any_joystick_button_pressed(joysticks, joystick_count, TG5040_JOY_X)) {
        mask |= UI_INPUT_BIT(JOY_X);
    }
    if (ui_any_joystick_button_pressed(joysticks, joystick_count, TG5040_JOY_Y)) {
        mask |= UI_INPUT_BIT(JOY_Y);
    }
    if (ui_any_joystick_button_pressed(joysticks, joystick_count, TG5040_JOY_L1)) {
        mask |= UI_INPUT_BIT(JOY_L1);
    }
    if (ui_any_joystick_button_pressed(joysticks, joystick_count, TG5040_JOY_R1)) {
        mask |= UI_INPUT_BIT(JOY_R1);
    }
    if (ui_any_joystick_button_pressed(joysticks, joystick_count, TG5040_JOY_L2)) {
        mask |= UI_INPUT_BIT(JOY_L2);
    }
    if (ui_any_joystick_button_pressed(joysticks, joystick_count, TG5040_JOY_R2)) {
        mask |= UI_INPUT_BIT(JOY_R2);
    }
    if (ui_any_joystick_button_pressed(joysticks, joystick_count, TG5040_JOY_SELECT)) {
        mask |= UI_INPUT_BIT(JOY_SELECT);
    }
    if (ui_any_joystick_button_pressed(joysticks, joystick_count, TG5040_JOY_START)) {
        mask |= UI_INPUT_BIT(JOY_START);
    }
    if (ui_any_joystick_hat_pressed(joysticks, joystick_count, SDL_HAT_UP)) {
        mask |= UI_INPUT_BIT(HAT_UP);
    }
    if (ui_any_joystick_hat_pressed(joysticks, joystick_count, SDL_HAT_DOWN)) {
        mask |= UI_INPUT_BIT(HAT_DOWN);
    }
    if (ui_any_joystick_hat_pressed(joysticks, joystick_count, SDL_HAT_LEFT)) {
        mask |= UI_INPUT_BIT(HAT_LEFT);
    }
    if (ui_any_joystick_hat_pressed(joysticks, joystick_count, SDL_HAT_RIGHT)) {
        mask |= UI_INPUT_BIT(HAT_RIGHT);
    }
    if (ui_any_joystick_axis_pressed(joysticks, joystick_count, 0, 1)) {
        mask |= UI_INPUT_BIT(AXIS_X_NEG);
    }
    if (ui_any_joystick_axis_pressed(joysticks, joystick_count, 0, 0)) {
        mask |= UI_INPUT_BIT(AXIS_X_POS);
    }
    if (ui_any_joystick_axis_pressed(joysticks, joystick_count, 1, 1)) {
        mask |= UI_INPUT_BIT(AXIS_Y_NEG);
    }
    if (ui_any_joystick_axis_pressed(joysticks, joystick_count, 1, 0)) {
        mask |= UI_INPUT_BIT(AXIS_Y_POS);
    }
    return mask;
}

UiInputMask ui_input_unblocked_mask(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                                    const UiInputSuppression *suppression) {
    UiInputMask mask = ui_input_current_mask(tg5040_input, joysticks, joystick_count);

    if (suppression) {
        mask &= ~suppression->blocked_mask;
    }
    return mask;
}

UiInputMask ui_input_apply_event(UiInputMask current_mask, const SDL_Event *event,
                                 int tg5040_input) {
    UiInputMask bit;

    if (!event) {
        return current_mask;
    }

    switch (event->type) {
        case SDL_KEYDOWN:
            if (event->key.repeat != 0) {
                return current_mask;
            }
            bit = ui_input_scancode_bit(event->key.keysym.scancode);
            return bit ? (current_mask | bit) : current_mask;
        case SDL_KEYUP:
            bit = ui_input_scancode_bit(event->key.keysym.scancode);
            return bit ? (current_mask & ~bit) : current_mask;
        case SDL_JOYBUTTONDOWN:
            if (!tg5040_input) {
                return current_mask;
            }
            bit = ui_input_button_bit(event->jbutton.button);
            return bit ? (current_mask | bit) : current_mask;
        case SDL_JOYBUTTONUP:
            if (!tg5040_input) {
                return current_mask;
            }
            bit = ui_input_button_bit(event->jbutton.button);
            return bit ? (current_mask & ~bit) : current_mask;
        case SDL_JOYHATMOTION:
            if (!tg5040_input) {
                return current_mask;
            }
            current_mask &=
                ~(UI_INPUT_BIT(HAT_UP) | UI_INPUT_BIT(HAT_DOWN) |
                  UI_INPUT_BIT(HAT_LEFT) | UI_INPUT_BIT(HAT_RIGHT));
            return current_mask | ui_input_hat_mask(event->jhat.value);
        case SDL_JOYAXISMOTION:
            if (!tg5040_input) {
                return current_mask;
            }
            if (event->jaxis.axis == 0) {
                current_mask &= ~(UI_INPUT_BIT(AXIS_X_NEG) | UI_INPUT_BIT(AXIS_X_POS));
            } else if (event->jaxis.axis == 1) {
                current_mask &= ~(UI_INPUT_BIT(AXIS_Y_NEG) | UI_INPUT_BIT(AXIS_Y_POS));
            } else {
                return current_mask;
            }
            return current_mask | ui_input_axis_mask(event->jaxis.axis, event->jaxis.value);
        default:
            return current_mask;
    }
}

UiInputMask ui_input_event_mask(const SDL_Event *event, int tg5040_input) {
    if (!event) {
        return 0;
    }
    switch (event->type) {
        case SDL_KEYDOWN:
            if (event->key.repeat != 0) {
                return 0;
            }
            return ui_input_scancode_bit(event->key.keysym.scancode);
        case SDL_JOYBUTTONDOWN:
            return tg5040_input ? ui_input_button_bit(event->jbutton.button) : 0;
        case SDL_JOYHATMOTION:
            return tg5040_input ? ui_input_hat_mask(event->jhat.value) : 0;
        case SDL_JOYAXISMOTION:
            return tg5040_input ?
                ui_input_axis_mask(event->jaxis.axis, event->jaxis.value) : 0;
        default:
            return 0;
    }
}

int ui_input_mask_is_held(UiInputMask mask, int tg5040_input, SDL_Joystick **joysticks,
                          int joystick_count, const UiInputSuppression *suppression) {
    return (ui_input_unblocked_mask(tg5040_input, joysticks, joystick_count, suppression) &
            mask) != 0;
}

int ui_input_is_up_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                        const UiInputSuppression *suppression) {
    return ui_input_mask_is_held(UI_INPUT_MASK_UP, tg5040_input, joysticks, joystick_count,
                                 suppression);
}

int ui_input_is_down_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                          const UiInputSuppression *suppression) {
    return ui_input_mask_is_held(UI_INPUT_MASK_DOWN, tg5040_input, joysticks, joystick_count,
                                 suppression);
}

int ui_input_is_left_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                          const UiInputSuppression *suppression) {
    return ui_input_mask_is_held(UI_INPUT_MASK_LEFT, tg5040_input, joysticks, joystick_count,
                                 suppression);
}

int ui_input_is_right_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                           const UiInputSuppression *suppression) {
    return ui_input_mask_is_held(UI_INPUT_MASK_RIGHT, tg5040_input, joysticks, joystick_count,
                                 suppression);
}

int ui_input_is_page_prev_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                               const UiInputSuppression *suppression) {
    return ui_input_mask_is_held(UI_INPUT_MASK_PAGE_PREV, tg5040_input, joysticks,
                                 joystick_count, suppression);
}

int ui_input_is_page_next_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                               const UiInputSuppression *suppression) {
    return ui_input_mask_is_held(UI_INPUT_MASK_PAGE_NEXT, tg5040_input, joysticks,
                                 joystick_count, suppression);
}

UiInputCommand ui_input_command_for_event(const SDL_Event *event, int tg5040_input,
                                          const UiInputSuppression *suppression) {
    UiInputMask mask = ui_input_event_mask(event, tg5040_input);

    if (suppression) {
        mask &= ~suppression->blocked_mask;
    }
    return ui_input_command_for_mask(mask);
}

UiInputAction ui_input_action_for_event(UiInputScope scope, const SDL_Event *event,
                                        int tg5040_input,
                                        const UiInputSuppression *suppression) {
    return ui_input_action_for_command(
        scope,
        ui_input_command_for_event(event, tg5040_input, suppression));
}

UiInputAction ui_input_repeat_action_current(UiInputScope scope, int tg5040_input,
                                             SDL_Joystick **joysticks,
                                             int joystick_count,
                                             const UiInputSuppression *suppression) {
    return ui_input_repeat_action_for_mask(
        scope,
        ui_input_unblocked_mask(tg5040_input, joysticks, joystick_count, suppression));
}

#endif /* HAVE_SDL */
