#include "window_gpu_bindings.h"
#include "user32.h"
#include <limits>
#include <chrono>

namespace win32::user {
static_assert(ui::GpuGdiPrototype::MaxSide==ui::Gdi::MaxWindowMaskSide,"masked commit and GPU image bounds must agree");
namespace {
bool SameRect(const ui::Rect& a,const ui::Rect& b) {
    return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
}
bool Same(const ui::WindowRasterTarget& a,const ui::WindowRasterTarget& b) {
    if(a.windowIdentity!=b.windowIdentity || a.dcIdentity!=b.dcIdentity ||
       a.layoutGeneration!=b.layoutGeneration || a.hwnd!=b.hwnd || a.hdc!=b.hdc ||
       !SameRect(a.frame,b.frame) || a.originX!=b.originX || a.originY!=b.originY ||
       a.windowOrg.x!=b.windowOrg.x || a.windowOrg.y!=b.windowOrg.y ||
       a.viewportOrg.x!=b.viewportOrg.x || a.viewportOrg.y!=b.viewportOrg.y ||
       a.wholeWindow!=b.wholeWindow || a.clip.Rects().size()!=b.clip.Rects().size())return false;
    for(size_t i=0;i<a.clip.Rects().size();++i)if(!SameRect(a.clip.Rects()[i],b.clip.Rects()[i]))return false;
    return true;
}
bool Size(const ui::Rect& r,int& w,int& h) {
    const int64_t x=int64_t(r.right)-r.left,y=int64_t(r.bottom)-r.top;
    if(x<=0 || y<=0 || x>ui::GpuGdiPrototype::MaxSide || y>ui::GpuGdiPrototype::MaxSide)return false;
    w=int(x);h=int(y);return true;
}
}
bool WindowGpuBindings::Initialize(){Shutdown();return targets_.Initialize();}
void WindowGpuBindings::Shutdown(){ownershipRevision_=ui::NewPresentationResourceId();if(!DisableCoherence())RevokeGate();slots_={};frameOwners_.Clear();targets_.Shutdown();}
void WindowGpuBindings::Prune() {
    if(coherent_ && (!SameSession() || fault_))return;
    for(auto& s:slots_)if(s.target) {
        auto* w=Win(s.hwnd);int width=0,height=0;
        if(!w || w->rasterIdentity!=s.identity || !Size(ScreenRect(w),width,height) ||
           width!=s.width || height!=s.height) {
            if(coherent_ && Dirty(s) && !CheckpointCpu())return;
            targets_.Destroy(s.target);s={};ownershipRevision_=ui::NewPresentationResourceId();
        }
    }
}
bool WindowGpuBindings::Acquire(uint32_t hdc,Lease& out) {
    out={};if(coherent_ && !SameSession())return false;Prune();
    if(coherent_ && (!SameSession() || fault_))return false;
    ui::WindowRasterTarget r;int width=0,height=0;
    if(!targets_.GpuActive() || !ui::g_gdi.DescribeWindowTarget(hdc,r) || r.hwnd==S().desktop ||
       !Size(r.frame,width,height) || r.clip.Empty() || r.clip.Rects().size()>MaxClipRects)return false;
    for(const auto& s:slots_)if(s.identity==r.windowIdentity && s.target){out={std::move(r),s.target};return true;}
    for(auto& s:slots_)if(!s.target) {
        const auto target=targets_.Create(width,height);if(!target)return false;
        s.hwnd=r.hwnd;s.identity=r.windowIdentity;s.width=width;s.height=height;s.target=target;
        s.seed=ui::PresentationBuffer<uint32_t>(allocator_);s.damage=ui::PresentationBuffer<uint8_t>(allocator_);
        out={std::move(r),target};return true;
    }
    return false;
}
bool WindowGpuBindings::Valid(const Lease& lease) {
    if(coherent_ && (!SameSession() || fault_))return false;
    if(!lease.target || lease.raster.clip.Rects().size()>MaxClipRects)return false;
    bool found=false;
    for(const auto& s:slots_)if(s.target==lease.target && s.identity==lease.raster.windowIdentity){found=true;break;}
    if(!found)return false;
    ui::WindowRasterTarget now;
    return ui::g_gdi.DescribeWindowTarget(lease.raster.hdc,now) && Same(now,lease.raster);
}
bool WindowGpuBindings::Fill(const Lease& lease,ui::Rect logical,uint32_t rgb) {
    if(!Valid(lease))return false;
    if(logical.Empty())return true;
    const auto& r=lease.raster;
    const int64_t dx=int64_t(r.originX)+r.viewportOrg.x-r.windowOrg.x;
    const int64_t dy=int64_t(r.originY)+r.viewportOrg.y-r.windowOrg.y;
    const int64_t x0=int64_t(logical.left)+dx,x1=int64_t(logical.right)+dx;
    const int64_t y0=int64_t(logical.top)+dy,y1=int64_t(logical.bottom)+dy;
    constexpr auto low=std::numeric_limits<int>::min(),high=std::numeric_limits<int>::max();
    if(x0<low || x0>high || x1<low || x1>high || y0<low || y0>high || y1<low || y1>high)return false;
    const ui::Rect screen{int(x0),int(y0),int(x1),int(y1)};
    auto* slot=FindSlot(lease);
    if(coherent_ && (!slot || !Seed(*slot,lease)))return false;
    ui::Region operation;
    for(const auto& clip:r.clip.Rects()) {
        const auto part=screen.Intersect(clip).Intersect(r.frame);if(part.Empty())continue;
        operation.UnionRect({int(int64_t(part.left)-r.frame.left),int(int64_t(part.top)-r.frame.top),
            int(int64_t(part.right)-r.frame.left),int(int64_t(part.bottom)-r.frame.top)});
    }
    if(coherent_) {
        if(operation.Rects().size()>MaxClipRects)return false;
        if(!slot->masked) {
            auto next=slot->dirty;bool fragmented=false;
            // Stop at the first excess; do not construct an entire cross-product
            // of fragmented clip rectangles just to discover that it is too big.
            for(const auto& part:operation.Rects()) {
                next.UnionRect(part);
                if(next.Rects().size()>MaxClipRects){fragmented=true;break;}
            }
            if(fragmented) {
                const bool promoted=exactDamage_ && PromoteDamage(*slot);
                if(!promoted) {
                    if(exactDamage_)++coherenceStats_.maskFallbacks;
                    if(!CheckpointCpu())return false;
                }
            }
        }
        slot->stamp.hwnd=r.hwnd;slot->stamp.windowIdentity=r.windowIdentity;
        slot->stamp.layoutGeneration=r.layoutGeneration;slot->stamp.frame=r.frame;
    }

    for(const auto& clip:r.clip.Rects()) {
        const auto part=screen.Intersect(clip).Intersect(r.frame);if(part.Empty())continue;
        const ui::Rect local{int(int64_t(part.left)-r.frame.left),int(int64_t(part.top)-r.frame.top),
            int(int64_t(part.right)-r.frame.left),int(int64_t(part.bottom)-r.frame.top)};
        const auto* counters=targets_.TargetStats(lease.target);
        const ui::GpuGdiPrototype::Stats before=counters?*counters:ui::GpuGdiPrototype::Stats{};
        const bool accepted=targets_.Fill(lease.target,local,rgb);
        counters=targets_.TargetStats(lease.target);
        if(coherent_ && counters) {
            coherenceStats_.capacityCheckpoints+=counters->capacityCheckpoints-before.capacityCheckpoints;
            coherenceStats_.capacityReadbacks+=counters->readbacks-before.readbacks;
            coherenceStats_.capacityReplays+=counters->replayCheckpoints-before.replayCheckpoints;
            coherenceStats_.capacityReplayPixels+=counters->replayPixels-before.replayPixels;
            coherenceStats_.capacityReplayBudgetFallbacks+=counters->replayBudgetFallbacks-before.replayBudgetFallbacks;
            coherenceStats_.capacityReplayFailures+=counters->replayFailures-before.replayFailures;
        }
        if(!accepted)return false;
        if(coherent_){if(!Dirty(*slot))ownershipRevision_=ui::NewPresentationResourceId();if(slot->masked)MarkDamage(*slot,local);else slot->dirty.UnionRect(local);++gpuRevision_;}
    }
    return true;
}
struct WindowGpuBindings::Gate final:ui::CpuRasterGate,ui::SolidRasterBackend {
    WindowGpuBindings* owner;
    explicit Gate(WindowGpuBindings* o):owner(o) {}
    bool Prepare() override {return owner && owner->BeforeCpu();}
    ui::RasterDispatch Fill(uint32_t dc,ui::Rect r,uint32_t rgb) override {
        return owner?owner->DispatchFill(dc,r,rgb):ui::RasterDispatch::Failed;
    }
};
bool WindowGpuBindings::SameSession() const {
    const auto* desktop=Desktop();return desktop && desktop->rasterIdentity==desktopIdentity_;
}
bool WindowGpuBindings::EnableCoherence() {
    if(coherent_)return SameSession() && !fault_;
    if(Pixels() || !GpuActive() || !Desktop() || !Desktop()->rasterIdentity)return false;
    auto gate=std::make_shared<Gate>(this);
    if(!ui::g_gdi.InstallCpuGate(gate))return false;
    gate_=std::move(gate);desktopIdentity_=Desktop()->rasterIdentity;
    coherent_=true;fault_=false;faultReason_=Fault::None;coherenceStats_={};gpuRevision_=0;ownershipRevision_=ui::NewPresentationResourceId();return true;
}
bool WindowGpuBindings::EnableSolidDispatch() {
    if(!PresentationReady() || !GpuActive())return false;
    if(dispatchEnabled_)return true;
    if(!ui::g_gdi.InstallSolidBackend(gate_))return false;
    targets_.EnableCoveredRetirement(true);targets_.EnableCapacityReplay(true);targets_.EnableDifferentialImport(true);exactDamage_=true;dispatchEnabled_=true;dispatchStats_={};return true;
}
ui::RasterDispatch WindowGpuBindings::DispatchFill(uint32_t dc,ui::Rect r,uint32_t rgb) {
    ++dispatchStats_.candidates;
    auto failed=[&](){++dispatchStats_.failed;Fail(Fault::Dispatch);return ui::RasterDispatch::Failed;};
    auto refused=[&](){++dispatchStats_.refused;return ui::RasterDispatch::NotHandled;};
    if(!dispatchEnabled_ || !PresentationReady())return failed();
    const auto* d=ui::g_gdi.Dc(dc);const auto* w=d?Win(d->hwnd):nullptr;
    if(!GpuActive() || !w || (w->parent && w->parent!=S().desktop))return refused();
    Lease lease;
    if(!Acquire(dc,lease))return PresentationReady()?refused():failed();
    auto* slot=FindSlot(lease);
    // Seed allocation/refusal precedes any admitted raster operation. Recovery
    // during import also goes through the genuine CPU path, with no double draw.
    if(!slot || !Seed(*slot,lease))return PresentationReady()?refused():failed();
    if(!GpuActive())return refused();
    if(!Fill(lease,r,rgb))return failed();
    ++dispatchStats_.accepted;return ui::RasterDispatch::Accepted;
}
void WindowGpuBindings::RevokeGate() {
    if(gate_)gate_->owner=nullptr;
    gate_.reset();coherent_=false;dispatchEnabled_=false;exactDamage_=false;
}
bool WindowGpuBindings::Fail(Fault reason) {
    fault_=true;if(faultReason_==Fault::None)faultReason_=reason;
    if(SameSession())S().presentationFault=true;
    return false;
}
const char* WindowGpuBindings::FaultName() const {
    switch(faultReason_) {
    case Fault::None:return "none";
    case Fault::DirtyImport:return "dirty import";
    case Fault::WindowCommit:return "window CPU commit";
    case Fault::WindowDescription:return "window frame metadata";
    case Fault::Detach:return "coherence detach";
    case Fault::ReentrantCpu:return "reentrant CPU access";
    case Fault::Dispatch:return "solid dispatch";
    }
    return "unknown";
}
bool WindowGpuBindings::DisableCoherence() {
    if(!coherent_)return true;
    if(!SameSession())return false; // a suspended owner must not be revoked by an inner session
    const bool checkpoint=CheckpointCpu();
    const bool removed=!dispatchEnabled_ || ui::g_gdi.RemoveSolidBackend(gate_);
    const bool ok=checkpoint && removed && ui::g_gdi.RemoveCpuGate(gate_);
    RevokeGate();
    return ok?true:Fail(Fault::Detach);
}
WindowGpuBindings::Slot* WindowGpuBindings::FindSlot(const Lease& lease) {
    for(auto& s:slots_)if(s.target==lease.target && s.identity==lease.raster.windowIdentity)return &s;
    return nullptr;
}
bool WindowGpuBindings::EnableDifferentialImport(bool enabled) {
    if(!PresentationReady())return false;
    targets_.EnableDifferentialImport(enabled);return true;
}
bool WindowGpuBindings::EnableCapacityReplay(bool enabled) {
    if(!PresentationReady())return false;
    targets_.EnableCapacityReplay(enabled);return true;
}
bool WindowGpuBindings::EnableExactDamage(bool enabled) {
    if(!PresentationReady())return false;
    exactDamage_=enabled;return true;
}
size_t WindowGpuBindings::DamageMaskBytes() const {
    size_t bytes=0;for(const auto& s:slots_)bytes+=s.damage.size();return bytes;
}
void WindowGpuBindings::MarkDamage(Slot& s,ui::Rect r) {
    // Bits describe accepted writes only, never their bounding rectangle.
    // Row spans are at most512 pixels; full bytes avoid a CPU raster per pixel.
    for(int y=r.top;y<r.bottom;++y) {
        size_t first=size_t(y)*s.width+r.left,last=size_t(y)*s.width+r.right;
        while(first<last && first%8){s.damage[first/8]|=uint8_t(1u<<(first%8));++first;}
        const auto end=last/8*8;
        if(first<end){std::fill(s.damage.data()+first/8,s.damage.data()+end/8,uint8_t(0xff));first=end;}
        while(first<last){s.damage[first/8]|=uint8_t(1u<<(first%8));++first;}
    }
    s.maskWrites=true;
}
bool WindowGpuBindings::PromoteDamage(Slot& s) {
    const size_t bytes=(size_t(s.width)*s.height+7)/8,live=DamageMaskBytes();
    if(live>MaxDamageBytes || bytes>MaxDamageBytes-live+s.damage.size() || !s.damage.Resize(bytes))return false;
    coherenceStats_.maskPeakBytes=std::max(coherenceStats_.maskPeakBytes,uint64_t(DamageMaskBytes()));
    std::fill(s.damage.data(),s.damage.data()+s.damage.size(),0);
    s.maskWrites=false;
    for(const auto& rect:s.dirty.Rects())MarkDamage(s,rect);
    s.dirty.Clear();s.masked=true;++coherenceStats_.maskPromotions;return true;
}
bool WindowGpuBindings::Seed(Slot& s,const Lease& lease) {
    if(!s.import)return true;
    if(Dirty(s))return Fail(Fault::DirtyImport);
    const size_t pixels=size_t(s.width)*s.height;
    using Clock=std::chrono::steady_clock;
    const auto start=Clock::now();++coherenceStats_.seedAttempts;
    const bool snapshot=s.seed.Resize(pixels) && ui::g_gdi.SnapshotWindow(lease.raster,s.seed.data(),pixels);
    const auto captured=Clock::now();
    coherenceStats_.seedSnapshotNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(captured-start).count();
    if(!snapshot)return false;
    const auto before=*targets_.TargetStats(s.target);
    const bool imported=targets_.ImportCpu(s.target,s.seed.data(),pixels);
    coherenceStats_.seedImportNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-captured).count();
    const auto& after=*targets_.TargetStats(s.target);
    coherenceStats_.seedUploadBytes+=after.uploadBytes-before.uploadBytes;
    coherenceStats_.seedScanPixels+=after.importScanPixels-before.importScanPixels;
    coherenceStats_.seedUnchanged+=after.importUnchanged-before.importUnchanged;
    if(!imported)return false;
    s.import=false;++coherenceStats_.imports;coherenceStats_.importPixels+=pixels;return true;
}
bool WindowGpuBindings::CheckpointCpu() {
    if(!coherent_)return false;
    if(!SameSession())return false;
    if(fault_)return false;
    for(auto& s:slots_)if(s.target && Dirty(s)) {
        const auto* image=targets_.CpuRead(s.target);size_t pixels=0;
        if(!image || fault_ || ui::g_gdi.CpuAccessFault())return Fail(Fault::WindowCommit);
        const bool committed=s.masked?ui::g_gdi.CommitWindowMask(s.stamp,*image,s.damage.data(),s.damage.size(),pixels):ui::g_gdi.CommitWindow(s.stamp,*image,s.dirty,pixels);
        if(!committed)return Fail(Fault::WindowCommit);
        if(s.masked){++coherenceStats_.maskCommits;coherenceStats_.maskPixels+=pixels;}
        s.dirty.Clear();s.masked=s.maskWrites=false;ownershipRevision_=ui::NewPresentationResourceId();++coherenceStats_.commits;coherenceStats_.commitPixels+=pixels;
    }
    return true;
}
bool WindowGpuBindings::BeforeCpu() {
    if(barrier_)return Fail(Fault::ReentrantCpu);
    barrier_=true;++coherenceStats_.cpuBarriers;
    const bool ok=CheckpointCpu();
    if(ok)for(auto& s:slots_)s.import=true; // conservative CPU reads/writes handoff
    barrier_=false;return ok;
}
const ui::Surface* WindowGpuBindings::CpuRead(const Lease& lease) {
    return Valid(lease)?targets_.CpuRead(lease.target):nullptr;
}
const ui::GpuGdiPrototype::Stats* WindowGpuBindings::Counters(const Lease& lease) {
    return Valid(lease)?targets_.TargetStats(lease.target):nullptr;
}
bool WindowGpuBindings::PresentationReady() const {
    return coherent_ && !fault_ && SameSession() && !ui::g_gdi.CpuAccessFault() && !S().presentationFault;
}
bool WindowGpuBindings::SubmitMixed(ui::MixedPresentation& output,const ui::WindowSceneView* overlay) {
    if(!PresentationReady())return false;
    const auto& screen=S().screen.s;
    if(!screen.Valid() || screen.bpp!=32 || screen.width>ui::GpuGdiTargets::MaxOutputWidth ||
       screen.height>ui::GpuGdiTargets::MaxOutputHeight)return false;
    if(!frameOwners_.Resize(size_t(screen.width)*screen.height))return false;
    std::fill(frameOwners_.data(),frameOwners_.data()+frameOwners_.size(),0);
    std::array<ui::GpuGdiTargets::Layer,ui::GpuGdiTargets::MaxLayers> resources{};
    std::array<ui::PresentationLayerFrame,ui::WindowLayers::MaxLayers> layers{};
    std::array<uint32_t,ui::GpuGdiTargets::MaxLayers> tokens{};
    size_t count=0;
    for(const auto& slot:slots_)if(slot.target && Dirty(slot)) {
        ui::WindowDcInfo image;
        if(slot.import || !ui::g_gdi.DescribeWindowImage(slot.stamp,image))return Fail(Fault::WindowDescription);
        const auto& r=slot.stamp.frame;
        const uint32_t token=uint32_t(count+1);tokens[count]=token;
        resources[count]={slot.target,{0,0,slot.width,slot.height}};
        layers[count]={nullptr,token,r.left,r.top,0,0,nullptr};
        // Seed owns every currently visible pixel, not only the dirty DC clip.
        // Occluded pixels were never seeded and must remain CPU/other owners.
        for(const auto& rect:image.visible.Rects())for(int y=rect.top;y<rect.bottom;++y)
            for(int x=rect.left;x<rect.right;++x) {
                auto& owner=frameOwners_[size_t(y)*screen.width+x];
                if(owner)return false; // overlapping DC visibility is unsupported here
                owner=token;
            }
        ++count;
    }
    size_t layerCount=count;
    if(overlay) {
        if(!overlay->owners || overlay->width!=screen.width || overlay->height!=screen.height ||
           overlay->count>layers.size()-count || (overlay->count && !overlay->layers))return false;
        for(size_t n=0;n<overlay->count;++n) {
            const auto& src=overlay->layers[n];
            if(src.token<=ui::GpuGdiTargets::MaxLayers)return false;
            layers[layerCount++]={src.surface,src.token,src.left,src.top,src.generation,0,src.versions};
        }
        for(size_t n=0;n<frameOwners_.size();++n)if(overlay->owners[n])frameOwners_[n]=overlay->owners[n];
    }
    ui::GpuGdiTargets::Frame frame;
    if(!targets_.PrepareFrame(resources.data(),count,screen.width,screen.height,frame))return false;
    ui::PresentationSceneFrame scene{frameOwners_.data(),screen.width,screen.height,0,0,nullptr,layers.data(),layerCount};
    return output.Submit(screen,scene,targets_,frame,tokens.data(),count);
}
bool WindowGpuBindings::RenderMixed(ui::MixedPresentation& frame,ui::OpenGlPresentation& renderer,
    const ui::OutputBuffer& out,ui::PresentationCursor cursor) {
    return PresentationReady() && targets_.GpuActive() && frame.Render(targets_,renderer,out,cursor);
}
bool WindowGpuBindings::RenderMixedCpu(ui::MixedPresentation& frame,ui::SoftwarePresentation& renderer,
    const ui::OutputBuffer& out,ui::PresentationCursor cursor) {
    return PresentationReady() && frame.RenderCpu(targets_,renderer,out,cursor);
}
} // namespace win32::user
