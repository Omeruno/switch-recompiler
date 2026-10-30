#include "source/runtime/resources.h"
#include <cassert>
#include <cstdio>
static void W16(rt::Module& m,size_t p,uint16_t v){m.image[p]=uint8_t(v);m.image[p+1]=uint8_t(v>>8);}
static void W32(rt::Module& m,size_t p,uint32_t v){W16(m,p,uint16_t(v));W16(m,p+2,uint16_t(v>>16));}
static rt::Module Make(){rt::Module m;m.resourceRva=0x100;m.image.resize(0x400);
    W16(m,0x10e,1);W32(m,0x110,6);W32(m,0x114,0x80000030);
    W16(m,0x13e,1);W32(m,0x140,1);W32(m,0x144,0x80000060);
    W16(m,0x16e,1);W32(m,0x170,0x409);W32(m,0x174,0x90);
    W32(m,0x190,0x200);W32(m,0x194,36);W16(m,0x202,2);W16(m,0x204,'A');W16(m,0x206,'B');return m;}
int main(){
    auto m=Make();rt::ResourceData data;uint32_t offset=0;uint16_t chars=0;std::u16string text;
    assert(rt::FindStringResourceSpan(&m,1,data,offset,chars) && offset==4 && chars==2 && data.rva==0x200);
    assert(rt::LoadStringResource(&m,0x10001,text) && text==u"AB");
    assert(rt::FindStringResourceSpan(&m,3,data,offset,chars) && !chars);
    text=u"old";assert(!rt::LoadStringResource(&m,3,text) && text.empty());
    for(unsigned n=0;n<9;++n){auto bad=Make();
        switch(n){
        case 0:bad.resourceRva=0xfffffff0;break;
        case 1:W32(bad,0x114,0xfffffff0);break;
        case 2:W16(bad,0x10c,0xffff);W16(bad,0x10e,0xffff);break;
        case 3:W32(bad,0x144,0x800003f8);break;
        case 4:W32(bad,0x174,0x3fc);break;
        case 5:W32(bad,0x190,0xfffffff0);break;
        case 6:W32(bad,0x194,0xffffffff);break;
        case 7:W16(bad,0x200,0xffff);break;
        case 8:W16(bad,0x202,0xffff);break;
        }
        assert(!rt::FindStringResourceSpan(&bad,1,data,offset,chars));text=u"old";assert(!rt::LoadStringResource(&bad,1,text) && text.empty());
    }
    auto named=Make();W32(named,0x110,0xfffffffc);assert(!rt::FindResource(&named,rt::ResId::Str(u"WAVE"),rt::ResId::Num(1),data));
    m.unloaded=true;assert(!rt::FindStringResourceSpan(&m,1,data,offset,chars));
    puts("Resource spans PASS (real UTF16 table/empty/low-word ID,full directory/name/data bounds,overflow/truncation/malformed predecessor strings,unloaded module rejection)");
}
