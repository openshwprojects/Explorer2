#pragma once
// linux/win32_hal/include/windows.h - Win32 Hardware Abstraction Layer for Linux builds of Explorer2.

#include "PlatformCompat.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifndef WINAPI
#define WINAPI
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef STDAPICALLTYPE
#define STDAPICALLTYPE
#endif
#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif

// ---- Additional Core Scalar, Pointer & Handle Types -----------------------------------------------------------
using VOID = void;
using PVOID = void*;
using CHAR = char;
using PCHAR = char*;
using PWCHAR = wchar_t*;
using BOOLEAN = uint8_t;
using PUCHAR = unsigned char*;
using PBYTE = uint8_t*;
using LPBYTE = uint8_t*;
using LPDWORD = DWORD*;
using PDWORD = DWORD*;
using PULONG = ULONG*;
using PUINT = UINT*;
using DWORD64 = uint64_t;
using PDWORD64 = uint64_t*;
using ULONG64 = uint64_t;
using PULONG64 = uint64_t*;
using LPLONG = LONG*;
using LPBOOL = BOOL*;
using LPHANDLE = HANDLE*;
using PSECURITY_DESCRIPTOR = void*;
using PWSTR = wchar_t*;
using PCWSTR = const wchar_t*;
using LPWCH = wchar_t*;
using LPCWCH = const wchar_t*;
using OLECHAR = wchar_t;
using LPOLESTR = OLECHAR*;
using LPCOLESTR = const OLECHAR*;
using BSTR = OLECHAR*;
using VARTYPE = unsigned short;
using CLIPFORMAT = WORD;
using LRESULT = intptr_t;
using DWORD_PTR = uintptr_t;
using NTSTATUS = int32_t;
using HRESULT = int32_t;
using errno_t = int;

struct HINSTANCE__ { int unused; };
using HINSTANCE = HINSTANCE__*;
using HMODULE = HINSTANCE;

struct HDC__ { int unused; };
using HDC = HDC__*;
using HGDIOBJ = void*;
struct HBITMAP__ { int unused; };
using HBITMAP = HBITMAP__*;
struct HICON__ { int unused; };
using HICON = HICON__*;
using HCURSOR = HICON;
struct HFONT__ { int unused; };
using HFONT = HFONT__*;
struct HBRUSH__ { int unused; };
using HBRUSH = HBRUSH__*;
struct HPEN__ { int unused; };
using HPEN = HPEN__*;
struct HENHMETAFILE__ { int unused; };
using HENHMETAFILE = HENHMETAFILE__*;
struct HMENU__ { int unused; };
using HMENU = HMENU__*;
struct HMONITOR__ { int unused; };
using HMONITOR = HMONITOR__*;
struct HDROP__ { int unused; };
using HDROP = HDROP__*;
struct HKEY__ { int unused; };
using HKEY = HKEY__*;
using PHKEY = HKEY*;
using REGSAM = DWORD;
using HGLOBAL = void*;
using HLOCAL = void*;
using PSID = void*;

// ---- GUID / IID / CLSID ---------------------------------------------------------------------------------------
struct GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
};
using IID = GUID;
using CLSID = GUID;
using REFGUID = const GUID&;
using REFIID = const IID&;
using REFCLSID = const CLSID&;

inline constexpr bool operator==(const GUID& a, const GUID& b) noexcept {
    return a.Data1 == b.Data1 && a.Data2 == b.Data2 && a.Data3 == b.Data3 &&
           a.Data4[0] == b.Data4[0] && a.Data4[1] == b.Data4[1] &&
           a.Data4[2] == b.Data4[2] && a.Data4[3] == b.Data4[3] &&
           a.Data4[4] == b.Data4[4] && a.Data4[5] == b.Data4[5] &&
           a.Data4[6] == b.Data4[6] && a.Data4[7] == b.Data4[7];
}
inline constexpr bool operator!=(const GUID& a, const GUID& b) noexcept { return !(a == b); }
inline constexpr BOOL IsEqualGUID(REFGUID a, REFGUID b) noexcept { return (a == b) ? TRUE : FALSE; }
inline constexpr BOOL IsEqualIID(REFIID a, REFIID b) noexcept { return (a == b) ? TRUE : FALSE; }
inline constexpr BOOL IsEqualCLSID(REFCLSID a, REFCLSID b) noexcept { return (a == b) ? TRUE : FALSE; }
inline constexpr GUID GUID_NULL = { 0, 0, 0, { 0,0,0,0,0,0,0,0 } };

// ---- Bit / Word / Memory Macros -------------------------------------------------------------------------------
#ifndef LOWORD
#define LOWORD(l) (static_cast<WORD>(static_cast<DWORD_PTR>(l) & 0xFFFFu))
#endif
#ifndef HIWORD
#define HIWORD(l) (static_cast<WORD>((static_cast<DWORD_PTR>(l) >> 16) & 0xFFFFu))
#endif
#ifndef LOBYTE
#define LOBYTE(w) (static_cast<BYTE>(static_cast<DWORD_PTR>(w) & 0xFFu))
#endif
#ifndef HIBYTE
#define HIBYTE(w) (static_cast<BYTE>((static_cast<DWORD_PTR>(w) >> 8) & 0xFFu))
#endif
#ifndef MAKEWORD
#define MAKEWORD(a, b) (static_cast<WORD>((static_cast<BYTE>(static_cast<DWORD_PTR>(a) & 0xFFu)) | (static_cast<WORD>(static_cast<BYTE>(static_cast<DWORD_PTR>(b) & 0xFFu)) << 8)))
#endif
#ifndef MAKELONG
#define MAKELONG(a, b) (static_cast<LONG>((static_cast<WORD>(static_cast<DWORD_PTR>(a) & 0xFFFFu)) | (static_cast<DWORD>(static_cast<WORD>(static_cast<DWORD_PTR>(b) & 0xFFFFu)) << 16)))
#endif
#ifndef MAKEWPARAM
#define MAKEWPARAM(l, h) (static_cast<WPARAM>(static_cast<DWORD>(MAKELONG(l, h))))
#endif
#ifndef MAKELPARAM
#define MAKELPARAM(l, h) (static_cast<LPARAM>(static_cast<DWORD>(MAKELONG(l, h))))
#endif
#ifndef MAKEINTRESOURCEW
#define MAKEINTRESOURCEW(i) (reinterpret_cast<LPWSTR>(static_cast<ULONG_PTR>(static_cast<WORD>(i))))
#endif
#ifndef MAKEINTRESOURCEA
#define MAKEINTRESOURCEA(i) (reinterpret_cast<LPSTR>(static_cast<ULONG_PTR>(static_cast<WORD>(i))))
#endif
#ifndef RGB
#define RGB(r, g, b) (static_cast<DWORD>(static_cast<BYTE>(r) | (static_cast<WORD>(static_cast<BYTE>(g)) << 8) | (static_cast<DWORD>(static_cast<BYTE>(b)) << 16)))
#endif

#define ZeroMemory(dst, len) std::memset((dst), 0, (len))
#define CopyMemory(dst, src, len) std::memcpy((dst), (src), (len))
#define MoveMemory(dst, src, len) std::memmove((dst), (src), (len))
#define FillMemory(dst, len, fill) std::memset((dst), (fill), (len))

// ---- HRESULT & Additional Error Codes -------------------------------------------------------------------------
#define E_NOTIMPL    (static_cast<HRESULT>(0x80004001u))
#define E_NOINTERFACE (static_cast<HRESULT>(0x80004002u))
#define E_POINTER    (static_cast<HRESULT>(0x80004003u))
#define E_ABORT      (static_cast<HRESULT>(0x80004004u))
#define E_ACCESSDENIED (static_cast<HRESULT>(0x80070005u))
#define E_OUTOFMEMORY (static_cast<HRESULT>(0x8007000Eu))
#define E_INVALIDARG (static_cast<HRESULT>(0x80070057u))
#define RPC_E_CHANGED_MODE (static_cast<HRESULT>(0x80010106u))
#define DRAGDROP_S_DROP (static_cast<HRESULT>(0x00040100L))
#define DRAGDROP_S_CANCEL (static_cast<HRESULT>(0x00040101L))
#define DRAGDROP_S_USEDEFAULTCURSORS (static_cast<HRESULT>(0x00040102L))
#define FACILITY_WIN32 7
#define HRESULT_FROM_WIN32(x) (static_cast<HRESULT>((x) <= 0 ? (static_cast<HRESULT>(x)) : (static_cast<HRESULT>(((x) & 0x0000FFFFu) | (FACILITY_WIN32 << 16) | 0x80000000u))))
#define HRESULT_FACILITY(hr) (((hr) >> 16) & 0x1FFF)
#define HRESULT_CODE(hr) ((hr) & 0xFFFF)

#define NO_ERROR 0L
#define ERROR_INVALID_FUNCTION 1L
#define ERROR_INVALID_DATA 13L
#define ERROR_CRC 23L
#define ERROR_LOCK_VIOLATION 33L
#define ERROR_REM_NOT_LIST 51L
#define ERROR_DUP_NAME 52L
#define ERROR_BAD_NETPATH 53L
#define ERROR_NETWORK_BUSY 54L
#define ERROR_DEV_NOT_EXIST 55L
#define ERROR_TOO_MANY_CMDS 56L
#define ERROR_BAD_NET_RESP 58L
#define ERROR_UNEXP_NET_ERR 59L
#define ERROR_NETNAME_DELETED 64L
#define ERROR_NETWORK_ACCESS_DENIED 65L
#define ERROR_BAD_NET_NAME 67L
#define ERROR_BUFFER_OVERFLOW 111L
#define ERROR_SEM_TIMEOUT 121L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_INVALID_NAME 123L
#define ERROR_MOD_NOT_FOUND 126L
#define ERROR_PROC_NOT_FOUND 127L
#define ERROR_BAD_PATHNAME 161L
#define ERROR_ENVVAR_NOT_FOUND 203L
#define ERROR_MORE_DATA 234L
#define ERROR_VC_DISCONNECTED 240L
#define ERROR_NO_MORE_ITEMS 259L
#define ERROR_DIRECTORY 267L
#define ERROR_IO_PENDING 997L
#define ERROR_NO_NET_OR_BAD_PATH 1203L
#define ERROR_EXTENDED_ERROR 1208L
#define ERROR_NO_NETWORK 1222L
#define ERROR_CANCELLED 1223L
#define ERROR_CONNECTION_REFUSED 1225L
#define ERROR_NETWORK_UNREACHABLE 1231L
#define ERROR_HOST_UNREACHABLE 1232L
#define ERROR_PORT_UNREACHABLE 1234L
#define ERROR_CONNECTION_ABORTED 1236L
#define ERROR_RETRY 1237L
#define ERROR_HOST_DOWN 1256L
#define ERROR_LOGON_FAILURE 1326L

#define FILE_ATTRIBUTE_COMPRESSED 0x00000800u
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED 0x00002000u
#define FILE_ATTRIBUTE_ENCRYPTED 0x00004000u

#define HWND_MESSAGE (reinterpret_cast<HWND>(static_cast<intptr_t>(-3)))
#define HTCLIENT 1
#define COLOR_WINDOW 5
#define IMAGE_BITMAP 0
#define IMAGE_ICON 1
#define IMAGE_CURSOR 2
#define LR_DEFAULTCOLOR 0x00000000u
#define LR_SHARED 0x00008000u

#define WAIT_OBJECT_0 0x00000000u
#define WAIT_ABANDONED 0x00000080u
#define WAIT_TIMEOUT 0x00000102u
#define WAIT_FAILED 0xFFFFFFFFu

// ---- File, Drive & Memory Flags -------------------------------------------------------------------------------
#define DELETE 0x00010000u
#define READ_CONTROL 0x00020000u
#define SYNCHRONIZE 0x00100000u
#define FILE_LIST_DIRECTORY 0x0001u
#define FILE_READ_DATA 0x0001u
#define FILE_WRITE_DATA 0x0002u
#define FILE_APPEND_DATA 0x0004u
#define FILE_READ_ATTRIBUTES 0x0080u
#define FILE_WRITE_ATTRIBUTES 0x0100u
#define MOVEFILE_WRITE_THROUGH 0x00000008u
#define COPY_FILE_FAIL_IF_EXISTS 0x00000001u
#define COPY_FILE_RESTARTABLE 0x00000002u
#define COPY_FILE_OPEN_SOURCE_FOR_WRITE 0x00000004u
#define COPY_FILE_ALLOW_DECRYPTED_DESTINATION 0x00000008u
#define COPY_FILE_COPY_SYMLINK 0x00000800u
#define COPY_FILE_NO_BUFFERING 0x00001000u
#define COPY_FILE_REQUEST_COMPRESSED_TRAFFIC 0x10000000u
#define PROGRESS_CONTINUE 0
#define PROGRESS_CANCEL 1
#define PROGRESS_STOP 2
#define PROGRESS_QUIET 3

#define FILE_NOTIFY_CHANGE_FILE_NAME 0x00000001u
#define FILE_NOTIFY_CHANGE_DIR_NAME 0x00000002u
#define FILE_NOTIFY_CHANGE_ATTRIBUTES 0x00000004u
#define FILE_NOTIFY_CHANGE_SIZE 0x00000008u
#define FILE_NOTIFY_CHANGE_LAST_WRITE 0x00000010u

#define DRIVE_UNKNOWN 0
#define DRIVE_NO_ROOT_DIR 1
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED 3
#define DRIVE_REMOTE 4
#define DRIVE_CDROM 5
#define DRIVE_RAMDISK 6

#define FILE_VOLUME_IS_COMPRESSED 0x00008000u
#define FILE_SUPPORTS_SPARSE_FILES 0x00000040u
#define FILE_SUPPORTS_REPARSE_POINTS 0x00000080u
#define FILE_SUPPORTS_ENCRYPTION 0x00020000u
#define FILE_SUPPORTS_BLOCK_REFCOUNTING 0x08000000u

#define SEM_FAILCRITICALERRORS 0x0001u
#define SEM_NOGPFAULTERRORBOX 0x0002u
#define SEM_NOOPENFILEERRORBOX 0x8000u

#define MEM_COMMIT 0x00001000u
#define MEM_RESERVE 0x00002000u
#define MEM_RELEASE 0x00008000u
#define PAGE_READWRITE 0x04u

#define GMEM_FIXED 0x0000u
#define GMEM_MOVEABLE 0x0002u
#define GMEM_ZEROINIT 0x0040u

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100u
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200u
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000u

#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800u
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002u
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004u

// ---- Geometry, Windowing, Input, Menus & GDI Structs/Constants ------------------------------------------------
struct POINT {
    LONG x;
    LONG y;
};
using LPPOINT = POINT*;

struct SIZE {
    LONG cx;
    LONG cy;
};
using LPSIZE = SIZE*;

struct RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
};
using LPRECT = RECT*;
using LPCRECT = const RECT*;

using WNDPROC = LRESULT (CALLBACK*)(HWND, UINT, WPARAM, LPARAM);
using TIMERPROC = void (CALLBACK*)(HWND, UINT, UINT_PTR, DWORD);

struct MSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
};
using LPMSG = MSG*;

struct WNDCLASSEXW {
    UINT cbSize;
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCWSTR lpszMenuName;
    LPCWSTR lpszClassName;
    HICON hIconSm;
};

struct CREATESTRUCTW {
    LPVOID lpCreateParams;
    HINSTANCE hInstance;
    HMENU hMenu;
    HWND hwndParent;
    int cy;
    int cx;
    int y;
    int x;
    LONG style;
    LPCWSTR lpszName;
    LPCWSTR lpszClass;
    DWORD dwExStyle;
};

struct PAINTSTRUCT {
    HDC hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
};
using LPPAINTSTRUCT = PAINTSTRUCT*;

struct WINDOWPLACEMENT {
    UINT length;
    UINT flags;
    UINT showCmd;
    POINT ptMinPosition;
    POINT ptMaxPosition;
    RECT rcNormalPosition;
};

struct MONITORINFO {
    DWORD cbSize;
    RECT rcMonitor;
    RECT rcWork;
    DWORD dwFlags;
};
using LPMONITORINFO = MONITORINFO*;

struct MENUITEMINFOW {
    UINT cbSize;
    UINT fMask;
    UINT fType;
    UINT fState;
    UINT wID;
    HMENU hSubMenu;
    HBITMAP hbmpChecked;
    HBITMAP hbmpUnchecked;
    ULONG_PTR dwItemData;
    LPWSTR dwTypeData;
    UINT cch;
    HBITMAP hbmpItem;
};
using LPCMENUITEMINFOW = const MENUITEMINFOW*;

// Window Styles & Messages
#define CS_VREDRAW 0x0001u
#define CS_HREDRAW 0x0002u
#define CS_DBLCLKS 0x0008u

#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP      (static_cast<LONG>(0x80000000u))
#define WS_CHILD      0x40000000L
#define WS_MINIMIZE   0x20000000L
#define WS_VISIBLE    0x10000000L
#define WS_DISABLED   0x08000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_MAXIMIZE   0x01000000L
#define WS_CAPTION    0x00C00000L
#define WS_BORDER     0x00800000L
#define WS_DLGFRAME   0x00400000L
#define WS_VSCROLL    0x00200000L
#define WS_HSCROLL    0x00100000L
#define WS_SYSMENU    0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_GROUP      0x00020000L
#define WS_TABSTOP    0x00010000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)

#define WS_EX_DLGMODALFRAME 0x00000001u
#define WS_EX_TOPMOST       0x00000008u
#define WS_EX_ACCEPTFILES   0x00000010u
#define WS_EX_TOOLWINDOW    0x00000080u
#define WS_EX_WINDOWEDGE    0x00000100u
#define WS_EX_CLIENTEDGE    0x00000200u
#define WS_EX_APPWINDOW     0x00040000u

#define CW_USEDEFAULT (static_cast<int>(0x80000000u))

#define ES_LEFT        0x0000L
#define ES_MULTILINE   0x0004L
#define ES_AUTOVSCROLL 0x0040L
#define ES_AUTOHSCROLL 0x0080L
#define ES_NOHIDESEL   0x0100L
#define ES_READONLY    0x0800L
#define ES_WANTRETURN  0x1000L
#define BS_PUSHBUTTON    0x0000L
#define BS_DEFPUSHBUTTON 0x0001L
#define SS_LEFT          0x0000L

#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10

#define HWND_DESKTOP (reinterpret_cast<HWND>(0))
#define HWND_TOP     (reinterpret_cast<HWND>(0))
#define HWND_TOPMOST (reinterpret_cast<HWND>(static_cast<intptr_t>(-1)))

#define SWP_NOSIZE       0x0001u
#define SWP_NOMOVE       0x0002u
#define SWP_NOZORDER     0x0004u
#define SWP_NOREDRAW     0x0008u
#define SWP_NOACTIVATE   0x0010u
#define SWP_FRAMECHANGED 0x0020u
#define SWP_SHOWWINDOW   0x0040u
#define SWP_HIDEWINDOW   0x0080u
#define SWP_NOOWNERZORDER 0x0200u

#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWLP_WNDPROC (-4)
#define GWLP_HINSTANCE (-6)
#define GWLP_HWNDPARENT (-8)
#define GWLP_USERDATA (-21)
#define GWLP_ID (-12)
#define GA_PARENT 1
#define GA_ROOT 2
#define GA_ROOTOWNER 3

#define PM_NOREMOVE 0x0000u
#define PM_REMOVE   0x0001u

// Window Messages
#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_GETTEXTLENGTH 0x000E
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_ENDSESSION 0x0016
#define WM_SHOWWINDOW 0x0018
#define WM_SETCURSOR 0x0020
#define WM_GETMINMAXINFO 0x0024
#define WM_SETFONT 0x0030
#define WM_GETFONT 0x0031
#define WM_NOTIFY 0x004E
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCCALCSIZE 0x0083
#define WM_NCHITTEST 0x0084
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_HSCROLL 0x0114
#define WM_VSCROLL 0x0115
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MOUSEWHEEL 0x020A
#define WM_XBUTTONDOWN 0x020B
#define WM_XBUTTONUP 0x020C
#define WM_MOUSELEAVE 0x02A3
#define WM_DROPFILES 0x0233
#define WM_DPICHANGED 0x02E0
#define WM_COPY 0x0301
#define WM_PASTE 0x0302
#define WM_USER 0x0400
#define WM_APP 0x8000

// Edit Control Messages & Notifications
#define EM_GETSEL 0x00B0
#define EM_SETSEL 0x00B1
#define EM_SCROLLCARET 0x00B7
#define EM_GETMODIFY 0x00B8
#define EM_SETMODIFY 0x00B9
#define EM_GETLINECOUNT 0x00BA
#define EM_LINEINDEX 0x00BB
#define EM_LINELENGTH 0x00C1
#define EM_REPLACESEL 0x00C2
#define EM_SETLIMITTEXT 0x00C5
#define EM_LINEFROMCHAR 0x00C9
#define EM_SETREADONLY 0x00CF
#define EM_GETFIRSTVISIBLELINE 0x00CE
#define EN_SETFOCUS 0x0100
#define EN_KILLFOCUS 0x0200
#define EN_CHANGE 0x0300
#define EN_UPDATE 0x0400
#define EN_MAXTEXT 0x0501
#define BN_CLICKED 0

// Scrollbar constants
#define SB_LINEUP 0
#define SB_LINEDOWN 1
#define SB_PAGEUP 2
#define SB_PAGEDOWN 3
#define SB_THUMBPOSITION 4
#define SB_THUMBTRACK 5
#define SB_TOP 6
#define SB_BOTTOM 7
#define SB_ENDSCROLL 8

// Mouse & Virtual Key Constants
#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT   0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010
#define MK_XBUTTON1 0x0020
#define MK_XBUTTON2 0x0040
#define XBUTTON1 0x0001
#define XBUTTON2 0x0002
#define WHEEL_DELTA 120
#define GET_WHEEL_DELTA_WPARAM(wParam) (static_cast<short>(HIWORD(wParam)))
#define GET_KEYSTATE_WPARAM(wParam) (LOWORD(wParam))
#define GET_XBUTTON_WPARAM(wParam) (HIWORD(wParam))

#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_BACK    0x08
#define VK_TAB     0x09
#define VK_RETURN  0x0D
#define VK_SHIFT   0x10
#define VK_CONTROL 0x11
#define VK_MENU    0x12
#define VK_PAUSE   0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE  0x1B
#define VK_SPACE   0x20
#define VK_PRIOR   0x21
#define VK_NEXT    0x22
#define VK_END     0x23
#define VK_HOME    0x24
#define VK_LEFT    0x25
#define VK_UP      0x26
#define VK_RIGHT   0x27
#define VK_DOWN    0x28
#define VK_INSERT  0x2D
#define VK_DELETE  0x2E
#define VK_LWIN    0x5B
#define VK_RWIN    0x5C
#define VK_APPS    0x5D
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_MULTIPLY 0x6A
#define VK_ADD     0x6B
#define VK_SUBTRACT 0x6D
#define VK_F1      0x70
#define VK_F2      0x71
#define VK_F3      0x72
#define VK_F4      0x73
#define VK_F5      0x74
#define VK_F6      0x75
#define VK_F7      0x76
#define VK_F8      0x77
#define VK_F9      0x78
#define VK_F10     0x79
#define VK_F11     0x7A
#define VK_F12     0x7B
#define VK_OEM_PLUS  0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2   0xBF

// Cursors, Metrics, Monitors, MessageBox, Menus, Clipboard
#define IDC_ARROW  MAKEINTRESOURCEW(32512)
#define IDC_IBEAM  MAKEINTRESOURCEW(32513)
#define IDC_WAIT   MAKEINTRESOURCEW(32514)
#define IDC_CROSS  MAKEINTRESOURCEW(32515)
#define IDC_SIZENWSE MAKEINTRESOURCEW(32642)
#define IDC_SIZENESW MAKEINTRESOURCEW(32643)
#define IDC_SIZEWE MAKEINTRESOURCEW(32644)
#define IDC_SIZENS MAKEINTRESOURCEW(32645)
#define IDC_HAND   MAKEINTRESOURCEW(32649)

#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXICON 11
#define SM_CYICON 12
#define SM_CXSMICON 49
#define SM_CYSMICON 50

#define MONITOR_DEFAULTTONULL 0x00000000u
#define MONITOR_DEFAULTTOPRIMARY 0x00000001u
#define MONITOR_DEFAULTTONEAREST 0x00000002u

#define SPI_SETDESKWALLPAPER 0x0014u
#define SPIF_UPDATEINIFILE 0x0001u
#define SPIF_SENDCHANGE 0x0002u

#define MB_OK 0x00000000u
#define MB_OKCANCEL 0x00000001u
#define MB_YESNOCANCEL 0x00000003u
#define MB_YESNO 0x00000004u
#define MB_ICONHAND 0x00000010u
#define MB_ICONERROR MB_ICONHAND
#define MB_ICONQUESTION 0x00000020u
#define MB_ICONEXCLAMATION 0x00000030u
#define MB_ICONWARNING MB_ICONEXCLAMATION
#define MB_ICONASTERISK 0x00000040u
#define MB_ICONINFORMATION MB_ICONASTERISK
#define MB_DEFBUTTON1 0x00000000u
#define MB_DEFBUTTON2 0x00000100u

#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7

#define MF_ENABLED 0x00000000u
#define MF_GRAYED 0x00000001u
#define MF_DISABLED 0x00000002u
#define MF_STRING 0x00000000u
#define MF_BITMAP 0x00000004u
#define MF_UNCHECKED 0x00000000u
#define MF_CHECKED 0x00000008u
#define MF_POPUP 0x00000010u
#define MF_MENUBARBREAK 0x00000020u
#define MF_MENUBREAK 0x00000040u
#define MF_SEPARATOR 0x00000800u
#define MIIM_STATE 0x00000001u
#define MIIM_ID 0x00000002u
#define MIIM_SUBMENU 0x00000004u
#define MIIM_STRING 0x00000040u
#define MIIM_BITMAP 0x00000080u
#define TPM_LEFTBUTTON 0x0000u
#define TPM_RIGHTBUTTON 0x0002u
#define TPM_LEFTALIGN 0x0000u
#define TPM_CENTERALIGN 0x0004u
#define TPM_RIGHTALIGN 0x0008u
#define TPM_TOPALIGN 0x0000u
#define TPM_VCENTERALIGN 0x0010u
#define TPM_BOTTOMALIGN 0x0020u
#define TPM_NONOTIFY 0x0080u
#define TPM_RETURNCMD 0x0100u

#define CF_TEXT 1
#define CF_BITMAP 2
#define CF_UNICODETEXT 13
#define CF_HDROP 15

// ---- GDI Structs & Constants ----------------------------------------------------------------------------------
#define BI_RGB 0u
#define DIB_RGB_COLORS 0u
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define DEFAULT_GUI_FONT 17

#define FW_DONTCARE 0
#define FW_NORMAL   400
#define FW_MEDIUM   500
#define FW_SEMIBOLD 600
#define FW_BOLD     700
#define ANSI_CHARSET    0
#define DEFAULT_CHARSET 1
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_QUALITY 0
#define DRAFT_QUALITY 1
#define PROOF_QUALITY 2
#define NONANTIALIASED_QUALITY 3
#define ANTIALIASED_QUALITY 4
#define CLEARTYPE_QUALITY 5
#define DEFAULT_PITCH 0
#define FIXED_PITCH   1
#define VARIABLE_PITCH 2
#define FF_DONTCARE   (0 << 4)
#define FF_ROMAN      (1 << 4)
#define FF_SWISS      (2 << 4)
#define FF_MODERN     (3 << 4)
#define PS_SOLID      0
#define PS_DASH       1
#define DI_NORMAL     0x0003u

struct RGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
};

struct BITMAP {
    LONG bmType;
    LONG bmWidth;
    LONG bmHeight;
    LONG bmWidthBytes;
    WORD bmPlanes;
    WORD bmBitsPixel;
    LPVOID bmBits;
};

struct BITMAPINFOHEADER {
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
};

struct BITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
};

// ---- Registry Constants ---------------------------------------------------------------------------------------
#define HKEY_CLASSES_ROOT        (reinterpret_cast<HKEY>(static_cast<uintptr_t>(0x80000000u)))
#define HKEY_CURRENT_USER        (reinterpret_cast<HKEY>(static_cast<uintptr_t>(0x80000001u)))
#define HKEY_LOCAL_MACHINE       (reinterpret_cast<HKEY>(static_cast<uintptr_t>(0x80000002u)))
#define HKEY_USERS               (reinterpret_cast<HKEY>(static_cast<uintptr_t>(0x80000003u)))

#define KEY_QUERY_VALUE 0x0001u
#define KEY_SET_VALUE   0x0002u
#define KEY_CREATE_SUB_KEY 0x0004u
#define KEY_ENUMERATE_SUB_KEYS 0x0008u
#define KEY_READ        0x20019u
#define KEY_WRITE       0x20006u
#define KEY_ALL_ACCESS  0xF003Fu
#define KEY_WOW64_64KEY 0x0100u
#define KEY_WOW64_32KEY 0x0200u

#define REG_NONE 0u
#define REG_SZ 1u
#define REG_EXPAND_SZ 2u
#define REG_BINARY 3u
#define REG_DWORD 4u
#define REG_MULTI_SZ 7u
#define REG_QWORD 11u
#define RRF_RT_REG_SZ 0x00000002u
#define RRF_RT_REG_DWORD 0x00000010u
#define RRF_RT_ANY 0x0000FFFFu

// ---- Process, Pipe, Security, System & Exception Structs ------------------------------------------------------
struct SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
};
using LPSECURITY_ATTRIBUTES = SECURITY_ATTRIBUTES*;

struct OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct {
            DWORD Offset;
            DWORD OffsetHigh;
        };
        PVOID Pointer;
    };
    HANDLE hEvent;
};
using LPOVERLAPPED = OVERLAPPED*;

struct STARTUPINFOW {
    DWORD cb;
    LPWSTR lpReserved;
    LPWSTR lpDesktop;
    LPWSTR lpTitle;
    DWORD dwX;
    DWORD dwY;
    DWORD dwXSize;
    DWORD dwYSize;
    DWORD dwXCountChars;
    DWORD dwYCountChars;
    DWORD dwFillAttribute;
    DWORD dwFlags;
    WORD wShowWindow;
    WORD cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
};

struct _PROC_THREAD_ATTRIBUTE_LIST;
using LPPROC_THREAD_ATTRIBUTE_LIST = _PROC_THREAD_ATTRIBUTE_LIST*;

struct STARTUPINFOEXW {
    STARTUPINFOW StartupInfo;
    LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList;
};

struct PROCESS_INFORMATION {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD dwProcessId;
    DWORD dwThreadId;
};

#define STARTF_USESHOWWINDOW 0x00000001u
#define STARTF_USESTDHANDLES 0x00000100u
#define CREATE_SUSPENDED 0x00000004u
#define DETACHED_PROCESS 0x00000008u
#define CREATE_NEW_PROCESS_GROUP 0x00000200u
#define CREATE_UNICODE_ENVIRONMENT 0x00000400u
#define BELOW_NORMAL_PRIORITY_CLASS 0x00004000u
#define NORMAL_PRIORITY_CLASS 0x00000020u
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000u
#define CREATE_NO_WINDOW 0x08000000u
#define EVENT_MODIFY_STATE 0x0002u
#define HANDLE_FLAG_INHERIT 0x00000001u
#define PROC_THREAD_ATTRIBUTE_HANDLE_LIST 0x00020002u
#define STILL_ACTIVE 259u
#define PROCESS_TERMINATE 0x0001u
#define PROCESS_VM_READ 0x0010u
#define PROCESS_QUERY_INFORMATION 0x0400u
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000u

#define JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE 0x00002000u
enum JOBOBJECTINFOCLASS { JobObjectExtendedLimitInformation = 9 };
struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
    LARGE_INTEGER PerProcessUserTimeLimit;
    LARGE_INTEGER PerJobUserTimeLimit;
    DWORD LimitFlags;
    SIZE_T MinimumWorkingSetSize;
    SIZE_T MaximumWorkingSetSize;
    DWORD ActiveProcessLimit;
    ULONG_PTR Affinity;
    DWORD PriorityClass;
    DWORD SchedulingClass;
};
struct IO_COUNTERS {
    ULONGLONG ReadOperationCount;
    ULONGLONG WriteOperationCount;
    ULONGLONG OtherOperationCount;
    ULONGLONG ReadTransferCount;
    ULONGLONG WriteTransferCount;
    ULONGLONG OtherTransferCount;
};
struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
    JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
    IO_COUNTERS IoInfo;
    SIZE_T ProcessMemoryLimit;
    SIZE_T JobMemoryLimit;
    SIZE_T PeakProcessMemoryUsed;
    SIZE_T PeakJobMemoryUsed;
};

struct BY_HANDLE_FILE_INFORMATION {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD dwVolumeSerialNumber;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD nNumberOfLinks;
    DWORD nFileIndexHigh;
    DWORD nFileIndexLow;
};

struct FILE_NOTIFY_INFORMATION {
    DWORD NextEntryOffset;
    DWORD Action;
    DWORD FileNameLength;
    WCHAR FileName[1];
};

enum FINDEX_INFO_LEVELS { FindExInfoStandard = 0, FindExInfoBasic = 1 };
enum FINDEX_SEARCH_OPS { FindExSearchNameMatch = 0 };
#define FIND_FIRST_EX_LARGE_FETCH 0x00000002u

enum FILE_INFO_BY_HANDLE_CLASS {
    FileBasicInfo = 0,
    FileStandardInfo = 1,
    FileNameInfo = 2,
    FileRenameInfo = 3,
    FileDispositionInfo = 4,
    FileAllocationInfo = 5,
    FileEndOfFileInfo = 6
};
struct FILE_BASIC_INFO {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    DWORD FileAttributes;
};
struct FILE_END_OF_FILE_INFO {
    LARGE_INTEGER EndOfFile;
};
struct FILE_ALLOCATION_INFO {
    LARGE_INTEGER AllocationSize;
};
struct FILE_DISPOSITION_INFO {
    BYTE DeleteFile;
};

struct MEMORYSTATUSEX {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    ULONGLONG ullTotalPhys;
    ULONGLONG ullAvailPhys;
    ULONGLONG ullTotalPageFile;
    ULONGLONG ullAvailPageFile;
    ULONGLONG ullTotalVirtual;
    ULONGLONG ullAvailVirtual;
    ULONGLONG ullAvailExtendedVirtual;
};

#define PROCESSOR_ARCHITECTURE_INTEL 0
#define PROCESSOR_ARCHITECTURE_ARM 5
#define PROCESSOR_ARCHITECTURE_AMD64 9
#define PROCESSOR_ARCHITECTURE_ARM64 12
#define PF_VIRT_FIRMWARE_ENABLED 21

struct SYSTEM_INFO {
    union {
        DWORD dwOemId;
        struct {
            WORD wProcessorArchitecture;
            WORD wReserved;
        };
    };
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
     DWORD_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
};
using LPSYSTEM_INFO = SYSTEM_INFO*;

enum LOGICAL_PROCESSOR_RELATIONSHIP {
    RelationProcessorCore = 0,
    RelationNumaNode = 1,
    RelationCache = 2,
    RelationProcessorPackage = 3,
    RelationGroup = 4,
    RelationAll = 0xffff
};
enum PROCESSOR_CACHE_TYPE { CacheUnified = 0, CacheInstruction = 1, CacheData = 2, CacheTrace = 3 };
struct CACHE_RELATIONSHIP {
    BYTE Level;
    BYTE Associativity;
    WORD LineSize;
    DWORD CacheSize;
    PROCESSOR_CACHE_TYPE Type;
    BYTE Reserved[18];
    WORD GroupCount;
    ULONG_PTR GroupMask;
};
struct PROCESSOR_RELATIONSHIP {
    BYTE Flags;
    BYTE EfficiencyClass;
    BYTE Reserved[20];
    WORD GroupCount;
    struct {
        ULONG_PTR Mask;
        WORD Group;
        WORD Reserved[3];
    } GroupMask[1];
};
struct NUMA_NODE_RELATIONSHIP {
    DWORD NodeNumber;
    BYTE Reserved[18];
    WORD GroupCount;
    ULONG_PTR GroupMask;
};
struct SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX {
    LOGICAL_PROCESSOR_RELATIONSHIP Relationship;
    DWORD Size;
    union {
        PROCESSOR_RELATIONSHIP Processor;
        NUMA_NODE_RELATIONSHIP NumaNode;
        CACHE_RELATIONSHIP Cache;
    };
};
using PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX = SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*;

struct SYSTEM_POWER_STATUS {
    BYTE ACLineStatus;
    BYTE BatteryFlag;
    BYTE BatteryLifePercent;
    BYTE SystemStatusFlag;
    DWORD BatteryLifeTime;
    DWORD BatteryFullLifeTime;
};

struct RTL_OSVERSIONINFOW {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    WCHAR szCSDVersion[128];
};
using PRTL_OSVERSIONINFOW = RTL_OSVERSIONINFOW*;

#define MAX_COMPUTERNAME_LENGTH 31

// Display structs
#define DISPLAY_DEVICE_ATTACHED_TO_DESKTOP 0x00000001u
#define DISPLAY_DEVICE_PRIMARY_DEVICE 0x00000004u
#define ENUM_CURRENT_SETTINGS (static_cast<DWORD>(-1))
struct DISPLAY_DEVICEW {
    DWORD cb;
    WCHAR DeviceName[32];
    WCHAR DeviceString[128];
    DWORD StateFlags;
    WCHAR DeviceID[128];
    WCHAR DeviceKey[128];
};
struct DEVMODEW {
    WCHAR dmDeviceName[32];
    WORD dmSpecVersion;
    WORD dmDriverVersion;
    WORD dmSize;
    WORD dmDriverExtra;
    DWORD dmFields;
    DWORD dmPelsWidth;
    DWORD dmPelsHeight;
    DWORD dmBitsPerPel;
    DWORD dmDisplayFrequency;
};

// Token / Security
#define TOKEN_QUERY 0x0008u
enum TOKEN_INFORMATION_CLASS { TokenUser = 1, TokenElevation = 20 };
struct SID_AND_ATTRIBUTES {
    PSID Sid;
    DWORD Attributes;
};
struct TOKEN_USER {
    SID_AND_ATTRIBUTES User;
};
struct TOKEN_ELEVATION {
    DWORD TokenIsElevated;
};

// Exceptions
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
struct EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    EXCEPTION_RECORD* ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[15];
};
struct CONTEXT {
    uint64_t Rip;
    uint64_t Rsp;
};
struct EXCEPTION_POINTERS {
    EXCEPTION_RECORD* ExceptionRecord;
    CONTEXT* ContextRecord;
};
using LPTOP_LEVEL_EXCEPTION_FILTER = LONG (WINAPI*)(EXCEPTION_POINTERS*);

// ---- Inline Interlocked & Math Helpers ------------------------------------------------------------------------
inline LONG InterlockedIncrement(volatile LONG* addend) noexcept {
    return __atomic_add_fetch(addend, 1, __ATOMIC_SEQ_CST);
}
inline ULONG InterlockedIncrement(volatile ULONG* addend) noexcept {
    return __atomic_add_fetch(addend, 1u, __ATOMIC_SEQ_CST);
}
inline LONG InterlockedDecrement(volatile LONG* addend) noexcept {
    return __atomic_sub_fetch(addend, 1, __ATOMIC_SEQ_CST);
}
inline ULONG InterlockedDecrement(volatile ULONG* addend) noexcept {
    return __atomic_sub_fetch(addend, 1u, __ATOMIC_SEQ_CST);
}
inline LONG InterlockedExchange(volatile LONG* target, LONG value) noexcept {
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}
inline int MulDiv(int nNumber, int nNumerator, int nDenominator) noexcept {
    if (nDenominator == 0) return -1;
    const int64_t prod = static_cast<int64_t>(nNumber) * static_cast<int64_t>(nNumerator);
    const int64_t half = (nDenominator > 0) ? (nDenominator / 2) : (-nDenominator / 2);
    if ((prod >= 0 && nDenominator > 0) || (prod < 0 && nDenominator < 0))
        return static_cast<int>((prod + (prod >= 0 ? half : -half)) / nDenominator);
    return static_cast<int>((prod - (prod >= 0 ? -half : half)) / nDenominator);
}
inline int lstrcmpiW(LPCWSTR a, LPCWSTR b) { return (!a && !b) ? 0 : (!a ? -1 : (!b ? 1 : _wcsicmp(a, b))); }
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount) {
    if (!lpPerformanceCount) return FALSE;
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    lpPerformanceCount->QuadPart = static_cast<LONGLONG>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
    return TRUE;
}
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency) {
    if (!lpFrequency) return FALSE;
    lpFrequency->QuadPart = 1000000000LL;
    return TRUE;
}

// ---- Win32 HAL Function Declarations (implemented in linux/win32_hal/src/*.cpp) -------------------------------
using LPPROGRESS_ROUTINE = DWORD (CALLBACK*)(LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, LPVOID);

// Module & Command Line
LPWSTR GetCommandLineW();
HMODULE GetModuleHandleW(LPCWSTR lpModuleName);
BOOL GetModuleHandleExW(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE* phModule);
DWORD GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
HMODULE LoadLibraryW(LPCWSTR lpLibFileName);
HMODULE LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
BOOL FreeLibrary(HMODULE hLibModule);
void* GetProcAddress(HMODULE hModule, LPCSTR lpProcName);

// Environment, System & Memory
BOOL SetEnvironmentVariableW(LPCWSTR lpName, LPCWSTR lpValue);
DWORD ExpandEnvironmentStringsW(LPCWSTR lpSrc, LPWSTR lpDst, DWORD nSize);
LPWCH GetEnvironmentStringsW();
BOOL FreeEnvironmentStringsW(LPWCH penv);
UINT GetWindowsDirectoryW(LPWSTR lpBuffer, UINT uSize);
DWORD SearchPathW(LPCWSTR lpPath, LPCWSTR lpFileName, LPCWSTR lpExtension, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR* lpFilePart);
DWORD FormatMessageW(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPWSTR lpBuffer, DWORD nSize, va_list* Arguments);
UINT SetErrorMode(UINT uMode);
BOOL SetThreadErrorMode(DWORD dwNewMode, LPDWORD lpOldMode);
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);
BOOL IsBadReadPtr(const void* lp, UINT_PTR ucb);
LPVOID VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect);
BOOL VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType);
HGLOBAL GlobalAlloc(UINT uFlags, SIZE_T dwBytes);
LPVOID GlobalLock(HGLOBAL hMem);
BOOL GlobalUnlock(HGLOBAL hMem);
HGLOBAL GlobalFree(HGLOBAL hMem);
SIZE_T GlobalSize(HGLOBAL hMem);
HLOCAL LocalFree(HLOCAL hMem);
BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX* lpBuffer);
void GetNativeSystemInfo(LPSYSTEM_INFO lpSystemInfo);
void GetSystemInfo(LPSYSTEM_INFO lpSystemInfo);
BOOL IsProcessorFeaturePresent(DWORD ProcessorFeature);
BOOL GetLogicalProcessorInformationEx(LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType, SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* Buffer, PDWORD ReturnedLength);
UINT GetSystemFirmwareTable(DWORD FirmwareTableProviderSignature, DWORD FirmwareTableID, PVOID pFirmwareTableBuffer, DWORD BufferSize);
BOOL GetSystemPowerStatus(SYSTEM_POWER_STATUS* lpSystemPowerStatus);
BOOL GetComputerNameW(LPWSTR lpBuffer, LPDWORD nSize);
BOOL GetUserNameW(LPWSTR lpBuffer, LPDWORD pcbBuffer);
BOOL EnumDisplayDevicesW(LPCWSTR lpDevice, DWORD iDevNum, DISPLAY_DEVICEW* lpDisplayDevice, DWORD dwFlags);
BOOL EnumDisplaySettingsExW(LPCWSTR lpszDeviceName, DWORD iModeNum, DEVMODEW* lpDevMode, DWORD dwFlags);

// File, Directory, Volume & INI Operations
HANDLE FindFirstFileExW(LPCWSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId, LPVOID lpFindFileData, FINDEX_SEARCH_OPS fSearchOp, LPVOID lpSearchFilter, DWORD dwAdditionalFlags);
BOOL CreateHardLinkW(LPCWSTR lpFileName, LPCWSTR lpExistingFileName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
BOOL CopyFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, LPPROGRESS_ROUTINE lpProgressRoutine, LPVOID lpData, LPBOOL pbCancel, DWORD dwCopyFlags);
BOOL CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists);
BOOL GetFileInformationByHandle(HANDLE hFile, BY_HANDLE_FILE_INFORMATION* lpFileInformation);
BOOL SetFileInformationByHandle(HANDLE hFile, int FileInformationClass, LPVOID lpFileInformation, DWORD dwBufferSize);
DWORD GetCompressedFileSizeW(LPCWSTR lpFileName, LPDWORD lpFileSizeHigh);
DWORD GetLogicalDrives();
DWORD GetLogicalDriveStringsW(DWORD nBufferLength, LPWSTR lpBuffer);
UINT GetDriveTypeW(LPCWSTR lpRootPathName);
BOOL GetDiskFreeSpaceExW(LPCWSTR lpDirectoryName, ULARGE_INTEGER* lpFreeBytesAvailableToCaller, ULARGE_INTEGER* lpTotalNumberOfBytes, ULARGE_INTEGER* lpTotalNumberOfFreeBytes);
BOOL GetDiskFreeSpaceW(LPCWSTR lpRootPathName, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters);
BOOL GetVolumePathNameW(LPCWSTR lpszFileName, LPWSTR lpszVolumePathName, DWORD cchBufferLength);
BOOL GetVolumeInformationW(LPCWSTR lpRootPathName, LPWSTR lpVolumeNameBuffer, DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags, LPWSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize);
BOOL ReadDirectoryChangesW(HANDLE hDirectory, LPVOID lpBuffer, DWORD nBufferLength, BOOL bWatchSubtree, DWORD dwNotifyFilter, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped, void* lpCompletionRoutine);
BOOL CancelIo(HANDLE hFile);
BOOL GetOverlappedResult(HANDLE hFile, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred, BOOL bWait);
BOOL DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped);

UINT GetPrivateProfileIntW(LPCWSTR lpAppName, LPCWSTR lpKeyName, INT nDefault, LPCWSTR lpFileName);
DWORD GetPrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName, LPCWSTR lpDefault, LPWSTR lpReturnedString, DWORD nSize, LPCWSTR lpFileName);
BOOL WritePrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName, LPCWSTR lpString, LPCWSTR lpFileName);

// Sync, Events, Mutexes, Pipes, Processes & Jobs
HANDLE GetCurrentProcess();
BOOL SetPriorityClass(HANDLE hProcess, DWORD dwPriorityClass);
HANDLE OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId);
BOOL TerminateProcess(HANDLE hProcess, UINT uExitCode);
BOOL GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode);
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCWSTR lpName);
HANDLE OpenEventW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);
BOOL SetEvent(HANDLE hEvent);
BOOL ResetEvent(HANDLE hEvent);
HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCWSTR lpName);
HANDLE OpenMutexW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);
BOOL ReleaseMutex(HANDLE hMutex);
DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
DWORD WaitForMultipleObjects(DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds);
BOOL CreatePipe(HANDLE* hReadPipe, HANDLE* hWritePipe, LPSECURITY_ATTRIBUTES lpPipeAttributes, DWORD nSize);
BOOL PeekNamedPipe(HANDLE hNamedPipe, LPVOID lpBuffer, DWORD nBufferSize, LPDWORD lpBytesRead, LPDWORD lpTotalBytesAvail, LPDWORD lpBytesLeftThisMessage);
BOOL SetHandleInformation(HANDLE hObject, DWORD dwMask, DWORD dwFlags);
BOOL InitializeProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList, DWORD dwAttributeCount, DWORD dwFlags, SIZE_T* lpSize);
BOOL UpdateProcThreadAttribute(LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList, DWORD dwFlags, DWORD_PTR Attribute, PVOID lpValue, SIZE_T cbSize, PVOID lpPreviousValue, SIZE_T* lpReturnSize);
void DeleteProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList);
BOOL CreateProcessW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, STARTUPINFOW* lpStartupInfo, PROCESS_INFORMATION* lpProcessInformation);
DWORD ResumeThread(HANDLE hThread);
HANDLE CreateJobObjectW(LPSECURITY_ATTRIBUTES lpJobAttributes, LPCWSTR lpName);
BOOL SetInformationJobObject(HANDLE hJob, JOBOBJECTINFOCLASS JobObjectInformationClass, LPVOID lpJobObjectInformation, DWORD cbJobObjectInformationLength);
BOOL AssignProcessToJobObject(HANDLE hJob, HANDLE hProcess);
BOOL TerminateJobObject(HANDLE hJob, UINT uExitCode);

// Security & Registry
BOOL OpenProcessToken(HANDLE ProcessHandle, DWORD DesiredAccess, HANDLE* TokenHandle);
BOOL GetTokenInformation(HANDLE TokenHandle, TOKEN_INFORMATION_CLASS TokenInformationClass, LPVOID TokenInformation, DWORD TokenInformationLength, LPDWORD ReturnLength);
LONG RegOpenKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
LONG RegCreateKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD Reserved, LPWSTR lpClass, DWORD dwOptions, REGSAM samDesired, LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, LPDWORD lpdwDisposition);
LONG RegQueryValueExW(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
LONG RegGetValueW(HKEY hkey, LPCWSTR lpSubKey, LPCWSTR lpValue, DWORD dwFlags, LPDWORD pdwType, PVOID pvData, LPDWORD pcbData);
LONG RegSetValueExW(HKEY hKey, LPCWSTR lpValueName, DWORD Reserved, DWORD dwType, const BYTE* lpData, DWORD cbData);
LONG RegDeleteValueW(HKEY hKey, LPCWSTR lpValueName);
LONG RegEnumKeyExW(HKEY hKey, DWORD dwIndex, LPWSTR lpName, LPDWORD lpcchName, LPDWORD lpReserved, LPWSTR lpClass, LPDWORD lpcchClass, FILETIME* lpftLastWriteTime);
LONG RegEnumValueW(HKEY hKey, DWORD dwIndex, LPWSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
LONG RegCloseKey(HKEY hKey);

// Windowing, Message Loop, Input, Menus, Clipboard & GDI
WORD RegisterClassExW(const WNDCLASSEXW* lpwcx);
HWND CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
BOOL DestroyWindow(HWND hWnd);
BOOL ShowWindow(HWND hWnd, int nCmdShow);
BOOL UpdateWindow(HWND hWnd);
BOOL IsWindow(HWND hWnd);
BOOL IsWindowVisible(HWND hWnd);
BOOL EnableWindow(HWND hWnd, BOOL bEnable);
BOOL SetForegroundWindow(HWND hWnd);
HWND SetFocus(HWND hWnd);
HWND GetFocus();
HWND GetParent(HWND hWnd);
HWND GetAncestor(HWND hwnd, UINT gaFlags);
HWND WindowFromPoint(POINT Point);
BOOL GetClientRect(HWND hWnd, LPRECT lpRect);
BOOL GetWindowRect(HWND hWnd, LPRECT lpRect);
BOOL ClientToScreen(HWND hWnd, LPPOINT lpPoint);
BOOL ScreenToClient(HWND hWnd, LPPOINT lpPoint);
int MapWindowPoints(HWND hWndFrom, HWND hWndTo, LPPOINT lpPoints, UINT cPoints);
BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
BOOL GetWindowPlacement(HWND hWnd, WINDOWPLACEMENT* lpwndpl);
BOOL SetWindowPlacement(HWND hWnd, const WINDOWPLACEMENT* lpwndpl);
LONG GetWindowLongW(HWND hWnd, int nIndex);
LONG SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong);
LONG_PTR GetWindowLongPtrW(HWND hWnd, int nIndex);
LONG_PTR SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR dwNewLong);
LRESULT CallWindowProcW(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
LRESULT DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
int GetWindowTextLengthW(HWND hWnd);
int GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount);
BOOL SetWindowTextW(HWND hWnd, LPCWSTR lpString);
int GetDlgCtrlID(HWND hWndCtl);
BOOL InvalidateRect(HWND hWnd, const RECT* lpRect, BOOL bErase);
BOOL ValidateRect(HWND hWnd, const RECT* lpRect);
HDC BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint);
BOOL EndPaint(HWND hWnd, const PAINTSTRUCT* lpPaint);

BOOL GetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
BOOL PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL TranslateMessage(const MSG* lpMsg);
LRESULT DispatchMessageW(const MSG* lpMsg);
BOOL IsDialogMessageW(HWND hDlg, LPMSG lpMsg);
LRESULT SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
void PostQuitMessage(int nExitCode);
UINT RegisterWindowMessageW(LPCWSTR lpString);
BOOL MessageBeep(UINT uType);
int MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);

HWND SetCapture(HWND hWnd);
BOOL ReleaseCapture();
HWND GetCapture();
BOOL GetCursorPos(LPPOINT lpPoint);
HCURSOR SetCursor(HCURSOR hCursor);
HCURSOR LoadCursorW(HINSTANCE hInstance, LPCWSTR lpCursorName);
UINT GetDoubleClickTime();
SHORT GetKeyState(int nVirtKey);
SHORT GetAsyncKeyState(int vKey);
UINT_PTR SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc);
BOOL KillTimer(HWND hWnd, UINT_PTR uIDEvent);
HMONITOR MonitorFromWindow(HWND hwnd, DWORD dwFlags);
BOOL GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi);
int GetSystemMetrics(int nIndex);
BOOL SystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni);

HMENU CreatePopupMenu();
BOOL DestroyMenu(HMENU hMenu);
BOOL AppendMenuW(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCWSTR lpNewItem);
BOOL SetMenuItemInfoW(HMENU hmenu, UINT item, BOOL fByPositon, LPCMENUITEMINFOW lpmii);
BOOL SetMenuDefaultItem(HMENU hMenu, UINT uItem, UINT fByPos);
int GetMenuItemCount(HMENU hMenu);
BOOL TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, const RECT* prcRect);

BOOL OpenClipboard(HWND hWndNewOwner);
BOOL EmptyClipboard();
BOOL CloseClipboard();
HANDLE GetClipboardData(UINT uFormat);
HANDLE SetClipboardData(UINT uFormat, HANDLE hMem);
BOOL IsClipboardFormatAvailable(UINT format);
UINT RegisterClipboardFormatW(LPCWSTR lpszFormat);

struct ICONINFO {
    BOOL fIcon;
    DWORD xHotspot;
    DWORD yHotspot;
    HBITMAP hbmMask;
    HBITMAP hbmColor;
};

DWORD GetLongPathNameW(LPCWSTR lpszShortPath, LPWSTR lpszLongPath, DWORD cchBuffer);
BOOL IsProcessDPIAware();
HANDLE LoadImageW(HINSTANCE hInst, LPCWSTR name, UINT type, int cx, int cy, UINT fuLoad);
HICON LoadIconW(HINSTANCE hInstance, LPCWSTR lpIconName);

HDC GetDC(HWND hWnd);
int ReleaseDC(HWND hWnd, HDC hDC);
HDC CreateCompatibleDC(HDC hdc);
BOOL DeleteDC(HDC hdc);
int GetDeviceCaps(HDC hdc, int index);
HGDIOBJ GetStockObject(int i);
HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h);
BOOL DeleteObject(HGDIOBJ ho);
int GetObjectW(HANDLE h, int c, LPVOID pv);
HBITMAP CreateDIBSection(HDC hdc, const BITMAPINFO* pbmi, UINT usage, VOID** ppvBits, HANDLE hSection, DWORD offset);
int GetDIBits(HDC hdc, HBITMAP hbm, UINT start, UINT cLines, LPVOID lpvBits, BITMAPINFO* lpbmi, UINT usage);
BOOL GdiFlush();
HBRUSH CreateSolidBrush(DWORD color);
HPEN CreatePen(int iStyle, int cWidth, DWORD color);
HFONT CreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName);
BOOL Rectangle(HDC hdc, int left, int top, int right, int bottom);
BOOL Ellipse(HDC hdc, int left, int top, int right, int bottom);
HDC CreateEnhMetaFileW(HDC hdc, LPCWSTR lpFilename, const RECT* lprc, LPCWSTR lpDesc);
HENHMETAFILE CloseEnhMetaFile(HDC hdc);
UINT GetEnhMetaFileBits(HENHMETAFILE hemf, UINT nSize, LPBYTE lpData);
BOOL DeleteEnhMetaFile(HENHMETAFILE hemf);
#ifndef WHITE_BRUSH
#define WHITE_BRUSH 0
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH 2
#define DKGRAY_BRUSH 3
#define BLACK_BRUSH 4
#define NULL_BRUSH 5
#define WHITE_PEN 6
#define BLACK_PEN 7
#define NULL_PEN 8
#endif

struct RECTL { LONG left; LONG top; LONG right; LONG bottom; };
struct SIZEL { LONG cx; LONG cy; };

struct ENHMETAHEADER {
    DWORD iType;
    DWORD nSize;
    RECTL rclBounds;
    RECTL rclFrame;
    DWORD dSignature;
    DWORD nVersion;
    DWORD nBytes;
    DWORD nRecords;
    WORD  nHandles;
    WORD  sReserved;
    DWORD nDescription;
    DWORD offDescription;
    DWORD nPalEntries;
    SIZEL szlDevice;
    SIZEL szlMillimeters;
    DWORD cbPixelFormat;
    DWORD offPixelFormat;
    DWORD bOpenGL;
    SIZEL szlMicrometers;
};

BOOL FillRect(HDC hDC, const RECT* lprc, HBRUSH hbr);
HENHMETAFILE SetEnhMetaFileBits(UINT nSize, const BYTE* pb);
HENHMETAFILE SetWinMetaFileBits(UINT nSize, const BYTE* lpMeta16Data, HDC hdcRef, const void* lpmfp);
UINT GetEnhMetaFileHeader(HENHMETAFILE hemf, UINT nSize, ENHMETAHEADER* lpEnhMetaHeader);
BOOL PlayEnhMetaFile(HDC hdc, HENHMETAFILE hmf, const RECT* lprect);
BOOL DestroyIcon(HICON hIcon);
BOOL GetIconInfo(HICON hIcon, ICONINFO* piconinfo);
BOOL DrawIconEx(HDC hdc, int xLeft, int yTop, HICON hIcon, int cxWidth, int cyWidth, UINT istepIfAniCur, HBRUSH hbrFlickerFreeDraw, UINT diFlags);

using PDWORD = DWORD*;
