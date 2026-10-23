#pragma once
#include <map>
#include <memory>

namespace rt {
// HLE caches containing guest addresses/resources belong to the Process which
// owns them. Type keys are immutable; there is no RTTI or global pointer cache.
class ProcessLocalState {
    struct Erased {virtual ~Erased()=default;};
    template<class T> struct Value final:Erased {T data;};
public:
    template<class T> T& Get() {
        static const char key=0;
        auto& entry=values_[&key];
        if(!entry)entry=std::make_unique<Value<T>>();
        return static_cast<Value<T>*>(entry.get())->data;
    }
private:
    std::map<const void*,std::unique_ptr<Erased>> values_;
};
} // namespace rt
