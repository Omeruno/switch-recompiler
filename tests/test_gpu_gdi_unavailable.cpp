#include "source/ui/gpu_gdi_prototype.h"
#include "source/ui/gpu_gdi_targets.h"
#include <cassert>
#include <cstdio>
int main() {
    ui::GpuGdiPrototype gpu;
    assert(!gpu.Initialize(31,23));assert(gpu.Backend()==ui::GpuGdiPrototype::Mode::Empty);
    assert(!gpu.Fill({0,0,1,1},0));uint32_t pixel=0;
    assert(!gpu.Copy({0,0,1,1},&pixel,1));assert(!gpu.FlushGpu() && !gpu.CpuRead());
    gpu.ContextLost();assert(!gpu.Counters().draws && !gpu.Counters().readbacks);
    ui::GpuGdiTargets targets;assert(!targets.Initialize() && !targets.GpuActive() && !targets.Create(1,1));
    assert(!targets.Present(nullptr,0,64,32) && !targets.ComposeCpu(nullptr,0,64,32));targets.ContextLost();
    puts("GPU GDI unavailable: PASS (no simulated GPU success in ordinary host build)");
}
