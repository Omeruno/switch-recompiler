// crt_cpp.cpp — C++ на mingw: iostream, string, vector, map, исключения,
// виртуальные функции, глобальные конструкторы и деструкторы.
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static int failures;
#define CHECK(cond) do { if (cond) std::cout << "ok   " #cond "\n"; else { std::cout << "FAIL " #cond "\n"; failures++; } } while (0)

struct Shape { virtual ~Shape() {} virtual double area() const = 0; };
struct Rect : Shape { double w, h; Rect(double a, double b) : w(a), h(b) {} double area() const override { return w * h; } };
struct Circle : Shape { double r; explicit Circle(double x) : r(x) {} double area() const override { return 3.14159265358979 * r * r; } };

static int g_ctor = 0;
struct Global { Global() { g_ctor = 42; } };
static Global g_global;

int thrower(int x) { if (x > 2) throw std::runtime_error("too big: " + std::to_string(x)); return x; }

int main() {
    CHECK(g_ctor == 42);
    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.emplace_back(new Rect(2, 3));
    shapes.emplace_back(new Circle(1));
    double total = 0;
    for (auto& s : shapes) total += s->area();
    CHECK(total > 9.14 && total < 9.15);

    std::map<std::string, int> counts;
    std::istringstream in("the quick brown fox jumps over the lazy dog the end");
    std::string w;
    while (in >> w) counts[w]++;
    CHECK(counts["the"] == 3 && counts.size() == 9);

    std::vector<int> v = {5, 3, 9, 1, 7};
    std::sort(v.begin(), v.end());
    CHECK(v.front() == 1 && v.back() == 9);

    std::string caught;
    try { thrower(1); thrower(5); } catch (const std::exception& e) { caught = e.what(); }
    CHECK(caught == "too big: 5");

    int depth = 0;
    try {
        try { throw 7; } catch (int x) { depth = x; throw std::string("rethrown"); }
    } catch (const std::string& s) { depth += static_cast<int>(s.size()); }
    CHECK(depth == 15);

    std::ostringstream os;
    os << 3.5 << " " << 42 << " " << std::hex << 255;
    CHECK(os.str() == "3.5 42 ff");

    std::cout << "crt_cpp: " << (failures ? "FAILED" : "PASSED") << std::endl;
    return failures;
}
