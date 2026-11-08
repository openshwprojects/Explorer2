#include "FileEngine.h"
#include "ArchiveExplorer.h"
#include "FtpClient.h"
#include "PathUtils.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

namespace Explorer2 {

// Reads HKCU\Network\<Letter>\RemotePath in <0.005ms without touching the network!
// Formats "\\NAS191725\Backup" on 'X' as "Backup (\\NAS191725) (X:)" matching Windows 10 Explorer.
static std::wstring GetMappedNetworkDriveLabel(wchar_t driveLetter, Lang lang) {
    wchar_t subKey[64] = {};
    swprintf_s(subKey, L"Network\\%c", static_cast<wchar_t>(towupper(driveLetter)));
    std::wstring dl = std::wstring(1, static_cast<wchar_t>(towupper(driveLetter))) + L":";

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t remoteBuf[MAX_PATH] = {};
        DWORD cb = sizeof(remoteBuf);
        if (RegQueryValueExW(hKey, L"RemotePath", nullptr, nullptr, reinterpret_cast<LPBYTE>(remoteBuf), &cb) == ERROR_SUCCESS && remoteBuf[0]) {
            RegCloseKey(hKey);
            std::wstring rem = PathUtils::TrimTrailingSlashes(remoteBuf);
            // Parse "\\Server\Share" -> "Share (\\Server) (X:)"
            if (rem.rfind(L"\\\\", 0) == 0) {
                size_t slash = rem.find(L'\\', 2);
                if (slash != std::wstring::npos && slash + 1 < rem.size()) {
                    std::wstring server = rem.substr(0, slash);
                    std::wstring share = rem.substr(slash + 1);
                    return share + L" (" + server + L") (" + dl + L")";
                }
            }
            return rem + L" (" + dl + L")";
        }
        RegCloseKey(hKey);
    }
    return (lang == Lang::PL ? L"Dysk sieciowy (" : L"Network Drive (") + dl + L")";
}

void FileEngine::ProbeNetworkDriveAsync(wchar_t driveLetter) {
    wchar_t dl = static_cast<wchar_t>(towupper(driveLetter));
    {
        std::lock_guard<std::mutex> lk(m_netDriveMutex);
        auto it = m_netDrives.find(dl);
        if (it != m_netDrives.end() && it->second.probed) {
            return;
        }
        m_netDrives[dl] = { true, false, 0 };
    }

    HWND notifyHwnd = m_notifyHwnd;
    std::thread([this, dl, notifyHwnd]() {
        wchar_t rootPath[] = { dl, L':', L'\\', L'\0' };
        ULARGE_INTEGER freeBytes = {}, totalBytes = {};
        BOOL ok = GetDiskFreeSpaceExW(rootPath, &freeBytes, &totalBytes, nullptr);
        if (!m_running) return;
        {
            std::lock_guard<std::mutex> lk(m_netDriveMutex);
            m_netDrives[dl] = { true, ok != FALSE, ok ? totalBytes.QuadPart : 0ULL };
        }
        if (ok && notifyHwnd && m_running) {
            PostMessageW(notifyHwnd, WM_APP_FOLDER_SIZE_UPDATED, 0, 0);
        }
    }).detach();
}

bool FileEngine::IsDriveOffline(const std::wstring& path, Lang lang, std::wstring* outDisplayName) {
    if (path.size() < 2 || path[1] != L':') return false;
    wchar_t dl = static_cast<wchar_t>(towupper(path[0]));
    wchar_t rootPath[] = { dl, L':', L'\\', L'\0' };
    UINT dtype = GetDriveTypeW(rootPath);
    if (dtype == DRIVE_REMOTE) {
        if (outDisplayName) *outDisplayName = GetMappedNetworkDriveLabel(dl, lang);
        ProbeNetworkDriveAsync(dl);
        std::lock_guard<std::mutex> lk(m_netDriveMutex);
        auto it = m_netDrives.find(dl);
        return (it == m_netDrives.end()) || !it->second.online;
    }
    if (dtype == DRIVE_CDROM) {
        wchar_t volName[MAX_PATH] = {};
        if (!GetVolumeInformationW(rootPath, volName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0)) {
            if (outDisplayName) {
                *outDisplayName = (lang == Lang::PL ? L"Stacja dysków BD-ROM (" : L"BD-ROM Drive (") + std::wstring(1, dl) + L":)";
            }
            return true;
        }
    }
    return false;
}

#ifndef FILE_DISPOSITION_DELETE
#define FILE_DISPOSITION_DELETE                         0x00000001
#define FILE_DISPOSITION_POSIX_SEMANTICS                0x00000002
#define FILE_DISPOSITION_FORCE_IMAGE_SECTION_CHECK      0x00000004
#define FILE_DISPOSITION_ON_CLOSE                       0x00000008
#define FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE      0x00000010
#endif

struct MY_FILE_DISPOSITION_INFO_EX {
    DWORD Flags;
};

FileEngine::FileEngine() = default;

FileEngine::~FileEngine() {
    Shutdown();
}

void FileEngine::Initialize(HWND notifyHwnd) {
    m_notifyHwnd = notifyHwnd;
    m_running = true;

    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(L"Folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
                       SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON)) {
        m_folderIconIndex = sfi.iIcon;
    }

    m_deleteThread = std::thread(&FileEngine::DeleteWorkerLoop, this);
    for (int i = 0; i < 4; ++i) {
        m_sizeThreads.emplace_back(&FileEngine::FolderSizeWorkerLoop, this);
    }
    m_searchThread = std::thread(&FileEngine::SearchWorkerLoop, this);
}

void FileEngine::Shutdown() {
    if (m_running.exchange(false)) {
        m_sizeGeneration.fetch_add(1);
        m_searchGeneration.fetch_add(1);
        m_deleteCv.notify_all();
        m_sizeCv.notify_all();
        m_searchCv.notify_all();
        if (m_deleteThread.joinable()) {
            m_deleteThread.join();
        }
        for (auto& t : m_sizeThreads) {
            if (t.joinable()) t.join();
        }
        m_sizeThreads.clear();
        if (m_searchThread.joinable()) {
            m_searchThread.join();
        }
        CleanupAllUndoStaging();
    }
}

const FileEngine::ExtTypeInfo& FileEngine::GetExtensionInfo(const std::wstring& extLower, bool isDir, Lang lang) {
    std::lock_guard<std::mutex> lock(m_extMutex);
    const Strings& s = GetStrings(lang);

    if (isDir) {
        std::wstring key = (lang == Lang::PL) ? L"__DIR_PL__" : L"__DIR_EN__";
        auto it = m_extCache.find(key);
        if (it != m_extCache.end()) return it->second;

        ExtTypeInfo info;
        info.typeName = s.typeFolder;
        info.sysIconIndex = m_folderIconIndex;
        return m_extCache.emplace(key, std::move(info)).first->second;
    }

    std::wstring key = extLower.empty() ? L"__NO_EXT__" : extLower;
    auto it = m_extCache.find(key);
    if (it != m_extCache.end()) {
        return it->second;
    }

    ExtTypeInfo info;
    SHFILEINFOW sfi = {};
    std::wstring dummyName = extLower.empty() ? L"file" : (L"file" + extLower);
    if (SHGetFileInfoW(dummyName.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                       SHGFI_USEFILEATTRIBUTES | SHGFI_TYPENAME | SHGFI_SYSICONINDEX | SHGFI_SMALLICON)) {
        info.typeName = sfi.szTypeName[0] ? sfi.szTypeName : s.typeFile;
        info.sysIconIndex = sfi.iIcon;
    } else {
        info.typeName = s.typeFile;
        info.sysIconIndex = 0;
    }

    return m_extCache.emplace(key, std::move(info)).first->second;
}

std::vector<FileEntry> FileEngine::EnumerateDirectory(
    const std::wstring& dirPath,
    bool showHidden,
    bool showExtensions,
    Lang lang)
{
    std::vector<FileEntry> results;

    if (dirPath == L"THIS_PC" || dirPath.empty()) {
        DWORD drivesMask = GetLogicalDrives();
        wchar_t rootPath[] = L"A:\\";
        for (int i = 0; i < 26; ++i) {
            if (drivesMask & (1 << i)) {
                wchar_t dlChar = static_cast<wchar_t>(L'A' + i);
                rootPath[0] = dlChar;
                std::wstring driveLetter = std::wstring(1, dlChar) + L":";
                UINT dtype = GetDriveTypeW(rootPath);

                FileEntry entry;
                entry.fullPath = rootPath;
                entry.normKey = PathUtils::NormalizePathKey(rootPath);
                entry.isDirectory = true;
                entry.isDrive = true;
                entry.sysIconIndex = m_folderIconIndex;

                if (dtype == DRIVE_REMOTE) {
                    entry.isNetworkDrive = true;
                    entry.name = GetMappedNetworkDriveLabel(dlChar, lang);
                    entry.displayName = entry.name;
                    ProbeNetworkDriveAsync(dlChar);

                    bool online = false;
                    uint64_t netTotal = 0;
                    {
                        std::lock_guard<std::mutex> lk(m_netDriveMutex);
                        auto it = m_netDrives.find(dlChar);
                        if (it != m_netDrives.end()) {
                            online = it->second.online;
                            netTotal = it->second.totalBytes;
                        }
                    }
                    entry.isOffline = !online;
                    entry.size = netTotal;
                    entry.sizeStr = (online && netTotal > 0) ? FormatSizeDetailed(netTotal) : (lang == Lang::PL ? L"Offline" : L"Offline");
                    entry.typeName = online
                        ? (lang == Lang::PL ? L"Dysk sieciowy" : L"Network Drive")
                        : (lang == Lang::PL ? L"Odłączony dysk sieciowy" : L"Disconnected Network Drive");
                } else if (dtype == DRIVE_CDROM) {
                    entry.isOpticalDrive = true;
                    wchar_t volName[MAX_PATH] = {};
                    BOOL volOk = GetVolumeInformationW(rootPath, volName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
                    entry.isOffline = !volOk;
                    if (volOk && volName[0]) {
                        entry.name = std::wstring(volName) + L" (" + driveLetter + L")";
                    } else {
                        entry.name = (lang == Lang::PL ? L"Stacja dysków BD-ROM (" : L"BD-ROM Drive (") + driveLetter + L")";
                    }
                    entry.displayName = entry.name;
                    entry.typeName = (lang == Lang::PL) ? L"Stacja dysków CD/DVD/BD-ROM" : L"CD/DVD/BD-ROM Drive";
                    if (volOk) {
                        ULARGE_INTEGER freeBytes = {}, totalBytes = {};
                        if (GetDiskFreeSpaceExW(rootPath, &freeBytes, &totalBytes, nullptr)) {
                            entry.size = totalBytes.QuadPart;
                            entry.sizeStr = FormatSizeDetailed(totalBytes.QuadPart);
                        }
                    }
                } else {
                    wchar_t volName[MAX_PATH] = {};
                    BOOL volOk = GetVolumeInformationW(rootPath, volName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
                    entry.isOffline = !volOk;

                    ULARGE_INTEGER freeBytes = {}, totalBytes = {};
                    if (volOk) {
                        GetDiskFreeSpaceExW(rootPath, &freeBytes, &totalBytes, nullptr);
                    }

                    if (volName[0]) {
                        entry.name = std::wstring(volName) + L" (" + driveLetter + L")";
                    } else {
                        entry.name = (lang == Lang::PL ? L"Dysk lokalny (" : L"Local Disk (") + driveLetter + L")";
                    }
                    entry.displayName = entry.name;
                    entry.size = totalBytes.QuadPart;
                    if (totalBytes.QuadPart > 0) {
                        entry.sizeStr = FormatSizeDetailed(totalBytes.QuadPart);
                    }
                    entry.typeName = (lang == Lang::PL) ? L"Dysk lokalny" : L"Local Disk";
                }

                results.push_back(std::move(entry));
            }
        }
        return results;
    }

    if (FtpClient::IsFtpPath(dirPath)) {
        FtpClient::EnumerateFtpDirectory(dirPath, showHidden, showExtensions, lang, results, nullptr);
        return results;
    }

    results.reserve(512);

    std::wstring cleanDir = PathUtils::TrimTrailingSlashes(dirPath);
    std::wstring prefix = PathUtils::EnsureTrailingSlash(cleanDir);
    std::wstring searchPattern = prefix + L"*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileExW(
        searchPattern.c_str(),
        FindExInfoBasic,
        &ffd,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);

    if (hFind == INVALID_HANDLE_VALUE) {
        std::wstring arcFile, innerDir;
        if (ArchiveExplorer::SplitVirtualArchivePath(cleanDir, arcFile, innerDir)) {
            std::vector<ArchiveItemEntry> arcChildren;
            if (ArchiveExplorer::EnumerateArchiveFolder(arcFile, innerDir, arcChildren)) {
                for (const auto& ac : arcChildren) {
                    FileEntry entry;
                    entry.name = ac.name;
                    entry.fullPath = prefix + ac.name;
                    entry.normKey = PathUtils::NormalizePathKey(entry.fullPath);
                    entry.isDirectory = ac.isDirectory;
                    entry.size = ac.uncompressedSize;
                    if (!entry.isDirectory) {
                        entry.sizeStr = FormatSizeKB(entry.size);
                        entry.ext = PathUtils::GetExtension(entry.name);
                        entry.displayName = showExtensions ? entry.name : PathUtils::GetStem(entry.name, false);
                        entry.isImage = IsImageExtension(entry.ext);
                        entry.isVideo = IsVideoExtension(entry.ext);
                        entry.isAudio = IsAudioExtension(entry.ext);
                    } else {
                        entry.displayName = entry.name;
                        if (entry.size > 0) entry.sizeStr = FormatSizeKB(entry.size);
                    }
                    if (ac.dosDate != 0) {
                        FILETIME localFt = {}, utcFt = {};
                        if (DosDateTimeToFileTime(ac.dosDate, ac.dosTime, &localFt) &&
                            LocalFileTimeToFileTime(&localFt, &utcFt)) {
                            entry.writeTime = utcFt;
                            entry.dateStr = FormatFileTime(utcFt);
                        }
                    }
                    const ExtTypeInfo& extInfo = GetExtensionInfo(entry.ext, entry.isDirectory, lang);
                    entry.typeName = extInfo.typeName;
                    entry.sysIconIndex = extInfo.sysIconIndex;
                    results.push_back(std::move(entry));
                }
            }
        }
        return results;
    }

    do {
        const wchar_t* fileName = ffd.cFileName;
        if (fileName[0] == L'.' && (fileName[1] == L'\0' || (fileName[1] == L'.' && fileName[2] == L'\0'))) {
            continue;
        }
        if (wcscmp(fileName, L".explorer2_undo") == 0) {
            continue;
        }

        bool isHidden = (ffd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        if (!showHidden && isHidden) {
            continue;
        }

        FileEntry entry;
        entry.name = fileName;
        entry.fullPath = prefix + entry.name;
        entry.normKey = PathUtils::NormalizePathKey(entry.fullPath);
        entry.attributes = ffd.dwFileAttributes;
        entry.isDirectory = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.isHidden = isHidden;
        entry.writeTime = ffd.ftLastWriteTime;
        entry.dateStr = FormatFileTime(ffd.ftLastWriteTime);

        if (!entry.isDirectory) {
            entry.size = (static_cast<uint64_t>(ffd.nFileSizeHigh) << 32) | ffd.nFileSizeLow;
            entry.sizeStr = FormatSizeKB(entry.size);

            const wchar_t* dot = wcsrchr(fileName, L'.');
            if (dot && dot != fileName) {
                entry.ext = PathUtils::ToLower(dot);
                entry.displayName = showExtensions
                    ? entry.name
                    : entry.name.substr(0, static_cast<size_t>(dot - fileName));
            } else {
                entry.displayName = entry.name;
            }
            entry.isImage = IsImageExtension(entry.ext);
            entry.isVideo = IsVideoExtension(entry.ext);
            entry.isAudio = IsAudioExtension(entry.ext);
        } else {
            entry.displayName = entry.name;
        }

        const ExtTypeInfo& extInfo = GetExtensionInfo(entry.ext, entry.isDirectory, lang);
        entry.typeName = extInfo.typeName;
        entry.sysIconIndex = extInfo.sysIconIndex;

        results.push_back(std::move(entry));
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);

    // Hide entries that a background delete is still working on, so a refresh during a long delete
    // does not make a half-deleted folder "come back" before it is really gone.
    {
        std::lock_guard<std::mutex> lock(m_deleteStateMutex);
        if (!m_pendingDeleteKeys.empty()) {
            results.erase(std::remove_if(results.begin(), results.end(), [this](const FileEntry& e) {
                return m_pendingDeleteKeys.count(e.normKey) != 0;
            }), results.end());
        }
    }

    // Single-lock sync of any cached folder sizes from our hash table!
    SyncDirectorySizes(results);
    return results;
}

void FileEngine::SortEntries(std::vector<FileEntry>& entries, SortColumn column, bool ascending) {
    std::sort(entries.begin(), entries.end(), [column, ascending](const FileEntry& a, const FileEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory;
        }

        int cmp = 0;
        switch (column) {
        case SortColumn::Name:
            cmp = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
            break;
        case SortColumn::DateModified:
            cmp = CompareFileTime(&a.writeTime, &b.writeTime);
            if (cmp == 0) cmp = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
            break;
        case SortColumn::Type:
            cmp = StrCmpLogicalW(a.typeName.c_str(), b.typeName.c_str());
            if (cmp == 0) cmp = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
            break;
        case SortColumn::Size:
            if (a.size < b.size) cmp = -1;
            else if (a.size > b.size) cmp = 1;
            else cmp = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
            break;
        }

        return ascending ? (cmp < 0) : (cmp > 0);
    });
}

std::vector<std::pair<std::wstring, std::wstring>> FileEngine::GetDefaultQuickAccessFolders(Lang lang) {
    const Strings& s = GetStrings(lang);
    std::vector<std::pair<std::wstring, std::wstring>> list;

    auto addKnown = [&](REFKNOWNFOLDERID rfid, const wchar_t* label) {
        PWSTR pszPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(rfid, 0, nullptr, &pszPath)) && pszPath) {
            list.emplace_back(label, PathUtils::TrimTrailingSlashes(pszPath));
            CoTaskMemFree(pszPath);
        }
    };
    addKnown(FOLDERID_Desktop, s.navDesktop);
    addKnown(FOLDERID_Downloads, s.navDownloads);
    addKnown(FOLDERID_Documents, s.navDocuments);
    addKnown(FOLDERID_Pictures, s.navPictures);

    if (GetFileAttributesW(L"W:\\Fooocus_Advanced") != INVALID_FILE_ATTRIBUTES) {
        list.emplace_back(L"Fooocus_Advanced", L"W:\\Fooocus_Advanced");
    }
    return list;
}

std::vector<TreeNode> FileEngine::BuildNavigationTree(
    Lang lang,
    const std::vector<std::pair<std::wstring, std::wstring>>& pinnedQuickAccess)
{
    std::vector<TreeNode> nodes;
    const Strings& s = GetStrings(lang);

    // 1. Quick Access Header (expanded by default)
    TreeNode qaHeader;
    qaHeader.label = s.navQuickAccess;
    qaHeader.normKey = L"__HEADER_QA__";
    qaHeader.indent = 0;
    qaHeader.isHeader = true;
    qaHeader.isQuickAccess = true;
    qaHeader.hasChildren = true;
    qaHeader.isExpanded = true;
    nodes.push_back(qaHeader);

    for (const auto& [label, path] : pinnedQuickAccess) {
        TreeNode n;
        n.label = label;
        n.path = path;
        n.normKey = PathUtils::NormalizePathKey(path);
        n.indent = 1;
        n.isQuickAccess = true;
        n.hasChildren = true;
        n.isExpanded = false;
        nodes.push_back(std::move(n));
    }

    wchar_t userProfile[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH)) {
        std::wstring dropboxPath = std::wstring(userProfile) + L"\\Dropbox";
        if (GetFileAttributesW(dropboxPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            TreeNode n;
            n.label = L"Dropbox";
            n.path = dropboxPath;
            n.normKey = PathUtils::NormalizePathKey(dropboxPath);
            n.indent = 0;
            n.hasChildren = true;
            n.isExpanded = false;
            nodes.push_back(std::move(n));
        }
        std::wstring oneDrivePath = std::wstring(userProfile) + L"\\OneDrive";
        if (GetFileAttributesW(oneDrivePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            TreeNode n;
            n.label = L"OneDrive - Personal";
            n.path = oneDrivePath;
            n.normKey = PathUtils::NormalizePathKey(oneDrivePath);
            n.indent = 0;
            n.hasChildren = true;
            n.isExpanded = false;
            nodes.push_back(std::move(n));
        }
    }

    // 2. This PC ("Ten komputer", expanded by default)
    TreeNode pcHeader;
    pcHeader.label = s.navThisPC;
    pcHeader.path = L"THIS_PC";
    pcHeader.normKey = L"__HEADER_PC__";
    pcHeader.indent = 0;
    pcHeader.isHeader = true;
    pcHeader.hasChildren = true;
    pcHeader.isExpanded = true;
    nodes.push_back(pcHeader);

    DWORD drivesMask = GetLogicalDrives();
    wchar_t rootPath[] = L"A:\\";
    for (int i = 0; i < 26; ++i) {
        if (drivesMask & (1 << i)) {
            wchar_t dlChar = static_cast<wchar_t>(L'A' + i);
            rootPath[0] = dlChar;
            std::wstring driveLetter = std::wstring(1, dlChar) + L":";
            UINT dtype = GetDriveTypeW(rootPath);

            TreeNode driveNode;
            driveNode.path = rootPath;
            driveNode.normKey = PathUtils::NormalizePathKey(rootPath);
            driveNode.indent = 1;
            driveNode.isDrive = true;
            driveNode.hasChildren = true;
            driveNode.isExpanded = false;

            if (dtype == DRIVE_REMOTE) {
                driveNode.isNetworkDrive = true;
                driveNode.label = GetMappedNetworkDriveLabel(dlChar, lang);
                ProbeNetworkDriveAsync(dlChar);
                bool online = false;
                {
                    std::lock_guard<std::mutex> lk(m_netDriveMutex);
                    auto it = m_netDrives.find(dlChar);
                    if (it != m_netDrives.end()) online = it->second.online;
                }
                driveNode.isOffline = !online;
            } else if (dtype == DRIVE_CDROM) {
                driveNode.isOpticalDrive = true;
                wchar_t volName[MAX_PATH] = {};
                BOOL volOk = GetVolumeInformationW(rootPath, volName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
                driveNode.isOffline = !volOk;
                driveNode.label = (volOk && volName[0])
                    ? (std::wstring(volName) + L" (" + driveLetter + L")")
                    : ((lang == Lang::PL ? L"Stacja dysków BD-ROM (" : L"BD-ROM Drive (") + driveLetter + L")");
            } else {
                wchar_t volName[MAX_PATH] = {};
                BOOL volOk = GetVolumeInformationW(rootPath, volName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
                driveNode.isOffline = !volOk;
                if (volName[0]) {
                    driveNode.label = std::wstring(volName) + L" (" + driveLetter + L")";
                } else {
                    driveNode.label = (lang == Lang::PL ? L"Dysk lokalny (" : L"Local Disk (") + driveLetter + L")";
                }
            }
            nodes.push_back(std::move(driveNode));
        }
    }

    return nodes;
}

std::vector<TreeNode> FileEngine::EnumerateSubfoldersForTree(
    const std::wstring& parentPath,
    int childIndent,
    bool isQuickAccessBranch)
{
    std::vector<TreeNode> subNodes;
    if (parentPath.empty() || parentPath == L"THIS_PC") return subNodes;

    std::wstring prefix = PathUtils::EnsureTrailingSlash(PathUtils::TrimTrailingSlashes(parentPath));
    std::wstring pattern = prefix + L"*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &ffd,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);

    if (hFind == INVALID_HANDLE_VALUE) return subNodes;

    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (ffd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        const wchar_t* name = ffd.cFileName;
        if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) continue;
        if (wcscmp(name, L"$RECYCLE.BIN") == 0 || wcscmp(name, L"System Volume Information") == 0) continue;

        TreeNode child;
        child.label = name;
        child.path = prefix + name;
        child.normKey = PathUtils::NormalizePathKey(child.path);
        child.indent = childIndent;
        child.isQuickAccess = isQuickAccessBranch;
        child.hasChildren = true;
        child.isExpanded = false;
        subNodes.push_back(std::move(child));
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);

    std::sort(subNodes.begin(), subNodes.end(), [](const TreeNode& a, const TreeNode& b) {
        return StrCmpLogicalW(a.label.c_str(), b.label.c_str()) < 0;
    });
    return subNodes;
}

std::wstring FileEngine::GetUndoStagingDirForPath(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') {
        std::wstring root = path.substr(0, 2) + L"\\";
        return root + L".explorer2_undo";
    }
    return L"";
}

void FileEngine::EnqueueDelete(const std::vector<std::wstring>& paths, bool permanent) {
    if (paths.empty()) return;

    if (!permanent) {
        UndoDeleteBatch batch;
        static std::atomic<uint64_t> s_undoCounter{ 1 };

        for (const auto& origPath : paths) {
            DWORD attrs = GetFileAttributesW(origPath.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring stagingDir = GetUndoStagingDirForPath(origPath);
                if (!stagingDir.empty()) {
                    if (GetFileAttributesW(stagingDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
                        if (CreateDirectoryW(stagingDir.c_str(), nullptr)) {
                            SetFileAttributesW(stagingDir.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
                            std::lock_guard<std::mutex> lock(m_undoMutex);
                            m_stagingDirsCreated.push_back(stagingDir);
                        }
                    }
                    uint64_t id = s_undoCounter.fetch_add(1);
                    std::wstring baseName = PathUtils::GetFileName(origPath);
                    std::wstring backupPath = stagingDir + L"\\undo_" + std::to_wstring(id) + L"_" + baseName;
                    if (CreateHardLinkW(backupPath.c_str(), origPath.c_str(), nullptr)) {
                        batch.originalToTrashPaths.push_back({ origPath, backupPath });
                    }
                }
            }
        }

        if (!batch.originalToTrashPaths.empty()) {
            std::lock_guard<std::mutex> lock(m_undoMutex);
            m_undoStack.push_back(std::move(batch));
            if (m_undoStack.size() > 64) {
                for (const auto& pair : m_undoStack.front().originalToTrashPaths) {
                    DeletePathPermanently(pair.second);
                }
                m_undoStack.erase(m_undoStack.begin());
            }
        }
    }

    {
        // Hide the items from listings until the background thread has really removed them
        std::lock_guard<std::mutex> stateLock(m_deleteStateMutex);
        for (const auto& p : paths) m_pendingDeleteKeys.insert(PathUtils::NormalizePathKey(p));
        ++m_activeDeleteBatches;
    }
    {
        std::lock_guard<std::mutex> lock(m_deleteMutex);
        m_deleteQueue.push_back(DeleteTask{ paths, permanent });
    }
    m_deleteCv.notify_one();
}

std::vector<DeleteFailure> FileEngine::TakeDeleteFailures() {
    std::lock_guard<std::mutex> lock(m_deleteStateMutex);
    std::vector<DeleteFailure> out;
    out.swap(m_deleteFailures);
    return out;
}

size_t FileEngine::PendingDeleteBatches() const {
    std::lock_guard<std::mutex> lock(m_deleteStateMutex);
    return m_activeDeleteBatches;
}

void FileEngine::AddPendingHiddenPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_deleteStateMutex);
    m_pendingDeleteKeys.insert(PathUtils::NormalizePathKey(path));
}

void FileEngine::RemovePendingHiddenPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_deleteStateMutex);
    m_pendingDeleteKeys.erase(PathUtils::NormalizePathKey(path));
}

bool FileEngine::UndoLastDelete(std::wstring& outRestoredDir) {
    UndoDeleteBatch batch;
    {
        std::lock_guard<std::mutex> lock(m_undoMutex);
        if (m_undoStack.empty()) return false;
        batch = std::move(m_undoStack.back());
        m_undoStack.pop_back();
    }

    bool anyRestored = false;
    for (const auto& pair : batch.originalToTrashPaths) {
        const std::wstring& origPath = pair.first;
        const std::wstring& backupPath = pair.second;
        if (MoveFileExW(backupPath.c_str(), origPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            anyRestored = true;
            if (outRestoredDir.empty()) {
                outRestoredDir = PathUtils::GetParentPath(origPath);
            }
        }
    }
    return anyRestored;
}

void FileEngine::CleanupAllUndoStaging() {
    std::lock_guard<std::mutex> lock(m_undoMutex);
    for (const auto& batch : m_undoStack) {
        for (const auto& pair : batch.originalToTrashPaths) {
            DeletePathPermanently(pair.second);
        }
    }
    m_undoStack.clear();
    for (const auto& dir : m_stagingDirsCreated) {
        RemoveDirectoryW(dir.c_str());
    }
    m_stagingDirsCreated.clear();
}

namespace {

// Extended-length form (\\?\C:\... / \\?\UNC\server\share\...) so deep trees (> MAX_PATH) and names with
// trailing dots / spaces can be deleted too.
std::wstring ToExtendedPath(const std::wstring& p) {
    if (p.rfind(L"\\\\?\\", 0) == 0) return p;
    if (p.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + p.substr(2);
    if (p.size() >= 2 && p[1] == L':') return L"\\\\?\\" + p;
    return p;
}

bool IsTransientDeleteError(DWORD e) {
    return e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED || e == ERROR_LOCK_VIOLATION || e == ERROR_DIR_NOT_EMPTY;
}

} // namespace

bool FileEngine::DeletePathPermanently(const std::wstring& path, std::vector<DeleteFailure>* failures) {
    const std::wstring ext = ToExtendedPath(path);
    auto fail = [&](DWORD err) {
        if (failures) failures->push_back({ path, err });
        return false;
    };

    DWORD attrs = GetFileAttributesW(ext.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return true;   // already gone
        return fail(e);
    }

    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        bool childrenOk = true;
        // Never descend into junctions / symlinked folders: remove the link itself, not the target's contents
        if (!(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
            std::wstring pattern = PathUtils::EnsureTrailingSlash(ext) + L"*";
            WIN32_FIND_DATAW ffd;
            HANDLE hFind = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (ffd.cFileName[0] == L'.' &&
                        (ffd.cFileName[1] == L'\0' || (ffd.cFileName[1] == L'.' && ffd.cFileName[2] == L'\0'))) {
                        continue;
                    }
                    std::wstring child = PathUtils::EnsureTrailingSlash(path) + ffd.cFileName;
                    if (!DeletePathPermanently(child, failures)) childrenOk = false;
                } while (FindNextFileW(hFind, &ffd));
                FindClose(hFind);
            } else {
                DWORD e = GetLastError();
                if (e != ERROR_FILE_NOT_FOUND) return fail(e);   // cannot even list it (permissions)
            }
        }
        if (!childrenOk) return false;   // the failing children were already reported; the folder cannot be removed

        if (attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)) {
            SetFileAttributesW(ext.c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        DWORD err = 0;
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (RemoveDirectoryW(ext.c_str())) return true;
            err = GetLastError();
            if (attempt < 2 && IsTransientDeleteError(err)) Sleep(120); else break;   // AV / indexer may hold it briefly
        }
        return fail(err);
    }

    DWORD err = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        HANDLE hFile = CreateFileW(
            ext.c_str(),
            DELETE | FILE_WRITE_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);

        if (hFile != INVALID_HANDLE_VALUE) {
            MY_FILE_DISPOSITION_INFO_EX dispEx = {};
            dispEx.Flags = FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS | FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE;
            BOOL ok = SetFileInformationByHandle(hFile, static_cast<FILE_INFO_BY_HANDLE_CLASS>(21), &dispEx, sizeof(dispEx));
            if (!ok) {
                FILE_DISPOSITION_INFO disp = { TRUE };
                ok = SetFileInformationByHandle(hFile, FileDispositionInfo, &disp, sizeof(disp));
            }
            DWORD setErr = ok ? 0 : GetLastError();
            CloseHandle(hFile);
            if (ok) return true;
            err = setErr;
        } else {
            err = GetLastError();
        }

        if (attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)) {
            SetFileAttributesW(ext.c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        if (DeleteFileW(ext.c_str())) return true;
        err = GetLastError();
        if (attempt < 2 && IsTransientDeleteError(err)) Sleep(120); else break;
    }
    return fail(err);
}

void FileEngine::DeleteWorkerLoop() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    while (m_running) {
        DeleteTask task;
        {
            std::unique_lock<std::mutex> lock(m_deleteMutex);
            m_deleteCv.wait(lock, [this]() { return !m_running || !m_deleteQueue.empty(); });
            if (!m_running && m_deleteQueue.empty()) break;
            task = std::move(m_deleteQueue.front());
            m_deleteQueue.pop_front();
        }

        std::vector<DeleteFailure> failures;
        if (task.permanent) {
            for (const auto& p : task.paths) {
                DeletePathPermanently(p, &failures);
            }
        } else {
            std::vector<wchar_t> buf;
            for (const auto& p : task.paths) {
                buf.insert(buf.end(), p.begin(), p.end());
                buf.push_back(L'\0');
            }
            buf.push_back(L'\0');

            SHFILEOPSTRUCTW fileOp = {};
            fileOp.wFunc = FO_DELETE;
            fileOp.pFrom = buf.data();
            fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
            SHFileOperationW(&fileOp);

            // The shell API reports its own legacy codes; verify by checking what is still there
            for (const auto& p : task.paths) {
                if (GetFileAttributesW(ToExtendedPath(p).c_str()) != INVALID_FILE_ATTRIBUTES) failures.push_back({ p, 0 });
            }
        }

        {
            std::lock_guard<std::mutex> stateLock(m_deleteStateMutex);
            for (const auto& p : task.paths) m_pendingDeleteKeys.erase(PathUtils::NormalizePathKey(p));
            if (m_activeDeleteBatches > 0) --m_activeDeleteBatches;
            for (auto& f : failures) m_deleteFailures.push_back(std::move(f));
        }

        if (m_notifyHwnd) {
            PostMessageW(m_notifyHwnd, WM_APP_DELETE_DONE, task.permanent ? 1 : 0, 0);
        }
    }

    CoUninitialize();
}

bool FileEngine::CopyFilesToClipboard(HWND hwnd, const std::vector<std::wstring>& paths, bool isCut) {
    if (paths.empty()) return false;

    size_t totalChars = 1;
    for (const auto& p : paths) {
        totalChars += p.size() + 1;
    }

    size_t dropBytes = sizeof(DROPFILES) + totalChars * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, dropBytes);
    if (!hGlobal) return false;

    auto* df = static_cast<DROPFILES*>(GlobalLock(hGlobal));
    if (!df) {
        GlobalFree(hGlobal);
        return false;
    }
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;

    wchar_t* dst = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES));
    for (const auto& p : paths) {
        memcpy(dst, p.c_str(), (p.size() + 1) * sizeof(wchar_t));
        dst += p.size() + 1;
    }
    *dst = L'\0';
    GlobalUnlock(hGlobal);

    UINT cfDropEffect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    HGLOBAL hEffect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (hEffect) {
        DWORD* pEffect = static_cast<DWORD*>(GlobalLock(hEffect));
        if (pEffect) {
            *pEffect = isCut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
            GlobalUnlock(hEffect);
        }
    }

    if (!OpenClipboard(hwnd)) {
        GlobalFree(hGlobal);
        if (hEffect) GlobalFree(hEffect);
        return false;
    }

    EmptyClipboard();
    SetClipboardData(CF_HDROP, hGlobal);
    if (hEffect) {
        SetClipboardData(cfDropEffect, hEffect);
    }
    CloseClipboard();
    return true;
}

bool FileEngine::PasteFilesFromClipboard(HWND hwnd, const std::wstring& targetDir, bool& outWasCut) {
    outWasCut = false;
    if (targetDir.empty() || targetDir == L"THIS_PC") return false;
    if (!OpenClipboard(hwnd)) return false;

    bool isMove = false;
    UINT cfDropEffect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    HANDLE hEffect = GetClipboardData(cfDropEffect);
    if (hEffect) {
        DWORD* pEffect = static_cast<DWORD*>(GlobalLock(hEffect));
        if (pEffect) {
            isMove = (*pEffect & DROPEFFECT_MOVE) != 0;
            GlobalUnlock(hEffect);
        }
    }

    HDROP hDrop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    std::vector<std::wstring> srcPaths;
    if (hDrop) {
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
            std::wstring path(len, L'\0');
            DragQueryFileW(hDrop, i, path.data(), len + 1);
            srcPaths.push_back(std::move(path));
        }
    }
    CloseClipboard();

    if (srcPaths.empty()) return false;

    std::vector<wchar_t> fromBuf;
    for (const auto& p : srcPaths) {
        fromBuf.insert(fromBuf.end(), p.begin(), p.end());
        fromBuf.push_back(L'\0');
    }
    fromBuf.push_back(L'\0');

    std::vector<wchar_t> toBuf(targetDir.begin(), targetDir.end());
    toBuf.push_back(L'\0');
    toBuf.push_back(L'\0');

    SHFILEOPSTRUCTW op = {};
    op.hwnd = hwnd;
    op.wFunc = isMove ? FO_MOVE : FO_COPY;
    op.pFrom = fromBuf.data();
    op.pTo = toBuf.data();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR | FOF_RENAMEONCOLLISION;
    int res = SHFileOperationW(&op);
    outWasCut = isMove;
    return (res == 0 && !op.fAnyOperationsAborted);
}

bool FileEngine::CopyTextToClipboard(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return false;
    EmptyClipboard();

    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* ptr = GlobalLock(hMem);
        if (ptr) {
            memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
    return true;
}

bool FileEngine::CreateNewFolder(const std::wstring& parentDir, const std::wstring& baseName, std::wstring& outCreatedPath) {
    if (parentDir.empty() || parentDir == L"THIS_PC") return false;
    std::wstring cleanParent = PathUtils::EnsureTrailingSlash(parentDir);

    for (int i = 1; i < 500; ++i) {
        std::wstring candidate = cleanParent + baseName;
        if (i > 1) {
            candidate += L" (" + std::to_wstring(i) + L")";
        }
        if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (CreateDirectoryW(candidate.c_str(), nullptr)) {
                outCreatedPath = candidate;
                return true;
            }
            return false;
        }
    }
    return false;
}

namespace {

bool MoveFileWithRetry(const std::wstring& srcPath, const std::wstring& dstPath, bool isDirectory, bool replaceExisting, DWORD* outError) {
    if (outError) *outError = 0;
    const std::wstring extSrc = ToExtendedPath(srcPath);
    const std::wstring extDst = ToExtendedPath(dstPath);
    // Directory moves on Windows must use flags = 0; file moves can use MOVEFILE_COPY_ALLOWED / MOVEFILE_REPLACE_EXISTING
    const DWORD flags = isDirectory ? 0 : (MOVEFILE_COPY_ALLOWED | (replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0));

    DWORD lastErr = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (MoveFileExW(extSrc.c_str(), extDst.c_str(), flags)) return true;
        lastErr = GetLastError();
        if (replaceExisting && !isDirectory && (lastErr == ERROR_ACCESS_DENIED || lastErr == ERROR_SHARING_VIOLATION)) {
            DWORD dstAttrs = GetFileAttributesW(extDst.c_str());
            if (dstAttrs != INVALID_FILE_ATTRIBUTES && (dstAttrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) {
                SetFileAttributesW(extDst.c_str(), FILE_ATTRIBUTE_NORMAL);
            }
        }
        if (lastErr == ERROR_ACCESS_DENIED) {
            DWORD srcAttrs = GetFileAttributesW(extSrc.c_str());
            if (srcAttrs != INVALID_FILE_ATTRIBUTES && (srcAttrs & FILE_ATTRIBUTE_READONLY)) {
                SetFileAttributesW(extSrc.c_str(), srcAttrs & ~FILE_ATTRIBUTE_READONLY);
            }
        }
        if (attempt < 7 && (lastErr == ERROR_SHARING_VIOLATION || lastErr == ERROR_LOCK_VIOLATION || lastErr == ERROR_ACCESS_DENIED)) {
            Sleep(35 * (attempt + 1));
            continue;
        }
        break;
    }
    if (outError) *outError = lastErr;
    return false;
}

std::wstring NextFreeCollisionPath(const std::wstring& dstDir, const std::wstring& fileName) {
    std::wstring stem = fileName;
    std::wstring ext;
    size_t dot = fileName.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        stem = fileName.substr(0, dot);
        ext = fileName.substr(dot);
    }
    std::wstring base = PathUtils::EnsureTrailingSlash(dstDir);
    for (int idx = 2; idx < 10000; ++idx) {
        std::wstring cand = base + stem + L" (" + std::to_wstring(idx) + L")" + ext;
        if (GetFileAttributesW(ToExtendedPath(cand).c_str()) == INVALID_FILE_ATTRIBUTES) return cand;
    }
    return base + stem + L"_" + std::to_wstring(GetTickCount64()) + ext;
}

bool MergeDirectoriesRecursive(const std::wstring& srcDir, const std::wstring& dstDir, std::atomic<int>* progressCounter, MergeFolderResult& outResult) {
    const std::wstring extSrcDir = ToExtendedPath(srcDir);
    const std::wstring extDstDir = ToExtendedPath(dstDir);

    CreateDirectoryW(extDstDir.c_str(), nullptr);

    std::vector<WIN32_FIND_DATAW> entries;
    {
        std::wstring pattern = PathUtils::EnsureTrailingSlash(extSrcDir) + L"*";
        WIN32_FIND_DATAW ffd = {};
        HANDLE hFind = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (hFind == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND) {
                outResult.failures.push_back({ srcDir, e });
                return false;
            }
        } else {
            do {
                if (ffd.cFileName[0] == L'.' &&
                    (ffd.cFileName[1] == L'\0' || (ffd.cFileName[1] == L'.' && ffd.cFileName[2] == L'\0'))) {
                    continue;
                }
                entries.push_back(ffd);
            } while (FindNextFileW(hFind, &ffd));
            FindClose(hFind);   // close handle before moving or removing anything in srcDir
        }
    }

    bool allChildrenOk = true;
    for (const auto& ffd : entries) {
        const std::wstring childName = ffd.cFileName;
        const std::wstring srcChild = PathUtils::EnsureTrailingSlash(srcDir) + childName;
        const std::wstring dstChild = PathUtils::EnsureTrailingSlash(dstDir) + childName;
        const bool isSrcDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool isSrcReparse = (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        const std::wstring extDstChild = ToExtendedPath(dstChild);
        const DWORD dstAttrs = GetFileAttributesW(extDstChild.c_str());

        if (dstAttrs == INVALID_FILE_ATTRIBUTES) {
            // Fast path: move the file or the entire subfolder tree in O(1) metadata time
            DWORD err = 0;
            if (MoveFileWithRetry(srcChild, dstChild, isSrcDir, false, &err)) {
                ++outResult.movedItems;
                if (progressCounter) ++(*progressCounter);
                continue;
            }
            if (isSrcDir && !isSrcReparse) {
                // Fallback if a handle inside prevented whole-directory move: merge contents recursively
                if (!MergeDirectoriesRecursive(srcChild, dstChild, progressCounter, outResult)) {
                    allChildrenOk = false;
                }
                continue;
            }
            outResult.failures.push_back({ srcChild, err });
            allChildrenOk = false;
            continue;
        }

        const bool isDstDir = (dstAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isSrcDir && !isSrcReparse && isDstDir) {
            if (!MergeDirectoriesRecursive(srcChild, dstChild, progressCounter, outResult)) {
                allChildrenOk = false;
            }
            continue;
        }

        if (!isSrcDir && !isDstDir) {
            // Shell cache files in source can be discarded if destination already has them
            if (_wcsicmp(childName.c_str(), L"Thumbs.db") == 0 || _wcsicmp(childName.c_str(), L"desktop.ini") == 0) {
                const std::wstring extSrcChild = ToExtendedPath(srcChild);
                SetFileAttributesW(extSrcChild.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (DeleteFileW(extSrcChild.c_str())) continue;
            }

            // Check if the two files have identical size and last-write timestamp
            WIN32_FILE_ATTRIBUTE_DATA dstData = {};
            bool identical = false;
            if (GetFileAttributesExW(extDstChild.c_str(), GetFileExInfoStandard, &dstData)) {
                identical = (dstData.nFileSizeHigh == ffd.nFileSizeHigh &&
                             dstData.nFileSizeLow == ffd.nFileSizeLow &&
                             CompareFileTime(&dstData.ftLastWriteTime, &ffd.ftLastWriteTime) == 0);
            }
            if (identical) {
                DWORD err = 0;
                if (MoveFileWithRetry(srcChild, dstChild, false, true, &err)) {
                    ++outResult.movedItems;
                    if (progressCounter) ++(*progressCounter);
                    continue;
                }
            }

            // Different files with the same name: keep both by saving as "name (2).ext" so zero data is lost
            const std::wstring freeDst = NextFreeCollisionPath(dstDir, childName);
            DWORD err = 0;
            if (MoveFileWithRetry(srcChild, freeDst, false, false, &err)) {
                ++outResult.movedItems;
                ++outResult.renamedCollisions;
                if (progressCounter) ++(*progressCounter);
                if (outResult.collisionNotes.size() < 20) {
                    outResult.collisionNotes.push_back(childName + L" -> " + PathUtils::GetFileName(freeDst));
                }
            } else {
                outResult.failures.push_back({ srcChild, err });
                allChildrenOk = false;
            }
            continue;
        }

        // File vs folder name clash or reparse point: move under a non-colliding name
        const std::wstring freeDst = NextFreeCollisionPath(dstDir, childName);
        DWORD err = 0;
        if (MoveFileWithRetry(srcChild, freeDst, isSrcDir, false, &err)) {
            ++outResult.movedItems;
            ++outResult.renamedCollisions;
            if (progressCounter) ++(*progressCounter);
        } else {
            outResult.failures.push_back({ srcChild, err });
            allChildrenOk = false;
        }
    }

    if (!allChildrenOk) return false;

    DWORD srcAttrs = GetFileAttributesW(extSrcDir.c_str());
    if (srcAttrs == INVALID_FILE_ATTRIBUTES) return true;
    if (srcAttrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)) {
        SetFileAttributesW(extSrcDir.c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    DWORD rmErr = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (RemoveDirectoryW(extSrcDir.c_str())) return true;
        rmErr = GetLastError();
        if (rmErr == ERROR_FILE_NOT_FOUND || rmErr == ERROR_PATH_NOT_FOUND) return true;
        if (attempt < 7 && IsTransientDeleteError(rmErr)) Sleep(40 * (attempt + 1));
        else break;
    }
    outResult.failures.push_back({ srcDir, rmErr });
    return false;
}

} // namespace

bool FileEngine::RenameFileOrFolder(const std::wstring& oldPath, const std::wstring& newName, std::wstring& outNewPath, DWORD* outError) {
    if (outError) *outError = 0;
    if (newName.empty()) { if (outError) *outError = ERROR_INVALID_NAME; return false; }
    std::wstring parent = PathUtils::GetParentPath(oldPath);
    if (parent.empty()) { if (outError) *outError = ERROR_BAD_PATHNAME; return false; }

    outNewPath = PathUtils::EnsureTrailingSlash(parent) + newName;
    DWORD srcAttrs = GetFileAttributesW(ToExtendedPath(oldPath).c_str());
    bool isDir = (srcAttrs != INVALID_FILE_ATTRIBUTES) && ((srcAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
    return MoveFileWithRetry(oldPath, outNewPath, isDir, false, outError);
}

void FileEngine::MergeDirectoryContents(const std::wstring& srcDir, const std::wstring& dstDir, std::atomic<int>* progressCounter, MergeFolderResult& outResult) {
    if (_wcsicmp(PathUtils::NormalizePathKey(srcDir).c_str(), PathUtils::NormalizePathKey(dstDir).c_str()) == 0) return;
    MergeDirectoriesRecursive(srcDir, dstDir, progressCounter, outResult);
    outResult.srcRemoved = (GetFileAttributesW(ToExtendedPath(srcDir).c_str()) == INVALID_FILE_ATTRIBUTES);
}

void FileEngine::OpenFileOrProperties(HWND hwnd, const std::wstring& path, bool propertiesOnly) {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.hwnd = hwnd;
    sei.lpVerb = propertiesOnly ? L"properties" : L"open";
    sei.lpFile = path.c_str();
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = propertiesOnly ? SEE_MASK_INVOKEIDLIST : 0;
    ShellExecuteExW(&sei);
}

void FileEngine::OpenInClassicExplorer(const std::wstring& path, bool selectItem) {
    std::wstring params = selectItem ? (L"/select,\"" + path + L"\"") : (L"\"" + path + L"\"");
    ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
}

void FileEngine::OpenTerminalInFolder(const std::wstring& dirPath) {
    if (dirPath.empty() || dirPath == L"THIS_PC") return;
    ShellExecuteW(nullptr, L"open", L"powershell.exe", nullptr, dirPath.c_str(), SW_SHOWNORMAL);
}

std::wstring FileEngine::FormatSizeKB(uint64_t bytes) {
    uint64_t kb = (bytes == 0) ? 0 : ((bytes + 1023) / 1024);
    std::wstring raw = std::to_wstring(kb);
    std::wstring formatted;
    int count = 0;
    for (int i = static_cast<int>(raw.size()) - 1; i >= 0; --i) {
        if (count > 0 && (count % 3) == 0) {
            formatted.insert(formatted.begin(), L' ');
        }
        formatted.insert(formatted.begin(), raw[i]);
        ++count;
    }
    return formatted + L" KB";
}

std::wstring FileEngine::FormatSizeDetailed(uint64_t bytes) {
    wchar_t buf[64] = {};
    if (bytes >= (1ULL << 30)) {
        double gb = static_cast<double>(bytes) / static_cast<double>(1ULL << 30);
        swprintf_s(buf, L"%.2f GB", gb);
    } else if (bytes >= (1ULL << 20)) {
        double mb = static_cast<double>(bytes) / static_cast<double>(1ULL << 20);
        swprintf_s(buf, L"%.1f MB", mb);
    } else {
        return FormatSizeKB(bytes);
    }
    return buf;
}

std::wstring FileEngine::FormatFileTime(const FILETIME& ft) {
    FILETIME localFt = {};
    SYSTEMTIME st = {};
    FileTimeToLocalFileTime(&ft, &localFt);
    FileTimeToSystemTime(&localFt, &st);
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%02d.%02d.%04d %02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
    return buf;
}

bool FileEngine::IsAnyFolderSizeCalculating() const {
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    return !m_sizeQueue.empty() || m_activeSizeWorkers.load(std::memory_order_relaxed) > 0;
}

bool FileEngine::IsFolderInFlight(const std::wstring& normKey) const {
    if (normKey.empty()) return false;
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    return m_folderSizeInFlight.find(normKey) != m_folderSizeInFlight.end();
}

std::wstring FileEngine::GetActiveScanningSamplePath() const {
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    return m_currentScanSample;
}

bool FileEngine::HasAnyCachedFolderSizes(const std::vector<FileEntry>& entries) const {
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    for (const auto& e : entries) {
        if (e.isDirectory && m_folderSizeCache.find(e.normKey) != m_folderSizeCache.end()) {
            return true;
        }
    }
    return false;
}

void FileEngine::SyncDirectorySizes(std::vector<FileEntry>& entries) {
    m_sizeNotifyPending.store(false, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    for (auto& item : entries) {
        if (!item.isDirectory || item.normKey.empty()) continue;
        auto it = m_folderSizeCache.find(item.normKey);
        if (it != m_folderSizeCache.end()) {
            item.size = it->second;
            item.sizeStr = FormatSizeDetailed(it->second);
        } else if (m_folderSizeInFlight.find(item.normKey) != m_folderSizeInFlight.end()) {
            item.sizeStr = L"...";
        }
    }
}

bool FileEngine::GetFolderTreemapChildren(const std::wstring& normKey, std::vector<TreemapChildNode>& outChildren) const {
    if (normKey.empty()) return false;
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    auto it = m_folderChildrenCache.find(normKey);
    if (it == m_folderChildrenCache.end() || it->second.empty()) {
        return false;
    }
    outChildren = it->second;
    return true;
}

void FileEngine::RequestFolderSizes(const std::vector<FileEntry>& entries, bool forceRefresh) {
    uint64_t gen = m_sizeGeneration.load(std::memory_order_relaxed);
    if (forceRefresh) {
        // Increment generation so any in-progress recursive scans from the previous click abort immediately!
        gen = m_sizeGeneration.fetch_add(1) + 1;
    }

    {
        std::lock_guard<std::mutex> lock(m_sizeMutex);
        if (forceRefresh) {
            m_sizeQueue.clear();
            m_folderSizeInFlight.clear();
            for (const auto& e : entries) {
                if (!e.isDirectory || e.normKey.empty()) continue;
                std::wstring prefix = e.normKey + L"\\";
                for (auto it = m_folderSizeCache.begin(); it != m_folderSizeCache.end(); ) {
                    if (it->first == e.normKey || it->first.rfind(prefix, 0) == 0) {
                        it = m_folderSizeCache.erase(it);
                    } else {
                        ++it;
                    }
                }
                for (auto it = m_folderChildrenCache.begin(); it != m_folderChildrenCache.end(); ) {
                    if (it->first == e.normKey || it->first.rfind(prefix, 0) == 0) {
                        it = m_folderChildrenCache.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }

        for (const auto& e : entries) {
            if (!e.isDirectory || e.normKey.empty()) continue;
            if (!forceRefresh &&
                m_folderSizeCache.find(e.normKey) != m_folderSizeCache.end() &&
                m_folderChildrenCache.find(e.normKey) != m_folderChildrenCache.end()) {
                continue;
            }
            if (m_folderSizeInFlight.find(e.normKey) != m_folderSizeInFlight.end()) {
                continue;
            }
            m_folderSizeInFlight[e.normKey] = true;
            m_sizeQueue.push_back(FolderSizeTask{ e.fullPath, e.normKey, gen });
        }
    }
    m_sizeCv.notify_all();
}

uint64_t FileEngine::ComputeFolderSizeRecursive(const std::wstring& dirPath, int depth, uint64_t generation, bool& outAborted) {
    if (!m_running || generation != m_sizeGeneration.load(std::memory_order_relaxed)) {
        outAborted = true;
        return 0;
    }

    if (depth <= 2) {
        std::lock_guard<std::mutex> lock(m_sizeMutex);
        m_currentScanSample = dirPath;
    }

    std::wstring prefix = PathUtils::EnsureTrailingSlash(dirPath);
    std::wstring pattern = prefix + L"*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &ffd,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);

    if (hFind == INVALID_HANDLE_VALUE) {
        return 0;
    }

    uint64_t totalBytes = 0;
    std::vector<TreemapChildNode> directChildren;
    if (depth <= 4) {
        directChildren.reserve(64);
    }
    std::wstring dirKey = (depth <= 4) ? PathUtils::NormalizePathKey(dirPath) : L"";

    do {
        if (!m_running || generation != m_sizeGeneration.load(std::memory_order_relaxed)) {
            outAborted = true;
            break;
        }
        const wchar_t* name = ffd.cFileName;
        if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) {
            continue;
        }
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            continue;
        }

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring childDir = prefix + name;
            uint64_t subBytes = ComputeFolderSizeRecursive(childDir, depth + 1, generation, outAborted);
            if (outAborted) break;
            totalBytes += subBytes;

            std::wstring subKey;
            if (depth <= 4) {
                subKey = PathUtils::NormalizePathKey(childDir);
                std::lock_guard<std::mutex> lock(m_sizeMutex);
                m_folderSizeCache[subKey] = subBytes;
                if (depth == 0) {
                    // Stream partial size of top-level folder while scanning its subfolders!
                    m_folderSizeCache[dirKey] = totalBytes;
                }
            }
            if (depth <= 4 && subBytes > 0) {
                TreemapChildNode cn;
                cn.name = name;
                cn.fullPath = std::move(childDir);
                cn.normKey = std::move(subKey);
                cn.size = subBytes;
                cn.isDirectory = true;
                directChildren.push_back(std::move(cn));

                // Periodically publish partial treemap children for depth 0/1 so huge folders populate live!
                if (depth <= 1) {
                    uint64_t nowTick = GetTickCount64();
                    uint64_t prevTick = m_lastPartialNotifyTick.load(std::memory_order_relaxed);
                    if (nowTick - prevTick >= 150 &&
                        m_lastPartialNotifyTick.compare_exchange_strong(prevTick, nowTick, std::memory_order_relaxed)) {
                        std::vector<TreemapChildNode> snapshot = directChildren;
                        std::sort(snapshot.begin(), snapshot.end(),
                            [](const TreemapChildNode& a, const TreemapChildNode& b) { return a.size > b.size; });
                        if (snapshot.size() > 48) snapshot.resize(48);
                        {
                            std::lock_guard<std::mutex> lock(m_sizeMutex);
                            m_folderChildrenCache[dirKey] = std::move(snapshot);
                        }
                        if (m_notifyHwnd && !m_sizeNotifyPending.exchange(true, std::memory_order_relaxed)) {
                            PostMessageW(m_notifyHwnd, WM_APP_FOLDER_SIZE_UPDATED, 0, 0);
                        }
                    }
                }
            }
        } else {
            uint64_t fsize = (static_cast<uint64_t>(ffd.nFileSizeHigh) << 32) | ffd.nFileSizeLow;
            totalBytes += fsize;
            if (depth <= 4 && fsize > 0) {
                TreemapChildNode cn;
                cn.name = name;
                cn.fullPath = prefix + name;
                const wchar_t* dot = wcsrchr(name, L'.');
                if (dot) cn.ext = PathUtils::ToLower(dot);
                cn.size = fsize;
                cn.isDirectory = false;
                directChildren.push_back(std::move(cn));
            }
        }
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);

    if (!outAborted && depth <= 4 && !directChildren.empty()) {
        std::sort(directChildren.begin(), directChildren.end(),
            [](const TreemapChildNode& a, const TreemapChildNode& b) { return a.size > b.size; });

        constexpr size_t kMaxTreemapChildren = 52;
        if (directChildren.size() > kMaxTreemapChildren) {
            uint64_t restBytes = 0;
            size_t restCount = directChildren.size() - (kMaxTreemapChildren - 1);
            for (size_t i = kMaxTreemapChildren - 1; i < directChildren.size(); ++i) {
                restBytes += directChildren[i].size;
            }
            directChildren.resize(kMaxTreemapChildren - 1);
            if (restBytes > 0) {
                TreemapChildNode otherNode;
                otherNode.name = L"[+" + std::to_wstring(restCount) + L" mniejszych]";
                otherNode.fullPath = dirPath;
                otherNode.ext = L".other";
                otherNode.size = restBytes;
                otherNode.isDirectory = false;
                directChildren.push_back(std::move(otherNode));
            }
        }

        std::lock_guard<std::mutex> lock(m_sizeMutex);
        m_folderChildrenCache[dirKey] = std::move(directChildren);
    }

    return totalBytes;
}

void FileEngine::FolderSizeWorkerLoop() {
    while (m_running) {
        FolderSizeTask task;
        {
            std::unique_lock<std::mutex> lock(m_sizeMutex);
            m_sizeCv.wait(lock, [this]() { return !m_running || !m_sizeQueue.empty(); });
            if (!m_running) break;

            task = std::move(m_sizeQueue.front());
            m_sizeQueue.pop_front();
            m_activeSizeWorkers.fetch_add(1, std::memory_order_relaxed);
        }

        bool aborted = false;
        uint64_t totalBytes = ComputeFolderSizeRecursive(task.path, 0, task.generation, aborted);

        if (m_running && !aborted && task.generation == m_sizeGeneration.load(std::memory_order_relaxed)) {
            {
                std::lock_guard<std::mutex> lock(m_sizeMutex);
                m_folderSizeCache[task.normKey] = totalBytes;
                m_folderSizeInFlight.erase(task.normKey);
            }
            if (m_notifyHwnd && !m_sizeNotifyPending.exchange(true, std::memory_order_relaxed)) {
                PostMessageW(m_notifyHwnd, WM_APP_FOLDER_SIZE_UPDATED, 0, 0);
            }
        }
        if (m_activeSizeWorkers.fetch_sub(1, std::memory_order_relaxed) == 1 && m_notifyHwnd) {
            // Ensure final repaint when last active worker finishes
            PostMessageW(m_notifyHwnd, WM_APP_FOLDER_SIZE_UPDATED, 0, 0);
        }
    }
}

namespace {

bool FastSubstrMatch(const wchar_t* hay, const std::wstring& needle, bool caseSensitive) {
    if (needle.empty()) return true;
    size_t nLen = needle.size();
    const wchar_t* nPtr = needle.c_str();
    if (caseSensitive) {
        return wcsstr(hay, nPtr) != nullptr;
    }
    wchar_t firstLo = static_cast<wchar_t>(towlower(nPtr[0]));
    for (const wchar_t* p = hay; *p != L'\0'; ++p) {
        if (static_cast<wchar_t>(towlower(*p)) == firstLo) {
            bool match = true;
            for (size_t i = 1; i < nLen; ++i) {
                wchar_t hc = p[i];
                if (hc == L'\0' || static_cast<wchar_t>(towlower(hc)) != nPtr[i]) {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
    }
    return false;
}

uint32_t ClassifyExtCategory(const std::wstring& extLo) {
    if (extLo.empty()) return SEARCH_CAT_NONE;
    if (IsImageExtension(extLo)) return SEARCH_CAT_IMAGES;
    if (IsVideoExtension(extLo) || IsAudioExtension(extLo)) return SEARCH_CAT_MEDIA;
    if (extLo == L".cs" || extLo == L".cpp" || extLo == L".h" || extLo == L".hpp" || extLo == L".c" ||
        extLo == L".py" || extLo == L".js" || extLo == L".ts" || extLo == L".json" || extLo == L".xml" ||
        extLo == L".html" || extLo == L".css" || extLo == L".md" || extLo == L".txt" || extLo == L".ini" ||
        extLo == L".yaml" || extLo == L".yml" || extLo == L".shader" || extLo == L".hlsl" || extLo == L".glsl" ||
        extLo == L".sql" || extLo == L".log" || extLo == L".cmake" || extLo == L".sln" || extLo == L".vcxproj") {
        return SEARCH_CAT_CODE;
    }
    if (extLo == L".zip" || extLo == L".7z" || extLo == L".rar" || extLo == L".tar" ||
        extLo == L".gz" || extLo == L".iso" || extLo == L".cab" || extLo == L".tgz") {
        return SEARCH_CAT_ARCHIVES;
    }
    if (extLo == L".safetensors" || extLo == L".ckpt" || extLo == L".onnx" || extLo == L".pt" ||
        extLo == L".pth" || extLo == L".gguf" || extLo == L".bin" || extLo == L".db" || extLo == L".sqlite" ||
        extLo == L".fbx" || extLo == L".obj" || extLo == L".blend" || extLo == L".gltf" || extLo == L".glb") {
        return SEARCH_CAT_MODELS;
    }
    if (extLo == L".exe" || extLo == L".dll" || extLo == L".msi" || extLo == L".bat" ||
        extLo == L".cmd" || extLo == L".ps1" || extLo == L".sys") {
        return SEARCH_CAT_APPS;
    }
    if (extLo == L".pdf" || extLo == L".doc" || extLo == L".docx" || extLo == L".xls" ||
        extLo == L".xlsx" || extLo == L".ppt" || extLo == L".pptx" || extLo == L".rtf" || extLo == L".csv") {
        return SEARCH_CAT_DOCS;
    }
    return SEARCH_CAT_NONE;
}

bool FileContentContainsText(const std::wstring& filePath, const std::wstring& needleLower) {
    if (needleLower.empty()) return true;
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    // Convert needleLower to narrow UTF-8/ASCII lowercase for ultra-fast scan
    std::string asciiNeedle;
    asciiNeedle.reserve(needleLower.size());
    for (wchar_t wc : needleLower) {
        asciiNeedle.push_back(static_cast<char>(towlower(wc) & 0xFF));
    }

    char buf[32768];
    DWORD bytesRead = 0;
    DWORD totalRead = 0;
    constexpr DWORD kMaxScanBytes = 4 * 1024 * 1024; // Up to 4 MB per text file
    bool found = false;

    while (totalRead < kMaxScanBytes && ReadFile(hFile, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
        totalRead += bytesRead;
        for (DWORD i = 0; i < bytesRead; ++i) {
            char c = buf[i];
            if (c >= 'A' && c <= 'Z') buf[i] = c + ('a' - 'A');
        }
        if (bytesRead >= asciiNeedle.size()) {
            auto it = std::search(buf, buf + bytesRead, asciiNeedle.begin(), asciiNeedle.end());
            if (it != buf + bytesRead) {
                found = true;
                break;
            }
        }
    }
    CloseHandle(hFile);
    return found;
}

} // namespace

void FileEngine::StartAsyncSearch(
    const std::wstring& rootDir,
    const SearchOptions& opts,
    bool showHidden,
    bool showExtensions,
    Lang lang)
{
    m_searchGeneration.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        m_searchRootDir = rootDir;
        m_searchOpts = opts;
        m_searchShowHidden = showHidden;
        m_searchShowExtensions = showExtensions;
        m_searchLang = lang;
        m_hasPendingSearchJob = true;
        m_searchAccumResults.clear();
        m_searchProgress = SearchProgressInfo{};
        m_searchProgress.isRunning = true;
        m_searchProgress.currentFolder = rootDir;
        m_searchRunning.store(true, std::memory_order_relaxed);
    }
    m_searchCv.notify_one();
}

void FileEngine::CancelAsyncSearch() {
    m_searchGeneration.fetch_add(1);
    m_searchRunning.store(false, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(m_searchMutex);
    m_hasPendingSearchJob = false;
    m_searchProgress.isRunning = false;
}

bool FileEngine::IsSearchRunning() const {
    return m_searchRunning.load(std::memory_order_relaxed);
}

void FileEngine::SyncSearchResults(std::vector<FileEntry>& outResults, SearchProgressInfo& outProgress) {
    m_searchNotifyPending.store(false, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(m_searchMutex);
    outResults = m_searchAccumResults;
    outProgress = m_searchProgress;
}

void FileEngine::SearchWorkerLoop() {
    while (m_running) {
        std::wstring rootDir;
        SearchOptions opts;
        bool showHidden = true;
        bool showExtensions = true;
        Lang lang = Lang::PL;
        uint64_t myGen = 0;

        {
            std::unique_lock<std::mutex> lock(m_searchMutex);
            m_searchCv.wait(lock, [this]() { return !m_running || m_hasPendingSearchJob; });
            if (!m_running) break;

            m_hasPendingSearchJob = false;
            rootDir = m_searchRootDir;
            opts = m_searchOpts;
            showHidden = m_searchShowHidden;
            showExtensions = m_searchShowExtensions;
            lang = m_searchLang;
            myGen = m_searchGeneration.load(std::memory_order_relaxed);
        }

        if (rootDir.empty() || rootDir == L"THIS_PC") {
            std::lock_guard<std::mutex> lock(m_searchMutex);
            m_searchProgress.isRunning = false;
            m_searchProgress.isFinished = true;
            m_searchRunning.store(false, std::memory_order_relaxed);
            if (m_notifyHwnd) PostMessageW(m_notifyHwnd, WM_APP_SEARCH_UPDATED, 0, 0);
            continue;
        }

        ULONGLONG startTick = GetTickCount64();
        ULONGLONG lastNotifyTick = startTick;

        std::wstring needle = opts.caseSensitive ? opts.query : PathUtils::ToLower(opts.query);
        bool hasWildcard = (needle.find(L'*') != std::wstring::npos || needle.find(L'?') != std::wstring::npos);

        // Parse custom extension list (e.g. ".cs;.cpp;png")
        std::vector<std::wstring> customExts;
        if (opts.useTypeFilter && !opts.customExtList.empty()) {
            std::wstring cur;
            for (wchar_t ch : opts.customExtList) {
                if (ch == L';' || ch == L',' || ch == L' ') {
                    if (!cur.empty()) {
                        if (cur[0] != L'.') cur.insert(cur.begin(), L'.');
                        customExts.push_back(PathUtils::ToLower(cur));
                        cur.clear();
                    }
                } else if (ch != L'*') {
                    cur.push_back(ch);
                }
            }
            if (!cur.empty()) {
                if (cur[0] != L'.') cur.insert(cur.begin(), L'.');
                customExts.push_back(PathUtils::ToLower(cur));
            }
        }

        ULARGE_INTEGER ftMin = {}, ftMax = {};
        ftMin.LowPart = opts.dateFromFt.dwLowDateTime;
        ftMin.HighPart = opts.dateFromFt.dwHighDateTime;
        ftMax.LowPart = opts.dateToFt.dwLowDateTime;
        ftMax.HighPart = opts.dateToFt.dwHighDateTime;

        std::wstring rootClean = PathUtils::EnsureTrailingSlash(PathUtils::TrimTrailingSlashes(rootDir));
        std::deque<std::wstring> dirQueue;
        dirQueue.push_back(PathUtils::TrimTrailingSlashes(rootDir));

        uint64_t scannedFolders = 0;
        uint64_t scannedFiles = 0;
        uint64_t matchedCount = 0;
        std::vector<FileEntry> batchMatches;
        batchMatches.reserve(128);

        while (!dirQueue.empty() && m_running && myGen == m_searchGeneration.load(std::memory_order_relaxed)) {
            std::wstring curDir = std::move(dirQueue.front());
            dirQueue.pop_front();
            ++scannedFolders;

            std::wstring prefix = PathUtils::EnsureTrailingSlash(curDir);
            std::wstring pattern = prefix + L"*";

            WIN32_FIND_DATAW ffd;
            HANDLE hFind = FindFirstFileExW(
                pattern.c_str(),
                FindExInfoBasic,
                &ffd,
                FindExSearchNameMatch,
                nullptr,
                FIND_FIRST_EX_LARGE_FETCH);

            if (hFind == INVALID_HANDLE_VALUE) continue;

            // Relative subfolder label for results display
            std::wstring relFolder;
            if (curDir.size() > rootClean.size() && _wcsnicmp(curDir.c_str(), rootClean.c_str(), rootClean.size()) == 0) {
                relFolder = curDir.substr(rootClean.size());
            }

            do {
                if (!m_running || myGen != m_searchGeneration.load(std::memory_order_relaxed)) {
                    break;
                }
                const wchar_t* name = ffd.cFileName;
                if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) {
                    continue;
                }

                bool isHidden = (ffd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
                if (isHidden && !showHidden) continue;

                bool isDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (isDir) {
                    if (opts.recursive && !(ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                        if (wcscmp(name, L"$RECYCLE.BIN") != 0 && wcscmp(name, L"System Volume Information") != 0) {
                            dirQueue.push_back(prefix + name);
                        }
                    }
                    if (!opts.includeFolders) continue;
                } else {
                    ++scannedFiles;
                    if (!opts.includeFiles) continue;
                }

                // 1. Fast Date Filter (64-bit integer compare before any string allocation)
                if (opts.useDateFilter) {
                    ULARGE_INTEGER ftItem = {};
                    ftItem.LowPart = ffd.ftLastWriteTime.dwLowDateTime;
                    ftItem.HighPart = ffd.ftLastWriteTime.dwHighDateTime;
                    if (opts.hasDateFrom && ftItem.QuadPart < ftMin.QuadPart) continue;
                    if (opts.hasDateTo && ftItem.QuadPart > ftMax.QuadPart) continue;
                }

                // 2. Fast Size Filter (64-bit integer compare before any string allocation)
                uint64_t fsize = isDir ? 0 : ((static_cast<uint64_t>(ffd.nFileSizeHigh) << 32) | ffd.nFileSizeLow);
                if (opts.useSizeFilter && !isDir) {
                    if (fsize < opts.sizeMinBytes || fsize > opts.sizeMaxBytes) continue;
                } else if (opts.useSizeFilter && isDir) {
                    continue; // Size range applies to files
                }

                // 3. Extension / Type Filter
                std::wstring extLo;
                if (!isDir) {
                    const wchar_t* dot = wcsrchr(name, L'.');
                    if (dot) extLo = PathUtils::ToLower(dot);
                }
                if (opts.useTypeFilter) {
                    if (isDir) continue;
                    bool typeOk = false;
                    if (opts.categoryMask != SEARCH_CAT_NONE) {
                        uint32_t cat = ClassifyExtCategory(extLo);
                        if ((cat & opts.categoryMask) != 0) typeOk = true;
                    }
                    if (!typeOk && !customExts.empty()) {
                        for (const auto& ce : customExts) {
                            if (extLo == ce) { typeOk = true; break; }
                        }
                    }
                    if (!typeOk && opts.categoryMask == SEARCH_CAT_NONE && customExts.empty()) {
                        typeOk = true;
                    }
                    if (!typeOk) continue;
                }

                // 4. Query Match (Filename or File Content)
                bool nameMatched = false;
                if (needle.empty()) {
                    nameMatched = true;
                } else if (hasWildcard) {
                    nameMatched = PathMatchSpecW(name, needle.c_str()) != FALSE;
                } else {
                    nameMatched = FastSubstrMatch(name, needle, opts.caseSensitive);
                }

                if (!nameMatched) {
                    if (opts.searchContent && !isDir && !needle.empty() && fsize > 0 && fsize <= 4 * 1024 * 1024) {
                        uint32_t cat = ClassifyExtCategory(extLo);
                        if (cat == SEARCH_CAT_CODE || extLo == L".txt" || extLo == L".csv" || extLo == L".cfg") {
                            nameMatched = FileContentContainsText(prefix + name, needle);
                        }
                    }
                    if (!nameMatched) continue;
                }

                // Construct matched FileEntry
                FileEntry entry;
                entry.name = name;
                entry.fullPath = prefix + name;
                entry.normKey = PathUtils::NormalizePathKey(entry.fullPath);
                entry.attributes = ffd.dwFileAttributes;
                entry.writeTime = ffd.ftLastWriteTime;
                entry.isDirectory = isDir;
                entry.isHidden = isHidden;
                entry.dateStr = FormatFileTime(entry.writeTime);
                entry.ext = std::move(extLo);

                if (isDir) {
                    entry.displayName = entry.name;
                    const auto& extInfo = GetExtensionInfo(L"", true, lang);
                    entry.typeName = extInfo.typeName;
                    entry.sysIconIndex = extInfo.sysIconIndex;
                } else {
                    if (!showExtensions && !entry.ext.empty() && entry.name.size() > entry.ext.size()) {
                        entry.displayName = entry.name.substr(0, entry.name.size() - entry.ext.size());
                    } else {
                        entry.displayName = entry.name;
                    }
                    entry.size = fsize;
                    entry.sizeStr = FormatSizeKB(entry.size);
                    entry.isImage = IsImageExtension(entry.ext);
                    entry.isVideo = IsVideoExtension(entry.ext);
                    entry.isAudio = IsAudioExtension(entry.ext);

                    const auto& extInfo = GetExtensionInfo(entry.ext, false, lang);
                    entry.typeName = extInfo.typeName;
                    entry.sysIconIndex = extInfo.sysIconIndex;
                }

                if (!relFolder.empty()) {
                    entry.typeName += L"  [" + relFolder + L"]";
                }

                batchMatches.push_back(std::move(entry));
                ++matchedCount;

                ULONGLONG nowTick = GetTickCount64();
                if (nowTick - lastNotifyTick >= 75 || batchMatches.size() >= 200) {
                    lastNotifyTick = nowTick;
                    {
                        std::lock_guard<std::mutex> lock(m_searchMutex);
                        if (m_searchAccumResults.size() < 25000) {
                            for (auto& bm : batchMatches) {
                                m_searchAccumResults.push_back(std::move(bm));
                            }
                        }
                        batchMatches.clear();
                        m_searchProgress.matchedCount = matchedCount;
                        m_searchProgress.scannedFiles = scannedFiles;
                        m_searchProgress.scannedFolders = scannedFolders;
                        m_searchProgress.elapsedMs = nowTick - startTick;
                        m_searchProgress.currentFolder = curDir;
                    }
                    if (m_notifyHwnd && !m_searchNotifyPending.exchange(true, std::memory_order_relaxed)) {
                        PostMessageW(m_notifyHwnd, WM_APP_SEARCH_UPDATED, 0, 0);
                    }
                }
            } while (FindNextFileW(hFind, &ffd));

            FindClose(hFind);

            ULONGLONG nowTick = GetTickCount64();
            if (nowTick - lastNotifyTick >= 75) {
                lastNotifyTick = nowTick;
                {
                    std::lock_guard<std::mutex> lock(m_searchMutex);
                    if (!batchMatches.empty() && m_searchAccumResults.size() < 25000) {
                        for (auto& bm : batchMatches) {
                            m_searchAccumResults.push_back(std::move(bm));
                        }
                    }
                    batchMatches.clear();
                    m_searchProgress.matchedCount = matchedCount;
                    m_searchProgress.scannedFiles = scannedFiles;
                    m_searchProgress.scannedFolders = scannedFolders;
                    m_searchProgress.elapsedMs = nowTick - startTick;
                    m_searchProgress.currentFolder = curDir;
                }
                if (m_notifyHwnd && !m_searchNotifyPending.exchange(true, std::memory_order_relaxed)) {
                    PostMessageW(m_notifyHwnd, WM_APP_SEARCH_UPDATED, 0, 0);
                }
            }
        }

        if (m_running && myGen == m_searchGeneration.load(std::memory_order_relaxed)) {
            ULONGLONG endTick = GetTickCount64();
            {
                std::lock_guard<std::mutex> lock(m_searchMutex);
                if (!batchMatches.empty() && m_searchAccumResults.size() < 25000) {
                    for (auto& bm : batchMatches) {
                        m_searchAccumResults.push_back(std::move(bm));
                    }
                }
                batchMatches.clear();
                m_searchProgress.matchedCount = matchedCount;
                m_searchProgress.scannedFiles = scannedFiles;
                m_searchProgress.scannedFolders = scannedFolders;
                m_searchProgress.elapsedMs = endTick - startTick;
                m_searchProgress.isRunning = false;
                m_searchProgress.isFinished = true;
                m_searchRunning.store(false, std::memory_order_relaxed);
            }
            if (m_notifyHwnd) {
                PostMessageW(m_notifyHwnd, WM_APP_SEARCH_UPDATED, 0, 0);
            }
        }
    }
}

} // namespace Explorer2
