/*
 * ui_platform.c - Hardware abstraction layer for TG5040 platform
 *
 * Handles: haptics, brightness, screen lock, battery status
 */
#include "ui_internal.h"

#if HAVE_SDL

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int ui_tg5040_scale_brightness(int level) {
    switch (level) {
    case 0: return 1;
    case 1: return 8;
    case 2: return 16;
    case 3: return 32;
    case 4: return 48;
    case 5: return 72;
    case 6: return 96;
    case 7: return 128;
    case 8: return 160;
    case 9: return 192;
    case 10:
    default:
        return 255;
    }
}

int ui_write_text_file(const char *path, const char *value) {
    ssize_t written;
    size_t len;
    int fd;

    if (!path || !value) {
        return -1;
    }

    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    len = strlen(value);
    written = write(fd, value, len);
    close(fd);
    return written == (ssize_t)len ? 0 : -1;
}

static int ui_haptic_write_value(const UiHapticState *state, const char *value) {
    if (!state || !state->available || !state->value_path[0]) {
        return -1;
    }
    return ui_write_text_file(state->value_path, value);
}

int ui_platform_init_haptics(int tg5040_input, UiHapticState *state) {
    char gpio_path[64];

    if (!state) {
        return -1;
    }

    memset(state, 0, sizeof(*state));
    if (!tg5040_input) {
        return 0;
    }

    if (ui_write_text_file("/sys/class/gpio/export", "227") != 0 && errno != EBUSY) {
        fprintf(stderr, "Haptics: failed to export GPIO %d: %s\n", UI_HAPTIC_GPIO, strerror(errno));
        return -1;
    }

    snprintf(gpio_path, sizeof(gpio_path), "/sys/class/gpio/gpio%d/direction", UI_HAPTIC_GPIO);
    if (ui_write_text_file(gpio_path, "out") != 0) {
        fprintf(stderr, "Haptics: failed to set direction on %s: %s\n", gpio_path, strerror(errno));
        return -1;
    }

    snprintf(gpio_path, sizeof(gpio_path), "/sys/class/gpio/gpio%d/value", UI_HAPTIC_GPIO);
    if (ui_write_text_file(gpio_path, "0") != 0) {
        fprintf(stderr, "Haptics: failed to initialize %s: %s\n", gpio_path, strerror(errno));
        return -1;
    }

    state->available = 1;
    snprintf(state->value_path, sizeof(state->value_path), "%s", gpio_path);
    return 0;
}

void ui_platform_shutdown_haptics(UiHapticState *state) {
    if (!state) {
        return;
    }

    if (state->available) {
        (void)ui_haptic_write_value(state, "0");
    }

    state->available = 0;
    state->value_path[0] = '\0';
    state->next_allowed_tick = 0;
}

void ui_platform_haptic_poll(UiHapticState *state, Uint32 now) {
    (void)state;
    (void)now;
}

void ui_platform_haptic_pulse(UiHapticState *state, Uint32 duration_ms, Uint32 cooldown_ms) {
    Uint32 now;

    if (!state || !state->available) {
        return;
    }

    now = SDL_GetTicks();
    if ((Sint32)(now - state->next_allowed_tick) < 0) {
        return;
    }

    if (ui_haptic_write_value(state, "1") != 0) {
        fprintf(stderr, "Haptics: failed to write 1 to %s: %s\n",
                state->value_path, strerror(errno));
        return;
    }

    if (duration_ms > 0) {
        usleep((useconds_t)duration_ms * 1000U);
    }

    if (ui_haptic_write_value(state, "0") != 0) {
        fprintf(stderr, "Haptics: failed to write 0 to %s: %s\n",
                state->value_path, strerror(errno));
        return;
    }

    state->next_allowed_tick = now + cooldown_ms;
}

Uint32 ui_frame_interval_ms(UiView view, const UiMotionState *motion_state,
                            Uint32 poor_network_toast_until, Uint32 exit_confirm_until, Uint32 now) {
    int animated = 0;

    if (motion_state) {
        animated = motion_state->view_fade_active ||
            motion_state->catalog_animating_active;
    }
    if (poor_network_toast_until > now) {
        animated = 1;
    }
    if (exit_confirm_until > now) {
        animated = 1;
    }
    if (view == VIEW_BOOTSTRAP || view == VIEW_OPENING) {
        return UI_FRAME_INTERVAL_LOADING_MS;
    }
    if (view == VIEW_READER) {
        if (animated) {
            return UI_FRAME_INTERVAL_ACTIVE_MS;
        }
        return UI_FRAME_INTERVAL_READER_IDLE_MS;
    }
    return 0;
}

int ui_platform_apply_brightness_level(int tg5040_input, int level) {
    unsigned long param[4] = { 0, 0, 0, 0 };
    int fd;

    if (!tg5040_input) {
        return -1;
    }
    if (level < UI_BRIGHTNESS_MIN) {
        level = UI_BRIGHTNESS_MIN;
    } else if (level > UI_BRIGHTNESS_MAX) {
        level = UI_BRIGHTNESS_MAX;
    }

    fd = open("/dev/disp", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    param[1] = (unsigned long)ui_tg5040_scale_brightness(level);
    if (ioctl(fd, DISP_LCD_SET_BRIGHTNESS, param) != 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

int ui_platform_step_brightness(int tg5040_input, int *current_level, int delta) {
    int new_level;

    if (!tg5040_input || !current_level) {
        return -1;
    }
    new_level = *current_level + delta;
    if (new_level < UI_BRIGHTNESS_MIN) {
        new_level = UI_BRIGHTNESS_MIN;
    } else if (new_level > UI_BRIGHTNESS_MAX) {
        new_level = UI_BRIGHTNESS_MAX;
    }
    if (new_level == *current_level) {
        return 0;
    }
    if (ui_platform_apply_brightness_level(tg5040_input, new_level) == 0) {
        *current_level = new_level;
        return 0;
    }
    return -1;
}

int ui_platform_lock_screen(int tg5040_input) {
    int rc;

    if (!tg5040_input) {
        return -1;
    }
    rc = system("echo 1 > /sys/class/graphics/fb0/blank 2>/dev/null");
    return rc == 0 ? 0 : -1;
}

int ui_platform_restore_after_sleep(SDL_Renderer *renderer, SDL_Texture **scene_texture,
                                    const UiLayout *layout, int tg5040_input,
                                    int brightness_level) {
    if (!renderer || !scene_texture || !layout) {
        return -1;
    }

    usleep(150 * 1000);
    SDL_PumpEvents();

    /* Recreate scene texture - function defined in ui.c */
    extern int ui_recreate_scene_texture(SDL_Renderer *renderer, SDL_Texture **scene_texture,
                                         const UiLayout *layout);
    if (ui_recreate_scene_texture(renderer, scene_texture, layout) != 0) {
        return -1;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    (void)system("echo 0 > /sys/class/graphics/fb0/blank 2>/dev/null");
    (void)ui_platform_apply_brightness_level(tg5040_input, brightness_level);

    return 0;
}

int ui_is_tg5040_platform(const char *platform) {
    return platform && strcmp(platform, "tg5040") == 0;
}

void ui_battery_state_update(UiBatteryState *state, Uint32 now) {
    FILE *fp;
    char buf[64];

    if (!state) {
        return;
    }

    if (!state->available) {
        fp = fopen("/sys/class/power_supply/battery/present", "r");
        if (fp) {
            if (fgets(buf, sizeof(buf), fp) && buf[0] == '1') {
                state->available = 1;
            }
            fclose(fp);
        }
        if (!state->available) {
            return;
        }
    }

    if ((Sint32)(now - state->next_poll_tick) < 0) {
        return;
    }
    state->next_poll_tick = now + UI_BATTERY_POLL_INTERVAL_MS;

    fp = fopen("/sys/class/power_supply/battery/capacity", "r");
    if (fp) {
        if (fgets(buf, sizeof(buf), fp)) {
            state->percent = atoi(buf);
        }
        fclose(fp);
    }

    fp = fopen("/sys/class/power_supply/battery/status", "r");
    if (fp) {
        if (fgets(buf, sizeof(buf), fp)) {
            state->charging = (strstr(buf, "Charging") != NULL);
        }
        fclose(fp);
    }

    if (state->charging) {
        snprintf(state->text, sizeof(state->text), "%d%% ⚡", state->percent);
    } else {
        snprintf(state->text, sizeof(state->text), "%d%%", state->percent);
    }
}

#endif /* HAVE_SDL */
