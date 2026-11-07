#pragma once
#include "container.h"
#include "game_stats.h"
#include "issues.h"
#include <cstdint>
#include <memory>
namespace visual {
class Scripts {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    Scripts(const litehtml::document::ptr &, Container &, Resources &,
            std::vector<std::string> policies = {});
    ~Scripts();
    Scripts(const Scripts &) = delete;
    Scripts &operator=(const Scripts &) = delete;
    static uint64_t clock_ms();
    bool poll(uint64_t now);
    void render_frame(uint64_t now);
    void focus(const litehtml::element::ptr &);
    bool dispatch(const litehtml::element::ptr &, const std::string &type, bool cancelable = true,
                  const litehtml::element::ptr &submitter = {});
    bool eval(const std::string &, unsigned budget_ms = 50);
    bool key(const std::string &, const std::string &, const std::string &, bool repeat = false,
             unsigned modifiers = 0);
    bool pointer(const litehtml::element::ptr &, const std::string &, double, double, unsigned,
                 unsigned, const std::string &);
    void reset_input();
    GameStats game_stats() const;
    bool changed() const;
    void flush();
    std::string navigation();
    Submission submission();
    bool reload_requested();
    std::string error() const;
    const Issues &issues() const;
    bool ready() const;
};
} // namespace visual
