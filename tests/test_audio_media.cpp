#include "source/runtime/media_player.h"
#include "source/runtime/pcm_stream.h"
#include "source/runtime/clock.h"
#include "source/ui/desktop_shell.h"
#include "source/ui/font.h"
#include "tests/audio_capture_device.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstdlib>
namespace fs=std::filesystem;
static std::vector<uint8_t> Bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);assert(f);return {(std::istreambuf_iterator<char>(f)),{}};}
static void Click(ui::DesktopShell& s,int target){auto r=s.ControlBounds(target);assert(!r.Empty() && s.Enabled(target));const int x=r.left+r.Width()/2,y=r.top+r.Height()/2;s.PointerDown(x,y);s.PointerUp(x,y);}
static double Tone(const std::vector<int16_t>& pcm,unsigned channel,double hz){double a=0,b=0;const size_t end=std::min(pcm.size()/2,size_t(45000));assert(end>1000);for(size_t n=1000;n<end;++n){const double phase=6.283185307179586*hz*n/48000;a+=pcm[n*2+channel]*std::sin(phase);b+=pcm[n*2+channel]*std::cos(phase);}return 2*std::hypot(a,b)/double(end-1000);}
static void Release(AudioCaptureDevice& d){assert(!d.paused && !d.active.empty());const auto it=d.active.begin();d.playedFrames+=it->second->Bytes()/4;d.released.push_back(it->first);}
int main(int argc,char**argv){
    const fs::path fixtures=argc>1?argv[1]:"audio";
    ui::FontCache::SetDirectory((fs::absolute(fixtures).parent_path().parent_path().parent_path()/"romfs/fonts").string());
    for(const char* name:{"stereo-vbr.mp3","mono-cbr.mp3","stereo-raw.mp3"}){
        rt::AudioDecoder decoder;assert(decoder.OpenBytes(Bytes(fixtures/name)));assert(decoder.IsMp3());assert(decoder.SampleRate()==(std::string(name)=="stereo-vbr.mp3"?44100:std::string(name)=="mono-cbr.mp3"?22050:48000));assert(decoder.SourceFrameCount()*48000/decoder.SampleRate()==decoder.Duration());std::vector<int16_t> all,first;
        for(unsigned n=0;n<1000;++n){std::vector<int16_t> pcm;assert(decoder.Read(pcm));assert(pcm.size()<=rt::AudioDecoder::SourceFrames*12+12);if(pcm.empty())break;if(first.empty())first=pcm;all.insert(all.end(),pcm.begin(),pcm.end());}
        assert(all.size()/2==decoder.Duration());
        if(std::string(name)=="stereo-vbr.mp3"){
            assert(decoder.Duration()==384000 && all.size()>rt::AudioOutput::MaxSamples);
            assert(Tone(all,0,220)>6000 && Tone(all,1,660)>6000);
            assert(Tone(all,0,660)<300 && Tone(all,1,220)<300);
        }else if(std::string(name)=="mono-cbr.mp3"){
            assert(decoder.Duration()==48000 && Tone(all,0,440)>2500);
            for(size_t n=0;n<all.size();n+=2)assert(all[n]==all[n+1]);
        }else{assert(decoder.Duration()==49536 && Tone(all,0,330)>4000 && Tone(all,1,990)>4000);}
        assert(decoder.Seek(0));std::vector<int16_t> again;assert(decoder.Read(again) && again==first);
        // Arbitrary indexed forward/backward seeks must produce the actual
        // linear-decoded signal, not merely report the requested position.
        // Half-second positions align exactly at all three fixture rates.
        for(uint64_t at:{(decoder.Duration()/24000-1)*24000,uint64_t(24000),uint64_t(0)}){
            assert(decoder.Seek(at));assert(decoder.Read(again) && again.size()>=512);
            for(size_t n=0;n<512;++n)assert(std::abs(int(again[n])-int(all[size_t(at)*2+n]))<=2);
        }
        assert(decoder.Seek(decoder.Duration()) && decoder.Read(again) && again.empty());
        assert(decoder.Seek(0) && decoder.Read(again) && again==first);
        const auto old=decoder.Duration();assert(!decoder.OpenBytes({1,2,3,4}) && decoder.Duration()==old);
    }
    { // Real PCM WAV decoding and malformed-container refusal preserve old media.
        std::vector<uint8_t> wav(52);auto u16=[&](size_t at,uint16_t v){wav[at]=uint8_t(v);wav[at+1]=uint8_t(v>>8);};auto u32=[&](size_t at,uint32_t v){u16(at,uint16_t(v));u16(at+2,uint16_t(v>>16));};
        for(size_t n=0;n<4;++n){wav[n]=uint8_t("RIFF"[n]);wav[n+8]=uint8_t("WAVE"[n]);wav[n+12]=uint8_t("fmt "[n]);wav[n+36]=uint8_t("data"[n]);}
        u32(4,44);u32(16,16);u16(20,1);u16(22,1);u32(24,8000);u32(28,8000);u16(32,1);u16(34,8);u32(40,8);
        const uint8_t values[]={0,64,128,192,255,192,128,64};for(size_t n=0;n<8;++n)wav[44+n]=values[n];
        rt::AudioDecoder decoder;assert(decoder.OpenBytes(wav) && !decoder.IsMp3() && decoder.Duration()==48);
        std::vector<int16_t> pcm;assert(decoder.Read(pcm) && pcm.size()==96 && pcm[0]==-32768 && pcm[94]==-16384);for(size_t n=0;n<pcm.size();n+=2)assert(pcm[n]==pcm[n+1]);
        u16(32,2);assert(!decoder.OpenBytes(wav) && decoder.Duration()==48);assert(decoder.Seek(48) && decoder.Read(pcm) && pcm.empty());
    }
    // Bounded deterministic malformed-data probe, also run under ASan/UBSan.
    {uint32_t seed=0x19b7;for(unsigned iteration=0;iteration<128;++iteration){std::vector<uint8_t> bytes(64+iteration*17);for(auto& b:bytes){seed=seed*1664525+1013904223;b=uint8_t(seed>>24);}if(iteration%4==0){bytes[0]='R';bytes[1]='I';bytes[2]='F';bytes[3]='F';bytes[8]='W';bytes[9]='A';bytes[10]='V';bytes[11]='E';}
        rt::AudioDecoder decoder;if(decoder.OpenBytes(std::move(bytes))){std::vector<int16_t> pcm;assert(decoder.Read(pcm) && pcm.size()<=rt::AudioDecoder::SourceFrames*12+12);}}}
    // Independent linear ramp and chunk partition/phase equivalence.
    {rt::PcmStream linear({24000,1,16});std::vector<int16_t> out;assert(linear.Push({0,1000,2000},true,out));assert((out==std::vector<int16_t>{0,0,500,500,1000,1000,1500,1500,2000,2000,2000,2000}));}
    for(uint32_t rate:{8000u,22050u,44100u,48000u,96000u})for(bool interpolate:{false,true}){
        std::vector<int16_t> input(701);for(size_t n=0;n<input.size();++n)input[n]=int16_t((n*937)%60000-30000);
        rt::PcmStream whole({rate,1,16},interpolate),chunks({rate,1,16},interpolate);std::vector<int16_t> expected,actual,out;
        assert(whole.Push(input,true,expected));
        for(size_t at=0;at<input.size();){const auto count=std::min(size_t(17),input.size()-at);assert(chunks.Push({input.begin()+at,input.begin()+at+count},at+count==input.size(),out));actual.insert(actual.end(),out.begin(),out.end());at+=count;}
        assert(actual==expected && actual.size()/2==input.size()*48000/rate);
    }
    char pattern[]="/tmp/sr-media-XXXXXX";assert(mkdtemp(pattern));const fs::path root=pattern;
    struct Cleanup{fs::path p;~Cleanup(){fs::remove_all(p);}}cleanup{root};rt::Vfs vfs;std::string error;assert(vfs.ConfigurePersistent(root.string(),"","",error));
    fs::copy_file(fixtures/"stereo-vbr.mp3",root/"Programs/Моя музыка.mp3");
    AudioCaptureDevice device;assert(rt::Audio().Bind(&device));
    {
        rt::AudioScope scope;rt::MediaPlayer player;assert(player.Open(vfs,"C:\\Programs\\Моя музыка.mp3") && player.Duration()==384000);
        assert(player.Play() && device.active.size()==4 && rt::Audio().Client());
        std::array<int16_t,512> visual{};assert(player.Visualization(visual));const auto* actual=static_cast<const int16_t*>(device.active.begin()->second->Data());
        for(size_t n=0;n<visual.size();++n)assert(visual[n]==actual[n]);
        device.playedFrames+=128;assert(player.Visualization(visual));for(size_t n=0;n<visual.size();++n)assert(visual[n]==actual[n+256]);
        const auto first=device.active.begin()->second;assert(first->Bytes()<=rt::AudioDecoder::SourceFrames*24+24);
        assert(player.Pause() && device.paused);std::array<int16_t,512> frozen{};assert(player.Visualization(frozen));const auto position=player.Position();rt::Clock::SetFastForward(true);rt::Clock::Sleep(500);rt::Clock::SetFastForward(false);player.Tick();assert(player.Position()==position && device.active.size()==4);assert(player.Visualization(visual) && visual==frozen);
        assert(player.Seek(48000) && device.paused && device.active.empty() && player.Position()==48000);
        assert(player.Play() && !device.paused);player.Tick();assert(device.active.size()==4);
        assert(player.SetVolume(25) && player.Volume()==25 && device.volume==16383);
        device.resetFails=true;assert(!player.Seek(96000) && device.active.size()==4 && rt::Audio().Client());
        assert(!player.Stop() && device.active.size()==4 && rt::Audio().Client());device.resetFails=false;assert(player.Stop() && device.active.empty() && !rt::Audio().Client());
        assert(player.Play());for(unsigned n=0;n<1000 && player.Status()!=rt::MediaPlayer::State::Finished;++n){Release(device);player.Tick();}
        assert(player.Status()==rt::MediaPlayer::State::Finished && player.Position()==384000 && device.active.empty() && !rt::Audio().Client());
        assert(player.Play() && device.active.size()==4);const auto client=rt::Audio().Client();
        {rt::AudioScope nested;assert(device.active.empty());player.Tick();assert(player.Status()==rt::MediaPlayer::State::Error && !player.Stop());}
        assert(rt::Audio().Client()==client && player.Stop() && !rt::Audio().Client());
        const auto other=uint64_t(123456);assert(rt::Audio().Claim(rt::Audio().Owner(),other));assert(!player.Play() && !player.SetVolume(100));assert(rt::Audio().Client()==other);assert(player.Stop() && rt::Audio().Client()==other);assert(rt::Audio().ReleaseClient(rt::Audio().Owner(),other));
        assert(player.Play());device.controlFails=true;player.Tick();assert(player.Status()==rt::MediaPlayer::State::Error);device.controlFails=false;assert(player.Stop());
        // Real shell activation, controls, minimise/restore, resize and close.
        ui::DesktopShell shell;assert(shell.ConfigureFilesystem(root.string(),"","",error));Click(shell,ui::DesktopShell::Start);Click(shell,ui::DesktopShell::MediaApp);
        assert(shell.ActiveDialog()==ui::DesktopShell::Dialog::MediaPlayer);Click(shell,ui::DesktopShell::MediaChoose);assert(shell.PendingTextInput()==ui::DesktopShell::TextInput::MediaPath);
        shell.CompleteTextInput(true,"C:\\Programs\\Моя музыка.mp3");assert(shell.Media().Status()==rt::MediaPlayer::State::Playing);shell.ServiceMedia();
        const auto previewTokens=device.active.size();Click(shell,ui::DesktopShell::MediaEffect);assert(shell.MediaEffectMode()==1 && device.active.size()==previewTokens);
        Click(shell,ui::DesktopShell::MediaEffect);assert(shell.MediaEffectMode()==2);{ui::OwnedSurface frame;frame.Allocate(1280,720);shell.Render(frame.s,u"12:34");}Click(shell,ui::DesktopShell::MediaEffect);assert(shell.MediaEffectMode()==3);{ui::OwnedSurface frame;frame.Allocate(1280,720);shell.Render(frame.s,u"12:34");}Click(shell,ui::DesktopShell::MediaEffect);assert(shell.MediaEffectMode()==0);
        Click(shell,ui::DesktopShell::MediaSeek);assert(shell.Media().Position()>180000 && shell.Media().Position()<200000);
        // Repeated rail seeks across pending buffers, paused playback, EOF and
        // replay. A position-only decoder mock cannot pass the PCM oracle above.
        for(bool paused:{false,true}){
            if(paused)Click(shell,ui::DesktopShell::MediaPause);
            for(unsigned part:{9u,1u,7u,0u,10u,2u}){
                const auto rail=shell.ControlBounds(ui::DesktopShell::MediaSeek);
                const int x=rail.left+(rail.Width()-1)*int(part)/10,y=(rail.top+rail.bottom)/2;
                shell.PointerDown(x,y);shell.PointerUp(x,y);shell.ServiceMedia();
                assert(shell.Media().Error().empty());
                const auto expected=uint64_t(x-rail.left)*shell.Media().Duration()/uint64_t(rail.Width()-1);
                assert(shell.Media().Position()==expected);
                assert(shell.Media().Status()==(paused?rt::MediaPlayer::State::Paused:rt::MediaPlayer::State::Playing) || part==10);
                assert(device.active.size()<=4);
                if(part!=10){assert(shell.Media().Visualization(visual)==!paused);}
                else if(!paused){assert(shell.Media().Status()==rt::MediaPlayer::State::Finished);Click(shell,ui::DesktopShell::MediaPlay);}
            }
            if(paused)Click(shell,ui::DesktopShell::MediaPlay);
        }
        Click(shell,ui::DesktopShell::MediaPause);assert(device.paused);Click(shell,ui::DesktopShell::MediaForward);assert(device.paused && shell.Media().Position()==384000);
        Click(shell,ui::DesktopShell::MediaStop);Click(shell,ui::DesktopShell::MediaPlay);assert(!device.paused && device.active.size()==4);
        Click(shell,ui::DesktopShell::DialogMinimize);assert(shell.NativeMinimized());Release(device);shell.ServiceMedia();assert(device.active.size()==4);
        Click(shell,ui::DesktopShell::NativeTask);assert(!shell.NativeMinimized());Click(shell,ui::DesktopShell::DialogMaximize);assert(shell.NativeMaximized());
        ui::OwnedSurface surface;surface.Allocate(1280,720);assert(surface.s.Valid());shell.Render(surface.s,u"12:34");
        if(const auto* capture=std::getenv("AUDIO_MEDIA_CAPTURE")){
            FILE* file=std::fopen(capture,"wb");assert(file);std::fprintf(file,"P6\n1280 720\n255\n");
            for(int y=0;y<720;++y)for(int x=0;x<1280;++x){const auto pixel=surface.s.Get(x,y);const uint8_t rgb[]={uint8_t(pixel>>16),uint8_t(pixel>>8),uint8_t(pixel)};assert(std::fwrite(rgb,1,3,file)==3);}
            assert(!std::fclose(file));
        }
        Click(shell,ui::DesktopShell::DialogMaximize);
        if(const auto* capture=std::getenv("AUDIO_MEDIA_CAPTURE")){
            Click(shell,ui::DesktopShell::MediaEffect);shell.Render(surface.s,u"12:34");const auto path=std::string(capture)+"-wave.ppm";FILE* file=std::fopen(path.c_str(),"wb");assert(file);std::fprintf(file,"P6\n1280 720\n255\n");
            for(int y=0;y<720;++y)for(int x=0;x<1280;++x){const auto pixel=surface.s.Get(x,y);const uint8_t rgb[]={uint8_t(pixel>>16),uint8_t(pixel>>8),uint8_t(pixel)};assert(std::fwrite(rgb,1,3,file)==3);}
            assert(!std::fclose(file));Click(shell,ui::DesktopShell::MediaEffect);Click(shell,ui::DesktopShell::MediaEffect);Click(shell,ui::DesktopShell::MediaEffect);
        }
        const auto bounds=shell.DialogBounds();shell.PointerDown(bounds.right-2,bounds.bottom-2);assert(shell.DraggingWindow());shell.PointerMove(bounds.right+90,bounds.bottom+50);shell.PointerUp(bounds.right+90,bounds.bottom+50);assert(shell.DialogBounds().Width()>bounds.Width());
        Click(shell,ui::DesktopShell::DialogClose);assert(shell.ActiveDialog()==ui::DesktopShell::Dialog::None && !shell.Media().Loaded() && device.active.empty() && !rt::Audio().Client());
        // Default selection/activate route, not only the native Open dialog.
        Click(shell,ui::DesktopShell::Computer);Click(shell,ui::DesktopShell::QuickC);
        for(size_t n=0;n<shell.Browser().Entries().size();++n)if(shell.Browser().Entries()[n].name=="Programs"){Click(shell,ui::DesktopShell::BrowserRowBase+int(n));shell.Accept();break;}
        for(size_t n=0;n<shell.Browser().Entries().size();++n)if(shell.Browser().Entries()[n].name=="Моя музыка.mp3"){Click(shell,ui::DesktopShell::BrowserRowBase+int(n));shell.Accept();break;}
        if(shell.ActiveDialog()!=ui::DesktopShell::Dialog::MediaPlayer || shell.Media().Status()!=rt::MediaPlayer::State::Playing){fprintf(stderr,"Default media route path=%s dialog=%u state=%u error=%s\n",shell.Browser().Path().c_str(),unsigned(shell.ActiveDialog()),unsigned(shell.Media().Status()),shell.Media().Error().c_str());for(const auto& entry:shell.Browser().Entries())fprintf(stderr,"entry %s\n",entry.name.c_str());}
        assert(shell.ActiveDialog()==ui::DesktopShell::Dialog::MediaPlayer && shell.Media().Status()==rt::MediaPlayer::State::Playing);
        Click(shell,ui::DesktopShell::DialogClose);assert(device.active.empty() && !rt::Audio().Client());
    }
    assert(rt::Audio().Bind(nullptr));
    puts("Audio media PASS (real pinned MP3 VBR/CBR/ID3/raw frames,8-second stereo/mono/tone spectra,continuous PCM partition oracle,bounded streaming,pause/seek/volume/reset faults/busy/nested owner,real shell player controls/window lifecycle;hardware sound remains pending)");
}
