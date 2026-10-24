#include "source/ui/shell_browser.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"file operations %d: %s errno=%d\n",__LINE__,#x,errno);std::exit(1);}}while(0)
static std::string Bytes(const fs::path& p) {std::ifstream in(p);return {std::istreambuf_iterator<char>(in),{}};}
int main() {
    char pattern[]="/tmp/sr-file-ops-XXXXXX";const auto tmp=mkdtemp(pattern);CHECK(tmp);
    const fs::path base=tmp,sd=base/"sd",system=sd/"drive_c",rom=base/"rom";
    struct Cleanup {fs::path path;~Cleanup(){fs::remove_all(path);}} cleanup{base};
    fs::create_directories(sd);fs::create_directory(rom);std::ofstream(rom/"locked.txt")<<"locked";
    rt::Vfs v;std::string error;CHECK(v.ConfigurePersistent(system.string(),sd.string(),rom.string(),error));
    CHECK(v.CreateDirectory("C:\\TEMP\\папка"));
    CHECK(!v.CreateDirectory("c:\\temp\\папка") && errno==EEXIST);
    CHECK(!v.CreateDirectory("C:\\absent\\child") && errno==ENOENT);
    const auto dir=system/"TEMP/папка";
    std::ofstream(dir/"MiXeD.txt")<<"actual bytes";
    rt::FileProperties props;CHECK(v.QueryProperties("c:\\temp\\папка\\mixed.txt",props));
    struct stat st{};CHECK(stat((dir/"MiXeD.txt").c_str(),&st)==0);
    CHECK(props.kind==rt::FileProperties::Kind::File && props.size==12 && !props.mountReadOnly);
    CHECK(props.modifiedSeconds==st.st_mtime && props.accessedSeconds==st.st_atime);
    CHECK(v.QueryProperties("C:\\TEMP\\папка",props) && props.size==0 && props.kind==rt::FileProperties::Kind::Directory);
    CHECK(!v.DeleteFile("C:\\TEMP\\папка") && fs::is_directory(dir));
    CHECK(!v.RemoveDirectory("C:\\TEMP\\папка") && errno==ENOTEMPTY);
    std::ofstream(dir/"other.txt")<<"preserve";
    CHECK(!v.Move("C:\\TEMP\\папка\\mixed.txt","C:\\TEMP\\папка\\OTHER.txt") && errno==EEXIST);
    CHECK(Bytes(dir/"other.txt")=="preserve" && Bytes(dir/"MiXeD.txt")=="actual bytes");
    CHECK(!v.Move("C:\\TEMP\\папка\\mixed.txt","C:\\TEMP\\папка\\MIXED.TXT") && errno==EEXIST);
    CHECK(fs::exists(dir/"MiXeD.txt"));
    CHECK(!v.Move("C:\\TEMP\\absent","C:\\TEMP\\never-created") && errno==ENOENT);
    CHECK(!fs::exists(system/"TEMP/never-created"));
    CHECK(v.Move("C:\\TEMP\\папка\\mixed.txt","C:\\TEMP\\папка\\new.txt"));
    CHECK(!fs::exists(dir/"MiXeD.txt") && Bytes(dir/"new.txt")=="actual bytes");
    CHECK(v.Move("C:\\TEMP\\папка\\new.txt","C:\\TEMP\\папка\\other.txt",true));
    CHECK(Bytes(dir/"other.txt")=="actual bytes");
    CHECK(!v.Move("C:\\TEMP\\папка","C:\\TEMP",true));
    CHECK(!v.RemoveDirectory("C:\\") && errno==EBUSY);
    CHECK(!v.Move("S:\\drive_c","S:\\moved") && errno==EBUSY);
    CHECK(!v.DeleteFile("R:\\locked.txt") && errno==EROFS);
    CHECK(!v.CreateDirectory("R:\\folder") && errno==EROFS);
    CHECK(!v.Move("R:\\locked.txt","C:\\TEMP\\stolen") && errno==EROFS);
    CHECK(!v.RemoveDirectory("R:\\") && errno==EROFS);
    CHECK(!v.Move("C:\\TEMP\\папка\\other.txt","R:\\copied") && errno==EROFS);
    CHECK(v.QueryProperties("R:\\locked.txt",props) && props.mountReadOnly && props.size==6);
    props.size=123;CHECK(!v.QueryProperties("C:\\missing",props) && props.size==0);
    CHECK(!v.DeleteFile("Z:\\missing") && errno==ENODEV);
    CHECK(!v.DeleteFile("C:\\bad:stream") && errno==EINVAL);
    fs::create_symlink(rom/"locked.txt",system/"linked");
    CHECK(!v.DeleteFile("C:\\linked") && errno==EACCES && Bytes(rom/"locked.txt")=="locked");
    size_t budget=0;CHECK(!v.DeleteFile("c:\\temp\\папка\\OTHER.TXT",&budget) && errno==EOVERFLOW);
    CHECK(v.DeleteFile("C:\\TEMP\\папка\\other.txt"));
    CHECK(v.RemoveDirectory("C:\\TEMP\\папка"));
    CHECK(!v.DeleteFile("C:\\TEMP\\папка\\other.txt") && errno==ENOENT);
    // Browser and runtime operate on the same bytes, without a guest Process.
    ui::ShellBrowser browser;CHECK(browser.Configure(system.string(),sd.string(),rom.string(),error));
    using Result=ui::ShellBrowser::MutationResult;
    CHECK(browser.CreateFolder("no-volume")==Result::Failed);
    CHECK(browser.Navigate("C:\\TEMP"));
    CHECK(browser.CreateFolder("..\\escape")==Result::Failed && browser.LastError()==ui::ShellBrowser::Error::InvalidPath);
    CHECK(browser.CreateFolder("folder.")==Result::Failed);
    CHECK(browser.CreateFolder("данные")==Result::Applied && fs::is_directory(system/"TEMP/данные"));
    CHECK(browser.Entries()[browser.Selected()].name=="данные");
    CHECK(browser.SelectedProperties(props) && props.kind==rt::FileProperties::Kind::Directory);
    CHECK(browser.RenameSelected("renamed")==Result::Applied && !fs::exists(system/"TEMP/данные"));
    CHECK(browser.Entries()[browser.Selected()].name=="renamed");
    std::ofstream(system/"TEMP/renamed/keep.txt")<<"keep";
    CHECK(browser.DeleteSelected()==Result::Failed && browser.OperationErrno()==ENOTEMPTY);
    CHECK(browser.LastError()==ui::ShellBrowser::Error::NotEmpty && Bytes(system/"TEMP/renamed/keep.txt")=="keep");
    CHECK(v.DeleteFile("C:\\TEMP\\renamed\\keep.txt"));
    CHECK(browser.DeleteSelected()==Result::Applied && !fs::exists(system/"TEMP/renamed"));
    CHECK(browser.Navigate("R:\\"));
    CHECK(browser.SelectedProperties(props) && props.mountReadOnly);
    CHECK(browser.RenameSelected("no.txt")==Result::Failed && browser.LastError()==ui::ShellBrowser::Error::ReadOnly);
    CHECK(browser.DeleteSelected()==Result::Failed && Bytes(rom/"locked.txt")=="locked");
    // Revalidation rejects a selected item removed since the last listing.
    CHECK(browser.Navigate("C:\\TEMP"));CHECK(browser.CreateFolder("gone")==Result::Applied);
    fs::remove(system/"TEMP/gone");CHECK(!browser.SelectedProperties(props));
    CHECK(browser.DeleteSelected()==Result::Failed && browser.OperationErrno()==ENOENT);
    std::puts("PASSED: shared real mutations/properties, root/readonly/link/case/type/overwrite protection and browser revalidation");
}
