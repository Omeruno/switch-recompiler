/* gui_basic.c — оконная программа на чистом Win32 (USER32 + GDI32).
 *
 * Проверяет сама себя и печатает PASSED/FAIL, как остальные тесты:
 * классы и окна, сообщения (отправленные и из очереди), WM_CREATE/WM_SIZE/
 * WM_PAINT, таймеры, слова окна, клиентская область, рисование GDI
 * (кисти, перья, фигуры, текст), контексты в памяти, BitBlt, DIB-секции,
 * дочерние окна. Сценарий идёт по таймеру, ввод не нужен: программа
 * завершается сама через PostQuitMessage. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures;
#define CHECK(cond) do { if (cond) printf("ok   %s\n", #cond); else { printf("FAIL %s (line %d)\n", #cond, __LINE__); failures++; } } while (0)

#define WM_APP_PING (WM_APP + 1)
#define CLIENT_W 320
#define CLIENT_H 200

static int created, sized, painted, timerTicks, pinged, childCreated, destroyed;
static int lastSizeW, lastSizeH;
static HWND mainWnd, childWnd;

static COLORREF Pixel(HWND hwnd, int x, int y) {
    HDC dc = GetDC(hwnd);
    COLORREF c = GetPixel(dc, x, y);
    ReleaseDC(hwnd, dc);
    return c;
}

static void PaintScene(HDC dc) {
    RECT rc;
    GetClientRect(mainWnd, &rc);
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &rc, white);
    DeleteObject(white);

    /* Прямоугольник: красная рамка (перо 1px), синяя заливка */
    HPEN red = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
    HBRUSH blue = CreateSolidBrush(RGB(0, 0, 255));
    HGDIOBJ oldPen = SelectObject(dc, red);
    HGDIOBJ oldBrush = SelectObject(dc, blue);
    Rectangle(dc, 10, 10, 60, 40); /* правая и нижняя граница не включаются */
    SelectObject(dc, GetStockObject(BLACK_PEN));
    MoveToEx(dc, 100, 10, NULL);
    LineTo(dc, 150, 10);           /* последняя точка не рисуется */
    SelectObject(dc, GetStockObject(NULL_PEN));
    HBRUSH green = CreateSolidBrush(RGB(0, 128, 0));
    SelectObject(dc, green);
    Ellipse(dc, 200, 20, 260, 80);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(red);
    DeleteObject(blue);
    DeleteObject(green);

    /* Текст: чёрный, прозрачный фон */
    SetTextColor(dc, RGB(0, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    TextOutA(dc, 10, 100, "Hello", 5);

    /* Картинка из контекста в памяти */
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, 16, 16);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    RECT r16 = {0, 0, 16, 16};
    HBRUSH yellow = CreateSolidBrush(RGB(255, 255, 0));
    FillRect(mem, &r16, yellow);
    DeleteObject(yellow);
    SetPixel(mem, 3, 4, RGB(1, 2, 3));
    BitBlt(dc, 280, 150, 16, 16, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);

    /* DIB-секция: программа пишет пиксели прямо в память */
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 8;
    bi.bmiHeader.biHeight = -8; /* сверху вниз */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dib && bits) {
        DWORD* p = (DWORD*)bits;
        for (int i = 0; i < 64; i++) p[i] = 0x00FF00FF; /* пурпурный (0x00RRGGBB) */
        p[2 * 8 + 5] = 0x00123456;
        HDC dibDc = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(dibDc, dib);
        BitBlt(dc, 300, 10, 8, 8, dibDc, 0, 0, SRCCOPY);
        SelectObject(dibDc, old);
        DeleteDC(dibDc);
        DeleteObject(dib);
    }
}

static LRESULT CALLBACK ChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: childCreated++; return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH b = CreateSolidBrush(RGB(0, 200, 200));
        FillRect(dc, &rc, b);
        DeleteObject(b);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void CheckPaint(void) {
    CHECK(Pixel(mainWnd, 5, 5) == RGB(255, 255, 255));
    CHECK(Pixel(mainWnd, 10, 10) == RGB(255, 0, 0));
    CHECK(Pixel(mainWnd, 59, 39) == RGB(255, 0, 0));
    CHECK(Pixel(mainWnd, 60, 40) == RGB(255, 255, 255));
    CHECK(Pixel(mainWnd, 30, 25) == RGB(0, 0, 255));
    CHECK(Pixel(mainWnd, 120, 10) == RGB(0, 0, 0));
    CHECK(Pixel(mainWnd, 150, 10) == RGB(255, 255, 255));
    CHECK(Pixel(mainWnd, 230, 50) == RGB(0, 128, 0));
    CHECK(Pixel(mainWnd, 201, 21) == RGB(255, 255, 255)); /* угол вне эллипса */
    int ink = 0;
    for (int y = 100; y < 120; y++)
        for (int x = 10; x < 60; x++)
            if (Pixel(mainWnd, x, y) == RGB(0, 0, 0)) ink++;
    CHECK(ink > 20); /* текст что-то нарисовал */
    CHECK(Pixel(mainWnd, 281, 151) == RGB(255, 255, 0));
    CHECK(Pixel(mainWnd, 283, 154) == RGB(1, 2, 3));
    CHECK(Pixel(mainWnd, 300, 10) == RGB(255, 0, 255));
    CHECK(Pixel(mainWnd, 305, 12) == RGB(0x12, 0x34, 0x56));
    /* дочернее окно в (20,150) 40x30 своего цвета */
    CHECK(Pixel(mainWnd, 25, 155) == RGB(0, 200, 200));
    CHECK(Pixel(childWnd, 0, 0) == RGB(0, 200, 200));
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA* cs = (CREATESTRUCTA*)lp;
        created++;
        CHECK(cs->lpCreateParams == (void*)0x1234);
        SetWindowLongA(hwnd, GWL_USERDATA, 0x5678);
        return 0;
    }
    case WM_SIZE:
        sized++;
        lastSizeW = LOWORD(lp);
        lastSizeH = HIWORD(lp);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        painted++;
        PaintScene(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_APP_PING:
        pinged++;
        return wp + lp;
    case WM_TIMER:
        timerTicks++;
        if (timerTicks == 1) {
            CheckPaint();
            CHECK(PostMessageA(hwnd, WM_APP_PING, 1, 2));
        } else if (timerTicks == 3) {
            KillTimer(hwnd, 1);
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        destroyed++;
        PostQuitMessage(7);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(void) {
    HINSTANCE inst = GetModuleHandleA(NULL);
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "GuiBasicMain";
    CHECK(RegisterClassExA(&wc) != 0);
    wc.lpfnWndProc = ChildProc;
    wc.lpszClassName = "GuiBasicChild";
    CHECK(RegisterClassExA(&wc) != 0);

    /* Окно с рамкой: внешний размер считаем от нужной клиентской области */
    RECT want = {0, 0, CLIENT_W, CLIENT_H};
    DWORD style = WS_OVERLAPPEDWINDOW;
    CHECK(AdjustWindowRectEx(&want, style, FALSE, 0));
    CHECK(want.right - want.left > CLIENT_W && want.bottom - want.top > CLIENT_H);
    mainWnd = CreateWindowExA(0, "GuiBasicMain", "GUI test", style, 10, 10,
                              want.right - want.left, want.bottom - want.top, NULL, NULL, inst, (void*)0x1234);
    CHECK(mainWnd != NULL);
    CHECK(created == 1);
    CHECK(GetWindowLongA(mainWnd, GWL_USERDATA) == 0x5678);
    RECT rc;
    GetClientRect(mainWnd, &rc);
    CHECK(rc.left == 0 && rc.top == 0 && rc.right == CLIENT_W && rc.bottom == CLIENT_H);
    char title[32];
    CHECK(GetWindowTextA(mainWnd, title, sizeof(title)) == 8 && strcmp(title, "GUI test") == 0);

    childWnd = CreateWindowExA(0, "GuiBasicChild", "", WS_CHILD | WS_VISIBLE, 20, 150, 40, 30,
                               mainWnd, (HMENU)5, inst, NULL);
    CHECK(childWnd != NULL && childCreated == 1);
    CHECK(GetParent(childWnd) == mainWnd && GetDlgItem(mainWnd, 5) == childWnd);
    POINT pt = {0, 0};
    ClientToScreen(childWnd, &pt);
    POINT origin = {0, 0};
    ClientToScreen(mainWnd, &origin);
    CHECK(pt.x == origin.x + 20 && pt.y == origin.y + 150);

    CHECK(SendMessageA(mainWnd, WM_APP_PING, 40, 2) == 42 && pinged == 1);
    ShowWindow(mainWnd, SW_SHOW);
    UpdateWindow(mainWnd);
    CHECK(painted == 1);
    CHECK(sized >= 1 && lastSizeW == CLIENT_W && lastSizeH == CLIENT_H);
    CHECK(IsWindowVisible(mainWnd));
    CHECK(SetTimer(mainWnd, 1, 50, NULL) == 1);

    MSG msg;
    int got;
    while ((got = GetMessageA(&msg, NULL, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    CHECK(got == 0 && msg.message == WM_QUIT && msg.wParam == 7);
    CHECK(timerTicks == 3 && pinged == 2 && destroyed == 1);
    CHECK(!IsWindow(mainWnd) && !IsWindow(childWnd));

    /* Цветное -> монохромное: белыми становятся пиксели цвета фона источника */
    {
        HDC colorDc = CreateCompatibleDC(NULL), monoDc = CreateCompatibleDC(NULL);
        HDC screen = GetDC(NULL);
        HBITMAP color = CreateCompatibleBitmap(screen, 4, 1), mono = CreateBitmap(4, 1, 1, 1, NULL);
        ReleaseDC(NULL, screen);
        SelectObject(colorDc, color);
        SelectObject(monoDc, mono);
        SetPixel(colorDc, 0, 0, RGB(255, 0, 0));
        SetPixel(colorDc, 1, 0, RGB(255, 255, 255));
        SetPixel(colorDc, 2, 0, RGB(255, 0, 0));
        SetPixel(colorDc, 3, 0, RGB(0, 0, 0));
        SetBkColor(colorDc, RGB(255, 0, 0));
        BitBlt(monoDc, 0, 0, 4, 1, colorDc, 0, 0, SRCCOPY);
        CHECK(GetPixel(monoDc, 0, 0) == RGB(255, 255, 255) && GetPixel(monoDc, 2, 0) == RGB(255, 255, 255));
        CHECK(GetPixel(monoDc, 1, 0) == RGB(0, 0, 0) && GetPixel(monoDc, 3, 0) == RGB(0, 0, 0));
        DeleteDC(colorDc);
        DeleteDC(monoDc);
        DeleteObject(color);
        DeleteObject(mono);
    }

    printf(failures ? "gui_basic: FAILED (%d)\n" : "gui_basic: PASSED\n", failures);
    return failures ? 1 : 0;
}
