// shell32_module.cpp — SHELL32.dll: перетаскивание файлов из проводника
// (на Switch его нет — список всегда пуст) и уведомления оболочки.

#include "hle_util.h"

namespace win32 {
namespace {

using namespace hle;

#define FN(name) HostCallOutcome name(cpu::CpuState& c, cpu::IGuestMemory& m)

FN(DragAcceptFiles) { (void)c; (void)m; return Ret(0); }
FN(DragFinish) { (void)c; (void)m; return Ret(0); }
// UINT DragQueryFile(hDrop, index, buf, cch): файлов нет
FN(DragQueryFileW) {
    if (Arg(c, m, 2) && Arg(c, m, 3)) m.Write16(Arg(c, m, 2), 0);
    return Ret(0);
}
FN(DragQueryFileA) {
    if (Arg(c, m, 2) && Arg(c, m, 3)) m.Write8(Arg(c, m, 2), 0);
    return Ret(0);
}
FN(DragQueryPoint) {
    if (Arg(c, m, 1)) { m.Write32(Arg(c, m, 1), 0); m.Write32(Arg(c, m, 1) + 4, 0); }
    return Ret(0);
}
FN(SHChangeNotify) { (void)c; (void)m; return Ret(0); }

#define E(name, bytes) {"SHELL32.dll", #name, bytes, CallConv::Stdcall, &name}
const HostFunctionEntry kEntries[] = {
    E(DragAcceptFiles, 8), E(DragFinish, 4), E(DragQueryFileW, 16), E(DragQueryFileA, 16), E(DragQueryPoint, 8),
    E(SHChangeNotify, 16),
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace
} // namespace win32
