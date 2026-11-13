#pragma once
#include "Types.h"
#include "Localization.h"
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>

namespace Explorer2 {

enum class OfficeFileKind : int {
    None = 0,
    CsvTable = 1,
    ExcelXlsx = 2,
    ExcelXls = 3,
    WordDocx = 4,
    RichTextRtf = 5,
    PowerPointPptx = 6,
    EpubBook = 7,
    SqliteDb = 8,
    DbfTable = 9,
    DifSylkTable = 10
};

struct SpreadsheetSheet {
    std::wstring name;
    std::vector<std::vector<std::wstring>> rows;
    size_t maxCols = 0;
};

struct DocParagraph {
    std::wstring text;
    int headingLevel = 0; // 0 = normal, 1 = H1, 2 = H2, 3 = H3
    bool isBullet = false;
    bool isTableRow = false;
};

struct GpsTrackPoint {
    double lat = 0.0;
    double lon = 0.0;
    float elevM = 0.0f;
    float cumDistKm = 0.0f;
    int rowIndex = 0;
    std::wstring label;
};

struct ParsedOfficeDocument {
    std::wstring filePath;
    std::wstring formatTitle;
    OfficeFileKind kind = OfficeFileKind::None;
    bool isSpreadsheet = false;
    bool valid = false;

    // For Spreadsheets (.csv, .tsv, .xlsx, .xls)
    std::vector<SpreadsheetSheet> sheets;

    // For GPS / Telemetry Map Logs (.gpx, .kml, .geojson, .osm, .tcx, .nmea, .igc)
    bool hasGeoTrack = false;
    std::vector<GpsTrackPoint> geoPoints;
    double totalDistanceKm = 0.0;
    double totalAscentM = 0.0;

    // For Documents (.docx, .odt, .rtf)
    std::vector<DocParagraph> paragraphs;
    size_t wordCount = 0;
    size_t charCount = 0;
};

class SpreadsheetDocViewer {
public:
    SpreadsheetDocViewer() = default;
    ~SpreadsheetDocViewer() = default;

    static bool IsSpreadsheetOrDocExtension(const std::wstring& extLower);
    static bool InspectOfficeFile(const std::wstring& fullPath, ParsedOfficeDocument& outDoc);

    void Initialize(HWND parentHwnd);
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

private:
    void SortActiveSheetByColumn(int colIdx);
    void RecomputeColumnStats(int colIdx);

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
    std::wstring m_currentPath;
    ParsedOfficeDocument m_doc;

    int m_activeSheetIdx = 0;
    int m_selectedRow = 0;
    int m_selectedCol = 0;
    int m_sortedCol = -1;
    bool m_sortAscending = true;
    int m_topRow = 0;
    int m_leftCol = 0;
    float m_docScrollY = 0.0f;

    // Column statistics for selected column
    size_t m_statNumericCount = 0;
    double m_statSum = 0.0;
    double m_statAvg = 0.0;
    double m_statMin = 0.0;
    double m_statMax = 0.0;

    int m_hoverBtnId = 0;
    bool m_showMapView = true;
    float m_mapZoom = 1.0f;
    float m_mapPanX = 0.0f;
    float m_mapPanY = 0.0f;
    bool m_mapDragging = false;
    float m_mapDragStartX = 0.0f;
    float m_mapDragStartY = 0.0f;
    D2D1_RECT_F m_gridBodyRect = {};
    D2D1_RECT_F m_colHeaderRect = {};
    std::vector<HitRect> m_toolbarHits;
};

} // namespace Explorer2
