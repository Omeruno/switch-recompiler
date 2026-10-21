/* SHELL32 Win32 integration smoke test. Run without a GUI window. */
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

int main(void) {
    SHFILEINFOW wi;
    SHFILEINFOA ai;
    WCHAR folder[MAX_PATH] = L"";
    WCHAR missing[MAX_PATH] = L"";
    memset(&wi, 0xAA, sizeof(wi));
    memset(&ai, 0xAA, sizeof(ai));
    CHECK(SHGetFileInfoW(L"C:\\sample.bmp", FILE_ATTRIBUTE_NORMAL, &wi, sizeof(wi),
           SHGFI_USEFILEATTRIBUTES | SHGFI_DISPLAYNAME | SHGFI_TYPENAME) != 0);
    CHECK(wcscmp(wi.szDisplayName, L"sample.bmp") == 0);
    CHECK(wcscmp(wi.szTypeName, L"Bitmap Image") == 0);
    CHECK(wi.hIcon == NULL);
    CHECK(SHGetFileInfoA("C:\\subdir", FILE_ATTRIBUTE_DIRECTORY, &ai, sizeof(ai),
           SHGFI_USEFILEATTRIBUTES | SHGFI_DISPLAYNAME | SHGFI_TYPENAME | SHGFI_ATTRIBUTES) != 0);
    CHECK(strcmp(ai.szDisplayName, "subdir") == 0);
    CHECK((ai.dwAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    CHECK(SHGetSpecialFolderPathW(NULL, folder, CSIDL_MYPICTURES, TRUE) != FALSE);
    CHECK(wcscmp(folder, L"C:\\My Documents\\My Pictures") == 0);
    CHECK(GetFileAttributesW(folder) & FILE_ATTRIBUTE_DIRECTORY);
    CHECK(SHGetSpecialFolderPathW(NULL, missing, 0x7f, FALSE) == FALSE);
    CHECK(ExtractIconW(NULL, L"C:\\sample.bmp", 0) == NULL);
    if (!failures) puts("PASSED");
    return failures != 0;
}
