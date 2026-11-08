#include "NotepadViewer.h"
#include "PathUtils.h"
#include "AppLog.h"
#include <shellapi.h>
#include <commdlg.h>
#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace Explorer2 {

static constexpr int ID_NOTEPAD_PANE_EDIT = 1995;
static constexpr size_t kMaxEditableFileBytes = 4 * 1024 * 1024; // 4 MB editable, above that 1 MB preview

static D2D1_COLOR_F NpColor(uint32_t rgb, float a = 1.0f) {
    return PathUtils::ColorFromRgb(rgb, a);
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
            return 0;
        }
        if (wParam == L'\t') {
            return 0;
        }
    } else if (msg == WM_KEYUP || msg == WM_LBUTTONUP || msg == WM_MOUSEWHEEL || msg == WM_VSCROLL) {
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

void NotepadViewer::Initialize(HWND parentHwnd, HINSTANCE hInstance, IDWriteFactory* dwriteFactory) {
    m_parentHwnd = parentHwnd;
    m_hInstance = hInstance;
    m_dwriteFactory = dwriteFactory;

    if (!m_monoFont) {
        m_monoFont = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    }
    if (dwriteFactory && !m_fmtCodeMono) {
        dwriteFactory->CreateTextFormat(
            L"Consolas", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f, L"pl-PL", &m_fmtCodeMono);
        if (m_fmtCodeMono) {
            m_fmtCodeMono->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            m_fmtCodeMono->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (dwriteFactory && !m_fmtCodeBold) {
        dwriteFactory->CreateTextFormat(
            L"Consolas", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f, L"pl-PL", &m_fmtCodeBold);
        if (m_fmtCodeBold) {
            m_fmtCodeBold->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            m_fmtCodeBold->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
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
    if (m_fmtCodeMono) {
        m_fmtCodeMono->Release();
        m_fmtCodeMono = nullptr;
    }
    if (m_fmtCodeBold) {
        m_fmtCodeBold->Release();
        m_fmtCodeBold = nullptr;
    }
}

SyntaxLang NotepadViewer::DetectSyntaxLanguage(const std::wstring& extLower, std::wstring_view firstLine) {
    if (extLower == L".c" || extLower == L".cpp" || extLower == L".cc" || extLower == L".cxx" ||
        extLower == L".h" || extLower == L".hpp" || extLower == L".hxx" || extLower == L".ino" ||
        extLower == L".hlsl" || extLower == L".glsl" || extLower == L".shader") {
        return SyntaxLang::Cpp;
    }
    if (extLower == L".py" || extLower == L".pyw" || extLower == L".pyi") {
        return SyntaxLang::Python;
    }
    if (extLower == L".js" || extLower == L".jsx" || extLower == L".ts" || extLower == L".tsx" ||
        extLower == L".mjs" || extLower == L".cjs" || extLower == L".vue") {
        return SyntaxLang::JavaScript;
    }
    if (extLower == L".json" || extLower == L".jsonl" || extLower == L".har") {
        return SyntaxLang::Json;
    }
    if (extLower == L".xml" || extLower == L".html" || extLower == L".htm" || extLower == L".svg" ||
        extLower == L".xaml" || extLower == L".vcxproj" || extLower == L".csproj" || extLower == L".props") {
        return SyntaxLang::XmlHtml;
    }
    if (extLower == L".css" || extLower == L".scss" || extLower == L".less") {
        return SyntaxLang::Css;
    }
    if (extLower == L".sql") {
        return SyntaxLang::Sql;
    }
    if (extLower == L".cs" || extLower == L".java" || extLower == L".kt") {
        return SyntaxLang::CSharp;
    }
    if (extLower == L".rs" || extLower == L".go" || extLower == L".zig") {
        return SyntaxLang::Rust;
    }
    if (extLower == L".ps1" || extLower == L".bat" || extLower == L".cmd" || extLower == L".sh" || extLower == L".bash") {
        return SyntaxLang::ShellBatch;
    }
    if (extLower == L".ini" || extLower == L".cfg" || extLower == L".conf" || extLower == L".toml" ||
        extLower == L".yaml" || extLower == L".yml" || extLower == L".env" || extLower == L".gitignore") {
        return SyntaxLang::IniYaml;
    }
    if (extLower == L".md" || extLower == L".markdown" || extLower == L".rst") {
        return SyntaxLang::Markdown;
    }
    if (extLower == L".gcode" || extLower == L".nc" || extLower == L".kicad_pcb" || extLower == L".kicad_sch") {
        return SyntaxLang::GCodeEda;
    }
    if (!firstLine.empty()) {
        if (firstLine.rfind(L"<?xml", 0) == 0 || firstLine.rfind(L"<!DOCTYPE", 0) == 0) return SyntaxLang::XmlHtml;
        if (firstLine.rfind(L"#!/", 0) == 0 && firstLine.find(L"python") != std::wstring_view::npos) return SyntaxLang::Python;
        if (firstLine.rfind(L"#!/", 0) == 0) return SyntaxLang::ShellBatch;
        if (firstLine.front() == L'{' || firstLine.front() == L'[') return SyntaxLang::Json;
    }
    return SyntaxLang::PlainText;
}

const wchar_t* NotepadViewer::GetSyntaxLanguageName(SyntaxLang lang) {
    switch (lang) {
    case SyntaxLang::Cpp:        return L"C/C++";
    case SyntaxLang::Python:     return L"Python";
    case SyntaxLang::JavaScript: return L"JS/TS";
    case SyntaxLang::Json:       return L"JSON";
    case SyntaxLang::XmlHtml:    return L"XML/HTML";
    case SyntaxLang::Css:        return L"CSS";
    case SyntaxLang::Sql:        return L"SQL";
    case SyntaxLang::CSharp:     return L"C#/Java";
    case SyntaxLang::Rust:       return L"Rust/Go";
    case SyntaxLang::ShellBatch: return L"Shell/PS1";
    case SyntaxLang::IniYaml:    return L"INI/YAML";
    case SyntaxLang::Markdown:   return L"Markdown";
    case SyntaxLang::GCodeEda:   return L"G-Code/EDA";
    default:                     return L"Text";
    }
}

static bool IsIdentStart(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_' || c == L'$';
}

static bool IsIdentCont(wchar_t c) {
    return IsIdentStart(c) || (c >= L'0' && c <= L'9');
}

static bool IsCodeKeyword(std::wstring_view word, SyntaxLang lang) {
    static const std::unordered_set<std::wstring_view> kCommonKeywords = {
        L"if", L"else", L"for", L"while", L"do", L"switch", L"case", L"default", L"break", L"continue",
        L"return", L"try", L"catch", L"throw", L"new", L"delete", L"class", L"struct", L"enum", L"union",
        L"public", L"private", L"protected", L"virtual", L"override", L"final", L"static", L"const",
        L"constexpr", L"inline", L"template", L"typename", L"namespace", L"using", L"sizeof", L"nullptr",
        L"true", L"false", L"null", L"None", L"True", L"False", L"def", L"import", L"from", L"as",
        L"pass", L"lambda", L"yield", L"with", L"async", L"await", L"elif", L"except", L"finally",
        L"raise", L"global", L"nonlocal", L"and", L"or", L"not", L"in", L"is", L"function", L"let",
        L"var", L"export", L" interface", L"implements", L"extends", L"super", L"this", L"self",
        L"fn", L"mut", L"pub", L"impl", L"trait", L"match", L"where", L"use", L"mod", L"crate",
        L"func", L"package", L"defer", L"go", L"chan", L"select", L"foreach"
    };
    if (kCommonKeywords.count(word) > 0) return true;
    if (lang == SyntaxLang::Sql) {
        std::wstring up = PathUtils::ToUpper(word);
        return up == L"SELECT" || up == L"FROM" || up == L"WHERE" || up == L"JOIN" || up == L"LEFT" ||
               up == L"INNER" || up == L"ON" || up == L"GROUP" || up == L"BY" || up == L"ORDER" ||
               up == L"INSERT" || up == L"INTO" || up == L"VALUES" || up == L"UPDATE" || up == L"SET" ||
               up == L"DELETE" || up == L"CREATE" || up == L"TABLE" || up == L"PRIMARY" || up == L"KEY" ||
               up == L"AND" || up == L"OR" || up == L"NOT" || up == L"NULL" || up == L"LIMIT" || up == L"AS";
    }
    return false;
}

static bool IsCodeType(std::wstring_view word) {
    static const std::unordered_set<std::wstring_view> kTypes = {
        L"void", L"bool", L"char", L"wchar_t", L"short", L"int", L"long", L"float", L"double",
        L"signed", L"unsigned", L"size_t", L"uint8_t", L"uint16_t", L"uint32_t", L"uint64_t",
        L"int8_t", L"int16_t", L"int32_t", L"int64_t", L"string", L"wstring", L"string_view",
        L"wstring_view", L"vector", L"map", L"unordered_map", L"set", L"unordered_set", L"pair",
        L"std", L"BOOL", L"DWORD", L"WORD", L"BYTE", L"HANDLE", L"HWND", L"HINSTANCE", L"LRESULT",
        L"WPARAM", L"LPARAM", L"UINT", L"ULONG", L"LONGLONG", L"ULONGLONG", L"HRESULT", L"str", L"dict", L"list"
    };
    return kTypes.count(word) > 0;
}

void NotepadViewer::TokenizeLine(
    std::wstring_view line,
    SyntaxLang lang,
    bool& inOutBlockComment,
    std::vector<SyntaxToken>& outTokens)
{
    outTokens.clear();
    if (line.empty()) return;

    if (lang == SyntaxLang::PlainText) {
        outTokens.push_back({ 0, static_cast<uint32_t>(line.size()), SyntaxTokenKind::Plain });
        return;
    }

    // Markdown headings / quotes / code blocks
    if (lang == SyntaxLang::Markdown) {
        size_t firstNonSpace = line.find_first_not_of(L" \t");
        if (firstNonSpace != std::wstring_view::npos) {
            if (line[firstNonSpace] == L'#') {
                outTokens.push_back({ 0, static_cast<uint32_t>(line.size()), SyntaxTokenKind::Keyword });
                return;
            }
            if (line[firstNonSpace] == L'>' || line.substr(firstNonSpace, 3) == L"```") {
                outTokens.push_back({ 0, static_cast<uint32_t>(line.size()), SyntaxTokenKind::Comment });
                return;
            }
        }
    }

    // INI / YAML section headers [Section] and key=value / key: value
    if (lang == SyntaxLang::IniYaml) {
        size_t firstNonSpace = line.find_first_not_of(L" \t");
        if (firstNonSpace != std::wstring_view::npos) {
            wchar_t fc = line[firstNonSpace];
            if (fc == L'#' || fc == L';') {
                outTokens.push_back({ 0, static_cast<uint32_t>(line.size()), SyntaxTokenKind::Comment });
                return;
            }
            if (fc == L'[') {
                outTokens.push_back({ 0, static_cast<uint32_t>(line.size()), SyntaxTokenKind::Preprocessor });
                return;
            }
            size_t sep = line.find_first_of(L"=:", firstNonSpace);
            if (sep != std::wstring_view::npos) {
                outTokens.push_back({ 0, static_cast<uint32_t>(sep), SyntaxTokenKind::TagOrKey });
                outTokens.push_back({ static_cast<uint32_t>(sep), 1, SyntaxTokenKind::Operator });
                if (sep + 1 < line.size()) {
                    outTokens.push_back({ static_cast<uint32_t>(sep + 1), static_cast<uint32_t>(line.size() - sep - 1), SyntaxTokenKind::String });
                }
                return;
            }
        }
    }

    size_t i = 0;
    size_t n = line.size();

    while (i < n) {
        if (inOutBlockComment) {
            size_t endBlk = line.find(L"*/", i);
            if (endBlk == std::wstring_view::npos) {
                outTokens.push_back({ static_cast<uint32_t>(i), static_cast<uint32_t>(n - i), SyntaxTokenKind::Comment });
                return;
            }
            outTokens.push_back({ static_cast<uint32_t>(i), static_cast<uint32_t>(endBlk + 2 - i), SyntaxTokenKind::Comment });
            inOutBlockComment = false;
            i = endBlk + 2;
            continue;
        }

        wchar_t c = line[i];
        wchar_t nextC = (i + 1 < n) ? line[i + 1] : L'\0';

        // Whitespace
        if (c == L' ' || c == L'\t') {
            size_t start = i;
            while (i < n && (line[i] == L' ' || line[i] == L'\t')) ++i;
            outTokens.push_back({ static_cast<uint32_t>(start), static_cast<uint32_t>(i - start), SyntaxTokenKind::Plain });
            continue;
        }

        // Line comments: //, /*, #, --, ;
        if ((c == L'/' && nextC == L'/') ||
            (c == L'#' && (lang == SyntaxLang::Python || lang == SyntaxLang::ShellBatch || lang == SyntaxLang::IniYaml)) ||
            (c == L'-' && nextC == L'-' && lang == SyntaxLang::Sql) ||
            (c == L';' && lang == SyntaxLang::GCodeEda))
        {
            outTokens.push_back({ static_cast<uint32_t>(i), static_cast<uint32_t>(n - i), SyntaxTokenKind::Comment });
            return;
        }

        if (c == L'/' && nextC == L'*') {
            inOutBlockComment = true;
            continue;
        }

        // Preprocessor (#include, #pragma, #define) or @decorator
        if ((c == L'#' && (lang == SyntaxLang::Cpp || lang == SyntaxLang::CSharp)) ||
            (c == L'@' && (lang == SyntaxLang::Python || lang == SyntaxLang::JavaScript || lang == SyntaxLang::CSharp)))
        {
            size_t start = i++;
            while (i < n && (IsIdentCont(line[i]) || line[i] == L' ' || line[i] == L'<' || line[i] == L'>' || line[i] == L'.' || line[i] == L'/')) {
                if (line[i] == L' ' && i > start + 10) break;
                ++i;
            }
            outTokens.push_back({ static_cast<uint32_t>(start), static_cast<uint32_t>(i - start), SyntaxTokenKind::Preprocessor });
            continue;
        }

        // XML / HTML Tags <tag ...>
        if (lang == SyntaxLang::XmlHtml && c == L'<') {
            if (line.substr(i, 4) == L"<!--") {
                size_t endCmt = line.find(L"-->", i + 4);
                size_t len = (endCmt == std::wstring_view::npos) ? (n - i) : (endCmt + 3 - i);
                outTokens.push_back({ static_cast<uint32_t>(i), static_cast<uint32_t>(len), SyntaxTokenKind::Comment });
                i += len;
                continue;
            }
            size_t gt = line.find(L'>', i + 1);
            size_t len = (gt == std::wstring_view::npos) ? (n - i) : (gt + 1 - i);
            outTokens.push_back({ static_cast<uint32_t>(i), static_cast<uint32_t>(len), SyntaxTokenKind::TagOrKey });
            i += len;
            continue;
        }

        // String or Char literal ("..." or '...')
        if (c == L'"' || c == L'\'' || c == L'`') {
            wchar_t quote = c;
            size_t start = i++;
            while (i < n) {
                if (line[i] == L'\\' && i + 1 < n) {
                    i += 2;
                    continue;
                }
                if (line[i] == quote) {
                    ++i;
                    break;
                }
                ++i;
            }
            // In JSON, if followed by ':', highlight as TagOrKey!
            SyntaxTokenKind sk = SyntaxTokenKind::String;
            if (lang == SyntaxLang::Json && quote == L'"') {
                size_t k = i;
                while (k < n && (line[k] == L' ' || line[k] == L'\t')) ++k;
                if (k < n && line[k] == L':') sk = SyntaxTokenKind::TagOrKey;
            }
            outTokens.push_back({ static_cast<uint32_t>(start), static_cast<uint32_t>(i - start), sk });
            continue;
        }

        // Numeric literal (decimal, hex 0x..., float)
        if ((c >= L'0' && c <= L'9') || (c == L'-' && nextC >= L'0' && nextC <= L'9')) {
            size_t start = i++;
            while (i < n && ((line[i] >= L'0' && line[i] <= L'9') ||
                             (line[i] >= L'a' && line[i] <= L'f') ||
                             (line[i] >= L'A' && line[i] <= L'F') ||
                             line[i] == L'x' || line[i] == L'X' || line[i] == L'.' || line[i] == L'u' || line[i] == L'L')) {
                ++i;
            }
            outTokens.push_back({ static_cast<uint32_t>(start), static_cast<uint32_t>(i - start), SyntaxTokenKind::Number });
            continue;
        }

        // Identifiers / Keywords / Types
        if (IsIdentStart(c)) {
            size_t start = i++;
            while (i < n && IsIdentCont(line[i])) ++i;
            std::wstring_view word = line.substr(start, i - start);
            SyntaxTokenKind tk = SyntaxTokenKind::Plain;
            if (IsCodeKeyword(word, lang)) {
                tk = SyntaxTokenKind::Keyword;
            } else if (IsCodeType(word)) {
                tk = SyntaxTokenKind::Type;
            } else if (lang == SyntaxLang::GCodeEda && word.size() >= 2 &&
                       (word[0] == L'G' || word[0] == L'M' || word[0] == L'X' || word[0] == L'Y' || word[0] == L'Z')) {
                tk = SyntaxTokenKind::Keyword;
            }
            outTokens.push_back({ static_cast<uint32_t>(start), static_cast<uint32_t>(i - start), tk });
            continue;
        }

        // Operators & Punctuation
        size_t start = i++;
        outTokens.push_back({ static_cast<uint32_t>(start), 1, SyntaxTokenKind::Operator });
    }
}

void NotepadViewer::RebuildLinesFromText(const std::wstring& text) {
    m_codeLines.clear();
    m_codeLines.reserve(256);

    bool inBlockComment = false;
    size_t pos = 0;
    size_t n = text.size();
    while (pos <= n) {
        size_t eol = text.find(L'\n', pos);
        size_t lineEnd = (eol == std::wstring::npos) ? n : eol;
        size_t cleanEnd = lineEnd;
        if (cleanEnd > pos && text[cleanEnd - 1] == L'\r') --cleanEnd;

        CodeLineEntry entry;
        // Expand tabs to 4 spaces for clean syntax rendering alignment
        std::wstring_view rawSv(text.data() + pos, cleanEnd - pos);
        entry.text.reserve(rawSv.size());
        for (wchar_t ch : rawSv) {
            if (ch == L'\t') {
                if (m_showSymbols) entry.text += L"\x2192   ";
                else entry.text += L"    ";
            } else {
                entry.text.push_back(ch);
            }
        }
        if (!rawSv.empty() && (rawSv.back() == L'{' || rawSv.back() == L':')) {
            entry.isFoldHeader = true;
        }
        TokenizeLine(entry.text, m_syntaxLang, inBlockComment, entry.tokens);
        m_codeLines.push_back(std::move(entry));

        if (eol == std::wstring::npos) break;
        pos = eol + 1;
    }

    if (m_codeLines.empty()) {
        m_codeLines.push_back(CodeLineEntry{});
    }
    m_totalLines = static_cast<int>(m_codeLines.size());
    m_totalChars = static_cast<int>(text.size());
}

void NotepadViewer::SyncFromEditControlToLines() {
    if (!m_editHwnd) return;
    int len = GetWindowTextLengthW(m_editHwnd);
    std::wstring text;
    if (len > 0) {
        text.resize(static_cast<size_t>(len) + 1, L'\0');
        GetWindowTextW(m_editHwnd, text.data(), len + 1);
        text.resize(static_cast<size_t>(len));
    }
    RebuildLinesFromText(text);
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
        if (m_monoFont) {
            SendMessageW(m_editHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(m_monoFont), FALSE);
        }
        SendMessageW(m_editHwnd, EM_SETLIMITTEXT, static_cast<WPARAM>(kMaxEditableFileBytes), 0);
        SetWindowLongPtrW(m_editHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        g_origNotepadEditProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(m_editHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(NotepadEditSubclassProc)));

        if (!prevText.empty()) {
            m_suppressChangeEvent = true;
            SetWindowTextW(m_editHwnd, prevText.c_str());
            SendMessageW(m_editHwnd, EM_SETSEL, selStart, selEnd);
            m_suppressChangeEvent = false;
        }
        if (wasVisible && !m_syntaxMode) {
            ShowWindow(m_editHwnd, SW_SHOWNOACTIVATE);
        }
    }
}

void NotepadViewer::HideWindow() {
    if (m_editHwnd && IsWindowVisible(m_editHwnd)) {
        ShowWindow(m_editHwnd, SW_HIDE);
    }
}

bool NotepadViewer::HasFocus() const {
    return m_editHwnd && ::GetFocus() == m_editHwnd;
}

bool NotepadViewer::IsLikelyTextFile(const std::wstring& fullPath, const std::wstring& extLower, uint64_t fileSize) {
    if (extLower == L".txt"  || extLower == L".md"   || extLower == L".log"  || extLower == L".ini"  ||
        extLower == L".cfg"  || extLower == L".conf" || extLower == L".json" || extLower == L".xml"  ||
        extLower == L".yaml" || extLower == L".yml"  || extLower == L".toml" ||
        extLower == L".py"   || extLower == L".cpp"  || extLower == L".c"    || extLower == L".h"    ||
        extLower == L".hpp"  || extLower == L".cs"   || extLower == L".js"   || extLower == L".ts"   ||
        extLower == L".jsx"  || extLower == L".tsx"  || extLower == L".html" || extLower == L".htm"  ||
        extLower == L".css"  || extLower == L".scss" || extLower == L".sql"  || extLower == L".sh"   ||
        extLower == L".bat"  || extLower == L".cmd"  || extLower == L".ps1"  || extLower == L".reg"  ||
        extLower == L".rs"   || extLower == L".go"   || extLower == L".java" || extLower == L".php"  ||
        extLower == L".rb"   || extLower == L".lua"  || extLower == L".cmake"|| extLower == L".gitignore" ||
        extLower == L".gitattributes" || extLower == L".dockerignore" || extLower == L".env" ||
        extLower == L".sln"  || extLower == L".vcxproj" || extLower == L".props" || extLower == L".targets" ||
        extLower == L".nfo"  || extLower == L".srt"  || extLower == L".vtt"  || extLower == L".gcode")
    {
        return true;
    }

    if (fileSize == 0) return true;
    if (extLower == L".exe" || extLower == L".dll" || extLower == L".sys" || extLower == L".obj" ||
        extLower == L".lib" || extLower == L".pdb" || extLower == L".bin" || extLower == L".iso" ||
        extLower == L".zip" || extLower == L".7z"  || extLower == L".rar" || extLower == L".tar" ||
        extLower == L".gz"  || extLower == L".safetensors" || extLower == L".gguf" || extLower == L".pt" ||
        extLower == L".pth" || extLower == L".onnx" || extLower == L".stl" || extLower == L".ply" ||
        extLower == L".glb" || extLower == L".fbx" || extLower == L".pdf" || extLower == L".xlsx" ||
        extLower == L".xls" || extLower == L".docx" || extLower == L".tga" || extLower == L".psd" ||
        extLower == L".xcf")
    {
        return false;
    }

    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    uint8_t sample[1024] = {};
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, sample, sizeof(sample), &bytesRead, nullptr);
    CloseHandle(hFile);
    if (!ok || bytesRead == 0) return false;

    if (bytesRead >= 2 && ((sample[0] == 0xFF && sample[1] == 0xFE) || (sample[0] == 0xFE && sample[1] == 0xFF))) {
        return true;
    }
    if (bytesRead >= 3 && sample[0] == 0xEF && sample[1] == 0xBB && sample[2] == 0xBF) {
        return true;
    }

    size_t zeroBytes = 0;
    size_t controlBytes = 0;
    for (DWORD i = 0; i < bytesRead; ++i) {
        uint8_t b = sample[i];
        if (b == 0) ++zeroBytes;
        else if (b < 0x09 || (b > 0x0D && b < 0x20 && b != 0x1B)) ++controlBytes;
    }
    return zeroBytes == 0 && (controlBytes * 20 <= bytesRead);
}

void NotepadViewer::Clear() {
    m_filePath.clear();
    m_isModified = false;
    m_isReadOnlyTruncated = false;
    m_diskFileSize = 0;
    m_codeLines.clear();
    m_topVisibleLine = 0;
    m_selectedLineIdx = 0;
    if (m_editHwnd) {
        m_suppressChangeEvent = true;
        SetWindowTextW(m_editHwnd, L"");
        m_suppressChangeEvent = false;
    }
}

void NotepadViewer::LoadFile(const std::wstring& fullPath, bool forceLoad) {
    if (!forceLoad && m_filePath == fullPath && !m_filePath.empty()) {
        return;
    }
    m_filePath = fullPath;
    m_isModified = false;
    m_isReadOnlyTruncated = false;
    m_diskFileSize = 0;
    m_topVisibleLine = 0;
    m_selectedLineIdx = 0;

    if (!m_editHwnd) {
        CreateOrRecreateEditControl(m_wordWrap);
    }

    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        m_suppressChangeEvent = true;
        if (m_editHwnd) SetWindowTextW(m_editHwnd, L"");
        m_suppressChangeEvent = false;
        RebuildLinesFromText(L"");
        return;
    }

    LARGE_INTEGER liSize = {};
    if (GetFileSizeEx(hFile, &liSize) && liSize.QuadPart > 0) {
        m_diskFileSize = static_cast<uint64_t>(liSize.QuadPart);
    }

    size_t readCap = static_cast<size_t>(std::min<uint64_t>(m_diskFileSize, kMaxEditableFileBytes));
    if (m_diskFileSize > kMaxEditableFileBytes) {
        readCap = 1024 * 1024;
        m_isReadOnlyTruncated = true;
    }

    std::vector<uint8_t> raw(readCap);
    DWORD bytesRead = 0;
    if (readCap > 0) {
        ReadFile(hFile, raw.data(), static_cast<DWORD>(readCap), &bytesRead, nullptr);
        raw.resize(bytesRead);
    }
    CloseHandle(hFile);

    std::wstring decoded;
    m_encoding = TextFileEncoding::UTF8;

    if (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        m_encoding = TextFileEncoding::UTF8_BOM;
        const char* src = reinterpret_cast<const char*>(raw.data() + 3);
        int srcLen = static_cast<int>(raw.size() - 3);
        int wlen = MultiByteToWideChar(CP_UTF8, 0, src, srcLen, nullptr, 0);
        if (wlen > 0) {
            decoded.resize(static_cast<size_t>(wlen));
            MultiByteToWideChar(CP_UTF8, 0, src, srcLen, decoded.data(), wlen);
        }
    } else if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        m_encoding = TextFileEncoding::UTF16_LE;
        size_t wchars = (raw.size() - 2) / 2;
        decoded.resize(wchars);
        if (wchars > 0) std::memcpy(decoded.data(), raw.data() + 2, wchars * 2);
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
        const char* src = reinterpret_cast<const char*>(raw.data());
        int srcLen = static_cast<int>(raw.size());
        int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, srcLen, nullptr, 0);
        if (wlen > 0) {
            m_encoding = TextFileEncoding::UTF8;
            decoded.resize(static_cast<size_t>(wlen));
            MultiByteToWideChar(CP_UTF8, 0, src, srcLen, decoded.data(), wlen);
        } else {
            m_encoding = TextFileEncoding::ANSI;
            wlen = MultiByteToWideChar(CP_ACP, 0, src, srcLen, nullptr, 0);
            if (wlen > 0) {
                decoded.resize(static_cast<size_t>(wlen));
                MultiByteToWideChar(CP_ACP, 0, src, srcLen, decoded.data(), wlen);
            }
        }
    }

    m_useCrLf = (decoded.find(L"\r\n") != std::wstring::npos) || (decoded.find(L'\n') == std::wstring::npos);
    std::wstring normalized;
    normalized.reserve(decoded.size() + decoded.size() / 16);
    for (size_t i = 0; i < decoded.size(); ++i) {
        wchar_t c = decoded[i];
        if (c == L'\0') c = L' ';
        if (c == L'\r') {
            normalized += L"\r\n";
            if (i + 1 < decoded.size() && decoded[i + 1] == L'\n') ++i;
        } else if (c == L'\n') {
            normalized += L"\r\n";
        } else {
            normalized.push_back(c);
        }
    }

    std::wstring extLower = PathUtils::GetExtension(fullPath);
    size_t firstEol = normalized.find(L"\r\n");
    std::wstring_view firstLine = (firstEol != std::wstring::npos)
        ? std::wstring_view(normalized.data(), firstEol)
        : std::wstring_view(normalized);
    m_syntaxLang = DetectSyntaxLanguage(extLower, firstLine);

    RebuildLinesFromText(normalized);

    if (m_editHwnd) {
        m_suppressChangeEvent = true;
        SetWindowTextW(m_editHwnd, normalized.c_str());
        SendMessageW(m_editHwnd, EM_SETREADONLY, m_isReadOnlyTruncated ? TRUE : FALSE, 0);
        SendMessageW(m_editHwnd, EM_SETSEL, 0, 0);
        m_suppressChangeEvent = false;
    }
    UpdateCaretStats();
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
            *outStatusToast = pl ? L"\x26A0 Plik > 4 MB jest otwarty w trybie tylko do odczytu"
                                 : L"\x26A0 File > 4 MB is opened in read-only preview mode";
        }
        return false;
    }

    int len = GetWindowTextLengthW(m_editHwnd);
    std::wstring text(static_cast<size_t>(std::max(0, len)) + 1, L'\0');
    if (len > 0) {
        GetWindowTextW(m_editHwnd, text.data(), len + 1);
    }
    text.resize(static_cast<size_t>(std::max(0, len)));
    RebuildLinesFromText(text);

    std::wstring outW;
    if (m_useCrLf) {
        outW = std::move(text);
    } else {
        outW.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') continue;
            outW.push_back(text[i]);
        }
    }

    std::vector<uint8_t> outBytes;
    if (m_encoding == TextFileEncoding::UTF16_LE) {
        outBytes.push_back(0xFF);
        outBytes.push_back(0xFE);
        size_t byteCount = outW.size() * sizeof(wchar_t);
        outBytes.resize(2 + byteCount);
        if (byteCount > 0) std::memcpy(outBytes.data() + 2, outW.data(), byteCount);
    } else if (m_encoding == TextFileEncoding::UTF16_BE) {
        outBytes.push_back(0xFE);
        outBytes.push_back(0xFF);
        outBytes.resize(2 + outW.size() * 2);
        for (size_t i = 0; i < outW.size(); ++i) {
            uint16_t v = static_cast<uint16_t>(outW[i]);
            outBytes[2 + i * 2]     = static_cast<uint8_t>((v >> 8) & 0xFF);
            outBytes[2 + i * 2 + 1] = static_cast<uint8_t>(v & 0xFF);
        }
    } else {
        UINT cp = (m_encoding == TextFileEncoding::ANSI) ? CP_ACP : CP_UTF8;
        if (m_encoding == TextFileEncoding::UTF8_BOM) {
            outBytes.push_back(0xEF);
            outBytes.push_back(0xBB);
            outBytes.push_back(0xBF);
        }
        if (!outW.empty()) {
            int mbLen = WideCharToMultiByte(cp, 0, outW.data(), static_cast<int>(outW.size()), nullptr, 0, nullptr, nullptr);
            if (mbLen > 0) {
                size_t hdr = outBytes.size();
                outBytes.resize(hdr + mbLen);
                WideCharToMultiByte(cp, 0, outW.data(), static_cast<int>(outW.size()),
                    reinterpret_cast<char*>(outBytes.data() + hdr), mbLen, nullptr, nullptr);
            }
        }
    }

    if (PathUtils::WriteFileBytes(m_filePath, outBytes.data(), outBytes.size())) {
        m_isModified = false;
        m_diskFileSize = outBytes.size();
        AppLog::Ok(L"Notepad", L"Saved " + m_filePath + L" (" + std::to_wstring(outBytes.size()) + L" bytes)");
        if (outStatusToast) {
            *outStatusToast = (pl ? L"\x2713 Zapisano plik: " : L"\x2713 Saved file: ") +
                              PathUtils::GetFileName(m_filePath) + L" (" + std::to_wstring(outBytes.size()) + L" B)";
        }
        return true;
    }

    if (outStatusToast) {
        *outStatusToast = (pl ? L"\x2717 Nie udało się zapisać pliku: " : L"\x2717 Could not save file: ") +
                          PathUtils::GetFileName(m_filePath);
    }
    return false;
}

void NotepadViewer::ToggleWordWrap() {
    m_wordWrap = !m_wordWrap;
    CreateOrRecreateEditControl(m_wordWrap);
    UpdateCaretStats();
}

void NotepadViewer::ToggleSyntaxMode() {
    if (!m_syntaxMode) {
        SyncFromEditControlToLines();
        HideWindow();
        m_syntaxMode = true;
    } else {
        m_syntaxMode = false;
        if (m_editHwnd) {
            ShowWindow(m_editHwnd, SW_SHOWNOACTIVATE);
            SetFocus(m_editHwnd);
            int charIdx = static_cast<int>(SendMessageW(m_editHwnd, EM_LINEINDEX, m_selectedLineIdx, 0));
            if (charIdx >= 0) {
                SendMessageW(m_editHwnd, EM_SETSEL, charIdx, charIdx);
                SendMessageW(m_editHwnd, EM_SCROLLCARET, 0, 0);
            }
        }
    }
}

void NotepadViewer::ToggleShowSymbols() {
    m_showSymbols = !m_showSymbols;
    SyncFromEditControlToLines();
}

void NotepadViewer::ToggleLineEndings() {
    m_useCrLf = !m_useCrLf;
    m_isModified = true;
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
    if (m_lastFindQuery.empty() && OpenClipboard(m_parentHwnd)) {
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
    if (m_lastFindQuery.empty()) return;

    std::wstring hay = PathUtils::ToLower(text);
    std::wstring needle = PathUtils::ToLower(m_lastFindQuery);
    size_t pos = hay.find(needle, static_cast<size_t>(selEnd));
    if (pos == std::wstring::npos && selEnd > 0) {
        pos = hay.find(needle, 0);
    }
    if (pos != std::wstring::npos) {
        SendMessageW(m_editHwnd, EM_SETSEL, static_cast<WPARAM>(pos), static_cast<LPARAM>(pos + needle.size()));
        SendMessageW(m_editHwnd, EM_SCROLLCARET, 0, 0);
        int lineIdx = static_cast<int>(SendMessageW(m_editHwnd, EM_LINEFROMCHAR, pos, 0));
        m_selectedLineIdx = std::max(0, lineIdx);
        m_topVisibleLine = std::max(0, lineIdx - 4);
        UpdateCaretStats();
    }
}

void NotepadViewer::CopyAllToClipboard() {
    if (!m_editHwnd || !m_parentHwnd) return;
    int len = GetWindowTextLengthW(m_editHwnd);
    if (len <= 0) return;
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(m_editHwnd, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
    PathUtils::CopyWideToClipboard(m_parentHwnd, text);
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
    if (!m_syntaxMode) {
        DWORD selStart = 0, selEnd = 0;
        SendMessageW(m_editHwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
        int lineIdx = static_cast<int>(SendMessageW(m_editHwnd, EM_LINEFROMCHAR, selEnd, 0));
        int lineStartChar = static_cast<int>(SendMessageW(m_editHwnd, EM_LINEINDEX, lineIdx, 0));
        m_caretLine = std::max(1, lineIdx + 1);
        m_caretCol = std::max(1, static_cast<int>(selEnd) - lineStartChar + 1);
        m_totalLines = std::max(1, static_cast<int>(SendMessageW(m_editHwnd, EM_GETLINECOUNT, 0, 0)));
        m_totalChars = std::max(0, GetWindowTextLengthW(m_editHwnd));
    } else {
        m_caretLine = std::max(1, m_selectedLineIdx + 1);
        m_caretCol = 1;
        m_totalLines = std::max(1, static_cast<int>(m_codeLines.size()));
    }
}

static D2D1_COLOR_F ColorForSyntaxToken(SyntaxTokenKind kind) {
    switch (kind) {
    case SyntaxTokenKind::Keyword:      return NpColor(0x0033B3); // Notepad++ / JetBrains Bold Blue
    case SyntaxTokenKind::Type:         return NpColor(0x0369A1); // Teal Type
    case SyntaxTokenKind::Preprocessor: return NpColor(0x7C3AED); // Purple Directive
    case SyntaxTokenKind::String:       return NpColor(0x15803D); // Emerald String
    case SyntaxTokenKind::Comment:      return NpColor(0x64748B); // Slate Comment
    case SyntaxTokenKind::Number:       return NpColor(0xD97706); // Amber Number
    case SyntaxTokenKind::TagOrKey:     return NpColor(0x0284C7); // XML Tag / JSON Key
    case SyntaxTokenKind::Operator:     return NpColor(0x334155); // Dark Operator
    default:                            return NpColor(0x0F172A); // Plain Code
    }
}

void NotepadViewer::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& rect,
    Lang lang,
    D2D1_POINT_2F /*mousePt*/)
{
    const bool pl = (lang == Lang::PL);
    m_toolbarHits.clear();
    UpdateCaretStats();

    // 1. Pro Toolbar strip at top of Notepad++ pane (30px)
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
        rt->DrawText(label, static_cast<UINT32>(wcslen(label)), tf, br, brush);
        tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        bx += w + 4.0f;
    };

    drawBtn(BTN_SAVE, m_isModified ? 82.0f : 66.0f,
            m_isModified ? (pl ? L"\xD83D\xDCBE Zapisz *" : L"\xD83D\xDCBE Save *") : (pl ? L"\xD83D\xDCBE Zapisz" : L"\xD83D\xDCBE Save"),
            false, m_isModified);
    drawBtn(BTN_SYNTAX_EDIT, 76.0f,
            m_syntaxMode ? (pl ? L"\x270F Edytuj" : L"\x270F Edit") : (pl ? L"\xD83C\xDFA8 Składnia" : L"\xD83C\xDFA8 Syntax"),
            m_syntaxMode);
    drawBtn(BTN_FIND,       58.0f, pl ? L"\xD83D\xDD0D Szukaj" : L"\xD83D\xDD0D Find");
    drawBtn(BTN_SYMBOLS,    34.0f, L"\x00B6", m_showSymbols);
    drawBtn(BTN_EOL_TOGGLE, 44.0f, m_useCrLf ? L"CRLF" : L"LF", false);
    drawBtn(BTN_COPY_ALL,   58.0f, pl ? L"Kopiuj" : L"Copy");
    drawBtn(BTN_RELOAD,     58.0f, pl ? L"Odśwież" : L"Reload");
    drawBtn(BTN_EXT_NOTEPAD, 64.0f, L"\x2197 N++");

    D2D1_RECT_F bodyR = D2D1::RectF(rect.left, tbR.bottom, rect.right, std::max(tbR.bottom + 20.0f, rect.bottom - statusH));
    float gutterW = 46.0f;
    D2D1_RECT_F gutterR = D2D1::RectF(bodyR.left, bodyR.top, std::min(bodyR.right, bodyR.left + gutterW), bodyR.bottom);

    // Draw Line Numbers Gutter (in BOTH Syntax Pro Mode and Live Edit Mode!)
    brush->SetColor(NpColor(0xF1F5F9));
    rt->FillRectangle(gutterR, brush);
    brush->SetColor(NpColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(gutterR.right - 0.5f, gutterR.top), D2D1::Point2F(gutterR.right - 0.5f, gutterR.bottom), brush, 1.0f);

    IDWriteTextFormat* codeFmt = m_fmtCodeMono ? m_fmtCodeMono : fmtSmall;
    IDWriteTextFormat* codeBold = m_fmtCodeBold ? m_fmtCodeBold : fmtBold;

    if (!m_syntaxMode) {
        // Live Win32 Edit Mode with synchronized Line Numbers Gutter on the left
        int firstVis = m_editHwnd ? static_cast<int>(SendMessageW(m_editHwnd, EM_GETFIRSTVISIBLELINE, 0, 0)) : 0;
        float rowH = 16.0f;
        int visCount = static_cast<int>((gutterR.bottom - gutterR.top) / rowH) + 1;
        rt->PushAxisAlignedClip(gutterR, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        codeFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        brush->SetColor(NpColor(0x64748B));
        for (int r = 0; r < visCount && (firstVis + r) < m_totalLines; ++r) {
            wchar_t lnBuf[24] = {};
            swprintf_s(lnBuf, L"%d", firstVis + r + 1);
            float y = gutterR.top + 2.0f + r * rowH;
            rt->DrawText(lnBuf, static_cast<UINT32>(wcslen(lnBuf)), codeFmt,
                D2D1::RectF(gutterR.left, y, gutterR.right - 6.0f, y + rowH), brush);
        }
        codeFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        rt->PopAxisAlignedClip();

        D2D1_RECT_F editR = D2D1::RectF(gutterR.right + 1.0f, bodyR.top, bodyR.right - 1.0f, bodyR.bottom);
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
    } else {
        // Direct2D Notepad++ Pro Syntax Highlighted View + Line Numbers + Fold Markers + Minimap
        HideWindow();
        float minimapW = ((bodyR.right - gutterR.right) > 260.0f) ? 54.0f : 14.0f;
        m_codeAreaRect = D2D1::RectF(gutterR.right, bodyR.top, bodyR.right - minimapW, bodyR.bottom);
        m_minimapRect = D2D1::RectF(m_codeAreaRect.right, bodyR.top, bodyR.right, bodyR.bottom);

        brush->SetColor(NpColor(0xFFFFFF));
        rt->FillRectangle(m_codeAreaRect, brush);

        float rowH = 18.0f;
        float charCellW = 6.6f; // Exact advance for 12pt Consolas
        int visRows = std::max(1, static_cast<int>((m_codeAreaRect.bottom - m_codeAreaRect.top) / rowH));
        int maxTop = std::max(0, static_cast<int>(m_codeLines.size()) - visRows);
        m_topVisibleLine = std::clamp(m_topVisibleLine, 0, maxTop);

        rt->PushAxisAlignedClip(D2D1::RectF(gutterR.left, bodyR.top, m_codeAreaRect.right, bodyR.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        for (int r = 0; r <= visRows; ++r) {
            int lineIdx = m_topVisibleLine + r;
            if (lineIdx >= static_cast<int>(m_codeLines.size())) break;
            float ry = m_codeAreaRect.top + r * rowH;
            D2D1_RECT_F lineR = D2D1::RectF(m_codeAreaRect.left, ry, m_codeAreaRect.right, ry + rowH);

            // Current line highlight (Notepad++ style soft yellow/blue tint)
            if (lineIdx == m_selectedLineIdx) {
                brush->SetColor(NpColor(0xEFF6FF));
                rt->FillRectangle(lineR, brush);
                brush->SetColor(NpColor(0xBFDBFE));
                rt->DrawLine(D2D1::Point2F(lineR.left, lineR.bottom - 0.5f), D2D1::Point2F(lineR.right, lineR.bottom - 0.5f), brush, 1.0f);
            }

            // Line number + Fold marker in Gutter
            wchar_t lnBuf[24] = {};
            swprintf_s(lnBuf, L"%d", lineIdx + 1);
            codeFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            brush->SetColor(lineIdx == m_selectedLineIdx ? NpColor(0x0067B8) : NpColor(0x64748B));
            rt->DrawText(lnBuf, static_cast<UINT32>(wcslen(lnBuf)), codeFmt,
                D2D1::RectF(gutterR.left, ry, gutterR.right - 13.0f, ry + rowH), brush);
            codeFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

            const auto& cl = m_codeLines[lineIdx];
            if (cl.isFoldHeader) {
                brush->SetColor(NpColor(0x94A3B8));
                rt->DrawText(L"\x25BE", 1, fmtSmall, D2D1::RectF(gutterR.right - 11.0f, ry, gutterR.right, ry + rowH), brush);
            }

            // Highlight search matches if m_lastFindQuery is non-empty
            if (!m_lastFindQuery.empty() && !cl.text.empty()) {
                std::wstring lineLow = PathUtils::ToLower(cl.text);
                std::wstring qLow = PathUtils::ToLower(m_lastFindQuery);
                size_t mpos = 0;
                while ((mpos = lineLow.find(qLow, mpos)) != std::wstring::npos) {
                    float mx1 = lineR.left + 8.0f + static_cast<float>(mpos) * charCellW;
                    float mx2 = mx1 + static_cast<float>(qLow.size()) * charCellW;
                    brush->SetColor(NpColor(0xFEF08A));
                    rt->FillRectangle(D2D1::RectF(mx1, ry + 1.0f, mx2, ry + rowH - 1.0f), brush);
                    mpos += qLow.size();
                }
            }

            // Render syntax-highlighted tokens along the line
            float txBase = lineR.left + 8.0f;
            for (const auto& tok : cl.tokens) {
                if (tok.start >= cl.text.size() || tok.length == 0) continue;
                float tx = txBase + static_cast<float>(tok.start) * charCellW;
                if (tx > lineR.right) break;
                const wchar_t* ptr = cl.text.c_str() + tok.start;
                UINT32 tlen = static_cast<UINT32>(std::min<size_t>(tok.length, cl.text.size() - tok.start));
                brush->SetColor(ColorForSyntaxToken(tok.kind));
                IDWriteTextFormat* tf = (tok.kind == SyntaxTokenKind::Keyword) ? codeBold : codeFmt;
                rt->DrawText(ptr, tlen, tf, D2D1::RectF(tx, ry, lineR.right + 100.0f, ry + rowH), brush);
            }

            // Optional CRLF / LF symbol at end of line when [¶] is enabled
            if (m_showSymbols) {
                float eolX = txBase + static_cast<float>(cl.text.size()) * charCellW + 2.0f;
                if (eolX < lineR.right - 20.0f) {
                    brush->SetColor(NpColor(0x94A3B8));
                    const wchar_t* eolSym = m_useCrLf ? L"CRLF" : L"LF";
                    rt->DrawText(eolSym, static_cast<UINT32>(wcslen(eolSym)), fmtSmall,
                        D2D1::RectF(eolX, ry, eolX + 36.0f, ry + rowH), brush);
                }
            }
        }

        rt->PopAxisAlignedClip();

        // Draw Right-Side Code Minimap & Scrollbar
        brush->SetColor(NpColor(0xF8FAFC));
        rt->FillRectangle(m_minimapRect, brush);
        brush->SetColor(NpColor(0xE2E8F0));
        rt->DrawLine(D2D1::Point2F(m_minimapRect.left, m_minimapRect.top), D2D1::Point2F(m_minimapRect.left, m_minimapRect.bottom), brush, 1.0f);

        float mmH = std::max(1.0f, m_minimapRect.bottom - m_minimapRect.top);
        size_t totalLn = std::max<size_t>(1, m_codeLines.size());
        if (minimapW >= 40.0f) {
            int maxMiniLines = static_cast<int>(mmH / 2.0f);
            size_t step = std::max<size_t>(1, totalLn / std::max(1, maxMiniLines));
            for (size_t idx = 0; idx < totalLn; idx += step) {
                float my = m_minimapRect.top + (static_cast<float>(idx) / static_cast<float>(totalLn)) * mmH;
                if (my >= m_minimapRect.bottom - 2.0f) break;
                const auto& cl = m_codeLines[idx];
                if (cl.text.empty()) continue;
                size_t indent = cl.text.find_first_not_of(L' ');
                if (indent == std::wstring::npos) continue;
                float x1 = m_minimapRect.left + 4.0f + std::min(16.0f, static_cast<float>(indent) * 0.8f);
                float w = std::min(minimapW - 8.0f, static_cast<float>(cl.text.size() - indent) * 0.6f);
                SyntaxTokenKind domKind = cl.tokens.empty() ? SyntaxTokenKind::Plain : cl.tokens.front().kind;
                brush->SetColor(ColorForSyntaxToken(domKind));
                rt->FillRectangle(D2D1::RectF(x1, my, std::min(m_minimapRect.right - 3.0f, x1 + w), my + 1.3f), brush);
            }
        }

        // Viewport box on Minimap
        float vpFracStart = static_cast<float>(m_topVisibleLine) / static_cast<float>(totalLn);
        float vpFracH = std::min(1.0f, static_cast<float>(visRows) / static_cast<float>(totalLn));
        float vpTop = m_minimapRect.top + vpFracStart * mmH;
        float vpH = std::max(18.0f, vpFracH * mmH);
        D2D1_RECT_F vpR = D2D1::RectF(m_minimapRect.left + 1.0f, vpTop, m_minimapRect.right - 1.0f, std::min(m_minimapRect.bottom, vpTop + vpH));
        brush->SetColor(NpColor(0x0078D7, 0.18f));
        rt->FillRectangle(vpR, brush);
        brush->SetColor(NpColor(0x0078D7, 0.65f));
        rt->DrawRectangle(vpR, brush, 1.0f);
    }

    // 3. Bottom Notepad++ Status Bar (Language | Ln, Col | Lines | Encoding | EOL | Mode)
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

    wchar_t statBuf[320] = {};
    swprintf_s(statBuf,
        pl ? L"[%s]   Wiersz %d, Kol %d   |   Wierszy: %d   |   %s (%s)%s"
           : L"[%s]   Ln %d, Col %d   |   Lines: %d   |   %s (%s)%s",
        GetSyntaxLanguageName(m_syntaxLang),
        m_caretLine, m_caretCol, m_totalLines,
        encName, m_useCrLf ? L"CRLF" : L"LF",
        m_isModified ? (pl ? L"   \x25CF Zmodyfikowano (Ctrl+S)" : L"   \x25CF Modified (Ctrl+S)")
                     : (m_syntaxMode ? (pl ? L"   (2\xD7klik = edytuj)" : L"   (double-click = edit)") : L""));

    D2D1_RECT_F txtR = D2D1::RectF(sbR.left + 8.0f, sbR.top, sbR.right - 8.0f, sbR.bottom);
    brush->SetColor(m_isModified ? NpColor(0x0067B8) : NpColor(0x475569));
    rt->DrawText(statBuf, static_cast<UINT32>(wcslen(statBuf)), fmtSmall, txtR, brush);
    (void)fmtRegular;
}

bool NotepadViewer::OnMouseDown(float x, float y, Lang lang, std::wstring* outStatusToast) {
    const bool pl = (lang == Lang::PL);
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            switch (hit.id) {
            case BTN_SAVE:
                SaveFile(outStatusToast, lang);
                break;
            case BTN_SYNTAX_EDIT:
                ToggleSyntaxMode();
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
            case BTN_SYMBOLS:
                ToggleShowSymbols();
                break;
            case BTN_EOL_TOGGLE:
                ToggleLineEndings();
                if (outStatusToast) {
                    *outStatusToast = m_useCrLf
                        ? (pl ? L"Zmieniono końce wierszy na Windows (CRLF) — kliknij Zapisz" : L"Switched line endings to Windows (CRLF) — click Save")
                        : (pl ? L"Zmieniono końce wierszy na Unix (LF) — kliknij Zapisz" : L"Switched line endings to Unix (LF) — click Save");
                }
                break;
            case BTN_FIND:
                FindNextInteractive();
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

    if (m_syntaxMode) {
        if (PathUtils::PtInRectF(x, y, m_minimapRect)) {
            m_draggingMinimap = true;
            float frac = std::clamp((y - m_minimapRect.top) / std::max(1.0f, m_minimapRect.bottom - m_minimapRect.top), 0.0f, 1.0f);
            m_topVisibleLine = static_cast<int>(frac * static_cast<float>(m_codeLines.size()));
            return true;
        }
        if (PathUtils::PtInRectF(x, y, m_codeAreaRect)) {
            int clickedRow = static_cast<int>((y - m_codeAreaRect.top) / 18.0f);
            int lineIdx = std::clamp(m_topVisibleLine + clickedRow, 0, std::max(0, static_cast<int>(m_codeLines.size()) - 1));
            ULONGLONG now = GetTickCount64();
            bool isDbl = (lineIdx == m_lastClickedLineIdx && (now - m_lastLineClickTick) <= GetDoubleClickTime());
            m_selectedLineIdx = lineIdx;
            m_lastClickedLineIdx = lineIdx;
            m_lastLineClickTick = now;
            if (isDbl) {
                ToggleSyntaxMode(); // Switch immediately to live editing at this line!
            }
            return true;
        }
    }
    return false;
}

bool NotepadViewer::OnMouseMove(float x, float y) {
    if (m_draggingMinimap && m_syntaxMode) {
        float frac = std::clamp((y - m_minimapRect.top) / std::max(1.0f, m_minimapRect.bottom - m_minimapRect.top), 0.0f, 1.0f);
        m_topVisibleLine = static_cast<int>(frac * static_cast<float>(m_codeLines.size()));
        return true;
    }

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

bool NotepadViewer::OnMouseUp() {
    if (m_draggingMinimap) {
        m_draggingMinimap = false;
        return true;
    }
    return false;
}

bool NotepadViewer::OnMouseWheel(short delta, float x, float y) {
    if (!m_syntaxMode) return false;
    if (!PathUtils::PtInRectF(x, y, m_codeAreaRect) && !PathUtils::PtInRectF(x, y, m_minimapRect)) {
        return false;
    }
    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;
    int step = (GetKeyState(VK_CONTROL) & 0x8000) ? 18 : 4;
    int visRows = std::max(1, static_cast<int>((m_codeAreaRect.bottom - m_codeAreaRect.top) / 18.0f));
    int maxTop = std::max(0, static_cast<int>(m_codeLines.size()) - visRows);
    m_topVisibleLine = std::clamp(m_topVisibleLine - notches * step, 0, maxTop);
    return true;
}

} // namespace Explorer2
