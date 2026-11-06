#pragma once
#include "container.h"
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
    void focus(const litehtml::element::ptr &);
    bool dispatch(const litehtml::element::ptr &, const std::string &type, bool cancelable = true,
                  const litehtml::element::ptr &submitter = {});
    bool eval(const std::string &, unsigned budget_ms = 50);
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
