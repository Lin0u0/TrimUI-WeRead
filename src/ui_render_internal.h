#ifndef UI_RENDER_INTERNAL_H
#define UI_RENDER_INTERNAL_H

#include "ui_internal.h"

#if HAVE_SDL

typedef struct {
    SDL_Color ink;
    SDL_Color muted;
    SDL_Color accent;
    SDL_Color line;
    SDL_Color dim;
    Uint8 bg_r, bg_g, bg_b;
    Uint8 header_r, header_g, header_b;
    Uint8 card_r, card_g, card_b;
    Uint8 qr_slot_r, qr_slot_g, qr_slot_b;
    Uint8 cover_bg_r, cover_bg_g, cover_bg_b;
    Uint8 cover_empty_r, cover_empty_g, cover_empty_b;
    Uint8 shadow_r, shadow_g, shadow_b, shadow_a;
    Uint8 selection_border_r, selection_border_g, selection_border_b;
    Uint8 catalog_panel_r, catalog_panel_g, catalog_panel_b;
    Uint8 catalog_header_r, catalog_header_g, catalog_header_b;
    Uint8 catalog_highlight_r, catalog_highlight_g, catalog_highlight_b;
    Uint8 catalog_current_r, catalog_current_g, catalog_current_b;
    Uint8 backdrop_r, backdrop_g, backdrop_b, backdrop_a;
} UiTheme;

const UiTheme *ui_current_theme(void);
int reader_top_inset_for_font_size(int font_size);
void draw_text(SDL_Renderer *renderer, TTF_Font *font, int x, int y,
               SDL_Color color, const char *text);
void draw_rect_outline(SDL_Renderer *renderer, const SDL_Rect *rect,
                       SDL_Color color, int thickness);
void fit_text_ellipsis(TTF_Font *font, const char *text, int max_width,
                       char *out, size_t out_size);
void ui_draw_texture_cover(SDL_Renderer *renderer, SDL_Texture *texture,
                           int tex_w, int tex_h, const SDL_Rect *dst);
void ui_draw_texture_contain(SDL_Renderer *renderer, SDL_Texture *texture,
                             int tex_w, int tex_h, const SDL_Rect *dst);
void ui_user_profile_placeholder_text(const UiUserProfile *profile,
                                      char *out, size_t out_size);
void render_header_status(SDL_Renderer *renderer, TTF_Font *body_font,
                          const char *text, const UiLayout *layout);

#endif

#endif
