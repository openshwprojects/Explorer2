#pragma once
// UpdateCheck.h - looks on GitHub for a release of Explorer2 newer than the one that is running.
//
// One address is asked, the public "latest release" of the project: no account, no cookies, nothing about the user
// or the computer is sent beyond what any HTTPS request carries. The answer's tag ("v1.0.25") is compared with this
// build's version. Header-only, so that neither build script has a file to add.
#include "JsonDom.h"
#include "ToolProcess.h"
#include <windows.h>
#ifdef _WIN32
#include <wininet.h>
#endif
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

namespace Explorer2::UpdateCheck {

inline constexpr wchar_t kReleasesPageUrl[] = L"https://github.com/openshwprojects/Explorer2/releases/latest";
inline constexpr wchar_t kLatestReleaseApiUrl[] = L"https://api.github.com/repos/openshwprojects/Explorer2/releases/latest";

// The numbers a version starts with: "v1.0.25", "1.0.25.41" and "1.0.25 (3f51126)" all give 1, 0, 25. Up to four
// numbers are read and missing ones are 0. False when the text does not start with a number (after an optional "v").
inline bool ParseVersion(const std::wstring& text, int out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    size_t i = 0;
    while (i < text.size() && (text[i] == L' ' || text[i] == L'\t')) ++i;
    if (i < text.size() && (text[i] == L'v' || text[i] == L'V')) ++i;
    int count = 0;
    while (count < 4 && i < text.size() && text[i] >= L'0' && text[i] <= L'9') {
        long long value = 0;
        while (i < text.size() && text[i] >= L'0' && text[i] <= L'9') {
            value = std::min<long long>(value * 10 + (text[i] - L'0'), 1000000000LL);
            ++i;
        }
        out[count++] = static_cast<int>(value);
        if (i + 1 < text.size() && text[i] == L'.' && text[i + 1] >= L'0' && text[i + 1] <= L'9') ++i;
        else break;
    }
    return count > 0;
}

// Negative: a is older than b. Positive: a is newer. 0: the same version, or one of the two cannot be read.
inline int CompareVersions(const std::wstring& a, const std::wstring& b) {
    int va[4], vb[4];
    if (!ParseVersion(a, va) || !ParseVersion(b, vb)) return 0;
    for (int i = 0; i < 4; ++i) {
        if (va[i] != vb[i]) return (va[i] < vb[i]) ? -1 : 1;
    }
    return 0;
}

// The "tag_name" of GitHub's JSON answer for a release. Empty when the answer is not such a document, or when the
// tag is not a plain version name (it is shown in the window, so nothing but letters, digits and . - _ + is taken).
inline std::wstring TagFromReleaseJson(const std::string& json) {
    Json::Value doc;
    if (json.empty() || !Json::ParseDocument(json, doc)) return {};
    const Json::Value* tag = doc.Find("tag_name");
    if (!tag || !tag->IsString() || tag->strVal.empty() || tag->strVal.size() > 40) return {};
    std::wstring out;
    for (const char ch : tag->strVal) {
        const bool plain = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                           ch == '.' || ch == '-' || ch == '_' || ch == '+';
        if (!plain) return {};
        out.push_back(static_cast<wchar_t>(ch));
    }
    int numbers[4];
    return ParseVersion(out, numbers) ? out : std::wstring();
}

#ifdef _WIN32
// HTTPS GET through WinINet (the system's proxy settings apply). No cookies and no stored credentials are sent.
// Returns the body of a 200 answer, empty otherwise. Blocks until the answer or the timeout.
inline std::string HttpGetWinInet(const wchar_t* url, const std::wstring& userAgent, DWORD timeoutMs, size_t maxBytes) {
    std::string body;
    HINTERNET inet = InternetOpenW(userAgent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!inet) return body;
    InternetSetOptionW(inet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionW(inet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionW(inet, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    const wchar_t headers[] = L"Accept: application/vnd.github+json\r\n";
    const DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                        INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_AUTH | INTERNET_FLAG_NO_UI;
    HINTERNET request = InternetOpenUrlW(inet, url, headers, static_cast<DWORD>(-1), flags, 0);
    if (request) {
        DWORD status = 0;
        DWORD statusLen = sizeof(status);
        if (HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &statusLen, nullptr) && status == 200) {
            char buf[8192];
            DWORD got = 0;
            while (InternetReadFile(request, buf, sizeof(buf), &got) && got > 0) {
                if (body.size() + got > maxBytes) {
                    body.clear();       // not the small document that was asked for
                    break;
                }
                body.append(buf, got);
            }
        }
        InternetCloseHandle(request);
    }
    InternetCloseHandle(inet);
    return body;
}
#endif

// The same request through the curl program, which is what the Linux build has (WinINet exists there for FTP only).
// Compiled on Windows too, where it is not used, so that a mistake in it shows up in every build.
inline std::string HttpGetCurl(const wchar_t* url, const std::wstring& userAgent, DWORD timeoutMs, size_t maxBytes) {
#ifdef _WIN32
    wchar_t sysDir[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    const std::wstring curl = std::wstring(sysDir) + L"\\curl.exe";
    if (GetFileAttributesW(curl.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
#else
    const std::wstring curl = ToolProcess::FindExecutable(L"curl", { L"/usr/bin/curl", L"/bin/curl", L"/usr/local/bin/curl" });
    if (curl.empty()) return {};
#endif
    ToolProcess::Options opt;
    opt.timeoutMs = timeoutMs + 3000;
    opt.maxBytes = maxBytes;
    const std::vector<std::wstring> args = {
        L"--silent", L"--fail", L"--location", L"--proto", L"=https",
        L"--max-time", std::to_wstring(std::max<DWORD>(1, timeoutMs / 1000)),
        L"--user-agent", userAgent,
        L"--header", L"Accept: application/vnd.github+json",
        url
    };
    static const std::atomic<bool> kNeverStop{ false };
    const ToolProcess::Result res = ToolProcess::Run(curl, args, opt, kNeverStop);
    if (!res.ok() || res.out.size() > maxBytes) return {};
    return res.out;
}

// Asks GitHub for the tag of the newest release ("v1.0.25"). Empty when there is no answer (offline, a proxy in
// the way, the address changed). Blocks for up to about ten seconds: call it on a worker thread.
inline std::wstring FetchLatestReleaseTag(const std::wstring& userAgent) {
    constexpr DWORD kTimeoutMs = 10000;
    constexpr size_t kMaxBytes = 512u * 1024u;     // the document is a few kilobytes
#ifdef _WIN32
    const std::string json = HttpGetWinInet(kLatestReleaseApiUrl, userAgent, kTimeoutMs, kMaxBytes);
#else
    const std::string json = HttpGetCurl(kLatestReleaseApiUrl, userAgent, kTimeoutMs, kMaxBytes);
#endif
    return TagFromReleaseJson(json);
}

} // namespace Explorer2::UpdateCheck
