#include "ExplorerApp.h"
#include "CommandIds.h"
#include "PathUtils.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <unordered_set>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "user32.lib")

namespace Explorer2 {

constexpr int ID_EDIT_ADDRESS = 1001;
constexpr int ID_EDIT_SEARCH  = 1002;
constexpr int ID_EDIT_RENAME  = 1003;
constexpr UINT_PTR ID_TIMER_MEDIA     = 2001;
constexpr UINT_PTR ID_TIMER_WORK_ANIM = 2002;

static WNDPROC g_origEditProc = nullptr;
static ExplorerApp* g_appInstance = nullptr;

static LRESULT CALLBACK InlineEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), EN_MAXTEXT), reinterpret_cast<LPARAM>(hwnd));
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), EN_KILLFOCUS), 0);
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

    if (!m_hwnd) return false;

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

    NavigateTo(startDir, true);

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
            EnsureWorkAnimTimerState();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        SetFocus(m_hwnd);
        CancelInlineEdits();
        OnMouseDown(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)), false, wParam);
        return 0;

    case WM_RBUTTONDOWN:
        SetFocus(m_hwnd);
        CancelInlineEdits();
        OnMouseDown(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)), true, wParam);
        return 0;

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
            m_filterText = buf;
            TriggerAsyncSearch();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if ((id == ID_EDIT_ADDRESS || id == ID_EDIT_RENAME || id == ID_EDIT_SEARCH) && code == EN_MAXTEXT) {
            CommitAddressOrRenameEdit(reinterpret_cast<HWND>(lParam));
        } else if ((id == ID_EDIT_ADDRESS || id == ID_EDIT_RENAME) && code == EN_KILLFOCUS) {
            CancelInlineEdits();
        }
        return 0;
    }

    case WM_APP_THUMB_READY:
    case WM_APP_FULLIMG_READY:
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_APP_IMAGE_ROTATED:
        m_imageCache.TakeCompletedRotations();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

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

    case WM_APP_DELETE_DONE: {
        const Strings& s = GetStrings(m_lang);
        m_statusToast = (wParam != 0) ? s.statusDeletedPerm : s.statusDeletedRecycle;
        m_statusToastExpireTick = GetTickCount64() + 3500;
        InvalidateRect(m_hwnd, nullptr, FALSE);
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
    bool inMediaViewer = (m_viewMode == ViewMode::FullViewer || m_showPreviewPane);
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
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        DWORD attrs = GetFileAttributesW(cleanPath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            return;
        }
    }

    m_mediaPlayer.CloseMedia();
    KillTimer(m_hwnd, ID_TIMER_MEDIA);

    m_currentPath = cleanPath;
    m_currentNormKey = (cleanPath == L"THIS_PC") ? L"__HEADER_PC__" : PathUtils::NormalizePathKey(cleanPath);

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

    m_scrollY = 0.0f;
    m_filmstripScrollX = 0.0f;
    ResetViewerZoom();

    std::wstring folderTitle = (m_currentPath == L"THIS_PC")
        ? GetStrings(m_lang).navThisPC
        : PathUtils::GetFileName(m_currentPath);
    SetWindowTextW(m_hwnd, folderTitle.c_str());

    m_focusedIndex = -1;
    m_anchorIndex = -1;
    if (!selectFileName.empty()) {
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            if (_wcsicmp(m_items[i].name.c_str(), selectFileName.c_str()) == 0 ||
                _wcsicmp(m_items[i].fullPath.c_str(), selectFileName.c_str()) == 0) {
                SelectSingle(i, true);
                EnsureItemVisible(i);
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

    if (m_viewMode == ViewMode::Proportions && m_currentPath != L"THIS_PC") {
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
    if (m_viewMode == ViewMode::Proportions && m_currentPath != L"THIS_PC") {
        m_fileEngine.RequestFolderSizes(m_items, false);
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

D2D1_RECT_F ExplorerApp::GetMainViewRect() const {
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
    float rightX = (m_showPreviewPane && m_viewMode != ViewMode::FullViewer) ? std::max(leftX + 100.0f, w - m_previewPaneWidth) : w;

    return D2D1::RectF(leftX, topY, rightX, bottomY);
}

D2D1_RECT_F ExplorerApp::GetContentItemsRect() const {
    D2D1_RECT_F r = GetMainViewRect();
    if (m_isSearchActive && m_viewMode != ViewMode::FullViewer) {
        r.top += 28.0f;
    }
    if (m_viewMode == ViewMode::Details) {
        r.top += kDetailsHeaderHeight;
    }
    if (m_viewMode != ViewMode::Proportions) {
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

int ExplorerApp::HitTestItem(float x, float y, const D2D1_RECT_F& contentRect, bool* outOnLabelOrIcon) const {
    if (outOnLabelOrIcon) *outOnLabelOrIcon = false;
    if (!PathUtils::PtInRectF(x, y, contentRect)) {
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
    if (m_items.empty() || m_viewMode == ViewMode::Proportions) return 0.0f;
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
    if (m_viewMode == ViewMode::Proportions) return;

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
                if (m_activeRibbonTab != RibbonTab::Search) {
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
                SetWindowTextW(m_searchEdit, m_filterText.c_str());
                SetFocus(m_searchEdit);
                SendMessageW(m_searchEdit, EM_SETSEL, 0, -1);
                return;
            }
        }

        D2D1_RECT_F mainRect = GetMainViewRect();
        if (m_showNavPane && std::abs(x - m_treePaneWidth) <= 5.0f && y >= mainRect.top && y <= mainRect.bottom) {
            m_draggingTreeSplitter = true;
            SetCapture(m_hwnd);
            return;
        }
    }

    D2D1_RECT_F mainRect = GetMainViewRect();

    // Check Left Navigation Tree click (Left-Click expands/navigates; Right-Click opens Szybki dostęp / Tree Context Menu!)
    if (!m_isFullScreen && m_showNavPane && x < m_treePaneWidth && y >= mainRect.top && y <= mainRect.bottom) {
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
            } else {
                SelectSingle(hitIdx, true);
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (m_viewMode != ViewMode::Proportions) {
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
    m_hoverTreemapHitIndex = -1;
    if (m_viewMode == ViewMode::Proportions) {
        for (int i = static_cast<int>(m_treemapHits.size()) - 1; i >= 0; --i) {
            if (PathUtils::PtInRectF(x, y, m_treemapHits[i].rect)) {
                m_hoverTreemapHitIndex = i;
                break;
            }
        }
        m_hoverIndex = (m_hoverTreemapHitIndex >= 0) ? m_treemapHits[m_hoverTreemapHitIndex].topItemIndex : -1;
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
        prevTreemapHover != m_hoverTreemapHitIndex ||
        prevRowAction != m_hoverRowActionKind ||
        prevRibbonHover != m_hoverRibbonBtn ||
        prevTreeHover != m_hoverTreeIndex ||
        prevBcHover != m_hoverBreadcrumbIndex ||
        prevBcArrowHover != m_hoverBreadcrumbArrowIndex) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void ExplorerApp::OnMouseUp(float x, float y, bool rightButton, WPARAM keyFlags) {
    (void)x; (void)y; (void)rightButton; (void)keyFlags;
    if (m_draggingTreeSplitter || m_resizingCol >= 0 || m_draggingScrollbar || m_viewerDraggingPan || m_isBoxSelecting) {
        m_draggingTreeSplitter = false;
        m_resizingCol = -1;
        m_draggingScrollbar = false;
        m_viewerDraggingPan = false;
        m_isBoxSelecting = false;
        ReleaseCapture();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void ExplorerApp::OnMouseDoubleClick(float x, float y, WPARAM keyFlags) {
    (void)keyFlags;

    // Double-clicking a node in the Left Navigation Tree expands/collapses its subfolders!
    D2D1_RECT_F mainRect = GetMainViewRect();
    if (!m_isFullScreen && m_showNavPane && x < m_treePaneWidth && y >= mainRect.top && y <= mainRect.bottom) {
        for (int i = 0; i < static_cast<int>(m_treeNodes.size()); ++i) {
            if (PathUtils::PtInRectF(x, y, m_treeNodes[i].hitRect)) {
                ToggleTreeNodeExpand(i);
                return;
            }
        }
    }

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

    // If cursor is over the Left Navigation Tree, scroll the tree pane!
    D2D1_RECT_F mainRect = GetMainViewRect();
    if (!m_isFullScreen && m_showNavPane && x < m_treePaneWidth && y >= mainRect.top && y <= mainRect.bottom) {
        float maxTreeScroll = std::max(0.0f, static_cast<float>(m_treeNodes.size()) * 26.0f - (mainRect.bottom - mainRect.top) + 16.0f);
        m_treeScrollY = std::clamp(m_treeScrollY - steps * 52.0f, 0.0f, maxTreeScroll);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // Ctrl + MouseWheel ALWAYS cycles between all 9 View Modes (Details <-> List <-> Small <-> Medium <-> Large <-> ExtraLarge <-> Jumbo <-> Proportions <-> FullViewer)!
    if (ctrlHeld) {
        int cur = static_cast<int>(m_viewMode);
        int next = std::clamp(cur + (steps > 0 ? 1 : -1), 0, static_cast<int>(ViewMode::FullViewer));
        if (next != cur) {
            m_viewMode = static_cast<ViewMode>(next);
            if (m_viewMode != ViewMode::FullViewer) {
                m_lastNonViewerMode = m_viewMode;
            } else if (m_focusedIndex < 0 && !m_items.empty()) {
                SelectSingle(0, true);
            }
            if (m_viewMode == ViewMode::Proportions && m_currentPath != L"THIS_PC") {
                m_fileEngine.RequestFolderSizes(m_items, false);
            }
            ClampScrollOffset();
            EnsureItemVisible(m_focusedIndex);
            m_imageCache.BumpViewportGeneration();
            SyncMediaPlayerToSelection();

            const Strings& s = GetStrings(m_lang);
            const wchar_t* modeNames[] = {
                s.viewDetails, s.viewList, s.viewSmall, s.viewMedium,
                s.viewLarge, s.viewExtraLarge, s.viewJumbo, s.viewProportions, s.viewFullViewer
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
    m_scrollY -= steps * scrollStep;
    ClampScrollOffset();
    m_imageCache.BumpViewportGeneration();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::OnKeyDown(WPARAM vk) {
    bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;

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
    case CMD_TAB_FILE:   m_activeRibbonTab = RibbonTab::File;   m_ribbonCollapsed = false; break;
    case CMD_TAB_HOME:   m_activeRibbonTab = RibbonTab::Home;   m_ribbonCollapsed = false; break;
    case CMD_TAB_SHARE:  m_activeRibbonTab = RibbonTab::Share;  m_ribbonCollapsed = false; break;
    case CMD_TAB_VIEW:   m_activeRibbonTab = RibbonTab::View;   m_ribbonCollapsed = false; break;
    case CMD_TAB_SEARCH: m_activeRibbonTab = RibbonTab::Search; m_ribbonCollapsed = false; break;
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
    case CMD_DELETE_MAIN:   DeleteSelectedItems(false); break;
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
    case CMD_PROPERTIES: {
        auto paths = GetSelectedPaths();
        if (!paths.empty()) FileEngine::OpenFileOrProperties(m_hwnd, paths[0], true);
        else if (!m_currentPath.empty() && m_currentPath != L"THIS_PC") FileEngine::OpenFileOrProperties(m_hwnd, m_currentPath, true);
        break;
    }
    case CMD_OPEN:          ActivateFocusedOrSelectedItem(); break;
    case CMD_SELECT_ALL:    SelectAll(); break;
    case CMD_SELECT_NONE:   SelectNone(); break;
    case CMD_INVERT_SEL:    InvertSelection(); break;

    case CMD_TOGGLE_NAVPANE:     m_showNavPane = !m_showNavPane; break;
    case CMD_TOGGLE_PREVIEWPANE: m_showPreviewPane = !m_showPreviewPane; SyncMediaPlayerToSelection(); break;
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
        }
        SyncMediaPlayerToSelection();
        break;
    case CMD_VIEW_FULLVIEWER:    ToggleFullViewerMode(); break;
    case CMD_VIEW_FULLSCREEN:    ToggleFullScreen(); break;
    case CMD_TOGGLE_EXT:         m_showExtensions = !m_showExtensions; RefreshCurrentFolder(); break;
    case CMD_TOGGLE_HIDDEN:      m_showHidden = !m_showHidden; RefreshCurrentFolder(); break;

    case CMD_NAV_BACK:          NavigateBack(); break;
    case CMD_NAV_FORWARD:       NavigateForward(); break;
    case CMD_NAV_UP:            NavigateUp(); break;
    case CMD_NAV_REFRESH:       RefreshCurrentFolder(); break;
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

    m_fileEngine.EnqueueDelete({ delPath }, permanent);
}

void ExplorerApp::PerformUndoDelete() {
    std::wstring restoredDir;
    if (m_fileEngine.UndoLastDelete(restoredDir)) {
        const Strings& s = GetStrings(m_lang);
        m_statusToast = s.statusRestored;
        m_statusToastExpireTick = GetTickCount64() + 3500;
        RefreshCurrentFolder();
    }
}

void ExplorerApp::CopySelected(bool isCut) {
    auto paths = GetSelectedPaths();
    if (paths.empty()) return;

    if (FileEngine::CopyFilesToClipboard(m_hwnd, paths, isCut)) {
        for (auto& item : m_items) {
            item.isCut = isCut && item.isSelected;
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void ExplorerApp::PasteIntoCurrent() {
    bool wasCut = false;
    if (FileEngine::PasteFilesFromClipboard(m_hwnd, m_currentPath, wasCut)) {
        RefreshCurrentFolder();
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
        RefreshCurrentFolder(created);
        StartInlineRename();
    }
}

void ExplorerApp::StartInlineRename() {
    if (m_focusedIndex < 0 || m_focusedIndex >= static_cast<int>(m_items.size())) return;
    if (m_currentPath == L"THIS_PC" || m_viewMode == ViewMode::FullViewer) return;

    EnsureItemVisible(m_focusedIndex);
    D2D1_RECT_F contentRect = GetContentItemsRect();
    D2D1_RECT_F itemR = GetItemRect(m_focusedIndex, contentRect);

    m_renamingIndex = m_focusedIndex;
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

void ExplorerApp::CommitAddressOrRenameEdit(HWND editHwnd) {
    wchar_t buf[MAX_PATH] = {};
    GetWindowTextW(editHwnd, buf, MAX_PATH);
    ShowWindow(editHwnd, SW_HIDE);
    SetFocus(m_hwnd);

    if (editHwnd == m_addressEdit) {
        std::wstring newPath = buf;
        if (!newPath.empty()) {
            NavigateTo(newPath, true);
        }
    } else if (editHwnd == m_searchEdit) {
        m_filterText = buf;
        TriggerAsyncSearch();
    } else if (editHwnd == m_renameEdit && m_editingSearchFieldCmd != 0) {
        int fcmd = m_editingSearchFieldCmd;
        m_editingSearchFieldCmd = 0;
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
    } else if (editHwnd == m_renameEdit && m_renamingIndex >= 0 && m_renamingIndex < static_cast<int>(m_items.size())) {
        std::wstring newName = buf;
        int idx = m_renamingIndex;
        m_renamingIndex = -1;
        if (!newName.empty() && newName != m_items[idx].name) {
            std::wstring newFullPath;
            if (FileEngine::RenameFileOrFolder(m_items[idx].fullPath, newName, newFullPath)) {
                RefreshCurrentFolder(newFullPath);
            }
        }
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ExplorerApp::CancelInlineEdits() {
    if (m_addressEdit && IsWindowVisible(m_addressEdit)) ShowWindow(m_addressEdit, SW_HIDE);
    if (m_renameEdit && IsWindowVisible(m_renameEdit)) {
        if (m_editingSearchFieldCmd != 0) {
            CommitAddressOrRenameEdit(m_renameEdit);
            return;
        }
        ShowWindow(m_renameEdit, SW_HIDE);
    }
    if (m_searchEdit && IsWindowVisible(m_searchEdit) && m_filterText.empty()) ShowWindow(m_searchEdit, SW_HIDE);
    m_renamingIndex = -1;
    m_editingSearchFieldCmd = 0;
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

    if (cmd >= CMD_TOGGLE_NAVPANE && cmd <= CMD_VIEW_PROPORTIONS) {
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

    if (folderScanning || searchRunning || recentlyFinishedScan) {
        SetTimer(m_hwnd, ID_TIMER_WORK_ANIM, 35, nullptr);
    } else {
        KillTimer(m_hwnd, ID_TIMER_WORK_ANIM);
    }
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

} // namespace Explorer2
