#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace ui {
// Startup launcher inventory, shared by the Switch adapter and native tests.
// Cumulative budgets cover all roots; no unbounded directory/path list is kept.
class ExecutableCatalog {
public:
    static constexpr size_t MaxFiles=256,MaxEntries=4096,MaxPathBytes=1024;
    static constexpr int MaxDepth=2;
    void ScanRoot(const std::string& root,int depth);
    const std::vector<std::string>& Files() const {return files_;}
    size_t EntriesSeen() const {return entries_;}
    size_t Errors() const {return errors_;}
    bool Truncated() const {return truncated_;}
private:
    void ScanDirectory(const std::string& dir,int depth);
    std::vector<std::string> files_;
    size_t entries_=0,errors_=0;
    bool truncated_=false;
};
class LauncherSelection {
public:
    static constexpr size_t Rows=18;
    explicit LauncherSelection(size_t count):count_(count) {}
    void Move(int delta) {
        if(!count_)return;
        // Inventory bounds make signed wrap arithmetic safe.
        const int count=int(count_);selected_=size_t((int(selected_)+delta%count+count)%count);
    }
    size_t Selected() const {return selected_;}
    size_t FirstVisible() const {return selected_/Rows*Rows;}
    size_t EndVisible() const {const auto end=FirstVisible()+Rows;return end<count_?end:count_;}
private:
    size_t count_,selected_=0;
};
} // namespace ui
