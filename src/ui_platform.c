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
#include "state.h"

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

int ui_platform_step_brightness(ApiContext *ctx, int tg5040_input, int delta,
                                int *brightness_level) {
    int next_level;

    if (!brightness_level || delta == 0) {
        return -1;
    }

    next_level = *brightness_level;
    if (next_level < UI_BRIGHTNESS_MIN || next_level > UI_BRIGHTNESS_MAX) {
        next_level = UI_BRIGHTNESS_DEFAULT;
    }
    next_level += delta;
    if (next_level < UI_BRIGHTNESS_MIN) {
        next_level = UI_BRIGHTNESS_MIN;
    } else if (next_level > UI_BRIGHTNESS_MAX) {
        next_level = UI_BRIGHTNESS_MAX;
    }
    if (next_level == *brightness_level && *brightness_level >= UI_BRIGHTNESS_MIN &&
        *brightness_level <= UI_BRIGHTNESS_MAX) {
        return 0;
    }
    if (ui_platform_apply_brightness_level(tg5040_input, next_level) != 0) {
        return -1;
    }

    *brightness_level = next_level;
    if (ctx) {
        state_save_brightness_level(ctx, next_level);
    }
    return 0;
}

int ui_platform_lock_screen(int tg5040_input) {
    int rc;

    if (!tg5040_input) {
        return -1;
    }

    sync();
    rc = system("sh -c 'echo mem > /sys/power/state'");
    return rc == 0 ? 0 : -1;
}

int ui_platform_restore_after_sleep(SDL_Renderer *renderer, SDL_Texture **scene_texture,
                                    const UiLayout *layout, int tg5040_input,
                                    int brightness_level) {
    if (!renderer || !scene_texture || !layout) {
        return -1;
    }

    /* Let the display and SDL video backend settle after resume before we
     * rebuild render targets and force a fresh present. */
    usleep(150 * 1000);
    SDL_PumpEvents();

    if (ui_recreate_scene_texture(renderer, scene_texture, layout) != 0) {
        fprintf(stderr, "Failed to recreate scene texture after resume: %s\n", SDL_GetError());
        return -1;
    }

    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    if (brightness_level >= UI_BRIGHTNESS_MIN && brightness_level <= UI_BRIGHTNESS_MAX) {
        /* Some tg5040 firmwares restore the panel power before the brightness
         * controller is ready, so nudge it twice with a small gap. */
        ui_platform_apply_brightness_level(tg5040_input, brightness_level);
        usleep(30 * 1000);
        ui_platform_apply_brightness_level(tg5040_input, brightness_level);
    }

    return 0;
}

int ui_is_tg5040_platform(const char *platform) {
    return platform && strcmp(platform, "tg5040") == 0;
}

static int ui_read_first_line(const char *path, char *buf, size_t buf_size) {
    FILE *fp;

    if (!path || !buf || buf_size == 0) {
        return -1;
    }
    fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }
    if (!fgets(buf, (int)buf_size, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

static int ui_read_battery_percent(int *percent_out) {
    static const char *paths[] = {
        "/sys/class/power_supply/battery/capacity",
        "/sys/class/power_supply/BAT0/capacity",
        "/sys/class/power_supply/max170xx_battery/capacity",
        "/sys/class/power_supply/axp2202-battery/capacity",
        NULL
    };
    char buf[32];

    if (!percent_out) {
        return -1;
    }
    for (int i = 0; paths[i]; i++) {
        int value;
        if (ui_read_first_line(paths[i], buf, sizeof(buf)) != 0) {
            continue;
        }
        value = atoi(buf);
        if (value >= 0 && value <= 100) {
            *percent_out = value;
            return 0;
        }
    }
    return -1;
}

static int ui_read_battery_charging(int *charging_out) {
    static const char *paths[] = {
        "/sys/class/power_supply/battery/status",
        "/sys/class/power_supply/BAT0/status",
        "/sys/class/power_supply/max170xx_battery/status",
        "/sys/class/power_supply/axp2202-battery/status",
        NULL
    };
    char buf[32];

    if (!charging_out) {
        return -1;
    }
    for (int i = 0; paths[i]; i++) {
        if (ui_read_first_line(paths[i], buf, sizeof(buf)) != 0) {
            continue;
        }
        *charging_out = strcmp(buf, "Charging") == 0 || strcmp(buf, "Full") == 0;
        return 0;
    }
    return -1;
}

void ui_battery_state_update(UiBatteryState *state, Uint32 now) {
    int percent;
    int charging = 0;

    if (!state || state->next_poll_tick > now) {
        return;
    }
    if (ui_read_battery_percent(&percent) == 0) {
        state->available = 1;
        state->percent = percent;
        if (ui_read_battery_charging(&charging) == 0) {
            state->charging = charging;
        } else {
            state->charging = 0;
        }
        snprintf(state->text, sizeof(state->text), "%d%%%s", state->percent,
                 state->charging ? "+" : "");
    } else {
        state->available = 0;
        state->percent = -1;
        state->charging = 0;
        snprintf(state->text, sizeof(state->text), "--%%");
    }
    state->next_poll_tick = now + UI_BATTERY_POLL_INTERVAL_MS;
}

#endif /* HAVE_SDL */
