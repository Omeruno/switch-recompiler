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
    if (!failures) puts("PASSED");
    return failures ? 1 : 0;
}
