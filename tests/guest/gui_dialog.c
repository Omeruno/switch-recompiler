/* gui_dialog.c — диалоги, элементы управления и ресурсы (USER32).
 *
 * Модальный диалог из шаблона ресурса: надпись, поле ввода, флажок,
 * переключатели, список, комбинированный список, иконка, кнопки. Сценарий
 * идёт по таймеру внутри диалога: текст, ввод символов, щелчки
 * (BM_CLICK), списки, клавиатура (Tab, Enter -> кнопка по умолчанию).
 * Плюс меню, ускорители, строки, битмап и иконка из ресурсов. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures;
#define CHECK(cond) do { if (cond) printf("ok   %s\n", #cond); else { printf("FAIL %s (line %d)\n", #cond, __LINE__); failures++; } } while (0)

#define IDC_EDIT 101
#define IDC_CHECK 102
#define IDC_RADIO1 103
#define IDC_RADIO2 104
#define IDC_LIST 105
#define IDC_COMBO 106
#define IDC_LABEL 107
#define IDC_PIC 108

static int step, changes, clicks, initFocusOk;
static char finalText[64];

static void Text(HWND dlg, int id, char* buf, int n) { GetDlgItemTextA(dlg, id, buf, n); }

static INT_PTR CALLBACK DlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    char buf[64];
    switch (msg) {
    case WM_INITDIALOG:
        CHECK(lp == 0x77);
        CHECK(GetDlgItem(dlg, IDC_EDIT) != NULL && GetDlgItem(dlg, IDC_COMBO) != NULL);
        initFocusOk = (HWND)wp == GetDlgItem(dlg, IDC_EDIT);
        SetDlgItemTextA(dlg, IDC_EDIT, "abc");
        SendDlgItemMessageA(dlg, IDC_LIST, LB_ADDSTRING, 0, (LPARAM)"pear");
        SendDlgItemMessageA(dlg, IDC_LIST, LB_ADDSTRING, 0, (LPARAM)"apple");
        SendDlgItemMessageA(dlg, IDC_LIST, LB_ADDSTRING, 0, (LPARAM)"plum");
        SendDlgItemMessageA(dlg, IDC_COMBO, CB_ADDSTRING, 0, (LPARAM)"one");
        SendDlgItemMessageA(dlg, IDC_COMBO, CB_ADDSTRING, 0, (LPARAM)"two");
        SendDlgItemMessageA(dlg, IDC_COMBO, CB_SETCURSEL, 1, 0);
        CheckRadioButton(dlg, IDC_RADIO1, IDC_RADIO2, IDC_RADIO1);
        SetTimer(dlg, 1, 10, NULL);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_EDIT && HIWORD(wp) == EN_CHANGE) changes++;
        if ((LOWORD(wp) == IDC_CHECK || LOWORD(wp) == IDC_RADIO2) && HIWORD(wp) == BN_CLICKED) clicks++;
        if (LOWORD(wp) == IDOK) {
            Text(dlg, IDC_EDIT, finalText, sizeof(finalText));
            KillTimer(dlg, 1);
            EndDialog(dlg, 42);
        }
        if (LOWORD(wp) == IDCANCEL) { KillTimer(dlg, 1); EndDialog(dlg, 0); }
        return TRUE;
    case WM_TIMER: {
        HWND edit = GetDlgItem(dlg, IDC_EDIT);
        switch (++step) {
        case 1:
            CHECK(initFocusOk && GetFocus() == edit);
            Text(dlg, IDC_EDIT, buf, sizeof(buf));
            CHECK(strcmp(buf, "abc") == 0);
            SendMessageA(edit, WM_CHAR, 'd', 1);
            Text(dlg, IDC_EDIT, buf, sizeof(buf));
            CHECK(strcmp(buf, "dabc") == 0);
            SendMessageA(edit, EM_SETSEL, 0, -1);
            SendMessageA(edit, WM_CHAR, 'X', 1);
            Text(dlg, IDC_EDIT, buf, sizeof(buf));
            CHECK(strcmp(buf, "X") == 0 && changes >= 2);
            SetDlgItemInt(dlg, IDC_EDIT, (UINT)-12, TRUE);
            {
                BOOL ok = FALSE;
                CHECK((int)GetDlgItemInt(dlg, IDC_EDIT, &ok, TRUE) == -12 && ok);
            }
            SetDlgItemTextA(dlg, IDC_EDIT, "final");
            break;
        case 2:
            SendDlgItemMessageA(dlg, IDC_CHECK, BM_CLICK, 0, 0);
            CHECK(IsDlgButtonChecked(dlg, IDC_CHECK) == BST_CHECKED);
            SendDlgItemMessageA(dlg, IDC_RADIO2, BM_CLICK, 0, 0);
            CHECK(IsDlgButtonChecked(dlg, IDC_RADIO1) == 0 && IsDlgButtonChecked(dlg, IDC_RADIO2) == 1);
            CHECK(clicks == 2);
            break;
        case 3:
            CHECK(SendDlgItemMessageA(dlg, IDC_LIST, LB_GETCOUNT, 0, 0) == 3);
            SendDlgItemMessageA(dlg, IDC_LIST, LB_SETCURSEL, 2, 0);
            CHECK(SendDlgItemMessageA(dlg, IDC_LIST, LB_GETCURSEL, 0, 0) == 2);
            SendDlgItemMessageA(dlg, IDC_LIST, LB_GETTEXT, 2, (LPARAM)buf);
            CHECK(strcmp(buf, "plum") == 0);
            CHECK(SendDlgItemMessageA(dlg, IDC_LIST, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)"apple") == 1);
            CHECK(SendDlgItemMessageA(dlg, IDC_COMBO, CB_GETCURSEL, 0, 0) == 1);
            SendDlgItemMessageA(dlg, IDC_COMBO, CB_GETLBTEXT, 1, (LPARAM)buf);
            CHECK(strcmp(buf, "two") == 0);
            Text(dlg, IDC_COMBO, buf, sizeof(buf));
            CHECK(strcmp(buf, "two") == 0);
            break;
        case 4:
            // Tab из поля ввода — к флажку
            SetFocus(edit);
            PostMessageA(edit, WM_KEYDOWN, VK_TAB, 0x000F0001);
            break;
        case 5:
            CHECK(GetFocus() == GetDlgItem(dlg, IDC_CHECK));
            // Enter — кнопка по умолчанию (OK)
            PostMessageA(GetFocus(), WM_KEYDOWN, VK_RETURN, 0x001C0001);
            break;
        case 30:
            printf("FAIL dialog did not close\n");
            failures++;
            EndDialog(dlg, -1);
            break;
        }
        return TRUE;
    }
    }
    return FALSE;
}

int main(void) {
    HINSTANCE inst = GetModuleHandleA(NULL);
    char buf[128];

    /* меню и ускорители */
    HMENU menu = LoadMenuA(inst, MAKEINTRESOURCEA(20));
    CHECK(menu != NULL && GetMenuItemCount(menu) == 1);
    HMENU file = GetSubMenu(menu, 0);
    CHECK(file != NULL && GetMenuItemCount(file) == 3);
    CHECK(GetMenuItemID(file, 0) == 1001 && GetMenuItemID(file, 2) == 1002);
    CHECK((GetMenuState(menu, 1002, MF_BYCOMMAND) & MF_GRAYED) != 0);
    CHECK(GetMenuStringA(menu, 1001, buf, sizeof(buf), MF_BYCOMMAND) > 0 && strcmp(buf, "&Open...\tCtrl+O") == 0);
    CHECK(CheckMenuItem(menu, 1001, MF_CHECKED) == MF_UNCHECKED && (GetMenuState(menu, 1001, 0) & MF_CHECKED));
    CHECK(LoadAcceleratorsA(inst, MAKEINTRESOURCEA(30)) != NULL);
    DestroyMenu(menu);

    /* строки, битмап, иконка */
    CHECK(LoadStringA(inst, 40, buf, sizeof(buf)) == 25 && strcmp(buf, "Hello from a string table") == 0);
    HBITMAP bmp = LoadBitmapA(inst, MAKEINTRESOURCEA(2));
    BITMAP bm;
    CHECK(bmp != NULL && GetObjectA(bmp, sizeof(bm), &bm) == sizeof(bm) && bm.bmWidth == 4 && bm.bmHeight == 2);
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(mem, bmp);
    CHECK(GetPixel(mem, 0, 0) == RGB(255, 0, 0) && GetPixel(mem, 1, 1) == RGB(0, 255, 0) && GetPixel(mem, 3, 0) == RGB(255, 255, 255));
    SelectObject(mem, old);
    HBITMAP canvas = CreateCompatibleBitmap(screen, 32, 32);
    SelectObject(mem, canvas);
    RECT all = {0, 0, 32, 32};
    FillRect(mem, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
    HICON icon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    CHECK(icon != NULL && DrawIconEx(mem, 0, 0, icon, 16, 16, 0, NULL, DI_NORMAL));
    CHECK(GetPixel(mem, 2, 2) == RGB(255, 255, 0) && GetPixel(mem, 12, 2) == RGB(255, 255, 255));
    CHECK(LoadIconA(NULL, IDI_QUESTION) != NULL);
    DeleteDC(mem);
    DeleteObject(canvas);
    DeleteObject(bmp);
    ReleaseDC(NULL, screen);

    /* диалог */
    INT_PTR r = DialogBoxParamA(inst, MAKEINTRESOURCEA(10), NULL, DlgProc, 0x77);
    CHECK(r == 42);
    CHECK(strcmp(finalText, "final") == 0);
    CHECK(step == 5);

    printf(failures ? "gui_dialog: FAILED (%d)\n" : "gui_dialog: PASSED\n", failures);
    return failures ? 1 : 0;
}
