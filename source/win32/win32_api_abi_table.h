#pragma once
// win32_api_abi_table.h
//
// Размер стековых аргументов (callee cleanup, __stdcall) для функций Win32,
// которые paint.exe импортирует ПО ИМЕНИ, но у нас пока НЕ реализованы.
// Нужна страховочной заглушке: без неё IAT-слот такой функции оставался 0,
// и любой вызов был мгновенным крашем "вызов адреса 0" без имени функции.
// Теперь неизвестная функция возвращает 0 с КОРРЕКТНЫМ стеком и пишет в
// лог своё имя (как уже делают заглушки MFC-ординалов).
//
// Источник размеров: сигнатуры Windows SDK. Все перечисленные функции
// __stdcall, 4 байта на аргумент; исключения отмечены. Функции msvcrt.dll
// (__cdecl, стек чистит вызывающий) в таблицу не входят: для них 0 по
// определению (см. Win32ApiCalleeCleanup).
//
// ВАЖНО: это НЕ реализации. Функция, возвращающая 0, для приложения часто
// означает "ошибка". Каждая функция, реально нужная инструментам Paint
// (Rectangle, Ellipse, Polygon, ExtFloodFill, TextOutW...), должна
// получить честную реализацию в host_functions.cpp и исчезнуть отсюда
// сама собой (FindHostFunction находит её раньше, чем эту таблицу).
#include <cstdint>
#include <cstring>

namespace win32 {

struct Win32AbiEntry { const char* dll; const char* name; uint16_t argBytes; };

inline constexpr Win32AbiEntry g_win32ApiAbi[] = {
    // ADVAPI32
    {"advapi32.dll","RegEnumKeyW",16}, {"advapi32.dll","RegOpenKeyExW",20},
    // KERNEL32
    {"kernel32.dll","CloseHandle",4}, {"kernel32.dll","CreateDirectoryW",8}, {"kernel32.dll","CreateFileW",28},
    {"kernel32.dll","FileTimeToLocalFileTime",8}, {"kernel32.dll","FileTimeToSystemTime",8}, {"kernel32.dll","FindClose",4},
    {"kernel32.dll","FindFirstFileW",8}, {"kernel32.dll","FreeLibrary",4}, {"kernel32.dll","GetACP",0},
    {"kernel32.dll","GetCurrentProcess",0}, {"kernel32.dll","GetDateFormatW",24}, {"kernel32.dll","GetExitCodeThread",8},
    {"kernel32.dll","GetFileAttributesW",4}, {"kernel32.dll","GetFileSize",8}, {"kernel32.dll","GetFileTime",16},
    {"kernel32.dll","GetLocaleInfoW",16}, {"kernel32.dll","GetNumberFormatW",24}, {"kernel32.dll","GetTempFileNameW",16},
    {"kernel32.dll","GetTempPathW",8}, {"kernel32.dll","GetThreadLocale",0}, {"kernel32.dll","GetTimeFormatW",24},
    {"kernel32.dll","GlobalAlloc",8}, {"kernel32.dll","GlobalLock",4}, {"kernel32.dll","GlobalReAlloc",12},
    {"kernel32.dll","GlobalUnlock",4}, {"kernel32.dll","IsDBCSLeadByte",4}, {"kernel32.dll","LoadLibraryW",4},
    {"kernel32.dll","LocalAlloc",8}, {"kernel32.dll","LocalFree",4}, {"kernel32.dll","MulDiv",12},
    {"kernel32.dll","MultiByteToWideChar",24}, {"kernel32.dll","SetEndOfFile",4}, {"kernel32.dll","SetUnhandledExceptionFilter",4},
    {"kernel32.dll","TerminateProcess",8}, {"kernel32.dll","TerminateThread",8}, {"kernel32.dll","UnhandledExceptionFilter",4},
    {"kernel32.dll","lstrcatW",8}, {"kernel32.dll","lstrcmpW",8}, {"kernel32.dll","lstrcmpiW",8},
    {"kernel32.dll","lstrcpynW",12}, {"kernel32.dll","lstrlenA",4},
    // GDI32
    {"gdi32.dll","CreateDCW",16}, {"gdi32.dll","CreateDIBSection",24}, {"gdi32.dll","CreateDIBitmap",24},
    {"gdi32.dll","CreateHalftonePalette",4}, {"gdi32.dll","CreateICW",16}, {"gdi32.dll","CreatePalette",4},
    {"gdi32.dll","CreatePolygonRgn",12}, {"gdi32.dll","CreateRectRgnIndirect",4}, {"gdi32.dll","Ellipse",20},
    {"gdi32.dll","EnumFontFamiliesExW",20}, {"gdi32.dll","EnumFontFamiliesW",16}, {"gdi32.dll","Escape",20},
    {"gdi32.dll","ExtFloodFill",20}, {"gdi32.dll","ExtTextOutW",32}, {"gdi32.dll","FillRgn",12},
    {"gdi32.dll","GetBkMode",4}, {"gdi32.dll","GetCurrentObject",8}, {"gdi32.dll","GetDIBColorTable",16},
    {"gdi32.dll","GetDIBits",28}, {"gdi32.dll","GetNearestColor",8}, {"gdi32.dll","GetNearestPaletteIndex",8},
    {"gdi32.dll","GetObjectW",12}, {"gdi32.dll","GetPaletteEntries",16}, {"gdi32.dll","GetPixel",12},
    {"gdi32.dll","GetTextColor",4}, {"gdi32.dll","GetTextExtentPoint32W",16}, {"gdi32.dll","GetTextMetricsW",8},
    {"gdi32.dll","LPtoDP",12}, {"gdi32.dll","OffsetRgn",12}, {"gdi32.dll","PatBlt",24},
    {"gdi32.dll","PlayMetaFile",8}, {"gdi32.dll","PolyBezier",12}, {"gdi32.dll","Polygon",12},
    {"gdi32.dll","Polyline",12}, {"gdi32.dll","PtVisible",12}, {"gdi32.dll","RectVisible",8},
    {"gdi32.dll","Rectangle",20}, {"gdi32.dll","ResizePalette",8}, {"gdi32.dll","RestoreDC",8},
    {"gdi32.dll","RoundRect",28}, {"gdi32.dll","SaveDC",4}, {"gdi32.dll","SetBkColor",8},
    {"gdi32.dll","SetBkMode",8}, {"gdi32.dll","SetBrushOrgEx",16}, {"gdi32.dll","SetDIBColorTable",16},
    {"gdi32.dll","SetDIBits",28}, {"gdi32.dll","SetMapMode",8}, {"gdi32.dll","SetPaletteEntries",16},
    {"gdi32.dll","SetStretchBltMode",8}, {"gdi32.dll","SetTextAlign",8}, {"gdi32.dll","SetTextColor",8},
    {"gdi32.dll","SetViewportExtEx",16}, {"gdi32.dll","StretchBlt",44}, {"gdi32.dll","StretchDIBits",52},
    {"gdi32.dll","TextOutW",20}, {"gdi32.dll","TranslateCharsetInfo",12}, {"gdi32.dll","UnrealizeObject",4},
    // USER32
    {"user32.dll","BringWindowToTop",4}, {"user32.dll","CharNextW",4}, {"user32.dll","CheckDlgButton",12},
    {"user32.dll","ClientToScreen",8}, {"user32.dll","CreateCaret",16}, {"user32.dll","DestroyCaret",0},
    {"user32.dll","DestroyIcon",4}, {"user32.dll","DestroyWindow",4}, {"user32.dll","DrawFocusRect",8},
    {"user32.dll","DrawTextW",20}, {"user32.dll","EnableMenuItem",12}, {"user32.dll","EnableScrollBar",12},
    {"user32.dll","EnableWindow",8}, {"user32.dll","EqualRect",8}, {"user32.dll","FillRect",12},
    {"user32.dll","FrameRect",12}, {"user32.dll","GetCaretPos",4}, {"user32.dll","GetClassInfoW",12},
    {"user32.dll","GetClientRect",8}, {"user32.dll","GetCursorPos",4}, {"user32.dll","GetKeyboardLayout",4},
    {"user32.dll","GetMenu",4}, {"user32.dll","GetParent",4}, {"user32.dll","GetSubMenu",8},
    {"user32.dll","GetSystemMenu",8}, {"user32.dll","GetUpdateRect",12}, {"user32.dll","GetWindow",8},
    {"user32.dll","GetWindowDC",4}, {"user32.dll","GetWindowLongW",8}, {"user32.dll","GrayStringW",36},
    {"user32.dll","HideCaret",4}, {"user32.dll","IsClipboardFormatAvailable",4}, {"user32.dll","IsMenu",4},
    {"user32.dll","IsRectEmpty",4}, {"user32.dll","IsWindowVisible",4}, {"user32.dll","LoadBitmapW",8},
    {"user32.dll","MessageBeep",4}, {"user32.dll","MsgWaitForMultipleObjects",20}, {"user32.dll","OffsetRect",12},
    {"user32.dll","RedrawWindow",16}, {"user32.dll","RegisterClipboardFormatW",4}, {"user32.dll","RemoveMenu",12},
    {"user32.dll","ScreenToClient",8}, {"user32.dll","SendDlgItemMessageW",20}, {"user32.dll","SetCaretPos",8},
    {"user32.dll","SetClassLongW",12}, {"user32.dll","SetWindowLongW",12}, {"user32.dll","SetWindowTextW",8},
    {"user32.dll","ShowCaret",4}, {"user32.dll","ShowCursor",4}, {"user32.dll","TabbedTextOutW",32},
    {"user32.dll","UnionRect",12}, {"user32.dll","ValidateRect",8},
    {"user32.dll","WindowFromPoint",8},   // POINT передаётся ПО ЗНАЧЕНИЮ: 8 байт, не 4
    {"user32.dll","wvsprintfW",12},
    // COMDLG32
    {"comdlg32.dll","CommDlgExtendedError",0}, {"comdlg32.dll","GetFileTitleW",12},
    {"comdlg32.dll","GetOpenFileNameW",4}, {"comdlg32.dll","GetSaveFileNameW",4},
    // OLE32
    {"ole32.dll","CoGetInterfaceAndReleaseStream",12}, {"ole32.dll","CoInitialize",4},
    {"ole32.dll","CoMarshalInterThreadInterfaceInStream",12}, {"ole32.dll","CoTaskMemFree",4},
    {"ole32.dll","CoUninitialize",0}, {"ole32.dll","OleGetClipboard",4}, {"ole32.dll","PropVariantClear",4},
    {"ole32.dll","ReleaseStgMedium",4}, {"ole32.dll","WriteClassStg",8}, {"ole32.dll","WriteFmtUserTypeStg",12},
    // SHELL32
    {"shell32.dll","SHChangeNotify",16},
    // IMM32
    {"imm32.dll","ImmAssociateContext",8}, {"imm32.dll","ImmGetCompositionStringW",16},
    {"imm32.dll","ImmGetCompositionWindow",8}, {"imm32.dll","ImmGetContext",4}, {"imm32.dll","ImmNotifyIME",16},
    {"imm32.dll","ImmReleaseContext",8}, {"imm32.dll","ImmSetCompositionWindow",8},
};

inline bool AbiNameEqualsIgnoreCase(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return *a == *b;
}

// Возвращает true и размер очистки, если функцию можно безопасно заглушить.
// msvcrt.dll целиком __cdecl: очистка 0, известна всегда.
inline bool Win32ApiCalleeCleanup(const char* dll, const char* name, uint16_t& outBytes) {
    if (AbiNameEqualsIgnoreCase(dll, "msvcrt.dll")) { outBytes = 0; return true; }
    for (const auto& e : g_win32ApiAbi) {
        if (AbiNameEqualsIgnoreCase(e.dll, dll) && std::strcmp(e.name, name) == 0) {
            outBytes = e.argBytes;
            return true;
        }
    }
    return false;
}

} // namespace win32
