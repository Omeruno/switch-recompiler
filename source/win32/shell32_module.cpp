// shell32_module.cpp — minimal Win32 shell functionality backed by guest VFS.
// Avoid fake non-null HICON handles: missing icon resources are reported as 0.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include "hle_util.h"
#include "../runtime/files.h"

namespace win32 {
namespace {
using namespace hle;

constexpr uint32_t SHGFI_ICON = 0x000000100;
constexpr uint32_t SHGFI_DISPLAYNAME = 0x000000200;
constexpr uint32_t SHGFI_TYPENAME = 0x000000400;
constexpr uint32_t SHGFI_ATTRIBUTES = 0x000000800;
constexpr uint32_t SHGFI_SYSICONINDEX = 0x000004000;
constexpr uint32_t SHGFI_USEFILEATTRIBUTES = 0x000000010;
constexpr uint32_t FILE_ATTRIBUTE_DIRECTORY = 0x10;
constexpr uint32_t FILE_ATTRIBUTE_NORMAL = 0x80;
constexpr uint32_t CSIDL_FLAG_MASK = 0x00FF;
constexpr uint32_t CSIDL_DESKTOPDIRECTORY = 0x10;
constexpr uint32_t CSIDL_PERSONAL = 0x05;
constexpr uint32_t CSIDL_MYPICTURES = 0x27;
constexpr uint32_t CSIDL_APPDATA = 0x1a;
constexpr uint32_t CSIDL_LOCAL_APPDATA = 0x1c;
constexpr uint32_t CSIDL_COMMON_DOCUMENTS = 0x2e;
constexpr uint32_t CSIDL_PROFILE = 0x28;
constexpr uint32_t MAX_PATH_CHARS = 260;

#define FN(name) HostCallOutcome name(cpu::CpuState& c, cpu::IGuestMemory& m)
FN(DragAcceptFiles) { (void)c; (void)m; return Ret(0); }
FN(DragFinish) { (void)c; (void)m; return Ret(0); }
FN(DragQueryFileW) {
    if (Arg(c,m,2) && Arg(c,m,3)) m.Write16(Arg(c,m,2),0);
    return Ret(0);
}
FN(DragQueryFileA) {
    if (Arg(c,m,2) && Arg(c,m,3)) m.Write8(Arg(c,m,2),0);
    return Ret(0);
}
FN(DragQueryPoint) {
    if (Arg(c,m,1)) { m.Write32(Arg(c,m,1),0); m.Write32(Arg(c,m,1)+4,0); }
    return Ret(0);
}
FN(SHChangeNotify) { (void)c; (void)m; return Ret(0); }

std::u16string Utf8ToWide(const std::string& s) {
    std::u16string out;
    for (size_t i=0; i<s.size();) {
        uint32_t cp=0;
        unsigned char c=static_cast<unsigned char>(s[i++]);
        if (c<0x80) cp=c;
        else {
            int extra=(c&0xe0)==0xc0 ? 1 : (c&0xf0)==0xe0 ? 2 : (c&0xf8)==0xf0 ? 3 : -1;
            if (extra<0 || i+extra>s.size()) { out.push_back(u'?'); continue; }
            cp=c & (extra==1 ? 0x1f : extra==2 ? 0x0f : 0x07);
            for (int j=0;j<extra;++j) cp=(cp<<6)|(static_cast<unsigned char>(s[i++]) & 0x3f);
        }
        if (cp<0x10000 && !(cp>=0xd800 && cp<=0xdfff)) out.push_back(static_cast<char16_t>(cp));
        else if (cp<=0x10ffff) {
            cp-=0x10000;
            out.push_back(static_cast<char16_t>(0xd800|(cp>>10)));
            out.push_back(static_cast<char16_t>(0xdc00|(cp&0x3ff)));
        } else out.push_back(u'?');
    }
    return out;
}

std::string BaseName(const std::string& name) {
    size_t n=name.find_last_of("\\/");
    return n==std::string::npos ? name : name.substr(n+1);
}
std::string TypeName(const std::string& name, bool dir) {
    if (dir) return "File Folder";
    size_t dot=name.find_last_of('.');
    if (dot==std::string::npos) return "File";
    std::string ext=name.substr(dot+1);
    for (char& ch:ext) if(ch>='A'&&ch<='Z')ch=static_cast<char>(ch+32);
    if(ext=="bmp"||ext=="dib") return "Bitmap Image";
    if(ext=="png") return "PNG Image";
    if(ext=="jpg"||ext=="jpeg") return "JPEG Image";
    if(ext=="gif") return "GIF Image";
    if(ext=="ico") return "Icon";
    return ext+" File";
}

HostCallOutcome FileInfo(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    // DWORD_PTR SHGetFileInfo(PCWSTR path, DWORD attrs, SHFILEINFO*, UINT bytes, UINT flags)
    uint32_t pathptr=Arg(c,m,0), attrs=Arg(c,m,1), p=Arg(c,m,2), cb=Arg(c,m,3), flags=Arg(c,m,4);
    constexpr uint32_t sizeW=692, sizeA=352;
    if (!p || cb<(wide?sizeW:sizeA)) { SetLastError(m,ERROR_INVALID_PARAMETER); return Ret(0); }
    std::string path=wide ? WideToUtf8(ReadWide(m,pathptr)) : ReadAnsi(m,pathptr);
    if (path.empty()) { SetLastError(m,ERROR_INVALID_PARAMETER); return Ret(0); }
    struct stat st{};
    bool synthetic=(flags&SHGFI_USEFILEATTRIBUTES)!=0;
    bool exists=stat(rt::FileSystem().ToHost(path).c_str(),&st)==0;
    if (!synthetic && !exists) { SetLastError(m,ERROR_FILE_NOT_FOUND); return Ret(0); }
    bool dir=synthetic ? (attrs&FILE_ATTRIBUTE_DIRECTORY)!=0 : S_ISDIR(st.st_mode);
    uint32_t actualAttrs=dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    std::string filename=BaseName(path);
    if (filename.empty()) filename=path;
    // SHFILEINFO layout on Win32 (4-byte pointer): HICON 0; int iIcon 4;
    // DWORD dwAttributes 8; WCHAR szDisplayName[260] 12; WCHAR szTypeName[80] 532.
    // ANSI layout: display 12, type 272, total 352.
    m.Write32(p,0);  // no icon allocated: genuine absence, not fake HICON
    m.Write32(p+4,0);
    m.Write32(p+8,(flags&SHGFI_ATTRIBUTES)?actualAttrs:0);
    if (wide) {
        WriteWide(m,p+12,(flags&SHGFI_DISPLAYNAME)?Utf8ToWide(filename):u"",260);
        WriteWide(m,p+532,(flags&SHGFI_TYPENAME)?Utf8ToWide(TypeName(filename,dir)):u"",80);
    } else {
        WriteAnsi(m,p+12,(flags&SHGFI_DISPLAYNAME)?filename:"",260);
        WriteAnsi(m,p+272,(flags&SHGFI_TYPENAME)?TypeName(filename,dir):"",80);
    }
    SetLastError(m,ERROR_SUCCESS);
    // Win32 SHGetFileInfo returns nonzero success; we have no image list.
    // ICON and SYSICONINDEX callers must not be handed fabricated handles.
    if (flags&(SHGFI_ICON|SHGFI_SYSICONINDEX)) return Ret(0);
    return Ret(1);
}
FN(SHGetFileInfoW) { return FileInfo(c,m,true); }
FN(SHGetFileInfoA) { return FileInfo(c,m,false); }

HostCallOutcome SpecialFolder(cpu::CpuState& c, cpu::IGuestMemory& m, bool wide) {
    // BOOL SHGetSpecialFolderPath(HWND owner, PTSTR out[MAX_PATH], int csidl, BOOL create)
    uint32_t dest=Arg(c,m,1), csidl=Arg(c,m,2)&CSIDL_FLAG_MASK;
    bool create=Arg(c,m,3)!=0;
    if (!dest) { SetLastError(m,ERROR_INVALID_PARAMETER); return Ret(0); }
    std::string path;
    switch(csidl) {
        case CSIDL_DESKTOPDIRECTORY: path="C:\\Desktop"; break;
        case CSIDL_PERSONAL: path="C:\\My Documents"; break;
        case CSIDL_MYPICTURES: path="C:\\My Documents\\My Pictures"; break;
        case CSIDL_APPDATA: path="C:\\Application Data"; break;
        case CSIDL_LOCAL_APPDATA: path="C:\\Local Settings\\Application Data"; break;
        case CSIDL_COMMON_DOCUMENTS: path="C:\\Shared Documents"; break;
        case CSIDL_PROFILE: path="C:\\"; break;
        default:
            if (wide) m.Write16(dest,0); else m.Write8(dest,0);
            SetLastError(m,ERROR_INVALID_PARAMETER);
            return Ret(0);
    }
    if(rt::FileSystem().Persistent()) {
        const auto profile=rt::FileSystem().ProfileDirectory();
        if(csidl==CSIDL_PROFILE)path=profile;
        else if(csidl!=CSIDL_COMMON_DOCUMENTS)path=profile+"\\"+path.substr(3);
    }
    if (create) {
        std::string part="C:\\";
        size_t off=3;
        while (off<path.size()) {
            size_t end=path.find('\\',off);
            if(end==std::string::npos)end=path.size();
            part=path.substr(0,end);
            const auto writable=rt::FileSystem().ToHost(part,true);
            if(writable.empty() || mkdir(writable.c_str(),0777)!=0) {
                struct stat st{};
                if(stat(rt::FileSystem().ToHost(part).c_str(),&st)!=0||!S_ISDIR(st.st_mode)) {
                    SetLastError(m,ERROR_ACCESS_DENIED);
                    if (wide) m.Write16(dest,0); else m.Write8(dest,0);
                    return Ret(0);
                }
            }
            off=end+1;
        }
    }
    // Non-creating lookup must not lie about nonexistent guest directories.
    struct stat st{};
    if(stat(rt::FileSystem().ToHost(path).c_str(),&st)!=0||!S_ISDIR(st.st_mode)) {
        if(wide)m.Write16(dest,0);else m.Write8(dest,0);
        SetLastError(m,ERROR_PATH_NOT_FOUND);
        return Ret(0);
    }
    if(wide)WriteWide(m,dest,Utf8ToWide(path),MAX_PATH_CHARS);
    else WriteAnsi(m,dest,path,MAX_PATH_CHARS);
    SetLastError(m,ERROR_SUCCESS);
    return Ret(1);
}
FN(SHGetSpecialFolderPathW) { return SpecialFolder(c,m,true); }
FN(SHGetSpecialFolderPathA) { return SpecialFolder(c,m,false); }

// UINT ExtractIcon(HINSTANCE, LPCTSTR path, UINT iconIndex).
// Without an icon resource renderer, no genuine HICON can be provided.
FN(ExtractIconW) { (void)c; (void)m; return Ret(0); }
FN(ExtractIconA) { (void)c; (void)m; return Ret(0); }

#define E(name, bytes) {"SHELL32.dll", #name, bytes, CallConv::Stdcall, &name}
const HostFunctionEntry kEntries[] = {
    E(DragAcceptFiles,8), E(DragFinish,4), E(DragQueryFileW,16),
    E(DragQueryFileA,16), E(DragQueryPoint,8), E(SHChangeNotify,16),
    E(SHGetFileInfoW,20), E(SHGetFileInfoA,20),
    E(SHGetSpecialFolderPathW,16), E(SHGetSpecialFolderPathA,16),
    E(ExtractIconW,12), E(ExtractIconA,12),
};
const bool kRegistered=RegisterHostFunctions(kEntries,sizeof(kEntries)/sizeof(kEntries[0]));
} // namespace
} // namespace win32
