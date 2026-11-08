#pragma once
#include "Types.h"
#include "Localization.h"
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>

namespace Explorer2 {

enum class TextFileEncoding : int {
    UTF8 = 0,
    UTF8_BOM = 1,
    UTF16_LE = 2,
    UTF16_BE = 3,
    ANSI = 4
};

class NotepadViewer {
public:
    NotepadViewer() = default;
    ~NotepadViewer();

    void Initialize(HWND parentHwnd, HINSTANCE hInstance);
    void Shutdown();

    static bool IsLikelyTextFile(const std::wstring& fullPath, const std::wstring& extLower, uint64_t fileSize);

    void LoadFile(const std::wstring& fullPath, bool forceLoad = false);
    void Clear();
    bool SaveFile(std::wstring* outStatusToast = nullptr, Lang lang = Lang::PL);
    void ReloadFromDisk();
    void ToggleWordWrap();
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

private:
    void CreateOrRecreateEditControl(bool wordWrap);
    void UpdateCaretStats();

    enum ButtonId : int {
        BTN_NONE = 0,
        BTN_SAVE = 1,
        BTN_RELOAD = 2,
        BTN_WORDWRAP = 3,
        BTN_FIND = 4,
        BTN_COPY_ALL = 5,
        BTN_EXT_NOTEPAD = 6
    };

    struct ToolbarHit {
        int id = 0;
        D2D1_RECT_F rect = {};
    };

    HWND m_parentHwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    HWND m_editHwnd = nullptr;
    HFONT m_monoFont = nullptr;

    std::wstring m_filePath;
    TextFileEncoding m_encoding = TextFileEncoding::UTF8;
    bool m_useCrLf = true;
    bool m_wordWrap = true;
    bool m_isModified = false;
    bool m_isReadOnlyTruncated = false;
    bool m_suppressChangeEvent = false;
    uint64_t m_diskFileSize = 0;

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
