// linux/win32_hal/src/X11GuiHal.cpp
// Native Linux X11 Desktop Windowing, Event/Timer Loop, Inline EDIT Controls, Popup Menus,
// Clipboard, Zenity File/Folder Dialogs & Shell PIDL/IDataObject Drag-and-Drop HAL.
//
// Dynamically loads libX11.so.6 via dlopen() at runtime when $DISPLAY is present, with seamless
// in-memory windowing/message-loop fallback in headless CLI / --self-test / -m32 environments.

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "PathUtils.h"
#include "ToolProcess.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>

#ifndef SIZE_RESTORED
#define SIZE_RESTORED 0
#endif
#ifndef SIZE_MINIMIZED
#define SIZE_MINIMIZED 1
#endif
#ifndef DV_E_FORMATETC
#define DV_E_FORMATETC (static_cast<HRESULT>(0x80040064u))
#endif
#ifndef MB_ABORTRETRYIGNORE
#define MB_ABORTRETRYIGNORE 0x00000002u
#endif
#ifndef MB_RETRYCANCEL
#define MB_RETRYCANCEL 0x00000005u
#endif
#ifndef VK_OEM_1
#define VK_OEM_1 0xBA
#endif
#ifndef VK_OEM_3
#define VK_OEM_3 0xC0
#endif
#ifndef VK_OEM_4
#define VK_OEM_4 0xDB
#endif
#ifndef VK_OEM_5
#define VK_OEM_5 0xDC
#endif
#ifndef VK_OEM_6
#define VK_OEM_6 0xDD
#endif
#ifndef VK_OEM_7
#define VK_OEM_7 0xDE
#endif
#ifndef VK_OEM_102
#define VK_OEM_102 0xE2
#endif
#ifndef VK_SEPARATOR
#define VK_SEPARATOR 0x6C
#endif
#ifndef VK_DECIMAL
#define VK_DECIMAL 0x6E
#endif
#ifndef VK_DIVIDE
#define VK_DIVIDE 0x6F
#endif
#ifndef VK_SNAPSHOT
#define VK_SNAPSHOT 0x2C
#endif
#ifndef VK_NUMLOCK
#define VK_NUMLOCK 0x90
#endif
#ifndef VK_SCROLL
#define VK_SCROLL 0x91
#endif

// MSG::lPrivate (HAL only): the modifier keys as they were when the input event happened, applied to GetKeyState()
// when the message is taken from the queue (a slow paint must not turn Ctrl+C into a plain C), and keyboard messages
// that go to whichever window has the focus when they are taken (keys typed right after F2 belong in the rename box)
constexpr DWORD kMsgHasKeyState = 0x80000000u;
constexpr DWORD kMsgToFocus     = 0x40000000u;
constexpr DWORD kMsgShift       = 0x00000001u;
constexpr DWORD kMsgControl     = 0x00000002u;
constexpr DWORD kMsgAlt         = 0x00000004u;

namespace Explorer2::Hal {
void PresentHwndFramebuffer(HWND hwnd, const uint8_t* bgra, uint32_t w, uint32_t h);
} // namespace Explorer2::Hal

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <deque>
#include <dlfcn.h>
#include <fcntl.h>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

bool IsCliHeadlessMode() {
    static const bool s_headless = []() {
        const char* h = std::getenv("EXPLORER2_HEADLESS");
        if (h && *h == '1') return true;
        const int fd = ::open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;
        char buf[4096] = {};
        const ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
        ::close(fd);
        if (n <= 0) return false;
        size_t pos = 0;
        while (pos < static_cast<size_t>(n)) {
            const char* arg = buf + pos;
            const size_t len = std::strlen(arg);
            if (std::strcmp(arg, "--self-test") == 0 ||
                std::strcmp(arg, "--version") == 0 ||
                std::strcmp(arg, "-v") == 0 ||
                std::strcmp(arg, "--inspect-files") == 0 ||
                std::strcmp(arg, "--dump-hw") == 0 ||
                std::strcmp(arg, "--monitor-daemon") == 0) {
                return true;
            }
            pos += len + 1;
        }
        return false;
    }();
    return s_headless;
}

// Fast bounded pre-flight check before XOpenDisplay() so a stale WSL2 TCP DISPLAY=172.x.x.1:0 never hangs 60s
bool IsX11DisplayReachableFast(const char* disp) {
    if (!disp || !*disp) return false;
    const char* colon = std::strchr(disp, ':');
    if (!colon) return false;
    const int dpyNum = std::atoi(colon + 1);
    if (dpyNum < 0 || dpyNum > 999) return false;

    if (colon == disp || std::strncmp(disp, "unix:", 5) == 0) {
        // A local display never blocks: XOpenDisplay fails at once without a server. The socket file may also be
        // missing while the server listens on the abstract socket (a private /tmp), so XOpenDisplay always tries
        return true;
    }

    // Remote/TCP DISPLAY=host:N -> bounded 250ms non-blocking connect probe
    const std::string host(disp, static_cast<size_t>(colon - disp));
    const std::string port = std::to_string(6000 + dpyNum);
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) return false;

    bool reachable = false;
    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
        const int s = ::socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, ai->ai_protocol);
        if (s < 0) continue;
        int rc = ::connect(s, ai->ai_addr, ai->ai_addrlen);
        if (rc == 0) {
            reachable = true;
        } else if (errno == EINPROGRESS) {
            struct pollfd pfd = { s, POLLOUT, 0 };
            if (::poll(&pfd, 1, 250) > 0 && (pfd.revents & POLLOUT)) {
                int soErr = 0;
                socklen_t slen = sizeof(soErr);
                if (::getsockopt(s, SOL_SOCKET, SO_ERROR, &soErr, &slen) == 0 && soErr == 0) reachable = true;
            }
        }
        ::close(s);
        if (reachable) break;
    }
    ::freeaddrinfo(res);
    return reachable;
}

// ==============================================================================================================
// 1. Dynamic libX11.so.6 Function Table (zero link-time .so dependency; works on both x86_64 and -m32)
// ==============================================================================================================
struct X11Lib {
    void* handle = nullptr;
    bool tried = false;
    bool ready = false;

    Display* (*OpenDisplay)(const char*) = nullptr;
    int      (*CloseDisplay)(Display*) = nullptr;
    Window   (*CreateSimpleWindow)(Display*, Window, int, int, unsigned int, unsigned int, unsigned int, unsigned long, unsigned long) = nullptr;
    int      (*DestroyWindow)(Display*, Window) = nullptr;
    int      (*MapWindow)(Display*, Window) = nullptr;
    int      (*UnmapWindow)(Display*, Window) = nullptr;
    int      (*MoveResizeWindow)(Display*, Window, int, int, unsigned int, unsigned int) = nullptr;
    int      (*StoreName)(Display*, Window, const char*) = nullptr;
    int      (*ChangeProperty)(Display*, Window, Atom, Atom, int, int, const unsigned char*, int) = nullptr;
    Atom     (*InternAtom)(Display*, const char*, Bool) = nullptr;
    Status   (*SetWMProtocols)(Display*, Window, Atom*, int) = nullptr;
    int      (*SelectInput)(Display*, Window, long) = nullptr;
    int      (*Pending)(Display*) = nullptr;
    int      (*NextEvent)(Display*, XEvent*) = nullptr;
    GC       (*CreateGC)(Display*, Drawable, unsigned long, XGCValues*) = nullptr;
    int      (*FreeGC)(Display*, GC) = nullptr;
    XImage*  (*CreateImage)(Display*, Visual*, unsigned int, int, int, char*, unsigned int, unsigned int, int, int) = nullptr;
    int      (*PutImage)(Display*, Drawable, GC, XImage*, int, int, int, int, unsigned int, unsigned int) = nullptr;
    Pixmap   (*CreatePixmap)(Display*, Drawable, unsigned int, unsigned int, unsigned int) = nullptr;
    int      (*FreePixmap)(Display*, Pixmap) = nullptr;
    int      (*CopyArea)(Display*, Drawable, Drawable, GC, int, int, unsigned int, unsigned int, int, int) = nullptr;
    int      (*Flush)(Display*) = nullptr;
    int      (*LookupString)(XKeyEvent*, char*, int, KeySym*, XComposeStatus*) = nullptr;
    Bool     (*TranslateCoordinates)(Display*, Window, Window, int, int, int*, int*, Window*) = nullptr;
    int      (*ChangeWindowAttributes)(Display*, Window, unsigned long, XSetWindowAttributes*) = nullptr;
    int      (*MapRaised)(Display*, Window) = nullptr;
    int      (*GrabPointer)(Display*, Window, Bool, unsigned int, int, int, Window, Cursor, Time) = nullptr;
    int      (*UngrabPointer)(Display*, Time) = nullptr;
    Status   (*InitThreads)() = nullptr;
    XErrorHandler   (*SetErrorHandler)(XErrorHandler) = nullptr;
    XIOErrorHandler (*SetIOErrorHandler)(XIOErrorHandler) = nullptr;
    int      (*GetWindowProperty)(Display*, Window, Atom, long, long, Bool, Atom, Atom*, int*, unsigned long*, unsigned long*, unsigned char**) = nullptr;
    int      (*Free)(void*) = nullptr;
    Status   (*SendEvent)(Display*, Window, Bool, long, XEvent*) = nullptr;
    void     (*SetWMNormalHints)(Display*, Window, XSizeHints*) = nullptr;
    int      (*SetTransientForHint)(Display*, Window, Window) = nullptr;
    int      (*SetClassHint)(Display*, Window, XClassHint*) = nullptr;
    KeySym   (*LookupKeysym)(XKeyEvent*, int) = nullptr;
    int      (*RaiseWindow)(Display*, Window) = nullptr;
    int      (*SetSelectionOwner)(Display*, Atom, Window, Time) = nullptr;
    Window   (*GetSelectionOwner)(Display*, Atom) = nullptr;
    int      (*ConvertSelection)(Display*, Atom, Atom, Atom, Window, Time) = nullptr;
    int      (*DeleteProperty)(Display*, Window, Atom) = nullptr;
    Cursor   (*CreateFontCursor)(Display*, unsigned int) = nullptr;
    int      (*DefineCursor)(Display*, Window, Cursor) = nullptr;

    Display* dpy = nullptr;
    int screen = 0;
    Window root = 0;
    Atom wmDeleteWindow = 0;
    Atom wmProtocols = 0;
    Atom netWmPing = 0;
    Atom netWmName = 0;
    Atom utf8String = 0;
    Atom netWmState = 0;
    Atom netWmStateFullscreen = 0;
    Atom netWmStateMaxVert = 0;
    Atom netWmStateMaxHorz = 0;
    Atom netWorkArea = 0;
    Atom netActiveWindow = 0;
    Atom netWmWindowType = 0;
    Atom netWmWindowTypeDialog = 0;
    Atom netWmPid = 0;
    Atom clipboard = 0;
    Atom targets = 0;
    Atom textPlainUtf8 = 0;
    Atom uriList = 0;
    Atom gnomeCopiedFiles = 0;
    Atom halSelection = 0;          // property our own clipboard requests are delivered to

    bool EnsureConnected() {
        if (tried) return ready && dpy != nullptr;
        tried = true;
        if (IsCliHeadlessMode()) return false;
        const char* dispEnv = std::getenv("DISPLAY");
        if (!IsX11DisplayReachableFast(dispEnv)) return false;

#ifdef E2_STATIC_X11
        OpenDisplay            = &::XOpenDisplay;
        CloseDisplay           = &::XCloseDisplay;
        CreateSimpleWindow     = &::XCreateSimpleWindow;
        DestroyWindow          = &::XDestroyWindow;
        MapWindow              = &::XMapWindow;
        UnmapWindow            = &::XUnmapWindow;
        MoveResizeWindow       = &::XMoveResizeWindow;
        StoreName              = &::XStoreName;
        ChangeProperty         = &::XChangeProperty;
        InternAtom             = &::XInternAtom;
        SetWMProtocols         = &::XSetWMProtocols;
        SelectInput            = &::XSelectInput;
        Pending                = &::XPending;
        NextEvent              = &::XNextEvent;
        CreateGC               = &::XCreateGC;
        FreeGC                 = &::XFreeGC;
        CreateImage            = &::XCreateImage;
        PutImage               = &::XPutImage;
        CreatePixmap           = &::XCreatePixmap;
        FreePixmap             = &::XFreePixmap;
        CopyArea               = &::XCopyArea;
        Flush                  = &::XFlush;
        LookupString           = &::XLookupString;
        TranslateCoordinates   = &::XTranslateCoordinates;
        ChangeWindowAttributes = &::XChangeWindowAttributes;
        MapRaised              = &::XMapRaised;
        GrabPointer            = &::XGrabPointer;
        UngrabPointer          = &::XUngrabPointer;
        InitThreads            = &::XInitThreads;
        SetErrorHandler        = &::XSetErrorHandler;
        SetIOErrorHandler      = &::XSetIOErrorHandler;
        GetWindowProperty      = &::XGetWindowProperty;
        Free                   = &::XFree;
        SendEvent              = &::XSendEvent;
        SetWMNormalHints       = &::XSetWMNormalHints;
        SetTransientForHint    = &::XSetTransientForHint;
        SetClassHint           = &::XSetClassHint;
        LookupKeysym           = &::XLookupKeysym;
        RaiseWindow            = &::XRaiseWindow;
        SetSelectionOwner      = &::XSetSelectionOwner;
        GetSelectionOwner      = &::XGetSelectionOwner;
        ConvertSelection       = &::XConvertSelection;
        DeleteProperty         = &::XDeleteProperty;
        CreateFontCursor       = &::XCreateFontCursor;
        DefineCursor           = &::XDefineCursor;
#else
        handle = ::dlopen("libX11.so.6", RTLD_LAZY);
        if (!handle) handle = ::dlopen("libX11.so", RTLD_LAZY);
        if (!handle) return false;

        auto sym = [&](const char* name) { return ::dlsym(handle, name); };
        OpenDisplay          = reinterpret_cast<decltype(OpenDisplay)>(sym("XOpenDisplay"));
        CloseDisplay         = reinterpret_cast<decltype(CloseDisplay)>(sym("XCloseDisplay"));
        CreateSimpleWindow   = reinterpret_cast<decltype(CreateSimpleWindow)>(sym("XCreateSimpleWindow"));
        DestroyWindow        = reinterpret_cast<decltype(DestroyWindow)>(sym("XDestroyWindow"));
        MapWindow            = reinterpret_cast<decltype(MapWindow)>(sym("XMapWindow"));
        UnmapWindow          = reinterpret_cast<decltype(UnmapWindow)>(sym("XUnmapWindow"));
        MoveResizeWindow     = reinterpret_cast<decltype(MoveResizeWindow)>(sym("XMoveResizeWindow"));
        StoreName            = reinterpret_cast<decltype(StoreName)>(sym("XStoreName"));
        ChangeProperty       = reinterpret_cast<decltype(ChangeProperty)>(sym("XChangeProperty"));
        InternAtom           = reinterpret_cast<decltype(InternAtom)>(sym("XInternAtom"));
        SetWMProtocols       = reinterpret_cast<decltype(SetWMProtocols)>(sym("XSetWMProtocols"));
        SelectInput          = reinterpret_cast<decltype(SelectInput)>(sym("XSelectInput"));
        Pending              = reinterpret_cast<decltype(Pending)>(sym("XPending"));
        NextEvent            = reinterpret_cast<decltype(NextEvent)>(sym("XNextEvent"));
        CreateGC             = reinterpret_cast<decltype(CreateGC)>(sym("XCreateGC"));
        FreeGC               = reinterpret_cast<decltype(FreeGC)>(sym("XFreeGC"));
        CreateImage          = reinterpret_cast<decltype(CreateImage)>(sym("XCreateImage"));
        PutImage             = reinterpret_cast<decltype(PutImage)>(sym("XPutImage"));
        CreatePixmap         = reinterpret_cast<decltype(CreatePixmap)>(sym("XCreatePixmap"));
        FreePixmap           = reinterpret_cast<decltype(FreePixmap)>(sym("XFreePixmap"));
        CopyArea             = reinterpret_cast<decltype(CopyArea)>(sym("XCopyArea"));
        Flush                = reinterpret_cast<decltype(Flush)>(sym("XFlush"));
        LookupString         = reinterpret_cast<decltype(LookupString)>(sym("XLookupString"));
        TranslateCoordinates = reinterpret_cast<decltype(TranslateCoordinates)>(sym("XTranslateCoordinates"));
        ChangeWindowAttributes = reinterpret_cast<decltype(ChangeWindowAttributes)>(sym("XChangeWindowAttributes"));
        MapRaised            = reinterpret_cast<decltype(MapRaised)>(sym("XMapRaised"));
        GrabPointer          = reinterpret_cast<decltype(GrabPointer)>(sym("XGrabPointer"));
        UngrabPointer        = reinterpret_cast<decltype(UngrabPointer)>(sym("XUngrabPointer"));
        InitThreads          = reinterpret_cast<decltype(InitThreads)>(sym("XInitThreads"));
        SetErrorHandler      = reinterpret_cast<decltype(SetErrorHandler)>(sym("XSetErrorHandler"));
        SetIOErrorHandler    = reinterpret_cast<decltype(SetIOErrorHandler)>(sym("XSetIOErrorHandler"));
        GetWindowProperty    = reinterpret_cast<decltype(GetWindowProperty)>(sym("XGetWindowProperty"));
        Free                 = reinterpret_cast<decltype(Free)>(sym("XFree"));
        SendEvent            = reinterpret_cast<decltype(SendEvent)>(sym("XSendEvent"));
        SetWMNormalHints     = reinterpret_cast<decltype(SetWMNormalHints)>(sym("XSetWMNormalHints"));
        SetTransientForHint  = reinterpret_cast<decltype(SetTransientForHint)>(sym("XSetTransientForHint"));
        SetClassHint         = reinterpret_cast<decltype(SetClassHint)>(sym("XSetClassHint"));
        LookupKeysym         = reinterpret_cast<decltype(LookupKeysym)>(sym("XLookupKeysym"));
        RaiseWindow          = reinterpret_cast<decltype(RaiseWindow)>(sym("XRaiseWindow"));
        SetSelectionOwner    = reinterpret_cast<decltype(SetSelectionOwner)>(sym("XSetSelectionOwner"));
        GetSelectionOwner    = reinterpret_cast<decltype(GetSelectionOwner)>(sym("XGetSelectionOwner"));
        ConvertSelection     = reinterpret_cast<decltype(ConvertSelection)>(sym("XConvertSelection"));
        DeleteProperty       = reinterpret_cast<decltype(DeleteProperty)>(sym("XDeleteProperty"));
        CreateFontCursor     = reinterpret_cast<decltype(CreateFontCursor)>(sym("XCreateFontCursor"));
        DefineCursor         = reinterpret_cast<decltype(DefineCursor)>(sym("XDefineCursor"));
#endif

        if (!OpenDisplay || !CreateSimpleWindow || !MapWindow || !Pending || !NextEvent || !CreateGC || !CreateImage || !PutImage || !Flush) {
            return false;
        }
        // Thread-safe Xlib (libX11 >= 1.8 does this itself); must come before any other Xlib call
        if (InitThreads) InitThreads();
        dpy = OpenDisplay(nullptr);
        if (!dpy) return false;
        // Xlib's default handlers end the process: a protocol error is logged and ignored instead, and a lost display
        // (logout, XWayland restart) saves the settings before the process ends
        if (SetErrorHandler) SetErrorHandler(&NonFatalXErrorHandler);
        if (SetIOErrorHandler) SetIOErrorHandler(&DisplayLostHandler);
        screen = DefaultScreen(dpy);
        root   = RootWindow(dpy, screen);
        if (InternAtom) {
            wmDeleteWindow        = InternAtom(dpy, "WM_DELETE_WINDOW", False);
            wmProtocols           = InternAtom(dpy, "WM_PROTOCOLS", False);
            netWmPing             = InternAtom(dpy, "_NET_WM_PING", False);
            netWmName             = InternAtom(dpy, "_NET_WM_NAME", False);
            utf8String            = InternAtom(dpy, "UTF8_STRING", False);
            netWmState            = InternAtom(dpy, "_NET_WM_STATE", False);
            netWmStateFullscreen  = InternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
            netWmStateMaxVert     = InternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
            netWmStateMaxHorz     = InternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
            netWorkArea           = InternAtom(dpy, "_NET_WORKAREA", False);
            netActiveWindow       = InternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
            netWmWindowType       = InternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
            netWmWindowTypeDialog = InternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
            netWmPid              = InternAtom(dpy, "_NET_WM_PID", False);
            clipboard             = InternAtom(dpy, "CLIPBOARD", False);
            targets               = InternAtom(dpy, "TARGETS", False);
            textPlainUtf8         = InternAtom(dpy, "text/plain;charset=utf-8", False);
            uriList               = InternAtom(dpy, "text/uri-list", False);
            gnomeCopiedFiles      = InternAtom(dpy, "x-special/gnome-copied-files", False);
            halSelection          = InternAtom(dpy, "EXPLORER2_SELECTION", False);
        }
        ready = true;
        return true;
    }

    static int NonFatalXErrorHandler(Display*, XErrorEvent* e);
    static int DisplayLostHandler(Display*);
};

X11Lib& X11() {
    static X11Lib s_x11;
    return s_x11;
}

// ==============================================================================================================
// 2. Window, Control, Timer & Message Queue State
// ==============================================================================================================
struct SubclassRec {
    SUBCLASSPROC proc = nullptr;
    UINT_PTR id = 0;
    DWORD_PTR refData = 0;
};

struct WndRecord {
    HWND hwnd = nullptr;
    HWND parent = nullptr;          // child windows: the parent; top-level windows: the owner (dialogs) or null
    std::wstring className;
    std::wstring text;
    DWORD style = 0;
    DWORD exStyle = 0;
    int x = 0, y = 0;               // top level: screen position; child: position in the parent's client area
    int width = 1240, height = 760;
    int ctrlId = 0;
    HINSTANCE hInst = nullptr;
    WNDPROC wndProc = nullptr;
    LONG_PTR userData = 0;
    bool visible = false;
    bool enabled = true;
    bool needsPaint = false;
    bool topLevel = false;          // not WS_CHILD: its own X window when a display is open
    bool isDialog = false;          // owned top-level window: transient for its owner's X window, drawn by the HAL
    bool isEdit = false;
    bool isButton = false;
    bool isStatic = false;
    bool xmapped = false;           // XMapWindow done: later state changes go to the window manager as messages
    bool iconic = false;            // unmapped by the window manager (minimized): no repainting
    bool fullscreen = false;
    bool pressed = false;           // BUTTON: the left button is held on it
    bool hot = false;               // BUTTON: the pointer is over it
    UINT staticIcon = 0;            // STATIC made by MessageBoxW to show its icon (MB_ICONxxx)
    float scrollY = 0.0f;           // STATIC: text scrolled up by this much (a message taller than the screen)
    float contentH = 0.0f;          // STATIC: height of its whole text, measured when drawn
    float editScrollX = 0.0f;       // EDIT: horizontal scroll of the last drawn frame
    HWND focusChild = nullptr;      // top level: the child that had the keyboard focus when the window lost it
    HWND hotChild = nullptr;        // top level: the BUTTON under the pointer
    int msgBoxBandTop = 0;          // MessageBoxW: where the grey button band starts
    std::vector<long> pendingNetStates;   // _NET_WM_STATE atoms for the first map
    std::vector<SubclassRec> subclasses;  // SetWindowSubclass chain, last one runs first
    size_t caretPos = 0;
    size_t selStart = 0;
    Window xwin = 0;
    GC xgc = nullptr;
    Pixmap xpixmap = 0;
    uint32_t pixW = 0;
    uint32_t pixH = 0;
    ULONGLONG shownAtTick = 0;
    uint32_t paintedFrames = 0;
    std::vector<uint8_t> lastFrame;
    std::vector<uint8_t> prevSentFrame;
};

struct TimerKey {
    HWND hwnd = nullptr;
    UINT_PTR id = 0;
    bool operator<(const TimerKey& o) const {
        if (hwnd != o.hwnd) return hwnd < o.hwnd;
        return id < o.id;
    }
};

struct TimerEntry {
    UINT intervalMs = 100;
    ULONGLONG nextFireTick = 0;
    TIMERPROC proc = nullptr;
};

std::recursive_mutex g_guiMutex;
std::map<std::wstring, WNDCLASSEXW> g_wndClasses;
std::map<HWND, std::shared_ptr<WndRecord>> g_windows;
std::map<Window, HWND> g_xwinToHwnd;
std::map<TimerKey, TimerEntry> g_timers;
intptr_t g_nextHwndId = 0x20000;

HWND g_focusedHwnd = nullptr;
HWND g_captureHwnd = nullptr;
POINT g_cursorScreen = { 100, 100 };
SHORT g_keyStates[256] = {};        // GetKeyState: as of the input message being handled
SHORT g_liveKeyStates[256] = {};    // GetAsyncKeyState: as of the last X event read
Time g_lastLClickTime = 0;          // X server time of the last left press (double clicks)
Time g_lastButtonPressTime = 0;     // X server time of the last press of any button (the one that opened a menu)
bool g_lastLClickValid = false;
int g_lastLClickX = -1000, g_lastLClickY = -1000;
std::thread::id g_uiThread;         // the thread that created the first top-level window
bool g_uiThreadKnown = false;

std::mutex g_msgMutex;
std::condition_variable g_msgCv;
std::deque<MSG> g_msgQueue;

bool MsgMatches(const MSG& m, HWND hWnd, UINT fMin, UINT fMax) {
    if (hWnd != nullptr && m.hwnd != hWnd) return false;
    if (fMin == 0 && fMax == 0) return true;
    return m.message >= fMin && m.message <= fMax;
}

std::shared_ptr<WndRecord> FindWnd(HWND h) {
    std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
    auto it = g_windows.find(h);
    return (it != g_windows.end()) ? it->second : nullptr;
}

// The top-level window a window belongs to (itself for a top-level window)
HWND TopLevelOf(HWND h) {
    auto r = FindWnd(h);
    for (int guard = 0; r && !r->topLevel && guard < 32; ++guard) {
        if (!r->parent || r->parent == HWND_MESSAGE) break;
        auto p = FindWnd(r->parent);
        if (!p) break;
        r = p;
    }
    return r ? r->hwnd : nullptr;
}

std::vector<std::shared_ptr<WndRecord>> ChildrenOf(HWND parent) {
    std::vector<std::shared_ptr<WndRecord>> out;
    std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
    for (const auto& [h, r] : g_windows) {
        if (r->parent == parent && !r->topLevel) out.push_back(r);   // HWNDs grow: creation (tab) order
    }
    return out;
}

// The visible child control under a point of a top-level window's client area (the last created wins)
std::shared_ptr<WndRecord> ChildControlAt(HWND top, int x, int y) {
    std::shared_ptr<WndRecord> hit;
    for (const auto& ch : ChildrenOf(top)) {
        if (ch->visible && x >= ch->x && x < ch->x + ch->width && y >= ch->y && y < ch->y + ch->height) hit = ch;
    }
    return hit;
}

// Focus changes as Win32 makes them: WM_KILLFOCUS / WM_SETFOCUS, and an EDIT tells its parent EN_KILLFOCUS /
// EN_SETFOCUS (the app ends an inline rename or the address-bar editing on EN_KILLFOCUS)
void ChangeFocus(HWND newFocus) {
    const HWND old = g_focusedHwnd;
    if (old == newFocus) return;
    g_focusedHwnd = newFocus;
    if (auto r = FindWnd(old)) {
        SendMessageW(old, WM_KILLFOCUS, reinterpret_cast<WPARAM>(newFocus), 0);
        if (r->isEdit && r->parent && r->parent != HWND_MESSAGE) {
            SendMessageW(r->parent, WM_COMMAND, MAKEWPARAM(static_cast<WORD>(r->ctrlId), EN_KILLFOCUS), reinterpret_cast<LPARAM>(old));
        }
        if (r->parent && r->parent != HWND_MESSAGE && !r->topLevel) InvalidateRect(r->parent, nullptr, FALSE);
    }
    if (g_focusedHwnd != newFocus) return;      // an EN_KILLFOCUS handler moved the focus itself
    if (auto r = FindWnd(newFocus)) {
        SendMessageW(newFocus, WM_SETFOCUS, reinterpret_cast<WPARAM>(old), 0);
        if (r->isEdit && r->parent && r->parent != HWND_MESSAGE) {
            SendMessageW(r->parent, WM_COMMAND, MAKEWPARAM(static_cast<WORD>(r->ctrlId), EN_SETFOCUS), reinterpret_cast<LPARAM>(newFocus));
        }
        InvalidateRect(r->topLevel ? newFocus : r->parent, nullptr, FALSE);
    }
}

// Where the keyboard input of a top-level window goes: its focused (visible) child, or the window itself
HWND KeyTargetFor(HWND top) {
    const HWND f = g_focusedHwnd;
    if (f && f != top) {
        auto r = FindWnd(f);
        if (r && r->visible && TopLevelOf(f) == top) return f;
    }
    return top;
}

// Shift / Ctrl / Alt from an X event's state (the state before the event itself)
DWORD ModifiersFromXState(unsigned int state) {
    DWORD m = 0;
    if (state & ShiftMask)   m |= kMsgShift;
    if (state & ControlMask) m |= kMsgControl;
    if (state & Mod1Mask)    m |= kMsgAlt;
    return m;
}

void ApplyModifierSnapshot(SHORT* states, DWORD mods) {
    states[VK_SHIFT]   = (mods & kMsgShift)   ? static_cast<SHORT>(0x8000) : 0;
    states[VK_CONTROL] = (mods & kMsgControl) ? static_cast<SHORT>(0x8000) : 0;
    states[VK_MENU]    = (mods & kMsgAlt)     ? static_cast<SHORT>(0x8000) : 0;
}

// Runs a window's procedure through its SetWindowSubclass chain (the last subclass first)
thread_local std::vector<std::pair<HWND, size_t>> t_subclassFrames;

LRESULT CallWindowChain(const std::shared_ptr<WndRecord>& r, HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!r) return 0;
    if (r->subclasses.empty()) return r->wndProc ? r->wndProc(h, msg, wParam, lParam) : DefWindowProcW(h, msg, wParam, lParam);
    const size_t idx = r->subclasses.size() - 1;
    const SubclassRec sc = r->subclasses[idx];
    t_subclassFrames.push_back({ h, idx });
    const LRESULT res = sc.proc(h, msg, wParam, lParam, sc.id, sc.refData);
    t_subclassFrames.pop_back();
    return res;
}

// The desktop's work area (screen minus panels and docks) from _NET_WORKAREA, read again at most once a second
RECT X11WorkArea() {
    auto& x = X11();
    if (!x.ready || !x.dpy) return { 0, 0, 1920, 1040 };
    const int sw = std::max(320, DisplayWidth(x.dpy, x.screen));
    const int sh = std::max(240, DisplayHeight(x.dpy, x.screen));
    static std::mutex s_mutex;
    static ULONGLONG s_tick = 0;
    static RECT s_cached = {};
    std::lock_guard<std::mutex> lk(s_mutex);
    const ULONGLONG now = GetTickCount64();
    if (s_tick != 0 && now - s_tick < 1000) return s_cached;
    RECT rc = { 0, 0, sw, sh };
    if (x.GetWindowProperty && x.netWorkArea) {
        Atom type = 0;
        int format = 0;
        unsigned long count = 0, after = 0;
        unsigned char* data = nullptr;
        if (x.GetWindowProperty(x.dpy, x.root, x.netWorkArea, 0, 4, False, XA_CARDINAL, &type, &format, &count, &after, &data) == Success &&
            data && type == XA_CARDINAL && format == 32 && count >= 4) {
            const long* v = reinterpret_cast<const long*>(data);     // format-32 properties come as an array of long
            if (v[2] >= 200 && v[3] >= 150 && v[0] >= 0 && v[1] >= 0 && v[0] + v[2] <= sw && v[1] + v[3] <= sh) {
                rc = { static_cast<LONG>(v[0]), static_cast<LONG>(v[1]), static_cast<LONG>(v[0] + v[2]), static_cast<LONG>(v[1] + v[3]) };
            }
        }
        if (data && x.Free) x.Free(data);
    }
    s_cached = rc;
    s_tick = now;
    return rc;
}

// Adds or removes window-manager states (maximized, full screen): a property before the first map, a request to the
// window manager after it
void SetNetWmState(WndRecord& rec, bool add, Atom a1, Atom a2 = 0) {
    auto& x = X11();
    if (!x.ready || !x.dpy || rec.xwin == 0 || !x.netWmState || !a1) return;
    if (!rec.xmapped) {
        for (Atom a : { a1, a2 }) {
            if (!a) continue;
            auto it = std::find(rec.pendingNetStates.begin(), rec.pendingNetStates.end(), static_cast<long>(a));
            if (add && it == rec.pendingNetStates.end()) rec.pendingNetStates.push_back(static_cast<long>(a));
            if (!add && it != rec.pendingNetStates.end()) rec.pendingNetStates.erase(it);
        }
        if (x.ChangeProperty) {
            x.ChangeProperty(x.dpy, rec.xwin, x.netWmState, XA_ATOM, 32, PropModeReplace,
                             reinterpret_cast<const unsigned char*>(rec.pendingNetStates.data()),
                             static_cast<int>(rec.pendingNetStates.size()));
        }
    } else if (x.SendEvent) {
        XEvent e = {};
        e.xclient.type = ClientMessage;
        e.xclient.send_event = True;
        e.xclient.display = x.dpy;
        e.xclient.window = rec.xwin;
        e.xclient.message_type = x.netWmState;
        e.xclient.format = 32;
        e.xclient.data.l[0] = add ? 1 : 0;          // _NET_WM_STATE_ADD / _NET_WM_STATE_REMOVE
        e.xclient.data.l[1] = static_cast<long>(a1);
        e.xclient.data.l[2] = static_cast<long>(a2);
        e.xclient.data.l[3] = 1;                    // the request comes from an application
        x.SendEvent(x.dpy, x.root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    }
    x.Flush(x.dpy);
}

// Raises a top-level window and asks the window manager to give it the focus
void ActivateX11Window(const WndRecord& rec) {
    auto& x = X11();
    if (!x.ready || !x.dpy || rec.xwin == 0) return;
    if (x.RaiseWindow) x.RaiseWindow(x.dpy, rec.xwin);
    if (x.SendEvent && x.netActiveWindow && rec.xmapped) {
        XEvent e = {};
        e.xclient.type = ClientMessage;
        e.xclient.send_event = True;
        e.xclient.display = x.dpy;
        e.xclient.window = rec.xwin;
        e.xclient.message_type = x.netActiveWindow;
        e.xclient.format = 32;
        e.xclient.data.l[0] = 1;                    // from an application
        e.xclient.data.l[1] = CurrentTime;
        x.SendEvent(x.dpy, x.root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    }
    x.Flush(x.dpy);
}

// A disabled window (the owner of an open dialog) takes no input: a click on it brings the dialog to the front
void RaiseModalDialogOf(HWND owner) {
    std::shared_ptr<WndRecord> dlg;
    {
        std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
        for (const auto& [h, r] : g_windows) {
            if (r->topLevel && r->isDialog && r->parent == owner && r->visible && r->enabled) dlg = r;
        }
    }
    if (dlg) ActivateX11Window(*dlg);
}

int X11Lib::NonFatalXErrorHandler(Display*, XErrorEvent* e) {
    static std::atomic<int> s_logged{ 0 };
    if (e && s_logged.fetch_add(1) < 20) {
        std::fprintf(stderr, "explorer2: X error %d (request %d.%d, resource 0x%lx) ignored\n",
                     static_cast<int>(e->error_code), static_cast<int>(e->request_code), static_cast<int>(e->minor_code),
                     static_cast<unsigned long>(e->resourceid));
    }
    return 0;
}

int X11Lib::DisplayLostHandler(Display*) {
    // The display is gone (logout, XWayland restart): no Xlib call may follow. The windows still save their settings
    // as on a Windows logoff (WM_ENDSESSION), then the process ends, as Xlib requires.
    static std::atomic<bool> s_once{ false };
    if (!s_once.exchange(true)) {
        X11().ready = false;
        std::vector<HWND> tops;
        {
            std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
            for (const auto& [h, r] : g_windows) {
                if (r->topLevel && r->parent == nullptr) tops.push_back(h);
            }
        }
        for (HWND h : tops) SendMessageW(h, WM_ENDSESSION, TRUE, 0);
    }
    std::fflush(nullptr);
    _exit(1);
}

// Built-in Win32 EDIT control window procedure (address bar, search bar, inline rename)
LRESULT CALLBACK BuiltinEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto rec = FindWnd(hwnd);
    if (!rec) return 0;

    auto notifyParent = [&](WORD code) {
        if (rec->parent) {
            SendMessageW(rec->parent, WM_COMMAND, MAKEWPARAM(static_cast<WORD>(rec->ctrlId), code), reinterpret_cast<LPARAM>(hwnd));
            InvalidateRect(rec->parent, nullptr, FALSE);
        }
    };

    auto repaint = [&]() { if (rec->parent) InvalidateRect(rec->parent, nullptr, FALSE); };
    auto eraseSelection = [&]() -> bool {
        if (rec->selStart == rec->caretPos) return false;
        const size_t a = std::min(rec->selStart, rec->caretPos);
        const size_t b = std::max(rec->selStart, rec->caretPos);
        rec->text.erase(a, b - a);
        rec->caretPos = rec->selStart = a;
        return true;
    };
    auto copySelection = [&]() {
        if (rec->selStart == rec->caretPos || !OpenClipboard(hwnd)) return;
        EmptyClipboard();
        const size_t a = std::min(rec->selStart, rec->caretPos);
        const size_t b = std::max(rec->selStart, rec->caretPos);
        const std::wstring sel = rec->text.substr(a, b - a);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (sel.size() + 1) * sizeof(wchar_t));
        if (hMem) {
            if (auto* dst = static_cast<wchar_t*>(GlobalLock(hMem))) {
                std::wmemcpy(dst, sel.c_str(), sel.size() + 1);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
    };
    auto paste = [&]() {
        if (!OpenClipboard(hwnd)) return;
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            if (const auto* w = static_cast<const wchar_t*>(GlobalLock(hData))) {
                std::wstring ins(w);
                GlobalUnlock(hData);
                // A one-line box takes the first line of a multi-line text
                const size_t eol = ins.find_first_of(L"\r\n");
                if (eol != std::wstring::npos) ins.resize(eol);
                eraseSelection();
                rec->text.insert(rec->caretPos, ins);
                rec->caretPos += ins.size();
                rec->selStart = rec->caretPos;
                notifyParent(EN_CHANGE);
            }
        }
        CloseClipboard();
    };
    // Ctrl+Left / Ctrl+Right: the start of the previous / next word (separators: spaces and path characters)
    auto isWordBreak = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'/' || c == L'\\' || c == L'.' || c == L'-' || c == L'_'; };
    auto wordLeft = [&](size_t p) {
        while (p > 0 && isWordBreak(rec->text[p - 1])) --p;
        while (p > 0 && !isWordBreak(rec->text[p - 1])) --p;
        return p;
    };
    auto wordRight = [&](size_t p) {
        const size_t n = rec->text.size();
        while (p < n && !isWordBreak(rec->text[p])) ++p;
        while (p < n && isWordBreak(rec->text[p])) ++p;
        return p;
    };

    switch (msg) {
    case WM_SETTEXT: {
        const wchar_t* s = reinterpret_cast<const wchar_t*>(lParam);
        rec->text = s ? s : L"";
        rec->caretPos = rec->text.size();
        rec->selStart = rec->caretPos;
        rec->editScrollX = 0.0f;
        repaint();
        return TRUE;
    }
    case EM_GETSEL: {
        const DWORD a = static_cast<DWORD>(std::min(rec->selStart, rec->caretPos));
        const DWORD b = static_cast<DWORD>(std::max(rec->selStart, rec->caretPos));
        if (wParam) *reinterpret_cast<DWORD*>(wParam) = a;
        if (lParam) *reinterpret_cast<DWORD*>(lParam) = b;
        return static_cast<LRESULT>(static_cast<DWORD>(MAKELONG(std::min<DWORD>(a, 0xFFFF), std::min<DWORD>(b, 0xFFFF))));
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        repaint();
        return 0;
    case WM_GETTEXTLENGTH:
        return static_cast<LRESULT>(rec->text.size());
    case WM_GETTEXT: {
        auto* dst = reinterpret_cast<wchar_t*>(lParam);
        const size_t cap = static_cast<size_t>(wParam);
        if (!dst || cap == 0) return 0;
        const size_t n = std::min(rec->text.size(), cap - 1);
        std::wmemcpy(dst, rec->text.data(), n);
        dst[n] = L'\0';
        return static_cast<LRESULT>(n);
    }
    case EM_SETSEL: {
        const int s = static_cast<int>(wParam);
        const int e = static_cast<int>(lParam);
        if (s == 0 && e == -1) {
            rec->selStart = 0;
            rec->caretPos = rec->text.size();
        } else {
            rec->selStart = std::clamp<size_t>(std::max(0, s), 0, rec->text.size());
            rec->caretPos = (e < 0) ? rec->text.size() : std::clamp<size_t>(e, 0, rec->text.size());
        }
        if (rec->parent) InvalidateRect(rec->parent, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN: {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        // Moves the caret; with Shift the selection grows from where it started, without it the selection goes
        auto moveCaret = [&](size_t to) {
            rec->caretPos = std::min(to, rec->text.size());
            if (!shift) rec->selStart = rec->caretPos;
            repaint();
        };
        const bool hasSel = rec->selStart != rec->caretPos;
        switch (wParam) {
        case VK_LEFT:
            if (!shift && !ctrl && hasSel) { moveCaret(std::min(rec->selStart, rec->caretPos)); return 0; }
            moveCaret(ctrl ? wordLeft(rec->caretPos) : (rec->caretPos > 0 ? rec->caretPos - 1 : 0));
            return 0;
        case VK_RIGHT:
            if (!shift && !ctrl && hasSel) { moveCaret(std::max(rec->selStart, rec->caretPos)); return 0; }
            moveCaret(ctrl ? wordRight(rec->caretPos) : rec->caretPos + 1);
            return 0;
        case VK_HOME:
            moveCaret(0);
            return 0;
        case VK_END:
            moveCaret(rec->text.size());
            return 0;
        case VK_BACK:
            if (!eraseSelection() && rec->caretPos > 0) {
                const size_t from = ctrl ? wordLeft(rec->caretPos) : rec->caretPos - 1;
                rec->text.erase(from, rec->caretPos - from);
                rec->caretPos = rec->selStart = from;
            }
            notifyParent(EN_CHANGE);
            return 0;
        case VK_DELETE:
            if (shift && hasSel) {                      // Shift+Delete: cut
                copySelection();
                eraseSelection();
            } else if (!eraseSelection() && rec->caretPos < rec->text.size()) {
                const size_t to = ctrl ? wordRight(rec->caretPos) : rec->caretPos + 1;
                rec->text.erase(rec->caretPos, to - rec->caretPos);
            }
            notifyParent(EN_CHANGE);
            return 0;
        case VK_INSERT:
            if (shift) paste();                         // Shift+Insert: paste
            else if (ctrl) copySelection();             // Ctrl+Insert: copy
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_CHAR: {
        const wchar_t ch = static_cast<wchar_t>(wParam);
        if (ch == 1) { // Ctrl+A
            rec->selStart = 0;
            rec->caretPos = rec->text.size();
            repaint();
            return 0;
        }
        if (ch == 3 || ch == 24) { // Ctrl+C / Ctrl+X
            if (rec->selStart != rec->caretPos) {
                copySelection();
                if (ch == 24) {
                    eraseSelection();
                    notifyParent(EN_CHANGE);
                }
            }
            return 0;
        }
        if (ch == 22) { // Ctrl+V
            paste();
            return 0;
        }
        if (ch >= 32 && ch != 127) {
            eraseSelection();
            rec->text.insert(rec->caretPos, 1, ch);
            ++rec->caretPos;
            rec->selStart = rec->caretPos;
            notifyParent(EN_CHANGE);
            return 0;
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

// Built-in BUTTON control: pressed / hot state, and BN_CLICKED to its parent when a click ends on it
LRESULT CALLBACK BuiltinButtonWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto rec = FindWnd(hwnd);
    if (!rec) return 0;
    auto repaintParent = [&]() { if (rec->parent) InvalidateRect(rec->parent, nullptr, FALSE); };
    auto click = [&]() {
        if (rec->parent && rec->enabled) {
            PostMessageW(rec->parent, WM_COMMAND, MAKEWPARAM(static_cast<WORD>(rec->ctrlId), BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
        }
    };
    const int lx = GET_X_LPARAM(lParam);
    const int ly = GET_Y_LPARAM(lParam);
    const bool inside = lx >= 0 && ly >= 0 && lx < rec->width && ly < rec->height;
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (!rec->enabled) return 0;
        rec->pressed = true;
        rec->hot = true;
        SetCapture(hwnd);
        ChangeFocus(hwnd);
        repaintParent();
        return 0;
    case WM_MOUSEMOVE: {
        const bool captured = (GetCapture() == hwnd);
        const bool pressed = captured && inside;
        if (pressed != rec->pressed || inside != rec->hot) {
            rec->pressed = pressed;
            rec->hot = inside;
            repaintParent();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (rec->hot && GetCapture() != hwnd) {
            rec->hot = false;
            repaintParent();
        }
        return 0;
    case WM_LBUTTONUP: {
        if (GetCapture() != hwnd) return 0;
        ReleaseCapture();
        const bool clicked = rec->pressed && inside;
        rec->pressed = false;
        rec->hot = inside;
        repaintParent();
        if (clicked) click();
        return 0;
    }
    case WM_KEYUP:
        if (wParam == VK_SPACE) click();
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        repaintParent();
        return 0;
    default:
        return 0;
    }
}

// Built-in STATIC control: a text that does not fit its box scrolls with the wheel
LRESULT CALLBACK BuiltinStaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM) {
    auto rec = FindWnd(hwnd);
    if (!rec) return 0;
    if (msg == WM_MOUSEWHEEL) {
        const float maxScroll = std::max(0.0f, rec->contentH - static_cast<float>(rec->height));
        if (maxScroll > 0.0f) {
            const short delta = static_cast<short>(HIWORD(wParam));
            rec->scrollY = std::clamp(rec->scrollY - (static_cast<float>(delta) / WHEEL_DELTA) * 48.0f, 0.0f, maxScroll);
            if (rec->parent) InvalidateRect(rec->parent, nullptr, FALSE);
        }
    }
    return 0;
}

// The virtual key of a key, from its keysym without Shift / AltGr (Win32 virtual keys name the key, not the character)
int KeySymToVk(KeySym ks) {
    switch (ks) {
    case XK_BackSpace: return VK_BACK;
    case XK_Tab: case XK_ISO_Left_Tab: return VK_TAB;
    case XK_Return: case XK_KP_Enter: return VK_RETURN;
    case XK_Escape: return VK_ESCAPE;
    case XK_space: case XK_KP_Space: return VK_SPACE;
    case XK_Prior: case XK_KP_Prior: return VK_PRIOR;
    case XK_Next:  case XK_KP_Next:  return VK_NEXT;
    case XK_End:   case XK_KP_End:   return VK_END;
    case XK_Home:  case XK_KP_Home:  return VK_HOME;
    case XK_Left:  case XK_KP_Left:  return VK_LEFT;
    case XK_Up:    case XK_KP_Up:    return VK_UP;
    case XK_Right: case XK_KP_Right: return VK_RIGHT;
    case XK_Down:  case XK_KP_Down:  return VK_DOWN;
    case XK_Insert: case XK_KP_Insert: return VK_INSERT;
    case XK_Delete: case XK_KP_Delete: return VK_DELETE;
    case XK_Shift_L: case XK_Shift_R: return VK_SHIFT;
    case XK_Control_L: case XK_Control_R: return VK_CONTROL;
    case XK_Alt_L: case XK_Alt_R: case XK_Meta_L: case XK_Meta_R: return VK_MENU;
    case XK_Super_L: return VK_LWIN;
    case XK_Super_R: return VK_RWIN;
    case XK_Menu: return VK_APPS;
    case XK_Pause: return VK_PAUSE;
    case XK_Caps_Lock: return VK_CAPITAL;
    case XK_Num_Lock: return VK_NUMLOCK;
    case XK_Scroll_Lock: return VK_SCROLL;
    case XK_Print: return VK_SNAPSHOT;
    case XK_F1: return VK_F1; case XK_F2: return VK_F2; case XK_F3: return VK_F3; case XK_F4: return VK_F4;
    case XK_F5: return VK_F5; case XK_F6: return VK_F6; case XK_F7: return VK_F7; case XK_F8: return VK_F8;
    case XK_F9: return VK_F9; case XK_F10: return VK_F10; case XK_F11: return VK_F11; case XK_F12: return VK_F12;
    case XK_KP_Add: return VK_ADD;
    case XK_KP_Subtract: return VK_SUBTRACT;
    case XK_KP_Multiply: return VK_MULTIPLY;
    case XK_KP_Divide: return VK_DIVIDE;
    case XK_KP_Decimal: return VK_DECIMAL;
    case XK_KP_Separator: return VK_SEPARATOR;
    case XK_plus: case XK_equal: return VK_OEM_PLUS;
    case XK_minus: case XK_underscore: return VK_OEM_MINUS;
    case XK_comma: case XK_less: return VK_OEM_COMMA;
    case XK_period: case XK_greater: return VK_OEM_PERIOD;
    case XK_semicolon: case XK_colon: return VK_OEM_1;
    case XK_slash: case XK_question: return VK_OEM_2;
    case XK_grave: case XK_asciitilde: return VK_OEM_3;
    case XK_bracketleft: case XK_braceleft: return VK_OEM_4;
    case XK_backslash: case XK_bar: return VK_OEM_5;
    case XK_bracketright: case XK_braceright: return VK_OEM_6;
    case XK_apostrophe: case XK_quotedbl: return VK_OEM_7;
    default:
        if (ks >= 'a' && ks <= 'z') return static_cast<int>(ks - 'a' + 'A');
        if (ks >= 'A' && ks <= 'Z') return static_cast<int>(ks);
        if (ks >= '0' && ks <= '9') return static_cast<int>(ks);
        if (ks >= XK_KP_0 && ks <= XK_KP_9) return static_cast<int>(VK_NUMPAD0 + (ks - XK_KP_0));
        return 0;
    }
}

// The character a keysym types (0: none). XLookupString gives only Latin-1 bytes, and nothing at all for the
// Latin-2 keysyms of a Polish keyboard (ą ę ś ć ż ź ń ł), so WM_CHAR is made from the keysym.
uint32_t KeySymToUnicode(KeySym ks) {
    if ((ks >= 0x20 && ks <= 0x7E) || (ks >= 0xA0 && ks <= 0xFF)) return static_cast<uint32_t>(ks);   // ASCII, Latin-1
    if (ks >= 0x01000100 && ks <= 0x0110FFFF) return static_cast<uint32_t>(ks - 0x01000000);             // Unicode keysyms
    if (ks >= 0x1A1 && ks <= 0x1FF) {
        // Latin-2 keysyms are 0x100 + their ISO 8859-2 byte (only the bytes that differ from Latin-1 have one)
        static const uint16_t kIso8859_2[0x60] = {
            0x00A0, 0x0104, 0x02D8, 0x0141, 0x00A4, 0x013D, 0x015A, 0x00A7, 0x00A8, 0x0160, 0x015E, 0x0164, 0x0179, 0x00AD, 0x017D, 0x017B,
            0x00B0, 0x0105, 0x02DB, 0x0142, 0x00B4, 0x013E, 0x015B, 0x02C7, 0x00B8, 0x0161, 0x015F, 0x0165, 0x017A, 0x02DD, 0x017E, 0x017C,
            0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, 0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
            0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, 0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
            0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, 0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
            0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7, 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9 };
        return kIso8859_2[(ks & 0xFF) - 0xA0];
    }
    switch (ks) {
    case 0x20AC: return 0x20AC;             // XK_EuroSign
    case XK_KP_Space: return ' ';
    case XK_KP_Multiply: return '*';
    case XK_KP_Add: return '+';
    case XK_KP_Separator: return ',';
    case XK_KP_Subtract: return '-';
    case XK_KP_Decimal: return '.';
    case XK_KP_Divide: return '/';
    case XK_KP_Equal: return '=';
    default: break;
    }
    if (ks >= XK_KP_0 && ks <= XK_KP_9) return static_cast<uint32_t>('0' + (ks - XK_KP_0));
    return 0;
}

WPARAM ModStateToMkFlags(unsigned int xstate) {
    WPARAM mk = 0;
    if (xstate & Button1Mask) mk |= MK_LBUTTON;
    if (xstate & Button2Mask) mk |= MK_MBUTTON;
    if (xstate & Button3Mask) mk |= MK_RBUTTON;
    if (xstate & ShiftMask)   mk |= MK_SHIFT;
    if (xstate & ControlMask) mk |= MK_CONTROL;
    return mk;
}

void WriteBmp24Or32(const char* path, const uint8_t* bgra, uint32_t w, uint32_t h) {
    if (!path || !*path || !bgra || w == 0 || h == 0) return;
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    const uint32_t imgSize = w * h * 4u;
    const uint32_t fileSize = 54u + imgSize;
    uint8_t hdr[54] = {};
    hdr[0] = 'B'; hdr[1] = 'M';
    std::memcpy(hdr + 2, &fileSize, 4);
    const uint32_t dataOff = 54u, dibSz = 40u;
    const int32_t bw = static_cast<int32_t>(w), bh = -static_cast<int32_t>(h);
    const uint16_t planes = 1, bpp = 32;
    std::memcpy(hdr + 10, &dataOff, 4);
    std::memcpy(hdr + 14, &dibSz, 4);
    std::memcpy(hdr + 18, &bw, 4);
    std::memcpy(hdr + 22, &bh, 4);
    std::memcpy(hdr + 26, &planes, 2);
    std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &imgSize, 4);
    std::fwrite(hdr, 1, sizeof(hdr), f);
    std::fwrite(bgra, 1, imgSize, f);
    std::fclose(f);
}

// Overlay visible child EDIT boxes (address/search/rename) onto top-level window BGRA buffer using TrueType DirectWrite
void CompositeChildEdits(const WndRecord& parent, std::vector<uint8_t>& fb, uint32_t w, uint32_t h) {
    std::vector<std::shared_ptr<WndRecord>> edits;
    {
        std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
        for (const auto& [chHwnd, ch] : g_windows) {
            if (ch->parent == parent.hwnd && ch->isEdit && ch->visible && ch->width > 2 && ch->height > 2) {
                edits.push_back(ch);
            }
        }
    }
    if (edits.empty()) return;

    static Microsoft::WRL::ComPtr<IWICImagingFactory> s_wic;
    static Microsoft::WRL::ComPtr<ID2D1Factory> s_d2d;
    static Microsoft::WRL::ComPtr<IDWriteFactory> s_dw;
    static Microsoft::WRL::ComPtr<IDWriteTextFormat> s_tf;
    if (!s_tf) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_wic));
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, reinterpret_cast<void**>(s_d2d.GetAddressOf()));
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(s_dw.GetAddressOf()));
        if (s_dw) {
            s_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                   DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &s_tf);
            if (s_tf) {
                s_tf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                s_tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }
    }

    auto measurePrefixW = [&](const std::wstring& txt, size_t len) -> float {
        len = std::min(len, txt.size());
        if (len == 0 || !s_dw || !s_tf) return 0.0f;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> tl;
        if (FAILED(s_dw->CreateTextLayout(txt.c_str(), static_cast<UINT32>(len), s_tf.Get(), 8192.0f, 64.0f, &tl)) || !tl) {
            return static_cast<float>(len) * 7.0f;
        }
        DWRITE_TEXT_METRICS tm = {};
        if (FAILED(tl->GetMetrics(&tm))) return static_cast<float>(len) * 7.0f;
        return tm.widthIncludingTrailingWhitespace;
    };

    std::vector<uint8_t> editBuf;
    for (const auto& ed : edits) {
        const int x0 = std::clamp(ed->x, 0, static_cast<int>(w));
        const int y0 = std::clamp(ed->y, 0, static_cast<int>(h));
        const int x1 = std::clamp(ed->x + ed->width, x0, static_cast<int>(w));
        const int y1 = std::clamp(ed->y + ed->height, y0, static_cast<int>(h));
        const int ew = x1 - x0;
        const int eh = y1 - y0;
        if (ew <= 2 || eh <= 2) continue;

        const bool focused = (g_focusedHwnd == ed->hwnd);
        if (s_wic && s_d2d && s_tf) {
            Microsoft::WRL::ComPtr<IWICBitmap> bmp;
            Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt;
            D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
            if (SUCCEEDED(s_wic->CreateBitmap(static_cast<UINT>(ew), static_cast<UINT>(eh),
                                              GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)) &&
                bmp && SUCCEEDED(s_d2d->CreateWicBitmapRenderTarget(bmp.Get(), &rtp, &rt)) && rt)
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brBorder, brSel, brText, brCaret;
                rt->CreateSolidColorBrush(D2D1::ColorF(focused ? 0x0078D7 : 0xCBD5E1), &brBorder);
                rt->CreateSolidColorBrush(D2D1::ColorF(0xCCE8FF), &brSel);
                rt->CreateSolidColorBrush(D2D1::ColorF(0x1E1E1E), &brText);
                rt->CreateSolidColorBrush(D2D1::ColorF(0x005A9E), &brCaret);

                const size_t caretIdx = std::min(ed->caretPos, ed->text.size());
                const size_t selIdx   = std::min(ed->selStart, ed->text.size());
                const float caretPx   = measurePrefixW(ed->text, caretIdx);
                const float selPx     = (selIdx == caretIdx) ? caretPx : measurePrefixW(ed->text, selIdx);
                const float padX      = 6.0f;
                const float innerW    = std::max(8.0f, static_cast<float>(ew) - padX * 2.0f);
                // The text scrolls only as far as needed to keep the caret in view (a click maps back through it)
                const float textW     = (caretIdx == ed->text.size()) ? caretPx : measurePrefixW(ed->text, ed->text.size());
                float scrollX = ed->editScrollX;
                if (caretPx - scrollX > innerW - 4.0f) scrollX = caretPx - (innerW - 4.0f);
                if (caretPx - scrollX < 0.0f) scrollX = caretPx;
                scrollX = std::clamp(scrollX, 0.0f, std::max(0.0f, textW - (innerW - 4.0f)));
                ed->editScrollX = scrollX;

                rt->BeginDraw();
                rt->Clear(D2D1::ColorF(0xFFFFFF));
                rt->DrawRectangle(D2D1::RectF(0.5f, 0.5f, ew - 0.5f, eh - 0.5f), brBorder.Get(), 1.0f);
                rt->PushAxisAlignedClip(D2D1::RectF(2.0f, 1.0f, ew - 2.0f, eh - 1.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

                if (focused && selIdx != caretIdx) {
                    const float sx0 = padX - scrollX + std::min(selPx, caretPx);
                    const float sx1 = padX - scrollX + std::max(selPx, caretPx);
                    rt->FillRectangle(D2D1::RectF(sx0, 3.0f, sx1, eh - 3.0f), brSel.Get());
                }
                if (!ed->text.empty()) {
                    rt->DrawText(ed->text.c_str(), static_cast<UINT32>(ed->text.size()), s_tf.Get(),
                                 D2D1::RectF(padX - scrollX, 0.0f, padX - scrollX + 8192.0f, static_cast<float>(eh)),
                                 brText.Get());
                }
                if (focused) {
                    const float cx = std::clamp(padX - scrollX + caretPx, 2.0f, static_cast<float>(ew - 3));
                    rt->DrawLine(D2D1::Point2F(cx, 4.0f), D2D1::Point2F(cx, static_cast<float>(eh - 4)), brCaret.Get(), 1.4f);
                }

                rt->PopAxisAlignedClip();
                rt->EndDraw();

                editBuf.resize(static_cast<size_t>(ew) * eh * 4u);
                WICRect wrc = { 0, 0, ew, eh };
                if (SUCCEEDED(bmp->CopyPixels(&wrc, static_cast<UINT>(ew * 4), static_cast<UINT>(editBuf.size()), editBuf.data()))) {
                    for (int ry = 0; ry < eh; ++ry) {
                        std::memcpy(fb.data() + (static_cast<size_t>(y0 + ry) * w + x0) * 4u,
                                    editBuf.data() + static_cast<size_t>(ry) * ew * 4u,
                                    static_cast<size_t>(ew) * 4u);
                    }
                    continue;
                }
            }
        }
    }
}

// ==============================================================================================================
// 2b. Windows the HAL draws itself: owned dialogs (labels, push buttons, EDITs) and MessageBoxW
// ==============================================================================================================
struct HalUiResources {
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d;
    Microsoft::WRL::ComPtr<IDWriteFactory> dw;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text;     // labels, message text, EDIT measuring: wrapped, top-left
    Microsoft::WRL::ComPtr<IDWriteTextFormat> button;   // button captions: one line, centred
    Microsoft::WRL::ComPtr<IDWriteTextFormat> icon;     // the glyph of a message box icon
    bool ok = false;
};

HalUiResources& HalUi() {
    static HalUiResources s_ui = []() {
        HalUiResources r;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&r.wic));
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, reinterpret_cast<void**>(r.d2d.GetAddressOf()));
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(r.dw.GetAddressOf()));
        if (r.dw) {
            r.dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                   DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &r.text);
            r.dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                   DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &r.button);
            r.dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                   DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"en-us", &r.icon);
            if (r.text) r.text->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            for (IDWriteTextFormat* f : { r.button.Get(), r.icon.Get() }) {
                if (!f) continue;
                f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }
        r.ok = r.wic && r.d2d && r.dw && r.text && r.button && r.icon;
        return r;
    }();
    return s_ui;
}

// Size of a text laid out with fmt in a box maxW wide
D2D1_SIZE_F MeasureHalText(IDWriteTextFormat* fmt, const std::wstring& s, float maxW) {
    auto& ui = HalUi();
    if (!ui.dw || !fmt || s.empty()) return D2D1::SizeF(0.0f, 0.0f);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> tl;
    if (FAILED(ui.dw->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), fmt, maxW, 100000.0f, &tl)) || !tl) {
        return D2D1::SizeF(static_cast<float>(s.size()) * 7.0f, 17.0f);
    }
    DWRITE_TEXT_METRICS tm = {};
    tl->GetMetrics(&tm);
    return D2D1::SizeF(tm.widthIncludingTrailingWhitespace, tm.height);
}

void FillHalRect(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, uint32_t rgb) {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> br;
    if (SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(rgb), &br)) && br) rt->FillRectangle(r, br.Get());
}

void FrameHalRect(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, uint32_t rgb, float width) {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> br;
    if (SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(rgb), &br)) && br) rt->DrawRectangle(r, br.Get(), width);
}

void DrawHalText(ID2D1RenderTarget* rt, const std::wstring& s, IDWriteTextFormat* fmt, const D2D1_RECT_F& r, uint32_t rgb) {
    if (s.empty() || !fmt) return;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> br;
    if (SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(rgb), &br)) && br) {
        rt->DrawText(s.c_str(), static_cast<UINT32>(s.size()), fmt, r, br.Get());
    }
}

std::wstring StripMnemonics(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'&') {
            if (i + 1 < s.size() && s[i + 1] == L'&') { out.push_back(L'&'); ++i; }
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
}

void DrawHalButton(ID2D1RenderTarget* rt, const WndRecord& b, bool focused) {
    const D2D1_RECT_F r = D2D1::RectF(b.x + 0.5f, b.y + 0.5f, b.x + b.width - 0.5f, b.y + b.height - 0.5f);
    const bool isDefault = (b.style & 0x0Fu) == static_cast<DWORD>(BS_DEFPUSHBUTTON);
    uint32_t fill = 0xE1E1E1, border = 0xADADAD, textColor = 0x000000;
    float borderW = 1.0f;
    if (!b.enabled) {
        fill = 0xCCCCCC; border = 0xBFBFBF; textColor = 0x838383;
    } else if (b.pressed) {
        fill = 0xCCE4F7; border = 0x005499;
    } else if (b.hot) {
        fill = 0xE5F1FB; border = 0x0078D7;
    }
    if (b.enabled && (focused || isDefault)) {
        border = 0x0078D7;
        borderW = 2.0f;
    }
    FillHalRect(rt, r, fill);
    FrameHalRect(rt, r, border, borderW);
    DrawHalText(rt, StripMnemonics(b.text), HalUi().button.Get(),
                D2D1::RectF(static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.x + b.width), static_cast<float>(b.y + b.height)),
                textColor);
}

// The round icon of a message box: MB_ICONERROR, MB_ICONQUESTION, MB_ICONWARNING or MB_ICONINFORMATION
void DrawMessageBoxIcon(ID2D1RenderTarget* rt, UINT icon, const D2D1_RECT_F& r) {
    uint32_t color = 0x0078D7, glyphColor = 0xFFFFFF;
    const wchar_t* glyph = L"i";
    switch (icon) {
    case MB_ICONHAND:        color = 0xE81123; glyph = L"\x00D7"; break;
    case MB_ICONQUESTION:    color = 0x0078D7; glyph = L"?"; break;
    case MB_ICONEXCLAMATION: color = 0xFFB900; glyph = L"!"; glyphColor = 0x000000; break;
    default: break;
    }
    const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
    const float rad = std::min(r.right - r.left, r.bottom - r.top) * 0.5f - 1.0f;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> br;
    if (SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(color), &br)) && br) {
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), br.Get());
    }
    DrawHalText(rt, glyph, HalUi().icon.Get(), r, glyphColor);
}

void DrawHalStatic(ID2D1RenderTarget* rt, WndRecord& s) {
    const D2D1_RECT_F box = D2D1::RectF(static_cast<float>(s.x), static_cast<float>(s.y),
                                        static_cast<float>(s.x + s.width), static_cast<float>(s.y + s.height));
    if (s.staticIcon != 0) {
        DrawMessageBoxIcon(rt, s.staticIcon, box);
        return;
    }
    auto& ui = HalUi();
    const float w = static_cast<float>(std::max(1, s.width));
    const float h = static_cast<float>(std::max(1, s.height));
    s.contentH = MeasureHalText(ui.text.Get(), s.text, w).height;
    const float maxScroll = std::max(0.0f, s.contentH - h);
    s.scrollY = std::clamp(s.scrollY, 0.0f, maxScroll);
    rt->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
    DrawHalText(rt, s.text, ui.text.Get(),
                D2D1::RectF(box.left, box.top - s.scrollY, box.right - (maxScroll > 0.0f ? 10.0f : 0.0f),
                            box.top - s.scrollY + std::max(h, s.contentH) + 2.0f),
                s.enabled ? 0x1E1E1E : 0x838383);
    rt->PopAxisAlignedClip();
    if (maxScroll > 0.0f) {
        // A text taller than its box (the shortcuts help on a 768 px screen) scrolls: a thin bar shows where
        const D2D1_RECT_F track = D2D1::RectF(box.right - 6.0f, box.top, box.right - 2.0f, box.bottom);
        FillHalRect(rt, track, 0xEDEDED);
        const float thumbH = std::max(24.0f, h * h / s.contentH);
        const float thumbTop = box.top + (h - thumbH) * (s.scrollY / maxScroll);
        FillHalRect(rt, D2D1::RectF(track.left, thumbTop, track.right, thumbTop + thumbH), 0xA8A8A8);
    }
}

// The window class's background (COLOR_WINDOW + 1: white), as a 0xRRGGBB colour
uint32_t HalWindowBackground(const WndRecord& w) {
    std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
    auto it = g_wndClasses.find(Explorer2::PathUtils::ToLower(w.className));
    if (it != g_wndClasses.end() && reinterpret_cast<intptr_t>(it->second.hbrBackground) == COLOR_WINDOW + 1) return 0xFFFFFF;
    return 0xF0F0F0;
}

// Draws a top-level window the HAL is responsible for (an owned dialog, MessageBoxW): background, labels and
// buttons; PresentHwndFramebuffer then puts its EDIT children on top
void PaintHalWindow(const std::shared_ptr<WndRecord>& win) {
    auto& ui = HalUi();
    if (!ui.ok || !win || win->width <= 0 || win->height <= 0) return;
    const UINT w = static_cast<UINT>(win->width), h = static_cast<UINT>(win->height);
    Microsoft::WRL::ComPtr<IWICBitmap> bmp;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt;
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
    if (FAILED(ui.wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)) || !bmp ||
        FAILED(ui.d2d->CreateWicBitmapRenderTarget(bmp.Get(), &rtp, &rt)) || !rt) {
        return;
    }
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(HalWindowBackground(*win)));
    if (win->msgBoxBandTop > 0 && win->msgBoxBandTop < win->height) {
        const D2D1_RECT_F band = D2D1::RectF(0.0f, static_cast<float>(win->msgBoxBandTop), static_cast<float>(w), static_cast<float>(h));
        FillHalRect(rt.Get(), band, 0xF0F0F0);
        FillHalRect(rt.Get(), D2D1::RectF(band.left, band.top, band.right, band.top + 1.0f), 0xDFDFDF);
    }
    for (const auto& ch : ChildrenOf(win->hwnd)) {
        if (!ch->visible) continue;
        if (ch->isButton) DrawHalButton(rt.Get(), *ch, g_focusedHwnd == ch->hwnd);
        else if (ch->isStatic) DrawHalStatic(rt.Get(), *ch);
    }
    rt->EndDraw();
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4u);
    WICRect wrc = { 0, 0, static_cast<INT>(w), static_cast<INT>(h) };
    if (SUCCEEDED(bmp->CopyPixels(&wrc, w * 4u, static_cast<UINT>(px.size()), px.data()))) {
        Explorer2::Hal::PresentHwndFramebuffer(win->hwnd, px.data(), w, h);
    }
}

// The caret position in an EDIT for a click relX pixels from its left edge (through the scroll of its last frame)
size_t EditCaretFromX(const WndRecord& ed, int relX) {
    const float target = static_cast<float>(relX) - 6.0f + ed.editScrollX;      // 6: the text's left padding
    if (target <= 0.0f || ed.text.empty()) return 0;
    auto& ui = HalUi();
    if (!ui.ok) return std::min(ed.text.size(), static_cast<size_t>((target + 3.0f) / 7.0f));
    auto widthOf = [&](size_t n) { return n == 0 ? 0.0f : MeasureHalText(ui.text.Get(), ed.text.substr(0, n), 100000.0f).width; };
    size_t lo = 0, hi = ed.text.size();
    while (lo < hi) {                       // the longest prefix that ends left of the click
        const size_t mid = (lo + hi + 1) / 2;
        if (widthOf(mid) <= target) lo = mid;
        else hi = mid - 1;
    }
    if (lo < ed.text.size() && target - widthOf(lo) > widthOf(lo + 1) - target) ++lo;   // the nearer character edge
    return lo;
}

// ==============================================================================================================
// 2c. Message queue
// ==============================================================================================================
// Posts a message with its HAL data (the input's modifier keys, keyboard routing)
BOOL PostMessageEx(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, DWORD lPrivate) {
    {
        std::lock_guard<std::mutex> lk(g_msgMutex);
        if (Msg == WM_MOUSEMOVE) {
            for (auto it = g_msgQueue.rbegin(); it != g_msgQueue.rend(); ++it) {
                if (it->hwnd == hWnd && it->message == WM_MOUSEMOVE) {
                    it->wParam = wParam;
                    it->lParam = lParam;
                    it->time   = GetTickCount();
                    it->pt     = g_cursorScreen;
                    it->lPrivate = lPrivate;
                    return TRUE;
                }
                if (it->message != WM_MOUSEMOVE && it->message != WM_TIMER && it->message != WM_PAINT) {
                    break; // Preserve ordering across clicks, keys, wheel, or commands
                }
            }
        } else if (Msg == WM_PAINT) {
            for (const auto& qm : g_msgQueue) {
                if (qm.hwnd == hWnd && qm.message == WM_PAINT) return TRUE;
            }
        } else if (Msg == WM_TIMER) {
            for (const auto& qm : g_msgQueue) {
                if (qm.hwnd == hWnd && qm.message == WM_TIMER && qm.wParam == wParam) return TRUE;
            }
        }
        if (g_msgQueue.size() > 32768) {
            // A runaway producer: the oldest message goes, but never a WM_QUIT
            auto drop = std::find_if(g_msgQueue.begin(), g_msgQueue.end(), [](const MSG& m) { return m.message != WM_QUIT; });
            if (drop != g_msgQueue.end()) g_msgQueue.erase(drop);
        }
        MSG m = {};
        m.hwnd = hWnd;
        m.message = Msg;
        m.wParam = wParam;
        m.lParam = lParam;
        m.time = GetTickCount();
        m.pt = g_cursorScreen;
        m.lPrivate = lPrivate;
        g_msgQueue.push_back(m);
    }
    g_msgCv.notify_all();
    return TRUE;
}

// The next message to hand out: everything else before WM_TIMER (as on Win32, where timers come last, after
// WM_PAINT, so a slow timer handler cannot keep the window from repainting)
std::deque<MSG>::iterator FindNextMessage(HWND hWnd, UINT fMin, UINT fMax) {
    auto timerIt = g_msgQueue.end();
    for (auto it = g_msgQueue.begin(); it != g_msgQueue.end(); ++it) {
        if (!MsgMatches(*it, hWnd, fMin, fMax)) continue;
        if (it->message != WM_TIMER) return it;
        if (timerIt == g_msgQueue.end()) timerIt = it;
    }
    return timerIt;
}

// A message just taken from the queue: GetKeyState() shows the keys as they were when the input happened, and a
// keyboard message goes to the window that has the focus now
void PrepareTakenMessage(MSG& m) {
    if (m.lPrivate & kMsgHasKeyState) {
        ApplyModifierSnapshot(g_keyStates, m.lPrivate);
        if ((m.message == WM_KEYDOWN || m.message == WM_KEYUP) && m.wParam < 256 &&
            m.wParam != VK_SHIFT && m.wParam != VK_CONTROL && m.wParam != VK_MENU) {
            g_keyStates[m.wParam] = (m.message == WM_KEYDOWN) ? static_cast<SHORT>(0x8000) : 0;
        }
    }
    if (m.lPrivate & kMsgToFocus) m.hwnd = KeyTargetFor(m.hwnd);
}

// ==============================================================================================================
// 2d. Mouse cursor shapes (SetCursor / WM_SETCURSOR)
// ==============================================================================================================
Window g_cursorXwin = 0;        // the top-level X window under the pointer
uintptr_t g_cursorShape = 0;    // the IDC_ cursor shown there
std::map<Window, uintptr_t> g_cursorShapeOf;    // the cursor each top-level X window was given

void ApplyCursor(uintptr_t idc) {
    auto& x = X11();
    if (!x.ready || !x.dpy || g_cursorXwin == 0 || !x.CreateFontCursor || !x.DefineCursor) return;
    g_cursorShape = idc;
    uintptr_t& current = g_cursorShapeOf[g_cursorXwin];     // per window: the pointer moves between windows
    if (idc == current) return;
    current = idc;
    unsigned int shape = XC_left_ptr;
    switch (idc) {
    case 32513: shape = XC_xterm; break;                 // IDC_IBEAM
    case 32514: case 32650: shape = XC_watch; break;     // IDC_WAIT, IDC_APPSTARTING
    case 32515: shape = XC_crosshair; break;             // IDC_CROSS
    case 32642: shape = XC_bottom_right_corner; break;   // IDC_SIZENWSE
    case 32643: shape = XC_bottom_left_corner; break;    // IDC_SIZENESW
    case 32644: shape = XC_sb_h_double_arrow; break;     // IDC_SIZEWE
    case 32645: shape = XC_sb_v_double_arrow; break;     // IDC_SIZENS
    case 32646: shape = XC_fleur; break;                 // IDC_SIZEALL
    case 32648: shape = XC_circle; break;                // IDC_NO
    case 32649: shape = XC_hand2; break;                 // IDC_HAND
    default: break;
    }
    static std::map<unsigned int, Cursor> s_cursors;
    auto it = s_cursors.find(shape);
    if (it == s_cursors.end()) it = s_cursors.emplace(shape, x.CreateFontCursor(x.dpy, shape)).first;
    if (it->second) x.DefineCursor(x.dpy, g_cursorXwin, it->second);
}

// ==============================================================================================================
// 2e. X11 CLIPBOARD selection: what the app copies reaches other programs, and what they copy can be pasted here
// ==============================================================================================================
struct ClipboardState {
    std::recursive_mutex mutex;
    std::map<UINT, HGLOBAL> data;       // what this process put on the clipboard
    std::map<UINT, HGLOBAL> imported;   // converted from another program's clipboard, kept until the next OpenClipboard
    bool owner = false;                 // this process owns CLIPBOARD
    Window window = 0;                  // unmapped window that owns the selection and receives conversions
    ULONGLONG targetsTick = 0;
    std::vector<Atom> targetsCache;     // what the other program offers, asked again after 1.5 s
};

ClipboardState& Clip() {
    static ClipboardState s_clip;
    return s_clip;
}

bool IsUiThread() { return !g_uiThreadKnown || std::this_thread::get_id() == g_uiThread; }

Window ClipboardWindow() {
    auto& x = X11();
    auto& c = Clip();
    if (c.window == 0 && x.ready && x.dpy) {
        c.window = x.CreateSimpleWindow(x.dpy, x.root, -10, -10, 1, 1, 0, 0, 0);
    }
    return c.window;
}

UINT DropEffectFormat() {
    static const UINT s_fmt = RegisterClipboardFormatW(L"Preferred DropEffect");
    return s_fmt;
}

std::vector<std::wstring> PathsFromHDrop(HGLOBAL h) {
    std::vector<std::wstring> paths;
    const SIZE_T size = GlobalSize(h);
    const auto* base = static_cast<const uint8_t*>(GlobalLock(h));
    if (!base) return paths;
    if (size >= sizeof(DROPFILES)) {
        const auto* df = reinterpret_cast<const DROPFILES*>(base);
        if (df->fWide && df->pFiles >= sizeof(DROPFILES) && df->pFiles < size) {
            const auto* p = reinterpret_cast<const wchar_t*>(base + df->pFiles);
            const auto* end = reinterpret_cast<const wchar_t*>(base + size);
            while (p < end && *p) {
                const wchar_t* s = p;
                while (p < end && *p) ++p;
                paths.emplace_back(s, p);
                ++p;
            }
        }
    }
    GlobalUnlock(h);
    return paths;
}

HGLOBAL HDropFromPaths(const std::vector<std::wstring>& paths) {
    size_t chars = 1;
    for (const auto& p : paths) chars += p.size() + 1;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t));
    if (!h) return nullptr;
    auto* raw = static_cast<uint8_t*>(GlobalLock(h));
    auto* df = reinterpret_cast<DROPFILES*>(raw);
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    auto* dst = reinterpret_cast<wchar_t*>(raw + sizeof(DROPFILES));
    for (const auto& p : paths) {
        std::wmemcpy(dst, p.c_str(), p.size() + 1);
        dst += p.size() + 1;
    }
    *dst = L'\0';
    GlobalUnlock(h);
    return h;
}

HGLOBAL HGlobalFromText(const std::wstring& s) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (!h) return nullptr;
    if (auto* dst = static_cast<wchar_t*>(GlobalLock(h))) {
        std::wmemcpy(dst, s.c_str(), s.size() + 1);
        GlobalUnlock(h);
    }
    return h;
}

std::string FileUriFromPath(const std::wstring& path) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out = "file://";
    for (unsigned char c : Explorer2::PathUtils::WideToUtf8(path)) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 15]);
        }
    }
    return out;
}

// "file:///home/a%20b" -> L"/home/a b" (empty for anything that is not a local file URI)
std::wstring PathFromFileUri(std::string uri) {
    while (!uri.empty() && (uri.back() == '\r' || uri.back() == '\n' || uri.back() == ' ')) uri.pop_back();
    if (uri.rfind("file://", 0) != 0) return L"";
    size_t p = 7;
    if (p < uri.size() && uri[p] != '/') {           // file://host/path: only the local host
        const size_t slash = uri.find('/', p);
        if (slash == std::string::npos) return L"";
        const std::string host = uri.substr(p, slash - p);
        if (host != "localhost") return L"";
        p = slash;
    }
    std::string bytes;
    for (; p < uri.size(); ++p) {
        if (uri[p] == '%' && p + 2 < uri.size() && std::isxdigit(static_cast<unsigned char>(uri[p + 1])) &&
            std::isxdigit(static_cast<unsigned char>(uri[p + 2]))) {
            bytes.push_back(static_cast<char>(std::stoi(uri.substr(p + 1, 2), nullptr, 16)));
            p += 2;
        } else {
            bytes.push_back(uri[p]);
        }
    }
    return Explorer2::PathUtils::Utf8ToWide(bytes);
}

// Our clipboard data in an X target format as the bytes of the reply (false: not available in that format)
bool ClipboardBytesFor(Atom target, std::string& out) {
    auto& x = X11();
    auto& c = Clip();
    std::lock_guard<std::recursive_mutex> lk(c.mutex);
    out.clear();
    std::vector<std::wstring> files;
    if (auto it = c.data.find(CF_HDROP); it != c.data.end() && it->second) files = PathsFromHDrop(it->second);
    if (target == x.utf8String || target == x.textPlainUtf8) {
        auto it = c.data.find(CF_UNICODETEXT);
        if (it != c.data.end() && it->second) {
            const size_t maxChars = GlobalSize(it->second) / sizeof(wchar_t);
            if (const auto* w = static_cast<const wchar_t*>(GlobalLock(it->second))) {
                out = Explorer2::PathUtils::WideToUtf8(std::wstring(w, wcsnlen(w, maxChars)));
                GlobalUnlock(it->second);
            }
            return true;
        }
        if (files.empty()) return false;
        for (size_t i = 0; i < files.size(); ++i) {                 // files pasted as text: one path per line
            if (i) out.push_back('\n');
            out += Explorer2::PathUtils::WideToUtf8(files[i]);
        }
        return true;
    }
    if (files.empty()) return false;
    if (target == x.uriList) {
        for (const auto& f : files) out += FileUriFromPath(f) + "\r\n";
        return true;
    }
    if (target == x.gnomeCopiedFiles) {
        bool cut = false;
        if (auto it = c.data.find(DropEffectFormat()); it != c.data.end() && it->second && GlobalSize(it->second) >= sizeof(DWORD)) {
            if (const auto* e = static_cast<const DWORD*>(GlobalLock(it->second))) {
                cut = (*e & 2u) != 0 && (*e & 1u) == 0;             // DROPEFFECT_MOVE without DROPEFFECT_COPY
                GlobalUnlock(it->second);
            }
        }
        out = cut ? "cut" : "copy";
        for (const auto& f : files) out += "\n" + FileUriFromPath(f);
        return true;
    }
    return false;
}

// Another program asks for our clipboard: answer in the format it wants (or say we cannot)
void HandleSelectionRequest(const XSelectionRequestEvent& req) {
    auto& x = X11();
    auto& c = Clip();
    XEvent reply = {};
    reply.xselection.type = SelectionNotify;
    reply.xselection.display = req.display;
    reply.xselection.requestor = req.requestor;
    reply.xselection.selection = req.selection;
    reply.xselection.target = req.target;
    reply.xselection.time = req.time;
    reply.xselection.property = 0;
    const Atom prop = (req.property != 0) ? req.property : req.target;     // obsolete clients name no property
    bool owner = false;
    {
        std::lock_guard<std::recursive_mutex> lk(c.mutex);
        owner = c.owner;
    }
    if (owner && req.selection == x.clipboard && x.ChangeProperty) {
        if (req.target == x.targets) {
            std::vector<long> list = { static_cast<long>(x.targets) };
            std::string probe;
            for (Atom a : { x.utf8String, x.textPlainUtf8, x.uriList, x.gnomeCopiedFiles }) {
                if (a && ClipboardBytesFor(a, probe)) list.push_back(static_cast<long>(a));
            }
            x.ChangeProperty(x.dpy, req.requestor, prop, XA_ATOM, 32, PropModeReplace,
                             reinterpret_cast<const unsigned char*>(list.data()), static_cast<int>(list.size()));
            reply.xselection.property = prop;
        } else {
            std::string bytes;
            if (ClipboardBytesFor(req.target, bytes)) {
                x.ChangeProperty(x.dpy, req.requestor, prop, req.target, 8, PropModeReplace,
                                 reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()));
                reply.xselection.property = prop;
            }
        }
    }
    if (x.SendEvent) x.SendEvent(x.dpy, req.requestor, False, 0, &reply);
    x.Flush(x.dpy);
}

void FreeClipboardMap(std::map<UINT, HGLOBAL>& m) {
    for (auto& [fmt, h] : m) {
        if (h) GlobalFree(h);
    }
    m.clear();
}

void HandleX11Event(XEvent& ev);

// Asks the CLIPBOARD owner for its data in one format and waits for it briefly; other X events are handled as usual
// meanwhile. type/format/count describe what came (count = number of 8- or 32-bit items).
bool FetchSelection(Atom target, std::vector<unsigned char>& out, Atom& type, int& format, unsigned long& count) {
    auto& x = X11();
    out.clear();
    if (!x.ready || !x.dpy || !x.ConvertSelection || !x.GetWindowProperty || !x.clipboard || !target) return false;
    const Window w = ClipboardWindow();
    if (!w || (x.GetSelectionOwner && x.GetSelectionOwner(x.dpy, x.clipboard) == 0)) return false;
    if (x.DeleteProperty) x.DeleteProperty(x.dpy, w, x.halSelection);
    x.ConvertSelection(x.dpy, x.clipboard, target, x.halSelection, w, CurrentTime);
    x.Flush(x.dpy);
    const ULONGLONG deadline = GetTickCount64() + 700;
    bool done = false, ok = false;
    while (!done && GetTickCount64() < deadline && x.ready) {
        while (!done && x.ready && x.Pending(x.dpy) > 0) {
            XEvent ev = {};
            x.NextEvent(x.dpy, &ev);
            if (ev.type == SelectionNotify && ev.xselection.requestor == w) {
                done = true;
                ok = (ev.xselection.property != 0);
            } else {
                HandleX11Event(ev);
            }
        }
        if (!done && x.ready) {
            struct pollfd pfd = { ConnectionNumber(x.dpy), POLLIN, 0 };
            ::poll(&pfd, 1, 20);
        }
    }
    if (!ok) return false;
    static const Atom s_incr = x.InternAtom ? x.InternAtom(x.dpy, "INCR", False) : 0;
    unsigned long after = 0;
    unsigned char* data = nullptr;
    type = 0;
    format = 0;
    count = 0;
    if (x.GetWindowProperty(x.dpy, w, x.halSelection, 0, 0x1FFFFFFF, True, AnyPropertyType, &type, &format, &count, &after, &data) == Success && data) {
        if (type != s_incr) {
            const size_t itemBytes = (format == 32) ? sizeof(long) : (format == 16 ? sizeof(short) : 1);
            out.assign(data, data + count * itemBytes);
        }
        if (x.Free) x.Free(data);
    }
    return !out.empty();
}

// What another program's clipboard offers (its TARGETS), asked again at most every 1.5 s
std::vector<Atom> ForeignClipboardTargets() {
    auto& c = Clip();
    {
        std::lock_guard<std::recursive_mutex> lk(c.mutex);
        if (c.targetsTick != 0 && GetTickCount64() - c.targetsTick < 1500) return c.targetsCache;
    }
    std::vector<unsigned char> bytes;
    Atom type = 0;
    int format = 0;
    unsigned long count = 0;
    std::vector<Atom> atoms;
    if (FetchSelection(X11().targets, bytes, type, format, count) && format == 32) {
        const auto* v = reinterpret_cast<const long*>(bytes.data());
        for (unsigned long i = 0; i < count; ++i) atoms.push_back(static_cast<Atom>(v[i]));
    }
    std::lock_guard<std::recursive_mutex> lk(c.mutex);
    c.targetsCache = atoms;
    c.targetsTick = GetTickCount64();
    return atoms;
}

// Another program's clipboard in a Win32 format: text, or files (with "Preferred DropEffect" for a cut)
HGLOBAL ImportForeignClipboard(UINT format) {
    auto& x = X11();
    std::vector<unsigned char> bytes;
    Atom type = 0;
    int fmtBits = 0;
    unsigned long count = 0;
    if (format == CF_UNICODETEXT) {
        if ((FetchSelection(x.utf8String, bytes, type, fmtBits, count) || FetchSelection(x.textPlainUtf8, bytes, type, fmtBits, count)) && fmtBits == 8) {
            return HGlobalFromText(Explorer2::PathUtils::Utf8ToWide(std::string(bytes.begin(), bytes.end())));
        }
        return nullptr;
    }
    if (format != CF_HDROP && format != DropEffectFormat()) return nullptr;
    std::vector<std::wstring> paths;
    bool cut = false;
    std::string list;
    if (FetchSelection(x.gnomeCopiedFiles, bytes, type, fmtBits, count) && fmtBits == 8) {
        list.assign(bytes.begin(), bytes.end());
        const size_t nl = list.find('\n');
        cut = list.compare(0, 3, "cut") == 0;
        list = (nl == std::string::npos) ? std::string() : list.substr(nl + 1);
    } else if (FetchSelection(x.uriList, bytes, type, fmtBits, count) && fmtBits == 8) {
        list.assign(bytes.begin(), bytes.end());
    }
    size_t pos = 0;
    while (pos < list.size()) {
        size_t eol = list.find('\n', pos);
        if (eol == std::string::npos) eol = list.size();
        std::string line = list.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty() || line[0] == '#') continue;
        std::wstring p = PathFromFileUri(line);
        if (!p.empty()) paths.push_back(std::move(p));
    }
    if (paths.empty()) return nullptr;
    auto& c = Clip();
    std::lock_guard<std::recursive_mutex> lk(c.mutex);
    if (auto it = c.imported.find(CF_HDROP); it != c.imported.end() && it->second) GlobalFree(it->second);
    if (auto it = c.imported.find(DropEffectFormat()); it != c.imported.end() && it->second) GlobalFree(it->second);
    c.imported[CF_HDROP] = HDropFromPaths(paths);
    HGLOBAL effect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (effect) {
        if (auto* e = static_cast<DWORD*>(GlobalLock(effect))) {
            *e = cut ? 2u : 1u;                             // DROPEFFECT_MOVE : DROPEFFECT_COPY
            GlobalUnlock(effect);
        }
    }
    c.imported[DropEffectFormat()] = effect;
    return c.imported[format];
}

// ==============================================================================================================
// 2f. The X event loop
// ==============================================================================================================
HWND g_editDragHwnd = nullptr;  // EDIT in which a mouse selection is being dragged

// Posts a mouse message to the window that gets it on Win32: the capture window, a dialog's button or label under
// the pointer, or the top-level window itself. x, y: the top-level window's client coordinates.
void PostMouseInput(HWND top, const std::shared_ptr<WndRecord>& topRec, UINT msg, WPARAM wParam, int x, int y, DWORD mods) {
    const DWORD lp = kMsgHasKeyState | mods;
    if (g_captureHwnd) {
        if (auto cr = FindWnd(g_captureHwnd)) {
            int tx = x, ty = y;
            if (g_captureHwnd != top) {
                if (!cr->topLevel && cr->parent == top) {
                    tx = x - cr->x;
                    ty = y - cr->y;
                } else if (cr->topLevel) {
                    tx = x + topRec->x - cr->x;
                    ty = y + topRec->y - cr->y;
                }
            }
            PostMessageEx(g_captureHwnd, msg, wParam, MAKELPARAM(tx, ty), lp);
            return;
        }
        g_captureHwnd = nullptr;
    }
    auto ch = ChildControlAt(top, x, y);
    const HWND hot = (ch && ch->isButton) ? ch->hwnd : nullptr;
    if (topRec->hotChild != hot) {
        if (topRec->hotChild && FindWnd(topRec->hotChild)) PostMessageEx(topRec->hotChild, WM_MOUSELEAVE, 0, 0, 0);
        topRec->hotChild = hot;
    }
    if (ch && (ch->isButton || ch->isStatic)) {
        PostMessageEx(ch->hwnd, msg, wParam, MAKELPARAM(x - ch->x, y - ch->y), lp);
        return;
    }
    PostMessageEx(top, msg, wParam, MAKELPARAM(x, y), lp);
}

// One X event for the HAL's windows (the popup-menu loop and clipboard waits pass the events that are not theirs here)
void HandleX11Event(XEvent& ev) {
    auto& x = X11();
    if (!x.ready) return;
    if (ev.type == SelectionRequest) {
        HandleSelectionRequest(ev.xselectionrequest);
        return;
    }
    if (ev.type == SelectionClear) {
        auto& c = Clip();
        std::lock_guard<std::recursive_mutex> lk(c.mutex);
        if (ev.xselectionclear.selection == x.clipboard) {
            // Another program copied something: our clipboard data is replaced, as on Windows
            c.owner = false;
            FreeClipboardMap(c.data);
            c.targetsTick = 0;
        }
        return;
    }

    HWND hwnd = nullptr;
    std::shared_ptr<WndRecord> rec;
    {
        std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
        auto it = g_xwinToHwnd.find(ev.xany.window);
        if (it != g_xwinToHwnd.end()) {
            hwnd = it->second;
            rec = FindWnd(hwnd);
        }
    }
    if (!hwnd || !rec) return;

    switch (ev.type) {
    case Expose:
        if (ev.xexpose.count == 0) {
            if (rec->xpixmap != 0 && rec->xgc && x.CopyArea &&
                rec->pixW == static_cast<uint32_t>(rec->width) &&
                rec->pixH == static_cast<uint32_t>(rec->height)) {
                x.CopyArea(x.dpy, rec->xpixmap, rec->xwin, rec->xgc, 0, 0, rec->pixW, rec->pixH, 0, 0);
                x.Flush(x.dpy);
            } else {
                rec->needsPaint = true;
            }
        }
        break;

    case MapNotify:
        rec->iconic = false;
        rec->needsPaint = true;
        break;

    case UnmapNotify:
        // Minimized: no repainting until it is shown again
        if (rec->visible) rec->iconic = true;
        break;

    case ConfigureNotify: {
        const int nw = std::max(1, ev.xconfigure.width);
        const int nh = std::max(1, ev.xconfigure.height);
        // A reparenting window manager reports the position inside its frame: the screen position comes from its own
        // (synthetic) notifications, or is asked from the server
        if (ev.xconfigure.send_event) {
            rec->x = ev.xconfigure.x;
            rec->y = ev.xconfigure.y;
        } else if (x.TranslateCoordinates) {
            int rx = 0, ry = 0;
            Window child = 0;
            if (x.TranslateCoordinates(x.dpy, rec->xwin, x.root, 0, 0, &rx, &ry, &child)) {
                rec->x = rx;
                rec->y = ry;
            }
        }
        if (nw != rec->width || nh != rec->height) {
            rec->width = nw;
            rec->height = nh;
            rec->needsPaint = true;
            PostMessageEx(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(nw, nh), 0);
        }
        break;
    }

    case FocusIn:
    case FocusOut: {
        if (ev.xfocus.detail == NotifyInferior || ev.xfocus.detail == NotifyPointer ||
            ev.xfocus.mode == NotifyGrab || ev.xfocus.mode == NotifyUngrab) {
            break;
        }
        // Keys held while the focus moved (Alt+Tab, a shortcut that switched windows) are not held here any more
        std::fill(std::begin(g_keyStates), std::end(g_keyStates), static_cast<SHORT>(0));
        std::fill(std::begin(g_liveKeyStates), std::end(g_liveKeyStates), static_cast<SHORT>(0));
        // The focus moves when this reaches the front of the queue (ApplyXFocus), after the keys typed before it:
        // a name typed quickly before switching to another program is complete when the rename is committed
        PostMessageEx(hwnd, WM_NULL, (ev.type == FocusIn) ? 1 : 0, 0, kMsgXFocus);
        break;
    }

    case MotionNotify: {
        g_cursorScreen = { ev.xmotion.x_root, ev.xmotion.y_root };
        const DWORD mods = ModifiersFromXState(ev.xmotion.state);
        ApplyModifierSnapshot(g_liveKeyStates, mods);
        if (!rec->enabled) break;
        const int mx = ev.xmotion.x, my = ev.xmotion.y;
        if (g_editDragHwnd && (ev.xmotion.state & Button1Mask)) {
            // Dragging in an EDIT selects (in queue order, BuiltinEditWndProc)
            auto ed = FindWnd(g_editDragHwnd);
            if (ed && ed->visible && ed->parent == hwnd) {
                PostMessageEx(g_editDragHwnd, WM_MOUSEMOVE, ModStateToMkFlags(ev.xmotion.state),
                              MAKELPARAM(mx - ed->x, my - ed->y), kMsgHasKeyState | mods);
                break;
            }
            g_editDragHwnd = nullptr;          // the EDIT was hidden or went away: that drag is over
        }
        if (!g_captureHwnd) {
            // The shape of the pointer, as the window wants it (resize arrows over splitters)
            g_cursorXwin = rec->xwin;
            auto ch = ChildControlAt(hwnd, mx, my);
            if (ch && ch->isEdit) ApplyCursor(32513);                       // IDC_IBEAM
            else SendMessageW(hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        }
        PostMouseInput(hwnd, rec, WM_MOUSEMOVE, ModStateToMkFlags(ev.xmotion.state), mx, my, mods);
        break;
    }

    case ButtonPress:
    case ButtonRelease: {
        g_cursorScreen = { ev.xbutton.x_root, ev.xbutton.y_root };
        const DWORD mods = ModifiersFromXState(ev.xbutton.state);
        ApplyModifierSnapshot(g_liveKeyStates, mods);
        const bool down = (ev.type == ButtonPress);
        const unsigned int button = ev.xbutton.button;
        const int mx = ev.xbutton.x;
        const int my = ev.xbutton.y;
        WPARAM mk = ModStateToMkFlags(ev.xbutton.state);
        if (down && button <= Button3) g_lastButtonPressTime = ev.xbutton.time;
        // A drag in an EDIT ends with the left button's release, even one that reaches a window disabled meanwhile (a
        // message box opened during the drag); a new left press means any earlier drag is over
        if (button == Button1 && (down || !rec->enabled)) g_editDragHwnd = nullptr;
        if (!rec->enabled) {
            // The owner of an open dialog takes no input: a click brings the dialog (or the file picker) to the front
            if (down) {
                if (g_zenityOwner == hwnd) ActivateZenityPicker();
                else RaiseModalDialogOf(hwnd);
            }
            break;
        }
        if (button == Button4 || button == Button5) {
            if (down) {
                const short delta = (button == Button4) ? WHEEL_DELTA : -WHEEL_DELTA;
                HWND wheelTarget = hwnd;
                if (auto ch = ChildControlAt(hwnd, mx, my); ch && ch->isStatic) wheelTarget = ch->hwnd;
                PostMessageEx(wheelTarget, WM_MOUSEWHEEL, MAKEWPARAM(mk, static_cast<USHORT>(delta)),
                              MAKELPARAM(ev.xbutton.x_root, ev.xbutton.y_root), kMsgHasKeyState | mods);
            }
            break;
        }
        if (button == 6 || button == 7) break;                  // horizontal scrolling: not used
        if (button == 8 || button == 9) {                       // the mouse's Back / Forward thumb buttons
            const WORD xb = (button == 8) ? XBUTTON1 : XBUTTON2;
            PostMessageEx(hwnd, down ? WM_XBUTTONDOWN : WM_XBUTTONUP, MAKEWPARAM(mk, xb), MAKELPARAM(mx, my), kMsgHasKeyState | mods);
            break;
        }
        if (!down && g_editDragHwnd) {
            const HWND dragEdit = g_editDragHwnd;
            g_editDragHwnd = nullptr;
            if (button == Button1) {
                // The end of a drag in an EDIT: its release goes to the EDIT too, as on Win32
                if (auto er = FindWnd(dragEdit)) {
                    PostMessageEx(dragEdit, WM_LBUTTONUP, mk & ~static_cast<WPARAM>(MK_LBUTTON),
                                  MAKELPARAM(mx - er->x, my - er->y), kMsgHasKeyState | mods);
                }
                break;
            }
        }

        // Double clicks are timed by the X server, not by when the event was read (a slow paint would split them)
        bool dbl = false;
        if (down && button == Button1) {
            const Time t = ev.xbutton.time;
            dbl = g_lastLClickValid && static_cast<Time>(t - g_lastLClickTime) <= static_cast<Time>(GetDoubleClickTime()) &&
                  std::abs(mx - g_lastLClickX) <= 4 && std::abs(my - g_lastLClickY) <= 4;
            g_lastLClickValid = !dbl;
            g_lastLClickTime = t;
            g_lastLClickX = mx;
            g_lastLClickY = my;
        }

        // A press on a visible EDIT goes to the EDIT (focus, caret, selection); the window under it gets nothing. The
        // EDIT handles it when it is taken from the queue, after the keys typed before the click.
        if (down && !g_captureHwnd) {
            auto ch = ChildControlAt(hwnd, mx, my);
            if (ch && ch->isEdit) {
                if (button == Button1) {
                    if (!dbl) g_editDragHwnd = ch->hwnd;
                    PostMessageEx(ch->hwnd, dbl ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN, mk | MK_LBUTTON,
                                  MAKELPARAM(mx - ch->x, my - ch->y), kMsgHasKeyState | mods);
                }
                break;
            }
        }

        if (button == Button1) {
            if (down) PostMouseInput(hwnd, rec, dbl ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN, mk | MK_LBUTTON, mx, my, mods);
            else      PostMouseInput(hwnd, rec, WM_LBUTTONUP, mk & ~static_cast<WPARAM>(MK_LBUTTON), mx, my, mods);
        } else if (button == Button2) {
            if (down) PostMouseInput(hwnd, rec, WM_MBUTTONDOWN, mk | MK_MBUTTON, mx, my, mods);
            else      PostMouseInput(hwnd, rec, WM_MBUTTONUP, mk & ~static_cast<WPARAM>(MK_MBUTTON), mx, my, mods);
        } else if (button == Button3) {
            if (down) PostMouseInput(hwnd, rec, WM_RBUTTONDOWN, mk | MK_RBUTTON, mx, my, mods);
            else      PostMouseInput(hwnd, rec, WM_RBUTTONUP, mk & ~static_cast<WPARAM>(MK_RBUTTON), mx, my, mods);
        }
        break;
    }

    case KeyPress:
    case KeyRelease: {
        char strBuf[32] = {};
        KeySym ks = NoSymbol;
        const int nChars = x.LookupString ? x.LookupString(&ev.xkey, strBuf, sizeof(strBuf) - 1, &ks, nullptr) : 0;
        // The virtual key names the key (unshifted, first layout group), as on Win32. A keypad key is named by what
        // NumLock (and Shift) made of it, as on Win32 too: KP_1 -> VK_NUMPAD1 and KP_Decimal -> VK_DECIMAL with
        // NumLock on, KP_End -> VK_END and KP_Delete -> VK_DELETE with it off (the unshifted keysym of a keypad key
        // is always the navigation key, which deleted files when the keypad's decimal key was typed)
        KeySym baseKs = x.LookupKeysym ? x.LookupKeysym(&ev.xkey, 0) : ks;
        if (IsKeypadKey(baseKs) && ks != NoSymbol) baseKs = ks;
        int vk = KeySymToVk(baseKs);
        if (vk == 0) vk = KeySymToVk(ks);
        const bool down = (ev.type == KeyPress);
        DWORD mods = ModifiersFromXState(ev.xkey.state);            // the state before this event
        const DWORD ownBit = (vk == VK_SHIFT) ? kMsgShift : (vk == VK_CONTROL) ? kMsgControl : (vk == VK_MENU) ? kMsgAlt : 0;
        if (ownBit) mods = down ? (mods | ownBit) : (mods & ~ownBit);
        ApplyModifierSnapshot(g_liveKeyStates, mods);
        if (vk > 0 && vk < 256 && !ownBit) g_liveKeyStates[vk] = down ? static_cast<SHORT>(0x8000) : 0;
        if (!rec->enabled) break;                                    // the owner of an open dialog gets no keys
        // Posted to the top-level window and given to the window that has the focus when it is taken from the queue
        const DWORD lp = kMsgHasKeyState | kMsgToFocus | mods;
        if (vk != 0) {
            PostMessageEx(hwnd, down ? WM_KEYDOWN : WM_KEYUP, static_cast<WPARAM>(vk), down ? 1 : static_cast<LPARAM>(0xC0000001u), lp);
        }
        if (down) {
            const unsigned char c0 = static_cast<unsigned char>(strBuf[0]);
            if (nChars == 1 && c0 == 0x7F) {
                // Delete types nothing on Win32 (XLookupString gives DEL for it)
            } else if (nChars == 1 && c0 < 0x20) {
                // Control characters as XLookupString makes them: Ctrl+A/C/V/X, Backspace, Tab, Enter, Escape
                PostMessageEx(hwnd, WM_CHAR, static_cast<WPARAM>(c0), 1, lp);
            } else if (!(mods & (kMsgControl | kMsgAlt))) {
                // A character: from the keysym (a Polish letter has no Latin-1 byte), Ctrl/Alt combinations type none
                const uint32_t uc = KeySymToUnicode(ks);
                if (uc >= 0x20 && uc != 0x7F) PostMessageEx(hwnd, WM_CHAR, static_cast<WPARAM>(uc), 1, lp);
            }
        }
        break;
    }

    case ClientMessage:
        if (x.netWmPing && ev.xclient.message_type == x.wmProtocols &&
            static_cast<Atom>(ev.xclient.data.l[0]) == x.netWmPing) {
            // The window manager checks that the app still responds: the answer goes back to the root window
            if (x.SendEvent) {
                XEvent reply = ev;
                reply.xclient.window = x.root;
                x.SendEvent(x.dpy, x.root, False, SubstructureNotifyMask | SubstructureRedirectMask, &reply);
                x.Flush(x.dpy);
            }
        } else if (x.wmDeleteWindow && static_cast<Atom>(ev.xclient.data.l[0]) == x.wmDeleteWindow) {
            // The window manager's close button: not while the window is disabled by an open dialog. While a file
            // picker (zenity) holds it, the close button cancels the picker, which may be hidden behind the window.
            if (rec->enabled) PostMessageEx(hwnd, WM_CLOSE, 0, 0, 0);
            else if (g_zenityOwner == hwnd && g_zenityStop) g_zenityStop->store(true);
            else RaiseModalDialogOf(hwnd);
        }
        break;

    default:
        break;
    }
}

void PumpX11AndTimers(ULONGLONG* outNextTimerWaitMs = nullptr) {
    if (outNextTimerWaitMs) *outNextTimerWaitMs = 50;

    // 1. Drain X11 server events if connected
    auto& x = X11();
    while (x.ready && x.dpy && x.Pending(x.dpy) > 0) {
        XEvent ev = {};
        x.NextEvent(x.dpy, &ev);
        HandleX11Event(ev);
    }

    // 2. Fire due Win32 timers & check optional EXPLORER2_AUTO_CLOSE_MS smoke-test hook
    const ULONGLONG now = GetTickCount64();
    ULONGLONG minWait = 50;
    {
        std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
        for (auto& [tk, te] : g_timers) {
            if (now >= te.nextFireTick) {
                te.nextFireTick = now + std::max(1u, te.intervalMs);
                if (te.proc) te.proc(tk.hwnd, WM_TIMER, tk.id, static_cast<DWORD>(now));
                else PostMessageW(tk.hwnd, WM_TIMER, tk.id, 0);
            }
            const ULONGLONG rem = (te.nextFireTick > now) ? (te.nextFireTick - now) : 1ULL;
            minWait = std::min(minWait, rem);
        }

        static const int s_autoCloseMs = []() {
            const char* e = std::getenv("EXPLORER2_AUTO_CLOSE_MS");
            return (e && *e) ? std::atoi(e) : 0;
        }();
        if (s_autoCloseMs > 0) {
            for (auto& [hw, r] : g_windows) {
                if (r->parent == nullptr && r->visible && r->shownAtTick > 0 && r->paintedFrames >= 1 &&
                    (now - r->shownAtTick) >= static_cast<ULONGLONG>(s_autoCloseMs)) {
                    r->shownAtTick = 0; // fire once
                    PostMessageW(hw, WM_CLOSE, 0, 0);
                }
            }
        }
    }
    if (outNextTimerWaitMs) *outNextTimerWaitMs = std::clamp<ULONGLONG>(minWait, 5ULL, 50ULL);

    // 3. WM_PAINT when nothing but timers is waiting (timers are handed out after it, see FindNextMessage), for every
    //    shown top-level window that needs it (a minimized one does not)
    bool onlyLowPriority = true;
    {
        std::lock_guard<std::mutex> lk(g_msgMutex);
        for (const auto& qm : g_msgQueue) {
            if (qm.message != WM_TIMER && qm.message != WM_PAINT) {
                onlyLowPriority = false;
                break;
            }
        }
    }
    if (onlyLowPriority) {
        std::vector<HWND> toPaint;
        {
            std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
            for (auto& [hw, r] : g_windows) {
                if (r->visible && r->needsPaint && r->topLevel && !r->iconic) {
                    r->needsPaint = false;
                    toPaint.push_back(hw);
                }
            }
        }
        for (HWND hw : toPaint) PostMessageEx(hw, WM_PAINT, 0, 0, 0);
    }
}

} // namespace

namespace Explorer2::Hal {

void PresentHwndFramebuffer(HWND hwnd, const uint8_t* bgra, uint32_t w, uint32_t h) {
    if (!hwnd || !bgra || w == 0 || h == 0) return;
    auto rec = FindWnd(hwnd);
    if (!rec) return;

    rec->lastFrame.assign(bgra, bgra + static_cast<size_t>(w) * h * 4u);
    CompositeChildEdits(*rec, rec->lastFrame, w, h);
    ++rec->paintedFrames;

    static const char* s_dumpBmp = std::getenv("EXPLORER2_DUMP_BMP");
    if (s_dumpBmp && *s_dumpBmp && rec->parent == nullptr) {
        WriteBmp24Or32(s_dumpBmp, rec->lastFrame.data(), w, h);
    }

    auto& x = X11();
    if (x.ready && x.dpy && rec->xwin != 0) {
        if (!rec->xgc) rec->xgc = x.CreateGC(x.dpy, rec->xwin, 0, nullptr);
        if (rec->xgc) {
            Visual* vis = DefaultVisual(x.dpy, x.screen);
            const unsigned int depth = static_cast<unsigned int>(DefaultDepth(x.dpy, x.screen));
            const size_t rowBytes = static_cast<size_t>(w) * 4u;

            // Maintain server-side offscreen Pixmap backbuffer so X11/XWayland composites completed frames atomically
            if (x.CreatePixmap && x.CopyArea) {
                if (rec->xpixmap == 0 || rec->pixW != w || rec->pixH != h) {
                    if (rec->xpixmap != 0 && x.FreePixmap) {
                        x.FreePixmap(x.dpy, rec->xpixmap);
                    }
                    rec->xpixmap = x.CreatePixmap(x.dpy, rec->xwin, w, h, depth);
                    rec->pixW = w;
                    rec->pixH = h;
                    rec->prevSentFrame.clear();
                }
            }

            uint32_t dirtyY0 = 0;
            uint32_t dirtyY1 = h;
            if (rec->prevSentFrame.size() == rec->lastFrame.size() && rec->xpixmap != 0) {
                dirtyY0 = h;
                dirtyY1 = 0;
                for (uint32_t y = 0; y < h; ++y) {
                    const size_t off = static_cast<size_t>(y) * rowBytes;
                    if (std::memcmp(rec->lastFrame.data() + off, rec->prevSentFrame.data() + off, rowBytes) != 0) {
                        if (dirtyY0 == h) dirtyY0 = y;
                        dirtyY1 = y + 1;
                    }
                }
                if (dirtyY0 >= dirtyY1) {
                    return; // Frame pixels unchanged — zero X11 upload needed
                }
            }
            rec->prevSentFrame = rec->lastFrame;

            const uint32_t dirtyH = dirtyY1 - dirtyY0;
            char* stripPtr = reinterpret_cast<char*>(rec->lastFrame.data() + static_cast<size_t>(dirtyY0) * rowBytes);
            XImage* img = x.CreateImage(
                x.dpy, vis, depth, ZPixmap, 0,
                stripPtr, w, dirtyH, 32, static_cast<int>(rowBytes));
            if (img) {
                if (rec->xpixmap != 0 && x.CopyArea) {
                    x.PutImage(x.dpy, rec->xpixmap, rec->xgc, img, 0, 0, 0, static_cast<int>(dirtyY0), w, dirtyH);
                    x.CopyArea(x.dpy, rec->xpixmap, rec->xwin, rec->xgc,
                               0, static_cast<int>(dirtyY0), w, dirtyH,
                               0, static_cast<int>(dirtyY0));
                } else {
                    x.PutImage(x.dpy, rec->xwin, rec->xgc, img, 0, 0, 0, static_cast<int>(dirtyY0), w, dirtyH);
                }
                img->data = nullptr; // Owned by rec->lastFrame
                XDestroyImage(img);
                x.Flush(x.dpy);
            }
        }
    }
}

} // namespace Explorer2::Hal

// ==============================================================================================================
// 3. Strong Overrides of Win32 Windowing, Message Loop, Menus, Clipboard, Dialogs & Shell DataObject
// ==============================================================================================================
WORD RegisterClassExW(const WNDCLASSEXW* lpwcx) {
    if (!lpwcx || !lpwcx->lpszClassName) return 0;
    std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
    WNDCLASSEXW copy = *lpwcx;
    std::wstring key = Explorer2::PathUtils::ToLower(lpwcx->lpszClassName);
    g_wndClasses[key] = copy;
    return 1;
}

HWND CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle,
                     int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    if (!(dwStyle & WS_CHILD) && hWndParent == nullptr && !IsCliHeadlessMode() && !X11().EnsureConnected()) {
        // No display (a text console, ssh without X forwarding, root on a Wayland desktop): the app would run with
        // no window at all and never end, so the main window is not created and the app exits
        static bool s_told = false;
        if (!s_told) {
            s_told = true;
            const char* d = std::getenv("DISPLAY");
            std::fprintf(stderr, "Explorer2: cannot open the X display (DISPLAY=%s)\n", (d && *d) ? d : "not set");
        }
        return nullptr;
    }
    auto rec = std::make_shared<WndRecord>();
    HWND hwnd = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
        hwnd = reinterpret_cast<HWND>(++g_nextHwndId);
        rec->hwnd      = hwnd;
        rec->parent    = (hWndParent == HWND_MESSAGE) ? HWND_MESSAGE : hWndParent;
        rec->className = lpClassName ? lpClassName : L"STATIC";
        rec->text      = lpWindowName ? lpWindowName : L"";
        rec->style     = dwStyle;
        rec->exStyle   = dwExStyle;
        rec->x         = (X == CW_USEDEFAULT) ? 80 : X;
        rec->y         = (Y == CW_USEDEFAULT) ? 60 : Y;
        rec->width     = (nWidth == CW_USEDEFAULT || nWidth <= 0) ? 1240 : nWidth;
        rec->height    = (nHeight == CW_USEDEFAULT || nHeight <= 0) ? 760 : nHeight;
        rec->ctrlId    = static_cast<int>(reinterpret_cast<intptr_t>(hMenu));
        rec->hInst     = hInstance;
        rec->visible   = (dwStyle & WS_VISIBLE) != 0;

        const std::wstring clsLow = Explorer2::PathUtils::ToLower(rec->className);
        if (clsLow == L"edit") {
            rec->isEdit  = true;
            rec->wndProc = BuiltinEditWndProc;
        } else if (clsLow == L"button") {
            rec->isButton = true;
            rec->wndProc = BuiltinButtonWndProc;
        } else if (clsLow == L"static") {
            rec->isStatic = true;
            rec->wndProc = BuiltinStaticWndProc;
        } else {
            auto it = g_wndClasses.find(clsLow);
            rec->wndProc = (it != g_wndClasses.end() && it->second.lpfnWndProc) ? it->second.lpfnWndProc : DefWindowProcW;
        }

        // Not WS_CHILD: a top-level window. With a parent HWND it is an owned window (a dialog): Win32 keeps it above
        // its owner, so its X window is transient for the owner's
        rec->topLevel = !(dwStyle & WS_CHILD) && hWndParent != HWND_MESSAGE;
        rec->isDialog = rec->topLevel && hWndParent != nullptr;
        if (rec->topLevel && !g_uiThreadKnown) {
            g_uiThread = std::this_thread::get_id();
            g_uiThreadKnown = true;
        }

        // Create real X11 top-level window when $DISPLAY is available (skip message-only & child windows)
        if (rec->topLevel && X11().EnsureConnected()) {
            auto& x = X11();
            rec->xwin = x.CreateSimpleWindow(
                x.dpy, x.root, rec->x, rec->y,
                static_cast<unsigned int>(rec->width), static_cast<unsigned int>(rec->height),
                0, BlackPixel(x.dpy, x.screen), WhitePixel(x.dpy, x.screen));
            if (rec->xwin != 0) {
                g_xwinToHwnd[rec->xwin] = hwnd;
                if (x.ChangeWindowAttributes) {
                    XSetWindowAttributes swa = {};
                    swa.background_pixmap = None;
                    swa.bit_gravity = NorthWestGravity;
                    x.ChangeWindowAttributes(x.dpy, rec->xwin, CWBackPixmap | CWBitGravity, &swa);
                }
                x.SelectInput(x.dpy, rec->xwin,
                              ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                              ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask);
                if (x.SetWMProtocols && x.wmDeleteWindow) {
                    // _NET_WM_PING: the window manager offers "Force Quit" when the event loop stops answering
                    Atom protocols[2] = { x.wmDeleteWindow, x.netWmPing };
                    x.SetWMProtocols(x.dpy, rec->xwin, protocols, x.netWmPing ? 2 : 1);
                }
                const std::string titleUtf8 = Explorer2::PathUtils::WideToUtf8(rec->text);
                if (x.StoreName) x.StoreName(x.dpy, rec->xwin, titleUtf8.c_str());
                if (x.ChangeProperty && x.netWmName && x.utf8String) {
                    x.ChangeProperty(x.dpy, rec->xwin, x.netWmName, x.utf8String, 8, PropModeReplace,
                                     reinterpret_cast<const unsigned char*>(titleUtf8.c_str()),
                                     static_cast<int>(titleUtf8.size()));
                }
                // WM_CLASS (the desktop groups the windows and finds the app's icon by it) and _NET_WM_PID
                if (x.SetClassHint) {
                    char resName[] = "explorer2";
                    char resClass[] = "Explorer2";
                    XClassHint ch = { resName, resClass };
                    x.SetClassHint(x.dpy, rec->xwin, &ch);
                }
                if (x.ChangeProperty && x.netWmPid) {
                    const long pid = static_cast<long>(::getpid());
                    x.ChangeProperty(x.dpy, rec->xwin, x.netWmPid, XA_CARDINAL, 32, PropModeReplace,
                                     reinterpret_cast<const unsigned char*>(&pid), 1);
                    // The window manager kills a hung window by its PID only when it runs on this machine
                    char host[256] = {};
                    if (::gethostname(host, sizeof(host) - 1) == 0 && host[0]) {
                        x.ChangeProperty(x.dpy, rec->xwin, XA_WM_CLIENT_MACHINE, XA_STRING, 8, PropModeReplace,
                                         reinterpret_cast<const unsigned char*>(host), static_cast<int>(std::strlen(host)));
                    }
                }
                if (rec->isDialog) {
                    auto owner = g_windows.find(TopLevelOf(hWndParent));
                    if (owner != g_windows.end() && owner->second->xwin && x.SetTransientForHint) {
                        x.SetTransientForHint(x.dpy, rec->xwin, owner->second->xwin);
                    }
                    if (x.ChangeProperty && x.netWmWindowType && x.netWmWindowTypeDialog) {
                        const long type = static_cast<long>(x.netWmWindowTypeDialog);
                        x.ChangeProperty(x.dpy, rec->xwin, x.netWmWindowType, XA_ATOM, 32, PropModeReplace,
                                         reinterpret_cast<const unsigned char*>(&type), 1);
                    }
                }
                x.Flush(x.dpy);
            }
        }

        g_windows[hwnd] = rec;
    }

    CREATESTRUCTW cs = {};
    cs.lpCreateParams = lpParam;
    cs.hInstance      = hInstance;
    cs.hMenu          = hMenu;
    cs.hwndParent     = hWndParent;
    cs.cy             = rec->height;
    cs.cx             = rec->width;
    cs.y              = rec->y;
    cs.x              = rec->x;
    cs.style          = static_cast<LONG>(dwStyle);
    cs.lpszName       = rec->text.c_str();
    cs.lpszClass      = rec->className.c_str();
    cs.dwExStyle      = dwExStyle;

    if (rec->wndProc) {
        rec->wndProc(hwnd, WM_NCCREATE, 0, reinterpret_cast<LPARAM>(&cs));
        rec->wndProc(hwnd, WM_CREATE, 0, reinterpret_cast<LPARAM>(&cs));
    }
    if (dwStyle & WS_VISIBLE) {
        // Created visible (a dialog): shown now, as Win32 does after WM_CREATE; a visible control repaints its parent
        if (rec->topLevel && rec->xwin != 0) {
            rec->visible = false;
            ShowWindow(hwnd, SW_SHOW);
        } else if (!rec->topLevel && rec->parent && rec->parent != HWND_MESSAGE) {
            InvalidateRect(rec->parent, nullptr, FALSE);
        }
    }
    return hwnd;
}

BOOL DestroyWindow(HWND hWnd) {
    auto rec = FindWnd(hWnd);
    if (!rec) return FALSE;
    // A window that goes away cannot keep the keyboard focus or the mouse capture
    if (g_focusedHwnd && (g_focusedHwnd == hWnd || (rec->topLevel && TopLevelOf(g_focusedHwnd) == hWnd))) g_focusedHwnd = nullptr;
    if (g_captureHwnd == hWnd) g_captureHwnd = nullptr;
    if (g_editDragHwnd == hWnd) g_editDragHwnd = nullptr;
    CallWindowChain(rec, hWnd, WM_DESTROY, 0, 0);

    std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
    // Destroy child windows (and windows it owns)
    std::vector<HWND> kids;
    for (const auto& [ch, cr] : g_windows) if (cr->parent == hWnd) kids.push_back(ch);
    for (HWND kh : kids) DestroyWindow(kh);
    CallWindowChain(rec, hWnd, WM_NCDESTROY, 0, 0);
    rec->subclasses.clear();
    if (rec->xwin != 0 && rec->xwin == g_cursorXwin) {
        g_cursorXwin = 0;
        g_cursorShape = 0;
    }
    if (rec->xwin != 0) g_cursorShapeOf.erase(rec->xwin);

    // Remove timers
    for (auto it = g_timers.begin(); it != g_timers.end(); ) {
        if (it->first.hwnd == hWnd) it = g_timers.erase(it);
        else ++it;
    }

    auto& x = X11();
    if (x.ready && x.dpy && rec->xwin != 0) {
        if (rec->xpixmap != 0 && x.FreePixmap) {
            x.FreePixmap(x.dpy, rec->xpixmap);
            rec->xpixmap = 0;
        }
        if (rec->xgc && x.FreeGC) x.FreeGC(x.dpy, rec->xgc);
        g_xwinToHwnd.erase(rec->xwin);
        if (x.DestroyWindow) x.DestroyWindow(x.dpy, rec->xwin);
        x.Flush(x.dpy);
    }
    if (g_focusedHwnd == hWnd) g_focusedHwnd = nullptr;
    if (g_captureHwnd == hWnd) g_captureHwnd = nullptr;
    g_windows.erase(hWnd);
    return TRUE;
}

// Before a top-level window is first shown: its size hints for the window manager; a main window that would not fit
// the work area (a 1024 x 768 screen) starts maximized, so the window manager fits it with its title bar
void PrepareFirstMap(WndRecord& rec, int nCmdShow) {
    auto& x = X11();
    if (!x.ready || !x.dpy || rec.xwin == 0) return;
    if (x.SetWMNormalHints) {
        XSizeHints hints = {};
        hints.flags = PPosition | PSize | PMinSize;
        hints.x = rec.x;
        hints.y = rec.y;
        hints.width = rec.width;
        hints.height = rec.height;
        hints.min_width = std::min(rec.width, 480);
        hints.min_height = std::min(rec.height, 320);
        if (rec.isDialog && !(rec.style & WS_THICKFRAME)) {
            hints.flags |= PMaxSize;
            hints.min_width = hints.max_width = rec.width;
            hints.min_height = hints.max_height = rec.height;
        }
        x.SetWMNormalHints(x.dpy, rec.xwin, &hints);
    }
    if (!rec.isDialog) {
        const RECT wa = X11WorkArea();
        const bool tooBig = rec.width >= (wa.right - wa.left) - 8 || rec.height >= (wa.bottom - wa.top) - 24;
        if (tooBig || nCmdShow == SW_SHOWMAXIMIZED) SetNetWmState(rec, true, x.netWmStateMaxVert, x.netWmStateMaxHorz);
    }
}

BOOL ShowWindow(HWND hWnd, int nCmdShow) {
    auto rec = FindWnd(hWnd);
    if (!rec) return FALSE;
    const bool wasVisible = rec->visible;
    const bool show = (nCmdShow != SW_HIDE);
    rec->visible = show;
    if (show) {
        rec->needsPaint = true;
        if (rec->shownAtTick == 0) rec->shownAtTick = GetTickCount64();
        if (rec->topLevel && !rec->isDialog && !g_focusedHwnd) g_focusedHwnd = hWnd;
    } else if (wasVisible && g_focusedHwnd &&
               (g_focusedHwnd == hWnd || (rec->topLevel && TopLevelOf(g_focusedHwnd) == hWnd))) {
        // Hiding the focused window gives the focus to its window (a dialog's owner), as Win32 does: an EDIT hidden
        // after Escape must not keep the keyboard
        ChangeFocus(rec->topLevel ? (rec->isDialog ? rec->parent : nullptr) : TopLevelOf(hWnd));
    }
    auto& x = X11();
    if (x.ready && x.dpy && rec->xwin != 0) {
        if (show) {
            if (!rec->xmapped) PrepareFirstMap(*rec, nCmdShow);
            else if (nCmdShow == SW_SHOWMAXIMIZED) SetNetWmState(*rec, true, x.netWmStateMaxVert, x.netWmStateMaxHorz);
            if (!wasVisible || !rec->xmapped) x.MapWindow(x.dpy, rec->xwin);
            rec->xmapped = true;
            rec->iconic = false;
            if (rec->isDialog) ActivateX11Window(*rec);
        } else if (x.UnmapWindow) {
            x.UnmapWindow(x.dpy, rec->xwin);
        }
        x.Flush(x.dpy);
    }
    if (show && rec->topLevel && !rec->isDialog) {
        SendMessageW(hWnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rec->width, rec->height));
    }
    if (!rec->topLevel && rec->parent && rec->parent != HWND_MESSAGE) {
        InvalidateRect(rec->parent, nullptr, FALSE);
    }
    return TRUE;
}

BOOL UpdateWindow(HWND hWnd) {
    auto rec = FindWnd(hWnd);
    if (!rec) return FALSE;
    if (rec->visible && rec->needsPaint) {
        rec->needsPaint = false;
        SendMessageW(hWnd, WM_PAINT, 0, 0);
    }
    return TRUE;
}

BOOL IsWindow(HWND hWnd) { return FindWnd(hWnd) != nullptr ? TRUE : FALSE; }
BOOL IsWindowVisible(HWND hWnd) { auto r = FindWnd(hWnd); return (r && r->visible) ? TRUE : FALSE; }
// A disabled window gets no mouse or keyboard input (the owner while its dialog is open). Returns TRUE if it was
// disabled before, as Win32 does.
BOOL EnableWindow(HWND hWnd, BOOL bEnable) {
    auto r = FindWnd(hWnd);
    if (!r) return FALSE;
    const BOOL wasDisabled = r->enabled ? FALSE : TRUE;
    r->enabled = (bEnable != FALSE);
    if (!r->enabled && g_captureHwnd && (g_captureHwnd == hWnd || TopLevelOf(g_captureHwnd) == hWnd)) g_captureHwnd = nullptr;
    if (!r->topLevel && r->parent && r->parent != HWND_MESSAGE) InvalidateRect(r->parent, nullptr, FALSE);
    return wasDisabled;
}

BOOL SetForegroundWindow(HWND hWnd) {
    if (auto r = FindWnd(TopLevelOf(hWnd))) ActivateX11Window(*r);
    return TRUE;
}

HWND SetFocus(HWND hWnd) {
    const HWND prev = g_focusedHwnd;
    if (hWnd && !FindWnd(hWnd)) return prev;
    ChangeFocus(hWnd);
    return prev;
}
HWND GetFocus() { return g_focusedHwnd; }
HWND GetParent(HWND hWnd) { auto r = FindWnd(hWnd); return (r && r->parent != HWND_MESSAGE) ? r->parent : nullptr; }
HWND GetAncestor(HWND hWnd, UINT gaFlags) {
    if (gaFlags == GA_PARENT) {
        auto r = FindWnd(hWnd);
        return (r && !r->topLevel && r->parent != HWND_MESSAGE) ? r->parent : nullptr;
    }
    const HWND top = TopLevelOf(hWnd);
    return top ? top : hWnd;
}
HWND WindowFromPoint(POINT) { return g_focusedHwnd; }

BOOL GetClientRect(HWND hWnd, LPRECT lpRect) {
    if (!lpRect) return FALSE;
    auto r = FindWnd(hWnd);
    *lpRect = { 0, 0, r ? r->width : 1240, r ? r->height : 760 };
    return TRUE;
}

// Screen coordinates of a window's client-area origin: a top-level window's position is kept up to date from
// ConfigureNotify (no server round trip per call); a child adds its place in its parent
POINT ClientOriginOnScreen(const std::shared_ptr<WndRecord>& r) {
    POINT p = { 0, 0 };
    auto cur = r;
    for (int guard = 0; cur && guard < 32; ++guard) {
        p.x += cur->x;
        p.y += cur->y;
        if (cur->topLevel || !cur->parent || cur->parent == HWND_MESSAGE) break;
        cur = FindWnd(cur->parent);
    }
    return p;
}

BOOL GetWindowRect(HWND hWnd, LPRECT lpRect) {
    if (!lpRect) return FALSE;
    auto r = FindWnd(hWnd);
    if (!r) { *lpRect = { 0, 0, 1240, 760 }; return TRUE; }
    const POINT o = ClientOriginOnScreen(r);       // Win32: screen coordinates, for child windows too
    *lpRect = { o.x, o.y, o.x + r->width, o.y + r->height };
    return TRUE;
}

BOOL ClientToScreen(HWND hWnd, LPPOINT lpPoint) {
    if (!lpPoint) return FALSE;
    auto r = FindWnd(hWnd);
    if (!r) return TRUE;
    const POINT o = ClientOriginOnScreen(r);
    lpPoint->x += o.x;
    lpPoint->y += o.y;
    return TRUE;
}

BOOL ScreenToClient(HWND hWnd, LPPOINT lpPoint) {
    if (!lpPoint) return FALSE;
    POINT origin = { 0, 0 };
    ClientToScreen(hWnd, &origin);
    lpPoint->x -= origin.x;
    lpPoint->y -= origin.y;
    return TRUE;
}

int MapWindowPoints(HWND hWndFrom, HWND hWndTo, LPPOINT lpPoints, UINT cPoints) {
    if (!lpPoints) return 0;
    for (UINT i = 0; i < cPoints; ++i) {
        if (hWndFrom) ClientToScreen(hWndFrom, &lpPoints[i]);
        if (hWndTo)   ScreenToClient(hWndTo, &lpPoints[i]);
    }
    return 0;
}

BOOL SetWindowPos(HWND hWnd, HWND, int X, int Y, int cx, int cy, UINT uFlags) {
    auto r = FindWnd(hWnd);
    if (!r) return FALSE;
    const bool moved = !(uFlags & SWP_NOMOVE) && (X != r->x || Y != r->y);
    const bool sizeChanged = !(uFlags & SWP_NOSIZE) && (cx > 0 && cy > 0) && (cx != r->width || cy != r->height);
    if (!(uFlags & SWP_NOMOVE)) { r->x = X; r->y = Y; }
    if (!(uFlags & SWP_NOSIZE) && cx > 0 && cy > 0) { r->width = cx; r->height = cy; }
    const bool wasVisible = r->visible;
    if (uFlags & SWP_SHOWWINDOW) r->visible = true;
    if (uFlags & SWP_HIDEWINDOW) {
        if (wasVisible && g_focusedHwnd == hWnd) ChangeFocus(TopLevelOf(r->parent ? r->parent : hWnd));
        r->visible = false;
    }
    const bool shownChanged = (r->visible != wasVisible);

    auto& x = X11();
    if (x.ready && x.dpy && r->xwin != 0 && x.MoveResizeWindow && (moved || sizeChanged)) {
        x.MoveResizeWindow(x.dpy, r->xwin, r->x, r->y,
                           static_cast<unsigned int>(std::max(1, r->width)),
                           static_cast<unsigned int>(std::max(1, r->height)));
        x.Flush(x.dpy);
    }
    if (sizeChanged && r->topLevel && !r->isDialog) {
        SendMessageW(hWnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r->width, r->height));
    }
    // Repaint only when something changed: a window that calls SetWindowPos with its current place from every paint
    // (the text editor's EDIT) must not repaint forever
    if (moved || sizeChanged || shownChanged) {
        if (!r->topLevel && r->parent && r->parent != HWND_MESSAGE) InvalidateRect(r->parent, nullptr, FALSE);
        else InvalidateRect(hWnd, nullptr, FALSE);
    }
    return TRUE;
}

BOOL GetWindowPlacement(HWND hWnd, WINDOWPLACEMENT* lpwndpl) {
    if (!lpwndpl) return FALSE;
    std::memset(lpwndpl, 0, sizeof(*lpwndpl));
    lpwndpl->length = sizeof(*lpwndpl);
    lpwndpl->showCmd = SW_SHOWNORMAL;
    GetWindowRect(hWnd, &lpwndpl->rcNormalPosition);
    return TRUE;
}
BOOL SetWindowPlacement(HWND hWnd, const WINDOWPLACEMENT* lpwndpl) {
    if (!lpwndpl) return FALSE;
    const RECT& rc = lpwndpl->rcNormalPosition;
    SetWindowPos(hWnd, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
    return TRUE;
}

LONG GetWindowLongW(HWND hWnd, int nIndex) {
    return static_cast<LONG>(GetWindowLongPtrW(hWnd, nIndex));
}
LONG SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong) {
    return static_cast<LONG>(SetWindowLongPtrW(hWnd, nIndex, dwNewLong));
}
LONG_PTR GetWindowLongPtrW(HWND hWnd, int nIndex) {
    auto r = FindWnd(hWnd);
    if (!r) return 0;
    if (nIndex == GWLP_USERDATA) return r->userData;
    if (nIndex == GWLP_WNDPROC)  return reinterpret_cast<LONG_PTR>(r->wndProc);
    if (nIndex == GWL_STYLE)     return static_cast<LONG_PTR>(r->style);
    if (nIndex == GWL_EXSTYLE)   return static_cast<LONG_PTR>(r->exStyle);
    return 0;
}
LONG_PTR SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR dwNewLong) {
    auto r = FindWnd(hWnd);
    if (!r) return 0;
    if (nIndex == GWLP_USERDATA) { LONG_PTR old = r->userData; r->userData = dwNewLong; return old; }
    if (nIndex == GWLP_WNDPROC)  { auto old = reinterpret_cast<LONG_PTR>(r->wndProc); r->wndProc = reinterpret_cast<WNDPROC>(dwNewLong); return old; }
    if (nIndex == GWL_STYLE) {
        const LONG_PTR old = r->style;
        r->style = static_cast<DWORD>(dwNewLong);
        // A main window that drops its caption and frame goes full screen (F11), and comes back when they return:
        // the window manager's full-screen state also hides its title bar and the desktop's panels
        if (r->topLevel && !r->isDialog && r->xwin != 0) {
            const bool hadCaption = (old & WS_CAPTION) == WS_CAPTION;
            const bool hasCaption = (r->style & WS_CAPTION) == WS_CAPTION;
            if (hadCaption && !hasCaption && !r->fullscreen) {
                r->fullscreen = true;
                SetNetWmState(*r, true, X11().netWmStateFullscreen);
            } else if (!hadCaption && hasCaption && r->fullscreen) {
                r->fullscreen = false;
                SetNetWmState(*r, false, X11().netWmStateFullscreen);
            }
        }
        return old;
    }
    if (nIndex == GWL_EXSTYLE)   { LONG_PTR old = r->exStyle; r->exStyle = static_cast<DWORD>(dwNewLong); return old; }
    return 0;
}

LRESULT CallWindowProcW(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    return lpPrevWndFunc ? lpPrevWndFunc(hWnd, Msg, wParam, lParam) : 0;
}

LRESULT DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    switch (Msg) {
    case WM_CLOSE:
        // As on Win32 only the window goes; quitting is up to its WM_DESTROY handler
        DestroyWindow(hWnd);
        return 0;
    case WM_PAINT: {
        // A dialog the HAL draws (labels, buttons, its EDITs)
        auto r = FindWnd(hWnd);
        if (r) {
            r->needsPaint = false;
            if (r->topLevel && r->isDialog && r->xwin != 0 && r->visible) PaintHalWindow(r);
        }
        return 0;
    }
    case WM_SETCURSOR:
        // The class cursor (an arrow) wherever the window does not choose another one
        if (LOWORD(lParam) == HTCLIENT) {
            ApplyCursor(32512);                     // IDC_ARROW
            return TRUE;
        }
        return 0;
    default:
        return 0;
    }
}

int GetWindowTextLengthW(HWND hWnd) {
    if (auto r = FindWnd(hWnd)) {
        if (r->isEdit) return static_cast<int>(SendMessageW(hWnd, WM_GETTEXTLENGTH, 0, 0));
        return static_cast<int>(r->text.size());
    }
    return 0;
}

int GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount) {
    if (!lpString || nMaxCount <= 0) return 0;
    if (auto r = FindWnd(hWnd)) {
        if (r->isEdit) return static_cast<int>(SendMessageW(hWnd, WM_GETTEXT, static_cast<WPARAM>(nMaxCount), reinterpret_cast<LPARAM>(lpString)));
        const size_t n = std::min<size_t>(r->text.size(), static_cast<size_t>(nMaxCount - 1));
        std::wmemcpy(lpString, r->text.data(), n);
        lpString[n] = L'\0';
        return static_cast<int>(n);
    }
    lpString[0] = L'\0';
    return 0;
}

BOOL SetWindowTextW(HWND hWnd, LPCWSTR lpString) {
    auto r = FindWnd(hWnd);
    if (!r) return FALSE;
    if (r->isEdit) {
        SendMessageW(hWnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(lpString));
        return TRUE;
    }
    r->text = lpString ? lpString : L"";
    auto& x = X11();
    if (x.ready && x.dpy && r->xwin != 0) {
        const std::string u8 = Explorer2::PathUtils::WideToUtf8(r->text);
        if (x.StoreName) x.StoreName(x.dpy, r->xwin, u8.c_str());
        if (x.ChangeProperty && x.netWmName && x.utf8String) {
            x.ChangeProperty(x.dpy, r->xwin, x.netWmName, x.utf8String, 8, PropModeReplace,
                             reinterpret_cast<const unsigned char*>(u8.c_str()), static_cast<int>(u8.size()));
        }
        x.Flush(x.dpy);
    }
    if (!r->topLevel && r->parent && r->parent != HWND_MESSAGE) InvalidateRect(r->parent, nullptr, FALSE);
    return TRUE;
}

int GetDlgCtrlID(HWND hWnd) { auto r = FindWnd(hWnd); return r ? r->ctrlId : 0; }

BOOL InvalidateRect(HWND hWnd, const RECT*, BOOL) {
    // Controls are drawn as part of their top-level window
    if (auto r = FindWnd(hWnd)) {
        if (!r->topLevel) {
            if (auto top = FindWnd(TopLevelOf(hWnd))) top->needsPaint = true;
        } else {
            r->needsPaint = true;
        }
    }
    return TRUE;
}
BOOL ValidateRect(HWND hWnd, const RECT*) {
    if (auto r = FindWnd(hWnd)) r->needsPaint = false;
    return TRUE;
}

HDC BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint) {
    auto r = FindWnd(hWnd);
    if (r) r->needsPaint = false;
    if (lpPaint) {
        std::memset(lpPaint, 0, sizeof(*lpPaint));
        lpPaint->hdc = reinterpret_cast<HDC>(1);
        lpPaint->rcPaint = { 0, 0, r ? r->width : 1240, r ? r->height : 760 };
    }
    return reinterpret_cast<HDC>(1);
}
BOOL EndPaint(HWND, const PAINTSTRUCT*) { return TRUE; }

// ---- Message Queue, Dispatch, Input & Timers -----------------------------------------------------------------
BOOL PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    return PostMessageEx(hWnd, Msg, wParam, lParam, 0);
}

void PostQuitMessage(int nExitCode) {
    PostMessageW(nullptr, WM_QUIT, static_cast<WPARAM>(nExitCode), 0);
}

BOOL PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    if (!lpMsg) return FALSE;
    PumpX11AndTimers();
    {
        std::lock_guard<std::mutex> lk(g_msgMutex);
        auto it = FindNextMessage(hWnd, wMsgFilterMin, wMsgFilterMax);
        if (it == g_msgQueue.end()) return FALSE;
        *lpMsg = *it;
        if (!(wRemoveMsg & PM_REMOVE)) return TRUE;
        g_msgQueue.erase(it);
    }
    PrepareTakenMessage(*lpMsg);
    return TRUE;
}

BOOL GetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    if (!lpMsg) return FALSE;
    for (;;) {
        ULONGLONG waitMs = 30;
        PumpX11AndTimers(&waitMs);
        {
            std::unique_lock<std::mutex> lk(g_msgMutex);
            auto it = FindNextMessage(hWnd, wMsgFilterMin, wMsgFilterMax);
            if (it != g_msgQueue.end()) {
                *lpMsg = *it;
                g_msgQueue.erase(it);
                lk.unlock();
                PrepareTakenMessage(*lpMsg);
                return (lpMsg->message == WM_QUIT) ? FALSE : TRUE;
            }
            auto& x = X11();
            if (x.ready && x.dpy) {
                lk.unlock();
                struct pollfd pfd = { ConnectionNumber(x.dpy), POLLIN, 0 };
                ::poll(&pfd, 1, static_cast<int>(std::min<ULONGLONG>(waitMs, 20ULL)));
            } else {
                g_msgCv.wait_for(lk, std::chrono::milliseconds(waitMs));
            }
        }
    }
}

BOOL TranslateMessage(const MSG*) { return TRUE; }

LRESULT DispatchMessageW(const MSG* lpMsg) {
    if (!lpMsg || !lpMsg->hwnd) return 0;
    auto r = FindWnd(lpMsg->hwnd);
    if (r && (r->wndProc || !r->subclasses.empty())) {
        return CallWindowChain(r, lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    }
    return 0;
}

LRESULT SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    auto r = FindWnd(hWnd);
    if (r && (r->wndProc || !r->subclasses.empty())) {
        return CallWindowChain(r, hWnd, Msg, wParam, lParam);
    }
    return 0;
}

// ---- Window subclassing (comctl32 SetWindowSubclass) -----------------------------------------------------------
BOOL SetWindowSubclass(HWND hWnd, SUBCLASSPROC pfnSubclass, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    auto r = FindWnd(hWnd);
    if (!r || !pfnSubclass) return FALSE;
    std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
    for (auto& sc : r->subclasses) {
        if (sc.proc == pfnSubclass && sc.id == uIdSubclass) {
            sc.refData = dwRefData;
            return TRUE;
        }
    }
    r->subclasses.push_back({ pfnSubclass, uIdSubclass, dwRefData });
    return TRUE;
}

BOOL RemoveWindowSubclass(HWND hWnd, SUBCLASSPROC pfnSubclass, UINT_PTR uIdSubclass) {
    auto r = FindWnd(hWnd);
    if (!r) return FALSE;
    std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
    auto it = std::find_if(r->subclasses.begin(), r->subclasses.end(),
                           [&](const SubclassRec& sc) { return sc.proc == pfnSubclass && sc.id == uIdSubclass; });
    if (it == r->subclasses.end()) return FALSE;
    r->subclasses.erase(it);
    return TRUE;
}

// The next procedure in the chain: the subclass installed before the one running, or the window's own procedure
LRESULT DefSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto r = FindWnd(hWnd);
    if (!r) return 0;
    for (auto fr = t_subclassFrames.rbegin(); fr != t_subclassFrames.rend(); ++fr) {
        if (fr->first != hWnd) continue;
        const size_t idx = std::min(fr->second, r->subclasses.size());
        if (idx == 0) break;
        const SubclassRec sc = r->subclasses[idx - 1];
        t_subclassFrames.push_back({ hWnd, idx - 1 });
        const LRESULT res = sc.proc(hWnd, uMsg, wParam, lParam, sc.id, sc.refData);
        t_subclassFrames.pop_back();
        return res;
    }
    return r->wndProc ? r->wndProc(hWnd, uMsg, wParam, lParam) : DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// ---- Dialog keyboard (IsDialogMessageW): Enter, Escape and Tab in a window with controls --------------------------
BOOL IsDialogMessageW(HWND hDlg, LPMSG lpMsg) {
    if (!hDlg || !lpMsg) return FALSE;
    auto dlg = FindWnd(hDlg);
    if (!dlg) return FALSE;
    if (lpMsg->message != WM_KEYDOWN && lpMsg->message != WM_CHAR) return FALSE;
    if (lpMsg->hwnd != hDlg && TopLevelOf(lpMsg->hwnd) != hDlg) return FALSE;
    const WPARAM key = lpMsg->wParam;
    if (lpMsg->message == WM_CHAR) {
        // The characters of the keys handled below are swallowed (no beep, no CR in the EDIT)
        return (key == L'\r' || key == L'\n' || key == 27 || key == L'\t') ? TRUE : FALSE;
    }
    auto focused = FindWnd(g_focusedHwnd);
    const bool focusInDlg = focused && TopLevelOf(g_focusedHwnd) == hDlg;
    auto command = [&](int id, HWND ctl) {
        PostMessageW(hDlg, WM_COMMAND, MAKEWPARAM(static_cast<WORD>(id), BN_CLICKED), reinterpret_cast<LPARAM>(ctl));
    };
    if (key == VK_ESCAPE) {
        command(IDCANCEL, nullptr);
        return TRUE;
    }
    if (key == VK_RETURN) {
        // The focused button, else the default push button, else IDOK
        if (focusInDlg && focused->isButton) {
            command(focused->ctrlId, focused->hwnd);
            return TRUE;
        }
        for (const auto& ch : ChildrenOf(hDlg)) {
            if (ch->isButton && ch->visible && ch->enabled && (ch->style & 0x0Fu) == static_cast<DWORD>(BS_DEFPUSHBUTTON)) {
                command(ch->ctrlId, ch->hwnd);
                return TRUE;
            }
        }
        command(IDOK, nullptr);
        return TRUE;
    }
    if (key == VK_TAB) {
        std::vector<std::shared_ptr<WndRecord>> stops;
        for (const auto& ch : ChildrenOf(hDlg)) {
            if (ch->visible && ch->enabled && (ch->style & WS_TABSTOP)) stops.push_back(ch);
        }
        if (stops.empty()) return TRUE;
        const bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        size_t cur = stops.size();
        for (size_t i = 0; i < stops.size(); ++i) if (stops[i]->hwnd == g_focusedHwnd) cur = i;
        size_t next = 0;
        if (cur == stops.size()) next = back ? stops.size() - 1 : 0;
        else next = back ? (cur + stops.size() - 1) % stops.size() : (cur + 1) % stops.size();
        ChangeFocus(stops[next]->hwnd);
        if (stops[next]->isEdit) SendMessageW(stops[next]->hwnd, EM_SETSEL, 0, -1);
        return TRUE;
    }
    return FALSE;
}

HWND SetCapture(HWND hWnd) { HWND prev = g_captureHwnd; g_captureHwnd = hWnd; return prev; }
BOOL ReleaseCapture() { g_captureHwnd = nullptr; return TRUE; }
HWND GetCapture() { return g_captureHwnd; }
BOOL GetCursorPos(LPPOINT lpPoint) { if (!lpPoint) return FALSE; *lpPoint = g_cursorScreen; return TRUE; }
SHORT GetKeyState(int nVirtKey) { return (nVirtKey >= 0 && nVirtKey < 256) ? g_keyStates[nVirtKey] : 0; }
SHORT GetAsyncKeyState(int vKey) { return (vKey >= 0 && vKey < 256) ? g_liveKeyStates[vKey] : 0; }

// ---- Mouse cursors -------------------------------------------------------------------------------------------
// LoadCursorW(nullptr, IDC_xxx) gives the IDC number back as the handle; SetCursor shows it on the window under
// the pointer
HCURSOR LoadCursorW(HINSTANCE, LPCWSTR lpCursorName) {
    const uintptr_t id = reinterpret_cast<uintptr_t>(lpCursorName);
    return reinterpret_cast<HCURSOR>(id < 0x10000 ? id : static_cast<uintptr_t>(32512));
}

HCURSOR SetCursor(HCURSOR hCursor) {
    const auto prev = reinterpret_cast<HCURSOR>(g_cursorShape ? g_cursorShape : 32512);
    const uintptr_t id = reinterpret_cast<uintptr_t>(hCursor);
    ApplyCursor((id >= 32512 && id < 32768) ? id : 32512);
    return prev;
}

// ---- Clipboard (X11 CLIPBOARD selection) ----------------------------------------------------------------------
BOOL OpenClipboard(HWND) {
    // What came from another program's clipboard may have changed since: converted again on demand
    auto& c = Clip();
    std::lock_guard<std::recursive_mutex> lk(c.mutex);
    FreeClipboardMap(c.imported);
    c.targetsTick = 0;
    return TRUE;
}

BOOL CloseClipboard() { return TRUE; }

BOOL EmptyClipboard() {
    auto& c = Clip();
    std::lock_guard<std::recursive_mutex> lk(c.mutex);
    FreeClipboardMap(c.data);
    FreeClipboardMap(c.imported);
    return TRUE;
}

HANDLE SetClipboardData(UINT uFormat, HANDLE hMem) {
    auto& c = Clip();
    {
        std::lock_guard<std::recursive_mutex> lk(c.mutex);
        auto it = c.data.find(uFormat);
        if (it != c.data.end() && it->second && it->second != hMem) GlobalFree(it->second);
        c.data[uFormat] = static_cast<HGLOBAL>(hMem);
    }
    // Other programs get it through the CLIPBOARD selection, which this process now owns
    auto& x = X11();
    if (x.ready && x.dpy && x.SetSelectionOwner && x.clipboard && IsUiThread()) {
        const Window w = ClipboardWindow();
        if (w) {
            x.SetSelectionOwner(x.dpy, x.clipboard, w, CurrentTime);
            const bool owned = !x.GetSelectionOwner || x.GetSelectionOwner(x.dpy, x.clipboard) == w;
            std::lock_guard<std::recursive_mutex> lk(c.mutex);
            c.owner = owned;
        }
    }
    return hMem;
}

HANDLE GetClipboardData(UINT uFormat) {
    auto& c = Clip();
    {
        std::lock_guard<std::recursive_mutex> lk(c.mutex);
        if (c.owner || !X11().ready) {
            auto it = c.data.find(uFormat);
            return (it != c.data.end()) ? it->second : nullptr;
        }
        auto imp = c.imported.find(uFormat);
        if (imp != c.imported.end()) return imp->second;
    }
    if (!IsUiThread()) return nullptr;
    HGLOBAL h = ImportForeignClipboard(uFormat);
    if (h && uFormat == CF_UNICODETEXT) {
        std::lock_guard<std::recursive_mutex> lk(c.mutex);
        c.imported[uFormat] = h;
    }
    return h;
}

BOOL IsClipboardFormatAvailable(UINT format) {
    auto& c = Clip();
    {
        std::lock_guard<std::recursive_mutex> lk(c.mutex);
        if (c.owner || !X11().ready) return c.data.count(format) ? TRUE : FALSE;
    }
    if (!IsUiThread()) return FALSE;
    auto& x = X11();
    const std::vector<Atom> targets = ForeignClipboardTargets();
    auto offers = [&](Atom a) { return a != 0 && std::find(targets.begin(), targets.end(), a) != targets.end(); };
    if (format == CF_UNICODETEXT || format == CF_TEXT) return (offers(x.utf8String) || offers(x.textPlainUtf8) || offers(XA_STRING)) ? TRUE : FALSE;
    if (format == CF_HDROP) return (offers(x.uriList) || offers(x.gnomeCopiedFiles)) ? TRUE : FALSE;
    return FALSE;
}

UINT_PTR SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc) {
    std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
    const UINT_PTR id = nIDEvent ? nIDEvent : 1u;
    const UINT interval = std::max(1u, uElapse);
    g_timers[{ hWnd, id }] = { interval, GetTickCount64() + interval, lpTimerFunc };
    return id;
}

BOOL KillTimer(HWND hWnd, UINT_PTR uIDEvent) {
    std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
    return g_timers.erase({ hWnd, uIDEvent }) > 0 ? TRUE : FALSE;
}

BOOL GetMonitorInfoW(HMONITOR, LPMONITORINFO lpmi) {
    if (!lpmi) return FALSE;
    int sw = 1920, sh = 1080;
    auto& x = X11();
    if (x.ready && x.dpy) {
        sw = std::max(640, DisplayWidth(x.dpy, x.screen));
        sh = std::max(480, DisplayHeight(x.dpy, x.screen));
    }
    lpmi->rcMonitor = { 0, 0, sw, sh };
    // The desktop's real work area (GNOME's top bar, a taskbar): what is left for windows
    lpmi->rcWork    = (x.ready && x.dpy) ? X11WorkArea() : RECT{ 0, 0, sw, std::max(440, sh - 36) };
    lpmi->dwFlags   = 1;
    return TRUE;
}

int GetSystemMetrics(int idx) {
    auto& x = X11();
    switch (idx) {
    case SM_CXSCREEN: return (x.ready && x.dpy) ? std::max(640, DisplayWidth(x.dpy, x.screen)) : 1920;
    case SM_CYSCREEN: return (x.ready && x.dpy) ? std::max(480, DisplayHeight(x.dpy, x.screen)) : 1080;
    case SM_CXICON: case SM_CYICON: return 32;
    case SM_CXSMICON: case SM_CYSMICON: return 16;
    default: return 0;
    }
}

// ---- Popup Menus ---------------------------------------------------------------------------------------------
namespace {
struct MenuItemRec {
    UINT flags = 0;
    UINT_PTR id = 0;
    std::wstring text;
    HMENU subMenu = nullptr;
};
struct MenuRec {
    std::vector<MenuItemRec> items;
};
std::mutex g_menuMutex;
std::map<HMENU, std::unique_ptr<MenuRec>> g_menus;
intptr_t g_nextMenuId = 0x40000;
} // namespace

HMENU CreatePopupMenu() {
    std::lock_guard<std::mutex> lk(g_menuMutex);
    HMENU h = reinterpret_cast<HMENU>(++g_nextMenuId);
    g_menus[h] = std::make_unique<MenuRec>();
    return h;
}
BOOL DestroyMenu(HMENU hMenu) {
    std::lock_guard<std::mutex> lk(g_menuMutex);
    if (g_menus.find(hMenu) == g_menus.end()) return FALSE;
    // The menu and its submenus, as Win32 does. Not by calling DestroyMenu again: locking the std::mutex a second
    // time from this thread hung the app whenever a menu with a submenu (New, Open with, Sort by) was closed.
    std::vector<HMENU> pending = { hMenu };
    for (size_t guard = 0; !pending.empty() && guard < 4096; ++guard) {
        const HMENU h = pending.back();
        pending.pop_back();
        auto it = g_menus.find(h);
        if (it == g_menus.end()) continue;
        for (const auto& item : it->second->items) if (item.subMenu) pending.push_back(item.subMenu);
        g_menus.erase(it);
    }
    return TRUE;
}
BOOL AppendMenuW(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCWSTR lpNewItem) {
    std::lock_guard<std::mutex> lk(g_menuMutex);
    auto it = g_menus.find(hMenu);
    if (it == g_menus.end()) return FALSE;
    MenuItemRec item;
    item.flags   = uFlags;
    item.id      = uIDNewItem;
    item.text    = (lpNewItem && !(uFlags & MF_SEPARATOR)) ? lpNewItem : L"";
    item.subMenu = (uFlags & MF_POPUP) ? reinterpret_cast<HMENU>(uIDNewItem) : nullptr;
    it->second->items.push_back(std::move(item));
    return TRUE;
}
BOOL SetMenuItemInfoW(HMENU, UINT, BOOL, LPCMENUITEMINFOW) { return TRUE; }
BOOL SetMenuDefaultItem(HMENU, UINT, UINT) { return TRUE; }
int GetMenuItemCount(HMENU hMenu) {
    std::lock_guard<std::mutex> lk(g_menuMutex);
    auto it = g_menus.find(hMenu);
    return (it != g_menus.end()) ? static_cast<int>(it->second->items.size()) : 0;
}
BOOL TrackPopupMenu(HMENU hMenu, UINT uFlags, int xPos, int yPos, int, HWND hWnd, const RECT*) {
    const char* autoClose = std::getenv("EXPLORER2_AUTO_CLOSE_MS");
    if (IsCliHeadlessMode() || (autoClose && *autoClose) || !X11().EnsureConnected()) {
        std::lock_guard<std::mutex> lk(g_menuMutex);
        (void)g_menus.find(hMenu);
        return 0;
    }

    struct SnapItem {
        UINT flags = 0;
        UINT_PTR id = 0;
        std::wstring label;
        std::wstring accel;
        int yTop = 0;
        int rowH = 26;
        int subIdx = -1;
    };
    struct SnapMenu {
        std::vector<SnapItem> items;
        int width = 220;
        int height = 8;             // all its rows
        int viewH = 8;              // the window: no taller than the screen, the rows scroll in it
    };

    std::vector<SnapMenu> menus;
    auto stripMnemonic = [](const std::wstring& raw, std::wstring& outLabel, std::wstring& outAccel) {
        outLabel.clear();
        outAccel.clear();
        const size_t tab = raw.find(L'\t');
        const std::wstring left = (tab == std::wstring::npos) ? raw : raw.substr(0, tab);
        if (tab != std::wstring::npos && tab + 1 < raw.size()) outAccel = raw.substr(tab + 1);
        for (size_t i = 0; i < left.size(); ++i) {
            if (left[i] == L'&') {
                if (i + 1 < left.size() && left[i + 1] == L'&') { outLabel.push_back(L'&'); ++i; }
            } else {
                outLabel.push_back(left[i]);
            }
        }
    };

    auto snapRecursive = [&](auto& self, HMENU hm, int depth) -> int {
        if (!hm || depth > 5) return -1;
        auto it = g_menus.find(hm);
        if (it == g_menus.end() || it->second->items.empty()) return -1;
        const int curIdx = static_cast<int>(menus.size());
        menus.push_back(SnapMenu{});
        const auto srcItems = it->second->items;
        int y = 4;
        size_t maxChars = 18;
        for (const auto& si : srcItems) {
            SnapItem di;
            di.flags = si.flags;
            di.id    = si.id;
            di.yTop  = y;
            if (si.flags & MF_SEPARATOR) {
                di.rowH = 9;
            } else {
                stripMnemonic(si.text, di.label, di.accel);
                di.rowH = 26;
                maxChars = std::max(maxChars, di.label.size() + (di.accel.empty() ? 0u : di.accel.size() + 4u));
                if (si.subMenu) di.subIdx = self(self, si.subMenu, depth + 1);
            }
            y += di.rowH;
            menus[curIdx].items.push_back(std::move(di));
        }
        menus[curIdx].height = std::max(20, y + 4);
        menus[curIdx].width  = std::clamp(static_cast<int>(maxChars * 7 + 52), 200, 440);
        return curIdx;
    };

    {
        std::lock_guard<std::mutex> lk(g_menuMutex);
        if (snapRecursive(snapRecursive, hMenu, 0) < 0 || menus.empty()) return 0;
    }

    auto& x = X11();
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d;
    Microsoft::WRL::ComPtr<IDWriteFactory> dw;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> tf;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, reinterpret_cast<void**>(d2d.GetAddressOf()));
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dw.GetAddressOf()));
    if (!wic || !d2d || !dw) return 0;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                         DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-us", &tf);
    if (!tf) return 0;
    tf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    std::vector<int> navStack = { 0 };
    int curMenu = 0;
    int hoverRow = -1;
    int scrollOff = 0;              // rows of the current menu scrolled up out of its window

    const int scrW = std::max(640, DisplayWidth(x.dpy, x.screen));
    const int scrH = std::max(480, DisplayHeight(x.dpy, x.screen));
    for (auto& m : menus) m.viewH = std::min(m.height, scrH - 8);
    auto computePopupPos = [&](int menuIdx, int& outX, int& outY) {
        const int mw = menus[menuIdx].width;
        const int mh = menus[menuIdx].viewH;
        int px = xPos;
        int py = yPos;
        if (uFlags & TPM_RIGHTALIGN)  px -= mw;
        else if (uFlags & TPM_CENTERALIGN) px -= mw / 2;
        if (uFlags & TPM_BOTTOMALIGN) py -= mh;
        // As Win32 does: a menu that does not fit below / right of the point opens above / left of it, so it does not
        // end up under the pointer that opened it
        if (py + mh > scrH - 4 && yPos - mh >= 4) py = yPos - mh;
        if (px + mw > scrW - 4 && xPos - mw >= 4) px = xPos - mw;
        outX = std::clamp(px, 4, std::max(4, scrW - mw - 4));
        outY = std::clamp(py, 4, std::max(4, scrH - mh - 4));
    };

    int winX = 0, winY = 0;
    computePopupPos(0, winX, winY);

    Window popWin = x.CreateSimpleWindow(
        x.dpy, x.root, winX, winY,
        static_cast<unsigned int>(menus[0].width), static_cast<unsigned int>(menus[0].viewH),
        0, BlackPixel(x.dpy, x.screen), WhitePixel(x.dpy, x.screen));
    if (!popWin) return 0;

    if (x.ChangeWindowAttributes) {
        XSetWindowAttributes swa = {};
        swa.override_redirect = True;
        swa.background_pixmap = None;
        x.ChangeWindowAttributes(x.dpy, popWin, CWOverrideRedirect | CWBackPixmap, &swa);
    }
    x.SelectInput(x.dpy, popWin, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask);
    if (x.MapRaised) x.MapRaised(x.dpy, popWin);
    else x.MapWindow(x.dpy, popWin);
    if (x.GrabPointer) {
        x.GrabPointer(x.dpy, popWin, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                      GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    }
    GC popGc = x.CreateGC(x.dpy, popWin, 0, nullptr);
    std::vector<uint8_t> pixels;

    auto paintPopup = [&]() {
        const auto& m = menus[curMenu];
        const UINT mw = static_cast<UINT>(m.width);
        const UINT mh = static_cast<UINT>(m.viewH);
        const float so = static_cast<float>(scrollOff);
        Microsoft::WRL::ComPtr<IWICBitmap> bmp;
        if (FAILED(wic->CreateBitmap(mw, mh, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)) || !bmp) return;
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt;
        D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
        if (FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), &rtp, &rt)) || !rt) return;

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brBg, brBorder, brHover, brAccent, brText, brMuted, brSep;
        rt->CreateSolidColorBrush(D2D1::ColorF(0xF8FAFC), &brBg);
        rt->CreateSolidColorBrush(D2D1::ColorF(0x94A3B8), &brBorder);
        rt->CreateSolidColorBrush(D2D1::ColorF(0xE0F2FE), &brHover);
        rt->CreateSolidColorBrush(D2D1::ColorF(0x0284C7), &brAccent);
        rt->CreateSolidColorBrush(D2D1::ColorF(0x0F172A), &brText);
        rt->CreateSolidColorBrush(D2D1::ColorF(0x94A3B8), &brMuted);
        rt->CreateSolidColorBrush(D2D1::ColorF(0xE2E8F0), &brSep);

        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0xFFFFFF));
        rt->DrawRectangle(D2D1::RectF(0.5f, 0.5f, mw - 0.5f, mh - 0.5f), brBorder.Get(), 1.0f);

        for (size_t i = 0; i < m.items.size(); ++i) {
            const auto& it = m.items[i];
            if (it.yTop + it.rowH - scrollOff <= 0 || it.yTop - scrollOff >= m.viewH) continue;
            if (it.flags & MF_SEPARATOR) {
                const float sy = static_cast<float>(it.yTop + it.rowH / 2) + 0.5f - so;
                rt->DrawLine(D2D1::Point2F(10.0f, sy), D2D1::Point2F(mw - 10.0f, sy), brSep.Get(), 1.0f);
                continue;
            }
            const bool disabled = (it.flags & (MF_GRAYED | MF_DISABLED)) != 0;
            const bool hovered  = (static_cast<int>(i) == hoverRow) && !disabled;
            const D2D1_RECT_F rowRc = D2D1::RectF(3.0f, static_cast<float>(it.yTop) - so, mw - 3.0f, static_cast<float>(it.yTop + it.rowH) - so);
            if (hovered) {
                rt->FillRectangle(rowRc, brHover.Get());
                rt->FillRectangle(D2D1::RectF(3.0f, rowRc.top + 3.0f, 6.0f, rowRc.bottom - 3.0f), brAccent.Get());
            }
            ID2D1SolidColorBrush* fg = disabled ? brMuted.Get() : brText.Get();
            if (it.flags & MF_CHECKED) {
                const std::wstring chk = L"\x2713";
                rt->DrawText(chk.c_str(), static_cast<UINT32>(chk.size()), tf.Get(),
                             D2D1::RectF(9.0f, rowRc.top, 25.0f, rowRc.bottom), hovered ? brAccent.Get() : fg);
            }
            rt->DrawText(it.label.c_str(), static_cast<UINT32>(it.label.size()), tf.Get(),
                         D2D1::RectF(26.0f, rowRc.top, mw - 24.0f, rowRc.bottom), fg);
            if (!it.accel.empty()) {
                tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                rt->DrawText(it.accel.c_str(), static_cast<UINT32>(it.accel.size()), tf.Get(),
                             D2D1::RectF(26.0f, rowRc.top, mw - 22.0f, rowRc.bottom), brMuted.Get());
                tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
            if (it.subIdx >= 0) {
                const std::wstring arr = L"\x25B8";
                rt->DrawText(arr.c_str(), static_cast<UINT32>(arr.size()), tf.Get(),
                             D2D1::RectF(mw - 18.0f, rowRc.top, mw - 5.0f, rowRc.bottom), fg);
            }
        }
        if (m.height > m.viewH) {
            // A menu taller than the screen (a folder with many subfolders): a thin bar shows which part is in view;
            // the wheel and the arrow keys scroll it
            const float vh = static_cast<float>(m.viewH), ch = static_cast<float>(m.height);
            const float thumbH = std::max(24.0f, vh * vh / ch);
            const float maxOff = ch - vh;
            const float thumbTop = 2.0f + (vh - 4.0f - thumbH) * (maxOff > 0.0f ? so / maxOff : 0.0f);
            rt->FillRectangle(D2D1::RectF(mw - 5.0f, thumbTop, mw - 2.0f, thumbTop + thumbH), brMuted.Get());
            rt->DrawRectangle(D2D1::RectF(0.5f, 0.5f, mw - 0.5f, mh - 0.5f), brBorder.Get(), 1.0f);
        }
        rt->EndDraw();

        pixels.resize(static_cast<size_t>(mw) * mh * 4u);
        WICRect wrc = { 0, 0, static_cast<INT>(mw), static_cast<INT>(mh) };
        bmp->CopyPixels(&wrc, mw * 4u, static_cast<UINT>(pixels.size()), pixels.data());
        if (popGc) {
            Visual* vis = DefaultVisual(x.dpy, x.screen);
            const unsigned int depth = static_cast<unsigned int>(DefaultDepth(x.dpy, x.screen));
            XImage* img = x.CreateImage(x.dpy, vis, depth, ZPixmap, 0,
                                        reinterpret_cast<char*>(pixels.data()), mw, mh, 32, static_cast<int>(mw * 4u));
            if (img) {
                x.PutImage(x.dpy, popWin, popGc, img, 0, 0, 0, 0, mw, mh);
                img->data = nullptr;
                XDestroyImage(img);
                x.Flush(x.dpy);
            }
        }
    };

    auto switchMenu = [&](int nextMenuIdx) {
        curMenu = nextMenuIdx;
        hoverRow = -1;
        scrollOff = 0;
        computePopupPos(curMenu, winX, winY);
        if (x.MoveResizeWindow) {
            x.MoveResizeWindow(x.dpy, popWin, winX, winY,
                               static_cast<unsigned int>(menus[curMenu].width),
                               static_cast<unsigned int>(menus[curMenu].viewH));
        }
        paintPopup();
    };

    auto insideMenu = [&](int lx, int ly) {
        return lx >= 0 && lx < menus[curMenu].width && ly >= 0 && ly < menus[curMenu].viewH;
    };

    auto hitTestRow = [&](int lx, int ly) -> int {
        const auto& m = menus[curMenu];
        if (!insideMenu(lx, ly)) return -1;
        const int cy = ly + scrollOff;
        for (size_t i = 0; i < m.items.size(); ++i) {
            if (cy >= m.items[i].yTop && cy < m.items[i].yTop + m.items[i].rowH) {
                if (m.items[i].flags & MF_SEPARATOR) return -1;
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    auto scrollTo = [&](int off) {
        const auto& m = menus[curMenu];
        off = std::clamp(off, 0, std::max(0, m.height - m.viewH));
        if (off == scrollOff) return false;
        scrollOff = off;
        return true;
    };
    auto scrollRowIntoView = [&](int row) {
        const auto& m = menus[curMenu];
        if (row < 0 || row >= static_cast<int>(m.items.size()) || m.height <= m.viewH) return;
        const auto& it = m.items[row];
        if (it.yTop - 4 < scrollOff) scrollTo(it.yTop - 4);
        else if (it.yTop + it.rowH + 4 > scrollOff + m.viewH) scrollTo(it.yTop + it.rowH + 4 - m.viewH);
    };

    paintPopup();

    UINT_PTR chosenCmd = 0;
    bool running = true;
    bool sawPress = false;          // a button was pressed while the menu is open
    bool movedAway = false;         // the pointer moved since the menu opened (press, drag onto an item, release)
    const int openX = g_cursorScreen.x, openY = g_cursorScreen.y;
    std::shared_ptr<WndRecord> ownerRec = FindWnd(TopLevelOf(hWnd));
    const Window ownerXwin = ownerRec ? ownerRec->xwin : 0;
    while (running && x.ready) {
        // Wait without blocking forever: the menu also ends when the owner window loses the focus
        if (x.Pending(x.dpy) <= 0) {
            struct pollfd pfd = { ConnectionNumber(x.dpy), POLLIN, 0 };
            ::poll(&pfd, 1, 100);
            if (x.Pending(x.dpy) <= 0) continue;
        }
        XEvent ev = {};
        x.NextEvent(x.dpy, &ev);
        const bool forPopup = (ev.xany.window == popWin);
        switch (ev.type) {
        case Expose:
            if (forPopup) {
                if (ev.xexpose.count == 0) paintPopup();
            } else {
                HandleX11Event(ev);
            }
            break;
        case MotionNotify: {
            if (std::abs(ev.xmotion.x_root - openX) > 4 || std::abs(ev.xmotion.y_root - openY) > 4) movedAway = true;
            const int lx = ev.xmotion.x_root - winX;
            const int ly = ev.xmotion.y_root - winY;
            const int r = hitTestRow(lx, ly);
            if (r != hoverRow) {
                hoverRow = r;
                paintPopup();
            }
            break;
        }
        case ButtonPress: {
            const int lx = ev.xbutton.x_root - winX;
            const int ly = ev.xbutton.y_root - winY;
            if (ev.xbutton.button == Button4 || ev.xbutton.button == Button5) {
                // The wheel neither selects nor closes: it scrolls a menu taller than the screen
                if (scrollTo(scrollOff + (ev.xbutton.button == Button4 ? -78 : 78))) {
                    hoverRow = hitTestRow(lx, ly);
                    paintPopup();
                }
                break;
            }
            if (ev.xbutton.button > Button3) break;
            sawPress = true;
            if (!insideMenu(lx, ly)) running = false;
            break;
        }
        case ButtonRelease: {
            // Only a release that ends a click made in the open menu (or a drag onto an item) chooses: the release of
            // the click that opened the menu must not run the item that happens to be under the pointer
            if (ev.xbutton.button > Button3 || ev.xbutton.button == Button2) break;
            // A drag onto an item: the button was held a while and the pointer moved away from where the menu opened
            // (a click that only shakes a little does not choose the row a menu clamped to the screen put under it)
            const bool dragged = movedAway && static_cast<Time>(ev.xbutton.time - g_lastButtonPressTime) >= 250;
            if (!sawPress && !dragged) break;
            const int lx = ev.xbutton.x_root - winX;
            const int ly = ev.xbutton.y_root - winY;
            const int r = hitTestRow(lx, ly);
            if (r >= 0) {
                const auto& it = menus[curMenu].items[r];
                if (!(it.flags & (MF_GRAYED | MF_DISABLED))) {
                    if (it.subIdx >= 0) {
                        navStack.push_back(it.subIdx);
                        switchMenu(it.subIdx);
                    } else {
                        chosenCmd = it.id;
                        running = false;
                    }
                }
            } else if (sawPress && !insideMenu(lx, ly)) {
                running = false;
            }
            break;
        }
        case KeyPress: {
            char keyBuf[16] = {};
            KeySym ks = NoSymbol;
            if (x.LookupString) x.LookupString(&ev.xkey, keyBuf, sizeof(keyBuf) - 1, &ks, nullptr);
            const auto& items = menus[curMenu].items;
            if (ks == XK_Escape) {
                if (navStack.size() > 1) {
                    navStack.pop_back();
                    switchMenu(navStack.back());
                } else {
                    running = false;
                }
            } else if (ks == XK_Left && navStack.size() > 1) {
                navStack.pop_back();
                switchMenu(navStack.back());
            } else if (ks == XK_Down || ks == XK_Up) {
                const int n = static_cast<int>(items.size());
                if (n > 0) {
                    int step = (ks == XK_Down) ? 1 : -1;
                    int idx = (hoverRow < 0) ? ((ks == XK_Down) ? -1 : n) : hoverRow;
                    for (int tries = 0; tries < n; ++tries) {
                        idx = (idx + step + n) % n;
                        if (!(items[idx].flags & (MF_SEPARATOR | MF_GRAYED | MF_DISABLED))) {
                            hoverRow = idx;
                            scrollRowIntoView(idx);
                            paintPopup();
                            break;
                        }
                    }
                }
            } else if ((ks == XK_Right || ks == XK_Return || ks == XK_KP_Enter) && hoverRow >= 0 && hoverRow < static_cast<int>(items.size())) {
                const auto& it = items[hoverRow];
                if (!(it.flags & (MF_GRAYED | MF_DISABLED))) {
                    if (it.subIdx >= 0) {
                        navStack.push_back(it.subIdx);
                        switchMenu(it.subIdx);
                    } else if (ks == XK_Return || ks == XK_KP_Enter) {
                        chosenCmd = it.id;
                        running = false;
                    }
                }
            }
            break;
        }
        case KeyRelease:
            // The menu takes the keys; a released modifier is still not held afterwards
            ApplyModifierSnapshot(g_liveKeyStates, ModifiersFromXState(ev.xkey.state));
            break;
        case FocusOut:
            // The user switched to another window (Alt+Tab, a click into another program): the menu closes
            if (ev.xfocus.window == ownerXwin && ev.xfocus.detail != NotifyInferior && ev.xfocus.detail != NotifyPointer &&
                ev.xfocus.mode != NotifyGrab && ev.xfocus.mode != NotifyUngrab) {
                running = false;
            }
            HandleX11Event(ev);
            break;
        case ClientMessage:
            // Closing the window from the desktop: the menu goes and the window gets its WM_CLOSE
            if (x.wmDeleteWindow && static_cast<Atom>(ev.xclient.data.l[0]) == x.wmDeleteWindow) running = false;
            HandleX11Event(ev);
            break;
        default:
            // Everything else (repaint from the back buffer, resizes, clipboard requests) as outside the menu
            if (!forPopup) HandleX11Event(ev);
            break;
        }
    }

    if (x.UngrabPointer) x.UngrabPointer(x.dpy, CurrentTime);
    if (popGc) x.FreeGC(x.dpy, popGc);
    x.DestroyWindow(x.dpy, popWin);
    x.Flush(x.dpy);

    if (chosenCmd != 0) {
        if (uFlags & TPM_RETURNCMD) {
            return static_cast<BOOL>(chosenCmd);
        }
        if (hWnd && !(uFlags & TPM_NONOTIFY)) {
            PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(static_cast<WORD>(chosenCmd), 0), 0);
        }
        return TRUE;
    }
    return 0;
}

// ---- Zenity File/Folder/Message Dialogs (with safe headless fallback) ----------------------------------------
namespace {

// The loop of a modal dialog or helper, as Win32 runs one: every window keeps repainting and its timers keep running
// (the disabled owner takes no input) until done(); a WM_QUIT that arrives meanwhile is passed on
void RunModalLoop(HWND dialog, const std::function<bool()>& done, const std::function<bool(const MSG&)>& preTranslate = nullptr) {
    MSG msg = {};
    while (!done()) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) {
            PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (preTranslate && preTranslate(msg)) continue;
        if (dialog && IsDialogMessageW(dialog, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

std::wstring FindZenity() {
    return Explorer2::ToolProcess::FindExecutable(L"zenity", { L"/usr/bin/zenity", L"/bin/zenity" });
}

// Runs a zenity file / folder picker. Meanwhile the owner window keeps repainting and takes no input, as under a
// Win32 modal dialog, instead of freezing until the picker is closed.
bool RunZenityDialog(const std::vector<std::wstring>& args, std::wstring& outText, HWND owner = nullptr) {
    outText.clear();
    if (IsCliHeadlessMode()) return false;
    const char* disp = std::getenv("DISPLAY");
    if (!IsX11DisplayReachableFast(disp)) return false;
    const std::wstring zenity = FindZenity();
    if (zenity.empty()) return false;
    Explorer2::ToolProcess::Options opts;
    opts.timeoutMs = 600000;
    std::atomic<bool> stop{ false };
    if (!IsUiThread()) {
        const auto res = Explorer2::ToolProcess::Run(zenity, args, opts, stop);
        if (!res.ok()) return false;
        outText = Explorer2::PathUtils::Utf8ToWide(res.out);
    } else {
        std::atomic<bool> finished{ false };
        Explorer2::ToolProcess::Result res;
        std::thread worker([&]() {
            res = Explorer2::ToolProcess::Run(zenity, args, opts, stop);
            finished = true;
            PostMessageW(nullptr, WM_NULL, 0, 0);       // wakes the loop below
        });
        const HWND top = owner ? TopLevelOf(owner) : nullptr;
        auto topRec = FindWnd(top);
        const bool wasEnabled = topRec && topRec->enabled;
        if (wasEnabled) EnableWindow(top, FALSE);
        RunModalLoop(nullptr, [&]() { return finished.load(); });
        if (!finished.load()) stop = true;              // the app is quitting: the picker goes too
        worker.join();
        if (wasEnabled && FindWnd(top)) EnableWindow(top, TRUE);
        if (!res.ok()) return false;
        outText = Explorer2::PathUtils::Utf8ToWide(res.out);
    }
    while (!outText.empty() && (outText.back() == L'\n' || outText.back() == L'\r')) outText.pop_back();
    return !outText.empty();
}

// A message box from a thread other than the UI thread: zenity, run on that thread
int ZenityMessageBox(LPCWSTR lpText, LPCWSTR lpCaption, UINT uType, int safeAnswer) {
    const std::wstring zenity = FindZenity();
    if (zenity.empty()) return safeAnswer;
    const UINT kind = uType & 0x0Fu;
    const bool isQuestion = (kind == MB_YESNO || kind == MB_YESNOCANCEL || kind == MB_OKCANCEL);
    std::vector<std::wstring> args = { isQuestion ? L"--question" : L"--info", L"--no-markup" };
    if (isQuestion && (uType & MB_DEFBUTTON2)) args.push_back(L"--default-cancel");
    if (lpCaption && *lpCaption) { args.push_back(L"--title"); args.push_back(lpCaption); }
    if (lpText && *lpText)       { args.push_back(L"--text");  args.push_back(lpText); }
    Explorer2::ToolProcess::Options opts;
    opts.timeoutMs = 600000;
    std::atomic<bool> stop{ false };
    const auto res = Explorer2::ToolProcess::Run(zenity, args, opts, stop);
    if (kind == MB_YESNO || kind == MB_YESNOCANCEL) return res.ok() ? IDYES : (kind == MB_YESNO ? IDNO : IDCANCEL);
    if (kind == MB_OKCANCEL) return res.ok() ? IDOK : IDCANCEL;
    return IDOK;
}

const wchar_t kHalMsgBoxClass[] = L"Explorer2HalMessageBox";

struct HalMsgBoxState {
    UINT type = 0;
    int result = 0;
    bool done = false;
    HWND text = nullptr;
    std::vector<HWND> buttons;
};

LRESULT CALLBACK HalMsgBoxWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<HalMsgBoxState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_COMMAND:
        if (st && !st->done) {
            const int id = LOWORD(wParam);
            const UINT kind = st->type & 0x0Fu;
            int res = id;
            if (id == IDCANCEL) {
                // Escape or the title bar's close button: Cancel where there is one, else the answer that changes nothing
                if (kind == MB_OK) res = IDOK;
                else if (kind == MB_YESNO) res = IDNO;
                else if (kind == MB_ABORTRETRYIGNORE) res = IDABORT;
            }
            st->result = res;
            st->done = true;
        }
        return 0;
    case WM_CLOSE:
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        return 0;
    case WM_MOUSEWHEEL:
        if (st && st->text) return SendMessageW(st->text, WM_MOUSEWHEEL, wParam, lParam);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// Polish button labels when the message is Polish (the HAL does not know the app's language setting)
bool HalTextLooksPolish(const std::wstring& s) {
    return s.find_first_of(L"\x0105\x0107\x0119\x0142\x0144\x00F3\x015B\x017A\x017C\x0104\x0106\x0118\x0141\x0143\x00D3\x015A\x0179\x017B") != std::wstring::npos;
}

} // namespace

// The HAL's own message box: a dialog window above its owner (no zenity: a box that opened behind the window, or
// zenity missing, made the app look frozen or answered every question with No)
int MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType) {
    const UINT kind = uType & 0x0Fu;
    const int headlessAnswer = (kind == MB_YESNO || kind == MB_YESNOCANCEL) ? IDYES : IDOK;
    // Without a box the answer that changes nothing: a delete is not confirmed by an error
    const int safeAnswer = (kind == MB_YESNO) ? IDNO : (kind == MB_OK ? IDOK : (kind == MB_ABORTRETRYIGNORE ? IDABORT : IDCANCEL));
    const char* autoClose = std::getenv("EXPLORER2_AUTO_CLOSE_MS");
    if (IsCliHeadlessMode() || (autoClose && *autoClose) || !X11().EnsureConnected()) return headlessAnswer;
    if (!IsUiThread()) return ZenityMessageBox(lpText, lpCaption, uType, safeAnswer);
    auto& ui = HalUi();
    if (!ui.ok) return ZenityMessageBox(lpText, lpCaption, uType, safeAnswer);

    const std::wstring text = lpText ? lpText : L"";
    const std::wstring caption = (lpCaption && *lpCaption) ? lpCaption : L"Explorer2";
    const bool pl = HalTextLooksPolish(caption) || HalTextLooksPolish(text);
    struct Btn { int id; const wchar_t* label; };
    std::vector<Btn> btns;
    switch (kind) {
    case MB_OKCANCEL:         btns = { { IDOK, L"OK" }, { IDCANCEL, pl ? L"Anuluj" : L"Cancel" } }; break;
    case MB_ABORTRETRYIGNORE: btns = { { IDABORT, pl ? L"Przerwij" : L"Abort" }, { IDRETRY, pl ? L"Pon\x00F3w" : L"Retry" },
                                       { IDIGNORE, pl ? L"Ignoruj" : L"Ignore" } }; break;
    case MB_YESNOCANCEL:      btns = { { IDYES, pl ? L"Tak" : L"Yes" }, { IDNO, pl ? L"Nie" : L"No" },
                                       { IDCANCEL, pl ? L"Anuluj" : L"Cancel" } }; break;
    case MB_YESNO:            btns = { { IDYES, pl ? L"Tak" : L"Yes" }, { IDNO, pl ? L"Nie" : L"No" } }; break;
    case MB_RETRYCANCEL:      btns = { { IDRETRY, pl ? L"Pon\x00F3w" : L"Retry" }, { IDCANCEL, pl ? L"Anuluj" : L"Cancel" } }; break;
    default:                  btns = { { IDOK, L"OK" } }; break;
    }
    const size_t defIdx = std::min<size_t>(btns.size() - 1, (uType >> 8) & 0x0Fu);     // MB_DEFBUTTON2 / 3
    const UINT icon = uType & 0xF0u;

    // Layout: icon and text on white, the buttons on a grey band at the bottom, right-aligned
    const RECT wa = X11WorkArea();
    const int waW = wa.right - wa.left, waH = wa.bottom - wa.top;
    const int pad = 18, gap = 14, iconSz = icon ? 32 : 0, btnH = 26, btnGap = 8, bandPad = 11;
    const float maxTextW = std::clamp(static_cast<float>(waW) * 0.5f, 280.0f, 560.0f);
    const D2D1_SIZE_F tsz = MeasureHalText(ui.text.Get(), text, maxTextW);
    int btnW = 84;
    for (const auto& b : btns) btnW = std::max(btnW, static_cast<int>(std::ceil(MeasureHalText(ui.button.Get(), b.label, 400.0f).width)) + 28);
    const int btnsW = static_cast<int>(btns.size()) * btnW + static_cast<int>(btns.size() - 1) * btnGap;
    const int textW = static_cast<int>(std::ceil(tsz.width)) + 6;
    int textH = std::max(17, static_cast<int>(std::ceil(tsz.height)) + 2);
    const int w = std::min(std::max({ 300, textW + (iconSz ? iconSz + gap : 0) + 2 * pad, btnsW + 2 * pad }), std::max(300, waW - 40));
    const int bandH = btnH + 2 * bandPad;
    const int maxTop = std::max(120, waH - 70 - bandH);         // room for the window manager's title bar
    int topH = std::max(textH, iconSz) + 2 * pad;
    if (topH > maxTop) {                                         // a text taller than the screen scrolls
        topH = maxTop;
        textH = topH - 2 * pad;
    }
    const int h = topH + bandH;

    // Over its owner's window (Win32 makes an ownerless box from the app's thread its window's box too)
    HWND owner = hWnd ? TopLevelOf(hWnd) : nullptr;
    if (!owner && g_focusedHwnd) owner = TopLevelOf(g_focusedHwnd);
    RECT orc = wa;
    if (owner) GetWindowRect(owner, &orc);
    const int bx0 = std::clamp(static_cast<int>(orc.left) + ((orc.right - orc.left) - w) / 2,
                               static_cast<int>(wa.left), std::max(static_cast<int>(wa.left), static_cast<int>(wa.right) - w));
    const int by0 = std::clamp(static_cast<int>(orc.top) + ((orc.bottom - orc.top) - h) / 2,
                               static_cast<int>(wa.top), std::max(static_cast<int>(wa.top), static_cast<int>(wa.bottom) - h));

    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = HalMsgBoxWndProc;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<intptr_t>(COLOR_WINDOW + 1));
        wc.lpszClassName = kHalMsgBoxClass;
        RegisterClassExW(&wc);
        s_registered = true;
    }
    HalMsgBoxState st;
    st.type = uType;
    HWND box = CreateWindowExW(WS_EX_DLGMODALFRAME, kHalMsgBoxClass, caption.c_str(),
                               static_cast<DWORD>(WS_POPUP | WS_CAPTION | WS_SYSMENU),
                               bx0, by0, w, h, owner, nullptr, nullptr, nullptr);
    if (!box) return ZenityMessageBox(lpText, lpCaption, uType, safeAnswer);
    SetWindowLongPtrW(box, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));
    if (auto br = FindWnd(box)) {
        br->isDialog = true;                                     // drawn by the HAL even without an owner
        br->msgBoxBandTop = topH;
    }
    if (icon) {
        HWND ic = CreateWindowExW(0, L"STATIC", L"", static_cast<DWORD>(WS_CHILD | WS_VISIBLE), pad, pad, iconSz, iconSz,
                                  box, nullptr, nullptr, nullptr);
        if (auto ir = FindWnd(ic)) ir->staticIcon = icon;
    }
    const int textX = pad + (iconSz ? iconSz + gap : 0);
    const int textY = pad + ((iconSz > textH) ? (iconSz - textH) / 2 : 0);
    st.text = CreateWindowExW(0, L"STATIC", text.c_str(), static_cast<DWORD>(WS_CHILD | WS_VISIBLE),
                              textX, textY, std::max(40, w - textX - pad), textH, box, nullptr, nullptr, nullptr);
    int bx = w - pad - btnsW;
    const int by = topH + bandPad;
    for (size_t i = 0; i < btns.size(); ++i) {
        const DWORD style = static_cast<DWORD>(WS_CHILD | WS_VISIBLE | WS_TABSTOP) |
                            static_cast<DWORD>(i == defIdx ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
        st.buttons.push_back(CreateWindowExW(0, L"BUTTON", btns[i].label, style, bx, by, btnW, btnH, box,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(btns[i].id)), nullptr, nullptr));
        bx += btnW + btnGap;
    }

    const HWND prevFocus = g_focusedHwnd;
    auto ownerRec = FindWnd(owner);
    const bool ownerWasEnabled = ownerRec && ownerRec->enabled;
    if (ownerWasEnabled) EnableWindow(owner, FALSE);
    ShowWindow(box, SW_SHOW);
    if (!st.buttons.empty() && st.buttons[defIdx]) ChangeFocus(st.buttons[defIdx]);

    auto preTranslate = [&](const MSG& m) -> bool {
        if (!m.hwnd || TopLevelOf(m.hwnd) != box) return false;
        if (m.message == WM_KEYDOWN) {
            if (m.wParam == VK_LEFT || m.wParam == VK_RIGHT) {
                // The arrows move between the buttons
                size_t cur = 0;
                for (size_t i = 0; i < st.buttons.size(); ++i) if (st.buttons[i] == g_focusedHwnd) cur = i;
                const size_t n = st.buttons.size();
                const size_t next = (m.wParam == VK_LEFT) ? (cur + n - 1) % n : (cur + 1) % n;
                ChangeFocus(st.buttons[next]);
                return true;
            }
            if (m.wParam == VK_UP || m.wParam == VK_DOWN || m.wParam == VK_PRIOR || m.wParam == VK_NEXT ||
                m.wParam == VK_HOME || m.wParam == VK_END) {
                // A long text scrolls with the arrow and page keys
                if (auto tr = FindWnd(st.text)) {
                    const float page = std::max(20.0f, static_cast<float>(tr->height) - 20.0f);
                    float s = tr->scrollY;
                    switch (m.wParam) {
                    case VK_UP:    s -= 40.0f; break;
                    case VK_DOWN:  s += 40.0f; break;
                    case VK_PRIOR: s -= page; break;
                    case VK_NEXT:  s += page; break;
                    case VK_HOME:  s = 0.0f; break;
                    default:       s = tr->contentH; break;
                    }
                    tr->scrollY = std::clamp(s, 0.0f, std::max(0.0f, tr->contentH - static_cast<float>(tr->height)));
                    InvalidateRect(box, nullptr, FALSE);
                }
                return true;
            }
            return false;
        }
        if (m.message == WM_CHAR) {
            // The buttons' letters answer, as the underlined ones do on Windows: T / Y = yes, N = no
            const wchar_t c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(m.wParam)));
            for (HWND b : st.buttons) {
                auto br = FindWnd(b);
                if (!br) continue;
                if ((br->ctrlId == IDYES && (c == L't' || c == L'y')) || (br->ctrlId == IDNO && c == L'n')) {
                    PostMessageW(box, WM_COMMAND, MAKEWPARAM(static_cast<WORD>(br->ctrlId), BN_CLICKED), reinterpret_cast<LPARAM>(b));
                    return true;
                }
            }
        }
        return false;
    };
    RunModalLoop(box, [&]() { return st.done; }, preTranslate);

    if (ownerWasEnabled && FindWnd(owner)) EnableWindow(owner, TRUE);
    if (FindWnd(box)) DestroyWindow(box);
    if (auto orr = FindWnd(owner)) {
        ActivateX11Window(*orr);
        auto pf = FindWnd(prevFocus);
        ChangeFocus((pf && pf->visible && TopLevelOf(prevFocus) == owner) ? prevFocus : owner);
    }
    return st.done ? st.result : safeAnswer;
}

BOOL GetOpenFileNameW(LPOPENFILENAMEW lpofn) {
    if (!lpofn || !lpofn->lpstrFile || lpofn->nMaxFile == 0) return FALSE;
    std::vector<std::wstring> args = { L"--file-selection" };
    if (lpofn->lpstrTitle && *lpofn->lpstrTitle) { args.push_back(L"--title"); args.push_back(lpofn->lpstrTitle); }
    std::wstring chosen;
    if (!RunZenityDialog(args, chosen, lpofn->hwndOwner)) return FALSE;
    wcsncpy_s(lpofn->lpstrFile, lpofn->nMaxFile, chosen.c_str(), _TRUNCATE);
    return TRUE;
}

BOOL GetSaveFileNameW(LPOPENFILENAMEW lpofn) {
    if (!lpofn || !lpofn->lpstrFile || lpofn->nMaxFile == 0) return FALSE;
    std::vector<std::wstring> args = { L"--file-selection", L"--save", L"--confirm-overwrite" };
    if (lpofn->lpstrTitle && *lpofn->lpstrTitle) { args.push_back(L"--title"); args.push_back(lpofn->lpstrTitle); }
    if (lpofn->lpstrFile[0]) { args.push_back(L"--filename"); args.push_back(lpofn->lpstrFile); }
    std::wstring chosen;
    if (!RunZenityDialog(args, chosen, lpofn->hwndOwner)) return FALSE;
    wcsncpy_s(lpofn->lpstrFile, lpofn->nMaxFile, chosen.c_str(), _TRUNCATE);
    return TRUE;
}

// ==============================================================================================================
// 4. Shell Binary PIDL Hierarchy & CF_HDROP IDataObject (Drag-Out + SHBrowseForFolderW)
// ==============================================================================================================
namespace {

std::vector<std::string> SplitPathSegmentsForPidl(const std::wstring& wpath) {
    std::string u8 = Explorer2::Compat::NativePath(wpath.c_str());
    std::vector<std::string> segs;
    if (!u8.empty() && u8[0] == '/') segs.push_back("/");
    size_t i = (!u8.empty() && u8[0] == '/') ? 1 : 0;
    while (i < u8.size()) {
        size_t sl = u8.find('/', i);
        std::string part = (sl == std::string::npos) ? u8.substr(i) : u8.substr(i, sl - i);
        if (!part.empty()) segs.push_back(std::move(part));
        if (sl == std::string::npos) break;
        i = sl + 1;
    }
    return segs;
}

PIDLIST_ABSOLUTE BuildPidlFromSegments(const std::vector<std::string>& segs) {
    size_t totalBytes = sizeof(USHORT); // terminal 0
    for (const auto& s : segs) totalBytes += sizeof(USHORT) + s.size() + 1;
    auto* buf = static_cast<uint8_t*>(CoTaskMemAlloc(totalBytes));
    if (!buf) return nullptr;
    uint8_t* cur = buf;
    for (const auto& s : segs) {
        const USHORT cb = static_cast<USHORT>(sizeof(USHORT) + s.size() + 1);
        std::memcpy(cur, &cb, sizeof(USHORT));
        std::memcpy(cur + sizeof(USHORT), s.c_str(), s.size() + 1);
        cur += cb;
    }
    const USHORT zero = 0;
    std::memcpy(cur, &zero, sizeof(USHORT));
    return reinterpret_cast<PIDLIST_ABSOLUTE>(buf);
}

std::vector<std::string> ExtractSegmentsFromPidl(PCIDLIST_ABSOLUTE pidl) {
    std::vector<std::string> segs;
    if (!pidl) return segs;
    const auto* cur = reinterpret_cast<const uint8_t*>(pidl);
    for (int guard = 0; guard < 1024; ++guard) {
        USHORT cb = 0;
        std::memcpy(&cb, cur, sizeof(USHORT));
        if (cb < sizeof(USHORT) + 1) break;
        const char* str = reinterpret_cast<const char*>(cur + sizeof(USHORT));
        segs.emplace_back(str, strnlen(str, cb - sizeof(USHORT)));
        cur += cb;
    }
    return segs;
}

std::wstring SegmentsToWidePath(const std::vector<std::string>& segs) {
    if (segs.empty()) return {};
    std::string out;
    for (const auto& s : segs) {
        if (s == "/") {
            if (out.empty()) out = "/";
        } else {
            if (!out.empty() && out.back() != '/') out.push_back('/');
            out += s;
        }
    }
    return Explorer2::PathUtils::Utf8ToWide(out);
}

size_t PidlByteSize(PCIDLIST_ABSOLUTE pidl) {
    if (!pidl) return 0;
    const auto* cur = reinterpret_cast<const uint8_t*>(pidl);
    size_t total = 0;
    for (int guard = 0; guard < 1024; ++guard) {
        USHORT cb = 0;
        std::memcpy(&cb, cur + total, sizeof(USHORT));
        if (cb == 0) return total + sizeof(USHORT);
        total += cb;
    }
    return total + sizeof(USHORT);
}

class LinuxHDropDataObject final : public IDataObject {
public:
    explicit LinuxHDropDataObject(std::vector<std::wstring> paths) : m_paths(std::move(paths)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDataObject)) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) override {
        if (!pformatetcIn || !pmedium) return E_POINTER;
        if (pformatetcIn->cfFormat != CF_HDROP || !(pformatetcIn->tymed & TYMED_HGLOBAL)) {
            return DV_E_FORMATETC;
        }
        size_t chars = 1; // trailing extra NUL
        for (const auto& p : m_paths) chars += p.size() + 1;
        const size_t totalBytes = sizeof(DROPFILES) + chars * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalBytes);
        if (!hMem) return E_OUTOFMEMORY;
        auto* raw = static_cast<uint8_t*>(GlobalLock(hMem));
        auto* df = reinterpret_cast<DROPFILES*>(raw);
        df->pFiles = sizeof(DROPFILES);
        df->fWide  = TRUE;
        auto* dst = reinterpret_cast<wchar_t*>(raw + sizeof(DROPFILES));
        for (const auto& p : m_paths) {
            std::wmemcpy(dst, p.c_str(), p.size() + 1);
            dst += p.size() + 1;
        }
        *dst = L'\0';
        GlobalUnlock(hMem);

        std::memset(pmedium, 0, sizeof(*pmedium));
        pmedium->tymed = TYMED_HGLOBAL;
        pmedium->hGlobal = hMem;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pfmt) override {
        if (!pfmt) return E_POINTER;
        return (pfmt->cfFormat == CF_HDROP && (pfmt->tymed & TYMED_HGLOBAL)) ? S_OK : DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return S_OK; }

private:
    std::atomic<ULONG> m_ref{ 1 };
    std::vector<std::wstring> m_paths;
};

} // namespace

PIDLIST_ABSOLUTE ILCreateFromPathW(LPCWSTR pszPath) {
    if (!pszPath || !*pszPath) return nullptr;
    const std::string nat = Explorer2::Compat::NativePath(pszPath);
    struct stat st = {};
    if (::lstat(nat.c_str(), &st) != 0) return nullptr;
    return BuildPidlFromSegments(SplitPathSegmentsForPidl(pszPath));
}

HRESULT SHParseDisplayName(LPCWSTR pszName, void*, PIDLIST_ABSOLUTE* ppidl, ULONG, ULONG* psfgaoOut) {
    if (!ppidl) return E_POINTER;
    *ppidl = nullptr;
    if (psfgaoOut) *psfgaoOut = 0;
    if (!pszName || !*pszName) return E_INVALIDARG;
    PIDLIST_ABSOLUTE p = ILCreateFromPathW(pszName);
    if (!p) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    *ppidl = p;
    return S_OK;
}

PIDLIST_ABSOLUTE ILClone(PCIDLIST_ABSOLUTE pidl) {
    if (!pidl) return nullptr;
    const size_t cb = PidlByteSize(pidl);
    auto* copy = static_cast<PIDLIST_ABSOLUTE>(CoTaskMemAlloc(cb));
    if (copy) std::memcpy(copy, pidl, cb);
    return copy;
}

BOOL ILRemoveLastID(LPITEMIDLIST pidl) {
    if (!pidl) return FALSE;
    auto* cur = reinterpret_cast<uint8_t*>(pidl);
    USHORT firstCb = 0;
    std::memcpy(&firstCb, cur, sizeof(USHORT));
    if (firstCb == 0) return FALSE;
    uint8_t* prev = nullptr;
    for (int guard = 0; guard < 1024; ++guard) {
        USHORT cb = 0;
        std::memcpy(&cb, cur, sizeof(USHORT));
        if (cb == 0) break;
        prev = cur;
        cur += cb;
    }
    if (!prev) return FALSE;
    const USHORT zero = 0;
    std::memcpy(prev, &zero, sizeof(USHORT));
    return TRUE;
}

PCUITEMID_CHILD ILFindLastID(PCIDLIST_ABSOLUTE pidl) {
    if (!pidl) return nullptr;
    const auto* cur = reinterpret_cast<const uint8_t*>(pidl);
    const uint8_t* last = cur;
    for (int guard = 0; guard < 1024; ++guard) {
        USHORT cb = 0;
        std::memcpy(&cb, cur, sizeof(USHORT));
        if (cb == 0) break;
        last = cur;
        cur += cb;
    }
    return reinterpret_cast<PCUITEMID_CHILD>(last);
}

BOOL ILIsParent(PCIDLIST_ABSOLUTE pidl1, PCIDLIST_ABSOLUTE pidl2, BOOL fImmediate) {
    if (!pidl1 || !pidl2) return FALSE;
    const auto s1 = ExtractSegmentsFromPidl(pidl1);
    const auto s2 = ExtractSegmentsFromPidl(pidl2);
    if (s2.size() <= s1.size()) return FALSE;
    if (fImmediate && s2.size() != s1.size() + 1) return FALSE;
    for (size_t i = 0; i < s1.size(); ++i) {
        if (s1[i] != s2[i]) return FALSE;
    }
    return TRUE;
}

BOOL SHGetPathFromIDListW(PCIDLIST_ABSOLUTE pidl, LPWSTR pszPath) {
    if (!pidl || !pszPath) return FALSE;
    const std::wstring w = SegmentsToWidePath(ExtractSegmentsFromPidl(pidl));
    if (w.empty()) return FALSE;
    wcsncpy_s(pszPath, MAX_PATH, w.c_str(), _TRUNCATE);
    return TRUE;
}

HRESULT SHCreateDataObject(PCIDLIST_ABSOLUTE pidlFolder, UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
                           IDataObject*, REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (cidl == 0 || !apidl) return E_INVALIDARG;

    const auto parentSegs = ExtractSegmentsFromPidl(pidlFolder);
    std::vector<std::wstring> paths;
    paths.reserve(cidl);
    for (UINT i = 0; i < cidl; ++i) {
        if (!apidl[i]) continue;
        auto combined = parentSegs;
        const auto childSegs = ExtractSegmentsFromPidl(apidl[i]);
        combined.insert(combined.end(), childSegs.begin(), childSegs.end());
        std::wstring full = SegmentsToWidePath(combined);
        if (!full.empty()) paths.push_back(std::move(full));
    }
    if (paths.empty()) return E_FAIL;

    auto* obj = new LinuxHDropDataObject(std::move(paths));
    HRESULT hr = obj->QueryInterface(riid, ppv);
    obj->Release();
    return hr;
}

PIDLIST_ABSOLUTE SHBrowseForFolderW(BROWSEINFOW* lpbi) {
    if (!lpbi) return nullptr;
    std::vector<std::wstring> args = { L"--file-selection", L"--directory" };
    if (lpbi->lpszTitle && *lpbi->lpszTitle) { args.push_back(L"--title"); args.push_back(lpbi->lpszTitle); }
    std::wstring chosen;
    if (!RunZenityDialog(args, chosen, lpbi->hwndOwner)) return nullptr;
    if (lpbi->pszDisplayName) {
        std::wstring leaf = Explorer2::PathUtils::GetFileName(chosen);
        wcsncpy_s(lpbi->pszDisplayName, MAX_PATH, leaf.c_str(), _TRUNCATE);
    }
    return ILCreateFromPathW(chosen.c_str());
}
