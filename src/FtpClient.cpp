#include "FtpClient.h"
#include "PathUtils.h"
#include "FileEngine.h"
#include "AppLog.h"
#include <wininet.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <map>
#include <mutex>

#pragma comment(lib, "wininet.lib")

namespace Explorer2 {

namespace {

std::mutex g_mockFtpMutex;
std::map<std::wstring, std::string> g_mockListings;
std::map<std::wstring, std::vector<uint8_t>> g_mockFiles;

std::wstring TrimWStr(const std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
    return s.substr(a, b - a);
}

std::string TrimAStr(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::wstring UrlDecodeW(const std::wstring& s) {
    std::string u8;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'%' && i + 2 < s.size()) {
            auto hexVal = [](wchar_t c) -> int {
                if (c >= L'0' && c <= L'9') return c - L'0';
                if (c >= L'a' && c <= L'f') return c - L'a' + 10;
                if (c >= L'A' && c <= L'F') return c - L'A' + 10;
                return -1;
            };
            int hi = hexVal(s[i + 1]);
            int lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                u8.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        if (s[i] < 0x80) {
            u8.push_back(static_cast<char>(s[i]));
        } else {
            std::wstring one(1, s[i]);
            u8 += PathUtils::WideToUtf8(one);
        }
    }
    return PathUtils::Utf8ToWide(u8);
}

std::wstring NormalizeMockKey(const FtpUrlInfo& info) {
    std::wstring p = info.remotePath;
    for (auto& ch : p) if (ch == L'\\') ch = L'/';
    while (p.size() > 1 && p.back() == L'/') p.pop_back();
    if (p.empty()) p = L"/";
    return PathUtils::ToLower(info.host) + L":" + std::to_wstring(info.port) + PathUtils::ToLower(p);
}

void PopulateFileEntryTypeFlags(FileEntry& entry, bool showExtensions, Lang lang) {
    entry.normKey = PathUtils::NormalizePathKey(entry.fullPath);
    if (entry.isDirectory) {
        entry.displayName = entry.name;
        entry.typeName = (lang == Lang::PL ? L"Folder plików (FTP)" : L"File folder (FTP)");
        entry.sizeStr = L"";
        return;
    }

    size_t dotPos = entry.name.find_last_of(L'.');
    if (dotPos != std::wstring::npos && dotPos > 0) {
        entry.ext = PathUtils::ToLower(entry.name.substr(dotPos));
        entry.displayName = showExtensions ? entry.name : entry.name.substr(0, dotPos);
    } else {
        entry.displayName = entry.name;
    }

    entry.isImage = IsImageExtension(entry.ext);
    entry.isVideo = IsVideoExtension(entry.ext);
    entry.isAudio = IsAudioExtension(entry.ext);

    entry.sizeStr = FileEngine::FormatSizeKB(entry.size);
    if (!entry.ext.empty()) {
        std::wstring upExt = entry.ext.substr(1);
        for (auto& c : upExt) c = static_cast<wchar_t>(std::towupper(c));
        entry.typeName = (lang == Lang::PL ? L"Plik " : L"File ") + upExt + L" (FTP)";
    } else {
        entry.typeName = (lang == Lang::PL ? L"Plik (FTP)" : L"File (FTP)");
    }
}

} // namespace

bool FtpClient::IsFtpPath(const std::wstring& path) {
    std::wstring t = TrimWStr(path);
    if (t.size() < 6) return false;
    return (_wcsnicmp(t.c_str(), L"ftp://", 6) == 0 ||
            _wcsnicmp(t.c_str(), L"ftps://", 7) == 0);
}

bool FtpClient::ParseFtpUrl(const std::wstring& urlIn, FtpUrlInfo& outInfo) {
    outInfo = FtpUrlInfo{};
    std::wstring u = TrimWStr(urlIn);
    for (auto& ch : u) {
        if (ch == L'\\') ch = L'/';
    }

    size_t schemeLen = 0;
    if (_wcsnicmp(u.c_str(), L"ftp://", 6) == 0) {
        schemeLen = 6;
        outInfo.useFtps = false;
    } else if (_wcsnicmp(u.c_str(), L"ftps://", 7) == 0) {
        schemeLen = 7;
        outInfo.useFtps = true;
    } else {
        return false;
    }

    std::wstring rest = u.substr(schemeLen);
    if (rest.empty()) return false;

    size_t slashPos = rest.find(L'/');
    std::wstring authority = (slashPos == std::wstring::npos) ? rest : rest.substr(0, slashPos);
    std::wstring rawPath   = (slashPos == std::wstring::npos) ? L"/" : rest.substr(slashPos);

    if (authority.empty()) return false;

    // Split optional user[:password]@ from host[:port]
    size_t atPos = authority.rfind(L'@');
    std::wstring hostPort = authority;
    if (atPos != std::wstring::npos) {
        std::wstring userPass = authority.substr(0, atPos);
        hostPort = authority.substr(atPos + 1);
        size_t colonPos = userPass.find(L':');
        if (colonPos != std::wstring::npos) {
            outInfo.user = UrlDecodeW(userPass.substr(0, colonPos));
            outInfo.password = UrlDecodeW(userPass.substr(colonPos + 1));
        } else {
            outInfo.user = UrlDecodeW(userPass);
            outInfo.password = L"";
        }
        if (outInfo.user.empty()) outInfo.user = L"anonymous";
    }

    if (hostPort.empty()) return false;

    size_t portColon = hostPort.rfind(L':');
    if (portColon != std::wstring::npos) {
        outInfo.host = hostPort.substr(0, portColon);
        int p = _wtoi(hostPort.substr(portColon + 1).c_str());
        if (p <= 0 || p > 65535) return false;
        outInfo.port = static_cast<uint16_t>(p);
    } else {
        outInfo.host = hostPort;
        outInfo.port = 21;
    }

    if (outInfo.host.empty()) return false;

    // Normalize remotePath: collapse duplicate slashes, remove trailing slash unless root "/"
    std::wstring normPath;
    for (wchar_t ch : rawPath) {
        if (ch == L'/' && !normPath.empty() && normPath.back() == L'/') continue;
        normPath.push_back(ch);
    }
    if (normPath.empty() || normPath.front() != L'/') normPath = L"/" + normPath;
    while (normPath.size() > 1 && normPath.back() == L'/') normPath.pop_back();
    outInfo.remotePath = UrlDecodeW(normPath);
    outInfo.valid = true;
    return true;
}

std::wstring FtpClient::BuildFtpUrl(const FtpUrlInfo& info, bool includePassword) {
    if (!info.valid && info.host.empty()) return L"";
    std::wstring url = info.useFtps ? L"ftps://" : L"ftp://";
    if (!info.user.empty() && _wcsicmp(info.user.c_str(), L"anonymous") != 0) {
        url += info.user;
        if (includePassword && !info.password.empty()) {
            url += L":" + info.password;
        }
        url += L"@";
    }
    url += info.host;
    if (info.port != 21 && info.port != 0) {
        url += L":" + std::to_wstring(info.port);
    }
    if (info.remotePath.empty() || info.remotePath.front() != L'/') {
        url += L"/";
    }
    url += info.remotePath;
    return url;
}

std::wstring FtpClient::JoinFtpPath(const std::wstring& baseFtpUrl, const std::wstring& childName) {
    FtpUrlInfo info;
    if (!ParseFtpUrl(baseFtpUrl, info)) return baseFtpUrl;
    std::wstring cleanChild = childName;
    for (auto& ch : cleanChild) if (ch == L'\\') ch = L'/';
    while (!cleanChild.empty() && cleanChild.front() == L'/') cleanChild.erase(cleanChild.begin());
    while (!cleanChild.empty() && cleanChild.back() == L'/') cleanChild.pop_back();
    if (cleanChild.empty()) return BuildFtpUrl(info, true);

    if (info.remotePath.empty() || info.remotePath == L"/") {
        info.remotePath = L"/" + cleanChild;
    } else {
        info.remotePath += L"/" + cleanChild;
    }
    return BuildFtpUrl(info, true);
}

std::wstring FtpClient::GetParentFtpUrl(const std::wstring& ftpUrl) {
    FtpUrlInfo info;
    if (!ParseFtpUrl(ftpUrl, info)) return L"THIS_PC";
    if (info.remotePath.empty() || info.remotePath == L"/") {
        return L"THIS_PC";
    }
    size_t lastSlash = info.remotePath.find_last_of(L'/');
    if (lastSlash == std::wstring::npos || lastSlash == 0) {
        info.remotePath = L"/";
    } else {
        info.remotePath = info.remotePath.substr(0, lastSlash);
    }
    return BuildFtpUrl(info, true);
}

std::wstring FtpClient::GetFtpLeafName(const std::wstring& ftpUrl) {
    FtpUrlInfo info;
    if (!ParseFtpUrl(ftpUrl, info)) return ftpUrl;
    if (info.remotePath.empty() || info.remotePath == L"/") {
        return L"FTP: " + info.host;
    }
    size_t lastSlash = info.remotePath.find_last_of(L'/');
    return (lastSlash == std::wstring::npos) ? info.remotePath : info.remotePath.substr(lastSlash + 1);
}

bool FtpClient::ParseFtpListLine(
    const std::string& rawLine,
    const std::wstring& parentFtpUrl,
    bool showExtensions,
    Lang lang,
    FileEntry& outEntry) {
    std::string line = TrimAStr(rawLine);
    if (line.empty()) return false;
    if (line.rfind("total ", 0) == 0) return false;

    outEntry = FileEntry{};

    // 1. Check RFC 3659 MLSD format: "type=dir;modify=20261107123000; Filename" or "type=file;size=4096;modify=20261107123000; report.csv"
    if (line.find("type=") != std::string::npos && line.find(';') != std::string::npos) {
        size_t sp = line.find("; ");
        if (sp == std::string::npos) sp = line.rfind(';');
        if (sp != std::string::npos && sp + 1 < line.size()) {
            std::string facts = line.substr(0, sp + 1);
            std::string nameA = TrimAStr(line.substr(sp + 1));
            if (nameA.empty() || nameA == "." || nameA == "..") return false;

            std::string factsLo = facts;
            for (auto& c : factsLo) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (factsLo.find("type=cdir") != std::string::npos || factsLo.find("type=pdir") != std::string::npos) return false;

            outEntry.isDirectory = (factsLo.find("type=dir") != std::string::npos);
            size_t szPos = factsLo.find("size=");
            if (szPos != std::string::npos) {
                outEntry.size = std::strtoull(factsLo.c_str() + szPos + 5, nullptr, 10);
            }
            size_t modPos = factsLo.find("modify=");
            if (modPos != std::string::npos && modPos + 15 <= factsLo.size()) {
                std::string m = facts.substr(modPos + 7, 12); // YYYYMMDDHHMM
                outEntry.dateStr = PathUtils::Utf8ToWide(
                    m.substr(6, 2) + "." + m.substr(4, 2) + "." + m.substr(0, 4) + " " + m.substr(8, 2) + ":" + m.substr(10, 2));
            } else {
                outEntry.dateStr = L"FTP";
            }
            outEntry.name = PathUtils::Utf8ToWide(nameA);
            outEntry.fullPath = JoinFtpPath(parentFtpUrl, outEntry.name);
            PopulateFileEntryTypeFlags(outEntry, showExtensions, lang);
            return true;
        }
    }

    // 2. Check Windows / IIS FTP format: "11-08-26  03:45PM       <DIR>          Backups" or "11-08-26  03:45PM                12345 notes.txt"
    if (line.size() >= 18 && std::isdigit(static_cast<unsigned char>(line[0])) &&
        (line[2] == '-' || line[2] == '/') && (line[5] == '-' || line[5] == '/')) {
        std::string dateTok = line.substr(0, 8);
        size_t pos = 8;
        while (pos < line.size() && line[pos] == ' ') ++pos;
        size_t timeEnd = line.find(' ', pos);
        if (timeEnd != std::string::npos) {
            std::string timeTok = line.substr(pos, timeEnd - pos);
            pos = timeEnd;
            while (pos < line.size() && line[pos] == ' ') ++pos;
            if (pos < line.size()) {
                if (line.compare(pos, 5, "<DIR>") == 0) {
                    outEntry.isDirectory = true;
                    pos += 5;
                } else {
                    outEntry.isDirectory = false;
                    size_t szEnd = line.find(' ', pos);
                    if (szEnd == std::string::npos) return false;
                    outEntry.size = std::strtoull(line.substr(pos, szEnd - pos).c_str(), nullptr, 10);
                    pos = szEnd;
                }
                while (pos < line.size() && line[pos] == ' ') ++pos;
                std::string nameA = line.substr(pos);
                if (nameA.empty() || nameA == "." || nameA == "..") return false;
                outEntry.name = PathUtils::Utf8ToWide(nameA);
                outEntry.dateStr = PathUtils::Utf8ToWide(dateTok + " " + timeTok);
                outEntry.fullPath = JoinFtpPath(parentFtpUrl, outEntry.name);
                PopulateFileEntryTypeFlags(outEntry, showExtensions, lang);
                return true;
            }
        }
    }

    // 3. Standard Unix ls -l format: "drwxr-xr-x   2 user group     4096 Nov  8 03:20 pub"
    // or "-rw-r--r--   1 user group   1048576 Nov  8 03:20 firmware.bin"
    char first = line[0];
    if (first == 'd' || first == '-' || first == 'l') {
        // Tokenize first 8 whitespace-separated columns: perms, links, owner, group, size, month, day, time/year, then filename
        size_t pos = 0;
        std::vector<std::string> cols;
        cols.reserve(8);
        for (int c = 0; c < 8 && pos < line.size(); ++c) {
            while (pos < line.size() && line[pos] == ' ') ++pos;
            if (pos >= line.size()) break;
            size_t tokEnd = line.find(' ', pos);
            if (tokEnd == std::string::npos) { pos = line.size(); break; }
            cols.push_back(line.substr(pos, tokEnd - pos));
            pos = tokEnd;
        }
        while (pos < line.size() && line[pos] == ' ') ++pos;
        if (cols.size() == 8 && pos < line.size()) {
            std::string nameA = line.substr(pos);
            // If symlink "link -> target", take the link name
            size_t arrow = nameA.find(" -> ");
            if (arrow != std::string::npos) nameA = nameA.substr(0, arrow);
            if (nameA.empty() || nameA == "." || nameA == "..") return false;

            outEntry.isDirectory = (first == 'd');
            outEntry.size = outEntry.isDirectory ? 0 : std::strtoull(cols[4].c_str(), nullptr, 10);
            outEntry.dateStr = PathUtils::Utf8ToWide(cols[5] + " " + cols[6] + " " + cols[7]);
            outEntry.name = PathUtils::Utf8ToWide(nameA);
            outEntry.fullPath = JoinFtpPath(parentFtpUrl, outEntry.name);
            PopulateFileEntryTypeFlags(outEntry, showExtensions, lang);
            return true;
        }
    }

    return false;
}

std::vector<FileEntry> FtpClient::ParseFtpDirectoryListingText(
    const std::string& listingText,
    const std::wstring& parentFtpUrl,
    bool showHidden,
    bool showExtensions,
    Lang lang) {
    std::vector<FileEntry> entries;
    size_t pos = 0;
    while (pos < listingText.size()) {
        size_t eol = listingText.find('\n', pos);
        if (eol == std::string::npos) eol = listingText.size();
        std::string line = listingText.substr(pos, eol - pos);
        pos = eol + 1;

        FileEntry fe;
        if (ParseFtpListLine(line, parentFtpUrl, showExtensions, lang, fe)) {
            if (!showHidden && !fe.name.empty() && fe.name[0] == L'.') continue;
            entries.push_back(std::move(fe));
        }
    }
    return entries;
}

void FtpClient::RegisterMockFtpListing(const std::wstring& hostAndPath, const std::string& rawListingText) {
    FtpUrlInfo info;
    if (!ParseFtpUrl(hostAndPath, info)) return;
    std::lock_guard<std::mutex> lk(g_mockFtpMutex);
    g_mockListings[NormalizeMockKey(info)] = rawListingText;
}

void FtpClient::RegisterMockFtpFileContent(const std::wstring& ftpFileUrl, const std::vector<uint8_t>& content) {
    FtpUrlInfo info;
    if (!ParseFtpUrl(ftpFileUrl, info)) return;
    std::lock_guard<std::mutex> lk(g_mockFtpMutex);
    g_mockFiles[NormalizeMockKey(info)] = content;
}

void FtpClient::ClearMockFtpData() {
    std::lock_guard<std::mutex> lk(g_mockFtpMutex);
    g_mockListings.clear();
    g_mockFiles.clear();
}

bool FtpClient::EnumerateFtpDirectory(
    const std::wstring& ftpUrl,
    bool showHidden,
    bool showExtensions,
    Lang lang,
    std::vector<FileEntry>& outEntries,
    std::wstring* outError) {
    outEntries.clear();
    FtpUrlInfo info;
    if (!ParseFtpUrl(ftpUrl, info)) {
        if (outError) *outError = L"Nieprawidłowy adres URL FTP";
        return false;
    }

    // 1. Check registered mock FTP listings first (instant offline self-test & demo mode)
    {
        std::lock_guard<std::mutex> lk(g_mockFtpMutex);
        auto it = g_mockListings.find(NormalizeMockKey(info));
        if (it != g_mockListings.end()) {
            outEntries = ParseFtpDirectoryListingText(it->second, BuildFtpUrl(info, true), showHidden, showExtensions, lang);
            return true;
        }
    }

    // 2. Live WinINet FTP session
    HINTERNET hInet = InternetOpenW(L"Explorer2/2.0 FTP Client", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) {
        if (outError) *outError = L"InternetOpenW failed";
        return false;
    }

    DWORD timeoutMs = 4500;
    InternetSetOptionW(hInet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionW(hInet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionW(hInet, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    DWORD flags = INTERNET_FLAG_PASSIVE;
    HINTERNET hConn = InternetConnectW(
        hInet,
        info.host.c_str(),
        info.port,
        info.user.c_str(),
        info.password.c_str(),
        INTERNET_SERVICE_FTP,
        flags,
        0);
    if (!hConn) {
        DWORD err = GetLastError();
        InternetCloseHandle(hInet);
        if (outError) *outError = L"Nie można połączyć z serwerem FTP " + info.host + L" (kod " + std::to_wstring(err) + L")";
        return false;
    }

    std::wstring dirToSet = info.remotePath.empty() ? L"/" : info.remotePath;
    FtpSetCurrentDirectoryW(hConn, dirToSet.c_str());

    WIN32_FIND_DATAW fd = {};
    HINTERNET hFind = FtpFindFirstFileW(hConn, L"*", &fd, INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE, 0);
    if (hFind) {
        std::wstring baseUrl = BuildFtpUrl(info, true);
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            if (!showHidden && fd.cFileName[0] == L'.') continue;

            FileEntry fe;
            fe.name = fd.cFileName;
            fe.fullPath = JoinFtpPath(baseUrl, fe.name);
            fe.isDirectory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            fe.size = fe.isDirectory ? 0 : ((static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow);
            fe.writeTime = fd.ftLastWriteTime;
            fe.dateStr = FileEngine::FormatFileTime(fd.ftLastWriteTime);
            PopulateFileEntryTypeFlags(fe, showExtensions, lang);
            outEntries.push_back(std::move(fe));
        } while (InternetFindNextFileW(hFind, &fd));

        InternetCloseHandle(hFind);
    }

    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);
    return true;
}

bool FtpClient::DownloadFtpFileToLocal(
    const std::wstring& ftpFileUrl,
    const std::wstring& localDestPath,
    std::wstring* outError) {
    FtpUrlInfo info;
    if (!ParseFtpUrl(ftpFileUrl, info)) {
        if (outError) *outError = L"Nieprawidłowy adres URL FTP";
        return false;
    }

    // 1. Check mock FTP files
    {
        std::lock_guard<std::mutex> lk(g_mockFtpMutex);
        auto it = g_mockFiles.find(NormalizeMockKey(info));
        if (it != g_mockFiles.end()) {
            return PathUtils::WriteFileBytes(localDestPath, it->second.data(), it->second.size());
        }
    }

    HINTERNET hInet = InternetOpenW(L"Explorer2/2.0 FTP Client", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) return false;
    DWORD timeoutMs = 8000;
    InternetSetOptionW(hInet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionW(hInet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    HINTERNET hConn = InternetConnectW(
        hInet, info.host.c_str(), info.port, info.user.c_str(), info.password.c_str(),
        INTERNET_SERVICE_FTP, INTERNET_FLAG_PASSIVE, 0);
    if (!hConn) {
        InternetCloseHandle(hInet);
        if (outError) *outError = L"Błąd połączenia FTP przy pobieraniu pliku";
        return false;
    }

    BOOL ok = FtpGetFileW(
        hConn,
        info.remotePath.c_str(),
        localDestPath.c_str(),
        FALSE,
        FILE_ATTRIBUTE_NORMAL,
        FTP_TRANSFER_TYPE_BINARY | INTERNET_FLAG_RELOAD,
        0);

    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);
    return ok != FALSE;
}

bool FtpClient::DownloadFtpFileToTempCache(
    const std::wstring& ftpFileUrl,
    std::wstring& outTempFilePath) {
    outTempFilePath.clear();
    std::wstring leaf = GetFtpLeafName(ftpFileUrl);
    if (leaf.empty()) leaf = L"ftp_item.bin";
    for (auto& ch : leaf) {
        if (ch == L'/' || ch == L'\\' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
            ch = L'_';
        }
    }
    wchar_t tmpDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpDir);
    uint32_t h = 2166136261u;
    for (wchar_t c : ftpFileUrl) {
        h ^= static_cast<uint32_t>(c);
        h *= 16777619u;
    }
    wchar_t prefix[64] = {};
    swprintf_s(prefix, L"E2Ftp_%08X_", h);
    std::wstring dest = std::wstring(tmpDir) + prefix + leaf;
    if (DownloadFtpFileToLocal(ftpFileUrl, dest, nullptr)) {
        outTempFilePath = dest;
        return true;
    }
    return false;
}

bool FtpClient::UploadLocalFileToFtp(
    const std::wstring& localSrcPath,
    const std::wstring& ftpTargetDirUrl,
    std::wstring* outError) {
    std::wstring fileName = PathUtils::GetFileName(localSrcPath);
    if (fileName.empty()) return false;
    std::wstring fullRemoteUrl = JoinFtpPath(ftpTargetDirUrl, fileName);

    FtpUrlInfo info;
    if (!ParseFtpUrl(fullRemoteUrl, info)) return false;

    // If uploading to a mock host in self-tests, store in g_mockFiles
    {
        FtpUrlInfo dirInfo;
        ParseFtpUrl(ftpTargetDirUrl, dirInfo);
        std::lock_guard<std::mutex> lk(g_mockFtpMutex);
        if (g_mockListings.find(NormalizeMockKey(dirInfo)) != g_mockListings.end()) {
            std::vector<uint8_t> bytes;
            if (PathUtils::ReadFileBytes(localSrcPath, 64 * 1024 * 1024, bytes)) {
                g_mockFiles[NormalizeMockKey(info)] = std::move(bytes);
                return true;
            }
        }
    }

    HINTERNET hInet = InternetOpenW(L"Explorer2/2.0 FTP Client", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) return false;
    HINTERNET hConn = InternetConnectW(
        hInet, info.host.c_str(), info.port, info.user.c_str(), info.password.c_str(),
        INTERNET_SERVICE_FTP, INTERNET_FLAG_PASSIVE, 0);
    if (!hConn) {
        InternetCloseHandle(hInet);
        if (outError) *outError = L"Błąd połączenia FTP przy wysyłaniu pliku";
        return false;
    }

    BOOL ok = FtpPutFileW(hConn, localSrcPath.c_str(), info.remotePath.c_str(), FTP_TRANSFER_TYPE_BINARY, 0);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);
    return ok != FALSE;
}

bool FtpClient::CreateFtpDirectory(const std::wstring& ftpDirUrl, std::wstring* outError) {
    FtpUrlInfo info;
    if (!ParseFtpUrl(ftpDirUrl, info)) return false;
    HINTERNET hInet = InternetOpenW(L"Explorer2/2.0 FTP Client", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) return false;
    HINTERNET hConn = InternetConnectW(
        hInet, info.host.c_str(), info.port, info.user.c_str(), info.password.c_str(),
        INTERNET_SERVICE_FTP, INTERNET_FLAG_PASSIVE, 0);
    if (!hConn) {
        InternetCloseHandle(hInet);
        if (outError) *outError = L"Błąd połączenia FTP";
        return false;
    }
    BOOL ok = FtpCreateDirectoryW(hConn, info.remotePath.c_str());
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);
    return ok != FALSE;
}

bool FtpClient::DeleteFtpItem(const std::wstring& ftpItemUrl, bool isDirectory, std::wstring* outError) {
    FtpUrlInfo info;
    if (!ParseFtpUrl(ftpItemUrl, info)) return false;
    HINTERNET hInet = InternetOpenW(L"Explorer2/2.0 FTP Client", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) return false;
    HINTERNET hConn = InternetConnectW(
        hInet, info.host.c_str(), info.port, info.user.c_str(), info.password.c_str(),
        INTERNET_SERVICE_FTP, INTERNET_FLAG_PASSIVE, 0);
    if (!hConn) {
        InternetCloseHandle(hInet);
        if (outError) *outError = L"Błąd połączenia FTP";
        return false;
    }
    BOOL ok = isDirectory
        ? FtpRemoveDirectoryW(hConn, info.remotePath.c_str())
        : FtpDeleteFileW(hConn, info.remotePath.c_str());
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);
    return ok != FALSE;
}

std::wstring FtpClient::DefaultBookmarksPath() {
    wchar_t localAppData[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH)
        ? (std::wstring(localAppData) + L"\\Explorer2")
        : L".";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\ftp_bookmarks.ini";
}

std::vector<FtpBookmark> FtpClient::LoadBookmarks(const std::wstring& iniPathIn) {
    std::wstring path = iniPathIn.empty() ? DefaultBookmarksPath() : iniPathIn;
    std::vector<FtpBookmark> out;
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        out.push_back({ L"Lokalny serwer FTP (127.0.0.1:21)", L"ftp://anonymous@127.0.0.1:21/" });
        out.push_back({ L"Serwer NAS / Router (192.168.1.1:21)", L"ftp://admin@192.168.1.1:21/" });
        return out;
    }

    int count = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"FtpBookmarks", L"Count", 0, path.c_str())), 0, 40);
    for (int i = 0; i < count; ++i) {
        wchar_t sec[32] = {};
        swprintf_s(sec, L"Bookmark%d", i);
        wchar_t nameBuf[256] = {};
        wchar_t urlBuf[1024] = {};
        GetPrivateProfileStringW(sec, L"Name", L"FTP", nameBuf, static_cast<DWORD>(std::size(nameBuf)), path.c_str());
        GetPrivateProfileStringW(sec, L"Url", L"", urlBuf, static_cast<DWORD>(std::size(urlBuf)), path.c_str());
        if (urlBuf[0]) {
            out.push_back({ nameBuf, urlBuf });
        }
    }
    return out;
}

bool FtpClient::SaveBookmarks(const std::vector<FtpBookmark>& bookmarks, const std::wstring& iniPathIn) {
    std::wstring path = iniPathIn.empty() ? DefaultBookmarksPath() : iniPathIn;
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const uint16_t bom = 0xFEFF;
            DWORD wr = 0;
            WriteFile(h, &bom, sizeof(bom), &wr, nullptr);
            CloseHandle(h);
        }
    }
    WritePrivateProfileStringW(L"FtpBookmarks", L"Count", std::to_wstring(bookmarks.size()).c_str(), path.c_str());
    for (size_t i = 0; i < bookmarks.size(); ++i) {
        wchar_t sec[32] = {};
        swprintf_s(sec, L"Bookmark%zu", i);
        WritePrivateProfileStringW(sec, L"Name", bookmarks[i].name.c_str(), path.c_str());
        WritePrivateProfileStringW(sec, L"Url", bookmarks[i].url.c_str(), path.c_str());
    }
    return true;
}

} // namespace Explorer2
