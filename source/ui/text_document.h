#pragma once
#include "../runtime/files.h"
#include "../runtime/file_copy_io.h"
namespace ui {
// Native document data, independent of Process/USER and platform keyboard.
class TextDocument {
public:
    static constexpr size_t MaxBytes=64*1024,MaxEditUnits=500;
    enum class Encoding {Utf8,Utf8Bom,Utf16LE,Utf16BE};
    struct Line {std::u16string text,ending;};
    void New();
    bool Open(const rt::Vfs&,const std::string&,rt::FileCopyIo* io=nullptr);
    bool Edit(size_t,const std::string& utf8);
    bool InsertAfter(size_t);
    bool Erase(size_t);
    bool Save(const rt::Vfs&,const std::string& saveAs="",rt::FileCopyIo* io=nullptr);
    const std::vector<Line>& Lines() const {return lines_;}
    const std::string& Path() const {return path_;}
    const std::string& BackupPath() const {return backup_;}
    const std::string& RecoveryPath() const {return recovery_;}
    bool Dirty() const;
    int Error() const {return error_;}
    Encoding Format() const {return encoding_;}
    static bool DecodeUtf8(const std::string&,std::u16string&);
    static std::string Utf8(const std::u16string&);
private:
    bool Fail(int e);
    bool Read(rt::FileCopyIo&,const std::string&,std::string&);
    std::string Bytes() const;
    bool Fits(const std::vector<Line>&) const;
    std::vector<Line> lines_{{}};
    std::u16string ending_=u"\r\n";
    std::string path_,original_,backup_,recovery_;
    Encoding encoding_=Encoding::Utf8;
    bool fresh_=true;
    int error_=0;
};
} // namespace ui
