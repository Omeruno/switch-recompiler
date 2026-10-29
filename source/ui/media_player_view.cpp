#include "desktop_shell.h"
#include "audio_visualization.h"
#include "font.h"
#include "xp_icons.h"
#include "xp_theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace ui {namespace {
void Fill(Surface& s,Rect r,uint32_t color){r=r.Intersect({0,0,s.width,s.height});for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x)s.Set(x,y,color);}
uint32_t Mix(uint32_t a,uint32_t b,int n,int d){uint32_t c=0;for(int shift=0;shift<=16;shift+=8)c|=uint32_t((int((a>>shift)&255)*(d-n)+int((b>>shift)&255)*n)/d)<<shift;return c;}
void Gradient(Surface& s,Rect r,uint32_t a,uint32_t b){for(int y=r.top;y<r.bottom;++y)Fill(s,{r.left,y,r.right,y+1},Mix(a,b,y-r.top,std::max(1,r.Height()-1)));}
void Border(Surface& s,Rect r,uint32_t c){Fill(s,{r.left,r.top,r.right,r.top+1},c);Fill(s,{r.left,r.bottom-1,r.right,r.bottom},c);Fill(s,{r.left,r.top,r.left+1,r.bottom},c);Fill(s,{r.right-1,r.top,r.right,r.bottom},c);}
std::u16string Wide(const std::string& text){std::u16string out;TextDocument::DecodeUtf8(text,out);return out;}
void Text(Surface& s,Rect r,std::u16string text,uint32_t color,int size=14,bool bold=false){FontDesc d;d.face="Liberation Sans";d.height=-size;d.weight=bold?700:400;auto* font=FontCache::Get(d);
    if(font->MeasureWidth(text)>r.Width()){while(!text.empty() && font->MeasureWidth(text+u"…")>r.Width()){const auto c=text.back();text.pop_back();if(c>=0xdc00 && c<=0xdfff && !text.empty())text.pop_back();}text+=u"…";}
    font->Draw(s,Region(r),r.left,r.top,text,color);}
void Line(Surface& s,int x0,int y0,int x1,int y1,Rect clip,uint32_t color){const int steps=std::max(std::abs(x1-x0),std::abs(y1-y0));for(int n=0;n<=steps;++n){const int x=x0+(x1-x0)*n/std::max(1,steps),y=y0+(y1-y0)*n/std::max(1,steps);if(clip.Contains(x,y))s.Set(x,y,color);}}
void Disk(Surface& s,int x,int y,int radius,uint32_t top,uint32_t bottom){for(int dy=-radius;dy<=radius;++dy){const int width=int(std::sqrt(float(radius*radius-dy*dy)));Fill(s,{x-width,y+dy,x+width+1,y+dy+1},Mix(top,bottom,dy+radius,2*radius));}}
}
void DesktopShell::RenderMedia(Surface& s) const {
    const auto d=DialogBounds();const Rect client{d.left+4,d.top+34,d.right-4,d.bottom-4};
    Gradient(s,client,0xDEE7F2,0x8499B4);
    const bool sidebar=d.Width()>=850;const int side=sidebar?158:0;
    const auto path=media_.Path();const auto slash=path.find_last_of("/\\");
    Gradient(s,{client.left,client.top,client.right,client.top+48},0xF2F7FD,0xCAD8EA);
    xp::DrawIcon(s,xp::IconKind::MusicFile,client.left+10,client.top+7,32);
    Text(s,{client.left+50,client.top+6,client.right-12,client.top+27},path.empty()?u"Switch Media Player":Wide(path.substr(slash==std::string::npos?0:slash+1)),0x172F52,17,true);
    const auto state=media_.Status();using MS=rt::MediaPlayer::State;
    const auto status=state==MS::Playing?u"Воспроизведение":state==MS::Paused?u"Пауза":state==MS::Finished?u"Завершено":state==MS::Error?u"Ошибка":u"Готов";
    char info[160];const auto now=media_.Position()/48000,total=media_.Duration()/48000;
    std::snprintf(info,sizeof(info),"%llu:%02llu / %llu:%02llu  ·  %s  ·  %.1f kHz",(unsigned long long)(now/60),(unsigned long long)(now%60),(unsigned long long)(total/60),(unsigned long long)(total%60),media_.Mp3()?"MP3":"PCM WAV",media_.SampleRate()/1000.0);
    Text(s,{client.left+50,client.top+28,client.right-12,client.top+47},media_.Loaded()?Wide(info)+u"  ·  "+status:u"Ваша музыка на Nintendo Switch",0x48617F,12);
    const Rect display{client.left+8+side,client.top+54,client.right-8,d.bottom-154};
    if(sidebar){const Rect nav{client.left+8,client.top+54,client.left+150,d.bottom-154};Gradient(s,nav,0x2D4D7A,0x162B4A);Border(s,nav,0x142940);
        Text(s,{nav.left+12,nav.top+16,nav.right-8,nav.top+54},u"Сейчас играет",0xFFFFFF,15,true);
        Fill(s,{nav.left+1,nav.top+49,nav.right-1,nav.top+52},0x7AB6E8);
        if(nav.Height()>210)xp::DrawIcon(s,xp::IconKind::MusicFile,nav.left+44,nav.top+72,48);
        const int labels=nav.Height()>210?nav.top+134:nav.bottom-50;
        Text(s,{nav.left+12,labels,nav.right-8,labels+20},u"Визуализация",0xADCAE9,13);
        Text(s,{nav.left+12,labels+24,nav.right-8,labels+46},mediaEffect_==0?u"Спектр":mediaEffect_==1?u"Стереоволна":mediaEffect_==2?u"Круги":u"Значок файла",0xFFFFFF,14,true);
    }
    Gradient(s,display,0x061323,0x122D46);Border(s,display,0x273D59);
    for(int y=display.top+24;y<display.bottom;y+=24)Fill(s,{display.left+1,y,display.right-1,y+1},0x173148);
    std::array<int16_t,512> pcm{};const bool live=media_.Visualization(pcm);AudioVisualization analysis;
    if(live)analysis=AudioVisualization::Analyze(pcm);
    const Rect plot{display.left+12,display.top+12,display.right-36,display.bottom-26};
    if(mediaEffect_==0){const int width=std::max(1,plot.Width()/32);
        for(unsigned n=0;n<32;++n){const int height=int(analysis.bands[n]*std::max(1,plot.Height()-8));
            for(int y=0;y<height;y+=5){const auto color=Mix(0x64D7C8,0xC9EE6D,y,std::max(1,plot.Height()));Fill(s,{plot.left+int(n)*width,plot.bottom-y-4,plot.left+int(n)*width+std::max(1,width-3),plot.bottom-y},color);}}
    }else if(mediaEffect_==1){for(unsigned channel=0;channel<2;++channel){const int center=plot.top+plot.Height()*(channel?3:1)/4,scale=std::max(1,plot.Height()/5);
            for(unsigned n=1;n<256;++n)Line(s,plot.left+int(n-1)*std::max(1,plot.Width()-1)/255,center-int(int32_t(pcm[(n-1)*2+channel])*scale/32768),plot.left+int(n)*std::max(1,plot.Width()-1)/255,center-int(int32_t(pcm[n*2+channel])*scale/32768),plot,channel?0xA5E48A:0x72D4FF);}
    }else if(mediaEffect_==2){
        const float radius=std::max(1,std::min(plot.Width(),plot.Height())/2-5),cx=plot.left+plot.Width()/2.0f,cy=plot.top+plot.Height()/2.0f;
        int previousX=0,previousY=0,firstX=0,firstY=0;
        for(unsigned n=0;n<256;++n){const float amplitude=std::max(std::abs(int(pcm[n*2])),std::abs(int(pcm[n*2+1])))/32768.0f;
            const float angle=6.283185307179586f*n/256,r=radius*(0.65f+amplitude*0.3f);const int x=int(cx+std::cos(angle)*r),y=int(cy+std::sin(angle)*r);
            if(n)Line(s,previousX,previousY,x,y,plot,Mix(0x69C9F2,0xB9E789,int(n),255));else{firstX=x;firstY=y;}previousX=x;previousY=y;
        }
        Line(s,previousX,previousY,firstX,firstY,plot,0x84DDD1);
    }else{xp::DrawIcon(s,xp::IconKind::MusicFile,display.left+(display.Width()-64)/2,display.top+(display.Height()-64)/2,64);}
    for(unsigned channel=0;channel<2;++channel){const int x=display.right-28+int(channel)*12;const float rms=channel?analysis.rightRms:analysis.leftRms;const int height=int(std::min(1.0f,rms*3)*plot.Height());
        Fill(s,{x,plot.top,x+6,plot.bottom},0x263D50);Gradient(s,{x,plot.bottom-height,x+6,plot.bottom},0xC5EE78,0x48BDB1);}
    Text(s,{display.left+12,display.bottom-22,display.right-10,display.bottom-3},live?(state==MS::Paused?u"Пауза":mediaEffect_==0?u"Спектр · стерео":mediaEffect_==1?u"Стереоволна":mediaEffect_==2?u"Круги · стерео":u"Сейчас играет"):(media_.Loaded()?status:u"Откройте музыку и нажмите Играть"),0x9ABAD2,12);
    const auto seek=ControlBounds(MediaSeek);Fill(s,{seek.left,seek.top+7,seek.right,seek.top+13},0x3C506C);Border(s,{seek.left,seek.top+6,seek.right,seek.top+14},0x6B82A3);
    const int progress=media_.Duration()?int(uint64_t(seek.Width()-1)*std::min(media_.Position(),media_.Duration())/media_.Duration()):0;
    Fill(s,{seek.left,seek.top+8,seek.left+progress,seek.top+12},0x64C0E8);Disk(s,seek.left+progress,seek.top+10,7,0xFAFDFF,0x758FAB);
    auto button=[&](int target,const std::u16string& label){const auto r=ControlBounds(target);const bool enabled=Enabled(target),hot=hover_==target && enabled;
        Gradient(s,r,enabled?(hot?0xFAFDFF:0xE7EFF8):0xA6B4C5,enabled?0x9DAFC6:0x8E9FB4);Border(s,r,hot?0xFFFFFF:0x5B7293);
        if(pressed_==target && hot)Fill(s,r,0x92B3D6);
        Text(s,{r.left+7,r.top+(r.Height()-16)/2,r.right-5,r.bottom-2},label,enabled?0x143050:0x65768E,13,target==MediaPlay);
        if(focus_==target && enabled)xp::DrawFocusRect(s,{r.left+3,r.top+3,r.right-3,r.bottom-3});};
    button(MediaPlay,u"Играть");button(MediaPause,u"Пауза");button(MediaStop,u"Стоп");button(MediaBack,u"−10 сек");button(MediaForward,u"+10 сек");
    const auto play=ControlBounds(MediaPlay);Disk(s,play.right-20,play.top+play.Height()/2,12,Enabled(MediaPlay)?0xC6F39D:0xB6C2CE,Enabled(MediaPlay)?0x427F32:0x788DA3);
    const int cx=play.right-23,cy=play.top+play.Height()/2;for(int x=0;x<9;++x)Fill(s,{cx+x,cy-7+x*7/9,cx+x+1,cy+8-x*7/9},0xFFFFFF);
    button(MediaChoose,u"Открыть…");button(MediaEffect,mediaEffect_==0?u"Эффект: спектр":mediaEffect_==1?u"Эффект: волна":mediaEffect_==2?u"Эффект: круги":u"Эффект: выкл.");button(MediaVolumeDown,u"−");button(MediaVolumeUp,u"+");button(CancelDialog,u"Закрыть");
    const auto volume=ControlBounds(MediaVolumeDown);Text(s,{volume.left,d.bottom-63,d.right-128,d.bottom-49},u"Громкость "+Wide(std::to_string(media_.Volume()))+u"%",0x243F60,12);
    if(!media_.Error().empty())Text(s,{client.left+12,display.bottom+2,client.right-8,display.bottom+18},Wide(media_.Error()),0x8E201A,12);
}
}
