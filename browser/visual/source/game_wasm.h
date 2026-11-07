#pragma once
#include "game_stats.h"
extern "C" {
#include "quickjs.h"
}
#include <memory>
#include <string>
namespace visual {
class GameWasm {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    explicit GameWasm(JSContext *);
    ~GameWasm();
    JSValue operation(const std::string &, int, JSValueConst *);
    GameStats stats() const;
};
} // namespace visual
