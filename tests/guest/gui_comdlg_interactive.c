/* End-to-end Win32 common dialog test, driven by winrun's scripted mouse input.
 * Usage: gui_comdlg_interactive.exe open|save
 * The host test creates sample.bmp beside the executable.
 */
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

int main(int argc, char **argv) {
    const int save = argc > 1 && strcmp(argv[1], "save") == 0;
    wchar_t filename[MAX_PATH];
    OPENFILENAMEW o;
    DWORD attr;
    HANDLE h;
    memset(&o, 0, sizeof(o));
    memset(filename, 0, sizeof(filename));
    wcscpy(filename, save ? L"output.bmp" : L"sample.bmp");
    o.lStructSize = sizeof(o);
    o.lpstrFilter = L"Bitmaps (*.bmp)\0*.bmp\0All files (*.*)\0*.*\0";
    o.nFilterIndex = 1;
    o.lpstrFile = filename;
    o.nMaxFile = MAX_PATH;
    o.lpstrDefExt = L"bmp";
    o.Flags = OFN_PATHMUSTEXIST | (save ? 0 : OFN_FILEMUSTEXIST);
    printf("COMMONDLG: starting %s\n", save ? "save" : "open");
    fflush(stdout);
    if (!(save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o))) {
        printf("FAIL: common dialog returned FALSE, extended error=0x%08lx\n",
            (unsigned long)CommDlgExtendedError());
        return 1;
    }
    printf("COMMONDLG: returned offset=%u, extension=%u, path=", (unsigned)o.nFileOffset,
        (unsigned)o.nFileExtension);
    for (const wchar_t *p = filename; *p; ++p) putchar((*p < 128) ? (char)*p : '?');
    putchar('\n');
    if (wcsstr(filename, save ? L"output.bmp" : L"sample.bmp") == NULL) {
        puts("FAIL: wrong selected file"); return 1;
    }
    if (o.nFileOffset == 0 || filename[o.nFileOffset] == 0) {
        puts("FAIL: invalid file offset"); return 1;
    }
    if (!save) {
        attr = GetFileAttributesW(filename);
        if (attr == INVALID_FILE_ATTRIBUTES) {
            puts("FAIL: selected file is invisible to KERNEL32"); return 1;
        }
        h = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            puts("FAIL: cannot open returned file path"); return 1;
        }
        CloseHandle(h);
    } else {
        h = CreateFileW(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            puts("FAIL: cannot create selected save file"); return 1;
        }
        CloseHandle(h);
    }
    puts("PASSED");
    return 0;
}
