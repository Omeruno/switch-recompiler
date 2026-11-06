#pragma once
#include "forms.h"
#include "resources.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <litehtml.h>
#include <map>
#include <string>
#include <vector>
namespace visual {
class Container final : public litehtml::document_container {
    SDL_Renderer *renderer;
    Resources &resources;
    std::string font_path;
    struct Image {
        SDL_Texture *texture = nullptr;
        int w = 0, h = 0;
    };
    std::map<std::string, Image> images;
    struct Text {
        SDL_Texture *texture;
        int w, h;
    };
    std::map<std::string, Text> texts;
    size_t image_bytes = 0, text_bytes = 0;
    std::vector<SDL_Rect> clips;
    std::map<std::string, SDL_Texture *> gradients;
    size_t gradient_bytes = 0;
    void paint_tile(SDL_Texture *, const litehtml::background_layer &);
    void paint_gradient(const litehtml::background_layer &,
                        const litehtml::background_layer::gradient_base &, const std::string &,
                        const std::function<float(float, float)> &);
    SDL_Rect screen(const litehtml::position &) const;
    void color(litehtml::web_color);
    void apply_clip();
    Image *image(const char *, const char *);
    void fill(const litehtml::position &, litehtml::web_color);

  public:
    static constexpr int width = 1280, height = 720, toolbar = 84;
    Forms forms;
    litehtml::element::ptr clicked;
    int viewport_width = 1280;
    float viewport_height = 636;
    float zoom = 1, scroll_x = 0, scroll_y = 0;
    std::string page_url = "demo:/index.html", form_notice;
    std::string base = "demo:/index.html", caption, anchor, cursor;
    unsigned unsupported = 0, painted_text = 0, painted_images = 0, painted_gradients = 0;
    Container(SDL_Renderer *, Resources &, std::string font);
    ~Container() override;
    size_t decoded_images() const;
    void reset();
    void invalidate_text();
    void begin_paint();
    void end_paint();
    litehtml::uint_ptr create_font(const litehtml::font_description &, const litehtml::document *,
                                   litehtml::font_metrics *) override;
    void delete_font(litehtml::uint_ptr) override;
    litehtml::pixel_t text_width(const char *, litehtml::uint_ptr) override;
    void draw_text(litehtml::uint_ptr, const char *, litehtml::uint_ptr, litehtml::web_color,
                   const litehtml::position &) override;
    litehtml::pixel_t pt_to_px(float pt) const override { return pt * 96.f / 72.f; }
    litehtml::pixel_t get_default_font_size() const override { return 18; }
    const char *get_default_font_name() const override { return "DejaVu Sans"; }
    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker &) override;
    void load_image(const char *, const char *, bool) override;
    void get_image_size(const char *, const char *, litehtml::size &) override;
    void draw_image(litehtml::uint_ptr, const litehtml::background_layer &, const std::string &,
                    const std::string &) override;
    void draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer &,
                         const litehtml::web_color &) override;
    void draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer &,
                              const litehtml::background_layer::linear_gradient &) override;
    void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer &,
                              const litehtml::background_layer::radial_gradient &) override;
    void draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer &,
                             const litehtml::background_layer::conic_gradient &) override;
    void draw_borders(litehtml::uint_ptr, const litehtml::borders &, const litehtml::position &,
                      bool) override;
    void set_caption(const char *s) override { caption = s ? s : ""; }
    void set_base_url(const char *s) override;
    void link(const std::shared_ptr<litehtml::document> &,
              const litehtml::element::ptr &) override {}
    void on_anchor_click(const char *, const litehtml::element::ptr &) override;
    void on_mouse_event(const litehtml::element::ptr &, litehtml::mouse_event) override {}
    void set_cursor(const char *s) override { cursor = s ? s : ""; }
    void transform_text(std::string &, litehtml::text_transform) override;
    void import_css(std::string &, const std::string &, std::string &) override;
    void set_clip(const litehtml::position &, const litehtml::border_radiuses &) override;
    void del_clip() override;
    void get_viewport(litehtml::position &) const override;
    litehtml::element::ptr create_element(const char *, const litehtml::string_map &,
                                          const std::shared_ptr<litehtml::document> &) override;
    bool on_element_click(const litehtml::element::ptr &) override;
    void get_media_features(litehtml::media_features &) const override;
    void get_language(std::string &language, std::string &culture) const override {
        language = "ru";
        culture = "RU";
    }
};
} // namespace visual
