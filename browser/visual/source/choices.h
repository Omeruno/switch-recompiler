#pragma once
#include "forms.h"
#include "interaction.h"
namespace visual {
struct Choices {
    static constexpr int x = 180, y = 40, width = 920, list_y = 90, row = 44, visible = 8,
                         footer_y = list_y + row * visible;
    std::shared_ptr<Control> control;
    std::vector<litehtml::element::ptr> items;
    View view;
    Gestures gestures;
    int hover = 0;
    bool pad_navigation = false;
    explicit Choices(std::shared_ptr<Control> c)
        : control(std::move(c)), items(control->options()), gestures(view) {
        view.viewport_width = width;
        view.viewport_height = row * visible;
        view.content_width = width;
        view.content_height = float(items.size() * row);
        hover = std::max(0, control->selected_index());
        view.y = float(std::max(0, hover - visible / 2) * row);
        view.clamp();
        gestures.zoom_enabled = false;
    }
    Point local(Point p) const { return {p.x - x, p.y - list_y}; }
    int hit(Point p) const {
        if (p.x < x || p.x >= x + width || p.y < list_y || p.y >= footer_y)
            return -1;
        int i = int((p.y - list_y + view.y) / row);
        return i >= 0 && size_t(i) < items.size() ? i : -1;
    }
    void scroll(float pixels) { view.drag(0, pixels); }
    void move(int delta) {
        pad_navigation = true;
        if (items.empty())
            return;
        hover = std::clamp(hover + delta, 0, int(items.size()) - 1);
        float top = hover * row;
        if (top < view.y)
            view.y = top;
        else if (top + row > view.y + view.viewport_height)
            view.y = top + row - view.viewport_height;
        view.clamp();
    }
};
} // namespace visual
