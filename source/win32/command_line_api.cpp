#include "command_line_api.h"
#include "hle_util.h"
#include "../runtime/command_line.h"
#include "../runtime/process.h"
#include "../runtime/heap.h"
#include "../runtime/files.h"
namespace win32 {
HostCallOutcome ParseCommandLineWide(cpu::CpuState& c,cpu::IGuestMemory& m) {
    using namespace hle;
    const auto source=Arg(c,m,0),count=Arg(c,m,1);
    if(!source || !count){SetLastError(m,87);return Ret(0);}
    std::u16string text;
    for(size_t i=0;i<=rt::MaxCommandLine;++i) {
        const auto ch=m.Read16(source+uint32_t(i*2));if(!ch)break;text.push_back(char16_t(ch));
    }
    std::vector<std::u16string> args;
    if(!rt::ParseCommandLine(text,args)){SetLastError(m,87);return Ret(0);}
    if(text.empty()) {
        const auto module=rt::g_process && rt::g_process->MainModule()?rt::FileSystem().ToGuest(rt::g_process->MainModule()->path):"app.exe";
        args.push_back(Utf8PathToWide(module));
    } else if(text.front()==u' ' || text.front()==u'\t') {
        if(args.size()==rt::MaxCommandArgs){SetLastError(m,87);return Ret(0);}
        args.insert(args.begin(),u"");
    }
    uint32_t bytes=uint32_t((args.size()+1)*4);for(const auto& arg:args)bytes+=uint32_t((arg.size()+1)*2);
    const uint32_t block=rt::ProcessHeap().Alloc(bytes,true);
    if(!block){SetLastError(m,8);return Ret(0);}
    uint32_t next=block+uint32_t((args.size()+1)*4);
    for(size_t i=0;i<args.size();++i) {
        m.Write32(block+uint32_t(i*4),next);
        for(auto ch:args[i]){m.Write16(next,ch);next+=2;}m.Write16(next,0);next+=2;
    }
    m.Write32(count,uint32_t(args.size()));return Ret(block);
}
} // namespace win32
