#include "NotepadViewer.h"
#include "PathUtils.h"
#include "AppLog.h"
#include <shellapi.h>
#include <commdlg.h>
#include <algorithm>
#include <cstring>

namespace Explorer2 {

static constexpr int ID_NOTEPAD_PANE_EDIT = 1995;
static constexpr size_t kMaxEditableFileBytes = 4 * 1024 * 1024; // 4 MB editable, above that 1 MB preview

static D2D1_COLOR_F NpColor(uint32_t rgb, float a = 1.0f) {
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        a);
}

static WNDPROC g_origNotepadEditProc = nullptr;

static LRESULT CALLBACK NotepadEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    NotepadViewer* self = reinterpret_cast<NotepadViewer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_KEYDOWN) {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrl && (wParam == 'S' || wParam == 's')) {
            if (self) {
                self->SaveFile(nullptr, Lang::PL);
                InvalidateRect(GetParent(hwnd), nullptr, FALSE);
            }
            return 0;
        }
        if (ctrl && (wParam == 'A' || wParam == 'a')) {
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            if (self) InvalidateRect(GetParent(hwnd), nullptr, FALSE);
            return 0;
        }
        if (ctrl && (wParam == 'F' || wParam == 'f')) {
            if (self) self->FindNextInteractive();
            return 0;
        }
        if (wParam == VK_TAB && !ctrl) {
            SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"    "));
            return 0;
        }
    } else if (msg == WM_CHAR) {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrl && (wParam == 19 || wParam == 1 || wParam == 6)) {
            // Suppress beep on Ctrl+S (19), Ctrl+A (1), Ctrl+F (6)
            return 0;
        }
        if (wParam == L'\t') {
            return 0;
        }
    } else if (msg == WM_KEYUP || msg == WM_LBUTTONUP || msg == WM_MOUSEWHEEL) {
        LRESULT res = CallWindowProcW(g_origNotepadEditProc, hwnd, msg, wParam, lParam);
        if (self) {
            InvalidateRect(GetParent(hwnd), nullptr, FALSE);
        }
        return res;
    }
    return CallWindowProcW(g_origNotepadEditProc, hwnd, msg, wParam, lParam);
}

NotepadViewer::~NotepadViewer() {
    Shutdown();
}

void NotepadViewer::Initialize(HWND parentHwnd, HINSTANCE hInstance) {
    m_parentHwnd = parentHwnd;
    m_hInstance = hInstance;

    if (!m_monoFont) {
        m_monoFont = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    }
    CreateOrRecreateEditControl(m_wordWrap);
}

void NotepadViewer::Shutdown() {
    if (m_editHwnd) {
        DestroyWindow(m_editHwnd);
        m_editHwnd = nullptr;
    }
    if (m_monoFont) {
        DeleteObject(m_monoFont);
        m_monoFont = nullptr;
    }
}

void NotepadViewer::CreateOrRecreateEditControl(bool wordWrap) {
    std::wstring prevText;
    DWORD selStart = 0, selEnd = 0;
    bool wasVisible = false;
    if (m_editHwnd) {
        wasVisible = IsWindowVisible(m_editHwnd) != FALSE;
        int len = GetWindowTextLengthW(m_editHwnd);
        if (len > 0) {
            prevText.resize(static_cast<size_t>(len) + 1, L'\0');
            GetWindowTextW(m_editHwnd, prevText.data(), len + 1);
            prevText.resize(static_cast<size_t>(len));
        }
        SendMessageW(m_editHwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
        DestroyWindow(m_editHwnd);
        m_editHwnd = nullptr;
    }

    if (!m_parentHwnd) return;

    DWORD style = WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL;
    if (!wordWrap) {
        style |= WS_HSCROLL | ES_AUTOHSCROLL;
    }

    m_editHwnd = CreateWindowExW(
        0, L"EDIT", L"",
        style,
        0, 0, 100, 100,
        m_parentHwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_NOTEPAD_PANE_EDIT)),
        m_hInstance,
        nullptr);

    if (m_editHwnd) {
        SetWindowLongPtrW(m_editHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        WNDPROC prevProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            m_editHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(NotepadEditSubclassProc)));
        if (!g_origNotepadEditProc) {
            g_origNotepadEditProc = prevProc;
        }
        if (m_monoFont) {
            SendMessageW(m_editHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(m_monoFont), TRUE);
        }
        SendMessageW(m_editHwnd, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
        SendMessageW(m_editHwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));

        if (!prevText.empty()) {
            m_suppressChangeEvent = true;
            SetWindowTextW(m_editHwnd, prevText.c_str());
            SendMessageW(m_editHwnd, EM_SETSEL, selStart, selEnd);
            SendMessageW(m_editHwnd, EM_SETREADONLY, m_isReadOnlyTruncated ? TRUE : FALSE, 0);
            m_suppressChangeEvent = false;
        }
        if (wasVisible && m_lastEditRect.right > m_lastEditRect.left) {
            SetWindowPos(m_editHwnd, HWND_TOP,
                static_cast<int>(m_lastEditRect.left),
                static_cast<int>(m_lastEditRect.top),
                static_cast<int>(m_lastEditRect.right - m_lastEditRect.left),
                static_cast<int>(m_lastEditRect.bottom - m_lastEditRect.top),
                SWP_SHOWWINDOW);
        }
    }
}

bool NotepadViewer::IsLikelyTextFile(const std::wstring& fullPath, const std::wstring& extLower, uint64_t fileSize) {
    static const wchar_t* const kTextExts[] = {
        L".txt", L".md", L".markdown", L".rst", L".log", L".ini", L".cfg", L".conf",
        L".json", L".jsonl", L".xml", L".yaml", L".yml", L".toml", L".csv", L".tsv",
        L".py", L".pyw", L".pyi", L".cpp", L".c", L".cc", L".cxx", L".h", L".hpp", L".hxx", L".inl",
        L".cs", L".js", L".mjs", L".cjs", L".ts", L".tsx", L".jsx", L".vue", L".svelte",
        L".html", L".htm", L".css", L".scss", L".less", L".svg",
        L".sql", L".sh", L".bash", L".zsh", L".bat", L".cmd", L".ps1", L".psm1",
        L".rs", L".go", L".java", L".kt", L".kts", L".gradle", L".php", L".rb", L".lua",
        L".r", L".swift", L".dart", L".scala", L".pl", L".pm", L".tcl", L".awk",
        L".cmake", L".mak", L".mk", L".props", L".targets", L".sln", L".vcxproj", L".csproj",
        L".gitignore", L".gitattributes", L".gitmodules", L".editorconfig", L".env",
        L".reg", L".inf", L".nfo", L".srt", L".vtt", L".ass", L".glsl", L".hlsl", L".fx",
        L".tex", L".bib", L".Diff", L".patch", L".dockerfile", L".manifest"
    };
    for (const wchar_t* e : kTextExts) {
        if (_wcsicmp(extLower.c_str(), e) == 0) return true;
    }

    std::wstring fileName = PathUtils::ToLower(PathUtils::GetFileName(fullPath));
    if (fileName == L"dockerfile" || fileName == L"makefile" || fileName == L"cmakelists.txt" ||
        fileName == L"license" || fileName == L"readme" || fileName == L"authors" ||
        fileName == L"changelog" || fileName == L".gitignore" || fileName == L".env") {
        return true;
    }

    // Never auto-classify known binary formats as text
    if (IsImageExtension(extLower) || IsVideoExtension(extLower) ||
        extLower == L".obj" || extLower == L".stl" || extLower == L".ply" || extLower == L".off" ||
        extLower == L".glb" || extLower == L".fbx" || extLower == L".3ds" || extLower == L".3mf" ||
        extLower == L".exe" || extLower == L".dll" || extLower == L".sys" || extLower == L".bin" ||
        extLower == L".pdb" || extLower == L".zip" || extLower == L".7z"  || extLower == L".rar" ||
        extLower == L".pdf" || extLower == L".safetensors" || extLower == L".gguf" ||
        extLower == L".onnx" || extLower == L".pt" || extLower == L".pth" || extLower == L".ckpt") {
        return false;
    }

    // Heuristic probe for extensionless or custom small files: check first 512 bytes for null bytes
    if (fileSize > 0 && fileSize <= 2 * 1024 * 1024) {
        HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            uint8_t buf[512] = {};
            DWORD readBytes = 0;
            BOOL ok = ReadFile(hFile, buf, sizeof(buf), &readBytes, nullptr);
            CloseHandle(hFile);
            if (ok && readBytes > 0) {
                if (readBytes >= 2 && ((buf[0] == 0xFF && buf[1] == 0xFE) || (buf[0] == 0xFE && buf[1] == 0xFF))) {
                    return true; // UTF-16 BOM
                }
                if (readBytes >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
                    return true; // UTF-8 BOM
                }
                int ctrlCount = 0;
                for (DWORD i = 0; i < readBytes; ++i) {
                    uint8_t c = buf[i];
                    if (c == 0) return false; // Null byte -> binary
                    if (c < 0x20 && c != '\r' && c != '\n' && c != '\t' && c != 0x1B) {
                        ++ctrlCount;
                    }
                }
                if (ctrlCount * 20 <= static_cast<int>(readBytes)) {
                    return true;
                }
            }
        }
    }
    return false;
}

void NotepadViewer::Clear() {
    m_filePath.clear();
    m_isModified = false;
    m_isReadOnlyTruncated = false;
    m_diskFileSize = 0;
    m_totalLines = 1;
    m_totalChars = 0;
    m_caretLine = 1;
    m_caretCol = 1;
    if (m_editHwnd) {
        m_suppressChangeEvent = true;
        SetWindowTextW(m_editHwnd, L"");
        m_suppressChangeEvent = false;
        ShowWindow(m_editHwnd, SW_HIDE);
    }
}

void NotepadViewer::HideWindow() {
    if (m_editHwnd && IsWindowVisible(m_editHwnd)) {
        ShowWindow(m_editHwnd, SW_HIDE);
    }
}

bool NotepadViewer::HasFocus() const {
    return m_editHwnd != nullptr && GetFocus() == m_editHwnd;
}

void NotepadViewer::LoadFile(const std::wstring& fullPath, bool forceLoad) {
    if (!forceLoad && m_filePath == fullPath && !m_filePath.empty()) {
        return;
    }

    m_filePath = fullPath;
    m_isModified = false;
    m_isReadOnlyTruncated = false;
    m_encoding = TextFileEncoding::UTF8;
    m_useCrLf = true;
    m_diskFileSize = 0;

    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (m_editHwnd) {
            m_suppressChangeEvent = true;
            SetWindowTextW(m_editHwnd, L"");
            m_suppressChangeEvent = false;
        }
        return;
    }

    LARGE_INTEGER liSize = {};
    GetFileSizeEx(hFile, &liSize);
    m_diskFileSize = static_cast<uint64_t>(std::max<LONGLONG>(0, liSize.QuadPart));

    size_t bytesToRead = static_cast<size_t>(m_diskFileSize);
    if (m_diskFileSize > kMaxEditableFileBytes) {
        bytesToRead = 1024 * 1024; // 1 MB fast read-only preview for huge log files
        m_isReadOnlyTruncated = true;
    }

    std::vector<uint8_t> raw(bytesToRead);
    DWORD bytesRead = 0;
    if (bytesToRead > 0) {
        ReadFile(hFile, raw.data(), static_cast<DWORD>(bytesToRead), &bytesRead, nullptr);
        raw.resize(bytesRead);
    }
    CloseHandle(hFile);

    std::wstring decoded;
    if (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        m_encoding = TextFileEncoding::UTF8_BOM;
        const char* src = reinterpret_cast<const char*>(raw.data() + 3);
        int srcLen = static_cast<int>(raw.size() - 3);
        if (srcLen > 0) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, src, srcLen, nullptr, 0);
            decoded.resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, src, srcLen, decoded.data(), wlen);
        }
    } else if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        m_encoding = TextFileEncoding::UTF16_LE;
        size_t wchars = (raw.size() - 2) / 2;
        decoded.resize(wchars);
        if (wchars > 0) {
            std::memcpy(decoded.data(), raw.data() + 2, wchars * sizeof(wchar_t));
        }
    } else if (raw.size() >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        m_encoding = TextFileEncoding::UTF16_BE;
        size_t wchars = (raw.size() - 2) / 2;
        decoded.resize(wchars);
        for (size_t i = 0; i < wchars; ++i) {
            uint16_t hi = raw[2 + i * 2];
            uint16_t lo = raw[2 + i * 2 + 1];
            decoded[i] = static_cast<wchar_t>((hi << 8) | lo);
        }
    } else if (!raw.empty()) {
        // Replace any embedded null bytes if user forced Notepad mode on a binary file
        for (uint8_t& b : raw) {
            if (b == 0) b = ' ';
        }
        const char* src = reinterpret_cast<const char*>(raw.data());
        int srcLen = static_cast<int>(raw.size());
        int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, srcLen, nullptr, 0);
        if (wlen > 0) {
            m_encoding = TextFileEncoding::UTF8;
            decoded.resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, src, srcLen, decoded.data(), wlen);
        } else {
            m_encoding = TextFileEncoding::ANSI;
            wlen = MultiByteToWideChar(CP_ACP, 0, src, srcLen, nullptr, 0);
            decoded.resize(wlen);
            MultiByteToWideChar(CP_ACP, 0, src, srcLen, decoded.data(), wlen);
        }
    }

    // Detect original line endings (CRLF vs LF) and normalize to CRLF for Win32 EDIT control
    size_t crlfCount = 0;
    size_t lfOnlyCount = 0;
    std::wstring normalized;
    normalized.reserve(decoded.size() + decoded.size() / 16);
    for (size_t i = 0; i < decoded.size(); ++i) {
        wchar_t c = decoded[i];
        if (c == L'\r') {
            if (i + 1 < decoded.size() && decoded[i + 1] == L'\n') {
                ++crlfCount;
                ++i;
            }
            normalized += L"\r\n";
        } else if (c == L'\n') {
            ++lfOnlyCount;
            normalized += L"\r\n";
        } else if (c == L'\0') {
            normalized += L' ';
        } else {
            normalized += c;
        }
    }
    m_useCrLf = (lfOnlyCount <= crlfCount);

    if (m_editHwnd) {
        m_suppressChangeEvent = true;
        SetWindowTextW(m_editHwnd, normalized.c_str());
        SendMessageW(m_editHwnd, EM_SETREADONLY, m_isReadOnlyTruncated ? TRUE : FALSE, 0);
        SendMessageW(m_editHwnd, EM_SETSEL, 0, 0);
        SendMessageW(m_editHwnd, EM_SCROLLCARET, 0, 0);
        m_suppressChangeEvent = false;
        UpdateCaretStats();
    }
}

void NotepadViewer::ReloadFromDisk() {
    if (!m_filePath.empty()) {
        LoadFile(m_filePath, true);
    }
}

bool NotepadViewer::SaveFile(std::wstring* outStatusToast, Lang lang) {
    const bool pl = (lang == Lang::PL);
    if (m_filePath.empty() || !m_editHwnd) return false;
    if (m_isReadOnlyTruncated) {
        if (outStatusToast) {
            *outStatusToast = pl
                ? L"\x26A0 Plik > 4 MB jest otwarty w trybie podglądu (tylko do odczytu)"
                : L"\x26A0 File > 4 MB is opened in read-only preview mode";
        }
        return false;
    }

    int wlen = GetWindowTextLengthW(m_editHwnd);
    std::wstring text;
    if (wlen > 0) {
        text.resize(static_cast<size_t>(wlen) + 1, L'\0');
        GetWindowTextW(m_editHwnd, text.data(), wlen + 1);
        text.resize(static_cast<size_t>(wlen));
    }

    // Convert CRLF -> LF if original file used LF line endings
    std::wstring outW;
    if (!m_useCrLf) {
        outW.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') {
                continue;
            }
            outW.push_back(text[i]);
        }
    } else {
        outW = std::move(text);
    }

    std::vector<uint8_t> outBytes;
    if (m_encoding == TextFileEncoding::UTF16_LE) {
        outBytes.push_back(0xFF);
        outBytes.push_back(0xFE);
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(outW.data());
        outBytes.insert(outBytes.end(), ptr, ptr + outW.size() * sizeof(wchar_t));
    } else if (m_encoding == TextFileEncoding::UTF16_BE) {
        outBytes.push_back(0xFE);
        outBytes.push_back(0xFF);
        outBytes.reserve(2 + outW.size() * 2);
        for (wchar_t wc : outW) {
            uint16_t u = static_cast<uint16_t>(wc);
            outBytes.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
            outBytes.push_back(static_cast<uint8_t>(u & 0xFF));
        }
    } else if (m_encoding == TextFileEncoding::ANSI) {
        if (!outW.empty()) {
            int mbLen = WideCharToMultiByte(CP_ACP, 0, outW.data(), static_cast<int>(outW.size()), nullptr, 0, nullptr, nullptr);
            outBytes.resize(mbLen);
            WideCharToMultiByte(CP_ACP, 0, outW.data(), static_cast<int>(outW.size()),
                reinterpret_cast<char*>(outBytes.data()), mbLen, nullptr, nullptr);
        }
    } else {
        if (m_encoding == TextFileEncoding::UTF8_BOM) {
            outBytes.push_back(0xEF);
            outBytes.push_back(0xBB);
            outBytes.push_back(0xBF);
        }
        if (!outW.empty()) {
            int mbLen = WideCharToMultiByte(CP_UTF8, 0, outW.data(), static_cast<int>(outW.size()), nullptr, 0, nullptr, nullptr);
            size_t hdr = outBytes.size();
            outBytes.resize(hdr + mbLen);
            WideCharToMultiByte(CP_UTF8, 0, outW.data(), static_cast<int>(outW.size()),
                reinterpret_cast<char*>(outBytes.data() + hdr), mbLen, nullptr, nullptr);
        }
    }

    HANDLE hFile = CreateFileW(m_filePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        AppLog::Error(L"Notepad", L"Save failed for " + m_filePath + L" (err " + std::to_wstring(err) + L")");
        if (outStatusToast) {
            *outStatusToast = (pl ? L"\x2717 Nie udało się zapisać pliku: " : L"\x2717 Could not save file: ") +
                              PathUtils::GetFileName(m_filePath);
        }
        return false;
    }

    DWORD written = 0;
    BOOL ok = TRUE;
    if (!outBytes.empty()) {
        ok = WriteFile(hFile, outBytes.data(), static_cast<DWORD>(outBytes.size()), &written, nullptr);
    }
    CloseHandle(hFile);

    if (ok) {
        m_isModified = false;
        m_diskFileSize = outBytes.size();
        AppLog::Ok(L"Notepad", L"Saved " + m_filePath + L" (" + std::to_wstring(outBytes.size()) + L" bytes)");
        if (outStatusToast) {
            *outStatusToast = (pl ? L"\x2713 Zapisano plik: " : L"\x2713 Saved file: ") +
                              PathUtils::GetFileName(m_filePath) + L" (" + std::to_wstring(outBytes.size()) + L" B)";
        }
        return true;
    }
    return false;
}

void NotepadViewer::ToggleWordWrap() {
    m_wordWrap = !m_wordWrap;
    CreateOrRecreateEditControl(m_wordWrap);
    UpdateCaretStats();
}

void NotepadViewer::FindNextInteractive() {
    if (!m_editHwnd) return;
    int len = GetWindowTextLengthW(m_editHwnd);
    if (len <= 0) return;

    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(m_editHwnd, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));

    DWORD selStart = 0, selEnd = 0;
    SendMessageW(m_editHwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
    if (selEnd > selStart && (selEnd - selStart) <= 128) {
        m_lastFindQuery = text.substr(selStart, selEnd - selStart);
    }
    if (m_lastFindQuery.empty()) {
        // If nothing is selected yet, grab word under caret or prompt via clipboard if available
        if (OpenClipboard(m_parentHwnd)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                const wchar_t* clip = static_cast<const wchar_t*>(GlobalLock(hData));
                if (clip && *clip && wcslen(clip) <= 128) {
                    m_lastFindQuery = clip;
                }
                if (clip) GlobalUnlock(hData);
            }
            CloseClipboard();
        }
    }
    if (m_lastFindQuery.empty()) return;

    std::wstring hay = PathUtils::ToLower(text);
    std::wstring needle = PathUtils::ToLower(m_lastFindQuery);
    size_t pos = hay.find(needle, static_cast<size_t>(selEnd));
    if (pos == std::wstring::npos && selEnd > 0) {
        pos = hay.find(needle, 0); // Wrap around
    }
    if (pos != std::wstring::npos) {
        SetFocus(m_editHwnd);
        SendMessageW(m_editHwnd, EM_SETSEL, static_cast<WPARAM>(pos), static_cast<LPARAM>(pos + needle.size()));
        SendMessageW(m_editHwnd, EM_SCROLLCARET, 0, 0);
        UpdateCaretStats();
    }
}

void NotepadViewer::CopyAllToClipboard() {
    if (!m_editHwnd || !m_parentHwnd) return;
    int len = GetWindowTextLengthW(m_editHwnd);
    if (len <= 0) return;
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(m_editHwnd, text.data(), len + 1);
    if (OpenClipboard(m_parentHwnd)) {
        EmptyClipboard();
        size_t bytes = (static_cast<size_t>(len) + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* p = GlobalLock(hMem);
            if (p) {
                std::memcpy(p, text.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
    }
}

void NotepadViewer::OpenInExternalNotepad() {
    if (m_filePath.empty()) return;
    std::wstring qArg = L"\"" + m_filePath + L"\"";
    ShellExecuteW(m_parentHwnd, L"open", L"notepad.exe", qArg.c_str(), nullptr, SW_SHOWNORMAL);
}

void NotepadViewer::OnEditChanged() {
    if (m_suppressChangeEvent) return;
    m_isModified = true;
    UpdateCaretStats();
}

void NotepadViewer::UpdateCaretStats() {
    if (!m_editHwnd) return;
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(m_editHwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
    int lineIdx = static_cast<int>(SendMessageW(m_editHwnd, EM_LINEFROMCHAR, selEnd, 0));
    int lineStartChar = static_cast<int>(SendMessageW(m_editHwnd, EM_LINEINDEX, lineIdx, 0));
    m_caretLine = std::max(1, lineIdx + 1);
    m_caretCol = std::max(1, static_cast<int>(selEnd) - lineStartChar + 1);
    m_totalLines = std::max(1, static_cast<int>(SendMessageW(m_editHwnd, EM_GETLINECOUNT, 0, 0)));
    m_totalChars = std::max(0, GetWindowTextLengthW(m_editHwnd));
}

void NotepadViewer::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* /*fmtRegular*/,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& rect,
    Lang lang,
    D2D1_POINT_2F /*mousePt*/)
{
    const bool pl = (lang == Lang::PL);
    m_toolbarHits.clear();
    UpdateCaretStats();

    // 1. Toolbar strip at top of Notepad pane (30px)
    float toolbarH = 30.0f;
    float statusH = 24.0f;
    D2D1_RECT_F tbR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + toolbarH);
    brush->SetColor(NpColor(0xF8FAFC));
    rt->FillRectangle(tbR, brush);
    brush->SetColor(NpColor(0xE2E8F0));
    rt->DrawLine(D2D1::Point2F(tbR.left, tbR.bottom - 0.5f), D2D1::Point2F(tbR.right, tbR.bottom - 0.5f), brush, 1.0f);

    float bx = tbR.left + 6.0f;
    auto drawBtn = [&](int id, float w, const wchar_t* label, bool highlighted = false, bool primary = false) {
        if (bx + w > tbR.right - 4.0f) return;
        D2D1_RECT_F br = D2D1::RectF(bx, tbR.top + 4.0f, bx + w, tbR.bottom - 4.0f);
        m_toolbarHits.push_back({ id, br });
        bool hov = (m_hoverBtnId == id);
        if (primary) {
            brush->SetColor(hov ? NpColor(0x005A9E) : NpColor(0x0078D7));
            rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush);
            brush->SetColor(NpColor(0xFFFFFF));
        } else {
            brush->SetColor(highlighted ? NpColor(0xDBEAFE) : (hov ? NpColor(0xE2E8F0) : NpColor(0xFFFFFF)));
            rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush);
            brush->SetColor(highlighted ? NpColor(0x3B82F6) : NpColor(0xCBD5E1));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush, 1.0f);
            brush->SetColor(highlighted ? NpColor(0x1D4ED8) : NpColor(0x1E293B));
        }
        IDWriteTextFormat* tf = primary ? fmtBold : fmtSmall;
        tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), tf, br, brush);
        tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        bx += w + 4.0f;
    };

    drawBtn(BTN_SAVE,        m_isModified ? 92.0f : 72.0f,
            m_isModified ? (pl ? L"\xD83D\xDCBE Zapisz *" : L"\xD83D\xDCBE Save *") : (pl ? L"\xD83D\xDCBE Zapisz" : L"\xD83D\xDCBE Save"),
            false, m_isModified);
    drawBtn(BTN_RELOAD,      68.0f, pl ? L"\x21BA Odśwież" : L"\x21BA Reload");
    drawBtn(BTN_WORDWRAP,    76.0f, pl ? L"\x21C4 Zawijaj" : L"\x21C4 Wrap", m_wordWrap);
    drawBtn(BTN_FIND,        62.0f, pl ? L"\xD83D\xDD0D Szukaj" : L"\xD83D\xDD0D Find");
    drawBtn(BTN_COPY_ALL,    62.0f, pl ? L"\xD83D\xDCCB Kopiuj" : L"\xD83D\xDCCB Copy");
    drawBtn(BTN_EXT_NOTEPAD, 76.0f, pl ? L"\x2197 Notatnik" : L"\x2197 Notepad");

    // 2. Position native Win32 multiline EDIT control inside the body rect
    D2D1_RECT_F editR = D2D1::RectF(rect.left + 1.0f, tbR.bottom, rect.right - 1.0f, std::max(tbR.bottom + 20.0f, rect.bottom - statusH));
    m_lastEditRect = editR;
    if (m_editHwnd) {
        RECT curRc = {};
        GetWindowRect(m_editHwnd, &curRc);
        MapWindowPoints(HWND_DESKTOP, m_parentHwnd, reinterpret_cast<LPPOINT>(&curRc), 2);
        int targetX = static_cast<int>(editR.left);
        int targetY = static_cast<int>(editR.top);
        int targetW = std::max(20, static_cast<int>(editR.right - editR.left));
        int targetH = std::max(20, static_cast<int>(editR.bottom - editR.top));
        if (!IsWindowVisible(m_editHwnd) ||
            curRc.left != targetX || curRc.top != targetY ||
            (curRc.right - curRc.left) != targetW || (curRc.bottom - curRc.top) != targetH)
        {
            SetWindowPos(m_editHwnd, HWND_TOP, targetX, targetY, targetW, targetH, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        }
    }

    // 3. Bottom status bar (Line / Col, Lines, Encoding, CRLF/LF, Modified state)
    D2D1_RECT_F sbR = D2D1::RectF(rect.left, rect.bottom - statusH, rect.right, rect.bottom);
    brush->SetColor(NpColor(0xF1F5F9));
    rt->FillRectangle(sbR, brush);
    brush->SetColor(NpColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(sbR.left, sbR.top), D2D1::Point2F(sbR.right, sbR.top), brush, 1.0f);

    const wchar_t* encName = L"UTF-8";
    switch (m_encoding) {
    case TextFileEncoding::UTF8:     encName = L"UTF-8"; break;
    case TextFileEncoding::UTF8_BOM: encName = L"UTF-8 BOM"; break;
    case TextFileEncoding::UTF16_LE: encName = L"UTF-16 LE"; break;
    case TextFileEncoding::UTF16_BE: encName = L"UTF-16 BE"; break;
    case TextFileEncoding::ANSI:     encName = L"ANSI"; break;
    }

    wchar_t statBuf[256] = {};
    swprintf_s(statBuf,
        pl ? L"Wiersz %d, Kol %d   |   Wierszy: %d   |   Znaków: %d   |   %s (%s)%s"
           : L"Ln %d, Col %d   |   Lines: %d   |   Chars: %d   |   %s (%s)%s",
        m_caretLine, m_caretCol, m_totalLines, m_totalChars,
        encName, m_useCrLf ? L"CRLF" : L"LF",
        m_isReadOnlyTruncated ? (pl ? L"   [Podgląd 1 MB]" : L"   [1 MB Preview]")
                              : (m_isModified ? (pl ? L"   \x25CF Zmodyfikowano (Ctrl+S)" : L"   \x25CF Modified (Ctrl+S)") : L""));

    D2D1_RECT_F txtR = D2D1::RectF(sbR.left + 8.0f, sbR.top, sbR.right - 8.0f, sbR.bottom);
    brush->SetColor(m_isModified ? NpColor(0x0067B8) : NpColor(0x475569));
    rt->DrawTextW(statBuf, static_cast<UINT32>(wcslen(statBuf)), fmtSmall, txtR, brush);
}

bool NotepadViewer::OnMouseDown(float x, float y, Lang lang, std::wstring* outStatusToast) {
    const bool pl = (lang == Lang::PL);
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            switch (hit.id) {
            case BTN_SAVE:
                SaveFile(outStatusToast, lang);
                break;
            case BTN_RELOAD:
                ReloadFromDisk();
                if (outStatusToast) {
                    *outStatusToast = pl ? L"\x21BA Ponownie wczytano plik z dysku" : L"\x21BA Reloaded file from disk";
                }
                break;
            case BTN_WORDWRAP:
                ToggleWordWrap();
                break;
            case BTN_FIND:
                FindNextInteractive();
                if (outStatusToast && m_lastFindQuery.empty()) {
                    *outStatusToast = pl ? L"\xD83D\xDD0D Zaznacz szukane słowo w tekście i kliknij Szukaj (lub Ctrl+F)"
                                         : L"\xD83D\xDD0D Select a word in text and click Find (or Ctrl+F)";
                }
                break;
            case BTN_COPY_ALL:
                CopyAllToClipboard();
                if (outStatusToast) {
                    *outStatusToast = pl ? L"\x2713 Skopiowano zawartość pliku do schowka" : L"\x2713 Copied file content to clipboard";
                }
                break;
            case BTN_EXT_NOTEPAD:
                OpenInExternalNotepad();
                break;
            }
            return true;
        }
    }
    return false;
}

bool NotepadViewer::OnMouseMove(float x, float y) {
    int newHov = 0;
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newHov = hit.id;
            break;
        }
    }
    if (newHov != m_hoverBtnId) {
        m_hoverBtnId = newHov;
        return true;
    }
    return false;
}

} // namespace Explorer2
