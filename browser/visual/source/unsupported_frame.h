#pragma once
#include <litehtml.h>
namespace visual {
class UnsupportedFrame final : public litehtml::html_tag {
  public:
    explicit UnsupportedFrame(const litehtml::document::ptr &d) : html_tag(d) {}
    bool is_replaced() const override { return true; }
    void get_content_size(litehtml::size &, litehtml::pixel_t) override;
    std::shared_ptr<litehtml::render_item>
    create_render_item(const std::shared_ptr<litehtml::render_item> &) override;
    void draw(litehtml::uint_ptr, litehtml::pixel_t, litehtml::pixel_t, const litehtml::position *,
              const std::shared_ptr<litehtml::render_item> &) override;
};
} // namespace visual
