#include "ExplorerApp.h"
#include "CommandIds.h"
#include "PathUtils.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <winnetwk.h>
#include <fstream>
#include <unordered_set>
#include <memory>
#include <thread>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "user32.lib")

namespace Explorer2 {

constexpr int ID_EDIT_ADDRESS = 1001;
constexpr int ID_EDIT_SEARCH  = 1002;
constexpr int ID_EDIT_RENAME  = 1003;
constexpr UINT_PTR ID_TIMER_MEDIA     = 2001;
constexpr UINT_PTR ID_TIMER_WORK_ANIM = 2002;
constexpr UINT_PTR ID_TIMER_MONITOR   = 2003;   // slow refresh of the Monitoring history view
constexpr UINT_PTR ID_TIMER_GIT       = 2004;   // periodic git status refresh while a repository is shown

static WNDPROC g_origEditProc = nullptr;

// Result of a background rename (heap-allocated by the worker, owned by the WM_APP_RENAME_DONE handler)
struct RenameJobResult {
    std::wstring oldPath, newPath, oldName, newName;
    DWORD error = 0;
    bool ok = false;         // the OS call succeeded
    bool verified = false;   // the new name really exists afterwards (and the old one is gone)
};

static std::wstring SystemErrorText(DWORD err) {
    wchar_t buf[256] = {};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, 0, buf, 255, nullptr);
    std::wstring s = buf;
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
    return s + L" (" + std::to_wstring(err) + L")";
}

// Empty string = valid Windows file name, otherwise the reason
static std::wstring ValidateFileName(const std::wstring& n, bool pl) {
    if (n.empty()) return pl ? L"Nazwa nie może być pusta." : L"The name cannot be empty.";
    if (n.size() > 255) return pl ? L"Nazwa jest za długa (maks. 255 znaków)." : L"The name is too long (255 characters max).";
    for (wchar_t c : n) {
        if (c < 32 || wcschr(L"<>:\"/\\|?*", c)) {
            return pl ? L"Nazwa nie może zawierać znaków:  \\ / : * ? \" < > |" : L"A name cannot contain any of these characters:  \\ / : * ? \" < > |";
        }
    }
    if (n.back() == L'.' || n.back() == L' ') return pl ? L"Nazwa nie może kończyć się kropką ani spacją." : L"The name cannot end with a dot or a space.";
    std::wstring stem = n.substr(0, n.find(L'.'));
    while (!stem.empty() && stem.back() == L' ') stem.pop_back();
    static const wchar_t* kReserved[] = { L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
                                          L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9" };
    for (const wchar_t* r : kReserved) {
        if (_wcsicmp(stem.c_str(), r) == 0) return pl ? L"To zastrzeżona nazwa systemu Windows." : L"This is a reserved Windows device name.";
    }
    return L"";
}

static std::wstring DescribePaths(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return L"";
    std::wstring s = L"\"" + PathUtils::GetFileName(paths[0]) + L"\"";
    if (paths.size() > 1) s += L" (+" + std::to_wstring(paths.size() - 1) + L" more)";
    return s + L"  in  " + PathUtils::GetParentPath(paths[0]);
}
static ExplorerApp* g_appInstance = nullptr;
constexpr WORD kEditCancelCode = 0x0FF0;   // sent by InlineEditSubclassProc when the user presses Escape

static LRESULT CALLBACK InlineEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), EN_MAXTEXT), reinterpret_cast<LPARAM>(hwnd));
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), kEditCancelCode), reinterpret_cast<LPARAM>(hwnd));
            return 0;
        }
    } else if (msg == WM_CHAR && (wParam == L'\r' || wParam == L'\n' || wParam == 27)) {
        return 0;
    }
    return CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
}

ExplorerApp::ExplorerApp() {
    g_appInstance = this;
}

ExplorerApp::~ExplorerApp() {
    AppLog::Info(L"App", L"Explorer2 shutting down");
    m_contentPane.Shutdown();
    m_mediaPlayer.Shutdown();
    m_imageCache.Shutdown();
    m_fileEngine.Shutdown();
    DiscardDeviceResources();

    if (m_fmtRegular) m_fmtRegular->Release();
    if (m_fmtSmall) m_fmtSmall->Release();
    if (m_fmtBold) m_fmtBold->Release();
    if (m_fmtCenterSmall) m_fmtCenterSmall->Release();
    if (m_fmtViewerHud) m_fmtViewerHud->Release();
    if (m_dwriteFactory) m_dwriteFactory->Release();
    if (m_d2dFactory) m_d2dFactory->Release();
    if (m_editFont) DeleteObject(m_editFont);
}

bool ExplorerApp::Initialize(HINSTANCE hInstance, int nCmdShow, const std::wstring& initialPath) {
    m_hInstance = hInstance;

    AppLog::Init();
    {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        AppLog::Info(L"App", std::wstring(L"Explorer2 started  (pid ") + std::to_wstring(GetCurrentProcessId()) + L")  exe: " + exe);
        AppLog::Info(L"App", std::wstring(L"Start folder: ") + (initialPath.empty() ? L"(default)" : initialPath) + L"   log file: " + AppLog::FilePath());
    }

    if (!CreateDeviceIndependentResources()) {
        return false;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = ExplorerApp::WndProcStatic;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEW(4));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"Explorer2MainWndClass";
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"Explorer2",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1240, 760,
        nullptr, nullptr, hInstance, this);

    if (!m_hwnd) { AppLog::Error(L"App", L"Could not create the main window"); return false; }
    AppLog::SetNotify(m_hwnd, WM_APP_LOG_ADDED);

    m_editFont = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    m_addressEdit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        0, 0, 100, 24,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDIT_ADDRESS)), hInstance, nullptr);
    SendMessageW(m_addressEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_editFont), TRUE);
    g_origEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        m_addressEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(InlineEditSubclassProc)));
    // Folder-path suggestions (drop-down list like Windows Explorer's address bar)
    SHAutoComplete(m_addressEdit, SHACF_FILESYS_DIRS | SHACF_AUTOSUGGEST_FORCE_ON | SHACF_AUTOAPPEND_FORCE_OFF);

    m_searchEdit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        0, 0, 100, 24,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDIT_SEARCH)), hInstance, nullptr);
    SendMessageW(m_searchEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_editFont), TRUE);
    SetWindowLongPtrW(m_searchEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(InlineEditSubclassProc));

    m_renameEdit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        0, 0, 100, 22,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDIT_RENAME)), hInstance, nullptr);
    SendMessageW(m_renameEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_editFont), TRUE);
    SetWindowLongPtrW(m_renameEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(InlineEditSubclassProc));

    m_fileEngine.Initialize(m_hwnd);
    m_imageCache.Initialize(m_hwnd);
    m_shellEmulator.InitializeAsync();
    m_mediaPlayer.Initialize(m_hwnd);
    m_contentPane.Initialize(m_hwnd, hInstance, m_d2dFactory, m_dwriteFactory);

    m_pinnedFolders = FileEngine::GetDefaultQuickAccessFolders(m_lang);
    RebuildTreeNodes();

    std::wstring startDir = initialPath;
    if (startDir.empty()) {
        if (GetFileAttributesW(L"W:\\Fooocus_Advanced") != INVALID_FILE_ATTRIBUTES) {
            startDir = L"W:\\Fooocus_Advanced";
        } else {
            wchar_t cwd[MAX_PATH] = {};
            GetCurrentDirectoryW(MAX_PATH, cwd);
            startDir = cwd;
        }
    }

    LoadUiSettings();
    NavigateTo(startDir, true);

    // If the user enabled background recording earlier, make sure the single shared recorder is alive
    // (e.g. after a reboot without autostart). Any window can revive it; only one instance ever runs.
    // An administrator recorder is never started from here (that would pop a UAC prompt at every launch).
    {
        MonitorSettings ms = MonitorStore::LoadSettings();
        if (ms.enabled && !ms.elevated && !MonitorDaemon::IsRunning()) {
            MonitorDaemon::Start(false);
        }
    }

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    return true;
}

int ExplorerApp::RunMessageLoop() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

bool ExplorerApp::CreateDeviceIndependentResources() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2dFactory);
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&m_dwriteFactory));
    if (FAILED(hr)) return false;

    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.0f, L"pl-PL", &m_fmtRegular);
    if (m_fmtRegular) {
        m_fmtRegular->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_fmtRegular->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* ellipsis = nullptr;
        m_dwriteFactory->CreateEllipsisTrimmingSign(m_fmtRegular, &ellipsis);
        m_fmtRegular->SetTrimming(&trim, ellipsis);
        if (ellipsis) ellipsis->Release();
    }

    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.0f, L"pl-PL", &m_fmtSmall);
    if (m_fmtSmall) {
        m_fmtSmall->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_fmtSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.0f, L"pl-PL", &m_fmtBold);
    if (m_fmtBold) {
        m_fmtBold->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_fmtBold->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.5f, L"pl-PL", &m_fmtCenterSmall);
    if (m_fmtCenterSmall) {
        m_fmtCenterSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fmtCenterSmall->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* ellipsis = nullptr;
        m_dwriteFactory->CreateEllipsisTrimmingSign(m_fmtCenterSmall, &ellipsis);
        m_fmtCenterSmall->SetTrimming(&trim, ellipsis);
        if (ellipsis) ellipsis->Release();
    }

    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f, L"pl-PL", &m_fmtViewerHud);
    if (m_fmtViewerHud) {
        m_fmtViewerHud->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_fmtViewerHud->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    return true;
}

bool ExplorerApp::CreateDeviceResources() {
    if (m_rt) return true;

    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    rtProps.dpiX = 96.0f;
    rtProps.dpiY = 96.0f;

    HRESULT hr = m_d2dFactory->CreateHwndRenderTarget(
        rtProps,
        D2D1::HwndRenderTargetProperties(m_hwnd, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        &m_rt);
    if (FAILED(hr)) return false;

    m_rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &m_solidBrush);
    return true;
}

void ExplorerApp::DiscardDeviceResources() {
    m_mediaPlayer.DiscardDeviceResources();
    m_imageCache.DiscardDeviceResources();
    if (m_solidBrush) {
        m_solidBrush->Release();
        m_solidBrush = nullptr;
    }
    if (m_rt) {
        m_rt->Release();
        m_rt = nullptr;
    }
}

void ExplorerApp::OnResize(UINT width, UINT height) {
    if (m_rt) {
        m_rt->Resize(D2D1::SizeU(width, height));
    }
    ClampScrollOffset();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

LRESULT CALLBACK ExplorerApp::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ExplorerApp* app = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<ExplorerApp*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->m_hwnd = hwnd;
    } else {
        app = reinterpret_cast<ExplorerApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (app) {
        return app->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT ExplorerApp::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        OnResize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_TIMER:
        if (wParam == ID_TIMER_MEDIA && m_mediaPlayer.HasMedia() && m_mediaPlayer.IsPlaying()) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (wParam == ID_TIMER_WORK_ANIM) {
            if (m_viewMode == ViewMode::Hardware && !IsProgramsView() && !IsMonitoringView() && !IsLanView() && !IsLogView()) {
                m_hwInspector.ApplyStagedData();
                if (m_hwLiveRefresh) {
                    ULONGLONG now = GetTickCount64();
                    if (now - m_lastHwLiveUpdateTick >= 450) {
                        m_lastHwLiveUpdateTick = now;
                        m_hwInspector.UpdateLiveTelemetry();
                    }
                }
            }
            if (PreviewPaneVisible()) {
                m_contentPane.StepAnimation();
            }
            // A pull / branch switch changed the working tree: reload the listing once git is done
            if (m_gitReloadFolderWhenIdle && !m_git.IsBusy()) {
                m_gitReloadFolderWhenIdle = false;
                RefreshCurrentFolder();
            }
            // Log the end of background jobs the UI only polls
            {
                const bool gitBusy = m_git.IsBusy();
                if (m_gitWasBusy && !gitBusy && m_gitActionLogPending) {
                    m_gitActionLogPending = false;
                    std::wstring out = m_git.SnapshotOutput();
                    while (!out.empty() && (out.back() == L'\n' || out.back() == L'\r' || out.back() == L' ')) out.pop_back();
                    size_t nl = out.find_last_of(L'\n');
                    std::wstring lastLine = (nl == std::wstring::npos) ? out : out.substr(nl + 1);
                    if (m_git.LastActionSucceeded()) AppLog::Ok(L"Git", L"Action finished" + (lastLine.empty() ? std::wstring() : L": " + lastLine));
                    else AppLog::Error(L"Git", L"Action failed" + (lastLine.empty() ? std::wstring() : L": " + lastLine));
                }
                m_gitWasBusy = gitBusy;
                const bool lanScan = m_lan.IsScanning();
                if (m_lanWasScanning && !lanScan) {
                    AppLog::Ok(L"LAN", L"Network scan finished: " + std::to_wstring(m_lan.SnapshotHosts().size()) + L" host(s), " +
                                       std::to_wstring(m_lan.SnapshotSsdp().size()) + L" SSDP device(s)");
                }
                m_lanWasScanning = lanScan;
            }
            EnsureWorkAnimTimerState();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (wParam == ID_TIMER_GIT) {
            if (GitDockVisible() && !m_git.IsBusy()) SyncGitRepo(true);
        } else if (wParam == ID_TIMER_MONITOR) {
            if (IsMonitoringView()) {
                // Long ranges are re-parsed less often (the recorder only appends one row per interval)
                if (m_monRangeIdx < 3 || GetTickCount64() - m_monLoadTick > 30000) m_monForceReload = true;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            } else {
                KillTimer(m_hwnd, ID_TIMER_MONITOR);
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (m_renameEdit && IsWindowVisible(m_renameEdit)) {
            CommitAddressOrRenameEdit(m_renameEdit);
        }
        SetFocus(m_hwnd);
        CancelInlineEdits(false);
        OnMouseDown(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)), false, wParam);
        return 0;

    case WM_RBUTTONDOWN:
        if (m_renameEdit && IsWindowVisible(m_renameEdit)) {
            CommitAddressOrRenameEdit(m_renameEdit);
        }
        SetFocus(m_hwnd);
        CancelInlineEdits(false);
        OnMouseDown(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)), true, wParam);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt = {};
            GetCursorPos(&pt);
            ScreenToClient(m_hwnd, &pt);
            if (m_draggingTreeSplitter || m_draggingGitSplitter || m_draggingSplitSplitter || m_draggingPreviewSplitter || m_resizingCol >= 0 ||
                HitResizeHandle(static_cast<float>(pt.x), static_cast<float>(pt.y)) != 0) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;

    case WM_MOUSEMOVE:
        OnMouseMove(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)), wParam);
        return 0;

    case WM_LBUTTONUP:
        OnMouseUp(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)), false, wParam);
        return 0;

    case WM_RBUTTONUP:
        OnMouseUp(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)), true, wParam);
        return 0;

    case WM_LBUTTONDBLCLK:
        OnMouseDoubleClick(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)), wParam);
        return 0;

    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(m_hwnd, &pt);
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), static_cast<float>(pt.x), static_cast<float>(pt.y), GET_KEYSTATE_WPARAM(wParam));
        return 0;
    }

    case WM_XBUTTONDOWN:
        if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) {
            NavigateBack();
        } else if (GET_XBUTTON_WPARAM(wParam) == XBUTTON2) {
            NavigateForward();
        }
        return TRUE;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        OnKeyDown(wParam);
        return 0;

    case WM_CHAR:
        OnChar(static_cast<wchar_t>(wParam));
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == ID_EDIT_SEARCH && code == EN_CHANGE) {
            wchar_t buf[256] = {};
            GetWindowTextW(m_searchEdit, buf, 256);
            if (IsProgramsView()) {
                // On the Programy tab the search box filters the program list instead of searching files
                m_progFilter = buf;
                m_hwScrollY = 0.0f;
            } else {
                m_filterText = buf;
                TriggerAsyncSearch();
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if ((id == ID_EDIT_ADDRESS || id == ID_EDIT_RENAME || id == ID_EDIT_SEARCH) && code == EN_MAXTEXT) {
            CommitAddressOrRenameEdit(reinterpret_cast<HWND>(lParam));
        } else if ((id == ID_EDIT_ADDRESS || id == ID_EDIT_RENAME || id == ID_EDIT_SEARCH) && code == kEditCancelCode) {
            CancelInlineEdits(true);
        } else if (id == ID_EDIT_RENAME && code == EN_KILLFOCUS) {
            if (!m_renamingPath.empty() || m_renamingIndex >= 0 || m_editingSearchFieldCmd != 0) {
                CommitAddressOrRenameEdit(m_renameEdit);
            } else {
                CancelInlineEdits(false);
            }
        } else if (id == ID_EDIT_ADDRESS && code == EN_KILLFOCUS) {
            CancelInlineEdits(false);
        } else if (m_contentPane.GetNotepadEditHwnd() && reinterpret_cast<HWND>(lParam) == m_contentPane.GetNotepadEditHwnd() && code == EN_CHANGE) {
            m_contentPane.OnNotepadEditChanged();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_APP_THUMB_READY:
    case WM_APP_FULLIMG_READY:
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_APP_IMAGE_ROTATED: {
        m_imageCache.TakeCompletedRotations();
        auto transforms = m_imageCache.TakeCompletedTransforms();
        if (!transforms.empty()) {
            const bool pl = (m_lang == Lang::PL);
            int okCount = 0;
            int failCount = 0;
            ImageTransformResult lastOk = {};
            ImageTransformResult lastFail = {};
            for (const auto& tr : transforms) {
                if (tr.ok) {
                    ++okCount;
                    lastOk = tr;
                    wchar_t logBuf[512] = {};
                    swprintf_s(logBuf, L"%s -> %s (%ux%u px -> %ux%u px, %s -> %s)",
                        PathUtils::GetFileName(tr.srcPath).c_str(),
                        PathUtils::GetFileName(tr.outPath).c_str(),
                        tr.origW, tr.origH, tr.newW, tr.newH,
                        FileEngine::FormatSizeDetailed(tr.origBytes).c_str(),
                        FileEngine::FormatSizeDetailed(tr.newBytes).c_str());
                    AppLog::Ok(L"ImageTools", logBuf);
                } else {
                    ++failCount;
                    lastFail = tr;
                    AppLog::Error(L"ImageTools", PathUtils::GetFileName(tr.srcPath) + L": " + tr.errorMsg);
                }
            }
            if (okCount > 0) {
                wchar_t msg[512] = {};
                std::wstring outName = PathUtils::GetFileName(lastOk.outPath);
                std::wstring oldSz = FileEngine::FormatSizeDetailed(lastOk.origBytes);
                std::wstring newSz = FileEngine::FormatSizeDetailed(lastOk.newBytes);
                if (okCount == 1 && !m_imageCache.IsAnyImageTransforming()) {
                    swprintf_s(msg, pl
                        ? L"\x2713 Obraz gotowy: %s  (%u\xD7%u px \x2192 %u\xD7%u px,  %s \x2192 %s)"
                        : L"\x2713 Image ready: %s  (%u\xD7%u px \x2192 %u\xD7%u px,  %s \x2192 %s)",
                        outName.c_str(), lastOk.origW, lastOk.origH, lastOk.newW, lastOk.newH, oldSz.c_str(), newSz.c_str());
                } else {
                    swprintf_s(msg, pl
                        ? L"\x2713 Przetworzono: %s  (%u\xD7%u px, %s)"
                        : L"\x2713 Processed: %s  (%u\xD7%u px, %s)",
                        outName.c_str(), lastOk.newW, lastOk.newH, newSz.c_str());
                }
                m_statusToast = msg;
                m_statusToastExpireTick = GetTickCount64() + 5500;
                if (!m_imageCache.IsAnyImageTransforming() &&
                    PathUtils::NormalizePathKey(PathUtils::GetParentPath(lastOk.srcPath)) == m_currentNormKey) {
                    RefreshCurrentFolder(m_viewMode == ViewMode::FullViewer ? lastOk.srcPath : lastOk.outPath);
                }
            } else if (failCount > 0) {
                m_statusToast = (pl ? L"\x2717 Błąd przetwarzania obrazu: " : L"\x2717 Image processing error: ") +
                                PathUtils::GetFileName(lastFail.srcPath) + L" (" + lastFail.errorMsg + L")";
                m_statusToastExpireTick = GetTickCount64() + 6000;
            }
        }
        EnsureWorkAnimTimerState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_APP_FOLDER_SIZE_UPDATED:
        if (m_currentPath == L"THIS_PC") {
            RebuildTreeNodes();
            RefreshCurrentFolder();
        } else {
            SyncFolderSizesFromCache();
            EnsureWorkAnimTimerState();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_APP_SEARCH_UPDATED:
        if (m_isSearchActive) {
            m_fileEngine.SyncSearchResults(m_items, m_searchProgress);
            FileEngine::SortEntries(m_items, m_sortCol, m_sortAsc);
            ClampScrollOffset();
            EnsureWorkAnimTimerState();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_APP_LOG_ADDED:
        if (IsLogView()) InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_APP_RENAME_DONE: {
        std::unique_ptr<RenameJobResult> r(reinterpret_cast<RenameJobResult*>(lParam));
        if (!r) return 0;
        if (m_renamePending.load() > 0) --m_renamePending;
        if (m_renamePending.load() == 0) m_renamePendingText.clear();
        const bool pl = (m_lang == Lang::PL);
        const std::wstring arrow = r->oldName + L"  \x2192  " + r->newName;
        if (r->ok && r->verified) {
            m_statusToast = (pl ? L"\x2713 Zmieniono nazwę: " : L"\x2713 Renamed: ") + arrow;
            m_statusToastExpireTick = GetTickCount64() + 5000;
            AppLog::Ok(L"Rename", L"Renamed and verified: " + r->oldPath + L"  ->  " + r->newPath);
        } else if (r->ok) {
            m_statusToast = (pl ? L"\x26A0 Zmiana nazwy zgłoszona jako udana, ale nie udało się jej potwierdzić: " : L"\x26A0 Rename reported success but could not be verified: ") + arrow;
            m_statusToastExpireTick = GetTickCount64() + 6000;
            AppLog::Warn(L"Rename", L"Reported success but the new name was not found afterwards: " + r->oldPath + L"  ->  " + r->newPath);
        } else {
            ShowRenameFailure(r->oldName, r->newName, SystemErrorText(r->error), r->error);
        }
        // Reload only when the user is still looking at the folder that contains the item
        if (PathUtils::NormalizePathKey(PathUtils::GetParentPath(r->oldPath)) == m_currentNormKey) {
            RefreshCurrentFolder(r->ok ? r->newPath : r->oldPath);
        }
        EnsureWorkAnimTimerState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_APP_MERGE_DONE: {
        std::unique_ptr<MergeFolderResult> r(reinterpret_cast<MergeFolderResult*>(lParam));
        if (!r) return 0;
        m_fileEngine.RemovePendingHiddenPath(r->srcDir);
        if (m_mergePending.load() > 0) --m_mergePending;
        if (m_mergePending.load() == 0) {
            m_mergePendingText.clear();
            m_mergeMovedCount = 0;
        }
        const bool pl = (m_lang == Lang::PL);
        const std::wstring arrow = r->srcName + L"  \x2192  " + r->dstName;
        for (const auto& note : r->collisionNotes) {
            AppLog::Warn(L"Merge", L"Duplicate file kept under new name: " + note);
        }
        if (r->failures.empty() && r->srcRemoved) {
            std::wstring stats = (pl ? L" (przeniesiono " : L" (moved ") + std::to_wstring(r->movedItems) + (pl ? L" el." : L" item(s)");
            if (r->renamedCollisions > 0) {
                stats += (pl ? L", zachowano duplikatów: " : L", duplicates kept: ") + std::to_wstring(r->renamedCollisions);
            }
            stats += L")";
            m_statusToast = (pl ? L"\x2713 Scalono foldery: " : L"\x2713 Folders merged: ") + arrow + stats;
            m_statusToastExpireTick = GetTickCount64() + 6500;
            AppLog::Ok(L"Merge", L"Merged and verified (source folder removed): " + r->srcDir + L"  ->  " + r->dstDir + stats);
        } else if (r->failures.empty() && !r->srcRemoved) {
            m_statusToast = (pl ? L"\x26A0 Scalono pliki, ale folder źródłowy nadal istnieje: " : L"\x26A0 Files merged, but source folder still exists: ") + arrow;
            m_statusToastExpireTick = GetTickCount64() + 7500;
            AppLog::Warn(L"Merge", L"Moved items but source folder still exists: " + r->srcDir);
        } else {
            const auto& f0 = r->failures.front();
            std::wstring errTxt = f0.error ? SystemErrorText(f0.error) : std::wstring(pl ? L"błąd dostępu" : L"access error");
            m_statusToast = (pl ? L"\x2717 Błąd scalania: " : L"\x2717 Merge error: ") + arrow + L"  \x2014  " +
                            PathUtils::GetFileName(f0.path) + L": " + errTxt;
            m_statusToastExpireTick = GetTickCount64() + 9000;
            for (const auto& f : r->failures) {
                AppLog::Error(L"Merge", L"Failed on " + f.path + L"  ->  " + (f.error ? SystemErrorText(f.error) : std::wstring(L"unknown error")));
            }
        }
        if (PathUtils::NormalizePathKey(PathUtils::GetParentPath(r->srcDir)) == m_currentNormKey) {
            RefreshCurrentFolder(r->dstDir);
        }
        EnsureWorkAnimTimerState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_APP_DELETE_DONE: {
        const Strings& s = GetStrings(m_lang);
        m_statusToast = (wParam != 0) ? s.statusDeletedPerm : s.statusDeletedRecycle;
        m_statusToastExpireTick = GetTickCount64() + 3500;

        // Items the worker could not remove: tell the user why instead of letting them silently reappear
        std::vector<DeleteFailure> failures = m_fileEngine.TakeDeleteFailures();
        if (failures.empty()) {
            AppLog::Ok(L"Delete", wParam != 0 ? L"Permanent delete finished" : L"Move to Recycle Bin finished");
        } else {
            AppLog::Warn(L"Delete", std::to_wstring(failures.size()) + L" item(s) could not be deleted");
            for (const auto& f : failures) AppLog::Error(L"Delete", f.path + L"  ->  " + (f.error ? SystemErrorText(f.error) : std::wstring(L"unknown reason")));
        }
        if (!failures.empty()) {
            const bool pl = (m_lang == Lang::PL);
            std::wstring msg = pl ? (std::to_wstring(failures.size()) + L" element(ów) nie udało się usunąć:\n\n")
                                  : (std::to_wstring(failures.size()) + L" item(s) could not be deleted:\n\n");
            bool sawLockOrAccess = false;
            size_t shown = 0;
            for (const auto& f : failures) {
                if (shown++ >= 6) break;
                std::wstring reason;
                if (f.error != 0) {
                    wchar_t buf[256] = {};
                    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, f.error, 0, buf, 255, nullptr);
                    reason = buf;
                    while (!reason.empty() && (reason.back() == L'\r' || reason.back() == L'\n' || reason.back() == L' ')) reason.pop_back();
                    reason += L" (" + std::to_wstring(f.error) + L")";
                    if (f.error == ERROR_SHARING_VIOLATION || f.error == ERROR_ACCESS_DENIED || f.error == ERROR_LOCK_VIOLATION) sawLockOrAccess = true;
                } else {
                    reason = pl ? L"nieznany powód" : L"unknown reason";
                }
                msg += f.path + L"\n    \x2192 " + reason + L"\n";
            }
            if (failures.size() > 6) msg += L"...\n";
            if (sawLockOrAccess) {
                msg += pl ? L"\nPlik jest prawdopodobnie używany przez inny program (maszyna wirtualna, Unity, indeksowanie, antywirus) albo wymaga uprawnień administratora."
                          : L"\nThe file is probably in use by another program (VM, Unity, indexer, antivirus) or needs administrator rights.";
            }
            MessageBoxW(m_hwnd, msg.c_str(), pl ? L"Nie wszystko zostało usunięte" : L"Not everything was deleted", MB_OK | MB_ICONWARNING);
            RefreshCurrentFolder();   // show what is really still there
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        EnsureWorkAnimTimerState();
        return 0;
    }

    case WM_DESTROY:
        KillTimer(m_hwnd, ID_TIMER_MEDIA);
        KillTimer(m_hwnd, ID_TIMER_WORK_ANIM);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void ExplorerApp::SyncMediaPlayerToSelection() {
    const FileEntry* selEntry = (m_focusedIndex >= 0 && m_focusedIndex < static_cast<int>(m_items.size()))
        ? &m_items[m_focusedIndex]
        : nullptr;
    m_contentPane.SyncToSelection(selEntry, PreviewPaneVisible());
    EnsureWorkAnimTimerState();

    bool inMediaViewer = (m_viewMode == ViewMode::FullViewer || PreviewPaneVisible());
    if (inMediaViewer && m_focusedIndex >= 0 && m_focusedIndex < static_cast<int>(m_items.size())) {
        const FileEntry& item = m_items[m_focusedIndex];
        if (item.isVideo || item.isAudio) {
            if (m_mediaPlayer.GetCurrentPath() != item.fullPath) {
                m_mediaPlayer.OpenAndPlay(item.fullPath);
                SetTimer(m_hwnd, ID_TIMER_MEDIA, 33, nullptr);
            }
            return;
        }
    }
    if (m_mediaPlayer.HasMedia()) {
        m_mediaPlayer.CloseMedia();
        KillTimer(m_hwnd, ID_TIMER_MEDIA);
    }
}

void ExplorerApp::NavigateTo(const std::wstring& path, bool addToHistory, const std::wstring& selectFileName) {
    std::wstring cleanPath = PathUtils::TrimTrailingSlashes(path);

    if (cleanPath != L"THIS_PC") {
        std::wstring offlineName;
        if (m_fileEngine.IsDriveOffline(cleanPath, m_lang, &offlineName)) {
            m_statusToast = (m_lang == Lang::PL ? L"Dysk jest niedostępny (offline): " : L"Drive is unavailable (offline): ") + offlineName;
            m_statusToastExpireTick = GetTickCount64() + 4000;
            AppLog::Warn(L"Navigate", L"Drive unavailable (offline): " + cleanPath);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        DWORD attrs = GetFileAttributesW(cleanPath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            AppLog::Warn(L"Navigate", L"Folder not found or not a folder: " + cleanPath);
            return;
        }
    }

    m_mediaPlayer.CloseMedia();
    KillTimer(m_hwnd, ID_TIMER_MEDIA);

    // Save current folder's scroll offset and focused item before leaving
    const std::wstring prevPath = m_currentPath;
    if (!m_currentNormKey.empty() && !m_isSearchActive) {
        FolderViewState& saved = m_folderViewStates[m_currentNormKey];
        saved.scrollY = m_scrollY;
        saved.filmstripScrollX = m_filmstripScrollX;
        if (m_focusedIndex >= 0 && m_focusedIndex < static_cast<int>(m_items.size())) {
            saved.focusedPath = m_items[m_focusedIndex].fullPath;
        }
    }

    // If leaving a subfolder back to its parent/ancestor (via Back, Up, or Breadcrumb), remember the subfolder we came from
    std::wstring effectiveSelectName = selectFileName;
    if (effectiveSelectName.empty() && !prevPath.empty() && prevPath != L"THIS_PC") {
        if (cleanPath == L"THIS_PC" && prevPath.size() >= 2 && prevPath[1] == L':') {
            effectiveSelectName = prevPath.substr(0, 2) + L"\\";
        } else if (cleanPath != L"THIS_PC") {
            std::wstring cleanPrefix = PathUtils::EnsureTrailingSlash(cleanPath);
            if (prevPath.size() > cleanPrefix.size() &&
                _wcsnicmp(prevPath.c_str(), cleanPrefix.c_str(), cleanPrefix.size()) == 0) {
                std::wstring rel = prevPath.substr(cleanPrefix.size());
                size_t slash = rel.find_first_of(L"\\/");
                effectiveSelectName = (slash == std::wstring::npos) ? rel : rel.substr(0, slash);
            }
        }
    }

    m_currentPath = cleanPath;
    m_currentNormKey = (cleanPath == L"THIS_PC") ? L"__HEADER_PC__" : PathUtils::NormalizePathKey(cleanPath);
    RecordVisitedDirectory(m_currentPath);

    if (m_viewMode == ViewMode::Hardware) {
        m_viewMode = m_lastNonViewerMode;
        if (m_activeRibbonTab == RibbonTab::Hardware ||
            m_activeRibbonTab == RibbonTab::Programs ||
            m_activeRibbonTab == RibbonTab::Monitoring ||
            m_activeRibbonTab == RibbonTab::Lan ||
            m_activeRibbonTab == RibbonTab::Log) {
            m_activeRibbonTab = RibbonTab::Home;
        }
    }

    if (addToHistory) {
        if (m_historyIndex + 1 < static_cast<int>(m_history.size())) {
            m_history.erase(m_history.begin() + m_historyIndex + 1, m_history.end());
        }
        if (m_history.empty() || m_history.back() != m_currentPath) {
            m_history.push_back(m_currentPath);
            m_historyIndex = static_cast<int>(m_history.size()) - 1;
        }
    }

    m_filterText.clear();
    m_fileEngine.CancelAsyncSearch();
    m_isSearchActive = false;
    m_searchProgress = {};
    m_editingSearchFieldCmd = 0;
    if (m_searchEdit && IsWindowVisible(m_searchEdit)) {
        SetWindowTextW(m_searchEdit, L"");
        ShowWindow(m_searchEdit, SW_HIDE);
    }

    m_imageCache.BumpViewportGeneration();
    m_allItems = m_fileEngine.EnumerateDirectory(m_currentPath, m_showHidden, m_showExtensions, m_lang);
    ApplyFilterAndSort();
    RebuildBreadcrumbs();
    SyncGitRepo();

    bool restoredScroll = false;
    auto stateIt = m_folderViewStates.find(m_currentNormKey);
    if (stateIt != m_folderViewStates.end()) {
        m_scrollY = stateIt->second.scrollY;
        m_filmstripScrollX = stateIt->second.filmstripScrollX;
        ClampScrollOffset();
        restoredScroll = true;
        if (effectiveSelectName.empty() && !stateIt->second.focusedPath.empty()) {
            effectiveSelectName = stateIt->second.focusedPath;
        }
    } else {
        m_scrollY = 0.0f;
        m_filmstripScrollX = 0.0f;
    }
    ResetViewerZoom();

    std::wstring folderTitle = (m_currentPath == L"THIS_PC")
        ? GetStrings(m_lang).navThisPC
        : PathUtils::GetFileName(m_currentPath);
    SetWindowTextW(m_hwnd, folderTitle.c_str());

    m_focusedIndex = -1;
    m_anchorIndex = -1;
    if (!effectiveSelectName.empty()) {
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            if (_wcsicmp(m_items[i].name.c_str(), effectiveSelectName.c_str()) == 0 ||
                _wcsicmp(m_items[i].fullPath.c_str(), effectiveSelectName.c_str()) == 0) {
                SelectSingle(i, true);
                if (restoredScroll) {
                    D2D1_RECT_F contentRect = GetContentItemsRect();
                    D2D1_RECT_F itemRect = GetItemRect(i, contentRect);
                    if (itemRect.top < contentRect.top || itemRect.bottom > contentRect.bottom) {
                        EnsureItemVisible(i);
                    }
                } else {
                    EnsureItemVisible(i);
                }
                break;
            }
        }
    } else if (m_viewMode == ViewMode::FullViewer && !m_items.empty()) {
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            if (m_items[i].isImage || m_items[i].isVideo || m_items[i].isAudio) {
                SelectSingle(i, true);
                break;
            }
        }
        if (m_focusedIndex < 0) SelectSingle(0, true);
    }

    if (m_viewMode == ViewMode::Hardware) {
        m_viewMode = m_lastNonViewerMode;
        if (m_activeRibbonTab == RibbonTab::Hardware || m_activeRibbonTab == RibbonTab::Programs ||
            m_activeRibbonTab == RibbonTab::Monitoring || m_activeRibbonTab == RibbonTab::Lan ||
            m_activeRibbonTab == RibbonTab::Log) {
            m_activeRibbonTab = RibbonTab::Home;
        }
    }

    if ((m_viewMode == ViewMode::Proportions || m_viewMode == ViewMode::PieChart) && m_currentPath != L"THIS_PC") {
        m_fileEngine.RequestFolderSizes(m_items, false);
        EnsureWorkAnimTimerState();
    }

    SyncMediaPlayerToSelection();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::NavigateBack() {
    if (m_historyIndex > 0) {
        m_historyIndex--;
        NavigateTo(m_history[m_historyIndex], false);
    }
}

void ExplorerApp::NavigateForward() {
    if (m_historyIndex + 1 < static_cast<int>(m_history.size())) {
        m_historyIndex++;
        NavigateTo(m_history[m_historyIndex], false);
    }
}

void ExplorerApp::NavigateUp() {
    if (m_currentPath.empty() || m_currentPath == L"THIS_PC") return;
    if (m_currentPath.size() <= 3 && m_currentPath[1] == L':') {
        NavigateTo(L"THIS_PC", true, m_currentPath);
        return;
    }
    std::wstring childName = PathUtils::GetFileName(m_currentPath);
    std::wstring parent = PathUtils::GetParentPath(m_currentPath);
    if (!parent.empty()) {
        NavigateTo(parent, true, childName);
    }
}

void ExplorerApp::RefreshCurrentFolder(const std::wstring& preserveSelectedPath) {
    std::wstring selPath = preserveSelectedPath;
    if (selPath.empty() && m_focusedIndex >= 0 && m_focusedIndex < static_cast<int>(m_items.size())) {
        selPath = m_items[m_focusedIndex].fullPath;
    }
    float prevScroll = m_scrollY;
    m_allItems = m_fileEngine.EnumerateDirectory(m_currentPath, m_showHidden, m_showExtensions, m_lang);
    ApplyFilterAndSort();
    SyncGitRepo();
    m_scrollY = prevScroll;
    ClampScrollOffset();

    if (!selPath.empty()) {
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            if (_wcsicmp(m_items[i].fullPath.c_str(), selPath.c_str()) == 0) {
                SelectSingle(i, true);
                break;
            }
        }
    }
    if ((m_viewMode == ViewMode::Proportions || m_viewMode == ViewMode::PieChart) && m_currentPath != L"THIS_PC") {
        m_fileEngine.RequestFolderSizes(m_items, false);
        EnsureWorkAnimTimerState();
    }

    if (IsSplitViewActive() && !m_inSplitRefresh && !m_otherPane.currentPath.empty() && !m_otherPane.isSearchActive) {
        m_inSplitRefresh = true;
        SwapWithOtherPane();
        m_overrideMainRect = true;
        m_customMainRect = GetSplitPaneContentMainRect(1 - m_activePane);
        std::wstring otherSel;
        if (m_focusedIndex >= 0 && m_focusedIndex < static_cast<int>(m_items.size())) {
            otherSel = m_items[m_focusedIndex].fullPath;
        }
        float otherPrevScroll = m_scrollY;
        m_allItems = m_fileEngine.EnumerateDirectory(m_currentPath, m_showHidden, m_showExtensions, m_lang);
        ApplyFilterAndSort();
        m_scrollY = otherPrevScroll;
        ClampScrollOffset();
        if (!otherSel.empty()) {
            for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
                if (_wcsicmp(m_items[i].fullPath.c_str(), otherSel.c_str()) == 0) {
                    SelectSingle(i, true);
                    break;
                }
            }
        }
        m_overrideMainRect = false;
        SwapWithOtherPane();
        m_inSplitRefresh = false;
    }

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::RebuildBreadcrumbs() {
    m_breadcrumbs.clear();
    const Strings& s = GetStrings(m_lang);

    BreadcrumbSegment rootSeg;
    rootSeg.label = s.navThisPC;
    rootSeg.path = L"THIS_PC";
    m_breadcrumbs.push_back(rootSeg);

    if (m_currentPath.empty() || m_currentPath == L"THIS_PC") {
        return;
    }

    if (m_currentPath.size() >= 2 && m_currentPath[1] == L':') {
        std::wstring driveRoot = m_currentPath.substr(0, 2) + L"\\";
        wchar_t volName[MAX_PATH] = {};
        if (GetDriveTypeW(driveRoot.c_str()) != DRIVE_REMOTE) {
            GetVolumeInformationW(driveRoot.c_str(), volName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
        }
        BreadcrumbSegment driveSeg;
        std::wstring dl = m_currentPath.substr(0, 2);
        driveSeg.label = volName[0] ? (std::wstring(volName) + L" (" + dl + L")") : dl;
        driveSeg.path = driveRoot;
        m_breadcrumbs.push_back(driveSeg);

        size_t pos = 3;
        while (pos < m_currentPath.size()) {
            size_t nextSlash = m_currentPath.find_first_of(L"\\/", pos);
            if (nextSlash == std::wstring::npos) nextSlash = m_currentPath.size();
            if (nextSlash > pos) {
                BreadcrumbSegment seg;
                seg.label = m_currentPath.substr(pos, nextSlash - pos);
                seg.path = m_currentPath.substr(0, nextSlash);
                m_breadcrumbs.push_back(seg);
            }
            pos = nextSlash + 1;
        }
    }
}

void ExplorerApp::ApplyFilterAndSort() {
    if (m_isSearchActive) {
        FileEngine::SortEntries(m_items, m_sortCol, m_sortAsc);
        ClampScrollOffset();
        return;
    }

    m_items.clear();
    m_items.reserve(m_allItems.size());

    if (m_filterText.empty()) {
        m_items = m_allItems;
    } else {
        std::wstring needle = PathUtils::ToLower(m_filterText);
        for (const auto& item : m_allItems) {
            std::wstring hay = PathUtils::ToLower(item.name);
            if (hay.find(needle) != std::wstring::npos) {
                m_items.push_back(item);
            }
        }
    }

    FileEngine::SortEntries(m_items, m_sortCol, m_sortAsc);
    ClampScrollOffset();
}

D2D1_RECT_F ExplorerApp::GetFullWorkspaceRect() const {
    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    float w = static_cast<float>(rc.right - rc.left);
    float h = static_cast<float>(rc.bottom - rc.top);

    if (m_isFullScreen) {
        return D2D1::RectF(0.0f, 0.0f, w, h);
    }

    float topY = kRibbonTabsHeight + (m_ribbonCollapsed ? 0.0f : kRibbonBodyHeight) + kNavBarHeight;
    float bottomY = std::max(topY, h - kStatusBarHeight);
    float leftX = m_showNavPane ? m_treePaneWidth : 0.0f;
    float rightX = PreviewPaneVisible() ? std::max(leftX + 100.0f, w - m_previewPaneWidth) : w;
    if (GitDockVisible()) rightX = std::max(leftX + 100.0f, rightX - m_gitDockWidth);

    return D2D1::RectF(leftX, topY, rightX, bottomY);
}

D2D1_RECT_F ExplorerApp::GetSplitPaneOuterRect(int paneIdx) const {
    D2D1_RECT_F full = GetFullWorkspaceRect();
    if (!IsSplitViewActive()) return full;
    float totalW = std::max(100.0f, full.right - full.left);
    float splitX = full.left + std::round(totalW * std::clamp(m_splitRatio, 0.20f, 0.80f));
    if (paneIdx == 0) {
        return D2D1::RectF(full.left, full.top, std::max(full.left + 60.0f, splitX - 3.0f), full.bottom);
    } else {
        return D2D1::RectF(std::min(full.right - 60.0f, splitX + 3.0f), full.top, full.right, full.bottom);
    }
}

D2D1_RECT_F ExplorerApp::GetSplitPaneContentMainRect(int paneIdx) const {
    D2D1_RECT_F r = GetSplitPaneOuterRect(paneIdx);
    if (IsSplitViewActive()) {
        r.top = std::min(r.bottom, r.top + kSplitPaneHeaderHeight);
    }
    return r;
}

D2D1_RECT_F ExplorerApp::GetMainViewRect() const {
    if (m_overrideMainRect) {
        return m_customMainRect;
    }
    if (IsSplitViewActive()) {
        return GetSplitPaneContentMainRect(m_activePane);
    }
    return GetFullWorkspaceRect();
}

void ExplorerApp::SwapWithOtherPane() {
    std::swap(m_currentPath, m_otherPane.currentPath);
    std::swap(m_currentNormKey, m_otherPane.currentNormKey);
    std::swap(m_history, m_otherPane.history);
    std::swap(m_historyIndex, m_otherPane.historyIndex);
    std::swap(m_filterText, m_otherPane.filterText);
    std::swap(m_isSearchActive, m_otherPane.isSearchActive);
    std::swap(m_allItems, m_otherPane.allItems);
    std::swap(m_items, m_otherPane.items);
    std::swap(m_breadcrumbs, m_otherPane.breadcrumbs);
    std::swap(m_sortCol, m_otherPane.sortCol);
    std::swap(m_sortAsc, m_otherPane.sortAsc);
    std::swap(m_scrollY, m_otherPane.scrollY);
    std::swap(m_filmstripScrollX, m_otherPane.filmstripScrollX);
    std::swap(m_focusedIndex, m_otherPane.focusedIndex);
    std::swap(m_anchorIndex, m_otherPane.anchorIndex);
    std::swap(m_hoverIndex, m_otherPane.hoverIndex);
    std::swap(m_hoverRowActionKind, m_otherPane.hoverRowActionKind);
    std::swap(m_ScrollbarThumbRect, m_otherPane.scrollbarThumbRect);
    std::swap(m_folderViewStates, m_otherPane.folderViewStates);
    std::swap(m_treemapHits, m_otherPane.treemapHits);
    std::swap(m_hoverTreemapHitIndex, m_otherPane.hoverTreemapHitIndex);
    std::swap(m_pieHits, m_otherPane.pieHits);
    std::swap(m_hoverPieHitIndex, m_otherPane.hoverPieHitIndex);
    std::swap(m_pieCenterPt, m_otherPane.pieCenterPt);
    std::swap(m_pieCenterRadius, m_otherPane.pieCenterRadius);
}

void ExplorerApp::SetActivePane(int paneIdx) {
    paneIdx = (paneIdx == 0) ? 0 : 1;
    if (paneIdx == m_activePane) return;
    CancelInlineEdits(false);
    SwapWithOtherPane();
    m_activePane = paneIdx;
    RebuildBreadcrumbs();
    SyncGitRepo();
    SyncMediaPlayerToSelection();
    std::wstring folderTitle = (m_currentPath == L"THIS_PC")
        ? GetStrings(m_lang).navThisPC
        : PathUtils::GetFileName(m_currentPath);
    SetWindowTextW(m_hwnd, folderTitle.c_str());
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::ToggleSplitView() {
    CancelInlineEdits(false);
    m_splitView = !m_splitView;
    if (m_splitView) {
        if (m_viewMode == ViewMode::Hardware || m_viewMode == ViewMode::FullViewer) {
            m_viewMode = m_lastNonViewerMode;
        }
        if (m_otherPane.currentPath.empty()) {
            std::wstring initPath = m_currentPath.empty() ? L"THIS_PC" : m_currentPath;
            int prevActive = m_activePane;
            SwapWithOtherPane();
            m_activePane = 1 - prevActive;
            NavigateTo(initPath, true);
            SwapWithOtherPane();
            m_activePane = prevActive;
            RebuildBreadcrumbs();
            SyncGitRepo();
        }
    } else if (m_activePane != 0) {
        // When closing split view, keep whichever pane was active as the single main pane (index 0)
        m_activePane = 0;
    }
    ClampScrollOffset();
    SaveUiSettings();
    const bool pl = (m_lang == Lang::PL);
    m_statusToast = m_splitView
        ? (pl ? L"\x25EB Włączono podwójny widok (2 panele katalogów)  \x2022  Tab przełącza aktywny panel" : L"\x25EB Enabled Split View (2 directory panes)  \x2022  Tab switches active pane")
        : (pl ? L"\x25A1 Wyłączono podwójny widok (pojedynczy panel)" : L"\x25A1 Disabled Split View (single pane)");
    m_statusToastExpireTick = GetTickCount64() + 3200;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::CopyOrMoveToOtherPane(bool move) {
    const bool pl = (m_lang == Lang::PL);
    if (!IsSplitViewActive()) {
        ToggleSplitView();
        return;
    }
    auto paths = GetSelectedPaths();
    if (paths.empty()) {
        m_statusToast = pl ? L"\x26A0 Zaznacz pliki lub foldery, aby skopiować / przenieść do drugiego panelu"
                           : L"\x26A0 Select files or folders to copy / move to the other pane";
        m_statusToastExpireTick = GetTickCount64() + 3500;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    const std::wstring dstDir = m_otherPane.currentPath;
    if (dstDir.empty() || dstDir == L"THIS_PC") {
        m_statusToast = pl ? L"\x26A0 Otwórz folder docelowy w drugim panelu (nie można kopiować do „Ten komputer”)"
                           : L"\x26A0 Open a destination folder in the other pane (cannot copy into 'This PC')";
        m_statusToastExpireTick = GetTickCount64() + 3800;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (PathUtils::NormalizePathKey(m_currentPath) == PathUtils::NormalizePathKey(dstDir)) {
        m_statusToast = pl ? L"\x26A0 Oba panele wskazują ten sam folder: " + dstDir
                           : L"\x26A0 Both panes point to the same folder: " + dstDir;
        m_statusToastExpireTick = GetTickCount64() + 3500;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    std::wstring fromDoubleNull;
    for (const auto& p : paths) {
        fromDoubleNull += p;
        fromDoubleNull.push_back(L'\0');
    }
    fromDoubleNull.push_back(L'\0');

    std::wstring toDoubleNull = dstDir;
    toDoubleNull.push_back(L'\0');
    toDoubleNull.push_back(L'\0');

    SHFILEOPSTRUCTW fop = {};
    fop.hwnd = m_hwnd;
    fop.wFunc = move ? FO_MOVE : FO_COPY;
    fop.pFrom = fromDoubleNull.c_str();
    fop.pTo   = toDoubleNull.c_str();
    fop.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR;
    int rc = SHFileOperationW(&fop);
    if (rc == 0 && !fop.fAnyOperationsAborted) {
        AppLog::Ok(move ? L"Move" : L"Copy",
                   std::wstring(move ? L"Moved to 2nd pane (" : L"Copied to 2nd pane (") + dstDir + L"): " + std::to_wstring(paths.size()) + L" item(s)");
        m_statusToast = (move ? (pl ? L"\x2713 Przeniesiono do 2. panelu: " : L"\x2713 Moved to 2nd pane: ")
                              : (pl ? L"\x2713 Skopiowano do 2. panelu: " : L"\x2713 Copied to 2nd pane: ")) +
                        std::to_wstring(paths.size()) + (pl ? L" el.  \x2192  " : L" item(s)  \x2192  ") + PathUtils::GetFileName(dstDir);
        m_statusToastExpireTick = GetTickCount64() + 4000;
    }
    RefreshCurrentFolder();
}

void ExplorerApp::BeginPaneAddressEdit(int paneIdx) {
    SetActivePane(paneIdx);
    D2D1_RECT_F r = m_paneHeaderAddressRect[paneIdx];
    if (r.right <= r.left + 20.0f) r = m_addressBarRect;
    SetWindowPos(m_addressEdit, HWND_TOP,
        static_cast<int>(r.left),
        static_cast<int>(r.top),
        static_cast<int>(r.right - r.left),
        static_cast<int>(r.bottom - r.top),
        SWP_SHOWWINDOW);
    SetWindowTextW(m_addressEdit, m_currentPath == L"THIS_PC" ? L"" : m_currentPath.c_str());
    SetFocus(m_addressEdit);
    SendMessageW(m_addressEdit, EM_SETSEL, 0, -1);
}

D2D1_RECT_F ExplorerApp::GetGitDockRect() const {
    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    float w = static_cast<float>(rc.right - rc.left);
    float h = static_cast<float>(rc.bottom - rc.top);
    float topY = kRibbonTabsHeight + (m_ribbonCollapsed ? 0.0f : kRibbonBodyHeight) + kNavBarHeight;
    float bottomY = std::max(topY, h - kStatusBarHeight);
    float leftX = m_showNavPane ? m_treePaneWidth : 0.0f;
    float right = PreviewPaneVisible() ? std::max(leftX + 100.0f, w - m_previewPaneWidth) : w;
    float left = std::max(leftX + 100.0f, right - m_gitDockWidth);
    return D2D1::RectF(left, topY, right, bottomY);
}

D2D1_RECT_F ExplorerApp::GetPreviewPaneRect() const {
    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    float w = static_cast<float>(rc.right - rc.left);
    float h = static_cast<float>(rc.bottom - rc.top);
    float topY = kRibbonTabsHeight + (m_ribbonCollapsed ? 0.0f : kRibbonBodyHeight) + kNavBarHeight;
    float bottomY = std::max(topY, h - kStatusBarHeight);
    float leftX = m_showNavPane ? m_treePaneWidth : 0.0f;
    float left = std::max(leftX + 120.0f, w - m_previewPaneWidth);
    return D2D1::RectF(left, topY, w, bottomY);
}

int ExplorerApp::HitResizeHandle(float x, float y) const {
    if (m_isFullScreen) return 0;
    D2D1_RECT_F fullRect = GetFullWorkspaceRect();
    if (y < fullRect.top || y > fullRect.bottom) return 0;
    if (PreviewPaneVisible()) {
        float prevLeft = GetPreviewPaneRect().left;
        if (std::abs(x - prevLeft) <= 5.0f) return 5;
    }
    if (GitDockVisible()) {
        float dockLeft = GetGitDockRect().left;
        if (std::abs(x - dockLeft) <= 3.0f) return 2;
    }
    if (m_showNavPane && std::abs(x - m_treePaneWidth) <= 5.0f) return 1;
    if (IsSplitViewActive()) {
        float totalW = std::max(100.0f, fullRect.right - fullRect.left);
        float splitX = fullRect.left + std::round(totalW * std::clamp(m_splitRatio, 0.20f, 0.80f));
        if (std::abs(x - splitX) <= 5.0f) return 4;
    }
    D2D1_RECT_F checkRect = GetMainViewRect();
    bool checkSearch = m_isSearchActive;
    if (IsSplitViewActive()) {
        D2D1_RECT_F r0 = GetSplitPaneContentMainRect(0);
        D2D1_RECT_F r1 = GetSplitPaneContentMainRect(1);
        if (x >= r0.left && x <= r0.right) {
            checkRect = r0;
            checkSearch = (m_activePane == 0) ? m_isSearchActive : m_otherPane.isSearchActive;
        } else if (x >= r1.left && x <= r1.right) {
            checkRect = r1;
            checkSearch = (m_activePane == 1) ? m_isSearchActive : m_otherPane.isSearchActive;
        }
    }
    float hdrTop = checkRect.top + (checkSearch ? 28.0f : 0.0f);
    if (m_viewMode == ViewMode::Details && y >= hdrTop && y <= hdrTop + kDetailsHeaderHeight &&
        x >= checkRect.left && x <= checkRect.right) {
        float colX = checkRect.left + 6.0f;
        for (int c = 0; c < 5; ++c) {
            colX += m_colWidths[c];
            if (std::abs(x - colX) <= 5.0f) return 3;
        }
    }
    return 0;
}

static std::wstring UiSettingsPath() {
    return PathUtils::GetParentPath(MonitorStore::DataDir()) + L"\\settings.ini";
}

void ExplorerApp::LoadUiSettings() {
    std::wstring ini = UiSettingsPath();
    m_gitEnabled = GetPrivateProfileIntW(L"UI", L"GitIntegration", 1, ini.c_str()) != 0;
    int splitPct = GetPrivateProfileIntW(L"UI", L"SplitRatioPct", 50, ini.c_str());
    m_splitRatio = std::clamp(static_cast<float>(splitPct) / 100.0f, 0.20f, 0.80f);
    m_showPreviewPane = GetPrivateProfileIntW(L"UI", L"ShowPreviewPane", 0, ini.c_str()) != 0;
    int prevW = GetPrivateProfileIntW(L"UI", L"PreviewPaneWidth", 450, ini.c_str());
    m_previewPaneWidth = std::clamp(static_cast<float>(prevW), 260.0f, 1000.0f);

    int recentCount = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"RecentDirs", L"Count", 0, ini.c_str())), 0, 30);
    m_recentDirs.clear();
    for (int i = 0; i < recentCount; ++i) {
        wchar_t key[32] = {};
        swprintf_s(key, L"Dir%d", i);
        wchar_t val[MAX_PATH * 2] = {};
        GetPrivateProfileStringW(L"RecentDirs", key, L"", val, static_cast<DWORD>(std::size(val)), ini.c_str());
        std::wstring p = PathUtils::TrimTrailingSlashes(val);
        if (!p.empty()) {
            std::wstring nk = (p == L"THIS_PC") ? L"__HEADER_PC__" : PathUtils::NormalizePathKey(p);
            bool dup = false;
            for (const auto& existing : m_recentDirs) {
                std::wstring enk = (existing == L"THIS_PC") ? L"__HEADER_PC__" : PathUtils::NormalizePathKey(existing);
                if (enk == nk) { dup = true; break; }
            }
            if (!dup) m_recentDirs.push_back(p);
        }
    }
}

void ExplorerApp::SaveUiSettings() const {
    std::wstring ini = UiSettingsPath();
    WritePrivateProfileStringW(L"UI", L"GitIntegration", m_gitEnabled ? L"1" : L"0", ini.c_str());
    int splitPct = static_cast<int>(std::round(std::clamp(m_splitRatio, 0.20f, 0.80f) * 100.0f));
    WritePrivateProfileStringW(L"UI", L"SplitRatioPct", std::to_wstring(splitPct).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"UI", L"ShowPreviewPane", m_showPreviewPane ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"UI", L"PreviewPaneWidth", std::to_wstring(static_cast<int>(std::round(m_previewPaneWidth))).c_str(), ini.c_str());

    int cnt = std::min(static_cast<int>(m_recentDirs.size()), 30);
    WritePrivateProfileStringW(L"RecentDirs", L"Count", std::to_wstring(cnt).c_str(), ini.c_str());
    for (int i = 0; i < cnt; ++i) {
        wchar_t key[32] = {};
        swprintf_s(key, L"Dir%d", i);
        WritePrivateProfileStringW(L"RecentDirs", key, m_recentDirs[i].c_str(), ini.c_str());
    }
}

// Finds the git repository (if any) that contains the folder being browsed and asks the worker to refresh it.
void ExplorerApp::SyncGitRepo(bool force) {
    std::wstring root;
    if (m_gitEnabled && !m_currentPath.empty() && m_currentPath != L"THIS_PC") {
        root = GitIntegration::FindRepoRoot(m_currentPath);
    }
    bool changed = (root != m_gitRoot);
    if (changed) {
        m_gitRoot = root;
        m_gitScrollY = 0.0f;
        m_gitSelectedCommit.clear();
        m_gitShowOutput = false;
        m_gitStateVersion = ~0ull;
        if (root.empty()) KillTimer(m_hwnd, ID_TIMER_GIT);
        else SetTimer(m_hwnd, ID_TIMER_GIT, 20000, nullptr);   // periodic refresh while a repo is shown
    }
    if (!m_gitRoot.empty()) {
        ULONGLONG now = GetTickCount64();
        if (changed || force || now - m_gitLastRequestTick > 2000) {   // coalesce bursts of folder refreshes
            m_gitLastRequestTick = now;
            m_git.RequestRefresh(m_gitRoot);
            EnsureWorkAnimTimerState();
        }
    }
}

bool ExplorerApp::HandleGitDockClick(float x, float y, bool rightButton) {
    if (!GitDockVisible()) return false;
    if (!PathUtils::PtInRectF(x, y, GetGitDockRect())) return false;
    if (rightButton) return true;

    const bool pl = (m_lang == Lang::PL);
    for (const GitDockHit& h : m_gitHits) {
        if (!PathUtils::PtInRectF(x, y, h.rect)) continue;
        switch (h.kind) {
        case GH_Tab:
            m_gitTab = h.index;
            m_gitScrollY = 0.0f;
            break;
        case GH_Refresh:
            SyncGitRepo(true);
            break;
        case GH_Fetch:
            m_gitShowOutput = true;
            AppLog::Info(L"Git", L"fetch  in " + m_gitRoot);
            m_gitActionLogPending = true;
            m_git.RunAction(m_gitRoot, GitAction::Fetch);
            break;
        case GH_Pull:
        case GH_Push: {
            const bool pull = (h.kind == GH_Pull);
            std::wstring q = (pull ? (pl ? L"Wykonać git pull (tylko fast-forward) w repozytorium:\n\n" : L"Run git pull (fast-forward only) in:\n\n")
                                   : (pl ? L"Wykonać git push z bieżącej gałęzi w repozytorium:\n\n" : L"Run git push for the current branch in:\n\n")) + m_gitRoot;
            if (MessageBoxW(m_hwnd, q.c_str(), pull ? L"git pull" : L"git push", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            m_gitShowOutput = true;
            AppLog::Info(L"Git", std::wstring(pull ? L"pull" : L"push") + L"  in " + m_gitRoot);
            m_gitActionLogPending = true;
            m_git.RunAction(m_gitRoot, pull ? GitAction::Pull : GitAction::Push);
            if (pull) m_gitReloadFolderWhenIdle = true;
            break;
        }
        case GH_DiffStat:
            m_gitShowOutput = true;
            m_git.RunAction(m_gitRoot, GitAction::ShowDiffStat);
            break;
        case GH_File: {
            std::wstring rel = h.arg;
            while (!rel.empty() && (rel.back() == L'/' || rel.back() == L'\\')) rel.pop_back();
            std::replace(rel.begin(), rel.end(), L'/', L'\\');
            std::wstring abs = PathUtils::EnsureTrailingSlash(m_gitRoot) + rel;
            DWORD attrs = GetFileAttributesW(abs.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                NavigateTo(abs, true);
            } else {
                std::wstring parent = PathUtils::GetParentPath(abs);
                if (!parent.empty()) NavigateTo(parent, true, attrs != INVALID_FILE_ATTRIBUTES ? PathUtils::GetFileName(abs) : L"");
            }
            break;
        }
        case GH_Commit:
            m_gitSelectedCommit = h.arg;
            m_gitShowOutput = true;
            m_git.RunAction(m_gitRoot, GitAction::ShowCommit, h.arg);
            break;
        case GH_CopyHash:
            FileEngine::CopyTextToClipboard(m_hwnd, h.arg);
            m_statusToast = (pl ? L"\x2713 Skopiowano hash: " : L"\x2713 Copied hash: ") + h.arg.substr(0, 10);
            m_statusToastExpireTick = GetTickCount64() + 2600;
            break;
        case GH_Branch: {
            std::wstring q = (pl ? L"Przełączyć na gałąź „" : L"Switch to branch \"") + h.arg + (pl ? L"”?\n\nNiezatwierdzone zmiany mogą uniemożliwić przełączenie (git zgłosi błąd i nic nie zmieni)." : L"\"?\n\nUncommitted changes may block the switch (git will report an error and change nothing).");
            if (MessageBoxW(m_hwnd, q.c_str(), L"git switch", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            m_gitShowOutput = true;
            AppLog::Info(L"Git", L"switch to branch " + h.arg + L"  in " + m_gitRoot);
            m_gitActionLogPending = true;
            m_git.RunAction(m_gitRoot, GitAction::Checkout, h.arg);
            m_gitReloadFolderWhenIdle = true;
            break;
        }
        case GH_CloseOutput:
            m_gitShowOutput = false;
            break;
        case GH_CopyOutput:
            FileEngine::CopyTextToClipboard(m_hwnd, m_gitOutput);
            m_statusToast = pl ? L"\x2713 Skopiowano wynik git" : L"\x2713 Copied git output";
            m_statusToastExpireTick = GetTickCount64() + 2600;
            break;
        default: break;
        }
        EnsureWorkAnimTimerState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return true;
    }
    return true;
}

D2D1_RECT_F ExplorerApp::GetContentItemsRect() const {
    D2D1_RECT_F r = GetMainViewRect();
    if (m_isSearchActive && m_viewMode != ViewMode::FullViewer && m_viewMode != ViewMode::Hardware) {
        r.top += 28.0f;
    }
    if (m_viewMode == ViewMode::Details) {
        r.top += kDetailsHeaderHeight;
    }
    if (m_viewMode != ViewMode::Proportions && m_viewMode != ViewMode::PieChart && m_viewMode != ViewMode::Hardware) {
        r.right = std::max(r.left, r.right - kScrollbarWidth);
    }
    return r;
}

void ExplorerApp::GetGridMetrics(const D2D1_RECT_F& contentRect, float& outCellW, float& outCellH, float& outIconSize, int& outCols) const {
    float availW = std::max(60.0f, contentRect.right - contentRect.left - 12.0f);
    switch (m_viewMode) {
    case ViewMode::SmallIcons:
        outIconSize = 20.0f;
        outCellW = 190.0f;
        outCellH = 26.0f;
        break;
    case ViewMode::List:
        outIconSize = 16.0f;
        outCellW = 220.0f;
        outCellH = 22.0f;
        break;
    case ViewMode::MediumIcons:
        outIconSize = 64.0f;
        outCellW = 96.0f;
        outCellH = 104.0f;
        break;
    case ViewMode::LargeIcons:
        outIconSize = 128.0f;
        outCellW = 152.0f;
        outCellH = 164.0f;
        break;
    case ViewMode::ExtraLargeIcons:
        outIconSize = 236.0f;
        outCellW = 256.0f;
        outCellH = 272.0f;
        break;
    case ViewMode::JumboIcons:
    default:
        outCellW = std::min(368.0f, std::max(240.0f, availW));
        outIconSize = outCellW - 24.0f;
        outCellH = outIconSize + 40.0f;
        break;
    }
    outCols = std::max(1, static_cast<int>(availW / outCellW));
}

D2D1_RECT_F ExplorerApp::GetItemRect(int index, const D2D1_RECT_F& contentRect) const {
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return D2D1::RectF(0, 0, 0, 0);
    }

    if (m_viewMode == ViewMode::Details) {
        float y = contentRect.top + index * kDetailsRowHeight - m_scrollY;
        float totalColsW = m_colWidths[0] + m_colWidths[1] + m_colWidths[2] + m_colWidths[3] + m_colWidths[4];
        float rowW = std::max(totalColsW, contentRect.right - contentRect.left - 4.0f);
        return D2D1::RectF(contentRect.left + 2.0f, y, contentRect.left + rowW, y + kDetailsRowHeight);
    }

    float cellW = 0, cellH = 0, iconSize = 0;
    int cols = 1;
    GetGridMetrics(contentRect, cellW, cellH, iconSize, cols);
    int col = index % cols;
    int row = index / cols;
    float x = contentRect.left + 8.0f + col * cellW;
    float y = contentRect.top + 6.0f + row * cellH - m_scrollY;
    return D2D1::RectF(x, y, x + cellW - 4.0f, y + cellH - 4.0f);
}

static int FindHoveredPieSectorIndex(float x, float y, const std::vector<PieSectorHitNode>& pieHits, D2D1_POINT_2F centerPt) {
    // 1. Check right-side legend rows first
    for (int i = 0; i < static_cast<int>(pieHits.size()); ++i) {
        if (pieHits[i].hasLegendRect && PathUtils::PtInRectF(x, y, pieHits[i].legendRect)) {
            return i;
        }
    }
    // 2. Check radial sunburst rings (from outermost ring inward)
    float dx = x - centerPt.x;
    float dy = y - centerPt.y;
    float dist = std::hypot(dx, dy);
    // 0 degrees = 12 o'clock (-90 deg standard atan2), clockwise [0..360)
    float angDeg = std::atan2(dy, dx) * (180.0f / 3.1415926535f) + 90.0f;
    if (angDeg < 0.0f) angDeg += 360.0f;
    if (angDeg >= 360.0f) angDeg -= 360.0f;

    for (int i = static_cast<int>(pieHits.size()) - 1; i >= 0; --i) {
        const auto& s = pieHits[i];
        if (dist >= s.innerRadius && dist <= s.outerRadius &&
            angDeg >= s.startAngleDeg && angDeg <= (s.startAngleDeg + s.sweepAngleDeg)) {
            return i;
        }
    }
    return -1;
}

int ExplorerApp::HitTestItem(float x, float y, const D2D1_RECT_F& contentRect, bool* outOnLabelOrIcon) const {
    if (outOnLabelOrIcon) *outOnLabelOrIcon = false;
    if (!PathUtils::PtInRectF(x, y, contentRect)) {
        return -1;
    }

    if (m_viewMode == ViewMode::Hardware) {
        return -1;
    }

    if (m_viewMode == ViewMode::Proportions) {
        for (int i = static_cast<int>(m_treemapHits.size()) - 1; i >= 0; --i) {
            if (PathUtils::PtInRectF(x, y, m_treemapHits[i].rect)) {
                if (outOnLabelOrIcon) *outOnLabelOrIcon = true;
                return m_treemapHits[i].topItemIndex;
            }
        }
        return -1;
    }

    if (m_viewMode == ViewMode::PieChart) {
        int sIdx = FindHoveredPieSectorIndex(x, y, m_pieHits, m_pieCenterPt);
        if (sIdx >= 0) {
            if (outOnLabelOrIcon) *outOnLabelOrIcon = true;
            return m_pieHits[sIdx].topItemIndex;
        }
        return -1;
    }

    if (m_viewMode == ViewMode::Details) {
        float relY = y - contentRect.top + m_scrollY;
        if (relY < 0.0f) return -1;
        int idx = static_cast<int>(relY / kDetailsRowHeight);
        if (idx >= 0 && idx < static_cast<int>(m_items.size())) {
            float nameAndColsW = m_colWidths[0] + m_colWidths[1] + m_colWidths[2] + m_colWidths[3] + m_colWidths[4];
            if (x <= contentRect.left + nameAndColsW) {
                if (outOnLabelOrIcon) *outOnLabelOrIcon = true;
                return idx;
            }
        }
        return -1;
    }

    float cellW = 0, cellH = 0, iconSize = 0;
    int cols = 1;
    GetGridMetrics(contentRect, cellW, cellH, iconSize, cols);
    float relX = x - (contentRect.left + 8.0f);
    float relY = y - (contentRect.top + 6.0f) + m_scrollY;
    if (relX < 0.0f || relY < 0.0f) return -1;

    int col = static_cast<int>(relX / cellW);
    int row = static_cast<int>(relY / cellH);
    if (col >= cols) return -1;

    int idx = row * cols + col;
    if (idx >= 0 && idx < static_cast<int>(m_items.size())) {
        D2D1_RECT_F r = GetItemRect(idx, contentRect);
        if (PathUtils::PtInRectF(x, y, r)) {
            if (outOnLabelOrIcon) *outOnLabelOrIcon = true;
            return idx;
        }
    }
    return -1;
}

float ExplorerApp::GetTotalContentHeight(const D2D1_RECT_F& contentRect) const {
    if (m_items.empty() || m_viewMode == ViewMode::Proportions || m_viewMode == ViewMode::PieChart || m_viewMode == ViewMode::Hardware) return 0.0f;
    if (m_viewMode == ViewMode::Details) {
        return static_cast<float>(m_items.size()) * kDetailsRowHeight + 8.0f;
    }
    float cellW = 0, cellH = 0, iconSize = 0;
    int cols = 1;
    GetGridMetrics(contentRect, cellW, cellH, iconSize, cols);
    int rows = (static_cast<int>(m_items.size()) + cols - 1) / cols;
    return rows * cellH + 16.0f;
}

void ExplorerApp::ClampScrollOffset() {
    D2D1_RECT_F contentRect = GetContentItemsRect();
    float viewH = std::max(1.0f, contentRect.bottom - contentRect.top);
    float totalH = GetTotalContentHeight(contentRect);
    float maxScroll = std::max(0.0f, totalH - viewH);
    m_scrollY = std::clamp(m_scrollY, 0.0f, maxScroll);
}

void ExplorerApp::EnsureItemVisible(int index) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;
    if (m_viewMode == ViewMode::Proportions || m_viewMode == ViewMode::PieChart || m_viewMode == ViewMode::Hardware) return;

    if (m_viewMode == ViewMode::FullViewer) {
        float thumbStep = 74.0f;
        D2D1_RECT_F mainRect = GetMainViewRect();
        float availW = std::max(100.0f, mainRect.right - mainRect.left);
        float targetX = index * thumbStep - (availW * 0.5f) + (thumbStep * 0.5f);
        float maxScrollX = std::max(0.0f, static_cast<float>(m_items.size()) * thumbStep - availW + 20.0f);
        m_filmstripScrollX = std::clamp(targetX, 0.0f, maxScrollX);
        return;
    }

    D2D1_RECT_F contentRect = GetContentItemsRect();
    D2D1_RECT_F itemR = GetItemRect(index, contentRect);
    if (itemR.top < contentRect.top) {
        m_scrollY -= (contentRect.top - itemR.top);
    } else if (itemR.bottom > contentRect.bottom) {
        m_scrollY += (itemR.bottom - contentRect.bottom);
    }
    ClampScrollOffset();
}

void ExplorerApp::SelectSingle(int index, bool makeAnchor) {
    for (auto& item : m_items) item.isSelected = false;
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        m_items[index].isSelected = true;
        m_focusedIndex = index;
        if (makeAnchor) m_anchorIndex = index;
    } else {
        m_focusedIndex = -1;
        if (makeAnchor) m_anchorIndex = -1;
    }
    SyncMediaPlayerToSelection();
}

void ExplorerApp::SelectRange(int fromIndex, int toIndex, bool keepExisting) {
    if (m_items.empty()) return;
    if (!keepExisting) {
        for (auto& item : m_items) item.isSelected = false;
    }
    int lo = std::clamp(std::min(fromIndex, toIndex), 0, static_cast<int>(m_items.size()) - 1);
    int hi = std::clamp(std::max(fromIndex, toIndex), 0, static_cast<int>(m_items.size()) - 1);
    for (int i = lo; i <= hi; ++i) {
        m_items[i].isSelected = true;
    }
    m_focusedIndex = std::clamp(toIndex, 0, static_cast<int>(m_items.size()) - 1);
    SyncMediaPlayerToSelection();
}

void ExplorerApp::SelectAll() {
    for (auto& item : m_items) item.isSelected = true;
    if (!m_items.empty() && m_focusedIndex < 0) m_focusedIndex = 0;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::SelectNone() {
    for (auto& item : m_items) item.isSelected = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::InvertSelection() {
    for (auto& item : m_items) item.isSelected = !item.isSelected;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

std::vector<std::wstring> ExplorerApp::GetSelectedPaths() const {
    std::vector<std::wstring> paths;
    for (const auto& item : m_items) {
        if (item.isSelected) {
            paths.push_back(item.fullPath);
        }
    }
    if (paths.empty() && m_focusedIndex >= 0 && m_focusedIndex < static_cast<int>(m_items.size())) {
        paths.push_back(m_items[m_focusedIndex].fullPath);
    }
    return paths;
}

int ExplorerApp::GetSelectedCount(uint64_t* outTotalBytes) const {
    int count = 0;
    uint64_t bytes = 0;
    for (const auto& item : m_items) {
        if (item.isSelected) {
            ++count;
            if (!item.isDirectory) bytes += item.size;
        }
    }
    if (outTotalBytes) *outTotalBytes = bytes;
    return count;
}

void ExplorerApp::UpdateMarqueeSelection(bool ctrlHeld, bool shiftHeld) {
    D2D1_RECT_F contentRect = GetContentItemsRect();
    float minX = std::min(m_boxStartPt.x, m_boxCurrentPt.x);
    float maxX = std::max(m_boxStartPt.x, m_boxCurrentPt.x);
    float minY = std::min(m_boxStartPt.y, m_boxCurrentPt.y);
    float maxY = std::max(m_boxStartPt.y, m_boxCurrentPt.y);

    D2D1_RECT_F boxScreen = D2D1::RectF(minX, minY - m_scrollY, maxX, maxY - m_scrollY);

    for (size_t i = 0; i < m_items.size(); ++i) {
        D2D1_RECT_F itemR = GetItemRect(static_cast<int>(i), contentRect);
        bool intersects = !(itemR.right < boxScreen.left ||
                            itemR.left > boxScreen.right ||
                            itemR.bottom < boxScreen.top ||
                            itemR.top > boxScreen.bottom);

        bool wasSelected = (i < m_preBoxSelection.size()) ? m_preBoxSelection[i] : false;
        if (ctrlHeld) {
            m_items[i].isSelected = intersects ? !wasSelected : wasSelected;
        } else if (shiftHeld) {
            m_items[i].isSelected = intersects || wasSelected;
        } else {
            m_items[i].isSelected = intersects;
        }

        if (intersects) {
            m_focusedIndex = static_cast<int>(i);
        }
    }
}

void ExplorerApp::OnMouseDown(float x, float y, bool rightButton, WPARAM keyFlags) {
    bool ctrlHeld  = (keyFlags & MK_CONTROL) != 0;
    bool shiftHeld = (keyFlags & MK_SHIFT) != 0;

    // Git dock splitter (drag its left edge to resize the dock)
    if (!rightButton && HitResizeHandle(x, y) == 2) {
        m_draggingGitSplitter = true;
        SetCapture(m_hwnd);
        return;
    }

    // Git dock (right side): handled first, it never overlaps the ribbon / navigation chrome
    if (HandleGitDockClick(x, y, rightButton)) return;

    // 1. Check Ribbon & Chrome hit targets
    if (!rightButton && !m_isFullScreen) {
        for (const auto& btn : m_ribbonHits) {
            if (PathUtils::PtInRectF(x, y, btn.rect)) {
                ExecuteRibbonCommand(btn.id);
                return;
            }
        }

        float navTop = kRibbonTabsHeight + (m_ribbonCollapsed ? 0.0f : kRibbonBodyHeight);
        if (y >= navTop && y <= navTop + kNavBarHeight) {
            for (int i = 0; i < static_cast<int>(m_breadcrumbs.size()); ++i) {
                const auto& bc = m_breadcrumbs[i];
                if (PathUtils::PtInRectF(x, y, bc.arrowRect)) {
                    ShowBreadcrumbSubfolderMenu(i);
                    return;
                }
                if (PathUtils::PtInRectF(x, y, bc.hitRect)) {
                    NavigateTo(bc.path, true);
                    return;
                }
            }
            if (PathUtils::PtInRectF(x, y, m_addressBarRect)) {
                SetWindowPos(m_addressEdit, HWND_TOP,
                    static_cast<int>(m_addressBarRect.left),
                    static_cast<int>(m_addressBarRect.top),
                    static_cast<int>(m_addressBarRect.right - m_addressBarRect.left),
                    static_cast<int>(m_addressBarRect.bottom - m_addressBarRect.top),
                    SWP_SHOWWINDOW);
                SetWindowTextW(m_addressEdit, m_currentPath == L"THIS_PC" ? L"" : m_currentPath.c_str());
                SetFocus(m_addressEdit);
                SendMessageW(m_addressEdit, EM_SETSEL, 0, -1);
                return;
            }
            if (PathUtils::PtInRectF(x, y, m_searchBarRect)) {
                m_editingSearchFieldCmd = 0;
                if (m_activeRibbonTab != RibbonTab::Search && !IsProgramsView()) {
                    m_activeRibbonTab = RibbonTab::Search;
                    m_ribbonCollapsed = false;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
                float navY = kRibbonTabsHeight + (m_ribbonCollapsed ? 0.0f : kRibbonBodyHeight) + 5.0f;
                SetWindowPos(m_searchEdit, HWND_TOP,
                    static_cast<int>(m_searchBarRect.left),
                    static_cast<int>(navY),
                    static_cast<int>(m_searchBarRect.right - m_searchBarRect.left - 44),
                    static_cast<int>(m_searchBarRect.bottom - m_searchBarRect.top),
                    SWP_SHOWWINDOW);
                SetWindowTextW(m_searchEdit, IsProgramsView() ? m_progFilter.c_str() : m_filterText.c_str());
                SetFocus(m_searchEdit);
                SendMessageW(m_searchEdit, EM_SETSEL, 0, -1);
                return;
            }
        }

        D2D1_RECT_F fullRect = GetFullWorkspaceRect();
        if (m_showNavPane && std::abs(x - m_treePaneWidth) <= 5.0f && y >= fullRect.top && y <= fullRect.bottom) {
            m_draggingTreeSplitter = true;
            SetCapture(m_hwnd);
            return;
        }
        if (PreviewPaneVisible() && HitResizeHandle(x, y) == 5) {
            m_draggingPreviewSplitter = true;
            SetCapture(m_hwnd);
            return;
        }
        if (IsSplitViewActive() && HitResizeHandle(x, y) == 4) {
            m_draggingSplitSplitter = true;
            SetCapture(m_hwnd);
            return;
        }
    }

    // Check Right-Side File Content Pane click (Notepad / Hex / 3D / Media / Image Inspector)
    if (PreviewPaneVisible()) {
        D2D1_RECT_F prevRect = GetPreviewPaneRect();
        if (PathUtils::PtInRectF(x, y, prevRect)) {
            if (!rightButton && m_mediaPlayer.HasMedia() && PathUtils::PtInRectF(x, y, m_mediaSeekBarRect)) {
                float pct = std::clamp((x - m_mediaSeekBarRect.left) / std::max(1.0f, m_mediaSeekBarRect.right - m_mediaSeekBarRect.left), 0.0f, 1.0f);
                m_mediaPlayer.SeekToSeconds(pct * m_mediaPlayer.GetDurationSec());
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
            bool closeReq = false;
            int trigCmd = 0;
            std::wstring toastMsg;
            if (m_contentPane.OnMouseDown(x, y, rightButton, m_lang, &closeReq, &trigCmd, &toastMsg)) {
                if (closeReq) {
                    m_showPreviewPane = false;
                    SyncMediaPlayerToSelection();
                    ClampScrollOffset();
                    SaveUiSettings();
                } else if (trigCmd != 0) {
                    ExecuteRibbonCommand(trigCmd);
                    return;
                }
                if (!toastMsg.empty()) {
                    m_statusToast = toastMsg;
                    m_statusToastExpireTick = GetTickCount64() + 3600;
                }
                EnsureWorkAnimTimerState();
                SetCapture(m_hwnd);
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        }
    }

    D2D1_RECT_F fullRect = GetFullWorkspaceRect();

    // Check Left Navigation Tree click (Left-Click expands/navigates; Right-Click opens Szybki dostęp / Tree Context Menu!)
    if (!m_isFullScreen && m_showNavPane && x < m_treePaneWidth && y >= fullRect.top && y <= fullRect.bottom) {
        int hitTreeIdx = -1;
        for (int i = 0; i < static_cast<int>(m_treeNodes.size()); ++i) {
            if (PathUtils::PtInRectF(x, y, m_treeNodes[i].hitRect)) {
                hitTreeIdx = i;
                break;
            }
        }
        if (rightButton) {
            POINT pt = { static_cast<LONG>(x), static_cast<LONG>(y) };
            ClientToScreen(m_hwnd, &pt);
            ShowTreeContextMenu(pt.x, pt.y, hitTreeIdx);
            return;
        }
        if (hitTreeIdx >= 0) {
            const auto& node = m_treeNodes[hitTreeIdx];
            if (PathUtils::PtInRectF(x, y, node.chevronRect) || node.path.empty()) {
                ToggleTreeNodeExpand(hitTreeIdx);
            } else {
                NavigateTo(node.path, true);
            }
        }
        return;
    }

    // If Split View is active and the user clicks anywhere inside the inactive pane, activate it immediately!
    if (IsSplitViewActive()) {
        int otherIdx = 1 - m_activePane;
        D2D1_RECT_F otherOuter = GetSplitPaneOuterRect(otherIdx);
        if (PathUtils::PtInRectF(x, y, otherOuter)) {
            SetActivePane(otherIdx);
        }
    }

    D2D1_RECT_F mainRect = GetMainViewRect();

    // 2. If in FullViewer mode: check HUD buttons, seek bar, filmstrip click, or start image pan
    if (m_viewMode == ViewMode::FullViewer) {
        if (!rightButton) {
            for (const auto& btn : m_ribbonHits) {
                if (PathUtils::PtInRectF(x, y, btn.rect)) {
                    ExecuteRibbonCommand(btn.id);
                    return;
                }
            }

            // Check Media Player Seek Bar click
            if (m_mediaPlayer.HasMedia() && PathUtils::PtInRectF(x, y, m_mediaSeekBarRect)) {
                float pct = std::clamp((x - m_mediaSeekBarRect.left) / std::max(1.0f, m_mediaSeekBarRect.right - m_mediaSeekBarRect.left), 0.0f, 1.0f);
                m_mediaPlayer.SeekToSeconds(pct * m_mediaPlayer.GetDurationSec());
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }

            float stripTop = mainRect.bottom - kFilmstripHeight;
            if (!m_isFullScreen && y >= stripTop && y <= mainRect.bottom && x >= mainRect.left && x <= mainRect.right) {
                float relX = (x - mainRect.left - 10.0f) + m_filmstripScrollX;
                int idx = static_cast<int>(relX / 74.0f);
                if (idx >= 0 && idx < static_cast<int>(m_items.size())) {
                    SelectSingle(idx, true);
                    ResetViewerZoom();
                    EnsureItemVisible(idx);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
                return;
            }

            if (x >= mainRect.left && x <= mainRect.right && y >= mainRect.top && y < (m_isFullScreen ? mainRect.bottom : stripTop)) {
                if (m_mediaPlayer.HasMedia()) {
                    m_mediaPlayer.TogglePlayPause();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else {
                    m_viewerDraggingPan = true;
                    m_viewerDragStart = D2D1::Point2F(x - m_viewerPanX, y - m_viewerPanY);
                    SetCapture(m_hwnd);
                }
            }
        } else {
            POINT pt = { static_cast<LONG>(x), static_cast<LONG>(y) };
            ClientToScreen(m_hwnd, &pt);
            ShowContextMenu(pt.x, pt.y, m_focusedIndex);
        }
        return;
    }

    // 3. Check Details Header column click / resize
    float hdrTop = mainRect.top + (m_isSearchActive ? 28.0f : 0.0f);
    if (m_viewMode == ViewMode::Details && !rightButton &&
        y >= hdrTop && y <= hdrTop + kDetailsHeaderHeight &&
        x >= mainRect.left && x <= mainRect.right)
    {
        float colX = mainRect.left + 6.0f;
        for (int c = 0; c < 5; ++c) {
            float colRight = colX + m_colWidths[c];
            if (std::abs(x - colRight) <= 5.0f) {
                m_resizingCol = c;
                m_resizeStartX = x;
                m_resizeStartWidth = m_colWidths[c];
                SetCapture(m_hwnd);
                return;
            }
            if (c < 4 && x >= colX && x < colRight) {
                SortColumn clickedCol = static_cast<SortColumn>(c);
                if (m_sortCol == clickedCol) {
                    m_sortAsc = !m_sortAsc;
                } else {
                    m_sortCol = clickedCol;
                    m_sortAsc = true;
                }
                ApplyFilterAndSort();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
            colX = colRight;
        }
        return;
    }

    // 4. Check Scrollbar click/drag
    D2D1_RECT_F contentRect = GetContentItemsRect();
    if (!rightButton && x >= contentRect.right && x <= mainRect.right && y >= contentRect.top && y <= contentRect.bottom) {
        if (y >= m_ScrollbarThumbRect.top && y <= m_ScrollbarThumbRect.bottom) {
            m_draggingScrollbar = true;
            m_scrollbarDragOffsetY = y - m_ScrollbarThumbRect.top;
            SetCapture(m_hwnd);
        } else {
            float viewH = contentRect.bottom - contentRect.top;
            m_scrollY += (y < m_ScrollbarThumbRect.top) ? -viewH : viewH;
            ClampScrollOffset();
            m_imageCache.BumpViewportGeneration();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    // 5. Main Items Content Area: Hit-test item vs start Rubber-band Box Select
    if (PathUtils::PtInRectF(x, y, contentRect)) {
        if (m_viewMode == ViewMode::Hardware) {
            if (!rightButton) {
                for (const auto& card : m_hwCardHits) {
                    if (PathUtils::PtInRectF(x, y, card.rect)) {
                        if (card.cmdId != 0) {
                            m_hwActionIndex = card.actionIndex;
                            m_hwActionArg = card.actionArg;
                            ExecuteRibbonCommand(card.cmdId);
                        } else if (!card.copyText.empty()) {
                            if (OpenClipboard(m_hwnd)) {
                                EmptyClipboard();
                                size_t bytes = (card.copyText.size() + 1) * sizeof(wchar_t);
                                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
                                if (hMem) {
                                    void* p = GlobalLock(hMem);
                                    if (p) {
                                        std::memcpy(p, card.copyText.c_str(), bytes);
                                        GlobalUnlock(hMem);
                                        SetClipboardData(CF_UNICODETEXT, hMem);
                                    }
                                }
                                CloseClipboard();
                            }
                            m_statusToast = (m_lang == Lang::PL ? L"\x2713 Skopiowano do schowka: " : L"\x2713 Copied to clipboard: ") + card.label;
                            m_statusToastExpireTick = GetTickCount64() + 2600;
                            InvalidateRect(m_hwnd, nullptr, FALSE);
                        }
                        return;
                    }
                }
            }
            return;
        }

        if (m_viewMode == ViewMode::PieChart && !rightButton && m_pieCenterRadius > 0.0f) {
            float distCenter = std::hypot(x - m_pieCenterPt.x, y - m_pieCenterPt.y);
            if (distCenter <= m_pieCenterRadius) {
                NavigateUp();
                return;
            }
        }

        bool onItem = false;
        int hitIdx = HitTestItem(x, y, contentRect, &onItem);

        // Check "Operacje" column buttons ([Kosz] and [Usuń]) in Details view
        if (!rightButton && m_viewMode == ViewMode::Details && hitIdx >= 0 && m_currentPath != L"THIS_PC") {
            D2D1_RECT_F rowR = GetItemRect(hitIdx, contentRect);
            float opColX = contentRect.left + 6.0f + m_colWidths[0] + m_colWidths[1] + m_colWidths[2] + m_colWidths[3];
            D2D1_RECT_F trashBtn = D2D1::RectF(opColX + 6.0f, rowR.top + 2.0f, opColX + 62.0f, rowR.bottom - 2.0f);
            D2D1_RECT_F permBtn  = D2D1::RectF(opColX + 66.0f, rowR.top + 2.0f, opColX + 124.0f, rowR.bottom - 2.0f);
            if (PathUtils::PtInRectF(x, y, trashBtn)) {
                ConfirmAndDeleteRowItem(hitIdx, false);
                return;
            }
            if (PathUtils::PtInRectF(x, y, permBtn)) {
                ConfirmAndDeleteRowItem(hitIdx, true);
                return;
            }
        }

        if (rightButton) {
            if (hitIdx >= 0) {
                if (!m_items[hitIdx].isSelected) {
                    SelectSingle(hitIdx, true);
                } else {
                    m_focusedIndex = hitIdx;
                }
            } else if (!ctrlHeld && !shiftHeld) {
                SelectNone();
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
            POINT pt = { static_cast<LONG>(x), static_cast<LONG>(y) };
            ClientToScreen(m_hwnd, &pt);
            ShowContextMenu(pt.x, pt.y, hitIdx);
            return;
        }

        if (hitIdx >= 0 && onItem) {
            if (shiftHeld && m_anchorIndex >= 0) {
                SelectRange(m_anchorIndex, hitIdx, ctrlHeld);
            } else if (ctrlHeld) {
                m_items[hitIdx].isSelected = !m_items[hitIdx].isSelected;
                m_focusedIndex = hitIdx;
                m_anchorIndex = hitIdx;
                SyncMediaPlayerToSelection();
            } else {
                SelectSingle(hitIdx, true);
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (m_viewMode != ViewMode::Proportions && m_viewMode != ViewMode::PieChart && m_viewMode != ViewMode::Hardware) {
            if (!ctrlHeld && !shiftHeld) {
                SelectNone();
            }
            m_preBoxSelection.resize(m_items.size());
            for (size_t i = 0; i < m_items.size(); ++i) {
                m_preBoxSelection[i] = m_items[i].isSelected;
            }
            m_isBoxSelecting = true;
            m_boxStartPt = D2D1::Point2F(x, y + m_scrollY);
            m_boxCurrentPt = m_boxStartPt;
            SetCapture(m_hwnd);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
}

void ExplorerApp::OnMouseMove(float x, float y, WPARAM keyFlags) {
    m_mousePt = D2D1::Point2F(x, y);

    if (m_draggingTreeSplitter) {
        m_treePaneWidth = std::clamp(x, 140.0f, 500.0f);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_draggingGitSplitter) {
        float dockRight = GetGitDockRect().right;
        float leftX = m_showNavPane ? m_treePaneWidth : 0.0f;
        float maxW = std::max(260.0f, dockRight - leftX - 240.0f);
        m_gitDockWidth = std::clamp(dockRight - x, 260.0f, std::min(900.0f, maxW));
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_draggingSplitSplitter) {
        D2D1_RECT_F full = GetFullWorkspaceRect();
        float totalW = std::max(100.0f, full.right - full.left);
        m_splitRatio = std::clamp((x - full.left) / totalW, 0.20f, 0.80f);
        ClampScrollOffset();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_draggingPreviewSplitter) {
        RECT rc = {};
        GetClientRect(m_hwnd, &rc);
        float w = static_cast<float>(rc.right - rc.left);
        float leftX = m_showNavPane ? m_treePaneWidth : 0.0f;
        float maxW = std::max(260.0f, w - leftX - 220.0f);
        m_previewPaneWidth = std::clamp(w - x, 260.0f, std::min(1000.0f, maxW));
        ClampScrollOffset();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (PreviewPaneVisible() && m_contentPane.OnMouseMove(x, y, keyFlags)) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    if (m_resizingCol >= 0) {
        float delta = x - m_resizeStartX;
        m_colWidths[m_resizingCol] = std::max(55.0f, m_resizeStartWidth + delta);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_draggingScrollbar) {
        D2D1_RECT_F contentRect = GetContentItemsRect();
        float trackH = std::max(1.0f, contentRect.bottom - contentRect.top);
        float thumbH = m_ScrollbarThumbRect.bottom - m_ScrollbarThumbRect.top;
        float scrollableTrack = std::max(1.0f, trackH - thumbH);
        float relThumbTop = std::clamp(y - m_scrollbarDragOffsetY - contentRect.top, 0.0f, scrollableTrack);
        float totalH = GetTotalContentHeight(contentRect);
        float maxScroll = std::max(0.0f, totalH - trackH);
        m_scrollY = (relThumbTop / scrollableTrack) * maxScroll;
        ClampScrollOffset();
        m_imageCache.BumpViewportGeneration();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_viewerDraggingPan) {
        m_viewerPanX = x - m_viewerDragStart.x;
        m_viewerPanY = y - m_viewerDragStart.y;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_isBoxSelecting) {
        D2D1_RECT_F contentRect = GetContentItemsRect();
        if (y < contentRect.top + 18.0f) {
            m_scrollY -= 16.0f;
            ClampScrollOffset();
        } else if (y > contentRect.bottom - 18.0f) {
            m_scrollY += 16.0f;
            ClampScrollOffset();
        }

        m_boxCurrentPt = D2D1::Point2F(
            std::clamp(x, contentRect.left, contentRect.right),
            y + m_scrollY);
        UpdateMarqueeSelection((keyFlags & MK_CONTROL) != 0, (keyFlags & MK_SHIFT) != 0);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    D2D1_RECT_F contentRect = GetContentItemsRect();
    int prevHover = m_hoverIndex;
    int prevTreemapHover = m_hoverTreemapHitIndex;
    int prevPieHover = m_hoverPieHitIndex;
    int prevHwHover = m_hoverHwCardIndex;
    m_hoverTreemapHitIndex = -1;
    m_hoverPieHitIndex = -1;
    m_hoverHwCardIndex = -1;

    if (m_viewMode == ViewMode::Proportions) {
        for (int i = static_cast<int>(m_treemapHits.size()) - 1; i >= 0; --i) {
            if (PathUtils::PtInRectF(x, y, m_treemapHits[i].rect)) {
                m_hoverTreemapHitIndex = i;
                break;
            }
        }
        m_hoverIndex = (m_hoverTreemapHitIndex >= 0) ? m_treemapHits[m_hoverTreemapHitIndex].topItemIndex : -1;
    } else if (m_viewMode == ViewMode::PieChart) {
        m_hoverPieHitIndex = FindHoveredPieSectorIndex(x, y, m_pieHits, m_pieCenterPt);
        m_hoverIndex = (m_hoverPieHitIndex >= 0) ? m_pieHits[m_hoverPieHitIndex].topItemIndex : -1;
    } else if (m_viewMode == ViewMode::Hardware) {
        for (int i = static_cast<int>(m_hwCardHits.size()) - 1; i >= 0; --i) {
            if (PathUtils::PtInRectF(x, y, m_hwCardHits[i].rect)) {
                m_hoverHwCardIndex = i;
                break;
            }
        }
        m_hoverIndex = -1;
    } else {
        m_hoverIndex = (m_viewMode == ViewMode::FullViewer) ? -1 : HitTestItem(x, y, contentRect, nullptr);
    }

    int prevRowAction = m_hoverRowActionKind;
    m_hoverRowActionKind = 0;
    if (m_viewMode == ViewMode::Details && m_hoverIndex >= 0 && m_currentPath != L"THIS_PC") {
        D2D1_RECT_F rowR = GetItemRect(m_hoverIndex, contentRect);
        float opColX = contentRect.left + 6.0f + m_colWidths[0] + m_colWidths[1] + m_colWidths[2] + m_colWidths[3];
        D2D1_RECT_F trashBtn = D2D1::RectF(opColX + 6.0f, rowR.top + 2.0f, opColX + 62.0f, rowR.bottom - 2.0f);
        D2D1_RECT_F permBtn  = D2D1::RectF(opColX + 66.0f, rowR.top + 2.0f, opColX + 124.0f, rowR.bottom - 2.0f);
        if (PathUtils::PtInRectF(x, y, trashBtn)) m_hoverRowActionKind = 1;
        else if (PathUtils::PtInRectF(x, y, permBtn)) m_hoverRowActionKind = 2;
    }

    int prevOtherHover = m_otherPane.hoverIndex;
    int prevOtherRowAction = m_otherPane.hoverRowActionKind;
    if (IsSplitViewActive()) {
        int otherIdx = 1 - m_activePane;
        D2D1_RECT_F otherMain = GetSplitPaneContentMainRect(otherIdx);
        if (PathUtils::PtInRectF(x, y, otherMain)) {
            SwapWithOtherPane();
            m_overrideMainRect = true;
            m_customMainRect = otherMain;
            D2D1_RECT_F otherContent = GetContentItemsRect();
            m_hoverIndex = (m_viewMode == ViewMode::FullViewer || m_viewMode == ViewMode::Hardware) ? -1 : HitTestItem(x, y, otherContent, nullptr);
            m_hoverRowActionKind = 0;
            if (m_viewMode == ViewMode::Details && m_hoverIndex >= 0 && m_currentPath != L"THIS_PC") {
                D2D1_RECT_F rowR = GetItemRect(m_hoverIndex, otherContent);
                float opColX = otherContent.left + 6.0f + m_colWidths[0] + m_colWidths[1] + m_colWidths[2] + m_colWidths[3];
                D2D1_RECT_F trashBtn = D2D1::RectF(opColX + 6.0f, rowR.top + 2.0f, opColX + 62.0f, rowR.bottom - 2.0f);
                D2D1_RECT_F permBtn  = D2D1::RectF(opColX + 66.0f, rowR.top + 2.0f, opColX + 124.0f, rowR.bottom - 2.0f);
                if (PathUtils::PtInRectF(x, y, trashBtn)) m_hoverRowActionKind = 1;
                else if (PathUtils::PtInRectF(x, y, permBtn)) m_hoverRowActionKind = 2;
            }
            m_overrideMainRect = false;
            SwapWithOtherPane();
        } else {
            m_otherPane.hoverIndex = -1;
            m_otherPane.hoverRowActionKind = 0;
        }
    }

    int prevRibbonHover = m_hoverRibbonBtn;
    m_hoverRibbonBtn = 0;
    for (const auto& btn : m_ribbonHits) {
        if (PathUtils::PtInRectF(x, y, btn.rect)) {
            m_hoverRibbonBtn = btn.id;
            break;
        }
    }

    int prevBcHover = m_hoverBreadcrumbIndex;
    int prevBcArrowHover = m_hoverBreadcrumbArrowIndex;
    m_hoverBreadcrumbIndex = -1;
    m_hoverBreadcrumbArrowIndex = -1;
    for (int i = 0; i < static_cast<int>(m_breadcrumbs.size()); ++i) {
        if (PathUtils::PtInRectF(x, y, m_breadcrumbs[i].arrowRect)) {
            m_hoverBreadcrumbArrowIndex = i;
            break;
        }
        if (PathUtils::PtInRectF(x, y, m_breadcrumbs[i].hitRect)) {
            m_hoverBreadcrumbIndex = i;
            break;
        }
    }

    int prevTreeHover = m_hoverTreeIndex;
    m_hoverTreeIndex = -1;
    if (m_showNavPane && x < m_treePaneWidth) {
        for (int i = 0; i < static_cast<int>(m_treeNodes.size()); ++i) {
            if (PathUtils::PtInRectF(x, y, m_treeNodes[i].hitRect)) {
                m_hoverTreeIndex = i;
                break;
            }
        }
    }

    if (prevHover != m_hoverIndex ||
        prevOtherHover != m_otherPane.hoverIndex ||
        prevOtherRowAction != m_otherPane.hoverRowActionKind ||
        prevTreemapHover != m_hoverTreemapHitIndex ||
        prevPieHover != m_hoverPieHitIndex ||
        prevHwHover != m_hoverHwCardIndex ||
        prevRowAction != m_hoverRowActionKind ||
        prevRibbonHover != m_hoverRibbonBtn ||
        prevTreeHover != m_hoverTreeIndex ||
        prevBcHover != m_hoverBreadcrumbIndex ||
        prevBcArrowHover != m_hoverBreadcrumbArrowIndex ||
        (m_viewMode == ViewMode::Hardware) ||
        (GitDockVisible() && PathUtils::PtInRectF(x, y, GetGitDockRect())) ||
        (m_viewMode == ViewMode::Proportions && m_hoverTreemapHitIndex >= 0) ||
        (m_viewMode == ViewMode::PieChart && (m_hoverPieHitIndex >= 0 || m_pieCenterRadius > 0.0f))) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void ExplorerApp::OnMouseUp(float x, float y, bool rightButton, WPARAM keyFlags) {
    (void)x; (void)y; (void)rightButton; (void)keyFlags;
    bool contentUp = m_contentPane.OnMouseUp();
    if (m_draggingTreeSplitter || m_draggingGitSplitter || m_draggingSplitSplitter || m_draggingPreviewSplitter || m_resizingCol >= 0 || m_draggingScrollbar || m_viewerDraggingPan || m_isBoxSelecting || contentUp) {
        if (m_draggingSplitSplitter || m_draggingPreviewSplitter) SaveUiSettings();
        m_draggingTreeSplitter = false;
        m_draggingGitSplitter = false;
        m_draggingSplitSplitter = false;
        m_draggingPreviewSplitter = false;
        m_resizingCol = -1;
        m_draggingScrollbar = false;
        m_viewerDraggingPan = false;
        m_isBoxSelecting = false;
        ReleaseCapture();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else if (GetCapture() == m_hwnd) {
        ReleaseCapture();
    }
}

void ExplorerApp::OnMouseDoubleClick(float x, float y, WPARAM keyFlags) {
    (void)keyFlags;

    // Double-clicking a node in the Left Navigation Tree expands/collapses its subfolders!
    D2D1_RECT_F fullRect = GetFullWorkspaceRect();
    if (!m_isFullScreen && m_showNavPane && x < m_treePaneWidth && y >= fullRect.top && y <= fullRect.bottom) {
        for (int i = 0; i < static_cast<int>(m_treeNodes.size()); ++i) {
            if (PathUtils::PtInRectF(x, y, m_treeNodes[i].hitRect)) {
                ToggleTreeNodeExpand(i);
                return;
            }
        }
    }

    if (IsSplitViewActive()) {
        int otherIdx = 1 - m_activePane;
        if (PathUtils::PtInRectF(x, y, GetSplitPaneOuterRect(otherIdx))) {
            SetActivePane(otherIdx);
        }
    }

    D2D1_RECT_F mainRect = GetMainViewRect();

    if (m_viewMode == ViewMode::FullViewer) {
        float canvasBottom = m_isFullScreen ? mainRect.bottom : (mainRect.bottom - kFilmstripHeight);
        if (x >= mainRect.left && x <= mainRect.right && y >= mainRect.top && y < canvasBottom) {
            ToggleFullScreen();
            return;
        }
    }

    if (m_viewMode == ViewMode::Proportions) {
        for (int i = static_cast<int>(m_treemapHits.size()) - 1; i >= 0; --i) {
            if (PathUtils::PtInRectF(x, y, m_treemapHits[i].rect)) {
                const TreemapHitNode hitCopy = m_treemapHits[i];
                if (hitCopy.isDirectory && !hitCopy.fullPath.empty()) {
                    NavigateTo(hitCopy.fullPath, true);
                    return;
                }
                if (hitCopy.depth > 0 && !hitCopy.isDirectory && !hitCopy.fullPath.empty()) {
                    std::wstring parentDir = PathUtils::GetParentPath(hitCopy.fullPath);
                    if (!parentDir.empty()) {
                        NavigateTo(parentDir, true, PathUtils::GetFileName(hitCopy.fullPath));
                        return;
                    }
                }
                if (hitCopy.topItemIndex >= 0 && hitCopy.topItemIndex < static_cast<int>(m_items.size())) {
                    SelectSingle(hitCopy.topItemIndex, true);
                    ActivateFocusedOrSelectedItem();
                    return;
                }
            }
        }
        return;
    }

    if (m_viewMode == ViewMode::PieChart) {
        int sIdx = FindHoveredPieSectorIndex(x, y, m_pieHits, m_pieCenterPt);
        if (sIdx >= 0 && sIdx < static_cast<int>(m_pieHits.size())) {
            const PieSectorHitNode hitCopy = m_pieHits[sIdx];
            if (hitCopy.isDirectory && !hitCopy.fullPath.empty()) {
                NavigateTo(hitCopy.fullPath, true);
                return;
            }
            if (hitCopy.depth > 0 && !hitCopy.isDirectory && !hitCopy.fullPath.empty()) {
                std::wstring parentDir = PathUtils::GetParentPath(hitCopy.fullPath);
                if (!parentDir.empty()) {
                    NavigateTo(parentDir, true, PathUtils::GetFileName(hitCopy.fullPath));
                    return;
                }
            }
            if (hitCopy.topItemIndex >= 0 && hitCopy.topItemIndex < static_cast<int>(m_items.size())) {
                SelectSingle(hitCopy.topItemIndex, true);
                ActivateFocusedOrSelectedItem();
                return;
            }
        }
        return;
    }

    D2D1_RECT_F contentRect = GetContentItemsRect();
    bool onItem = false;
    int hitIdx = HitTestItem(x, y, contentRect, &onItem);
    if (hitIdx >= 0 && onItem) {
        SelectSingle(hitIdx, true);
        ActivateFocusedOrSelectedItem();
    }
}

void ExplorerApp::OnMouseWheel(short delta, float x, float y, WPARAM keyFlags) {
    bool ctrlHeld = (keyFlags & MK_CONTROL) != 0;
    int steps = delta / WHEEL_DELTA;
    if (steps == 0) steps = (delta > 0) ? 1 : -1;

    // If cursor is over the Right-Side File Content Pane (Hex / 3D), scroll or zoom it!
    if (PreviewPaneVisible() && m_contentPane.OnMouseWheel(delta, x, y)) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // If cursor is over the Left Navigation Tree, scroll the tree pane!
    D2D1_RECT_F fullRect = GetFullWorkspaceRect();
    D2D1_RECT_F mainRect = GetMainViewRect();
    if (GitDockVisible() && PathUtils::PtInRectF(x, y, GetGitDockRect())) {
        m_gitScrollY = std::clamp(m_gitScrollY - steps * 66.0f, 0.0f, m_gitMaxScrollY);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (!m_isFullScreen && m_showNavPane && x < m_treePaneWidth && y >= fullRect.top && y <= fullRect.bottom) {
        float maxTreeScroll = std::max(0.0f, static_cast<float>(m_treeNodes.size()) * 26.0f - (fullRect.bottom - fullRect.top) + 16.0f);
        m_treeScrollY = std::clamp(m_treeScrollY - steps * 52.0f, 0.0f, maxTreeScroll);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_viewMode == ViewMode::Hardware && !ctrlHeld) {
        m_hwScrollY = std::clamp(m_hwScrollY - steps * 88.0f, 0.0f, m_hwMaxScrollY);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // Ctrl + MouseWheel ALWAYS cycles between View Modes!
    if (ctrlHeld) {
        int cur = static_cast<int>(m_viewMode);
        int next = std::clamp(cur + (steps > 0 ? 1 : -1), 0, static_cast<int>(ViewMode::FullViewer));
        if (next != cur) {
            m_viewMode = static_cast<ViewMode>(next);
            if (m_viewMode != ViewMode::FullViewer && m_viewMode != ViewMode::Hardware) {
                m_lastNonViewerMode = m_viewMode;
            } else if (m_viewMode == ViewMode::FullViewer && m_focusedIndex < 0 && !m_items.empty()) {
                SelectSingle(0, true);
            } else if (m_viewMode == ViewMode::Hardware) {
                m_hwInspector.InitializeStaticInventory();
                EnsureWorkAnimTimerState();
            }
            if ((m_viewMode == ViewMode::Proportions || m_viewMode == ViewMode::PieChart) && m_currentPath != L"THIS_PC") {
                m_fileEngine.RequestFolderSizes(m_items, false);
                EnsureWorkAnimTimerState();
            }
            ClampScrollOffset();
            EnsureItemVisible(m_focusedIndex);
            m_imageCache.BumpViewportGeneration();
            SyncMediaPlayerToSelection();

            const Strings& s = GetStrings(m_lang);
            const wchar_t* modeNames[] = {
                s.viewDetails, s.viewList, s.viewSmall, s.viewMedium,
                s.viewLarge, s.viewExtraLarge, s.viewJumbo, s.viewProportions,
                s.viewPieChart, s.tabHardware, s.viewFullViewer
            };
            m_statusToast = std::wstring(m_lang == Lang::PL ? L"Widok: " : L"View: ") + modeNames[next];
            m_statusToastExpireTick = GetTickCount64() + 2200;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    if (m_viewMode == ViewMode::FullViewer) {
        if (!m_isFullScreen && y >= mainRect.bottom - kFilmstripHeight) {
            m_filmstripScrollX -= steps * 140.0f;
            float maxScrollX = std::max(0.0f, static_cast<float>(m_items.size()) * 74.0f - (mainRect.right - mainRect.left) + 20.0f);
            m_filmstripScrollX = std::clamp(m_filmstripScrollX, 0.0f, maxScrollX);
        } else {
            m_viewerActualPixels = false;
            float factor = (steps > 0) ? 1.2f : (1.0f / 1.2f);
            m_viewerZoom = std::clamp(m_viewerZoom * factor, 0.15f, 16.0f);
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    float scrollStep = (m_viewMode == ViewMode::Details || m_viewMode == ViewMode::List) ? (kDetailsRowHeight * 3.0f) : 96.0f;
    if (IsSplitViewActive() && PathUtils::PtInRectF(x, y, GetSplitPaneOuterRect(1 - m_activePane))) {
        int otherIdx = 1 - m_activePane;
        SwapWithOtherPane();
        m_overrideMainRect = true;
        m_customMainRect = GetSplitPaneContentMainRect(otherIdx);
        m_scrollY -= steps * scrollStep;
        ClampScrollOffset();
        m_overrideMainRect = false;
        SwapWithOtherPane();
        m_imageCache.BumpViewportGeneration();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    m_scrollY -= steps * scrollStep;
    ClampScrollOffset();
    m_imageCache.BumpViewportGeneration();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::OnKeyDown(WPARAM vk) {
    bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;

    if (vk == VK_TAB && !ctrl && !alt && IsSplitViewActive()) {
        SetActivePane(1 - m_activePane);
        return;
    }

    if (vk == VK_F11) {
        ToggleFullScreen();
        return;
    }
    if (vk == VK_ESCAPE) {
        if (m_isFullScreen) {
            ToggleFullScreen();
            return;
        }
        if (m_viewMode == ViewMode::FullViewer) {
            m_mediaPlayer.CloseMedia();
            KillTimer(m_hwnd, ID_TIMER_MEDIA);
            m_viewMode = m_lastNonViewerMode;
            EnsureItemVisible(m_focusedIndex);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        CancelInlineEdits();
        return;
    }

    if (alt) {
        if (vk == VK_LEFT) { NavigateBack(); return; }
        if (vk == VK_RIGHT) { NavigateForward(); return; }
        if (vk == VK_UP) { NavigateUp(); return; }
        if (vk == VK_RETURN) {
            auto paths = GetSelectedPaths();
            if (!paths.empty()) FileEngine::OpenFileOrProperties(m_hwnd, paths[0], true);
            return;
        }
        if (vk == 'D') {
            OnMouseDown(m_addressBarRect.left + 10.0f, m_addressBarRect.top + 5.0f, false, 0);
            return;
        }
    }

    if (ctrl) {
        if (vk == 'A') { SelectAll(); return; }
        if (vk == 'Z') { PerformUndoDelete(); return; }
        if (vk == 'C') {
            if (shift) CopySelectedPaths();
            else CopySelected(false);
            return;
        }
        if (vk == 'X') { CopySelected(true); return; }
        if (vk == 'V') { PasteIntoCurrent(); return; }
        if (vk == 'N' && shift) { CreateNewFolderAction(); return; }
        if (vk == 'L') {
            OnMouseDown(m_addressBarRect.left + 10.0f, m_addressBarRect.top + 5.0f, false, 0);
            return;
        }
        if (vk == 'F') {
            OnMouseDown(m_searchBarRect.left + 10.0f, m_searchBarRect.top + 5.0f, false, 0);
            return;
        }
        if (vk == 'R') { RefreshCurrentFolder(); return; }
    }

    if (vk == VK_F2) { StartInlineRename(); return; }
    if (vk == VK_F3) { OnMouseDown(m_searchBarRect.left + 10.0f, m_searchBarRect.top + 5.0f, false, 0); return; }
    if (vk == VK_F4) { OnMouseDown(m_addressBarRect.left + 10.0f, m_addressBarRect.top + 5.0f, false, 0); return; }
    if (vk == VK_F5) { RefreshCurrentFolder(); return; }
    if (vk == VK_BACK) { NavigateBack(); return; }
    if (vk == VK_DELETE) { DeleteSelectedItems(shift); return; }
    if (vk == VK_SPACE) { ToggleFullViewerMode(); return; }
    if (vk == VK_RETURN) { ActivateFocusedOrSelectedItem(); return; }

    if (m_viewMode == ViewMode::FullViewer) {
        if (vk == 'K' || vk == 'P') {
            if (m_mediaPlayer.HasMedia()) {
                m_mediaPlayer.TogglePlayPause();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return;
        }
        if (vk == '0' || vk == VK_NUMPAD0) { ResetViewerZoom(); return; }
        if (vk == '1' || vk == VK_NUMPAD1) { SetViewerZoom100(); return; }
        if (vk == VK_LEFT || vk == VK_UP || vk == VK_PRIOR) { StepViewerImage(-1); return; }
        if (vk == VK_RIGHT || vk == VK_DOWN || vk == VK_NEXT) { StepViewerImage(1); return; }
        if (vk == VK_HOME && !m_items.empty()) { SelectSingle(0, true); ResetViewerZoom(); EnsureItemVisible(0); InvalidateRect(m_hwnd, nullptr, FALSE); return; }
        if (vk == VK_END && !m_items.empty()) { int last = static_cast<int>(m_items.size()) - 1; SelectSingle(last, true); ResetViewerZoom(); EnsureItemVisible(last); InvalidateRect(m_hwnd, nullptr, FALSE); return; }
        return;
    }

    if (m_items.empty()) return;

    D2D1_RECT_F contentRect = GetContentItemsRect();
    float cellW = 0, cellH = 0, iconSize = 0;
    int cols = 1;
    if (m_viewMode != ViewMode::Details) {
        GetGridMetrics(contentRect, cellW, cellH, iconSize, cols);
    }

    int cur = (m_focusedIndex >= 0) ? m_focusedIndex : 0;
    int target = cur;

    switch (vk) {
    case VK_UP:    target = cur - cols; break;
    case VK_DOWN:  target = cur + cols; break;
    case VK_LEFT:  target = cur - 1; break;
    case VK_RIGHT: target = cur + 1; break;
    case VK_HOME:  target = 0; break;
    case VK_END:   target = static_cast<int>(m_items.size()) - 1; break;
    case VK_PRIOR: {
        int pageRows = std::max(1, static_cast<int>((contentRect.bottom - contentRect.top) / (m_viewMode == ViewMode::Details ? kDetailsRowHeight : cellH)));
        target = cur - pageRows * cols;
        break;
    }
    case VK_NEXT: {
        int pageRows = std::max(1, static_cast<int>((contentRect.bottom - contentRect.top) / (m_viewMode == ViewMode::Details ? kDetailsRowHeight : cellH)));
        target = cur + pageRows * cols;
        break;
    }
    default:
        return;
    }

    target = std::clamp(target, 0, static_cast<int>(m_items.size()) - 1);
    if (shift) {
        if (m_anchorIndex < 0) m_anchorIndex = cur;
        SelectRange(m_anchorIndex, target, false);
    } else if (ctrl) {
        m_focusedIndex = target;
    } else {
        SelectSingle(target, true);
    }
    EnsureItemVisible(target);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::OnChar(wchar_t ch) {
    if (ch < 32 || m_viewMode == ViewMode::FullViewer) return;
    if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) return;

    ULONGLONG now = GetTickCount64();
    if (now - m_lastTypeAheadTick > 900) {
        m_typeAheadBuf.clear();
    }
    m_lastTypeAheadTick = now;
    m_typeAheadBuf.push_back(static_cast<wchar_t>(towlower(ch)));

    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        std::wstring lowName = PathUtils::ToLower(m_items[i].name);
        if (lowName.rfind(m_typeAheadBuf, 0) == 0) {
            SelectSingle(i, true);
            EnsureItemVisible(i);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            break;
        }
    }
}

void ExplorerApp::ExecuteRibbonCommand(int cmdId) {
    switch (cmdId) {
    case CMD_TAB_FILE:
        m_activeRibbonTab = RibbonTab::File;
        m_ribbonCollapsed = false;
        break;
    case CMD_TAB_HOME:
        m_activeRibbonTab = RibbonTab::Home;
        m_ribbonCollapsed = false;
        if (m_viewMode == ViewMode::Hardware) m_viewMode = m_lastNonViewerMode;
        break;
    case CMD_TAB_SHARE:
        m_activeRibbonTab = RibbonTab::Share;
        m_ribbonCollapsed = false;
        if (m_viewMode == ViewMode::Hardware) m_viewMode = m_lastNonViewerMode;
        break;
    case CMD_TAB_VIEW:
        m_activeRibbonTab = RibbonTab::View;
        m_ribbonCollapsed = false;
        if (m_viewMode == ViewMode::Hardware) m_viewMode = m_lastNonViewerMode;
        break;
    case CMD_TAB_SEARCH:
        m_activeRibbonTab = RibbonTab::Search;
        m_ribbonCollapsed = false;
        if (m_viewMode == ViewMode::Hardware) m_viewMode = m_lastNonViewerMode;
        break;
    case CMD_TAB_HARDWARE:
        m_activeRibbonTab = RibbonTab::Hardware;
        m_ribbonCollapsed = false;
        m_hwInspector.InitializeStaticInventory();
        m_viewMode = ViewMode::Hardware;
        EnsureWorkAnimTimerState();
        break;
    case CMD_TOGGLE_LANG:
        m_lang = (m_lang == Lang::PL) ? Lang::EN : Lang::PL;
        RebuildTreeNodes();
        RefreshCurrentFolder();
        break;
    case CMD_TOGGLE_RIBBON:
        m_ribbonCollapsed = !m_ribbonCollapsed;
        ClampScrollOffset();
        break;

    case CMD_PIN_QUICK: {
        auto paths = GetSelectedPaths();
        if (!paths.empty() && (GetFileAttributesW(paths[0].c_str()) & FILE_ATTRIBUTE_DIRECTORY)) {
            PinFolderToQuickAccess(paths[0]);
        } else if (!m_currentPath.empty() && m_currentPath != L"THIS_PC") {
            PinFolderToQuickAccess(m_currentPath);
        }
        break;
    }
    case CMD_COPY:          CopySelected(false); break;
    case CMD_CUT:           CopySelected(true); break;
    case CMD_PASTE:         PasteIntoCurrent(); break;
    case CMD_COPY_PATH:     CopySelectedPaths(); break;
    case CMD_PASTE_SHORTCUT: {
        if (OpenClipboard(m_hwnd)) {
            HDROP hDrop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
            if (hDrop) {
                UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < count; ++i) {
                    UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
                    std::wstring src(len, L'\0');
                    DragQueryFileW(hDrop, i, src.data(), len + 1);
                    std::wstring bn = PathUtils::GetFileName(src);
                    std::wstring lnk = PathUtils::EnsureTrailingSlash(m_currentPath) + bn + (m_lang == Lang::PL ? L" - skrót.lnk" : L" - Shortcut.lnk");
                    ShellMenuEmulator::CreateShortcutFile(src, lnk);
                }
            }
            CloseClipboard();
            RefreshCurrentFolder();
        }
        break;
    }
    case CMD_MOVE_TO:
    case CMD_COPY_TO: {
        auto paths = GetSelectedPaths();
        if (paths.empty()) break;
        wchar_t displayName[MAX_PATH] = {};
        BROWSEINFOW bi = {};
        bi.hwndOwner = m_hwnd;
        bi.pszDisplayName = displayName;
        bi.lpszTitle = (cmdId == CMD_MOVE_TO)
            ? (m_lang == Lang::PL ? L"Wybierz folder docelowy (Przenieś do):" : L"Select destination folder (Move to):")
            : (m_lang == Lang::PL ? L"Wybierz folder docelowy (Kopiuj do):" : L"Select destination folder (Copy to):");
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
            wchar_t destFolder[MAX_PATH] = {};
            if (SHGetPathFromIDListW(pidl, destFolder) && destFolder[0]) {
                std::wstring fromDoubleNull;
                for (const auto& p : paths) {
                    fromDoubleNull += p;
                    fromDoubleNull.push_back(L'\0');
                }
                fromDoubleNull.push_back(L'\0');
                std::wstring toDoubleNull = destFolder;
                toDoubleNull.push_back(L'\0');
                toDoubleNull.push_back(L'\0');

                SHFILEOPSTRUCTW fop = {};
                fop.hwnd = m_hwnd;
                fop.wFunc = (cmdId == CMD_MOVE_TO) ? FO_MOVE : FO_COPY;
                fop.pFrom = fromDoubleNull.c_str();
                fop.pTo   = toDoubleNull.c_str();
                fop.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR;
                SHFileOperationW(&fop);
                RefreshCurrentFolder();
            }
            CoTaskMemFree(pidl);
        }
        break;
    }
    case CMD_DELETE_MAIN:   DeleteSelectedItems(false); break; // Kosz
    case CMD_DELETE_PERM:   DeleteSelectedItems(true);  break; // Usuń (trwale)
    case CMD_DELETE_DROPDOWN: {
        for (const auto& btn : m_ribbonHits) {
            if (btn.id == CMD_DELETE_DROPDOWN) {
                POINT pt = { static_cast<LONG>(btn.rect.left), static_cast<LONG>(btn.rect.bottom) };
                ClientToScreen(m_hwnd, &pt);
                ShowDeleteDropdownMenu(pt.x, pt.y);
                return;
            }
        }
        break;
    }
    case CMD_RENAME:        StartInlineRename(); break;
    case CMD_NEW_FOLDER:    CreateNewFolderAction(); break;
    case CMD_NEW_ITEM: {
        if (m_currentPath.empty() || m_currentPath == L"THIS_PC") break;
        std::wstring created;
        std::wstring base = (m_lang == Lang::PL) ? L"Nowy dokument tekstowy" : L"New Text Document";
        if (ShellMenuEmulator::CreateNewEmptyFile(m_currentPath, base, L".txt", created)) {
            RefreshCurrentFolder(created);
            StartInlineRename();
        }
        break;
    }
    case CMD_PROPERTIES: {
        auto paths = GetSelectedPaths();
        if (!paths.empty()) FileEngine::OpenFileOrProperties(m_hwnd, paths[0], true);
        else if (!m_currentPath.empty() && m_currentPath != L"THIS_PC") FileEngine::OpenFileOrProperties(m_hwnd, m_currentPath, true);
        break;
    }
    case CMD_OPEN:          ActivateFocusedOrSelectedItem(); break;
    case CMD_EDIT: {
        auto paths = GetSelectedPaths();
        if (!paths.empty()) {
            std::wstring qArg = L"\"" + paths[0] + L"\"";
            ShellExecuteW(m_hwnd, L"open", L"notepad.exe", qArg.c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    }
    case CMD_SELECT_ALL:    SelectAll(); break;
    case CMD_SELECT_NONE:   SelectNone(); break;
    case CMD_INVERT_SEL:    InvertSelection(); break;

    case CMD_TOGGLE_NAVPANE:     m_showNavPane = !m_showNavPane; break;
    case CMD_TOGGLE_PREVIEWPANE: {
        m_showPreviewPane = !m_showPreviewPane;
        if (m_showPreviewPane && (m_viewMode == ViewMode::Hardware || m_viewMode == ViewMode::FullViewer)) {
            m_viewMode = m_lastNonViewerMode;
        }
        SyncMediaPlayerToSelection();
        ClampScrollOffset();
        SaveUiSettings();
        const bool pl = (m_lang == Lang::PL);
        m_statusToast = m_showPreviewPane
            ? (pl ? L"\x25E8 Włączono prawy panel zawartości pliku (Notatnik / Hex / 3D / Podgląd)" : L"\x25E8 Enabled Right File Content Pane (Notepad / Hex / 3D / Preview)")
            : (pl ? L"\x25A1 Ukryto prawy panel zawartości pliku" : L"\x25A1 Hidden Right File Content Pane");
        m_statusToastExpireTick = GetTickCount64() + 3200;
        EnsureWorkAnimTimerState();
        break;
    }
    case CMD_TOGGLE_SPLIT_VIEW:  ToggleSplitView(); break;
    case CMD_SPLIT_SWAP_PANES:
        if (!IsSplitViewActive()) {
            ToggleSplitView();
        } else {
            SwapWithOtherPane();
            RebuildBreadcrumbs();
            SyncGitRepo();
            SyncMediaPlayerToSelection();
            ClampScrollOffset();
        }
        break;
    case CMD_SPLIT_COPY_OTHER:   CopyOrMoveToOtherPane(false); break;
    case CMD_SPLIT_MOVE_OTHER:   CopyOrMoveToOtherPane(true);  break;
    case CMD_SPLIT_SYNC_OTHER:
        if (IsSplitViewActive() && !m_currentPath.empty()) {
            std::wstring targetPath = m_currentPath;
            int prevActive = m_activePane;
            SwapWithOtherPane();
            m_activePane = 1 - prevActive;
            NavigateTo(targetPath, true);
            SwapWithOtherPane();
            m_activePane = prevActive;
            RebuildBreadcrumbs();
            SyncGitRepo();
        }
        break;
    case CMD_PANE0_BACK:        SetActivePane(0); NavigateBack(); break;
    case CMD_PANE0_FWD:         SetActivePane(0); NavigateForward(); break;
    case CMD_PANE0_UP:          SetActivePane(0); NavigateUp(); break;
    case CMD_PANE0_REFRESH:     SetActivePane(0); RefreshCurrentFolder(); break;
    case CMD_PANE0_RECENT_DIRS: ShowRecentDirsDropdown(CMD_PANE0_RECENT_DIRS); return;
    case CMD_PANE0_ADDR:        BeginPaneAddressEdit(0); return;
    case CMD_PANE0_COPY_OTHER:  SetActivePane(0); CopyOrMoveToOtherPane(false); break;
    case CMD_PANE0_MOVE_OTHER:  SetActivePane(0); CopyOrMoveToOtherPane(true);  break;
    case CMD_PANE1_BACK:        SetActivePane(1); NavigateBack(); break;
    case CMD_PANE1_FWD:         SetActivePane(1); NavigateForward(); break;
    case CMD_PANE1_UP:          SetActivePane(1); NavigateUp(); break;
    case CMD_PANE1_REFRESH:     SetActivePane(1); RefreshCurrentFolder(); break;
    case CMD_PANE1_RECENT_DIRS: ShowRecentDirsDropdown(CMD_PANE1_RECENT_DIRS); return;
    case CMD_PANE1_ADDR:        BeginPaneAddressEdit(1); return;
    case CMD_PANE1_COPY_OTHER:  SetActivePane(1); CopyOrMoveToOtherPane(false); break;
    case CMD_PANE1_MOVE_OTHER:  SetActivePane(1); CopyOrMoveToOtherPane(true);  break;
    case CMD_VIEW_JUMBO:         m_viewMode = m_lastNonViewerMode = ViewMode::JumboIcons; ClampScrollOffset(); SyncMediaPlayerToSelection(); break;
    case CMD_VIEW_EXTRALARGE:    m_viewMode = m_lastNonViewerMode = ViewMode::ExtraLargeIcons; ClampScrollOffset(); SyncMediaPlayerToSelection(); break;
    case CMD_VIEW_LARGE:         m_viewMode = m_lastNonViewerMode = ViewMode::LargeIcons; ClampScrollOffset(); SyncMediaPlayerToSelection(); break;
    case CMD_VIEW_MEDIUM:        m_viewMode = m_lastNonViewerMode = ViewMode::MediumIcons; ClampScrollOffset(); SyncMediaPlayerToSelection(); break;
    case CMD_VIEW_SMALL:         m_viewMode = m_lastNonViewerMode = ViewMode::SmallIcons; ClampScrollOffset(); SyncMediaPlayerToSelection(); break;
    case CMD_VIEW_LIST:          m_viewMode = m_lastNonViewerMode = ViewMode::List; ClampScrollOffset(); SyncMediaPlayerToSelection(); break;
    case CMD_VIEW_DETAILS:       m_viewMode = m_lastNonViewerMode = ViewMode::Details; ClampScrollOffset(); SyncMediaPlayerToSelection(); break;
    case CMD_VIEW_PROPORTIONS:
        m_viewMode = m_lastNonViewerMode = ViewMode::Proportions;
        m_scrollY = 0.0f;
        if (m_currentPath != L"THIS_PC") {
            m_fileEngine.RequestFolderSizes(m_items, false);
            EnsureWorkAnimTimerState();
        }
        SyncMediaPlayerToSelection();
        break;
    case CMD_VIEW_PIECHART:
        m_viewMode = m_lastNonViewerMode = ViewMode::PieChart;
        m_scrollY = 0.0f;
        if (m_currentPath != L"THIS_PC") {
            m_fileEngine.RequestFolderSizes(m_items, false);
            EnsureWorkAnimTimerState();
        }
        SyncMediaPlayerToSelection();
        break;
    case CMD_VIEW_FULLVIEWER:    ToggleFullViewerMode(); break;
    case CMD_VIEW_FULLSCREEN:    ToggleFullScreen(); break;
    case CMD_TOGGLE_EXT:         m_showExtensions = !m_showExtensions; RefreshCurrentFolder(); break;
    case CMD_TOGGLE_HIDDEN:      m_showHidden = !m_showHidden; RefreshCurrentFolder(); break;
    case CMD_TOGGLE_GIT:
        m_gitEnabled = !m_gitEnabled;
        SaveUiSettings();
        SyncGitRepo(true);
        ClampScrollOffset();
        break;

    case CMD_NAV_BACK:          NavigateBack(); break;
    case CMD_NAV_FORWARD:       NavigateForward(); break;
    case CMD_NAV_UP:            NavigateUp(); break;
    case CMD_NAV_REFRESH:       RefreshCurrentFolder(); break;
    case CMD_NAV_RECENT_DIRS:   ShowRecentDirsDropdown(CMD_NAV_RECENT_DIRS); return;
    case CMD_CALC_FOLDER_SIZES: TriggerFolderSizeCalculation(); break;
    case CMD_CLEAR_SEARCH:      ClearSearchAndRestoreFolder(); break;
    case CMD_SEARCH_TRIGGER_BTN: TriggerAsyncSearch(); break;
    case CMD_SEARCH_ADVANCED_GEAR:
        m_activeRibbonTab = RibbonTab::Search;
        m_ribbonCollapsed = false;
        break;

    // Advanced Search ("Wyszukiwanie") Ribbon Commands
    case CMD_SEARCH_START:
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_STOP:
        m_fileEngine.CancelAsyncSearch();
        m_searchProgress.isRunning = false;
        m_searchProgress.isFinished = true;
        EnsureWorkAnimTimerState();
        break;
    case CMD_SEARCH_RESET_FILTERS:
        m_searchOpts = SearchOptions{};
        if (m_filterText.empty()) {
            ClearSearchAndRestoreFolder();
        } else {
            TriggerAsyncSearch();
        }
        break;
    case CMD_SEARCH_CHK_RECURSIVE:
        m_searchOpts.recursive = !m_searchOpts.recursive;
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_CHK_FILES:
        m_searchOpts.includeFiles = !m_searchOpts.includeFiles;
        if (!m_searchOpts.includeFiles && !m_searchOpts.includeFolders) m_searchOpts.includeFolders = true;
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_CHK_FOLDERS:
        m_searchOpts.includeFolders = !m_searchOpts.includeFolders;
        if (!m_searchOpts.includeFiles && !m_searchOpts.includeFolders) m_searchOpts.includeFiles = true;
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_CHK_CASESENS:
        m_searchOpts.caseSensitive = !m_searchOpts.caseSensitive;
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_CHK_CONTENT:
        m_searchOpts.searchContent = !m_searchOpts.searchContent;
        TriggerAsyncSearch();
        break;

    case CMD_SEARCH_CHK_DATE:
        m_searchOpts.useDateFilter = !m_searchOpts.useDateFilter;
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_EDIT_DATE_FROM:
    case CMD_SEARCH_EDIT_DATE_TO:
    case CMD_SEARCH_EDIT_SIZE_MIN:
    case CMD_SEARCH_EDIT_SIZE_MAX:
    case CMD_SEARCH_EDIT_CUSTOM_EXT:
        for (const auto& btn : m_ribbonHits) {
            if (btn.id == cmdId) {
                BeginSearchFieldEdit(cmdId, btn.rect);
                return;
            }
        }
        break;
    case CMD_SEARCH_PRESET_TODAY:
    case CMD_SEARCH_PRESET_7DAYS:
    case CMD_SEARCH_PRESET_30DAYS:
    case CMD_SEARCH_PRESET_YEAR: {
        SYSTEMTIME stNow = {};
        GetLocalTime(&stNow);
        wchar_t toBuf[32] = {};
        swprintf_s(toBuf, L"%04d-%02d-%02d", stNow.wYear, stNow.wMonth, stNow.wDay);
        m_searchOpts.dateToStr = toBuf;

        if (cmdId == CMD_SEARCH_PRESET_TODAY) {
            m_searchOpts.dateFromStr = toBuf;
        } else if (cmdId == CMD_SEARCH_PRESET_YEAR) {
            wchar_t yrBuf[32] = {};
            swprintf_s(yrBuf, L"%04d-01-01", stNow.wYear);
            m_searchOpts.dateFromStr = yrBuf;
        } else {
            FILETIME ft = {};
            SystemTimeToFileTime(&stNow, &ft);
            ULARGE_INTEGER ui = {};
            ui.LowPart = ft.dwLowDateTime;
            ui.HighPart = ft.dwHighDateTime;
            uint64_t days = (cmdId == CMD_SEARCH_PRESET_7DAYS) ? 7ULL : 30ULL;
            ui.QuadPart -= days * 24ULL * 3600ULL * 10000000ULL;
            ft.dwLowDateTime = ui.LowPart;
            ft.dwHighDateTime = ui.HighPart;
            SYSTEMTIME stFrom = {};
            FileTimeToSystemTime(&ft, &stFrom);
            wchar_t frBuf[32] = {};
            swprintf_s(frBuf, L"%04d-%02d-%02d", stFrom.wYear, stFrom.wMonth, stFrom.wDay);
            m_searchOpts.dateFromStr = frBuf;
        }
        m_searchOpts.useDateFilter = true;
        TriggerAsyncSearch();
        break;
    }

    case CMD_SEARCH_CHK_SIZE:
        m_searchOpts.useSizeFilter = !m_searchOpts.useSizeFilter;
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_CYCLE_SIZE_UNIT: {
        int u = (static_cast<int>(m_searchOpts.sizeUnit) + 1) % 4;
        m_searchOpts.sizeUnit = static_cast<SearchSizeUnit>(u);
        if (m_searchOpts.useSizeFilter) TriggerAsyncSearch();
        break;
    }
    case CMD_SEARCH_PRESET_SMALL:
        m_searchOpts.useSizeFilter = true;
        m_searchOpts.sizeUnit = SearchSizeUnit::MB;
        m_searchOpts.sizeMinStr = L"0";
        m_searchOpts.sizeMaxStr = L"1";
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_PRESET_GT10MB:
        m_searchOpts.useSizeFilter = true;
        m_searchOpts.sizeUnit = SearchSizeUnit::MB;
        m_searchOpts.sizeMinStr = L"10";
        m_searchOpts.sizeMaxStr = L"";
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_PRESET_GT100MB:
        m_searchOpts.useSizeFilter = true;
        m_searchOpts.sizeUnit = SearchSizeUnit::MB;
        m_searchOpts.sizeMinStr = L"100";
        m_searchOpts.sizeMaxStr = L"";
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_PRESET_GT1GB:
        m_searchOpts.useSizeFilter = true;
        m_searchOpts.sizeUnit = SearchSizeUnit::GB;
        m_searchOpts.sizeMinStr = L"1";
        m_searchOpts.sizeMaxStr = L"";
        TriggerAsyncSearch();
        break;

    case CMD_SEARCH_CHK_TYPE:
        m_searchOpts.useTypeFilter = !m_searchOpts.useTypeFilter;
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_CAT_IMAGES:
        m_searchOpts.categoryMask ^= SEARCH_CAT_IMAGES;
        m_searchOpts.useTypeFilter = (m_searchOpts.categoryMask != 0 || !m_searchOpts.customExtList.empty());
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_CAT_MEDIA:
        m_searchOpts.categoryMask ^= SEARCH_CAT_MEDIA;
        m_searchOpts.useTypeFilter = (m_searchOpts.categoryMask != 0 || !m_searchOpts.customExtList.empty());
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_CAT_CODE:
        m_searchOpts.categoryMask ^= SEARCH_CAT_CODE;
        m_searchOpts.useTypeFilter = (m_searchOpts.categoryMask != 0 || !m_searchOpts.customExtList.empty());
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_CAT_ARCHIVES:
        m_searchOpts.categoryMask ^= SEARCH_CAT_ARCHIVES;
        m_searchOpts.useTypeFilter = (m_searchOpts.categoryMask != 0 || !m_searchOpts.customExtList.empty());
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_CAT_MODELS:
        m_searchOpts.categoryMask ^= SEARCH_CAT_MODELS;
        m_searchOpts.useTypeFilter = (m_searchOpts.categoryMask != 0 || !m_searchOpts.customExtList.empty());
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_CAT_APPS:
        m_searchOpts.categoryMask ^= SEARCH_CAT_APPS;
        m_searchOpts.useTypeFilter = (m_searchOpts.categoryMask != 0 || !m_searchOpts.customExtList.empty());
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_CAT_DOCS:
        m_searchOpts.categoryMask ^= SEARCH_CAT_DOCS;
        m_searchOpts.useTypeFilter = (m_searchOpts.categoryMask != 0 || !m_searchOpts.customExtList.empty());
        TriggerAsyncSearch();
        break;
    case CMD_SEARCH_TYPE_LIST_MENU:
        for (const auto& btn : m_ribbonHits) {
            if (btn.id == CMD_SEARCH_TYPE_LIST_MENU) {
                POINT pt = { static_cast<LONG>(btn.rect.left), static_cast<LONG>(btn.rect.bottom) };
                ClientToScreen(m_hwnd, &pt);
                ShowSearchTypeListMenu(pt.x, pt.y);
                return;
            }
        }
        break;

    case CMD_VIEWER_PREV:        StepViewerImage(-1); break;
    case CMD_VIEWER_NEXT:        StepViewerImage(1); break;
    case CMD_VIEWER_DELETE:      DeleteSelectedItems(false); break;
    case CMD_VIEWER_PERM_DELETE: DeleteSelectedItems(true); break;
    case CMD_VIEWER_ZOOM_FIT:    ResetViewerZoom(); break;
    case CMD_VIEWER_ZOOM_100:    SetViewerZoom100(); break;
    case CMD_VIEWER_FULLSCREEN:  ToggleFullScreen(); break;
    case CMD_VIEWER_CLOSE:
        if (m_isFullScreen) ToggleFullScreen();
        m_mediaPlayer.CloseMedia();
        KillTimer(m_hwnd, ID_TIMER_MEDIA);
        m_viewMode = m_lastNonViewerMode;
        EnsureItemVisible(m_focusedIndex);
        break;
    case CMD_MEDIA_PLAY_PAUSE:
        m_mediaPlayer.TogglePlayPause();
        break;

    // Share ("Udostępnianie") & File ("Plik") Ribbon Commands
    case CMD_SHARE_ZIP: {
        auto paths = GetSelectedPaths();
        if (paths.empty()) {
            m_statusToast = m_lang == Lang::PL ? L"Zaznacz plik lub folder, aby skompresować do .zip" : L"Select a file or folder to compress to .zip";
            m_statusToastExpireTick = GetTickCount64() + 2600;
            break;
        }
        if (m_shellEmulator.Has7Zip()) {
            m_shellEmulator.Execute7ZipAction(SZ_ADD_ZIP, paths, m_currentPath);
        } else {
            std::wstring firstName = PathUtils::GetFileName(paths[0]);
            size_t dotPos = firstName.find_last_of(L'.');
            if (dotPos != std::wstring::npos && dotPos > 0) firstName = firstName.substr(0, dotPos);
            std::wstring zipOut = PathUtils::EnsureTrailingSlash(m_currentPath) + firstName + L".zip";
            std::wstring args = L"-a -c -f \"" + zipOut + L"\"";
            for (const auto& p : paths) {
                args += L" \"" + PathUtils::GetFileName(p) + L"\"";
            }
            ShellExecuteW(m_hwnd, L"open", L"tar.exe", args.c_str(), m_currentPath.c_str(), SW_HIDE);
            m_statusToast = (m_lang == Lang::PL ? L"Tworzenie archiwum ZIP: " : L"Creating ZIP archive: ") + firstName + L".zip";
            m_statusToastExpireTick = GetTickCount64() + 3000;
        }
        break;
    }
    case CMD_SHARE_EXTRACT_ZIP: {
        auto paths = GetSelectedPaths();
        if (paths.empty()) break;
        if (m_shellEmulator.Has7Zip()) {
            m_shellEmulator.Execute7ZipAction(SZ_EXTRACT_SUBFOLDER, paths, m_currentPath);
        } else {
            std::wstring args = L"-x -f \"" + paths[0] + L"\"";
            ShellExecuteW(m_hwnd, L"open", L"tar.exe", args.c_str(), m_currentPath.c_str(), SW_SHOWNORMAL);
        }
        break;
    }
    case CMD_SHARE_EMAIL: {
        auto paths = GetSelectedPaths();
        std::wstring subj = L"Udostepnione pliki";
        std::wstring body;
        for (const auto& p : paths) {
            if (!body.empty()) body += L"%0D%0A";
            body += p;
        }
        if (body.empty()) body = m_currentPath;
        std::wstring mailUrl = L"mailto:?subject=" + subj + L"&body=" + body;
        ShellExecuteW(m_hwnd, L"open", mailUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    case CMD_SHARE_PRINT: {
        auto paths = GetSelectedPaths();
        if (!paths.empty()) {
            ShellExecuteW(m_hwnd, L"print", paths[0].c_str(), nullptr, m_currentPath.c_str(), SW_SHOWNORMAL);
        }
        break;
    }
    case CMD_SHARE_COPY_NAME: {
        auto paths = GetSelectedPaths();
        std::wstring names;
        for (const auto& p : paths) {
            if (!names.empty()) names += L"\r\n";
            names += PathUtils::GetFileName(p);
        }
        if (names.empty() && !m_currentPath.empty()) names = PathUtils::GetFileName(m_currentPath);
        if (!names.empty() && OpenClipboard(m_hwnd)) {
            EmptyClipboard();
            size_t bytes = (names.size() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hMem) {
                void* ptr = GlobalLock(hMem);
                if (ptr) {
                    std::memcpy(ptr, names.c_str(), bytes);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
            }
            CloseClipboard();
            m_statusToast = (m_lang == Lang::PL ? L"\x2713 Skopiowano nazwę: " : L"\x2713 Copied name: ") + names;
            m_statusToastExpireTick = GetTickCount64() + 2500;
        }
        break;
    }
    case CMD_SHARE_WIN_EXPLORER: {
        auto paths = GetSelectedPaths();
        if (!paths.empty()) FileEngine::OpenInClassicExplorer(paths[0], true);
        else FileEngine::OpenInClassicExplorer(m_currentPath, false);
        break;
    }
    case CMD_SHARE_POWERSHELL:
        FileEngine::OpenTerminalInFolder(m_currentPath);
        break;
    case CMD_SHARE_CMD: {
        std::wstring dir = (m_currentPath.empty() || m_currentPath == L"THIS_PC") ? L"C:\\" : m_currentPath;
        ShellExecuteW(m_hwnd, L"open", L"cmd.exe", nullptr, dir.c_str(), SW_SHOWNORMAL);
        break;
    }
    case CMD_SHARE_MAP_DRIVE:
        WNetConnectionDialog(m_hwnd, RESOURCETYPE_DISK);
        RebuildTreeNodes();
        RefreshCurrentFolder();
        break;
    case CMD_SHARE_DISCONNECT_DRIVE:
        WNetDisconnectDialog(m_hwnd, RESOURCETYPE_DISK);
        RebuildTreeNodes();
        RefreshCurrentFolder();
        break;
    case CMD_FILE_NEW_WINDOW: {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring arg = (m_currentPath.empty() || m_currentPath == L"THIS_PC") ? L"" : (L"\"" + m_currentPath + L"\"");
        ShellExecuteW(m_hwnd, L"open", exePath, arg.empty() ? nullptr : arg.c_str(), nullptr, SW_SHOWNORMAL);
        break;
    }
    case CMD_FILE_EXIT:
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        return;

    // Hardware Pro Mode ("Sprzęt") Ribbon & Dashboard Commands
    case CMD_HW_SUB_ALL:
    case CMD_HW_SUB_CPU:
    case CMD_HW_SUB_GPU:
    case CMD_HW_SUB_RAM:
    case CMD_HW_SUB_MOBO:
    case CMD_HW_SUB_DISKS:
    case CMD_HW_SUB_NET:
    case CMD_HW_SUB_SYSTEM:
    case CMD_HW_SUB_THERMAL:
        m_hwSubTab = (cmdId == CMD_HW_SUB_THERMAL) ? HardwareSubTab::Thermal
                                                  : static_cast<HardwareSubTab>(cmdId - CMD_HW_SUB_ALL);
        m_hwScrollY = 0.0f;
        m_viewMode = ViewMode::Hardware;
        m_hwInspector.InitializeStaticInventory();
        EnsureWorkAnimTimerState();
        break;
    case CMD_HW_REFRESH:
        m_hwInspector.RefreshFullInventory(false);
        EnsureWorkAnimTimerState();
        m_statusToast = m_lang == Lang::PL ? L"\x21BB Odświeżanie diagnostyki sprzętowej PC w tle..." : L"\x21BB Refreshing PC hardware diagnostics in background...";
        m_statusToastExpireTick = GetTickCount64() + 2500;
        break;
    case CMD_HW_REFRESH_SMART:
        if (m_hwSelectedDisk >= 0) {
            m_hwInspector.RequestAsyncRefresh(false, false, m_hwSelectedDisk);
            EnsureWorkAnimTimerState();
        }
        break;
    case CMD_HW_SELECT_DISK:
        m_hwSelectedDisk = m_hwActionIndex;
        m_hwInspector.RequestAsyncRefresh(false, false, m_hwActionIndex);
        EnsureWorkAnimTimerState();
        break;
    case CMD_HW_NET_SETTINGS:
        HardwareInspector::OpenNetworkAdapterProperties(m_hwnd, m_hwActionArg);
        break;

    // "Monitoring" tab: background recorder (single shared process) + history view
    case CMD_TAB_MONITORING:
        m_activeRibbonTab = RibbonTab::Monitoring;
        m_ribbonCollapsed = false;
        m_viewMode = ViewMode::Hardware;   // hosted by the card-hit dashboard view
        m_hwScrollY = 0.0f;
        if (m_monLoadTick == 0 || GetTickCount64() - m_monLoadTick > 3000) {
            m_monForceReload = true;
        }
        m_monZoomChart = -1;
        m_hwInspector.EnsureDiskModelsLoaded();   // fast kernel-cached drive names / types for per-drive graphs (< 0.2 ms)
        SetTimer(m_hwnd, ID_TIMER_MONITOR, 5000, nullptr);
        EnsureWorkAnimTimerState();
        break;
    case CMD_MON_TOGGLE: {
        MonitorSettings ms = MonitorStore::LoadSettings();
        if (ms.enabled || MonitorDaemon::IsRunning()) {
            ms.enabled = false;
            MonitorStore::SaveSettings(ms);
            MonitorDaemon::RequestStop();
            AppLog::Info(L"Monitoring", L"Background recording stopped by the user");
            m_statusToast = m_lang == Lang::PL ? L"\x23F9 Zatrzymano nagrywanie w tle" : L"\x23F9 Background recording stopped";
        } else {
            ms.enabled = true;
            MonitorStore::SaveSettings(ms);
            const bool started = MonitorDaemon::Start(ms.elevated);
            if (started) AppLog::Ok(L"Monitoring", std::wstring(L"Background recording started") + (ms.elevated ? L" (administrator)" : L""));
            else AppLog::Error(L"Monitoring", L"Could not start the background recorder (UAC cancelled?)");
            if (!started) {
                m_statusToast = m_lang == Lang::PL ? L"Nie udało się uruchomić rejestratora (anulowano UAC?)" : L"Could not start the recorder (UAC cancelled?)";
            } else {
                m_statusToast = m_lang == Lang::PL ? L"\x23FA Uruchomiono nagrywanie w tle (jeden proces dla wszystkich okien)"
                                                   : L"\x23FA Background recording started (one process for all windows)";
            }
        }
        m_statusToastExpireTick = GetTickCount64() + 3500;
        m_monForceReload = true;
        break;
    }
    case CMD_MON_AUTOSTART: {
        MonitorSettings ms = MonitorStore::LoadSettings();
        if (MonitorDaemon::AutostartMode() != 0) MonitorDaemon::SetAutostart(false, false);
        else MonitorDaemon::SetAutostart(true, ms.elevated);   // elevated => scheduled task through a UAC prompt
        AppLog::Info(L"Monitoring", L"Recorder autostart changed (elevated: " + std::wstring(ms.elevated ? L"yes" : L"no") + L")");
        m_monAutostartMode = -1;
        break;
    }
    case CMD_MON_ELEVATED: {
        MonitorSettings ms = MonitorStore::LoadSettings();
        ms.elevated = !ms.elevated;
        MonitorStore::SaveSettings(ms);
        if (MonitorDaemon::IsRunning()) {
            // Restart the recorder with the other privilege level
            MonitorDaemon::RequestStop();
            for (int i = 0; i < 40 && MonitorDaemon::IsRunning(); ++i) Sleep(100);
            if (ms.enabled) MonitorDaemon::Start(ms.elevated);
        }
        if (MonitorDaemon::AutostartMode() != 0) MonitorDaemon::SetAutostart(true, ms.elevated);
        m_monAutostartMode = -1;
        m_monForceReload = true;
        m_statusToast = ms.elevated ? (m_lang == Lang::PL ? L"Rejestrator będzie działał jako administrator (SMART dysków SATA)" : L"Recorder will run as administrator (SATA SMART)")
                                    : (m_lang == Lang::PL ? L"Rejestrator działa jako zwykły proces" : L"Recorder runs as a standard process");
        m_statusToastExpireTick = GetTickCount64() + 3500;
        break;
    }
    case CMD_MON_ZOOM:
        m_monZoomChart = m_hwActionIndex;   // chart id, or -1 to return to the overview
        m_hwScrollY = 0.0f;
        break;

    // "LAN" tab: network scanner (first visit starts the scan from the draw code)
    case CMD_TAB_LAN:
        m_activeRibbonTab = RibbonTab::Lan;
        m_ribbonCollapsed = false;
        m_viewMode = ViewMode::Hardware;   // hosted by the card-hit dashboard view
        m_hwScrollY = 0.0f;
        EnsureWorkAnimTimerState();
        break;
    case CMD_LAN_SCAN:
        if (m_lan.IsScanning()) {
            m_lan.CancelScan();
        } else {
            m_lanAdapters = LanScanner::EnumerateAdapters();
            if (!m_lanAdapters.empty()) {
                m_lanAdapterIdx = std::clamp(m_lanAdapterIdx, 0, static_cast<int>(m_lanAdapters.size()) - 1);
                m_lan.StartScan(m_lanAdapters[static_cast<size_t>(m_lanAdapterIdx)]);
                m_lan.StartSsdp();   // after StartScan so it binds to the same adapter
                AppLog::Info(L"LAN", L"Network scan started on " + m_lanAdapters[static_cast<size_t>(m_lanAdapterIdx)].name);
            }
        }
        EnsureWorkAnimTimerState();
        break;
    case CMD_LAN_SSDP:
        m_lan.StartSsdp();
        EnsureWorkAnimTimerState();
        break;
    case CMD_LAN_ADAPTER:
        if (!m_lanAdapters.empty()) {
            m_lanAdapterIdx = (m_lanAdapterIdx + 1) % static_cast<int>(m_lanAdapters.size());
            m_lanSelectedIp.clear();
            m_lan.StartScan(m_lanAdapters[static_cast<size_t>(m_lanAdapterIdx)]);
            m_lan.StartSsdp();
        }
        EnsureWorkAnimTimerState();
        break;
    case CMD_LAN_SELECT:
        m_lanSelectedIp = m_hwActionArg;
        break;
    case CMD_LAN_PORTSCAN:
        AppLog::Info(L"LAN", L"Port scan started: " + (m_hwActionArg.empty() ? m_lanSelectedIp : m_hwActionArg));
        m_lan.StartPortScan(m_hwActionArg.empty() ? m_lanSelectedIp : m_hwActionArg);
        EnsureWorkAnimTimerState();
        break;
    case CMD_LAN_OPEN_WEB:
        // The URL may come from a device on the network (SSDP LOCATION): only ever hand http(s) links to the shell
        if (m_hwActionArg.rfind(L"http://", 0) == 0 || m_hwActionArg.rfind(L"https://", 0) == 0) {
            ShellExecuteW(m_hwnd, L"open", m_hwActionArg.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;
    case CMD_LAN_OPEN_SMB: {
        std::wstring unc = L"\\\\" + m_hwActionArg;
        ShellExecuteW(m_hwnd, L"open", unc.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    case CMD_LAN_COPY:
        FileEngine::CopyTextToClipboard(m_hwnd, m_hwActionArg);
        m_statusToast = (m_lang == Lang::PL ? L"\x2713 Skopiowano: " : L"\x2713 Copied: ") + m_hwActionArg;
        m_statusToastExpireTick = GetTickCount64() + 2600;
        break;
    // "Log" tab: this run's operation log
    case CMD_TAB_LOG:
        m_activeRibbonTab = RibbonTab::Log;
        m_ribbonCollapsed = false;
        m_viewMode = ViewMode::Hardware;   // hosted by the card-hit dashboard view
        m_hwScrollY = 0.0f;
        break;
    case CMD_LOG_LEVEL:
        m_logFilter = (m_logFilter + 1) % 3;
        m_hwScrollY = 0.0f;
        break;
    case CMD_LOG_CLEAR:
        AppLog::ClearView();
        m_hwScrollY = 0.0f;
        break;
    case CMD_LOG_OPEN_FOLDER:
        ShellExecuteW(m_hwnd, L"open", AppLog::Directory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case CMD_LOG_OPEN_FILE: {
        std::wstring quoted = L"\"" + AppLog::FilePath() + L"\"";
        ShellExecuteW(m_hwnd, L"open", L"notepad.exe", quoted.c_str(), nullptr, SW_SHOWNORMAL);
        break;
    }
    case CMD_LOG_COPY: {
        std::wstring all;
        for (const LogEntry& e : AppLog::Snapshot()) {
            if (m_logFilter == 1 && e.level < LogLevel::Warn) continue;
            if (m_logFilter == 2 && e.level < LogLevel::Error) continue;
            all += AppLog::FormatLine(e) + L"\r\n";
        }
        FileEngine::CopyTextToClipboard(m_hwnd, all);
        m_statusToast = m_lang == Lang::PL ? L"\x2713 Skopiowano log do schowka" : L"\x2713 Log copied to the clipboard";
        m_statusToastExpireTick = GetTickCount64() + 2600;
        break;
    }
    case CMD_MON_RANGE:
        m_monRangeIdx = std::clamp(m_hwActionIndex, 0, 4);
        m_monForceReload = true;
        break;
    case CMD_MON_INTERVAL: {
        static const int kIntervals[] = { 15, 30, 60, 300 };
        MonitorSettings ms = MonitorStore::LoadSettings();
        size_t next = 0;
        for (size_t i = 0; i < std::size(kIntervals); ++i) {
            if (kIntervals[i] == ms.intervalSec) { next = (i + 1) % std::size(kIntervals); break; }
        }
        ms.intervalSec = kIntervals[next];
        MonitorStore::SaveSettings(ms);
        m_monForceReload = true;
        break;
    }
    case CMD_MON_RETENTION: {
        static const int kRetention[] = { 7, 30, 90, 365 };
        MonitorSettings ms = MonitorStore::LoadSettings();
        size_t next = 0;
        for (size_t i = 0; i < std::size(kRetention); ++i) {
            if (kRetention[i] == ms.retentionDays) { next = (i + 1) % std::size(kRetention); break; }
        }
        ms.retentionDays = kRetention[next];
        MonitorStore::SaveSettings(ms);
        m_monForceReload = true;
        break;
    }
    case CMD_MON_OPENDIR:
        ShellExecuteW(m_hwnd, L"open", MonitorStore::DataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case CMD_MON_REFRESH:
        m_monForceReload = true;
        break;

    // "Programy" tab: installed programs, add / remove
    case CMD_TAB_PROGRAMS:
        m_activeRibbonTab = RibbonTab::Programs;
        m_ribbonCollapsed = false;
        m_viewMode = ViewMode::Hardware;   // hosted by the card-hit dashboard view
        m_hwScrollY = 0.0f;
        m_progLoaded = false;              // always show a fresh list when the tab is opened
        EnsureWorkAnimTimerState();
        break;
    case CMD_PROG_REFRESH:
        m_progLoaded = false;
        break;
    case CMD_PROG_SELECT:
        m_progSelectedKey = m_hwActionArg;
        break;
    case CMD_PROG_SORT:
        if (m_progSortCol == m_hwActionIndex) {
            m_progSortAsc = !m_progSortAsc;
        } else {
            m_progSortCol = m_hwActionIndex;
            m_progSortAsc = (m_progSortCol != 3);   // size: biggest first
        }
        m_programs.Sort(m_progSortCol, m_progSortAsc);
        break;
    case CMD_PROG_ADD: {
        wchar_t fileBuf[MAX_PATH * 2] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
        const wchar_t* filterPl = L"Instalatory (*.exe;*.msi;*.msix;*.appx)\0*.exe;*.msi;*.msix;*.appx\0Wszystkie pliki (*.*)\0*.*\0";
        const wchar_t* filterEn = L"Installers (*.exe;*.msi;*.msix;*.appx)\0*.exe;*.msi;*.msix;*.appx\0All files (*.*)\0*.*\0";
        ofn.lpstrFilter = (m_lang == Lang::PL) ? filterPl : filterEn;
        ofn.lpstrTitle = (m_lang == Lang::PL) ? L"Wybierz instalator programu" : L"Choose a program installer";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        if (GetOpenFileNameW(&ofn)) {
            const bool installStarted = ProgramInventory::InstallFromFile(m_hwnd, fileBuf);
            if (installStarted) AppLog::Ok(L"Programs", std::wstring(L"Installer started: ") + fileBuf);
            else AppLog::Error(L"Programs", std::wstring(L"Could not start the installer: ") + fileBuf);
            if (installStarted) {
                m_statusToast = (m_lang == Lang::PL ? L"\x2795 Uruchomiono instalator: " : L"\x2795 Installer started: ") + PathUtils::GetFileName(fileBuf);
                ScheduleProgramsRefresh(15);
            } else {
                m_statusToast = m_lang == Lang::PL ? L"Nie udało się uruchomić instalatora" : L"Could not start the installer";
            }
            m_statusToastExpireTick = GetTickCount64() + 4000;
        }
        break;
    }
    case CMD_PROG_UNINSTALL:
    case CMD_PROG_UNINSTALL_ROW: {
        if (cmdId == CMD_PROG_UNINSTALL_ROW) m_progSelectedKey = m_hwActionArg;
        const InstalledProgram* prog = m_programs.Find(m_progSelectedKey);
        if (!prog) {
            m_statusToast = m_lang == Lang::PL ? L"Zaznacz program na liście, aby go odinstalować" : L"Select a program in the list first";
            m_statusToastExpireTick = GetTickCount64() + 2800;
            break;
        }
        if (!prog->canUninstall) {
            m_statusToast = m_lang == Lang::PL ? L"Ten program nie udostępnia dezinstalatora" : L"This program does not provide an uninstaller";
            m_statusToastExpireTick = GetTickCount64() + 2800;
            break;
        }
        std::wstring q = (m_lang == Lang::PL ? L"Czy odinstalować program:\n\n" : L"Uninstall this program?\n\n") + prog->name +
                         (prog->version.empty() ? L"" : (L"  (" + prog->version + L")")) + L"?";
        if (MessageBoxW(m_hwnd, q.c_str(), m_lang == Lang::PL ? L"Odinstaluj program" : L"Uninstall program", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
        const bool uninstallStarted = ProgramInventory::Uninstall(m_hwnd, *prog);
        if (uninstallStarted) AppLog::Ok(L"Programs", L"Uninstaller started: " + prog->name);
        else AppLog::Error(L"Programs", L"Could not start the uninstaller of: " + prog->name);
        if (uninstallStarted) {
            m_statusToast = (m_lang == Lang::PL ? L"\xD83D\xDDD1 Uruchomiono dezinstalator: " : L"\xD83D\xDDD1 Uninstaller started: ") + prog->name;
            ScheduleProgramsRefresh(15);
        } else {
            m_statusToast = m_lang == Lang::PL ? L"Nie udało się uruchomić dezinstalatora" : L"Could not start the uninstaller";
        }
        m_statusToastExpireTick = GetTickCount64() + 4000;
        break;
    }
    case CMD_PROG_APPWIZ:
        ShellExecuteW(m_hwnd, L"open", L"appwiz.cpl", nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case CMD_PROG_SETTINGS:
        ShellExecuteW(m_hwnd, L"open", L"ms-settings:appsfeatures", nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case CMD_PROG_OPENLOC: {
        const InstalledProgram* prog = m_programs.Find(m_progSelectedKey);
        if (prog && !prog->installLocation.empty()) {
            ShellExecuteW(m_hwnd, L"open", prog->installLocation.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else {
            m_statusToast = m_lang == Lang::PL ? L"Brak zapisanego folderu instalacji dla tego programu" : L"No install folder recorded for this program";
            m_statusToastExpireTick = GetTickCount64() + 2800;
        }
        break;
    }
    case CMD_HW_RELAUNCH_ADMIN: {
        wchar_t selfExe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, selfExe, MAX_PATH);
        // Standard UAC elevation prompt; the current (non-elevated) window closes once the new one starts.
        HINSTANCE r = ShellExecuteW(m_hwnd, L"runas", selfExe, nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(r) > 32) {
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        }
        break;
    }
    case CMD_HW_TOGGLE_LIVE:
        m_hwLiveRefresh = !m_hwLiveRefresh;
        EnsureWorkAnimTimerState();
        break;
    case CMD_HW_BENCH_CPU: {
        SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        m_hwInspector.RunCpuBenchmark();
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        wchar_t bmsg[256] = {};
        swprintf_s(bmsg, m_lang == Lang::PL
            ? L"\x26A1 Benchmark CPU: Single %.2f GOPS  \x2022  Multi %.2f GOPS  (MP x%.1f)"
            : L"\x26A1 CPU Benchmark: Single %.2f GOPS  \x2022  Multi %.2f GOPS  (MP x%.1f)",
            m_hwInspector.GetCpu().benchSingleCoreGops,
            m_hwInspector.GetCpu().benchMultiCoreGops,
            m_hwInspector.GetCpu().benchMpRatio);
        m_statusToast = bmsg;
        m_statusToastExpireTick = GetTickCount64() + 4500;
        break;
    }
    case CMD_HW_BENCH_RAM: {
        SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        m_hwInspector.RunRamBenchmark();
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        wchar_t rmsg[256] = {};
        swprintf_s(rmsg, m_lang == Lang::PL
            ? L"\x26A1 Benchmark RAM: Odczyt %.1f GB/s  \x2022  Zapis %.1f GB/s  \x2022  Kopia %.1f GB/s"
            : L"\x26A1 RAM Benchmark: Read %.1f GB/s  \x2022  Write %.1f GB/s  \x2022  Copy %.1f GB/s",
            m_hwInspector.GetRam().benchReadGBps,
            m_hwInspector.GetRam().benchWriteGBps,
            m_hwInspector.GetRam().benchCopyGBps);
        m_statusToast = rmsg;
        m_statusToastExpireTick = GetTickCount64() + 4500;
        break;
    }
    case CMD_HW_COPY_REPORT: {
        std::wstring rep = m_hwInspector.BuildFullTextReport(m_lang);
        if (!rep.empty() && OpenClipboard(m_hwnd)) {
            EmptyClipboard();
            size_t bytes = (rep.size() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hMem) {
                void* ptr = GlobalLock(hMem);
                if (ptr) {
                    std::memcpy(ptr, rep.c_str(), bytes);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
            }
            CloseClipboard();
            m_statusToast = m_lang == Lang::PL ? L"\x2713 Skopiowano pełny raport sprzętowy PRO do schowka!" : L"\x2713 Copied full PRO hardware report to clipboard!";
            m_statusToastExpireTick = GetTickCount64() + 3200;
        }
        break;
    }
    case CMD_HW_SAVE_REPORT: {
        std::wstring rep = m_hwInspector.BuildFullTextReport(m_lang);
        std::wstring folder = (m_currentPath.empty() || m_currentPath == L"THIS_PC") ? L"C:\\" : m_currentPath;
        std::wstring outPath = PathUtils::EnsureTrailingSlash(folder) + L"Raport_Sprzetowy_PRO.txt";
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, rep.c_str(), static_cast<int>(rep.size()), nullptr, 0, nullptr, nullptr);
        if (utf8Len > 0) {
            std::string utf8(utf8Len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, rep.c_str(), static_cast<int>(rep.size()), utf8.data(), utf8Len, nullptr, nullptr);
            std::ofstream ofs(outPath, std::ios::binary);
            if (ofs) {
                ofs.write(utf8.data(), utf8.size());
                ofs.close();
                m_statusToast = (m_lang == Lang::PL ? L"\xD83D\xDCBE Zapisano: " : L"\xD83D\xDCBE Saved: ") + outPath;
                m_statusToastExpireTick = GetTickCount64() + 3800;
                ShellExecuteW(m_hwnd, L"open", L"notepad.exe", (L"\"" + outPath + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
            }
        }
        break;
    }
    case CMD_HW_OPEN_DEVMGR:
        ShellExecuteW(m_hwnd, L"open", L"devmgmt.msc", nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case CMD_HW_OPEN_TASKMGR:
        ShellExecuteW(m_hwnd, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case CMD_HW_OPEN_MSINFO:
        ShellExecuteW(m_hwnd, L"open", L"msinfo32.exe", nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case CMD_HW_OPEN_DXDIAG:
        ShellExecuteW(m_hwnd, L"open", L"dxdiag.exe", nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case CMD_HW_BACK_TO_FILES:
        m_viewMode = m_lastNonViewerMode;
        m_activeRibbonTab = RibbonTab::Home;
        EnsureWorkAnimTimerState();
        break;
    case CMD_IMG_TOOL_MENU: {
        POINT pt = {};
        GetCursorPos(&pt);
        for (const auto& hit : m_ribbonHits) {
            if (hit.id == CMD_IMG_TOOL_MENU) {
                POINT bpt = { static_cast<LONG>(hit.rect.left), static_cast<LONG>(hit.rect.bottom) };
                ClientToScreen(m_hwnd, &bpt);
                pt = bpt;
                break;
            }
        }
        ShowImageToolsMenu(pt.x, pt.y, m_focusedIndex);
        break;
    }
    case CMD_CTX_ROTATE_CW:
    case CMD_CTX_ROTATE_CCW:
    case CMD_IMG_TOGGLE_OVERWRITE:
    case CMD_IMG_SCALE_75:
    case CMD_IMG_SCALE_50:
    case CMD_IMG_SCALE_33:
    case CMD_IMG_SCALE_25:
    case CMD_IMG_SCALE_CUSTOM:
    case CMD_IMG_MAXDIM_3840:
    case CMD_IMG_MAXDIM_2560:
    case CMD_IMG_MAXDIM_1920:
    case CMD_IMG_MAXDIM_1280:
    case CMD_IMG_MAXDIM_1024:
    case CMD_IMG_MAXDIM_512:
    case CMD_IMG_TARGET_5MB:
    case CMD_IMG_TARGET_2MB:
    case CMD_IMG_TARGET_1MB:
    case CMD_IMG_TARGET_500KB:
    case CMD_IMG_TARGET_250KB:
    case CMD_IMG_TARGET_100KB:
    case CMD_IMG_TARGET_CUSTOM:
    case CMD_IMG_FLIP_H:
    case CMD_IMG_FLIP_V:
    case CMD_IMG_ROTATE_180:
    case CMD_IMG_CONVERT_JPG_92:
    case CMD_IMG_CONVERT_JPG_80:
    case CMD_IMG_CONVERT_PNG:
    case CMD_IMG_CONVERT_BMP:
        HandleImageToolCommand(cmdId, m_focusedIndex);
        break;
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::ActivateFocusedOrSelectedItem() {
    if (m_focusedIndex < 0 || m_focusedIndex >= static_cast<int>(m_items.size())) return;
    const FileEntry& item = m_items[m_focusedIndex];
    if (item.isDirectory) {
        NavigateTo(item.fullPath, true);
    } else if (item.isImage || item.isVideo || item.isAudio) {
        if (m_viewMode != ViewMode::FullViewer) {
            m_lastNonViewerMode = m_viewMode;
            m_viewMode = ViewMode::FullViewer;
            ResetViewerZoom();
            EnsureItemVisible(m_focusedIndex);
            SyncMediaPlayerToSelection();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else {
            ToggleFullScreen();
        }
    } else {
        FileEngine::OpenFileOrProperties(m_hwnd, item.fullPath, false);
    }
}

void ExplorerApp::DeleteSelectedItems(bool permanent) {
    std::vector<std::wstring> toDelete = GetSelectedPaths();
    if (toDelete.empty()) return;

    if (m_confirmDelete) {
        const Strings& s = GetStrings(m_lang);
        const wchar_t* msg = permanent ? s.dlgConfirmPermMsg : s.dlgConfirmRecycleMsg;
        if (MessageBoxW(m_hwnd, msg, s.dlgConfirmDeleteTitle, MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }
    }

    // Release any active video/audio stream before deleting so files are never locked!
    m_mediaPlayer.CloseMedia();
    KillTimer(m_hwnd, ID_TIMER_MEDIA);

    int nextFocusIdx = m_focusedIndex;

    // Build O(1) hash set of normalized path keys for strictly O(N) UI removal
    std::unordered_set<std::wstring> deleteKeys;
    deleteKeys.reserve(toDelete.size() * 2);
    for (const auto& p : toDelete) {
        m_imageCache.EvictPath(p);
        deleteKeys.insert(PathUtils::NormalizePathKey(p));
    }

    auto shouldRemove = [&](const FileEntry& e) {
        return deleteKeys.find(e.normKey) != deleteKeys.end();
    };

    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), shouldRemove), m_items.end());
    m_allItems.erase(std::remove_if(m_allItems.begin(), m_allItems.end(), shouldRemove), m_allItems.end());

    if (!m_items.empty()) {
        nextFocusIdx = std::clamp(nextFocusIdx, 0, static_cast<int>(m_items.size()) - 1);
        SelectSingle(nextFocusIdx, true);
        if (m_viewMode == ViewMode::FullViewer) {
            ResetViewerZoom();
        }
        EnsureItemVisible(nextFocusIdx);
    } else {
        m_focusedIndex = -1;
        m_anchorIndex = -1;
    }

    ClampScrollOffset();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    AppLog::Info(L"Delete", std::wstring(permanent ? L"Permanent delete queued: " : L"Move to Recycle Bin queued: ") + DescribePaths(toDelete));
    m_fileEngine.EnqueueDelete(toDelete, permanent);
}

void ExplorerApp::ConfirmAndDeleteRowItem(int itemIndex, bool permanent) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(m_items.size())) return;
    if (m_currentPath == L"THIS_PC") return;

    const FileEntry itemCopy = m_items[itemIndex];
    const Strings& s = GetStrings(m_lang);

    std::wstring prompt;
    if (m_lang == Lang::PL) {
        prompt = permanent
            ? (L"Czy na pewno chcesz TRWALE usunąć (bez Kosza):\n\n\"" + itemCopy.name + L"\"?")
            : (L"Czy przenieść do Kosza element:\n\n\"" + itemCopy.name + L"\"?");
    } else {
        prompt = permanent
            ? (L"Are you sure you want to PERMANENTLY delete:\n\n\"" + itemCopy.name + L"\"?")
            : (L"Move this item to the Recycle Bin?\n\n\"" + itemCopy.name + L"\"");
    }

    UINT flags = MB_YESNO | (permanent ? (MB_ICONWARNING | MB_DEFBUTTON2) : MB_ICONQUESTION);
    if (MessageBoxW(m_hwnd, prompt.c_str(), s.dlgConfirmDeleteTitle, flags) != IDYES) {
        return;
    }

    m_mediaPlayer.CloseMedia();
    KillTimer(m_hwnd, ID_TIMER_MEDIA);

    std::wstring delPath = itemCopy.fullPath;
    std::wstring delKey  = itemCopy.normKey;
    m_imageCache.EvictPath(delPath);

    auto shouldRemove = [&](const FileEntry& e) { return e.normKey == delKey; };
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), shouldRemove), m_items.end());
    m_allItems.erase(std::remove_if(m_allItems.begin(), m_allItems.end(), shouldRemove), m_allItems.end());

    if (!m_items.empty()) {
        int nextFocusIdx = std::clamp(itemIndex, 0, static_cast<int>(m_items.size()) - 1);
        SelectSingle(nextFocusIdx, true);
    } else {
        m_focusedIndex = -1;
        m_anchorIndex = -1;
    }

    ClampScrollOffset();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    AppLog::Info(L"Delete", std::wstring(permanent ? L"Permanent delete queued: " : L"Move to Recycle Bin queued: ") + DescribePaths({ delPath }));
    m_fileEngine.EnqueueDelete({ delPath }, permanent);
}

void ExplorerApp::PerformUndoDelete() {
    std::wstring restoredDir;
    if (m_fileEngine.UndoLastDelete(restoredDir)) {
        const Strings& s = GetStrings(m_lang);
        m_statusToast = s.statusRestored;
        m_statusToastExpireTick = GetTickCount64() + 3500;
        AppLog::Ok(L"Delete", L"Undo: restored the last deleted item(s) to " + restoredDir);
        RefreshCurrentFolder();
    } else {
        AppLog::Warn(L"Delete", L"Undo: nothing to restore");
    }
}

void ExplorerApp::CopySelected(bool isCut) {
    auto paths = GetSelectedPaths();
    if (paths.empty()) return;

    if (FileEngine::CopyFilesToClipboard(m_hwnd, paths, isCut)) {
        AppLog::Info(L"Clipboard", std::wstring(isCut ? L"Cut: " : L"Copied: ") + DescribePaths(paths));
        for (auto& item : m_items) {
            item.isCut = isCut && item.isSelected;
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void ExplorerApp::PasteIntoCurrent() {
    bool wasCut = false;
    if (FileEngine::PasteFilesFromClipboard(m_hwnd, m_currentPath, wasCut)) {
        AppLog::Ok(L"Paste", std::wstring(wasCut ? L"Moved" : L"Copied") + L" files into " + m_currentPath);
        RefreshCurrentFolder();
    } else {
        AppLog::Warn(L"Paste", L"Paste into " + m_currentPath + L" did not complete (empty clipboard, cancelled or failed)");
    }
}

void ExplorerApp::CopySelectedPaths() {
    auto paths = GetSelectedPaths();
    if (paths.empty()) return;
    std::wstring combined;
    for (size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) combined += L"\r\n";
        combined += L"\"" + paths[i] + L"\"";
    }
    FileEngine::CopyTextToClipboard(m_hwnd, combined);
}

void ExplorerApp::CreateNewFolderAction() {
    const Strings& s = GetStrings(m_lang);
    std::wstring created;
    if (FileEngine::CreateNewFolder(m_currentPath, s.dlgNewFolderDefault, created)) {
        AppLog::Ok(L"NewFolder", L"Created folder: " + created);
        RefreshCurrentFolder(created);
        StartInlineRename();
    } else {
        AppLog::Error(L"NewFolder", L"Could not create a folder in " + m_currentPath);
    }
}

void ExplorerApp::StartInlineRename() {
    const bool pl = (m_lang == Lang::PL);
    if (m_viewMode == ViewMode::FullViewer) return;
    if (m_currentPath == L"THIS_PC") {
        m_statusToast = pl ? L"\x26A0 Nie można zmieniać nazw dysków w widoku Ten komputer" : L"\x26A0 Cannot rename drives in This PC view";
        m_statusToastExpireTick = GetTickCount64() + 3500;
        EnsureWorkAnimTimerState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (m_focusedIndex < 0 || m_focusedIndex >= static_cast<int>(m_items.size())) {
        m_statusToast = pl ? L"\x26A0 Zaznacz plik lub folder, aby zmienić jego nazwę" : L"\x26A0 Select a file or folder to rename";
        m_statusToastExpireTick = GetTickCount64() + 3500;
        EnsureWorkAnimTimerState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    EnsureItemVisible(m_focusedIndex);
    D2D1_RECT_F contentRect = GetContentItemsRect();
    D2D1_RECT_F itemR = GetItemRect(m_focusedIndex, contentRect);

    m_renamingIndex = m_focusedIndex;
    m_renamingPath = m_items[m_focusedIndex].fullPath;
    m_renamingOldName = m_items[m_focusedIndex].name;
    float editLeft = itemR.left + (m_viewMode == ViewMode::Details ? 22.0f : 2.0f);
    float editTop  = (m_viewMode == ViewMode::Details) ? itemR.top : (itemR.bottom - 24.0f);
    float editW    = std::max(120.0f, (m_viewMode == ViewMode::Details ? m_colWidths[0] - 24.0f : (itemR.right - itemR.left - 4.0f)));

    SetWindowPos(m_renameEdit, HWND_TOP,
        static_cast<int>(editLeft), static_cast<int>(editTop),
        static_cast<int>(editW), 22, SWP_SHOWWINDOW);
    SetWindowTextW(m_renameEdit, m_items[m_focusedIndex].name.c_str());
    SetFocus(m_renameEdit);

    const std::wstring& name = m_items[m_focusedIndex].name;
    size_t dot = m_items[m_focusedIndex].isDirectory ? std::wstring::npos : name.find_last_of(L'.');
    SendMessageW(m_renameEdit, EM_SETSEL, 0, (dot != std::wstring::npos && dot > 0) ? static_cast<LPARAM>(dot) : -1);
}

void ExplorerApp::ShowRenameFailure(const std::wstring& oldName, const std::wstring& newName, const std::wstring& reason, DWORD err) {
    (void)err;
    const bool pl = (m_lang == Lang::PL);
    const std::wstring shownNew = newName.empty() ? std::wstring(pl ? L"(pusta nazwa)" : L"(empty name)") : newName;
    m_statusToast = (pl ? L"\x2717 Nie udało się zmienić nazwy: " : L"\x2717 Rename failed: ") +
                    oldName + L"  \x2192  " + shownNew + L"   \x2014   " + reason;
    m_statusToastExpireTick = GetTickCount64() + 8500;
    AppLog::Error(L"Rename", L"FAILED  \"" + oldName + L"\"  ->  \"" + shownNew + L"\":  " + reason);
    EnsureWorkAnimTimerState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::MergeFolderInto(const std::wstring& srcDir, const std::wstring& dstDir, const std::wstring& srcName, const std::wstring& dstName) {
    AppLog::Info(L"Merge", L"Started background merge: contents of " + srcDir + L"  ->  " + dstDir);

    // Release any active media stream or cached thumbnails inside srcDir before moving
    m_mediaPlayer.CloseMedia();
    KillTimer(m_hwnd, ID_TIMER_MEDIA);
    m_imageCache.EvictPath(srcDir);

    // Hide srcDir immediately in the UI and across refreshes until the background worker completes
    m_fileEngine.AddPendingHiddenPath(srcDir);
    const std::wstring srcKey = PathUtils::NormalizePathKey(srcDir);
    const std::wstring dstKey = PathUtils::NormalizePathKey(dstDir);
    auto isSrc = [&](const FileEntry& e) { return e.normKey == srcKey; };
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), isSrc), m_items.end());
    m_allItems.erase(std::remove_if(m_allItems.begin(), m_allItems.end(), isSrc), m_allItems.end());
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (m_items[i].normKey == dstKey) {
            SelectSingle(i, true);
            break;
        }
    }
    ClampScrollOffset();

    ++m_mergePending;
    m_mergeMovedCount = 0;
    m_mergePendingText = srcName + L"  \x2192  " + dstName;
    EnsureWorkAnimTimerState();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    HWND hwnd = m_hwnd;
    std::atomic<int>* progressPtr = &m_mergeMovedCount;
    std::thread([hwnd, srcDir, dstDir, srcName, dstName, progressPtr]() {
        auto* r = new MergeFolderResult();
        r->srcDir = srcDir;
        r->dstDir = dstDir;
        r->srcName = srcName;
        r->dstName = dstName;
        FileEngine::MergeDirectoryContents(srcDir, dstDir, progressPtr, *r);
        if (!PostMessageW(hwnd, WM_APP_MERGE_DONE, 0, reinterpret_cast<LPARAM>(r))) delete r;
    }).detach();
}

void ExplorerApp::StartRenameJob(const std::wstring& oldPath, const std::wstring& oldName, const std::wstring& newName) {
    const bool pl = (m_lang == Lang::PL);

    std::wstring bad = ValidateFileName(newName, pl);
    if (!bad.empty()) { ShowRenameFailure(oldName, newName, bad, 0); return; }

    const std::wstring parent = PathUtils::GetParentPath(oldPath);
    const std::wstring target = PathUtils::EnsureTrailingSlash(parent) + newName;
    const bool caseOnly = (_wcsicmp(oldName.c_str(), newName.c_str()) == 0);

    auto toExt = [](const std::wstring& p) -> std::wstring {
        if (p.rfind(L"\\\\?\\", 0) == 0) return p;
        if (p.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + p.substr(2);
        if (p.size() >= 2 && p[1] == L':') return L"\\\\?\\" + p;
        return p;
    };

    const DWORD targetAttrs = GetFileAttributesW(toExt(target).c_str());
    if (!caseOnly && targetAttrs != INVALID_FILE_ATTRIBUTES) {
        const DWORD srcAttrs = GetFileAttributesW(toExt(oldPath).c_str());
        const bool targetIsDir = (targetAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool srcIsDir = srcAttrs != INVALID_FILE_ATTRIBUTES && (srcAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (targetIsDir && srcIsDir) {
            // Non-blocking background merge into the existing folder (zero Windows Explorer popups, zero UI lag)
            MergeFolderInto(oldPath, target, oldName, newName);
            return;
        }
        ShowRenameFailure(oldName, newName,
            (targetIsDir != srcIsDir) ? (pl ? L"Element o tej nazwie już istnieje, ale jest innego typu (plik / folder)." : L"An item with that name already exists but is of a different kind (file / folder).")
                                      : (pl ? L"Plik o takiej nazwie już istnieje w tym folderze." : L"A file with that name already exists in this folder."),
            ERROR_ALREADY_EXISTS);
        return;
    }

    // Release any open media / thumbnail handles on the item before renaming
    m_mediaPlayer.CloseMedia();
    KillTimer(m_hwnd, ID_TIMER_MEDIA);
    m_imageCache.EvictPath(oldPath);

    // Optimistic UI update so the new name is visible immediately while the worker verifies on disk
    const std::wstring oldKey = PathUtils::NormalizePathKey(oldPath);
    const std::wstring newKey = PathUtils::NormalizePathKey(target);
    auto updateEntry = [&](FileEntry& e) {
        if (e.normKey == oldKey) {
            e.name = newName;
            if (m_showExtensions || e.isDirectory) {
                e.displayName = newName;
            } else {
                size_t dot = newName.find_last_of(L'.');
                e.displayName = (dot != std::wstring::npos && dot > 0) ? newName.substr(0, dot) : newName;
            }
            e.fullPath = target;
            e.normKey = newKey;
        }
    };
    for (auto& e : m_items) updateEntry(e);
    for (auto& e : m_allItems) updateEntry(e);

    ++m_renamePending;
    m_renamePendingText = oldName + L"  \x2192  " + newName;
    AppLog::Info(L"Rename", L"Started: " + oldPath + L"  ->  " + newName);
    EnsureWorkAnimTimerState();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    HWND hwnd = m_hwnd;
    std::thread([hwnd, oldPath, oldName, newName, caseOnly, toExt]() {
        auto* r = new RenameJobResult();
        r->oldPath = oldPath; r->oldName = oldName; r->newName = newName;
        DWORD err = 0;
        r->ok = FileEngine::RenameFileOrFolder(oldPath, newName, r->newPath, &err);
        r->error = err;
        if (r->ok) {
            // Do not trust the return value alone: verify on the file system using extended-length paths
            if (caseOnly) {
                WIN32_FIND_DATAW fd = {};
                HANDLE h = FindFirstFileW(toExt(r->newPath).c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) { r->verified = (newName == fd.cFileName); FindClose(h); }
            } else {
                r->verified = GetFileAttributesW(toExt(r->newPath).c_str()) != INVALID_FILE_ATTRIBUTES &&
                              GetFileAttributesW(toExt(oldPath).c_str()) == INVALID_FILE_ATTRIBUTES;
            }
        }
        if (!PostMessageW(hwnd, WM_APP_RENAME_DONE, 0, reinterpret_cast<LPARAM>(r))) delete r;
    }).detach();
}

void ExplorerApp::CommitAddressOrRenameEdit(HWND editHwnd) {
    wchar_t buf[2048] = {};
    GetWindowTextW(editHwnd, buf, 2048);
    // Hiding the focused edit sends EN_KILLFOCUS synchronously: capture and clear the editing state first
    const int renamingIdxAtCommit = m_renamingIndex;
    const std::wstring renamingPathAtCommit = std::move(m_renamingPath);
    const std::wstring renamingOldNameAtCommit = std::move(m_renamingOldName);
    const int fieldCmdAtCommit = m_editingSearchFieldCmd;
    m_renamingIndex = -1;
    m_renamingPath.clear();
    m_renamingOldName.clear();
    m_editingSearchFieldCmd = 0;

    ShowWindow(editHwnd, SW_HIDE);
    SetFocus(m_hwnd);

    if (editHwnd == m_addressEdit) {
        std::wstring newPath = buf;
        if (!newPath.empty()) {
            NavigateTo(newPath, true);
        }
    } else if (editHwnd == m_searchEdit) {
        if (IsProgramsView()) {
            m_progFilter = buf;
        } else {
            m_filterText = buf;
            TriggerAsyncSearch();
        }
    } else if (editHwnd == m_renameEdit && fieldCmdAtCommit != 0) {
        int fcmd = fieldCmdAtCommit;
        std::wstring val = buf;
        if (fcmd == CMD_SEARCH_EDIT_DATE_FROM) {
            m_searchOpts.dateFromStr = val;
            m_searchOpts.useDateFilter = true;
        } else if (fcmd == CMD_SEARCH_EDIT_DATE_TO) {
            m_searchOpts.dateToStr = val;
            m_searchOpts.useDateFilter = true;
        } else if (fcmd == CMD_SEARCH_EDIT_SIZE_MIN) {
            m_searchOpts.sizeMinStr = val;
            m_searchOpts.useSizeFilter = true;
        } else if (fcmd == CMD_SEARCH_EDIT_SIZE_MAX) {
            m_searchOpts.sizeMaxStr = val;
            m_searchOpts.useSizeFilter = true;
        } else if (fcmd == CMD_SEARCH_EDIT_CUSTOM_EXT) {
            m_searchOpts.customExtList = val;
            m_searchOpts.useTypeFilter = (!val.empty() || m_searchOpts.categoryMask != 0);
        }
        TriggerAsyncSearch();
    } else if (editHwnd == m_renameEdit && (!renamingPathAtCommit.empty() || (renamingIdxAtCommit >= 0 && renamingIdxAtCommit < static_cast<int>(m_items.size())))) {
        std::wstring oldPath = !renamingPathAtCommit.empty() ? renamingPathAtCommit : m_items[renamingIdxAtCommit].fullPath;
        std::wstring oldName = !renamingOldNameAtCommit.empty() ? renamingOldNameAtCommit : m_items[renamingIdxAtCommit].name;
        std::wstring newName = buf;
        while (!newName.empty() && iswspace(newName.front())) newName.erase(newName.begin());
        while (!newName.empty() && iswspace(newName.back())) newName.pop_back();
        const bool pl = (m_lang == Lang::PL);
        if (newName.empty()) {
            ShowRenameFailure(oldName, newName, ValidateFileName(newName, pl), ERROR_INVALID_NAME);
        } else if (newName == oldName) {
            m_statusToast = (pl ? L"Nazwa bez zmian: " : L"Name unchanged: ") + oldName;
            m_statusToastExpireTick = GetTickCount64() + 2500;
            EnsureWorkAnimTimerState();
        } else {
            StartRenameJob(oldPath, oldName, newName);
        }
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::CancelInlineEdits(bool byEscape) {
    const bool hadRenameOpen = (!m_renamingPath.empty() || m_renamingIndex >= 0) && m_renameEdit && IsWindowVisible(m_renameEdit);
    const std::wstring cancelledName = m_renamingOldName;
    if (m_renameEdit && IsWindowVisible(m_renameEdit) && m_editingSearchFieldCmd != 0 && !byEscape) {
        CommitAddressOrRenameEdit(m_renameEdit);
        return;
    }
    m_renamingIndex = -1;
    m_renamingPath.clear();
    m_renamingOldName.clear();
    m_editingSearchFieldCmd = 0;
    if (m_addressEdit && IsWindowVisible(m_addressEdit)) ShowWindow(m_addressEdit, SW_HIDE);
    if (m_renameEdit && IsWindowVisible(m_renameEdit)) ShowWindow(m_renameEdit, SW_HIDE);
    if (m_searchEdit && IsWindowVisible(m_searchEdit) && m_filterText.empty()) ShowWindow(m_searchEdit, SW_HIDE);
    if (byEscape && hadRenameOpen) {
        m_statusToast = (m_lang == Lang::PL ? L"Anulowano zmianę nazwy: " : L"Rename cancelled: ") + cancelledName;
        m_statusToastExpireTick = GetTickCount64() + 2500;
        AppLog::Info(L"Rename", L"Cancelled by Escape: " + cancelledName);
        EnsureWorkAnimTimerState();
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::ShowDeleteDropdownMenu(int screenX, int screenY) {
    const Strings& s = GetStrings(m_lang);
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, CMD_CTX_DELETE_RECYCLE, s.menuDeleteRecycle);
    AppendMenuW(hMenu, MF_STRING, CMD_CTX_DELETE_PERM, s.menuDeletePerm);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (m_confirmDelete ? MF_CHECKED : MF_UNCHECKED), CMD_CTX_TOGGLE_CONFIRM, s.menuDeleteConfirmToggle);

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screenX, screenY, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd == CMD_CTX_DELETE_RECYCLE) DeleteSelectedItems(false);
    else if (cmd == CMD_CTX_DELETE_PERM) DeleteSelectedItems(true);
    else if (cmd == CMD_CTX_TOGGLE_CONFIRM) m_confirmDelete = !m_confirmDelete;
}

void ExplorerApp::RebuildTreeNodes() {
    m_treeNodes = m_fileEngine.BuildNavigationTree(m_lang, m_pinnedFolders);
}

void ExplorerApp::ToggleTreeNodeExpand(int nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_treeNodes.size())) return;

    TreeNode& node = m_treeNodes[nodeIndex];

    // Case 1: Node is already expanded -> collapse ("zwiń") all descendant nodes underneath it!
    if (node.isExpanded) {
        node.isExpanded = false;
        int parentIndent = node.indent;
        int removeStart = nodeIndex + 1;
        int removeEnd = removeStart;
        while (removeEnd < static_cast<int>(m_treeNodes.size())) {
            if (node.isHeader) {
                // Header nodes ("Szybki dostęp", "Ten komputer") own all indent >= 1 nodes until the next indent 0 node
                if (m_treeNodes[removeEnd].indent == 0) break;
            } else {
                if (m_treeNodes[removeEnd].indent <= parentIndent) break;
            }
            ++removeEnd;
        }
        if (removeEnd > removeStart) {
            m_treeNodes.erase(m_treeNodes.begin() + removeStart, m_treeNodes.begin() + removeEnd);
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // Case 2: Node is collapsed -> expand ("rozwiń") its immediate children!
    std::vector<TreeNode> children;
    if (node.isHeader && node.isQuickAccess) {
        auto fullTree = m_fileEngine.BuildNavigationTree(m_lang, m_pinnedFolders);
        for (size_t i = 1; i < fullTree.size() && fullTree[i].isQuickAccess && !fullTree[i].isHeader; ++i) {
            children.push_back(fullTree[i]);
        }
    } else if (node.isHeader && node.path == L"THIS_PC") {
        auto fullTree = m_fileEngine.BuildNavigationTree(m_lang, m_pinnedFolders);
        bool afterPc = false;
        for (const auto& tn : fullTree) {
            if (tn.path == L"THIS_PC") { afterPc = true; continue; }
            if (afterPc) children.push_back(tn);
        }
    } else if (!node.path.empty() && node.path != L"THIS_PC") {
        if (node.isDrive && node.isOffline) {
            node.hasChildren = false;
            node.isExpanded = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        children = FileEngine::EnumerateSubfoldersForTree(node.path, node.indent + 1, node.isQuickAccess);
    }

    if (children.empty() && !node.isHeader) {
        node.hasChildren = false;
        node.isExpanded = false;
    } else {
        node.isExpanded = true;
        m_treeNodes.insert(m_treeNodes.begin() + nodeIndex + 1, children.begin(), children.end());
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

bool ExplorerApp::IsFolderPinnedToQuickAccess(const std::wstring& folderPath) const {
    if (folderPath.empty() || folderPath == L"THIS_PC") return false;
    std::wstring targetKey = PathUtils::NormalizePathKey(folderPath);
    for (const auto& [label, path] : m_pinnedFolders) {
        if (PathUtils::NormalizePathKey(path) == targetKey) return true;
    }
    return false;
}

void ExplorerApp::PinFolderToQuickAccess(const std::wstring& folderPath) {
    if (folderPath.empty() || folderPath == L"THIS_PC") return;
    if (IsFolderPinnedToQuickAccess(folderPath)) return;

    std::wstring label = PathUtils::GetFileName(folderPath);
    if (label.empty()) label = folderPath;
    m_pinnedFolders.push_back({ label, folderPath });
    RebuildTreeNodes();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::UnpinFolderFromQuickAccess(const std::wstring& folderPath) {
    if (folderPath.empty()) return;
    std::wstring targetKey = PathUtils::NormalizePathKey(folderPath);
    auto it = std::remove_if(m_pinnedFolders.begin(), m_pinnedFolders.end(),
        [&](const std::pair<std::wstring, std::wstring>& kv) {
            return PathUtils::NormalizePathKey(kv.second) == targetKey;
        });
    if (it != m_pinnedFolders.end()) {
        m_pinnedFolders.erase(it, m_pinnedFolders.end());
        RebuildTreeNodes();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void ExplorerApp::ShowBreadcrumbSubfolderMenu(int breadcrumbIndex) {
    if (breadcrumbIndex < 0 || breadcrumbIndex >= static_cast<int>(m_breadcrumbs.size())) return;
    const BreadcrumbSegment& seg = m_breadcrumbs[breadcrumbIndex];

    std::vector<std::pair<std::wstring, std::wstring>> subTargets;
    if (seg.path == L"THIS_PC") {
        auto drives = m_fileEngine.EnumerateDirectory(L"THIS_PC", false, false, m_lang);
        for (const auto& d : drives) {
            subTargets.push_back({ d.displayName, d.fullPath });
        }
    } else {
        auto subNodes = FileEngine::EnumerateSubfoldersForTree(seg.path, 1, false);
        for (const auto& sn : subNodes) {
            subTargets.push_back({ sn.label, sn.path });
        }
    }

    HMENU hMenu = CreatePopupMenu();
    HBITMAP hFolderBmp = m_shellEmulator.GetFolderMenuBitmap();
    if (subTargets.empty()) {
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, m_lang == Lang::PL ? L"(Brak podfolderów)" : L"(No subfolders)");
    } else {
        for (size_t i = 0; i < subTargets.size() && i < 150; ++i) {
            UINT id = CMD_BASE_BREADCRUMB + static_cast<UINT>(i);
            AppendMenuW(hMenu, MF_STRING, id, subTargets[i].first.c_str());
            if (hFolderBmp) {
                ShellMenuEmulator::AttachMenuBitmap(hMenu, id, false, hFolderBmp);
            }
        }
    }

    POINT pt = { static_cast<LONG>(seg.hitRect.left), static_cast<LONG>(seg.arrowRect.bottom) };
    ClientToScreen(m_hwnd, &pt);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd >= CMD_BASE_BREADCRUMB && cmd < CMD_BASE_BREADCRUMB + static_cast<int>(subTargets.size())) {
        NavigateTo(subTargets[cmd - CMD_BASE_BREADCRUMB].second, true);
    }
}

void ExplorerApp::RecordVisitedDirectory(const std::wstring& path) {
    std::wstring clean = PathUtils::TrimTrailingSlashes(path);
    if (clean.empty()) return;
    std::wstring nk = (clean == L"THIS_PC") ? L"__HEADER_PC__" : PathUtils::NormalizePathKey(clean);
    if (nk.empty()) return;

    auto it = std::remove_if(m_recentDirs.begin(), m_recentDirs.end(),
        [&](const std::wstring& existing) {
            std::wstring enk = (existing == L"THIS_PC") ? L"__HEADER_PC__" : PathUtils::NormalizePathKey(existing);
            return enk == nk;
        });
    m_recentDirs.erase(it, m_recentDirs.end());
    m_recentDirs.insert(m_recentDirs.begin(), clean);
    if (m_recentDirs.size() > 30) {
        m_recentDirs.resize(30);
    }
    SaveUiSettings();
}

void ExplorerApp::ShowRecentDirsDropdown(int triggerCmdId) {
    if (triggerCmdId == CMD_PANE0_RECENT_DIRS) {
        SetActivePane(0);
    } else if (triggerCmdId == CMD_PANE1_RECENT_DIRS) {
        SetActivePane(1);
    }

    const bool pl = (m_lang == Lang::PL);
    const Strings& s = GetStrings(m_lang);

    HMENU hMenu = CreatePopupMenu();
    HBITMAP hFolderBmp = m_shellEmulator.GetFolderMenuBitmap();

    wchar_t hdr[128] = {};
    swprintf_s(hdr, pl
        ? L"🕘 Ostatnio odwiedzone foldery (%zu / 30)"
        : L"🕘 Recently Visited Folders (%zu / 30)",
        m_recentDirs.size());
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, hdr);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    if (m_recentDirs.empty()) {
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, pl ? L"(Brak historii odwiedzonych folderów)" : L"(No visited folders history)");
    } else {
        for (size_t i = 0; i < m_recentDirs.size() && i < 30; ++i) {
            const std::wstring& dir = m_recentDirs[i];
            std::wstring nk = (dir == L"THIS_PC") ? L"__HEADER_PC__" : PathUtils::NormalizePathKey(dir);
            bool isCurrent = (!nk.empty() && nk == m_currentNormKey);

            std::wstring label;
            if (dir == L"THIS_PC") {
                label = std::to_wstring(i + 1) + L".   " + s.navThisPC;
            } else {
                std::wstring leaf = PathUtils::GetFileName(dir);
                if (leaf.empty() || leaf == dir) {
                    label = std::to_wstring(i + 1) + L".   " + dir;
                } else {
                    label = std::to_wstring(i + 1) + L".   " + leaf + L"     —     " + dir;
                }
            }

            UINT id = CMD_BASE_RECENT_DIRS + static_cast<UINT>(i);
            AppendMenuW(hMenu, MF_STRING | (isCurrent ? MF_CHECKED : MF_UNCHECKED), id, label.c_str());
            if (!isCurrent && hFolderBmp) {
                ShellMenuEmulator::AttachMenuBitmap(hMenu, id, false, hFolderBmp);
            }
        }
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_CLEAR_RECENT_DIRS,
            pl ? L"🗑 Wyczyść listę ostatnio odwiedzonych folderów"
               : L"🗑 Clear recently visited folders list");
    }

    POINT pt = {};
    GetCursorPos(&pt);
    UINT alignFlags = TPM_RETURNCMD | TPM_TOPALIGN | TPM_RIGHTALIGN;
    for (const auto& hit : m_ribbonHits) {
        if (hit.id == triggerCmdId) {
            // If clicked on the Ribbon "Historia" button (top < kRibbonTabsHeight + kRibbonBodyHeight), left-align;
            // if clicked on the [▾] dropdown button at the right end of the address bar, right-align to the button!
            bool isRibbonBtn = (hit.rect.bottom <= kRibbonTabsHeight + kRibbonBodyHeight + 2.0f);
            POINT bpt = {
                static_cast<LONG>(isRibbonBtn ? hit.rect.left : hit.rect.right),
                static_cast<LONG>(hit.rect.bottom)
            };
            ClientToScreen(m_hwnd, &bpt);
            pt = bpt;
            alignFlags = TPM_RETURNCMD | TPM_TOPALIGN | (isRibbonBtn ? TPM_LEFTALIGN : TPM_RIGHTALIGN);
            break;
        }
    }

    int cmd = TrackPopupMenu(hMenu, alignFlags, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd == CMD_CLEAR_RECENT_DIRS) {
        m_recentDirs.clear();
        if (!m_currentPath.empty()) {
            m_recentDirs.push_back(m_currentPath);
        }
        SaveUiSettings();
        m_statusToast = pl ? L"✓ Wyczyszczono listę ostatnio odwiedzonych folderów"
                           : L"✓ Cleared recently visited folders list";
        m_statusToastExpireTick = GetTickCount64() + 3200;
        EnsureWorkAnimTimerState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (cmd >= CMD_BASE_RECENT_DIRS && cmd < CMD_BASE_RECENT_DIRS + static_cast<int>(m_recentDirs.size())) {
        std::wstring chosen = m_recentDirs[cmd - CMD_BASE_RECENT_DIRS];
        NavigateTo(chosen, true);
    }
}

void ExplorerApp::ShowTreeContextMenu(int screenX, int screenY, int treeNodeIndex) {
    if (treeNodeIndex < 0 || treeNodeIndex >= static_cast<int>(m_treeNodes.size())) return;
    const TreeNode nodeCopy = m_treeNodes[treeNodeIndex];
    const Strings& s = GetStrings(m_lang);

    HMENU hMenu = CreatePopupMenu();

    // Expand / Collapse option
    if (nodeCopy.isHeader || nodeCopy.hasChildren) {
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_EXPAND_COLLAPSE, nodeCopy.isExpanded ? s.ctxCollapse : s.ctxExpand);
        if (nodeCopy.isHeader && nodeCopy.isQuickAccess) {
            SetMenuDefaultItem(hMenu, CMD_CTX_EXPAND_COLLAPSE, FALSE);
        }
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    }

    if (!nodeCopy.path.empty()) {
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_OPEN, s.ctxOpen);
        SetMenuDefaultItem(hMenu, CMD_CTX_OPEN, FALSE);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_OPEN_NEW_WINDOW, s.ctxOpenNewWindow);
        if (HBITMAP hExpBmp = m_shellEmulator.GetExplorerMenuBitmap()) {
            ShellMenuEmulator::AttachMenuBitmap(hMenu, CMD_CTX_OPEN_NEW_WINDOW, false, hExpBmp);
        }
    }

    std::vector<EmulatedShellVerb> regVerbs;
    if (!nodeCopy.path.empty() && nodeCopy.path != L"THIS_PC") {
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        if (nodeCopy.isQuickAccess || IsFolderPinnedToQuickAccess(nodeCopy.path)) {
            AppendMenuW(hMenu, MF_STRING, CMD_CTX_UNPIN_QUICK, s.ctxUnpinQuickAccess);
        } else {
            AppendMenuW(hMenu, MF_STRING, CMD_CTX_PIN_QUICK, s.ctxPinQuickAccess);
        }

        AppendMenuW(hMenu, MF_STRING, CMD_CTX_TERMINAL, s.ctxOpenTerminal);
        if (HBITMAP hTermBmp = m_shellEmulator.GetTerminalMenuBitmap()) {
            ShellMenuEmulator::AttachMenuBitmap(hMenu, CMD_CTX_TERMINAL, false, hTermBmp);
        }

        if (m_shellEmulator.Has7Zip() && !nodeCopy.isOffline) {
            HMENU h7z = CreatePopupMenu();
            std::wstring stem = PathUtils::GetFileName(nodeCopy.path);
            if (stem.empty()) stem = L"Archive";
            AppendMenuW(h7z, MF_STRING, CMD_BASE_7ZIP + SZ_ADD_DIALOG, m_lang == Lang::PL ? L"Dodaj do archiwum..." : L"Add to archive...");
            std::wstring add7z = (m_lang == Lang::PL ? L"Dodaj do \"" : L"Add to \"") + stem + L".7z\"";
            std::wstring addZip = (m_lang == Lang::PL ? L"Dodaj do \"" : L"Add to \"") + stem + L".zip\"";
            AppendMenuW(h7z, MF_STRING, CMD_BASE_7ZIP + SZ_ADD_7Z, add7z.c_str());
            AppendMenuW(h7z, MF_STRING, CMD_BASE_7ZIP + SZ_ADD_ZIP, addZip.c_str());
            AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(h7z), L"7-Zip");
            if (HBITMAP h7zBmp = m_shellEmulator.Get7ZipMenuBitmap()) {
                ShellMenuEmulator::AttachMenuBitmap(hMenu, static_cast<UINT>(GetMenuItemCount(hMenu) - 1), true, h7zBmp);
            }
        }

        regVerbs = m_shellEmulator.GetDiscoveredVerbs(false, true, false);
        if (!regVerbs.empty()) {
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            for (size_t i = 0; i < regVerbs.size() && i < 20; ++i) {
                UINT id = CMD_BASE_REG_VERBS + static_cast<UINT>(i);
                AppendMenuW(hMenu, MF_STRING, id, regVerbs[i].label.c_str());
                if (regVerbs[i].hMenuBmp) {
                    ShellMenuEmulator::AttachMenuBitmap(hMenu, id, false, regVerbs[i].hMenuBmp);
                }
            }
        }

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_COPY_PATH, s.ctxCopyPath);
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_PROPERTIES, s.ctxProperties);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_EXPLORER, s.ctxOpenExplorer);
        if (HBITMAP hExpBmp = m_shellEmulator.GetExplorerMenuBitmap()) {
            ShellMenuEmulator::AttachMenuBitmap(hMenu, CMD_CTX_EXPLORER, false, hExpBmp);
        }
    }

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screenX, screenY, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
    if (cmd <= 0) return;

    if (cmd == CMD_CTX_EXPAND_COLLAPSE) {
        ToggleTreeNodeExpand(treeNodeIndex);
        return;
    }
    if (cmd == CMD_CTX_OPEN && !nodeCopy.path.empty()) {
        NavigateTo(nodeCopy.path, true);
        return;
    }
    if (cmd == CMD_CTX_OPEN_NEW_WINDOW && !nodeCopy.path.empty()) {
        wchar_t selfExe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, selfExe, MAX_PATH);
        std::wstring qArg = L"\"" + nodeCopy.path + L"\"";
        ShellExecuteW(nullptr, L"open", selfExe, qArg.c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    if (cmd == CMD_CTX_PIN_QUICK) {
        PinFolderToQuickAccess(nodeCopy.path);
        return;
    }
    if (cmd == CMD_CTX_UNPIN_QUICK) {
        UnpinFolderFromQuickAccess(nodeCopy.path);
        return;
    }
    if (cmd == CMD_CTX_TERMINAL) {
        FileEngine::OpenTerminalInFolder(nodeCopy.path);
        return;
    }
    if (cmd == CMD_CTX_COPY_PATH) {
        FileEngine::CopyTextToClipboard(m_hwnd, L"\"" + nodeCopy.path + L"\"");
        return;
    }
    if (cmd == CMD_CTX_PROPERTIES) {
        FileEngine::OpenFileOrProperties(m_hwnd, nodeCopy.path, true);
        return;
    }
    if (cmd == CMD_CTX_EXPLORER) {
        FileEngine::OpenInClassicExplorer(nodeCopy.path, false);
        return;
    }
    if (cmd >= CMD_BASE_7ZIP && cmd < CMD_BASE_7ZIP + 20) {
        m_shellEmulator.Execute7ZipAction(cmd - CMD_BASE_7ZIP, { nodeCopy.path }, PathUtils::GetParentPath(nodeCopy.path));
        return;
    }
    if (cmd >= CMD_BASE_REG_VERBS && cmd < CMD_BASE_REG_VERBS + static_cast<int>(regVerbs.size())) {
        ShellMenuEmulator::ExecuteVerbCommand(regVerbs[cmd - CMD_BASE_REG_VERBS], nodeCopy.path);
        return;
    }
}

void ExplorerApp::ShowContextMenu(int screenX, int screenY, int hitIndex) {
    const Strings& s = GetStrings(m_lang);

    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        auto selPaths = GetSelectedPaths();
        if (!selPaths.empty()) {
            ShellMenuEmulator::ShowNativeShellContextMenu(m_hwnd, selPaths, screenX, screenY);
            RefreshCurrentFolder();
            return;
        }
    }

    HMENU hMenu = CreatePopupMenu();
    std::vector<EmulatedShellVerb> regVerbs;
    std::vector<OpenWithApp> openWithApps;
    std::vector<SendToTarget> sendToTargets = m_shellEmulator.GetSendToTargets(m_lang);

    if (hitIndex >= 0 && hitIndex < static_cast<int>(m_items.size())) {
        const FileEntry& item = m_items[hitIndex];
        bool isFolder = item.isDirectory;
        bool isImg    = item.isImage;

        AppendMenuW(hMenu, MF_STRING, CMD_CTX_OPEN, s.ctxOpen);
        SetMenuDefaultItem(hMenu, CMD_CTX_OPEN, FALSE);

        if (isFolder) {
            AppendMenuW(hMenu, MF_STRING, CMD_CTX_OPEN_NEW_WINDOW, s.ctxOpenNewWindow);
            if (HBITMAP hExpBmp = m_shellEmulator.GetExplorerMenuBitmap()) {
                ShellMenuEmulator::AttachMenuBitmap(hMenu, CMD_CTX_OPEN_NEW_WINDOW, false, hExpBmp);
            }
            if (IsFolderPinnedToQuickAccess(item.fullPath)) {
                AppendMenuW(hMenu, MF_STRING, CMD_CTX_UNPIN_QUICK, s.ctxUnpinQuickAccess);
            } else {
                AppendMenuW(hMenu, MF_STRING, CMD_CTX_PIN_QUICK, s.ctxPinQuickAccess);
            }
            AppendMenuW(hMenu, MF_STRING, CMD_CTX_TERMINAL, s.ctxOpenTerminal);
            if (HBITMAP hTermBmp = m_shellEmulator.GetTerminalMenuBitmap()) {
                ShellMenuEmulator::AttachMenuBitmap(hMenu, CMD_CTX_TERMINAL, false, hTermBmp);
            }
        } else if (isImg || item.isVideo || item.isAudio) {
            AppendMenuW(hMenu, MF_STRING, CMD_CTX_FULLVIEWER, s.ctxFullViewer);
            AppendMenuW(hMenu, MF_STRING, CMD_CTX_FULLSCREEN, s.ctxFullScreen);
            if (isImg) {
                AppendMenuW(hMenu, MF_STRING, CMD_CTX_EDIT_IMAGE, s.ctxEdit);
                AppendMenuW(hMenu, MF_STRING, CMD_CTX_SET_WALLPAPER, s.ctxSetWallpaper);
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                std::vector<std::wstring> selImgs = GetSelectedImagePaths(hitIndex);
                if (!selImgs.empty()) {
                    HMENU hImgTools = BuildImageToolsSubmenu(selImgs);
                    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hImgTools),
                        m_lang == Lang::PL ? L"\xD83D\xDCD0 Narzędzia obrazu (Rozmiar / Limit KB / Format)"
                                           : L"\xD83D\xDCD0 Image Tools (Resize / Target KB / Format)");
                }
                AppendMenuW(hMenu, MF_STRING, CMD_CTX_ROTATE_CW, s.ctxRotateRight);
                AppendMenuW(hMenu, MF_STRING, CMD_CTX_ROTATE_CCW, s.ctxRotateLeft);
            }
        }

        if (!isFolder) {
            openWithApps = m_shellEmulator.GetOpenWithApps(isImg);
            HMENU hOpenWith = CreatePopupMenu();
            for (size_t i = 0; i < openWithApps.size() && i < 25; ++i) {
                UINT id = CMD_BASE_OPENWITH + static_cast<UINT>(i);
                AppendMenuW(hOpenWith, MF_STRING, id, openWithApps[i].name.c_str());
                if (openWithApps[i].hMenuBmp) {
                    ShellMenuEmulator::AttachMenuBitmap(hOpenWith, id, false, openWithApps[i].hMenuBmp);
                }
            }
            if (!openWithApps.empty()) {
                AppendMenuW(hOpenWith, MF_SEPARATOR, 0, nullptr);
            }
            AppendMenuW(hOpenWith, MF_STRING, CMD_CTX_OPEN_WITH_DIALOG, s.ctxChooseAnotherApp);
            AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hOpenWith), s.ctxOpenWith);
        }

        if (m_shellEmulator.Has7Zip()) {
            HMENU h7z = CreatePopupMenu();
            std::wstring stem = item.displayName;
            bool isArchive = (item.ext == L".zip" || item.ext == L".7z" || item.ext == L".rar" ||
                              item.ext == L".tar" || item.ext == L".gz" || item.ext == L".iso");
            if (!isFolder && isArchive) {
                AppendMenuW(h7z, MF_STRING, CMD_BASE_7ZIP + SZ_OPEN_ARCHIVE, m_lang == Lang::PL ? L"Otwórz archiwum" : L"Open archive");
                AppendMenuW(h7z, MF_STRING, CMD_BASE_7ZIP + SZ_EXTRACT_FILES, m_lang == Lang::PL ? L"Wypakuj pliki..." : L"Extract files...");
                AppendMenuW(h7z, MF_STRING, CMD_BASE_7ZIP + SZ_EXTRACT_HERE, m_lang == Lang::PL ? L"Wypakuj tutaj" : L"Extract Here");
                std::wstring extSub = (m_lang == Lang::PL ? L"Wypakuj do \"" : L"Extract to \"") + stem + L"\\\"";
                AppendMenuW(h7z, MF_STRING, CMD_BASE_7ZIP + SZ_EXTRACT_SUBFOLDER, extSub.c_str());
                AppendMenuW(h7z, MF_SEPARATOR, 0, nullptr);
            }
            AppendMenuW(h7z, MF_STRING, CMD_BASE_7ZIP + SZ_ADD_DIALOG, m_lang == Lang::PL ? L"Dodaj do archiwum..." : L"Add to archive...");
            std::wstring add7z = (m_lang == Lang::PL ? L"Dodaj do \"" : L"Add to \"") + stem + L".7z\"";
            std::wstring addZip = (m_lang == Lang::PL ? L"Dodaj do \"" : L"Add to \"") + stem + L".zip\"";
            AppendMenuW(h7z, MF_STRING, CMD_BASE_7ZIP + SZ_ADD_7Z, add7z.c_str());
            AppendMenuW(h7z, MF_STRING, CMD_BASE_7ZIP + SZ_ADD_ZIP, addZip.c_str());
            AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(h7z), L"7-Zip");
            if (HBITMAP h7zBmp = m_shellEmulator.Get7ZipMenuBitmap()) {
                ShellMenuEmulator::AttachMenuBitmap(hMenu, static_cast<UINT>(GetMenuItemCount(hMenu) - 1), true, h7zBmp);
            }
        }

        regVerbs = m_shellEmulator.GetDiscoveredVerbs(!isFolder, isFolder, false);
        if (!regVerbs.empty()) {
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            for (size_t i = 0; i < regVerbs.size() && i < 20; ++i) {
                UINT id = CMD_BASE_REG_VERBS + static_cast<UINT>(i);
                AppendMenuW(hMenu, MF_STRING, id, regVerbs[i].label.c_str());
                if (regVerbs[i].hMenuBmp) {
                    ShellMenuEmulator::AttachMenuBitmap(hMenu, id, false, regVerbs[i].hMenuBmp);
                }
            }
        }

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        HMENU hSendTo = CreatePopupMenu();
        for (size_t i = 0; i < sendToTargets.size() && i < 40; ++i) {
            UINT id = CMD_BASE_SENDTO + static_cast<UINT>(i);
            AppendMenuW(hSendTo, MF_STRING, id, sendToTargets[i].label.c_str());
            if (sendToTargets[i].hMenuBmp) {
                ShellMenuEmulator::AttachMenuBitmap(hSendTo, id, false, sendToTargets[i].hMenuBmp);
            }
        }
        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSendTo), s.ctxSendTo);

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_CUT, s.ctxCut);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_COPY, s.ctxCopy);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_COPY_PATH, s.ctxCopyPath);

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_CREATE_SHORTCUT, s.ctxCreateShortcut);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_DELETE_RECYCLE, s.ctxDeleteRecycle);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_DELETE_PERM, s.ctxDeletePerm);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_RENAME, s.ctxRename);

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_PROPERTIES, s.ctxProperties);
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_EXPLORER, s.ctxOpenExplorer);
        if (HBITMAP hExpBmp = m_shellEmulator.GetExplorerMenuBitmap()) {
            ShellMenuEmulator::AttachMenuBitmap(hMenu, CMD_CTX_EXPLORER, false, hExpBmp);
        }
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_NATIVE_SHELL_MENU, s.ctxNativeShellMenu);
    } else {
        HMENU hViewMenu = CreatePopupMenu();
        auto addViewOption = [&](int cmdId, const wchar_t* label, bool isCurrent) {
            AppendMenuW(hViewMenu, MF_STRING | (isCurrent ? MF_CHECKED : MF_UNCHECKED), cmdId, label);
        };
        addViewOption(CMD_VIEW_JUMBO,       s.viewJumbo,       m_viewMode == ViewMode::JumboIcons);
        addViewOption(CMD_VIEW_EXTRALARGE,  s.viewExtraLarge,  m_viewMode == ViewMode::ExtraLargeIcons);
        addViewOption(CMD_VIEW_LARGE,       s.viewLarge,       m_viewMode == ViewMode::LargeIcons);
        addViewOption(CMD_VIEW_MEDIUM,      s.viewMedium,      m_viewMode == ViewMode::MediumIcons);
        addViewOption(CMD_VIEW_SMALL,       s.viewSmall,       m_viewMode == ViewMode::SmallIcons);
        addViewOption(CMD_VIEW_LIST,        s.viewList,        m_viewMode == ViewMode::List);
        addViewOption(CMD_VIEW_DETAILS,     s.viewDetails,     m_viewMode == ViewMode::Details);
        addViewOption(CMD_VIEW_PROPORTIONS, s.viewProportions, m_viewMode == ViewMode::Proportions);
        addViewOption(CMD_VIEW_PIECHART,    s.viewPieChart,    m_viewMode == ViewMode::PieChart);
        AppendMenuW(hViewMenu, MF_SEPARATOR, 0, nullptr);
        addViewOption(CMD_VIEW_FULLVIEWER, s.viewFullViewer, m_viewMode == ViewMode::FullViewer);
        addViewOption(CMD_VIEW_FULLSCREEN, s.viewFullScreen, m_isFullScreen);
        AppendMenuW(hViewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hViewMenu, MF_STRING | (m_showNavPane ? MF_CHECKED : MF_UNCHECKED), CMD_TOGGLE_NAVPANE,
                    m_lang == Lang::PL ? L"Okienko nawigacji" : L"Navigation pane");
        AppendMenuW(hViewMenu, MF_STRING | (m_showPreviewPane ? MF_CHECKED : MF_UNCHECKED), CMD_TOGGLE_PREVIEWPANE,
                    m_lang == Lang::PL ? L"Okienko podglądu" : L"Preview pane");
        AppendMenuW(hViewMenu, MF_STRING | (m_showExtensions ? MF_CHECKED : MF_UNCHECKED), CMD_TOGGLE_EXT, s.chkFileExt);
        AppendMenuW(hViewMenu, MF_STRING | (m_showHidden ? MF_CHECKED : MF_UNCHECKED), CMD_TOGGLE_HIDDEN, s.chkHiddenItems);
        AppendMenuW(hViewMenu, MF_STRING | (m_gitEnabled ? MF_CHECKED : MF_UNCHECKED), CMD_TOGGLE_GIT,
                    m_lang == Lang::PL ? L"Integracja GIT" : L"Git integration");
        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hViewMenu), s.ctxViewSubmenu);

        HMENU hSortMenu = CreatePopupMenu();
        AppendMenuW(hSortMenu, MF_STRING | (m_sortCol == SortColumn::Name ? MF_CHECKED : 0), CMD_SORT_NAME, s.colName);
        AppendMenuW(hSortMenu, MF_STRING | (m_sortCol == SortColumn::DateModified ? MF_CHECKED : 0), CMD_SORT_DATE, s.colDateModified);
        AppendMenuW(hSortMenu, MF_STRING | (m_sortCol == SortColumn::Type ? MF_CHECKED : 0), CMD_SORT_TYPE, s.colType);
        AppendMenuW(hSortMenu, MF_STRING | (m_sortCol == SortColumn::Size ? MF_CHECKED : 0), CMD_SORT_SIZE, s.colSize);
        AppendMenuW(hSortMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hSortMenu, MF_STRING | (m_sortAsc ? MF_CHECKED : 0), CMD_SORT_ASC, s.ctxSortAsc);
        AppendMenuW(hSortMenu, MF_STRING | (!m_sortAsc ? MF_CHECKED : 0), CMD_SORT_DESC, s.ctxSortDesc);
        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSortMenu), s.ctxSortBySubmenu);

        HMENU hGroupMenu = CreatePopupMenu();
        AppendMenuW(hGroupMenu, MF_STRING | MF_CHECKED, CMD_NONE, s.ctxGroupNone);
        AppendMenuW(hGroupMenu, MF_STRING, CMD_SORT_NAME, s.colName);
        AppendMenuW(hGroupMenu, MF_STRING, CMD_SORT_DATE, s.colDateModified);
        AppendMenuW(hGroupMenu, MF_STRING, CMD_SORT_TYPE, s.colType);
        AppendMenuW(hGroupMenu, MF_STRING, CMD_SORT_SIZE, s.colSize);
        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hGroupMenu), s.ctxGroupBySubmenu);

        AppendMenuW(hMenu, MF_STRING, CMD_CTX_REFRESH, s.ctxRefresh);

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        bool hasClipFiles = IsClipboardFormatAvailable(CF_HDROP) != FALSE;
        AppendMenuW(hMenu, MF_STRING | (hasClipFiles ? MF_ENABLED : MF_GRAYED), CMD_CTX_PASTE, s.ctxPaste);
        AppendMenuW(hMenu, MF_STRING | (hasClipFiles ? MF_ENABLED : MF_GRAYED), CMD_CTX_PASTE_SHORTCUT, s.ctxPasteShortcut);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_UNDO_DELETE, s.ctxUndoDelete);

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_TERMINAL, s.ctxOpenTerminal);
        if (HBITMAP hTermBmp = m_shellEmulator.GetTerminalMenuBitmap()) {
            ShellMenuEmulator::AttachMenuBitmap(hMenu, CMD_CTX_TERMINAL, false, hTermBmp);
        }
        if (IsFolderPinnedToQuickAccess(m_currentPath)) {
            AppendMenuW(hMenu, MF_STRING, CMD_CTX_UNPIN_QUICK, s.ctxUnpinQuickAccess);
        } else {
            AppendMenuW(hMenu, MF_STRING, CMD_CTX_PIN_QUICK, s.ctxPinQuickAccess);
        }
        regVerbs = m_shellEmulator.GetDiscoveredVerbs(false, false, true);
        for (size_t i = 0; i < regVerbs.size() && i < 20; ++i) {
            UINT id = CMD_BASE_REG_VERBS + static_cast<UINT>(i);
            AppendMenuW(hMenu, MF_STRING, id, regVerbs[i].label.c_str());
            if (regVerbs[i].hMenuBmp) {
                ShellMenuEmulator::AttachMenuBitmap(hMenu, id, false, regVerbs[i].hMenuBmp);
            }
        }

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        HMENU hNewMenu = CreatePopupMenu();
        AppendMenuW(hNewMenu, MF_STRING, CMD_NEW_FOLDER_CTX, s.ctxNewFolder);
        if (HBITMAP hFldBmp = m_shellEmulator.GetFolderMenuBitmap()) {
            ShellMenuEmulator::AttachMenuBitmap(hNewMenu, CMD_NEW_FOLDER_CTX, false, hFldBmp);
        }
        AppendMenuW(hNewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hNewMenu, MF_STRING, CMD_NEW_BMP_FILE, s.ctxNewBmp);
        AppendMenuW(hNewMenu, MF_STRING, CMD_NEW_TXT_FILE, s.ctxNewTxt);
        AppendMenuW(hNewMenu, MF_STRING, CMD_NEW_PY_FILE, s.ctxNewPy);
        AppendMenuW(hNewMenu, MF_STRING, CMD_NEW_MD_FILE, s.ctxNewMd);
        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hNewMenu), s.ctxNew);

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_PROPERTIES, s.ctxProperties);
        AppendMenuW(hMenu, MF_STRING, CMD_CTX_EXPLORER, s.ctxOpenExplorer);
        if (HBITMAP hExpBmp = m_shellEmulator.GetExplorerMenuBitmap()) {
            ShellMenuEmulator::AttachMenuBitmap(hMenu, CMD_CTX_EXPLORER, false, hExpBmp);
        }
    }

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screenX, screenY, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
    if (cmd <= 0) return;

    if (cmd >= CMD_IMG_TOGGLE_OVERWRITE && cmd <= CMD_IMG_CONVERT_BMP) {
        HandleImageToolCommand(cmd, hitIndex);
        return;
    }

    if (cmd == CMD_TOGGLE_GIT) {
        ExecuteRibbonCommand(cmd);
        return;
    }

    if (cmd >= CMD_TOGGLE_NAVPANE && cmd <= CMD_VIEW_PIECHART) {
        ExecuteRibbonCommand(cmd);
        return;
    }

    if (cmd >= CMD_SORT_NAME && cmd <= CMD_SORT_DESC) {
        if (cmd == CMD_SORT_NAME) m_sortCol = SortColumn::Name;
        else if (cmd == CMD_SORT_DATE) m_sortCol = SortColumn::DateModified;
        else if (cmd == CMD_SORT_TYPE) m_sortCol = SortColumn::Type;
        else if (cmd == CMD_SORT_SIZE) m_sortCol = SortColumn::Size;
        else if (cmd == CMD_SORT_ASC)  m_sortAsc = true;
        else if (cmd == CMD_SORT_DESC) m_sortAsc = false;
        ApplyFilterAndSort();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (cmd == CMD_NEW_FOLDER_CTX) {
        CreateNewFolderAction();
        return;
    }
    if (cmd >= CMD_NEW_TXT_FILE && cmd <= CMD_NEW_MD_FILE) {
        std::wstring ext = L".txt";
        std::wstring base = (m_lang == Lang::PL) ? L"Nowy dokument tekstowy" : L"New Text Document";
        if (cmd == CMD_NEW_BMP_FILE) { ext = L".bmp"; base = (m_lang == Lang::PL) ? L"Nowy obraz" : L"New Bitmap Image"; }
        else if (cmd == CMD_NEW_PY_FILE) { ext = L".py"; base = L"script"; }
        else if (cmd == CMD_NEW_MD_FILE) { ext = L".md"; base = L"README"; }

        std::wstring created;
        if (ShellMenuEmulator::CreateNewEmptyFile(m_currentPath, base, ext, created)) {
            RefreshCurrentFolder(created);
            StartInlineRename();
        }
        return;
    }

    if (cmd >= CMD_BASE_7ZIP && cmd < CMD_BASE_7ZIP + 20) {
        m_shellEmulator.Execute7ZipAction(cmd - CMD_BASE_7ZIP, GetSelectedPaths(), m_currentPath);
        return;
    }

    if (cmd >= CMD_BASE_OPENWITH && cmd < CMD_BASE_OPENWITH + static_cast<int>(openWithApps.size())) {
        const auto& app = openWithApps[cmd - CMD_BASE_OPENWITH];
        auto paths = GetSelectedPaths();
        if (!paths.empty()) {
            std::wstring qArg = L"\"" + paths[0] + L"\"";
            ShellExecuteW(m_hwnd, L"open", app.exePath.c_str(), qArg.c_str(), m_currentPath.c_str(), SW_SHOWNORMAL);
        }
        return;
    }

    if (cmd >= CMD_BASE_SENDTO && cmd < CMD_BASE_SENDTO + static_cast<int>(sendToTargets.size())) {
        const auto& target = sendToTargets[cmd - CMD_BASE_SENDTO];
        auto paths = GetSelectedPaths();
        if (!paths.empty()) {
            if (target.isDesktopShortcut) {
                PWSTR pszDesk = nullptr;
                if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &pszDesk)) && pszDesk) {
                    std::wstring deskDir = PathUtils::EnsureTrailingSlash(pszDesk);
                    CoTaskMemFree(pszDesk);
                    for (const auto& p : paths) {
                        std::wstring bn = PathUtils::GetFileName(p);
                        std::wstring lnk = deskDir + bn + L" - skrót.lnk";
                        ShellMenuEmulator::CreateShortcutFile(p, lnk);
                    }
                }
            } else if (target.isZipArchive) {
                if (m_shellEmulator.Has7Zip()) {
                    m_shellEmulator.Execute7ZipAction(SZ_ADD_ZIP, paths, m_currentPath);
                } else {
                    std::wstring bn = PathUtils::GetFileName(paths[0]);
                    std::wstring zipPath = PathUtils::EnsureTrailingSlash(m_currentPath) + bn + L".zip";
                    std::wstring psCmd = L"-NoProfile -Command \"Compress-Archive -Force -LiteralPath '" + paths[0] + L"' -DestinationPath '" + zipPath + L"'\"";
                    ShellExecuteW(nullptr, L"open", L"powershell.exe", psCmd.c_str(), m_currentPath.c_str(), SW_HIDE);
                }
            } else {
                std::wstring qArg = L"\"" + paths[0] + L"\"";
                ShellExecuteW(m_hwnd, L"open", target.targetPath.c_str(), qArg.c_str(), m_currentPath.c_str(), SW_SHOWNORMAL);
            }
        }
        return;
    }

    if (cmd >= CMD_BASE_REG_VERBS && cmd < CMD_BASE_REG_VERBS + static_cast<int>(regVerbs.size())) {
        const auto& verb = regVerbs[cmd - CMD_BASE_REG_VERBS];
        std::wstring targetPath = m_currentPath;
        if (hitIndex >= 0 && hitIndex < static_cast<int>(m_items.size())) {
            targetPath = m_items[hitIndex].fullPath;
        }
        ShellMenuEmulator::ExecuteVerbCommand(verb, targetPath);
        return;
    }

    switch (cmd) {
    case CMD_CTX_OPEN:
        if (hitIndex >= 0 && hitIndex < static_cast<int>(m_items.size())) {
            if (m_items[hitIndex].isDirectory) NavigateTo(m_items[hitIndex].fullPath, true);
            else if (m_items[hitIndex].isImage || m_items[hitIndex].isVideo || m_items[hitIndex].isAudio) ToggleFullViewerMode();
            else FileEngine::OpenFileOrProperties(m_hwnd, m_items[hitIndex].fullPath, false);
        }
        break;
    case CMD_CTX_OPEN_NEW_WINDOW:
        if (hitIndex >= 0 && hitIndex < static_cast<int>(m_items.size())) {
            wchar_t selfExe[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, selfExe, MAX_PATH);
            std::wstring qArg = L"\"" + m_items[hitIndex].fullPath + L"\"";
            ShellExecuteW(nullptr, L"open", selfExe, qArg.c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    case CMD_CTX_PIN_QUICK:
        if (hitIndex >= 0 && hitIndex < static_cast<int>(m_items.size()) && m_items[hitIndex].isDirectory) {
            PinFolderToQuickAccess(m_items[hitIndex].fullPath);
        } else {
            PinFolderToQuickAccess(m_currentPath);
        }
        break;
    case CMD_CTX_UNPIN_QUICK:
        if (hitIndex >= 0 && hitIndex < static_cast<int>(m_items.size()) && m_items[hitIndex].isDirectory) {
            UnpinFolderFromQuickAccess(m_items[hitIndex].fullPath);
        } else {
            UnpinFolderFromQuickAccess(m_currentPath);
        }
        break;
    case CMD_CTX_FULLVIEWER:     ToggleFullViewerMode(); break;
    case CMD_CTX_FULLSCREEN:     ToggleFullScreen(); break;
    case CMD_CTX_EDIT_IMAGE:
        if (hitIndex >= 0 && hitIndex < static_cast<int>(m_items.size())) {
            std::wstring qArg = L"\"" + m_items[hitIndex].fullPath + L"\"";
            ShellExecuteW(m_hwnd, L"open", L"mspaint.exe", qArg.c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    case CMD_CTX_SET_WALLPAPER:
        if (hitIndex >= 0 && hitIndex < static_cast<int>(m_items.size())) {
            SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t*>(m_items[hitIndex].fullPath.c_str()), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
        }
        break;
    case CMD_CTX_ROTATE_CW:
    case CMD_CTX_ROTATE_CCW: {
        std::vector<std::wstring> toRotate = GetSelectedPaths();
        if (toRotate.empty() && hitIndex >= 0 && hitIndex < static_cast<int>(m_items.size())) {
            toRotate.push_back(m_items[hitIndex].fullPath);
        }
        if (!toRotate.empty()) {
            m_imageCache.QueueRotateImages(toRotate, cmd == CMD_CTX_ROTATE_CW);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        break;
    }
    case CMD_CTX_OPEN_WITH_DIALOG:
        if (hitIndex >= 0 && hitIndex < static_cast<int>(m_items.size())) {
            ShellMenuEmulator::ShowOpenWithDialog(m_hwnd, m_items[hitIndex].fullPath);
        }
        break;
    case CMD_CTX_CREATE_SHORTCUT:
        if (hitIndex >= 0 && hitIndex < static_cast<int>(m_items.size())) {
            const auto& it = m_items[hitIndex];
            std::wstring lnkPath = PathUtils::EnsureTrailingSlash(m_currentPath) + it.name + (m_lang == Lang::PL ? L" - skrót.lnk" : L" - Shortcut.lnk");
            if (ShellMenuEmulator::CreateShortcutFile(it.fullPath, lnkPath)) {
                RefreshCurrentFolder(lnkPath);
            }
        }
        break;
    case CMD_CTX_PASTE_SHORTCUT: {
        if (OpenClipboard(m_hwnd)) {
            HDROP hDrop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
            if (hDrop) {
                UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < count; ++i) {
                    UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
                    std::wstring src(len, L'\0');
                    DragQueryFileW(hDrop, i, src.data(), len + 1);
                    std::wstring bn = PathUtils::GetFileName(src);
                    std::wstring lnk = PathUtils::EnsureTrailingSlash(m_currentPath) + bn + (m_lang == Lang::PL ? L" - skrót.lnk" : L" - Shortcut.lnk");
                    ShellMenuEmulator::CreateShortcutFile(src, lnk);
                }
            }
            CloseClipboard();
            RefreshCurrentFolder();
        }
        break;
    }
    case CMD_CTX_EXPLORER:
        if (hitIndex >= 0 && hitIndex < static_cast<int>(m_items.size()))
            FileEngine::OpenInClassicExplorer(m_items[hitIndex].fullPath, true);
        else
            FileEngine::OpenInClassicExplorer(m_currentPath, false);
        break;
    case CMD_CTX_TERMINAL:
        if (hitIndex >= 0 && hitIndex < static_cast<int>(m_items.size()) && m_items[hitIndex].isDirectory)
            FileEngine::OpenTerminalInFolder(m_items[hitIndex].fullPath);
        else
            FileEngine::OpenTerminalInFolder(m_currentPath);
        break;
    case CMD_CTX_CUT:            CopySelected(true); break;
    case CMD_CTX_COPY:           CopySelected(false); break;
    case CMD_CTX_PASTE:          PasteIntoCurrent(); break;
    case CMD_CTX_COPY_PATH:      CopySelectedPaths(); break;
    case CMD_CTX_DELETE_RECYCLE: DeleteSelectedItems(false); break;
    case CMD_CTX_DELETE_PERM:    DeleteSelectedItems(true); break;
    case CMD_CTX_RENAME:         StartInlineRename(); break;
    case CMD_CTX_UNDO_DELETE:    PerformUndoDelete(); break;
    case CMD_CTX_NATIVE_SHELL_MENU: {
        auto selPaths = GetSelectedPaths();
        if (!selPaths.empty()) {
            ShellMenuEmulator::ShowNativeShellContextMenu(m_hwnd, selPaths, screenX, screenY);
            RefreshCurrentFolder();
        }
        break;
    }
    case CMD_CTX_PROPERTIES: {
        auto paths = GetSelectedPaths();
        if (!paths.empty() && hitIndex >= 0) FileEngine::OpenFileOrProperties(m_hwnd, paths[0], true);
        else FileEngine::OpenFileOrProperties(m_hwnd, m_currentPath, true);
        break;
    }
    case CMD_CTX_REFRESH:        RefreshCurrentFolder(); break;
    }
}

void ExplorerApp::ToggleFullViewerMode() {
    if (m_viewMode == ViewMode::FullViewer) {
        if (m_isFullScreen) ToggleFullScreen();
        m_mediaPlayer.CloseMedia();
        KillTimer(m_hwnd, ID_TIMER_MEDIA);
        m_viewMode = m_lastNonViewerMode;
        EnsureItemVisible(m_focusedIndex);
    } else {
        m_lastNonViewerMode = m_viewMode;
        m_viewMode = ViewMode::FullViewer;
        if (m_focusedIndex < 0 ||
            (!m_items[m_focusedIndex].isImage && !m_items[m_focusedIndex].isVideo && !m_items[m_focusedIndex].isAudio)) {
            for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
                if (m_items[i].isImage || m_items[i].isVideo || m_items[i].isAudio) {
                    SelectSingle(i, true);
                    break;
                }
            }
            if (m_focusedIndex < 0 && !m_items.empty()) SelectSingle(0, true);
        }
        ResetViewerZoom();
        EnsureItemVisible(m_focusedIndex);
        SyncMediaPlayerToSelection();
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::ToggleFullScreen() {
    DWORD style = GetWindowLongW(m_hwnd, GWL_STYLE);
    if (!m_isFullScreen) {
        if (m_viewMode != ViewMode::FullViewer) {
            m_lastNonViewerMode = m_viewMode;
            m_viewMode = ViewMode::FullViewer;
            if (m_focusedIndex < 0 && !m_items.empty()) SelectSingle(0, true);
        }
        GetWindowPlacement(m_hwnd, &m_prevWndPlacement);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongW(m_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(m_hwnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_isFullScreen = true;
        }
        SyncMediaPlayerToSelection();
    } else {
        SetWindowLongW(m_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(m_hwnd, &m_prevWndPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        m_isFullScreen = false;
    }
    ResetViewerZoom();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::StepViewerImage(int delta) {
    if (m_items.empty()) return;
    int cur = (m_focusedIndex >= 0) ? m_focusedIndex : 0;

    int count = static_cast<int>(m_items.size());
    for (int step = 1; step <= count; ++step) {
        int candidate = cur + delta * step;
        if (candidate < 0 || candidate >= count) break;
        if (m_items[candidate].isImage || m_items[candidate].isVideo || m_items[candidate].isAudio) {
            SelectSingle(candidate, true);
            ResetViewerZoom();
            EnsureItemVisible(candidate);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    int fallback = std::clamp(cur + delta, 0, count - 1);
    SelectSingle(fallback, true);
    ResetViewerZoom();
    EnsureItemVisible(fallback);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::ResetViewerZoom() {
    m_viewerZoom = 1.0f;
    m_viewerActualPixels = false;
    m_viewerPanX = 0.0f;
    m_viewerPanY = 0.0f;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::SetViewerZoom100() {
    m_viewerActualPixels = true;
    m_viewerZoom = 1.0f;
    m_viewerPanX = 0.0f;
    m_viewerPanY = 0.0f;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::TriggerFolderSizeCalculation() {
    if (m_currentPath.empty() || m_currentPath == L"THIS_PC") return;

    bool anyAlreadyCached = m_fileEngine.HasAnyCachedFolderSizes(m_allItems);
    m_fileEngine.RequestFolderSizes(m_allItems, anyAlreadyCached);
    SyncFolderSizesFromCache();
    EnsureWorkAnimTimerState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::SyncFolderSizesFromCache() {
    if (m_currentPath == L"THIS_PC") return;

    // Single-lock batch sync for m_allItems and m_items!
    m_fileEngine.SyncDirectorySizes(m_allItems);
    m_fileEngine.SyncDirectorySizes(m_items);

    if (m_sortCol == SortColumn::Size) {
        FileEngine::SortEntries(m_items, m_sortCol, m_sortAsc);
    }
}

void ExplorerApp::EnsureWorkAnimTimerState() {
    bool folderScanning = m_fileEngine.IsAnyFolderSizeCalculating();
    if (m_wasFolderScanning && !folderScanning) {
        m_folderScanFinishedTick = GetTickCount64();
    }
    m_wasFolderScanning = folderScanning;

    bool searchRunning = m_fileEngine.IsSearchRunning();
    bool recentlyFinishedScan = (m_folderScanFinishedTick > 0 && (GetTickCount64() - m_folderScanFinishedTick) < 3000);
    bool hwLiveActive = (m_viewMode == ViewMode::Hardware && !IsProgramsView() && !IsMonitoringView() && !IsLanView() && !IsLogView() &&
                         (m_hwLiveRefresh || m_hwInspector.IsAsyncBusy() || m_hwInspector.HasStagedData()));
    bool programsPolling = IsProgramsView() && m_progRefreshAtTick != 0;
    bool lanBusy = IsLanView() && (m_lan.IsScanning() || m_lan.IsSsdpRunning() || m_lan.IsPortScanning());
    bool gitBusy = GitDockVisible() && (m_git.IsBusy() || m_gitReloadFolderWhenIdle);

    bool deletePending = m_fileEngine.PendingDeleteBatches() > 0 || m_renamePending.load() > 0 || m_mergePending.load() > 0 || m_imageCache.IsAnyImageTransforming();
    bool toastActive = !m_statusToast.empty() && GetTickCount64() < m_statusToastExpireTick;
    bool contentAnim = PreviewPaneVisible() && m_contentPane.NeedsAnimationTick();

    if (folderScanning || searchRunning || recentlyFinishedScan || hwLiveActive || programsPolling || deletePending || toastActive || lanBusy || gitBusy || contentAnim) {
        SetTimer(m_hwnd, ID_TIMER_WORK_ANIM, 35, nullptr);
    } else {
        KillTimer(m_hwnd, ID_TIMER_WORK_ANIM);
    }
}

void ExplorerApp::ScheduleProgramsRefresh(int attempts) {
    // Installers / uninstallers run asynchronously: re-read the registry every few seconds for a while
    m_progRefreshesLeft = attempts;
    m_progRefreshAtTick = GetTickCount64() + 3000;
    EnsureWorkAnimTimerState();
}

static bool ParseDateStringToFileTime(const std::wstring& str, bool endOfDay, FILETIME& outFt) {
    if (str.empty()) return false;
    int y = 0, m = 0, d = 0;
    // Try YYYY-MM-DD or YYYY.MM.DD or YYYY/MM/DD
    if (swscanf_s(str.c_str(), L"%d-%d-%d", &y, &m, &d) == 3 ||
        swscanf_s(str.c_str(), L"%d.%d.%d", &y, &m, &d) == 3 ||
        swscanf_s(str.c_str(), L"%d/%d/%d", &y, &m, &d) == 3) {
        if (y < 100 && d > 1900) {
            std::swap(y, d); // DD.MM.YYYY
        }
        SYSTEMTIME st = {};
        st.wYear = static_cast<WORD>(std::clamp(y, 1970, 2100));
        st.wMonth = static_cast<WORD>(std::clamp(m, 1, 12));
        st.wDay = static_cast<WORD>(std::clamp(d, 1, 31));
        if (endOfDay) {
            st.wHour = 23; st.wMinute = 59; st.wSecond = 59; st.wMilliseconds = 999;
        }
        FILETIME localFt = {};
        if (SystemTimeToFileTime(&st, &localFt)) {
            LocalFileTimeToFileTime(&localFt, &outFt);
            return true;
        }
    }
    return false;
}

void ExplorerApp::RebuildParsedSearchOptions() {
    m_searchOpts.query = m_filterText;

    m_searchOpts.hasDateFrom = ParseDateStringToFileTime(m_searchOpts.dateFromStr, false, m_searchOpts.dateFromFt);
    m_searchOpts.hasDateTo   = ParseDateStringToFileTime(m_searchOpts.dateToStr, true, m_searchOpts.dateToFt);

    uint64_t mult = 1ULL;
    if (m_searchOpts.sizeUnit == SearchSizeUnit::KB) mult = 1024ULL;
    else if (m_searchOpts.sizeUnit == SearchSizeUnit::MB) mult = 1024ULL * 1024ULL;
    else if (m_searchOpts.sizeUnit == SearchSizeUnit::GB) mult = 1024ULL * 1024ULL * 1024ULL;

    double minVal = 0.0;
    if (!m_searchOpts.sizeMinStr.empty()) {
        minVal = _wtof(m_searchOpts.sizeMinStr.c_str());
    }
    m_searchOpts.sizeMinBytes = static_cast<uint64_t>(std::max(0.0, minVal) * static_cast<double>(mult));

    if (m_searchOpts.sizeMaxStr.empty() || m_searchOpts.sizeMaxStr == L"\x221E") {
        m_searchOpts.sizeMaxBytes = UINT64_MAX;
    } else {
        double maxVal = _wtof(m_searchOpts.sizeMaxStr.c_str());
        m_searchOpts.sizeMaxBytes = static_cast<uint64_t>(std::max(0.0, maxVal) * static_cast<double>(mult));
    }
}

void ExplorerApp::TriggerAsyncSearch() {
    if (m_currentPath.empty() || m_currentPath == L"THIS_PC") {
        ApplyFilterAndSort();
        return;
    }

    RebuildParsedSearchOptions();

    if (m_filterText.empty() && !m_searchOpts.HasAnyActiveFilter()) {
        ClearSearchAndRestoreFolder();
        return;
    }

    m_isSearchActive = true;
    m_scrollY = 0.0f;
    m_fileEngine.StartAsyncSearch(m_currentPath, m_searchOpts, m_showHidden, m_showExtensions, m_lang);
    m_fileEngine.SyncSearchResults(m_items, m_searchProgress);
    EnsureWorkAnimTimerState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::ClearSearchAndRestoreFolder() {
    m_fileEngine.CancelAsyncSearch();
    m_isSearchActive = false;
    m_searchProgress = {};
    m_filterText.clear();
    if (m_searchEdit) {
        SetWindowTextW(m_searchEdit, L"");
        ShowWindow(m_searchEdit, SW_HIDE);
    }
    ApplyFilterAndSort();
    EnsureWorkAnimTimerState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::BeginSearchFieldEdit(int fieldCmdId, const D2D1_RECT_F& fieldRect) {
    m_editingSearchFieldCmd = fieldCmdId;
    m_renamingIndex = -1;

    std::wstring initVal;
    if (fieldCmdId == CMD_SEARCH_EDIT_DATE_FROM) initVal = m_searchOpts.dateFromStr;
    else if (fieldCmdId == CMD_SEARCH_EDIT_DATE_TO) initVal = m_searchOpts.dateToStr;
    else if (fieldCmdId == CMD_SEARCH_EDIT_SIZE_MIN) initVal = m_searchOpts.sizeMinStr;
    else if (fieldCmdId == CMD_SEARCH_EDIT_SIZE_MAX) initVal = m_searchOpts.sizeMaxStr;
    else if (fieldCmdId == CMD_SEARCH_EDIT_CUSTOM_EXT) initVal = m_searchOpts.customExtList;

    SetWindowPos(m_renameEdit, HWND_TOP,
        static_cast<int>(fieldRect.left),
        static_cast<int>(fieldRect.top),
        static_cast<int>(std::max(64.0f, fieldRect.right - fieldRect.left)),
        static_cast<int>(std::max(20.0f, fieldRect.bottom - fieldRect.top)),
        SWP_SHOWWINDOW);
    SetWindowTextW(m_renameEdit, initVal.c_str());
    SetFocus(m_renameEdit);
    SendMessageW(m_renameEdit, EM_SETSEL, 0, -1);
}

void ExplorerApp::ShowSearchTypeListMenu(int screenX, int screenY) {
    // Collect distinct extensions from current directory + popular extensions
    std::vector<std::wstring> extList = {
        L".jpg", L".png", L".webp", L".mp4", L".mkv", L".mp3", L".wav",
        L".cs", L".cpp", L".h", L".py", L".json", L".txt", L".md",
        L".zip", L".7z", L".rar", L".exe", L".dll", L".pdf", L".safetensors"
    };
    for (const auto& it : m_allItems) {
        if (!it.isDirectory && !it.ext.empty()) {
            if (std::find(extList.begin(), extList.end(), it.ext) == extList.end() && extList.size() < 45) {
                extList.push_back(it.ext);
            }
        }
    }

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, CMD_SEARCH_EDIT_CUSTOM_EXT,
        m_lang == Lang::PL ? L"Wpisz własne rozszerzenia (np. .cs;.prefab)..." : L"Type custom extensions (e.g. .cs;.prefab)...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    std::wstring curListLo = PathUtils::ToLower(m_searchOpts.customExtList);
    for (size_t i = 0; i < extList.size(); ++i) {
        bool checked = (curListLo.find(extList[i]) != std::wstring::npos);
        AppendMenuW(hMenu, MF_STRING | (checked ? MF_CHECKED : MF_UNCHECKED),
            CMD_BASE_SEARCH_EXT_MENU + static_cast<UINT>(i), extList[i].c_str());
    }

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screenX, screenY, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd == CMD_SEARCH_EDIT_CUSTOM_EXT) {
        ExecuteRibbonCommand(CMD_SEARCH_EDIT_CUSTOM_EXT);
        return;
    }
    if (cmd >= CMD_BASE_SEARCH_EXT_MENU && cmd < CMD_BASE_SEARCH_EXT_MENU + static_cast<int>(extList.size())) {
        std::wstring chosen = extList[cmd - CMD_BASE_SEARCH_EXT_MENU];
        size_t pos = curListLo.find(chosen);
        if (pos != std::wstring::npos) {
            m_searchOpts.customExtList.erase(pos, chosen.size());
            if (pos < m_searchOpts.customExtList.size() && m_searchOpts.customExtList[pos] == L';') {
                m_searchOpts.customExtList.erase(pos, 1);
            }
        } else {
            if (!m_searchOpts.customExtList.empty() && m_searchOpts.customExtList.back() != L';') {
                m_searchOpts.customExtList += L";";
            }
            m_searchOpts.customExtList += chosen;
        }
        m_searchOpts.useTypeFilter = (!m_searchOpts.customExtList.empty() || m_searchOpts.categoryMask != 0);
        TriggerAsyncSearch();
    }
}

namespace {

struct CustomImgDialogState {
    HWND hEdit = nullptr;
    bool accepted = false;
    bool closed = false;
    std::wstring resultText;
};

static LRESULT CALLBACK CustomImgDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<CustomImgDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDOK && st) {
            wchar_t buf[256] = {};
            if (st->hEdit) GetWindowTextW(st->hEdit, buf, 256);
            st->resultText = buf;
            st->accepted = true;
            st->closed = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDCANCEL && st) {
            st->accepted = false;
            st->closed = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        if (st) {
            st->accepted = false;
            st->closed = true;
        }
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK CustomImgEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            return 0;
        }
    } else if (msg == WM_CHAR && (wParam == L'\r' || wParam == L'\n' || wParam == 27)) {
        return 0;
    } else if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, CustomImgEditSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static bool PromptCustomImageInput(
    HWND owner,
    HINSTANCE hInst,
    HFONT hFont,
    const wchar_t* title,
    const wchar_t* promptText,
    const wchar_t* initialVal,
    bool pl,
    std::wstring& outVal) {
    outVal.clear();

    static bool classRegistered = false;
    const wchar_t* clsName = L"Explorer2CustomImgInputDlg";
    if (!classRegistered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = CustomImgDlgProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = clsName;
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    RECT ownerRc = {};
    GetWindowRect(owner, &ownerRc);
    int dlgW = 430;
    int dlgH = 175;
    int dlgX = ownerRc.left + ((ownerRc.right - ownerRc.left) - dlgW) / 2;
    int dlgY = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - dlgH) / 2;

    CustomImgDialogState st;
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        clsName, title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        dlgX, dlgY, dlgW, dlgH,
        owner, nullptr, hInst, nullptr);
    if (!hDlg) return false;

    SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));

    HWND hLbl = CreateWindowExW(0, L"STATIC", promptText, WS_CHILD | WS_VISIBLE,
        16, 14, dlgW - 40, 38, hDlg, nullptr, hInst, nullptr);
    st.hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initialVal,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        16, 56, dlgW - 40, 26, hDlg, reinterpret_cast<HMENU>(1001), hInst, nullptr);
    HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        dlgW - 196, 94, 82, 28, hDlg, reinterpret_cast<HMENU>(IDOK), hInst, nullptr);
    HWND hCancel = CreateWindowExW(0, L"BUTTON", pl ? L"Anuluj" : L"Cancel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        dlgW - 106, 94, 82, 28, hDlg, reinterpret_cast<HMENU>(IDCANCEL), hInst, nullptr);

    if (hFont) {
        SendMessageW(hLbl, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        SendMessageW(st.hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        SendMessageW(hOk, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        SendMessageW(hCancel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    }

    SetWindowSubclass(st.hEdit, CustomImgEditSubclassProc, 1, 0);
    EnableWindow(owner, FALSE);
    SetFocus(st.hEdit);
    SendMessageW(st.hEdit, EM_SETSEL, 0, -1);

    MSG msg = {};
    while (!st.closed && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    SetFocus(owner);

    if (st.accepted && !st.resultText.empty()) {
        outVal = st.resultText;
        return true;
    }
    return false;
}

} // namespace

std::vector<std::wstring> ExplorerApp::GetSelectedImagePaths(int fallbackHitIndex) const {
    std::vector<std::wstring> imgs;
    for (const auto& it : m_items) {
        if (it.isSelected && it.isImage && !it.isDirectory) {
            imgs.push_back(it.fullPath);
        }
    }
    if (imgs.empty() && fallbackHitIndex >= 0 && fallbackHitIndex < static_cast<int>(m_items.size())) {
        const auto& it = m_items[fallbackHitIndex];
        if (it.isImage && !it.isDirectory) {
            imgs.push_back(it.fullPath);
        }
    }
    if (imgs.empty() && m_focusedIndex >= 0 && m_focusedIndex < static_cast<int>(m_items.size())) {
        const auto& it = m_items[m_focusedIndex];
        if (it.isImage && !it.isDirectory) {
            imgs.push_back(it.fullPath);
        }
    }
    return imgs;
}

HMENU ExplorerApp::BuildImageToolsSubmenu(const std::vector<std::wstring>& imgPaths) const {
    const bool pl = (m_lang == Lang::PL);
    HMENU hImg = CreatePopupMenu();
    if (imgPaths.empty()) return hImg;

    uint32_t w0 = 0, h0 = 0;
    uint64_t bytes0 = 0;
    ShellMenuEmulator::ProbeImageDimensions(imgPaths[0], w0, h0, &bytes0);

    // 1. Info header showing current image dimensions and size
    wchar_t hdrBuf[256] = {};
    if (imgPaths.size() == 1) {
        if (w0 > 0 && h0 > 0) {
            swprintf_s(hdrBuf, L"\xD83D\xDDBC %s   \x2022   %u \xD7 %u px   \x2022   %s",
                PathUtils::GetFileName(imgPaths[0]).c_str(), w0, h0,
                FileEngine::FormatSizeDetailed(bytes0).c_str());
        } else {
            swprintf_s(hdrBuf, L"\xD83D\xDDBC %s   \x2022   %s",
                PathUtils::GetFileName(imgPaths[0]).c_str(),
                FileEngine::FormatSizeDetailed(bytes0).c_str());
        }
    } else {
        if (w0 > 0 && h0 > 0) {
            swprintf_s(hdrBuf, pl
                ? L"\xD83D\xDDBC Zaznaczono obrazów: %zu   (1.: %u × %u px, %s)"
                : L"\xD83D\xDDBC Selected images: %zu   (1st: %u × %u px, %s)",
                imgPaths.size(), w0, h0, FileEngine::FormatSizeDetailed(bytes0).c_str());
        } else {
            swprintf_s(hdrBuf, pl ? L"\xD83D\xDDBC Zaznaczono obrazów: %zu" : L"\xD83D\xDDBC Selected images: %zu", imgPaths.size());
        }
    }
    AppendMenuW(hImg, MF_STRING | MF_GRAYED, 0, hdrBuf);

    // 2. Output mode toggle: Save as copy (safe default) vs Overwrite original
    const wchar_t* modeLbl = !m_imgToolsOverwrite
        ? (pl ? L"Tryb zapisu: Zapisz jako kopię (bezpieczne — kliknij, aby nadpisywać oryginał)"
              : L"Save mode: Save as copy (safe — click to overwrite original)")
        : (pl ? L"Tryb zapisu: NADPISZ ORYGINAŁ (kliknij, aby zapisywać jako kopię)"
              : L"Save mode: OVERWRITE ORIGINAL (click to save as copy)");
    AppendMenuW(hImg, MF_STRING | (m_imgToolsOverwrite ? MF_CHECKED : MF_UNCHECKED), CMD_IMG_TOGGLE_OVERWRITE, modeLbl);
    AppendMenuW(hImg, MF_SEPARATOR, 0, nullptr);

    auto fmtScaledDims = [&](int pct) -> std::wstring {
        if (w0 == 0 || h0 == 0) return L"";
        uint32_t nw = std::max(1u, static_cast<uint32_t>(std::lround(w0 * (pct / 100.0))));
        uint32_t nh = std::max(1u, static_cast<uint32_t>(std::lround(h0 * (pct / 100.0))));
        wchar_t b[64] = {};
        swprintf_s(b, L"   (%u × %u px)", nw, nh);
        return b;
    };

    auto fmtMaxDim = [&](uint32_t maxD) -> std::wstring {
        if (w0 == 0 || h0 == 0) return L"";
        uint32_t longest = std::max(w0, h0);
        if (longest <= maxD) {
            return pl ? L"   (już mniejszy)" : L"   (already smaller)";
        }
        double s = static_cast<double>(maxD) / static_cast<double>(longest);
        uint32_t nw = std::max(1u, static_cast<uint32_t>(std::lround(w0 * s)));
        uint32_t nh = std::max(1u, static_cast<uint32_t>(std::lround(h0 * s)));
        wchar_t b[64] = {};
        swprintf_s(b, L"   → %u × %u px", nw, nh);
        return b;
    };

    // 3. Quick 1-click actions right at top level
    AppendMenuW(hImg, MF_STRING, CMD_IMG_SCALE_50,
        ((pl ? L"⚡ Szybko: Zmniejsz o połowę (50%)" : L"⚡ Quick: Downscale to 50%") + fmtScaledDims(50)).c_str());
    AppendMenuW(hImg, MF_STRING, CMD_IMG_MAXDIM_1920,
        ((pl ? L"⚡ Szybko: Max 1920 px (Full HD)" : L"⚡ Quick: Max 1920 px (Full HD)") + fmtMaxDim(1920)).c_str());
    AppendMenuW(hImg, MF_STRING, CMD_IMG_TARGET_500KB,
        pl ? L"⚡ Szybko: Zmniejsz do rozmiaru ≤ 500 KB" : L"⚡ Quick: Compress/Downscale to ≤ 500 KB");
    AppendMenuW(hImg, MF_STRING, CMD_IMG_TARGET_1MB,
        pl ? L"⚡ Szybko: Zmniejsz do rozmiaru ≤ 1 MB" : L"⚡ Quick: Compress/Downscale to ≤ 1 MB");
    AppendMenuW(hImg, MF_SEPARATOR, 0, nullptr);

    // 4. Submenu: Scale by Percentage (%)
    HMENU hScalePct = CreatePopupMenu();
    AppendMenuW(hScalePct, MF_STRING, CMD_IMG_SCALE_75, (L"75%" + fmtScaledDims(75)).c_str());
    AppendMenuW(hScalePct, MF_STRING, CMD_IMG_SCALE_50, (L"50%" + fmtScaledDims(50)).c_str());
    AppendMenuW(hScalePct, MF_STRING, CMD_IMG_SCALE_33, (L"33%" + fmtScaledDims(33)).c_str());
    AppendMenuW(hScalePct, MF_STRING, CMD_IMG_SCALE_25, (L"25%" + fmtScaledDims(25)).c_str());
    AppendMenuW(hScalePct, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hScalePct, MF_STRING, CMD_IMG_SCALE_CUSTOM,
        pl ? L"Własna skala % lub wymiar Szer×Wys..." : L"Custom scale % or W×H...");
    AppendMenuW(hImg, MF_POPUP, reinterpret_cast<UINT_PTR>(hScalePct),
        pl ? L"\xD83D\xDCD0 Skaluj procentowo (%)" : L"\xD83D\xDCD0 Scale by Percentage (%)");

    // 5. Submenu: Downscale to Max Resolution (longest edge)
    HMENU hMaxDim = CreatePopupMenu();
    AppendMenuW(hMaxDim, MF_STRING, CMD_IMG_MAXDIM_3840, (L"Max 3840 px  (4K UHD)" + fmtMaxDim(3840)).c_str());
    AppendMenuW(hMaxDim, MF_STRING, CMD_IMG_MAXDIM_2560, (L"Max 2560 px  (QHD / 2K)" + fmtMaxDim(2560)).c_str());
    AppendMenuW(hMaxDim, MF_STRING, CMD_IMG_MAXDIM_1920, (L"Max 1920 px  (Full HD 1080p)" + fmtMaxDim(1920)).c_str());
    AppendMenuW(hMaxDim, MF_STRING, CMD_IMG_MAXDIM_1280, (L"Max 1280 px  (HD 720p)" + fmtMaxDim(1280)).c_str());
    AppendMenuW(hMaxDim, MF_STRING, CMD_IMG_MAXDIM_1024, (L"Max 1024 px" + fmtMaxDim(1024)).c_str());
    AppendMenuW(hMaxDim, MF_STRING, CMD_IMG_MAXDIM_512,  (L"Max 512 px   (Miniatura)" + fmtMaxDim(512)).c_str());
    AppendMenuW(hMaxDim, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMaxDim, MF_STRING, CMD_IMG_SCALE_CUSTOM,
        pl ? L"Własna rozdzielczość (px lub Szer×Wys)..." : L"Custom resolution (px or W×H)...");
    AppendMenuW(hImg, MF_POPUP, reinterpret_cast<UINT_PTR>(hMaxDim),
        pl ? L"\xD83D\xDCCF Zmniejsz do rozdzielczości (dłuższy bok)" : L"\xD83D\xDCCF Downscale to Max Resolution");

    // 6. Submenu: Downscale / Compress to Target File Size (KB / MB)
    HMENU hTargetSz = CreatePopupMenu();
    AppendMenuW(hTargetSz, MF_STRING, CMD_IMG_TARGET_5MB,   pl ? L"Do ≤ 5 MB"   : L"To ≤ 5 MB");
    AppendMenuW(hTargetSz, MF_STRING, CMD_IMG_TARGET_2MB,   pl ? L"Do ≤ 2 MB"   : L"To ≤ 2 MB");
    AppendMenuW(hTargetSz, MF_STRING, CMD_IMG_TARGET_1MB,   pl ? L"Do ≤ 1 MB"   : L"To ≤ 1 MB");
    AppendMenuW(hTargetSz, MF_STRING, CMD_IMG_TARGET_500KB, pl ? L"Do ≤ 500 KB" : L"To ≤ 500 KB");
    AppendMenuW(hTargetSz, MF_STRING, CMD_IMG_TARGET_250KB, pl ? L"Do ≤ 250 KB" : L"To ≤ 250 KB");
    AppendMenuW(hTargetSz, MF_STRING, CMD_IMG_TARGET_100KB, pl ? L"Do ≤ 100 KB" : L"To ≤ 100 KB");
    AppendMenuW(hTargetSz, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hTargetSz, MF_STRING, CMD_IMG_TARGET_CUSTOM,
        pl ? L"Własny docelowy rozmiar pliku (np. 350 KB, 1.5 MB)..." : L"Custom target file size (e.g. 350 KB, 1.5 MB)...");
    AppendMenuW(hImg, MF_POPUP, reinterpret_cast<UINT_PTR>(hTargetSz),
        pl ? L"\xD83D\xDCE6 Zmniejsz do rozmiaru pliku (limit KB / MB)" : L"\xD83D\xDCE6 Downscale to Target File Size (KB / MB)");

    // 7. Submenu: Convert Image Format
    HMENU hConv = CreatePopupMenu();
    AppendMenuW(hConv, MF_STRING, CMD_IMG_CONVERT_JPG_92, pl ? L"Konwertuj do JPG (wysoka jakość 92%)" : L"Convert to JPG (high quality 92%)");
    AppendMenuW(hConv, MF_STRING, CMD_IMG_CONVERT_JPG_80, pl ? L"Konwertuj do JPG (kompresja Web 80%)" : L"Convert to JPG (Web compression 80%)");
    AppendMenuW(hConv, MF_STRING, CMD_IMG_CONVERT_PNG,    pl ? L"Konwertuj do PNG (bezstratny)"        : L"Convert to PNG (lossless)");
    AppendMenuW(hConv, MF_STRING, CMD_IMG_CONVERT_BMP,    pl ? L"Konwertuj do BMP (mapa bitowa)"       : L"Convert to BMP (bitmap)");
    AppendMenuW(hImg, MF_POPUP, reinterpret_cast<UINT_PTR>(hConv),
        pl ? L"\xD83D\xDD04 Konwertuj format (JPG / PNG / BMP)" : L"\xD83D\xDD04 Convert Format (JPG / PNG / BMP)");

    // 8. Submenu: Rotate & Flip
    HMENU hRotFlip = CreatePopupMenu();
    AppendMenuW(hRotFlip, MF_STRING, CMD_CTX_ROTATE_CW,  pl ? L"Obróć w prawo o 90°" : L"Rotate Right 90°");
    AppendMenuW(hRotFlip, MF_STRING, CMD_CTX_ROTATE_CCW, pl ? L"Obróć w lewo o 90°"  : L"Rotate Left 90°");
    AppendMenuW(hRotFlip, MF_STRING, CMD_IMG_ROTATE_180, pl ? L"Obróć o 180°"        : L"Rotate 180°");
    AppendMenuW(hRotFlip, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hRotFlip, MF_STRING, CMD_IMG_FLIP_H,     pl ? L"Odbij poziomo (lustro)" : L"Flip Horizontal (mirror)");
    AppendMenuW(hRotFlip, MF_STRING, CMD_IMG_FLIP_V,     pl ? L"Odbij pionowo"          : L"Flip Vertical");
    AppendMenuW(hImg, MF_POPUP, reinterpret_cast<UINT_PTR>(hRotFlip),
        pl ? L"↻ Obróć i odbij" : L"↻ Rotate & Flip");

    return hImg;
}

void ExplorerApp::ShowImageToolsMenu(int screenX, int screenY, int fallbackHitIndex) {
    const bool pl = (m_lang == Lang::PL);
    std::vector<std::wstring> imgPaths = GetSelectedImagePaths(fallbackHitIndex);
    if (imgPaths.empty()) {
        m_statusToast = pl
            ? L"⚠ Zaznacz jeden lub więcej plików obrazu (.jpg, .png, .webp, .bmp...), aby użyć Narzędzi obrazu"
            : L"⚠ Select one or more image files (.jpg, .png, .webp, .bmp...) to use Image Tools";
        m_statusToastExpireTick = GetTickCount64() + 3800;
        EnsureWorkAnimTimerState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    for (;;) {
        HMENU hMenu = BuildImageToolsSubmenu(imgPaths);
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screenX, screenY, 0, m_hwnd, nullptr);
        DestroyMenu(hMenu);
        if (cmd <= 0) break;

        if (cmd == CMD_IMG_TOGGLE_OVERWRITE) {
            m_imgToolsOverwrite = !m_imgToolsOverwrite;
            // Re-open the menu immediately so the user sees the updated checkmark and can pick their action
            continue;
        }

        HandleImageToolCommand(cmd, fallbackHitIndex);
        break;
    }
}

bool ExplorerApp::HandleImageToolCommand(int cmdId, int fallbackHitIndex) {
    const bool pl = (m_lang == Lang::PL);

    if (cmdId == CMD_IMG_TOGGLE_OVERWRITE) {
        m_imgToolsOverwrite = !m_imgToolsOverwrite;
        m_statusToast = m_imgToolsOverwrite
            ? (pl ? L"\xD83D\xDCD0 Tryb narzędzi obrazu: NADPISYWANIE ORYGINAŁU" : L"\xD83D\xDCD0 Image Tools mode: OVERWRITE ORIGINAL")
            : (pl ? L"\xD83D\xDCD0 Tryb narzędzi obrazu: Zapisz jako kopię (bezpieczne)" : L"\xD83D\xDCD0 Image Tools mode: Save as copy (safe)");
        m_statusToastExpireTick = GetTickCount64() + 3500;
        EnsureWorkAnimTimerState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return true;
    }

    std::vector<std::wstring> imgPaths = GetSelectedImagePaths(fallbackHitIndex);
    if (imgPaths.empty()) {
        m_statusToast = pl
            ? L"⚠ Zaznacz obraz, aby wykonać operację"
            : L"⚠ Select an image to perform this operation";
        m_statusToastExpireTick = GetTickCount64() + 3500;
        EnsureWorkAnimTimerState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return false;
    }

    ImageTransformParams params;
    params.overwriteOriginal = m_imgToolsOverwrite;
    params.jpegQuality = 0.92f;

    switch (cmdId) {
    case CMD_CTX_ROTATE_CW:
        params.kind = ImageTransformKind::RotateCW;
        params.overwriteOriginal = true; // Standard 90-deg rotation rotates in-place
        params.suffix = L"_rot90";
        break;
    case CMD_CTX_ROTATE_CCW:
        params.kind = ImageTransformKind::RotateCCW;
        params.overwriteOriginal = true;
        params.suffix = L"_rot270";
        break;
    case CMD_IMG_ROTATE_180:
        params.kind = ImageTransformKind::Rotate180;
        params.suffix = L"_rot180";
        break;
    case CMD_IMG_FLIP_H:
        params.kind = ImageTransformKind::FlipH;
        params.suffix = L"_flipH";
        break;
    case CMD_IMG_FLIP_V:
        params.kind = ImageTransformKind::FlipV;
        params.suffix = L"_flipV";
        break;
    case CMD_IMG_SCALE_75:
        params.kind = ImageTransformKind::ScalePercent;
        params.scalePct = 75;
        params.suffix = L"_75pct";
        break;
    case CMD_IMG_SCALE_50:
        params.kind = ImageTransformKind::ScalePercent;
        params.scalePct = 50;
        params.suffix = L"_50pct";
        break;
    case CMD_IMG_SCALE_33:
        params.kind = ImageTransformKind::ScalePercent;
        params.scalePct = 33;
        params.suffix = L"_33pct";
        break;
    case CMD_IMG_SCALE_25:
        params.kind = ImageTransformKind::ScalePercent;
        params.scalePct = 25;
        params.suffix = L"_25pct";
        break;
    case CMD_IMG_MAXDIM_3840:
        params.kind = ImageTransformKind::FitMaxDim;
        params.maxDim = 3840;
        params.suffix = L"_3840px";
        break;
    case CMD_IMG_MAXDIM_2560:
        params.kind = ImageTransformKind::FitMaxDim;
        params.maxDim = 2560;
        params.suffix = L"_2560px";
        break;
    case CMD_IMG_MAXDIM_1920:
        params.kind = ImageTransformKind::FitMaxDim;
        params.maxDim = 1920;
        params.suffix = L"_1920px";
        break;
    case CMD_IMG_MAXDIM_1280:
        params.kind = ImageTransformKind::FitMaxDim;
        params.maxDim = 1280;
        params.suffix = L"_1280px";
        break;
    case CMD_IMG_MAXDIM_1024:
        params.kind = ImageTransformKind::FitMaxDim;
        params.maxDim = 1024;
        params.suffix = L"_1024px";
        break;
    case CMD_IMG_MAXDIM_512:
        params.kind = ImageTransformKind::FitMaxDim;
        params.maxDim = 512;
        params.suffix = L"_512px";
        break;
    case CMD_IMG_TARGET_5MB:
        params.kind = ImageTransformKind::TargetFileSize;
        params.targetBytes = 5ULL * 1024ULL * 1024ULL;
        params.suffix = L"_5MB";
        break;
    case CMD_IMG_TARGET_2MB:
        params.kind = ImageTransformKind::TargetFileSize;
        params.targetBytes = 2ULL * 1024ULL * 1024ULL;
        params.suffix = L"_2MB";
        break;
    case CMD_IMG_TARGET_1MB:
        params.kind = ImageTransformKind::TargetFileSize;
        params.targetBytes = 1ULL * 1024ULL * 1024ULL;
        params.suffix = L"_1MB";
        break;
    case CMD_IMG_TARGET_500KB:
        params.kind = ImageTransformKind::TargetFileSize;
        params.targetBytes = 500ULL * 1024ULL;
        params.suffix = L"_500KB";
        break;
    case CMD_IMG_TARGET_250KB:
        params.kind = ImageTransformKind::TargetFileSize;
        params.targetBytes = 250ULL * 1024ULL;
        params.suffix = L"_250KB";
        break;
    case CMD_IMG_TARGET_100KB:
        params.kind = ImageTransformKind::TargetFileSize;
        params.targetBytes = 100ULL * 1024ULL;
        params.suffix = L"_100KB";
        break;
    case CMD_IMG_CONVERT_JPG_92:
        params.kind = ImageTransformKind::ConvertFormat;
        params.forceExt = L".jpg";
        params.jpegQuality = 0.92f;
        params.suffix = L"_jpg92";
        break;
    case CMD_IMG_CONVERT_JPG_80:
        params.kind = ImageTransformKind::ConvertFormat;
        params.forceExt = L".jpg";
        params.jpegQuality = 0.80f;
        params.suffix = L"_jpg80";
        break;
    case CMD_IMG_CONVERT_PNG:
        params.kind = ImageTransformKind::ConvertFormat;
        params.forceExt = L".png";
        params.suffix = L"_png";
        break;
    case CMD_IMG_CONVERT_BMP:
        params.kind = ImageTransformKind::ConvertFormat;
        params.forceExt = L".bmp";
        params.suffix = L"_bmp";
        break;
    case CMD_IMG_SCALE_CUSTOM: {
        uint32_t w0 = 0, h0 = 0;
        ShellMenuEmulator::ProbeImageDimensions(imgPaths[0], w0, h0);
        wchar_t initStr[64] = L"60%";
        if (w0 > 0 && h0 > 0) {
            swprintf_s(initStr, L"%ux%u", std::max(1u, w0 / 2), std::max(1u, h0 / 2));
        }
        std::wstring input;
        if (!PromptCustomImageInput(
                m_hwnd, m_hInstance, m_editFont,
                pl ? L"Własny rozmiar / skala obrazu" : L"Custom Image Size / Scale",
                pl ? L"Podaj skalę w % (np. 60%), dłuższy bok w px (np. 1600)\nlub dokładny wymiar Szer×Wys (np. 1920x1080):"
                   : L"Enter scale % (e.g. 60%), max edge in px (e.g. 1600),\nor exact Width×Height (e.g. 1920x1080):",
                initStr, pl, input)) {
            return false;
        }
        std::wstring lo = PathUtils::ToLower(input);
        uint32_t tw = 0, th = 0;
        if (lo.find(L'%') != std::wstring::npos) {
            int pct = std::clamp(_wtoi(lo.c_str()), 1, 800);
            params.kind = ImageTransformKind::ScalePercent;
            params.scalePct = pct;
            params.suffix = L"_" + std::to_wstring(pct) + L"pct";
        } else if (swscanf_s(lo.c_str(), L"%u x %u", &tw, &th) == 2 ||
                   swscanf_s(lo.c_str(), L"%ux%u", &tw, &th) == 2 ||
                   swscanf_s(lo.c_str(), L"%u*%u", &tw, &th) == 2 ||
                   swscanf_s(lo.c_str(), L"%u,%u", &tw, &th) == 2) {
            tw = std::clamp(tw, 1u, 32768u);
            th = std::clamp(th, 1u, 32768u);
            params.kind = ImageTransformKind::ExactWidthHeight;
            params.targetW = tw;
            params.targetH = th;
            params.suffix = L"_" + std::to_wstring(tw) + L"x" + std::to_wstring(th);
        } else {
            int val = _wtoi(lo.c_str());
            if (val <= 0) return false;
            if (val <= 100 && lo.find(L"px") == std::wstring::npos) {
                params.kind = ImageTransformKind::ScalePercent;
                params.scalePct = val;
                params.suffix = L"_" + std::to_wstring(val) + L"pct";
            } else {
                uint32_t maxD = static_cast<uint32_t>(std::clamp(val, 16, 32768));
                params.kind = ImageTransformKind::FitMaxDim;
                params.maxDim = maxD;
                params.suffix = L"_" + std::to_wstring(maxD) + L"px";
            }
        }
        break;
    }
    case CMD_IMG_TARGET_CUSTOM: {
        std::wstring input;
        if (!PromptCustomImageInput(
                m_hwnd, m_hInstance, m_editFont,
                pl ? L"Zmniejsz obraz do podanego rozmiaru pliku" : L"Downscale Image to Target File Size",
                pl ? L"Podaj docelowy maksymalny rozmiar pliku\n(np. 350 KB, 800 KB, 1.5 MB — domyślnie w KB):"
                   : L"Enter target maximum file size\n(e.g. 350 KB, 800 KB, 1.5 MB — defaults to KB):",
                L"350 KB", pl, input)) {
            return false;
        }
        for (auto& ch : input) {
            if (ch == L',') ch = L'.';
        }
        std::wstring lo = PathUtils::ToLower(input);
        double num = _wtof(lo.c_str());
        if (num <= 0.0) return false;
        uint64_t targetB = 0;
        std::wstring sfx;
        if (lo.find(L"mb") != std::wstring::npos || lo.find(L'm') != std::wstring::npos) {
            targetB = static_cast<uint64_t>(num * 1024.0 * 1024.0);
            wchar_t sb[32] = {};
            swprintf_s(sb, L"_%.1fMB", num);
            sfx = sb;
        } else if (lo.find(L"kb") != std::wstring::npos || lo.find(L'k') != std::wstring::npos) {
            targetB = static_cast<uint64_t>(num * 1024.0);
            sfx = L"_" + std::to_wstring(static_cast<int>(std::lround(num))) + L"KB";
        } else if (lo.find(L'b') != std::wstring::npos && num >= 2048.0) {
            targetB = static_cast<uint64_t>(num);
            sfx = L"_" + std::to_wstring(static_cast<int>(targetB / 1024)) + L"KB";
        } else {
            // Bare number: treat as KB
            targetB = static_cast<uint64_t>(num * 1024.0);
            sfx = L"_" + std::to_wstring(static_cast<int>(std::lround(num))) + L"KB";
        }
        targetB = std::max<uint64_t>(2048ULL, targetB);
        params.kind = ImageTransformKind::TargetFileSize;
        params.targetBytes = targetB;
        params.suffix = sfx;
        break;
    }
    default:
        return false;
    }

    m_statusToast = (pl ? L"\x23F3 Przetwarzanie obrazu w tle (" : L"\x23F3 Processing image in background (") +
                    std::to_wstring(imgPaths.size()) + L")...";
    m_statusToastExpireTick = GetTickCount64() + 10000;
    m_imageCache.QueueTransformImages(imgPaths, params);
    EnsureWorkAnimTimerState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    return true;
}

} // namespace Explorer2
