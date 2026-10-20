#pragma once
// user32.h
//
// Новая оконная подсистема (USER32, слой HLE "ui2"): классы и окна,
// иерархия и порядок окон, неклиентская область, отрисовка через области
// обновления, очередь сообщений, таймеры, ввод, фокус и активация.
// Рисует в общий экран ui::g_gdi (экран — поверхность, которую фронтенд
// показывает на дисплее).
//
// Общие объявления для user32_*.cpp.

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../cpu/cpu_state.h"
#include "../cpu/guest_memory.h"
#include "../ui/gdi.h"
#include "../ui/region.h"
#include "hle_util.h"

namespace win32 {
namespace user {

using ui::Rect;
using ui::Region;

// --- Стили и константы Windows --------------------------------------------
constexpr uint32_t WS_OVERLAPPED = 0x00000000, WS_POPUP = 0x80000000, WS_CHILD = 0x40000000,
                   WS_MINIMIZE = 0x20000000, WS_VISIBLE = 0x10000000, WS_DISABLED = 0x08000000,
                   WS_CLIPSIBLINGS = 0x04000000, WS_CLIPCHILDREN = 0x02000000, WS_MAXIMIZE = 0x01000000,
                   WS_CAPTION = 0x00C00000, WS_BORDER = 0x00800000, WS_DLGFRAME = 0x00400000,
                   WS_VSCROLL = 0x00200000, WS_HSCROLL = 0x00100000, WS_SYSMENU = 0x00080000,
                   WS_THICKFRAME = 0x00040000, WS_MINIMIZEBOX = 0x00020000, WS_MAXIMIZEBOX = 0x00010000;
constexpr uint32_t WS_EX_DLGMODALFRAME = 0x1, WS_EX_TOPMOST = 0x8, WS_EX_TOOLWINDOW = 0x80,
                   WS_EX_WINDOWEDGE = 0x100, WS_EX_CLIENTEDGE = 0x200, WS_EX_STATICEDGE = 0x20000,
                   WS_EX_APPWINDOW = 0x40000, WS_EX_NOACTIVATE = 0x08000000;

enum : uint32_t {
    WM_NULL = 0x0000, WM_CREATE = 0x0001, WM_DESTROY = 0x0002, WM_MOVE = 0x0003, WM_SIZE = 0x0005,
    WM_ACTIVATE = 0x0006, WM_SETFOCUS = 0x0007, WM_KILLFOCUS = 0x0008, WM_ENABLE = 0x000A, WM_SETREDRAW = 0x000B,
    WM_SETTEXT = 0x000C, WM_GETTEXT = 0x000D, WM_GETTEXTLENGTH = 0x000E, WM_PAINT = 0x000F, WM_CLOSE = 0x0010,
    WM_QUERYENDSESSION = 0x0011, WM_QUIT = 0x0012, WM_ERASEBKGND = 0x0014, WM_SYSCOLORCHANGE = 0x0015,
    WM_SHOWWINDOW = 0x0018, WM_ACTIVATEAPP = 0x001C, WM_SETCURSOR = 0x0020, WM_MOUSEACTIVATE = 0x0021,
    WM_GETMINMAXINFO = 0x0024, WM_SETFONT = 0x0030, WM_GETFONT = 0x0031, WM_WINDOWPOSCHANGING = 0x0046,
    WM_WINDOWPOSCHANGED = 0x0047, WM_NOTIFY = 0x004E, WM_GETICON = 0x007F, WM_SETICON = 0x0080,
    WM_NCCREATE = 0x0081, WM_NCDESTROY = 0x0082, WM_NCCALCSIZE = 0x0083, WM_NCHITTEST = 0x0084,
    WM_NCPAINT = 0x0085, WM_NCACTIVATE = 0x0086, WM_GETDLGCODE = 0x0087,
    WM_NCMOUSEMOVE = 0x00A0, WM_NCLBUTTONDOWN = 0x00A1, WM_NCLBUTTONUP = 0x00A2, WM_NCLBUTTONDBLCLK = 0x00A3,
    WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101, WM_CHAR = 0x0102, WM_DEADCHAR = 0x0103, WM_SYSKEYDOWN = 0x0104,
    WM_SYSKEYUP = 0x0105, WM_SYSCHAR = 0x0106, WM_INITDIALOG = 0x0110, WM_COMMAND = 0x0111,
    WM_SYSCOMMAND = 0x0112, WM_TIMER = 0x0113, WM_HSCROLL = 0x0114, WM_VSCROLL = 0x0115, WM_INITMENU = 0x0116,
    WM_INITMENUPOPUP = 0x0117, WM_MENUSELECT = 0x011F, WM_ENTERIDLE = 0x0121,
    WM_CTLCOLORMSGBOX = 0x0132, WM_CTLCOLOREDIT = 0x0133, WM_CTLCOLORLISTBOX = 0x0134, WM_CTLCOLORBTN = 0x0135,
    WM_CTLCOLORDLG = 0x0136, WM_CTLCOLORSCROLLBAR = 0x0137, WM_CTLCOLORSTATIC = 0x0138,
    WM_MOUSEMOVE = 0x0200, WM_LBUTTONDOWN = 0x0201, WM_LBUTTONUP = 0x0202, WM_LBUTTONDBLCLK = 0x0203,
    WM_RBUTTONDOWN = 0x0204, WM_RBUTTONUP = 0x0205, WM_RBUTTONDBLCLK = 0x0206, WM_MBUTTONDOWN = 0x0207,
    WM_MBUTTONUP = 0x0208, WM_MBUTTONDBLCLK = 0x0209, WM_MOUSEWHEEL = 0x020A, WM_PARENTNOTIFY = 0x0210,
    WM_ENTERMENULOOP = 0x0211, WM_EXITMENULOOP = 0x0212, WM_CAPTURECHANGED = 0x0215,
    WM_MDIACTIVATE = 0x0222, WM_ENTERSIZEMOVE = 0x0231, WM_EXITSIZEMOVE = 0x0232,
    WM_IME_SETCONTEXT = 0x0281, WM_IME_NOTIFY = 0x0282, WM_MOUSELEAVE = 0x02A3,
    WM_CUT = 0x0300, WM_COPY = 0x0301, WM_PASTE = 0x0302, WM_CLEAR = 0x0303, WM_UNDO = 0x0304,
    WM_PRINTCLIENT = 0x0318, WM_APP = 0x8000, WM_USER = 0x0400,
};

constexpr uint32_t HTERROR = 0xFFFFFFFE, HTTRANSPARENT = 0xFFFFFFFF, HTNOWHERE = 0, HTCLIENT = 1, HTCAPTION = 2,
                   HTSYSMENU = 3, HTMENU = 5, HTHSCROLL = 6, HTVSCROLL = 7, HTMINBUTTON = 8, HTMAXBUTTON = 9,
                   HTLEFT = 10, HTRIGHT = 11, HTTOP = 12, HTTOPLEFT = 13, HTTOPRIGHT = 14, HTBOTTOM = 15,
                   HTBOTTOMLEFT = 16, HTBOTTOMRIGHT = 17, HTBORDER = 18, HTCLOSE = 20;

// Метрики классической темы Windows XP
constexpr int kCaptionH = 19, kSmCaptionH = 16, kMenuH = 19, kScrollW = 16, kFrame = 4, kDlgFrame = 3, kEdge = 2;

// --- Классы и окна --------------------------------------------------------

// Встроенная (на стороне хоста) оконная процедура: системные классы и
// DefWindowProc. Вызывается напрямую, без исполнения кода гостя.
using HostWndProc = uint32_t (*)(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode);

struct WndClass {
    std::u16string name;
    uint16_t atom = 0;
    uint32_t style = 0;
    uint32_t wndProc = 0;       // адрес процедуры (гостя или host-заглушки)
    bool unicode = false;       // зарегистрирован через *W
    int cbClsExtra = 0, cbWndExtra = 0;
    uint32_t hInstance = 0, hIcon = 0, hIconSm = 0, hCursor = 0, hbrBackground = 0;
    uint32_t menuName = 0;      // указатель или ID ресурса (как в WNDCLASS)
    std::vector<uint8_t> clsExtra;
    bool system = false;        // системный класс: доступен любому hInstance
};

struct Window {
    uint32_t hwnd = 0;
    WndClass* cls = nullptr;
    uint32_t style = 0, exStyle = 0;
    uint32_t parent = 0;        // 0 у окон верхнего уровня (внутренне — потомки рабочего стола)
    uint32_t owner = 0;
    std::vector<uint32_t> children; // порядок по оси Z: первый — сверху
    Rect rect;                  // прямоугольник окна в координатах клиентской области родителя
    Rect client;                // клиентская область в координатах окна
    std::u16string text;
    uint32_t wndProc = 0;
    bool unicode = false;
    std::vector<uint8_t> extra;
    uint32_t userData = 0, id = 0, hInstance = 0, menu = 0;
    uint32_t hIconBig = 0, hIconSmall = 0;
    Region update;              // в координатах клиентской области
    bool erase = false;         // нужен WM_ERASEBKGND
    bool ncDirty = false;       // нужна перерисовка рамки (WM_NCPAINT)
    bool internalPaint = false;
    bool destroying = false;
    bool active = false;        // заголовок в активном виде
    bool redraw = true;         // WM_SETREDRAW
    std::map<std::u16string, uint32_t> props;
    // Встроенные элементы управления хранят здесь своё состояние
    std::shared_ptr<void> controlState;
};

// --- Глобальное состояние --------------------------------------------------

struct Msg {
    uint32_t hwnd = 0, message = 0, wParam = 0, lParam = 0, time = 0;
    int x = 0, y = 0;
};

struct Timer {
    uint32_t hwnd = 0, id = 0, interval = 0, proc = 0;
    uint64_t due = 0; // мс по rt::Clock
};

struct State {
    bool initialized = false;
    int screenW = 800, screenH = 600;
    ui::OwnedSurface screen;
    std::map<uint32_t, std::unique_ptr<Window>> windows;
    std::vector<std::unique_ptr<WndClass>> classes;
    uint32_t desktop = 0;
    uint32_t nextHandle = 0x00010020;
    uint16_t nextAtom = 0xC000;
    uint32_t active = 0, focus = 0, capture = 0;
    std::deque<Msg> posted, input;
    bool quit = false;
    uint32_t quitCode = 0;
    std::vector<Timer> timers;
    uint32_t nextTimerId = 0x7FFE;
    int cursorX = 0, cursorY = 0;
    uint8_t keys[256] = {};       // состояние клавиш на момент последнего сообщения (GetKeyState)
    uint8_t asyncKeys[256] = {};  // текущее состояние (GetAsyncKeyState)
    uint32_t lastClickTime = 0, lastClickHwnd = 0, lastClickMsg = 0;
    int lastClickX = 0, lastClickY = 0;
    uint64_t presentedChanges = 0;
    uint32_t lastPresentMs = 0;
    uint32_t defWindowProcA = 0, defWindowProcW = 0; // host-адреса DefWindowProc
};

State& S();

// Текущий вызов из гостя: нужен для вызова оконных процедур гостя
struct GuestCtx {
    cpu::CpuState* c = nullptr;
    cpu::IGuestMemory* m = nullptr;
};
GuestCtx& G();

// Инициализация: экран, рабочий стол, системные классы
void Init();

Window* Win(uint32_t hwnd);           // nullptr для 0 и несуществующих
Window* Desktop();
Window* ParentWin(Window* w);         // родитель (для верхнего уровня — рабочий стол)
inline bool IsChildStyle(Window* w) { return w && (w->style & WS_CHILD); }

// Координаты
int ClientOriginX(Window* w);         // (0,0) клиентской области на экране
int ClientOriginY(Window* w);
Rect ScreenRect(Window* w);           // прямоугольник окна на экране
Rect ScreenClientRect(Window* w);
bool IsVisibleChain(Window* w);       // окно и все его предки видимы

// Неклиентская область по стилю: отступы клиентской области от краёв окна
void NonClientInsets(uint32_t style, uint32_t exStyle, bool hasMenu, Rect& insets);
int BorderWidth(uint32_t style, uint32_t exStyle);

// Окно под точкой экрана (самое глубокое видимое)
Window* WindowFromPoint(int x, int y, bool skipDisabled = true);

// Отрисовка
void InvalidateClient(Window* w, const Region& rgnClient, bool erase, bool children);
void InvalidateScreen(const Rect& screenRect);   // открылась область экрана (окно скрыто/сдвинуто)
void RepaintDesktop(const Region& screenRgn);
void DrawNonClient(Window* w);
bool NeedsPaint(Window* w);

// Сообщения
uint32_t SendMsg(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool fromUnicode = true);
uint32_t CallProc(uint32_t proc, bool procUnicode, uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool fromUnicode);
void PostMsg(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);
uint32_t DefWindowProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode);

// Встроенные процедуры (зарегистрированы как host-функции)
void RegisterHostWndProc(uint32_t address, HostWndProc fn, bool unicode);
HostWndProc FindHostWndProc(uint32_t address, bool& unicode);

// Окна
uint32_t CreateWindowInternal(uint32_t exStyle, const std::u16string& className, uint16_t classAtom,
                              const std::u16string& title, uint32_t style, int x, int y, int w, int h,
                              uint32_t parent, uint32_t menu, uint32_t hInstance, uint32_t param, bool unicode,
                              uint32_t titlePtr, uint32_t classPtr);
bool DestroyWindowInternal(uint32_t hwnd);
bool SetWindowPosInternal(uint32_t hwnd, uint32_t after, int x, int y, int cx, int cy, uint32_t flags);
bool ShowWindowInternal(uint32_t hwnd, int cmd);
void SetActive(uint32_t hwnd);
uint32_t SetFocusInternal(uint32_t hwnd);
WndClass* FindClass(const std::u16string& name, uint16_t atom, uint32_t hInstance);
uint32_t RegisterClassInternal(const WndClass& wc);

// Кисть системного цвета (GetSysColorBrush), не удаляется
uint32_t SysColorBrush(int index);
void RegisterSystemClasses();
void DrawScrollBars(Window* w, uint32_t hdc);
void DrawMenuBarInto(Window* w, uint32_t hdc, const Rect& bar);

// Выборка сообщения из очереди (GetMessage/PeekMessage)
bool FetchMessage(Msg& out, uint32_t hwnd, uint32_t min, uint32_t max, bool remove, bool wait);
// TranslateMessage: WM_KEYDOWN -> WM_CHAR
bool TranslateKey(const Msg& m);

// Время и вывод на экран
uint32_t NowMs();
void PresentIfNeeded(bool force = false);

// Строки гостя по флагу кодировки
std::u16string ReadText(uint32_t ptr, bool unicode);
uint32_t WriteText(uint32_t ptr, const std::u16string& s, uint32_t cap, bool unicode);
// Место на стеке гостя под временные структуры (для сообщений с указателями)
class StackBuffer {
public:
    explicit StackBuffer(uint32_t size);
    ~StackBuffer();
    uint32_t addr() const { return addr_; }
private:
    uint32_t savedEsp_, addr_;
};

// Хост-платформа: ввод и вывод кадра (фронтенд Switch, winrun)
struct InputEvent {
    enum Type { MouseMove, MouseDown, MouseUp, Wheel, KeyDown, KeyUp, Char, Quit } type = MouseMove;
    int x = 0, y = 0;
    int button = 0;   // 0 левая, 1 правая, 2 средняя
    int wheel = 0;
    uint32_t vk = 0;
    uint32_t ch = 0;
};
struct Platform {
    // Ждать ввода не дольше timeoutMs (<0 — без ограничения); события — в out.
    // false: ввода больше не будет (головной режим без сценария).
    bool (*wait)(int timeoutMs, std::vector<InputEvent>& out) = nullptr;
    // Экран изменился: показать
    void (*present)(const ui::Surface& screen) = nullptr;
};
extern Platform g_platform;

// Включает слой "ui2" (вызывать до загрузки EXE). screenW/H — размер экрана.
void EnableUi(int screenW = 800, int screenH = 600);

} // namespace user
} // namespace win32
