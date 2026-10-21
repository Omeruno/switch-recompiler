// gdiplus_module.cpp — gdiplus.dll: кодеков нет. Списки кодировщиков и
// декодеров пусты, поэтому программы работают только со своими форматами
// (Paint сам читает и пишет BMP); загрузка и сохранение через GDI+
// отвечают NotImplemented. GdiplusStartup/Shutdown — в host_functions.cpp.

#include "hle_util.h"
#include "../runtime/heap.h"

namespace win32 {
namespace {

using namespace hle;

constexpr uint32_t Ok = 0, InvalidParameter = 2, NotImplemented = 6;

#define FN(name) HostCallOutcome name(cpu::CpuState& c, cpu::IGuestMemory& m)

// GpStatus GdipGetImageEncodersSize/DecodersSize(UINT* num, UINT* size)
FN(CodecsSize) {
    if (!Arg(c, m, 0) || !Arg(c, m, 1)) return Ret(InvalidParameter);
    m.Write32(Arg(c, m, 0), 0);
    m.Write32(Arg(c, m, 1), 0);
    return Ret(Ok);
}
// GpStatus GdipGetImageEncoders/Decoders(UINT num, UINT size, ImageCodecInfo*)
FN(Codecs) { return Ret(Arg(c, m, 0) == 0 ? Ok : InvalidParameter); }
FN(GdipAlloc) { return Ret(rt::ProcessHeap().Alloc(Arg(c, m, 0) ? Arg(c, m, 0) : 1)); }
FN(GdipFree) {
    if (Arg(c, m, 0)) rt::ProcessHeap().Free(Arg(c, m, 0));
    return Ret(0);
}
// Создание картинки: выходной указатель обнуляется
template <int Out>
HostCallOutcome NoImage(cpu::CpuState& c, cpu::IGuestMemory& m) {
    if (Arg(c, m, Out)) m.Write32(Arg(c, m, Out), 0);
    return Ret(NotImplemented);
}
template <uint32_t R>
HostCallOutcome Fixed(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(R); }

#define GP(name, bytes, fn) {"gdiplus.dll", name, bytes, CallConv::Stdcall, fn}
const HostFunctionEntry kEntries[] = {
    GP("GdipGetImageEncodersSize", 8, &CodecsSize), GP("GdipGetImageDecodersSize", 8, &CodecsSize),
    GP("GdipGetImageEncoders", 12, &Codecs), GP("GdipGetImageDecoders", 12, &Codecs),
    GP("GdipAlloc", 4, &GdipAlloc), GP("GdipFree", 4, &GdipFree),
    GP("GdipCreateBitmapFromFile", 8, &NoImage<1>), GP("GdipCreateBitmapFromFileICM", 8, &NoImage<1>),
    GP("GdipCreateBitmapFromHBITMAP", 12, &NoImage<2>), GP("GdipCloneImage", 8, &NoImage<1>),
    GP("GdipDisposeImage", 4, &Fixed<Ok>), GP("GdipSaveImageToFile", 16, &Fixed<NotImplemented>),
    GP("GdipSaveImageToStream", 16, &Fixed<NotImplemented>), GP("GdipGetImageRawFormat", 8, &Fixed<NotImplemented>),
    GP("GdipGetPropertySize", 12, &Fixed<NotImplemented>), GP("GdipGetAllPropertyItems", 16, &Fixed<NotImplemented>),
    GP("GdipSetPropertyItem", 8, &Fixed<NotImplemented>),
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace
} // namespace win32
