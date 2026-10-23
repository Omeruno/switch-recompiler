#include "presentation_layers.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace ui {
namespace {
constexpr int Tile=32;
bool ValidLayer(const Surface& s) {
    return s.bits && s.width>0 && s.height>0 && s.width<=8192 && s.height<=8192 &&
        s.bpp==32 && s.pitch>=int64_t(s.width)*4 && s.pitch%4==0;
}
}
void PresentationLayers::Clear() {
    for(auto& slot:slots_)slot=Slot{};
    owners_.Clear();versions_.Clear();sourceVersions_.Clear();sourceOwnerVersions_.Clear();sourceOwnerGeneration_=0;frame_={};serial_=0;++generation_;
}
size_t PresentationLayers::Bytes() const {
    size_t bytes=owners_.size()*4+versions_.size()*8+sourceVersions_.size()*8+sourceOwnerVersions_.size()*8;
    for(const auto& slot:slots_)bytes+=slot.pixels.size()*4+slot.versions.size()*8+slot.sourceVersions.size()*8;
    return bytes;
}
bool PresentationLayers::CopyLayer(Slot& slot,const WindowLayerView& layer,bool tracked) {
    const auto& source=*layer.surface;
    const int cols=(source.width+Tile-1)/Tile,rows=(source.height+Tile-1)/Tile;
    bool fresh=slot.token!=layer.token || slot.generation!=layer.generation ||
        slot.surface.width!=source.width || slot.surface.height!=source.height;
    if(fresh) {
        PresentationBuffer<uint32_t> pixels(allocator_);PresentationBuffer<uint64_t> versions(allocator_);
        if(!pixels.Resize(size_t(source.width)*source.height) || !versions.Resize(size_t(cols)*rows))return false;
        slot.pixels.Swap(pixels);slot.versions.Swap(versions);
        std::memset(slot.versions.data(),0,slot.versions.size()*8);
        slot.surface={};slot.surface.width=source.width;slot.surface.height=source.height;slot.surface.pitch=source.width*4;
        slot.surface.bits=reinterpret_cast<uint8_t*>(slot.pixels.data());
        slot.token=layer.token;slot.generation=layer.generation;slot.serial=0;
    }
    if(slot.serial==std::numeric_limits<uint64_t>::max()){slot.serial=0;fresh=true;++slot.generation;}
    const uint64_t revision=slot.serial+1;bool any=false;
    const bool cached=!fresh && tracked && layer.versions && layer.versions->size()==size_t(cols)*rows &&
        slot.sourceVersions.size()==layer.versions->size();
    for(int ty=0;ty<rows;++ty)for(int tx=0;tx<cols;++tx) {
        const size_t tile=size_t(ty)*cols+tx;
        if(cached && slot.sourceVersions[tile]==(*layer.versions)[tile])continue;
        bool changed=fresh;const int first=tx*Tile,last=std::min(first+Tile,source.width);
        for(int y=ty*Tile;y<std::min((ty+1)*Tile,source.height);++y) {
            auto* dst=slot.surface.Row(y)+size_t(first)*4;const auto* src=source.Row(y)+size_t(first)*4;
            const size_t bytes=size_t(last-first)*4;
            comparedBytes_+=bytes;
            if(fresh || std::memcmp(dst,src,bytes)!=0){std::memcpy(dst,src,bytes);changed=true;}
        }
        if(changed){slot.versions[size_t(ty)*cols+tx]=revision;any=true;}
    }
    if(any)slot.serial=revision;
    if(tracked && layer.versions && layer.versions->size()==size_t(cols)*rows && slot.sourceVersions.Resize(layer.versions->size()))
        std::memcpy(slot.sourceVersions.data(),layer.versions->data(),layer.versions->size()*8);
    else slot.sourceVersions.Clear();
    return true;
}
bool PresentationLayers::Submit(const Surface& canonical,const WindowSceneView& scene,const PresentationBuffer<uint64_t>* sourceVersions) {
    auto fail=[&](){Clear();return false;};
    if(!ValidLayer(canonical) || uint64_t(canonical.width)*canonical.height*4>WindowLayers::MaxPixelBytes ||
       !scene.owners || scene.width!=canonical.width || scene.height!=canonical.height ||
       scene.count>WindowLayers::MaxLayers || (scene.count && !scene.layers))return fail();
    size_t total=0;
    for(size_t n=0;n<scene.count;++n) {
        const auto& layer=scene.layers[n];if(!layer.token || !layer.surface || !ValidLayer(*layer.surface) ||
            layer.left < -16384 || layer.top < -16384 || layer.left>16384 || layer.top>16384)return fail();
        const uint64_t bytes=uint64_t(layer.surface->width)*layer.surface->height*4;
        if(bytes>WindowLayers::MaxPixelBytes || total>WindowLayers::MaxPixelBytes-bytes)return fail();
        total+=size_t(bytes);
        for(size_t i=0;i<n;++i)if(scene.layers[i].token==layer.token)return fail();
    }
    const int cols=(scene.width+Tile-1)/Tile,rows=(scene.height+Tile-1)/Tile;
    const bool fresh=frame_.width!=scene.width || frame_.height!=scene.height || !frame_.owners;
    if(fresh) {
        PresentationBuffer<uint32_t> owners(allocator_);PresentationBuffer<uint64_t> versions(allocator_);
        if(!owners.Resize(size_t(scene.width)*scene.height) || !versions.Resize(size_t(cols)*rows))return fail();
        owners_.Swap(owners);versions_.Swap(versions);std::memset(versions_.data(),0,versions_.size()*8);
        ++generation_;serial_=0;
    }
    for(size_t n=scene.count;n<slots_.size();++n)slots_[n]=Slot{};
    for(size_t n=0;n<scene.count;++n) {
        if(!CopyLayer(slots_[n],scene.layers[n],scene.writeThrough))return fail();
        const auto& input=scene.layers[n];const auto& slot=slots_[n];
        views_[n]={&slot.surface,input.token,input.left,input.top,input.generation,slot.serial,&slot.versions};
    }
    // Fixed hash table avoids per-pixel linear searches and frame allocations.
    struct Entry {uint32_t token=0;size_t index=0;};std::array<Entry,128> lookup{};
    auto bucket=[](uint32_t token){return size_t((token*2654435761u)>>25);};
    for(size_t n=0;n<scene.count;++n) {
        auto i=bucket(views_[n].token);while(lookup[i].token)i=(i+1)&127;
        lookup[i]={views_[n].token,n};
    }
    if(serial_==std::numeric_limits<uint64_t>::max()){serial_=0;++generation_;std::fill(versions_.data(),versions_.data()+versions_.size(),0);}
    const uint64_t revision=serial_+1;bool any=false;
    const bool cached=!fresh && scene.writeThrough && sourceVersions && sourceVersions->size()==size_t(cols)*rows &&
        sourceVersions_.size()==sourceVersions->size();
    const bool ownerTracked=scene.ownershipVersions && scene.ownershipVersions->size()==size_t(cols)*rows;
    const bool ownerCached=ownerTracked && sourceOwnerVersions_.size()==scene.ownershipVersions->size() &&
        sourceOwnerGeneration_==scene.ownershipGeneration;
    for(int ty=0;ty<rows;++ty)for(int tx=0;tx<cols;++tx) {
        const size_t tile=size_t(ty)*cols+tx;
        // Write-through producers cannot change a visible window pixel without
        // changing canonical RGB or ownership. Both checks are row memcmp/epochs;
        // unchanged tiles avoid per-pixel reconciliation/hash lookups entirely.
        if(cached && sourceVersions_[tile]==(*sourceVersions)[tile]) {
            bool same=ownerCached && sourceOwnerVersions_[tile]==(*scene.ownershipVersions)[tile];
            for(int y=ty*Tile;!ownerTracked && y<std::min((ty+1)*Tile,scene.height);++y) {
                if(y==ty*Tile)same=true;
                const size_t first=size_t(y)*scene.width+tx*Tile,bytes=size_t(std::min(Tile,scene.width-tx*Tile))*4;
                if(std::memcmp(owners_.data()+first,scene.owners+first,bytes)!=0){same=false;break;}
            }
            if(same)continue;
        }
        bool changed=fresh;
        for(int y=ty*Tile;y<std::min((ty+1)*Tile,scene.height);++y)for(int x=tx*Tile;x<std::min((tx+1)*Tile,scene.width);++x) {
            ++checkedPixels_;
            const size_t pixel=size_t(y)*scene.width+x;uint32_t token=scene.owners[pixel];
            if(token) {
                auto i=bucket(token);while(lookup[i].token && lookup[i].token!=token)i=(i+1)&127;
                bool valid=false;
                if(lookup[i].token) {
                    const auto& layer=views_[lookup[i].index];
                    const int64_t lx=int64_t(x)-layer.left,ly=int64_t(y)-layer.top;
                    if(lx>=0 && ly>=0 && lx<layer.surface->width && ly<layer.surface->height) {
                        uint32_t rgb;std::memcpy(&rgb,canonical.Row(y)+size_t(x)*4,4);
                        valid=layer.surface->Get(int(lx),int(ly))==(rgb&0xFFFFFFu);
                    }
                }
                if(!valid){token=0;++reconciled_;}
            }
            if(fresh || owners_[pixel]!=token){owners_[pixel]=token;changed=true;}
        }
        if(changed){versions_[size_t(ty)*cols+tx]=revision;any=true;}
    }
    if(any)serial_=revision;
    if(sourceVersions && scene.writeThrough && sourceVersions->size()==size_t(cols)*rows && sourceVersions_.Resize(sourceVersions->size()))
        std::memcpy(sourceVersions_.data(),sourceVersions->data(),sourceVersions->size()*8);
    else sourceVersions_.Clear();
    if(scene.writeThrough && ownerTracked && sourceOwnerVersions_.Resize(scene.ownershipVersions->size())) {
        std::memcpy(sourceOwnerVersions_.data(),scene.ownershipVersions->data(),scene.ownershipVersions->size()*8);
        sourceOwnerGeneration_=scene.ownershipGeneration;
    } else {sourceOwnerVersions_.Clear();sourceOwnerGeneration_=0;}
    frame_={owners_.data(),scene.width,scene.height,generation_,serial_,&versions_,views_.data(),scene.count};
    return true;
}
} // namespace ui
