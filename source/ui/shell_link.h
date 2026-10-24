#pragma once
#include "../runtime/files.h"
#include "../runtime/file_copy_io.h"
namespace ui {
// Explicit Unicode MS-SHLLINK subset, not COM/IShellLink emulation.
struct ShellLink {
    std::string target,arguments,workingDirectory,name,icon;
    static constexpr size_t MaxBytes=64*1024;
    bool Load(const rt::Vfs&,const std::string&,rt::FileCopyIo* io=nullptr);
    bool Save(const rt::Vfs&,const std::string&,rt::FileCopyIo* io=nullptr) const; // new file only
    bool Decode(const std::vector<uint8_t>&);
    std::vector<uint8_t> Encode() const;
    static bool IsLink(const std::string&);
    static bool Absolute(const std::string&);
};
} // namespace ui
