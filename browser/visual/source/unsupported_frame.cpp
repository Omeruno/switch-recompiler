#include "unsupported_frame.h"
#include "container.h"
#include <algorithm>
#include <cstdlib>
#include <litehtml/render_image.h>
namespace visual {
void UnsupportedFrame::get_content_size(litehtml::size &s, litehtml::pixel_t) {
    auto dimension = [&](const char *key, int fallback) {
        const char *v = get_attr(key);
        if (!v || !*v)
            return fallback;
        char *end = nullptr;
        auto n = strtol(v, &end, 10);
        return end == v || n < 0 ? fallback : int(std::min(n, 2048L));
    };
    s.width = dimension("width", 300);
    s.height = dimension("height", 150);
}
std::shared_ptr<litehtml::render_item>
UnsupportedFrame::create_render_item(const std::shared_ptr<litehtml::render_item> &parent) {
    auto item = std::make_shared<litehtml::render_item_image>(shared_from_this());
    item->parent(parent);
    return item;
}
void UnsupportedFrame::draw(litehtml::uint_ptr hdc, litehtml::pixel_t x, litehtml::pixel_t y,
                            const litehtml::position *clip,
                            const std::shared_ptr<litehtml::render_item> &item) {
    html_tag::draw(hdc, x, y, clip, item);
    auto p = item->pos();
    p.x += x;
    p.y += y;
    if (!p.does_intersect(clip) || float(p.width) < 120 || float(p.height) < 40)
        return;
    auto *c = dynamic_cast<Container *>(get_document()->container());
    if (!c)
        return;
    ++c->painted_frames;
    c->set_clip(p, {});
    const char *lines[] = {"Встроенный документ", "пока не поддерживается.",
                           "Проверка сайта может не работать."};
    float lineheight = std::max(14.f, float(css().get_font_size()) * 1.3f);
    auto line = p;
    line.x += 4;
    line.y += 4;
    line.width = std::max(0.f, float(p.width) - 8);
    for (auto text : lines) {
        if (float(line.y) + lineheight > float(p.bottom()))
            break;
        std::string fitted = text;
        while (!fitted.empty() && c->text_width(fitted.c_str(), css().get_font()) > line.width) {
            fitted.pop_back();
            while (!fitted.empty() && (static_cast<unsigned char>(fitted.back()) & 0xc0) == 0x80)
                fitted.pop_back();
            if (!fitted.empty() && static_cast<unsigned char>(fitted.back()) >= 0xc0)
                fitted.pop_back();
        }
        c->draw_text(hdc, fitted.c_str(), css().get_font(), {120, 65, 25}, line);
        line.y += lineheight;
    }
    c->del_clip();
}
} // namespace visual
