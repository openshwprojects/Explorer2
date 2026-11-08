#pragma once
#include "Types.h"
#include "Localization.h"
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer2 {

enum class TextFileEncoding : int {
    UTF8 = 0,
    UTF8_BOM = 1,
    UTF16_LE = 2,
    UTF16_BE = 3,
    ANSI = 4
};

enum class SyntaxLang : int {
    PlainText = 0,
    Cpp = 1,
    Python = 2,
    JavaScript = 3,
    Json = 4,
    XmlHtml = 5,
    Css = 6,
    Sql = 7,
    CSharp = 8,
    Rust = 9,
    ShellBatch = 10,
    IniYaml = 11,
    Markdown = 12,
    GCodeEda = 13
};

enum class SyntaxTokenKind : uint8_t {
    Plain = 0,
    Keyword = 1,
    Type = 2,
    Preprocessor = 3,
    String = 4,
    Comment = 5,
    Number = 6,
    TagOrKey = 7,
    Operator = 8
};

struct SyntaxToken {
    uint32_t start = 0;
    uint32_t length = 0;
    SyntaxTokenKind kind = SyntaxTokenKind::Plain;
};

class NotepadViewer {
public:
    NotepadViewer() = default;
    ~NotepadViewer();

    void Initialize(HWND parentHwnd, HINSTANCE hInstance, IDWriteFactory* dwriteFactory = nullptr);
    void Shutdown();

    static bool IsLikelyTextFile(const std::wstring& fullPath, const std::wstring& extLower, uint64_t fileSize);
    static SyntaxLang DetectSyntaxLanguage(const std::wstring& extLower, std::wstring_view firstLine = L"");
    static const wchar_t* GetSyntaxLanguageName(SyntaxLang lang);
    static void TokenizeLine(
        std::wstring_view line,
        SyntaxLang lang,
        bool& inOutBlockComment,
        std::vector<SyntaxToken>& outTokens);

    void LoadFile(const std::wstring& fullPath, bool forceLoad = false);
    void Clear();
    bool SaveFile(std::wstring* outStatusToast = nullptr, Lang lang = Lang::PL);
    void ReloadFromDisk();
    void ToggleWordWrap();
    void ToggleSyntaxMode();
    void ToggleShowSymbols();
    void ToggleLineEndings();
    void FindNextInteractive();
    void CopyAllToClipboard();
    void OpenInExternalNotepad();

    void HideWindow();
    bool HasFocus() const;
    HWND GetEditHwnd() const { return m_editHwnd; }
    const std::wstring& GetCurrentPath() const { return m_filePath; }
    bool IsModified() const { return m_isModified; }

    void OnEditChanged();

    void Draw(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush,
        IDWriteTextFormat* fmtRegular,
        IDWriteTextFormat* fmtSmall,
        IDWriteTextFormat* fmtBold,
        const D2D1_RECT_F& rect,
        Lang lang,
        D2D1_POINT_2F mousePt);

    bool OnMouseDown(float x, float y, Lang lang, std::wstring* outStatusToast = nullptr);
    bool OnMouseMove(float x, float y);
    bool OnMouseUp();
    bool OnMouseWheel(short delta, float x, float y);

private:
    void CreateOrRecreateEditControl(bool wordWrap);
    void UpdateCaretStats();
    void RebuildLinesFromText(const std::wstring& text);
    void SyncFromEditControlToLines();

    enum ButtonId : int {
        BTN_NONE = 0,
        BTN_SAVE = 1,
        BTN_SYNTAX_EDIT = 2,
        BTN_RELOAD = 3,
        BTN_WORDWRAP = 4,
        BTN_SYMBOLS = 5,
        BTN_EOL_TOGGLE = 6,
        BTN_FIND = 7,
        BTN_COPY_ALL = 8,
        BTN_EXT_NOTEPAD = 9
    };

    struct ToolbarHit {
        int id = 0;
        D2D1_RECT_F rect = {};
    };

    struct CodeLineEntry {
        std::wstring text;
        std::vector<SyntaxToken> tokens;
        bool isFoldHeader = false;
    };

    HWND m_parentHwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    HWND m_editHwnd = nullptr;
    HFONT m_monoFont = nullptr;
    IDWriteFactory* m_dwriteFactory = nullptr;
    IDWriteTextFormat* m_fmtCodeMono = nullptr;
    IDWriteTextFormat* m_fmtCodeBold = nullptr;

    std::wstring m_filePath;
    TextFileEncoding m_encoding = TextFileEncoding::UTF8;
    SyntaxLang m_syntaxLang = SyntaxLang::PlainText;
    bool m_syntaxMode = true; // True = Direct2D Syntax Highlighted Notepad++ Pro View, False = Live Win32 Edit
    bool m_showSymbols = false;
    bool m_useCrLf = true;
    bool m_wordWrap = true;
    bool m_isModified = false;
    bool m_isReadOnlyTruncated = false;
    bool m_suppressChangeEvent = false;
    uint64_t m_diskFileSize = 0;

    std::vector<CodeLineEntry> m_codeLines;
    int m_topVisibleLine = 0;
    int m_selectedLineIdx = 0;
    ULONGLONG m_lastLineClickTick = 0;
    int m_lastClickedLineIdx = -1;
    bool m_draggingMinimap = false;
    D2D1_RECT_F m_codeAreaRect = {};
    D2D1_RECT_F m_minimapRect = {};

    int m_caretLine = 1;
    int m_caretCol = 1;
    int m_totalLines = 1;
    int m_totalChars = 0;

    std::wstring m_lastFindQuery;
    int m_hoverBtnId = 0;
    std::vector<ToolbarHit> m_toolbarHits;
    D2D1_RECT_F m_lastEditRect = {};
};

} // namespace Explorer2
