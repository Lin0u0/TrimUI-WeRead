#include "ui_internal.h"

#if HAVE_SDL

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <limits.h>
#include "stb_image.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "json.h"
#include "shelf.h"

static char *dup_or_null(const char *s) {
    return s ? strdup(s) : NULL;
}

static char *shelf_cover_normalize_url(const char *url) {
    static const char http_qlogo_prefix[] = "http://wx.qlogo.cn/";
    static const char https_qlogo_prefix[] = "https://wx.qlogo.cn/";

    if (!url || !url[0]) {
        return NULL;
    }
    if (strncmp(url, http_qlogo_prefix, strlen(http_qlogo_prefix)) == 0) {
        size_t suffix_len = strlen(url) - strlen(http_qlogo_prefix);
        char *normalized = malloc(strlen(https_qlogo_prefix) + suffix_len + 1);

        if (!normalized) {
            return NULL;
        }
        memcpy(normalized, https_qlogo_prefix, strlen(https_qlogo_prefix));
        memcpy(normalized + strlen(https_qlogo_prefix),
               url + strlen(http_qlogo_prefix),
               suffix_len + 1);
        return normalized;
    }
    return strdup(url);
}

static void shelf_cover_entry_reset(ShelfCoverEntry *entry) {
    if (!entry) {
        return;
    }
    free(entry->book_id);
    free(entry->cover_url);
    if (entry->texture) {
        SDL_DestroyTexture(entry->texture);
    }
    memset(entry, 0, sizeof(*entry));
}

void shelf_cover_cache_reset(ShelfCoverCache *cache) {
    if (!cache) {
        return;
    }
    if (cache->entries) {
        for (int i = 0; i < cache->count; i++) {
            shelf_cover_entry_reset(&cache->entries[i]);
        }
    }
    if (cache->has_article_entry) {
        shelf_cover_entry_reset(&cache->article_entry);
    }
    free(cache->entries);
    memset(cache, 0, sizeof(*cache));
    cache->last_trim_selected = INT_MIN;
    cache->last_trim_visible_start = INT_MIN;
    cache->last_trim_visible_end = INT_MIN;
    cache->last_trim_keep_radius = -1;
}

static const char *shelf_cover_url(cJSON *book) {
    static const char *paths[] = {
        "cover",
        "coverUrl",
        "cover_url",
        "coverMiddle",
        "coverMid",
        "coverLarge",
        "coverSmall",
        "bookCover",
        "bookInfo.cover",
        "bookInfo.coverUrl",
        "bookInfo.coverLarge",
        "bookInfo.coverMiddle",
        "bookInfo.coverSmall",
        "bookInfo.bookCover",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        const char *value = json_get_string(book, paths[i]);
        if (value && *value) {
            return value;
        }
    }
    return NULL;
}

static int ensure_dir(const char *path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static SDL_Surface *load_image_stb(const char *path) {
    FILE *fp;
    long file_size;
    unsigned char *file_data;
    int w, h, channels;
    unsigned char *pixels;
    SDL_Surface *surface;

    fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (file_size <= 0 || file_size > 16 * 1024 * 1024) {
        fclose(fp);
        return NULL;
    }
    file_data = malloc((size_t)file_size);
    if (!file_data) {
        fclose(fp);
        return NULL;
    }
    if ((long)fread(file_data, 1, (size_t)file_size, fp) != file_size) {
        free(file_data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    pixels = stbi_load_from_memory(file_data, (int)file_size, &w, &h, &channels, 4);
    free(file_data);
    if (!pixels) {
        return NULL;
    }

    surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, 32, w * 4,
                                                 SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        stbi_image_free(pixels);
        return NULL;
    }
    return surface;
}

static int shelf_cover_prepare(ApiContext *ctx, SDL_Renderer *renderer, ShelfCoverEntry *entry) {
    SDL_Surface *surface;

    if (!ctx || !renderer || !entry) {
        return -1;
    }
    if (entry->texture) {
        return entry->texture ? 0 : -1;
    }
    if (!entry->cover_url || !entry->cache_path[0]) {
        return -1;
    }

    if (access(entry->cache_path, F_OK) != 0) {
        entry->attempted = 1;
        return -1;
    }

    surface = load_image_stb(entry->cache_path);
    if (!surface) {
        return -1;
    }
    entry->texture = SDL_CreateTextureFromSurface(renderer, surface);
    {
        void *pixels = surface->pixels;
        SDL_FreeSurface(surface);
        stbi_image_free(pixels);
    }
    return entry->texture ? 0 : -1;
}

static int shelf_cover_prepare_logical(ApiContext *ctx, SDL_Renderer *renderer,
                                       ShelfCoverCache *cache, int article_count,
                                       int book_count, int logical_index) {
    int index = shelf_ui_cover_cache_index_with_counts(
        article_count, book_count, logical_index);
    ShelfCoverEntry *entry;

    if (index < 0 || index >= cache->count) {
        return 0;
    }

    entry = &cache->entries[index];
    if (entry->texture || !entry->cache_path[0] || access(entry->cache_path, F_OK) != 0) {
        return 0;
    }

    return shelf_cover_prepare(ctx, renderer, entry) == 0 ? 1 : 0;
}

int shelf_cover_prepare_nearby(ApiContext *ctx, SDL_Renderer *renderer,
                               cJSON *nuxt, ShelfCoverCache *cache,
                               float selected_pos, int selected,
                               int direction, int max_prepare) {
    int article_count;
    int book_count;
    int visual_center;
    int min_selected;
    int max_selected;
    int prepared = 0;

    if (!ctx || !renderer || !cache || !cache->entries || cache->count <= 0 ||
        max_prepare <= 0) {
        return 0;
    }

    article_count = shelf_article_count(nuxt);
    book_count = shelf_normal_book_count(nuxt);
    min_selected = article_count > 0 ? -article_count : 0;
    max_selected = book_count > 0 ? book_count - 1 : -1;
    if (max_selected < min_selected) {
        return 0;
    }
    if (selected < min_selected) {
        selected = min_selected;
    } else if (selected > max_selected) {
        selected = max_selected;
    }
    if (direction > 0) {
        direction = 1;
    } else if (direction < 0) {
        direction = -1;
    }

    visual_center = (int)lroundf(selected_pos);
    prepared += shelf_cover_prepare_logical(ctx, renderer, cache, article_count,
                                            book_count, selected);
    if (prepared >= max_prepare) {
        return prepared;
    }

    if (direction != 0) {
        for (int distance = 1; distance <= UI_SHELF_COVER_PREPARE_AHEAD_RADIUS; distance++) {
            prepared += shelf_cover_prepare_logical(
                ctx, renderer, cache, article_count, book_count,
                selected + direction * distance);
            if (prepared >= max_prepare) {
                return prepared;
            }
        }
    }

    for (int distance = 0; distance <= 3; distance++) {
        int candidates[2] = { visual_center - distance, visual_center + distance };
        int candidate_count = distance == 0 ? 1 : 2;

        for (int i = 0; i < candidate_count; i++) {
            prepared += shelf_cover_prepare_logical(ctx, renderer, cache, article_count,
                                                    book_count, candidates[i]);
            if (prepared >= max_prepare) {
                return prepared;
            }
        }
    }

    if (direction != 0) {
        for (int distance = 1; distance <= UI_SHELF_COVER_PREPARE_AHEAD_RADIUS; distance++) {
            prepared += shelf_cover_prepare_logical(
                ctx, renderer, cache, article_count, book_count,
                selected - direction * distance);
            if (prepared >= max_prepare) {
                return prepared;
            }
        }
    }

    return prepared;
}

void shelf_cover_cache_build(ApiContext *ctx, cJSON *nuxt, ShelfCoverCache *cache) {
    char covers_dir[1024];
    char file_name[256];
    int article_count;
    int book_count;
    int count;

    shelf_cover_cache_reset(cache);
    if (!ctx) {
        return;
    }

    snprintf(covers_dir, sizeof(covers_dir), "%s/covers", ctx->state_dir);
    if (ensure_dir(covers_dir) != 0) {
        return;
    }

    article_count = shelf_article_count(nuxt);
    book_count = shelf_normal_book_count(nuxt);
    count = article_count + book_count;
    if (count <= 0) {
        return;
    }
    cache->entries = calloc((size_t)count, sizeof(ShelfCoverEntry));
    if (!cache->entries) {
        cache->count = 0;
        return;
    }
    cache->count = count;

    for (int i = 0; i < count; i++) {
        cJSON *book = i < article_count ?
            shelf_article_at(nuxt, i, NULL) :
            shelf_normal_book_at(nuxt, i - article_count, NULL);
        ShelfCoverEntry *entry = &cache->entries[i];
        const char *book_id = json_get_string(book, "bookId");
        const char *cover = shelf_cover_url(book);

        entry->book_id = dup_or_null(book_id);
        entry->cover_url = shelf_cover_normalize_url(cover);
        if (book_id && *book_id) {
            if (strlen(book_id) + strlen(".img") + 1 > sizeof(file_name) ||
                snprintf(file_name, sizeof(file_name), "%s.img", book_id) < 0 ||
                ui_join_path_checked(entry->cache_path, sizeof(entry->cache_path),
                                     covers_dir, file_name) != 0) {
                entry->cache_path[0] = '\0';
            }
        } else {
            if (snprintf(file_name, sizeof(file_name), "book-%d.img", i) < 0 ||
                ui_join_path_checked(entry->cache_path, sizeof(entry->cache_path),
                                     covers_dir, file_name) != 0) {
                entry->cache_path[0] = '\0';
            }
        }
    }

    cache->has_article_entry = 0;
}

static const char *ui_user_profile_pick_string(cJSON *root, const char *const *paths) {
    for (int i = 0; root && paths && paths[i]; i++) {
        const char *value = json_get_string(root, paths[i]);
        if (value && *value) {
            return value;
        }
    }
    return NULL;
}

void ui_user_profile_clear(UiUserProfile *profile) {
    if (!profile) {
        return;
    }
    if (profile->avatar_texture) {
        SDL_DestroyTexture(profile->avatar_texture);
        profile->avatar_texture = NULL;
    }
    memset(profile, 0, sizeof(*profile));
}

void ui_user_profile_sync(UiUserProfile *profile, cJSON *shelf_nuxt,
                          const char *data_dir) {
    static const char *const nickname_paths[] = {
        "state.shelf.userInfo.name",
        "state.shelf.userInfo.nick",
        "state.shelf.userInfo.nickName",
        "state.shelf.userInfo.nickname",
        "state.shelf.userInfo.userName",
        "state.user.name",
        "state.user.nick",
        "state.user.nickName",
        "state.user.nickname",
        NULL
    };
    static const char *const avatar_paths[] = {
        "state.shelf.userInfo.avatar",
        "state.shelf.userInfo.avatarUrl",
        "state.shelf.userInfo.avatarURI",
        "state.shelf.userInfo.head",
        "state.shelf.userInfo.headImgUrl",
        "state.shelf.userInfo.headImageUrl",
        "state.user.avatar",
        "state.user.avatarUrl",
        "state.user.avatarURI",
        "state.user.head",
        "state.user.headImgUrl",
        NULL
    };
    const char *nickname;
    const char *avatar_url;

    if (!profile) {
        return;
    }

    nickname = ui_user_profile_pick_string(shelf_nuxt, nickname_paths);
    avatar_url = ui_user_profile_pick_string(shelf_nuxt, avatar_paths);
    if (!nickname) {
        nickname = "";
    }
    if (!avatar_url) {
        avatar_url = "";
    }

    if (strcmp(profile->nickname, nickname) == 0 &&
        strcmp(profile->avatar_url, avatar_url) == 0) {
        if (!profile->avatar_path[0] && data_dir && *data_dir) {
            (void)snprintf(profile->avatar_path, sizeof(profile->avatar_path),
                           "%s/weread-user-avatar.img", data_dir);
        }
        return;
    }

    if (profile->avatar_texture) {
        SDL_DestroyTexture(profile->avatar_texture);
        profile->avatar_texture = NULL;
    }
    profile->avatar_w = 0;
    profile->avatar_h = 0;
    profile->avatar_attempted = 0;
    snprintf(profile->nickname, sizeof(profile->nickname), "%s", nickname);
    snprintf(profile->avatar_url, sizeof(profile->avatar_url), "%s", avatar_url);
    if (data_dir && *data_dir) {
        (void)snprintf(profile->avatar_path, sizeof(profile->avatar_path),
                       "%s/weread-user-avatar.img", data_dir);
    } else {
        profile->avatar_path[0] = '\0';
    }
}

static int ui_user_profile_prepare_avatar(ApiContext *ctx, UiUserProfile *profile) {
    Buffer buf = {0};
    const char *avatar_url;
    char normalized_url[1200];
    FILE *fp;
    size_t data_size = 0;
    size_t written;

    if (!ctx || !profile || profile->avatar_attempted) {
        return 0;
    }
    profile->avatar_attempted = 1;
    if (!profile->avatar_url[0] || !profile->avatar_path[0]) {
        return 0;
    }

    avatar_url = profile->avatar_url;
    if (strncmp(avatar_url, "//", 2) == 0) {
        snprintf(normalized_url, sizeof(normalized_url), "https:%s", avatar_url);
        avatar_url = normalized_url;
    }
    if (api_download(ctx, avatar_url, &buf) != 0 || !buf.data || buf.size == 0) {
        api_buffer_free(&buf);
        return -1;
    }

    fp = fopen(profile->avatar_path, "wb");
    if (!fp) {
        api_buffer_free(&buf);
        return -1;
    }
    data_size = buf.size;
    written = fwrite(buf.data, 1, data_size, fp);
    fclose(fp);
    api_buffer_free(&buf);
    return written == data_size ? 0 : -1;
}

void ui_user_profile_prepare_avatar_texture(ApiContext *ctx, SDL_Renderer *renderer,
                                            UiUserProfile *profile) {
    SDL_Surface *avatar_surface;

    if (!ctx || !renderer || !profile || profile->avatar_texture || !profile->avatar_path[0]) {
        return;
    }
    if (!profile->avatar_attempted) {
        (void)ui_user_profile_prepare_avatar(ctx, profile);
    }
    if (profile->avatar_texture || access(profile->avatar_path, F_OK) != 0) {
        return;
    }

    avatar_surface = load_image_stb(profile->avatar_path);
    if (!avatar_surface) {
        return;
    }
    profile->avatar_texture = SDL_CreateTextureFromSurface(renderer, avatar_surface);
    profile->avatar_w = avatar_surface->w;
    profile->avatar_h = avatar_surface->h;
    {
        void *pixels = avatar_surface->pixels;
        SDL_FreeSurface(avatar_surface);
        stbi_image_free(pixels);
    }
}

#endif
