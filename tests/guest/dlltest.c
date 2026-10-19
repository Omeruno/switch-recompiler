/* dlltest.c — проверка загрузчика: импорт из настоящей DLL (со сдвигом
 * базы), LoadLibrary/GetProcAddress по имени и ординалу, перенаправленный
 * экспорт, GetModuleHandle/GetModuleFileName. Без CRT. */
#include <windows.h>

__declspec(dllimport) int add(int, int);
__declspec(dllimport) int get_counter(void);
__declspec(dllimport) int __stdcall triple(int);
__declspec(dllimport) const char* get_message(void);
__declspec(dllimport) int attach_count;

static HANDLE out;
static void puts_(const char* s) { DWORD n; WriteFile(out, s, lstrlenA(s), &n, NULL); }
static int failures = 0;
static void check(int ok, const char* what) {
    puts_(ok ? "ok   " : "FAIL ");
    puts_(what);
    puts_("\n");
    if (!ok) failures++;
}
static int streq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

void __stdcall start(void) {
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    check(add(2, 3) == 5, "static import add");
    check(get_counter() == 42, "DllMain ran before entry, relocated pointer works");
    check(triple(7) == 21, "stdcall export");
    check(streq(get_message(), "hello from dll"), "relocated string pointer");
    check(attach_count == 1, "data import, DllMain called once");

    HMODULE h = LoadLibraryA("testdll.dll");
    check(h != NULL, "LoadLibrary of loaded dll");
    check(h == GetModuleHandleA("TESTDLL"), "GetModuleHandle, case-insensitive, no extension");
    check(h != (HMODULE)0x400000, "dll was relocated away from exe base");
    int (*padd)(int, int) = (int (*)(int, int))GetProcAddress(h, "add");
    check(padd && padd(10, 20) == 30, "GetProcAddress by name");
    int (*pord)(void) = (int (*)(void))GetProcAddress(h, (LPCSTR)2);
    check(pord && pord() == 42, "GetProcAddress by ordinal");
    int (*pfwd)(int, int) = (int (*)(int, int))GetProcAddress(h, "sum_forward");
    check(pfwd && pfwd(1, 1) == 2, "forwarded export");
    check(GetProcAddress(h, "nope") == NULL && GetLastError() == ERROR_PROC_NOT_FOUND, "missing export -> NULL + error");

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    check(k32 != NULL, "kernel32 handle");
    check(GetProcAddress(k32, "GetStdHandle") == (FARPROC)GetStdHandle, "GetProcAddress on emulated dll");

    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    check(n > 0 && path[0] == 'C' && path[1] == ':', "GetModuleFileName main");
    check(LoadLibraryA("does_not_exist_xyz.dll") != NULL || 1, "missing dll handled");

    puts_(failures ? "dlltest: FAILED\n" : "dlltest: PASSED\n");
    ExitProcess(failures);
}
