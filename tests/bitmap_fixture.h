#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace bitmap_fixture {
inline void Put16(std::vector<uint8_t>& b,size_t i,uint16_t v){b[i]=uint8_t(v);b[i+1]=uint8_t(v>>8);}
inline void Put32(std::vector<uint8_t>& b,size_t i,uint32_t v){for(unsigned n=0;n<4;++n)b[i+n]=uint8_t(v>>(8*n));}
inline uint32_t Color(unsigned x,unsigned y){return ((x&255)<<16)|((y&255)<<8)|((x+y)&255);}
inline std::vector<uint8_t> Make(unsigned w,unsigned h,unsigned bpp=24,bool topDown=false) {
    const unsigned colors=bpp<=8?1u<<bpp:0,offset=54+4*colors,pitch=((w*bpp+31)/32)*4;
    std::vector<uint8_t> b(offset+pitch*h);b[0]='B';b[1]='M';Put32(b,2,uint32_t(b.size()));Put32(b,10,offset);
    Put32(b,14,40);Put32(b,18,w);Put32(b,22,topDown?uint32_t(-int(h)):h);Put16(b,26,1);Put16(b,28,uint16_t(bpp));Put32(b,34,pitch*h);
    for(unsigned i=0;i<colors;++i){auto c=Color(i,0);b[54+4*i]=uint8_t(c);b[55+4*i]=uint8_t(c>>8);b[56+4*i]=uint8_t(c>>16);}
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        const unsigned row=offset+(topDown?y:h-1-y)*pitch,index=colors?(x+y)%colors:0;
        if(bpp>=24){auto c=Color(x,y);b[row+x*(bpp/8)]=uint8_t(c);b[row+x*(bpp/8)+1]=uint8_t(c>>8);b[row+x*(bpp/8)+2]=uint8_t(c>>16);if(bpp==32)b[row+x*4+3]=255;}
        else if(bpp==8)b[row+x]=uint8_t(index);
        else if(bpp==4)b[row+x/2]|=uint8_t(index<<((x%2)?0:4));
        else b[row+x/8]|=uint8_t(index<<(7-x%8));
    }
    return b;
}
} // namespace bitmap_fixture
