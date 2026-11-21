#include "ShellMenuEmulator.h"
#include "ThreadUtils.h"
#include "ImageFormatLoader.h"
#include "ImageOrientation.h"
#include "JpegSegments.h"
#include "PathUtils.h"
#ifndef _WIN32
#include "ToolProcess.h"    // jpegtran: JPEG turned without loss on Linux
#endif
#include <wrl/client.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <wincodec.h>
#ifdef _WIN32
#include <wincodecsdk.h>    // IWICMetadataBlockReader / Writer: a photo's EXIF data goes along into the saved file
#include <shobjidl.h>       // SHAssocEnumHandlers / IAssocHandler: the programs Windows recommends for a file type
#endif
#include <thread>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "advapi32.lib")

using Microsoft::WRL::ComPtr;

namespace Explorer2 {

// An empty 16 x 16 menu picture: 32 bits per pixel, top-down, with its pixels in *bits (nullptr when it failed)
static HBITMAP CreateMenuDib16(void** bits) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 16;
    bmi.bmiHeader.biHeight = -16;           // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    *bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, bits, nullptr, 0);
    ReleaseDC(nullptr, screenDc);
    if (dib && !*bits) {
        DeleteObject(dib);
        return nullptr;
    }
    return dib;
}

static HBITMAP HIconToMenuBitmap16(HICON hIcon) {
    if (!hIcon) return nullptr;

    void* bits = nullptr;
    HBITMAP hDib = CreateMenuDib16(&bits);
    if (hDib) {
        memset(bits, 0, 16 * 16 * 4);
        HDC memDc = CreateCompatibleDC(nullptr);
        HGDIOBJ oldObj = SelectObject(memDc, hDib);
        DrawIconEx(memDc, 0, 0, hIcon, 16, 16, 0, nullptr, DI_NORMAL);
        SelectObject(memDc, oldObj);
        DeleteDC(memDc);

        // Premultiply alpha for Win32 MENUITEMINFOW MIIM_BITMAP
        uint8_t* px = static_cast<uint8_t*>(bits);
        bool anyAlpha = false;
        for (int i = 0; i < 16 * 16 * 4; i += 4) {
            if (px[i + 3] != 0) { anyAlpha = true; break; }
        }
        if (anyAlpha) {
            for (int i = 0; i < 16 * 16 * 4; i += 4) {
                uint8_t a = px[i + 3];
                px[i + 0] = static_cast<uint8_t>((px[i + 0] * a) / 255);
                px[i + 1] = static_cast<uint8_t>((px[i + 1] * a) / 255);
                px[i + 2] = static_cast<uint8_t>((px[i + 2] * a) / 255);
            }
        }
    }
    return hDib;
}

static std::wstring ExtractExecutableOrIconPath(const std::wstring& rawSpec, int& outIconIndex) {
    outIconIndex = 0;
    std::wstring s = rawSpec;
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
    if (s.empty()) return L"";

    // Check trailing ",index" (e.g. "C:\...\app.exe,0" or "\"C:\...\app.exe\",-101")
    size_t comma = s.find_last_of(L',');
    if (comma != std::wstring::npos && comma + 1 < s.size()) {
        bool allDigits = true;
        for (size_t i = comma + 1; i < s.size(); ++i) {
            if (s[i] != L'-' && (s[i] < L'0' || s[i] > L'9')) { allDigits = false; break; }
        }
        if (allDigits) {
            outIconIndex = _wtoi(s.substr(comma + 1).c_str());
            s = s.substr(0, comma);
        }
    }

    if (s.front() == L'\"') {
        size_t endQ = s.find(L'\"', 1);
        if (endQ != std::wstring::npos) {
            s = s.substr(1, endQ - 1);
        } else {
            s.erase(s.begin());
        }
    } else {
        std::wstring lower = PathUtils::ToLower(s);
        size_t exePos = lower.find(L".exe");
        if (exePos != std::wstring::npos) {
            s = s.substr(0, exePos + 4);
        }
    }

    wchar_t expanded[MAX_PATH] = {};
    if (ExpandEnvironmentStringsW(s.c_str(), expanded, MAX_PATH) && expanded[0]) {
        s = expanded;
    }
    return s;
}

static HBITMAP ExtractSmallMenuBitmap(const std::wstring& rawSpec) {
    int iconIdx = 0;
    std::wstring path = ExtractExecutableOrIconPath(rawSpec, iconIdx);
    if (path.empty()) return nullptr;

    HICON hSmall = nullptr;
    if (ExtractIconExW(path.c_str(), iconIdx, nullptr, &hSmall, 1) > 0 && hSmall) {
        HBITMAP hBmp = HIconToMenuBitmap16(hSmall);
        DestroyIcon(hSmall);
        if (hBmp) return hBmp;
    }

    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON) && sfi.hIcon) {
        HBITMAP hBmp = HIconToMenuBitmap16(sfi.hIcon);
        DestroyIcon(sfi.hIcon);
        return hBmp;
    }
    return nullptr;
}

void ShellMenuEmulator::AttachMenuBitmap(HMENU hMenu, UINT itemIdOrPos, bool byPosition, HBITMAP hBmp) {
    if (!hMenu || !hBmp) return;
    MENUITEMINFOW mii = { sizeof(mii) };
    mii.fMask = MIIM_BITMAP;
    mii.hbmpItem = hBmp;
    SetMenuItemInfoW(hMenu, itemIdOrPos, byPosition ? TRUE : FALSE, &mii);
}

ShellMenuEmulator::ShellMenuEmulator() = default;

ShellMenuEmulator::~ShellMenuEmulator() {
    // The scan fills this object in, so the object must not go away under it. It reads the registry and
    // one folder, which takes a moment at most.
    JoinServingCom(m_scanThread);
}

void ShellMenuEmulator::InitializeAsync() {
    if (m_scanThread.joinable()) m_scanThread.join();      // a scan started earlier finishes first
    m_scanThread = std::thread([this]() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ScanStartupData();
        CoUninitialize();
    });
}

void ShellMenuEmulator::ScanStartupData() {
    std::wstring sevenZGui, sevenZFm, winRar;

    HKEY hKey7z = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\7-Zip", 0, KEY_READ | KEY_WOW64_64KEY, &hKey7z) == ERROR_SUCCESS) {
        wchar_t pathBuf[MAX_PATH] = {};
        DWORD cb = sizeof(pathBuf);
        if (RegQueryValueExW(hKey7z, L"Path64", nullptr, nullptr, reinterpret_cast<LPBYTE>(pathBuf), &cb) == ERROR_SUCCESS ||
            RegQueryValueExW(hKey7z, L"Path", nullptr, nullptr, reinterpret_cast<LPBYTE>(pathBuf), &cb) == ERROR_SUCCESS) {
            std::wstring base = PathUtils::EnsureTrailingSlash(pathBuf);
            if (GetFileAttributesW((base + L"7zG.exe").c_str()) != INVALID_FILE_ATTRIBUTES) {
                sevenZGui = base + L"7zG.exe";
            }
            if (GetFileAttributesW((base + L"7zFM.exe").c_str()) != INVALID_FILE_ATTRIBUTES) {
                sevenZFm = base + L"7zFM.exe";
            }
        }
        RegCloseKey(hKey7z);
    }
    if (sevenZGui.empty() && GetFileAttributesW(L"C:\\Program Files\\7-Zip\\7zG.exe") != INVALID_FILE_ATTRIBUTES) {
        sevenZGui = L"C:\\Program Files\\7-Zip\\7zG.exe";
        sevenZFm = L"C:\\Program Files\\7-Zip\\7zFM.exe";
    }
#ifndef _WIN32
    if (sevenZGui.empty()) {
        for (const wchar_t* cand : { L"/usr/bin/7z", L"/usr/bin/7zz", L"/usr/bin/7za", L"/bin/7z" }) {
            if (GetFileAttributesW(cand) != INVALID_FILE_ATTRIBUTES) {
                sevenZGui = cand;
                break;
            }
        }
    }
#endif

    if (GetFileAttributesW(L"C:\\Program Files\\WinRAR\\WinRAR.exe") != INVALID_FILE_ATTRIBUTES) {
        winRar = L"C:\\Program Files\\WinRAR\\WinRAR.exe";
    }

    HBITMAP bmp7z = !sevenZFm.empty() ? ExtractSmallMenuBitmap(sevenZFm) : (!sevenZGui.empty() ? ExtractSmallMenuBitmap(sevenZGui) : nullptr);
    HBITMAP bmpTerm = ExtractSmallMenuBitmap(L"powershell.exe");
    HBITMAP bmpExp = ExtractSmallMenuBitmap(L"explorer.exe");

    HBITMAP bmpFolder = nullptr;
    SHFILEINFOW sfiFolder = {};
    if (SHGetFileInfoW(L"C:\\Windows", FILE_ATTRIBUTE_DIRECTORY, &sfiFolder, sizeof(sfiFolder),
                       SHGFI_USEFILEATTRIBUTES | SHGFI_ICON | SHGFI_SMALLICON) && sfiFolder.hIcon) {
        bmpFolder = HIconToMenuBitmap16(sfiFolder.hIcon);
        DestroyIcon(sfiFolder.hIcon);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_7zGPath = sevenZGui;
        m_7zFMPath = sevenZFm;
        m_winRarPath = winRar;
        m_7zMenuBmp = bmp7z;
        m_terminalMenuBmp = bmpTerm;
        m_explorerMenuBmp = bmpExp;
        m_folderMenuBmp = bmpFolder;
    }

    ScanRegistryShellKey(HKEY_CLASSES_ROOT, L"*\\shell", true, false, false);
    ScanRegistryShellKey(HKEY_CLASSES_ROOT, L"Directory\\shell", false, true, false);
    ScanRegistryShellKey(HKEY_CLASSES_ROOT, L"Directory\\Background\\shell", false, false, true);

    std::vector<OpenWithApp> apps;
    auto tryAddApp = [&](const wchar_t* label, const std::wstring& path, bool imgOnly) {
        if (!path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            apps.push_back({ label, path, ExtractSmallMenuBitmap(path), imgOnly });
        }
    };

    wchar_t sysDir[MAX_PATH] = {};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring sysBase = PathUtils::EnsureTrailingSlash(sysDir);

    tryAddApp(L"Paint", sysBase + L"mspaint.exe", true);
    tryAddApp(L"Notatnik (Notepad)", sysBase + L"notepad.exe", false);

    wchar_t localAppData[MAX_PATH] = {};
    GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    std::wstring localBase = localAppData[0] ? PathUtils::EnsureTrailingSlash(localAppData) : L"";

    if (!localBase.empty()) {
        tryAddApp(L"Visual Studio Code", localBase + L"Programs\\Microsoft VS Code\\Code.exe", false);
    }
    tryAddApp(L"Visual Studio Code", L"C:\\Program Files\\Microsoft VS Code\\Code.exe", false);
    tryAddApp(L"Notepad++", L"C:\\Program Files\\Notepad++\\notepad++.exe", false);
    tryAddApp(L"Notepad++ (x86)", L"C:\\Program Files (x86)\\Notepad++\\notepad++.exe", false);
    tryAddApp(L"IrfanView", L"C:\\Program Files\\IrfanView\\i_view64.exe", true);
    tryAddApp(L"Honeyview", L"C:\\Program Files\\Honeyview\\Honeyview.exe", true);
#ifndef _WIN32
    tryAddApp(L"Visual Studio Code", L"/usr/bin/code", false);
    tryAddApp(L"GNOME Text Editor (gedit)", L"/usr/bin/gedit", false);
    tryAddApp(L"KDE Kate", L"/usr/bin/kate", false);
    tryAddApp(L"Mousepad", L"/usr/bin/mousepad", false);
    tryAddApp(L"GIMP", L"/usr/bin/gimp", true);
    tryAddApp(L"Inkscape", L"/usr/bin/inkscape", true);
    tryAddApp(L"Eye of GNOME (eog)", L"/usr/bin/eog", true);
    tryAddApp(L"feh", L"/usr/bin/feh", true);
#endif

    std::vector<SendToTarget> sendTo;
    PWSTR pszSendTo = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SendTo, 0, nullptr, &pszSendTo)) && pszSendTo) {
        std::wstring sendToDir = PathUtils::EnsureTrailingSlash(pszSendTo);
        CoTaskMemFree(pszSendTo);

        std::wstring pattern = sendToDir + L"*";
        WIN32_FIND_DATAW ffd = {};
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &ffd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (ffd.cFileName[0] == L'.') continue;
                if (ffd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
                std::wstring fname = ffd.cFileName;
                size_t dot = fname.find_last_of(L'.');
                std::wstring ext = (dot != std::wstring::npos) ? fname.substr(dot) : L"";
                std::wstring cleanTitle = (dot != std::wstring::npos) ? fname.substr(0, dot) : fname;
                if (_wcsicmp(ext.c_str(), L".lnk") == 0 || _wcsicmp(ext.c_str(), L".exe") == 0) {
                    SendToTarget st;
                    st.label = cleanTitle;
                    st.targetPath = sendToDir + fname;
                    st.hMenuBmp = ExtractSmallMenuBitmap(st.targetPath);
                    sendTo.push_back(std::move(st));
                }
            } while (FindNextFileW(hFind, &ffd));
            FindClose(hFind);
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_openWithApps = std::move(apps);
        m_sendToTargets = std::move(sendTo);
        m_ready = true;
    }
}

void ShellMenuEmulator::ScanRegistryShellKey(HKEY hRoot, const wchar_t* subKeyPath, bool isFile, bool isFolder, bool isBg) {
    HKEY hShellKey = nullptr;
    if (RegOpenKeyExW(hRoot, subKeyPath, 0, KEY_READ, &hShellKey) != ERROR_SUCCESS) {
        return;
    }

    wchar_t subName[256] = {};
    for (DWORD idx = 0; ; ++idx) {
        DWORD nameLen = 256;
        if (RegEnumKeyExW(hShellKey, idx, subName, &nameLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }

        if (_wcsicmp(subName, L"open") == 0 ||
            _wcsicmp(subName, L"opennewprocess") == 0 ||
            _wcsicmp(subName, L"opennewwindow") == 0 ||
            _wcsicmp(subName, L"openas") == 0 ||
            _wcsicmp(subName, L"print") == 0 ||
            _wcsicmp(subName, L"printto") == 0 ||
            _wcsicmp(subName, L"edit") == 0 ||
            _wcsicmp(subName, L"find") == 0 ||
            _wcsicmp(subName, L"runas") == 0 ||
            _wcsicmp(subName, L"runasuser") == 0 ||
            _wcsicmp(subName, L" pintohome") == 0 ||
            _wcsicmp(subName, L"pintohome") == 0 ||
            _wcsicmp(subName, L"cmd") == 0 ||
            _wcsicmp(subName, L"Powershell") == 0) {
            continue;
        }

        HKEY hVerbKey = nullptr;
        if (RegOpenKeyExW(hShellKey, subName, 0, KEY_READ, &hVerbKey) == ERROR_SUCCESS) {
            if (RegQueryValueExW(hVerbKey, L"LegacyDisable", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS ||
                RegQueryValueExW(hVerbKey, L"ProgrammaticAccessOnly", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                RegCloseKey(hVerbKey);
                continue;
            }

            wchar_t labelBuf[256] = {};
            DWORD cbLabel = sizeof(labelBuf);
            if (RegQueryValueExW(hVerbKey, L"MUIVerb", nullptr, nullptr, reinterpret_cast<LPBYTE>(labelBuf), &cbLabel) != ERROR_SUCCESS || !labelBuf[0]) {
                cbLabel = sizeof(labelBuf);
                RegQueryValueExW(hVerbKey, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(labelBuf), &cbLabel);
            }

            std::wstring label = labelBuf[0] ? labelBuf : subName;
            if (!label.empty() && label[0] == L'@') {
                wchar_t resolved[256] = {};
                if (SUCCEEDED(SHLoadIndirectString(label.c_str(), resolved, 256, nullptr)) && resolved[0]) {
                    label = resolved;
                } else {
                    RegCloseKey(hVerbKey);
                    continue;
                }
            }

            wchar_t iconBuf[512] = {};
            DWORD cbIcon = sizeof(iconBuf);
            RegQueryValueExW(hVerbKey, L"Icon", nullptr, nullptr, reinterpret_cast<LPBYTE>(iconBuf), &cbIcon);

            HKEY hCmdKey = nullptr;
            if (RegOpenKeyExW(hVerbKey, L"command", 0, KEY_READ, &hCmdKey) == ERROR_SUCCESS) {
                wchar_t cmdBuf[1024] = {};
                DWORD cbCmd = sizeof(cmdBuf);
                if (RegQueryValueExW(hCmdKey, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(cmdBuf), &cbCmd) == ERROR_SUCCESS && cmdBuf[0]) {
                    EmulatedShellVerb v;
                    v.label = label;
                    v.commandTemplate = cmdBuf;
                    v.hMenuBmp = ExtractSmallMenuBitmap(iconBuf[0] ? iconBuf : cmdBuf);
                    v.forFiles = isFile;
                    v.forFolders = isFolder;
                    v.forBackground = isBg;

                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_verbs.push_back(std::move(v));
                }
                RegCloseKey(hCmdKey);
            }
            RegCloseKey(hVerbKey);
        }
    }

    RegCloseKey(hShellKey);
}

bool ShellMenuEmulator::Has7Zip(std::wstring* out7zGuiExe, std::wstring* out7zFmExe) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (out7zGuiExe) *out7zGuiExe = m_7zGPath;
    if (out7zFmExe) *out7zFmExe = m_7zFMPath;
    return !m_7zGPath.empty();
}

std::vector<EmulatedShellVerb> ShellMenuEmulator::GetDiscoveredVerbs(bool isFile, bool isFolder, bool isBackground) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<EmulatedShellVerb> result;
    for (const auto& v : m_verbs) {
        if ((isFile && v.forFiles) || (isFolder && v.forFolders) || (isBackground && v.forBackground)) {
            result.push_back(v);
        }
    }
    return result;
}

std::vector<OpenWithApp> ShellMenuEmulator::GetOpenWithApps(bool isImage) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<OpenWithApp> result;
    for (const auto& app : m_openWithApps) {
        if (!app.forImagesOnly || isImage) {
            result.push_back(app);
        }
    }
    return result;
}

#ifdef _WIN32
// A picture file (the PNG icon of a Store app) as a 16 x 16 menu picture, premultiplied like HIconToMenuBitmap16's
static HBITMAP PictureFileToMenuBitmap16(const std::wstring& path) {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) || !wic ||
        FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder ||
        FAILED(decoder->GetFrame(0, &frame)) || !frame || FAILED(wic->CreateBitmapScaler(&scaler)) || !scaler ||
        FAILED(scaler->Initialize(frame.Get(), 16, 16, WICBitmapInterpolationModeFant)) ||
        FAILED(wic->CreateFormatConverter(&converter)) || !converter ||
        FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        return nullptr;
    }
    void* bits = nullptr;
    HBITMAP dib = CreateMenuDib16(&bits);
    if (!dib || FAILED(converter->CopyPixels(nullptr, 16 * 4, 16 * 16 * 4, static_cast<BYTE*>(bits)))) {
        if (dib) DeleteObject(dib);
        return nullptr;
    }
    return dib;
}

// The menu picture of a program as the shell names its icon: "@{...}" is a resource of a Store app (it resolves to
// a PNG file), anything else a file with icons and an index
static HBITMAP HandlerMenuBitmap(const wchar_t* location, int index) {
    if (!location || !location[0]) return nullptr;
    if (location[0] == L'@') {
        wchar_t resolved[MAX_PATH] = {};
        if (SUCCEEDED(SHLoadIndirectString(location, resolved, MAX_PATH, nullptr)) && resolved[0]) {
            return PictureFileToMenuBitmap16(resolved);
        }
        return nullptr;
    }
    HICON hSmall = nullptr;
    if (ExtractIconExW(location, index, nullptr, &hSmall, 1) > 0 && hSmall) {
        HBITMAP bmp = HIconToMenuBitmap16(hSmall);
        DestroyIcon(hSmall);
        if (bmp) return bmp;
    }
    return ExtractSmallMenuBitmap(location);
}
#endif

std::vector<OpenWithApp> ShellMenuEmulator::OpenWithAppsFor(const std::wstring& extLower, bool isImage) {
#ifdef _WIN32
    if (extLower.size() > 1 && extLower[0] == L'.') {
        constexpr ULONGLONG kKeepMs = 3 * 60 * 1000;      // a program installed meanwhile shows up after this
        const ULONGLONG now = GetTickCount64();
        auto it = m_openWithCache.find(extLower);
        if (it == m_openWithCache.end() || now - it->second.tick > kKeepMs) {
            OpenWithCacheEntry entry;
            entry.tick = now;
            ComPtr<IEnumAssocHandlers> handlers;
            if (SUCCEEDED(SHAssocEnumHandlers(extLower.c_str(), ASSOC_FILTER_RECOMMENDED, &handlers)) && handlers) {
                IAssocHandler* raw = nullptr;
                ULONG fetched = 0;
                while (entry.apps.size() < kMaxOpenWithApps && handlers->Next(1, &raw, &fetched) == S_OK && fetched == 1 && raw) {
                    std::shared_ptr<void> holder(raw, [](void* p) { static_cast<IAssocHandler*>(p)->Release(); });
                    LPWSTR uiName = nullptr, name = nullptr, icon = nullptr;
                    int iconIndex = 0;
                    raw->GetUIName(&uiName);
                    raw->GetName(&name);
                    raw->GetIconLocation(&icon, &iconIndex);
                    raw = nullptr;
                    OpenWithApp app;
                    app.name = uiName ? uiName : L"";
                    app.exePath = name ? name : L"";
                    // Unnamed entries (the shell lists a few) are left out, and a program listed twice is shown once
                    const bool listed = std::any_of(entry.apps.begin(), entry.apps.end(),
                                                    [&](const OpenWithApp& a) { return _wcsicmp(a.name.c_str(), app.name.c_str()) == 0; });
                    if (!app.name.empty() && !listed) {
                        const std::wstring iconKey = std::wstring(icon ? icon : L"") + L"," + std::to_wstring(iconIndex);
                        auto ic = m_handlerIcons.find(iconKey);
                        if (ic == m_handlerIcons.end()) ic = m_handlerIcons.emplace(iconKey, HandlerMenuBitmap(icon, iconIndex)).first;
                        app.hMenuBmp = ic->second;
                        app.handler = std::move(holder);
                        entry.apps.push_back(std::move(app));
                    }
                    CoTaskMemFree(uiName);
                    CoTaskMemFree(name);
                    CoTaskMemFree(icon);
                }
            }
            it = m_openWithCache.insert_or_assign(extLower, std::move(entry)).first;
        }
        if (!it->second.apps.empty()) return it->second.apps;
    }
#endif
    return GetOpenWithApps(isImage);
}

void ShellMenuEmulator::AddOpenWithItems(HMENU popup, std::vector<OpenWithApp>& apps, UINT firstCmdId) {
    if (!popup) return;
    if (apps.size() > kMaxOpenWithApps) apps.resize(kMaxOpenWithApps);
    UINT pos = 0;
    for (size_t i = 0; i < apps.size(); ++i, ++pos) {
        const UINT id = firstCmdId + static_cast<UINT>(i);
#ifdef _WIN32
        InsertMenuW(popup, pos, MF_BYPOSITION | MF_STRING, id, apps[i].name.c_str());
#else
        AppendMenuW(popup, MF_STRING, id, apps[i].name.c_str());     // the HAL's menus have no InsertMenuW
#endif
        if (apps[i].hMenuBmp) AttachMenuBitmap(popup, id, false, apps[i].hMenuBmp);
    }
    if (pos == 0) return;
#ifdef _WIN32
    InsertMenuW(popup, pos, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
#else
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
#endif
}

void ShellMenuEmulator::ReleaseOpenWithCache() {
    m_openWithCache.clear();
    for (auto& [location, bmp] : m_handlerIcons) {
        if (bmp) DeleteObject(bmp);
    }
    m_handlerIcons.clear();
}

bool ShellMenuEmulator::LaunchOpenWithApp(HWND hwnd, const OpenWithApp& app, const std::vector<std::wstring>& paths, const std::wstring& workDir) {
    if (paths.empty()) return false;
#ifdef _WIN32
    if (app.handler) {
        // The files as the shell hands them to a program: a data object of their items, given to the handler
        std::vector<PIDLIST_ABSOLUTE> pidls;
        for (const std::wstring& p : paths) {
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (SUCCEEDED(SHParseDisplayName(p.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) pidls.push_back(pidl);
        }
        bool started = false;
        {
            std::vector<PCIDLIST_ABSOLUTE> items_(pidls.begin(), pidls.end());     // the type the call takes
            ComPtr<IShellItemArray> items;
            ComPtr<IDataObject> data;
            if (!items_.empty() &&
                SUCCEEDED(SHCreateShellItemArrayFromIDLists(static_cast<UINT>(items_.size()), items_.data(), &items)) &&
                items && SUCCEEDED(items->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data))) && data) {
                started = SUCCEEDED(static_cast<IAssocHandler*>(app.handler.get())->Invoke(data.Get()));
            }
        }
        for (PIDLIST_ABSOLUTE pidl : pidls) CoTaskMemFree(pidl);
        if (started) return true;
    }
#endif
    // The fixed list, and a handler that did not start: the program itself, with the first file
    if (app.exePath.empty() || GetFileAttributesW(app.exePath.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    const std::wstring arg = L"\"" + paths.front() + L"\"";
    return reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", app.exePath.c_str(), arg.c_str(),
                                                   workDir.empty() ? nullptr : workDir.c_str(), SW_SHOWNORMAL)) > 32;
}

std::vector<SendToTarget> ShellMenuEmulator::GetSendToTargets(Lang lang) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SendToTarget> result;

    SendToTarget desk;
    desk.label = (lang == Lang::PL) ? L"Pulpit (utwórz skrót)" : L"Desktop (create shortcut)";
    desk.isDesktopShortcut = true;
    result.push_back(desk);

    SendToTarget zip;
    zip.label = (lang == Lang::PL) ? L"Folder skompresowany (zip)" : L"Compressed (zipped) folder";
    zip.isZipArchive = true;
    result.push_back(zip);

    for (const auto& t : m_sendToTargets) {
        result.push_back(t);
    }
    return result;
}

void ShellMenuEmulator::ExecuteVerbCommand(const EmulatedShellVerb& verb, const std::wstring& targetPath) {
    std::wstring cmd = verb.commandTemplate;
    const wchar_t* tokens[] = { L"\"%1\"", L"%1", L"\"%V\"", L"%V", L"\"%v\"", L"%v", L"\"%L\"", L"%L" };
    bool replaced = false;
    for (const wchar_t* tok : tokens) {
        size_t pos = cmd.find(tok);
        if (pos != std::wstring::npos) {
            std::wstring replacement = L"\"" + targetPath + L"\"";
            cmd.replace(pos, wcslen(tok), replacement);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        cmd += L" \"" + targetPath + L"\"";
    }

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    std::wstring workDir = targetPath;
    DWORD attrs = GetFileAttributesW(targetPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        workDir = PathUtils::GetParentPath(targetPath);
    }

    if (CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       workDir.empty() ? nullptr : workDir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void ShellMenuEmulator::Execute7ZipAction(int actionId, const std::vector<std::wstring>& selectedPaths, const std::wstring& currentDir) const {
    if (selectedPaths.empty()) return;
    std::wstring guiExe, fmExe;
    if (!Has7Zip(&guiExe, &fmExe)) return;

    const std::wstring& firstPath = selectedPaths[0];
    std::wstring baseName = PathUtils::GetFileName(firstPath);
    DWORD attrs = GetFileAttributesW(firstPath.c_str());
    bool isDir = (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
    std::wstring stem = PathUtils::GetStem(baseName, isDir);

    std::wstring quotedFiles;
    for (const auto& p : selectedPaths) {
        if (!quotedFiles.empty()) quotedFiles += L" ";
        quotedFiles += L"\"" + p + L"\"";
    }

    std::wstring exeToRun = guiExe;
    std::wstring args;

    switch (actionId) {
    case SZ_OPEN_ARCHIVE:
#ifndef _WIN32
        if (fmExe.empty()) {
            ShellExecuteW(nullptr, L"open", firstPath.c_str(), nullptr, currentDir.c_str(), SW_SHOWNORMAL);
            return;
        }
#endif
        exeToRun = fmExe.empty() ? guiExe : fmExe;
        args = L"\"" + firstPath + L"\"";
        break;
    case SZ_EXTRACT_FILES:
#ifndef _WIN32
        args = L"x \"" + firstPath + L"\" -o\"" + currentDir + L"\" -y";
#else
        args = L"x \"" + firstPath + L"\" -o\"" + currentDir + L"\" -ad";
#endif
        break;
    case SZ_EXTRACT_HERE:
#ifndef _WIN32
        args = L"x \"" + firstPath + L"\" -o\"" + currentDir + L"\" -y";
#else
        args = L"x \"" + firstPath + L"\" -o\"" + currentDir + L"\"";
#endif
        break;
    case SZ_EXTRACT_SUBFOLDER:
#ifndef _WIN32
        args = L"x \"" + firstPath + L"\" -o\"" + PathUtils::EnsureTrailingSlash(currentDir) + stem + L"\" -y";
#else
        args = L"x \"" + firstPath + L"\" -o\"" + PathUtils::EnsureTrailingSlash(currentDir) + stem + L"\"";
#endif
        break;
    case SZ_ADD_DIALOG:
#ifndef _WIN32
        args = L"a -t7z \"" + PathUtils::EnsureTrailingSlash(currentDir) + stem + L".7z\" " + quotedFiles;
#else
        args = L"a -ad \"" + PathUtils::EnsureTrailingSlash(currentDir) + stem + L"\" " + quotedFiles;
#endif
        break;
    case SZ_ADD_7Z:
        args = L"a -t7z \"" + PathUtils::EnsureTrailingSlash(currentDir) + stem + L".7z\" " + quotedFiles;
        break;
    case SZ_ADD_ZIP:
        args = L"a -tzip \"" + PathUtils::EnsureTrailingSlash(currentDir) + stem + L".zip\" " + quotedFiles;
        break;
    }

    ShellExecuteW(nullptr, L"open", exeToRun.c_str(), args.c_str(), currentDir.c_str(), SW_SHOWNORMAL);
}

void ShellMenuEmulator::ShowOpenWithDialog(HWND hwnd, const std::wstring& filePath) {
    std::wstring args = L"shell32.dll,OpenAs_RunDLL " + filePath;
    ShellExecuteW(hwnd, L"open", L"rundll32.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

bool ShellMenuEmulator::CreateShortcutFile(const std::wstring& targetFullPath, const std::wstring& linkFilePath) {
    ComPtr<IShellLinkW> psl;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl))) || !psl) {
        return false;
    }

    psl->SetPath(targetFullPath.c_str());
    std::wstring workDir = PathUtils::GetParentPath(targetFullPath);
    if (!workDir.empty()) {
        psl->SetWorkingDirectory(workDir.c_str());
    }

    ComPtr<IPersistFile> ppf;
    if (FAILED(psl.As(&ppf)) || !ppf) {
        return false;
    }
    return SUCCEEDED(ppf->Save(linkFilePath.c_str(), TRUE));
}

bool ShellMenuEmulator::CreateNewEmptyFile(const std::wstring& parentDir, const std::wstring& baseName, const std::wstring& ext, std::wstring& outCreatedPath) {
    if (parentDir.empty() || IsVirtualFolder(parentDir)) return false;
    std::wstring cleanParent = PathUtils::EnsureTrailingSlash(parentDir);

    for (int i = 1; i < 500; ++i) {
        std::wstring candidate = cleanParent + baseName;
        if (i > 1) candidate += L" (" + std::to_wstring(i) + L")";
        candidate += ext;
        if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) {
            HANDLE hFile = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                CloseHandle(hFile);
                outCreatedPath = candidate;
                return true;
            }
            return false;
        }
    }
    return false;
}

// A quarter turn saved into the file itself. It goes through TransformImageFile like every other change to a
// picture, so it sees the picture upright, keeps a photo's EXIF data and turns a JPEG without compressing it
// again where that is possible.
bool ShellMenuEmulator::RotateImageFile(const std::wstring& imagePath, bool clockwise) {
    ImageTransformParams params;
    params.kind = clockwise ? ImageTransformKind::RotateCW : ImageTransformKind::RotateCCW;
    params.overwriteOriginal = true;
    params.sameFormatOnly = true;
    ImageTransformResult result;
    return TransformImageFile(imagePath, params, result);
}

int ShellMenuEmulator::ReadExifOrientation(IWICBitmapFrameDecode* frame) {
    if (!frame) return ImageTurn::kNone;
    ComPtr<IWICMetadataQueryReader> query;
    if (FAILED(frame->GetMetadataQueryReader(&query)) || !query) return ImageTurn::kNone;
    for (const wchar_t* name : { L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}" }) {       // JPEG, TIFF
        PROPVARIANT value;
        PropVariantInit(&value);
        const bool found = SUCCEEDED(query->GetMetadataByName(name, &value)) && value.vt == VT_UI2;
        const int code = found ? static_cast<int>(value.uiVal) : 0;
        PropVariantClear(&value);
        if (found) return ImageTurn::IsValid(code) ? code : ImageTurn::kNone;
    }
    return ImageTurn::kNone;
}

// A JPEG's brightness quantisation table is libjpeg's standard table multiplied by a factor that depends on the
// quality it was saved at, so the table tells the quality. Windows' encoder is on the same scale (measured: a
// file written at 0.85 gives 85 here, at every step from 0.50 to 0.97), so a photo compressed again at this
// quality comes out about as large as it was, instead of growing because a higher quality was picked for it.
int ShellMenuEmulator::EstimateJpegQuality(const uint8_t* data, size_t size) {
    if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) return 0;
    size_t at = 2;
    while (at + 4 <= size) {
        if (data[at] != 0xFF) return 0;
        const uint8_t marker = data[at + 1];
        if (marker == 0xFF) { ++at; continue; }                                                 // a fill byte
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) { at += 2; continue; }        // markers without a length
        if (marker == 0xD9 || marker == 0xDA) return 0;                                         // the picture begins: no table came before it
        const size_t length = (static_cast<size_t>(data[at + 2]) << 8) | data[at + 3];
        if (length < 2 || length > size - at - 2) return 0;
        if (marker == 0xDB) {
            size_t p = at + 4;
            const size_t end = at + 2 + length;
            while (p < end) {
                const int precision = data[p] >> 4;             // 0: one byte for a value, 1: two
                const int tableId = data[p] & 0x0F;             // 0 is the brightness table
                ++p;
                const size_t valueBytes = precision ? 128 : 64;
                if (precision > 1 || valueBytes > end - p) return 0;
                if (tableId == 0) {
                    uint32_t sum = 0;
                    for (size_t k = 0; k < 64; ++k) {
                        sum += precision ? ((static_cast<uint32_t>(data[p + 2 * k]) << 8) | data[p + 2 * k + 1]) : data[p + k];
                    }
                    const double scale = 100.0 * static_cast<double>(sum) / 3688.0;     // the standard table's 64 values add up to 3688
                    const double quality = (scale <= 100.0) ? (200.0 - scale) / 2.0 : 5000.0 / scale;
                    return static_cast<int>(std::clamp(std::lround(quality), 1L, 100L));
                }
                p += valueBytes;
            }
        }
        at += 2 + length;
    }
    return 0;
}

// A decoded picture as 4-byte BGRA pixels, rows from the top
static bool CopySourceToBgra(IWICImagingFactory* wic, IWICBitmapSource* source, uint32_t& outW, uint32_t& outH, std::vector<uint8_t>& outPixels) {
    UINT w = 0, h = 0;
    if (!wic || !source || FAILED(source->GetSize(&w, &h)) || w == 0 || h == 0) return false;
    const uint64_t byteCount = static_cast<uint64_t>(w) * h * 4;
    if (byteCount > 0x7FFFFFF0ull) return false;        // CopyPixels takes the buffer size as 32 bits
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(&conv)) || !conv ||
        FAILED(conv->Initialize(source, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        return false;
    }
    outPixels.resize(static_cast<size_t>(byteCount));
    if (FAILED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(byteCount), outPixels.data()))) {
        outPixels.clear();
        return false;
    }
    outW = w;
    outH = h;
    return true;
}

static bool ReadStreamBytes(IStream* stream, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    STATSTG stat = {};
    HGLOBAL hMem = nullptr;
    if (!stream || FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart == 0 ||
        FAILED(GetHGlobalFromStream(stream, &hMem)) || !hMem) {
        return false;
    }
    void* ptr = GlobalLock(hMem);
    if (!ptr) return false;
    const size_t byteCount = static_cast<size_t>(stat.cbSize.QuadPart);
    outBytes.assign(static_cast<const uint8_t*>(ptr), static_cast<const uint8_t*>(ptr) + byteCount);
    GlobalUnlock(hMem);
    return !outBytes.empty();
}

// Whether a picture can be written in this format. Windows' CreateEncoder says no itself when it cannot. The Linux
// build's encoders write JPEG, PNG and BMP only, and its CreateEncoder accepts any format - writing PNG bytes for
// all others, so a GIF or a TIFF turned in place used to become a PNG under its old name.
static bool PlatformEncodes(const GUID& container) {
#ifdef _WIN32
    (void)container;
    return true;
#else
    return container == GUID_ContainerFormatJpeg || container == GUID_ContainerFormatPng || container == GUID_ContainerFormatBmp;
#endif
}

// A picture written in the format it came in and with its own pixel format, as the encoder takes it: a palette
// PNG or a GIF stays a palette picture, a 16-bit PNG keeps its depth
static bool EncodeSameFormat(IWICImagingFactory* wic, IWICBitmapSource* source, const GUID& container, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    if (!PlatformEncodes(container)) return false;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (!wic || !source || FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) || !stream ||
        FAILED(wic->CreateEncoder(container, nullptr, &encoder)) || !encoder ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || !frame || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->WriteSource(source, nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        return false;
    }
    return ReadStreamBytes(stream.Get(), outBytes);
}

#ifdef _WIN32
// A photo's metadata (EXIF, XMP, IPTC), colour profile and resolution go from the decoded frame into the frame
// being written, and what the turn or the crop made untrue is corrected: the orientation becomes "as stored",
// the small preview picture embedded in the EXIF data is dropped (it would keep showing the old picture, and
// Windows shows it in thumbnails), and the noted pixel size follows. Best effort throughout: a file that cannot
// take some of it is saved without it.
static void CarryPhotoMetadata(IWICImagingFactory* wic, IWICBitmapFrameDecode* from, IWICBitmapFrameEncode* to, UINT newW, UINT newH) {
    if (!wic || !from || !to) return;

    double dpiX = 0.0, dpiY = 0.0;
    if (SUCCEEDED(from->GetResolution(&dpiX, &dpiY)) && dpiX > 0.0 && dpiY > 0.0) to->SetResolution(dpiX, dpiY);

    UINT profileCount = 0;
    if (SUCCEEDED(from->GetColorContexts(0, nullptr, &profileCount)) && profileCount > 0 && profileCount <= 8) {
        std::vector<ComPtr<IWICColorContext>> profiles(profileCount);
        std::vector<IWICColorContext*> raw(profileCount, nullptr);
        bool created = true;
        for (UINT i = 0; i < profileCount && created; ++i) {
            created = SUCCEEDED(wic->CreateColorContext(&profiles[i])) && profiles[i];
            raw[i] = profiles[i].Get();
        }
        UINT got = 0;
        if (created && SUCCEEDED(from->GetColorContexts(profileCount, raw.data(), &got)) && got > 0) to->SetColorContexts(got, raw.data());
    }

    ComPtr<IWICMetadataBlockReader> reader;
    ComPtr<IWICMetadataBlockWriter> writer;
    if (FAILED(from->QueryInterface(IID_PPV_ARGS(&reader))) || !reader ||
        FAILED(to->QueryInterface(IID_PPV_ARGS(&writer))) || !writer ||
        FAILED(writer->InitializeFromBlockReader(reader.Get()))) {
        return;
    }
    ComPtr<IWICMetadataQueryReader> have;
    ComPtr<IWICMetadataQueryWriter> query;
    if (FAILED(from->GetMetadataQueryReader(&have)) || !have || FAILED(to->GetMetadataQueryWriter(&query)) || !query) return;

    // A tag is corrected only where the file has it, and with the type it has there
    auto correct = [&](const wchar_t* name, UINT newValue) {
        PROPVARIANT old;
        PropVariantInit(&old);
        if (SUCCEEDED(have->GetMetadataByName(name, &old)) && (old.vt == VT_UI2 || old.vt == VT_UI4)) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (old.vt == VT_UI2 && newValue <= 0xFFFFu) {
                value.vt = VT_UI2;
                value.uiVal = static_cast<USHORT>(newValue);
            } else {
                value.vt = VT_UI4;
                value.ulVal = newValue;
            }
            query->SetMetadataByName(name, &value);
        }
        PropVariantClear(&old);
    };
    correct(L"/app1/ifd/{ushort=274}", static_cast<UINT>(ImageTurn::kNone));
    correct(L"/app1/ifd/exif/{ushort=40962}", newW);        // PixelXDimension
    correct(L"/app1/ifd/exif/{ushort=40963}", newH);        // PixelYDimension
    query->RemoveMetadataByName(L"/app1/thumb");
}

// Turns a JPEG without decoding it to pixels: Windows' codec moves the compressed blocks, so nothing is lost and
// the file keeps its size. That works only when both sides are multiples of 16 and for one quarter turn or one
// flip at a time - measured on Windows 10: any other size, and a turn combined with a flip, is quietly decoded
// and compressed again at the codec's own quality. Those cases return false here and take the ordinary way.
static bool TurnJpegLossless(IWICImagingFactory* wic, const std::vector<uint8_t>& srcBytes, DWORD srcSize, int turn,
                             std::vector<uint8_t>& outBytes, UINT& outW, UINT& outH) {
    int option = WICBitmapTransformRotate0;
    switch (turn) {
    case ImageTurn::kNone:           option = WICBitmapTransformRotate0; break;
    case ImageTurn::kFlipHorizontal: option = WICBitmapTransformFlipHorizontal; break;
    case ImageTurn::kRotate180:      option = WICBitmapTransformRotate180; break;
    case ImageTurn::kFlipVertical:   option = WICBitmapTransformFlipVertical; break;
    case ImageTurn::kRotateRight:    option = WICBitmapTransformRotate90; break;
    case ImageTurn::kRotateLeft:     option = WICBitmapTransformRotate270; break;
    default: return false;
    }

    ComPtr<IWICStream> inStream;
    ComPtr<IWICBitmapDecoder> decoder;
    // WICDecodeMetadataCacheOnDemand keeps the decoder on the stream, which is what lets the encoder take the
    // compressed data instead of pixels
    if (FAILED(wic->CreateStream(&inStream)) || !inStream ||
        FAILED(inStream->InitializeFromMemory(const_cast<BYTE*>(srcBytes.data()), srcSize)) ||
        FAILED(wic->CreateDecoderFromStream(inStream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder) {
        return false;
    }
    GUID container = {};
    ComPtr<IWICBitmapFrameDecode> frame;
    UINT w = 0, h = 0;
    if (FAILED(decoder->GetContainerFormat(&container)) || container != GUID_ContainerFormatJpeg ||
        FAILED(decoder->GetFrame(0, &frame)) || !frame || FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) {
        return false;
    }
    // (A picture that only has its orientation note corrected - the stored turn and the user's cancel out - is
    // copied as it is, whatever its size.)
    if (option != WICBitmapTransformRotate0 && ((w % 16) != 0 || (h % 16) != 0)) return false;

    ComPtr<IStream> outStream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frameEncode;
    ComPtr<IPropertyBag2> propBag;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &outStream)) || !outStream ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder)) || !encoder ||
        FAILED(encoder->Initialize(outStream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frameEncode, &propBag)) || !frameEncode) {
        return false;
    }
    if (option != WICBitmapTransformRotate0) {
        if (!propBag) return false;
        PROPBAG2 name = {};
        name.pstrName = const_cast<LPOLESTR>(L"BitmapTransform");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_UI1;
        value.bVal = static_cast<BYTE>(option);
        if (FAILED(propBag->Write(1, &name, &value))) return false;
    }
    if (FAILED(frameEncode->Initialize(propBag.Get()))) return false;

    outW = ImageTurn::SwapsSides(turn) ? h : w;
    outH = ImageTurn::SwapsSides(turn) ? w : h;
    CarryPhotoMetadata(wic, frame.Get(), frameEncode.Get(), outW, outH);     // before the picture, so that it has its room

    // The decoder's own frame and no rectangle: this is the call that copies the compressed data
    if (FAILED(frameEncode->WriteSource(frame.Get(), nullptr)) || FAILED(frameEncode->Commit()) || FAILED(encoder->Commit())) {
        return false;
    }
    return ReadStreamBytes(outStream.Get(), outBytes);
}
#else
// Linux: a JPEG turned without being compressed again. A turn that only undoes the stored orientation needs no
// tool: the orientation note is corrected and the picture stays as it is. Any other turn is left to jpegtran
// (libjpeg's tool: "libjpeg-turbo-progs" or "libjpeg-progs" in most distributions) when it is installed; -perfect
// makes it refuse a picture whose edge blocks it could not move, and the caller then compresses the picture again.
// The EXIF block comes along (-copy all) and is corrected here as on Windows. storedW x storedH: the picture as it
// is stored, before any turn.
static bool TurnJpegLosslessLinux(const std::vector<uint8_t>& srcBytes, size_t srcSize, int turn, UINT storedW, UINT storedH,
                                  std::vector<uint8_t>& outBytes, UINT& outW, UINT& outH) {
    outW = ImageTurn::SwapsSides(turn) ? storedH : storedW;
    outH = ImageTurn::SwapsSides(turn) ? storedW : storedH;
    std::vector<uint8_t> turned;
    if (turn == ImageTurn::kNone) {
        turned.assign(srcBytes.begin(), srcBytes.begin() + static_cast<std::ptrdiff_t>(srcSize));
    } else {
        std::vector<std::wstring> how;
        switch (turn) {
        case ImageTurn::kFlipHorizontal: how = { L"-flip", L"horizontal" }; break;
        case ImageTurn::kRotate180:      how = { L"-rotate", L"180" }; break;
        case ImageTurn::kFlipVertical:   how = { L"-flip", L"vertical" }; break;
        case ImageTurn::kTranspose:      how = { L"-transpose" }; break;
        case ImageTurn::kRotateRight:    how = { L"-rotate", L"90" }; break;
        case ImageTurn::kTransverse:     how = { L"-transverse" }; break;
        case ImageTurn::kRotateLeft:     how = { L"-rotate", L"270" }; break;
        default: return false;
        }
        const std::wstring tool = ToolProcess::FindExecutable(L"jpegtran", { L"/usr/bin/jpegtran", L"/bin/jpegtran", L"/usr/local/bin/jpegtran" });
        if (tool.empty()) return false;
        // The tool works on files: a copy of the picture in the temporary folder, so that the path given to it is
        // a plain one this process chose
        static std::atomic<unsigned> s_counter{ 0 };
        wchar_t tmpDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmpDir);
        const std::wstring stem = std::wstring(tmpDir) + L"explorer2_turn_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(++s_counter);
        const std::wstring inPath = stem + L"_in.jpg";
        const std::wstring outPath = stem + L"_out.jpg";
        if (!PathUtils::WriteFileBytes(inPath, srcBytes.data(), srcSize)) return false;
        std::vector<std::wstring> args = { L"-copy", L"all", L"-perfect" };
        args.insert(args.end(), how.begin(), how.end());
        args.insert(args.end(), { L"-outfile", outPath, inPath });
        ToolProcess::Options opt;
        opt.timeoutMs = 60000;
        opt.maxBytes = 1u << 20;
        static const std::atomic<bool> s_never{ false };
        const ToolProcess::Result res = ToolProcess::Run(tool, args, opt, s_never);
        const bool ok = res.ok() && PathUtils::ReadFileBytes(outPath, 1024ull << 20, turned) && turned.size() > 4;
        DeleteFileW(inPath.c_str());
        DeleteFileW(outPath.c_str());
        if (!ok) return false;
    }
    std::vector<uint8_t> exif = JpegSegments::FindExif(turned.data(), turned.size());
    if (!exif.empty() && JpegSegments::FixExifForSavedPicture(exif, outW, outH) &&
        !JpegSegments::ReplacePhotoSegments(turned, exif, JpegSegments::FindIccSegments(turned.data(), turned.size()))) {
        return false;
    }
    outBytes = std::move(turned);
    return true;
}
#endif // _WIN32

bool ShellMenuEmulator::ProbeImageDimensions(const std::wstring& imagePath, uint32_t& outW, uint32_t& outH, uint64_t* outFileBytes) {
    outW = 0;
    outH = 0;
    if (outFileBytes) *outFileBytes = 0;

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(imagePath.c_str(), GetFileExInfoStandard, &fad)) {
        if (outFileBytes) {
            *outFileBytes = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        }
    }

    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) || !wic) {
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(imagePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder) {
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) return false;

    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) return false;
    // The size of the picture as it is shown: a photo stored turned by a quarter has its sides the other way round
    if (ImageTurn::SwapsSides(ReadExifOrientation(frame.Get()))) std::swap(w, h);
    outW = w;
    outH = h;
    return true;
}

static bool EncodeWicSourceToMemory(
    IWICImagingFactory* wic,
    IWICBitmapSource* source,
    GUID containerFormat,
    float jpegQuality,
    std::vector<uint8_t>& outEncoded,
    GUID* outActualContainer = nullptr,
    IWICBitmapFrameDecode* metadataFrom = nullptr) {    // a JPEG written from a JPEG: the frame whose EXIF data goes along
    outEncoded.clear();
    if (!wic || !source) return false;

    ComPtr<IStream> memStream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &memStream)) || !memStream) {
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    if (!PlatformEncodes(containerFormat) || FAILED(wic->CreateEncoder(containerFormat, nullptr, &encoder)) || !encoder) {
        // Fallback for formats without a built-in WIC encoder (e.g. WebP / RAW): encode as JPEG
        encoder.Reset();
        containerFormat = GUID_ContainerFormatJpeg;
        if (FAILED(wic->CreateEncoder(containerFormat, nullptr, &encoder)) || !encoder) {
            return false;
        }
    }
    if (outActualContainer) *outActualContainer = containerFormat;

    if (FAILED(encoder->Initialize(memStream.Get(), WICBitmapEncoderNoCache))) {
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> frameEncode;
    ComPtr<IPropertyBag2> propBag;
    if (FAILED(encoder->CreateNewFrame(&frameEncode, &propBag)) || !frameEncode) {
        return false;
    }

    if (containerFormat == GUID_ContainerFormatJpeg && propBag) {
        PROPBAG2 option = {};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT varValue;
        VariantInit(&varValue);
        varValue.vt = VT_R4;
        varValue.fltVal = std::clamp(jpegQuality, 0.05f, 1.0f);
        propBag->Write(1, &option, &varValue);
    }

    if (FAILED(frameEncode->Initialize(propBag.Get()))) {
        return false;
    }

#ifdef _WIN32
    if (metadataFrom && containerFormat == GUID_ContainerFormatJpeg) {
        UINT newW = 0, newH = 0;
        source->GetSize(&newW, &newH);
        CarryPhotoMetadata(wic, metadataFrom, frameEncode.Get(), newW, newH);    // before the pixels, so that it has its room
    }
#else
    (void)metadataFrom;     // the Linux build's encoders write the picture only
#endif

    // Ensure pixel format compatibility with the target container (e.g. 32bpp RGBA PNG -> 24bpp BGR JPEG)
    ComPtr<IWICBitmapSource> writeSource = source;
    WICPixelFormatGUID srcPf = {};
    if (SUCCEEDED(source->GetPixelFormat(&srcPf))) {
        if (containerFormat == GUID_ContainerFormatJpeg || containerFormat == GUID_ContainerFormatBmp) {
            if (srcPf != GUID_WICPixelFormat24bppBGR && srcPf != GUID_WICPixelFormat8bppGray) {
                ComPtr<IWICFormatConverter> conv;
                if (SUCCEEDED(wic->CreateFormatConverter(&conv)) && conv &&
                    SUCCEEDED(conv->Initialize(source, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                    writeSource = conv;
                }
            }
        } else if (containerFormat == GUID_ContainerFormatPng) {
            if (srcPf != GUID_WICPixelFormat32bppBGRA && srcPf != GUID_WICPixelFormat24bppBGR) {
                ComPtr<IWICFormatConverter> conv;
                if (SUCCEEDED(wic->CreateFormatConverter(&conv)) && conv &&
                    SUCCEEDED(conv->Initialize(source, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                    writeSource = conv;
                }
            }
        }
    }

    if (FAILED(frameEncode->WriteSource(writeSource.Get(), nullptr)) ||
        FAILED(frameEncode->Commit()) ||
        FAILED(encoder->Commit())) {
        return false;
    }

    STATSTG stat = {};
    if (FAILED(memStream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart == 0) {
        return false;
    }

    HGLOBAL hMem = nullptr;
    if (FAILED(GetHGlobalFromStream(memStream.Get(), &hMem)) || !hMem) {
        return false;
    }

    void* ptr = GlobalLock(hMem);
    if (!ptr) return false;
    size_t byteCount = static_cast<size_t>(stat.cbSize.QuadPart);
    outEncoded.assign(static_cast<const uint8_t*>(ptr), static_cast<const uint8_t*>(ptr) + byteCount);
    GlobalUnlock(hMem);
    return !outEncoded.empty();
}

static ComPtr<IWICBitmap> ScaleBitmapCached(
    IWICImagingFactory* wic,
    IWICBitmapSource* source,
    uint32_t newW,
    uint32_t newH) {
    newW = std::max(1u, newW);
    newH = std::max(1u, newH);

    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(wic->CreateBitmapScaler(&scaler)) || !scaler) return nullptr;
    if (FAILED(scaler->Initialize(source, newW, newH, WICBitmapInterpolationModeHighQualityCubic))) {
        if (FAILED(scaler->Initialize(source, newW, newH, WICBitmapInterpolationModeFant))) {
            return nullptr;
        }
    }

    ComPtr<IWICBitmap> cached;
    if (FAILED(wic->CreateBitmapFromSource(scaler.Get(), WICBitmapCacheOnLoad, &cached))) {
        return nullptr;
    }
    return cached;
}

static bool WriteBytesToFileAtomic(const std::wstring& destPath, const std::vector<uint8_t>& data) {
    if (data.empty()) return false;
    std::wstring tmpPath = destPath + L".img_tmp";
    DeleteFileW(tmpPath.c_str());

    HANDLE hOut = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(hOut, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(hOut);
    if (!ok || written != static_cast<DWORD>(data.size())) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }

    if (!MoveFileExW(tmpPath.c_str(), destPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    return true;
}

bool ShellMenuEmulator::TransformImageFile(const std::wstring& imagePath, const ImageTransformParams& params, ImageTransformResult& outRes) {
    outRes = {};
    outRes.srcPath = imagePath;

    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) || !wic) {
        outRes.errorMsg = L"WIC factory init failed";
        return false;
    }

    HANDLE hIn = CreateFileW(imagePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hIn == INVALID_HANDLE_VALUE) {
        outRes.errorMsg = L"Cannot open source image";
        return false;
    }

    LARGE_INTEGER sz = {};
    if (!GetFileSizeEx(hIn, &sz) || sz.QuadPart <= 0) {
        CloseHandle(hIn);
        outRes.errorMsg = L"Empty or invalid image file";
        return false;
    }
    std::vector<uint8_t> srcBytes(static_cast<size_t>(sz.QuadPart));
    DWORD readBytes = 0;
    BOOL readOk = ReadFile(hIn, srcBytes.data(), static_cast<DWORD>(srcBytes.size()), &readBytes, nullptr);
    CloseHandle(hIn);
    if (!readOk || readBytes == 0) {
        outRes.errorMsg = L"Failed reading source image";
        return false;
    }
    outRes.origBytes = readBytes;

    ComPtr<IWICBitmap> cachedBitmap;
    GUID containerFormat = GUID_ContainerFormatPng;
    UINT origW = 0, origH = 0;
    std::wstring extCheck = PathUtils::GetExtension(imagePath);

    ComPtr<IWICStream> inStream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> srcFrame;     // stays empty for the formats our own readers decode
    if (!ImageFormatLoader::IsExtendedImageExtension(extCheck) &&
        SUCCEEDED(wic->CreateStream(&inStream)) &&
        SUCCEEDED(inStream->InitializeFromMemory(srcBytes.data(), readBytes)) &&
        SUCCEEDED(wic->CreateDecoderFromStream(inStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        decoder)
    {
        decoder->GetContainerFormat(&containerFormat);
        if (FAILED(decoder->GetFrame(0, &srcFrame)) || !srcFrame ||
            FAILED(srcFrame->GetSize(&origW, &origH)) || origW == 0 || origH == 0) {
            outRes.errorMsg = L"Cannot decode image frame";
            return false;
        }
        if (FAILED(wic->CreateBitmapFromSource(srcFrame.Get(), WICBitmapCacheOnLoad, &cachedBitmap)) || !cachedBitmap) {
            outRes.errorMsg = L"Out of memory caching bitmap";
            return false;
        }
    } else {
        if (params.sameFormatOnly) {
            outRes.errorMsg = L"This format cannot be written";
            return false;
        }
        ExtendedImageInfo extInfo;
        if (!ImageFormatLoader::DecodeImageMemory(srcBytes.data(), readBytes, extCheck, 0, extInfo) || !extInfo.valid) {
            outRes.errorMsg = L"Unsupported image format";
            return false;
        }
        origW = extInfo.width;
        origH = extInfo.height;
        containerFormat = GUID_ContainerFormatPng;
        if (FAILED(wic->CreateBitmapFromMemory(
                origW, origH,
                GUID_WICPixelFormat32bppPBGRA,
                origW * 4,
                static_cast<UINT>(extInfo.bgraPixels.size()),
                extInfo.bgraPixels.data(),
                &cachedBitmap)) || !cachedBitmap) {
            outRes.errorMsg = L"Out of memory caching extended bitmap";
            return false;
        }
    }

    // The picture as the viewer shows it. A photo that is stored turned (EXIF orientation; phones do that) is made
    // upright first, so that "rotate right" and a crop rectangle mean what the user saw; the saved file then
    // needs no turning. One pass over the pixels does the stored turn and the user's together.
    const int storedTurn = ReadExifOrientation(srcFrame.Get());
    int userTurn = ImageTurn::kNone;
    switch (params.kind) {
    case ImageTransformKind::RotateCW:  userTurn = ImageTurn::kRotateRight; break;
    case ImageTransformKind::RotateCCW: userTurn = ImageTurn::kRotateLeft; break;
    case ImageTransformKind::Rotate180: userTurn = ImageTurn::kRotate180; break;
    case ImageTransformKind::FlipH:     userTurn = ImageTurn::kFlipHorizontal; break;
    case ImageTransformKind::FlipV:     userTurn = ImageTurn::kFlipVertical; break;
    default: break;
    }
    const bool isTurnKind = userTurn != ImageTurn::kNone;
    const bool isCrop = params.kind == ImageTransformKind::Crop;
    const int totalTurn = ImageTurn::Compose(storedTurn, userTurn);
    const UINT storedW = origW, storedH = origH;
    (void)storedW;
    (void)storedH;
    if (ImageTurn::SwapsSides(storedTurn)) std::swap(origW, origH);
    outRes.origW = origW;
    outRes.origH = origH;

    // Parse source path parts: parentDir, stem, srcExt
    std::wstring parentDir = PathUtils::EnsureTrailingSlash(PathUtils::GetParentPath(imagePath));
    std::wstring fileName  = PathUtils::GetFileName(imagePath);
    std::wstring stem      = fileName;
    std::wstring srcExt    = L".jpg";
    size_t dotPos = fileName.find_last_of(L'.');
    if (dotPos != std::wstring::npos && dotPos > 0) {
        stem   = fileName.substr(0, dotPos);
        srcExt = fileName.substr(dotPos);
    }
    std::wstring srcExtLower = PathUtils::ToLower(srcExt);

    GUID targetContainer = containerFormat;
    std::wstring outExt  = ImageFormatLoader::IsExtendedImageExtension(srcExtLower) ? L".png" : srcExt;
    if (!params.forceExt.empty()) {
        std::wstring fe = PathUtils::ToLower(params.forceExt);
        outExt = fe;
        if (fe == L".jpg" || fe == L".jpeg") targetContainer = GUID_ContainerFormatJpeg;
        else if (fe == L".png")              targetContainer = GUID_ContainerFormatPng;
        else if (fe == L".bmp")              targetContainer = GUID_ContainerFormatBmp;
    }

    std::vector<uint8_t> finalBytes;
    uint32_t finalW = origW;
    uint32_t finalH = origH;
    float finalQuality = params.jpegQuality;
    GUID actualContainer = targetContainer;

    // A JPEG that is only turned keeps every pixel where Windows can move its compressed blocks around
    bool turnedLossless = false;
#ifdef _WIN32
    if (isTurnKind && srcFrame && containerFormat == GUID_ContainerFormatJpeg && targetContainer == GUID_ContainerFormatJpeg) {
        UINT turnedW = 0, turnedH = 0;
        if (TurnJpegLossless(wic.Get(), srcBytes, readBytes, totalTurn, finalBytes, turnedW, turnedH)) {
            finalW = turnedW;
            finalH = turnedH;
            finalQuality = 1.0f;
            turnedLossless = true;
            outRes.lossless = true;
        } else {
            finalBytes.clear();
        }
    }
#else
    if (isTurnKind && srcFrame && containerFormat == GUID_ContainerFormatJpeg && targetContainer == GUID_ContainerFormatJpeg) {
        UINT turnedW = 0, turnedH = 0;
        if (TurnJpegLosslessLinux(srcBytes, readBytes, totalTurn, storedW, storedH, finalBytes, turnedW, turnedH)) {
            finalW = turnedW;
            finalH = turnedH;
            finalQuality = 1.0f;
            turnedLossless = true;
            outRes.lossless = true;
        } else {
            finalBytes.clear();
        }
    }
#endif

    // A picture that is no JPEG and is not stored turned (PNG, GIF, BMP, a TIFF without an orientation) is turned
    // the way it always was: Windows' flip-rotator over the decoded frame, written back in the same format with
    // the same pixel format
    bool turnedSameFormat = false;
    if (!turnedLossless && isTurnKind && srcFrame && storedTurn == ImageTurn::kNone &&
        containerFormat != GUID_ContainerFormatJpeg && targetContainer == containerFormat) {
        WICBitmapTransformOptions option = WICBitmapTransformRotate0;
        switch (userTurn) {
        case ImageTurn::kRotateRight:    option = WICBitmapTransformRotate90; break;
        case ImageTurn::kRotateLeft:     option = WICBitmapTransformRotate270; break;
        case ImageTurn::kRotate180:      option = WICBitmapTransformRotate180; break;
        case ImageTurn::kFlipHorizontal: option = WICBitmapTransformFlipHorizontal; break;
        case ImageTurn::kFlipVertical:   option = WICBitmapTransformFlipVertical; break;
        default: break;
        }
        ComPtr<IWICBitmapFlipRotator> rotator;
        UINT turnedW = 0, turnedH = 0;
        if (SUCCEEDED(wic->CreateBitmapFlipRotator(&rotator)) && rotator &&
            SUCCEEDED(rotator->Initialize(cachedBitmap.Get(), option)) && SUCCEEDED(rotator->GetSize(&turnedW, &turnedH)) &&
            EncodeSameFormat(wic.Get(), rotator.Get(), containerFormat, finalBytes)) {
            finalW = turnedW;
            finalH = turnedH;
            finalQuality = 1.0f;
            turnedSameFormat = true;
        } else {
            finalBytes.clear();     // the way through the pixels below is tried instead
        }
    }

    // Otherwise the turns and the crop are done on the pixels; what follows (scaling, encoding) gets the result
    if (!turnedLossless && !turnedSameFormat && (storedTurn != ImageTurn::kNone || isTurnKind || isCrop)) {
        uint32_t w = 0, h = 0;
        std::vector<uint8_t> pixels;
        if (!CopySourceToBgra(wic.Get(), cachedBitmap.Get(), w, h, pixels)) {
            outRes.errorMsg = L"Out of memory turning the picture";
            return false;
        }
        ImageTurn::TurnPixels(totalTurn, w, h, pixels);
        if (isCrop) {
            const uint32_t cropX = std::min(params.cropX, w - 1u);
            const uint32_t cropY = std::min(params.cropY, h - 1u);
            const uint32_t cropW = std::min(params.cropW, w - cropX);
            const uint32_t cropH = std::min(params.cropH, h - cropY);
            if (!ImageTurn::CropPixels(w, h, pixels, cropX, cropY, cropW, cropH)) {
                outRes.errorMsg = L"The crop rectangle is empty";
                return false;
            }
        }
        ComPtr<IWICBitmap> turned;
        if (FAILED(wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGRA, w * 4, static_cast<UINT>(pixels.size()),
                                               pixels.data(), &turned)) || !turned) {
            outRes.errorMsg = L"Out of memory turning the picture";
            return false;
        }
        cachedBitmap = turned;
        finalW = w;
        finalH = h;
    }

    if (turnedLossless || turnedSameFormat) {
        // nothing more to do to the picture
    } else if (params.kind == ImageTransformKind::TargetFileSize) {
        uint64_t targetB = std::max<uint64_t>(2048, params.targetBytes);
        targetContainer = GUID_ContainerFormatJpeg;
        actualContainer = GUID_ContainerFormatJpeg;
        if (srcExtLower != L".jpg" && srcExtLower != L".jpeg") {
            outExt = L".jpg";
        }

        // If original file is already a JPEG within targetB, keep exact original bytes!
        if (outRes.origBytes <= targetB && (srcExtLower == L".jpg" || srcExtLower == L".jpeg")) {
            finalBytes = srcBytes;
            finalW = origW;
            finalH = origH;
            finalQuality = 1.0f;
        } else {
            // Iterative quality + dimension solver in RAM
            double curScale = 1.0;
            uint32_t longest = std::max(origW, origH);
            if (longest > 4096 && targetB <= 512 * 1024) {
                curScale = 4096.0 / static_cast<double>(longest);
            }

            bool solved = false;
            std::vector<uint8_t> bestFitBytes;
            uint32_t bestFitW = origW, bestFitH = origH;
            float bestFitQ = 0.85f;

            std::vector<uint8_t> smallestTrialBytes;
            uint32_t smallestTrialW = origW, smallestTrialH = origH;
            float smallestTrialQ = 0.30f;

            for (int step = 0; step < 8 && !solved; ++step) {
                uint32_t sw = std::max(1u, static_cast<uint32_t>(std::lround(origW * curScale)));
                uint32_t sh = std::max(1u, static_cast<uint32_t>(std::lround(origH * curScale)));

                ComPtr<IWICBitmapSource> stepBmp = cachedBitmap;
                if (sw != origW || sh != origH) {
                    ComPtr<IWICBitmap> scaled = ScaleBitmapCached(wic.Get(), cachedBitmap.Get(), sw, sh);
                    if (scaled) stepBmp = scaled;
                }

                // 1) Try high quality (0.90 on first pass, 0.82 on scaled passes)
                float hiQ = (step == 0) ? 0.92f : 0.84f;
                std::vector<uint8_t> trialHi;
                if (EncodeWicSourceToMemory(wic.Get(), stepBmp.Get(), GUID_ContainerFormatJpeg, hiQ, trialHi)) {
                    if (smallestTrialBytes.empty() || trialHi.size() < smallestTrialBytes.size()) {
                        smallestTrialBytes = trialHi;
                        smallestTrialW = sw;
                        smallestTrialH = sh;
                        smallestTrialQ = hiQ;
                    }
                    if (trialHi.size() <= targetB) {
                        // Even high quality at this scale fits! Binary search upward toward 0.95 if step > 0
                        bestFitBytes = std::move(trialHi);
                        bestFitW = sw;
                        bestFitH = sh;
                        bestFitQ = hiQ;
                        if (step > 0 && bestFitBytes.size() < targetB * 85 / 100) {
                            float lo = hiQ, hi = 0.94f;
                            for (int b = 0; b < 3; ++b) {
                                float mid = (lo + hi) * 0.5f;
                                std::vector<uint8_t> tr;
                                if (EncodeWicSourceToMemory(wic.Get(), stepBmp.Get(), GUID_ContainerFormatJpeg, mid, tr) && tr.size() <= targetB) {
                                    bestFitBytes = std::move(tr);
                                    bestFitQ = mid;
                                    lo = mid;
                                } else {
                                    hi = mid;
                                }
                            }
                        }
                        solved = true;
                        break;
                    }
                }

                // 2) Try visual floor quality (0.50f) at current resolution
                float floorQ = 0.50f;
                std::vector<uint8_t> trialFloor;
                if (EncodeWicSourceToMemory(wic.Get(), stepBmp.Get(), GUID_ContainerFormatJpeg, floorQ, trialFloor)) {
                    if (smallestTrialBytes.empty() || trialFloor.size() < smallestTrialBytes.size()) {
                        smallestTrialBytes = trialFloor;
                        smallestTrialW = sw;
                        smallestTrialH = sh;
                        smallestTrialQ = floorQ;
                    }
                    if (trialFloor.size() <= targetB) {
                        // Optimal quality at (sw x sh) is between [floorQ, hiQ]! Binary search 5 iterations:
                        bestFitBytes = std::move(trialFloor);
                        bestFitW = sw;
                        bestFitH = sh;
                        bestFitQ = floorQ;

                        float lo = floorQ, hi = hiQ;
                        for (int b = 0; b < 5; ++b) {
                            float mid = (lo + hi) * 0.5f;
                            std::vector<uint8_t> tr;
                            if (EncodeWicSourceToMemory(wic.Get(), stepBmp.Get(), GUID_ContainerFormatJpeg, mid, tr)) {
                                if (tr.size() <= targetB) {
                                    bestFitBytes = std::move(tr);
                                    bestFitQ = mid;
                                    lo = mid;
                                    if (bestFitBytes.size() >= targetB * 94 / 100) break;
                                } else {
                                    hi = mid;
                                }
                            } else {
                                break;
                            }
                        }
                        solved = true;
                        break;
                    }

                    // Still larger than targetB even at q = 0.50: step down resolution proportionally
                    double ratio = static_cast<double>(targetB) / static_cast<double>(trialFloor.size());
                    double factor = std::clamp(std::sqrt(ratio) * 0.90, 0.32, 0.84);
                    curScale *= factor;
                    if (curScale < 0.05 || (sw <= 64 && sh <= 64)) {
                        // Reached minimum resolution floor: binary search quality down to 0.10f
                        float lo = 0.08f, hi = floorQ;
                        for (int b = 0; b < 5; ++b) {
                            float mid = (lo + hi) * 0.5f;
                            std::vector<uint8_t> tr;
                            if (EncodeWicSourceToMemory(wic.Get(), stepBmp.Get(), GUID_ContainerFormatJpeg, mid, tr)) {
                                if (smallestTrialBytes.empty() || tr.size() < smallestTrialBytes.size()) {
                                    smallestTrialBytes = tr;
                                    smallestTrialW = sw;
                                    smallestTrialH = sh;
                                    smallestTrialQ = mid;
                                }
                                if (tr.size() <= targetB) {
                                    bestFitBytes = std::move(tr);
                                    bestFitW = sw;
                                    bestFitH = sh;
                                    bestFitQ = mid;
                                    lo = mid;
                                    solved = true;
                                } else {
                                    hi = mid;
                                }
                            }
                        }
                        break;
                    }
                } else {
                    break;
                }
            }

            if (!bestFitBytes.empty()) {
                finalBytes = std::move(bestFitBytes);
                finalW = bestFitW;
                finalH = bestFitH;
                finalQuality = bestFitQ;
            } else if (!smallestTrialBytes.empty()) {
                finalBytes = std::move(smallestTrialBytes);
                finalW = smallestTrialW;
                finalH = smallestTrialH;
                finalQuality = smallestTrialQ;
            } else {
                outRes.errorMsg = L"Failed encoding to target size";
                return false;
            }
        }
    } else {
        // Geometric or format transform
        ComPtr<IWICBitmapSource> transformed = cachedBitmap;

        if (isTurnKind || isCrop) {
            // done above, on the pixels: finalW and finalH are the picture's new sides
        } else if (params.kind == ImageTransformKind::ScalePercent) {
            double s = std::clamp(params.scalePct, 1, 800) / 100.0;
            finalW = std::max(1u, static_cast<uint32_t>(std::lround(origW * s)));
            finalH = std::max(1u, static_cast<uint32_t>(std::lround(origH * s)));
            if (finalW != origW || finalH != origH) {
                ComPtr<IWICBitmap> scaled = ScaleBitmapCached(wic.Get(), cachedBitmap.Get(), finalW, finalH);
                if (!scaled) {
                    outRes.errorMsg = L"Image scaling failed";
                    return false;
                }
                transformed = scaled;
            }
        } else if (params.kind == ImageTransformKind::FitMaxDim) {
            uint32_t maxD = std::max(1u, params.maxDim);
            uint32_t longest = std::max(origW, origH);
            if (longest > maxD) {
                double s = static_cast<double>(maxD) / static_cast<double>(longest);
                finalW = std::max(1u, static_cast<uint32_t>(std::lround(origW * s)));
                finalH = std::max(1u, static_cast<uint32_t>(std::lround(origH * s)));
                ComPtr<IWICBitmap> scaled = ScaleBitmapCached(wic.Get(), cachedBitmap.Get(), finalW, finalH);
                if (!scaled) {
                    outRes.errorMsg = L"Image downscaling failed";
                    return false;
                }
                transformed = scaled;
            }
        } else if (params.kind == ImageTransformKind::ExactWidthHeight) {
            finalW = std::max(1u, params.targetW);
            finalH = std::max(1u, params.targetH);
            if (finalW != origW || finalH != origH) {
                ComPtr<IWICBitmap> scaled = ScaleBitmapCached(wic.Get(), cachedBitmap.Get(), finalW, finalH);
                if (!scaled) {
                    outRes.errorMsg = L"Image resize failed";
                    return false;
                }
                transformed = scaled;
            }
        }

        // A turn or a crop is not meant to change anything else about a photo. A JPEG that has to be compressed
        // again is written at the quality it has (a little above it, so that the second compression costs less; 0.95
        // when its bytes do not tell), and it keeps its EXIF data, colour profile and resolution.
        const bool keepsPhotoData = (isTurnKind || isCrop) && srcFrame && containerFormat == GUID_ContainerFormatJpeg;
        float quality = params.jpegQuality;
        if (isTurnKind || isCrop) {
            const int had = (containerFormat == GUID_ContainerFormatJpeg) ? EstimateJpegQuality(srcBytes.data(), readBytes) : 0;
            quality = (had > 0) ? std::clamp(static_cast<float>(had + 2) / 100.0f, 0.80f, 0.97f) : 0.95f;
        }
        bool encoded = EncodeWicSourceToMemory(wic.Get(), transformed.Get(), targetContainer, quality, finalBytes, &actualContainer,
                                               keepsPhotoData ? srcFrame.Get() : nullptr);
        // A photo whose metadata the encoder will not take back is saved without it rather than not at all
        if (!encoded && keepsPhotoData) {
            encoded = EncodeWicSourceToMemory(wic.Get(), transformed.Get(), targetContainer, quality, finalBytes, &actualContainer);
        }
        if (!encoded) {
            outRes.errorMsg = L"Failed encoding output image";
            return false;
        }
#ifndef _WIN32
        // The Linux build's JPEG encoder writes the picture only: the photo's EXIF block (corrected for the upright,
        // possibly smaller picture) and its colour profile are put back from the source file
        if (keepsPhotoData && actualContainer == GUID_ContainerFormatJpeg) {
            JpegSegments::CarryPhotoSegments(srcBytes.data(), readBytes, finalBytes, finalW, finalH);
        }
#endif
        finalQuality = quality;
        if (params.sameFormatOnly && actualContainer != containerFormat) {
            outRes.errorMsg = L"This format cannot be written";
            return false;
        }
        if (actualContainer == GUID_ContainerFormatJpeg && targetContainer != GUID_ContainerFormatJpeg) {
            outExt = L".jpg";
        }
    }

    // Determine final output file path (overwrite in-place OR save as sibling copy)
    std::wstring destPath;
    if (params.overwriteOriginal) {
        destPath = parentDir + stem + outExt;
        if (!WriteBytesToFileAtomic(destPath, finalBytes)) {
            outRes.errorMsg = L"Cannot write output file";
            return false;
        }
        // The old file goes only when the picture got a new name (another format). Compared as path keys: the same
        // file can be named with either separator ("/tmp/a\b.jpg" and "/tmp/a/b.jpg" on Linux, "C:/a/b.jpg" and
        // "C:/a\b.jpg" on Windows), and deleting it as "the other name" deleted the picture just saved.
        if (PathUtils::NormalizePathKey(destPath) != PathUtils::NormalizePathKey(imagePath)) {
            DeleteFileW(imagePath.c_str());
        }
    } else {
        std::wstring sfx = params.suffix;
        if (sfx.empty()) {
            if (_wcsicmp(outExt.c_str(), srcExt.c_str()) == 0) sfx = L"_copy";
        }
        std::wstring baseCandidate = parentDir + stem + sfx + outExt;
        destPath = baseCandidate;
        if (GetFileAttributesW(destPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            for (int n = 2; n < 1000; ++n) {
                std::wstring tryPath = parentDir + stem + sfx + L"_" + std::to_wstring(n) + outExt;
                if (GetFileAttributesW(tryPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    destPath = tryPath;
                    break;
                }
            }
        }
        if (!WriteBytesToFileAtomic(destPath, finalBytes)) {
            outRes.errorMsg = L"Cannot write copy file";
            return false;
        }
    }

    outRes.ok          = true;
    outRes.outPath     = destPath;
    outRes.newW        = finalW;
    outRes.newH        = finalH;
    outRes.newBytes    = finalBytes.size();
    outRes.usedQuality = finalQuality;
    return true;
}

void ShellMenuEmulator::ShowNativeShellContextMenu(HWND hwnd, const std::vector<std::wstring>& paths, int screenX, int screenY) {
    if (paths.empty()) return;

    PIDLIST_ABSOLUTE pidlFirst = ILCreateFromPathW(paths[0].c_str());
    if (!pidlFirst) return;

    ComPtr<IShellFolder> psfParent;
    PCUITEMID_CHILD pidlChild = nullptr;
    if (SUCCEEDED(SHBindToParent(pidlFirst, IID_PPV_ARGS(&psfParent), &pidlChild)) && psfParent) {
        ComPtr<IContextMenu> pcm;
        if (SUCCEEDED(psfParent->GetUIObjectOf(hwnd, 1, &pidlChild, IID_IContextMenu, nullptr, reinterpret_cast<void**>(pcm.GetAddressOf()))) && pcm) {
            HMENU hMenu = CreatePopupMenu();
            if (SUCCEEDED(pcm->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXPLORE))) {
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screenX, screenY, 0, hwnd, nullptr);
                if (cmd > 0) {
                    CMINVOKECOMMANDINFOEX ici = { sizeof(ici) };
                    ici.fMask = CMIC_MASK_UNICODE;
                    ici.hwnd = hwnd;
                    ici.lpVerb = MAKEINTRESOURCEA(cmd - 1);
                    ici.lpVerbW = MAKEINTRESOURCEW(cmd - 1);
                    ici.nShow = SW_SHOWNORMAL;
                    pcm->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&ici));
                }
            }
            DestroyMenu(hMenu);
        }
    }
    ILFree(pidlFirst);
}

} // namespace Explorer2
