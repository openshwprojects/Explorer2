#include "ShellMenuEmulator.h"
#include "PathUtils.h"
#include <wrl/client.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <wincodec.h>
#include <thread>
#include <algorithm>

#pragma comment(lib, "advapi32.lib")

using Microsoft::WRL::ComPtr;

namespace Explorer2 {

static HBITMAP HIconToMenuBitmap16(HICON hIcon) {
    if (!hIcon) return nullptr;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 16;
    bmi.bmiHeader.biHeight = -16; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HBITMAP hDib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (hDib && bits) {
        memset(bits, 0, 16 * 16 * 4);
        HDC memDc = CreateCompatibleDC(screenDc);
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
    ReleaseDC(nullptr, screenDc);
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
ShellMenuEmulator::~ShellMenuEmulator() = default;

void ShellMenuEmulator::InitializeAsync() {
    std::thread([this]() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ScanStartupData();
        CoUninitialize();
    }).detach();
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

bool ShellMenuEmulator::HasWinRAR(std::wstring* outWinRarExe) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (outWinRarExe) *outWinRarExe = m_winRarPath;
    return !m_winRarPath.empty();
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
        exeToRun = fmExe.empty() ? guiExe : fmExe;
        args = L"\"" + firstPath + L"\"";
        break;
    case SZ_EXTRACT_FILES:
        args = L"x \"" + firstPath + L"\" -o\"" + currentDir + L"\" -ad";
        break;
    case SZ_EXTRACT_HERE:
        args = L"x \"" + firstPath + L"\" -o\"" + currentDir + L"\"";
        break;
    case SZ_EXTRACT_SUBFOLDER:
        args = L"x \"" + firstPath + L"\" -o\"" + PathUtils::EnsureTrailingSlash(currentDir) + stem + L"\"";
        break;
    case SZ_ADD_DIALOG:
        args = L"a -ad \"" + PathUtils::EnsureTrailingSlash(currentDir) + stem + L"\" " + quotedFiles;
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
    if (parentDir.empty() || parentDir == L"THIS_PC") return false;
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

bool ShellMenuEmulator::RotateImageFile(const std::wstring& imagePath, bool clockwise) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) || !wic) {
        return false;
    }

    HANDLE hIn = CreateFileW(imagePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hIn == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz = {};
    if (!GetFileSizeEx(hIn, &sz) || sz.QuadPart <= 0) {
        CloseHandle(hIn);
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(sz.QuadPart));
    DWORD readBytes = 0;
    BOOL readOk = ReadFile(hIn, bytes.data(), static_cast<DWORD>(bytes.size()), &readBytes, nullptr);
    CloseHandle(hIn);
    if (!readOk || readBytes == 0) return false;

    ComPtr<IWICStream> memStream;
    if (FAILED(wic->CreateStream(&memStream)) || FAILED(memStream->InitializeFromMemory(bytes.data(), readBytes))) {
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromStream(memStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) || !decoder) {
        return false;
    }

    GUID containerFormat = GUID_ContainerFormatPng;
    decoder->GetContainerFormat(&containerFormat);

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) return false;

    // CRITICAL: Cache the decoded frame ONCE in RAM (WICBitmapCacheOnLoad) before rotating!
    // Without this, IWICBitmapFlipRotator re-decodes the JPEG stream from byte 0 for every output scanline!
    ComPtr<IWICBitmap> cachedBitmap;
    if (FAILED(wic->CreateBitmapFromSource(frame.Get(), WICBitmapCacheOnLoad, &cachedBitmap)) || !cachedBitmap) {
        return false;
    }

    ComPtr<IWICBitmapFlipRotator> rotator;
    WICBitmapTransformOptions opt = clockwise ? WICBitmapTransformRotate90 : WICBitmapTransformRotate270;
    if (FAILED(wic->CreateBitmapFlipRotator(&rotator)) || FAILED(rotator->Initialize(cachedBitmap.Get(), opt))) {
        return false;
    }

    // Encode to a temporary sibling file first, then atomically replace the original file
    std::wstring tmpPath = imagePath + L".rot_tmp";
    DeleteFileW(tmpPath.c_str());

    {
        ComPtr<IWICStream> outStream;
        if (FAILED(wic->CreateStream(&outStream)) || FAILED(outStream->InitializeFromFilename(tmpPath.c_str(), GENERIC_WRITE))) {
            return false;
        }

        ComPtr<IWICBitmapEncoder> encoder;
        if (FAILED(wic->CreateEncoder(containerFormat, nullptr, &encoder)) ||
            FAILED(encoder->Initialize(outStream.Get(), WICBitmapEncoderNoCache))) {
            return false;
        }

        ComPtr<IWICBitmapFrameEncode> frameEncode;
        if (FAILED(encoder->CreateNewFrame(&frameEncode, nullptr)) ||
            FAILED(frameEncode->Initialize(nullptr)) ||
            FAILED(frameEncode->WriteSource(rotator.Get(), nullptr)) ||
            FAILED(frameEncode->Commit()) ||
            FAILED(encoder->Commit())) {
            outStream.Reset();
            DeleteFileW(tmpPath.c_str());
            return false;
        }
    }

    if (!MoveFileExW(tmpPath.c_str(), imagePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }
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
