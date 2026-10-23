#include "crt_globals.h"
#include "hle_util.h"
#include "../runtime/command_line.h"
#include <cstring>

namespace win32 {
static_assert(CRT_GLOBALS_SIZE>=6*rt::MaxCommandLine+8*(rt::MaxCommandArgs+1)+0x400,"bounded CRT storage");
uint32_t g_crtGlobalsBase=0;
bool InitCrtGlobals(cpu::IGuestMemory& mem,uint32_t base,const char* commandLine) {
    const std::string cmd=commandLine;std::vector<std::string> args;
    if(!rt::ParseCommandLine(cmd,args))return false;
    if(args.empty())args.push_back("app.exe");
    uint32_t next=base+0x100;
    auto ansi=[&](const std::string& text) {
        const uint32_t addr=next;next+=uint32_t(text.size()+1);
        for(size_t i=0;i<text.size();++i)mem.Write8(addr+uint32_t(i),uint8_t(text[i]));
        mem.Write8(addr+uint32_t(text.size()),0);return addr;
    };
    auto wide=[&](const std::string& text) {
        const auto w=hle::Utf8PathToWide(text);next=(next+1)&~1u;
        const uint32_t addr=next;next+=uint32_t((w.size()+1)*2);
        for(size_t i=0;i<w.size();++i)mem.Write16(addr+uint32_t(i*2),w[i]);
        mem.Write16(addr+uint32_t(w.size()*2),0);return addr;
    };
    const auto command=ansi(cmd),wcommand=wide(cmd);next=(next+3)&~3u;
    const auto argv=next;next+=uint32_t((args.size()+1)*4);
    const auto wargv=next;next+=uint32_t((args.size()+1)*4);
    for(size_t i=0;i<args.size();++i){mem.Write32(argv+uint32_t(i*4),ansi(args[i]));mem.Write32(wargv+uint32_t(i*4),wide(args[i]));}
    mem.Write32(argv+uint32_t(args.size()*4),0);mem.Write32(wargv+uint32_t(args.size()*4),0);
    // MaxCommandLine/MaxCommandArgs bound all storage to less than 128 KiB.
    if(next>base+CRT_GLOBALS_SIZE)return false;
    g_crtGlobalsBase=base;
    mem.Write32(base+CRT_OFFSET_ADJUST_FDIV,0);mem.Write32(base+CRT_OFFSET_FMODE,0x8000);mem.Write32(base+CRT_OFFSET_COMMODE,0);
    mem.Write32(base+CRT_OFFSET_CMDLINE_PTR,command);mem.Write32(base+CRT_OFFSET_WCMDLINE_PTR,wcommand);
    mem.Write32(base+CRT_OFFSET_ARGC,uint32_t(args.size()));
    mem.Write32(base+CRT_OFFSET_ARGV_PTR,argv);mem.Write32(base+CRT_OFFSET_WARGV_PTR,wargv);
    mem.Write32(base+CRT_OFFSET_ARGV,mem.Read32(argv));mem.Write32(base+CRT_OFFSET_ARGV+4,0);
    mem.Write32(base+CRT_OFFSET_WARGV,mem.Read32(wargv));mem.Write32(base+CRT_OFFSET_WARGV+4,0);
    mem.Write32(base+CRT_OFFSET_ENV,0);mem.Write32(base+CRT_OFFSET_WENV,0);return true;
}
} // namespace win32
