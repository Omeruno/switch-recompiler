#include "unsupported_frame.h"
#include "container.h"
#include "document_source.h"
#include "frame_policy.h"
#include "scripts.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <litehtml/render_image.h>
#include <sstream>
namespace visual {
struct UnsupportedFrame::Impl {
    UnsupportedFrame &owner;
    Container &parent;
    std::shared_ptr<MessageHub> hub;
    uint32_t id = 0;
    std::string source, sandbox, source_document, notice = "Встроенный документ: адрес не задан.";
    bool attempted = false, announced = false, scripts_allowed = true, forms_allowed = true;
    bool sandbox_present = false, source_document_present = false;
    SDL_Surface *surface = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    int width = 0, height = 0;
    size_t pixels = 0;
    std::unique_ptr<Resources> resources;
    std::unique_ptr<Container> container;
    litehtml::document::ptr document;
    std::unique_ptr<Scripts> scripts;
    Resource *root = nullptr;
    unsigned style_revision = 0, image_revision = 0;
    uint64_t last_paint = 0, source_revision = 0, observed_revision = 0;
    explicit Impl(UnsupportedFrame &o)
        : owner(o), parent(*dynamic_cast<Container *>(o.get_document()->container())) {}
    bool connected() const {
        auto n = owner.parent();
        for (unsigned depth = 0; n && depth < 512; ++depth, n = n->parent())
            if (n == owner.get_document()->root())
                return true;
        return false;
    }
    uint32_t reserve() {
        if (!connected() || !parent.messages || !parent.message_window)
            return 0;
        if (id && hub && hub->window(id) && hub->window(id)->alive)
            return id;
        if (parent.frame_depth >= 2)
            throw std::runtime_error("Frame depth quota");
        hub = parent.messages;
        id = hub->create_window(parent.message_window);
        return id;
    }
    void clear_page() {
        scripts.reset();
        document.reset();
        container.reset();
        resources.reset();
        root = nullptr;
        if (id && hub && hub->window(id) && hub->window(id)->alive)
            hub->navigate(id);
        if (texture)
            SDL_DestroyTexture(texture);
        texture = nullptr;
        if (renderer)
            SDL_DestroyRenderer(renderer);
        renderer = nullptr;
        if (surface)
            SDL_FreeSurface(surface);
        surface = nullptr;
        if (hub)
            hub->frame_pixels -= pixels;
        pixels = 0;
        width = height = 0;
        announced = false;
    }
    ~Impl() {
        clear_page();
        if (id && hub)
            hub->close_window(id);
    }
    void fail(const std::string &s) {
        clear_page();
        attempted = true;
        notice = s;
    }
    bool sandbox_ok() {
        scripts_allowed = forms_allowed = true;
        const char *value = owner.get_attr("sandbox");
        if (!value)
            return true;
        std::istringstream tokens(value);
        std::string token;
        bool same = false;
        scripts_allowed = forms_allowed = false;
        while (tokens >> token) {
            same |= token == "allow-same-origin";
            scripts_allowed |= token == "allow-scripts";
            forms_allowed |= token == "allow-forms";
        }
        // Opaque-origin child realms require a separate origin/storage design.
        return same;
    }
    bool permitted(const std::string &url) {
        if (parent.page_url == "demo:/index.html" && url == "demo:/frame-demo.html")
            return true;
        return parent.script_runtime && parent.script_runtime->allows_frame(url) &&
               Resources::resolve("", url).rfind("https://", 0) == 0;
    }
    void navigate(const std::string &url) {
        clear_page();
        attempted = true;
        notice = "Загрузка встроенной страницы…";
        if (!reserve() || !sandbox_ok() || owner.get_attr("srcdoc") || !permitted(url)) {
            notice = "Встраивание запрещено или не поддерживается.";
            return;
        }
        resources = std::make_unique<Resources>(parent.loader().asset_root());
        resources->anonymous_context = true;
        root = resources->navigate_frame(url);
        if (!root)
            fail("Не удалось начать загрузку фрейма.");
    }
    static int dimension(litehtml::pixel_t value, int fallback) {
        float n = value;
        if (!std::isfinite(n) || n < 0 || n > 1536)
            throw std::length_error("Frame viewport quota");
        return n > 0 ? std::max(1, int(n)) : fallback;
    }
    bool target(int w, int h) {
        w = std::clamp(w, 1, 1536);
        h = std::clamp(h, 1, 1536);
        if (w == width && h == height && renderer)
            return true;
        const int raster_w = std::max(w, 640), raster_h = std::max(h, 512);
        const size_t next = size_t(raster_w) * raster_h;
        if (!hub || next > 2u * 1024u * 1024u - (hub->frame_pixels - pixels))
            return false;
        auto *next_surface =
            SDL_CreateRGBSurfaceWithFormat(0, raster_w, raster_h, 32, SDL_PIXELFORMAT_RGBA32);
        auto *next_renderer = next_surface ? SDL_CreateSoftwareRenderer(next_surface) : nullptr;
        auto *next_texture =
            next_renderer ? SDL_CreateTexture(parent.drawing_renderer(), SDL_PIXELFORMAT_RGBA32,
                                              SDL_TEXTUREACCESS_STREAMING, raster_w, raster_h)
                          : nullptr;
        if (!next_texture) {
            if (next_renderer)
                SDL_DestroyRenderer(next_renderer);
            if (next_surface)
                SDL_FreeSurface(next_surface);
            return false;
        }
        // Container caches belong to its renderer; only recreate targets before document creation.
        if (container) {
            SDL_DestroyTexture(next_texture);
            SDL_DestroyRenderer(next_renderer);
            SDL_FreeSurface(next_surface);
            return false;
        }
        if (texture)
            SDL_DestroyTexture(texture);
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (surface)
            SDL_FreeSurface(surface);
        texture = next_texture;
        renderer = next_renderer;
        surface = next_surface;
        hub->frame_pixels = hub->frame_pixels - pixels + next;
        pixels = next;
        width = w;
        height = h;
        return true;
    }
    void poll() {
        if (!connected()) {
            clear_page();
            if (id && hub)
                hub->close_window(id);
            id = 0;
            attempted = false;
            source.clear();
            return;
        }
        const std::string next = owner.get_attr("src", "");
        const std::string next_sandbox = owner.get_attr("sandbox", "");
        const std::string next_document = owner.get_attr("srcdoc", "");
        const bool next_sandbox_present = owner.get_attr("sandbox") != nullptr,
                   next_document_present = owner.get_attr("srcdoc") != nullptr;
        if (next != source || next_sandbox != sandbox || next_document != source_document ||
            next_sandbox_present != sandbox_present ||
            next_document_present != source_document_present ||
            source_revision != observed_revision) {
            observed_revision = source_revision;
            source = next;
            sandbox = next_sandbox;
            source_document = next_document;
            sandbox_present = next_sandbox_present;
            source_document_present = next_document_present;
            attempted = false;
            clear_page();
        }
        if (owner.get_attr("srcdoc")) {
            if (!attempted)
                fail("srcdoc пока не поддерживается.");
            return;
        }
        if (!attempted) {
            if (std::string(owner.get_attr("src", "..")).empty() || !owner.get_attr("src"))
                return;
            navigate(Resources::resolve(parent.base, source));
        }
        if (!resources || !root)
            return;
        if (!permitted(root->current_url.empty() ? root->url : root->current_url)) {
            fail("Фрейм заблокирован политикой страницы.");
            return;
        }
        resources->pump();
        if (root->state == Resource::checking) {
            resources->follow(*root, permitted(root->pending_url));
            return;
        }
        if (root->state == Resource::queued || root->state == Resource::loading)
            return;
        if (!document) {
            if (!root->transfer_ok || root->policy_blocked ||
                !frame_response_allowed(*root, hub->ancestors(id))) {
                fail("Сайт запретил встраивание или загрузка не удалась.");
                return;
            }
            auto item = owner.get_render_item();
            auto box = item ? item->pos() : litehtml::position{};
            int w = dimension(box.width, 300), h = dimension(box.height, 150);
            if (w > 1536 || h > 1536 || !target(w, h)) {
                fail("Превышен лимит размера фрейма.");
                return;
            }
            container = std::make_unique<Container>(renderer, *resources, parent.font_file());
            container->frame_context = true;
            container->frame_depth = parent.frame_depth + 1;
            container->messages = hub;
            container->message_window = id;
            container->paint_width = container->viewport_width = width;
            container->paint_height = height;
            container->viewport_height = height;
            container->paint_toolbar = 0;
            container->page_url = container->base = root->effective;
            auto html = document_source(*root);
            if (std::count(html.begin(), html.end(), '<') > 8192) {
                fail("Превышен лимит сложности фрейма.");
                return;
            }
            document = litehtml::document::createFromString(
                html, container.get(), litehtml::master_css + control_styles());
            if (!document) {
                fail("Ошибка HTML во фрейме.");
                return;
            }
            container->relayout(document);
            style_revision = resources->style_revision;
            image_revision = resources->image_revision;
            notice.clear();
        }
        if (style_revision != resources->style_revision) {
            container->restyle(document);
            style_revision = resources->style_revision;
        } else if (image_revision != resources->image_revision) {
            container->relayout(document);
            image_revision = resources->image_revision;
        }
        if (!scripts && scripts_allowed && !resources->pending_styles())
            scripts = std::make_unique<Scripts>(document, *container, *resources, root->policies,
                                                hub, id, true);
        if (scripts) {
            const auto now = Scripts::clock_ms();
            scripts->poll(now);
            scripts->render_frame(now);
            if (scripts->changed())
                scripts->flush();
            if (scripts->submission()) {
                notice = "Отправка формы во фрейме пока не поддерживается.";
            }
            auto next = scripts->navigation();
            if (scripts->reload_requested())
                next = container->page_url;
            if (!container->anchor.empty()) {
                next = container->anchor;
                container->anchor.clear();
            }
            if (!next.empty()) {
                navigate(next);
                return;
            }
        }
        if (!scripts && !container->anchor.empty()) {
            auto next = container->anchor;
            container->anchor.clear();
            navigate(next);
            return;
        }
        if (!scripts_allowed) {
            // Bind a real origin even when sandbox prohibits JS.
            auto *w = hub->window(id);
            if (w && !w->container)
                hub->bind(id, container.get(),
                          root->effective.substr(0, root->effective.find('/', 8)));
        }
        auto item = owner.get_render_item();
        auto box = item ? item->pos() : litehtml::position{};
        int next_w = dimension(box.width, 1), next_h = dimension(box.height, 1);
        if (next_w > 1536 || next_h > 1536) {
            fail("Превышен лимит размера фрейма.");
            return;
        }
        if (next_w != width || next_h != height) {
            width = next_w;
            height = next_h;
            container->viewport_width = container->paint_width = width;
            container->viewport_height = container->paint_height = height;
            container->restyle(document);
        }
        SDL_RenderSetScale(renderer, float(surface->w) / width, float(surface->h) / height);
        container->poll_frames();
        container->scroll_y =
            std::clamp(container->scroll_y, 0.f, std::max(0.f, float(document->height()) - height));
        container->scroll_x =
            std::clamp(container->scroll_x, 0.f, std::max(0.f, float(document->width()) - width));
        if (!announced && (!scripts || scripts->ready()) && !resources->pending_load()) {
            announced = true;
            if (parent.script_runtime)
                parent.script_runtime->dispatch(owner.shared_from_this(), "load", false);
        }
        auto placement = client_box();
        if (float(placement.bottom()) <= 0 || float(placement.y) >= parent.viewport_height ||
            float(placement.right()) <= 0 || float(placement.x) >= parent.viewport_width)
            return;
        const auto paint_time = Scripts::clock_ms();
        if (paint_time < last_paint + 33 && last_paint)
            return;
        last_paint = paint_time;
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        container->begin_paint();
        litehtml::position clip{0, 0, width, height};
        document->draw(0, -container->scroll_x, -container->scroll_y, &clip);
        container->end_paint();
        SDL_RenderPresent(renderer);
        SDL_UpdateTexture(texture, nullptr, surface->pixels, surface->pitch);
    }
    litehtml::position client_box() const {
        auto ri = owner.get_render_item();
        if (!ri)
            return {};
        auto box = ri->get_placement();
        bool fixed = false;
        for (auto n = owner.shared_from_this(); n; n = n->parent())
            fixed |= n->css().get_position() == litehtml::element_position_fixed;
        if (!fixed) {
            box.x -= parent.scroll_x;
            box.y -= parent.scroll_y;
        }
        return box;
    }
    bool click(double x, double y) {
        if (!document || !container)
            return false;
        auto ri = owner.get_render_item();
        if (!ri)
            return false;
        auto box = client_box();
        x -= float(box.x);
        y -= float(box.y);
        if (x < 0 || y < 0 || x >= width || y >= height)
            return false;
        auto redraw = [](const litehtml::position &) {};
        document->on_mouse_over(float(x) + container->scroll_x, float(y) + container->scroll_y,
                                float(x), float(y), redraw);
        auto hit = std::const_pointer_cast<litehtml::element>(document->get_over_element());
        if (container->frame_click(hit, x, y))
            return true;
        auto control = std::dynamic_pointer_cast<Control>(hit);
        if (control && control->disabled())
            return true;
        if (scripts && hit) {
            scripts->mouse(hit, "mousedown", x, y);
            scripts->mouse(hit, "mouseup", x, y);
        }
        bool toggled = false, previous = false;
        std::vector<std::pair<std::shared_ptr<Control>, Control::CheckedState>> saved;
        if (control && !control->disabled() &&
            (control->type() == "checkbox" || control->type() == "radio")) {
            auto form = form_owner(control);
            std::string name = control->get_attr("name", "");
            if (control->type() == "radio" && !name.empty()) {
                for (auto &el : document->root()->select_all("input"))
                    if (auto peer = std::dynamic_pointer_cast<Control>(el))
                        if (peer->type() == "radio" && name == peer->get_attr("name", "") &&
                            form_owner(peer) == form)
                            saved.push_back({peer, peer->checked_state()});
            } else
                saved.push_back({control, control->checked_state()});
            if (scripts)
                scripts->focus(hit);
            else
                container->focus(hit);
            previous = control->checked();
            control->checked(control->type() == "radio" || !previous);
            toggled = true;
        }
        if (scripts && hit && !scripts->click(hit, x, y)) {
            for (auto &state : saved)
                state.first->restore_checked(state.second);
            return true;
        }
        if (toggled) {
            if (scripts && control->checked() != previous) {
                scripts->dispatch(hit, "input", false);
                scripts->dispatch(hit, "change", false);
            }
            container->restyle(document);
            return true;
        }
        document->on_lbutton_down(float(x) + container->scroll_x, float(y) + container->scroll_y,
                                  float(x), float(y), redraw);
        document->on_lbutton_up(float(x) + container->scroll_x, float(y) + container->scroll_y,
                                float(x), float(y), redraw);
        container->clicked.reset();
        return true;
    }
};
UnsupportedFrame::UnsupportedFrame(const litehtml::document::ptr &doc)
    : html_tag(doc), impl(std::make_unique<Impl>(*this)) {}
UnsupportedFrame::~UnsupportedFrame() = default;
void UnsupportedFrame::set_attr(const char *name, const char *value) {
    html_tag::set_attr(name, value);
    if (impl && std::string(name) == "src")
        ++impl->source_revision;
}

uint32_t UnsupportedFrame::window_id() { return impl->reserve(); }
void UnsupportedFrame::poll() {
    try {
        impl->poll();
    } catch (const std::exception &) {
        impl->fail("Ошибка или лимит встроенной страницы.");
    }
}
bool UnsupportedFrame::click(double x, double y) { return impl->click(x, y); }
std::string UnsupportedFrame::status() const {
    return !impl->notice.empty() ? impl->notice
                                 : (impl->scripts ? impl->scripts->error() : std::string{});
}
bool UnsupportedFrame::loaded() const { return bool(impl->document); }
bool UnsupportedFrame::pending() const {
    if (!impl->connected() || (!get_attr("src") && !get_attr("srcdoc")))
        return false;
    if (std::string(get_attr("src", "")).empty() && !get_attr("srcdoc"))
        return false;
    return !impl->attempted || impl->source_revision != impl->observed_revision ||
           (impl->resources && !impl->announced);
}
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
    if (!p.does_intersect(clip))
        return;
    auto *c = dynamic_cast<Container *>(get_document()->container());
    if (!c)
        return;
    ++c->painted_frames;
    if (impl->texture && impl->document) {
        c->set_clip(p, {});
        c->draw_frame(impl->texture, p);
        c->del_clip();
        return;
    }
    if (float(p.width) < 120 || float(p.height) < 40)
        return;
    c->set_clip(p, {});
    const char *lines[] = {"Встроенный документ", impl->notice.c_str()};
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
