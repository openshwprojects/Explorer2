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

#ifndef SIZE_RESTORED
#define SIZE_RESTORED 0
#endif
#ifndef DV_E_FORMATETC
#define DV_E_FORMATETC (static_cast<HRESULT>(0x80040064u))
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <dlfcn.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
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
        char sockPath[64] = {};
        std::snprintf(sockPath, sizeof(sockPath), "/tmp/.X11-unix/X%d", dpyNum);
        return ::access(sockPath, R_OK | W_OK) == 0;
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
    int      (*Flush)(Display*) = nullptr;
    int      (*LookupString)(XKeyEvent*, char*, int, KeySym*, XComposeStatus*) = nullptr;
    Bool     (*TranslateCoordinates)(Display*, Window, Window, int, int, int*, int*, Window*) = nullptr;
    int      (*ChangeWindowAttributes)(Display*, Window, unsigned long, XSetWindowAttributes*) = nullptr;
    int      (*MapRaised)(Display*, Window) = nullptr;
    int      (*GrabPointer)(Display*, Window, Bool, unsigned int, int, int, Window, Cursor, Time) = nullptr;
    int      (*UngrabPointer)(Display*, Time) = nullptr;

    Display* dpy = nullptr;
    int screen = 0;
    Window root = 0;
    Atom wmDeleteWindow = 0;
    Atom netWmName = 0;
    Atom utf8String = 0;
    Atom netWmState = 0;
    Atom netWmStateFullscreen = 0;

    bool EnsureConnected() {
        if (tried) return ready && dpy != nullptr;
        tried = true;
        if (IsCliHeadlessMode()) return false;
        const char* dispEnv = std::getenv("DISPLAY");
        if (!IsX11DisplayReachableFast(dispEnv)) return false;

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
        Flush                = reinterpret_cast<decltype(Flush)>(sym("XFlush"));
        LookupString         = reinterpret_cast<decltype(LookupString)>(sym("XLookupString"));
        TranslateCoordinates = reinterpret_cast<decltype(TranslateCoordinates)>(sym("XTranslateCoordinates"));
        ChangeWindowAttributes = reinterpret_cast<decltype(ChangeWindowAttributes)>(sym("XChangeWindowAttributes"));
        MapRaised            = reinterpret_cast<decltype(MapRaised)>(sym("XMapRaised"));
        GrabPointer          = reinterpret_cast<decltype(GrabPointer)>(sym("XGrabPointer"));
        UngrabPointer        = reinterpret_cast<decltype(UngrabPointer)>(sym("XUngrabPointer"));

        if (!OpenDisplay || !CreateSimpleWindow || !MapWindow || !Pending || !NextEvent || !CreateGC || !CreateImage || !PutImage || !Flush) {
            return false;
        }
        dpy = OpenDisplay(nullptr);
        if (!dpy) return false;
        screen = DefaultScreen(dpy);
        root   = RootWindow(dpy, screen);
        if (InternAtom) {
            wmDeleteWindow       = InternAtom(dpy, "WM_DELETE_WINDOW", False);
            netWmName            = InternAtom(dpy, "_NET_WM_NAME", False);
            utf8String           = InternAtom(dpy, "UTF8_STRING", False);
            netWmState           = InternAtom(dpy, "_NET_WM_STATE", False);
            netWmStateFullscreen = InternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
        }
        ready = true;
        return true;
    }
};

X11Lib& X11() {
    static X11Lib s_x11;
    return s_x11;
}

// ==============================================================================================================
// 2. Window, Control, Timer & Message Queue State
// ==============================================================================================================
struct WndRecord {
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    std::wstring className;
    std::wstring text;
    DWORD style = 0;
    DWORD exStyle = 0;
    int x = 0, y = 0;
    int width = 1240, height = 760;
    int ctrlId = 0;
    HINSTANCE hInst = nullptr;
    WNDPROC wndProc = nullptr;
    LONG_PTR userData = 0;
    bool visible = false;
    bool enabled = true;
    bool needsPaint = false;
    bool isEdit = false;
    size_t caretPos = 0;
    size_t selStart = 0;
    Window xwin = 0;
    GC xgc = nullptr;
    ULONGLONG shownAtTick = 0;
    uint32_t paintedFrames = 0;
    std::vector<uint8_t> lastFrame;
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
SHORT g_keyStates[256] = {};
ULONGLONG g_lastLClickTick = 0;
int g_lastLClickX = -1000, g_lastLClickY = -1000;

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

    switch (msg) {
    case WM_SETTEXT: {
        const wchar_t* s = reinterpret_cast<const wchar_t*>(lParam);
        rec->text = s ? s : L"";
        rec->caretPos = rec->text.size();
        rec->selStart = rec->caretPos;
        if (rec->parent) InvalidateRect(rec->parent, nullptr, FALSE);
        return TRUE;
    }
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
        auto eraseSel = [&]() -> bool {
            if (rec->selStart == rec->caretPos) return false;
            const size_t a = std::min(rec->selStart, rec->caretPos);
            const size_t b = std::max(rec->selStart, rec->caretPos);
            rec->text.erase(a, b - a);
            rec->caretPos = rec->selStart = a;
            return true;
        };
        if (wParam == VK_LEFT) {
            if (rec->caretPos > 0) --rec->caretPos;
            rec->selStart = rec->caretPos;
            if (rec->parent) InvalidateRect(rec->parent, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_RIGHT) {
            if (rec->caretPos < rec->text.size()) ++rec->caretPos;
            rec->selStart = rec->caretPos;
            if (rec->parent) InvalidateRect(rec->parent, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_HOME) {
            rec->caretPos = rec->selStart = 0;
            if (rec->parent) InvalidateRect(rec->parent, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_END) {
            rec->caretPos = rec->selStart = rec->text.size();
            if (rec->parent) InvalidateRect(rec->parent, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_BACK) {
            if (!eraseSel() && rec->caretPos > 0) {
                rec->text.erase(rec->caretPos - 1, 1);
                --rec->caretPos;
                rec->selStart = rec->caretPos;
            }
            notifyParent(EN_CHANGE);
            return 0;
        }
        if (wParam == VK_DELETE) {
            if (!eraseSel() && rec->caretPos < rec->text.size()) {
                rec->text.erase(rec->caretPos, 1);
            }
            notifyParent(EN_CHANGE);
            return 0;
        }
        break;
    }
    case WM_CHAR: {
        const wchar_t ch = static_cast<wchar_t>(wParam);
        if (ch == 1) { // Ctrl+A
            rec->selStart = 0;
            rec->caretPos = rec->text.size();
            if (rec->parent) InvalidateRect(rec->parent, nullptr, FALSE);
            return 0;
        }
        if (ch == 22) { // Ctrl+V
            if (OpenClipboard(hwnd)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    if (const auto* w = static_cast<const wchar_t*>(GlobalLock(hData))) {
                        if (rec->selStart != rec->caretPos) {
                            const size_t a = std::min(rec->selStart, rec->caretPos);
                            const size_t b = std::max(rec->selStart, rec->caretPos);
                            rec->text.erase(a, b - a);
                            rec->caretPos = a;
                        }
                        std::wstring ins(w);
                        rec->text.insert(rec->caretPos, ins);
                        rec->caretPos += ins.size();
                        rec->selStart = rec->caretPos;
                        GlobalUnlock(hData);
                        notifyParent(EN_CHANGE);
                    }
                }
                CloseClipboard();
            }
            return 0;
        }
        if (ch >= 32 && ch != 127) {
            if (rec->selStart != rec->caretPos) {
                const size_t a = std::min(rec->selStart, rec->caretPos);
                const size_t b = std::max(rec->selStart, rec->caretPos);
                rec->text.erase(a, b - a);
                rec->caretPos = a;
            }
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

int KeySymToVk(KeySym ks) {
    switch (ks) {
    case XK_BackSpace: return VK_BACK;
    case XK_Tab: case XK_ISO_Left_Tab: return VK_TAB;
    case XK_Return: case XK_KP_Enter: return VK_RETURN;
    case XK_Escape: return VK_ESCAPE;
    case XK_space: return VK_SPACE;
    case XK_Prior: return VK_PRIOR;
    case XK_Next:  return VK_NEXT;
    case XK_End:   return VK_END;
    case XK_Home:  return VK_HOME;
    case XK_Left:  return VK_LEFT;
    case XK_Up:    return VK_UP;
    case XK_Right: return VK_RIGHT;
    case XK_Down:  return VK_DOWN;
    case XK_Insert:return VK_INSERT;
    case XK_Delete:return VK_DELETE;
    case XK_Shift_L: case XK_Shift_R: return VK_SHIFT;
    case XK_Control_L: case XK_Control_R: return VK_CONTROL;
    case XK_Alt_L: case XK_Alt_R: return VK_MENU;
    case XK_F1: return VK_F1; case XK_F2: return VK_F2; case XK_F3: return VK_F3; case XK_F4: return VK_F4;
    case XK_F5: return VK_F5; case XK_F6: return VK_F6; case XK_F7: return VK_F7; case XK_F8: return VK_F8;
    case XK_F9: return VK_F9; case XK_F10: return VK_F10; case XK_F11: return VK_F11; case XK_F12: return VK_F12;
    case XK_plus: case XK_equal: case XK_KP_Add: return VK_OEM_PLUS;
    case XK_minus: case XK_KP_Subtract: return VK_OEM_MINUS;
    default:
        if (ks >= 'a' && ks <= 'z') return static_cast<int>(ks - 'a' + 'A');
        if (ks >= 'A' && ks <= 'Z') return static_cast<int>(ks);
        if (ks >= '0' && ks <= '9') return static_cast<int>(ks);
        return 0;
    }
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

// Overlay visible child EDIT boxes (address/search/rename) onto top-level window BGRA buffer
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
    for (const auto& ed : edits) {
        const int x0 = std::clamp(ed->x, 0, static_cast<int>(w));
        const int y0 = std::clamp(ed->y, 0, static_cast<int>(h));
        const int x1 = std::clamp(ed->x + ed->width, x0, static_cast<int>(w));
        const int y1 = std::clamp(ed->y + ed->height, y0, static_cast<int>(h));
        const bool focused = (g_focusedHwnd == ed->hwnd);
        for (int y = y0; y < y1; ++y) {
            uint8_t* row = fb.data() + static_cast<size_t>(y) * w * 4u;
            for (int x = x0; x < x1; ++x) {
                const bool border = (x == x0 || x == x1 - 1 || y == y0 || y == y1 - 1);
                uint8_t* p = row + x * 4;
                if (border) {
                    p[0] = focused ? 215 : 203;
                    p[1] = focused ? 120 : 185;
                    p[2] = focused ? 29  : 172;
                    p[3] = 255;
                } else {
                    p[0] = 255; p[1] = 255; p[2] = 255; p[3] = 255;
                }
            }
        }
        // Simple 6x10 crisp glyph draw for child EDIT text + caret
        int cx = x0 + 5;
        const int cy = y0 + std::max(2, (y1 - y0 - 10) / 2);
        for (size_t i = 0; i < ed->text.size() && cx + 6 < x1 - 2; ++i) {
            const uint32_t cp = static_cast<uint32_t>(ed->text[i]);
            if (cp > 32) {
                for (int gy = 1; gy < 9 && cy + gy < y1 - 1; ++gy) {
                    for (int gx = 0; gx < 5 && cx + gx < x1 - 2; ++gx) {
                        if (((cp * 2654435761u) >> ((gy * 3 + gx) & 15)) & 1u) {
                            uint8_t* p = fb.data() + (static_cast<size_t>(cy + gy) * w + (cx + gx)) * 4u;
                            p[0] = 30; p[1] = 30; p[2] = 30; p[3] = 255;
                        }
                    }
                }
            }
            cx += 6;
        }
        if (focused && cx < x1 - 2) {
            for (int gy = 0; gy < 11 && cy + gy < y1 - 1; ++gy) {
                uint8_t* p = fb.data() + (static_cast<size_t>(cy + gy) * w + cx) * 4u;
                p[0] = 215; p[1] = 90; p[2] = 15; p[3] = 255;
            }
        }
    }
}

void PumpX11AndTimers(ULONGLONG* outNextTimerWaitMs = nullptr) {
    if (outNextTimerWaitMs) *outNextTimerWaitMs = 50;

    // 1. Drain X11 server events if connected
    auto& x = X11();
    if (x.ready && x.dpy) {
        while (x.Pending(x.dpy) > 0) {
            XEvent ev = {};
            x.NextEvent(x.dpy, &ev);

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
            if (!hwnd || !rec) continue;

            switch (ev.type) {
            case Expose:
                if (ev.xexpose.count == 0) {
                    rec->needsPaint = true;
                }
                break;

            case ConfigureNotify: {
                const int nw = std::max(1, ev.xconfigure.width);
                const int nh = std::max(1, ev.xconfigure.height);
                rec->x = ev.xconfigure.x;
                rec->y = ev.xconfigure.y;
                if (nw != rec->width || nh != rec->height) {
                    rec->width = nw;
                    rec->height = nh;
                    rec->needsPaint = true;
                    PostMessageW(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(nw, nh));
                }
                break;
            }

            case MotionNotify: {
                g_cursorScreen = { ev.xmotion.x_root, ev.xmotion.y_root };
                const WPARAM mk = ModStateToMkFlags(ev.xmotion.state);
                PostMessageW(g_captureHwnd ? g_captureHwnd : hwnd, WM_MOUSEMOVE, mk, MAKELPARAM(ev.xmotion.x, ev.xmotion.y));
                break;
            }

            case ButtonPress:
            case ButtonRelease: {
                g_cursorScreen = { ev.xbutton.x_root, ev.xbutton.y_root };
                const int mx = ev.xbutton.x;
                const int my = ev.xbutton.y;
                WPARAM mk = ModStateToMkFlags(ev.xbutton.state);
                const bool down = (ev.type == ButtonPress);

                if (ev.xbutton.button == Button4 || ev.xbutton.button == Button5) {
                    if (down) {
                        const short delta = (ev.xbutton.button == Button4) ? WHEEL_DELTA : -WHEEL_DELTA;
                        PostMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(mk, static_cast<USHORT>(delta)),
                                     MAKELPARAM(ev.xbutton.x_root, ev.xbutton.y_root));
                    }
                    break;
                }

                // Hit-test visible child EDIT controls on left click
                if (down && ev.xbutton.button == Button1) {
                    std::shared_ptr<WndRecord> hitEdit;
                    {
                        std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
                        for (const auto& [chHwnd, ch] : g_windows) {
                            if (ch->parent == hwnd && ch->isEdit && ch->visible &&
                                mx >= ch->x && mx < ch->x + ch->width && my >= ch->y && my < ch->y + ch->height) {
                                hitEdit = ch;
                                break;
                            }
                        }
                    }
                    if (hitEdit) {
                        g_focusedHwnd = hitEdit->hwnd;
                        rec->needsPaint = true;
                        break;
                    }
                    g_focusedHwnd = hwnd;
                }

                HWND target = g_captureHwnd ? g_captureHwnd : hwnd;
                if (ev.xbutton.button == Button1) {
                    if (down) {
                        mk |= MK_LBUTTON;
                        const ULONGLONG now = GetTickCount64();
                        const bool dbl = (now - g_lastLClickTick <= 500ULL) &&
                                         (std::abs(mx - g_lastLClickX) <= 4) && (std::abs(my - g_lastLClickY) <= 4);
                        g_lastLClickTick = dbl ? 0ULL : now;
                        g_lastLClickX = mx; g_lastLClickY = my;
                        PostMessageW(target, dbl ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN, mk, MAKELPARAM(mx, my));
                    } else {
                        mk &= ~static_cast<WPARAM>(MK_LBUTTON);
                        PostMessageW(target, WM_LBUTTONUP, mk, MAKELPARAM(mx, my));
                    }
                } else if (ev.xbutton.button == Button2) {
                    if (down) PostMessageW(target, WM_MBUTTONDOWN, mk | MK_MBUTTON, MAKELPARAM(mx, my));
                    else      PostMessageW(target, WM_MBUTTONUP, mk & ~static_cast<WPARAM>(MK_MBUTTON), MAKELPARAM(mx, my));
                } else if (ev.xbutton.button == Button3) {
                    if (down) PostMessageW(target, WM_RBUTTONDOWN, mk | MK_RBUTTON, MAKELPARAM(mx, my));
                    else      PostMessageW(target, WM_RBUTTONUP, mk & ~static_cast<WPARAM>(MK_RBUTTON), MAKELPARAM(mx, my));
                }
                break;
            }

            case KeyPress:
            case KeyRelease: {
                char strBuf[32] = {};
                KeySym ks = NoSymbol;
                const int nChars = x.LookupString ? x.LookupString(&ev.xkey, strBuf, sizeof(strBuf) - 1, &ks, nullptr) : 0;
                const int vk = KeySymToVk(ks);
                const bool down = (ev.type == KeyPress);
                if (vk > 0 && vk < 256) {
                    g_keyStates[vk] = down ? static_cast<SHORT>(0x8000) : 0;
                }
                HWND keyTarget = (g_focusedHwnd && FindWnd(g_focusedHwnd)) ? g_focusedHwnd : hwnd;
                if (vk != 0) {
                    PostMessageW(keyTarget, down ? WM_KEYDOWN : WM_KEYUP, static_cast<WPARAM>(vk), 1);
                }
                if (down && nChars > 0) {
                    strBuf[nChars] = '\0';
                    std::wstring ws = Explorer2::PathUtils::Utf8ToWide(strBuf);
                    for (wchar_t wc : ws) {
                        PostMessageW(keyTarget, WM_CHAR, static_cast<WPARAM>(wc), 1);
                    }
                }
                break;
            }

            case ClientMessage:
                if (x.wmDeleteWindow && static_cast<Atom>(ev.xclient.data.l[0]) == x.wmDeleteWindow) {
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                }
                break;

            default:
                break;
            }
        }
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

    // 3. Low-priority WM_PAINT synthesis when queue is otherwise idle
    bool queueEmpty = false;
    {
        std::lock_guard<std::mutex> lk(g_msgMutex);
        queueEmpty = g_msgQueue.empty();
    }
    if (queueEmpty) {
        std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
        for (auto& [hw, r] : g_windows) {
            if (r->visible && r->needsPaint && r->parent == nullptr) {
                r->needsPaint = false;
                PostMessageW(hw, WM_PAINT, 0, 0);
                break;
            }
        }
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
            XImage* img = x.CreateImage(
                x.dpy, vis, depth, ZPixmap, 0,
                reinterpret_cast<char*>(rec->lastFrame.data()),
                w, h, 32, static_cast<int>(w * 4u));
            if (img) {
                x.PutImage(x.dpy, rec->xwin, rec->xgc, img, 0, 0, 0, 0, w, h);
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
        } else {
            auto it = g_wndClasses.find(clsLow);
            rec->wndProc = (it != g_wndClasses.end() && it->second.lpfnWndProc) ? it->second.lpfnWndProc : DefWindowProcW;
        }

        // Create real X11 top-level window when $DISPLAY is available (skip message-only & child windows)
        const bool isTopGuiWindow = (hWndParent == nullptr) && !(dwStyle & WS_CHILD);
        if (isTopGuiWindow && X11().EnsureConnected()) {
            auto& x = X11();
            rec->xwin = x.CreateSimpleWindow(
                x.dpy, x.root, rec->x, rec->y,
                static_cast<unsigned int>(rec->width), static_cast<unsigned int>(rec->height),
                0, BlackPixel(x.dpy, x.screen), WhitePixel(x.dpy, x.screen));
            if (rec->xwin != 0) {
                g_xwinToHwnd[rec->xwin] = hwnd;
                x.SelectInput(x.dpy, rec->xwin,
                              ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                              ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask);
                if (x.SetWMProtocols && x.wmDeleteWindow) {
                    x.SetWMProtocols(x.dpy, rec->xwin, &x.wmDeleteWindow, 1);
                }
                const std::string titleUtf8 = Explorer2::PathUtils::WideToUtf8(rec->text);
                if (x.StoreName) x.StoreName(x.dpy, rec->xwin, titleUtf8.c_str());
                if (x.ChangeProperty && x.netWmName && x.utf8String) {
                    x.ChangeProperty(x.dpy, rec->xwin, x.netWmName, x.utf8String, 8, PropModeReplace,
                                     reinterpret_cast<const unsigned char*>(titleUtf8.c_str()),
                                     static_cast<int>(titleUtf8.size()));
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
    return hwnd;
}

BOOL DestroyWindow(HWND hWnd) {
    auto rec = FindWnd(hWnd);
    if (!rec) return FALSE;
    if (rec->wndProc) rec->wndProc(hWnd, WM_DESTROY, 0, 0);

    std::lock_guard<std::recursive_mutex> lk(g_guiMutex);
    // Destroy child windows
    std::vector<HWND> kids;
    for (const auto& [ch, cr] : g_windows) if (cr->parent == hWnd) kids.push_back(ch);
    for (HWND kh : kids) DestroyWindow(kh);

    // Remove timers
    for (auto it = g_timers.begin(); it != g_timers.end(); ) {
        if (it->first.hwnd == hWnd) it = g_timers.erase(it);
        else ++it;
    }

    auto& x = X11();
    if (x.ready && x.dpy && rec->xwin != 0) {
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

BOOL ShowWindow(HWND hWnd, int nCmdShow) {
    auto rec = FindWnd(hWnd);
    if (!rec) return FALSE;
    const bool show = (nCmdShow != SW_HIDE);
    rec->visible = show;
    if (show) {
        rec->needsPaint = true;
        if (rec->shownAtTick == 0) rec->shownAtTick = GetTickCount64();
        if (rec->parent == nullptr && !g_focusedHwnd) g_focusedHwnd = hWnd;
    }
    auto& x = X11();
    if (x.ready && x.dpy && rec->xwin != 0) {
        if (show) x.MapWindow(x.dpy, rec->xwin);
        else if (x.UnmapWindow) x.UnmapWindow(x.dpy, rec->xwin);
        x.Flush(x.dpy);
    }
    if (show && rec->parent == nullptr && rec->parent != HWND_MESSAGE) {
        SendMessageW(hWnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rec->width, rec->height));
    }
    if (rec->parent && rec->parent != HWND_MESSAGE) {
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
BOOL EnableWindow(HWND hWnd, BOOL bEnable) { auto r = FindWnd(hWnd); if (r) r->enabled = (bEnable != FALSE); return TRUE; }
BOOL SetForegroundWindow(HWND) { return TRUE; }

HWND SetFocus(HWND hWnd) {
    HWND prev = g_focusedHwnd;
    g_focusedHwnd = hWnd;
    if (auto r = FindWnd(hWnd)) {
        if (r->parent) InvalidateRect(r->parent, nullptr, FALSE);
        else InvalidateRect(hWnd, nullptr, FALSE);
    }
    return prev;
}
HWND GetFocus() { return g_focusedHwnd; }
HWND GetParent(HWND hWnd) { auto r = FindWnd(hWnd); return (r && r->parent != HWND_MESSAGE) ? r->parent : nullptr; }
HWND GetAncestor(HWND hWnd, UINT) {
    auto r = FindWnd(hWnd);
    while (r && r->parent && r->parent != HWND_MESSAGE) {
        auto pr = FindWnd(r->parent);
        if (!pr) break;
        r = pr;
    }
    return r ? r->hwnd : hWnd;
}
HWND WindowFromPoint(POINT) { return g_focusedHwnd; }

BOOL GetClientRect(HWND hWnd, LPRECT lpRect) {
    if (!lpRect) return FALSE;
    auto r = FindWnd(hWnd);
    *lpRect = { 0, 0, r ? r->width : 1240, r ? r->height : 760 };
    return TRUE;
}

BOOL GetWindowRect(HWND hWnd, LPRECT lpRect) {
    if (!lpRect) return FALSE;
    auto r = FindWnd(hWnd);
    if (!r) { *lpRect = { 0, 0, 1240, 760 }; return TRUE; }
    *lpRect = { r->x, r->y, r->x + r->width, r->y + r->height };
    return TRUE;
}

BOOL ClientToScreen(HWND hWnd, LPPOINT lpPoint) {
    if (!lpPoint) return FALSE;
    auto r = FindWnd(hWnd);
    if (!r) return TRUE;
    auto& x = X11();
    if (x.ready && x.dpy && r->xwin != 0 && x.TranslateCoordinates) {
        int rx = 0, ry = 0;
        Window child = 0;
        if (x.TranslateCoordinates(x.dpy, r->xwin, x.root, lpPoint->x, lpPoint->y, &rx, &ry, &child)) {
            lpPoint->x = rx;
            lpPoint->y = ry;
            return TRUE;
        }
    }
    lpPoint->x += r->x;
    lpPoint->y += r->y;
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
    if (!(uFlags & SWP_NOMOVE)) { r->x = X; r->y = Y; }
    const bool sizeChanged = !(uFlags & SWP_NOSIZE) && (cx > 0 && cy > 0) && (cx != r->width || cy != r->height);
    if (!(uFlags & SWP_NOSIZE) && cx > 0 && cy > 0) { r->width = cx; r->height = cy; }
    if (uFlags & SWP_SHOWWINDOW) r->visible = true;
    if (uFlags & SWP_HIDEWINDOW) r->visible = false;

    auto& x = X11();
    if (x.ready && x.dpy && r->xwin != 0 && x.MoveResizeWindow) {
        x.MoveResizeWindow(x.dpy, r->xwin, r->x, r->y,
                           static_cast<unsigned int>(std::max(1, r->width)),
                           static_cast<unsigned int>(std::max(1, r->height)));
        x.Flush(x.dpy);
    }
    if (sizeChanged && r->parent == nullptr) {
        SendMessageW(hWnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r->width, r->height));
    }
    if (r->parent && r->parent != HWND_MESSAGE) InvalidateRect(r->parent, nullptr, FALSE);
    else InvalidateRect(hWnd, nullptr, FALSE);
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
    if (nIndex == GWL_STYLE)     { LONG_PTR old = r->style; r->style = static_cast<DWORD>(dwNewLong); return old; }
    if (nIndex == GWL_EXSTYLE)   { LONG_PTR old = r->exStyle; r->exStyle = static_cast<DWORD>(dwNewLong); return old; }
    return 0;
}

LRESULT CallWindowProcW(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    return lpPrevWndFunc ? lpPrevWndFunc(hWnd, Msg, wParam, lParam) : 0;
}

LRESULT DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    (void)wParam; (void)lParam;
    if (Msg == WM_CLOSE) {
        DestroyWindow(hWnd);
        PostQuitMessage(0);
        return 0;
    }
    return 0;
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
    return TRUE;
}

int GetDlgCtrlID(HWND hWnd) { auto r = FindWnd(hWnd); return r ? r->ctrlId : 0; }

BOOL InvalidateRect(HWND hWnd, const RECT*, BOOL) {
    if (auto r = FindWnd(hWnd)) r->needsPaint = true;
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
    {
        std::lock_guard<std::mutex> lk(g_msgMutex);
        if (g_msgQueue.size() > 32768) g_msgQueue.pop_front();
        MSG m = {};
        m.hwnd = hWnd;
        m.message = Msg;
        m.wParam = wParam;
        m.lParam = lParam;
        m.time = GetTickCount();
        m.pt = g_cursorScreen;
        g_msgQueue.push_back(m);
    }
    g_msgCv.notify_all();
    return TRUE;
}

void PostQuitMessage(int nExitCode) {
    PostMessageW(nullptr, WM_QUIT, static_cast<WPARAM>(nExitCode), 0);
}

BOOL PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    if (!lpMsg) return FALSE;
    PumpX11AndTimers();
    std::lock_guard<std::mutex> lk(g_msgMutex);
    for (auto it = g_msgQueue.begin(); it != g_msgQueue.end(); ++it) {
        if (MsgMatches(*it, hWnd, wMsgFilterMin, wMsgFilterMax)) {
            *lpMsg = *it;
            if (wRemoveMsg & PM_REMOVE) g_msgQueue.erase(it);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL GetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    if (!lpMsg) return FALSE;
    for (;;) {
        ULONGLONG waitMs = 30;
        PumpX11AndTimers(&waitMs);
        {
            std::unique_lock<std::mutex> lk(g_msgMutex);
            for (auto it = g_msgQueue.begin(); it != g_msgQueue.end(); ++it) {
                if (MsgMatches(*it, hWnd, wMsgFilterMin, wMsgFilterMax)) {
                    *lpMsg = *it;
                    g_msgQueue.erase(it);
                    return (lpMsg->message == WM_QUIT) ? FALSE : TRUE;
                }
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
    if (r && r->wndProc) {
        return r->wndProc(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    }
    return 0;
}

LRESULT SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    auto r = FindWnd(hWnd);
    if (r && r->wndProc) {
        return r->wndProc(hWnd, Msg, wParam, lParam);
    }
    return 0;
}

HWND SetCapture(HWND hWnd) { HWND prev = g_captureHwnd; g_captureHwnd = hWnd; return prev; }
BOOL ReleaseCapture() { g_captureHwnd = nullptr; return TRUE; }
HWND GetCapture() { return g_captureHwnd; }
BOOL GetCursorPos(LPPOINT lpPoint) { if (!lpPoint) return FALSE; *lpPoint = g_cursorScreen; return TRUE; }
SHORT GetKeyState(int nVirtKey) { return (nVirtKey >= 0 && nVirtKey < 256) ? g_keyStates[nVirtKey] : 0; }
SHORT GetAsyncKeyState(int vKey) { return GetKeyState(vKey); }

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
    lpmi->rcWork    = { 0, 0, sw, std::max(440, sh - 36) };
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
    auto it = g_menus.find(hMenu);
    if (it == g_menus.end()) return FALSE;
    std::vector<HMENU> subs;
    for (const auto& item : it->second->items) if (item.subMenu) subs.push_back(item.subMenu);
    g_menus.erase(it);
    for (HMENU sub : subs) DestroyMenu(sub);
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
        int height = 8;
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

    const int scrW = std::max(640, DisplayWidth(x.dpy, x.screen));
    const int scrH = std::max(480, DisplayHeight(x.dpy, x.screen));
    auto computePopupPos = [&](int menuIdx, int& outX, int& outY) {
        const int mw = menus[menuIdx].width;
        const int mh = menus[menuIdx].height;
        int px = xPos;
        int py = yPos;
        if (uFlags & TPM_RIGHTALIGN)  px -= mw;
        else if (uFlags & TPM_CENTERALIGN) px -= mw / 2;
        if (uFlags & TPM_BOTTOMALIGN) py -= mh;
        outX = std::clamp(px, 4, std::max(4, scrW - mw - 4));
        outY = std::clamp(py, 4, std::max(4, scrH - mh - 4));
    };

    int winX = 0, winY = 0;
    computePopupPos(0, winX, winY);

    Window popWin = x.CreateSimpleWindow(
        x.dpy, x.root, winX, winY,
        static_cast<unsigned int>(menus[0].width), static_cast<unsigned int>(menus[0].height),
        0, BlackPixel(x.dpy, x.screen), WhitePixel(x.dpy, x.screen));
    if (!popWin) return 0;

    if (x.ChangeWindowAttributes) {
        XSetWindowAttributes swa = {};
        swa.override_redirect = True;
        x.ChangeWindowAttributes(x.dpy, popWin, CWOverrideRedirect, &swa);
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
        const UINT mh = static_cast<UINT>(m.height);
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
            if (it.flags & MF_SEPARATOR) {
                const float sy = static_cast<float>(it.yTop + it.rowH / 2) + 0.5f;
                rt->DrawLine(D2D1::Point2F(10.0f, sy), D2D1::Point2F(mw - 10.0f, sy), brSep.Get(), 1.0f);
                continue;
            }
            const bool disabled = (it.flags & (MF_GRAYED | MF_DISABLED)) != 0;
            const bool hovered  = (static_cast<int>(i) == hoverRow) && !disabled;
            const D2D1_RECT_F rowRc = D2D1::RectF(3.0f, static_cast<float>(it.yTop), mw - 3.0f, static_cast<float>(it.yTop + it.rowH));
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
        computePopupPos(curMenu, winX, winY);
        if (x.MoveResizeWindow) {
            x.MoveResizeWindow(x.dpy, popWin, winX, winY,
                               static_cast<unsigned int>(menus[curMenu].width),
                               static_cast<unsigned int>(menus[curMenu].height));
        }
        paintPopup();
    };

    auto hitTestRow = [&](int lx, int ly) -> int {
        const auto& m = menus[curMenu];
        if (lx < 0 || lx >= m.width || ly < 0 || ly >= m.height) return -1;
        for (size_t i = 0; i < m.items.size(); ++i) {
            if (ly >= m.items[i].yTop && ly < m.items[i].yTop + m.items[i].rowH) {
                if (m.items[i].flags & MF_SEPARATOR) return -1;
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    paintPopup();

    UINT_PTR chosenCmd = 0;
    bool running = true;
    bool sawPress = false;
    while (running) {
        XEvent ev = {};
        x.NextEvent(x.dpy, &ev);
        switch (ev.type) {
        case Expose:
            if (ev.xexpose.window == popWin && ev.xexpose.count == 0) paintPopup();
            break;
        case MotionNotify: {
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
            sawPress = true;
            const int lx = ev.xbutton.x_root - winX;
            const int ly = ev.xbutton.y_root - winY;
            if (lx < 0 || lx >= menus[curMenu].width || ly < 0 || ly >= menus[curMenu].height) {
                running = false;
            }
            break;
        }
        case ButtonRelease: {
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
            } else if (sawPress && (lx < 0 || lx >= menus[curMenu].width || ly < 0 || ly >= menus[curMenu].height)) {
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
        default:
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
bool RunZenityDialog(const std::vector<std::wstring>& args, std::wstring& outText) {
    outText.clear();
    if (IsCliHeadlessMode()) return false;
    const char* disp = std::getenv("DISPLAY");
    if (!IsX11DisplayReachableFast(disp)) return false;
    const std::wstring zenity = Explorer2::ToolProcess::FindExecutable(
        L"zenity", { L"/usr/bin/zenity", L"/bin/zenity" });
    if (zenity.empty()) return false;
    Explorer2::ToolProcess::Options opts;
    opts.timeoutMs = 300000;
    std::atomic<bool> stop{ false };
    const auto res = Explorer2::ToolProcess::Run(zenity, args, opts, stop);
    if (!res.ok()) return false;
    std::string s = res.out;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    outText = Explorer2::PathUtils::Utf8ToWide(s);
    return !outText.empty();
}
} // namespace

int MessageBoxW(HWND, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType) {
    const char* disp = std::getenv("DISPLAY");
    const char* autoClose = std::getenv("EXPLORER2_AUTO_CLOSE_MS");
    if (IsCliHeadlessMode() || !IsX11DisplayReachableFast(disp) || (autoClose && *autoClose)) {
        return ((uType & 0xFu) == MB_YESNO || (uType & 0xFu) == MB_YESNOCANCEL) ? IDYES : IDOK;
    }
    const std::wstring zenity = Explorer2::ToolProcess::FindExecutable(L"zenity", { L"/usr/bin/zenity" });
    if (zenity.empty()) return IDOK;

    std::vector<std::wstring> args;
    const bool isQuestion = ((uType & 0xFu) == MB_YESNO || (uType & 0xFu) == MB_YESNOCANCEL || (uType & 0xFu) == MB_OKCANCEL);
    args.push_back(isQuestion ? L"--question" : L"--info");
    if (lpCaption && *lpCaption) { args.push_back(L"--title"); args.push_back(lpCaption); }
    if (lpText && *lpText)       { args.push_back(L"--text");  args.push_back(lpText); }
    Explorer2::ToolProcess::Options opts;
    opts.timeoutMs = 120000;
    std::atomic<bool> stop{ false };
    const auto res = Explorer2::ToolProcess::Run(zenity, args, opts, stop);
    if ((uType & 0xFu) == MB_YESNO || (uType & 0xFu) == MB_YESNOCANCEL) return res.ok() ? IDYES : IDNO;
    if ((uType & 0xFu) == MB_OKCANCEL) return res.ok() ? IDOK : IDCANCEL;
    return IDOK;
}

BOOL GetOpenFileNameW(LPOPENFILENAMEW lpofn) {
    if (!lpofn || !lpofn->lpstrFile || lpofn->nMaxFile == 0) return FALSE;
    std::vector<std::wstring> args = { L"--file-selection" };
    if (lpofn->lpstrTitle && *lpofn->lpstrTitle) { args.push_back(L"--title"); args.push_back(lpofn->lpstrTitle); }
    std::wstring chosen;
    if (!RunZenityDialog(args, chosen)) return FALSE;
    wcsncpy_s(lpofn->lpstrFile, lpofn->nMaxFile, chosen.c_str(), _TRUNCATE);
    return TRUE;
}

BOOL GetSaveFileNameW(LPOPENFILENAMEW lpofn) {
    if (!lpofn || !lpofn->lpstrFile || lpofn->nMaxFile == 0) return FALSE;
    std::vector<std::wstring> args = { L"--file-selection", L"--save", L"--confirm-overwrite" };
    if (lpofn->lpstrTitle && *lpofn->lpstrTitle) { args.push_back(L"--title"); args.push_back(lpofn->lpstrTitle); }
    if (lpofn->lpstrFile[0]) { args.push_back(L"--filename"); args.push_back(lpofn->lpstrFile); }
    std::wstring chosen;
    if (!RunZenityDialog(args, chosen)) return FALSE;
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
    if (!RunZenityDialog(args, chosen)) return nullptr;
    if (lpbi->pszDisplayName) {
        std::wstring leaf = Explorer2::PathUtils::GetFileName(chosen);
        wcsncpy_s(lpbi->pszDisplayName, MAX_PATH, leaf.c_str(), _TRUNCATE);
    }
    return ILCreateFromPathW(chosen.c_str());
}
