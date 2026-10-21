// ole32_module.cpp — ole32.dll: минимум COM/OLE для программ на MFC.
//
// Объектов COM нет: инициализация проходит, память CoTaskMem — из кучи
// процесса, GUID переводится в строку, а создание объектов, хранилищ,
// моникеров и drag-and-drop честно отвечает кодом ошибки (выходной
// указатель обнуляется), чтобы MFC шла по пути «OLE недоступна».

#include <cstdio>

#include "hle_util.h"
#include "../runtime/heap.h"

namespace win32 {
namespace {

using namespace hle;

#define FN(name) HostCallOutcome name(cpu::CpuState& c, cpu::IGuestMemory& m)

constexpr uint32_t S_OK = 0, S_FALSE = 1, E_NOTIMPL = 0x80004001u, E_FAIL = 0x80004005u;
constexpr uint32_t REGDB_E_CLASSNOTREG = 0x80040154u, CO_E_CLASSSTRING = 0x800401F3u, MK_E_CANTOPENFILE = 0x800401EAu;
constexpr uint32_t STG_E_FILENOTFOUND = 0x80030002u;

// Ответ-код с обнулением выходного указателя (аргумент Out; -1 — нет)
template <uint32_t R, int Out>
HostCallOutcome Fixed(cpu::CpuState& c, cpu::IGuestMemory& m) {
    if (Out >= 0) {
        uint32_t p = Arg(c, m, Out);
        if (p) m.Write32(p, 0);
    }
    return Ret(R);
}

FN(CoTaskMemAlloc) { return Ret(rt::ProcessHeap().Alloc(Arg(c, m, 0) ? Arg(c, m, 0) : 1)); }
FN(CoTaskMemFree) {
    if (Arg(c, m, 0)) rt::ProcessHeap().Free(Arg(c, m, 0));
    return Ret(0);
}
FN(CoTaskMemRealloc) {
    uint32_t p = Arg(c, m, 0), n = Arg(c, m, 1);
    if (!p) return Ret(rt::ProcessHeap().Alloc(n ? n : 1));
    if (!n) { rt::ProcessHeap().Free(p); return Ret(0); }
    return Ret(rt::ProcessHeap().Realloc(p, n));
}

std::u16string GuidText(cpu::IGuestMemory& m, uint32_t g) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", m.Read32(g), m.Read16(g + 4),
                  m.Read16(g + 6), m.Read8(g + 8), m.Read8(g + 9), m.Read8(g + 10), m.Read8(g + 11), m.Read8(g + 12),
                  m.Read8(g + 13), m.Read8(g + 14), m.Read8(g + 15));
    return AnsiToWide(buf);
}
// int StringFromGUID2(REFGUID, LPOLESTR, int cch): символов с нулём, 0 — мал буфер
FN(StringFromGUID2) {
    if (static_cast<int32_t>(Arg(c, m, 2)) < 39) return Ret(0);
    WriteWide(m, Arg(c, m, 1), GuidText(m, Arg(c, m, 0)), 39);
    return Ret(39);
}
// HRESULT StringFromCLSID(REFCLSID, LPOLESTR*): строка в CoTaskMem
FN(StringFromCLSID) {
    uint32_t p = rt::ProcessHeap().Alloc(39 * 2);
    if (!p) return Ret(0x8007000Eu); // E_OUTOFMEMORY
    WriteWide(m, p, GuidText(m, Arg(c, m, 0)), 39);
    m.Write32(Arg(c, m, 1), p);
    return Ret(S_OK);
}
FN(PropVariantClear) {
    uint32_t p = Arg(c, m, 0);
    if (p) for (uint32_t i = 0; i < 16; i += 4) m.Write32(p + i, 0);
    return Ret(S_OK);
}
// HRESULT CoRegisterClassObject(clsid, unk, ctx, flags, LPDWORD cookie)
FN(CoRegisterClassObject) {
    static uint32_t cookie = 0x100;
    if (Arg(c, m, 4)) m.Write32(Arg(c, m, 4), ++cookie);
    return Ret(S_OK);
}

#define OLE(name, bytes, fn) {"ole32.dll", name, bytes, CallConv::Stdcall, fn}
#define OK(name, bytes) OLE(name, bytes, (&Fixed<S_OK, -1>))
const HostFunctionEntry kEntries[] = {
    OK("OleInitialize", 4), OK("OleUninitialize", 0), OK("CoInitialize", 4), OK("CoInitializeEx", 8),
    OK("CoUninitialize", 0), OK("CoFreeUnusedLibraries", 0),
    OLE("CoTaskMemAlloc", 4, &CoTaskMemAlloc), OLE("CoTaskMemFree", 4, &CoTaskMemFree),
    OLE("CoTaskMemRealloc", 8, &CoTaskMemRealloc), OLE("CoGetMalloc", 8, (&Fixed<E_NOTIMPL, 1>)),
    OLE("StringFromGUID2", 12, &StringFromGUID2), OLE("StringFromCLSID", 8, &StringFromCLSID),
    OLE("CLSIDFromProgID", 8, (&Fixed<CO_E_CLASSSTRING, -1>)), OLE("CoTreatAsClass", 8, (&Fixed<E_NOTIMPL, -1>)),
    OLE("CoGetClassObject", 20, (&Fixed<REGDB_E_CLASSNOTREG, 4>)),
    OLE("CoRegisterClassObject", 20, &CoRegisterClassObject), OK("CoRevokeClassObject", 4),
    OLE("CoRegisterMessageFilter", 8, (&Fixed<S_OK, 1>)), OK("CoLockObjectExternal", 12), OK("CoDisconnectObject", 8),
    OLE("CoMarshalInterThreadInterfaceInStream", 12, (&Fixed<E_NOTIMPL, 2>)),
    OLE("CoGetInterfaceAndReleaseStream", 12, (&Fixed<E_NOTIMPL, 2>)),
    OLE("PropVariantClear", 4, &PropVariantClear), OK("ReleaseStgMedium", 4),
    // моникеры, таблица запущенных объектов, кэши
    OLE("CreateBindCtx", 8, (&Fixed<E_NOTIMPL, 1>)), OLE("CreateFileMoniker", 8, (&Fixed<E_NOTIMPL, 1>)),
    OLE("CreateItemMoniker", 12, (&Fixed<E_NOTIMPL, 2>)), OLE("CreateGenericComposite", 12, (&Fixed<E_NOTIMPL, 2>)),
    OLE("GetRunningObjectTable", 8, (&Fixed<E_NOTIMPL, 1>)), OLE("GetClassFile", 8, (&Fixed<MK_E_CANTOPENFILE, -1>)),
    OLE("CreateDataAdviseHolder", 4, (&Fixed<E_NOTIMPL, 0>)), OLE("CreateOleAdviseHolder", 4, (&Fixed<E_NOTIMPL, 0>)),
    OLE("CreateDataCache", 16, (&Fixed<E_NOTIMPL, 3>)),
    // потоки и хранилища
    OLE("CreateStreamOnHGlobal", 12, (&Fixed<E_NOTIMPL, 2>)), OLE("CreateILockBytesOnHGlobal", 12, (&Fixed<E_NOTIMPL, 2>)),
    OLE("GetHGlobalFromILockBytes", 8, (&Fixed<E_NOTIMPL, 1>)),
    OLE("StgCreateDocfile", 16, (&Fixed<E_NOTIMPL, 3>)), OLE("StgCreateDocfileOnILockBytes", 16, (&Fixed<E_NOTIMPL, 3>)),
    OLE("StgOpenStorage", 24, (&Fixed<STG_E_FILENOTFOUND, 5>)), OLE("StgOpenStorageOnILockBytes", 24, (&Fixed<E_NOTIMPL, 5>)),
    OLE("StgIsStorageFile", 4, (&Fixed<S_FALSE, -1>)),
    OLE("ReadClassStg", 8, (&Fixed<E_NOTIMPL, -1>)), OLE("WriteClassStg", 8, (&Fixed<E_NOTIMPL, -1>)),
    OLE("ReadClassStm", 8, (&Fixed<E_NOTIMPL, -1>)), OLE("WriteClassStm", 8, (&Fixed<E_NOTIMPL, -1>)),
    OLE("ReadFmtUserTypeStg", 12, (&Fixed<E_NOTIMPL, 1>)), OLE("WriteFmtUserTypeStg", 12, (&Fixed<E_NOTIMPL, -1>)),
    OLE("SetConvertStg", 8, (&Fixed<E_NOTIMPL, -1>)),
    // объекты OLE
    OLE("OleCreate", 28, (&Fixed<E_NOTIMPL, 6>)), OLE("OleCreateFromData", 28, (&Fixed<E_NOTIMPL, 6>)),
    OLE("OleCreateFromFile", 32, (&Fixed<E_NOTIMPL, 7>)), OLE("OleCreateLinkFromData", 28, (&Fixed<E_NOTIMPL, 6>)),
    OLE("OleCreateLinkToFile", 28, (&Fixed<E_NOTIMPL, 6>)), OLE("OleCreateStaticFromData", 28, (&Fixed<E_NOTIMPL, 6>)),
    OLE("OleLoad", 16, (&Fixed<E_NOTIMPL, 3>)), OLE("OleLoadFromStream", 12, (&Fixed<E_NOTIMPL, 2>)),
    OLE("OleSave", 12, (&Fixed<E_NOTIMPL, -1>)), OLE("OleSaveToStream", 8, (&Fixed<E_NOTIMPL, -1>)),
    OLE("OleQueryCreateFromData", 4, (&Fixed<S_FALSE, -1>)), OLE("OleQueryLinkFromData", 4, (&Fixed<S_FALSE, -1>)),
    OK("OleRun", 4), OLE("OleIsRunning", 4, (&Fixed<0, -1>)), OK("OleLockRunning", 12), OK("OleSetContainedObject", 8),
    OLE("OleDuplicateData", 12, (&Fixed<0, -1>)), OLE("OleGetIconOfClass", 12, (&Fixed<0, -1>)),
    OLE("OleRegEnumVerbs", 8, (&Fixed<REGDB_E_CLASSNOTREG, 1>)), OLE("OleRegGetMiscStatus", 12, (&Fixed<REGDB_E_CLASSNOTREG, 2>)),
    OLE("OleRegGetUserType", 12, (&Fixed<REGDB_E_CLASSNOTREG, 2>)),
    // меню и ускорители встроенных объектов
    OLE("OleCreateMenuDescriptor", 8, (&Fixed<0, -1>)), OK("OleDestroyMenuDescriptor", 4), OK("OleSetMenuDescriptor", 20),
    OLE("IsAccelerator", 16, (&Fixed<0, -1>)), OLE("OleTranslateAccelerator", 12, (&Fixed<S_FALSE, -1>)),
    // буфер обмена и перетаскивание OLE
    OLE("OleGetClipboard", 4, (&Fixed<E_FAIL, 0>)), OK("OleSetClipboard", 4), OK("OleFlushClipboard", 0),
    OLE("OleIsCurrentClipboard", 4, (&Fixed<S_FALSE, -1>)),
    OK("RegisterDragDrop", 8), OK("RevokeDragDrop", 4), OLE("DoDragDrop", 16, (&Fixed<E_NOTIMPL, 3>)),
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace
} // namespace win32
