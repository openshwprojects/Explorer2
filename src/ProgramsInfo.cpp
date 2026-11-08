#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "ProgramsInfo.h"
#include <algorithm>
#include <cwctype>
#include <unordered_set>

namespace Explorer2 {

namespace {

constexpr const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

std::wstring ReadString(HKEY key, const wchar_t* valueName) {
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || size == 0) return L"";
    if (type != REG_SZ && type != REG_EXPAND_SZ) return L"";
    std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
    DWORD sz = size;
    if (RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &sz) != ERROR_SUCCESS) return L"";
    buf.resize(wcslen(buf.c_str()));
    if (type == REG_EXPAND_SZ && !buf.empty()) {
        DWORD need = ExpandEnvironmentStringsW(buf.c_str(), nullptr, 0);
        if (need > 0) {
            std::wstring expanded(need, L'\0');
            ExpandEnvironmentStringsW(buf.c_str(), expanded.data(), need);
            expanded.resize(wcslen(expanded.c_str()));
            buf = expanded;
        }
    }
    return buf;
}

DWORD ReadDword(HKEY key, const wchar_t* valueName, DWORD def = 0) {
    DWORD type = 0, val = 0, size = sizeof(val);
    if (RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS && type == REG_DWORD) {
        return val;
    }
    return def;
}

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::wstring NormalizeDate(const std::wstring& raw) {
    if (raw.size() == 8 && std::all_of(raw.begin(), raw.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
        return raw.substr(0, 4) + L"-" + raw.substr(4, 2) + L"-" + raw.substr(6, 2);
    }
    return raw;
}

// Splits `"C:\path\un.exe" /arg` or `C:\path\un.exe /arg` into executable and arguments.
void SplitCommandLine(const std::wstring& cmdIn, std::wstring& exe, std::wstring& args) {
    std::wstring cmd = Trim(cmdIn);
    exe.clear();
    args.clear();
    if (cmd.empty()) return;
    if (cmd[0] == L'"') {
        size_t close = cmd.find(L'"', 1);
        if (close == std::wstring::npos) { exe = cmd.substr(1); return; }
        exe = cmd.substr(1, close - 1);
        args = Trim(cmd.substr(close + 1));
        return;
    }
    std::wstring low = Lower(cmd);
    size_t pos = low.find(L".exe");
    if (pos != std::wstring::npos) {
        exe = cmd.substr(0, pos + 4);
        args = Trim(cmd.substr(pos + 4));
    } else {
        exe = cmd;
    }
}

int CompareNoCase(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str());
}

} // namespace

void ProgramInventory::Refresh() {
    m_items.clear();

    struct Source { HKEY root; REGSAM extra; bool perUser; bool is32; };
    const Source sources[] = {
        { HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, false, false },
        { HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, false, true  },
        { HKEY_CURRENT_USER,  0,               true,  false },
    };

    std::unordered_set<std::wstring> seen;

    for (const auto& src : sources) {
        HKEY hUninstall = nullptr;
        if (RegOpenKeyExW(src.root, kUninstallKey, 0, KEY_READ | src.extra, &hUninstall) != ERROR_SUCCESS) continue;

        for (DWORD i = 0;; ++i) {
            wchar_t subName[512] = {};
            DWORD subLen = 512;
            LONG rc = RegEnumKeyExW(hUninstall, i, subName, &subLen, nullptr, nullptr, nullptr, nullptr);
            if (rc == ERROR_NO_MORE_ITEMS) break;
            if (rc != ERROR_SUCCESS) continue;

            HKEY hApp = nullptr;
            if (RegOpenKeyExW(hUninstall, subName, 0, KEY_READ | src.extra, &hApp) != ERROR_SUCCESS) continue;

            InstalledProgram p;
            p.name = Trim(ReadString(hApp, L"DisplayName"));
            bool skip = p.name.empty();
            if (!skip && ReadDword(hApp, L"SystemComponent") == 1) skip = true;
            if (!skip && !ReadString(hApp, L"ParentKeyName").empty()) skip = true;   // updates of another product
            if (!skip) {
                std::wstring release = Lower(ReadString(hApp, L"ReleaseType"));
                if (release == L"security update" || release == L"update rollup" || release == L"hotfix") skip = true;
            }

            if (!skip) {
                p.regKey = subName;
                p.publisher = Trim(ReadString(hApp, L"Publisher"));
                p.version = Trim(ReadString(hApp, L"DisplayVersion"));
                p.installLocation = Trim(ReadString(hApp, L"InstallLocation"));
                p.uninstallString = Trim(ReadString(hApp, L"UninstallString"));
                if (p.uninstallString.empty()) p.uninstallString = Trim(ReadString(hApp, L"QuietUninstallString"));

                std::wstring dateStr = ReadString(hApp, L"InstallDate");
                if (dateStr.empty()) {
                    DWORD d = ReadDword(hApp, L"InstallDate", 0);
                    if (d > 0) dateStr = std::to_wstring(d);
                }
                p.installDate = NormalizeDate(dateStr);

                p.sizeBytes = static_cast<uint64_t>(ReadDword(hApp, L"EstimatedSize", 0)) * 1024ull;
                p.isMsi = (ReadDword(hApp, L"WindowsInstaller", 0) == 1) && p.regKey.size() > 2 && p.regKey[0] == L'{';
                p.isPerUser = src.perUser;
                p.is32Bit = src.is32;
                p.canUninstall = (ReadDword(hApp, L"NoRemove", 0) != 1) && (p.isMsi || !p.uninstallString.empty());

                std::wstring dedupe = Lower(p.name) + L"|" + Lower(p.version);
                if (seen.insert(dedupe).second) {
                    m_items.push_back(std::move(p));
                }
            }
            RegCloseKey(hApp);
        }
        RegCloseKey(hUninstall);
    }
}

void ProgramInventory::Sort(int column, bool ascending) {
    auto less = [column](const InstalledProgram& a, const InstalledProgram& b) {
        int c = 0;
        switch (column) {
        case 1: c = CompareNoCase(a.publisher, b.publisher); break;
        case 2: c = CompareNoCase(a.version, b.version); break;
        case 3: c = (a.sizeBytes < b.sizeBytes) ? -1 : (a.sizeBytes > b.sizeBytes ? 1 : 0); break;
        case 4: c = CompareNoCase(a.installDate, b.installDate); break;
        default: c = CompareNoCase(a.name, b.name); break;
        }
        if (c == 0) c = CompareNoCase(a.name, b.name);
        return c < 0;
    };
    std::stable_sort(m_items.begin(), m_items.end(), less);
    if (!ascending) std::reverse(m_items.begin(), m_items.end());
}

const InstalledProgram* ProgramInventory::Find(const std::wstring& regKey) const {
    for (const auto& p : m_items) {
        if (p.regKey == regKey) return &p;
    }
    return nullptr;
}

bool ProgramInventory::Uninstall(HWND owner, const InstalledProgram& program) {
    HINSTANCE result = nullptr;
    if (program.isMsi) {
        std::wstring args = L"/x " + program.regKey;
        result = ShellExecuteW(owner, L"open", L"msiexec.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
        std::wstring exe, args;
        SplitCommandLine(program.uninstallString, exe, args);
        if (exe.empty()) return false;
        result = ShellExecuteW(owner, L"open", exe.c_str(), args.empty() ? nullptr : args.c_str(), nullptr, SW_SHOWNORMAL);
    }
    return reinterpret_cast<INT_PTR>(result) > 32;
}

bool ProgramInventory::InstallFromFile(HWND owner, const std::wstring& installerPath) {
    if (installerPath.empty()) return false;
    std::wstring ext;
    size_t dot = installerPath.find_last_of(L'.');
    if (dot != std::wstring::npos) ext = Lower(installerPath.substr(dot));

    HINSTANCE result = nullptr;
    if (ext == L".msi") {
        std::wstring args = L"/i \"" + installerPath + L"\"";
        result = ShellExecuteW(owner, L"open", L"msiexec.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
        std::wstring dir;
        size_t slash = installerPath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir = installerPath.substr(0, slash);
        result = ShellExecuteW(owner, L"open", installerPath.c_str(), nullptr, dir.empty() ? nullptr : dir.c_str(), SW_SHOWNORMAL);
    }
    return reinterpret_cast<INT_PTR>(result) > 32;
}

} // namespace Explorer2
