#pragma once
#include <litehtml.h>
#include <map>
#include <string>
#include <vector>
namespace visual {
struct Forms {
    struct Value {
        std::string text;
        bool initialized = false, dirty = false;
        bool checked = false, checked_initialized = false, checked_dirty = false;
        bool selection_initialized = false, selection_dirty = false;
        std::vector<std::weak_ptr<litehtml::element>> selection;
    };
    std::map<unsigned, Value> values;
    unsigned next = 0;
    void clear() {
        values.clear();
        next = 0;
    }
    void relayout() { next = 0; }
};
litehtml::element::ptr form_owner(litehtml::element::ptr);
bool form_disabled(const litehtml::element::ptr &);
class Control final : public litehtml::html_tag {
    Forms &forms;
    unsigned key;
    void sync_defaults(bool refresh = false) const;
    void uncheck_group() const;

  public:
    Control(const litehtml::document::ptr &, Forms &, unsigned);
    std::string type() const;
    std::string value() const;
    void value(const std::string &);
    bool editable() const;
    bool disabled() const;
    bool is_disabled() const override;
    struct CheckedState {
        bool checked, initialized, dirty;
    };
    CheckedState checked_state() const;
    void restore_checked(CheckedState);
    bool checked() const;
    void checked(bool);
    bool is_checked() const override;
    std::vector<litehtml::element::ptr> options() const;
    std::vector<litehtml::element::ptr> selected_options() const;
    int selected_index() const;
    void selected_index(int);
    bool select_option(const litehtml::element::ptr &, bool selected = true);
    bool multiple() const;
    void reset();
    void parse_attributes() override;
    bool is_replaced() const override { return true; }
    void get_content_size(litehtml::size &, litehtml::pixel_t) override;
    std::shared_ptr<litehtml::render_item>
    create_render_item(const std::shared_ptr<litehtml::render_item> &) override;
    void draw(litehtml::uint_ptr, litehtml::pixel_t, litehtml::pixel_t, const litehtml::position *,
              const std::shared_ptr<litehtml::render_item> &) override;
};
class Option final : public litehtml::html_tag {
  public:
    explicit Option(const litehtml::document::ptr &d) : html_tag(d) {}
    bool is_checked() const override;
    bool is_disabled() const override;
};
std::string option_value(const litehtml::element::ptr &);
std::string option_label(const litehtml::element::ptr &);
const std::string &control_styles();
struct Submission {
    std::string url, method = "GET", body;
    explicit operator bool() const { return !url.empty(); }
};
Submission prepare_form(const litehtml::element::ptr &submitter, const std::string &base,
                        std::string &error, bool validate = true);
// Compatibility helper for GET-only callers.
std::string form_get(const litehtml::element::ptr &, const std::string &, std::string &);
void reset_form(const litehtml::element::ptr &);
std::vector<std::string> form_policies(const litehtml::document::ptr &, std::vector<std::string>);
bool form_action_allowed(const std::vector<std::string> &, const std::string &source,
                         const std::string &target);
} // namespace visual
