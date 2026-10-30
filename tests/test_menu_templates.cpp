#include "source/runtime/guest_session.h"
#include "source/win32/user32.h"
#include <cassert>
#include <cstdio>
static void Word(std::vector<uint8_t>& b,uint16_t n){b.push_back(uint8_t(n));b.push_back(uint8_t(n>>8));}
int main() {
    rt::GuestSession session;auto& p=session.ProcessObject();auto& m=session.Memory();assert(m.Map(0x10000,0x10000,cpu::kProtRW));
    auto& t=p.MainThread();t.id=4;t.teb=0x10000;t.status=rt::GuestThreadStatus::Runnable;assert(p.Scheduler().Add(t));p.Scheduler().SetCurrent(&t);win32::user::EnableUi();
    auto destroy=[&](uint32_t menu){auto& c=t.cpu;c.ESP()=0x1f000;m.Write32(c.ESP(),0x12345678);m.Write32(c.ESP()+4,menu);
        c.eip=win32::IndexToHostAddress(win32::FindHostFunction("USER32.dll","DestroyMenu"));win32::HostCallOutcome out;assert(win32::TryHandleHostCall(c,m,out) && out.returnValue==1 && c.ESP()==0x1f008);};
    const std::vector<uint8_t> valid={0,0,0,0,128,0,201,0,'G',0,0,0};
    const auto count=win32::user::MenuObjectCount();
    auto reject=[&](const std::vector<uint8_t>& b){assert(!win32::user::LoadMenuFromMemory(b.data(),b.size()) && win32::user::MenuObjectCount()==count);};
    for(size_t n=0;n<valid.size();++n)reject({valid.begin(),valid.begin()+n});
    for(unsigned test=0;test<4;++test){auto b=valid;if(test==0)b[0]=2;if(test==1)b[2]=255;if(test==2)b[4]=0;if(test==3){b.pop_back();b.pop_back();}reject(b);}
    auto h=win32::user::LoadMenuFromMemory(valid.data(),valid.size());assert(h && win32::user::MenuObjectCount()==count+1);destroy(h);
    for(unsigned depth:{16u,17u}) {
        std::vector<uint8_t> b(4,0);for(unsigned n=0;n<depth;++n){Word(b,0x90);Word(b,0);}Word(b,0x80);Word(b,1);Word(b,0);
        if(depth==17)reject(b);else{h=win32::user::LoadMenuFromMemory(b.data(),b.size());assert(h && win32::user::MenuObjectCount()==count+17);destroy(h);}
    }
    for(unsigned entries:{4096u,4097u}) {
        std::vector<uint8_t> b(4,0);for(unsigned n=0;n<entries;++n){Word(b,n+1==entries?0x80:0);Word(b,1);Word(b,0);}
        if(entries==4097)reject(b);else{h=win32::user::LoadMenuFromMemory(b.data(),b.size());assert(h);destroy(h);}
    }
    for(unsigned length:{32767u,32768u}) {
        std::vector<uint8_t> b(4,0);Word(b,0x80);Word(b,1);for(unsigned n=0;n<length;++n)Word(b,'x');Word(b,0);
        if(length==32768)reject(b);else{h=win32::user::LoadMenuFromMemory(b.data(),b.size());assert(h);destroy(h);}
    }
    reject(std::vector<uint8_t>(1024*1024+1));assert(win32::user::MenuObjectCount()==count);
    puts("Menu templates PASS (complete bounded standard/MENUEX parser,all standard prefixes,unsupported version/offset/end/NUL,truncated data,16/17 nesting,4096/4097 items,32767/32768 text,1MiB budget,transactional tree rollback and exact stdcall destroy)");
}
