#pragma once
#include "../runtime/files.h"
#include "../runtime/file_copy_io.h"
namespace ui {
struct ShellAssociation {std::string extension,application;};
struct ShellPreferences {uint32_t sort=0;std::vector<ShellAssociation> associations;};
class ShellPreferenceStore {
public:
    static constexpr size_t MaxBytes=160*1024,MaxAssociations=32;
    enum class Status {Missing,Loaded,Recovered,Corrupt,Unsupported,IoError,Saved,SaveFailed};
    Status Load(const rt::Vfs&,ShellPreferences&,rt::FileCopyIo* io=nullptr);
    bool Save(const rt::Vfs&,const ShellPreferences&,rt::FileCopyIo* io=nullptr);
    static bool Valid(const ShellPreferences&);
    static std::string Directory(const rt::Vfs&);
private:
    uint64_t generation_=0;
    int slot_=-1;
};
} // namespace ui
