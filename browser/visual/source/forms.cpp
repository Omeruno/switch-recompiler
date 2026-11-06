#include "forms.h"
#include "container.h"
#include "resources.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <litehtml/render_image.h>
#include <sstream>
namespace visual {
static std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
static bool is_tag(const litehtml::element::ptr &el, const char *name) {
    return el && std::string(el->get_tagName()) == name;
}
static bool inside(litehtml::element::ptr el, const litehtml::element::ptr &ancestor) {
    for (; el; el = el->parent())
        if (el == ancestor)
            return true;
    return false;
}
litehtml::element::ptr form_owner(litehtml::element::ptr el) {
    if (!el)
        return {};
    if (const char *id = el->get_attr("form")) {
        if (!*id)
            return {};
        for (auto &f : el->get_document()->root()->select_all("form"))
            if (std::string(id) == f->get_attr("id", ""))
                return f;
        return {};
    }
    for (; el; el = el->parent())
        if (is_tag(el, "form"))
            return el;
    return {};
}
bool form_disabled(const litehtml::element::ptr &el) {
    if (!el)
        return true;
    if (el->get_attr("disabled"))
        return true;
    for (auto p = el->parent(); p; p = p->parent()) {
        if (is_tag(p, "optgroup") && p->get_attr("disabled"))
            return true;
        if (is_tag(p, "fieldset") && p->get_attr("disabled")) {
            litehtml::element::ptr legend;
            for (auto &c : p->children())
                if (is_tag(c, "legend")) {
                    legend = c;
                    break;
                }
            if (!legend || !inside(el, legend))
                return true;
        }
    }
    return false;
}
static std::string compact(const std::string &s) {
    std::string out;
    bool space = false;
    for (unsigned char c : s) {
        if (std::isspace(c)) {
            space = !out.empty();
            continue;
        }
        if (space)
            out += ' ';
        space = false;
        out += char(c);
    }
    return out;
}
std::string option_value(const litehtml::element::ptr &el) {
    if (auto *v = el->get_attr("value"))
        return v;
    std::string s;
    el->get_text(s);
    return compact(s);
}
std::string option_label(const litehtml::element::ptr &el) {
    if (auto *v = el->get_attr("label"))
        return v;
    std::string s;
    el->get_text(s);
    return compact(s);
}
Control::Control(const litehtml::document::ptr &d, Forms &s, unsigned i)
    : html_tag(d), forms(s), key(i) {}
std::string Control::type() const {
    auto name = std::string(get_tagName());
    if (name == "textarea" || name == "select")
        return name;
    return lower(get_attr("type", "text"));
}
bool Control::disabled() const {
    return form_disabled(const_cast<Control *>(this)->shared_from_this());
}
bool Control::is_disabled() const { return disabled(); }
bool Control::editable() const {
    auto t = type();
    return !disabled() && !get_attr("readonly") &&
           (t == "text" || t == "search" || t == "email" || t == "url" || t == "tel" ||
            t == "number" || t == "password" || t == "textarea");
}
bool Control::multiple() const { return type() == "select" && get_attr("multiple"); }
std::vector<litehtml::element::ptr> Control::options() const {
    std::vector<litehtml::element::ptr> out;
    for (auto &el : const_cast<Control *>(this)->select_all("option")) {
        auto p = el->parent();
        while (p && !is_tag(p, "select"))
            p = p->parent();
        if (p.get() == this)
            out.push_back(el);
    }
    return out;
}
void Control::uncheck_group() const {
    std::string name = get_attr("name", "");
    if (name.empty())
        return;
    auto self = const_cast<Control *>(this)->shared_from_this();
    auto owner = form_owner(self);
    for (auto &el : get_document()->root()->select_all("input"))
        if (auto c = std::dynamic_pointer_cast<Control>(el)) {
            if (c.get() != this && c->type() == "radio" && name == c->get_attr("name", "") &&
                form_owner(c) == owner) {
                auto &other = forms.values[c->key];
                if (other.checked_initialized && other.checked) {
                    other.checked = false;
                    other.checked_dirty = true;
                }
            }
        }
}
static std::string bounded(const std::string &);
void Control::sync_defaults(bool refresh) const {
    auto &v = forms.values[key];
    auto t = type();
    if (!v.initialized || (refresh && !v.dirty)) {
        v.text.clear();
        if (t == "textarea")
            get_text(v.text);
        else
            v.text = get_attr("value", (t == "checkbox" || t == "radio") ? "on" : "");
        v.text = bounded(v.text);
        v.initialized = true;
    }
    if (!v.checked_initialized || (refresh && !v.checked_dirty)) {
        v.checked = get_attr("checked") != nullptr;
        v.checked_initialized = true;
        if (t == "radio" && v.checked)
            uncheck_group();
    }
    if (t == "select" && (!v.selection_initialized || (refresh && !v.selection_dirty))) {
        v.selection.clear();
        auto list = options();
        for (auto &o : list)
            if (o->get_attr("selected")) {
                if (!multiple())
                    v.selection.clear();
                v.selection.push_back(o);
            }
        if (!multiple() && v.selection.empty())
            for (auto &o : list)
                if (!form_disabled(o)) {
                    v.selection.push_back(o);
                    break;
                }
        v.selection_initialized = true;
    }
}
static std::string bounded(const std::string &s) {
    size_t n = std::min(s.size(), size_t(4096));
    while (n && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xc0) == 0x80)
        --n;
    return s.substr(0, n);
}
std::vector<litehtml::element::ptr> Control::selected_options() const {
    sync_defaults();
    auto list = options();
    std::vector<litehtml::element::ptr> out;
    for (auto &o : list)
        for (auto &weak : forms.values[key].selection)
            if (weak.lock() == o) {
                out.push_back(o);
                break;
            }
    return out;
}
std::string Control::value() const {
    if (type() == "select") {
        auto list = selected_options();
        return list.empty() ? "" : option_value(list.front());
    }
    sync_defaults();
    return forms.values[key].text;
}
void Control::value(const std::string &s) {
    sync_defaults();
    if (type() == "select") {
        auto &v = forms.values[key];
        v.selection.clear();
        v.selection_dirty = true;
        for (auto &o : options())
            if (option_value(o) == s) {
                v.selection.push_back(o);
                break;
            }
    } else {
        auto &v = forms.values[key];
        v.text = bounded(s);
        v.dirty = true;
    }
}
Control::CheckedState Control::checked_state() const {
    sync_defaults();
    const auto &v = forms.values[key];
    return {v.checked, v.checked_initialized, v.checked_dirty};
}
void Control::restore_checked(CheckedState state) {
    auto &v = forms.values[key];
    v.checked = state.checked;
    v.checked_initialized = state.initialized;
    v.checked_dirty = state.dirty;
}
bool Control::checked() const {
    sync_defaults();
    return forms.values[key].checked;
}
void Control::checked(bool yes) {
    sync_defaults();
    auto &v = forms.values[key];
    v.checked = yes;
    v.checked_dirty = true;
    if (type() == "radio" && yes)
        uncheck_group();
}
bool Control::is_checked() const {
    return (type() == "checkbox" || type() == "radio") && checked();
}
int Control::selected_index() const {
    auto selected = selected_options();
    if (selected.empty())
        return -1;
    auto all = options();
    return int(std::find(all.begin(), all.end(), selected.front()) - all.begin());
}
void Control::selected_index(int index) {
    sync_defaults();
    auto &v = forms.values[key];
    v.selection_dirty = true;
    v.selection.clear();
    auto all = options();
    if (index >= 0 && size_t(index) < all.size())
        v.selection.push_back(all[size_t(index)]);
}
bool Control::select_option(const litehtml::element::ptr &option, bool selected) {
    sync_defaults();
    auto all = options();
    if (std::find(all.begin(), all.end(), option) == all.end())
        return false;
    auto &v = forms.values[key];
    v.selection_dirty = true;
    if (!multiple() && selected)
        v.selection.clear();
    v.selection.erase(std::remove_if(v.selection.begin(), v.selection.end(),
                                     [&](auto &w) { return w.expired() || w.lock() == option; }),
                      v.selection.end());
    if (selected)
        v.selection.push_back(option);
    return true;
}
void Control::reset() {
    auto &v = forms.values[key];
    v.dirty = v.checked_dirty = v.selection_dirty = false;
    sync_defaults(true);
}
void Control::parse_attributes() {
    sync_defaults(true);
    html_tag::parse_attributes();
}
void Control::get_content_size(litehtml::size &s, litehtml::pixel_t) {
    auto t = type();
    if (t == "checkbox" || t == "radio") {
        s.width = 22;
        s.height = 22;
        return;
    }
    int chars =
        t == "select"
            ? 20
            : std::clamp(std::atoi(get_attr(t == "textarea" ? "cols" : "size", "20")), 1, 80);
    s.width = chars * std::max(6.f, float(css().get_font_size()) * .6f);
    s.height = t == "textarea" ? std::clamp(std::atoi(get_attr("rows", "2")), 1, 20) *
                                     std::max(18.f, float(css().get_font_size()) * 1.3f)
                               : std::max(22.f, float(css().get_font_size()) * 1.3f);
}
std::shared_ptr<litehtml::render_item>
Control::create_render_item(const std::shared_ptr<litehtml::render_item> &p) {
    // Form controls have independent intrinsic width and height. An image renderer
    // incorrectly scales their height when CSS changes only their width.
    class RenderControl final : public litehtml::render_item_image {
        litehtml::pixel_t _render(litehtml::pixel_t x, litehtml::pixel_t y,
                                  const litehtml::containing_block_context &cb,
                                  litehtml::formatting_context *, bool) override {
            using CB = litehtml::containing_block_context;
            calc_outlines(cb.width);
            auto size = calculate_containing_block_context(cb);
            litehtml::size intrinsic;
            src_el()->get_content_size(intrinsic, cb.width);
            auto constrain = [](litehtml::pixel_t value, const CB::typed_pixel &min,
                                const CB::typed_pixel &max) {
                if (max.type != CB::cbc_value_type_none && max.type != CB::cbc_value_type_auto)
                    value = std::min(value, max.value);
                if (min.type != CB::cbc_value_type_none && min.type != CB::cbc_value_type_auto)
                    value = std::max(value, min.value);
                return std::max(litehtml::pixel_t(0), value);
            };
            m_pos.move_to(x + content_offset_left(), y + content_offset_top());
            m_pos.width =
                constrain(size.width.type == CB::cbc_value_type_auto ? intrinsic.width
                                                                     : size.render_width.value,
                          size.min_width, size.max_width);
            bool auto_height = size.height.type == CB::cbc_value_type_auto ||
                               (size.height.type == CB::cbc_value_type_percentage &&
                                cb.height.type == CB::cbc_value_type_auto);
            m_pos.height =
                constrain(auto_height ? intrinsic.height : size.height.value - box_sizing_height(),
                          size.min_height, size.max_height);
            return m_pos.width + content_offset_width();
        }

      public:
        explicit RenderControl(const litehtml::element::ptr &el) : render_item_image(el) {}
        std::shared_ptr<litehtml::render_item> clone() override {
            return std::make_shared<RenderControl>(src_el());
        }
    };
    auto r = std::make_shared<RenderControl>(shared_from_this());
    r->parent(p);
    return r;
}
void Control::draw(litehtml::uint_ptr hdc, litehtml::pixel_t x, litehtml::pixel_t y,
                   const litehtml::position *clip,
                   const std::shared_ptr<litehtml::render_item> &ri) {
    html_tag::draw(hdc, x, y, clip, ri);
    auto p = ri->pos();
    p.x += x;
    p.y += y;
    if (!p.does_intersect(clip))
        return;
    auto *c = get_document()->container();
    auto t = type();
    auto *native = dynamic_cast<Container *>(c);
    bool focused = native && native->focused().get() == this;
    bool text_field = editable() || (get_attr("readonly") && (t == "text" || t == "textarea" ||
                                                              t == "password" || t == "search"));
    bool has_border = false;
    auto &borders = css().get_borders();
    for (auto side : {borders.top, borders.right, borders.bottom, borders.left})
        has_border = has_border || (side.width.val() > 0 && side.color.alpha &&
                                    side.style != litehtml::border_style_none &&
                                    side.style != litehtml::border_style_hidden);
    // A native affordance keeps transparent, borderless fields discoverable even
    // when a site's shadow/outline based decoration is unsupported.
    if (focused || (text_field && !has_border)) {
        auto frame = p;
        frame.x -= ri->padding_left() + ri->border_left();
        frame.y -= ri->padding_top() + ri->border_top();
        frame.width +=
            ri->padding_left() + ri->padding_right() + ri->border_left() + ri->border_right();
        frame.height +=
            ri->padding_top() + ri->padding_bottom() + ri->border_top() + ri->border_bottom();
        litehtml::borders outline;
        for (auto side : {&outline.top, &outline.right, &outline.bottom, &outline.left}) {
            side->width = focused ? 2 : 1;
            side->style = litehtml::border_style_solid;
            side->color =
                focused ? litehtml::web_color(24, 100, 200) : litehtml::web_color(122, 134, 153);
        }
        c->draw_borders(hdc, outline, frame, false);
    }
    std::string text = value();
    if (t == "checkbox" || t == "radio")
        text = checked() ? (t == "radio" ? "●" : "✓") : (t == "radio" ? "○" : " ");
    if (t == "select") {
        auto list = selected_options();
        text = list.empty() ? "Выберите…" : option_label(list.front());
        if (list.size() > 1)
            text += " (+" + std::to_string(list.size() - 1) + ")";
        text += "  ▾";
    }
    if (t == "password") {
        size_t n = 0;
        for (unsigned char ch : text)
            if ((ch & 0xc0) != 0x80)
                ++n;
        text.assign(std::min(size_t(128), n), '*');
    }
    bool placeholder = text.empty();
    if (placeholder)
        text = get_attr("placeholder", t == "submit"  ? "Отправить"
                                       : t == "reset" ? "Сбросить"
                                                      : " ");
    c->set_clip(p, {});
    size_t start = 0;
    int line = 0;
    float lineheight = std::max(18.f, float(css().get_font_size()) * 1.3f);
    // Fit UTF-8 at character boundaries before creating a texture. Long input
    // strings previously exceeded the text texture quota and vanished entirely.
    auto fit = [&](const std::string &value, bool tail) {
        std::vector<size_t> offsets{0};
        for (size_t i = 1; i < value.size(); ++i)
            if ((static_cast<unsigned char>(value[i]) & 0xc0) != 0x80)
                offsets.push_back(i);
        offsets.push_back(value.size());
        size_t lo = 0, hi = offsets.size() - 1;
        while (lo < hi) {
            size_t mid = (lo + hi + 1) / 2;
            auto piece = tail ? value.substr(offsets[offsets.size() - 1 - mid])
                              : value.substr(0, offsets[mid]);
            if (c->text_width(piece.c_str(), css().get_font()) <= p.width)
                lo = mid;
            else
                hi = mid - 1;
        }
        return tail ? value.substr(offsets[offsets.size() - 1 - lo]) : value.substr(0, offsets[lo]);
    };
    do {
        size_t end = text.find('\n', start);
        auto part = text.substr(start, end == std::string::npos ? end : end - start);
        auto fitted = fit(part, focused && !placeholder && t != "textarea");
        bool wrap = t == "textarea" && lower(get_attr("wrap", "soft")) != "off";
        size_t consumed = part.size();
        if (wrap && fitted.size() < part.size()) {
            if (fitted.empty())
                break;
            auto space = fitted.find_last_of(" \t");
            if (space != std::string::npos && space > 0)
                fitted.resize(space + 1);
            consumed = fitted.size();
        }
        auto pos = p;
        pos.y += line * lineheight;
        if (float(pos.y) >= float(p.bottom()))
            break;
        c->draw_text(hdc, fitted.c_str(), css().get_font(),
                     disabled() || placeholder ? litehtml::web_color(110, 110, 110)
                                               : css().get_color(),
                     pos);
        ++line;
        if (t != "textarea")
            break;
        if (wrap && consumed < part.size())
            start += consumed;
        else if (end != std::string::npos)
            start = end + 1;
        else
            break;
    } while (start < text.size());
    c->del_clip();
}
bool Option::is_checked() const {
    auto self = const_cast<Option *>(this)->shared_from_this();
    auto p = parent();
    while (p && !is_tag(p, "select"))
        p = p->parent();
    if (auto c = std::dynamic_pointer_cast<Control>(p)) {
        auto list = c->selected_options();
        return std::find(list.begin(), list.end(), self) != list.end();
    }
    return get_attr("selected") != nullptr;
}
bool Option::is_disabled() const {
    return form_disabled(const_cast<Option *>(this)->shared_from_this());
}
const std::string &control_styles() {
    static const std::string s = R"CSS(
main,figure,figcaption,search {display:block;} [hidden],noscript {display:none;}
input,textarea,select,button {font-family:inherit;font-size:16px;line-height:1.3;vertical-align:middle;padding:8px 10px;border:1px solid #7a8699;border-radius:5px;background-color:#fff;color:#172438;}
input[type=hidden] {display:none;} input[type=submit],input[type=button],input[type=reset],button {background-color:#e5ebf2;cursor:pointer;}
input[type=checkbox],input[type=radio] {padding:2px;width:22px;height:22px;}
input[type=radio] {border:0;background-color:transparent;}
input:disabled,textarea:disabled,select:disabled,button:disabled {background-color:#e5e7eb;color:#747d8c;}
)CSS";
    return s;
}
static std::vector<litehtml::element::ptr> controls(const litehtml::element::ptr &form) {
    std::vector<litehtml::element::ptr> out;
    for (auto &el : form->get_document()->root()->select_all("*")) {
        auto t = std::string(el->get_tagName());
        if ((t == "input" || t == "textarea" || t == "select" || t == "button") &&
            form_owner(el) == form)
            out.push_back(el);
    }
    return out;
}
void reset_form(const litehtml::element::ptr &el) {
    auto form = form_owner(el);
    if (!form)
        return;
    for (auto &item : controls(form))
        if (auto c = std::dynamic_pointer_cast<Control>(item))
            c->reset();
}
Submission prepare_form(const litehtml::element::ptr &submitter, const std::string &base,
                        std::string &error, bool validate) {
    error.clear();
    Submission result;
    auto form = form_owner(submitter);
    if (!form) {
        error = "У кнопки нет формы";
        return {};
    }
    result.method =
        lower(submitter->get_attr("formmethod", form->get_attr("method", "get"))) == "post" ? "POST"
                                                                                            : "GET";
    if (result.method == "POST" &&
        lower(submitter->get_attr(
            "formenctype", form->get_attr("enctype", "application/x-www-form-urlencoded"))) !=
            "application/x-www-form-urlencoded") {
        error = "Этот формат отправки формы пока не поддерживается";
        return {};
    }
    result.url = Resources::resolve(
        base, submitter->get_attr("formaction", form->get_attr("action", base.c_str())));
    if (result.url.rfind("https://", 0) != 0) {
        error = "Форме нужен HTTPS-адрес";
        return {};
    }
    validate = validate && !form->get_attr("novalidate") && !submitter->get_attr("formnovalidate");
    auto all = controls(form);
    std::string query;
    auto invalid = [&](const litehtml::element::ptr &el) {
        error = "Проверьте поле: " +
                std::string(el->get_attr("aria-label", el->get_attr("name", "обязательное поле")));
    };
    auto normalize_lines = [](const std::string &text) {
        std::string out;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\r' || text[i] == '\n') {
                if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                    ++i;
                out += "\r\n";
            } else
                out += text[i];
        }
        return out;
    };
    auto add = [&](const std::string &name, const std::string &value) {
        auto normalized_name = normalize_lines(name), normalized_value = normalize_lines(value);
        char *n = curl_easy_escape(nullptr, normalized_name.c_str(), int(normalized_name.size())),
             *v = curl_easy_escape(nullptr, normalized_value.c_str(), int(normalized_value.size()));
        if (!n || !v) {
            curl_free(n);
            curl_free(v);
            return false;
        }
        if (!query.empty())
            query += '&';
        query += n;
        query += '=';
        query += v;
        curl_free(n);
        curl_free(v);
        return query.size() <= 65536;
    };
    for (auto &el : all) {
        if (form_disabled(el))
            continue;
        auto c = std::dynamic_pointer_cast<Control>(el);
        std::string t = c ? c->type() : lower(el->get_attr("type", "submit"));
        auto value = c ? c->value() : el->get_attr("value", "");
        if (validate && c && !el->get_attr("readonly") && t != "hidden") {
            bool bad = false;
            if (el->get_attr("required")) {
                if (t == "checkbox")
                    bad = !c->checked();
                else if (t == "radio") {
                    bool checked = false;
                    auto name = std::string(el->get_attr("name", ""));
                    for (auto &peer : all)
                        if (auto radio = std::dynamic_pointer_cast<Control>(peer))
                            if (radio->type() == "radio" &&
                                ((!name.empty() && name == radio->get_attr("name", "")) ||
                                 radio == c) &&
                                radio->checked())
                                checked = true;
                    bad = !checked;
                } else if (t == "text" || t == "search" || t == "password" || t == "email" ||
                           t == "url" || t == "tel" || t == "number" || t == "textarea" ||
                           t == "select")
                    bad = value.empty();
            }
            if (!value.empty() && t == "email")
                bad = bad || value.find('@') == std::string::npos || value.front() == '@' ||
                      value.back() == '@' ||
                      std::any_of(value.begin(), value.end(),
                                  [](unsigned char ch) { return std::isspace(ch); });
            if (!value.empty() && t == "number") {
                char *end = nullptr;
                double n = std::strtod(value.c_str(), &end);
                bad = bad || !end || *end || !std::isfinite(n);
                for (auto attr : {"min", "max"})
                    if (auto limit = el->get_attr(attr)) {
                        char *limit_end = nullptr;
                        double bound = std::strtod(limit, &limit_end);
                        if (limit_end != limit && !*limit_end && std::isfinite(bound))
                            bad = bad || (std::string(attr) == "min" ? n < bound : n > bound);
                    }
            }
            if (bad) {
                invalid(el);
                return {};
            }
        }
        std::string name = el->get_attr("name", "");
        if (name.empty())
            continue;
        if (t == "reset" || t == "button" ||
            ((t == "submit" || is_tag(el, "button")) && el != submitter))
            continue;
        if (t == "file" || t == "image") {
            error = "Этот тип поля формы пока не подключён";
            return {};
        }
        if (t == "password" && result.method != "POST") {
            error = "Отправка формы с паролем требует POST";
            return {};
        }
        if ((t == "checkbox" || t == "radio") && (!c || !c->checked()))
            continue;
        if (t == "select" && c) {
            for (auto &o : c->selected_options())
                if (!form_disabled(o) && !add(name, option_value(o))) {
                    error = "Форма превышает лимит данных";
                    return {};
                }
        } else if (!add(name, value)) {
            error = "Форма превышает лимит данных";
            return {};
        }
    }
    if (result.method == "POST")
        result.body = std::move(query);
    else {
        CURLU *u = curl_url();
        char *out = nullptr;
        bool ok = u && curl_url_set(u, CURLUPART_URL, result.url.c_str(), 0) == CURLUE_OK &&
                  curl_url_set(u, CURLUPART_QUERY, query.c_str(), 0) == CURLUE_OK &&
                  curl_url_get(u, CURLUPART_URL, &out, 0) == CURLUE_OK;
        if (ok)
            result.url = out;
        curl_free(out);
        curl_url_cleanup(u);
        if (!ok) {
            error = "Не удалось подготовить адрес формы";
            return {};
        }
    }
    return result;
}
std::string form_get(const litehtml::element::ptr &b, const std::string &base, std::string &error) {
    auto r = prepare_form(b, base, error);
    if (r && r.method != "GET") {
        error = "Для этой формы нужен POST";
        return {};
    }
    return r.url;
}
std::vector<std::string> form_policies(const litehtml::document::ptr &doc,
                                       std::vector<std::string> out) {
    if (doc)
        for (auto &m : doc->root()->select_all("meta"))
            if (lower(m->get_attr("http-equiv", "")) == "content-security-policy")
                out.push_back(m->get_attr("content", ""));
    return out;
}
static std::string origin(const std::string &url) {
    CURLU *u = curl_url();
    char *host = nullptr, *port = nullptr;
    std::string out;
    if (u && curl_url_set(u, CURLUPART_URL, url.c_str(), 0) == CURLUE_OK &&
        curl_url_get(u, CURLUPART_HOST, &host, 0) == CURLUE_OK) {
        curl_url_get(u, CURLUPART_PORT, &port, CURLU_DEFAULT_PORT);
        out = lower(host) + ":" + (port ? port : "443");
    }
    curl_free(host);
    curl_free(port);
    curl_url_cleanup(u);
    return out;
}
bool form_action_allowed(const std::vector<std::string> &policies, const std::string &source,
                         const std::string &target) {
    if (Resources::resolve("", target).empty())
        return false;
    for (const auto &policy : policies) {
        bool found = false, yes = false;
        size_t p = 0;
        while (p < policy.size()) {
            auto end = policy.find(';', p);
            std::istringstream tokens(policy.substr(p, end == std::string::npos ? end : end - p));
            std::string name, t;
            tokens >> name;
            if (lower(name) == "form-action") {
                found = true;
                while (tokens >> t) {
                    if (t == "*" || t == "https:" ||
                        (t == "'self'" && origin(source) == origin(target)))
                        yes = true;
                    else if (t.rfind("https://", 0) == 0 && origin(t) == origin(target)) {
                        CURLU *a = curl_url(), *b = curl_url();
                        char *ap = nullptr, *bp = nullptr, *query = nullptr, *fragment = nullptr;
                        bool valid = a && b &&
                                     curl_url_set(a, CURLUPART_URL, t.c_str(), 0) == CURLUE_OK &&
                                     curl_url_set(b, CURLUPART_URL, target.c_str(), 0) == CURLUE_OK;
                        if (valid) {
                            curl_url_get(a, CURLUPART_PATH, &ap, 0);
                            curl_url_get(b, CURLUPART_PATH, &bp, 0);
                            bool suffix =
                                curl_url_get(a, CURLUPART_QUERY, &query, 0) == CURLUE_OK ||
                                curl_url_get(a, CURLUPART_FRAGMENT, &fragment, 0) == CURLUE_OK;
                            std::string path = ap ? ap : "/", actual = bp ? bp : "/";
                            if (!suffix &&
                                (path == "/" || (path.back() == '/' ? actual.rfind(path, 0) == 0
                                                                    : actual == path)))
                                yes = true;
                        }
                        curl_free(ap);
                        curl_free(bp);
                        curl_free(query);
                        curl_free(fragment);
                        curl_url_cleanup(a);
                        curl_url_cleanup(b);
                    }
                }
                break;
            }
            if (end == std::string::npos)
                break;
            p = end + 1;
        }
        if (found && !yes)
            return false;
    }
    return true;
}
} // namespace visual
