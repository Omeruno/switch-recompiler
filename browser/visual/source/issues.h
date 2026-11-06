#pragma once
#include <algorithm>
#include <string>
#include <vector>
namespace visual {
struct Issue {
    std::string category, code, origin;
    unsigned count = 1;
};
class Issues {
    std::vector<Issue> records;
    bool full = false;

  public:
    void add(std::string category, std::string code, std::string origin) {
        for (auto &r : records)
            if (r.category == category && r.code == code && r.origin == origin) {
                r.count = std::min(65535u, r.count + 1);
                return;
            }
        if (records.size() == 24) {
            full = true;
            return;
        }
        records.push_back({std::move(category), std::move(code), std::move(origin), 1});
    }
    const std::vector<Issue> &entries() const { return records; }
    bool truncated() const { return full; }
};
} // namespace visual
