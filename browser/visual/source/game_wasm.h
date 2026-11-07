#pragma once
extern "C" {
#include "quickjs.h"
}
#include <memory>
#include <string>
namespace visual {
struct GameStats {
    unsigned modules = 0, instances = 0, calls = 0, traps = 0;
};
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
