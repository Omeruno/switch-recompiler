#include "container.h"
#include <SDL_image.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <webp/decode.h>
namespace visual {
Container::Container(SDL_Renderer *r, Resources &res, std::string font)
    : renderer(r), resources(res), font_path(std::move(font)) {}
Container::~Container() { reset(); }
size_t Container::decoded_images() const {
    size_t count = 0;
    for (const auto &kv : images)
        if (kv.second.texture)
            ++count;
    return count;
}
size_t Container::failed_images() const {
    size_t count = 0;
    for (const auto &kv : images)
        if (!kv.second.texture)
            ++count;
    return count;
}
void Container::relayout(const litehtml::document::ptr &doc) {
    doc->render(viewport_width);
    ++layout_passes;
}
void Container::restyle(const litehtml::document::ptr &doc) {
    invalidate_text();
    doc->refresh_native_dom();
    ++style_passes;
    relayout(doc);
}
void Container::invalidate_text() {
    for (auto &kv : texts)
        SDL_DestroyTexture(kv.second.texture);
    texts.clear();
    text_bytes = 0;
}
namespace {
class SourceElement : public litehtml::html_tag {
  public:
    explicit SourceElement(const litehtml::document::ptr &doc) : html_tag(doc) {}
    void parse_attributes() override {
        if (std::string(get_tagName()) == "style") {
            std::string css;
            get_text(css);
            get_document()->add_stylesheet(css.c_str(), nullptr, get_attr("media"));
        }
    }
};
} // namespace
void Container::reset() {
    game_input = {};
    canvases = std::make_shared<CanvasBudget>();
    scroll_requested = false;
    style_passes = layout_passes = 0;
    focus({});
    for (auto &kv : images)
        if (kv.second.texture)
            SDL_DestroyTexture(kv.second.texture);
    for (auto &kv : texts)
        SDL_DestroyTexture(kv.second.texture);
    images.clear();
    texts.clear();
    for (auto &kv : gradients)
        SDL_DestroyTexture(kv.second);
    gradients.clear();
    gradient_bytes = 0;
    image_bytes = 0;
    text_bytes = 0;
    unsupported = 0;
    anchor.clear();
    clicked.reset();
    forms.relayout();
    clips.clear();
}
litehtml::element::ptr Container::focused() const {
    auto el = focused_node.lock();
    if (!el)
        return {};
    auto doc = el->get_document();
    if (!doc || form_disabled(el))
        return {};
    for (auto p = el; p; p = p->parent()) {
        if (p->css().get_display() == litehtml::display_none ||
            p->css().get_visibility() != litehtml::visibility_visible)
            return {};
        if (p == doc->root())
            return el;
    }
    return {};
}
bool Container::focus(const litehtml::element::ptr &el) {
    if (el) {
        if (form_disabled(el) || el->is_text())
            return false;
        auto control = std::dynamic_pointer_cast<Control>(el);
        std::string tag = el->get_tagName();
        if ((!control && tag != "button" && tag != "a" && tag != "canvas" &&
             !el->get_attr("tabindex")) ||
            (control && control->type() == "hidden"))
            return false;
        auto old = focused_node;
        focused_node = el;
        bool visible = bool(focused());
        focused_node = old;
        if (!visible)
            return false;
    }
    auto old = focused_node.lock();
    if (old == el)
        return false;
    for (auto node : {old, el}) {
        if (!node)
            continue;
        bool yes = node == el;
        node->set_pseudo_class(litehtml::_id("focus"), yes);
        for (auto p = node; p; p = p->parent())
            p->set_pseudo_class(litehtml::_id("focus-within"), false);
    }
    if (el)
        for (auto p = el; p; p = p->parent())
            p->set_pseudo_class(litehtml::_id("focus-within"), true);
    focused_node = el;
    return true;
}
bool Container::prune_focus() { return focused_node.lock() && !focused() ? focus({}) : false; }
SDL_Rect Container::screen(const litehtml::position &p) const {
    float x = float(p.x) * zoom, y = float(p.y) * zoom + toolbar;
    return {int(std::floor(x)), int(std::floor(y)), int(std::ceil(float(p.width) * zoom)),
            int(std::ceil(float(p.height) * zoom))};
}
void Container::color(litehtml::web_color c) {
    SDL_SetRenderDrawColor(renderer, c.red, c.green, c.blue, c.alpha);
}
void Container::apply_clip() {
    SDL_Rect r = {0, toolbar, width, height - toolbar};
    for (auto c : clips) {
        SDL_Rect out;
        SDL_IntersectRect(&r, &c, &out);
        r = out;
    }
    SDL_RenderSetClipRect(renderer, &r);
}
void Container::draw_canvas(SDL_Texture *texture, const litehtml::position &position) {
    auto target = screen(position);
    apply_clip();
    if (SDL_RenderCopy(renderer, texture, nullptr, &target) == 0)
        ++painted_canvases;
    else
        ++unsupported;
}
void Container::begin_paint() {
    painted_canvases = 0;
    painted_text = painted_images = painted_gradients = unsupported = 0;
    clips.clear();
    apply_clip();
}
void Container::end_paint() {
    clips.clear();
    SDL_RenderSetClipRect(renderer, nullptr);
}
void Container::fill(const litehtml::position &p, litehtml::web_color c) {
    if (!c.alpha)
        return;
    color(c);
    auto r = screen(p);
    SDL_RenderFillRect(renderer, &r);
}
litehtml::uint_ptr Container::create_font(const litehtml::font_description &d,
                                          const litehtml::document *, litehtml::font_metrics *m) {
    TTF_Font *f = TTF_OpenFont(font_path.c_str(), std::clamp(int(float(d.size)), 6, 144));
    if (!f)
        throw std::runtime_error(TTF_GetError());
    int style = TTF_STYLE_NORMAL;
    if (d.weight >= 600)
        style |= TTF_STYLE_BOLD;
    if (d.style == litehtml::font_style_italic)
        style |= TTF_STYLE_ITALIC;
    if (d.decoration_line & litehtml::text_decoration_line_underline)
        style |= TTF_STYLE_UNDERLINE;
    if (d.decoration_line & litehtml::text_decoration_line_line_through)
        style |= TTF_STYLE_STRIKETHROUGH;
    TTF_SetFontStyle(f, style);
    *m = {};
    m->font_size = d.size;
    m->height = TTF_FontHeight(f);
    m->ascent = TTF_FontAscent(f);
    m->descent = -TTF_FontDescent(f);
    int w = 0, h = 0;
    TTF_SizeUTF8(f, "x", &w, &h);
    m->x_height = h / 2;
    TTF_SizeUTF8(f, "0", &w, &h);
    m->ch_width = w;
    m->draw_spaces = false;
    m->sub_shift = int(float(d.size) / 3);
    m->super_shift = int(float(d.size) / 3);
    return reinterpret_cast<litehtml::uint_ptr>(f);
}
void Container::delete_font(litehtml::uint_ptr p) {
    TTF_CloseFont(reinterpret_cast<TTF_Font *>(p));
}
litehtml::pixel_t Container::text_width(const char *text, litehtml::uint_ptr p) {
    int w = 0, h = 0;
    if (text)
        TTF_SizeUTF8(reinterpret_cast<TTF_Font *>(p), text, &w, &h);
    return w;
}
void Container::draw_text(litehtml::uint_ptr, const char *text, litehtml::uint_ptr p,
                          litehtml::web_color c, const litehtml::position &pos) {
    if (!text || !*text)
        return;
    auto dst = screen(pos);
    if (dst.y + dst.h < toolbar || dst.y >= height || dst.x + dst.w < 0 || dst.x >= width)
        return;
    std::string key = std::to_string(p) + ":" + std::to_string(c.red) + "," +
                      std::to_string(c.green) + "," + std::to_string(c.blue) + "," +
                      std::to_string(c.alpha) + ":" + text;
    auto it = texts.find(key);
    if (it == texts.end()) {
        int measured_width = 0, measured_height = 0;
        if (TTF_SizeUTF8(reinterpret_cast<TTF_Font *>(p), text, &measured_width,
                         &measured_height) != 0 ||
            measured_width <= 0 || measured_height <= 0 || measured_width > 8192 ||
            measured_height > 512 ||
            size_t(measured_width) * size_t(measured_height) * 4 > 8u * 1024u * 1024u) {
            ++unsupported;
            return;
        }
        SDL_Surface *s = TTF_RenderUTF8_Blended(reinterpret_cast<TTF_Font *>(p), text,
                                                {c.red, c.green, c.blue, c.alpha});
        if (!s)
            return;
        size_t bytes = size_t(s->w) * size_t(s->h) * 4;
        if (bytes > 8u * 1024u * 1024u) {
            SDL_FreeSurface(s);
            return;
        }
        if (text_bytes + bytes > 8u * 1024u * 1024u || texts.size() >= 1024) {
            for (auto &kv : texts)
                SDL_DestroyTexture(kv.second.texture);
            texts.clear();
            text_bytes = 0;
        }
        SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);
        int w = s->w, h = s->h;
        SDL_FreeSurface(s);
        if (!t)
            return;
        it = texts.emplace(std::move(key), Text{t, w, h}).first;
        text_bytes += bytes;
    }
    dst.w = int(it->second.w * zoom);
    dst.h = int(it->second.h * zoom);
    SDL_RenderCopy(renderer, it->second.texture, nullptr, &dst);
    ++painted_text;
}
// Inspect dimensions before invoking a decoder, avoiding unbounded surface allocation.
static bool dimensions(const std::vector<unsigned char> &b, int &w, int &h) {
    auto be16 = [&](size_t n) { return int(b[n]) * 256 + b[n + 1]; };
    auto be32 = [&](size_t n) {
        return uint32_t(b[n]) << 24 | uint32_t(b[n + 1]) << 16 | uint32_t(b[n + 2]) << 8 | b[n + 3];
    };
    if (b.size() >= 12 && std::string(reinterpret_cast<const char *>(b.data()), 4) == "RIFF" &&
        std::string(reinterpret_cast<const char *>(b.data() + 8), 4) == "WEBP") {
        if (!WebPGetInfo(b.data(), b.size(), &w, &h))
            return false;
    } else if (b.size() >= 24 && b[0] == 137 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') {
        uint32_t x = be32(16), y = be32(20);
        if (x > 4096 || y > 4096)
            return false;
        w = int(x);
        h = int(y);
    } else if (b.size() > 4 && b[0] == 255 && b[1] == 216) {
        size_t p = 2;
        bool found = false;
        while (p + 4 <= b.size()) {
            if (b[p++] != 255)
                return false;
            while (p < b.size() && b[p] == 255)
                ++p;
            if (p >= b.size())
                return false;
            unsigned marker = b[p++];
            if (marker == 0xd9 || marker == 0xda)
                break;
            if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
                continue;
            if (p + 2 > b.size())
                break;
            size_t len = be16(p);
            if (len < 2 || len > b.size() - p)
                break;
            if ((marker >= 0xc0 && marker <= 0xc3) || (marker >= 0xc5 && marker <= 0xc7) ||
                (marker >= 0xc9 && marker <= 0xcb) || (marker >= 0xcd && marker <= 0xcf)) {
                if (len < 8)
                    return false;
                h = be16(p + 3);
                w = be16(p + 5);
                found = true;
                break;
            }
            p += len;
        }
        if (!found)
            return false;
    } else
        return false;
    return w > 0 && h > 0 && w <= 4096 && h <= 4096 && size_t(w) * size_t(h) <= 4u * 1024u * 1024u;
}
Container::Image *Container::image(const char *src, const char *baseurl) {
    std::string url = Resources::resolve(baseurl && *baseurl ? baseurl : base, src ? src : "");
    auto old = images.find(url);
    if (old != images.end())
        return &old->second;
    Resource *r = resources.request(url, 8u * 1024u * 1024u);
    if (r)
        r->image = true;
    if (!r || r->state != Resource::ready)
        return nullptr;
    Image out;
    if (dimensions(r->data, out.w, out.h) &&
        image_bytes + size_t(out.w) * out.h * 4 <= 32u * 1024u * 1024u) {
        SDL_Surface *s = nullptr;
        if (r->data.size() >= 12 &&
            std::string(reinterpret_cast<const char *>(r->data.data()), 4) == "RIFF") {
            s = SDL_CreateRGBSurfaceWithFormat(0, out.w, out.h, 32, SDL_PIXELFORMAT_RGBA32);
            if (s && !WebPDecodeRGBAInto(r->data.data(), r->data.size(),
                                         static_cast<uint8_t *>(s->pixels), size_t(s->pitch) * s->h,
                                         s->pitch)) {
                SDL_FreeSurface(s);
                s = nullptr;
            }
        } else
            s = IMG_Load_RW(SDL_RWFromConstMem(r->data.data(), int(r->data.size())), 1);
        if (s) {
            if (s->w == out.w && s->h == out.h)
                out.texture = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FreeSurface(s);
        }
    }
    if (out.texture)
        image_bytes += size_t(out.w) * out.h * 4;
    else {
        out.w = out.h = 0;
        ++unsupported;
    }
    return &images.emplace(url, out).first->second;
}
void Container::load_image(const char *s, const char *b, bool) { image(s, b); }
void Container::get_image_size(const char *s, const char *b, litehtml::size &sz) {
    auto *im = image(s, b);
    sz.width = im ? im->w : 0;
    sz.height = im ? im->h : 0;
}
void Container::draw_image(litehtml::uint_ptr, const litehtml::background_layer &l,
                           const std::string &url, const std::string &baseurl) {
    auto *im = image(url.c_str(), baseurl.c_str());
    if (!im || !im->texture)
        return;
    paint_tile(im->texture, l);
    ++painted_images;
}
void Container::paint_tile(SDL_Texture *texture, const litehtml::background_layer &l) {
    auto origin = screen(l.origin_box), clip = screen(l.clip_box), border = screen(l.border_box);
    SDL_Rect previous;
    SDL_RenderGetClipRect(renderer, &previous);
    SDL_Rect out;
    SDL_IntersectRect(&previous, &clip, &out);
    SDL_IntersectRect(&out, &border, &clip);
    SDL_RenderSetClipRect(renderer, &clip);
    if (origin.w > 0 && origin.h > 0 && clip.w > 0 && clip.h > 0) {
        bool rx = l.repeat == litehtml::background_repeat_repeat ||
                  l.repeat == litehtml::background_repeat_repeat_x;
        bool ry = l.repeat == litehtml::background_repeat_repeat ||
                  l.repeat == litehtml::background_repeat_repeat_y;
        int x0 = origin.x, y0 = origin.y;
        if (rx)
            x0 = clip.x - ((clip.x - origin.x) % origin.w + origin.w) % origin.w;
        if (ry)
            y0 = clip.y - ((clip.y - origin.y) % origin.h + origin.h) % origin.h;
        unsigned tiles = 0;
        for (int y = y0; y < (ry ? clip.y + clip.h : y0 + 1) && tiles < 4096; y += origin.h)
            for (int x = x0; x < (rx ? clip.x + clip.w : x0 + 1) && tiles < 4096; x += origin.w) {
                SDL_Rect d = {x, y, origin.w, origin.h};
                SDL_RenderCopy(renderer, texture, nullptr, &d);
                ++tiles;
            }
    }
    SDL_RenderSetClipRect(renderer, &previous);
}
void Container::draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer &l,
                                const litehtml::web_color &c) {
    auto r = screen(l.border_box), cl = screen(l.clip_box);
    SDL_Rect old, clip;
    SDL_RenderGetClipRect(renderer, &old);
    SDL_IntersectRect(&old, &cl, &clip);
    SDL_RenderSetClipRect(renderer, &clip);
    float radii[8] = {
        float(l.border_radius.top_left_x) * zoom,     float(l.border_radius.top_left_y) * zoom,
        float(l.border_radius.top_right_x) * zoom,    float(l.border_radius.top_right_y) * zoom,
        float(l.border_radius.bottom_right_x) * zoom, float(l.border_radius.bottom_right_y) * zoom,
        float(l.border_radius.bottom_left_x) * zoom,  float(l.border_radius.bottom_left_y) * zoom};
    color(c);
    for (int y = std::max(r.y, clip.y); y < std::min(r.y + r.h, clip.y + clip.h); ++y) {
        float left = 0, right = 0;
        auto inset = [](float rx, float ry, float distance) {
            return rx > 0 && ry > 0 && distance < ry
                       ? rx *
                             (1 - std::sqrt(std::max(0.f, 1 - std::pow((ry - distance) / ry, 2.f))))
                       : 0.f;
        };
        left = std::max(inset(radii[0], radii[1], y - r.y + .5f),
                        inset(radii[6], radii[7], r.y + r.h - y - .5f));
        right = std::max(inset(radii[2], radii[3], y - r.y + .5f),
                         inset(radii[4], radii[5], r.y + r.h - y - .5f));
        SDL_RenderDrawLine(renderer, r.x + int(std::ceil(left)), y,
                           r.x + r.w - 1 - int(std::ceil(right)), y);
    }
    SDL_RenderSetClipRect(renderer, &old);
}
void Container::draw_borders(litehtml::uint_ptr, const litehtml::borders &b,
                             const litehtml::position &p, bool) {
    auto side = [&](const litehtml::border &s, litehtml::position rect) {
        if (s.style == litehtml::border_style_none || s.style == litehtml::border_style_hidden)
            return;
        if (s.style != litehtml::border_style_solid)
            ++unsupported;
        fill(rect, s.color);
    };
    side(b.top, {p.x, p.y, p.width, b.top.width});
    side(b.bottom, {p.x, p.y + p.height - b.bottom.width, p.width, b.bottom.width});
    side(b.left, {p.x, p.y, b.left.width, p.height});
    side(b.right, {p.x + p.width - b.right.width, p.y, b.right.width, p.height});
}
void Container::draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker &m) {
    fill(m.pos, m.color);
}
void Container::set_base_url(const char *s) {
    if (s && *s) {
        auto u = Resources::resolve(base, s);
        if (!u.empty())
            base = u;
    }
}
void Container::on_anchor_click(const char *s, const litehtml::element::ptr &) {
    anchor = Resources::resolve(base, s ? s : "");
}
void Container::transform_text(std::string &s, litehtml::text_transform t) {
    // ASCII case mapping only; leave UTF-8 multibyte characters intact.
    bool word = true;
    for (char &c : s) {
        unsigned char u = c;
        if (u >= 128)
            continue;
        if (t == litehtml::text_transform_uppercase ||
            (t == litehtml::text_transform_capitalize && word))
            c = char(std::toupper(u));
        else if (t == litehtml::text_transform_lowercase)
            c = char(std::tolower(u));
        word = std::isspace(u);
    }
}
void Container::import_css(std::string &text, const std::string &url, std::string &baseurl) {
    auto resolved = Resources::resolve(baseurl.empty() ? base : baseurl, url);
    auto *r = resources.request(resolved, 512u * 1024u);
    if (r) {
        r->stylesheet = true;
        r->critical = true;
    }
    if (r && r->state == Resource::ready) {
        text.assign(r->data.begin(), r->data.end());
        baseurl = r->effective;
    }
}
void Container::set_clip(const litehtml::position &p, const litehtml::border_radiuses &) {
    clips.push_back(screen(p));
    apply_clip();
}
void Container::del_clip() {
    if (!clips.empty())
        clips.pop_back();
    apply_clip();
}
void Container::get_viewport(litehtml::position &p) const {
    p = {0, 0, viewport_width, viewport_height};
}
void Container::get_media_features(litehtml::media_features &m) const {
    m = {};
    m.type = litehtml::media_type_screen;
    m.width = viewport_width;
    m.height = viewport_height;
    m.device_width = viewport_width;
    m.device_height = float(height) * viewport_width / width;
    m.color = 8;
    m.resolution = 96.f * width / viewport_width;
}
litehtml::element::ptr Container::create_element(const char *tag, const litehtml::string_map &,
                                                 const std::shared_ptr<litehtml::document> &doc) {
    if (std::string(tag) == "canvas")
        return std::make_shared<GameCanvas>(doc, renderer, canvases);
    if (std::string(tag) == "script" || std::string(tag) == "style" ||
        std::string(tag) == "noscript")
        return std::make_shared<SourceElement>(doc);
    if (std::string(tag) == "option")
        return std::make_shared<Option>(doc);
    if (std::string(tag) == "input" || std::string(tag) == "textarea" ||
        std::string(tag) == "select")
        return std::make_shared<Control>(doc, forms, forms.next++);
    return {};
}
bool Container::on_element_click(const litehtml::element::ptr &el) {
    std::string tag = el->get_tagName();
    if (tag == "input" || tag == "textarea" || tag == "button" || tag == "select" ||
        tag == "label") {
        clicked = el;
        return true;
    }
    return false;
}
} // namespace visual
