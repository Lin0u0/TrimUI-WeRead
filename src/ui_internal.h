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
#include "auth.h"
#include "reader.h"

/* ====================== Type Definitions ====================== */

typedef enum {
    VIEW_SHELF = 0,
    VIEW_LOGIN = 1,
    VIEW_READER = 2,
    VIEW_BOOTSTRAP = 3,
    VIEW_OPENING = 4,
    VIEW_SETTINGS = 5
} UiView;

typedef enum {
    UI_SETTINGS_ORIGIN_NONE = 0,
    UI_SETTINGS_ORIGIN_SHELF,
    UI_SETTINGS_ORIGIN_READER
} UiSettingsOrigin;

typedef enum {
    UI_SETTINGS_ITEM_READER_FONT_SIZE = 0,
    UI_SETTINGS_ITEM_DARK_MODE,
    UI_SETTINGS_ITEM_BRIGHTNESS,
    UI_SETTINGS_ITEM_ROTATION,
    UI_SETTINGS_ITEM_LOGOUT,
    UI_SETTINGS_ITEM_COUNT
} UiSettingsItem;

typedef struct {
    UiSettingsItem item;
    const char *title;
} UiSettingsItemSpec;

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

typedef uint64_t UiInputMask;

typedef struct {
    UiInputMask blocked_mask;
    UiInputMask released_since_block_mask;
} UiInputSuppression;

enum {
    UI_INPUT_PHYS_KEY_UP = 0,
    UI_INPUT_PHYS_KEY_W,
    UI_INPUT_PHYS_KEY_DOWN,
    UI_INPUT_PHYS_KEY_S,
    UI_INPUT_PHYS_KEY_LEFT,
    UI_INPUT_PHYS_KEY_A,
    UI_INPUT_PHYS_KEY_RIGHT,
    UI_INPUT_PHYS_KEY_D,
    UI_INPUT_PHYS_KEY_ESCAPE,
    UI_INPUT_PHYS_KEY_B,
    UI_INPUT_PHYS_KEY_BACKSPACE,
    UI_INPUT_PHYS_KEY_RETURN,
    UI_INPUT_PHYS_KEY_SPACE,
    UI_INPUT_PHYS_KEY_R,
    UI_INPUT_PHYS_KEY_C,
    UI_INPUT_PHYS_KEY_Y,
    UI_INPUT_PHYS_KEY_TAB,
    UI_INPUT_PHYS_KEY_T,
    UI_INPUT_PHYS_KEY_LEFTBRACKET,
    UI_INPUT_PHYS_KEY_RIGHTBRACKET,
    UI_INPUT_PHYS_KEY_PAGEUP,
    UI_INPUT_PHYS_KEY_PAGEDOWN,
    UI_INPUT_PHYS_KEY_P,
    UI_INPUT_PHYS_KEY_POWER,
    UI_INPUT_PHYS_KEY_L,
    UI_INPUT_PHYS_JOY_A,
    UI_INPUT_PHYS_JOY_B,
    UI_INPUT_PHYS_JOY_X,
    UI_INPUT_PHYS_JOY_Y,
    UI_INPUT_PHYS_JOY_L1,
    UI_INPUT_PHYS_JOY_R1,
    UI_INPUT_PHYS_JOY_L2,
    UI_INPUT_PHYS_JOY_R2,
    UI_INPUT_PHYS_JOY_SELECT,
    UI_INPUT_PHYS_JOY_START,
    UI_INPUT_PHYS_HAT_UP,
    UI_INPUT_PHYS_HAT_DOWN,
    UI_INPUT_PHYS_HAT_LEFT,
    UI_INPUT_PHYS_HAT_RIGHT,
    UI_INPUT_PHYS_AXIS_X_NEG,
    UI_INPUT_PHYS_AXIS_X_POS,
    UI_INPUT_PHYS_AXIS_Y_NEG,
    UI_INPUT_PHYS_AXIS_Y_POS
};

#define UI_INPUT_BIT(name) (1ULL << UI_INPUT_PHYS_##name)
#define UI_INPUT_MASK_UP \
    (UI_INPUT_BIT(KEY_UP) | UI_INPUT_BIT(KEY_W) | UI_INPUT_BIT(HAT_UP) | UI_INPUT_BIT(AXIS_Y_NEG))
#define UI_INPUT_MASK_DOWN \
    (UI_INPUT_BIT(KEY_DOWN) | UI_INPUT_BIT(KEY_S) | UI_INPUT_BIT(HAT_DOWN) | UI_INPUT_BIT(AXIS_Y_POS))
#define UI_INPUT_MASK_LEFT \
    (UI_INPUT_BIT(KEY_LEFT) | UI_INPUT_BIT(KEY_A) | UI_INPUT_BIT(HAT_LEFT) | UI_INPUT_BIT(AXIS_X_NEG))
#define UI_INPUT_MASK_RIGHT \
    (UI_INPUT_BIT(KEY_RIGHT) | UI_INPUT_BIT(KEY_D) | UI_INPUT_BIT(HAT_RIGHT) | UI_INPUT_BIT(AXIS_X_POS))
#define UI_INPUT_MASK_LOGIN_BACK \
    (UI_INPUT_BIT(KEY_ESCAPE) | UI_INPUT_BIT(KEY_B) | UI_INPUT_BIT(KEY_BACKSPACE) | UI_INPUT_BIT(JOY_B))
#define UI_INPUT_MASK_LOGIN_CONFIRM \
    (UI_INPUT_BIT(KEY_RETURN) | UI_INPUT_BIT(KEY_SPACE) | UI_INPUT_BIT(KEY_A) | UI_INPUT_BIT(JOY_A))
#define UI_INPUT_MASK_CONFIRM \
    (UI_INPUT_BIT(KEY_RETURN) | UI_INPUT_BIT(KEY_SPACE) | UI_INPUT_BIT(JOY_A))
#define UI_INPUT_MASK_SHELF_RESUME \
    (UI_INPUT_BIT(KEY_R) | UI_INPUT_BIT(JOY_X))
#define UI_INPUT_MASK_CATALOG_TOGGLE \
    (UI_INPUT_BIT(KEY_C) | UI_INPUT_BIT(JOY_X))
#define UI_INPUT_MASK_SETTINGS_OPEN \
    (UI_INPUT_BIT(KEY_Y) | UI_INPUT_BIT(JOY_Y))
#define UI_INPUT_MASK_ROTATE \
    (UI_INPUT_BIT(KEY_TAB) | UI_INPUT_BIT(JOY_R2))
#define UI_INPUT_MASK_DARK_MODE \
    (UI_INPUT_BIT(KEY_T) | UI_INPUT_BIT(JOY_L2))
#define UI_INPUT_MASK_BRIGHTNESS_UP (UI_INPUT_BIT(KEY_RIGHTBRACKET))
#define UI_INPUT_MASK_BRIGHTNESS_DOWN (UI_INPUT_BIT(KEY_LEFTBRACKET))
#define UI_INPUT_MASK_LOCK_BUTTON \
    (UI_INPUT_BIT(KEY_POWER) | UI_INPUT_BIT(KEY_P))
#define UI_INPUT_MASK_CHAPTER_PREV \
    (UI_INPUT_MASK_UP | UI_INPUT_BIT(KEY_PAGEUP))
#define UI_INPUT_MASK_CHAPTER_NEXT \
    (UI_INPUT_MASK_DOWN | UI_INPUT_BIT(KEY_PAGEDOWN))
#define UI_INPUT_MASK_PAGE_PREV \
    (UI_INPUT_MASK_LEFT | UI_INPUT_BIT(JOY_L1))
#define UI_INPUT_MASK_PAGE_NEXT \
    (UI_INPUT_MASK_RIGHT | UI_INPUT_MASK_CONFIRM | UI_INPUT_BIT(JOY_R1))

typedef struct {
    float shelf_selected_visual;
    int shelf_initialized;
    float catalog_progress;
    float settings_progress;
    float catalog_selected_visual;
    int catalog_animating_active;
    int settings_animating_active;
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
    int motor_on;
    int worker_stop;
    SDL_mutex *lock;
    SDL_cond *cond;
    SDL_Thread *thread;
} UiHapticState;

typedef struct {
    int available;
    int percent;
    int charging;
    char text[32];
    Uint32 next_poll_tick;
} UiBatteryState;

typedef struct {
    char data_dir[512];
    char ca_file[512];
    char qr_path[1024];
    AuthSession session;
    int running;
    int success;
    int failed;
} LoginStartState;

typedef struct {
    char data_dir[512];
    char ca_file[512];
    AuthSession session;
    int running;
    int completed;
    int stop;
    AuthPollStatus last_status;
} LoginPollState;

typedef struct {
    char data_dir[512];
    char ca_file[512];
    cJSON *shelf_nuxt;
    int session_ok;
    int running;
    int completed;
    int poor_network;
} StartupState;

typedef struct {
    char *book_id;
    char *cover_url;
    char cache_path[1024];
    SDL_Texture *texture;
    int attempted;
    int download_failed;
} ShelfCoverEntry;

typedef struct {
    ShelfCoverEntry *entries;
    int count;
    ShelfCoverEntry article_entry;
    int has_article_entry;
} ShelfCoverCache;

typedef struct {
    char data_dir[512];
    char ca_file[512];
    char cover_url[2048];
    char cache_path[1024];
    int running;
    int ready;
    int failed;
    int entry_index;
} ShelfCoverDownloadState;

typedef struct {
    ReaderDocument doc;
    char source_target[2048];
    char fallback_font_path[512];
    char **lines;
    int *line_offsets;
    int line_count;
    int line_capacity;
    int lines_per_page;
    int line_height;
    int current_page;
    int catalog_open;
    int catalog_selected;
    int catalog_scroll_top;
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

typedef struct {
    char data_dir[512];
    char ca_file[512];
    char source_target[2048];
    char book_id[256];
    int font_size;
    int content_font_size;
    int initial_page;
    int initial_offset;
    int honor_saved_position;
    int running;
    int ready;
    int failed;
    int poor_network;
    ReaderDocument doc;
} ReaderOpenState;

typedef struct {
    char data_dir[512];
    char ca_file[512];
    ReaderDocument doc;
    int current_page;
    int total_pages;
    int chapter_offset;
    int reading_seconds;
    int compute_progress;
    char page_summary[128];
    int running;
    int result;
} ProgressReportState;

typedef struct {
    char data_dir[512];
    char ca_file[512];
    char target[2048];
    int font_size;
    int running;
    int ready;
    int failed;
    ReaderDocument doc;
} ChapterPrefetchState;

typedef struct {
    ChapterPrefetchState state;
    SDL_Thread *thread;
} ChapterPrefetchSlot;

typedef struct {
    ChapterPrefetchSlot slots[10];
    int last_update_index;
} ChapterPrefetchCache;

typedef struct {
    char data_dir[512];
    char ca_file[512];
    char book_id[256];
    char chapter_uid[64];
    int direction;
    int range_start;
    int range_end;
    int running;
    int ready;
    int failed;
    int item_count;
    int last_requested_direction;
    ReaderCatalogItem *items;
} CatalogHydrationState;

typedef struct {
    int open;
    int quick_open;
    int selected;
    int logout_confirm_armed;
    int shelf_selected;
    UiSettingsOrigin origin;
    UiView return_view;
} SettingsFlowState;

typedef struct {
    char nickname[128];
    char avatar_url[1024];
    char avatar_path[1024];
    SDL_Texture *avatar_texture;
    int avatar_w;
    int avatar_h;
    int avatar_attempted;
} UiUserProfile;

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
    UI_SHELF_COVER_DOWNLOAD_RADIUS = 3,
    UI_INPUT_REPEAT_DELAY_MS = 280,
    UI_PAGE_REPEAT_DELAY_MS = 340,
    UI_INPUT_REPEAT_INTERVAL_MS = 85,
    UI_FRAME_INTERVAL_ACTIVE_MS = 33,
    UI_FRAME_INTERVAL_LOADING_MS = 100,
    UI_FRAME_INTERVAL_READER_IDLE_MS = 180,
    UI_PROGRESS_REPORT_INTERVAL_MS = 30000,
    UI_PROGRESS_PAUSE_TIMEOUT_MS = 120000,
    UI_CHAPTER_PREFETCH_RADIUS = 5,
    UI_CHAPTER_PREFETCH_MAX_RUNNING = 1,
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

int ui_event_is_keydown(const SDL_Event *event, SDL_Keycode key, SDL_Scancode scancode,
                        const UiInputSuppression *suppression);
int ui_event_is_up(const SDL_Event *event, int tg5040_input,
                   const UiInputSuppression *suppression);
int ui_event_is_down(const SDL_Event *event, int tg5040_input,
                     const UiInputSuppression *suppression);
int ui_event_is_left(const SDL_Event *event, int tg5040_input,
                     const UiInputSuppression *suppression);
int ui_event_is_right(const SDL_Event *event, int tg5040_input,
                      const UiInputSuppression *suppression);
int ui_event_is_back(const SDL_Event *event, int tg5040_input,
                     const UiInputSuppression *suppression);
int ui_event_is_confirm(const SDL_Event *event, int tg5040_input,
                        const UiInputSuppression *suppression);
int ui_event_is_shelf_resume(const SDL_Event *event, int tg5040_input,
                             const UiInputSuppression *suppression);
int ui_event_is_catalog_toggle(const SDL_Event *event, int tg5040_input,
                               const UiInputSuppression *suppression);
int ui_event_is_rotate(const SDL_Event *event, int tg5040_input,
                       const UiInputSuppression *suppression);
int ui_event_is_rotate_combo(const SDL_Event *event, int tg5040_input, int select_pressed,
                             int start_pressed, const UiInputSuppression *suppression);
int ui_event_is_dark_mode_toggle(const SDL_Event *event, int tg5040_input,
                                 int select_pressed, const UiInputSuppression *suppression);
int ui_event_is_brightness_up(const SDL_Event *event, int tg5040_input, int start_pressed,
                              const UiInputSuppression *suppression);
int ui_event_is_brightness_down(const SDL_Event *event, int tg5040_input, int start_pressed,
                                const UiInputSuppression *suppression);
int ui_event_is_lock_button(const SDL_Event *event, const UiInputSuppression *suppression);
int ui_event_is_chapter_prev(const SDL_Event *event, int tg5040_input,
                             const UiInputSuppression *suppression);
int ui_event_is_chapter_next(const SDL_Event *event, int tg5040_input,
                             const UiInputSuppression *suppression);
int ui_event_is_page_prev(const SDL_Event *event, int tg5040_input,
                          const UiInputSuppression *suppression);
int ui_event_is_page_next(const SDL_Event *event, int tg5040_input,
                          const UiInputSuppression *suppression);
int ui_event_is_settings_open(const SDL_Event *event, int tg5040_input,
                              const UiInputSuppression *suppression);
int ui_any_joystick_button_pressed(SDL_Joystick **joysticks, int joystick_count, Uint8 button);
int ui_any_joystick_hat_pressed(SDL_Joystick **joysticks, int joystick_count, Uint8 mask);
int ui_any_joystick_axis_pressed(SDL_Joystick **joysticks, int joystick_count, Uint8 axis, int negative);
UiInputMask ui_input_current_mask(int tg5040_input, SDL_Joystick **joysticks, int joystick_count);
UiInputMask ui_input_unblocked_mask(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                                    const UiInputSuppression *suppression);
UiInputMask ui_input_apply_event(UiInputMask current_mask, const SDL_Event *event,
                                 int tg5040_input);
void ui_input_suppression_reset(UiInputSuppression *suppression);
void ui_input_suppression_begin(UiInputSuppression *suppression, UiInputMask active_mask);
void ui_input_suppression_refresh(UiInputSuppression *suppression, UiInputMask active_mask);
int ui_input_mask_is_held(UiInputMask mask, int tg5040_input, SDL_Joystick **joysticks,
                          int joystick_count, const UiInputSuppression *suppression);
int ui_input_is_up_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                        const UiInputSuppression *suppression);
int ui_input_is_down_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                          const UiInputSuppression *suppression);
int ui_input_is_left_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                          const UiInputSuppression *suppression);
int ui_input_is_right_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                           const UiInputSuppression *suppression);
int ui_input_is_page_prev_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                               const UiInputSuppression *suppression);
int ui_input_is_page_next_held(int tg5040_input, SDL_Joystick **joysticks, int joystick_count,
                               const UiInputSuppression *suppression);
UiRepeatAction ui_repeat_action_current(UiView view, const ReaderViewState *reader_state,
                                        UiInputMask observed_input_mask,
                                        int tg5040_input, SDL_Joystick **joysticks,
                                        int joystick_count,
                                        const UiInputSuppression *suppression);

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

/* ====================== ui_reader_view.c ====================== */

/*
 * Reader-view helpers are an internal UI sub-boundary shared by ui.c and
 * ui_flow_reader.c. They are not part of ui.h and must stay within the ui_*
 * subsystem even when ownership moves out of ui.c.
 */
void ui_reader_view_free(ReaderViewState *state);
int ui_reader_view_total_pages(ReaderViewState *state);
int ui_reader_view_find_page_for_offset(const ReaderViewState *state, int target_offset);
int ui_reader_view_adopt_document(TTF_Font *font, ReaderDocument *doc,
                                  int content_width, int content_height,
                                  int honor_saved_position, ReaderViewState *state);
int ui_reader_view_load(ApiContext *ctx, TTF_Font *font, const char *target,
                        int font_size, int content_width, int content_height,
                        int honor_saved_position, ReaderViewState *state);
int ui_reader_view_reset_content_font(TTF_Font *fallback_font, ReaderViewState *state);
int ui_reader_view_rewrap(TTF_Font *font, int content_width, int content_height,
                          ReaderViewState *state);
void ui_reader_view_set_source_target(ReaderViewState *state, const char *source_target);
void ui_reader_view_clamp_current_page(ReaderViewState *state);
void ui_reader_view_save_local_position(ApiContext *ctx, ReaderViewState *state);
int ui_reader_view_current_page_offset(const ReaderViewState *state);
int ui_reader_view_current_catalog_index(ReaderViewState *state);
void ui_reader_view_build_page_summary(ReaderViewState *state, char *out, size_t out_size);
void ui_reader_view_note_progress_activity(ReaderViewState *state, Uint32 now);
void ui_reader_view_flush_progress_blocking(ApiContext *ctx, ReaderViewState *state,
                                            int compute_progress);
void ui_reader_view_open_catalog(ApiContext *ctx, ReaderViewState *state,
                                 char *status, size_t status_size);
int ui_reader_view_expand_catalog_for_selection(ApiContext *ctx, ReaderViewState *state,
                                                int direction, char *status,
                                                size_t status_size);

/* ====================== ui_flow_startup_login.c ====================== */

void ui_startup_login_startup_state_reset(StartupState *state);
void ui_startup_login_begin_startup_refresh(ApiContext *ctx, StartupState *startup_state,
                                            SDL_Thread **startup_thread_handle);
int ui_startup_login_finish_startup(SDL_Thread **startup_thread_handle,
                                    StartupState *startup_state);
void ui_startup_login_begin_login_flow(ApiContext *ctx, LoginStartState *login_start,
                                       SDL_Thread **login_thread, UiView *view,
                                       char *status, size_t status_size, const char *qr_path);
int ui_startup_login_finish_login_start(ApiContext *ctx, LoginStartState *login_start,
                                        SDL_Thread **login_thread, LoginPollState *login_poll,
                                        SDL_Thread **login_poll_thread_handle,
                                        AuthSession *session, Uint32 *last_poll,
                                        int *login_active, char *status,
                                        size_t status_size, int *render_requested);
int ui_startup_login_poll_login(LoginPollState *login_poll,
                                SDL_Thread **login_poll_thread_handle, Uint32 *last_poll,
                                char *status, size_t status_size, int *render_requested);
void ui_startup_login_shutdown(LoginPollState *login_poll, SDL_Thread **login_thread,
                               SDL_Thread **startup_thread_handle,
                               SDL_Thread **login_poll_thread_handle);

/* ====================== ui_flow_shelf.c ====================== */

void ui_shelf_flow_cover_download_state_reset(ShelfCoverDownloadState *state);
void ui_shelf_flow_cover_download_maybe_start(ApiContext *ctx, ShelfCoverCache *cache,
                                              ShelfCoverDownloadState *state,
                                              SDL_Thread **thread_handle, int selected);
void ui_shelf_flow_cover_download_poll(ShelfCoverCache *cache,
                                       ShelfCoverDownloadState *state,
                                       SDL_Thread **thread_handle);
void ui_shelf_flow_cover_download_stop(ShelfCoverDownloadState *state,
                                       SDL_Thread **thread_handle);
int ui_shelf_flow_prepare_resume(ApiContext *ctx, char *target, size_t target_size,
                                 int *font_size, char *loading_title,
                                 size_t loading_title_size, char *status,
                                 size_t status_size);
int ui_shelf_flow_prepare_selected_open(cJSON *shelf_nuxt, int selected,
                                        char *target, size_t target_size,
                                        char *book_id, size_t book_id_size,
                                        char *loading_title, size_t loading_title_size,
                                        char *status, size_t status_size,
                                        char *shelf_status,
                                        size_t shelf_status_size);
int ui_shelf_flow_prepare_article_open(ApiContext *ctx, cJSON *shelf_nuxt, int font_size,
                                       char *target, size_t target_size,
                                       char *loading_title, size_t loading_title_size,
                                       char *status, size_t status_size,
                                       char *shelf_status, size_t shelf_status_size);

/* ====================== ui_flow_reader.c ====================== */

void ui_reader_flow_progress_report_state_reset(ProgressReportState *state);
void ui_reader_flow_reader_open_state_reset(ReaderOpenState *state);
void ui_reader_flow_chapter_prefetch_cache_reset(ChapterPrefetchCache *cache);
int ui_reader_flow_chapter_prefetch_has_running_work(const ChapterPrefetchCache *cache);
void ui_reader_flow_catalog_hydration_state_reset(CatalogHydrationState *state);
int ui_reader_flow_catalog_hydration_has_running_work(const CatalogHydrationState *state,
                                                     SDL_Thread *thread_handle);
int ui_reader_flow_chapter_prefetch_cache_adopt(ChapterPrefetchCache *cache,
                                                const char *target, TTF_Font *body_font,
                                                ReaderViewState *reader_state,
                                                const UiLayout *current_layout);
int ui_reader_flow_tick_reader(ApiContext *ctx, ReaderViewState *reader_state,
                               ProgressReportState *progress_report,
                               SDL_Thread **progress_report_thread_handle,
                               ChapterPrefetchCache *chapter_prefetch_cache,
                               CatalogHydrationState *catalog_hydration,
                               SDL_Thread **catalog_hydration_thread_handle);
void ui_reader_flow_poll_background(ChapterPrefetchCache *chapter_prefetch_cache,
                                    CatalogHydrationState *catalog_hydration,
                                    SDL_Thread **catalog_hydration_thread_handle,
                                    ReaderViewState *reader_state);
void ui_reader_flow_begin_reader_open(ApiContext *ctx, ReaderOpenState *reader_open,
                                      SDL_Thread **reader_open_thread_handle,
                                      const char *source_target, const char *book_id,
                                      int font_size);
int ui_reader_flow_finish_open(ApiContext *ctx, TTF_Font *body_font,
                               ReaderOpenState *reader_open,
                               SDL_Thread **reader_open_thread_handle,
                               ReaderViewState *reader_state, UiView *view,
                               char *status, size_t status_size,
                               char *shelf_status, size_t shelf_status_size,
                               Uint32 *poor_network_toast_until,
                               const UiLayout *current_layout, int shelf_available);
void ui_reader_flow_shutdown(ReaderOpenState *reader_open,
                             SDL_Thread **reader_open_thread_handle,
                             ProgressReportState *progress_report,
                             SDL_Thread **progress_report_thread_handle,
                             ChapterPrefetchCache *chapter_prefetch_cache,
                             CatalogHydrationState *catalog_hydration,
                             SDL_Thread **catalog_hydration_thread_handle);

/* ====================== ui_flow_settings.c ====================== */

void ui_settings_flow_state_reset(SettingsFlowState *state);
int ui_settings_flow_open(SettingsFlowState *state, UiView *view,
                          UiSettingsOrigin origin, int quick_open, int shelf_selected);
int ui_settings_flow_open_from_shelf(SettingsFlowState *state, UiView *view,
                                     int quick_open, int shelf_selected);
int ui_settings_flow_open_from_reader(SettingsFlowState *state, UiView *view,
                                      int quick_open);
int ui_settings_flow_begin_close(SettingsFlowState *state);
int ui_settings_flow_finish_close(SettingsFlowState *state, UiView *view, int *shelf_selected);
const UiSettingsItemSpec *ui_settings_flow_items(int *count_out);

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
