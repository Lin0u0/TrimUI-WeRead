#ifndef UI_RUNTIME_INTERNAL_H
#define UI_RUNTIME_INTERNAL_H

#include "ui_internal.h"

#if HAVE_SDL

#include <time.h>
#include "json.h"

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Joystick **joysticks;
    TTF_Font *title_font;
    TTF_Font *body_font;
    cJSON *shelf_nuxt;
    ReaderViewState reader_state;
    StartupState startup_state;
    ReaderOpenState reader_open;
    UiView view;
    int selected;
    int shelf_start;
    int running;
    Uint32 last_poll;
    AuthSession session;
    LoginStartState login_start;
    LoginPollState login_poll;
    ProgressReportState progress_report;
    ShelfCoverCache shelf_covers;
    ShelfCoverDownloadState shelf_cover_download;
    ChapterPrefetchCache chapter_prefetch_cache;
    CatalogHydrationState catalog_hydration;
    SDL_Thread *login_thread;
    SDL_Thread *startup_thread_handle;
    SDL_Thread *reader_open_thread_handle;
    SDL_Thread *shelf_cover_download_thread_handle;
    SDL_Thread *login_poll_thread_handle;
    SDL_Thread *progress_report_thread_handle;
    SDL_Thread *catalog_hydration_thread_handle;
    SDL_Texture *scene_texture;
    SDL_Texture *qr_texture;
    int qr_tex_w;
    int qr_tex_h;
    int login_active;
    char status[256];
    char shelf_status[256];
    char loading_title[128];
    char qr_path[1024];
    int tg5040_input;
    int brightness_level;
    int joystick_count;
    UiRotation rotation;
    UiLayout current_layout;
    UiRepeatState repeat_state;
    UiInputSuppression input_suppression;
    UiMotionState motion_state;
    UiHapticState haptic_state;
    UiBatteryState battery_state;
    SettingsFlowState settings_state;
    UiUserProfile user_profile;
    int preferred_reader_font_size;
    Uint32 poor_network_toast_until;
    Uint32 exit_confirm_until;
    Uint32 reader_exit_confirm_until;
    UiInputState input_state;
    Uint32 lock_button_ignore_until;
    Uint32 last_lock_trigger_tick;
    time_t last_clock_minute;
    int rc;
} UiRuntime;

typedef struct {
    Uint32 frame_now;
    time_t wall_now;
    float dt_seconds;
    UiView frame_view_before_updates;
    int catalog_open_before_updates;
    float shelf_selected_visual_before_updates;
    float catalog_selected_visual_before_updates;
    float catalog_progress_before_updates;
    float settings_progress_before_updates;
    char frame_reader_target_before[2048];
    int reader_input_seen;
    int render_requested;
    time_t current_clock_minute;
    UiInputTransitionContext transition_input;
} UiRuntimeFrame;

int ui_runtime_boot(UiRuntime *runtime, ApiContext *ctx,
                    const char *font_path, const char *platform);
void ui_runtime_shutdown(UiRuntime *runtime, ApiContext *ctx);
void ui_runtime_tick_after_input(UiRuntime *runtime, ApiContext *ctx,
                                 UiRuntimeFrame *frame);
void ui_runtime_process_events(UiRuntime *runtime, ApiContext *ctx,
                               UiRuntimeFrame *frame);
void ui_runtime_render_frame(UiRuntime *runtime, ApiContext *ctx,
                             const UiRuntimeFrame *frame);
void ui_runtime_wait_for_next_frame(UiRuntime *runtime, Uint32 frame_now);

#endif

#endif
