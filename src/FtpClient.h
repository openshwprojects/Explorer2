#pragma once
#include "Types.h"
#include "Localization.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Explorer2 {

// Progress & cooperative pause/cancel callback for chunked FTP transfers:
// receives bytes transferred in latest chunk; return false to abort immediately.
using FtpTransferProgressFn = std::function<bool(uint64_t chunkBytes)>;

struct FtpUrlInfo {
    bool valid = false;
    bool useFtps = false;
    std::wstring user = L"anonymous";
    std::wstring password = L"anonymous@explorer2.local";
    std::wstring host;
    uint16_t port = 21;
    std::wstring remotePath = L"/"; // Always starts with '/'
};

struct FtpBookmark {
    std::wstring name;
    std::wstring url;
    std::wstring host;
    std::wstring user;
    std::wstring password;
    uint16_t port = 21;
    bool useFtps = false;
    bool fromFileZilla = false;
};

class FtpClient {
public:
    // URL detection, parsing, and path manipulation
    static bool IsFtpPath(const std::wstring& path);
    static bool ParseFtpUrl(const std::wstring& url, FtpUrlInfo& outInfo);
    static std::wstring BuildFtpUrl(const FtpUrlInfo& info, bool includePassword = true);
    static std::wstring JoinFtpPath(const std::wstring& baseFtpUrl, const std::wstring& childName);
    static std::wstring GetParentFtpUrl(const std::wstring& ftpUrl);
    static std::wstring GetFtpLeafName(const std::wstring& ftpUrl);

    // RFC 959 (Unix ls -l & Windows/IIS) and RFC 3659 (MLSD) directory listing parser
    static bool ParseFtpListLine(
        const std::string& line,
        const std::wstring& parentFtpUrl,
        bool showExtensions,
        Lang lang,
        FileEntry& outEntry);

    static std::vector<FileEntry> ParseFtpDirectoryListingText(
        const std::string& listingText,
        const std::wstring& parentFtpUrl,
        bool showHidden,
        bool showExtensions,
        Lang lang);

    // WinINet FTP directory enumeration & file transfer (works in both Main Pane and Split Pane)
    static bool EnumerateFtpDirectory(
        const std::wstring& ftpUrl,
        bool showHidden,
        bool showExtensions,
        Lang lang,
        std::vector<FileEntry>& outEntries,
        std::wstring* outError = nullptr);

    static bool ProbeFtpItemInfo(
        const std::wstring& ftpUrl,
        bool& outIsDirectory,
        uint64_t& outSize,
        FILETIME* outWriteTime = nullptr);

    static bool DownloadFtpFileToLocal(
        const std::wstring& ftpFileUrl,
        const std::wstring& localDestPath,
        std::wstring* outError = nullptr);

    static bool DownloadFtpFileToLocalEx(
        const std::wstring& ftpFileUrl,
        const std::wstring& localDestPath,
        const FtpTransferProgressFn& onChunk,
        std::wstring* outError = nullptr);

    // Downloads an FTP file into the temp folder (once: a copy that is there already is used again).
    // keepGoing, when given, is asked as the data comes in; false stops the download. The copy gets its
    // name only when it is complete, so a download that was stopped or broke off is never taken for one.
    static bool DownloadFtpFileToTempCache(
        const std::wstring& ftpFileUrl,
        std::wstring& outTempFilePath,
        const std::function<bool()>& keepGoing = {});

    static bool UploadLocalFileToFtp(
        const std::wstring& localSrcPath,
        const std::wstring& ftpTargetDirUrl,
        std::wstring* outError = nullptr);

    static bool UploadLocalFileToFtpEx(
        const std::wstring& localSrcPath,
        const std::wstring& ftpTargetDirUrl,
        const FtpTransferProgressFn& onChunk,
        std::wstring* outError = nullptr,
        const std::wstring& remoteFileNameOverride = L"");

    static bool CreateFtpDirectory(
        const std::wstring& ftpDirUrl,
        std::wstring* outError = nullptr);

    static bool DeleteFtpItem(
        const std::wstring& ftpItemUrl,
        bool isDirectory,
        std::wstring* outError = nullptr);

    static bool RenameFtpItem(
        const std::wstring& oldFtpUrl,
        const std::wstring& newName,
        std::wstring& outNewFtpUrl,
        std::wstring* outError = nullptr);

    static bool UploadLocalTreeToFtp(
        const std::wstring& localSrcPath,
        const std::wstring& ftpTargetDirUrl,
        std::wstring* outError = nullptr);

    static bool DownloadFtpTreeToLocal(
        const std::wstring& ftpItemUrl,
        bool isDirectory,
        const std::wstring& localTargetDir,
        std::wstring* outError = nullptr);

    // Mock / In-Memory Virtual FTP Host registration (used for deterministic offline self-tests
    // and instant local demonstration without requiring an external internet FTP server)
    static void RegisterMockFtpListing(const std::wstring& hostAndPath, const std::string& rawListingText);
    static void RegisterMockFtpFileContent(const std::wstring& ftpFileUrl, const std::vector<uint8_t>& content);
    static void ClearMockFtpData();

    // FTP Bookmarks persistence (%LOCALAPPDATA%\Explorer2\ftp_bookmarks.ini) + FileZilla auto-import (%APPDATA%\FileZilla)
    static std::wstring DefaultBookmarksPath();
    static std::vector<FtpBookmark> ImportFileZillaSites(const std::wstring& customFileZillaDir = L"");
    static std::vector<FtpBookmark> LoadBookmarks(const std::wstring& iniPath = L"");
    static bool SaveBookmarks(const std::vector<FtpBookmark>& bookmarks, const std::wstring& iniPath = L"");
    static void InvalidateListingCache();
};

} // namespace Explorer2
