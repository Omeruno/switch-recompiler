#include "source/ui/presentation.h"
#include <cstdio>
#include <cassert>
int main(){
    std::puts("Host CPU snapshot microbenchmark: 120 warm native-size submissions; excludes GDI rasterization, GPU and scanout. Not Switch FPS.");
    ui::WindowLayers windows;assert(windows.Enable(1280,720));ui::OwnedSurface screen;screen.Allocate(1280,720);
    ui::Surface raster=screen.s;raster.writeCapture=windows.Capture(1,0,0,1280,720);
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x)raster.Set(x,y,0x182030);
    ui::Presentation flat,layered;assert(flat.Submit(screen.s) && layered.Submit(screen.s,windows.Scene()));
    for(int mode=0;mode<2;++mode){
        flat.ResetStats();layered.ResetStats();
        for(int n=0;n<120;++n){if(mode){raster.writeCapture=windows.Capture(1,0,0,1280,720);raster.Set(63,63,0x123400+n);}assert(flat.Submit(screen.s) && layered.Submit(screen.s,windows.Scene()));}
        std::printf("host %s: flat snapshot %.3f ms, layered snapshot %.3f ms (layer stage %.3f ms), owned layer storage %zu bytes\n",mode?"one-pixel change":"unchanged",flat.Stats().snapshotNs/120e6,layered.Stats().snapshotNs/120e6,layered.Stats().layerSnapshotNs/120e6,layered.LayerBytes());
    }
}
