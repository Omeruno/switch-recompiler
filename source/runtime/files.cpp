// files.cpp — файловая система гостя (см. files.h)

#include "files.h"

#include <cctype>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

namespace rt {

namespace {

bool HostExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

// Ищет в каталоге dir элемент с именем name без учёта регистра.
std::string FindCaseInsensitive(const std::string& dir, const std::string& name) {
    std::string direct = dir + "/" + name;
    if (HostExists(direct)) return name;
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string found;
    while (dirent* e = readdir(d)) {
        if (strcasecmp(e->d_name, name.c_str()) == 0) { found = e->d_name; break; }
    }
    closedir(d);
    return found;
}

std::vector<std::string> SplitPath(const std::string& p) {
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : p) {
        if (ch == '\\' || ch == '/') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

} // namespace

Vfs& FileSystem() {
    static Vfs vfs;
    return vfs;
}

FileTable& Files() {
    static FileTable table;
    return table;
}

void Vfs::SetRoot(const std::string& hostDir) {
    root_ = hostDir.empty() ? "." : hostDir;
    while (root_.size() > 1 && (root_.back() == '/' || root_.back() == '\\')) root_.pop_back();
}

std::string Vfs::FullGuestPath(const std::string& guestPath) const {
    std::string p = guestPath;
    std::string drive = "C:";
    std::vector<std::string> base;
    if (p.size() >= 2 && p[1] == ':') {
        drive = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(p[0])))) + ":";
        p = p.substr(2);
        if (p.empty() || (p[0] != '\\' && p[0] != '/')) base = SplitPath(cwd_.substr(2)); // "C:file" — относительно cwd
    } else if (!p.empty() && (p[0] == '\\' || p[0] == '/')) {
        // от корня текущего диска
    } else {
        base = SplitPath(cwd_.substr(2));
    }
    std::vector<std::string> parts = base;
    for (const std::string& part : SplitPath(p)) {
        if (part == ".") continue;
        if (part == "..") { if (!parts.empty()) parts.pop_back(); continue; }
        parts.push_back(part);
    }
    std::string out = drive;
    for (const std::string& part : parts) out += "\\" + part;
    if (parts.empty()) out += "\\";
    return out;
}

bool Vfs::SetCurrentDirectory(const std::string& guestPath) {
    std::string full = FullGuestPath(guestPath);
    struct stat st;
    std::string host = ToHost(full);
    if (stat(host.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    while (full.size() > 2 && full.back() == '\\') full.pop_back();
    cwd_ = full;
    return true;
}

std::string Vfs::ToHost(const std::string& guestPath) const {
    std::string full = FullGuestPath(guestPath);
    std::vector<std::string> parts = SplitPath(full.substr(2));
    std::string cur = root_;
    for (size_t i = 0; i < parts.size(); ++i) {
        std::string name = FindCaseInsensitive(cur, parts[i]);
        if (name.empty()) {
            // дальше путь не существует: оставляем как есть
            for (size_t k = i; k < parts.size(); ++k) cur += "/" + parts[k];
            return cur;
        }
        cur += "/" + name;
    }
    return cur;
}

std::string Vfs::ToGuest(const std::string& hostPath) const {
    std::string p = hostPath;
    if (p.compare(0, root_.size(), root_) == 0) p = p.substr(root_.size());
    std::string out = "C:";
    for (const std::string& part : SplitPath(p)) out += "\\" + part;
    if (out.size() == 2) out += "\\";
    return out;
}

OpenFile::~OpenFile() {
    if (f) std::fclose(f);
    if (deleteOnClose && !hostPath.empty()) std::remove(hostPath.c_str());
}

FileTable::FileTable() {
    auto in = std::make_shared<OpenFile>();
    in->kind = OpenFile::ConsoleIn;
    in->canRead = true;
    auto out = std::make_shared<OpenFile>();
    out->kind = OpenFile::ConsoleOut;
    out->canWrite = true;
    auto err = std::make_shared<OpenFile>();
    err->kind = OpenFile::ConsoleErr;
    err->canWrite = true;
    handles_[kStdIn] = in;
    handles_[kStdOut] = out;
    handles_[kStdErr] = err;
}

uint32_t FileTable::Add(std::shared_ptr<OpenFile> f) {
    uint32_t h = next_;
    next_ += 4;
    handles_[h] = std::move(f);
    return h;
}

std::shared_ptr<OpenFile> FileTable::Get(uint32_t handle) const {
    auto it = handles_.find(handle);
    return it == handles_.end() ? nullptr : it->second;
}

bool FileTable::Close(uint32_t handle) {
    if (handle == kStdIn || handle == kStdOut || handle == kStdErr) return true;
    return handles_.erase(handle) != 0;
}

uint32_t FileTable::Duplicate(uint32_t handle) {
    auto f = Get(handle);
    if (!f) return 0;
    return Add(f);
}

} // namespace rt
