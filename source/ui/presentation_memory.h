#pragma once
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>
#include <type_traits>

namespace ui {
// Borrowed allocator, restricted to renderer-owned trivial buffers. Failure is
// returned explicitly even in the native -fno-exceptions build.
struct PresentationAllocator {
    virtual ~PresentationAllocator()=default;
    virtual void* Allocate(size_t bytes)=0;
    virtual void Free(void* pointer)=0;
};
template<class T> class PresentationBuffer {
    static_assert(std::is_trivially_copyable<T>::value,"trivial renderer storage only");
public:
    PresentationBuffer(PresentationAllocator* allocator=nullptr):allocator_(allocator) {}
    ~PresentationBuffer() {Clear();}
    PresentationBuffer(const PresentationBuffer&)=delete;
    PresentationBuffer& operator=(const PresentationBuffer&)=delete;
    PresentationBuffer(PresentationBuffer&& other) noexcept {Swap(other);}
    PresentationBuffer& operator=(PresentationBuffer&& other) noexcept {Swap(other);return *this;}
    // Exact capacity; failed resize leaves the previous buffer untouched.
    bool Resize(size_t count) {
        if(count==size_)return true;
        if(!count) {Clear();return true;}
        if(count>std::numeric_limits<size_t>::max()/sizeof(T))return false;
        auto* next=static_cast<T*>((allocator_?allocator_->Allocate(count*sizeof(T)):std::malloc(count*sizeof(T))));
        if(!next)return false;
        Clear();data_=next;size_=count;return true;
    }
    void Clear() {if(data_) {if(allocator_)allocator_->Free(data_);else std::free(data_);}data_=nullptr;size_=0;}
    void Swap(PresentationBuffer& other) noexcept {
        std::swap(data_,other.data_);std::swap(size_,other.size_);std::swap(allocator_,other.allocator_);
    }
    T* data() {return data_;}
    const T* data() const {return data_;}
    size_t size() const {return size_;}
    size_t capacity() const {return size_;}
    T& operator[](size_t index) {return data_[index];}
    const T& operator[](size_t index) const {return data_[index];}
private:
    T* data_=nullptr;
    size_t size_=0;
    PresentationAllocator* allocator_=nullptr;
};
} // namespace ui
