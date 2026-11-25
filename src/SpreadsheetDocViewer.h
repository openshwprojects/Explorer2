#pragma once
#include "Types.h"
#include "Localization.h"
#include "OfficeDocTypes.h"
#include "DocReaders.h"
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer2 {

class SpreadsheetDocViewer {
public:
    SpreadsheetDocViewer() = default;
    ~SpreadsheetDocViewer();

    // The readers themselves live in DocReaders.cpp
    static bool IsSpreadsheetOrDocExtension(const std::wstring& extLower) {
        return DocReaders::IsSpreadsheetOrDocExtension(extLower);
    }
    static bool InspectOfficeFile(const std::wstring& fullPath, ParsedOfficeDocument& outDoc) {
        return DocReaders::InspectOfficeFile(fullPath, outDoc);
    }

    void Initialize(HWND parentHwnd, IDWriteFactory* dwriteFactory = nullptr);
    void Shutdown();

    void LoadFile(const std::wstring& fullPath, bool forceReload = false);
    void Clear();

    // A document read on another thread (InspectOfficeFile) goes into the view, which then shows what
    // LoadFile would have shown.
    void ShowLoadedDocument(const std::wstring& fullPath, ParsedOfficeDocument&& doc);
    bool IsShowing(const std::wstring& fullPath) const { return !m_currentPath.empty() && m_currentPath == fullPath; }
    const std::wstring& LoadedPath() const { return m_currentPath; }
    bool HasValidDocument() const { return m_doc.valid; }

    void Draw(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush,
        IDWriteTextFormat* fmtRegular,
        IDWriteTextFormat* fmtSmall,
        IDWriteTextFormat* fmtBold,
        const D2D1_RECT_F& rect,
        Lang lang,
        D2D1_POINT_2F mousePt);

    bool OnMouseDown(float x, float y, bool rightButton, Lang lang, std::wstring* outStatusToast);
    bool OnMouseMove(float x, float y);
    bool OnMouseUp();
    bool OnMouseWheel(short delta, float x, float y);
    bool OnMouseHWheel(short delta, float x, float y);
    bool WantsHorizontalResizeCursor(float x, float y) const;

    int TopRow() const { return m_topRow; }
    int LeftCol() const { return m_leftCol; }
    float DocScrollY() const { return m_docScrollY; }
    const D2D1_RECT_F& VScrollThumbRect() const { return m_vScrollThumbRect; }
    const D2D1_RECT_F& HScrollThumbRect() const { return m_hScrollThumbRect; }

private:
    static constexpr float kDefaultCellWidth = 108.0f;
    static constexpr float kMinCellWidth = 48.0f;
    static constexpr float kMaxCellWidth = 640.0f;

    void SortActiveSheetByColumn(int colIdx);
    void RecomputeColumnStats(int colIdx);
    void ResetColumnWidthsForActiveSheet();
    void AutoFitColumnWidth(int colIdx);
    float GetColWidth(int colIdx) const;
    int HitColumnResizeHandle(float x, float y) const;
    int HitColumnAtX(float x) const;

    enum BtnId : int {
        BTN_NONE = 0,
        BTN_COPY_ALL = 1,
        BTN_COPY_CELL = 2,
        BTN_SORT_ASC_DESC = 3,
        BTN_TOGGLE_MAP_VIEW = 4,
        BTN_RESET_MAP_CAM = 5,
        BTN_OPEN_OSM_WEB = 6,
        BTN_SHEET_BASE = 100 // 100 + sheetIdx
    };

    struct HitRect {
        int id = 0;
        D2D1_RECT_F rect = {};
    };

    HWND m_parentHwnd = nullptr;
    IDWriteTextFormat* m_fmtCellTrim = nullptr;
    IDWriteTextFormat* m_fmtHeaderTrim = nullptr;
    std::wstring m_lineScratch;
    std::wstring m_currentPath;
    ParsedOfficeDocument m_doc;

    int m_activeSheetIdx = 0;
    int m_selectedRow = 0;
    int m_selectedCol = 0;
    int m_sortedCol = -1;
    bool m_sortAscending = true;
    int m_topRow = 0;
    int m_leftCol = 0;
    int m_maxTopRow = 0;
    int m_maxLeftCol = 0;
    int m_visDataRows = 1;
    float m_docScrollY = 0.0f;
    float m_maxDocScrollY = 0.0f;

    std::vector<float> m_colWidths;
    int m_resizingCol = -1;
    float m_resizeStartX = 0.0f;
    float m_resizeStartW = kDefaultCellWidth;
    ULONGLONG m_lastDividerClickTick = 0;
    int m_lastDividerClickCol = -1;

    // Column statistics for selected column
    size_t m_statNumericCount = 0;
    double m_statSum = 0.0;
    double m_statAvg = 0.0;
    double m_statMin = 0.0;
    double m_statMax = 0.0;

    int m_hoverBtnId = 0;
    int m_hoverScrollPart = 0; // 0 = none, 1 = vertical thumb, 2 = horizontal thumb
    bool m_showMapView = true;
    float m_mapZoom = 1.0f;
    float m_mapPanX = 0.0f;
    float m_mapPanY = 0.0f;
    bool m_mapDragging = false;
    float m_mapDragStartX = 0.0f;
    float m_mapDragStartY = 0.0f;
    bool m_draggingVScroll = false;
    bool m_draggingHScroll = false;
    float m_scrollDragOffset = 0.0f;
    D2D1_RECT_F m_gridBodyRect = {};
    D2D1_RECT_F m_colHeaderRect = {};
    D2D1_RECT_F m_vScrollTrackRect = {};
    D2D1_RECT_F m_vScrollThumbRect = {};
    D2D1_RECT_F m_hScrollTrackRect = {};
    D2D1_RECT_F m_hScrollThumbRect = {};
    std::vector<HitRect> m_toolbarHits;
};

} // namespace Explorer2
