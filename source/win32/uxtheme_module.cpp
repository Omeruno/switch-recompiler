// uxtheme_module.cpp — UxTheme.dll: темы оформления выключены (как в
// классической теме XP). OpenThemeData возвращает NULL, и программы
// рисуют элементы сами; остальное отвечает «нет темы».

#include "hle_util.h"

namespace win32 {
namespace {

using namespace hle;

constexpr uint32_t S_OK = 0, E_FAIL = 0x80004005u;

template <uint32_t R>
HostCallOutcome Fixed(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(R); }

#define UX(name, bytes, r) {"UxTheme.dll", name, bytes, CallConv::Stdcall, &Fixed<r>}
const HostFunctionEntry kEntries[] = {
    UX("OpenThemeData", 8, 0), UX("OpenThemeDataEx", 12, 0), UX("CloseThemeData", 4, S_OK),
    UX("IsThemeActive", 0, 0), UX("IsAppThemed", 0, 0), UX("IsCompositionActive", 0, 0),
    UX("GetThemeAppProperties", 0, 0), UX("SetThemeAppProperties", 4, 0),
    UX("GetWindowTheme", 4, 0), UX("SetWindowTheme", 12, S_OK), UX("EnableThemeDialogTexture", 8, S_OK),
    UX("IsThemeDialogTextureEnabled", 4, 0), UX("IsThemePartDefined", 12, 0),
    UX("IsThemeBackgroundPartiallyTransparent", 12, 0),
    UX("DrawThemeBackground", 24, E_FAIL), UX("DrawThemeBackgroundEx", 24, E_FAIL), UX("DrawThemeText", 36, E_FAIL),
    UX("DrawThemeEdge", 32, E_FAIL), UX("DrawThemeIcon", 28, E_FAIL), UX("DrawThemeParentBackground", 12, E_FAIL),
    UX("GetThemeBackgroundContentRect", 24, E_FAIL), UX("GetThemeBackgroundExtent", 24, E_FAIL),
    UX("GetThemePartSize", 28, E_FAIL), UX("GetThemeTextExtent", 36, E_FAIL), UX("GetThemeTextMetrics", 20, E_FAIL),
    UX("GetThemeColor", 20, E_FAIL), UX("GetThemeMetric", 24, E_FAIL), UX("GetThemeInt", 20, E_FAIL),
    UX("GetThemeBool", 20, E_FAIL), UX("GetThemeMargins", 28, E_FAIL), UX("GetThemeFont", 24, E_FAIL),
    UX("GetThemeRect", 20, E_FAIL), UX("GetThemePosition", 20, E_FAIL), UX("GetThemeEnumValue", 20, E_FAIL),
    UX("GetThemeString", 24, E_FAIL), UX("GetThemeFilename", 24, E_FAIL),
    UX("GetThemeSysColor", 8, 0), UX("GetThemeSysColorBrush", 8, 0), UX("GetThemeSysFont", 12, E_FAIL),
    UX("GetThemeSysSize", 8, 0), UX("GetThemeSysBool", 8, 0), UX("GetCurrentThemeName", 24, E_FAIL),
    UX("HitTestThemeBackground", 40, E_FAIL), UX("BufferedPaintInit", 0, S_OK), UX("BufferedPaintUnInit", 0, S_OK),
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace
} // namespace win32
