// ArchiveExplorer.cpp - the archive pane on the right side (table of contents, browsing, extract buttons). The
// readers it shows live in ArchiveReaders.cpp.
#include "ArchiveExplorer.h"
#include "PathUtils.h"
#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace Explorer2 {

namespace {

static D2D1_COLOR_F ArcColor(uint32_t rgb, float a = 1.0f) {
    return PathUtils::ColorFromRgb(rgb, a);
}

} // namespace

// ============================================================================
// Right-Side FileContentPane UI Implementation
// ============================================================================
void ArchiveExplorer::Initialize(HWND parentHwnd) {
    m_parentHwnd = parentHwnd;
}

void ArchiveExplorer::Shutdown() {
    Clear();
}

void ArchiveExplorer::Clear() {
    m_archivePath.clear();
    m_currentInnerDir.clear();
    m_summary = {};
    m_visibleItems.clear();
    m_scrollY = 0.0f;
    m_selectedIdx = -1;
    m_hoverIdx = -1;
}

void ArchiveExplorer::LoadFile(const std::wstring& fullPath, bool forceReload) {
    if (!forceReload && m_archivePath == fullPath && m_summary.valid) {
        return;
    }
    Clear();
    m_archivePath = fullPath;
    m_currentInnerDir.clear();
    EnumerateArchiveFolder(fullPath, L"", m_visibleItems, &m_summary);
}

void ArchiveExplorer::ShowLoadedArchive(const std::wstring& fullPath, ArchiveSummaryInfo&& summary, std::vector<ArchiveItemEntry>&& rootItems) {
    Clear();
    m_archivePath = fullPath;
    m_currentInnerDir.clear();
    m_summary = std::move(summary);
    m_visibleItems = std::move(rootItems);
}

void ArchiveExplorer::RebuildCurrentSubfolderView() {
    m_visibleItems.clear();
    m_scrollY = 0.0f;
    m_selectedIdx = -1;
    m_hoverIdx = -1;
    if (m_summary.valid) {
        ArchiveReaders::FilterArchiveSummaryFolder(m_summary, m_currentInnerDir, m_visibleItems);
    } else {
        EnumerateArchiveFolder(m_archivePath, m_currentInnerDir, m_visibleItems, &m_summary);
    }
}

void ArchiveExplorer::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& rect,
    Lang lang,
    D2D1_POINT_2F mousePt)
{
    (void)mousePt;
    m_toolbarHits.clear();
    const bool pl = (lang == Lang::PL);

    if (!m_summary.valid) {
        const wchar_t* msg = pl ? L"Nie udało się odczytać struktury archiwum." : L"Failed to read archive structure.";
        brush->SetColor(ArcColor(0x64748B));
        rt->DrawText(msg, static_cast<UINT32>(wcslen(msg)), fmtRegular,
            D2D1::RectF(rect.left + 16.0f, rect.top + 24.0f, rect.right - 16.0f, rect.bottom), brush);
        return;
    }

    // 1. Top Toolbar (32px): [↑ W górę] [Otwórz w panelu ➔] [Rozpakuj tutaj] [Rozpakuj do podfolderu]
    float barH = 32.0f;
    D2D1_RECT_F barR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + barH);
    brush->SetColor(ArcColor(0xF8FAFC));
    rt->FillRectangle(barR, brush);
    brush->SetColor(ArcColor(0xE2E8F0));
    rt->DrawLine(D2D1::Point2F(barR.left, barR.bottom - 0.5f), D2D1::Point2F(barR.right, barR.bottom - 0.5f), brush, 1.0f);

    float bx = barR.left + 8.0f;
    auto addBtn = [&](int id, float w, const wchar_t* label, bool enabled = true) {
        D2D1_RECT_F r = D2D1::RectF(bx, barR.top + 4.0f, bx + w, barR.bottom - 4.0f);
        if (enabled) m_toolbarHits.push_back({ id, r });
        PathUtils::DrawToolbarButton(rt, brush, fmtSmall, r, label, false, enabled && (m_hoverBtnId == id));
        bx += w + 5.0f;
    };

    if (!m_currentInnerDir.empty()) {
        addBtn(BTN_UP_DIR, pl ? 68.0f : 56.0f, pl ? L"\x2191 W górę" : L"\x2191 Up");
    }
    addBtn(BTN_OPEN_AS_FOLDER, pl ? 114.0f : 108.0f, pl ? L"Otwórz w panelu \x2794" : L"Browse in pane \x2794");
    addBtn(BTN_EXTRACT_HERE, pl ? 98.0f : 90.0f, pl ? L"\xD83D\xDCE6 Rozpakuj tu" : L"\xD83D\xDCE6 Extract Here");
    addBtn(BTN_EXTRACT_SUBFOLDER, pl ? 118.0f : 114.0f, pl ? L"\xD83D\xDCC2 Do podfolderu" : L"\xD83D\xDCC2 To Subfolder");

    // 2. Archive Info & Breadcrumb Banner (26px)
    float infoH = 26.0f;
    D2D1_RECT_F infoR = D2D1::RectF(rect.left, barR.bottom, rect.right, barR.bottom + infoH);
    brush->SetColor(ArcColor(0xEFF6FF));
    rt->FillRectangle(infoR, brush);
    brush->SetColor(ArcColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(infoR.left, infoR.bottom - 0.5f), D2D1::Point2F(infoR.right, infoR.bottom - 0.5f), brush, 1.0f);

    int ratioPct = (m_summary.totalUncompressedBytes > 0)
        ? static_cast<int>((m_summary.totalCompressedBytes * 100ULL) / m_summary.totalUncompressedBytes)
        : 100;
    std::wstring unpStr = PathUtils::FormatByteSize(m_summary.totalUncompressedBytes);
    std::wstring cmpStr = PathUtils::FormatByteSize(m_summary.totalCompressedBytes);
    std::wstring pathBadge = m_currentInnerDir.empty() ? L"/" : (L"/" + m_currentInnerDir);

    wchar_t infoBuf[384] = {};
    swprintf_s(infoBuf,
        pl ? L"[%s]  \xD83D\xDCC2 %s   \x2022   %zu plików, %zu fold.   \x2022   %s \x2192 %s (%d%%)   (2\xD7klik = podgląd)"
           : L"[%s]  \xD83D\xDCC2 %s   \x2022   %zu files, %zu dirs   \x2022   %s \x2192 %s (%d%%)   (double-click = preview)",
        m_summary.formatName.c_str(), pathBadge.c_str(),
        m_summary.totalFiles, m_summary.totalFolders,
        unpStr.c_str(), cmpStr.c_str(), ratioPct);
    brush->SetColor(ArcColor(0x1E3A8A));
    rt->DrawText(infoBuf, static_cast<UINT32>(wcslen(infoBuf)), fmtSmall,
        D2D1::RectF(infoR.left + 8.0f, infoR.top + 4.0f, infoR.right - 8.0f, infoR.bottom), brush);

    // 3. Column Headers (22px)
    float colH = 22.0f;
    D2D1_RECT_F colR = D2D1::RectF(rect.left, infoR.bottom, rect.right, infoR.bottom + colH);
    brush->SetColor(ArcColor(0xF1F5F9));
    rt->FillRectangle(colR, brush);
    brush->SetColor(ArcColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(colR.left, colR.bottom - 0.5f), D2D1::Point2F(colR.right, colR.bottom - 0.5f), brush, 1.0f);

    float scrollW = 12.0f;
    float listRight = rect.right - scrollW;
    float packColW = 84.0f;
    float unpColW  = 84.0f;
    float nameRight = std::max(colR.left + 80.0f, listRight - packColW - unpColW);

    brush->SetColor(ArcColor(0x475569));
    rt->DrawText(pl ? L"Nazwa w archiwum" : L"Name in archive", pl ? 16 : 15, fmtBold,
        D2D1::RectF(colR.left + 10.0f, colR.top + 2.0f, nameRight - 4.0f, colR.bottom), brush);
    fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (nameRight + unpColW <= listRight) {
        rt->DrawText(pl ? L"Rozmiar" : L"Size", pl ? 7 : 4, fmtBold,
            D2D1::RectF(nameRight, colR.top + 2.0f, nameRight + unpColW - 6.0f, colR.bottom), brush);
    }
    rt->DrawText(pl ? L"Skompres." : L"Packed", pl ? 9 : 6, fmtBold,
        D2D1::RectF(listRight - packColW, colR.top + 2.0f, listRight - 8.0f, colR.bottom), brush);
    fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // 4. Scrollable Archive Entries List
    m_listRect = D2D1::RectF(rect.left, colR.bottom, listRight, rect.bottom);
    float rowH = 22.0f;
    float listH = std::max(1.0f, m_listRect.bottom - m_listRect.top);
    float totalH = static_cast<float>(m_visibleItems.size()) * rowH;
    float maxScroll = std::max(0.0f, totalH - listH);
    m_scrollY = std::clamp(m_scrollY, 0.0f, maxScroll);

    rt->PushAxisAlignedClip(m_listRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    int firstIdx = std::max(0, static_cast<int>(m_scrollY / rowH));
    int lastIdx = std::min(static_cast<int>(m_visibleItems.size()) - 1,
                           static_cast<int>((m_scrollY + listH) / rowH) + 1);
    for (int i = firstIdx; i <= lastIdx; ++i) {
        const auto& item = m_visibleItems[i];
        float ry = m_listRect.top + i * rowH - m_scrollY;
        D2D1_RECT_F rowR = D2D1::RectF(m_listRect.left, ry, m_listRect.right, ry + rowH);

        if (i == m_selectedIdx) {
            brush->SetColor(ArcColor(0xCCE8FF));
            rt->FillRectangle(rowR, brush);
            brush->SetColor(ArcColor(0x0078D7));
            rt->DrawRectangle(D2D1::RectF(rowR.left + 0.5f, rowR.top + 0.5f, rowR.right - 0.5f, rowR.bottom - 0.5f), brush, 1.0f);
        } else if (i == m_hoverIdx) {
            brush->SetColor(ArcColor(0xE5F3FF));
            rt->FillRectangle(rowR, brush);
        } else if (i % 2 == 1) {
            brush->SetColor(ArcColor(0xF8FAFC));
            rt->FillRectangle(rowR, brush);
        }

        D2D1_RECT_F icoR = D2D1::RectF(rowR.left + 8.0f, rowR.top + 4.0f, rowR.left + 21.0f, rowR.bottom - 4.0f);
        if (item.isDirectory) {
            brush->SetColor(ArcColor(0xF59E0B));
            rt->FillRoundedRectangle(D2D1::RoundedRect(icoR, 2.0f, 2.0f), brush);
        } else {
            brush->SetColor(ArcColor(0x3B82F6));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(icoR, 1.5f, 1.5f), brush, 1.2f);
        }

        brush->SetColor(ArcColor(0x0F172A));
        rt->DrawText(item.name.c_str(), static_cast<UINT32>(item.name.size()), fmtRegular,
            D2D1::RectF(rowR.left + 26.0f, rowR.top + 1.0f, nameRight - 4.0f, rowR.bottom), brush);

        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        std::wstring uStr = PathUtils::FormatByteSize(item.uncompressedSize);
        std::wstring cStr = PathUtils::FormatByteSize(item.compressedSize);
        if (nameRight + unpColW <= listRight) {
            brush->SetColor(ArcColor(0x475569));
            rt->DrawText(uStr.c_str(), static_cast<UINT32>(uStr.size()), fmtSmall,
                D2D1::RectF(nameRight, rowR.top + 2.0f, nameRight + unpColW - 6.0f, rowR.bottom), brush);
        }
        brush->SetColor(ArcColor(0x64748B));
        rt->DrawText(cStr.c_str(), static_cast<UINT32>(cStr.size()), fmtSmall,
            D2D1::RectF(listRight - packColW, rowR.top + 2.0f, listRight - 8.0f, rowR.bottom), brush);
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    rt->PopAxisAlignedClip();

    // Scrollbar
    m_scrollTrackRect = D2D1::RectF(listRight, colR.bottom, rect.right, rect.bottom);
    brush->SetColor(ArcColor(0xF1F5F9));
    rt->FillRectangle(m_scrollTrackRect, brush);
    if (totalH > listH) {
        float thumbH = std::max(24.0f, listH * (listH / totalH));
        float frac = (maxScroll > 0.0f) ? (m_scrollY / maxScroll) : 0.0f;
        float thumbTop = m_scrollTrackRect.top + frac * (listH - thumbH);
        m_scrollThumbRect = D2D1::RectF(m_scrollTrackRect.left + 2.0f, thumbTop, m_scrollTrackRect.right - 2.0f, thumbTop + thumbH);
        brush->SetColor(m_draggingScroll ? ArcColor(0x0078D7) : ArcColor(0x94A3B8));
        rt->FillRoundedRectangle(D2D1::RoundedRect(m_scrollThumbRect, 3.0f, 3.0f), brush);
    } else {
        m_scrollThumbRect = {};
    }
}

bool ArchiveExplorer::OnMouseDown(float x, float y, bool rightButton, Lang lang, std::wstring* outStatusToast) {
    if (rightButton) return false;
    const bool pl = (lang == Lang::PL);

    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            switch (hit.id) {
            case BTN_UP_DIR: {
                size_t slash = m_currentInnerDir.find_last_of(L'/');
                if (slash == std::wstring::npos) m_currentInnerDir.clear();
                else m_currentInnerDir = m_currentInnerDir.substr(0, slash);
                RebuildCurrentSubfolderView();
                return true;
            }
            case BTN_OPEN_AS_FOLDER: {
                std::wstring target = m_archivePath;
                if (!m_currentInnerDir.empty()) {
                    std::wstring sub = m_currentInnerDir;
                    for (wchar_t& ch : sub) if (ch == L'/') ch = L'\\';
                    target = PathUtils::EnsureTrailingSlash(target) + sub;
                }
                m_pendingNavigateArchivePath = target;
                return true;
            }
            case BTN_EXTRACT_HERE:
            case BTN_EXTRACT_SUBFOLDER: {
                std::wstring target = PathUtils::GetParentPath(m_archivePath);
                if (hit.id == BTN_EXTRACT_SUBFOLDER) {
                    std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(m_archivePath), false);
                    target = PathUtils::EnsureTrailingSlash(target) + stem;
                }
                m_pendingExtract = { m_archivePath, target, hit.id == BTN_EXTRACT_SUBFOLDER };
                m_hasPendingExtract = true;
                return true;
            }
            }
        }
    }

    if (PathUtils::PtInRectF(x, y, m_scrollTrackRect)) {
        if (PathUtils::PtInRectF(x, y, m_scrollThumbRect)) {
            m_draggingScroll = true;
            m_scrollDragOffset = y - m_scrollThumbRect.top;
        } else {
            float rowH = 22.0f;
            float listH = std::max(1.0f, m_listRect.bottom - m_listRect.top);
            float maxScroll = std::max(0.0f, static_cast<float>(m_visibleItems.size()) * rowH - listH);
            float frac = std::clamp((y - m_scrollTrackRect.top) / listH, 0.0f, 1.0f);
            m_scrollY = frac * maxScroll;
        }
        return true;
    }

    if (PathUtils::PtInRectF(x, y, m_listRect)) {
        int idx = static_cast<int>((y - m_listRect.top + m_scrollY) / 22.0f);
        if (idx >= 0 && idx < static_cast<int>(m_visibleItems.size())) {
            ULONGLONG now = GetTickCount64();
            bool isDbl = (idx == m_lastClickIdx && (now - m_lastClickTick) <= GetDoubleClickTime());
            m_selectedIdx = idx;
            m_lastClickIdx = idx;
            m_lastClickTick = now;

            if (isDbl) {
                m_lastClickIdx = -1;
                const auto itemCopy = m_visibleItems[idx];
                if (itemCopy.isDirectory) {
                    m_currentInnerDir = itemCopy.pathInArchive;
                    RebuildCurrentSubfolderView();
                } else {
                    m_pendingPreviewEntry.archivePath = m_archivePath;
                    m_pendingPreviewEntry.innerPath = itemCopy.pathInArchive;
                    m_pendingPreviewEntry.name = itemCopy.name;
                    m_pendingPreviewEntry.size = itemCopy.uncompressedSize;
                    m_hasPendingPreviewEntry = true;
                    if (outStatusToast) {
                        *outStatusToast = (pl ? L"Podgląd z archiwum: " : L"Preview from archive: ") + itemCopy.name;
                    }
                }
            }
            return true;
        }
    }

    return false;
}

bool ArchiveExplorer::OnMouseMove(float x, float y) {
    bool changed = false;
    int newBtn = 0;
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newBtn = hit.id;
            break;
        }
    }
    if (newBtn != m_hoverBtnId) {
        m_hoverBtnId = newBtn;
        changed = true;
    }

    if (m_draggingScroll) {
        float rowH = 22.0f;
        float listH = std::max(1.0f, m_listRect.bottom - m_listRect.top);
        float maxScroll = std::max(0.0f, static_cast<float>(m_visibleItems.size()) * rowH - listH);
        float thumbH = m_scrollThumbRect.bottom - m_scrollThumbRect.top;
        float usable = std::max(1.0f, listH - thumbH);
        float frac = std::clamp((y - m_scrollDragOffset - m_scrollTrackRect.top) / usable, 0.0f, 1.0f);
        m_scrollY = frac * maxScroll;
        return true;
    }

    int newHov = -1;
    if (PathUtils::PtInRectF(x, y, m_listRect)) {
        int idx = static_cast<int>((y - m_listRect.top + m_scrollY) / 22.0f);
        if (idx >= 0 && idx < static_cast<int>(m_visibleItems.size())) {
            newHov = idx;
        }
    }
    if (newHov != m_hoverIdx) {
        m_hoverIdx = newHov;
        changed = true;
    }
    return changed;
}

bool ArchiveExplorer::OnMouseUp() {
    if (m_draggingScroll) {
        m_draggingScroll = false;
        return true;
    }
    return false;
}

bool ArchiveExplorer::OnMouseWheel(short delta, float x, float y) {
    if (!PathUtils::PtInRectF(x, y, m_listRect)) return false;
    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;
    float rowH = 22.0f;
    float listH = std::max(1.0f, m_listRect.bottom - m_listRect.top);
    float maxScroll = std::max(0.0f, static_cast<float>(m_visibleItems.size()) * rowH - listH);
    m_scrollY = std::clamp(m_scrollY - notches * rowH * 3.0f, 0.0f, maxScroll);
    return true;
}

} // namespace Explorer2
