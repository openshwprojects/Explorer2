#pragma once
// Rules of "Wyślij do" (Send to) and "E-mail", kept apart from the window so that the self-tests can check them
// (Stage16.Test31 and Test32)
#include "PlatformCompat.h"     // <windows.h> on Windows, FILE_ATTRIBUTE_* on Linux
#include "PathUtils.h"
#include <cwctype>
#include <string>

namespace Explorer2 {

// A mailto: link (RFC 6068) whose subject and body may hold any text: UTF-8 with every byte other than letters,
// digits and "-._~" written as %XX, line breaks as %0D%0A. File paths used to go in as they were, so the first "&",
// "#" or "?" in a name cut the message short.
inline std::wstring MailtoUrl(const std::wstring& subject, const std::wstring& body) {
    auto encode = [](const std::wstring& text) {
        std::wstring lines;
        lines.reserve(text.size());
        for (wchar_t c : text) {
            if (c == L'\r') continue;
            if (c == L'\n') lines += L"\r\n"; else lines += c;
        }
        static const wchar_t kHex[] = L"0123456789ABCDEF";
        std::wstring out;
        for (const unsigned char c : PathUtils::WideToUtf8(lines)) {
            const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                               c == '-' || c == '.' || c == '_' || c == '~';
            if (plain) {
                out += static_cast<wchar_t>(c);
            } else {
                out += L'%';
                out += kHex[c >> 4];
                out += kHex[c & 15];
            }
        }
        return out;
    };
    return L"mailto:?subject=" + encode(subject) + L"&body=" + encode(body);
}

// Which files of the SendTo folder are entries of "Wyślij do": what Windows Explorer lists there - shortcuts,
// programs, "Adresat poczty" (.MAPIMail), "Dokumenty" (.mydocs), "Transfer plików Bluetooth" - except desktop.ini,
// other hidden files and folders, and the two entries Explorer2 makes itself: "Pulpit (utwórz skrót)" (.DeskLink)
// and "Folder skompresowany (zip)" (.ZFSendToTarget). Only .lnk and .exe files used to be listed.
inline bool IsSendToMenuEntry(const std::wstring& fileName, DWORD attributes) {
    if (fileName.empty() || fileName[0] == L'.') return false;
    if (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY)) return false;
    const size_t dot = fileName.find_last_of(L'.');
    std::wstring ext = (dot == std::wstring::npos) ? std::wstring() : fileName.substr(dot);
    for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    return ext != L".desklink" && ext != L".zfsendtotarget" && ext != L".ini";
}

} // namespace Explorer2
