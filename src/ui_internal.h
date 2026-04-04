/*
 * ui_internal.h - Internal shared types and declarations for UI modules
 *
 * This header is NOT part of the public API. It provides shared types
 * and forward declarations used across ui_*.c files.
 */
#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include "ui.h"

#if HAVE_SDL

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdint.h>
#include "reader.h"

/* ====================== Type Definitions ====================== */

typedef enum {
    VIEW_SHELF = 0,
    VIEW_LOGIN = 1,
    VIEW_READER = 2,
    VIEW_BOOTSTRAP = 3,
    VIEW_OPENING = 4
} UiView;

typedef enum {
    UI_ROTATE_LANDSCAPE = 0,
    UI_ROTATE_RIGHT_PORTRAIT = 1,
    UI_ROTATE_LEFT_PORTRAIT = 2
} UiRotation;

typedef enum {
    UI_REPEAT_NONE = 0,
    UI_REPEAT_SHELF_PREV,
    UI_REPEAT_SHELF_NEXT,
    UI_REPEAT_CATALOG_UP,
    UI_REPEAT_CATALOG_DOWN,
    UI_REPEAT_CATALOG_PAGE_PREV,
    UI_REPEAT_CATALOG_PAGE_NEXT,
    UI_REPEAT_PAGE_PREV,
    UI_REPEAT_PAGE_NEXT
} UiRepeatAction;

typedef struct {
    int canvas_w;
    int canvas_h;
    int content_x;
    int content_w;
    int reader_content_w;
    int reader_content_h;
} UiLayout;

typedef struct {
    UiRepeatAction action;
    Uint32 next_tick;
} UiRepeatState;

typedef struct {
    float shelf_selected_visual;
    int shelf_initialized;
    float catalog_progress;
    float catalog_selected_visual;
    int catalog_animating_active;
    int catalog_selection_initialized;
    int catalog_selection_animating_active;
    Uint32 last_tick;
    Uint32 view_fade_start_tick;
    Uint32 view_fade_duration_ms;
    int view_fade_active;
} UiMotionState;

typedef struct {
    int available;
    char value_path[64];
    Uint32 next_allowed_tick;
    Uint32 stop_tick;
} UiHapticState;

typedef struct {
    int available;
    int percent;
    int charging;
    char text[32];
    Uint32 next_poll_tick;
} UiBatteryState;

typedef struct {
    ReaderDocument doc;
    char source_target[2048];
    char **lines;
    int *line_offsets;
    int line_count;
    int line_capacity;
    int lines_per_page;
    int line_height;
    int current_page;
    int catalog_open;
    int catalog_selected;
    int content_font_size;
    TTF_Font *content_font;
    Uint32 progress_start_tick;
    Uint32 progress_pause_tick;
    Uint32 progress_pause_deadline_tick;
    Uint32 progress_report_due_tick;
    int progress_initialized;
    int progress_paused;
    int progress_initial_report_pending;
    int progress_session_expired;
} ReaderViewState;

/* ====================== Constants ====================== */

enum {
    UI_CANVAS_WIDTH = 1024,
    UI_CANVAS_HEIGHT = 768,
    UI_CANVAS_PORTRAIT_WIDTH = 768,
    UI_CANVAS_PORTRAIT_HEIGHT = 1024,
    UI_TITLE_FONT_SIZE = 36,
    UI_BODY_FONT_SIZE = 28,
    UI_READER_CONTENT_FONT_SIZE = 36,
    UI_READER_EXTRA_LEADING = 10,
    UI_READER_PARAGRAPH_GAP_LINES = 0,
    UI_READER_CONTENT_WIDTH = 912,
    UI_READER_CONTENT_HEIGHT = 640,
    UI_SHELF_COVER_TEXTURE_KEEP_RADIUS = 4,
    UI_INPUT_REPEAT_DELAY_MS = 280,
    UI_PAGE_REPEAT_DELAY_MS = 340,
    UI_INPUT_REPEAT_INTERVAL_MS = 85,
    UI_FRAME_INTERVAL_ACTIVE_MS = 33,
    UI_FRAME_INTERVAL_LOADING_MS = 100,
    UI_FRAME_INTERVAL_READER_IDLE_MS = 180,
    UI_PROGRESS_REPORT_INTERVAL_MS = 30000,
    UI_PROGRESS_PAUSE_TIMEOUT_MS = 120000,
    UI_CHAPTER_PREFETCH_RADIUS = 5,
    UI_TOAST_DURATION_MS = 3000,
    UI_TOAST_FADE_MS = 280,
    UI_VIEW_FADE_DURATION_MS = 320,
    UI_EXIT_CONFIRM_DURATION_MS = 1500,
    UI_BATTERY_POLL_INTERVAL_MS = 30000
};

#define UI_CATALOG_ANIMATION_SPEED 7.0f
#define UI_CATALOG_CLOSE_ANIMATION_SPEED 12.0f
#define UI_SHELF_SELECTION_SPEED 12.0f
#define UI_CATALOG_SELECTION_SPEED 24.0f

enum {
    UI_BRIGHTNESS_MIN = 0,
    UI_BRIGHTNESS_MAX = 10,
    UI_BRIGHTNESS_DEFAULT = 7,
    DISP_LCD_SET_BRIGHTNESS = 0x102
};

enum {
    UI_HAPTIC_GPIO = 227,
    UI_HAPTIC_NAV_MS = 38,
    UI_HAPTIC_PAGE_MS = 22,
    UI_HAPTIC_COOLDOWN_MS = 80
};

/* TG5040 joystick button mappings */
enum {
    TG5040_JOY_B = 0,
    TG5040_JOY_A = 1,
    TG5040_JOY_Y = 2,
    TG5040_JOY_X = 3,
    TG5040_JOY_L1 = 4,
    TG5040_JOY_R1 = 5,
    TG5040_JOY_SELECT = 6,
    TG5040_JOY_START = 7,
    TG5040_JOY_MENU = 8,
    TG5040_JOY_L2 = 9,
    TG5040_JOY_R2 = 10
};

/* ====================== ui_platform.c ====================== */

int ui_write_text_file(const char *path, const char *value);
int ui_platform_init_haptics(int tg5040_input, UiHapticState *state);
void ui_platform_shutdown_haptics(UiHapticState *state);
void ui_platform_haptic_poll(UiHapticState *state, Uint32 now);
void ui_platform_haptic_pulse(UiHapticState *state, Uint32 duration_ms, Uint32 cooldown_ms);
int ui_platform_apply_brightness_level(int tg5040_input, int level);
int ui_platform_step_brightness(ApiContext *ctx, int tg5040_input, int delta, int *brightness_level);
int ui_platform_lock_screen(int tg5040_input);
int ui_platform_restore_after_sleep(SDL_Renderer *renderer, SDL_Texture **scene_texture,
                                    const UiLayout *layout, int tg5040_input, int brightness_level);
int ui_is_tg5040_platform(const char *platform);
void ui_battery_state_update(UiBatteryState *state, Uint32 now);
int ui_tg5040_scale_brightness(int level);
Uint32 ui_frame_interval_ms(UiView view, const UiMotionState *motion_state,
                            Uint32 poor_network_toast_until, Uint32 exit_confirm_until, Uint32 now);

/* ====================== ui_input.c ====================== */

int ui_event_is_keydown(const SDL_Event *event, SDL_Keycode key);
int ui_event_is_tg5040_button_down(const SDL_Event *event, int enabled, Uint8 button);
int ui_event_is_tg5040_axis(const SDL_Event *event, int enabled, Uint8 axis, int negative);
int ui_event_is_up(const SDL_Event *event, int tg5040_input);
int ui_event_is_down(const SDL_Event *event, int tg5040_input);
int ui_event_is_left(const SDL_Event *event, int tg5040_input);
int ui_event_is_right(const SDL_Event *event, int tg5040_input);
int ui_event_is_back(const SDL_Event *event, int tg5040_input);
int ui_event_is_confirm(const SDL_Event *event, int tg5040_input);
int ui_event_is_shelf_resume(const SDL_Event *event, int tg5040_input);
int ui_event_is_catalog_toggle(const SDL_Event *event, int tg5040_input);
int ui_event_is_rotate(const SDL_Event *event, int tg5040_input);
int ui_event_is_rotate_combo(const SDL_Event *event, int tg5040_input, int select_pressed, int start_pressed);
int ui_event_is_dark_mode_toggle(const SDL_Event *event, int tg5040_input, int select_pressed);
int ui_event_is_brightness_up(const SDL_Event *event, int tg5040_input, int start_pressed);
int ui_event_is_brightness_down(const SDL_Event *event, int tg5040_input, int start_pressed);
int ui_event_is_lock_button(const SDL_Event *event);
int ui_event_is_chapter_prev(const SDL_Event *event, int tg5040_input);
int ui_event_is_chapter_next(const SDL_Event *event, int tg5040_input);
int ui_event_is_page_prev(const SDL_Event *event, int tg5040_input);
int ui_event_is_page_next(const SDL_Event *event, int tg5040_input);
int ui_any_joystick_button_pressed(SDL_Joystick **joysticks, int joystick_count, Uint8 button);
int ui_any_joystick_hat_pressed(SDL_Joystick **joysticks, int joystick_count, Uint8 mask);
int ui_any_joystick_axis_pressed(SDL_Joystick **joysticks, int joystick_count, Uint8 axis, int negative);
int ui_input_is_up_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count);
int ui_input_is_down_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count);
int ui_input_is_left_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count);
int ui_input_is_right_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count);
int ui_input_is_page_prev_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count);
int ui_input_is_page_next_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count);
UiRepeatAction ui_repeat_action_current(UiView view, const ReaderViewState *reader_state,
                                        int tg5040_input, SDL_Joystick **joysticks, int joystick_count);

/* ====================== ui_text.c ====================== */

int utf8_char_len(unsigned char c);
uint32_t utf8_decode(const char *s, int *out_len);
int is_cjk_codepoint(uint32_t cp);
int is_cjk_char(const char *s);
int is_latin_or_digit(const char *s);
void char_width_cache_reset(void);
int get_char_width_fast(TTF_Font *font, const char *s, int char_len);
int is_forbidden_line_start_punct(const char *text);
const char *skip_line_start_spacing(const char *text, const char *end);

/* ====================== ui.c ====================== */

int ui_recreate_scene_texture(SDL_Renderer *renderer, SDL_Texture **scene_texture,
                              const UiLayout *layout);

/* ====================== Utility functions ====================== */

int ui_join_path_checked(char *dst, size_t dst_size, const char *dir, const char *name);
void ui_copy_string(char *dst, size_t dst_size, const char *src);
float ui_clamp01f(float x);
float ui_ease_out_cubic(float t);
float ui_ease_in_out_cubic(float t);
Uint8 ui_view_fade_alpha(float progress);
float ui_motion_step(float current, float target, float speed, float dt_seconds);

#endif /* HAVE_SDL */
#endif /* UI_INTERNAL_H */
