#pragma once
#include "Types.h"
#include "Localization.h"
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <cstdint>

namespace Explorer2 {

class HexViewer {
public:
    HexViewer() = default;
    ~HexViewer();

    void Initialize(HWND parentHwnd, IDWriteFactory* dwriteFactory);
    void Shutdown();

    void LoadFile(const std::wstring& fullPath, bool forceReload = false);
    void Clear();
    const std::wstring& GetCurrentPath() const { return m_filePath; }

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
    void EnsureWindowCached(uint64_t startOffset, size_t byteCount);
    bool ReadBytesAt(uint64_t offset, uint8_t* outBuf, size_t count, size_t* outRead) const;
    std::wstring DetectMagicSignature(Lang lang) const;
    void CopyVisibleHexDumpToClipboard();
    void CopySelectedBytesHexToClipboard();

    enum ButtonId : int {
        BTN_NONE = 0,
        BTN_JUMP_START = 1,
        BTN_JUMP_MID = 2,
        BTN_JUMP_END = 3,
        BTN_COPY_PAGE = 4,
        BTN_COPY_SEL = 5
    };

    struct ToolbarHit {
        int id = 0;
        D2D1_RECT_F rect = {};
    };

    HWND m_parentHwnd = nullptr;
    IDWriteTextFormat* m_fmtMono = nullptr;

    std::wstring m_filePath;
    uint64_t m_fileSize = 0;
    uint64_t m_topRowIndex = 0;       // 16 bytes per row
    uint64_t m_selectedOffset = 0;
    int m_visibleRows = 24;

    // Cached sliding window of bytes (8 KB) so scrolling & painting never hit disk unnecessarily
    uint64_t m_cacheBaseOffset = UINT64_MAX;
    std::vector<uint8_t> m_cacheBytes;
    std::vector<uint8_t> m_headerBytes; // First 128 bytes for magic signature & header preview

    D2D1_RECT_F m_gridRect = {};
    D2D1_RECT_F m_scrollTrackRect = {};
    D2D1_RECT_F m_scrollThumbRect = {};
    bool m_draggingScroll = false;
    float m_scrollDragOffsetY = 0.0f;

    int m_hoverBtnId = 0;
    std::vector<ToolbarHit> m_toolbarHits;
};

} // namespace Explorer2
