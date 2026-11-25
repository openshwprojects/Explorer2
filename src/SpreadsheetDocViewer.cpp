#include "SpreadsheetDocViewer.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ThemeColors.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#pragma comment(lib, "bcrypt.lib")
#include <cstdio>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <array>
#include <limits>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Explorer2 {

namespace {

// A colour of the light design, or its counterpart in the dark theme (ThemeColors.h)
static D2D1_COLOR_F SdvColor(uint32_t rgb, float a = 1.0f) {
    return PathUtils::ColorFromRgb(ThemedPaneRgb(rgb), a);
}


} // namespace

#include "formats/common/GpsTrackMap.h"   // DrawGpsMapAndElevationView
#include <shellapi.h>

SpreadsheetDocViewer::~SpreadsheetDocViewer() {
    Shutdown();
}

void SpreadsheetDocViewer::Initialize(HWND parentHwnd, IDWriteFactory* dwriteFactory) {
    m_parentHwnd = parentHwnd;
    if (dwriteFactory) {
        if (!m_fmtCellTrim) {
            PathUtils::CreateTrimmedTextFormat(dwriteFactory, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11.0f, &m_fmtCellTrim);
        }
        if (!m_fmtHeaderTrim) {
            PathUtils::CreateTrimmedTextFormat(dwriteFactory, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.0f, &m_fmtHeaderTrim);
        }
    }
}

void SpreadsheetDocViewer::Shutdown() {
    Clear();
    if (m_fmtCellTrim) {
        m_fmtCellTrim->Release();
        m_fmtCellTrim = nullptr;
    }
    if (m_fmtHeaderTrim) {
        m_fmtHeaderTrim->Release();
        m_fmtHeaderTrim = nullptr;
    }
}

void SpreadsheetDocViewer::Clear() {
    m_currentPath.clear();
    m_doc = ParsedOfficeDocument{};
    m_activeSheetIdx = 0;
    m_selectedRow = 0;
    m_selectedCol = 0;
    m_sortedCol = -1;
    m_topRow = 0;
    m_leftCol = 0;
    m_maxTopRow = 0;
    m_maxLeftCol = 0;
    m_visDataRows = 1;
    m_docScrollY = 0.0f;
    m_maxDocScrollY = 0.0f;
    m_colWidths.clear();
    m_resizingCol = -1;
    m_draggingVScroll = false;
    m_draggingHScroll = false;
    m_mapDragging = false;
    m_vScrollTrackRect = {};
    m_vScrollThumbRect = {};
    m_hScrollTrackRect = {};
    m_hScrollThumbRect = {};
}

void SpreadsheetDocViewer::ResetColumnWidthsForActiveSheet() {
    m_colWidths.clear();
    if (!m_doc.isSpreadsheet || m_activeSheetIdx < 0 || static_cast<size_t>(m_activeSheetIdx) >= m_doc.sheets.size()) {
        return;
    }
    size_t cols = std::max<size_t>(m_doc.sheets[m_activeSheetIdx].maxCols, 8);
    m_colWidths.assign(cols, kDefaultCellWidth);
}

void SpreadsheetDocViewer::AutoFitColumnWidth(int colIdx) {
    if (colIdx < 0 || !m_doc.isSpreadsheet || m_activeSheetIdx < 0 ||
        static_cast<size_t>(m_activeSheetIdx) >= m_doc.sheets.size()) {
        return;
    }
    const auto& rows = m_doc.sheets[m_activeSheetIdx].rows;
    size_t maxChars = ColIndexToExcelLetters(colIdx).size() + 2;
    size_t limit = std::min<size_t>(rows.size(), 512);
    for (size_t r = 0; r < limit; ++r) {
        if (static_cast<size_t>(colIdx) < rows[r].size()) {
            std::wstring_view sv = PathUtils::SingleLineView(rows[r][colIdx], m_lineScratch);
            maxChars = std::max(maxChars, sv.size());
        }
    }
    if (static_cast<size_t>(colIdx) >= m_colWidths.size()) {
        m_colWidths.resize(static_cast<size_t>(colIdx) + 1, kDefaultCellWidth);
    }
    m_colWidths[colIdx] = std::clamp(static_cast<float>(maxChars) * 6.8f + 18.0f, 64.0f, 520.0f);
}

float SpreadsheetDocViewer::GetColWidth(int colIdx) const {
    if (colIdx >= 0 && static_cast<size_t>(colIdx) < m_colWidths.size()) {
        return m_colWidths[colIdx];
    }
    return kDefaultCellWidth;
}

int SpreadsheetDocViewer::HitColumnResizeHandle(float x, float y) const {
    if (!m_doc.isSpreadsheet || (m_doc.hasGeoTrack && m_showMapView) || m_doc.sheets.empty()) return -1;
    if (!PathUtils::PtInRectF(x, y, m_colHeaderRect)) return -1;
    const auto& sh = m_doc.sheets[std::clamp(m_activeSheetIdx, 0, static_cast<int>(m_doc.sheets.size()) - 1)];
    int displayCols = std::max(8, static_cast<int>(sh.maxCols));
    float cx = m_colHeaderRect.left;
    for (int colIdx = m_leftCol; colIdx < displayCols && cx < m_colHeaderRect.right; ++colIdx) {
        float rightEdge = cx + GetColWidth(colIdx);
        if (std::abs(x - rightEdge) <= 5.0f && rightEdge <= m_colHeaderRect.right + 4.0f) {
            return colIdx;
        }
        cx = rightEdge;
    }
    return -1;
}

int SpreadsheetDocViewer::HitColumnAtX(float x) const {
    if (!m_doc.isSpreadsheet || m_doc.sheets.empty()) return -1;
    if (x < m_colHeaderRect.left || x >= m_colHeaderRect.right) return -1;
    const auto& sh = m_doc.sheets[std::clamp(m_activeSheetIdx, 0, static_cast<int>(m_doc.sheets.size()) - 1)];
    int displayCols = std::max(8, static_cast<int>(sh.maxCols));
    float cx = m_colHeaderRect.left;
    for (int colIdx = m_leftCol; colIdx < displayCols && cx < m_colHeaderRect.right; ++colIdx) {
        float cw = GetColWidth(colIdx);
        if (x >= cx && x < cx + cw) return colIdx;
        cx += cw;
    }
    return -1;
}

bool SpreadsheetDocViewer::WantsHorizontalResizeCursor(float x, float y) const {
    return m_resizingCol >= 0 || HitColumnResizeHandle(x, y) >= 0;
}

void SpreadsheetDocViewer::LoadFile(const std::wstring& fullPath, bool forceReload) {
    if (!forceReload && m_currentPath == fullPath && !m_currentPath.empty()) {
        return;
    }
    ParsedOfficeDocument doc;
    InspectOfficeFile(fullPath, doc);
    ShowLoadedDocument(fullPath, std::move(doc));
}

void SpreadsheetDocViewer::ShowLoadedDocument(const std::wstring& fullPath, ParsedOfficeDocument&& doc) {
    m_currentPath = fullPath;
    m_activeSheetIdx = 0;
    m_selectedRow = 0;
    m_selectedCol = 0;
    m_sortedCol = -1;
    m_topRow = 0;
    m_leftCol = 0;
    m_maxTopRow = 0;
    m_maxLeftCol = 0;
    m_docScrollY = 0.0f;
    m_maxDocScrollY = 0.0f;
    m_mapZoom = 1.0f;
    m_mapPanX = 0.0f;
    m_mapPanY = 0.0f;
    m_mapDragging = false;
    m_resizingCol = -1;
    m_draggingVScroll = false;
    m_draggingHScroll = false;
    m_doc = std::move(doc);
    m_showMapView = m_doc.hasGeoTrack;
    ResetColumnWidthsForActiveSheet();
    RecomputeColumnStats(0);
}

void SpreadsheetDocViewer::RecomputeColumnStats(int colIdx) {
    m_statNumericCount = 0;
    m_statSum = 0.0;
    m_statAvg = 0.0;
    m_statMin = 0.0;
    m_statMax = 0.0;

    if (!m_doc.isSpreadsheet || m_activeSheetIdx < 0 || static_cast<size_t>(m_activeSheetIdx) >= m_doc.sheets.size()) {
        return;
    }
    const auto& rows = m_doc.sheets[m_activeSheetIdx].rows;
    size_t startRow = (rows.size() > 1) ? 1 : 0; // Skip header row if >1 row
    for (size_t r = startRow; r < rows.size(); ++r) {
        if (colIdx < 0 || static_cast<size_t>(colIdx) >= rows[r].size()) continue;
        const std::wstring& cell = rows[r][colIdx];
        if (cell.empty()) continue;
        wchar_t* endPtr = nullptr;
        double v = std::wcstod(cell.c_str(), &endPtr);
        if (endPtr != cell.c_str() && std::isfinite(v)) {
            if (m_statNumericCount == 0) {
                m_statMin = v;
                m_statMax = v;
            } else {
                m_statMin = std::min(m_statMin, v);
                m_statMax = std::max(m_statMax, v);
            }
            m_statSum += v;
            ++m_statNumericCount;
        }
    }
    if (m_statNumericCount > 0) {
        m_statAvg = m_statSum / static_cast<double>(m_statNumericCount);
    }
}

void SpreadsheetDocViewer::SortActiveSheetByColumn(int colIdx) {
    if (!m_doc.isSpreadsheet || m_activeSheetIdx < 0 || static_cast<size_t>(m_activeSheetIdx) >= m_doc.sheets.size()) {
        return;
    }
    auto& rows = m_doc.sheets[m_activeSheetIdx].rows;
    if (rows.size() <= 2) return;

    if (m_sortedCol == colIdx) {
        m_sortAscending = !m_sortAscending;
    } else {
        m_sortedCol = colIdx;
        m_sortAscending = true;
    }

    bool asc = m_sortAscending;
    // Keep row 0 as frozen header row, sort rows[1..end]
    std::sort(rows.begin() + 1, rows.end(), [colIdx, asc](const std::vector<std::wstring>& a, const std::vector<std::wstring>& b) {
        std::wstring_view sa = (colIdx >= 0 && static_cast<size_t>(colIdx) < a.size()) ? std::wstring_view(a[colIdx]) : std::wstring_view{};
        std::wstring_view sb = (colIdx >= 0 && static_cast<size_t>(colIdx) < b.size()) ? std::wstring_view(b[colIdx]) : std::wstring_view{};
        wchar_t* endA = nullptr;
        wchar_t* endB = nullptr;
        double da = sa.empty() ? 0.0 : std::wcstod(sa.data(), &endA);
        double db = sb.empty() ? 0.0 : std::wcstod(sb.data(), &endB);
        if (!sa.empty() && !sb.empty() && endA != sa.data() && endB != sb.data()) {
            return asc ? (da < db) : (da > db);
        }
        int cmp = CompareStringOrdinal(sa.data(), static_cast<int>(sa.size()), sb.data(), static_cast<int>(sb.size()), TRUE);
        return asc ? (cmp == CSTR_LESS_THAN) : (cmp == CSTR_GREATER_THAN);
    });
}

void SpreadsheetDocViewer::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& rect,
    Lang lang,
    D2D1_POINT_2F mousePt)
{
    const bool pl = (lang == Lang::PL);
    m_toolbarHits.clear();

    float tbH = 30.0f;
    float statusH = 26.0f;
    D2D1_RECT_F tbR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + tbH);
    brush->SetColor(SdvColor(0xF8FAFC));
    rt->FillRectangle(tbR, brush);
    brush->SetColor(SdvColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(tbR.left, tbR.bottom - 0.5f), D2D1::Point2F(tbR.right, tbR.bottom - 0.5f), brush, 1.0f);

    float bx = tbR.left + 6.0f;
    auto addBtn = [&](int id, float w, const wchar_t* label, bool active = false) {
        if (bx + w > tbR.right - 4.0f) return;
        D2D1_RECT_F br = D2D1::RectF(bx, tbR.top + 4.0f, bx + w, tbR.bottom - 4.0f);
        m_toolbarHits.push_back({ id, br });
        PathUtils::DrawToolbarButton(rt, brush, fmtSmall, br, label, active, m_hoverBtnId == id);
        bx += w + 4.0f;
    };

    if (m_doc.isSpreadsheet) {
        if (m_doc.hasGeoTrack) {
            addBtn(BTN_TOGGLE_MAP_VIEW, pl ? 104.0f : 98.0f,
                m_showMapView ? (pl ? L"\xD83D\xDDFA Mapa GPS" : L"\xD83D\xDDFA GPS Map")
                              : (pl ? L"\xD83D\xDCCA Tabela" : L"\xD83D\xDCCA Table"),
                m_showMapView);
            addBtn(BTN_OPEN_OSM_WEB, pl ? 124.0f : 118.0f, pl ? L"\xD83C\xDF0D OpenStreetMap" : L"\xD83C\xDF0D OpenStreetMap");
            if (m_showMapView) {
                addBtn(BTN_RESET_MAP_CAM, pl ? 72.0f : 68.0f, pl ? L"\xD83C\xDFAF Reset" : L"\xD83C\xDFAF Fit");
            }
        }
        for (size_t s = 0; s < std::min<size_t>(m_doc.sheets.size(), 5); ++s) {
            std::wstring sName = L"\xD83D\xDCCA " + m_doc.sheets[s].name;
            float sw = std::clamp(static_cast<float>(sName.size()) * 7.5f + 16.0f, 64.0f, 120.0f);
            addBtn(BTN_SHEET_BASE + static_cast<int>(s), sw, sName.c_str(), !m_showMapView && static_cast<int>(s) == m_activeSheetIdx);
        }
        addBtn(BTN_SORT_ASC_DESC, pl ? 78.0f : 72.0f, pl ? L"\x21C5 Sortuj kol." : L"\x21C5 Sort Col", m_sortedCol >= 0);
        addBtn(BTN_COPY_CELL,     pl ? 82.0f : 76.0f, pl ? L"Kopiuj kom." : L"Copy Cell");
        addBtn(BTN_COPY_ALL,      pl ? 76.0f : 72.0f, pl ? L"Kopiuj CSV" : L"Copy CSV");
    } else {
        addBtn(BTN_COPY_ALL, pl ? 110.0f : 100.0f, pl ? L"\xD83D\xDCCB Kopiuj tekst" : L"\xD83D\xDCCB Copy Text");
    }

    IDWriteTextFormat* cellFmt = m_fmtCellTrim ? m_fmtCellTrim : fmtSmall;
    IDWriteTextFormat* hdrFmt  = m_fmtHeaderTrim ? m_fmtHeaderTrim : fmtBold;

    D2D1_RECT_F bodyR = D2D1::RectF(rect.left, tbR.bottom, rect.right, std::max(tbR.bottom + 20.0f, rect.bottom - statusH));

    if (m_doc.isSpreadsheet && m_doc.hasGeoTrack && m_showMapView) {
        m_gridBodyRect = bodyR;
        m_colHeaderRect = {};
        m_vScrollTrackRect = {};
        m_vScrollThumbRect = {};
        m_hScrollTrackRect = {};
        m_hScrollThumbRect = {};
        Formats::DrawGpsMapAndElevationView(rt, brush, fmtRegular, fmtSmall, fmtBold,
            bodyR, m_doc, m_mapZoom, m_mapPanX, m_mapPanY, m_selectedRow, mousePt, pl);
    } else if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
        const auto& sheet = m_doc.sheets[std::clamp(m_activeSheetIdx, 0, static_cast<int>(m_doc.sheets.size()) - 1)];

        // Formula / Active Cell Inspector Strip (24px)
        float fxH = 24.0f;
        D2D1_RECT_F fxR = D2D1::RectF(bodyR.left, bodyR.top, bodyR.right, bodyR.top + fxH);
        brush->SetColor(SdvColor(0xEFF6FF));
        rt->FillRectangle(fxR, brush);
        brush->SetColor(SdvColor(0xDBEAFE));
        rt->DrawLine(D2D1::Point2F(fxR.left, fxR.bottom - 0.5f), D2D1::Point2F(fxR.right, fxR.bottom - 0.5f), brush, 1.0f);

        std::wstring cellRef = ColIndexToExcelLetters(m_selectedCol) + std::to_wstring(m_selectedRow + 1);
        std::wstring cellVal;
        if (m_selectedRow >= 0 && static_cast<size_t>(m_selectedRow) < sheet.rows.size() &&
            m_selectedCol >= 0 && static_cast<size_t>(m_selectedCol) < sheet.rows[m_selectedRow].size()) {
            cellVal = sheet.rows[m_selectedRow][m_selectedCol];
        }
        std::wstring fxText = L"[" + cellRef + L"]   fx = " + cellVal;
        brush->SetColor(SdvColor(0x1E3A8A));
        PathUtils::DrawSingleLineClipped(rt, fxText, cellFmt,
            D2D1::RectF(fxR.left + 8.0f, fxR.top, fxR.right - 8.0f, fxR.bottom), brush, m_lineScratch);

        // Column Letters Header (A, B, C...) + Grid + Vertical & Horizontal Scrollbars
        constexpr float colHdrH = 22.0f;
        constexpr float rowNumW = 46.0f;
        constexpr float rowH = 21.0f;
        constexpr float sbSize = 12.0f;

        float vScrollLeft = std::max(bodyR.left + rowNumW + 24.0f, bodyR.right - sbSize);
        float hdrBottom = std::min(bodyR.bottom, fxR.bottom + colHdrH);
        float hScrollTop = (bodyR.bottom - hdrBottom > rowH + sbSize)
            ? (bodyR.bottom - sbSize)
            : std::max(hdrBottom, bodyR.bottom - sbSize);

        m_colHeaderRect = D2D1::RectF(bodyR.left + rowNumW, fxR.bottom, vScrollLeft, hdrBottom);
        m_gridBodyRect = D2D1::RectF(bodyR.left, hdrBottom, vScrollLeft, hScrollTop);
        m_vScrollTrackRect = D2D1::RectF(vScrollLeft, hdrBottom, bodyR.right, hScrollTop);
        m_hScrollTrackRect = D2D1::RectF(bodyR.left, hScrollTop, vScrollLeft, bodyR.bottom);

        // Compute clamped vertical and horizontal scroll ranges
        int totalRows = static_cast<int>(sheet.rows.size());
        int totalDataRows = std::max(0, totalRows - 1);
        float gridH = std::max(1.0f, m_gridBodyRect.bottom - m_gridBodyRect.top);
        int fullVisRows = std::max(1, static_cast<int>(gridH / rowH));
        m_visDataRows = std::max(1, fullVisRows - 1);
        m_maxTopRow = (totalDataRows > m_visDataRows) ? (totalDataRows - m_visDataRows) : 0;
        m_topRow = std::clamp(m_topRow, 0, m_maxTopRow);

        int actualCols = std::max(1, static_cast<int>(sheet.maxCols));
        int displayCols = std::max(actualCols, 8);
        float gridColsW = std::max(1.0f, m_colHeaderRect.right - m_colHeaderRect.left);
        float sumRightW = 0.0f;
        int fitFromRight = 0;
        for (int c = actualCols - 1; c >= 0; --c) {
            float cw = GetColWidth(c);
            if (fitFromRight > 0 && sumRightW + cw > gridColsW) break;
            sumRightW += cw;
            ++fitFromRight;
        }
        m_maxLeftCol = std::max(0, actualCols - std::max(1, fitFromRight));
        m_leftCol = std::clamp(m_leftCol, 0, m_maxLeftCol);

        // Header bar background + Grid background
        brush->SetColor(SdvColor(0xF1F5F9));
        rt->FillRectangle(D2D1::RectF(bodyR.left, fxR.bottom, bodyR.right, hdrBottom), brush);
        brush->SetColor(SdvColor(0xFFFFFF));
        rt->FillRectangle(m_gridBodyRect, brush);
        brush->SetColor(SdvColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(bodyR.left, hdrBottom - 0.5f), D2D1::Point2F(bodyR.right, hdrBottom - 0.5f), brush, 1.0f);

        struct VisCol { int colIdx; float x1; float x2; };
        std::vector<VisCol> visCols;
        visCols.reserve(32);
        for (int colIdx = m_leftCol; colIdx < displayCols; ++colIdx) {
            float x1 = visCols.empty() ? m_colHeaderRect.left : visCols.back().x2;
            if (x1 >= vScrollLeft) break;
            visCols.push_back({ colIdx, x1, x1 + GetColWidth(colIdx) });
        }

        // Draw Column Letters inside header clip
        rt->PushAxisAlignedClip(D2D1::RectF(bodyR.left, fxR.bottom, vScrollLeft, hdrBottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        DWRITE_TEXT_ALIGNMENT prevHdrAlign = hdrFmt->GetTextAlignment();
        hdrFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        for (const auto& vc : visCols) {
            D2D1_RECT_F chR = D2D1::RectF(vc.x1, m_colHeaderRect.top, vc.x2, m_colHeaderRect.bottom);
            if (vc.colIdx == m_selectedCol) {
                brush->SetColor(SdvColor(0xDBEAFE));
                rt->FillRectangle(chR, brush);
            }
            std::wstring colLbl = ColIndexToExcelLetters(vc.colIdx);
            if (vc.colIdx == m_sortedCol) colLbl += (m_sortAscending ? L" \x25B2" : L" \x25BC");
            brush->SetColor(SdvColor(0x334155));
            PathUtils::DrawSingleLineClipped(rt, colLbl, hdrFmt,
                D2D1::RectF(chR.left + 2.0f, chR.top, chR.right - 2.0f, chR.bottom), brush, m_lineScratch);
            brush->SetColor(SdvColor(0xCBD5E1));
            rt->DrawLine(D2D1::Point2F(chR.right, chR.top), D2D1::Point2F(chR.right, chR.bottom), brush, 0.9f);
        }
        hdrFmt->SetTextAlignment(prevHdrAlign);
        brush->SetColor(SdvColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(m_colHeaderRect.left, fxR.bottom), D2D1::Point2F(m_colHeaderRect.left, hdrBottom), brush, 0.9f);
        rt->PopAxisAlignedClip();

        // Draw Rows & Grid Cells inside grid body clip
        rt->PushAxisAlignedClip(m_gridBodyRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        int drawSlots = static_cast<int>(std::ceil(gridH / rowH)) + 1;
        D2D1_RECT_F activeSelRect = {};
        bool hasActiveSelRect = false;

        for (int r = 0; r < drawSlots; ++r) {
            int rowIdx = (r == 0) ? 0 : (m_topRow + r); // Freeze Row 0 at top
            if (rowIdx >= totalRows) break;

            float ry = m_gridBodyRect.top + r * rowH;
            if (ry >= m_gridBodyRect.bottom) break;

            D2D1_RECT_F rowNumR = D2D1::RectF(bodyR.left, ry, bodyR.left + rowNumW, ry + rowH);
            brush->SetColor(rowIdx == 0 ? SdvColor(0xE2E8F0) : (rowIdx == m_selectedRow ? SdvColor(0xDBEAFE) : SdvColor(0xF8FAFC)));
            rt->FillRectangle(rowNumR, brush);

            wchar_t rnBuf[24] = {};
            swprintf_s(rnBuf, L"%d", rowIdx + 1);
            DWRITE_TEXT_ALIGNMENT prevSmallAlign = fmtSmall->GetTextAlignment();
            fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            brush->SetColor(rowIdx == m_selectedRow ? SdvColor(0x1E3A8A) : SdvColor(0x64748B));
            rt->DrawText(rnBuf, static_cast<UINT32>(wcslen(rnBuf)), fmtSmall, rowNumR, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            fmtSmall->SetTextAlignment(prevSmallAlign);

            const auto& rowVec = sheet.rows[rowIdx];
            for (const auto& vc : visCols) {
                D2D1_RECT_F cellR = D2D1::RectF(vc.x1, ry, vc.x2, ry + rowH);

                if (rowIdx == m_selectedRow && vc.colIdx == m_selectedCol) {
                    brush->SetColor(SdvColor(0xDBEAFE));
                    rt->FillRectangle(cellR, brush);
                    activeSelRect = cellR;
                    hasActiveSelRect = true;
                } else if (rowIdx == 0) {
                    brush->SetColor(SdvColor(0xF8FAFC));
                    rt->FillRectangle(cellR, brush);
                } else if (rowIdx % 2 == 1) {
                    brush->SetColor(SdvColor(0xFCFDFF));
                    rt->FillRectangle(cellR, brush);
                }

                if (static_cast<size_t>(vc.colIdx) < rowVec.size() && !rowVec[vc.colIdx].empty()) {
                    brush->SetColor(rowIdx == 0 ? SdvColor(0x0F172A) : SdvColor(0x1E293B));
                    IDWriteTextFormat* tf = (rowIdx == 0) ? hdrFmt : cellFmt;
                    D2D1_RECT_F textR = D2D1::RectF(cellR.left + 5.0f, cellR.top, cellR.right - 5.0f, cellR.bottom);
                    PathUtils::DrawSingleLineClipped(rt, rowVec[vc.colIdx], tf, textR, brush, m_lineScratch);
                }
            }

            brush->SetColor(rowIdx == 0 ? SdvColor(0x94A3B8) : SdvColor(0xE2E8F0));
            rt->DrawLine(D2D1::Point2F(bodyR.left, ry + rowH), D2D1::Point2F(vScrollLeft, ry + rowH), brush, rowIdx == 0 ? 1.2f : 0.7f);
        }

        // Crisp vertical column gridlines drawn over cell fills
        brush->SetColor(SdvColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(m_colHeaderRect.left, m_gridBodyRect.top), D2D1::Point2F(m_colHeaderRect.left, m_gridBodyRect.bottom), brush, 0.9f);
        brush->SetColor(SdvColor(0xE2E8F0));
        for (const auto& vc : visCols) {
            rt->DrawLine(D2D1::Point2F(vc.x2, m_gridBodyRect.top), D2D1::Point2F(vc.x2, m_gridBodyRect.bottom), brush, 0.8f);
        }

        if (hasActiveSelRect) {
            brush->SetColor(SdvColor(0x0078D7));
            rt->DrawRectangle(D2D1::RectF(activeSelRect.left + 0.8f, activeSelRect.top + 0.8f, activeSelRect.right - 0.8f, activeSelRect.bottom - 0.8f), brush, 1.6f);
        }
        rt->PopAxisAlignedClip();

        // Vertical Scrollbar (right side of grid)
        brush->SetColor(SdvColor(0xF1F5F9));
        rt->FillRectangle(m_vScrollTrackRect, brush);
        brush->SetColor(SdvColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(m_vScrollTrackRect.left, m_vScrollTrackRect.top), D2D1::Point2F(m_vScrollTrackRect.left, m_vScrollTrackRect.bottom), brush, 0.8f);
        if (m_maxTopRow > 0 && m_vScrollTrackRect.bottom > m_vScrollTrackRect.top + 8.0f) {
            float trackH = m_vScrollTrackRect.bottom - m_vScrollTrackRect.top;
            float thumbH = std::clamp(trackH * (static_cast<float>(m_visDataRows) / static_cast<float>(std::max(1, totalDataRows))), 24.0f, trackH);
            float frac = static_cast<float>(m_topRow) / static_cast<float>(m_maxTopRow);
            float thumbTop = m_vScrollTrackRect.top + frac * (trackH - thumbH);
            m_vScrollThumbRect = D2D1::RectF(m_vScrollTrackRect.left + 2.0f, thumbTop, m_vScrollTrackRect.right - 2.0f, thumbTop + thumbH);
            bool hovV = PathUtils::PtInRectF(mousePt.x, mousePt.y, m_vScrollThumbRect);
            brush->SetColor(m_draggingVScroll ? SdvColor(0x0078D7) : (hovV ? SdvColor(0x64748B) : SdvColor(0x94A3B8)));
            rt->FillRoundedRectangle(D2D1::RoundedRect(m_vScrollThumbRect, 3.0f, 3.0f), brush);
        } else {
            m_vScrollThumbRect = {};
        }

        // Horizontal Scrollbar (bottom of grid) + bottom-right corner cap
        D2D1_RECT_F cornerR = D2D1::RectF(vScrollLeft, hScrollTop, bodyR.right, bodyR.bottom);
        brush->SetColor(SdvColor(0xE2E8F0));
        rt->FillRectangle(cornerR, brush);
        brush->SetColor(SdvColor(0xF1F5F9));
        rt->FillRectangle(m_hScrollTrackRect, brush);
        brush->SetColor(SdvColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(m_hScrollTrackRect.left, m_hScrollTrackRect.top), D2D1::Point2F(bodyR.right, m_hScrollTrackRect.top), brush, 0.8f);
        if (m_maxLeftCol > 0 && m_hScrollTrackRect.right > m_hScrollTrackRect.left + 8.0f) {
            float totalColsPx = 0.0f;
            for (int c = 0; c < actualCols; ++c) totalColsPx += GetColWidth(c);
            float trackW = m_hScrollTrackRect.right - m_hScrollTrackRect.left;
            float thumbW = std::clamp(trackW * (gridColsW / std::max(1.0f, totalColsPx)), 24.0f, trackW);
            float frac = static_cast<float>(m_leftCol) / static_cast<float>(m_maxLeftCol);
            float thumbLeft = m_hScrollTrackRect.left + frac * (trackW - thumbW);
            m_hScrollThumbRect = D2D1::RectF(thumbLeft, m_hScrollTrackRect.top + 2.0f, thumbLeft + thumbW, m_hScrollTrackRect.bottom - 2.0f);
            bool hovH = PathUtils::PtInRectF(mousePt.x, mousePt.y, m_hScrollThumbRect);
            brush->SetColor(m_draggingHScroll ? SdvColor(0x0078D7) : (hovH ? SdvColor(0x64748B) : SdvColor(0x94A3B8)));
            rt->FillRoundedRectangle(D2D1::RoundedRect(m_hScrollThumbRect, 3.0f, 3.0f), brush);
        } else {
            m_hScrollThumbRect = {};
        }
    } else {
        // Rich Document View (.docx, .odt, .rtf) with Vertical Scrollbar
        constexpr float sbSize = 12.0f;
        float docRight = std::max(bodyR.left + 40.0f, bodyR.right - sbSize);
        m_colHeaderRect = {};
        m_hScrollTrackRect = {};
        m_hScrollThumbRect = {};
        m_gridBodyRect = D2D1::RectF(bodyR.left, bodyR.top, docRight, bodyR.bottom);
        m_vScrollTrackRect = D2D1::RectF(docRight, bodyR.top, bodyR.right, bodyR.bottom);

        brush->SetColor(SdvColor(0xF1F5F9));
        rt->FillRectangle(m_gridBodyRect, brush);

        D2D1_RECT_F pageR = D2D1::RectF(m_gridBodyRect.left + 12.0f, m_gridBodyRect.top + 10.0f, m_gridBodyRect.right - 12.0f, m_gridBodyRect.bottom - 10.0f);
        brush->SetColor(SdvColor(0xFFFFFF));
        rt->FillRoundedRectangle(D2D1::RoundedRect(pageR, 4.0f, 4.0f), brush);
        brush->SetColor(SdvColor(0xCBD5E1));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(pageR, 4.0f, 4.0f), brush, 1.0f);

        float textW = std::max(80.0f, pageR.right - pageR.left - 32.0f);
        int charsPerLine = std::max(20, static_cast<int>(textW / 6.8f));
        auto calcBlockH = [&](const DocParagraph& para) -> float {
            int explicitBreaks = static_cast<int>(std::count(para.text.begin(), para.text.end(), L'\n'));
            int wrappedLines = std::max(1, static_cast<int>((para.text.size() + charsPerLine - 1) / charsPerLine));
            int lines = std::max(wrappedLines, explicitBreaks + 1);
            return lines * 20.0f + (para.headingLevel > 0 ? 10.0f : (para.isTableRow ? 8.0f : 6.0f));
        };

        float totalDocH = 28.0f;
        for (const auto& para : m_doc.paragraphs) {
            totalDocH += calcBlockH(para);
        }
        float pageViewH = std::max(1.0f, pageR.bottom - pageR.top);
        m_maxDocScrollY = std::max(0.0f, totalDocH - pageViewH);
        m_docScrollY = std::clamp(m_docScrollY, 0.0f, m_maxDocScrollY);

        rt->PushAxisAlignedClip(pageR, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float y = pageR.top + 14.0f - m_docScrollY;

        for (const auto& para : m_doc.paragraphs) {
            float blockH = calcBlockH(para);
            if (y + blockH >= pageR.top && y <= pageR.bottom) {
                float xLeft = pageR.left + 16.0f + (para.isBullet ? 14.0f : 0.0f);
                if (para.isTableRow) {
                    D2D1_RECT_F trR = D2D1::RectF(pageR.left + 14.0f, y - 1.0f, pageR.right - 14.0f, y + blockH - 3.0f);
                    brush->SetColor(SdvColor(0xF8FAFC));
                    rt->FillRectangle(trR, brush);
                    brush->SetColor(SdvColor(0xCBD5E1));
                    rt->DrawRectangle(trR, brush, 0.8f);
                    xLeft = pageR.left + 20.0f;
                } else if (para.isBullet) {
                    brush->SetColor(SdvColor(0x0078D7));
                    rt->DrawText(L"\x2022", 1, fmtBold, D2D1::RectF(pageR.left + 16.0f, y, xLeft, y + 20.0f), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
                IDWriteTextFormat* tf = (para.headingLevel > 0) ? fmtBold : (para.isTableRow ? fmtSmall : fmtRegular);
                brush->SetColor(para.headingLevel == 1 ? SdvColor(0x1E3A8A) : (para.headingLevel == 2 ? SdvColor(0x1E293B) : SdvColor(0x0F172A)));
                DWRITE_WORD_WRAPPING prevWrap = tf->GetWordWrapping();
                tf->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                rt->DrawText(para.text.c_str(), static_cast<UINT32>(para.text.size()), tf,
                    D2D1::RectF(xLeft, y + (para.isTableRow ? 2.0f : 0.0f), pageR.right - 16.0f, y + blockH),
                    brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                tf->SetWordWrapping(prevWrap);
            }
            y += blockH;
        }
        rt->PopAxisAlignedClip();

        // Vertical Scrollbar for Rich Document View
        brush->SetColor(SdvColor(0xF1F5F9));
        rt->FillRectangle(m_vScrollTrackRect, brush);
        brush->SetColor(SdvColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(m_vScrollTrackRect.left, m_vScrollTrackRect.top), D2D1::Point2F(m_vScrollTrackRect.left, m_vScrollTrackRect.bottom), brush, 0.8f);
        if (m_maxDocScrollY > 0.0f && m_vScrollTrackRect.bottom > m_vScrollTrackRect.top + 8.0f) {
            float trackH = m_vScrollTrackRect.bottom - m_vScrollTrackRect.top;
            float thumbH = std::clamp(trackH * (pageViewH / std::max(1.0f, totalDocH)), 24.0f, trackH);
            float frac = m_docScrollY / m_maxDocScrollY;
            float thumbTop = m_vScrollTrackRect.top + frac * (trackH - thumbH);
            m_vScrollThumbRect = D2D1::RectF(m_vScrollTrackRect.left + 2.0f, thumbTop, m_vScrollTrackRect.right - 2.0f, thumbTop + thumbH);
            bool hovV = PathUtils::PtInRectF(mousePt.x, mousePt.y, m_vScrollThumbRect);
            brush->SetColor(m_draggingVScroll ? SdvColor(0x0078D7) : (hovV ? SdvColor(0x64748B) : SdvColor(0x94A3B8)));
            rt->FillRoundedRectangle(D2D1::RoundedRect(m_vScrollThumbRect, 3.0f, 3.0f), brush);
        } else {
            m_vScrollThumbRect = {};
        }
    }

    // Bottom Status Strip (Rows x Cols + Live Numeric Column Stats: Sum, Avg, Min, Max, or Word/Char Count)
    D2D1_RECT_F stR = D2D1::RectF(rect.left, bodyR.bottom, rect.right, rect.bottom);
    brush->SetColor(SdvColor(0xF1F5F9));
    rt->FillRectangle(stR, brush);
    brush->SetColor(SdvColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(stR.left, stR.top), D2D1::Point2F(stR.right, stR.top), brush, 1.0f);

    wchar_t stBuf[320] = {};
    if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
        const auto& sh = m_doc.sheets[std::clamp(m_activeSheetIdx, 0, static_cast<int>(m_doc.sheets.size()) - 1)];
        if (m_statNumericCount > 0) {
            swprintf_s(stBuf,
                pl ? L"%s (%zu wierszy \xD7 %zu kol.)   \x2022   Kol %s: Suma = %.2f  |  Śr = %.2f  |  Min = %.2f  |  Max = %.2f"
                   : L"%s (%zu rows \xD7 %zu cols)   \x2022   Col %s: Sum = %.2f  |  Avg = %.2f  |  Min = %.2f  |  Max = %.2f",
                m_doc.formatTitle.c_str(), sh.rows.size(), sh.maxCols,
                ColIndexToExcelLetters(m_selectedCol).c_str(),
                m_statSum, m_statAvg, m_statMin, m_statMax);
        } else {
            swprintf_s(stBuf,
                pl ? L"%s   \x2022   Wierszy: %zu   |   Kolumn: %zu   |   Arkuszy: %zu"
                   : L"%s   \x2022   Rows: %zu   |   Columns: %zu   |   Sheets: %zu",
                m_doc.formatTitle.c_str(), sh.rows.size(), sh.maxCols, m_doc.sheets.size());
        }
    } else {
        swprintf_s(stBuf,
            pl ? L"%s   \x2022   Akapitów: %zu   |   Słów: %zu   |   Znaków: %zu"
               : L"%s   \x2022   Paragraphs: %zu   |   Words: %zu   |   Characters: %zu",
            m_doc.formatTitle.c_str(), m_doc.paragraphs.size(), m_doc.wordCount, m_doc.charCount);
    }
    brush->SetColor(SdvColor(0x334155));
    PathUtils::DrawSingleLineClipped(rt, stBuf, cellFmt,
        D2D1::RectF(stR.left + 8.0f, stR.top, stR.right - 8.0f, stR.bottom), brush, m_lineScratch);
}

bool SpreadsheetDocViewer::OnMouseDown(float x, float y, bool rightButton, Lang lang, std::wstring* outStatusToast) {
    if (rightButton) return false;
    const bool pl = (lang == Lang::PL);

    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            if (hit.id == BTN_TOGGLE_MAP_VIEW) {
                m_showMapView = !m_showMapView;
                return true;
            }
            if (hit.id == BTN_RESET_MAP_CAM) {
                m_mapZoom = 1.0f;
                m_mapPanX = 0.0f;
                m_mapPanY = 0.0f;
                return true;
            }
            if (hit.id == BTN_OPEN_OSM_WEB && !m_doc.geoPoints.empty()) {
                double cLat = m_doc.geoPoints[m_doc.geoPoints.size() / 2].lat;
                double cLon = m_doc.geoPoints[m_doc.geoPoints.size() / 2].lon;
                wchar_t url[256] = {};
                swprintf_s(url, L"https://www.openstreetmap.org/?mlat=%.6f&mlon=%.6f#map=14/%.6f/%.6f", cLat, cLon, cLat, cLon);
                ShellExecuteW(m_parentHwnd, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
                if (outStatusToast) *outStatusToast = pl ? L"\xD83C\xDF0D Otwarto trasę w OpenStreetMap" : L"\xD83C\xDF0D Opened track in OpenStreetMap";
                return true;
            }
            if (hit.id >= BTN_SHEET_BASE && hit.id < BTN_SHEET_BASE + 32) {
                m_showMapView = false;
                m_activeSheetIdx = hit.id - BTN_SHEET_BASE;
                m_topRow = 0;
                m_leftCol = 0;
                m_selectedRow = 0;
                m_selectedCol = 0;
                m_sortedCol = -1;
                ResetColumnWidthsForActiveSheet();
                RecomputeColumnStats(0);
                return true;
            }
            if (hit.id == BTN_SORT_ASC_DESC) {
                SortActiveSheetByColumn(m_selectedCol);
                return true;
            }
            if (hit.id == BTN_COPY_CELL && m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
                const auto& sh = m_doc.sheets[m_activeSheetIdx];
                if (m_selectedRow >= 0 && static_cast<size_t>(m_selectedRow) < sh.rows.size() &&
                    m_selectedCol >= 0 && static_cast<size_t>(m_selectedCol) < sh.rows[m_selectedRow].size()) {
                    PathUtils::CopyWideToClipboard(m_parentHwnd, sh.rows[m_selectedRow][m_selectedCol]);
                    if (outStatusToast) *outStatusToast = pl ? L"\x2713 Skopiowano komórkę do schowka" : L"\x2713 Copied cell to clipboard";
                }
                return true;
            }
            if (hit.id == BTN_COPY_ALL) {
                std::wstring out;
                if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
                    const auto& sh = m_doc.sheets[m_activeSheetIdx];
                    for (const auto& r : sh.rows) {
                        for (size_t c = 0; c < r.size(); ++c) {
                            if (c > 0) out += L"\t";
                            out += r[c];
                        }
                        out += L"\r\n";
                    }
                } else {
                    for (const auto& p : m_doc.paragraphs) {
                        out += p.text + L"\r\n\r\n";
                    }
                }
                PathUtils::CopyWideToClipboard(m_parentHwnd, out);
                if (outStatusToast) *outStatusToast = pl ? L"\x2713 Skopiowano zawartość do schowka" : L"\x2713 Copied content to clipboard";
                return true;
            }
        }
    }

    if (m_doc.isSpreadsheet && m_doc.hasGeoTrack && m_showMapView && PathUtils::PtInRectF(x, y, m_gridBodyRect)) {
        m_mapDragging = true;
        m_mapDragStartX = x - m_mapPanX;
        m_mapDragStartY = y - m_mapPanY;
        return true;
    }

    // Vertical scrollbar click / drag (Spreadsheet Grid or Rich Document View)
    if (m_vScrollTrackRect.right > m_vScrollTrackRect.left && PathUtils::PtInRectF(x, y, m_vScrollTrackRect)) {
        bool isGrid = (m_doc.isSpreadsheet && !m_doc.sheets.empty());
        bool hasRange = isGrid ? (m_maxTopRow > 0) : (m_maxDocScrollY > 0.0f);
        if (hasRange) {
            float trackH = std::max(1.0f, m_vScrollTrackRect.bottom - m_vScrollTrackRect.top);
            float thumbH = std::max(24.0f, m_vScrollThumbRect.bottom - m_vScrollThumbRect.top);
            if (PathUtils::PtInRectF(x, y, m_vScrollThumbRect)) {
                m_draggingVScroll = true;
                m_scrollDragOffset = y - m_vScrollThumbRect.top;
            } else {
                float usable = std::max(1.0f, trackH - thumbH);
                float frac = std::clamp((y - thumbH * 0.5f - m_vScrollTrackRect.top) / usable, 0.0f, 1.0f);
                if (isGrid) {
                    m_topRow = std::clamp(static_cast<int>(std::lround(frac * static_cast<float>(m_maxTopRow))), 0, m_maxTopRow);
                } else {
                    m_docScrollY = frac * m_maxDocScrollY;
                }
                m_draggingVScroll = true;
                m_scrollDragOffset = thumbH * 0.5f;
            }
        }
        return true;
    }

    // Horizontal scrollbar click / drag (Spreadsheet Grid View)
    if (m_hScrollTrackRect.right > m_hScrollTrackRect.left && PathUtils::PtInRectF(x, y, m_hScrollTrackRect)) {
        if (m_doc.isSpreadsheet && !m_doc.sheets.empty() && m_maxLeftCol > 0) {
            float trackW = std::max(1.0f, m_hScrollTrackRect.right - m_hScrollTrackRect.left);
            float thumbW = std::max(24.0f, m_hScrollThumbRect.right - m_hScrollThumbRect.left);
            if (PathUtils::PtInRectF(x, y, m_hScrollThumbRect)) {
                m_draggingHScroll = true;
                m_scrollDragOffset = x - m_hScrollThumbRect.left;
            } else {
                float usable = std::max(1.0f, trackW - thumbW);
                float frac = std::clamp((x - thumbW * 0.5f - m_hScrollTrackRect.left) / usable, 0.0f, 1.0f);
                m_leftCol = std::clamp(static_cast<int>(std::lround(frac * static_cast<float>(m_maxLeftCol))), 0, m_maxLeftCol);
                m_draggingHScroll = true;
                m_scrollDragOffset = thumbW * 0.5f;
            }
        }
        return true;
    }

    if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
        int divCol = HitColumnResizeHandle(x, y);
        if (divCol >= 0) {
            ULONGLONG now = GetTickCount64();
            if (divCol == m_lastDividerClickCol && (now - m_lastDividerClickTick) <= GetDoubleClickTime()) {
                m_lastDividerClickCol = -1;
                AutoFitColumnWidth(divCol);
                return true;
            }
            m_lastDividerClickCol = divCol;
            m_lastDividerClickTick = now;
            if (static_cast<size_t>(divCol) >= m_colWidths.size()) {
                m_colWidths.resize(static_cast<size_t>(divCol) + 1, kDefaultCellWidth);
            }
            m_resizingCol = divCol;
            m_resizeStartX = x;
            m_resizeStartW = GetColWidth(divCol);
            return true;
        }
        if (PathUtils::PtInRectF(x, y, m_colHeaderRect)) {
            int colIdx = HitColumnAtX(x);
            if (colIdx >= 0) {
                m_selectedCol = colIdx;
                SortActiveSheetByColumn(colIdx);
                RecomputeColumnStats(colIdx);
            }
            return true;
        }
        if (PathUtils::PtInRectF(x, y, m_gridBodyRect)) {
            int r = static_cast<int>((y - m_gridBodyRect.top) / 21.0f);
            const auto& sh = m_doc.sheets[m_activeSheetIdx];
            int rowIdx = (r == 0) ? 0 : (m_topRow + r);
            if (rowIdx >= 0 && static_cast<size_t>(rowIdx) < sh.rows.size()) {
                m_selectedRow = rowIdx;
            }
            int colIdx = HitColumnAtX(x);
            if (colIdx >= 0) {
                m_selectedCol = colIdx;
                RecomputeColumnStats(m_selectedCol);
            }
            return true;
        }
    }
    return false;
}

bool SpreadsheetDocViewer::OnMouseMove(float x, float y) {
    if (m_mapDragging) {
        m_mapPanX = x - m_mapDragStartX;
        m_mapPanY = y - m_mapDragStartY;
        return true;
    }
    if (m_resizingCol >= 0) {
        if (static_cast<size_t>(m_resizingCol) >= m_colWidths.size()) {
            m_colWidths.resize(static_cast<size_t>(m_resizingCol) + 1, kDefaultCellWidth);
        }
        m_colWidths[m_resizingCol] = std::clamp(m_resizeStartW + (x - m_resizeStartX), kMinCellWidth, kMaxCellWidth);
        return true;
    }
    if (m_draggingVScroll) {
        float trackH = std::max(1.0f, m_vScrollTrackRect.bottom - m_vScrollTrackRect.top);
        float thumbH = std::max(24.0f, m_vScrollThumbRect.bottom - m_vScrollThumbRect.top);
        float usable = std::max(1.0f, trackH - thumbH);
        float frac = std::clamp((y - m_scrollDragOffset - m_vScrollTrackRect.top) / usable, 0.0f, 1.0f);
        if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
            m_topRow = std::clamp(static_cast<int>(std::lround(frac * static_cast<float>(m_maxTopRow))), 0, m_maxTopRow);
        } else {
            m_docScrollY = frac * m_maxDocScrollY;
        }
        return true;
    }
    if (m_draggingHScroll) {
        float trackW = std::max(1.0f, m_hScrollTrackRect.right - m_hScrollTrackRect.left);
        float thumbW = std::max(24.0f, m_hScrollThumbRect.right - m_hScrollThumbRect.left);
        float usable = std::max(1.0f, trackW - thumbW);
        float frac = std::clamp((x - m_scrollDragOffset - m_hScrollTrackRect.left) / usable, 0.0f, 1.0f);
        m_leftCol = std::clamp(static_cast<int>(std::lround(frac * static_cast<float>(m_maxLeftCol))), 0, m_maxLeftCol);
        return true;
    }

    bool changed = false;
    int newHov = 0;
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newHov = hit.id;
            break;
        }
    }
    if (newHov != m_hoverBtnId) {
        m_hoverBtnId = newHov;
        changed = true;
    }
    int newScrollHov = 0;
    if (m_vScrollThumbRect.bottom > m_vScrollThumbRect.top && PathUtils::PtInRectF(x, y, m_vScrollThumbRect)) {
        newScrollHov = 1;
    } else if (m_hScrollThumbRect.right > m_hScrollThumbRect.left && PathUtils::PtInRectF(x, y, m_hScrollThumbRect)) {
        newScrollHov = 2;
    }
    if (newScrollHov != m_hoverScrollPart) {
        m_hoverScrollPart = newScrollHov;
        changed = true;
    }
    return changed;
}

bool SpreadsheetDocViewer::OnMouseUp() {
    bool wasActive = m_mapDragging || m_draggingVScroll || m_draggingHScroll || (m_resizingCol >= 0);
    m_mapDragging = false;
    m_draggingVScroll = false;
    m_draggingHScroll = false;
    m_resizingCol = -1;
    return wasActive;
}

bool SpreadsheetDocViewer::OnMouseWheel(short delta, float x, float y) {
    bool overGrid = PathUtils::PtInRectF(x, y, m_gridBodyRect) ||
                    PathUtils::PtInRectF(x, y, m_colHeaderRect) ||
                    PathUtils::PtInRectF(x, y, m_vScrollTrackRect) ||
                    PathUtils::PtInRectF(x, y, m_hScrollTrackRect);
    if (!overGrid) return false;

    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;

    if (m_doc.isSpreadsheet && m_doc.hasGeoTrack && m_showMapView) {
        float factor = (notches > 0) ? 1.22f : (1.0f / 1.22f);
        m_mapZoom = std::clamp(m_mapZoom * factor, 0.25f, 30.0f);
        return true;
    }
    if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
        bool overHScroll = (m_hScrollTrackRect.right > m_hScrollTrackRect.left) && PathUtils::PtInRectF(x, y, m_hScrollTrackRect);
        if (overHScroll || (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            m_leftCol = std::clamp(m_leftCol - notches, 0, m_maxLeftCol);
        } else {
            m_topRow = std::clamp(m_topRow - notches * 3, 0, m_maxTopRow);
        }
        return true;
    } else {
        m_docScrollY = std::clamp(m_docScrollY - notches * 48.0f, 0.0f, m_maxDocScrollY);
        return true;
    }
}

bool SpreadsheetDocViewer::OnMouseHWheel(short delta, float x, float y) {
    if (!m_doc.isSpreadsheet || (m_doc.hasGeoTrack && m_showMapView) || m_doc.sheets.empty()) {
        return false;
    }
    bool overGrid = PathUtils::PtInRectF(x, y, m_gridBodyRect) ||
                    PathUtils::PtInRectF(x, y, m_colHeaderRect) ||
                    PathUtils::PtInRectF(x, y, m_vScrollTrackRect) ||
                    PathUtils::PtInRectF(x, y, m_hScrollTrackRect);
    if (!overGrid) return false;

    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;
    // In WM_MOUSEHWHEEL, positive delta tilts/scrolls right
    m_leftCol = std::clamp(m_leftCol + notches, 0, m_maxLeftCol);
    return true;
}

} // namespace Explorer2
