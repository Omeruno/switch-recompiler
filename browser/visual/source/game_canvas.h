#pragma once
#include <SDL.h>
#include <litehtml.h>
#include <memory>
#include <vector>
namespace visual {
struct CanvasBudget {
    size_t bytes = 0;
    unsigned count = 0;
    uint64_t frames = 0;
};
class GameCanvas final : public litehtml::html_tag {
    SDL_Renderer *renderer;
    SDL_Texture *texture = nullptr;
    std::shared_ptr<CanvasBudget> budget;
    unsigned w = 300, h = 150;
    bool dirty = true, initialized = false;
    std::vector<uint8_t> pixels;
    std::string width_attribute, height_attribute;
    void blend(unsigned, unsigned, const uint8_t *, double);

  public:
    static constexpr unsigned axis_limit = 2048;
    GameCanvas(const std::shared_ptr<litehtml::document> &, SDL_Renderer *,
               std::shared_ptr<CanvasBudget>);
    ~GameCanvas() override;
    void resize(unsigned, unsigned);
    void ensure();
    unsigned width() {
        ensure();
        return w;
    }
    unsigned height() {
        ensure();
        return h;
    }
    void fill(double, double, double, double, uint32_t, double, bool clear = false);
    void put(const uint8_t *, unsigned, unsigned, int, int, int, int, int, int);
    std::vector<uint8_t> get(int, int, unsigned, unsigned);
    void image(GameCanvas &, double, double, double, double, double, double, double, double, double,
               bool);
    void read_attributes(bool force);
    void parse_attributes() override { read_attributes(false); }
    bool is_replaced() const override { return true; }
    void get_content_size(litehtml::size &, litehtml::pixel_t) override;
    std::shared_ptr<litehtml::render_item>
    create_render_item(const std::shared_ptr<litehtml::render_item> &) override;
    void draw(litehtml::uint_ptr, litehtml::pixel_t, litehtml::pixel_t, const litehtml::position *,
              const std::shared_ptr<litehtml::render_item> &) override;
};
} // namespace visual
