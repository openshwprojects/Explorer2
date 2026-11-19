#pragma once
// WinEOverride.h - makes Win+E start Explorer2 instead of Windows Explorer, for the current user only.
//
// Win+E runs the "opennewwindow" verb of the shell object {52205fd8-5dfb-447d-801a-d0b52f2e83e1}. Its command
// normally comes from HKEY_LOCAL_MACHINE and hands over to Windows Explorer through a DelegateExecute value. A key
// of the same name under HKEY_CURRENT_USER\Software\Classes takes precedence: with a command of its own and an
// empty DelegateExecute value, Windows starts that command instead. Removing the key gives Win+E back to Windows
// Explorer. Nothing outside the current user's own registry is touched and no administrator rights are needed.
//
// Header-only (neither build script has a file to add). Every function takes the key the override lives in, so
// that the self-tests can run them on a scratch key instead of the real one.
#include <windows.h>
#include <string>

namespace Explorer2::WinEOverride {

inline constexpr wchar_t kShellObjectKey[] = L"Software\\Classes\\CLSID\\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}";

// The program a command line starts: the text between its first pair of quotes, or up to the first space
inline std::wstring ExeOfCommand(const std::wstring& command) {
    size_t i = 0;
    while (i < command.size() && command[i] == L' ') ++i;
    if (i < command.size() && command[i] == L'"') {
        const size_t end = command.find(L'"', i + 1);
        return command.substr(i + 1, (end == std::wstring::npos) ? std::wstring::npos : end - i - 1);
    }
    const size_t end = command.find(L' ', i);
    return command.substr(i, (end == std::wstring::npos) ? std::wstring::npos : end - i);
}

#ifdef _WIN32

// The command Win+E runs for this user, empty when there is no override (Windows Explorer opens)
inline std::wstring ReadCommand(const std::wstring& baseKey) {
    HKEY key = nullptr;
    const std::wstring path = baseKey + L"\\shell\\opennewwindow\\command";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return {};
    wchar_t buf[2048] = {};
    DWORD type = 0;
    DWORD bytes = sizeof(buf) - sizeof(wchar_t);
    std::wstring out;
    if (RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(buf), &bytes) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        out = buf;
    }
    RegCloseKey(key);
    return out;
}

// True when Win+E starts exactly this program
inline bool IsSetFor(const std::wstring& baseKey, const std::wstring& exePath) {
    const std::wstring current = ExeOfCommand(ReadCommand(baseKey));
    return !current.empty() && !exePath.empty() && _wcsicmp(current.c_str(), exePath.c_str()) == 0;
}

// Points Win+E at exePath. False when the registry refuses the write.
inline bool Set(const std::wstring& baseKey, const std::wstring& exePath) {
    if (exePath.empty() || exePath.find(L'"') != std::wstring::npos) return false;
    HKEY key = nullptr;
    const std::wstring path = baseKey + L"\\shell\\opennewwindow\\command";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring command = L"\"" + exePath + L"\"";
    const wchar_t empty[1] = { 0 };
    const bool ok =
        RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS &&
        // Present and empty: without it Windows keeps handing the verb to Windows Explorer
        RegSetValueExW(key, L"DelegateExecute", 0, REG_SZ, reinterpret_cast<const BYTE*>(empty), sizeof(empty)) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

// Gives Win+E back to Windows Explorer: removes the override and the keys that only held it. True when no
// override is left afterwards.
inline bool Remove(const std::wstring& baseKey) {
    RegDeleteTreeW(HKEY_CURRENT_USER, (baseKey + L"\\shell\\opennewwindow").c_str());
    // RegDeleteKeyW removes a key only when it has no subkeys: anything another program keeps here stays
    RegDeleteKeyW(HKEY_CURRENT_USER, (baseKey + L"\\shell\\opennewwindow").c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, (baseKey + L"\\shell").c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, baseKey.c_str());
    return ReadCommand(baseKey).empty();
}

#else

// Win+E is a Windows shortcut: the Linux build has nothing to set
inline std::wstring ReadCommand(const std::wstring&) { return {}; }
inline bool IsSetFor(const std::wstring&, const std::wstring&) { return false; }
inline bool Set(const std::wstring&, const std::wstring&) { return false; }
inline bool Remove(const std::wstring&) { return true; }

#endif

} // namespace Explorer2::WinEOverride
