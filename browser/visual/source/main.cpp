#include "choices.h"
#include "container.h"
#include "diagnostics.h"
#include "interaction.h"
#include "scripts.h"
extern "C" {
#include "profile.h"
}
#include <SDL_image.h>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#endif
namespace {
void text(SDL_Renderer *renderer, TTF_Font *font, int x, int y, const std::string &s,
          SDL_Color color, int maxwidth = 1250) {
    if (s.empty())
        return;
    auto bounded = s.substr(0, 1024);
    while (bounded.size() < s.size() && !bounded.empty() &&
           (static_cast<unsigned char>(s[bounded.size()]) & 0xc0) == 0x80)
        bounded.pop_back();
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, bounded.c_str(), color);
    if (!surface)
        return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect src = {0, 0, std::min(surface->w, maxwidth), surface->h},
                 dst = {x, y, src.w, src.h};
        SDL_RenderCopy(renderer, texture, &src, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}
struct Browser {
    SDL_Renderer *renderer;
    visual::Resources resources;
    visual::Container container;
    litehtml::document::ptr document;
    std::unique_ptr<visual::Scripts> scripts;
    std::unique_ptr<visual::Choices> choices;
    bool report_open = false, report_saved = false;
    std::vector<std::string> report_lines;
    int report_page = 0;
    visual::View report_view;
    visual::Gestures report_gestures{report_view};
    visual::Resource *root_resource = nullptr;
    std::vector<std::string> submission_rules;
    visual::View view;
    visual::Gestures gestures;
    std::string url = "demo:/index.html", notice;
    struct History {
        std::string url;
        bool post = false;
    };
    std::vector<History> history;
    unsigned built_revision = 0, built_style_revision = 0, built_image_revision = 0;
    bool rebuild = true, js_failed = false, js_reported = false;
    int viewport_mode = 0;
    TTF_Font *ui = nullptr;
    int pointer_x = 640, pointer_y = 318;
    Browser(SDL_Renderer *r, const std::string &root, const std::string &font)
        : renderer(r), resources(root), container(r, resources, font), gestures(view) {
        report_gestures.zoom_enabled = false;
        ui = TTF_OpenFont(font.c_str(), 18);
        if (!ui)
            throw std::runtime_error(TTF_GetError());
        navigate(url, false);
    }
    ~Browser() {
        report_open = false;
        choices.reset();
        scripts.reset();
        document.reset();
        container.reset();
        TTF_CloseFont(ui);
    }
    void navigate(std::string next, bool record = true, const visual::Submission *form = nullptr,
                  const std::string &source = {}, const std::vector<std::string> &policies = {}) {
        if (next.empty())
            return;
        if (!form && next.front() == '#') {
            jump(next.substr(1));
            return;
        }
        auto fragment = next.find('#');
        if (!form && fragment != std::string::npos &&
            (next.substr(0, fragment) == url.substr(0, url.find('#')) ||
             next.substr(0, fragment) == container.base.substr(0, container.base.find('#')))) {
            jump(next.substr(fragment + 1));
            return;
        }
        if (record) {
            history.push_back({url, root_resource && root_resource->method == "POST"});
            if (history.size() > 16)
                history.erase(history.begin());
        }
        report_open = false;
        choices.reset();
        scripts.reset();
        document.reset();
        container.reset();
        root_resource = nullptr;
        resources.clear();
        container.forms.clear();
        submission_rules.clear();
        gestures.cancel();
        view = {};
        view.base_scale = 1280.f / container.viewport_width;
        url = next;
        container.base = url;
        container.page_url = url;
        notice.clear();
        js_failed = js_reported = false;
        built_revision = 0;
        rebuild = true;
        root_resource = form ? resources.navigate_form(*form, source, policies)
                             : resources.request(url, 1024u * 1024u);
        if (!root_resource)
            notice = "Не удалось подготовить загрузку страницы";
    }
    void back() {
        if (report_open) {
            report_open = false;
            return;
        }
        if (choices) {
            choices.reset();
            return;
        }
        while (!history.empty()) {
            auto previous = history.back();
            history.pop_back();
            if (previous.post)
                continue;
            navigate(previous.url, false);
            return;
        }
        navigate("demo:/index.html", false);
    }
    void reload() {
        if (root_resource && root_resource->method == "POST") {
            notice = "Чтобы обновить эту страницу, отправьте форму заново.";
            return;
        }
        navigate(root_resource ? root_resource->effective : url, false);
    }
    void jump(const std::string &id) {
        if (!document)
            return;
        if (id.empty()) {
            view.y = 0;
            return;
        }
        // Attribute selector quoting is deliberately avoided for arbitrary IDs.
        auto nodes = document->root()->select_all("[id]");
        for (const auto &el : nodes)
            if (id == el->get_attr("id", "")) {
                view.y = float(el->get_placement().y);
                view.clamp();
                break;
            }
    }
    void build() {
        auto *r = root_resource;
        if (!r)
            return;
        if (r->state == visual::Resource::failed && !r->transfer_ok) {
            notice = r->error;
            built_revision = resources.revision;
            rebuild = false;
            return;
        }
        if (r->state != visual::Resource::ready && !r->transfer_ok)
            return;
        url = r->effective;
        container.base = r->effective;
        container.page_url = r->effective;
        container.zoom = view.scale();
        try {
            if (!document) {
                container.reset();
                std::string html(r->data.begin(), r->data.end());
                if (std::count(html.begin(), html.end(), '<') > 8192)
                    throw std::runtime_error("Превышен лимит сложности страницы");
                document = litehtml::document::createFromString(
                    html, &container, litehtml::master_css + visual::control_styles());
                if (!document)
                    throw std::runtime_error("HTML parse failed");
                submission_rules = visual::form_policies(document, r->policies);
                ++container.style_passes;
                container.relayout(document);
            } else if (scripts && scripts->changed())
                scripts->flush();
            else if (rebuild || built_style_revision != resources.style_revision)
                container.restyle(document);
            else
                container.relayout(document);
            view.content_width =
                std::max(float(container.viewport_width), float(document->width()));
            view.content_height = std::max(container.viewport_height, float(document->height()));
            view.clamp();
            visual::diagnostic("layout.ready",
                               "width=" + std::to_string(int(view.content_width)) +
                                   " height=" + std::to_string(int(view.content_height)) +
                                   " resources=" + std::to_string(resources.count()));
            built_revision = resources.revision;
            built_style_revision = resources.style_revision;
            built_image_revision = resources.image_revision;
            rebuild = false;
        } catch (const std::exception &e) {
            notice = e.what();
            visual::diagnostic("layout.failed", notice);
            scripts.reset();
            document.reset();
            rebuild = false;
            built_revision = resources.revision;
        }
    }
    void apply_script_scroll() {
        if (container.scroll_requested) {
            container.scroll_requested = false;
            gestures.stop_inertia();
            view.x = container.scroll_x;
            view.y = container.scroll_y;
            // Geometry reads may already have flushed a larger/smaller document.
            if (document) {
                view.content_width =
                    std::max(float(container.viewport_width), float(document->width()));
                view.content_height =
                    std::max(container.viewport_height, float(document->height()));
            }
            view.clamp();
            container.scroll_x = view.x;
            container.scroll_y = view.y;
        }
    }
    bool apply_script_navigation() {
        apply_script_scroll();
        if (!scripts)
            return false;
        auto form = scripts->submission();
        if (form) {
            auto policies = visual::form_policies(document, submission_rules);
            if (!visual::form_action_allowed(policies, container.page_url, form.url)) {
                notice = "Отправка формы запрещена политикой сайта";
                return true;
            }
            auto source = container.page_url;
            navigate(form.url, true, &form, source, policies);
            return true;
        }
        auto target = scripts->navigation();
        if (!target.empty()) {
            navigate(target);
            return true;
        }
        if (scripts->reload_requested()) {
            reload();
            return true;
        }
        return false;
    }
    void pump() {
        apply_script_scroll();
        if (container.prune_focus())
            rebuild = true;
        container.scroll_x = view.x;
        container.scroll_y = view.y;
        resources.pump();
        if (rebuild || (!document && built_revision != resources.revision) ||
            (document && (built_style_revision != resources.style_revision ||
                          built_image_revision != resources.image_revision)))
            build();
        if (!document)
            return;
        if (!scripts && !js_failed && !resources.pending_styles()) {
            auto *root = root_resource;
            try {
                visual::diagnostic("js.start", "QuickJS native DOM");
                scripts = std::make_unique<visual::Scripts>(document, container, resources,
                                                            root ? root->policies
                                                                 : std::vector<std::string>{});
            } catch (const std::exception &e) {
                notice = e.what();
                js_failed = true;
                return;
            }
        }
        if (scripts) {
            try {
                unsigned layouts_before = container.layout_passes;
                auto turn_time = visual::Scripts::clock_ms();
                scripts->poll(turn_time);
                if (apply_script_navigation())
                    return;
                scripts->render_frame(turn_time);
                if (scripts->changed())
                    scripts->flush();
                if (container.layout_passes != layouts_before) {
                    view.content_width =
                        std::max(float(container.viewport_width), float(document->width()));
                    view.content_height =
                        std::max(container.viewport_height, float(document->height()));
                    view.clamp();
                    built_revision = resources.revision;
                }
                if (scripts->ready() && !js_reported) {
                    js_reported = true;
                    visual::diagnostic("js.ready", "page script phase complete");
                }
                if (!scripts->error().empty())
                    notice = "JS: " + scripts->error();
                if (apply_script_navigation())
                    return;
                if (!container.form_notice.empty()) {
                    notice = std::move(container.form_notice);
                    container.form_notice.clear();
                }
            } catch (const std::exception &e) {
                notice = e.what();
                scripts.reset();
                js_failed = true;
                visual::diagnostic("js.failed", "script or layout error");
            }
        }
        if (choices) {
            auto node = std::static_pointer_cast<litehtml::element>(choices->control);
            while (node && node != document->root())
                node = node->parent();
            if (!node || choices->control->disabled() ||
                choices->control->css().get_display() == litehtml::display_none)
                choices.reset();
            else {
                auto items = choices->control->options();
                if (items != choices->items) {
                    choices->gestures.cancel();
                    choices->items = std::move(items);
                    choices->view.content_height =
                        float(choices->items.size() * visual::Choices::row);
                    choices->hover =
                        std::clamp(choices->hover, 0, std::max(0, int(choices->items.size()) - 1));
                    choices->view.clamp();
                }
            }
        }
    }
    void click(visual::Point p) {
        if (report_open) {
            if (p.y < 70)
                report_open = false;
            else
                report_page = std::clamp(report_page + (p.x >= 640 ? 1 : -1), 0,
                                         std::max(0, (int(report_lines.size()) - 1) / 11));
            return;
        }
        if (p.y < 0 && p.y >= -42) {
            toggle_report();
            return;
        }
        if (choices) {
            int index = choices->hit(p);
            if (index >= 0) {
                choose_option(index);
                return;
            }
            if (p.y >= visual::Choices::footer_y && p.y < visual::Choices::footer_y + 60) {
                choices.reset();
                return;
            }
            if (p.x < visual::Choices::x || p.x >= visual::Choices::x + visual::Choices::width ||
                p.y < visual::Choices::y || p.y >= visual::Choices::footer_y + 60)
                choices.reset();
            return;
        }
        if (p.y < 0) {
            if (p.x < 130)
                back();
            else if (p.x < 270)
                navigate("demo:/index.html");
            else if (p.x >= 1120)
                edit_address();
            else
                edit_address();
            return;
        }
        if (!document)
            return;
        auto q = view.document(p);
        auto redraw = [](const litehtml::position &) {};
        document->on_mouse_over(q.x, q.y, p.x / view.scale(), p.y / view.scale(), redraw);
        auto hit = std::const_pointer_cast<litehtml::element>(document->get_over_element());
        if (auto check = std::dynamic_pointer_cast<visual::Control>(hit))
            if (check->type() == "checkbox" || check->type() == "radio") {
                activate_check(check, true);
                return;
            }
        if (scripts && hit) {
            if (!scripts->dispatch(hit, "click"))
                return;
            document->on_mouse_over(q.x, q.y, p.x / view.scale(), p.y / view.scale(), redraw);
            if (apply_script_navigation())
                return;
        }
        document->on_lbutton_down(q.x, q.y, p.x / view.scale(), p.y / view.scale(), redraw);
        document->on_lbutton_up(q.x, q.y, p.x / view.scale(), p.y / view.scale(), redraw);
        if (container.clicked) {
            auto el = container.clicked;
            container.clicked.reset();
            activate_control(el);
            return;
        }
        if (!hit || (!std::dynamic_pointer_cast<visual::Control>(hit) &&
                     std::string(hit->get_tagName()) != "button"))
            focus({});
        if (!container.anchor.empty()) {
            auto next = container.anchor;
            container.anchor.clear();
            navigate(next);
        }
    }
    void cycle_viewport() {
        viewport_mode = (viewport_mode + 1) % 3;
        static const int widths[] = {1280, 960, 640};
        container.viewport_width = widths[viewport_mode];
        view.base_scale = 1280.f / container.viewport_width;
        container.viewport_height = 636 / view.base_scale;
        view.zoom = 1;
        view.x = view.y = 0;
        gestures.cancel();
        rebuild = true;
    }
    void control_changed(const litehtml::element::ptr &el) {
        rebuild = true;
        if (scripts) {
            scripts->dispatch(el, "input", false);
            scripts->dispatch(el, "change", false);
        }
    }
    void focus(const litehtml::element::ptr &el) {
        if (scripts)
            scripts->focus(el);
        else
            container.focus(el);
        rebuild = true;
    }
    void activate_check(const std::shared_ptr<visual::Control> &control, bool emit_click) {
        if (control->disabled())
            return;
        focus(control);
        if (apply_script_navigation())
            return;
        std::vector<std::pair<std::shared_ptr<visual::Control>, visual::Control::CheckedState>>
            saved;
        auto owner = visual::form_owner(control);
        std::string name = control->get_attr("name", "");
        if (control->type() == "radio" && !name.empty()) {
            for (auto &el : document->root()->select_all("input"))
                if (auto peer = std::dynamic_pointer_cast<visual::Control>(el))
                    if (peer->type() == "radio" && name == peer->get_attr("name", "") &&
                        visual::form_owner(peer) == owner)
                        saved.push_back({peer, peer->checked_state()});
        } else
            saved.push_back({control, control->checked_state()});
        bool before = control->checked();
        control->checked(control->type() == "radio" || !before);
        if (emit_click && scripts && !scripts->dispatch(control, "click")) {
            for (auto &state : saved)
                state.first->restore_checked(state.second);
            rebuild = true;
            return;
        }
        if (apply_script_navigation())
            return;
        if (before != control->checked())
            control_changed(control);
    }
    void choose_option(int index) {
        if (!choices || index < 0 || size_t(index) >= choices->items.size())
            return;
        auto control = choices->control;
        auto parent = std::static_pointer_cast<litehtml::element>(control);
        while (parent && parent != document->root())
            parent = parent->parent();
        if (!parent) {
            choices.reset();
            return;
        }
        auto option = choices->items[size_t(index)];
        if (control->disabled() || visual::form_disabled(option))
            return;
        auto selected = control->selected_options();
        bool yes = !control->multiple() ||
                   std::find(selected.begin(), selected.end(), option) == selected.end();
        if (control->select_option(option, yes))
            control_changed(control);
        if (!control->multiple())
            choices.reset();
    }
    void cancel_input() {
        report_gestures.cancel();
        gestures.cancel();
        if (choices)
            choices->gestures.cancel();
    }
    void touch_down(int64_t id, visual::Point p, uint32_t now) {
        if (report_open) {
            report_gestures.down(id, p, now);
            return;
        }
        if (choices)
            choices->gestures.down(id, choices->local(p), now);
        else
            gestures.down(id, p, now);
    }
    void touch_move(int64_t id, visual::Point p, uint32_t now) {
        if (report_open) {
            report_gestures.move(id, p, now);
            return;
        }
        if (choices)
            choices->gestures.move(id, choices->local(p), now);
        else
            gestures.move(id, p, now);
    }
    void touch_up(int64_t id, visual::Point p, uint32_t now) {
        if (report_open) {
            if (report_gestures.up(id, p, now))
                click(p);
            return;
        }
        if (choices) {
            auto tap = choices->gestures.up(id, choices->local(p), now);
            if (tap)
                click(p);
        } else {
            auto tap = gestures.up(id, p, now);
            if (tap)
                click(*tap);
        }
    }
    void scroll(float dx, float dy) {
        if (report_open) {
            report_page = std::clamp(report_page + (dy < 0   ? 1
                                                    : dy > 0 ? -1
                                                             : 0),
                                     0, std::max(0, (int(report_lines.size()) - 1) / 11));
            return;
        }
        if (choices)
            choices->scroll(dy);
        else
            view.drag(dx, dy);
    }
    void send_form(const litehtml::element::ptr &el) {
        auto prepared = visual::prepare_form(el, container.base, notice, true);
        if (!prepared)
            return;
        auto form = visual::form_owner(el);
        if (scripts && form && !scripts->dispatch(form, "submit", true, el))
            return;
        if (apply_script_navigation())
            return;
        prepared = visual::prepare_form(el, container.base, notice, false);
        if (!prepared)
            return;
        auto policies = visual::form_policies(document, submission_rules);
        if (!visual::form_action_allowed(policies, container.page_url, prepared.url)) {
            notice = "Отправка формы запрещена политикой сайта";
            return;
        }
        auto source = container.page_url;
        navigate(prepared.url, true, &prepared, source, policies);
    }
    void activate_control(const litehtml::element::ptr &el) {
        if (visual::form_disabled(el))
            return;
        if (std::string(el->get_tagName()) == "label") {
            auto id = std::string(el->get_attr("for", ""));
            litehtml::element::ptr target;
            if (!id.empty()) {
                for (auto &item : document->root()->select_all("[id]"))
                    if (id == item->get_attr("id", "")) {
                        target = item;
                        break;
                    }
            } else
                for (auto &item : el->select_all("*"))
                    if (std::dynamic_pointer_cast<visual::Control>(item)) {
                        target = item;
                        break;
                    }
            if (target && target != el &&
                (std::dynamic_pointer_cast<visual::Control>(target) ||
                 std::string(target->get_tagName()) == "button"))
                activate_control(target);
            return;
        }
        auto control = std::dynamic_pointer_cast<visual::Control>(el);
        focus(el);
        if (apply_script_navigation())
            return;
        if (control && (control->type() == "checkbox" || control->type() == "radio")) {
            activate_check(control, true);
            return;
        }
        if (control && control->type() == "select") {
            gestures.cancel();
            choices = std::make_unique<visual::Choices>(control);
            return;
        }
        if (control && control->editable()) {
#ifdef __SWITCH__
            gestures.cancel();
            SwkbdConfig keyboard;
            if (R_FAILED(swkbdCreate(&keyboard, 0)))
                return;
            swkbdConfigMakePresetDefault(&keyboard);
            if (control->type() == "password")
                swkbdConfigSetPasswordFlag(&keyboard, 1);
            swkbdConfigSetHeaderText(&keyboard, "Ввод текста");
            swkbdConfigSetGuideText(&keyboard,
                                    el->get_attr("placeholder", el->get_attr("name", "")));
            swkbdConfigSetInitialText(&keyboard, control->value().c_str());
            int maxlength = std::clamp(std::atoi(el->get_attr("maxlength", "1024")), 1, 1024);
            swkbdConfigSetStringLenMax(&keyboard, maxlength);
            char value[4097] = {};
            Result rc = swkbdShow(&keyboard, value, sizeof(value));
            swkbdClose(&keyboard);
            if (R_SUCCEEDED(rc)) {
                bool changed = control->value() != value;
                control->value(value);
                if (changed)
                    control_changed(el);
            }
#else
            notice = "Поле выбрано. Экранная клавиатура доступна на Switch.";
#endif
            return;
        }
        std::string type = control ? control->type() : el->get_attr("type", "submit");
        if (type == "submit")
            send_form(el);
        else if (type == "reset") {
            auto form = visual::form_owner(el);
            if (scripts && form && !scripts->dispatch(form, "reset"))
                return;
            visual::reset_form(el);
            rebuild = true;
        } else if (type != "button" && !el->get_attr("readonly"))
            notice = "Этот тип элемента пока не подключён";
    }
    void edit_address() {
        report_open = false;
        choices.reset();
#ifdef __SWITCH__
        gestures.cancel();
        SwkbdConfig keyboard;
        if (R_FAILED(swkbdCreate(&keyboard, 0)))
            return;
        swkbdConfigMakePresetDefault(&keyboard);
        swkbdConfigSetHeaderText(&keyboard, "HTTPS address");
        swkbdConfigSetStringLenMax(&keyboard, 2047);
        swkbdConfigSetInitialText(
            &keyboard, url.rfind("https://", 0) == 0 ? url.c_str() : "https://keldurn.com/ru/");
        char address[2048] = {};
        Result rc = swkbdShow(&keyboard, address, sizeof(address));
        swkbdClose(&keyboard);
        if (R_SUCCEEDED(rc)) {
            auto u = visual::Resources::resolve("", address);
            if (!u.empty())
                navigate(u);
            else
                notice = "Нужен адрес https://";
        }
#else
        notice = "Host: pass an HTTPS address as an argument";
#endif
    }
    void paint_choices() {
        if (!choices)
            return;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 165);
        SDL_Rect shade = {0, 0, 1280, 720};
        SDL_RenderFillRect(renderer, &shade);
        SDL_SetRenderDrawColor(renderer, 245, 248, 252, 255);
        SDL_Rect panel = {visual::Choices::x, visual::Choices::y + 84, visual::Choices::width,
                          visual::Choices::footer_y + 60 - visual::Choices::y};
        SDL_RenderFillRect(renderer, &panel);
        text(renderer, ui, visual::Choices::x + 20, visual::Choices::y + 96,
             choices->control->get_attr("aria-label", "Выберите вариант"), {24, 42, 66, 255}, 860);
        SDL_Rect clip = {visual::Choices::x + 8, visual::Choices::list_y + 84,
                         visual::Choices::width - 16,
                         visual::Choices::row * visual::Choices::visible};
        SDL_RenderSetClipRect(renderer, &clip);
        auto selected = choices->control->selected_options();
        int first = int(choices->view.y / visual::Choices::row);
        for (int i = first; i < int(choices->items.size()) && i <= first + visual::Choices::visible;
             i++) {
            auto option = choices->items[size_t(i)];
            int y = visual::Choices::list_y + 84 + i * visual::Choices::row - int(choices->view.y);
            bool chosen = std::find(selected.begin(), selected.end(), option) != selected.end();
            SDL_Rect row = {visual::Choices::x + 8, y, visual::Choices::width - 16,
                            visual::Choices::row - 1};
            SDL_SetRenderDrawColor(renderer,
                                   chosen                ? 191
                                   : choices->hover == i ? 222
                                                         : 245,
                                   chosen                ? 225
                                   : choices->hover == i ? 233
                                                         : 248,
                                   chosen                ? 233
                                   : choices->hover == i ? 244
                                                         : 252,
                                   255);
            SDL_RenderFillRect(renderer, &row);
            text(renderer, ui, visual::Choices::x + 22, y + 10, chosen ? "●" : "○",
                 {30, 95, 122, 255}, 30);
            text(renderer, ui, visual::Choices::x + 62, y + 10, visual::option_label(option),
                 visual::form_disabled(option) ? SDL_Color{135, 140, 148, 255}
                                               : SDL_Color{24, 42, 66, 255},
                 820);
        }
        SDL_RenderSetClipRect(renderer, nullptr);
        text(renderer, ui, visual::Choices::x + 20, visual::Choices::footer_y + 100,
             choices->control->multiple() ? "Готово · B — закрыть" : "B — закрыть · A — выбрать",
             {20, 88, 117, 255}, 860);
    }
    void toggle_report() {
        if (report_open) {
            report_open = false;
            return;
        }
        choices.reset();
        cancel_input();
        report_page = 0;
        auto failures = resources.failures();
        char site[384] = {};
        browser_origin(container.page_url.c_str(), site, sizeof(site));
        report_lines = {
            "Switch visual browser 0.11.0 · " + std::string(site[0] ? site : "offline"),
            "Загрузка: HTTP " + std::to_string(failures.http) + " · TLS " +
                std::to_string(failures.tls) + " · сеть " + std::to_string(failures.transport),
            "Лимиты загрузки: " + std::to_string(failures.limits) +
                " · блокировки: " + std::to_string(failures.policy),
            "Изображения: загружено " + std::to_string(container.decoded_images()) +
                " · не декодировано " + std::to_string(container.failed_images()),
            "Рисование: пропусков за последний кадр " + std::to_string(container.unsupported),
            "CSS-проходов: " + std::to_string(container.style_passes) +
                " · расчётов раскладки: " + std::to_string(container.layout_passes),
            "JS: " + std::string(
                         scripts ? (scripts->ready() ? "начальные скрипты обработаны" : "загрузка")
                                 : "не запущен"),
            "Ниже — группы сбоев JavaScript; xN означает число повторов."};
        if (scripts) {
            for (const auto &issue : scripts->issues().entries())
                report_lines.push_back(issue.category + " / " + issue.code + " · x" +
                                       std::to_string(issue.count) + " · " + issue.origin);
            if (scripts->issues().truncated())
                report_lines.push_back("Достигнут лимит: 24 группы JavaScript.");
        }
        report_lines.push_back("Известные пробелы: Grid, полный DOM/CSSOM, import maps, workers.");
        report_lines.push_back(
            "Также отсутствуют: полный CORS, GPU, видео и аудио.");
        report_lines.push_back(
            "Счётчики не определяют причину отказа сайта без дальнейшей проверки.");
        std::string report;
        for (const auto &line : report_lines)
            report += line + "\n";
        report_saved = visual::write_core_report(report);
        report_open = true;
    }
    void paint_report() {
        if (!report_open)
            return;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
        SDL_Rect shade = {0, 84, 1280, 636};
        SDL_RenderFillRect(renderer, &shade);
        SDL_SetRenderDrawColor(renderer, 246, 249, 253, 255);
        SDL_Rect panel = {24, 102, 1232, 590};
        SDL_RenderFillRect(renderer, &panel);
        text(renderer, ui, 44, 119, "Отчёт ядра · L / B / коснитесь заголовка — закрыть",
             {24, 42, 66, 255});
        for (int i = 0; i < 11; ++i) {
            size_t index = size_t(report_page * 11 + i);
            if (index >= report_lines.size())
                break;
            text(renderer, ui, 44, 165 + i * 37, report_lines[index], {24, 42, 66, 255}, 1170);
        }
        text(renderer, ui, 44, 597,
             "← → коснитесь половины окна · страница " + std::to_string(report_page + 1) + "/" +
                 std::to_string((report_lines.size() + 10) / 11),
             {20, 88, 117, 255});
        text(renderer, ui, 44, 645,
             report_saved ? "SD: /switch/switch-visual-browser/core-report.txt" :
#ifdef __SWITCH__
                          "Не удалось сохранить отчёт на SD"
#else
                          "Предпросмотр отчёта (F1)"
#endif
             ,
             {20, 88, 117, 255});
    }
    void paint() {
        SDL_SetRenderDrawColor(renderer, 245, 247, 250, 255);
        SDL_RenderClear(renderer);
        container.zoom = view.scale();
        container.begin_paint();
        if (document) {
            litehtml::position clip = {0, 0, 1280 / view.scale(), 636 / view.scale()};
            document->draw(0, -view.x, -view.y, &clip);
        }
        container.end_paint();
        SDL_SetRenderDrawColor(renderer, 20, 28, 41, 255);
        SDL_Rect chrome = {0, 0, 1280, 84};
        SDL_RenderFillRect(renderer, &chrome);
        text(renderer, ui, 16, 10, "‹ Назад", {230, 238, 250, 255}, 110);
        text(renderer, ui, 144, 10, "Демо", {112, 220, 235, 255}, 100);
        text(renderer, ui, 280, 10, url, {230, 238, 250, 255}, 840);
        text(renderer, ui, 1135, 10, "Адрес X", {112, 220, 235, 255}, 135);
        text(renderer, ui, 16, 46,
             notice.empty() ? "Отчёт L · " + resources.status() + " · масштаб " +
                                  std::to_string(int(view.zoom * 100)) + "% · " +
                                  std::string(viewport_mode == 0   ? "ПК"
                                              : viewport_mode == 1 ? "Планшет"
                                                                   : "Мобильный") +
                                  " · " +
                                  std::string(scripts ? (scripts->ready() ? "JS" : "JS: загрузка")
                                                      : "JS: ожидание")
                            : notice,
             {174, 190, 211, 255}, 1250);
        if (!document && notice.empty())
            text(renderer, ui, 40, 140, "Загрузка страницы…", {25, 40, 60, 255});
        paint_choices();
        paint_report();
        if (document && !gestures.active() && !report_open) {
            SDL_SetRenderDrawColor(renderer, 15, 100, 160, 200);
            SDL_RenderDrawLine(renderer, pointer_x - 7, pointer_y + 84, pointer_x + 7,
                               pointer_y + 84);
            SDL_RenderDrawLine(renderer, pointer_x, pointer_y + 77, pointer_x, pointer_y + 91);
        }
    }
};
#ifndef __SWITCH__
#include "../tests/network_browser.h"
#endif
} // namespace
int main(int argc, char **argv) {
    std::string root, font, ca;
#ifdef __SWITCH__
    visual::diagnostic("boot", "0.11.0");
    Result romfs = romfsInit();
    if (R_FAILED(romfs)) {
        visual::fatal_screen("RomFS failed: " + std::to_string(romfs));
        return 1;
    }
    root = "romfs:/visual";
    font = "romfs:/DejaVuSans.ttf";
    ca = "romfs:/cacert.pem";
    bool sockets = R_SUCCEEDED(socketInitializeDefault());
#else
    if (argc < 3) {
        fprintf(stderr, "Usage: visual-host ASSET_ROOT FONT [--test SCREENSHOT.bmp | --capture "
                        "HTTPS_URL SCREENSHOT.bmp | HTTPS_URL]\n");
        return 1;
    }
    root = argv[1];
    font = argv[2];
    ca = "";
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) || TTF_Init()) {
        std::string error =
            std::string("SDL/font init: ") + SDL_GetError() + " / " + TTF_GetError();
        TTF_Quit();
        SDL_Quit();
        visual::fatal_screen(error);
        return 1;
    }
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    curl_global_init(CURL_GLOBAL_DEFAULT);
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0700);
    mkdir("sdmc:/switch/switch-visual-browser", 0700);
    browser_profile_open("sdmc:/switch/switch-visual-browser", ca.c_str());
    hidInitializeTouchScreen();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
#else
    if (argc >= 10 &&
        (std::string(argv[3]) == "--network-test" || std::string(argv[3]) == "--dynamic-test"))
        browser_profile_open(argv[4], argv[8]);
    else
        browser_profile_open(nullptr, nullptr);
#endif
    SDL_Window *window = SDL_CreateWindow("Switch visual browser", SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED, 1280, 720, 0);
    SDL_Renderer *renderer =
        window
            ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)
            : nullptr;
    if (!renderer && window)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    int result = 0;
    std::string fatal;
    if (!renderer) {
        fatal = std::string("Renderer: ") + SDL_GetError();
        result = 1;
    } else
        try {
            SDL_RenderSetLogicalSize(renderer, 1280, 720);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            Browser app(renderer, root, font);
#ifndef __SWITCH__
            bool network_test = argc >= 10 && (std::string(argv[3]) == "--network-test" ||
                                               std::string(argv[3]) == "--dynamic-test");
            if (network_test) {
                if (std::string(argv[3]) == "--dynamic-test")
                    browser_dynamic_checks(app, argv[5]);
                else
                    browser_network_checks(app, argv[5]);
            }
            bool test = argc >= 5 && std::string(argv[3]) == "--test";
            bool capture = argc >= 6 && std::string(argv[3]) == "--capture";
            uint32_t capture_start = SDL_GetTicks();
            if (capture)
                app.navigate(argv[4]);
            else if (argc >= 4 && !test && !network_test)
                app.navigate(argv[3]);
#endif
            bool quit = false;
#ifndef __SWITCH__
            quit = network_test;
#endif
            uint32_t previous = SDL_GetTicks();
            while (!quit) {
#ifdef __SWITCH__
                if (!appletMainLoop())
                    break;
                padUpdate(&pad);
                u64 down = padGetButtonsDown(&pad), held = padGetButtons(&pad);
                if (down & HidNpadButton_Plus)
                    break;
                if (down & HidNpadButton_Minus)
                    app.cycle_viewport();
                if (down & HidNpadButton_X)
                    app.edit_address();
                if (down & HidNpadButton_B)
                    app.back();
                if (down & HidNpadButton_Y)
                    app.reload();
                if (down & HidNpadButton_L)
                    app.toggle_report();
                if (down & HidNpadButton_A) {
                    if (app.choices && app.choices->pad_navigation)
                        app.choose_option(app.choices->hover);
                    else
                        app.click({float(app.pointer_x), float(app.pointer_y)});
                }
                if (app.choices) {
                    if (down & HidNpadButton_Down)
                        app.choices->move(1);
                    if (down & HidNpadButton_Up)
                        app.choices->move(-1);
                } else {
                    if (held & HidNpadButton_Down)
                        app.scroll(0, -10);
                    if (held & HidNpadButton_Up)
                        app.scroll(0, 10);
                }
                HidAnalogStickState right = padGetStickPos(&pad, 1);
                float scroll_speed = visual::stick_scroll_speed(right.y / 32767.f);
                if (scroll_speed != 0 && !app.gestures.active() &&
                    (!app.choices || !app.choices->gestures.active())) {
                    app.gestures.stop_inertia();
                    float seconds = std::min(.05f, (SDL_GetTicks() - previous) / 1000.f);
                    app.scroll(0, scroll_speed * seconds);
                }
                HidAnalogStickState stick = padGetStickPos(&pad, 0);
                if (app.choices && (std::abs(stick.x) > 4096 || std::abs(stick.y) > 4096))
                    app.choices->pad_navigation = false;
                app.pointer_x = std::clamp(app.pointer_x + stick.x / 4096, 0, 1279);
                app.pointer_y = std::clamp(app.pointer_y - stick.y / 4096, 0, 635);
                if (!app.report_open && !app.choices && (down & HidNpadButton_ZR))
                    app.view.pinch({640, 318}, {640, 318}, 1.2f);
                if (!app.report_open && !app.choices && (down & HidNpadButton_ZL))
                    app.view.pinch({640, 318}, {640, 318}, 1 / 1.2f);
#endif
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    if (event.type == SDL_QUIT)
                        quit = true;
                    else if (event.type == SDL_WINDOWEVENT &&
                             event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                        app.cancel_input();
                    else if (event.type == SDL_FINGERDOWN)
                        app.touch_down(event.tfinger.fingerId,
                                       {event.tfinger.x * 1280, event.tfinger.y * 720 - 84},
                                       event.tfinger.timestamp);
                    else if (event.type == SDL_FINGERMOTION)
                        app.touch_move(event.tfinger.fingerId,
                                       {event.tfinger.x * 1280, event.tfinger.y * 720 - 84},
                                       event.tfinger.timestamp);
                    else if (event.type == SDL_FINGERUP)
                        app.touch_up(event.tfinger.fingerId,
                                     {event.tfinger.x * 1280, event.tfinger.y * 720 - 84},
                                     event.tfinger.timestamp);
                    else if (event.type == SDL_MOUSEBUTTONDOWN &&
                             event.button.which != SDL_TOUCH_MOUSEID &&
                             event.button.button == SDL_BUTTON_LEFT)
                        app.touch_down(-1, {float(event.button.x), float(event.button.y - 84)},
                                       event.button.timestamp);
                    else if (event.type == SDL_MOUSEMOTION &&
                             event.motion.which != SDL_TOUCH_MOUSEID &&
                             (event.motion.state & SDL_BUTTON_LMASK))
                        app.touch_move(-1, {float(event.motion.x), float(event.motion.y - 84)},
                                       event.motion.timestamp);
                    else if (event.type == SDL_MOUSEBUTTONUP &&
                             event.button.which != SDL_TOUCH_MOUSEID &&
                             event.button.button == SDL_BUTTON_LEFT)
                        app.touch_up(-1, {float(event.button.x), float(event.button.y - 84)},
                                     event.button.timestamp);
                    else if (event.type == SDL_MOUSEWHEEL)
                        app.scroll(0, event.wheel.y * 72.f);
                    else if (event.type == SDL_KEYDOWN) {
                        if (event.key.keysym.sym == SDLK_ESCAPE)
                            quit = true;
                        else if (event.key.keysym.sym == SDLK_F1)
                            app.toggle_report();
                        else if (event.key.keysym.sym == SDLK_BACKSPACE)
                            app.back();
                        else if (event.key.keysym.sym == SDLK_m)
                            app.cycle_viewport();
                        else if (event.key.keysym.sym == SDLK_HOME)
                            app.navigate("demo:/index.html");
                        else if (event.key.keysym.sym == SDLK_DOWN)
                            app.scroll(0, -60);
                        else if (event.key.keysym.sym == SDLK_UP)
                            app.scroll(0, 60);
                    }
                }
                uint32_t now = SDL_GetTicks();
                if (app.choices)
                    app.choices->gestures.tick((now - previous) / 1000.f);
                else
                    app.gestures.tick((now - previous) / 1000.f);
                previous = now;
                app.pump();
                app.paint();
#ifndef __SWITCH__
                if (capture && !app.resources.pending() && app.scripts && app.scripts->ready()) {
                    if (!app.document)
                        throw std::runtime_error(app.notice.empty() ? "No rendered document"
                                                                    : app.notice);
                    SDL_Surface *shot =
                        SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
                    if (!shot ||
                        SDL_RenderReadPixels(renderer, nullptr, shot->format->format, shot->pixels,
                                             shot->pitch) ||
                        SDL_SaveBMP(shot, argv[5]))
                        throw std::runtime_error(SDL_GetError());
                    SDL_FreeSurface(shot);
                    fprintf(stderr,
                            "CAPTURE height=%.1f resources=%zu images=%zu text=%u unsupported=%u\n",
                            app.view.content_height, app.resources.count(),
                            app.container.decoded_images(), app.container.painted_text,
                            app.container.unsupported);
                    quit = true;
                }
                if (capture && SDL_GetTicks() - capture_start > 30000)
                    throw std::runtime_error("Capture timed out");
                if (test && app.scripts && !app.scripts->ready()) {
                    SDL_RenderPresent(renderer);
                    SDL_Delay(10);
                    continue;
                }
                if (test) {
                    if (!app.document)
                        throw std::runtime_error("demo document missing");
                    auto link = app.document->root()->select_one("#jump");
                    auto target = app.document->root()->select_one("#details");
                    auto a = app.document->root()->select_one("#card-a"),
                         b = app.document->root()->select_one("#card-b");
                    fprintf(stderr,
                            "layout: link=%d target=%d a=%d b=%d ax=%.1f bx=%.1f height=%.1f "
                            "images=%u decoded=%zu text=%u notice=%s\n",
                            bool(link), bool(target), bool(a), bool(b),
                            a ? float(a->get_placement().x) : -1,
                            b ? float(b->get_placement().x) : -1, app.view.content_height,
                            app.container.painted_images, app.container.decoded_images(),
                            app.container.painted_text, app.notice.c_str());
                    if (!link || !target || !a || !b ||
                        !(float(a->get_placement().x) < float(b->get_placement().x)) ||
                        !(app.view.content_height > 636) || app.container.painted_images < 1 ||
                        app.container.decoded_images() != 3 || app.container.painted_text < 10)
                        throw std::runtime_error("CSS/image/layout assertions failed");
                    SDL_Surface *shot =
                        SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
                    if (!shot ||
                        SDL_RenderReadPixels(renderer, nullptr, shot->format->format, shot->pixels,
                                             shot->pitch) ||
                        SDL_SaveBMP(shot, argv[4]))
                        throw std::runtime_error(SDL_GetError());
                    SDL_FreeSurface(shot);
                    auto field = std::dynamic_pointer_cast<visual::Control>(
                        app.document->root()->select_one("#search"));
                    auto submit = app.document->root()->select_one("#search-button");
                    if (!field || !field->editable() || float(field->get_placement().width) < 100)
                        throw std::runtime_error("search control not laid out");
                    auto input_box = field->get_placement();
                    app.click({float(input_box.x) + 5, float(input_box.y) + 5});
                    if (app.notice.find("Поле выбрано") == std::string::npos)
                        throw std::runtime_error("input hit-test/keyboard dispatch failed");
                    field->value("Привет Switch + &");
                    std::string form_error;
                    auto search_url = visual::form_get(submit, app.container.base, form_error);
                    std::string normalized_search = search_url;
                    for (char &ch : normalized_search)
                        ch = char(std::toupper(static_cast<unsigned char>(ch)));
                    if (normalized_search.find("Q=%D0%9F%D1%80") == std::string::npos ||
                        normalized_search.find("%2B%20%26") == std::string::npos ||
                        normalized_search.find("HL=RU") == std::string::npos ||
                        std::count(search_url.begin(), search_url.end(), '&') != 1)
                        throw std::runtime_error("GET form serialization failed: " + form_error +
                                                 " " + search_url);
                    app.rebuild = true;
                    app.pump();
                    field = std::dynamic_pointer_cast<visual::Control>(
                        app.document->root()->select_one("#search"));
                    if (!field || field->value() != "Привет Switch + &")
                        throw std::runtime_error("form value lost on relayout");
                    link = app.document->root()->select_one("#jump");
                    auto box = link->get_placement();
                    app.click({float(box.x) + 4, float(box.y) + 4});
                    if (app.view.y <= 0)
                        throw std::runtime_error(
                            "painted anchor hit-test/fragment navigation failed");
                    app.paint();
                    if (app.container.painted_images < 1)
                        throw std::runtime_error("below-fold JPEG paint failed");
                    printf("PASS external CSS, flex layout, Russian text, PNG/JPEG paint, "
                           "scrolling and anchor hit testing\n");
                    app.cycle_viewport();
                    app.cycle_viewport();
                    app.pump();
                    if (app.container.viewport_width != 640 || app.view.scale() != 2)
                        throw std::runtime_error("mobile viewport failed");
                    field = std::dynamic_pointer_cast<visual::Control>(
                        app.document->root()->select_one("#search"));
                    if (!field || field->value() != "Привет Switch + &")
                        throw std::runtime_error("form value lost on responsive relayout");
                    auto mobile_box = field->get_placement();
                    app.view.y = std::max(0.f, float(mobile_box.y) - 80);
                    app.view.clamp();
                    app.notice.clear();
                    app.click({(float(mobile_box.x) + 5 - app.view.x) * app.view.scale(),
                               (float(mobile_box.y) + 5 - app.view.y) * app.view.scale()});
                    if (app.notice.find("Поле выбрано") == std::string::npos)
                        throw std::runtime_error("mobile input hit-test failed");
                    auto action = app.document->root()->select_one("#js-action");
                    auto action_box = action->get_placement();
                    app.view.y = std::max(0.f, float(action_box.y) - 80);
                    app.view.clamp();
                    app.click({(float(action_box.x) + 5 - app.view.x) * app.view.scale(),
                               (float(action_box.y) + 5 - app.view.y) * app.view.scale()});
                    app.pump();
                    std::string action_text;
                    app.document->root()->select_one("#js-result")->get_text(action_text);
                    if (action_text != "Нажатий: 1")
                        throw std::runtime_error("painted JavaScript button failed");
                    auto fetch_button = app.document->root()->select_one("#js-fetch");
                    auto fetch_box = fetch_button->get_placement();
                    app.view.y = std::max(0.f, float(fetch_box.y) - 80);
                    app.view.clamp();
                    app.click({(float(fetch_box.x) + 5 - app.view.x) * app.view.scale(),
                               (float(fetch_box.y) + 5 - app.view.y) * app.view.scale()});
                    app.pump();
                    app.pump();
                    std::string fetch_text;
                    app.document->root()->select_one("#fetch-result")->get_text(fetch_text);
                    if (fetch_text.find("JSON загружен") == std::string::npos)
                        throw std::runtime_error("painted async JavaScript button failed: " +
                                                 app.scripts->error());
                    auto tap_element = [&](const char *id) {
                        auto el = app.document->root()->select_one(std::string("#") + id);
                        if (!el)
                            throw std::runtime_error("Missing form demo control");
                        auto box = el->get_placement();
                        app.view.y = std::max(0.f, float(box.y) - 80);
                        app.view.clamp();
                        app.click({(float(box.x) + 5 - app.view.x) * app.view.scale(),
                                   (float(box.y) + 5 - app.view.y) * app.view.scale()});
                        app.pump();
                    };
                    auto widget = [&](const char *id) {
                        return std::dynamic_pointer_cast<visual::Control>(
                            app.document->root()->select_one(std::string("#") + id));
                    };
                    tap_element("demo-check");
                    std::string state;
                    app.document->root()->select_one("#forms-state")->get_text(state);
                    if (widget("demo-check")->checked() || state != "Выключено")
                        throw std::runtime_error("checkbox preactivation/event state failed");
                    tap_element("demo-radio-b");
                    if (widget("demo-radio-a")->checked() || !widget("demo-radio-b")->checked())
                        throw std::runtime_error("radio activation failed");
                    tap_element("demo-select");
                    if (!app.choices)
                        throw std::runtime_error("select did not open");
                    app.paint();
                    SDL_Surface *menu =
                        SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
                    if (!menu ||
                        SDL_RenderReadPixels(renderer, nullptr, menu->format->format, menu->pixels,
                                             menu->pitch) ||
                        SDL_SaveBMP(menu, (std::string(argv[4]) + ".choices.bmp").c_str()))
                        throw std::runtime_error(SDL_GetError());
                    SDL_FreeSurface(menu);
                    app.click({float(visual::Choices::x + 80),
                               float(visual::Choices::list_y + 2 * visual::Choices::row + 20)});
                    if (!app.choices || widget("demo-select")->value() != "ru")
                        throw std::runtime_error("disabled option selected");
                    float page_scroll = app.view.y;
                    app.touch_down(41, {300, 300}, 1000);
                    app.touch_move(41, {300, 180}, 1050);
                    app.touch_up(41, {300, 180}, 1060);
                    if (!app.choices || app.choices->view.y <= 0 || app.view.y != page_scroll)
                        throw std::runtime_error("select gesture/page isolation failed");
                    app.scroll(0, 10000);
                    app.click({float(visual::Choices::x + 80),
                               float(visual::Choices::list_y + visual::Choices::row + 20)});
                    app.pump();
                    if (app.choices || widget("demo-select")->value() != "en")
                        throw std::runtime_error("option hit-test failed");
                    tap_element("demo-multi");
                    if (!app.choices)
                        throw std::runtime_error("multiple select did not open");
                    app.click({float(visual::Choices::x + 80),
                               float(visual::Choices::list_y + visual::Choices::row + 20)});
                    app.pump();
                    if (!app.choices || widget("demo-multi")->selected_options().size() != 2)
                        throw std::runtime_error("multiple selection failed");
                    app.click(
                        {float(visual::Choices::x + 80), float(visual::Choices::footer_y + 20)});
                    if (app.choices)
                        throw std::runtime_error("select menu did not close");
                    widget("demo-email")->value("bad");
                    tap_element("demo-submit");
                    if (app.notice.find("Проверьте поле") == std::string::npos)
                        throw std::runtime_error("form validation failed");
                    widget("demo-email")->value("demo@example.test");
                    tap_element("demo-submit");
                    std::string submitted;
                    app.document->root()->select_one("#forms-result")->get_text(submitted);
                    if (submitted != "Готово: en / demo-submit" || app.url != "demo:/index.html")
                        throw std::runtime_error("cancelable form submit/submitter failed");
                    tap_element("demo-reset");
                    if (!widget("demo-check")->checked() || !widget("demo-radio-a")->checked() ||
                        widget("demo-select")->value() != "ru")
                        throw std::runtime_error("native reset failed");
                    tap_element("dynamic-run");
                    for (int i = 0; i < 8; ++i)
                        app.pump();
                    std::string dynamic_result, observed_result;
                    app.document->root()->select_one("#dynamic-target")->get_text(dynamic_result);
                    app.document->root()
                        ->select_one("#dynamic-observed")
                        ->get_text(observed_result);
                    if (dynamic_result != "Запуск: 1" || observed_result != "Изменений: 2")
                        throw std::runtime_error("dynamic demo first run failed: " +
                                                 dynamic_result + " / " + observed_result);
                    tap_element("dynamic-run");
                    for (int i = 0; i < 8; ++i)
                        app.pump();
                    dynamic_result.clear();
                    observed_result.clear();
                    app.document->root()->select_one("#dynamic-target")->get_text(dynamic_result);
                    app.document->root()
                        ->select_one("#dynamic-observed")
                        ->get_text(observed_result);
                    if (dynamic_result != "Запуск: 2" || observed_result != "Изменений: 4")
                        throw std::runtime_error("dynamic demo second run failed");
                    tap_element("network-xhr");
                    for (int i = 0; i < 8; i++)
                        app.pump();
                    std::string network_status;
                    app.document->root()->select_one("#network-status")->get_text(network_status);
                    if (network_status.find("Код 200:") != 0)
                        throw std::runtime_error("offline XHR demo failed: " + network_status);
                    tap_element("network-abort");
                    app.pump();
                    network_status.clear();
                    app.document->root()->select_one("#network-status")->get_text(network_status);
                    if (network_status != "Загрузка отменена")
                        throw std::runtime_error("offline abort demo failed");
                    tap_element("module-run");
                    for (int i = 0; i < 20; i++)
                        app.pump();
                    std::string module_status;
                    app.document->root()->select_one("#module-status")->get_text(module_status);
                    if (module_status != "Запуск модуля: 1")
                        throw std::runtime_error("module demo first run failed: " + module_status);
                    tap_element("module-run");
                    for (int i = 0; i < 20; i++)
                        app.pump();
                    module_status.clear();
                    app.document->root()->select_one("#module-status")->get_text(module_status);
                    if (module_status != "Запуск модуля: 2")
                        throw std::runtime_error("module demo second run failed: " + module_status);
                    float before_report_scroll = app.view.y;
                    app.toggle_report();
                    if (!app.report_open || app.report_lines.size() < 10)
                        throw std::runtime_error("core report not available");
                    app.paint();
                    SDL_Surface *report_shot =
                        SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
                    if (!report_shot ||
                        SDL_RenderReadPixels(renderer, nullptr, report_shot->format->format,
                                             report_shot->pixels, report_shot->pitch) ||
                        SDL_SaveBMP(report_shot, (std::string(argv[4]) + ".report.bmp").c_str()))
                        throw std::runtime_error(SDL_GetError());
                    SDL_FreeSurface(report_shot);
                    app.touch_down(51, {800, 200}, 15000);
                    app.touch_move(51, {800, 100}, 15100);
                    app.touch_up(51, {800, 100}, 15200);
                    if (!app.report_open || app.view.y != before_report_scroll)
                        throw std::runtime_error("core report gesture leaked into page");
                    app.back();
                    if (app.report_open)
                        throw std::runtime_error("core report did not close");
                    app.click({400, -20});
                    if (!app.report_open)
                        throw std::runtime_error("status bar report tap failed");
                    app.back();
                    app.reload();
                    for (int i = 0; i < 5; i++)
                        app.pump();
                    if (!app.document || app.url != "demo:/index.html")
                        throw std::runtime_error("GET reload failed");
                    quit = true;
                }
#endif
                SDL_RenderPresent(renderer);
                SDL_Delay(10);
            }
        } catch (const std::exception &e) {
            fatal = e.what();
            result = 1;
        }
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    browser_profile_close();
    curl_global_cleanup();
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    if (!fatal.empty())
        visual::fatal_screen(fatal);
#ifdef __SWITCH__
    if (sockets)
        socketExit();
    romfsExit();
#endif
    return result;
}
