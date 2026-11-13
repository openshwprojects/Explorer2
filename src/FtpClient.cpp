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
#include <thread>

#pragma comment(lib, "wininet.lib")

namespace Explorer2 {

namespace {

std::mutex g_mockFtpMutex;
std::map<std::wstring, std::string> g_mockListings;
std::map<std::wstring, std::vector<uint8_t>> g_mockFiles;

struct CachedFtpListing {
    ULONGLONG tick = 0;
    std::vector<FileEntry> entries;
};
std::mutex g_ftpListingCacheMutex;
std::map<std::wstring, CachedFtpListing> g_ftpListingCache;

std::mutex g_fzCacheMutex;
std::vector<FtpBookmark> g_cachedFzSites;
ULONGLONG g_cachedFzTick = 0;

std::wstring UrlEncodeUserPassComponent(const std::wstring& s) {
    std::string u8 = PathUtils::WideToUtf8(s);
    std::wstring out;
    out.reserve(u8.size() * 2);
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    for (unsigned char c : u8) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<wchar_t>(c));
        } else {
            out.push_back(L'%');
            out.push_back(kHex[(c >> 4) & 0x0F]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::wstring UrlEncodeRemotePathForCurl(const std::wstring& remotePath) {
    std::string u8 = PathUtils::WideToUtf8(remotePath);
    std::wstring out;
    out.reserve(u8.size() * 2);
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    for (unsigned char c : u8) {
        if (c == '/' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<wchar_t>(c));
        } else {
            out.push_back(L'%');
            out.push_back(kHex[(c >> 4) & 0x0F]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    if (out.empty() || out.front() != L'/') out.insert(out.begin(), L'/');
    return out;
}

std::string DecodeBase64String(const std::string& in) {
    static const int8_t kDecTable[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    std::string out;
    out.reserve((in.size() * 3) / 4);
    uint32_t val = 0;
    int valb = -8;
    for (unsigned char c : in) {
        int8_t d = kDecTable[c];
        if (d == -1) continue;
        if (d == -2) break; // '=' padding
        val = (val << 6) + static_cast<uint32_t>(d);
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string UnescapeXmlText(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            if (s.compare(i, 5, "&amp;") == 0)  { out.push_back('&');  i += 4; continue; }
            if (s.compare(i, 4, "&lt;") == 0)   { out.push_back('<');  i += 3; continue; }
            if (s.compare(i, 4, "&gt;") == 0)   { out.push_back('>');  i += 3; continue; }
            if (s.compare(i, 6, "&quot;") == 0) { out.push_back('"');  i += 5; continue; }
            if (s.compare(i, 6, "&apos;") == 0) { out.push_back('\''); i += 5; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::string ExtractXmlTagValue(const std::string& block, const char* tagName, std::string* outAttrs = nullptr) {
    std::string openPrefix = std::string("<") + tagName;
    size_t pos = 0;
    while ((pos = block.find(openPrefix, pos)) != std::string::npos) {
        size_t afterName = pos + openPrefix.size();
        if (afterName < block.size() && (block[afterName] == '>' || block[afterName] == ' ' || block[afterName] == '\t' || block[afterName] == '/')) {
            size_t closeBracket = block.find('>', afterName);
            if (closeBracket == std::string::npos) return "";
            if (block[closeBracket - 1] == '/') return ""; // self-closing <Pass />
            if (outAttrs) {
                *outAttrs = block.substr(afterName, closeBracket - afterName);
            }
            std::string closeTag = std::string("</") + tagName + ">";
            size_t endTag = block.find(closeTag, closeBracket + 1);
            if (endTag == std::string::npos) return "";
            return block.substr(closeBracket + 1, endTag - (closeBracket + 1));
        }
        pos = afterName;
    }
    return "";
}

bool RunCurlProcessCapture(
    const std::wstring& args,
    std::string* outStdout,
    std::string* outStderr,
    DWORD timeoutMs = 14000)
{
    if (outStdout) outStdout->clear();
    if (outStderr) outStderr->clear();

    wchar_t sysDir[MAX_PATH] = {};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring curlExe = std::wstring(sysDir) + L"\\curl.exe";
    if (GetFileAttributesW(curlExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hOutR = nullptr, hOutW = nullptr;
    HANDLE hErrR = nullptr, hErrW = nullptr;
    if (!CreatePipe(&hOutR, &hOutW, &sa, 0)) return false;
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
    if (!CreatePipe(&hErrR, &hErrW, &sa, 0)) {
        CloseHandle(hOutR);
        CloseHandle(hOutW);
        return false;
    }
    SetHandleInformation(hErrR, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmdLine = L"\"" + curlExe + L"\" " + args;
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hOutW;
    si.hStdError = hErrW;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {};
    BOOL started = CreateProcessW(
        curlExe.c_str(),
        cmdBuf.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    CloseHandle(hOutW);
    CloseHandle(hErrW);

    if (!started) {
        CloseHandle(hOutR);
        CloseHandle(hErrR);
        return false;
    }

    std::thread errReader([hErrR, outStderr]() {
        char buf[1024];
        DWORD n = 0;
        while (ReadFile(hErrR, buf, sizeof(buf), &n, nullptr) && n > 0) {
            if (outStderr && outStderr->size() < 16384) {
                outStderr->append(buf, n);
            }
        }
    });

    char buf[4096];
    DWORD n = 0;
    while (ReadFile(hOutR, buf, sizeof(buf), &n, nullptr) && n > 0) {
        if (outStdout && outStdout->size() < (16 * 1024 * 1024)) {
            outStdout->append(buf, n);
        }
    }

    DWORD waitRes = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (waitRes == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
    }

    if (errReader.joinable()) errReader.join();
    CloseHandle(hOutR);
    CloseHandle(hErrR);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (waitRes == WAIT_OBJECT_0 && exitCode == 0);
}

bool RunCurlStreamDownload(
    const std::wstring& args,
    HANDLE hDstFile,
    const FtpTransferProgressFn& onChunk,
    bool& outCancelled)
{
    outCancelled = false;
    wchar_t sysDir[MAX_PATH] = {};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring curlExe = std::wstring(sysDir) + L"\\curl.exe";
    if (GetFileAttributesW(curlExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    LARGE_INTEGER zero = {};
    SetFilePointerEx(hDstFile, zero, nullptr, FILE_BEGIN);
    SetEndOfFile(hDstFile);

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hOutR = nullptr, hOutW = nullptr;
    HANDLE hErrR = nullptr, hErrW = nullptr;
    if (!CreatePipe(&hOutR, &hOutW, &sa, 65536)) return false;
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
    if (!CreatePipe(&hErrR, &hErrW, &sa, 0)) {
        CloseHandle(hOutR);
        CloseHandle(hOutW);
        return false;
    }
    SetHandleInformation(hErrR, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmdLine = L"\"" + curlExe + L"\" " + args;
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hOutW;
    si.hStdError = hErrW;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {};
    BOOL started = CreateProcessW(
        curlExe.c_str(),
        cmdBuf.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    CloseHandle(hOutW);
    CloseHandle(hErrW);

    if (!started) {
        CloseHandle(hOutR);
        CloseHandle(hErrR);
        return false;
    }

    std::thread errReader([hErrR]() {
        char buf[1024];
        DWORD n = 0;
        while (ReadFile(hErrR, buf, sizeof(buf), &n, nullptr) && n > 0) {}
    });

    std::vector<uint8_t> buf(65536);
    DWORD n = 0;
    bool writeOk = true;
    while (ReadFile(hOutR, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) && n > 0) {
        DWORD written = 0;
        if (!WriteFile(hDstFile, buf.data(), n, &written, nullptr) || written != n) {
            writeOk = false;
            TerminateProcess(pi.hProcess, 1);
            break;
        }
        if (onChunk && !onChunk(n)) {
            outCancelled = true;
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }

    DWORD waitRes = WaitForSingleObject(pi.hProcess, outCancelled ? 2000 : 15000);
    if (waitRes == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
    }

    if (errReader.joinable()) errReader.join();
    CloseHandle(hOutR);
    CloseHandle(hErrR);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (writeOk && !outCancelled && waitRes == WAIT_OBJECT_0 && exitCode == 0);
}

bool RunCurlStreamUpload(
    const std::wstring& args,
    HANDLE hSrcFile,
    const FtpTransferProgressFn& onChunk,
    bool& outCancelled)
{
    outCancelled = false;
    wchar_t sysDir[MAX_PATH] = {};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring curlExe = std::wstring(sysDir) + L"\\curl.exe";
    if (GetFileAttributesW(curlExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    LARGE_INTEGER zero = {};
    SetFilePointerEx(hSrcFile, zero, nullptr, FILE_BEGIN);

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hInR = nullptr, hInW = nullptr;
    HANDLE hOutR = nullptr, hOutW = nullptr;
    HANDLE hErrR = nullptr, hErrW = nullptr;
    if (!CreatePipe(&hInR, &hInW, &sa, 65536)) return false;
    SetHandleInformation(hInW, HANDLE_FLAG_INHERIT, 0);
    if (!CreatePipe(&hOutR, &hOutW, &sa, 0)) {
        CloseHandle(hInR);
        CloseHandle(hInW);
        return false;
    }
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
    if (!CreatePipe(&hErrR, &hErrW, &sa, 0)) {
        CloseHandle(hInR);
        CloseHandle(hInW);
        CloseHandle(hOutR);
        CloseHandle(hOutW);
        return false;
    }
    SetHandleInformation(hErrR, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmdLine = L"\"" + curlExe + L"\" " + args;
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hInR;
    si.hStdOutput = hOutW;
    si.hStdError = hErrW;

    PROCESS_INFORMATION pi = {};
    BOOL started = CreateProcessW(
        curlExe.c_str(),
        cmdBuf.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    CloseHandle(hInR);
    CloseHandle(hOutW);
    CloseHandle(hErrW);

    if (!started) {
        CloseHandle(hInW);
        CloseHandle(hOutR);
        CloseHandle(hErrR);
        return false;
    }

    std::thread outReader([hOutR]() {
        char buf[1024];
        DWORD n = 0;
        while (ReadFile(hOutR, buf, sizeof(buf), &n, nullptr) && n > 0) {}
    });
    std::thread errReader([hErrR]() {
        char buf[1024];
        DWORD n = 0;
        while (ReadFile(hErrR, buf, sizeof(buf), &n, nullptr) && n > 0) {}
    });

    std::vector<uint8_t> buf(65536);
    DWORD nRead = 0;
    bool streamOk = true;
    while (true) {
        if (!ReadFile(hSrcFile, buf.data(), static_cast<DWORD>(buf.size()), &nRead, nullptr)) {
            streamOk = false;
            TerminateProcess(pi.hProcess, 1);
            break;
        }
        if (nRead == 0) break;

        DWORD nWritten = 0;
        if (!WriteFile(hInW, buf.data(), nRead, &nWritten, nullptr) || nWritten != nRead) {
            streamOk = false;
            break;
        }
        if (onChunk && !onChunk(nRead)) {
            outCancelled = true;
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }

    CloseHandle(hInW); // Signal EOF to curl.exe so FTP STOR completes cleanly

    DWORD waitRes = WaitForSingleObject(pi.hProcess, outCancelled ? 2000 : 30000);
    if (waitRes == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
    }

    if (outReader.joinable()) outReader.join();
    if (errReader.joinable()) errReader.join();
    CloseHandle(hOutR);
    CloseHandle(hErrR);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (streamOk && !outCancelled && waitRes == WAIT_OBJECT_0 && exitCode == 0);
}

std::wstring EscapeCurlArgString(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 4);
    for (wchar_t ch : s) {
        if (ch == L'"') out += L"\\\"";
        else out.push_back(ch);
    }
    return out;
}

void AutoFillCredentialsFromFileZilla(FtpUrlInfo& info) {
    if (info.host.empty()) return;
    bool hasExplicitPassword = !info.password.empty() && info.password != L"anonymous@explorer2.local";
    if (hasExplicitPassword) return;

    auto sites = FtpClient::ImportFileZillaSites();
    for (const auto& site : sites) {
        if (_wcsicmp(site.host.c_str(), info.host.c_str()) != 0) continue;
        bool userMatches = (info.user.empty() || _wcsicmp(info.user.c_str(), L"anonymous") == 0 ||
                            _wcsicmp(info.user.c_str(), site.user.c_str()) == 0);
        if (userMatches && !site.password.empty()) {
            if (info.user.empty() || _wcsicmp(info.user.c_str(), L"anonymous") == 0) {
                info.user = site.user;
            }
            info.password = site.password;
            if (site.useFtps) info.useFtps = true;
            return;
        }
    }
}

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
    AutoFillCredentialsFromFileZilla(outInfo);
    outInfo.valid = true;
    return true;
}

std::wstring FtpClient::BuildFtpUrl(const FtpUrlInfo& info, bool includePassword) {
    if (!info.valid && info.host.empty()) return L"";
    std::wstring url = info.useFtps ? L"ftps://" : L"ftp://";
    if (!info.user.empty() && _wcsicmp(info.user.c_str(), L"anonymous") != 0) {
        if (includePassword && !info.password.empty() && info.password != L"anonymous@explorer2.local") {
            url += UrlEncodeUserPassComponent(info.user) + L":" + UrlEncodeUserPassComponent(info.password);
        } else {
            url += info.user;
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

void FtpClient::InvalidateListingCache() {
    std::lock_guard<std::mutex> lk(g_ftpListingCacheMutex);
    g_ftpListingCache.clear();
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

    std::wstring mockKey = NormalizeMockKey(info);

    // 1. Check registered mock FTP listings first (instant offline self-test & demo mode)
    {
        std::lock_guard<std::mutex> lk(g_mockFtpMutex);
        auto it = g_mockListings.find(mockKey);
        if (it != g_mockListings.end()) {
            outEntries = ParseFtpDirectoryListingText(it->second, BuildFtpUrl(info, true), showHidden, showExtensions, lang);
            return true;
        }
    }

    // 2. Check short-TTL in-memory listing cache (15 seconds) so tree expansion & navigation are instant
    std::wstring cacheKey = mockKey + (showHidden ? L"|H1" : L"|H0") + (showExtensions ? L"|E1" : L"|E0");
    {
        std::lock_guard<std::mutex> lk(g_ftpListingCacheMutex);
        auto it = g_ftpListingCache.find(cacheKey);
        if (it != g_ftpListingCache.end() && (GetTickCount64() - it->second.tick) < 15000ULL) {
            outEntries = it->second.entries;
            return true;
        }
    }

    // 3. Try curl.exe with WinSSL (--ssl -k --ftp-pasv) which supports Explicit FTPS (AUTH TLS),
    //    Implicit FTPS (ftps://), self-signed hosting certificates, and automatic plain FTP fallback!
    {
        std::wstring scheme = (info.useFtps && info.port == 990) ? L"ftps://" : L"ftp://";
        std::wstring encodedPath = UrlEncodeRemotePathForCurl(info.remotePath);
        if (encodedPath.back() != L'/') encodedPath.push_back(L'/');
        std::wstring cleanTargetUrl = scheme + info.host + L":" + std::to_wstring(info.port) + encodedPath;
        std::wstring userPass = EscapeCurlArgString(info.user + L":" + info.password);

        std::string rawOut, rawErr;
        std::wstring curlArgsSsl = L"-s -S -k --ssl --ftp-pasv --connect-timeout 6 --max-time 14 --user \"" +
                                   userPass + L"\" \"" + cleanTargetUrl + L"\"";
        bool curlOk = RunCurlProcessCapture(curlArgsSsl, &rawOut, &rawErr, 15000);
        if (!curlOk && !info.useFtps) {
            // Retry plain FTP without --ssl in case an older FTP daemon rejects AUTH TLS abnormally
            std::wstring curlArgsPlain = L"-s -S -k --ftp-pasv --connect-timeout 5 --max-time 12 --user \"" +
                                         userPass + L"\" \"" + cleanTargetUrl + L"\"";
            curlOk = RunCurlProcessCapture(curlArgsPlain, &rawOut, &rawErr, 13000);
        }

        if (curlOk) {
            outEntries = ParseFtpDirectoryListingText(rawOut, BuildFtpUrl(info, true), showHidden, showExtensions, lang);
            std::lock_guard<std::mutex> lk(g_ftpListingCacheMutex);
            g_ftpListingCache[cacheKey] = { GetTickCount64(), outEntries };
            return true;
        }
    }

    // 4. Fallback to WinINet FTP session
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
        if (outError) *outError = L"Nie można połączyć z serwerem FTP/FTPS " + info.host + L" (kod " + std::to_wstring(err) + L")";
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
    {
        std::lock_guard<std::mutex> lk(g_ftpListingCacheMutex);
        g_ftpListingCache[cacheKey] = { GetTickCount64(), outEntries };
    }
    return true;
}

bool FtpClient::ProbeFtpItemInfo(
    const std::wstring& ftpUrl,
    bool& outIsDirectory,
    uint64_t& outSize,
    FILETIME* outWriteTime)
{
    outIsDirectory = false;
    outSize = 0;
    if (outWriteTime) *outWriteTime = {};

    FtpUrlInfo info;
    if (!ParseFtpUrl(ftpUrl, info)) return false;

    if (info.remotePath.empty() || info.remotePath == L"/") {
        outIsDirectory = true;
        return true;
    }

    // 1. Fast check against in-memory mock FTP tables
    {
        std::wstring selfKey = NormalizeMockKey(info);
        std::lock_guard<std::mutex> lk(g_mockFtpMutex);
        if (g_mockListings.find(selfKey) != g_mockListings.end()) {
            outIsDirectory = true;
            return true;
        }
        auto fIt = g_mockFiles.find(selfKey);
        if (fIt != g_mockFiles.end()) {
            outIsDirectory = false;
            outSize = fIt->second.size();
            return true;
        }
    }

    // 2. Inspect parent directory listing (hits 5s g_ftpListingCache so multi-file selection is O(1) network calls)
    std::wstring leaf = GetFtpLeafName(ftpUrl);
    std::wstring parentUrl = GetParentFtpUrl(ftpUrl);
    if (!leaf.empty() && !parentUrl.empty() && parentUrl != ftpUrl) {
        std::vector<FileEntry> siblings;
        if (EnumerateFtpDirectory(parentUrl, true, true, Lang::EN, siblings, nullptr)) {
            for (const auto& e : siblings) {
                if (e.name == leaf) {
                    outIsDirectory = e.isDirectory;
                    outSize = e.size;
                    if (outWriteTime) *outWriteTime = e.writeTime;
                    return true;
                }
            }
            for (const auto& e : siblings) {
                if (_wcsicmp(e.name.c_str(), leaf.c_str()) == 0) {
                    outIsDirectory = e.isDirectory;
                    outSize = e.size;
                    if (outWriteTime) *outWriteTime = e.writeTime;
                    return true;
                }
            }
        }
    }

    return false;
}

bool FtpClient::DownloadFtpFileToLocalEx(
    const std::wstring& ftpFileUrl,
    const std::wstring& localDestPath,
    const FtpTransferProgressFn& onChunk,
    std::wstring* outError)
{
    FtpUrlInfo info;
    if (!ParseFtpUrl(ftpFileUrl, info)) {
        if (outError) *outError = L"Nieprawidłowy adres URL FTP";
        return false;
    }

    std::wstring parentLocal = PathUtils::GetParentPath(localDestPath);
    if (!parentLocal.empty()) {
        std::wstring accum;
        size_t pos = 0;
        if (parentLocal.size() >= 2 && parentLocal[1] == L':') {
            accum = parentLocal.substr(0, 2);
            pos = 2;
        }
        while (pos < parentLocal.size()) {
            while (pos < parentLocal.size() && (parentLocal[pos] == L'\\' || parentLocal[pos] == L'/')) ++pos;
            if (pos >= parentLocal.size()) break;
            size_t nextSlash = parentLocal.find_first_of(L"\\/", pos);
            if (nextSlash == std::wstring::npos) nextSlash = parentLocal.size();
            accum += L"\\" + parentLocal.substr(pos, nextSlash - pos);
            CreateDirectoryW(accum.c_str(), nullptr);
            pos = nextSlash;
        }
    }

    // 1. Check mock FTP files (streamed in 64 KB chunks so progress, pause, throttle & cancel work identically)
    {
        std::vector<uint8_t> mockData;
        bool isMock = false;
        {
            std::lock_guard<std::mutex> lk(g_mockFtpMutex);
            auto it = g_mockFiles.find(NormalizeMockKey(info));
            if (it != g_mockFiles.end()) {
                mockData = it->second;
                isMock = true;
            }
        }
        if (isMock) {
            HANDLE hOut = CreateFileW(
                localDestPath.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                nullptr);
            if (hOut == INVALID_HANDLE_VALUE) {
                if (outError) *outError = L"Nie można utworzyć pliku docelowego";
                return false;
            }
            const size_t kSlice = 65536;
            size_t off = 0;
            bool ok = true;
            while (off < mockData.size()) {
                size_t chunk = (std::min)(kSlice, mockData.size() - off);
                DWORD written = 0;
                if (!WriteFile(hOut, mockData.data() + off, static_cast<DWORD>(chunk), &written, nullptr) ||
                    written != static_cast<DWORD>(chunk)) {
                    ok = false;
                    break;
                }
                off += chunk;
                if (onChunk && !onChunk(chunk)) {
                    ok = false;
                    if (outError) *outError = L"Anulowano transfer FTP";
                    break;
                }
            }
            if (mockData.empty() && onChunk && !onChunk(0)) {
                ok = false;
            }
            CloseHandle(hOut);
            if (!ok) DeleteFileW(localDestPath.c_str());
            return ok;
        }
    }

    HANDLE hDst = CreateFileW(
        localDestPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (hDst == INVALID_HANDLE_VALUE) {
        if (outError) *outError = L"Nie można utworzyć pliku lokalnego: " + localDestPath;
        return false;
    }

    uint64_t bytesSeen = 0;
    auto wrappedProgress = [&](uint64_t chunk) -> bool {
        bytesSeen += chunk;
        return onChunk ? onChunk(chunk) : true;
    };

    // 2. Try curl.exe stdout streaming (supports FTPS AUTH TLS, Implicit TLS & plain FTP without 60s timeout cap)
    {
        std::wstring scheme = (info.useFtps && info.port == 990) ? L"ftps://" : L"ftp://";
        std::wstring encodedPath = UrlEncodeRemotePathForCurl(info.remotePath);
        std::wstring cleanTargetUrl = scheme + info.host + L":" + std::to_wstring(info.port) + encodedPath;
        std::wstring userPass = EscapeCurlArgString(info.user + L":" + info.password);

        bool cancelled = false;
        std::wstring sslArgs = L"-s -S -k --ssl --ftp-pasv --connect-timeout 10 --speed-limit 512 --speed-time 25 --user \"" +
                               userPass + L"\" \"" + cleanTargetUrl + L"\"";
        if (RunCurlStreamDownload(sslArgs, hDst, wrappedProgress, cancelled)) {
            CloseHandle(hDst);
            return true;
        }
        if (cancelled) {
            CloseHandle(hDst);
            DeleteFileW(localDestPath.c_str());
            if (outError) *outError = L"Anulowano transfer FTP";
            return false;
        }
        // If server rejected AUTH TLS before streaming data on plain ftp://, retry cleanly without --ssl
        if (!info.useFtps && bytesSeen == 0) {
            std::wstring plainArgs = L"-s -S --ftp-pasv --connect-timeout 10 --speed-limit 512 --speed-time 25 --user \"" +
                                     userPass + L"\" \"" + cleanTargetUrl + L"\"";
            if (RunCurlStreamDownload(plainArgs, hDst, wrappedProgress, cancelled)) {
                CloseHandle(hDst);
                return true;
            }
            if (cancelled) {
                CloseHandle(hDst);
                DeleteFileW(localDestPath.c_str());
                if (outError) *outError = L"Anulowano transfer FTP";
                return false;
            }
        }
    }

    if (bytesSeen > 0) {
        // Avoid double-counting progress bytes if curl failed mid-stream; let caller retry cleanly
        CloseHandle(hDst);
        DeleteFileW(localDestPath.c_str());
        if (outError) *outError = L"Przerwano strumień FTP podczas pobierania";
        return false;
    }

    // 3. Chunked fallback to WinINet (FtpOpenFileW + InternetReadFile)
    LARGE_INTEGER zero = {};
    SetFilePointerEx(hDst, zero, nullptr, FILE_BEGIN);
    SetEndOfFile(hDst);

    HINTERNET hInet = InternetOpenW(L"Explorer2/2.0 FTP Client", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) {
        CloseHandle(hDst);
        DeleteFileW(localDestPath.c_str());
        return false;
    }
    DWORD timeoutMs = 12000;
    InternetSetOptionW(hInet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionW(hInet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    HINTERNET hConn = InternetConnectW(
        hInet, info.host.c_str(), info.port, info.user.c_str(), info.password.c_str(),
        INTERNET_SERVICE_FTP, INTERNET_FLAG_PASSIVE, 0);
    if (!hConn) {
        InternetCloseHandle(hInet);
        CloseHandle(hDst);
        DeleteFileW(localDestPath.c_str());
        if (outError) *outError = L"Błąd połączenia FTP przy pobieraniu pliku";
        return false;
    }

    HINTERNET hRemote = FtpOpenFileW(
        hConn,
        info.remotePath.c_str(),
        GENERIC_READ,
        FTP_TRANSFER_TYPE_BINARY | INTERNET_FLAG_RELOAD,
        0);
    bool ok = false;
    if (hRemote) {
        std::vector<uint8_t> buf(65536);
        DWORD got = 0;
        ok = true;
        while (true) {
            if (!InternetReadFile(hRemote, buf.data(), static_cast<DWORD>(buf.size()), &got)) {
                ok = false;
                break;
            }
            if (got == 0) break;
            DWORD written = 0;
            if (!WriteFile(hDst, buf.data(), got, &written, nullptr) || written != got) {
                ok = false;
                break;
            }
            if (!wrappedProgress(got)) {
                ok = false;
                if (outError) *outError = L"Anulowano transfer FTP";
                break;
            }
        }
        InternetCloseHandle(hRemote);
    }

    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);
    CloseHandle(hDst);
    if (!ok) {
        DeleteFileW(localDestPath.c_str());
    }
    return ok;
}

bool FtpClient::DownloadFtpFileToLocal(
    const std::wstring& ftpFileUrl,
    const std::wstring& localDestPath,
    std::wstring* outError)
{
    return DownloadFtpFileToLocalEx(ftpFileUrl, localDestPath, nullptr, outError);
}

bool FtpClient::DownloadFtpFileToTempCache(
    const std::wstring& ftpFileUrl,
    std::wstring& outTempFilePath,
    const std::function<bool()>& keepGoing) {
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

    auto isCached = [&dest]() {
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        return GetFileAttributesExW(dest.c_str(), GetFileExInfoStandard, &fad) &&
               !(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
               (fad.nFileSizeHigh > 0 || fad.nFileSizeLow > 0);
    };
    if (isCached()) {
        outTempFilePath = dest;
        return true;
    }

    // Written under a name of its own (one per thread: a thumbnail and the preview may fetch the same file
    // at once) and renamed when complete
    wchar_t partSuffix[48] = {};
    swprintf_s(partSuffix, L".%lu.part", GetCurrentThreadId());
    const std::wstring part = dest + partSuffix;
    FtpTransferProgressFn onChunk;
    if (keepGoing) {
        onChunk = [&keepGoing](uint64_t) { return keepGoing(); };
    }
    bool ok = false;
    if (DownloadFtpFileToLocalEx(ftpFileUrl, part, onChunk, nullptr)) {
        // Another thread may have put its copy in place first; then that one is used
        ok = MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING) || isCached();
    }
    DeleteFileW(part.c_str());      // still there only when it was not used
    if (ok) outTempFilePath = dest;
    return ok;
}

bool FtpClient::UploadLocalFileToFtpEx(
    const std::wstring& localSrcPath,
    const std::wstring& ftpTargetDirUrl,
    const FtpTransferProgressFn& onChunk,
    std::wstring* outError,
    const std::wstring& remoteFileNameOverride)
{
    std::wstring fileName = remoteFileNameOverride.empty()
        ? PathUtils::GetFileName(localSrcPath)
        : remoteFileNameOverride;
    if (fileName.empty()) return false;

    std::wstring fullRemoteUrl = ftpTargetDirUrl;
    std::wstring targetLeaf = GetFtpLeafName(ftpTargetDirUrl);
    if (ftpTargetDirUrl.empty() || ftpTargetDirUrl.back() == L'/' || _wcsicmp(targetLeaf.c_str(), fileName.c_str()) != 0) {
        fullRemoteUrl = JoinFtpPath(ftpTargetDirUrl, fileName);
    }

    FtpUrlInfo info;
    if (!ParseFtpUrl(fullRemoteUrl, info)) {
        if (outError) *outError = L"Nieprawidłowy docelowy adres URL FTP";
        return false;
    }

    InvalidateListingCache();

    // 1. Mock host support (chunked so progress, throttle, pause & cancel work identically)
    {
        FtpUrlInfo dirInfo;
        ParseFtpUrl(GetParentFtpUrl(fullRemoteUrl), dirInfo);
        std::wstring dirKey = NormalizeMockKey(dirInfo);
        bool isMockHost = false;
        {
            std::lock_guard<std::mutex> lk(g_mockFtpMutex);
            isMockHost = (g_mockListings.find(dirKey) != g_mockListings.end());
        }
        if (isMockHost) {
            HANDLE hIn = CreateFileW(
                localSrcPath.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_SEQUENTIAL_SCAN,
                nullptr);
            if (hIn == INVALID_HANDLE_VALUE) {
                if (outError) *outError = L"Nie można otworzyć pliku źródłowego";
                return false;
            }
            LARGE_INTEGER fsz = {};
            GetFileSizeEx(hIn, &fsz);
            std::vector<uint8_t> bytes;
            if (fsz.QuadPart > 0 && fsz.QuadPart <= 64LL * 1024 * 1024) {
                bytes.reserve(static_cast<size_t>(fsz.QuadPart));
            }
            std::vector<uint8_t> buf(65536);
            DWORD got = 0;
            bool ok = true;
            while (true) {
                if (!ReadFile(hIn, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) {
                    ok = false;
                    break;
                }
                if (got == 0) break;
                bytes.insert(bytes.end(), buf.data(), buf.data() + got);
                if (onChunk && !onChunk(got)) {
                    ok = false;
                    if (outError) *outError = L"Anulowano wysyłanie FTP";
                    break;
                }
            }
            CloseHandle(hIn);
            if (!ok) return false;

            std::lock_guard<std::mutex> lk(g_mockFtpMutex);
            std::string nameU8 = PathUtils::WideToUtf8(GetFtpLeafName(fullRemoteUrl));
            std::string& dirList = g_mockListings[dirKey];
            if (dirList.find(" " + nameU8 + "\r\n") == std::string::npos &&
                dirList.find(" " + nameU8 + "\n") == std::string::npos) {
                dirList += "-rw-r--r-- 1 user group " + std::to_string(bytes.size()) + " Nov 08 12:00 " + nameU8 + "\r\n";
            }
            g_mockFiles[NormalizeMockKey(info)] = std::move(bytes);
            return true;
        }
    }

    HANDLE hSrc = CreateFileW(
        localSrcPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (hSrc == INVALID_HANDLE_VALUE) {
        if (outError) *outError = L"Nie można otworzyć pliku źródłowego: " + localSrcPath;
        return false;
    }

    uint64_t bytesSeen = 0;
    auto wrappedProgress = [&](uint64_t chunk) -> bool {
        bytesSeen += chunk;
        return onChunk ? onChunk(chunk) : true;
    };

    // 2. Try curl.exe stdin streaming (-T -) without artificial 60s timeout cap
    {
        std::wstring scheme = (info.useFtps && info.port == 990) ? L"ftps://" : L"ftp://";
        std::wstring encodedPath = UrlEncodeRemotePathForCurl(info.remotePath);
        std::wstring cleanTargetUrl = scheme + info.host + L":" + std::to_wstring(info.port) + encodedPath;
        std::wstring userPass = EscapeCurlArgString(info.user + L":" + info.password);

        bool cancelled = false;
        std::wstring sslArgs = L"-s -S -k --ssl --ftp-pasv --connect-timeout 10 --speed-limit 512 --speed-time 25 --user \"" +
                               userPass + L"\" -T - \"" + cleanTargetUrl + L"\"";
        if (RunCurlStreamUpload(sslArgs, hSrc, wrappedProgress, cancelled)) {
            CloseHandle(hSrc);
            InvalidateListingCache();
            return true;
        }
        if (cancelled) {
            CloseHandle(hSrc);
            if (outError) *outError = L"Anulowano wysyłanie FTP";
            return false;
        }
        if (!info.useFtps && bytesSeen == 0) {
            std::wstring plainArgs = L"-s -S --ftp-pasv --connect-timeout 10 --speed-limit 512 --speed-time 25 --user \"" +
                                     userPass + L"\" -T - \"" + cleanTargetUrl + L"\"";
            if (RunCurlStreamUpload(plainArgs, hSrc, wrappedProgress, cancelled)) {
                CloseHandle(hSrc);
                InvalidateListingCache();
                return true;
            }
            if (cancelled) {
                CloseHandle(hSrc);
                if (outError) *outError = L"Anulowano wysyłanie FTP";
                return false;
            }
        }
    }

    if (bytesSeen > 0) {
        CloseHandle(hSrc);
        if (outError) *outError = L"Przerwano strumień FTP podczas wysyłania";
        return false;
    }

    // 3. Chunked fallback to WinINet (FtpOpenFileW + InternetWriteFile)
    LARGE_INTEGER zero = {};
    SetFilePointerEx(hSrc, zero, nullptr, FILE_BEGIN);

    HINTERNET hInet = InternetOpenW(L"Explorer2/2.0 FTP Client", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) {
        CloseHandle(hSrc);
        return false;
    }
    DWORD timeoutMs = 12000;
    InternetSetOptionW(hInet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionW(hInet, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    HINTERNET hConn = InternetConnectW(
        hInet, info.host.c_str(), info.port, info.user.c_str(), info.password.c_str(),
        INTERNET_SERVICE_FTP, INTERNET_FLAG_PASSIVE, 0);
    if (!hConn) {
        InternetCloseHandle(hInet);
        CloseHandle(hSrc);
        if (outError) *outError = L"Błąd połączenia FTP przy wysyłaniu pliku";
        return false;
    }

    HINTERNET hRemote = FtpOpenFileW(
        hConn,
        info.remotePath.c_str(),
        GENERIC_WRITE,
        FTP_TRANSFER_TYPE_BINARY,
        0);
    bool ok = false;
    if (hRemote) {
        std::vector<uint8_t> buf(65536);
        DWORD got = 0;
        ok = true;
        while (true) {
            if (!ReadFile(hSrc, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) {
                ok = false;
                break;
            }
            if (got == 0) break;
            DWORD written = 0;
            if (!InternetWriteFile(hRemote, buf.data(), got, &written) || written != got) {
                ok = false;
                break;
            }
            if (!wrappedProgress(got)) {
                ok = false;
                if (outError) *outError = L"Anulowano wysyłanie FTP";
                break;
            }
        }
        InternetCloseHandle(hRemote);
    }

    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);
    CloseHandle(hSrc);
    InvalidateListingCache();
    return ok;
}

bool FtpClient::UploadLocalFileToFtp(
    const std::wstring& localSrcPath,
    const std::wstring& ftpTargetDirUrl,
    std::wstring* outError)
{
    return UploadLocalFileToFtpEx(localSrcPath, ftpTargetDirUrl, nullptr, outError);
}

bool FtpClient::CreateFtpDirectory(const std::wstring& ftpDirUrl, std::wstring* outError) {
    FtpUrlInfo info;
    if (!ParseFtpUrl(ftpDirUrl, info)) return false;
    InvalidateListingCache();

    // Mock host support
    {
        FtpUrlInfo parentInfo;
        ParseFtpUrl(GetParentFtpUrl(ftpDirUrl), parentInfo);
        std::wstring parentKey = NormalizeMockKey(parentInfo);
        std::wstring selfKey = NormalizeMockKey(info);
        std::lock_guard<std::mutex> lk(g_mockFtpMutex);
        if (g_mockListings.find(parentKey) != g_mockListings.end() || g_mockListings.find(selfKey) != g_mockListings.end()) {
            if (g_mockListings.find(selfKey) == g_mockListings.end()) {
                g_mockListings[selfKey] = "";
            }
            std::string leafU8 = PathUtils::WideToUtf8(GetFtpLeafName(ftpDirUrl));
            std::string& pList = g_mockListings[parentKey];
            if (!leafU8.empty() && pList.find(" " + leafU8 + "\r\n") == std::string::npos) {
                pList += "drwxr-xr-x 2 user group 4096 Nov 08 12:00 " + leafU8 + "\r\n";
            }
            return true;
        }
    }

    {
        std::wstring scheme = (info.useFtps && info.port == 990) ? L"ftps://" : L"ftp://";
        std::wstring cleanRootUrl = scheme + info.host + L":" + std::to_wstring(info.port) + L"/";
        std::wstring userPass = EscapeCurlArgString(info.user + L":" + info.password);
        std::wstring args = L"-s -S -k --ssl --ftp-pasv --connect-timeout 8 --user \"" +
                            userPass + L"\" -Q \"MKD " + EscapeCurlArgString(info.remotePath) + L"\" \"" + cleanRootUrl + L"\"";
        std::string outA, errA;
        if (RunCurlProcessCapture(args, &outA, &errA, 12000)) {
            return true;
        }
    }

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
    InvalidateListingCache();

    // Mock host support
    {
        FtpUrlInfo parentInfo;
        ParseFtpUrl(GetParentFtpUrl(ftpItemUrl), parentInfo);
        std::wstring selfKey = NormalizeMockKey(info);
        std::wstring parentKey = NormalizeMockKey(parentInfo);
        std::lock_guard<std::mutex> lk(g_mockFtpMutex);
        if (g_mockFiles.find(selfKey) != g_mockFiles.end() ||
            g_mockListings.find(selfKey) != g_mockListings.end() ||
            g_mockListings.find(parentKey) != g_mockListings.end()) {
            g_mockFiles.erase(selfKey);
            g_mockListings.erase(selfKey);
            auto pIt = g_mockListings.find(parentKey);
            if (pIt != g_mockListings.end()) {
                std::string leafSuffix = " " + PathUtils::WideToUtf8(GetFtpLeafName(ftpItemUrl));
                std::string& raw = pIt->second;
                std::string filtered;
                filtered.reserve(raw.size());
                size_t lineStart = 0;
                while (lineStart < raw.size()) {
                    size_t nl = raw.find('\n', lineStart);
                    size_t lineEnd = (nl == std::string::npos) ? raw.size() : (nl + 1);
                    std::string line = raw.substr(lineStart, lineEnd - lineStart);
                    std::string trimmed = TrimAStr(line);
                    bool matchesLeaf = (trimmed.size() >= leafSuffix.size() &&
                                        trimmed.compare(trimmed.size() - leafSuffix.size(), leafSuffix.size(), leafSuffix) == 0);
                    if (!matchesLeaf) {
                        filtered += line;
                    }
                    lineStart = lineEnd;
                }
                raw = std::move(filtered);
            }
            return true;
        }
    }

    {
        std::wstring scheme = (info.useFtps && info.port == 990) ? L"ftps://" : L"ftp://";
        std::wstring cleanRootUrl = scheme + info.host + L":" + std::to_wstring(info.port) + L"/";
        std::wstring userPass = EscapeCurlArgString(info.user + L":" + info.password);
        std::wstring ftpCmd = (isDirectory ? L"RMD " : L"DELE ") + EscapeCurlArgString(info.remotePath);
        std::wstring args = L"-s -S -k --ssl --ftp-pasv --connect-timeout 8 --user \"" +
                            userPass + L"\" -Q \"" + ftpCmd + L"\" \"" + cleanRootUrl + L"\"";
        std::string outA, errA;
        if (RunCurlProcessCapture(args, &outA, &errA, 12000)) {
            return true;
        }
    }

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

bool FtpClient::RenameFtpItem(
    const std::wstring& oldFtpUrl,
    const std::wstring& newName,
    std::wstring& outNewFtpUrl,
    std::wstring* outError)
{
    outNewFtpUrl.clear();
    if (newName.empty()) return false;
    FtpUrlInfo oldInfo;
    if (!ParseFtpUrl(oldFtpUrl, oldInfo)) return false;

    std::wstring parentUrl = GetParentFtpUrl(oldFtpUrl);
    outNewFtpUrl = JoinFtpPath(parentUrl, newName);
    FtpUrlInfo newInfo;
    if (!ParseFtpUrl(outNewFtpUrl, newInfo)) return false;

    InvalidateListingCache();

    // Mock host support
    {
        std::lock_guard<std::mutex> lk(g_mockFtpMutex);
        std::wstring oldKey = NormalizeMockKey(oldInfo);
        auto it = g_mockFiles.find(oldKey);
        if (it != g_mockFiles.end()) {
            g_mockFiles[NormalizeMockKey(newInfo)] = std::move(it->second);
            g_mockFiles.erase(it);
            FtpUrlInfo parentInfo;
            ParseFtpUrl(parentUrl, parentInfo);
            std::wstring pKey = NormalizeMockKey(parentInfo);
            auto pIt = g_mockListings.find(pKey);
            if (pIt != g_mockListings.end()) {
                std::string oldLeaf = " " + PathUtils::WideToUtf8(GetFtpLeafName(oldFtpUrl)) + "\r\n";
                std::string newLeaf = " " + PathUtils::WideToUtf8(newName) + "\r\n";
                size_t pos = pIt->second.find(oldLeaf);
                if (pos != std::string::npos) {
                    pIt->second.replace(pos, oldLeaf.size(), newLeaf);
                } else {
                    pIt->second += "-rw-r--r-- 1 user group 36 Nov 08 12:00 " + PathUtils::WideToUtf8(newName) + "\r\n";
                }
            }
            return true;
        }
    }

    // Try curl.exe RNFR / RNTO
    {
        std::wstring scheme = (oldInfo.useFtps && oldInfo.port == 990) ? L"ftps://" : L"ftp://";
        std::wstring cleanRootUrl = scheme + oldInfo.host + L":" + std::to_wstring(oldInfo.port) + L"/";
        std::wstring userPass = EscapeCurlArgString(oldInfo.user + L":" + oldInfo.password);
        std::wstring rnfr = L"RNFR " + EscapeCurlArgString(oldInfo.remotePath);
        std::wstring rnto = L"RNTO " + EscapeCurlArgString(newInfo.remotePath);
        std::wstring args = L"-s -S -k --ssl --ftp-pasv --connect-timeout 8 --user \"" +
                            userPass + L"\" -Q \"" + rnfr + L"\" -Q \"" + rnto + L"\" \"" + cleanRootUrl + L"\"";
        std::string outA, errA;
        if (RunCurlProcessCapture(args, &outA, &errA, 12000)) {
            return true;
        }
        std::wstring argsPlain = L"-s -S --ftp-pasv --connect-timeout 8 --user \"" +
                                 userPass + L"\" -Q \"" + rnfr + L"\" -Q \"" + rnto + L"\" \"ftp://" +
                                 oldInfo.host + L":" + std::to_wstring(oldInfo.port) + L"/\"";
        if (RunCurlProcessCapture(argsPlain, &outA, &errA, 12000)) {
            return true;
        }
    }

    HINTERNET hInet = InternetOpenW(L"Explorer2/2.0 FTP Client", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) return false;
    HINTERNET hConn = InternetConnectW(
        hInet, oldInfo.host.c_str(), oldInfo.port, oldInfo.user.c_str(), oldInfo.password.c_str(),
        INTERNET_SERVICE_FTP, INTERNET_FLAG_PASSIVE, 0);
    if (!hConn) {
        InternetCloseHandle(hInet);
        if (outError) *outError = L"Błąd połączenia FTP przy zmianie nazwy";
        return false;
    }
    BOOL ok = FtpRenameFileW(hConn, oldInfo.remotePath.c_str(), newInfo.remotePath.c_str());
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);
    return ok != FALSE;
}

bool FtpClient::UploadLocalTreeToFtp(
    const std::wstring& localSrcPath,
    const std::wstring& ftpTargetDirUrl,
    std::wstring* outError)
{
    DWORD attrs = GetFileAttributesW(localSrcPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return UploadLocalFileToFtp(localSrcPath, ftpTargetDirUrl, outError);
    }

    std::wstring dirName = PathUtils::GetFileName(localSrcPath);
    if (dirName.empty()) return false;
    std::wstring remoteSubDir = JoinFtpPath(ftpTargetDirUrl, dirName);
    CreateFtpDirectory(remoteSubDir, nullptr);

    std::wstring pattern = PathUtils::EnsureTrailingSlash(localSrcPath) + L"*";
    WIN32_FIND_DATAW ffd = {};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return true;

    bool allOk = true;
    do {
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
        std::wstring childLocal = PathUtils::EnsureTrailingSlash(localSrcPath) + ffd.cFileName;
        if (!UploadLocalTreeToFtp(childLocal, remoteSubDir, outError)) {
            allOk = false;
        }
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);
    InvalidateListingCache();
    return allOk;
}

bool FtpClient::DownloadFtpTreeToLocal(
    const std::wstring& ftpItemUrl,
    bool isDirectory,
    const std::wstring& localTargetDir,
    std::wstring* outError)
{
    std::wstring leaf = GetFtpLeafName(ftpItemUrl);
    if (leaf.empty()) return false;
    std::wstring localDst = PathUtils::EnsureTrailingSlash(localTargetDir) + leaf;

    if (!isDirectory) {
        return DownloadFtpFileToLocal(ftpItemUrl, localDst, outError);
    }

    CreateDirectoryW(localDst.c_str(), nullptr);
    std::vector<FileEntry> children;
    if (!EnumerateFtpDirectory(ftpItemUrl, true, true, Lang::PL, children, outError)) {
        return false;
    }
    bool allOk = true;
    for (const auto& c : children) {
        if (!DownloadFtpTreeToLocal(c.fullPath, c.isDirectory, localDst, outError)) {
            allOk = false;
        }
    }
    return allOk;
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

std::vector<FtpBookmark> FtpClient::ImportFileZillaSites(const std::wstring& customFileZillaDir) {
    if (customFileZillaDir.empty()) {
        std::lock_guard<std::mutex> lk(g_fzCacheMutex);
        ULONGLONG now = GetTickCount64();
        if (g_cachedFzTick != 0 && (now - g_cachedFzTick) < 15000) {
            return g_cachedFzSites;
        }
    }

    std::vector<FtpBookmark> results;
    std::wstring fzDir = customFileZillaDir;
    if (fzDir.empty()) {
        wchar_t appData[MAX_PATH] = {};
        DWORD n = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return results;
        fzDir = std::wstring(appData) + L"\\FileZilla";
    }

    const wchar_t* kFiles[] = { L"sitemanager.xml", L"recentservers.xml", L"filezilla.xml" };

    struct RawFzEntry {
        std::wstring siteName;
        std::wstring host;
        uint16_t port = 21;
        int protocol = 0; // 0=FTP, 1=SFTP, 3=FTPS(Implicit), 4=FTPES(Explicit)
        std::wstring user;
        std::wstring pass;
    };
    std::vector<RawFzEntry> parsedEntries;

    auto parseBlocksForTag = [&](const std::string& xmlText, const char* blockTag) {
        std::string openTag = std::string("<") + blockTag;
        std::string closeTag = std::string("</") + blockTag + ">";
        size_t pos = 0;
        while ((pos = xmlText.find(openTag, pos)) != std::string::npos) {
            size_t afterOpen = pos + openTag.size();
            if (afterOpen >= xmlText.size() || (xmlText[afterOpen] != '>' && xmlText[afterOpen] != ' ' && xmlText[afterOpen] != '\t')) {
                pos = afterOpen;
                continue;
            }
            size_t gtPos = xmlText.find('>', afterOpen);
            if (gtPos == std::string::npos) break;
            size_t endPos = xmlText.find(closeTag, gtPos + 1);
            if (endPos == std::string::npos) break;

            std::string block = xmlText.substr(gtPos + 1, endPos - (gtPos + 1));
            pos = endPos + closeTag.size();

            std::string hostA = TrimAStr(UnescapeXmlText(ExtractXmlTagValue(block, "Host")));
            if (hostA.empty()) continue;

            std::string portA = TrimAStr(ExtractXmlTagValue(block, "Port"));
            std::string protoA = TrimAStr(ExtractXmlTagValue(block, "Protocol"));
            std::string userA = TrimAStr(UnescapeXmlText(ExtractXmlTagValue(block, "User")));
            std::string nameA = TrimAStr(UnescapeXmlText(ExtractXmlTagValue(block, "Name")));

            std::string passAttrs;
            std::string passRaw = ExtractXmlTagValue(block, "Pass", &passAttrs);
            std::string passDecoded;
            if (passAttrs.find("base64") != std::string::npos) {
                passDecoded = DecodeBase64String(passRaw);
            } else {
                passDecoded = UnescapeXmlText(passRaw);
            }

            RawFzEntry e;
            e.host = PathUtils::Utf8ToWide(hostA);
            int p = portA.empty() ? 21 : std::atoi(portA.c_str());
            e.port = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : 21;
            e.protocol = protoA.empty() ? 0 : std::atoi(protoA.c_str());
            e.user = userA.empty() ? L"anonymous" : PathUtils::Utf8ToWide(userA);
            e.pass = PathUtils::Utf8ToWide(passDecoded);
            e.siteName = PathUtils::Utf8ToWide(nameA);
            parsedEntries.push_back(std::move(e));
        }
    };

    for (const wchar_t* fn : kFiles) {
        std::wstring fullPath = PathUtils::EnsureTrailingSlash(fzDir) + fn;
        std::vector<uint8_t> bytes;
        if (!PathUtils::ReadFileBytes(fullPath, 8 * 1024 * 1024, bytes) || bytes.empty()) continue;
        std::string xmlText(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        parseBlocksForTag(xmlText, "Server");
        parseBlocksForTag(xmlText, "LastServer");
    }

    // Track which hosts have native FTP/FTPS (protocol 0, 3, 4) so we skip duplicate port-22 SFTP entries for the same host
    std::vector<std::wstring> ftpHosts;
    for (const auto& e : parsedEntries) {
        if (e.protocol == 0 || e.protocol == 3 || e.protocol == 4) {
            ftpHosts.push_back(PathUtils::ToLower(e.host));
        }
    }

    std::vector<std::wstring> seenKeys;
    for (const auto& e : parsedEntries) {
        std::wstring hostLo = PathUtils::ToLower(e.host);
        uint16_t effPort = e.port;
        bool useFtps = (e.protocol == 3 || e.protocol == 4);

        if (e.protocol == 1) {
            // If this host already has a native FTP/FTPS entry in FileZilla, skip the duplicate port-22 SFTP entry
            if (std::find(ftpHosts.begin(), ftpHosts.end(), hostLo) != ftpHosts.end()) {
                continue;
            }
            effPort = 21;
        }

        std::wstring dedupKey = hostLo + L"|" + PathUtils::ToLower(e.user) + L"|" + std::to_wstring(effPort);
        if (std::find(seenKeys.begin(), seenKeys.end(), dedupKey) != seenKeys.end()) {
            continue;
        }
        seenKeys.push_back(dedupKey);

        FtpUrlInfo info;
        info.valid = true;
        info.useFtps = useFtps;
        info.host = e.host;
        info.port = effPort;
        info.user = e.user;
        info.password = e.pass;
        info.remotePath = L"/";

        FtpBookmark bm;
        if (!e.siteName.empty() && _wcsicmp(e.siteName.c_str(), e.host.c_str()) != 0) {
            bm.name = e.siteName + L" (" + e.host + L")";
        } else if (!e.user.empty() && _wcsicmp(e.user.c_str(), L"anonymous") != 0) {
            bm.name = e.host + L" (" + e.user + L")";
        } else {
            bm.name = e.host;
        }
        bm.url = BuildFtpUrl(info, true);
        bm.host = e.host;
        bm.user = e.user;
        bm.password = e.pass;
        bm.port = effPort;
        bm.useFtps = useFtps;
        bm.fromFileZilla = true;
        results.push_back(std::move(bm));
    }

    if (customFileZillaDir.empty()) {
        std::lock_guard<std::mutex> lk(g_fzCacheMutex);
        g_cachedFzSites = results;
        g_cachedFzTick = GetTickCount64();
    }

    return results;
}

std::vector<FtpBookmark> FtpClient::LoadBookmarks(const std::wstring& iniPathIn) {
    const bool isCustomTestPath = !iniPathIn.empty();
    std::wstring path = isCustomTestPath ? iniPathIn : DefaultBookmarksPath();
    std::vector<FtpBookmark> out;
    std::vector<std::wstring> seenHostUser;

    auto addUniqueBookmark = [&](FtpBookmark bm) {
        FtpUrlInfo parsed;
        if (ParseFtpUrl(bm.url, parsed)) {
            if (bm.host.empty()) bm.host = parsed.host;
            if (bm.user.empty()) bm.user = parsed.user;
            if (bm.password.empty()) bm.password = parsed.password;
            bm.port = parsed.port;
            bm.useFtps = parsed.useFtps;
            std::wstring key = PathUtils::ToLower(bm.host) + L"|" + PathUtils::ToLower(bm.user) + L"|" + std::to_wstring(bm.port);
            if (!isCustomTestPath && std::find(seenHostUser.begin(), seenHostUser.end(), key) != seenHostUser.end()) {
                return;
            }
            seenHostUser.push_back(key);
        }
        out.push_back(std::move(bm));
    };

    // 1. Auto-import all FileZilla servers & passwords first when loading main bookmarks
    if (!isCustomTestPath) {
        auto fzSites = ImportFileZillaSites();
        for (auto& s : fzSites) {
            addUniqueBookmark(std::move(s));
        }
    }

    // 2. Load user-saved bookmarks from INI
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        int count = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"FtpBookmarks", L"Count", 0, path.c_str())), 0, 50);
        for (int i = 0; i < count; ++i) {
            wchar_t sec[32] = {};
            swprintf_s(sec, L"Bookmark%d", i);
            wchar_t nameBuf[256] = {};
            wchar_t urlBuf[1024] = {};
            GetPrivateProfileStringW(sec, L"Name", L"FTP", nameBuf, static_cast<DWORD>(std::size(nameBuf)), path.c_str());
            GetPrivateProfileStringW(sec, L"Url", L"", urlBuf, static_cast<DWORD>(std::size(urlBuf)), path.c_str());
            if (urlBuf[0]) {
                FtpBookmark bm;
                bm.name = nameBuf;
                bm.url = urlBuf;
                addUniqueBookmark(std::move(bm));
            }
        }
    } else if (out.empty()) {
        addUniqueBookmark({ L"Lokalny serwer FTP (127.0.0.1:21)", L"ftp://anonymous@127.0.0.1:21/" });
        addUniqueBookmark({ L"Serwer NAS / Router (192.168.1.1:21)", L"ftp://admin@192.168.1.1:21/" });
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
