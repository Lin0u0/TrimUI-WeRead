#include "ui.h"
#include "ui_internal.h"

#if HAVE_SDL

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "stb_image.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "auth.h"
#include "json.h"
#include "preferences_state.h"
#include "reader.h"
#include "reader_state.h"
#include "session_service.h"
#include "shelf.h"
#include "shelf_state.h"
#include "state.h"

int ui_join_path_checked(char *dst, size_t dst_size, const char *dir, const char *name) {
    size_t dir_len;
    size_t name_len;

    if (!dst || dst_size == 0 || !dir || !name) {
        return -1;
    }

    dir_len = strlen(dir);
    name_len = strlen(name);
    if (dir_len + 1 + name_len + 1 > dst_size) {
        dst[0] = '\0';
        return -1;
    }

    memcpy(dst, dir, dir_len);
    dst[dir_len] = '/';
    memcpy(dst + dir_len + 1, name, name_len + 1);
    return 0;
}

void ui_copy_string(char *dst, size_t dst_size, const char *src) {
    size_t copy_len;

    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    if (dst == src) {
        return;
    }

    copy_len = strlen(src);
    if (copy_len >= dst_size) {
        copy_len = dst_size - 1;
    }
    memmove(dst, src, copy_len);
    dst[copy_len] = '\0';
}

float ui_motion_step(float current, float target, float speed, float dt_seconds) {
    float factor;

    if (dt_seconds <= 0.0f) {
        return current;
    }
    factor = ui_clamp01f(speed * dt_seconds);
    return current + (target - current) * factor;
}

void ui_force_exit_from_login(UiHapticState *haptic_state) {
    ui_platform_shutdown_haptics(haptic_state);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    _Exit(0);
}

int ui_recreate_scene_texture(SDL_Renderer *renderer, SDL_Texture **scene_texture,
                                     const UiLayout *layout) {
    SDL_Texture *new_texture;

    if (!renderer || !scene_texture || !layout) {
        return -1;
    }

    new_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                    SDL_TEXTUREACCESS_TARGET,
                                    layout->canvas_w, layout->canvas_h);
    if (!new_texture) {
        return -1;
    }

    if (*scene_texture) {
        SDL_DestroyTexture(*scene_texture);
    }
    *scene_texture = new_texture;
    return 0;
}
int ui_is_available(void) {
    return 1;
}

int ui_run(ApiContext *ctx, const char *font_path, const char *platform) {
    return ui_runtime_exec(ctx, font_path, platform);
}

#else

#include <stdio.h>

int ui_is_available(void) {
    return 0;
}

int ui_run(ApiContext *ctx, const char *font_path, const char *platform) {
    (void)ctx;
    (void)font_path;
    (void)platform;
    fprintf(stderr, "UI support is unavailable in this build.\n");
    return -1;
}

#endif
