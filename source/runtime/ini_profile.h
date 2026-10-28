#pragma once
#include "files.h"
#include "file_copy_io.h"
namespace rt {
// Bounded real INI provider. No cache, registry redirection or private payloads.
class IniProfile {
public:
    static constexpr size_t MaxBytes=1024*1024,MaxLine=32768,MaxLines=16384;
    uint32_t Load(const Vfs&,const std::string&,FileCopyIo* =nullptr);
    std::u16string Read(const std::u16string&,const std::u16string&,const std::u16string&) const;
    std::vector<std::u16string> List(const std::u16string* section,bool entries=false) const;
    uint32_t Write(const Vfs&,const std::string&,const std::u16string& section,
                   const std::u16string* key,const std::u16string* value,FileCopyIo* =nullptr);
    static uint32_t Integer(const std::u16string&);
    static bool ValidUtf16(const std::u16string&);
private:
    bool unicode_=false;
    std::vector<std::u16string> lines_;
};
uint32_t ProfileIoError();
} // namespace rt
