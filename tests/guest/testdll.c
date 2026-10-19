/* testdll.c — тестовая DLL без CRT: экспорт по имени и ординалу,
 * данные с релокациями, DllMain. */
#include <windows.h>

static int counter = 5;
static int* pcounter = &counter;            /* абсолютный адрес: нужна релокация */
static const char* message = "hello from dll";

__declspec(dllexport) int add(int a, int b) { return a + b; }
__declspec(dllexport) int get_counter(void) { return *pcounter; }
__declspec(dllexport) int __stdcall triple(int a) { return a * 3; }
__declspec(dllexport) const char* get_message(void) { return message; }
__declspec(dllexport) int attach_count = 0;

BOOL WINAPI DllMainCRTStartup(HINSTANCE h, DWORD reason, LPVOID reserved) {
    (void)h; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) { counter = 42; attach_count++; }
    return TRUE;
}
