#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "ProgramsInfo.h"
#ifndef _WIN32
#include "PathUtils.h"
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#endif
#include <algorithm>
#include <cwctype>
#include <unordered_set>

namespace Explorer2 {

namespace {

#ifdef _WIN32
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
#endif

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

#ifdef _WIN32
std::wstring NormalizeDate(const std::wstring& raw) {
    if (raw.size() == 8 && std::all_of(raw.begin(), raw.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
        return raw.substr(0, 4) + L"-" + raw.substr(4, 2) + L"-" + raw.substr(6, 2);
    }
    return raw;
}
#endif

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
    m_items = ReadInstalled();
}

std::vector<InstalledProgram> ProgramInventory::ReadInstalled() {
    std::vector<InstalledProgram> items;
#ifndef _WIN32
    std::unordered_set<std::wstring> seen;

    auto trimA = [](const std::string& s) -> std::string {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
        return s.substr(a, b - a);
    };

    // 1. FreeDesktop .desktop GUI applications (/usr/share/applications + ~/.local/share/applications)
    std::vector<std::pair<std::string, bool>> desktopDirs = { { "/usr/share/applications", false } };
    if (const char* home = std::getenv("HOME"); home && *home) {
        desktopDirs.emplace_back(std::string(home) + "/.local/share/applications", true);
    }
    for (const auto& [dirPath, perUser] : desktopDirs) {
        DIR* d = ::opendir(dirPath.c_str());
        if (!d) continue;
        while (struct dirent* de = ::readdir(d)) {
            std::string fn = de->d_name;
            if (fn.size() <= 8 || fn.compare(fn.size() - 8, 8, ".desktop") != 0) continue;
            std::string full = dirPath + "/" + fn;
            std::ifstream f(full);
            if (!f) continue;
            bool inEntry = false, noDisplay = false;
            std::string nameA, commentA, execA;
            std::string line;
            while (std::getline(f, line)) {
                std::string t = trimA(line);
                if (t.empty() || t[0] == '#') continue;
                if (t[0] == '[') {
                    inEntry = (t == "[Desktop Entry]");
                    continue;
                }
                if (!inEntry) continue;
                if (t.rfind("Name=", 0) == 0 && nameA.empty()) nameA = trimA(t.substr(5));
                else if (t.rfind("Comment=", 0) == 0 && commentA.empty()) commentA = trimA(t.substr(8));
                else if (t.rfind("Exec=", 0) == 0 && execA.empty()) execA = trimA(t.substr(5));
                else if (t.rfind("NoDisplay=", 0) == 0 && trimA(t.substr(10)) == "true") noDisplay = true;
            }
            if (noDisplay || nameA.empty()) continue;
            InstalledProgram p;
            p.regKey = PathUtils::Utf8ToWide("desktop:" + fn);
            p.name = PathUtils::Utf8ToWide(nameA);
            p.publisher = commentA.empty() ? L"FreeDesktop Application" : PathUtils::Utf8ToWide(commentA);
            p.installLocation = PathUtils::Utf8ToWide(full);
            p.isPerUser = perUser;
            struct stat st = {};
            if (::stat(full.c_str(), &st) == 0) {
                struct tm lt = {};
                localtime_r(&st.st_mtime, &lt);
                wchar_t dbuf[32] = {};
                swprintf_s(dbuf, L"%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
                p.installDate = dbuf;
            }
            std::wstring dedupe = Lower(p.name) + L"|";
            if (seen.insert(dedupe).second) items.push_back(std::move(p));
        }
        ::closedir(d);
    }

    // 2. Debian / Ubuntu dpkg package database (/var/lib/dpkg/status)
    {
        std::ifstream df("/var/lib/dpkg/status");
        if (df) {
            std::string pkg, ver, maint, arch, status;
            uint64_t instKB = 0;
            auto flushPkg = [&]() {
                if (!pkg.empty() && status.find("install ok installed") != std::string::npos) {
                    InstalledProgram p;
                    p.regKey = PathUtils::Utf8ToWide(pkg);
                    p.name = p.regKey;
                    p.version = PathUtils::Utf8ToWide(ver);
                    size_t ltPos = maint.find('<');
                    std::string cleanMaint = (ltPos != std::string::npos && ltPos > 0) ? trimA(maint.substr(0, ltPos)) : maint;
                    p.publisher = PathUtils::Utf8ToWide(cleanMaint);
                    p.sizeBytes = instKB * 1024ULL;
                    p.is32Bit = (arch == "i386" || arch == "i686" || arch == "armhf");
                    p.uninstallString = L"apt remove " + p.regKey;
                    p.canUninstall = false;
                    std::wstring dedupe = Lower(p.name) + L"|" + Lower(p.version);
                    if (seen.insert(dedupe).second) items.push_back(std::move(p));
                }
                pkg.clear(); ver.clear(); maint.clear(); arch.clear(); status.clear(); instKB = 0;
            };

            std::string line;
            while (std::getline(df, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) {
                    flushPkg();
                    continue;
                }
                if (line[0] == ' ' || line[0] == '\t') continue; // multi-line Description continuation
                if (line.rfind("Package:", 0) == 0) pkg = trimA(line.substr(8));
                else if (line.rfind("Version:", 0) == 0) ver = trimA(line.substr(8));
                else if (line.rfind("Maintainer:", 0) == 0) maint = trimA(line.substr(11));
                else if (line.rfind("Architecture:", 0) == 0) arch = trimA(line.substr(13));
                else if (line.rfind("Status:", 0) == 0) status = trimA(line.substr(7));
                else if (line.rfind("Installed-Size:", 0) == 0) instKB = std::strtoull(line.c_str() + 15, nullptr, 10);
            }
            flushPkg();
        }
    }

    // 3. Fallback for minimal containers without dpkg or .desktop files: enumerate /usr/bin executables
    if (items.empty()) {
        if (DIR* d = ::opendir("/usr/bin")) {
            int count = 0;
            while (struct dirent* de = ::readdir(d)) {
                if (de->d_name[0] == '.') continue;
                InstalledProgram p;
                p.regKey = PathUtils::Utf8ToWide(de->d_name);
                p.name = p.regKey;
                p.publisher = L"POSIX System Utility";
                p.installLocation = L"/usr/bin/" + p.name;
                items.push_back(std::move(p));
                if (++count >= 200) break;
            }
            ::closedir(d);
        }
    }
    return items;
#else
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

        for (DWORD i = 0; i < 200000; ++i) {
            wchar_t subName[512] = {};
            DWORD subLen = 512;
            LONG rc = RegEnumKeyExW(hUninstall, i, subName, &subLen, nullptr, nullptr, nullptr, nullptr);
            if (rc == ERROR_MORE_DATA) continue;   // a key name longer than any product id: skip just that one
            // ERROR_NO_MORE_ITEMS ends the list. Any other error (e.g. the key was deleted by a running
            // uninstaller) repeats for every later index, so skipping it would never end.
            if (rc != ERROR_SUCCESS) break;

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
                    items.push_back(std::move(p));
                }
            }
            RegCloseKey(hApp);
        }
        RegCloseKey(hUninstall);
    }
#endif
    return items;
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
