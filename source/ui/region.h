#pragma once
// region.h
//
// Прямоугольники и регионы для оконной подсистемы и GDI. Регион — набор
// непересекающихся прямоугольников: этого хватает и для отсечения окон
// (видимая часть окна = его прямоугольник минус окна поверх него), и для
// областей обновления (InvalidateRect), и для регионов GDI из прямоугольников.
//
// Прямоугольники как в Windows: правая и нижняя граница не входят.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ui {

struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;

    Rect() = default;
    Rect(int l, int t, int r, int b) : left(l), top(t), right(r), bottom(b) {}

    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
    bool Empty() const { return right <= left || bottom <= top; }
    bool Contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
    Rect Offset(int dx, int dy) const { return Rect(left + dx, top + dy, right + dx, bottom + dy); }
    Rect Intersect(const Rect& o) const {
        Rect r(std::max(left, o.left), std::max(top, o.top), std::min(right, o.right), std::min(bottom, o.bottom));
        return r.Empty() ? Rect() : r;
    }
    bool Intersects(const Rect& o) const { return !Intersect(o).Empty(); }
    // Наименьший прямоугольник, содержащий оба (пустые не учитываются)
    Rect Union(const Rect& o) const {
        if (Empty()) return o;
        if (o.Empty()) return *this;
        return Rect(std::min(left, o.left), std::min(top, o.top), std::max(right, o.right), std::max(bottom, o.bottom));
    }
    bool operator==(const Rect& o) const {
        return left == o.left && top == o.top && right == o.right && bottom == o.bottom;
    }
    bool operator!=(const Rect& o) const { return !(*this == o); }
};

class Region {
public:
    Region() = default;
    explicit Region(const Rect& r) { if (!r.Empty()) rects_.push_back(r); }

    const std::vector<Rect>& Rects() const { return rects_; }
    bool Empty() const { return rects_.empty(); }
    void Clear() { rects_.clear(); }

    Rect Bounds() const {
        Rect b;
        for (const Rect& r : rects_) b = b.Union(r);
        return b;
    }

    bool Contains(int x, int y) const {
        for (const Rect& r : rects_) if (r.Contains(x, y)) return true;
        return false;
    }

    void Offset(int dx, int dy) {
        for (Rect& r : rects_) r = r.Offset(dx, dy);
    }

    // this = this ∩ rect
    void IntersectRect(const Rect& clip) {
        std::vector<Rect> out;
        for (const Rect& r : rects_) {
            Rect i = r.Intersect(clip);
            if (!i.Empty()) out.push_back(i);
        }
        rects_.swap(out);
    }

    // this = this ∩ other
    void Intersect(const Region& other) {
        std::vector<Rect> out;
        for (const Rect& a : rects_)
            for (const Rect& b : other.rects_) {
                Rect i = a.Intersect(b);
                if (!i.Empty()) out.push_back(i);
            }
        rects_.swap(out);
    }

    // this = this \ rect: каждый прямоугольник режется не более чем на 4
    void SubtractRect(const Rect& cut) {
        if (cut.Empty()) return;
        std::vector<Rect> out;
        for (const Rect& r : rects_) {
            if (!r.Intersects(cut)) { out.push_back(r); continue; }
            if (cut.top > r.top) out.push_back(Rect(r.left, r.top, r.right, cut.top));
            if (cut.bottom < r.bottom) out.push_back(Rect(r.left, cut.bottom, r.right, r.bottom));
            int t = std::max(r.top, cut.top), b = std::min(r.bottom, cut.bottom);
            if (cut.left > r.left) out.push_back(Rect(r.left, t, cut.left, b));
            if (cut.right < r.right) out.push_back(Rect(cut.right, t, r.right, b));
        }
        rects_.swap(out);
    }

    void Subtract(const Region& other) {
        for (const Rect& r : other.rects_) SubtractRect(r);
    }

    // this = this ∪ rect (добавляется только непокрытая часть)
    void UnionRect(const Rect& add) {
        if (add.Empty()) return;
        Region piece(add);
        for (const Rect& r : rects_) piece.SubtractRect(r);
        rects_.insert(rects_.end(), piece.rects_.begin(), piece.rects_.end());
    }

    void Union(const Region& other) {
        for (const Rect& r : other.rects_) UnionRect(r);
    }

    // this = this xor other
    void Xor(const Region& other) {
        Region a = *this;
        a.Subtract(other);
        Region b = other;
        b.Subtract(*this);
        *this = a;
        rects_.insert(rects_.end(), b.rects_.begin(), b.rects_.end());
    }

    bool operator==(const Region& o) const {
        Region a = *this;
        a.Xor(o);
        return a.Empty();
    }

private:
    std::vector<Rect> rects_;
};

} // namespace ui
