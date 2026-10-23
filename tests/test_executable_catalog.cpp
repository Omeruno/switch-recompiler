#include "source/ui/executable_catalog.h"
#include <cassert>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstdio>
namespace fs=std::filesystem;
struct Tree {
    std::string path;
    Tree(){char pattern[]="/tmp/sr-catalog-XXXXXX";const auto* p=mkdtemp(pattern);assert(p);path=p;}
    ~Tree(){fs::remove_all(path);}
    void File(const std::string& name){std::ofstream f(path+"/"+name);f<<"fixture";assert(f.good());}
};
int main() {
    Tree tree;fs::create_directories(tree.path+"/sub/deeper");
    tree.File("z.ExE");tree.File("a.exe");tree.File("note.txt");tree.File(".exe");
    tree.File("sub/b.EXE");tree.File("sub/deeper/hidden.exe");fs::create_directory(tree.path+"/folder.exe");
    ui::ExecutableCatalog root;root.ScanRoot(tree.path,0);
    assert(root.Files().size()==2 && root.Files()[0]==tree.path+"/a.exe");
    ui::ExecutableCatalog nested;nested.ScanRoot(tree.path,1);
    assert(nested.Files().size()==3 && !nested.Truncated());
    ui::ExecutableCatalog priority;priority.ScanRoot(tree.path+"/sub",0);priority.ScanRoot(tree.path,0);
    assert(priority.Files().size()==3 && priority.Files()[0]==tree.path+"/sub/b.EXE");
    ui::ExecutableCatalog missing;missing.ScanRoot(tree.path+"/absent",1);assert(missing.Errors()==1 && missing.Files().empty());
    missing.ScanRoot(std::string(ui::ExecutableCatalog::MaxPathBytes+1,'x'),0);assert(missing.Truncated());
    Tree many;for(size_t i=0;i<ui::ExecutableCatalog::MaxFiles+20;++i)many.File("app"+std::to_string(i)+".exe");
    ui::ExecutableCatalog capped;capped.ScanRoot(many.path,2);
    assert(capped.Files().size()==ui::ExecutableCatalog::MaxFiles && capped.Truncated());
    capped.ScanRoot(tree.path,1);assert(capped.Files().size()==ui::ExecutableCatalog::MaxFiles);
    Tree entries;for(size_t i=0;i<ui::ExecutableCatalog::MaxEntries+20;++i)entries.File("note"+std::to_string(i));
    ui::ExecutableCatalog work;work.ScanRoot(entries.path,2);
    assert(work.Files().empty() && work.Truncated() && work.EntriesSeen()==ui::ExecutableCatalog::MaxEntries);
    ui::LauncherSelection empty(0);empty.Move(INT_MIN);assert(!empty.EndVisible());
    ui::LauncherSelection select(40);select.Move(-1);assert(select.Selected()==39 && select.FirstVisible()==36 && select.EndVisible()==40);
    select.Move(1);assert(!select.Selected());select.Move(18);assert(select.FirstVisible()==18 && select.EndVisible()==36);
    select.Move(-18);assert(!select.Selected());select.Move(INT_MIN);assert(select.Selected()<40);
    puts("PASSED: bounded EXE inventory, file types/extensions, depth/root order, missing/long paths, entry/file budgets and paginated/wrapped selection");
}
