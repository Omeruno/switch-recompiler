#include "forms.h"
#include "resources.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <litehtml/render_image.h>
namespace visual {
static std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
Control::Control(const litehtml::document::ptr &doc, Forms &s, unsigned i)
    : html_tag(doc), forms(s), key(i) {}
std::string Control::type() const {
    return std::string(get_tagName()) == "textarea" ? "textarea" : lower(get_attr("type", "text"));
}
bool Control::disabled() const { return get_attr("disabled") != nullptr; }
bool Control::editable() const {
    auto t = type();
    return !disabled() && !get_attr("readonly") &&
           (t == "text" || t == "search" || t == "email" || t == "url" || t == "tel" ||
            t == "number" || t == "password" || t == "textarea");
}
std::string Control::value() const {
    auto i = forms.values.find(key);
    return i != forms.values.end() && i->second.initialized ? i->second.text
                                                            : get_attr("value", "");
}
static std::string bounded_value(const std::string &s) {
    size_t end = std::min(s.size(), size_t(4096));
    while (end < s.size() && end > 0 && (static_cast<unsigned char>(s[end]) & 0xc0) == 0x80)
        --end;
    return s.substr(0, end);
}
void Control::value(const std::string &s) { forms.values[key] = {bounded_value(s), true}; }
void Control::parse_attributes() {
    auto &v = forms.values[key];
    if (!v.initialized) {
        if (type() == "textarea")
            get_text(v.text);
        else
            v.text = get_attr("value", "");
        v.text = bounded_value(v.text);
        v.initialized = true;
    }
    html_tag::parse_attributes();
}
void Control::get_content_size(litehtml::size &s, litehtml::pixel_t) {
    int chars =
        std::clamp(std::atoi(get_attr(type() == "textarea" ? "cols" : "size", "20")), 1, 80);
    s.width = chars * std::max(6.f, float(css().get_font_size()) * .6f);
    s.height = type() == "textarea" ? std::clamp(std::atoi(get_attr("rows", "2")), 1, 20) *
                                          std::max(18.f, float(css().get_font_size()) * 1.3f)
                                    : std::max(22.f, float(css().get_font_size()) * 1.3f);
}
std::shared_ptr<litehtml::render_item>
Control::create_render_item(const std::shared_ptr<litehtml::render_item> &p) {
    auto r = std::make_shared<litehtml::render_item_image>(shared_from_this());
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
    std::string text = value();
    auto t = type();
    if (t == "password") {
        size_t count = 0;
        for (unsigned char ch : text)
            if ((ch & 0xc0) != 0x80)
                ++count;
        text.assign(std::min(size_t(128), count), '*');
    }
    if (text.empty())
        text = get_attr("placeholder", (t == "submit"  ? "Отправить"
                                        : t == "reset" ? "Сбросить"
                                                       : " "));
    c->set_clip(p, {});
    size_t start = 0;
    int line = 0;
    float lineheight = std::max(18.f, float(css().get_font_size()) * 1.3f);
    do {
        size_t end = text.find('\n', start);
        std::string part = text.substr(start, end == std::string::npos ? end : end - start);
        auto pos = p;
        pos.y += line * lineheight;
        if (float(pos.y) >= float(p.bottom()))
            break;
        c->draw_text(hdc, part.c_str(), css().get_font(),
                     disabled() ? litehtml::web_color(110, 110, 110) : css().get_color(), pos);
        ++line;
        if (end == std::string::npos || t != "textarea")
            break;
        start = end + 1;
    } while (start < text.size());
    c->del_clip();
}
const std::string &control_styles() {
    static const std::string s = R"CSS(
input,textarea,button {font-family:inherit; font-size:16px; line-height:1.3; vertical-align:middle; padding:8px 10px; border:1px solid #7a8699; border-radius:5px; background-color:#fff; color:#172438; }
input[type=hidden] {display:none;} input[type=submit],input[type=button],button {background-color:#e5ebf2;cursor:pointer;}
input[disabled],textarea[disabled],button[disabled] {background-color:#e5e7eb;color:#747d8c;}
)CSS";
    return s;
}
static litehtml::element::ptr owner(litehtml::element::ptr el) {
    for (; el; el = el->parent())
        if (std::string(el->get_tagName()) == "form")
            return el;
    return {};
}
std::string form_get(const litehtml::element::ptr &button, const std::string &base,
                     std::string &error) {
    error.clear();
    auto form = owner(button);
    if (!form) {
        error = "У кнопки нет формы";
        return {};
    }
    if (lower(button->get_attr("formmethod", form->get_attr("method", "get"))) != "get") {
        error = "Отправка этой формы пока не поддерживается";
        return {};
    }
    auto action = Resources::resolve(
        base, button->get_attr("formaction", form->get_attr("action", base.c_str())));
    if (action.rfind("https://", 0) != 0) {
        error = "Форме нужен HTTPS-адрес";
        return {};
    }
    std::string query;
    auto add = [&](const std::string &name, const std::string &value) {
        char *n = curl_easy_escape(nullptr, name.c_str(), int(name.size())),
             *v = curl_easy_escape(nullptr, value.c_str(), int(value.size()));
        if (n && v) {
            if (!query.empty())
                query += '&';
            query += n;
            query += '=';
            query += v;
        }
        curl_free(n);
        curl_free(v);
    };
    // litehtml's selector API accepts one selector, not a comma-separated list.
    for (auto &el : form->select_all("*")) {
        std::string tag = el->get_tagName();
        if (tag != "input" && tag != "textarea" && tag != "button")
            continue;
        if (owner(el) != form || el->get_attr("disabled"))
            continue;
        std::string name = el->get_attr("name", "");
        if (name.empty())
            continue;
        std::string type = lower(
            el->get_attr("type", std::string(el->get_tagName()) == "button" ? "submit" : "text"));
        if (type == "password") {
            error = "Отправка формы с паролем пока не поддерживается";
            return {};
        }
        if (type == "file" || type == "checkbox" || type == "radio" || type == "image") {
            error = "Этот тип поля формы пока не подключён";
            return {};
        }
        if (type == "reset" || type == "button" ||
            ((type == "submit" || std::string(el->get_tagName()) == "button") && el != button))
            continue;
        auto control = std::dynamic_pointer_cast<Control>(el);
        add(name, control ? control->value() : el->get_attr("value", ""));
    }
    CURLU *u = curl_url();
    char *out = nullptr;
    bool ok = u && curl_url_set(u, CURLUPART_URL, action.c_str(), 0) == CURLUE_OK &&
              curl_url_set(u, CURLUPART_QUERY, query.c_str(), 0) == CURLUE_OK &&
              curl_url_get(u, CURLUPART_URL, &out, 0) == CURLUE_OK;
    std::string result = ok ? out : "";
    curl_free(out);
    curl_url_cleanup(u);
    if (!ok)
        error = "Не удалось подготовить адрес формы";
    return result;
}
} // namespace visual
