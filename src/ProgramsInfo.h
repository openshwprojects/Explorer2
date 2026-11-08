#pragma once
#include "Types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Explorer2 {

// One entry of "Programs and Features" as stored under the Uninstall registry keys.
struct InstalledProgram {
    std::wstring regKey;            // sub-key name (MSI product code like {GUID} or an arbitrary id)
    std::wstring name;
    std::wstring publisher;
    std::wstring version;
    std::wstring installDate;       // YYYY-MM-DD (or empty)
    std::wstring installLocation;
    std::wstring uninstallString;
    uint64_t sizeBytes = 0;         // EstimatedSize (KB) * 1024, 0 when unknown
    bool isMsi = false;
    bool isPerUser = false;         // HKCU entry
    bool is32Bit = false;           // WOW6432Node entry
    bool canUninstall = true;
};

// Enumerates installed desktop programs and launches their (un)installers.
class ProgramInventory {
public:
    // Column ids shared with the UI: 0 name, 1 publisher, 2 version, 3 size, 4 install date
    void Refresh();
    void Sort(int column, bool ascending);

    const std::vector<InstalledProgram>& Items() const { return m_items; }
    const InstalledProgram* Find(const std::wstring& regKey) const;

    // Starts the program's own uninstaller (UAC prompt is shown by Windows when the uninstaller needs it).
    static bool Uninstall(HWND owner, const InstalledProgram& program);
    // Runs a .exe / .msi / .msix installer chosen by the user.
    static bool InstallFromFile(HWND owner, const std::wstring& installerPath);

private:
    std::vector<InstalledProgram> m_items;
};

} // namespace Explorer2
