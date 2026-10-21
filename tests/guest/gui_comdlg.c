/* Smoke test for COMDLG32's OPENFILENAME validation without user interaction. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <commdlg.h>
static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s\n", #x); ++failures; } } while (0)
int main(void) {
    OPENFILENAMEA a;
    OPENFILENAMEW w;
    char filename[MAX_PATH] = "";
    wchar_t wfilename[MAX_PATH] = L"";
    memset(&a, 0, sizeof(a));
    memset(&w, 0, sizeof(w));
    a.lStructSize = 3; /* invalid size: no modal window should open */
    a.lpstrFile = filename;
    a.nMaxFile = MAX_PATH;
    CHECK(!GetOpenFileNameA(&a));
    CHECK(CommDlgExtendedError() == CDERR_STRUCTSIZE);
    CHECK(!GetSaveFileNameA(&a));
    CHECK(CommDlgExtendedError() == CDERR_STRUCTSIZE);
    w.lStructSize = 3;
    w.lpstrFile = wfilename;
    w.nMaxFile = MAX_PATH;
    CHECK(!GetOpenFileNameW(&w));
    CHECK(CommDlgExtendedError() == CDERR_STRUCTSIZE);
    CHECK(!GetSaveFileNameW(&w));
    CHECK(CommDlgExtendedError() == CDERR_STRUCTSIZE);
    a.lStructSize = sizeof(a);
    a.lpstrFile = NULL;
    CHECK(!GetOpenFileNameA(&a));
    CHECK(CommDlgExtendedError() == CDERR_INITIALIZATION);
    /* Delay-loaded by MFC after file selection; must resolve via COMDLG32. */
    {
        HMODULE dll = LoadLibraryA("comdlg32.dll");
        typedef short (WINAPI *TitleW)(LPCWSTR, LPWSTR, WORD);
        typedef short (WINAPI *TitleA)(LPCSTR, LPSTR, WORD);
        TitleW getW = dll ? (TitleW)GetProcAddress(dll, "GetFileTitleW") : NULL;
        TitleA getA = dll ? (TitleA)GetProcAddress(dll, "GetFileTitleA") : NULL;
        WCHAR wt[80] = L"";
        CHAR at[80] = "";
        CHECK(dll != NULL);
        CHECK(getW != NULL && getA != NULL);
        if (getW) {
            CHECK(getW(L"C:\\images\\sample.bmp", wt, 80) == 0);
            CHECK(lstrcmpW(wt, L"sample.bmp") == 0);
            CHECK(getW(L"C:\\images\\sample.bmp", NULL, 0) == 11);
            CHECK(getW(L"C:\\images\\sample.bmp", wt, 3) == 11);
            CHECK(getW(L"C:/images/demo.bmp", wt, 80) == 0);
            CHECK(lstrcmpW(wt, L"demo.bmp") == 0);
        }
        if (getA) {
            CHECK(getA("C:\\images\\sample.bmp", at, 80) == 0);
            CHECK(lstrcmpA(at, "sample.bmp") == 0);
            CHECK(getA("C:\\images\\sample.bmp", NULL, 0) == 11);
            CHECK(getA("C:\\images\\sample.bmp", at, 2) == 11);
        }
    }
    if (!failures) puts("PASSED");
    return failures ? 1 : 0;
}
