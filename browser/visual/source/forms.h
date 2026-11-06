#pragma once
#include <litehtml.h>
#include <map>
#include <string>
namespace visual {
// Values live only for the current navigation and survive resource-driven relayout.
struct Forms {
    struct Value {
        std::string text;
        bool initialized = false;
    };
    std::map<unsigned, Value> values;
    unsigned next = 0;
    void clear() {
        values.clear();
        next = 0;
    }
    void relayout() { next = 0; }
};
class Control final : public litehtml::html_tag {
    Forms &forms;
    unsigned key;

  public:
    Control(const litehtml::document::ptr &doc, Forms &state, unsigned index);
    std::string type() const;
    std::string value() const;
    void value(const std::string &);
    bool editable() const;
    bool disabled() const;
    void parse_attributes() override;
    bool is_replaced() const override { return true; }
    void get_content_size(litehtml::size &, litehtml::pixel_t) override;
    std::shared_ptr<litehtml::render_item>
    create_render_item(const std::shared_ptr<litehtml::render_item> &) override;
    void draw(litehtml::uint_ptr, litehtml::pixel_t, litehtml::pixel_t, const litehtml::position *,
              const std::shared_ptr<litehtml::render_item> &) override;
};
const std::string &control_styles();
// GET-only submission. Unsupported methods/controls produce an explicit error.
std::string form_get(const litehtml::element::ptr &button, const std::string &base,
                     std::string &error);
} // namespace visual
