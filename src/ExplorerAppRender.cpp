#include "ExplorerApp.h"
#include "CommandIds.h"
#include "PathUtils.h"
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <ctime>
#include <limits>
#include <map>
#include <functional>

namespace Explorer2 {

static D2D1_COLOR_F HexColor(uint32_t rgb, float a = 1.0f) {
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        a);
}

static std::wstring FormatMediaTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    int totalSec = static_cast<int>(seconds + 0.5);
    int hrs = totalSec / 3600;
    int mins = (totalSec % 3600) / 60;
    int secs = totalSec % 60;
    wchar_t buf[64] = {};
    if (hrs > 0) {
        swprintf_s(buf, L"%d:%02d:%02d", hrs, mins, secs);
    } else {
        swprintf_s(buf, L"%02d:%02d", mins, secs);
    }
    return buf;
}

void ExplorerApp::OnPaint() {
    if (!CreateDeviceResources()) {
        ValidateRect(m_hwnd, nullptr);
        return;
    }

    PAINTSTRUCT ps = {};
    BeginPaint(m_hwnd, &ps);

    m_rt->BeginDraw();
    m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    m_rt->Clear(HexColor(0xFFFFFF));

    m_ribbonHits.clear();
    m_mediaSeekBarRect = {};

    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    float w = static_cast<float>(rc.right - rc.left);
    float h = static_cast<float>(rc.bottom - rc.top);

    if (m_isFullScreen) {
        m_contentPane.HideNativeControls();
        DrawFullViewerCanvas(D2D1::RectF(0.0f, 0.0f, w, h), false);
    } else {
        float ribbonTotalH = kRibbonTabsHeight + (m_ribbonCollapsed ? 0.0f : kRibbonBodyHeight);
        DrawRibbon(D2D1::RectF(0.0f, 0.0f, w, ribbonTotalH));
        DrawNavBar(D2D1::RectF(0.0f, ribbonTotalH, w, ribbonTotalH + kNavBarHeight));

        float bodyTop = ribbonTotalH + kNavBarHeight;
        float bodyBottom = std::max(bodyTop, h - kStatusBarHeight);

        if (m_showNavPane) {
            DrawTreePane(D2D1::RectF(0.0f, bodyTop, m_treePaneWidth, bodyBottom));
        }

        D2D1_RECT_F fullRect = GetFullWorkspaceRect();
        if (IsSplitViewActive()) {
            int otherIdx = 1 - m_activePane;

            // 1. Draw the inactive pane first
            D2D1_RECT_F otherOuter = GetSplitPaneOuterRect(otherIdx);
            D2D1_RECT_F otherMain  = GetSplitPaneContentMainRect(otherIdx);
            D2D1_RECT_F otherHdr   = D2D1::RectF(otherOuter.left, otherOuter.top, otherOuter.right, otherMain.top);
            SwapWithOtherPane();
            m_overrideMainRect = true;
            m_customMainRect = otherMain;
            DrawSplitPaneHeader(otherIdx, otherHdr, false);
            DrawMainContent(otherMain);
            m_overrideMainRect = false;
            SwapWithOtherPane();

            // 2. Draw the active pane
            D2D1_RECT_F activeOuter = GetSplitPaneOuterRect(m_activePane);
            D2D1_RECT_F activeMain  = GetSplitPaneContentMainRect(m_activePane);
            D2D1_RECT_F activeHdr   = D2D1::RectF(activeOuter.left, activeOuter.top, activeOuter.right, activeMain.top);
            DrawSplitPaneHeader(m_activePane, activeHdr, true);
            DrawMainContent(activeMain);

            // 3. Draw the draggable center splitter bar between Pane 0 and Pane 1
            D2D1_RECT_F p0Outer = GetSplitPaneOuterRect(0);
            D2D1_RECT_F p1Outer = GetSplitPaneOuterRect(1);
            D2D1_RECT_F splitBar = D2D1::RectF(p0Outer.right, fullRect.top, p1Outer.left, fullRect.bottom);
            m_solidBrush->SetColor(m_draggingSplitSplitter ? HexColor(0xCCE8FF) : HexColor(0xF1F5F9));
            m_rt->FillRectangle(splitBar, m_solidBrush);
            float splitCx = (splitBar.left + splitBar.right) * 0.5f;
            m_solidBrush->SetColor(m_draggingSplitSplitter ? HexColor(0x0078D7) : HexColor(0xCBD5E1));
            m_rt->DrawLine(D2D1::Point2F(splitCx, splitBar.top), D2D1::Point2F(splitCx, splitBar.bottom), m_solidBrush, m_draggingSplitSplitter ? 2.0f : 1.0f);
            float gripCy = (splitBar.top + splitBar.bottom) * 0.5f;
            m_solidBrush->SetColor(HexColor(0x64748B));
            for (int g = -2; g <= 2; ++g) {
                m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(splitCx, gripCy + g * 5.0f), 1.3f, 1.3f), m_solidBrush);
            }

            // Active pane highlight border
            m_solidBrush->SetColor(HexColor(0x0078D7, 0.70f));
            m_rt->DrawRectangle(D2D1::RectF(activeOuter.left + 0.5f, activeOuter.top + 0.5f, activeOuter.right - 0.5f, activeOuter.bottom - 0.5f), m_solidBrush, 1.2f);

            DrawOperationToastBanner(fullRect);
        } else {
            D2D1_RECT_F mainRect = GetMainViewRect();
            DrawMainContent(mainRect);
            DrawOperationToastBanner(mainRect);
        }

        if (GitDockVisible()) {
            DrawGitDock(GetGitDockRect());
        }

        if (PreviewPaneVisible()) {
            DrawPreviewPane(GetPreviewPaneRect());
        } else {
            m_contentPane.HideNativeControls();
        }

        DrawStatusBar(D2D1::RectF(0.0f, bodyBottom, w, h));
    }

    HRESULT hr = m_rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }

    EndPaint(m_hwnd, &ps);
}

void ExplorerApp::DrawFolderVectorIcon(const D2D1_RECT_F& r, bool smallIcon) {
    float w = r.right - r.left;
    float h = r.bottom - r.top;
    // Classic Windows 10 golden-yellow folder icon
    D2D1_RECT_F tabR = D2D1::RectF(r.left + 1.0f, r.top + h * 0.14f, r.left + w * 0.46f, r.top + h * 0.36f);
    D2D1_RECT_F bodyR = D2D1::RectF(r.left + 1.0f, r.top + h * 0.26f, r.right - 1.0f, r.bottom - (smallIcon ? 1.0f : 2.0f));

    m_solidBrush->SetColor(HexColor(0xE8B230));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(tabR, 1.5f, 1.5f), m_solidBrush);

    m_solidBrush->SetColor(HexColor(0xFFE07D));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(bodyR, 1.5f, 1.5f), m_solidBrush);

    m_solidBrush->SetColor(HexColor(0xD9A226));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(bodyR, 1.5f, 1.5f), m_solidBrush, 1.0f);
}

void ExplorerApp::DrawFolderWithContentPreview(const D2D1_RECT_F& r, const FileEntry& folderItem) {
    float w = r.right - r.left;
    float h = r.bottom - r.top;

    // 1. Back Flap & Top Tab of the folder
    D2D1_RECT_F tabR  = D2D1::RectF(r.left + 2.0f, r.top + h * 0.08f, r.left + w * 0.44f, r.top + h * 0.28f);
    D2D1_RECT_F bodyR = D2D1::RectF(r.left + 2.0f, r.top + h * 0.18f, r.right - 2.0f, r.bottom - 2.0f);

    m_solidBrush->SetColor(HexColor(0xDF9E23));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(tabR, 2.5f, 2.5f), m_solidBrush);

    m_solidBrush->SetColor(HexColor(0xF6CA56));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(bodyR, 3.0f, 3.0f), m_solidBrush);

    FolderPreviewData prevData;
    bool hasPrev = m_imageCache.GetOrQueueFolderPreview(folderItem.fullPath, folderItem.normKey, prevData);

    // 2. Render up to 4 preview items (2x2 grid or 1-2 cards) inside the folder body!
    if (hasPrev && !prevData.items.empty()) {
        float pad = std::max(3.0f, w * 0.045f);
        D2D1_RECT_F innerR = D2D1::RectF(
            bodyR.left + pad,
            bodyR.top + pad,
            bodyR.right - pad,
            bodyR.bottom - std::max(6.0f, h * 0.12f));

        m_rt->PushAxisAlignedClip(innerR, D2D1_ANTIALIAS_MODE_ALIASED);

        size_t count = std::min<size_t>(4, prevData.items.size());
        float iw = innerR.right - innerR.left;
        float ih = innerR.bottom - innerR.top;
        float gap = std::max(2.0f, w * 0.025f);

        auto getSlotRect = [&](size_t idx) -> D2D1_RECT_F {
            if (count == 1) {
                return innerR;
            }
            if (count == 2) {
                float cw = (iw - gap) * 0.5f;
                float x0 = innerR.left + idx * (cw + gap);
                return D2D1::RectF(x0, innerR.top, x0 + cw, innerR.bottom);
            }
            float cw = (iw - gap) * 0.5f;
            float ch = (ih - gap) * 0.5f;
            size_t col = idx % 2;
            size_t row = idx / 2;
            float x0 = innerR.left + col * (cw + gap);
            float y0 = innerR.top + row * (ch + gap);
            return D2D1::RectF(x0, y0, x0 + cw, y0 + ch);
        };

        for (size_t idx = 0; idx < count; ++idx) {
            const FolderPreviewItem& fp = prevData.items[idx];
            D2D1_RECT_F slotR = getSlotRect(idx);

            if (fp.isImage || fp.isVideo) {
                // White photo frame card + decoded thumbnail bitmap
                m_solidBrush->SetColor(HexColor(0xFFFFFF));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(slotR, 2.0f, 2.0f), m_solidBrush);

                ID2D1Bitmap* subBmp = m_imageCache.GetOrQueueThumbnail(m_rt, fp.fullPath);
                if (subBmp) {
                    D2D1_RECT_F imgBox = D2D1::RectF(slotR.left + 1.5f, slotR.top + 1.5f, slotR.right - 1.5f, slotR.bottom - 1.5f);
                    D2D1_SIZE_F bsz = subBmp->GetSize();
                    D2D1_RECT_F fitR = PathUtils::FitRectCentered(imgBox, bsz.width, bsz.height);
                    m_rt->DrawBitmap(subBmp, fitR, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                } else {
                    m_solidBrush->SetColor(HexColor(0xE2E8F0));
                    m_rt->FillRectangle(D2D1::RectF(slotR.left + 2.0f, slotR.top + 2.0f, slotR.right - 2.0f, slotR.bottom - 2.0f), m_solidBrush);
                }

                m_solidBrush->SetColor(HexColor(0xC49528));
                m_rt->DrawRoundedRectangle(D2D1::RoundedRect(slotR, 2.0f, 2.0f), m_solidBrush, 1.0f);
            } else if (fp.isDirectory) {
                // Mini subfolder card inside the folder
                m_solidBrush->SetColor(HexColor(0xFFF8E1, 0.92f));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(slotR, 2.0f, 2.0f), m_solidBrush);
                m_solidBrush->SetColor(HexColor(0xD9A226));
                m_rt->DrawRoundedRectangle(D2D1::RoundedRect(slotR, 2.0f, 2.0f), m_solidBrush, 1.0f);

                float sw = slotR.right - slotR.left;
                float sh = slotR.bottom - slotR.top;
                float fsz = std::min(sw * 0.65f, sh * 0.58f);
                D2D1_RECT_F subFldR = D2D1::RectF(
                    (slotR.left + slotR.right - fsz) * 0.5f,
                    slotR.top + 3.0f,
                    (slotR.left + slotR.right + fsz) * 0.5f,
                    slotR.top + 3.0f + fsz);
                DrawFolderVectorIcon(subFldR, true);

                if (sh >= 34.0f) {
                    D2D1_RECT_F lblR = D2D1::RectF(slotR.left + 2.0f, subFldR.bottom, slotR.right - 2.0f, slotR.bottom - 1.0f);
                    m_solidBrush->SetColor(HexColor(0x4A3508));
                    m_rt->DrawTextW(fp.name.c_str(), static_cast<UINT32>(fp.name.size()), m_fmtCenterSmall, lblR, m_solidBrush);
                }
            } else {
                // Mini document card with colored extension header band + filename
                m_solidBrush->SetColor(HexColor(0xFFFFFF, 0.96f));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(slotR, 2.0f, 2.0f), m_solidBrush);

                uint32_t bandRgb = 0x6366F1;
                if (fp.ext == L".pdf") bandRgb = 0xEF4444;
                else if (fp.ext == L".zip" || fp.ext == L".7z" || fp.ext == L".rar") bandRgb = 0xF59E0B;
                else if (fp.ext == L".exe" || fp.ext == L".dll" || fp.ext == L".msi") bandRgb = 0x3B82F6;
                else if (fp.ext == L".cpp" || fp.ext == L".h" || fp.ext == L".py" || fp.ext == L".js" || fp.ext == L".json") bandRgb = 0x06B6D4;
                else if (fp.ext == L".txt" || fp.ext == L".md" || fp.ext == L".doc" || fp.ext == L".docx") bandRgb = 0x475569;

                float sh = slotR.bottom - slotR.top;
                float bandH = std::clamp(sh * 0.32f, 5.0f, 16.0f);
                D2D1_RECT_F bandR = D2D1::RectF(slotR.left + 1.0f, slotR.top + 1.0f, slotR.right - 1.0f, slotR.top + bandH);
                m_solidBrush->SetColor(HexColor(bandRgb));
                m_rt->FillRectangle(bandR, m_solidBrush);

                if (bandH >= 11.0f && !fp.ext.empty()) {
                    std::wstring extUp = fp.ext.substr(1);
                    for (wchar_t& ch : extUp) ch = static_cast<wchar_t>(towupper(ch));
                    m_solidBrush->SetColor(HexColor(0xFFFFFF));
                    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    m_rt->DrawTextW(extUp.c_str(), static_cast<UINT32>(extUp.size()), m_fmtSmall, bandR, m_solidBrush);
                    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                }

                if (sh >= 28.0f) {
                    D2D1_RECT_F nmR = D2D1::RectF(slotR.left + 2.0f, bandR.bottom + 1.0f, slotR.right - 2.0f, slotR.bottom - 1.0f);
                    m_solidBrush->SetColor(HexColor(0x334155));
                    m_rt->DrawTextW(fp.name.c_str(), static_cast<UINT32>(fp.name.size()), m_fmtCenterSmall, nmR, m_solidBrush);
                }

                m_solidBrush->SetColor(HexColor(0xCBD5E1));
                m_rt->DrawRoundedRectangle(D2D1::RoundedRect(slotR, 2.0f, 2.0f), m_solidBrush, 1.0f);
            }
        }

        m_rt->PopAxisAlignedClip();
    } else if (hasPrev && prevData.items.empty() && w >= 90.0f) {
        // Subtle "Pusty" / "Empty" watermark on empty folders
        m_solidBrush->SetColor(HexColor(0xB8861B, 0.55f));
        const wchar_t* emptyTxt = (m_lang == Lang::PL) ? L"(pusty)" : L"(empty)";
        m_rt->DrawTextW(emptyTxt, static_cast<UINT32>(wcslen(emptyTxt)), m_fmtCenterSmall, bodyR, m_solidBrush);
    }

    // 3. Front Folder Pocket Lip along the bottom + Outer Border + Item Count Badge
    D2D1_RECT_F lipR = D2D1::RectF(bodyR.left, bodyR.bottom - std::max(6.0f, h * 0.14f), bodyR.right, bodyR.bottom);
    m_solidBrush->SetColor(HexColor(0xFFE07D, 0.90f));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(lipR, 2.5f, 2.5f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xE0AB2E, 0.75f));
    m_rt->DrawLine(D2D1::Point2F(lipR.left + 1.0f, lipR.top), D2D1::Point2F(lipR.right - 1.0f, lipR.top), m_solidBrush, 1.0f);

    m_solidBrush->SetColor(HexColor(0xD49B22));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(bodyR, 3.0f, 3.0f), m_solidBrush, 1.2f);

    if (hasPrev && prevData.totalCount > 0 && w >= 85.0f) {
        std::wstring cntStr = (prevData.totalCount >= 120) ? L"120+" : std::to_wstring(prevData.totalCount);
        float pillW = std::max(22.0f, static_cast<float>(cntStr.size()) * 7.0f + 10.0f);
        D2D1_RECT_F badgeR = D2D1::RectF(bodyR.right - pillW - 4.0f, bodyR.bottom - 18.0f, bodyR.right - 4.0f, bodyR.bottom - 3.0f);
        m_solidBrush->SetColor(HexColor(0x785109, 0.82f));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(badgeR, 3.0f, 3.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(cntStr.c_str(), static_cast<UINT32>(cntStr.size()), m_fmtSmall, badgeR, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
}

void ExplorerApp::DrawDriveVectorIcon(const D2D1_RECT_F& r, bool isNetwork, bool isOptical, bool isOffline) {
    float w = r.right - r.left;
    float h = r.bottom - r.top;
    float cx = (r.left + r.right) * 0.5f;
    float cy = (r.top + r.bottom) * 0.5f;

    // 1. Metallic HDD enclosure box
    D2D1_RECT_F boxR = D2D1::RectF(r.left + w * 0.06f, r.top + h * 0.24f, r.right - w * 0.06f, r.top + h * 0.72f);
    m_solidBrush->SetColor(HexColor(0xDCE1E7));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(boxR, 2.0f, 2.0f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0x8A95A5));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(boxR, 2.0f, 2.0f), m_solidBrush, 1.0f);

    // Front dark faceplate strip
    D2D1_RECT_F frontR = D2D1::RectF(boxR.left + 2.0f, boxR.bottom - std::max(3.0f, h * 0.16f), boxR.right - 2.0f, boxR.bottom - 1.5f);
    m_solidBrush->SetColor(HexColor(0x3B4252));
    m_rt->FillRectangle(frontR, m_solidBrush);

    if (isOptical) {
        // Optical disc circle on top of drive
        float rad = std::min(w, h) * 0.26f;
        D2D1_POINT_2F dc = D2D1::Point2F(cx, r.top + h * 0.36f);
        m_solidBrush->SetColor(HexColor(0x90CAF9));
        m_rt->FillEllipse(D2D1::Ellipse(dc, rad, rad), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x1E88E5));
        m_rt->DrawEllipse(D2D1::Ellipse(dc, rad, rad), m_solidBrush, 1.0f);
    }

    if (isNetwork) {
        // Network T-pipe below the drive enclosure
        float pipeY = boxR.bottom + std::max(2.0f, h * 0.12f);
        m_solidBrush->SetColor(HexColor(0x6D7885));
        m_rt->DrawLine(D2D1::Point2F(cx, boxR.bottom), D2D1::Point2F(cx, pipeY), m_solidBrush, 1.5f);
        m_rt->DrawLine(D2D1::Point2F(r.left + w * 0.22f, pipeY), D2D1::Point2F(r.right - w * 0.22f, pipeY), m_solidBrush, 1.8f);
    }

    // 2. Online Green Checkmark ('✓') OR Offline Red Cross ('✕') Badge on bottom-right!
    float badgeR = std::clamp(std::min(w, h) * 0.24f, 3.8f, 14.0f);
    D2D1_POINT_2F bc = D2D1::Point2F(r.right - badgeR - 0.5f, r.bottom - badgeR - 0.5f);

    if (isOffline) {
        m_solidBrush->SetColor(HexColor(0xE81123));
        m_rt->FillEllipse(D2D1::Ellipse(bc, badgeR, badgeR), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        float d = badgeR * 0.52f;
        float stroke = std::max(1.4f, badgeR * 0.28f);
        m_rt->DrawLine(D2D1::Point2F(bc.x - d, bc.y - d), D2D1::Point2F(bc.x + d, bc.y + d), m_solidBrush, stroke);
        m_rt->DrawLine(D2D1::Point2F(bc.x + d, bc.y - d), D2D1::Point2F(bc.x - d, bc.y + d), m_solidBrush, stroke);
    } else if (!isOptical) {
        m_solidBrush->SetColor(HexColor(0x10893E));
        m_rt->FillEllipse(D2D1::Ellipse(bc, badgeR, badgeR), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        float stroke = std::max(1.3f, badgeR * 0.26f);
        m_rt->DrawLine(D2D1::Point2F(bc.x - badgeR * 0.45f, bc.y), D2D1::Point2F(bc.x - badgeR * 0.1f, bc.y + badgeR * 0.4f), m_solidBrush, stroke);
        m_rt->DrawLine(D2D1::Point2F(bc.x - badgeR * 0.1f, bc.y + badgeR * 0.4f), D2D1::Point2F(bc.x + badgeR * 0.5f, bc.y - badgeR * 0.38f), m_solidBrush, stroke);
    }
}

void ExplorerApp::DrawRibbonIcon(int cmdId, const D2D1_RECT_F& r) {
    float cx = (r.left + r.right) * 0.5f;
    float cy = (r.top + r.bottom) * 0.5f;
    float sz = std::min(r.right - r.left, r.bottom - r.top) * 0.42f;

    switch (cmdId) {
    case CMD_PIN_QUICK: {
        // Grey/blue pushpin
        m_solidBrush->SetColor(HexColor(0x6D7885));
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.6f, cy + sz * 0.6f), D2D1::Point2F(cx + sz * 0.4f, cy - sz * 0.4f), m_solidBrush, 2.5f);
        m_solidBrush->SetColor(HexColor(0x3A78C9));
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + sz * 0.3f, cy - sz * 0.3f), sz * 0.45f, sz * 0.45f), m_solidBrush);
        break;
    }
    case CMD_COPY:
    case CMD_COPY_TO: {
        // Two overlapping pages
        D2D1_RECT_F p1 = D2D1::RectF(cx - sz * 0.7f, cy - sz * 0.8f, cx + sz * 0.25f, cy + sz * 0.4f);
        D2D1_RECT_F p2 = D2D1::RectF(cx - sz * 0.25f, cy - sz * 0.4f, cx + sz * 0.7f, cy + sz * 0.8f);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->FillRectangle(p1, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x4B89DA));
        m_rt->DrawRectangle(p1, m_solidBrush, 1.3f);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->FillRectangle(p2, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x2B6CB0));
        m_rt->DrawRectangle(p2, m_solidBrush, 1.3f);
        break;
    }
    case CMD_PASTE: {
        // Clipboard + page
        D2D1_RECT_F board = D2D1::RectF(cx - sz * 0.75f, cy - sz * 0.75f, cx + sz * 0.4f, cy + sz * 0.75f);
        m_solidBrush->SetColor(HexColor(0xD4A359));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(board, 2.0f, 2.0f), m_solidBrush);
        D2D1_RECT_F page = D2D1::RectF(cx - sz * 0.1f, cy - sz * 0.2f, cx + sz * 0.8f, cy + sz * 0.85f);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->FillRectangle(page, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x4B89DA));
        m_rt->DrawRectangle(page, m_solidBrush, 1.3f);
        break;
    }
    case CMD_CUT: {
        // Scissors X
        m_solidBrush->SetColor(HexColor(0x4B89DA));
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.6f, cy - sz * 0.6f), D2D1::Point2F(cx + sz * 0.6f, cy + sz * 0.6f), m_solidBrush, 1.8f);
        m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.6f, cy - sz * 0.6f), D2D1::Point2F(cx - sz * 0.6f, cy + sz * 0.6f), m_solidBrush, 1.8f);
        break;
    }
    case CMD_DELETE_MAIN: {
        // "Kosz" - recycle bin: lid + handle + tapered body with slats
        D2D1_RECT_F lid = D2D1::RectF(cx - sz * 0.75f, cy - sz * 0.55f, cx + sz * 0.75f, cy - sz * 0.35f);
        D2D1_RECT_F handle = D2D1::RectF(cx - sz * 0.25f, cy - sz * 0.8f, cx + sz * 0.25f, cy - sz * 0.55f);
        m_solidBrush->SetColor(HexColor(0x6D7885));
        m_rt->DrawRectangle(handle, m_solidBrush, 1.6f);
        m_rt->FillRectangle(lid, m_solidBrush);

        ID2D1PathGeometry* bin = nullptr;
        if (m_d2dFactory && SUCCEEDED(m_d2dFactory->CreatePathGeometry(&bin)) && bin) {
            ID2D1GeometrySink* sink = nullptr;
            if (SUCCEEDED(bin->Open(&sink)) && sink) {
                sink->BeginFigure(D2D1::Point2F(cx - sz * 0.62f, cy - sz * 0.28f), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(D2D1::Point2F(cx + sz * 0.62f, cy - sz * 0.28f));
                sink->AddLine(D2D1::Point2F(cx + sz * 0.48f, cy + sz * 0.85f));
                sink->AddLine(D2D1::Point2F(cx - sz * 0.48f, cy + sz * 0.85f));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                sink->Release();
            }
            m_solidBrush->SetColor(HexColor(0xE8F1FA));
            m_rt->FillGeometry(bin, m_solidBrush);
            m_solidBrush->SetColor(HexColor(0x4B89DA));
            m_rt->DrawGeometry(bin, m_solidBrush, 1.6f);
            bin->Release();
        }
        m_solidBrush->SetColor(HexColor(0x4B89DA));
        for (int i = -1; i <= 1; ++i) {
            float lx = cx + i * sz * 0.28f;
            m_rt->DrawLine(D2D1::Point2F(lx, cy - sz * 0.08f), D2D1::Point2F(lx, cy + sz * 0.65f), m_solidBrush, 1.4f);
        }
        break;
    }
    case CMD_DELETE_PERM: {
        // "Usuń" (permanent) - bold red X
        m_solidBrush->SetColor(HexColor(0xD13438));
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.75f, cy - sz * 0.75f), D2D1::Point2F(cx + sz * 0.75f, cy + sz * 0.75f), m_solidBrush, 3.6f);
        m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.75f, cy - sz * 0.75f), D2D1::Point2F(cx - sz * 0.75f, cy + sz * 0.75f), m_solidBrush, 3.6f);
        break;
    }
    case CMD_RENAME: {
        // Text box with I-beam cursor (like Win10 "Zmień nazwę")
        D2D1_RECT_F box = D2D1::RectF(cx - sz * 0.85f, cy - sz * 0.45f, cx + sz * 0.85f, cy + sz * 0.45f);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->FillRectangle(box, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x3B78C2));
        m_rt->DrawRectangle(box, m_solidBrush, 1.5f);
        m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.25f, cy - sz * 0.8f), D2D1::Point2F(cx + sz * 0.25f, cy + sz * 0.8f), m_solidBrush, 2.0f);
        break;
    }
    case CMD_NEW_FOLDER:
    case CMD_MOVE_TO: {
        DrawFolderVectorIcon(D2D1::RectF(cx - sz * 0.9f, cy - sz * 0.8f, cx + sz * 0.9f, cy + sz * 0.8f));
        if (cmdId == CMD_MOVE_TO) {
            m_solidBrush->SetColor(HexColor(0x2B6CB0));
            m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.4f, cy + sz * 0.1f), D2D1::Point2F(cx + sz * 0.4f, cy + sz * 0.1f), m_solidBrush, 2.2f);
        }
        break;
    }
    case CMD_PROPERTIES: {
        // White sheet with red checkmark (Win10 "Właściwości")
        D2D1_RECT_F sheet = D2D1::RectF(cx - sz * 0.7f, cy - sz * 0.85f, cx + sz * 0.7f, cy + sz * 0.85f);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->FillRectangle(sheet, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x6D7885));
        m_rt->DrawRectangle(sheet, m_solidBrush, 1.3f);
        m_solidBrush->SetColor(HexColor(0xD9534F));
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.35f, cy + sz * 0.1f), D2D1::Point2F(cx - sz * 0.05f, cy + sz * 0.4f), m_solidBrush, 2.2f);
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.05f, cy + sz * 0.4f), D2D1::Point2F(cx + sz * 0.45f, cy - sz * 0.3f), m_solidBrush, 2.2f);
        break;
    }
    case CMD_VIEW_PROPORTIONS: {
        // Multi-colored WinDirStat treemap blocks icon
        D2D1_RECT_F b1 = D2D1::RectF(cx - sz * 0.85f, cy - sz * 0.85f, cx + sz * 0.05f, cy + sz * 0.85f);
        D2D1_RECT_F b2 = D2D1::RectF(cx + sz * 0.18f, cy - sz * 0.85f, cx + sz * 0.85f, cy + sz * 0.05f);
        D2D1_RECT_F b3 = D2D1::RectF(cx + sz * 0.18f, cy + sz * 0.18f, cx + sz * 0.85f, cy + sz * 0.85f);
        m_solidBrush->SetColor(HexColor(0x1E88E5));
        m_rt->FillRectangle(b1, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x43A047));
        m_rt->FillRectangle(b2, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xFB8C00));
        m_rt->FillRectangle(b3, m_solidBrush);
        break;
    }
    case CMD_SHARE_ZIP:
    case CMD_SHARE_EXTRACT_ZIP: {
        // Golden folder with vertical zipper teeth
        DrawFolderVectorIcon(D2D1::RectF(cx - sz * 0.9f, cy - sz * 0.8f, cx + sz * 0.9f, cy + sz * 0.8f));
        m_solidBrush->SetColor(HexColor(0x5D4037));
        for (int z = -2; z <= 3; ++z) {
            float zy = cy + z * (sz * 0.22f);
            float zx = cx + ((z & 1) ? -1.5f : 1.5f);
            m_rt->FillRectangle(D2D1::RectF(zx - 2.0f, zy - 1.2f, zx + 2.0f, zy + 1.2f), m_solidBrush);
        }
        if (cmdId == CMD_SHARE_EXTRACT_ZIP) {
            m_solidBrush->SetColor(HexColor(0x0078D7));
            m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.35f, cy + sz * 0.5f), D2D1::Point2F(cx + sz * 0.35f, cy - sz * 0.45f), m_solidBrush, 2.2f);
            m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.35f, cy - sz * 0.45f), D2D1::Point2F(cx + sz * 0.05f, cy - sz * 0.15f), m_solidBrush, 2.2f);
            m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.35f, cy - sz * 0.45f), D2D1::Point2F(cx + sz * 0.65f, cy - sz * 0.15f), m_solidBrush, 2.2f);
        }
        break;
    }
    case CMD_SHARE_EMAIL: {
        // Blue/white envelope icon
        D2D1_RECT_F env = D2D1::RectF(cx - sz * 0.88f, cy - sz * 0.62f, cx + sz * 0.88f, cy + sz * 0.62f);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(env, 2.0f, 2.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x0078D7));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(env, 2.0f, 2.0f), m_solidBrush, 1.5f);
        m_rt->DrawLine(D2D1::Point2F(env.left + 1.5f, env.top + 1.5f), D2D1::Point2F(cx, cy + sz * 0.1f), m_solidBrush, 1.5f);
        m_rt->DrawLine(D2D1::Point2F(env.right - 1.5f, env.top + 1.5f), D2D1::Point2F(cx, cy + sz * 0.1f), m_solidBrush, 1.5f);
        break;
    }
    case CMD_SHARE_PRINT: {
        // Printer icon
        D2D1_RECT_F bodyR = D2D1::RectF(cx - sz * 0.85f, cy - sz * 0.25f, cx + sz * 0.85f, cy + sz * 0.45f);
        D2D1_RECT_F paperR = D2D1::RectF(cx - sz * 0.55f, cy - sz * 0.75f, cx + sz * 0.55f, cy + sz * 0.8f);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->FillRectangle(paperR, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x6D7885));
        m_rt->DrawRectangle(paperR, m_solidBrush, 1.2f);
        m_solidBrush->SetColor(HexColor(0x455A64));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(bodyR, 2.0f, 2.0f), m_solidBrush);
        break;
    }
    case CMD_SHARE_POWERSHELL:
    case CMD_SHARE_CMD: {
        // Console window [>_]
        D2D1_RECT_F winR = D2D1::RectF(cx - sz * 0.88f, cy - sz * 0.72f, cx + sz * 0.88f, cy + sz * 0.72f);
        m_solidBrush->SetColor(HexColor(cmdId == CMD_SHARE_POWERSHELL ? 0x012456 : 0x1E1E1E));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(winR, 2.5f, 2.5f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xECEFF4));
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.48f, cy - sz * 0.25f), D2D1::Point2F(cx - sz * 0.12f, cy + sz * 0.05f), m_solidBrush, 1.8f);
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.48f, cy + sz * 0.35f), D2D1::Point2F(cx - sz * 0.12f, cy + sz * 0.05f), m_solidBrush, 1.8f);
        m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.05f, cy + sz * 0.35f), D2D1::Point2F(cx + sz * 0.50f, cy + sz * 0.35f), m_solidBrush, 1.8f);
        break;
    }
    case CMD_SHARE_WIN_EXPLORER:
    case CMD_FILE_NEW_WINDOW: {
        DrawFolderVectorIcon(D2D1::RectF(cx - sz * 0.9f, cy - sz * 0.8f, cx + sz * 0.9f, cy + sz * 0.8f));
        m_solidBrush->SetColor(HexColor(0x0078D7));
        m_rt->FillRectangle(D2D1::RectF(cx - sz * 0.6f, cy + sz * 0.35f, cx + sz * 0.6f, cy + sz * 0.7f), m_solidBrush);
        break;
    }
    case CMD_FILE_EXIT: {
        m_solidBrush->SetColor(HexColor(0xE81123));
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.65f, cy - sz * 0.65f), D2D1::Point2F(cx + sz * 0.65f, cy + sz * 0.65f), m_solidBrush, 2.6f);
        m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.65f, cy - sz * 0.65f), D2D1::Point2F(cx - sz * 0.65f, cy + sz * 0.65f), m_solidBrush, 2.6f);
        break;
    }
    case CMD_VIEW_PIECHART: {
        // Multi-colored circular pie chart icon
        float r = sz * 0.85f;
        m_solidBrush->SetColor(HexColor(0x1E88E5));
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), m_solidBrush);
        if (m_d2dFactory) {
            auto drawSlice = [&](float a0, float a1, UINT32 col) {
                ID2D1PathGeometry* pg = nullptr;
                if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(&pg)) && pg) {
                    ID2D1GeometrySink* sk = nullptr;
                    if (SUCCEEDED(pg->Open(&sk)) && sk) {
                        sk->BeginFigure(D2D1::Point2F(cx, cy), D2D1_FIGURE_BEGIN_FILLED);
                        sk->AddLine(D2D1::Point2F(cx + std::cos(a0) * r, cy + std::sin(a0) * r));
                        sk->AddArc(D2D1::ArcSegment(
                            D2D1::Point2F(cx + std::cos(a1) * r, cy + std::sin(a1) * r),
                            D2D1::SizeF(r, r), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                            (a1 - a0 > 3.14159f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
                        sk->EndFigure(D2D1_FIGURE_END_CLOSED);
                        sk->Close();
                        sk->Release();
                    }
                    m_solidBrush->SetColor(HexColor(col));
                    m_rt->FillGeometry(pg, m_solidBrush);
                    pg->Release();
                }
            };
            drawSlice(-1.57f, 0.55f, 0x43A047);
            drawSlice(0.55f, 2.35f, 0xFB8C00);
        }
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 0.32f, r * 0.32f), m_solidBrush);
        break;
    }
    case CMD_MON_TOGGLE: {
        // Record dot / stop square
        if (MonitorDaemon::IsRunning()) {
            m_solidBrush->SetColor(HexColor(0xD13438));
            m_rt->FillRectangle(D2D1::RectF(cx - sz * 0.6f, cy - sz * 0.6f, cx + sz * 0.6f, cy + sz * 0.6f), m_solidBrush);
        } else {
            m_solidBrush->SetColor(HexColor(0xD13438));
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), sz * 0.7f, sz * 0.7f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(0xFFFFFF));
            m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), sz * 0.7f, sz * 0.7f), m_solidBrush, 1.4f);
        }
        break;
    }
    case CMD_MON_REFRESH: {
        // Mini line chart
        m_solidBrush->SetColor(HexColor(0x0078D7));
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.85f, cy + sz * 0.7f), D2D1::Point2F(cx + sz * 0.85f, cy + sz * 0.7f), m_solidBrush, 1.6f);
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.85f, cy - sz * 0.8f), D2D1::Point2F(cx - sz * 0.85f, cy + sz * 0.7f), m_solidBrush, 1.6f);
        m_solidBrush->SetColor(HexColor(0x059669));
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.75f, cy + sz * 0.3f), D2D1::Point2F(cx - sz * 0.3f, cy - sz * 0.2f), m_solidBrush, 2.2f);
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.3f, cy - sz * 0.2f), D2D1::Point2F(cx + sz * 0.1f, cy + sz * 0.15f), m_solidBrush, 2.2f);
        m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.1f, cy + sz * 0.15f), D2D1::Point2F(cx + sz * 0.8f, cy - sz * 0.6f), m_solidBrush, 2.2f);
        break;
    }
    case CMD_LAN_SCAN: {
        // Magnifier over a network dot
        m_solidBrush->SetColor(HexColor(0x0078D7));
        m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - sz * 0.15f, cy - sz * 0.15f), sz * 0.6f, sz * 0.6f), m_solidBrush, 2.4f);
        m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.28f, cy + sz * 0.28f), D2D1::Point2F(cx + sz * 0.85f, cy + sz * 0.85f), m_solidBrush, 3.0f);
        m_solidBrush->SetColor(HexColor(0x059669));
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - sz * 0.15f, cy - sz * 0.15f), sz * 0.16f, sz * 0.16f), m_solidBrush);
        break;
    }
    case CMD_LAN_SSDP:
    case CMD_FTP_CONNECT_DLG:
    case CMD_FTP_BOOKMARKS_MENU: {
        // Radiating arcs / network globe
        m_solidBrush->SetColor(HexColor(0x0284C7));
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), sz * 0.14f, sz * 0.14f), m_solidBrush);
        m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), sz * 0.5f, sz * 0.5f), m_solidBrush, 1.8f);
        m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), sz * 0.85f, sz * 0.85f), m_solidBrush, 1.8f);
        break;
    }
    case CMD_MON_OPENDIR: {
        DrawFolderVectorIcon(D2D1::RectF(cx - sz * 0.9f, cy - sz * 0.8f, cx + sz * 0.9f, cy + sz * 0.8f));
        break;
    }
    case CMD_PROG_ADD: {
        // Green disc with white plus
        m_solidBrush->SetColor(HexColor(0x2E9E4F));
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), sz * 0.85f, sz * 0.85f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.48f, cy), D2D1::Point2F(cx + sz * 0.48f, cy), m_solidBrush, 3.0f);
        m_rt->DrawLine(D2D1::Point2F(cx, cy - sz * 0.48f), D2D1::Point2F(cx, cy + sz * 0.48f), m_solidBrush, 3.0f);
        break;
    }
    case CMD_PROG_UNINSTALL:
    case CMD_PROG_UNINSTALL_ROW: {
        // Program window with a red cross
        D2D1_RECT_F win = D2D1::RectF(cx - sz * 0.85f, cy - sz * 0.7f, cx + sz * 0.85f, cy + sz * 0.7f);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->FillRectangle(win, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x4B89DA));
        m_rt->DrawRectangle(win, m_solidBrush, 1.4f);
        m_rt->FillRectangle(D2D1::RectF(win.left, win.top, win.right, win.top + sz * 0.3f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xD13438));
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.35f, cy - sz * 0.05f), D2D1::Point2F(cx + sz * 0.35f, cy + sz * 0.55f), m_solidBrush, 2.8f);
        m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.35f, cy - sz * 0.05f), D2D1::Point2F(cx - sz * 0.35f, cy + sz * 0.55f), m_solidBrush, 2.8f);
        break;
    }
    case CMD_PROG_REFRESH: {
        // Circular arrow
        m_solidBrush->SetColor(HexColor(0x0078D7));
        m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), sz * 0.65f, sz * 0.65f), m_solidBrush, 2.6f);
        m_solidBrush->SetColor(HexColor(0xF5F6F7));
        m_rt->FillRectangle(D2D1::RectF(cx + sz * 0.2f, cy - sz * 0.95f, cx + sz * 1.0f, cy - sz * 0.3f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x0078D7));
        m_rt->FillRectangle(D2D1::RectF(cx + sz * 0.25f, cy - sz * 0.95f, cx + sz * 0.95f, cy - sz * 0.9f), m_solidBrush);
        m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.3f, cy - sz * 0.8f), D2D1::Point2F(cx + sz * 0.8f, cy - sz * 0.35f), m_solidBrush, 2.4f);
        break;
    }
    case CMD_PROG_OPENLOC: {
        DrawFolderVectorIcon(D2D1::RectF(cx - sz * 0.9f, cy - sz * 0.8f, cx + sz * 0.9f, cy + sz * 0.8f));
        break;
    }
    case CMD_TAB_HARDWARE:
    case CMD_HW_BENCH_CPU:
    case CMD_HW_BENCH_RAM:
    case CMD_HW_REFRESH: {
        // CPU / Microchip vector icon
        D2D1_RECT_F chip = D2D1::RectF(cx - sz * 0.58f, cy - sz * 0.58f, cx + sz * 0.58f, cy + sz * 0.58f);
        m_solidBrush->SetColor(HexColor(0x0078D7));
        m_rt->FillRectangle(chip, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x50E6FF));
        m_rt->FillRectangle(D2D1::RectF(cx - sz * 0.32f, cy - sz * 0.32f, cx + sz * 0.32f, cy + sz * 0.32f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x005A9E));
        for (int i = -1; i <= 1; ++i) {
            float off = i * sz * 0.35f;
            m_rt->DrawLine(D2D1::Point2F(cx + off, cy - sz * 0.88f), D2D1::Point2F(cx + off, cy - sz * 0.58f), m_solidBrush, 1.5f);
            m_rt->DrawLine(D2D1::Point2F(cx + off, cy + sz * 0.58f), D2D1::Point2F(cx + off, cy + sz * 0.88f), m_solidBrush, 1.5f);
            m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.88f, cy + off), D2D1::Point2F(cx - sz * 0.58f, cy + off), m_solidBrush, 1.5f);
            m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.58f, cy + off), D2D1::Point2F(cx + sz * 0.88f, cy + off), m_solidBrush, 1.5f);
        }
        break;
    }
    case CMD_TOGGLE_SPLIT_VIEW: {
        // Two side-by-side panes with a vertical divider
        D2D1_RECT_F outer = D2D1::RectF(cx - sz * 0.88f, cy - sz * 0.72f, cx + sz * 0.88f, cy + sz * 0.72f);
        D2D1_RECT_F leftP = D2D1::RectF(outer.left, outer.top, cx - 1.0f, outer.bottom);
        D2D1_RECT_F rightP = D2D1::RectF(cx + 1.0f, outer.top, outer.right, outer.bottom);
        m_solidBrush->SetColor(HexColor(0xE0F2FE));
        m_rt->FillRectangle(leftP, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->FillRectangle(rightP, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x0078D7));
        m_rt->FillRectangle(D2D1::RectF(outer.left, outer.top, outer.right, outer.top + sz * 0.28f), m_solidBrush);
        m_rt->DrawRectangle(outer, m_solidBrush, 1.5f);
        m_rt->DrawLine(D2D1::Point2F(cx, outer.top), D2D1::Point2F(cx, outer.bottom), m_solidBrush, 1.6f);
        break;
    }
    case CMD_IMG_TOOL_MENU:
    case CMD_IMG_SCALE_50:
    case CMD_IMG_TARGET_CUSTOM:
    case CMD_CTX_ROTATE_CW: {
        // Photo frame + resize corner arrows
        D2D1_RECT_F outer = D2D1::RectF(cx - sz * 0.85f, cy - sz * 0.70f, cx + sz * 0.85f, cy + sz * 0.70f);
        m_solidBrush->SetColor(HexColor(0xE0F2FE));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(outer, 2.0f, 2.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x0284C7));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(outer, 2.0f, 2.0f), m_solidBrush, 1.5f);
        // Sun dot + mountain peak
        m_solidBrush->SetColor(HexColor(0xF59E0B));
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - sz * 0.42f, cy - sz * 0.30f), sz * 0.18f, sz * 0.18f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x0369A1));
        m_rt->DrawLine(D2D1::Point2F(outer.left + 2.0f, outer.bottom - 2.0f), D2D1::Point2F(cx, cy + sz * 0.05f), m_solidBrush, 1.6f);
        m_rt->DrawLine(D2D1::Point2F(cx, cy + sz * 0.05f), D2D1::Point2F(outer.right - 2.0f, outer.bottom - 2.0f), m_solidBrush, 1.6f);
        // Diagonal resize indicator
        m_solidBrush->SetColor(HexColor(0x0F172A));
        m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.15f, cy - sz * 0.15f), D2D1::Point2F(outer.right - 2.5f, outer.top + 2.5f), m_solidBrush, 1.5f);
        break;
    }
    case CMD_NAV_RECENT_DIRS: {
        // Clock / recent history icon
        m_solidBrush->SetColor(HexColor(0xE0F2FE));
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), sz * 0.78f, sz * 0.78f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x0284C7));
        m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), sz * 0.78f, sz * 0.78f), m_solidBrush, 1.5f);
        m_rt->DrawLine(D2D1::Point2F(cx, cy - sz * 0.45f), D2D1::Point2F(cx, cy), m_solidBrush, 1.6f);
        m_rt->DrawLine(D2D1::Point2F(cx, cy), D2D1::Point2F(cx + sz * 0.35f, cy + sz * 0.22f), m_solidBrush, 1.6f);
        break;
    }
    default: {
        D2D1_RECT_F box = D2D1::RectF(cx - sz * 0.55f, cy - sz * 0.55f, cx + sz * 0.55f, cy + sz * 0.55f);
        m_solidBrush->SetColor(HexColor(0x4B89DA));
        m_rt->DrawRectangle(box, m_solidBrush, 1.4f);
        break;
    }
    }
}

void ExplorerApp::DrawRibbon(const D2D1_RECT_F& rect) {
    const Strings& s = GetStrings(m_lang);

    // 1. Top Ribbon Tabs Strip
    D2D1_RECT_F tabStrip = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + kRibbonTabsHeight);
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRectangle(tabStrip, m_solidBrush);

    // Bottom border of tab strip
    m_solidBrush->SetColor(HexColor(0xDADADA));
    m_rt->DrawLine(D2D1::Point2F(tabStrip.left, tabStrip.bottom), D2D1::Point2F(tabStrip.right, tabStrip.bottom), m_solidBrush, 1.0f);

    struct TabDef {
        int cmdId;
        RibbonTab tab;
        const wchar_t* label;
        float width;
        bool isBlueFileTab;
    };
    TabDef tabs[] = {
        { CMD_TAB_FILE,     RibbonTab::File,     s.tabFile,     58.0f,  true  },
        { CMD_TAB_HOME,     RibbonTab::Home,     s.tabHome,     118.0f, false },
        { CMD_TAB_SHARE,    RibbonTab::Share,    s.tabShare,    102.0f, false },
        { CMD_TAB_VIEW,     RibbonTab::View,     s.tabView,     68.0f,  false },
        { CMD_TAB_SEARCH,   RibbonTab::Search,   s.tabSearch,   96.0f,  false },
        { CMD_TAB_HARDWARE, RibbonTab::Hardware, s.tabHardware, 76.0f,  false },
        { CMD_TAB_PROGRAMS, RibbonTab::Programs, m_lang == Lang::PL ? L"Programy" : L"Programs", 84.0f, false },
        { CMD_TAB_MONITORING, RibbonTab::Monitoring, L"Monitoring", 88.0f, false },
        { CMD_TAB_LAN, RibbonTab::Lan, L"LAN", 56.0f, false },
        { CMD_TAB_BACKUPS, RibbonTab::Backups, m_lang == Lang::PL ? L"Kopie zapasowe" : L"Backups", 104.0f, false },
        { CMD_TAB_LOG, RibbonTab::Log, L"Log", 50.0f, false }
    };

    float tx = rect.left;
    for (const auto& t : tabs) {
        D2D1_RECT_F tr = D2D1::RectF(tx, rect.top + 3.0f, tx + t.width, rect.top + kRibbonTabsHeight);
        m_ribbonHits.push_back({ t.cmdId, tr });

        if (t.isBlueFileTab) {
            m_solidBrush->SetColor(HexColor(0x0067B8));
            m_rt->FillRectangle(tr, m_solidBrush);
            m_solidBrush->SetColor(HexColor(0xFFFFFF));
        } else if (m_activeRibbonTab == t.tab && !m_ribbonCollapsed) {
            m_solidBrush->SetColor(HexColor(0xF5F6F7));
            m_rt->FillRectangle(tr, m_solidBrush);
            m_solidBrush->SetColor(HexColor(0xDADADA));
            m_rt->DrawLine(D2D1::Point2F(tr.left, tr.top), D2D1::Point2F(tr.right, tr.top), m_solidBrush, 1.0f);
            m_rt->DrawLine(D2D1::Point2F(tr.left, tr.top), D2D1::Point2F(tr.left, tr.bottom), m_solidBrush, 1.0f);
            m_rt->DrawLine(D2D1::Point2F(tr.right, tr.top), D2D1::Point2F(tr.right, tr.bottom), m_solidBrush, 1.0f);
            m_solidBrush->SetColor(HexColor(0x1E1E1E));
        } else {
            if (m_hoverRibbonBtn == t.cmdId) {
                m_solidBrush->SetColor(HexColor(0xE8F1FB));
                m_rt->FillRectangle(tr, m_solidBrush);
            }
            m_solidBrush->SetColor(HexColor(0x2B2B2B));
        }

        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(t.label, static_cast<UINT32>(wcslen(t.label)), m_fmtSmall, tr, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        tx += t.width;
    }

    // Right-side controls on Tab Strip: [PL / EN] language button + Collapse Ribbon '^' + Blue Help '?'
    D2D1_RECT_F langBtn = D2D1::RectF(rect.right - 110.0f, rect.top + 4.0f, rect.right - 52.0f, rect.top + kRibbonTabsHeight - 3.0f);
    m_ribbonHits.push_back({ CMD_TOGGLE_LANG, langBtn });
    m_solidBrush->SetColor(m_hoverRibbonBtn == CMD_TOGGLE_LANG ? HexColor(0xDCEBFC) : HexColor(0xF0F3F6));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(langBtn, 3.0f, 3.0f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xB8C4D0));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(langBtn, 3.0f, 3.0f), m_solidBrush, 1.0f);
    m_solidBrush->SetColor(HexColor(0x0067B8));
    const wchar_t* langLabel = (m_lang == Lang::PL) ? L"PL \x21C4 EN" : L"EN \x21C4 PL";
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_rt->DrawTextW(langLabel, static_cast<UINT32>(wcslen(langLabel)), m_fmtSmall, langBtn, m_solidBrush);

    D2D1_RECT_F collapseBtn = D2D1::RectF(rect.right - 48.0f, rect.top + 4.0f, rect.right - 26.0f, rect.top + kRibbonTabsHeight - 3.0f);
    m_ribbonHits.push_back({ CMD_TOGGLE_RIBBON, collapseBtn });
    m_solidBrush->SetColor(HexColor(0x666666));
    const wchar_t* chev = m_ribbonCollapsed ? L"\x25BC" : L"\x25B2";
    m_rt->DrawTextW(chev, 1, m_fmtSmall, collapseBtn, m_solidBrush);

    // Blue '?' help circle matching Win10 screenshot
    D2D1_POINT_2F helpCenter = D2D1::Point2F(rect.right - 13.0f, rect.top + 15.0f);
    m_solidBrush->SetColor(HexColor(0x0067B8));
    m_rt->FillEllipse(D2D1::Ellipse(helpCenter, 8.0f, 8.0f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    D2D1_RECT_F helpR = D2D1::RectF(helpCenter.x - 8.0f, helpCenter.y - 8.0f, helpCenter.x + 8.0f, helpCenter.y + 8.0f);
    m_rt->DrawTextW(L"?", 1, m_fmtSmall, helpR, m_solidBrush);
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    if (m_ribbonCollapsed) return;

    // 2. Expanded Ribbon Body (#F5F6F7 background, matching Windows 10 Explorer)
    D2D1_RECT_F body = D2D1::RectF(rect.left, rect.top + kRibbonTabsHeight, rect.right, rect.bottom);
    m_solidBrush->SetColor(HexColor(0xF5F6F7));
    m_rt->FillRectangle(body, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xDADADA));
    m_rt->DrawLine(D2D1::Point2F(body.left, body.bottom - 1.0f), D2D1::Point2F(body.right, body.bottom - 1.0f), m_solidBrush, 1.0f);

    auto drawLargeButton = [&](int cmdId, float x, float w, const wchar_t* label, bool activeHighlight = false) {
        D2D1_RECT_F br = D2D1::RectF(x, body.top + 4.0f, x + w, body.bottom - 17.0f);
        m_ribbonHits.push_back({ cmdId, br });
        if (activeHighlight || m_hoverRibbonBtn == cmdId) {
            m_solidBrush->SetColor(activeHighlight ? HexColor(0xCBE8F6) : HexColor(0xE1ECF4));
            m_rt->FillRectangle(br, m_solidBrush);
            m_solidBrush->SetColor(activeHighlight ? HexColor(0x26A0DA) : HexColor(0xA4C9E8));
            m_rt->DrawRectangle(br, m_solidBrush, 1.0f);
        }
        D2D1_RECT_F iconR = D2D1::RectF(x + (w - 30.0f) * 0.5f, body.top + 7.0f, x + (w + 30.0f) * 0.5f, body.top + 37.0f);
        DrawRibbonIcon(cmdId, iconR);

        D2D1_RECT_F textR = D2D1::RectF(x + 2.0f, body.top + 38.0f, x + w - 2.0f, body.bottom - 18.0f);
        m_solidBrush->SetColor(HexColor(0x1E1E1E));
        m_rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), m_fmtCenterSmall, textR, m_solidBrush);
    };

    auto drawSmallRowButton = [&](int cmdId, float x, float y, float w, const wchar_t* label, bool checked = false) {
        D2D1_RECT_F br = D2D1::RectF(x, y, x + w, y + 22.0f);
        m_ribbonHits.push_back({ cmdId, br });
        if (checked || m_hoverRibbonBtn == cmdId) {
            m_solidBrush->SetColor(checked ? HexColor(0xCBE8F6) : HexColor(0xE1ECF4));
            m_rt->FillRectangle(br, m_solidBrush);
            m_solidBrush->SetColor(checked ? HexColor(0x26A0DA) : HexColor(0xA4C9E8));
            m_rt->DrawRectangle(br, m_solidBrush, 1.0f);
        }
        D2D1_RECT_F iconR = D2D1::RectF(x + 3.0f, y + 3.0f, x + 19.0f, y + 19.0f);
        DrawRibbonIcon(cmdId, iconR);

        D2D1_RECT_F textR = D2D1::RectF(x + 23.0f, y, x + w - 2.0f, y + 22.0f);
        m_solidBrush->SetColor(HexColor(0x1E1E1E));
        m_rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), m_fmtSmall, textR, m_solidBrush);
    };

    auto drawGroupSeparatorAndTitle = [&](float groupStartX, float groupEndX, const wchar_t* groupTitle) {
        m_solidBrush->SetColor(HexColor(0xDFDFDF));
        m_rt->DrawLine(D2D1::Point2F(groupEndX, body.top + 5.0f), D2D1::Point2F(groupEndX, body.bottom - 5.0f), m_solidBrush, 1.0f);

        D2D1_RECT_F titleR = D2D1::RectF(groupStartX, body.bottom - 17.0f, groupEndX, body.bottom - 2.0f);
        m_solidBrush->SetColor(HexColor(0x6E6E6E));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(groupTitle, static_cast<UINT32>(wcslen(groupTitle)), m_fmtSmall, titleR, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    };

    if (m_activeRibbonTab == RibbonTab::View) {
        // WIDOK (View) Ribbon Tab: Panes + Layout (Jumbo 440px, ExtraLarge 256px, Large, Medium, Small, List, Details, FullViewer, FullScreen) + Show/Hide + Dual Pane
        const bool pl = (m_lang == Lang::PL);
        float gx = rect.left + 6.0f;
        drawLargeButton(CMD_TOGGLE_NAVPANE,     gx,          64.0f, s.btnNavPane, m_showNavPane);
        drawLargeButton(CMD_TOGGLE_PREVIEWPANE, gx + 66.0f,  64.0f, s.btnPreviewPane, m_showPreviewPane);
        drawLargeButton(CMD_TOGGLE_SPLIT_VIEW,  gx + 132.0f, 72.0f, pl ? L"Podwójny\nwidok" : L"Split\nView", m_splitView);
        drawGroupSeparatorAndTitle(gx, gx + 208.0f, s.grpPanes);

        float lx = gx + 214.0f;
        drawSmallRowButton(CMD_VIEW_JUMBO,      lx,          body.top + 6.0f,  135.0f, s.viewJumbo,      m_viewMode == ViewMode::JumboIcons);
        drawSmallRowButton(CMD_VIEW_EXTRALARGE, lx,          body.top + 29.0f, 135.0f, s.viewExtraLarge, m_viewMode == ViewMode::ExtraLargeIcons);
        drawSmallRowButton(CMD_VIEW_LARGE,      lx,          body.top + 52.0f, 135.0f, s.viewLarge,      m_viewMode == ViewMode::LargeIcons);

        drawSmallRowButton(CMD_VIEW_MEDIUM,     lx + 138.0f, body.top + 6.0f,  122.0f, s.viewMedium,     m_viewMode == ViewMode::MediumIcons);
        drawSmallRowButton(CMD_VIEW_SMALL,      lx + 138.0f, body.top + 29.0f, 122.0f, s.viewSmall,      m_viewMode == ViewMode::SmallIcons);
        drawSmallRowButton(CMD_VIEW_LIST,       lx + 138.0f, body.top + 52.0f, 122.0f, s.viewList,       m_viewMode == ViewMode::List);

        drawSmallRowButton(CMD_VIEW_DETAILS,     lx + 263.0f, body.top + 6.0f,  98.0f,  s.viewDetails,     m_viewMode == ViewMode::Details);
        drawSmallRowButton(CMD_VIEW_PROPORTIONS, lx + 263.0f, body.top + 29.0f, 98.0f,  s.viewProportions, m_viewMode == ViewMode::Proportions);
        drawSmallRowButton(CMD_VIEW_PIECHART,    lx + 263.0f, body.top + 52.0f, 98.0f,  s.viewPieChart,    m_viewMode == ViewMode::PieChart);

        // Next to Layout: In-Window Full Viewer & Full Screen buttons!
        drawLargeButton(CMD_VIEW_FULLVIEWER, lx + 365.0f, 88.0f, s.viewFullViewer, m_viewMode == ViewMode::FullViewer);
        drawLargeButton(CMD_VIEW_FULLSCREEN, lx + 455.0f, 78.0f, s.viewFullScreen, m_isFullScreen);
        drawGroupSeparatorAndTitle(lx, lx + 538.0f, s.grpLayout);

        float sx = lx + 546.0f;
        drawSmallRowButton(CMD_TOGGLE_EXT,    sx, body.top + 6.0f,  175.0f, s.chkFileExt,    m_showExtensions);
        drawSmallRowButton(CMD_TOGGLE_HIDDEN, sx, body.top + 29.0f, 175.0f, s.chkHiddenItems, m_showHidden);
        drawSmallRowButton(CMD_TOGGLE_GIT,    sx, body.top + 52.0f, 175.0f, pl ? L"Integracja GIT" : L"Git integration", m_gitEnabled);
        drawGroupSeparatorAndTitle(sx, sx + 182.0f, s.grpShowHide);

        float dx = sx + 190.0f;
        drawSmallRowButton(CMD_SPLIT_COPY_OTHER, dx, body.top + 6.0f,  168.0f, pl ? L"\x21C4 Kopiuj do 2. panelu" : L"\x21C4 Copy to 2nd pane");
        drawSmallRowButton(CMD_SPLIT_MOVE_OTHER, dx, body.top + 29.0f, 168.0f, pl ? L"\x2192 Przenieś do 2. panelu" : L"\x2192 Move to 2nd pane");
        drawSmallRowButton(CMD_SPLIT_SWAP_PANES, dx, body.top + 52.0f, 168.0f, pl ? L"\x21C6 Zamień panele (L\x2194P)" : L"\x21C6 Swap panes (L\x2194R)");
        drawGroupSeparatorAndTitle(dx, dx + 174.0f, pl ? L"Dwa panele" : L"Dual Pane");
        return;
    }

    if (m_activeRibbonTab == RibbonTab::Search) {
        // WYSZUKIWANIE (Advanced Search) Ribbon Tab:
        // Group 1: Zakres i opcje (Checkboxes)
        // Group 2: Edytowany od - do (Date range + presets)
        // Group 3: Rozmiar od - do (Size range + unit + presets)
        // Group 4: Typy (Lista) (Category checkboxes + extension checklist menu + custom input)
        // Group 5: Akcje (Szukaj / Stop / Reset)

        auto drawCheckboxItem = [&](int cmdId, float x, float y, float w, const wchar_t* label, bool checked) {
            D2D1_RECT_F br = D2D1::RectF(x, y, x + w, y + 21.0f);
            m_ribbonHits.push_back({ cmdId, br });
            if (m_hoverRibbonBtn == cmdId) {
                m_solidBrush->SetColor(HexColor(0xE1ECF4));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(br, 2.0f, 2.0f), m_solidBrush);
            }
            D2D1_RECT_F boxR = D2D1::RectF(x + 3.0f, y + 3.5f, x + 16.5f, y + 17.0f);
            m_solidBrush->SetColor(checked ? HexColor(0x0078D7) : HexColor(0xFFFFFF));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(boxR, 2.0f, 2.0f), m_solidBrush);
            m_solidBrush->SetColor(checked ? HexColor(0x005A9E) : HexColor(0x8A95A5));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(boxR, 2.0f, 2.0f), m_solidBrush, 1.1f);
            if (checked) {
                m_solidBrush->SetColor(HexColor(0xFFFFFF));
                m_rt->DrawLine(D2D1::Point2F(boxR.left + 3.0f, boxR.top + 7.0f), D2D1::Point2F(boxR.left + 6.0f, boxR.bottom - 3.0f), m_solidBrush, 1.8f);
                m_rt->DrawLine(D2D1::Point2F(boxR.left + 6.0f, boxR.bottom - 3.0f), D2D1::Point2F(boxR.right - 2.5f, boxR.top + 3.0f), m_solidBrush, 1.8f);
            }
            D2D1_RECT_F txtR = D2D1::RectF(x + 21.0f, y, x + w - 2.0f, y + 21.0f);
            m_solidBrush->SetColor(HexColor(0x1E1E1E));
            m_rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), m_fmtSmall, txtR, m_solidBrush);
        };

        auto drawEditableBox = [&](int cmdId, float x, float y, float labelW, float boxW, const wchar_t* prefixLbl, const std::wstring& value, const wchar_t* placeholder, bool enabled) {
            D2D1_RECT_F lblR = D2D1::RectF(x, y, x + labelW, y + 21.0f);
            m_solidBrush->SetColor(enabled ? HexColor(0x1E1E1E) : HexColor(0x666666));
            m_rt->DrawTextW(prefixLbl, static_cast<UINT32>(wcslen(prefixLbl)), m_fmtSmall, lblR, m_solidBrush);

            D2D1_RECT_F fieldR = D2D1::RectF(x + labelW, y + 1.0f, x + labelW + boxW, y + 20.0f);
            m_ribbonHits.push_back({ cmdId, fieldR });
            m_solidBrush->SetColor(HexColor(0xFFFFFF));
            m_rt->FillRectangle(fieldR, m_solidBrush);
            m_solidBrush->SetColor((m_hoverRibbonBtn == cmdId || enabled) ? HexColor(0x0078D7) : HexColor(0xBDBDBD));
            m_rt->DrawRectangle(fieldR, m_solidBrush, (m_hoverRibbonBtn == cmdId) ? 1.4f : 1.0f);

            std::wstring disp = value.empty() ? std::wstring(placeholder) : value;
            m_solidBrush->SetColor(value.empty() ? HexColor(0x888888) : HexColor(0x111111));
            D2D1_RECT_F valR = D2D1::RectF(fieldR.left + 4.0f, fieldR.top, fieldR.right - 3.0f, fieldR.bottom);
            m_rt->DrawTextW(disp.c_str(), static_cast<UINT32>(disp.size()), m_fmtSmall, valR, m_solidBrush);
        };

        auto drawPresetPill = [&](int cmdId, float x, float y, float w, const wchar_t* label, bool active = false) {
            D2D1_RECT_F pr = D2D1::RectF(x, y + 1.0f, x + w, y + 20.0f);
            m_ribbonHits.push_back({ cmdId, pr });
            m_solidBrush->SetColor(active ? HexColor(0x0078D7) : (m_hoverRibbonBtn == cmdId ? HexColor(0xDCEBFC) : HexColor(0xE8EEF4)));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(pr, 3.0f, 3.0f), m_solidBrush);
            m_solidBrush->SetColor(active ? HexColor(0x005A9E) : HexColor(0xB8C4D0));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(pr, 3.0f, 3.0f), m_solidBrush, 1.0f);
            m_solidBrush->SetColor(active ? HexColor(0xFFFFFF) : HexColor(0x2B3846));
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), m_fmtSmall, pr, m_solidBrush);
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        };

        float sx = rect.left + 8.0f;

        // 1. Zakres i dopasowanie (Scope & Matching)
        float g1Start = sx;
        drawCheckboxItem(CMD_SEARCH_CHK_RECURSIVE, sx, body.top + 5.0f,  150.0f, m_lang == Lang::PL ? L"Podfoldery (Rekurencja)" : L"Subfolders (Recursive)", m_searchOpts.recursive);
        drawCheckboxItem(CMD_SEARCH_CHK_FILES,     sx, body.top + 28.0f, 72.0f,  m_lang == Lang::PL ? L"Pliki" : L"Files", m_searchOpts.includeFiles);
        drawCheckboxItem(CMD_SEARCH_CHK_FOLDERS,   sx + 74.0f, body.top + 28.0f, 76.0f, m_lang == Lang::PL ? L"Foldery" : L"Folders", m_searchOpts.includeFolders);
        drawCheckboxItem(CMD_SEARCH_CHK_CONTENT,   sx, body.top + 51.0f, 150.0f, m_lang == Lang::PL ? L"Szukaj w treści plików" : L"Search file contents", m_searchOpts.searchContent);
        sx += 156.0f;
        drawGroupSeparatorAndTitle(g1Start, sx, m_lang == Lang::PL ? L"Zakres" : L"Scope");
        sx += 8.0f;

        // 2. Edytowany od - do (Date Modified Filter)
        float g2Start = sx;
        drawCheckboxItem(CMD_SEARCH_CHK_DATE, sx, body.top + 5.0f, 212.0f, m_lang == Lang::PL ? L"Edytowany (Filtr daty od – do)" : L"Modified (Date from – to)", m_searchOpts.useDateFilter);
        drawEditableBox(CMD_SEARCH_EDIT_DATE_FROM, sx,          body.top + 28.0f, 24.0f, 80.0f, m_lang == Lang::PL ? L"Od:" : L"Fr:", m_searchOpts.dateFromStr, L"RRRR-MM-DD", m_searchOpts.useDateFilter);
        drawEditableBox(CMD_SEARCH_EDIT_DATE_TO,   sx + 108.0f, body.top + 28.0f, 24.0f, 80.0f, m_lang == Lang::PL ? L"Do:" : L"To:", m_searchOpts.dateToStr,   L"RRRR-MM-DD", m_searchOpts.useDateFilter);

        drawPresetPill(CMD_SEARCH_PRESET_TODAY,  sx,          body.top + 52.0f, 48.0f, m_lang == Lang::PL ? L"Dziś" : L"Today");
        drawPresetPill(CMD_SEARCH_PRESET_7DAYS,  sx + 52.0f,  body.top + 52.0f, 48.0f, m_lang == Lang::PL ? L"7 dni" : L"7d");
        drawPresetPill(CMD_SEARCH_PRESET_30DAYS, sx + 104.0f, body.top + 52.0f, 52.0f, m_lang == Lang::PL ? L"30 dni" : L"30d");
        drawPresetPill(CMD_SEARCH_PRESET_YEAR,   sx + 160.0f, body.top + 52.0f, 52.0f, m_lang == Lang::PL ? L"Ten rok" : L"Year");
        sx += 218.0f;
        drawGroupSeparatorAndTitle(g2Start, sx, m_lang == Lang::PL ? L"Edytowany od – do" : L"Date Modified");
        sx += 8.0f;

        // 3. Rozmiar od - do (Size Filter)
        float g3Start = sx;
        drawCheckboxItem(CMD_SEARCH_CHK_SIZE, sx, body.top + 5.0f, 214.0f, m_lang == Lang::PL ? L"Rozmiar pliku (Filtr od – do)" : L"File Size (From – To)", m_searchOpts.useSizeFilter);
        drawEditableBox(CMD_SEARCH_EDIT_SIZE_MIN, sx,         body.top + 28.0f, 24.0f, 54.0f, m_lang == Lang::PL ? L"Od:" : L"Fr:", m_searchOpts.sizeMinStr, L"0", m_searchOpts.useSizeFilter);
        drawEditableBox(CMD_SEARCH_EDIT_SIZE_MAX, sx + 82.0f, body.top + 28.0f, 24.0f, 54.0f, m_lang == Lang::PL ? L"Do:" : L"To:", m_searchOpts.sizeMaxStr, L"\x221E", m_searchOpts.useSizeFilter);

        const wchar_t* unitNames[] = { L"B \x25BE", L"KB \x25BE", L"MB \x25BE", L"GB \x25BE" };
        drawPresetPill(CMD_SEARCH_CYCLE_SIZE_UNIT, sx + 164.0f, body.top + 28.0f, 48.0f, unitNames[static_cast<int>(m_searchOpts.sizeUnit)], true);

        drawPresetPill(CMD_SEARCH_PRESET_SMALL,   sx,          body.top + 52.0f, 50.0f, L"< 1 MB");
        drawPresetPill(CMD_SEARCH_PRESET_GT10MB,  sx + 54.0f,  body.top + 52.0f, 52.0f, L"> 10 MB");
        drawPresetPill(CMD_SEARCH_PRESET_GT100MB, sx + 110.0f, body.top + 52.0f, 56.0f, L"> 100 MB");
        drawPresetPill(CMD_SEARCH_PRESET_GT1GB,   sx + 170.0f, body.top + 52.0f, 44.0f, L"> 1 GB");
        sx += 220.0f;
        drawGroupSeparatorAndTitle(g3Start, sx, m_lang == Lang::PL ? L"Rozmiar od – do" : L"Size Range");
        sx += 8.0f;

        // 4. Typy (Lista) (Types Checklist + Dropdown Menu + Custom Extensions)
        float g4Start = sx;
        drawCheckboxItem(CMD_SEARCH_CHK_TYPE,     sx,          body.top + 5.0f,  98.0f, m_lang == Lang::PL ? L"Filtr typów:" : L"Filter types:", m_searchOpts.useTypeFilter);
        drawCheckboxItem(CMD_SEARCH_CAT_IMAGES,   sx + 100.0f, body.top + 5.0f,  74.0f, m_lang == Lang::PL ? L"Obrazy" : L"Images", (m_searchOpts.categoryMask & SEARCH_CAT_IMAGES) != 0);
        drawCheckboxItem(CMD_SEARCH_CAT_MEDIA,    sx + 176.0f, body.top + 5.0f,  90.0f, m_lang == Lang::PL ? L"Wideo/Audio" : L"Video/Audio", (m_searchOpts.categoryMask & SEARCH_CAT_MEDIA) != 0);
        drawCheckboxItem(CMD_SEARCH_CAT_DOCS,     sx + 268.0f, body.top + 5.0f,  86.0f, m_lang == Lang::PL ? L"Dokumenty" : L"Documents", (m_searchOpts.categoryMask & SEARCH_CAT_DOCS) != 0);

        drawCheckboxItem(CMD_SEARCH_CAT_CODE,     sx,          body.top + 28.0f, 98.0f, m_lang == Lang::PL ? L"Kod / Tekst" : L"Code / Text", (m_searchOpts.categoryMask & SEARCH_CAT_CODE) != 0);
        drawCheckboxItem(CMD_SEARCH_CAT_ARCHIVES, sx + 100.0f, body.top + 28.0f, 74.0f, m_lang == Lang::PL ? L"Archiwa" : L"Archives", (m_searchOpts.categoryMask & SEARCH_CAT_ARCHIVES) != 0);
        drawCheckboxItem(CMD_SEARCH_CAT_MODELS,   sx + 176.0f, body.top + 28.0f, 90.0f, m_lang == Lang::PL ? L"Modele/3D" : L"Models/3D", (m_searchOpts.categoryMask & SEARCH_CAT_MODELS) != 0);
        drawCheckboxItem(CMD_SEARCH_CAT_APPS,     sx + 268.0f, body.top + 28.0f, 86.0f, m_lang == Lang::PL ? L"Programy" : L"Apps (.exe)", (m_searchOpts.categoryMask & SEARCH_CAT_APPS) != 0);

        drawPresetPill(CMD_SEARCH_TYPE_LIST_MENU, sx, body.top + 52.0f, 128.0f, m_lang == Lang::PL ? L"Lista rozszerzeń \x25BE" : L"Extensions List \x25BE", !m_searchOpts.customExtList.empty());
        drawEditableBox(CMD_SEARCH_EDIT_CUSTOM_EXT, sx + 132.0f, body.top + 52.0f, 36.0f, 182.0f, m_lang == Lang::PL ? L"Własne:" : L"Ext:", m_searchOpts.customExtList, L"np. .cs;.cpp;.png", !m_searchOpts.customExtList.empty());
        sx += 356.0f;
        drawGroupSeparatorAndTitle(g4Start, sx, m_lang == Lang::PL ? L"Typy plików (Lista)" : L"File Types (List)");
        sx += 8.0f;

        // 5. Akcje (Szukaj / Stop / Reset)
        float g5Start = sx;
        drawPresetPill(CMD_SEARCH_START,         sx, body.top + 6.0f,  92.0f, m_lang == Lang::PL ? L"\xD83D\xDD0D Szukaj teraz" : L"\xD83D\xDD0D Search Now", true);
        drawPresetPill(CMD_SEARCH_STOP,          sx, body.top + 30.0f, 92.0f, m_lang == Lang::PL ? L"\x23F9 Zatrzymaj" : L"\x23F9 Stop", m_searchProgress.isRunning);
        drawPresetPill(CMD_SEARCH_RESET_FILTERS, sx, body.top + 53.0f, 92.0f, m_lang == Lang::PL ? L"\x21BA Resetuj filtry" : L"\x21BA Reset Filters", false);
        sx += 98.0f;
        drawGroupSeparatorAndTitle(g5Start, sx, m_lang == Lang::PL ? L"Akcje" : L"Actions");
        return;
    }

    if (m_activeRibbonTab == RibbonTab::Share) {
        // UDOSTĘPNIANIE (Share) Ribbon Tab — fully alive!
        float sx = rect.left + 8.0f;

        // Group 1: Wysyłanie i archiwa (Zip, Extract, Email, Print)
        float g1Start = sx;
        drawLargeButton(CMD_SHARE_ZIP,         sx, 86.0f, m_lang == Lang::PL ? L"Spakuj do\n.ZIP" : L"Compress\nto .ZIP"); sx += 88.0f;
        drawLargeButton(CMD_SHARE_EXTRACT_ZIP, sx, 86.0f, m_lang == Lang::PL ? L"Wypakuj\narchiwum" : L"Extract\nArchive"); sx += 88.0f;
        drawLargeButton(CMD_SHARE_EMAIL,       sx, 72.0f, m_lang == Lang::PL ? L"Wyślij\ne-mailem" : L"Send\nEmail"); sx += 74.0f;
        drawLargeButton(CMD_SHARE_PRINT,       sx, 68.0f, m_lang == Lang::PL ? L"Drukuj" : L"Print"); sx += 72.0f;
        drawGroupSeparatorAndTitle(g1Start, sx, m_lang == Lang::PL ? L"Wysyłanie i archiwa" : L"Send & Archives");
        sx += 8.0f;

        // Group 2: Schowek i ścieżki (Copy Path, Copy Name, Explorer)
        float g2Start = sx;
        drawSmallRowButton(CMD_COPY_PATH,          sx, body.top + 6.0f,  148.0f, m_lang == Lang::PL ? L"Kopiuj pełną ścieżkę" : L"Copy full path");
        drawSmallRowButton(CMD_SHARE_COPY_NAME,    sx, body.top + 29.0f, 148.0f, m_lang == Lang::PL ? L"Kopiuj samą nazwę" : L"Copy file name");
        drawSmallRowButton(CMD_SHARE_WIN_EXPLORER, sx, body.top + 52.0f, 148.0f, m_lang == Lang::PL ? L"Pokaż w Eksploratorze" : L"Show in Explorer");
        sx += 154.0f;
        drawGroupSeparatorAndTitle(g2Start, sx, m_lang == Lang::PL ? L"Ścieżki i powłoka" : L"Paths & Shell");
        sx += 8.0f;

        // Group 3: Terminal i sieć (PowerShell, CMD, Map Network Drive, Properties)
        float g3Start = sx;
        drawLargeButton(CMD_SHARE_POWERSHELL, sx, 76.0f, m_lang == Lang::PL ? L"PowerShell\ntutaj" : L"PowerShell\nhere"); sx += 78.0f;
        drawLargeButton(CMD_SHARE_CMD,        sx, 74.0f, m_lang == Lang::PL ? L"Wiersz\npoleceń" : L"CMD\nhere"); sx += 76.0f;
        drawSmallRowButton(CMD_SHARE_MAP_DRIVE,        sx, body.top + 8.0f,  154.0f, m_lang == Lang::PL ? L"Mapuj dysk sieciowy..." : L"Map network drive...");
        drawSmallRowButton(CMD_SHARE_DISCONNECT_DRIVE, sx, body.top + 31.0f, 154.0f, m_lang == Lang::PL ? L"Odłącz dysk sieciowy..." : L"Disconnect net drive...");
        drawSmallRowButton(CMD_PROPERTIES,             sx, body.top + 54.0f, 154.0f, m_lang == Lang::PL ? L"Właściwości / Uprawnienia" : L"Properties / Sharing");
        sx += 160.0f;
        drawGroupSeparatorAndTitle(g3Start, sx, m_lang == Lang::PL ? L"Terminal i sieć" : L"Terminal & Network");
        sx += 8.0f;

        // Group 4: Klient FTP (Main & Split Pane)
        float g4Start = sx;
        drawLargeButton(CMD_FTP_CONNECT_DLG,    sx, 86.0f, m_lang == Lang::PL ? L"Połącz z\nFTP..." : L"Connect\nFTP..."); sx += 88.0f;
        drawLargeButton(CMD_FTP_BOOKMARKS_MENU, sx, 88.0f, m_lang == Lang::PL ? L"Zakładki\nFTP \x25BE" : L"FTP\nBookmarks \x25BE"); sx += 90.0f;
        drawGroupSeparatorAndTitle(g4Start, sx, m_lang == Lang::PL ? L"Klient FTP (2 panele)" : L"FTP Client (Dual-Pane)");
        return;
    }

    if (m_activeRibbonTab == RibbonTab::File) {
        // PLIK (File) Ribbon Tab
        float fx = rect.left + 8.0f;
        float g1Start = fx;
        drawLargeButton(CMD_FILE_NEW_WINDOW,   fx, 82.0f, m_lang == Lang::PL ? L"Nowe\nokno" : L"New\nWindow"); fx += 84.0f;
        drawLargeButton(CMD_SHARE_POWERSHELL,  fx, 82.0f, m_lang == Lang::PL ? L"PowerShell\ntutaj" : L"PowerShell\nhere"); fx += 84.0f;
        drawLargeButton(CMD_SHARE_CMD,         fx, 76.0f, m_lang == Lang::PL ? L"Wiersz\npoleceń" : L"CMD\nhere"); fx += 78.0f;
        drawLargeButton(CMD_SHARE_WIN_EXPLORER,fx, 92.0f, m_lang == Lang::PL ? L"Otwórz w\nWindows" : L"Open in\nWindows"); fx += 94.0f;
        drawLargeButton(CMD_PROPERTIES,        fx, 72.0f, s.btnProperties); fx += 74.0f;
        drawLargeButton(CMD_FILE_EXIT,         fx, 72.0f, m_lang == Lang::PL ? L"Zamknij\nprogram" : L"Exit\nApp"); fx += 76.0f;
        drawGroupSeparatorAndTitle(g1Start, fx, m_lang == Lang::PL ? L"Plik i okno aplikacji" : L"Application & Shell");
        return;
    }

    if (m_activeRibbonTab == RibbonTab::Lan) {
        // LAN Ribbon Tab: scanner controls + Ping Auto-Refresh
        const bool pl = (m_lang == Lang::PL);
        const bool scanning = m_lan.IsScanning();
        const bool autoPing = m_lan.IsPingAutoRefreshEnabled();
        const int pingSec = std::max(1, m_lan.GetPingAutoRefreshIntervalMs() / 1000);
        float lx = rect.left + 6.0f;

        float lg1 = lx;
        drawLargeButton(CMD_LAN_SCAN, lx, 96.0f, scanning ? (pl ? L"Zatrzymaj\nskanowanie" : L"Stop\nscan") : (pl ? L"Skanuj sieć\nponownie" : L"Rescan\nnetwork"), scanning);
        lx += 98.0f;
        drawLargeButton(CMD_LAN_SSDP, lx, 84.0f, pl ? L"Szukaj\nSSDP / UPnP" : L"Find\nSSDP / UPnP", m_lan.IsSsdpRunning());
        lx += 86.0f;
        drawGroupSeparatorAndTitle(lg1, lx, pl ? L"Skanowanie" : L"Scanning");
        lx += 6.0f;

        float lgPing = lx;
        std::wstring autoLbl = autoPing
            ? (pl ? L"Auto-Ping:\nWŁĄCZONY" : L"Auto-Ping:\nENABLED")
            : (pl ? L"Auto-Ping:\nWYŁĄCZONY" : L"Auto-Ping:\nDISABLED");
        drawLargeButton(CMD_LAN_PING_AUTOREFRESH, lx, 92.0f, autoLbl.c_str(), autoPing);
        lx += 94.0f;
        std::wstring intvLbl = (pl ? L"Interwał:\nco " : L"Interval:\nevery ") + std::to_wstring(pingSec) + L" s";
        drawLargeButton(CMD_LAN_PING_INTERVAL, lx, 82.0f, intvLbl.c_str(), false);
        lx += 84.0f;
        drawLargeButton(CMD_LAN_PING_ONCE, lx, 78.0f, pl ? L"Pinguj\nteraz" : L"Ping\nnow", false);
        lx += 80.0f;
        drawGroupSeparatorAndTitle(lgPing, lx, pl ? L"Narzędzie Ping (RTT)" : L"Ping Tool (RTT)");
        lx += 6.0f;

        float lg2 = lx;
        std::wstring adapterLbl = pl ? L"Adapter:\n" : L"Adapter:\n";
        if (!m_lanAdapters.empty()) adapterLbl += m_lanAdapters[static_cast<size_t>(m_lanAdapterIdx) % m_lanAdapters.size()].name;
        else adapterLbl += pl ? L"(brak)" : L"(none)";
        drawLargeButton(CMD_LAN_ADAPTER, lx, 120.0f, adapterLbl.c_str());
        lx += 122.0f;
        drawGroupSeparatorAndTitle(lg2, lx, pl ? L"Interfejs (następny)" : L"Interface (next)");
        lx += 6.0f;

        float lg3 = lx;
        drawSmallRowButton(CMD_HW_BACK_TO_FILES, lx, body.top + 6.0f, 150.0f, pl ? L"Wróć do plików" : L"Back to Files");
        lx += 154.0f;
        drawGroupSeparatorAndTitle(lg3, lx, pl ? L"Okno" : L"Window");
        return;
    }

    if (m_activeRibbonTab == RibbonTab::Backups) {
        // KOPIE ZAPASOWE (Backups & Rclone Sync) Ribbon Tab
        const bool pl = (m_lang == Lang::PL);
        float bx = rect.left + 6.0f;

        float bg1 = bx;
        drawLargeButton(CMD_BACKUP_ADD_JOB, bx, 88.0f, pl ? L"Nowe\nzadanie" : L"New\nBackup Job");
        bx += 90.0f;
        drawLargeButton(CMD_BACKUP_EDIT_JOB, bx, 84.0f, pl ? L"Edytuj\nścieżki" : L"Edit\nPaths");
        bx += 86.0f;
        drawLargeButton(CMD_BACKUP_EDIT_FILTERS, bx, 88.0f, pl ? L"Filtry\nInc / Exc" : L"Filters\nInc / Exc");
        bx += 90.0f;
        drawLargeButton(CMD_BACKUP_DELETE_JOB, bx, 76.0f, pl ? L"Usuń\nzadanie" : L"Delete\nJob");
        bx += 78.0f;
        drawGroupSeparatorAndTitle(bg1, bx, pl ? L"Zadania kopii" : L"Backup Jobs");
        bx += 6.0f;

        float bg2 = bx;
        drawLargeButton(CMD_BACKUP_RUN_NOW, bx, 92.0f, pl ? L"Uruchom\nteraz" : L"Run\nNow", m_backups.IsBusy());
        bx += 94.0f;
        drawLargeButton(CMD_BACKUP_DRY_RUN, bx, 86.0f, pl ? L"Symulacja\n(Dry-Run)" : L"Test\n(Dry-Run)");
        bx += 88.0f;
        drawGroupSeparatorAndTitle(bg2, bx, pl ? L"Wykonanie" : L"Execution");
        bx += 6.0f;

        float bg3 = bx;
        drawSmallRowButton(CMD_BACKUP_CYCLE_SCHED,  bx, body.top + 6.0f,  176.0f, pl ? L"Zmień interwał harmonogramu" : L"Cycle schedule interval");
        drawSmallRowButton(CMD_BACKUP_TOGGLE_AVAIL, bx, body.top + 29.0f, 176.0f, pl ? L"Przełącz: Gdy cel dostępny" : L"Toggle: When target available");
        drawSmallRowButton(CMD_BACKUP_TOGGLE_RCLONE,bx, body.top + 52.0f, 176.0f, pl ? L"Przełącz: Silnik rclone / C++" : L"Toggle: rclone / Native C++");
        bx += 182.0f;
        drawGroupSeparatorAndTitle(bg3, bx, pl ? L"Automatyzacja i rclone" : L"Automation & rclone");
        bx += 6.0f;

        float bg4 = bx;
        drawSmallRowButton(CMD_HW_BACK_TO_FILES, bx, body.top + 6.0f, 150.0f, pl ? L"Wróć do plików" : L"Back to Files");
        bx += 154.0f;
        drawGroupSeparatorAndTitle(bg4, bx, pl ? L"Okno" : L"Window");
        return;
    }

    if (m_activeRibbonTab == RibbonTab::Log) {
        // LOG Ribbon Tab: operation log of this run
        const bool pl = (m_lang == Lang::PL);
        float lx = rect.left + 6.0f;

        float lg1 = lx;
        const wchar_t* flt = m_logFilter == 0 ? (pl ? L"Filtr:\nwszystko" : L"Filter:\nall")
                           : (m_logFilter == 1 ? (pl ? L"Filtr:\nostrzeżenia+" : L"Filter:\nwarnings+") : (pl ? L"Filtr:\nbłędy" : L"Filter:\nerrors"));
        drawLargeButton(CMD_LOG_LEVEL, lx, 100.0f, flt, m_logFilter != 0);
        lx += 102.0f;
        drawLargeButton(CMD_LOG_COPY, lx, 70.0f, pl ? L"Kopiuj\nlog" : L"Copy\nlog");
        lx += 72.0f;
        drawLargeButton(CMD_LOG_CLEAR, lx, 76.0f, pl ? L"Wyczyść\nwidok" : L"Clear\nview");
        lx += 78.0f;
        drawGroupSeparatorAndTitle(lg1, lx, pl ? L"Widok logu" : L"Log view");
        lx += 6.0f;

        float lg2 = lx;
        drawLargeButton(CMD_LOG_OPEN_FILE, lx, 88.0f, pl ? L"Otwórz\nplik logu" : L"Open\nlog file");
        lx += 90.0f;
        drawLargeButton(CMD_LOG_OPEN_FOLDER, lx, 76.0f, pl ? L"Folder\nlogów" : L"Logs\nfolder");
        lx += 78.0f;
        drawGroupSeparatorAndTitle(lg2, lx, pl ? L"Pliki logów (nowy przy każdym starcie)" : L"Log files (new one per start)");
        lx += 6.0f;

        float lg3 = lx;
        drawSmallRowButton(CMD_HW_BACK_TO_FILES, lx, body.top + 6.0f, 150.0f, pl ? L"Wróć do plików" : L"Back to Files");
        lx += 154.0f;
        drawGroupSeparatorAndTitle(lg3, lx, pl ? L"Okno" : L"Window");
        return;
    }

    if (m_activeRibbonTab == RibbonTab::Monitoring) {
        // MONITORING Ribbon Tab: control of the single background recorder + data options
        const bool pl = (m_lang == Lang::PL);
        const bool running = MonitorDaemon::IsRunning();
        const MonitorSettings ms = MonitorStore::LoadSettings();
        float mx = rect.left + 6.0f;

        // Group 1: Rejestrator
        float mg1 = mx;
        drawLargeButton(CMD_MON_TOGGLE, mx, 104.0f,
                        running ? (pl ? L"Zatrzymaj\nnagrywanie" : L"Stop\nrecording")
                                : (pl ? L"Uruchom\nnagrywanie w tle" : L"Start background\nrecording"), running);
        mx += 106.0f;
        if (m_monAutostartMode < 0) m_monAutostartMode = MonitorDaemon::AutostartMode();   // may spawn schtasks.exe: cached, refreshed on demand
        const int autoMode = m_monAutostartMode;
        wchar_t autoLbl[96] = {};
        swprintf_s(autoLbl, pl ? L"Autostart z Windows: %s" : L"Start with Windows: %s", autoMode == 2 ? (pl ? L"WŁ. (admin)" : L"ON (admin)") : (autoMode == 1 ? (pl ? L"WŁ." : L"ON") : (pl ? L"wył." : L"off")));
        wchar_t intLbl[96] = {};
        swprintf_s(intLbl, pl ? L"Próbkowanie co: %d s" : L"Sample every: %d s", ms.intervalSec);
        wchar_t retLbl[96] = {};
        swprintf_s(retLbl, pl ? L"Przechowuj dane: %d dni" : L"Keep data: %d days", ms.retentionDays);
        drawSmallRowButton(CMD_MON_AUTOSTART, mx, body.top + 6.0f,  190.0f, autoLbl, autoMode != 0);
        drawSmallRowButton(CMD_MON_INTERVAL,  mx, body.top + 29.0f, 190.0f, intLbl);
        drawSmallRowButton(CMD_MON_RETENTION, mx, body.top + 52.0f, 190.0f, retLbl);
        mx += 194.0f;
        drawSmallRowButton(CMD_MON_ELEVATED, mx, body.top + 6.0f, 262.0f,
                           ms.elevated ? (pl ? L"Rejestrator jako administrator: WŁ." : L"Recorder as administrator: ON")
                                       : (pl ? L"Rejestrator jako administrator: wył." : L"Recorder as administrator: off"), ms.elevated);
        drawSmallRowButton(CMD_NONE, mx, body.top + 29.0f, 262.0f, pl ? L"(admin = SMART dysków SATA w historii)" : L"(admin = SATA drive SMART in history)");
        mx += 266.0f;
        drawGroupSeparatorAndTitle(mg1, mx, pl ? L"Rejestrator w tle (jeden dla wszystkich okien)" : L"Background recorder (one for all windows)");
        mx += 6.0f;

        // Group 2: Dane
        float mg2 = mx;
        drawLargeButton(CMD_MON_REFRESH, mx, 78.0f, pl ? L"Odśwież\nwykresy" : L"Reload\ncharts"); mx += 80.0f;
        drawLargeButton(CMD_MON_OPENDIR, mx, 92.0f, pl ? L"Folder danych\n(CSV)" : L"Data folder\n(CSV)"); mx += 94.0f;
        drawGroupSeparatorAndTitle(mg2, mx, pl ? L"Dane" : L"Data");
        mx += 6.0f;

        // Group 3: Nawigacja
        float mg3 = mx;
        drawSmallRowButton(CMD_HW_BACK_TO_FILES, mx, body.top + 6.0f, 150.0f, pl ? L"Wróć do plików" : L"Back to Files");
        mx += 154.0f;
        drawGroupSeparatorAndTitle(mg3, mx, pl ? L"Okno" : L"Window");
        return;
    }

    if (m_activeRibbonTab == RibbonTab::Programs) {
        // PROGRAMY (installed programs: add / remove) Ribbon Tab
        const bool pl = (m_lang == Lang::PL);
        float px = rect.left + 6.0f;

        // Group 1: Dodaj
        float pg1 = px;
        drawLargeButton(CMD_PROG_ADD, px, 104.0f, pl ? L"Dodaj program\n(plik instalatora)" : L"Add program\n(installer file)"); px += 106.0f;
        drawGroupSeparatorAndTitle(pg1, px, pl ? L"Dodaj" : L"Add");
        px += 6.0f;

        // Group 2: Usuń
        float pg2 = px;
        drawLargeButton(CMD_PROG_UNINSTALL, px, 86.0f, pl ? L"Odinstaluj\nwybrany" : L"Uninstall\nselected"); px += 88.0f;
        drawLargeButton(CMD_PROG_OPENLOC, px, 78.0f, pl ? L"Folder\ninstalacji" : L"Install\nfolder"); px += 80.0f;
        drawGroupSeparatorAndTitle(pg2, px, pl ? L"Usuń / zarządzaj" : L"Remove / manage");
        px += 6.0f;

        // Group 3: Lista
        float pg3 = px;
        drawLargeButton(CMD_PROG_REFRESH, px, 78.0f, pl ? L"Odśwież\nlistę" : L"Refresh\nlist"); px += 80.0f;
        drawGroupSeparatorAndTitle(pg3, px, pl ? L"Lista" : L"List");
        px += 6.0f;

        // Group 4: Windows
        float pg4 = px;
        drawSmallRowButton(CMD_PROG_APPWIZ,    px, body.top + 6.0f,  210.0f, pl ? L"Programy i funkcje (klasyczne)" : L"Programs and Features (classic)");
        drawSmallRowButton(CMD_PROG_SETTINGS,  px, body.top + 29.0f, 210.0f, pl ? L"Aplikacje i funkcje (Ustawienia)" : L"Apps & features (Settings)");
        drawSmallRowButton(CMD_HW_BACK_TO_FILES, px, body.top + 52.0f, 210.0f, pl ? L"Wróć do plików" : L"Back to Files");
        px += 214.0f;
        drawGroupSeparatorAndTitle(pg4, px, pl ? L"Windows" : L"Windows");
        return;
    }

    if (m_activeRibbonTab == RibbonTab::Hardware) {
        // SPRZĘT (Hardware Pro Workstation) Ribbon Tab
        auto drawPill = [&](int cmdId, float x, float y, float w, const wchar_t* label, bool active = false) {
            D2D1_RECT_F pr = D2D1::RectF(x, y + 1.0f, x + w, y + 21.0f);
            m_ribbonHits.push_back({ cmdId, pr });
            m_solidBrush->SetColor(active ? HexColor(0x0078D7) : (m_hoverRibbonBtn == cmdId ? HexColor(0xDCEBFC) : HexColor(0xE8EEF4)));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(pr, 3.0f, 3.0f), m_solidBrush);
            m_solidBrush->SetColor(active ? HexColor(0x005A9E) : HexColor(0xB8C4D0));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(pr, 3.0f, 3.0f), m_solidBrush, 1.0f);
            m_solidBrush->SetColor(active ? HexColor(0xFFFFFF) : HexColor(0x1E293B));
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), m_fmtSmall, pr, m_solidBrush);
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        };

        float hx = rect.left + 8.0f;
        // Group 1: Sekcje sprzętowe (Sub-tabs)
        float g1 = hx;
        drawPill(CMD_HW_SUB_ALL,    hx,          body.top + 5.0f,  92.0f, m_lang == Lang::PL ? L"\x25C9 Wszystko PRO" : L"\x25C9 All PRO", m_hwSubTab == HardwareSubTab::All);
        drawPill(CMD_HW_SUB_CPU,    hx + 96.0f,  body.top + 5.0f,  86.0f, L"CPU & Cache", m_hwSubTab == HardwareSubTab::Cpu);
        drawPill(CMD_HW_SUB_GPU,    hx + 186.0f, body.top + 5.0f,  96.0f, L"GPU & Monitory", m_hwSubTab == HardwareSubTab::Gpu);
        drawPill(CMD_HW_SUB_RAM,    hx,          body.top + 29.0f, 92.0f, L"RAM (SMBIOS)", m_hwSubTab == HardwareSubTab::Ram);
        drawPill(CMD_HW_SUB_MOBO,   hx + 96.0f,  body.top + 29.0f, 86.0f, m_lang == Lang::PL ? L"Płyta & BIOS" : L"Board & BIOS", m_hwSubTab == HardwareSubTab::Motherboard);
        drawPill(CMD_HW_SUB_DISKS,  hx + 186.0f, body.top + 29.0f, 96.0f, m_lang == Lang::PL ? L"Dyski NVMe/SMART" : L"Storage/NVMe", m_hwSubTab == HardwareSubTab::Disks);
        drawPill(CMD_HW_SUB_NET,    hx,          body.top + 53.0f, 92.0f, m_lang == Lang::PL ? L"Sieć & NIC" : L"Network & NIC", m_hwSubTab == HardwareSubTab::Network);
        drawPill(CMD_HW_SUB_SYSTEM, hx + 96.0f,  body.top + 53.0f, 90.0f, m_lang == Lang::PL ? L"System & Audio" : L"OS & Audio", m_hwSubTab == HardwareSubTab::System);
        drawPill(CMD_HW_SUB_THERMAL, hx + 190.0f, body.top + 53.0f, 92.0f, m_lang == Lang::PL ? L"\xD83C\xDF21 Temperatury" : L"\xD83C\xDF21 Temperatures", m_hwSubTab == HardwareSubTab::Thermal);
        hx += 288.0f;
        drawGroupSeparatorAndTitle(g1, hx, m_lang == Lang::PL ? L"Moduły diagnostyczne PRO" : L"Diagnostic Modules");
        hx += 8.0f;

        // Group 2: Telemetria i odświeżanie
        float g2 = hx;
        drawLargeButton(CMD_HW_REFRESH, hx, 74.0f, m_lang == Lang::PL ? L"Odśwież\nsprzęt" : L"Rescan\nHardware", m_viewMode == ViewMode::Hardware);
        hx += 76.0f;
        drawPill(CMD_HW_TOGGLE_LIVE,  hx, body.top + 8.0f,  118.0f, !m_hwLiveRefresh ? (m_lang == Lang::PL ? L"\x25B6 Wznów wykresy" : L"\x25B6 Resume Live") : (m_lang == Lang::PL ? L"\x23F8 Pauza wykresów" : L"\x23F8 Pause Live"), !m_hwLiveRefresh);
        drawPill(CMD_HW_BACK_TO_FILES,hx, body.top + 38.0f, 118.0f, m_lang == Lang::PL ? L"\xD83D\xDCC1 Wróć do plików" : L"\xD83D\xDCC1 Back to Files", m_viewMode != ViewMode::Hardware);
        hx += 124.0f;
        drawGroupSeparatorAndTitle(g2, hx, m_lang == Lang::PL ? L"Telemetria Live" : L"Live Telemetry");
        hx += 8.0f;

        // Group 3: Benchmarki PRO (CPU & RAM)
        float g3 = hx;
        drawLargeButton(CMD_HW_BENCH_CPU, hx, 82.0f, m_lang == Lang::PL ? L"Benchmark\nCPU (GOPS)" : L"CPU Bench\n(GOPS)", m_hwInspector.GetCpu().benchMultiCoreGops > 0.0);
        hx += 84.0f;
        drawLargeButton(CMD_HW_BENCH_RAM, hx, 82.0f, m_lang == Lang::PL ? L"Benchmark\nRAM (GB/s)" : L"RAM Bench\n(GB/s)", m_hwInspector.GetRam().benchReadGBps > 0.0);
        hx += 88.0f;
        drawGroupSeparatorAndTitle(g3, hx, m_lang == Lang::PL ? L"Testy wydajności" : L"Benchmarks");
        hx += 8.0f;

        // Group 4: Eksport i Narzędzia systemowe
        float g4 = hx;
        drawPill(CMD_HW_COPY_REPORT, hx,          body.top + 6.0f,  116.0f, m_lang == Lang::PL ? L"\xD83D\xDCCB Kopiuj raport" : L"\xD83D\xDCCB Copy Report", false);
        drawPill(CMD_HW_SAVE_REPORT, hx,          body.top + 30.0f, 116.0f, m_lang == Lang::PL ? L"\xD83D\xDCBE Zapisz .TXT" : L"\xD83D\xDCBE Save .TXT", false);
        drawPill(CMD_HW_OPEN_DXDIAG, hx,          body.top + 53.0f, 116.0f, L"DirectX (dxdiag)", false);
        drawPill(CMD_HW_OPEN_DEVMGR, hx + 120.0f, body.top + 6.0f,  128.0f, m_lang == Lang::PL ? L"Menedżer urządzeń" : L"Device Manager", false);
        drawPill(CMD_HW_OPEN_TASKMGR,hx + 120.0f, body.top + 30.0f, 128.0f, m_lang == Lang::PL ? L"Menedżer zadań" : L"Task Manager", false);
        drawPill(CMD_HW_OPEN_MSINFO, hx + 120.0f, body.top + 53.0f, 128.0f, L"System Info (msinfo32)", false);
        hx += 254.0f;
        drawGroupSeparatorAndTitle(g4, hx, m_lang == Lang::PL ? L"Raport i Narzędzia Windows" : L"Report & System Tools");
        return;
    }

    // NARZĘDZIA GŁÓWNE (Home) Ribbon Tab — 1:1 with user's screenshot!
    float x = rect.left + 6.0f;

    // Group 1: Schowek (Clipboard)
    float g1Start = x;
    drawLargeButton(CMD_PIN_QUICK, x, 92.0f, s.btnPinQuick); x += 94.0f;
    drawLargeButton(CMD_COPY,      x, 48.0f, s.btnCopy);     x += 50.0f;
    drawLargeButton(CMD_PASTE,     x, 48.0f, s.btnPaste);    x += 52.0f;
    drawSmallRowButton(CMD_CUT,            x, body.top + 6.0f,  104.0f, s.btnCut);
    drawSmallRowButton(CMD_COPY_PATH,      x, body.top + 29.0f, 104.0f, s.btnCopyPath);
    drawSmallRowButton(CMD_PASTE_SHORTCUT, x, body.top + 52.0f, 104.0f, s.btnPasteShortcut);
    x += 108.0f;
    drawGroupSeparatorAndTitle(g1Start, x, s.grpClipboard);
    x += 6.0f;

    // Group 2: Organizowanie (Organize: Move to, Copy to, Split Delete "Usuń v", Rename)
    float g2Start = x;
    drawLargeButton(CMD_MOVE_TO, x, 54.0f, s.btnMoveTo); x += 56.0f;
    drawLargeButton(CMD_COPY_TO, x, 54.0f, s.btnCopyTo); x += 56.0f;

    // "Kosz": moves the selection to the Recycle Bin
    drawLargeButton(CMD_DELETE_MAIN, x, 46.0f, m_lang == Lang::PL ? L"Kosz" : L"Recycle\nBin"); x += 48.0f;

    // Split button "Usuń ▾": top = permanent delete, bottom arrow = mode menu (confirm toggle etc.)
    D2D1_RECT_F delTopR = D2D1::RectF(x, body.top + 4.0f, x + 52.0f, body.top + 44.0f);
    D2D1_RECT_F delBotR = D2D1::RectF(x, body.top + 44.0f, x + 52.0f, body.bottom - 17.0f);
    m_ribbonHits.push_back({ CMD_DELETE_PERM, delTopR });
    m_ribbonHits.push_back({ CMD_DELETE_DROPDOWN, delBotR });

    if (m_hoverRibbonBtn == CMD_DELETE_PERM || m_hoverRibbonBtn == CMD_DELETE_DROPDOWN) {
        D2D1_RECT_F fullDelR = D2D1::RectF(x, body.top + 4.0f, x + 52.0f, body.bottom - 17.0f);
        m_solidBrush->SetColor(HexColor(0xE1ECF4));
        m_rt->FillRectangle(fullDelR, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xA4C9E8));
        m_rt->DrawRectangle(fullDelR, m_solidBrush, 1.0f);
        m_rt->DrawLine(D2D1::Point2F(delBotR.left + 4.0f, delBotR.top), D2D1::Point2F(delBotR.right - 4.0f, delBotR.top), m_solidBrush, 1.0f);
    }
    DrawRibbonIcon(CMD_DELETE_PERM, D2D1::RectF(x + 11.0f, body.top + 8.0f, x + 41.0f, body.top + 38.0f));
    std::wstring delLabel = std::wstring(s.btnDelete) + L"\n\x25BE";
    m_solidBrush->SetColor(HexColor(0x1E1E1E));
    m_rt->DrawTextW(delLabel.c_str(), static_cast<UINT32>(delLabel.size()), m_fmtCenterSmall, delBotR, m_solidBrush);
    x += 54.0f;

    drawLargeButton(CMD_RENAME, x, 54.0f, s.btnRename); x += 58.0f;
    drawGroupSeparatorAndTitle(g2Start, x, s.grpOrganize);
    x += 6.0f;

    // Group 3: Nowy (New folder, New item, Easy access)
    float g3Start = x;
    drawLargeButton(CMD_NEW_FOLDER, x, 54.0f, s.btnNewFolder); x += 56.0f;
    drawSmallRowButton(CMD_NEW_ITEM, x, body.top + 10.0f, 106.0f, s.btnNewItem);
    drawSmallRowButton(CMD_NONE,     x, body.top + 36.0f, 106.0f, s.btnEasyAccess);
    x += 110.0f;
    drawGroupSeparatorAndTitle(g3Start, x, s.grpNew);
    x += 6.0f;

    // Group 4: Otwieranie (Properties, Open, Edit, History)
    float g4Start = x;
    drawLargeButton(CMD_PROPERTIES, x, 68.0f, s.btnProperties); x += 70.0f;
    drawSmallRowButton(CMD_OPEN, x, body.top + 6.0f,  78.0f, s.btnOpen);
    drawSmallRowButton(CMD_EDIT, x, body.top + 29.0f, 78.0f, s.btnEdit);
    drawSmallRowButton(CMD_NAV_RECENT_DIRS, x, body.top + 52.0f, 78.0f, s.btnHistory);
    x += 82.0f;
    drawGroupSeparatorAndTitle(g4Start, x, s.grpOpen);
    x += 6.0f;

    // Group 5: Zaznaczanie (Select all, Select none, Invert selection)
    float g5Start = x;
    drawSmallRowButton(CMD_SELECT_ALL,  x, body.top + 6.0f,  126.0f, s.btnSelectAll);
    drawSmallRowButton(CMD_SELECT_NONE, x, body.top + 29.0f, 126.0f, s.btnSelectNone);
    drawSmallRowButton(CMD_INVERT_SEL,  x, body.top + 52.0f, 126.0f, s.btnInvertSelection);
    x += 130.0f;
    drawGroupSeparatorAndTitle(g5Start, x, s.grpSelect);
    x += 6.0f;

    // Group 6: Narzędzia obrazu (Per-Image Resize, Downscale, Target File Size, Format)
    float g6Start = x;
    const bool pl = (m_lang == Lang::PL);
    drawLargeButton(CMD_IMG_TOOL_MENU, x, 76.0f, pl ? L"Narzędzia\nobrazu ▾" : L"Image\nTools ▾");
    x += 78.0f;
    drawSmallRowButton(CMD_IMG_SCALE_50,      x, body.top + 6.0f,  132.0f, pl ? L"Skaluj 50%" : L"Scale 50%");
    drawSmallRowButton(CMD_IMG_TARGET_CUSTOM, x, body.top + 29.0f, 132.0f, pl ? L"Do rozmiaru KB..." : L"To file size KB...");
    drawSmallRowButton(CMD_CTX_ROTATE_CW,     x, body.top + 52.0f, 132.0f, pl ? L"Obróć o 90°" : L"Rotate 90°");
    x += 136.0f;
    drawGroupSeparatorAndTitle(g6Start, x, pl ? L"Obrazy" : L"Images");
}

void ExplorerApp::DrawNavBar(const D2D1_RECT_F& rect) {
    const Strings& s = GetStrings(m_lang);

    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRectangle(rect, m_solidBrush);

    // Bottom subtle divider
    m_solidBrush->SetColor(HexColor(0xE5E5E5));
    m_rt->DrawLine(D2D1::Point2F(rect.left, rect.bottom - 1.0f), D2D1::Point2F(rect.right, rect.bottom - 1.0f), m_solidBrush, 1.0f);

    float cy = (rect.top + rect.bottom) * 0.5f;

    auto drawNavIconBtn = [&](int cmdId, float left, const wchar_t* glyph, bool enabled) {
        D2D1_RECT_F br = D2D1::RectF(left, rect.top + 5.0f, left + 28.0f, rect.bottom - 5.0f);
        m_ribbonHits.push_back({ cmdId, br });
        if (enabled && m_hoverRibbonBtn == cmdId) {
            m_solidBrush->SetColor(HexColor(0xE5F3FB));
            m_rt->FillRectangle(br, m_solidBrush);
        }
        m_solidBrush->SetColor(enabled ? HexColor(0x454545) : HexColor(0xBBBBBB));
        m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(glyph, static_cast<UINT32>(wcslen(glyph)), m_fmtRegular, br, m_solidBrush);
        m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    };

    drawNavIconBtn(CMD_NAV_BACK,    6.0f,  L"\x2190", m_historyIndex > 0);
    drawNavIconBtn(CMD_NAV_FORWARD, 35.0f, L"\x2192", m_historyIndex + 1 < static_cast<int>(m_history.size()));
    drawNavIconBtn(CMD_NAV_UP,      68.0f, L"\x2191", !m_currentPath.empty() && m_currentPath != L"THIS_PC");

    // Search bar on the right + Split View [◫] button + Right File Content Pane [◨] button + Advanced Gear [⚙] button
    float gearW = 28.0f;
    float previewBtnW = 30.0f;
    float splitBtnW = 30.0f;
    float searchW = std::min(255.0f, std::max(145.0f, (rect.right - rect.left) * 0.20f));
    D2D1_RECT_F gearBtnR    = D2D1::RectF(rect.right - gearW - 6.0f, rect.top + 5.0f, rect.right - 6.0f, rect.bottom - 5.0f);
    D2D1_RECT_F previewBtnR = D2D1::RectF(gearBtnR.left - previewBtnW - 4.0f, rect.top + 5.0f, gearBtnR.left - 4.0f, rect.bottom - 5.0f);
    D2D1_RECT_F splitBtnR   = D2D1::RectF(previewBtnR.left - splitBtnW - 4.0f, rect.top + 5.0f, previewBtnR.left - 4.0f, rect.bottom - 5.0f);
    m_searchBarRect = D2D1::RectF(splitBtnR.left - searchW - 4.0f, rect.top + 5.0f, splitBtnR.left - 4.0f, rect.bottom - 5.0f);

    // Address / Breadcrumb Bar between Up arrow and Recent Dirs [▾] / Refresh [↻] / Search bar
    float refreshBtnLeft = m_searchBarRect.left - 34.0f;
    float recentBtnLeft  = refreshBtnLeft - 26.0f;
    m_addressBarRect = D2D1::RectF(102.0f, rect.top + 5.0f, recentBtnLeft, rect.bottom - 5.0f);

    m_solidBrush->SetColor(HexColor(0xD9D9D9));
    m_rt->DrawRectangle(m_addressBarRect, m_solidBrush, 1.0f);

    // Folder icon inside address bar
    DrawFolderVectorIcon(D2D1::RectF(m_addressBarRect.left + 6.0f, cy - 8.0f, m_addressBarRect.left + 22.0f, cy + 8.0f), true);

    // Interactive Breadcrumb segments ("Ten komputer > WORK (W:) > Fooocus_Advanced")
    float bx = m_addressBarRect.left + 26.0f;
    for (int segIdx = 0; segIdx < static_cast<int>(m_breadcrumbs.size()); ++segIdx) {
        auto& seg = m_breadcrumbs[segIdx];
        float segW = static_cast<float>(seg.label.size()) * 7.2f + 14.0f;
        if (bx + segW > m_addressBarRect.right - 22.0f) {
            seg.hitRect = {};
            seg.arrowRect = {};
            break;
        }
        seg.hitRect = D2D1::RectF(bx, m_addressBarRect.top + 1.0f, bx + segW, m_addressBarRect.bottom - 1.0f);
        seg.arrowRect = D2D1::RectF(seg.hitRect.right, seg.hitRect.top, seg.hitRect.right + 16.0f, seg.hitRect.bottom);

        if (segIdx == m_hoverBreadcrumbIndex || segIdx == m_hoverBreadcrumbArrowIndex) {
            D2D1_RECT_F fullSegR = D2D1::RectF(seg.hitRect.left, seg.hitRect.top, seg.arrowRect.right, seg.hitRect.bottom);
            m_solidBrush->SetColor(HexColor(0xE5F3FB));
            m_rt->FillRectangle(fullSegR, m_solidBrush);
            m_solidBrush->SetColor(HexColor(0xA8D6F0));
            m_rt->DrawRectangle(fullSegR, m_solidBrush, 1.0f);
            m_rt->DrawLine(D2D1::Point2F(seg.arrowRect.left, seg.arrowRect.top), D2D1::Point2F(seg.arrowRect.left, seg.arrowRect.bottom), m_solidBrush, 1.0f);
        }

        if (segIdx == m_hoverBreadcrumbArrowIndex) {
            m_solidBrush->SetColor(HexColor(0xCCE8FF));
            m_rt->FillRectangle(seg.arrowRect, m_solidBrush);
        }

        m_solidBrush->SetColor(HexColor(0x1E1E1E));
        D2D1_RECT_F textR = D2D1::RectF(seg.hitRect.left + 5.0f, seg.hitRect.top, seg.hitRect.right - 2.0f, seg.hitRect.bottom);
        m_rt->DrawTextW(seg.label.c_str(), static_cast<UINT32>(seg.label.size()), m_fmtRegular, textR, m_solidBrush);

        m_solidBrush->SetColor(segIdx == m_hoverBreadcrumbArrowIndex ? HexColor(0x0067B8) : HexColor(0x666666));
        m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(L"\x203A", 1, m_fmtRegular, seg.arrowRect, m_solidBrush);
        m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        bx = seg.arrowRect.right;
    }

    // Recent Visited Directories dropdown button [▾] attached between address bar and refresh button
    D2D1_RECT_F recentR = D2D1::RectF(recentBtnLeft, m_addressBarRect.top, refreshBtnLeft, m_addressBarRect.bottom);
    m_ribbonHits.push_back({ CMD_NAV_RECENT_DIRS, recentR });
    bool hovRecent = (m_hoverRibbonBtn == CMD_NAV_RECENT_DIRS);
    if (hovRecent) {
        m_solidBrush->SetColor(HexColor(0xE5F3FB));
        m_rt->FillRectangle(recentR, m_solidBrush);
    }
    m_solidBrush->SetColor(hovRecent ? HexColor(0x0078D7) : HexColor(0xD9D9D9));
    m_rt->DrawRectangle(recentR, m_solidBrush, 1.0f);
    // Crisp downward chevron inside recentR
    {
        float rcx = (recentR.left + recentR.right) * 0.5f;
        float rcy = (recentR.top + recentR.bottom) * 0.5f;
        m_solidBrush->SetColor(hovRecent ? HexColor(0x0067B8) : HexColor(0x454545));
        m_rt->DrawLine(D2D1::Point2F(rcx - 4.5f, rcy - 2.0f), D2D1::Point2F(rcx, rcy + 2.5f), m_solidBrush, 1.7f);
        m_rt->DrawLine(D2D1::Point2F(rcx, rcy + 2.5f), D2D1::Point2F(rcx + 4.5f, rcy - 2.0f), m_solidBrush, 1.7f);
    }

    // Refresh button attached to right edge of address bar
    D2D1_RECT_F refreshR = D2D1::RectF(refreshBtnLeft, m_addressBarRect.top, refreshBtnLeft + 26.0f, m_addressBarRect.bottom);
    m_ribbonHits.push_back({ CMD_NAV_REFRESH, refreshR });
    if (m_hoverRibbonBtn == CMD_NAV_REFRESH) {
        m_solidBrush->SetColor(HexColor(0xE5F3FB));
        m_rt->FillRectangle(refreshR, m_solidBrush);
    }
    m_solidBrush->SetColor(HexColor(0xD9D9D9));
    m_rt->DrawRectangle(refreshR, m_solidBrush, 1.0f);
    m_solidBrush->SetColor(HexColor(0x454545));
    m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_rt->DrawTextW(L"\x21BB", 1, m_fmtRegular, refreshR, m_solidBrush);
    m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // Search box
    m_solidBrush->SetColor(m_isSearchActive ? HexColor(0x0078D7) : HexColor(0xD9D9D9));
    m_rt->DrawRectangle(m_searchBarRect, m_solidBrush, m_isSearchActive ? 1.4f : 1.0f);
    if (!IsWindowVisible(m_searchEdit)) {
        std::wstring folderShort = m_currentPath;
        const wchar_t* base = PathFindFileNameW(m_currentPath.c_str());
        if (base && *base) folderShort = base;
        std::wstring ph = m_filterText.empty() ? (std::wstring(s.searchPlaceholder) + folderShort) : m_filterText;
        m_solidBrush->SetColor(m_filterText.empty() ? HexColor(0x767676) : HexColor(0x1E1E1E));
        D2D1_RECT_F sr = D2D1::RectF(m_searchBarRect.left + 8.0f, m_searchBarRect.top, m_searchBarRect.right - 42.0f, m_searchBarRect.bottom);
        m_rt->DrawTextW(ph.c_str(), static_cast<UINT32>(ph.size()), m_fmtRegular, sr, m_solidBrush);
    }
    if (m_isSearchActive || !m_filterText.empty()) {
        D2D1_RECT_F clrR = D2D1::RectF(m_searchBarRect.right - 40.0f, m_searchBarRect.top + 2.0f, m_searchBarRect.right - 22.0f, m_searchBarRect.bottom - 2.0f);
        m_ribbonHits.push_back({ CMD_CLEAR_SEARCH, clrR });
        if (m_hoverRibbonBtn == CMD_CLEAR_SEARCH) {
            m_solidBrush->SetColor(HexColor(0xFDE7E9));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(clrR, 2.0f, 2.0f), m_solidBrush);
        }
        m_solidBrush->SetColor(HexColor(0xC42B1C));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(L"\x2715", 1, m_fmtSmall, clrR, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    D2D1_RECT_F magR = D2D1::RectF(m_searchBarRect.right - 21.0f, m_searchBarRect.top + 1.0f, m_searchBarRect.right - 1.0f, m_searchBarRect.bottom - 1.0f);
    m_ribbonHits.push_back({ CMD_SEARCH_TRIGGER_BTN, magR });
    if (m_hoverRibbonBtn == CMD_SEARCH_TRIGGER_BTN) {
        m_solidBrush->SetColor(HexColor(0xE5F3FB));
        m_rt->FillRectangle(magR, m_solidBrush);
    }
    m_solidBrush->SetColor(m_isSearchActive ? HexColor(0x0067B8) : HexColor(0x767676));
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_rt->DrawTextW(L"\xD83D\xDD0D", 2, m_fmtSmall, magR, m_solidBrush);

    // Quick Split View [◫] Toggle Button right next to Search Box (toggles 2-pane Explorer from any tab!)
    m_ribbonHits.push_back({ CMD_TOGGLE_SPLIT_VIEW, splitBtnR });
    bool splitHov = (m_hoverRibbonBtn == CMD_TOGGLE_SPLIT_VIEW);
    m_solidBrush->SetColor(m_splitView ? HexColor(0xCCE8FF) : (splitHov ? HexColor(0xE5F3FB) : HexColor(0xF5F6F7)));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(splitBtnR, 2.5f, 2.5f), m_solidBrush);
    m_solidBrush->SetColor(m_splitView ? HexColor(0x0078D7) : HexColor(0xCCCCCC));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(splitBtnR, 2.5f, 2.5f), m_solidBrush, 1.0f);
    {
        D2D1_RECT_F ic = D2D1::RectF(splitBtnR.left + 6.0f, splitBtnR.top + 6.0f, splitBtnR.right - 6.0f, splitBtnR.bottom - 6.0f);
        float icMid = (ic.left + ic.right) * 0.5f;
        m_solidBrush->SetColor(m_splitView ? HexColor(0x005A9E) : HexColor(0x333333));
        m_rt->DrawRectangle(ic, m_solidBrush, 1.3f);
        m_rt->DrawLine(D2D1::Point2F(icMid, ic.top), D2D1::Point2F(icMid, ic.bottom), m_solidBrush, 1.3f);
    }

    // Right-Side File Content Pane [◨] Toggle Button right next to Dual-Pane [◫] (Notepad / Hex / 3D / Media Inspector!)
    m_ribbonHits.push_back({ CMD_TOGGLE_PREVIEWPANE, previewBtnR });
    bool prevHov = (m_hoverRibbonBtn == CMD_TOGGLE_PREVIEWPANE);
    m_solidBrush->SetColor(m_showPreviewPane ? HexColor(0xCCE8FF) : (prevHov ? HexColor(0xE5F3FB) : HexColor(0xF5F6F7)));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(previewBtnR, 2.5f, 2.5f), m_solidBrush);
    m_solidBrush->SetColor(m_showPreviewPane ? HexColor(0x0078D7) : HexColor(0xCCCCCC));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(previewBtnR, 2.5f, 2.5f), m_solidBrush, 1.0f);
    {
        D2D1_RECT_F ic = D2D1::RectF(previewBtnR.left + 6.0f, previewBtnR.top + 6.0f, previewBtnR.right - 6.0f, previewBtnR.bottom - 6.0f);
        float icSplit = ic.left + (ic.right - ic.left) * 0.42f;
        m_solidBrush->SetColor(m_showPreviewPane ? HexColor(0x93C5FD, 0.55f) : HexColor(0xE2E8F0));
        m_rt->FillRectangle(D2D1::RectF(icSplit, ic.top + 0.5f, ic.right - 0.5f, ic.bottom - 0.5f), m_solidBrush);
        m_solidBrush->SetColor(m_showPreviewPane ? HexColor(0x005A9E) : HexColor(0x333333));
        m_rt->DrawRectangle(ic, m_solidBrush, 1.3f);
        m_rt->DrawLine(D2D1::Point2F(icSplit, ic.top), D2D1::Point2F(icSplit, ic.bottom), m_solidBrush, 1.2f);
        for (int ln = 0; ln < 3; ++ln) {
            float ly = ic.top + 3.2f + ln * 3.4f;
            if (ly < ic.bottom - 1.5f) {
                m_rt->DrawLine(D2D1::Point2F(icSplit + 2.2f, ly), D2D1::Point2F(ic.right - 2.0f, ly), m_solidBrush, 1.1f);
            }
        }
    }

    // Advanced Search Gear [⚙] Button right next to Search Box (opens "Wyszukiwanie" tab!)
    m_ribbonHits.push_back({ CMD_SEARCH_ADVANCED_GEAR, gearBtnR });
    bool gearActive = (m_activeRibbonTab == RibbonTab::Search && !m_ribbonCollapsed) || m_searchOpts.HasAnyActiveFilter();
    m_solidBrush->SetColor(gearActive ? HexColor(0xCCE8FF) : (m_hoverRibbonBtn == CMD_SEARCH_ADVANCED_GEAR ? HexColor(0xE5F3FB) : HexColor(0xF5F6F7)));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(gearBtnR, 2.5f, 2.5f), m_solidBrush);
    m_solidBrush->SetColor(gearActive ? HexColor(0x0078D7) : HexColor(0xCCCCCC));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(gearBtnR, 2.5f, 2.5f), m_solidBrush, 1.0f);
    m_solidBrush->SetColor(gearActive ? HexColor(0x005A9E) : HexColor(0x333333));
    m_rt->DrawTextW(L"\x2699", 1, m_fmtRegular, gearBtnR, m_solidBrush);
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}

void ExplorerApp::DrawSplitPaneHeader(int paneIdx, const D2D1_RECT_F& headerRect, bool isActivePane) {
    if (headerRect.right <= headerRect.left + 20.0f || headerRect.bottom <= headerRect.top + 4.0f) return;
    const Strings& s = GetStrings(m_lang);
    const bool pl = (m_lang == Lang::PL);

    m_rt->PushAxisAlignedClip(headerRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Header background + top active indicator stripe
    m_solidBrush->SetColor(isActivePane ? HexColor(0xEFF6FF) : HexColor(0xF8FAFC));
    m_rt->FillRectangle(headerRect, m_solidBrush);
    m_solidBrush->SetColor(isActivePane ? HexColor(0x0078D7) : HexColor(0xCBD5E1));
    m_rt->FillRectangle(D2D1::RectF(headerRect.left, headerRect.top, headerRect.right, headerRect.top + 2.5f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xD1D5DB));
    m_rt->DrawLine(D2D1::Point2F(headerRect.left, headerRect.bottom - 0.5f), D2D1::Point2F(headerRect.right, headerRect.bottom - 0.5f), m_solidBrush, 1.0f);

    const int cmdBack    = (paneIdx == 0) ? CMD_PANE0_BACK        : CMD_PANE1_BACK;
    const int cmdFwd     = (paneIdx == 0) ? CMD_PANE0_FWD         : CMD_PANE1_FWD;
    const int cmdUp      = (paneIdx == 0) ? CMD_PANE0_UP          : CMD_PANE1_UP;
    const int cmdRefresh = (paneIdx == 0) ? CMD_PANE0_REFRESH     : CMD_PANE1_REFRESH;
    const int cmdRecent  = (paneIdx == 0) ? CMD_PANE0_RECENT_DIRS : CMD_PANE1_RECENT_DIRS;
    const int cmdAddr    = (paneIdx == 0) ? CMD_PANE0_ADDR        : CMD_PANE1_ADDR;
    const int cmdCopyOth = (paneIdx == 0) ? CMD_PANE0_COPY_OTHER  : CMD_PANE1_COPY_OTHER;
    const int cmdMoveOth = (paneIdx == 0) ? CMD_PANE0_MOVE_OTHER  : CMD_PANE1_MOVE_OTHER;

    float btnTop = headerRect.top + 4.5f;
    float btnBot = headerRect.bottom - 3.5f;
    auto drawMiniNavBtn = [&](int cmdId, float left, float w, const wchar_t* label, bool enabled) {
        D2D1_RECT_F br = D2D1::RectF(left, btnTop, left + w, btnBot);
        m_ribbonHits.push_back({ cmdId, br });
        bool hov = enabled && (m_hoverRibbonBtn == cmdId);
        if (hov) {
            m_solidBrush->SetColor(HexColor(0xDBEAFE));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(br, 2.5f, 2.5f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(0x93C5FD));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(br, 2.5f, 2.5f), m_solidBrush, 1.0f);
        }
        m_solidBrush->SetColor(enabled ? (hov ? HexColor(0x1D4ED8) : HexColor(0x334155)) : HexColor(0xCBD5E1));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), m_fmtSmall, br, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    };

    float x = headerRect.left + 4.0f;
    drawMiniNavBtn(cmdBack, x, 22.0f, L"\x2190", m_historyIndex > 0); x += 23.0f;
    drawMiniNavBtn(cmdFwd,  x, 22.0f, L"\x2192", m_historyIndex + 1 < static_cast<int>(m_history.size())); x += 23.0f;
    drawMiniNavBtn(cmdUp,   x, 22.0f, L"\x2191", !m_currentPath.empty() && m_currentPath != L"THIS_PC"); x += 25.0f;

    // Right-side action buttons: [⇄ Kopiuj], [→ Przenieś], [↻]
    float rx = headerRect.right - 4.0f;
    bool hasSpaceForTransfer = (headerRect.right - headerRect.left) >= 340.0f;
    if (hasSpaceForTransfer) {
        const wchar_t* mvLbl = (paneIdx == 0) ? (pl ? L"\x2192 Przenieś" : L"\x2192 Move") : (pl ? L"\x2190 Przenieś" : L"\x2190 Move");
        const wchar_t* cpLbl = (paneIdx == 0) ? (pl ? L"\x2192 Kopiuj" : L"\x2192 Copy")   : (pl ? L"\x2190 Kopiuj" : L"\x2190 Copy");
        float mvW = 68.0f;
        float cpW = 58.0f;
        rx -= mvW;
        drawMiniNavBtn(cmdMoveOth, rx, mvW, mvLbl, !m_currentPath.empty() && m_currentPath != L"THIS_PC");
        rx -= (cpW + 3.0f);
        drawMiniNavBtn(cmdCopyOth, rx, cpW, cpLbl, !m_currentPath.empty() && m_currentPath != L"THIS_PC");
        rx -= 3.0f;
    }
    rx -= 24.0f;
    drawMiniNavBtn(cmdRefresh, rx, 24.0f, L"\x21BB", true);
    rx -= 25.0f;
    drawMiniNavBtn(cmdRecent, rx, 22.0f, L"\x25BE", true);
    rx -= 4.0f;

    // Clickable per-pane Address Box
    D2D1_RECT_F addrR = D2D1::RectF(x, btnTop, std::max(x + 40.0f, rx), btnBot);
    m_paneHeaderAddressRect[paneIdx] = addrR;
    m_ribbonHits.push_back({ cmdAddr, addrR });

    bool addrHov = (m_hoverRibbonBtn == cmdAddr);
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(addrR, 3.0f, 3.0f), m_solidBrush);
    m_solidBrush->SetColor(isActivePane ? HexColor(0x3B82F6) : (addrHov ? HexColor(0x60A5FA) : HexColor(0xCBD5E1)));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(addrR, 3.0f, 3.0f), m_solidBrush, isActivePane ? 1.3f : 1.0f);

    float cy = (addrR.top + addrR.bottom) * 0.5f;
    DrawFolderVectorIcon(D2D1::RectF(addrR.left + 5.0f, cy - 7.0f, addrR.left + 19.0f, cy + 7.0f), true);

    // Item count badge inside right edge of the pane's address box
    std::wstring cntTxt = std::to_wstring(m_items.size()) + (pl ? L" el." : L" it.");
    float cntW = static_cast<float>(cntTxt.size()) * 6.0f + 8.0f;
    if (addrR.right - addrR.left > 140.0f) {
        D2D1_RECT_F cntR = D2D1::RectF(addrR.right - cntW - 4.0f, addrR.top + 1.0f, addrR.right - 4.0f, addrR.bottom - 1.0f);
        m_solidBrush->SetColor(HexColor(0x64748B));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        m_rt->DrawTextW(cntTxt.c_str(), static_cast<UINT32>(cntTxt.size()), m_fmtSmall, cntR, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    } else {
        cntW = 0.0f;
    }

    std::wstring dispPath = (m_currentPath.empty() || m_currentPath == L"THIS_PC") ? std::wstring(s.navThisPC) : m_currentPath;
    if (m_isSearchActive) {
        dispPath = (pl ? L"[Wyniki wyszukiwania] " : L"[Search results] ") + dispPath;
    }
    D2D1_RECT_F pathTxtR = D2D1::RectF(addrR.left + 23.0f, addrR.top + 2.0f, std::max(addrR.left + 28.0f, addrR.right - cntW - 8.0f), addrR.bottom - 1.0f);
    DrawClippedText(dispPath, pathTxtR, isActivePane ? m_fmtBold : m_fmtSmall, isActivePane ? 0x0F172A : 0x334155);

    m_rt->PopAxisAlignedClip();
}

void ExplorerApp::DrawTreePane(const D2D1_RECT_F& rect) {
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRectangle(rect, m_solidBrush);

    // Right vertical splitter line
    m_solidBrush->SetColor(HexColor(0xE5E5E5));
    m_rt->DrawLine(D2D1::Point2F(rect.right - 1.0f, rect.top), D2D1::Point2F(rect.right - 1.0f, rect.bottom), m_solidBrush, 1.0f);

    m_rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);

    // Determine the single best active tree node (exact match or deepest visible ancestor)
    int activeTreeIdx = -1;
    size_t bestScore = 0;
    if (!m_currentNormKey.empty()) {
        for (int i = 0; i < static_cast<int>(m_treeNodes.size()); ++i) {
            const std::wstring& nk = m_treeNodes[i].normKey;
            if (nk.empty()) continue;
            if (m_currentNormKey == nk) {
                size_t score = nk.size() + 10000 + (m_treeNodes[i].isQuickAccess ? 0 : 500);
                if (score > bestScore) {
                    bestScore = score;
                    activeTreeIdx = i;
                }
            } else if (m_currentNormKey.size() > nk.size() &&
                       m_currentNormKey.compare(0, nk.size(), nk) == 0 &&
                       m_currentNormKey[nk.size()] == L'\\') {
                size_t score = nk.size();
                if (score > bestScore) {
                    bestScore = score;
                    activeTreeIdx = i;
                }
            }
        }
    }

    float y = rect.top + 6.0f - m_treeScrollY;
    for (int i = 0; i < static_cast<int>(m_treeNodes.size()); ++i) {
        auto& node = m_treeNodes[i];
        D2D1_RECT_F rowR = D2D1::RectF(rect.left + 2.0f, y, rect.right - 3.0f, y + 25.0f);
        node.hitRect = rowR;

        float indentX = rect.left + 6.0f + node.indent * 14.0f;
        node.chevronRect = D2D1::RectF(indentX - 2.0f, y, indentX + 16.0f, y + 25.0f);

        if (y + 25.0f >= rect.top && y <= rect.bottom) {
            bool isActive = (i == activeTreeIdx);
            if (isActive) {
                m_solidBrush->SetColor(HexColor(0xCBE8F6));
                m_rt->FillRectangle(rowR, m_solidBrush);
            } else if (m_hoverTreeIndex == i) {
                m_solidBrush->SetColor(HexColor(0xE5F3FB));
                m_rt->FillRectangle(rowR, m_solidBrush);
            }

            // Expand/Collapse chevron ('▾' when expanded, '›' when collapsed)
            if (node.hasChildren) {
                bool chevHover = (m_hoverTreeIndex == i &&
                                  PathUtils::PtInRectF(static_cast<float>(m_mousePt.x), static_cast<float>(m_mousePt.y), node.chevronRect));
                m_solidBrush->SetColor(chevHover ? HexColor(0x0078D7) : (node.isExpanded ? HexColor(0x444444) : HexColor(0x888888)));
                m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                m_rt->DrawTextW(node.isExpanded ? L"\x25BE" : L"\x203A", 1, m_fmtSmall, node.chevronRect, m_solidBrush);
                m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }

            // Node icon
            D2D1_RECT_F iconR = D2D1::RectF(indentX + 15.0f, y + 4.0f, indentX + 31.0f, y + 20.0f);
            if (node.isQuickAccess && node.isHeader) {
                m_solidBrush->SetColor(HexColor(0x1E88E5));
                m_rt->DrawTextW(L"\x2605", 1, m_fmtRegular, iconR, m_solidBrush);
            } else if (node.path == L"THIS_PC") {
                m_solidBrush->SetColor(HexColor(0x2B6CB0));
                m_rt->DrawRectangle(D2D1::RectF(iconR.left + 1.0f, iconR.top + 2.0f, iconR.right - 1.0f, iconR.bottom - 4.0f), m_solidBrush, 1.5f);
            } else if (node.isDrive) {
                DrawDriveVectorIcon(iconR, node.isNetworkDrive, node.isOpticalDrive, node.isOffline);
            } else {
                DrawFolderVectorIcon(iconR, true);
            }

            D2D1_RECT_F textR = D2D1::RectF(indentX + 36.0f, y, rect.right - 4.0f, y + 25.0f);
            m_solidBrush->SetColor(node.isOffline ? HexColor(0x787878) : HexColor(0x1E1E1E));
            m_rt->DrawTextW(node.label.c_str(), static_cast<UINT32>(node.label.size()), m_fmtRegular, textR, m_solidBrush);
        }

        y += 26.0f;
    }

    m_rt->PopAxisAlignedClip();
}

void ExplorerApp::DrawMainContent(const D2D1_RECT_F& rect) {
    if (m_viewMode == ViewMode::FullViewer) {
        DrawFullViewerCanvas(rect, true);
        return;
    }
    if (m_viewMode == ViewMode::Hardware) {
        m_ScrollbarThumbRect = {};
        DrawHardwareProView(rect);
        return;
    }

    D2D1_RECT_F viewRect = rect;
    if (m_isSearchActive) {
        // Draw Live Search Progress & Results Banner (28px height)
        D2D1_RECT_F sBar = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + 28.0f);
        viewRect.top += 28.0f;

        bool running = m_searchProgress.isRunning;
        m_solidBrush->SetColor(running ? HexColor(0xFFF8E7) : HexColor(0xEBF5FF));
        m_rt->FillRectangle(sBar, m_solidBrush);
        m_solidBrush->SetColor(running ? HexColor(0xFFB300) : HexColor(0x99D1FF));
        m_rt->DrawLine(D2D1::Point2F(sBar.left, sBar.bottom - 1.0f), D2D1::Point2F(sBar.right, sBar.bottom - 1.0f), m_solidBrush, 1.5f);

        if (running) {
            // Animated moving progress pulse bar along bottom edge of sBar
            float barW = sBar.right - sBar.left;
            float phase = static_cast<float>((GetTickCount64() % 1200ULL)) / 1200.0f;
            float segW = std::max(80.0f, barW * 0.28f);
            float sx0 = sBar.left + (barW + segW) * phase - segW;
            D2D1_RECT_F pulseR = D2D1::RectF(std::max(sBar.left, sx0), sBar.bottom - 3.5f, std::min(sBar.right, sx0 + segW), sBar.bottom);
            if (pulseR.right > pulseR.left) {
                m_solidBrush->SetColor(HexColor(0xFF8F00));
                m_rt->FillRectangle(pulseR, m_solidBrush);
            }

            // Rotating vector spinner on the left
            float cx = sBar.left + 16.0f;
            float cy = (sBar.top + sBar.bottom) * 0.5f;
            float angle = static_cast<float>((GetTickCount64() % 900ULL)) * (6.2831853f / 900.0f);
            for (int k = 0; k < 8; ++k) {
                float a = angle + k * (6.2831853f / 8.0f);
                float rIn = 3.5f, rOut = 7.5f;
                m_solidBrush->SetColor(HexColor(0xE65100, 0.2f + 0.8f * (static_cast<float>(k) / 7.0f)));
                m_rt->DrawLine(
                    D2D1::Point2F(cx + std::cos(a) * rIn, cy + std::sin(a) * rIn),
                    D2D1::Point2F(cx + std::cos(a) * rOut, cy + std::sin(a) * rOut),
                    m_solidBrush, 1.8f);
            }
        }

        wchar_t timeBuf[64] = {};
        swprintf_s(timeBuf, L"%.2f s", static_cast<double>(m_searchProgress.elapsedMs) / 1000.0);

        std::wstring qDisp = m_filterText.empty() ? (m_lang == Lang::PL ? L"[Filtry]" : L"[Filters]") : (L"\"" + m_filterText + L"\"");
        std::wstring msg;
        if (running) {
            std::wstring curShort = PathUtils::GetFileName(m_searchProgress.currentFolder);
            if (curShort.empty()) curShort = m_searchProgress.currentFolder;
            msg = (m_lang == Lang::PL ? L"Trwa wyszukiwanie " : L"Searching ") + qDisp +
                  (m_lang == Lang::PL ? L"   \x2022   Znaleziono: " : L"   \x2022   Found: ") + std::to_wstring(m_items.size()) +
                  (m_lang == Lang::PL ? L"   \x2022   Przeskanowano: " : L"   \x2022   Scanned: ") +
                  std::to_wstring(m_searchProgress.scannedFolders) + (m_lang == Lang::PL ? L" fold., " : L" dirs, ") +
                  std::to_wstring(m_searchProgress.scannedFiles) + (m_lang == Lang::PL ? L" plik. (" : L" files (") + timeBuf + L")" +
                  (m_lang == Lang::PL ? L"   \x2022   Teraz: " : L"   \x2022   Now: ") + curShort;
        } else {
            msg = (m_lang == Lang::PL ? L"\x2713 Wyniki wyszukiwania " : L"\x2713 Search results for ") + qDisp +
                  (m_lang == Lang::PL ? L":  znaleziono " : L":  found ") + std::to_wstring(m_items.size()) +
                  (m_lang == Lang::PL ? L" elem.   \x2022   Przeskanowano " : L" items   \x2022   Scanned ") +
                  std::to_wstring(m_searchProgress.scannedFolders) + (m_lang == Lang::PL ? L" folderów i " : L" folders & ") +
                  std::to_wstring(m_searchProgress.scannedFiles) + (m_lang == Lang::PL ? L" plików w " : L" files in ") + timeBuf;
        }

        D2D1_RECT_F txtR = D2D1::RectF(sBar.left + (running ? 32.0f : 10.0f), sBar.top, sBar.right - 245.0f, sBar.bottom);
        m_solidBrush->SetColor(running ? HexColor(0x8D3B00) : HexColor(0x004578));
        m_rt->DrawTextW(msg.c_str(), static_cast<UINT32>(msg.size()), m_fmtBold, txtR, m_solidBrush);

        // Action buttons on right side of Search Banner: [⚙ Zaawansowane] and [⏹ Stop / ✕ Zamknij]
        D2D1_RECT_F advBtnR = D2D1::RectF(sBar.right - 238.0f, sBar.top + 3.0f, sBar.right - 122.0f, sBar.bottom - 4.0f);
        m_ribbonHits.push_back({ CMD_SEARCH_ADVANCED_GEAR, advBtnR });
        m_solidBrush->SetColor(m_hoverRibbonBtn == CMD_SEARCH_ADVANCED_GEAR ? HexColor(0x0078D7) : HexColor(0xFFFFFF));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(advBtnR, 3.0f, 3.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x0078D7));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(advBtnR, 3.0f, 3.0f), m_solidBrush, 1.0f);
        m_solidBrush->SetColor(m_hoverRibbonBtn == CMD_SEARCH_ADVANCED_GEAR ? HexColor(0xFFFFFF) : HexColor(0x005A9E));
        const wchar_t* advLbl = m_lang == Lang::PL ? L"\x2699 Filtry / Opcje" : L"\x2699 Filters / Gear";
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(advLbl, static_cast<UINT32>(wcslen(advLbl)), m_fmtSmall, advBtnR, m_solidBrush);

        int rightCmd = running ? CMD_SEARCH_STOP : CMD_CLEAR_SEARCH;
        D2D1_RECT_F closeBtnR = D2D1::RectF(sBar.right - 116.0f, sBar.top + 3.0f, sBar.right - 8.0f, sBar.bottom - 4.0f);
        m_ribbonHits.push_back({ rightCmd, closeBtnR });
        m_solidBrush->SetColor(m_hoverRibbonBtn == rightCmd ? HexColor(0xC42B1C) : HexColor(0xFFFFFF));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(closeBtnR, 3.0f, 3.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xC42B1C));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(closeBtnR, 3.0f, 3.0f), m_solidBrush, 1.0f);
        m_solidBrush->SetColor(m_hoverRibbonBtn == rightCmd ? HexColor(0xFFFFFF) : HexColor(0xA80000));
        const wchar_t* clsLbl = running
            ? (m_lang == Lang::PL ? L"\x23F9 Zatrzymaj" : L"\x23F9 Stop")
            : (m_lang == Lang::PL ? L"\x2715 Zamknij wyniki" : L"\x2715 Close search");
        m_rt->DrawTextW(clsLbl, static_cast<UINT32>(wcslen(clsLbl)), m_fmtSmall, closeBtnR, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    if (m_viewMode == ViewMode::Proportions) {
        m_ScrollbarThumbRect = {};
        DrawProportionsView(viewRect);
        return;
    }
    if (m_viewMode == ViewMode::PieChart) {
        m_ScrollbarThumbRect = {};
        DrawPieChartView(viewRect);
        return;
    }

    if (m_viewMode == ViewMode::Details) {
        DrawDetailsView(viewRect);
    } else {
        DrawGridOrListView(viewRect);
    }

    // Draw vertical scrollbar on the right of the main view
    D2D1_RECT_F contentRect = GetContentItemsRect();
    D2D1_RECT_F sbTrack = D2D1::RectF(contentRect.right, contentRect.top, rect.right, contentRect.bottom);
    m_solidBrush->SetColor(HexColor(0xF3F3F3));
    m_rt->FillRectangle(sbTrack, m_solidBrush);

    float viewH = std::max(1.0f, contentRect.bottom - contentRect.top);
    float totalH = GetTotalContentHeight(contentRect);
    if (totalH > viewH) {
        float thumbH = std::max(28.0f, (viewH / totalH) * viewH);
        float maxScroll = std::max(1.0f, totalH - viewH);
        float thumbTop = contentRect.top + (m_scrollY / maxScroll) * (viewH - thumbH);
        m_ScrollbarThumbRect = D2D1::RectF(sbTrack.left + 2.0f, thumbTop, sbTrack.right - 2.0f, thumbTop + thumbH);
        m_solidBrush->SetColor(m_draggingScrollbar ? HexColor(0x909090) : HexColor(0xC2C2C2));
        m_rt->FillRectangle(m_ScrollbarThumbRect, m_solidBrush);
    } else {
        m_ScrollbarThumbRect = {};
    }
}

void ExplorerApp::DrawDetailsView(const D2D1_RECT_F& rect) {
    const Strings& s = GetStrings(m_lang);

    // 1. Details Column Header Row ("Nazwa | Data modyfikacji | Typ | Rozmiar [↻] | Operacje")
    D2D1_RECT_F hdrRect = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + kDetailsHeaderHeight);
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRectangle(hdrRect, m_solidBrush);

    const wchar_t* colTitles[5] = { s.colName, s.colDateModified, s.colType, s.colSize, s.colActions };
    float cx = rect.left + 6.0f;
    for (int c = 0; c < 5; ++c) {
        if (c == 4 && m_currentPath == L"THIS_PC") break;
        float cw = m_colWidths[c];
        D2D1_RECT_F cr = D2D1::RectF(cx + 6.0f, hdrRect.top, cx + cw - 6.0f, hdrRect.bottom);
        m_solidBrush->SetColor(HexColor(0x4C566A));
        m_rt->DrawTextW(colTitles[c], static_cast<UINT32>(wcslen(colTitles[c])), m_fmtRegular, cr, m_solidBrush);

        // Sort chevron '^' / 'v' matching Win10 screenshot
        if (c < 4 && static_cast<int>(m_sortCol) == c) {
            D2D1_RECT_F arrowR = D2D1::RectF(cx + cw * 0.5f - 8.0f, hdrRect.top, cx + cw * 0.5f + 8.0f, hdrRect.top + 11.0f);
            m_solidBrush->SetColor(HexColor(0x666666));
            m_rt->DrawTextW(m_sortAsc ? L"\x2303" : L"\x2304", 1, m_fmtSmall, arrowR, m_solidBrush);
        }

        // On "Rozmiar" (Size) column (c == 3): add clickable Refresh/Calculate [↻] icon button!
        if (c == 3) {
            D2D1_RECT_F calcBtn = D2D1::RectF(cx + cw - 24.0f, hdrRect.top + 3.0f, cx + cw - 4.0f, hdrRect.bottom - 3.0f);
            m_ribbonHits.push_back({ CMD_CALC_FOLDER_SIZES, calcBtn });
            bool calculating = m_fileEngine.IsAnyFolderSizeCalculating();
            bool hovered = (m_hoverRibbonBtn == CMD_CALC_FOLDER_SIZES);

            if (hovered || calculating) {
                m_solidBrush->SetColor(calculating ? HexColor(0xCCE8FF) : HexColor(0xE5F3FB));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(calcBtn, 2.5f, 2.5f), m_solidBrush);
                m_solidBrush->SetColor(HexColor(0x0078D7));
                m_rt->DrawRoundedRectangle(D2D1::RoundedRect(calcBtn, 2.5f, 2.5f), m_solidBrush, 1.0f);
            } else {
                m_solidBrush->SetColor(HexColor(0xF0F3F6));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(calcBtn, 2.5f, 2.5f), m_solidBrush);
                m_solidBrush->SetColor(HexColor(0xCCCCCC));
                m_rt->DrawRoundedRectangle(D2D1::RoundedRect(calcBtn, 2.5f, 2.5f), m_solidBrush, 1.0f);
            }

            m_solidBrush->SetColor((hovered || calculating) ? HexColor(0x0067B8) : HexColor(0x333333));
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_rt->DrawTextW(L"\x21BB", 1, m_fmtSmall, calcBtn, m_solidBrush);
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        // Column right divider line
        m_solidBrush->SetColor(HexColor(0xE0E0E0));
        m_rt->DrawLine(D2D1::Point2F(cx + cw, hdrRect.top + 4.0f), D2D1::Point2F(cx + cw, hdrRect.bottom - 4.0f), m_solidBrush, 1.0f);
        cx += cw;
    }

    // 2. Virtualized Rows (ONLY draw visible rows!)
    D2D1_RECT_F contentRect = GetContentItemsRect();
    m_rt->PushAxisAlignedClip(contentRect, D2D1_ANTIALIAS_MODE_ALIASED);

    int firstRow = std::max(0, static_cast<int>(m_scrollY / kDetailsRowHeight));
    int visibleRows = static_cast<int>((contentRect.bottom - contentRect.top) / kDetailsRowHeight) + 2;
    int lastRow = std::min(static_cast<int>(m_items.size()) - 1, firstRow + visibleRows);

    for (int i = firstRow; i <= lastRow; ++i) {
        const FileEntry& item = m_items[i];
        D2D1_RECT_F rowR = GetItemRect(i, contentRect);

        if (item.isSelected) {
            m_solidBrush->SetColor(HexColor(0xCCE8FF));
            m_rt->FillRectangle(rowR, m_solidBrush);
            m_solidBrush->SetColor(HexColor(0x99D1FF));
            m_rt->DrawRectangle(rowR, m_solidBrush, 1.0f);
        } else if (m_hoverIndex == i) {
            m_solidBrush->SetColor(HexColor(0xE5F3FF));
            m_rt->FillRectangle(rowR, m_solidBrush);
        } else if (m_focusedIndex == i) {
            m_solidBrush->SetColor(HexColor(0x99D1FF));
            m_rt->DrawRectangle(rowR, m_solidBrush, 1.0f);
        }

        // Icon (16x16)
        D2D1_RECT_F iconR = D2D1::RectF(rowR.left + 6.0f, rowR.top + 3.0f, rowR.left + 22.0f, rowR.top + 19.0f);
        float alpha = (item.isHidden || item.isCut || item.isOffline) ? 0.58f : 1.0f;
        if (item.isDrive) {
            DrawDriveVectorIcon(iconR, item.isNetworkDrive, item.isOpticalDrive, item.isOffline);
        } else if (item.isDirectory) {
            DrawFolderVectorIcon(iconR, true);
        } else {
            ID2D1Bitmap* sysIcon = m_imageCache.GetSystemIconBitmap(m_rt, item.sysIconIndex, false);
            if (sysIcon) {
                m_rt->DrawBitmap(sysIcon, iconR, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            } else {
                DrawRibbonIcon(CMD_NONE, iconR);
            }
        }

        // Columns: Name | Date Modified | Type | Size | Operacje (Kosz / Usuń)
        float x0 = rowR.left + 4.0f;
        D2D1_RECT_F nameR = D2D1::RectF(x0 + 24.0f, rowR.top, x0 + m_colWidths[0] - 4.0f, rowR.bottom);
        bool isRotating = m_imageCache.IsPathRotating(item.normKey);
        if (isRotating) {
            std::wstring rotTitle = item.displayName + (m_lang == Lang::PL ? L"   [\x23F3 Przetwarzanie...]" : L"   [\x23F3 Processing...]");
            m_solidBrush->SetColor(HexColor(0x0067B8));
            m_rt->DrawTextW(rotTitle.c_str(), static_cast<UINT32>(rotTitle.size()), m_fmtRegular, nameR, m_solidBrush);
        } else {
            m_solidBrush->SetColor(HexColor(0x1E1E1E, alpha));
            m_rt->DrawTextW(item.displayName.c_str(), static_cast<UINT32>(item.displayName.size()), m_fmtRegular, nameR, m_solidBrush);
        }

        float x1 = x0 + m_colWidths[0];
        D2D1_RECT_F dateR = D2D1::RectF(x1 + 6.0f, rowR.top, x1 + m_colWidths[1] - 4.0f, rowR.bottom);
        m_solidBrush->SetColor(HexColor(0x555555, alpha));
        m_rt->DrawTextW(item.dateStr.c_str(), static_cast<UINT32>(item.dateStr.size()), m_fmtRegular, dateR, m_solidBrush);

        float x2 = x1 + m_colWidths[1];
        D2D1_RECT_F typeR = D2D1::RectF(x2 + 6.0f, rowR.top, x2 + m_colWidths[2] - 4.0f, rowR.bottom);
        m_rt->DrawTextW(item.typeName.c_str(), static_cast<UINT32>(item.typeName.size()), m_fmtRegular, typeR, m_solidBrush);

        float x3 = x2 + m_colWidths[2];
        if (!item.sizeStr.empty()) {
            D2D1_RECT_F sizeR = D2D1::RectF(x3 + 6.0f, rowR.top, x3 + m_colWidths[3] - 8.0f, rowR.bottom);
            m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            m_rt->DrawTextW(item.sizeStr.c_str(), static_cast<UINT32>(item.sizeStr.size()), m_fmtRegular, sizeR, m_solidBrush);
            m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        // Column 4: Operacje (Kosz + Trwale usuń buttons with confirmation!)
        if (m_currentPath != L"THIS_PC") {
            float opColX = rect.left + 6.0f + m_colWidths[0] + m_colWidths[1] + m_colWidths[2] + m_colWidths[3];
            D2D1_RECT_F rTrash = D2D1::RectF(opColX + 6.0f, rowR.top + 2.0f, opColX + 62.0f, rowR.bottom - 2.0f);
            D2D1_RECT_F rPerm  = D2D1::RectF(opColX + 66.0f, rowR.top + 2.0f, opColX + 124.0f, rowR.bottom - 2.0f);

            bool hovTrash = (m_hoverIndex == i && m_hoverRowActionKind == 1);
            bool hovPerm  = (m_hoverIndex == i && m_hoverRowActionKind == 2);

            // [🗑 Kosz] button
            m_solidBrush->SetColor(hovTrash ? HexColor(0xDCEBFC) : HexColor(0xF3F5F8));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(rTrash, 2.5f, 2.5f), m_solidBrush);
            m_solidBrush->SetColor(hovTrash ? HexColor(0x0078D7) : HexColor(0xC8D0DA));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(rTrash, 2.5f, 2.5f), m_solidBrush, 1.0f);
            m_solidBrush->SetColor(hovTrash ? HexColor(0x005A9E) : HexColor(0x3B4252));
            std::wstring trashTxt = std::wstring(L"\xD83D\xDDD1 ") + s.btnRowTrash;
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_rt->DrawTextW(trashTxt.c_str(), static_cast<UINT32>(trashTxt.size()), m_fmtSmall, rTrash, m_solidBrush);

            // [✕ Usuń] permanent delete button
            m_solidBrush->SetColor(hovPerm ? HexColor(0xFDE7E9) : HexColor(0xFAF2F2));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(rPerm, 2.5f, 2.5f), m_solidBrush);
            m_solidBrush->SetColor(hovPerm ? HexColor(0xD13438) : HexColor(0xE2C2C4));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(rPerm, 2.5f, 2.5f), m_solidBrush, 1.0f);
            m_solidBrush->SetColor(hovPerm ? HexColor(0xA80000) : HexColor(0x9E2A2B));
            std::wstring permTxt = std::wstring(L"\x2715 ") + s.btnRowPermDel;
            m_rt->DrawTextW(permTxt.c_str(), static_cast<UINT32>(permTxt.size()), m_fmtSmall, rPerm, m_solidBrush);
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }

    // 3. Draw Rubber-band Marquee Box Selection if active!
    DrawMarqueeOverlay();

    m_rt->PopAxisAlignedClip();
}

void ExplorerApp::DrawMarqueeOverlay() {
    if (!m_isBoxSelecting) return;
    D2D1_RECT_F boxR = D2D1::RectF(
        std::min(m_boxStartPt.x, m_boxCurrentPt.x),
        std::min(m_boxStartPt.y, m_boxCurrentPt.y) - m_scrollY,
        std::max(m_boxStartPt.x, m_boxCurrentPt.x),
        std::max(m_boxStartPt.y, m_boxCurrentPt.y) - m_scrollY);
    m_solidBrush->SetColor(HexColor(0x0078D7, 0.22f));
    m_rt->FillRectangle(boxR, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0x0078D7, 0.85f));
    m_rt->DrawRectangle(boxR, m_solidBrush, 1.0f);
}

void ExplorerApp::DrawGridOrListView(const D2D1_RECT_F& rect) {
    (void)rect;
    D2D1_RECT_F contentRect = GetContentItemsRect();
    m_rt->PushAxisAlignedClip(contentRect, D2D1_ANTIALIAS_MODE_ALIASED);

    float cellW = 0, cellH = 0, iconSize = 0;
    int cols = 1;
    GetGridMetrics(contentRect, cellW, cellH, iconSize, cols);

    int firstRow = std::max(0, static_cast<int>(m_scrollY / cellH));
    int visibleRows = static_cast<int>((contentRect.bottom - contentRect.top) / cellH) + 2;
    int firstIdx = firstRow * cols;
    int lastIdx = std::min(static_cast<int>(m_items.size()) - 1, (firstRow + visibleRows) * cols - 1);

    bool compactHorizontal = (m_viewMode == ViewMode::List || m_viewMode == ViewMode::SmallIcons);

    for (int i = firstIdx; i <= lastIdx; ++i) {
        const FileEntry& item = m_items[i];
        D2D1_RECT_F itemR = GetItemRect(i, contentRect);

        if (item.isSelected) {
            m_solidBrush->SetColor(HexColor(0xCCE8FF));
            m_rt->FillRectangle(itemR, m_solidBrush);
            m_solidBrush->SetColor(HexColor(0x99D1FF));
            m_rt->DrawRectangle(itemR, m_solidBrush, 1.0f);
        } else if (m_hoverIndex == i) {
            m_solidBrush->SetColor(HexColor(0xE5F3FF));
            m_rt->FillRectangle(itemR, m_solidBrush);
        }

        float alpha = (item.isHidden || item.isCut || item.isOffline) ? 0.58f : 1.0f;

        if (compactHorizontal) {
            D2D1_RECT_F iconR = D2D1::RectF(itemR.left + 4.0f, itemR.top + (cellH - iconSize) * 0.5f, itemR.left + 4.0f + iconSize, itemR.top + (cellH + iconSize) * 0.5f);
            if (item.isDrive) {
                DrawDriveVectorIcon(iconR, item.isNetworkDrive, item.isOpticalDrive, item.isOffline);
            } else if (item.isDirectory) {
                DrawFolderVectorIcon(iconR, true);
            } else {
                ID2D1Bitmap* sysIcon = m_imageCache.GetSystemIconBitmap(m_rt, item.sysIconIndex, false);
                if (sysIcon) m_rt->DrawBitmap(sysIcon, iconR, alpha);
                else DrawRibbonIcon(CMD_NONE, iconR);
            }
            D2D1_RECT_F textR = D2D1::RectF(iconR.right + 6.0f, itemR.top, itemR.right - 4.0f, itemR.bottom);
            m_solidBrush->SetColor(HexColor(0x1E1E1E, alpha));
            m_rt->DrawTextW(item.displayName.c_str(), static_cast<UINT32>(item.displayName.size()), m_fmtRegular, textR, m_solidBrush);
        } else {
            // Thumbnail Tile (Medium / Large / Extra Large / Jumbo 440px Icons)
            D2D1_RECT_F thumbBox = D2D1::RectF(
                itemR.left + (itemR.right - itemR.left - iconSize) * 0.5f,
                itemR.top + 4.0f,
                itemR.left + (itemR.right - itemR.left + iconSize) * 0.5f,
                itemR.top + 4.0f + iconSize);

            if (item.isDrive) {
                float drvSz = std::min(96.0f, iconSize * 0.72f);
                D2D1_RECT_F drvR = D2D1::RectF(
                    (thumbBox.left + thumbBox.right - drvSz) * 0.5f,
                    (thumbBox.top + thumbBox.bottom - drvSz) * 0.5f,
                    (thumbBox.left + thumbBox.right + drvSz) * 0.5f,
                    (thumbBox.top + thumbBox.bottom + drvSz) * 0.5f);
                DrawDriveVectorIcon(drvR, item.isNetworkDrive, item.isOpticalDrive, item.isOffline);
            } else if (item.isDirectory) {
                DrawFolderWithContentPreview(thumbBox, item);
            } else if (item.isImage || item.isVideo) {
                ID2D1Bitmap* thumbBmp = m_imageCache.GetOrQueueThumbnail(m_rt, item.fullPath);
                if (thumbBmp) {
                    D2D1_SIZE_F bmpSz = thumbBmp->GetSize();
                    D2D1_RECT_F destR = PathUtils::FitRectCentered(thumbBox, bmpSz.width, bmpSz.height);
                    m_rt->DrawBitmap(thumbBmp, destR, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

                    // Subtle border around photo/video thumbnail
                    m_solidBrush->SetColor(HexColor(0xD8D8D8));
                    m_rt->DrawRectangle(destR, m_solidBrush, 1.0f);

                    if (item.isVideo) {
                        D2D1_RECT_F badgeR = D2D1::RectF(destR.left + 4.0f, destR.bottom - 20.0f, destR.left + 26.0f, destR.bottom - 4.0f);
                        m_solidBrush->SetColor(HexColor(0x000000, 0.68f));
                        m_rt->FillRoundedRectangle(D2D1::RoundedRect(badgeR, 3.0f, 3.0f), m_solidBrush);
                        m_solidBrush->SetColor(HexColor(0xFFFFFF));
                        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        m_rt->DrawTextW(L"\x25B6", 1, m_fmtSmall, badgeR, m_solidBrush);
                        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    }
                } else {
                    // Placeholder card while background thread decodes thumbnail
                    m_solidBrush->SetColor(HexColor(0xF2F4F7));
                    m_rt->FillRectangle(thumbBox, m_solidBrush);
                    m_solidBrush->SetColor(HexColor(0xDCE1E7));
                    m_rt->DrawRectangle(thumbBox, m_solidBrush, 1.0f);
                }

                // Background Image Processing Overlay ("⏳ Przetwarzanie..." / "⏳ Processing...")!
                if (m_imageCache.IsPathRotating(item.normKey)) {
                    m_solidBrush->SetColor(HexColor(0x0F172A, 0.66f));
                    m_rt->FillRectangle(thumbBox, m_solidBrush);

                    float cy = (thumbBox.top + thumbBox.bottom) * 0.5f;
                    D2D1_RECT_F pillR = D2D1::RectF(thumbBox.left + 8.0f, cy - 14.0f, thumbBox.right - 8.0f, cy + 14.0f);
                    m_solidBrush->SetColor(HexColor(0x0067B8, 0.94f));
                    m_rt->FillRoundedRectangle(D2D1::RoundedRect(pillR, 5.0f, 5.0f), m_solidBrush);
                    m_solidBrush->SetColor(HexColor(0xFFFFFF));
                    const wchar_t* rotTxt = (m_lang == Lang::PL) ? L"\x23F3 Przetwarzanie..." : L"\x23F3 Processing...";
                    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    m_rt->DrawTextW(rotTxt, static_cast<UINT32>(wcslen(rotTxt)), m_fmtSmall, pillR, m_solidBrush);
                    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                }
            } else {
                ID2D1Bitmap* sysIcon = m_imageCache.GetSystemIconBitmap(m_rt, item.sysIconIndex, true);
                float sysSz = std::min(48.0f, iconSize * 0.65f);
                D2D1_RECT_F sysR = D2D1::RectF(
                    (thumbBox.left + thumbBox.right - sysSz) * 0.5f,
                    (thumbBox.top + thumbBox.bottom - sysSz) * 0.5f,
                    (thumbBox.left + thumbBox.right + sysSz) * 0.5f,
                    (thumbBox.top + thumbBox.bottom + sysSz) * 0.5f);
                if (sysIcon) m_rt->DrawBitmap(sysIcon, sysR, alpha);
                else DrawRibbonIcon(CMD_NONE, sysR);
            }

            D2D1_RECT_F labelR = D2D1::RectF(itemR.left + 2.0f, thumbBox.bottom + 2.0f, itemR.right - 2.0f, itemR.bottom - 1.0f);
            m_solidBrush->SetColor(HexColor(0x1E1E1E, alpha));
            m_rt->DrawTextW(item.displayName.c_str(), static_cast<UINT32>(item.displayName.size()), m_fmtCenterSmall, labelR, m_solidBrush);
        }
    }

    // Marquee Box Selection overlay
    DrawMarqueeOverlay();

    m_rt->PopAxisAlignedClip();
}

namespace {

struct WeightedTreemapInput {
    int index = -1;
    double weight = 0.0;
};

// Recursive binary-split squarified treemap partitioner (data is sorted descending by weight)
void PartitionTreemapRecursive(
    const std::vector<WeightedTreemapInput>& items,
    int start,
    int end,
    const D2D1_RECT_F& bounds,
    std::vector<D2D1_RECT_F>& outRects) {
    if (start >= end) return;
    if (end - start == 1) {
        outRects[start] = bounds;
        return;
    }

    float w = std::max(0.0f, bounds.right - bounds.left);
    float h = std::max(0.0f, bounds.bottom - bounds.top);
    if (w <= 1.0f || h <= 1.0f) {
        for (int i = start; i < end; ++i) outRects[i] = bounds;
        return;
    }

    double total = 0.0;
    for (int i = start; i < end; ++i) total += items[i].weight;
    if (total <= 0.0) {
        for (int i = start; i < end; ++i) outRects[i] = bounds;
        return;
    }

    // Find split point closest to 50% of total weight
    double half = total * 0.5;
    double accum = 0.0;
    int split = start + 1;
    for (int i = start; i < end - 1; ++i) {
        accum += items[i].weight;
        split = i + 1;
        if (accum >= half) break;
    }

    // Strict byte-proportional split (no 6% artificial minimum clamp that inflates 1 KB files)
    double frac = std::clamp(accum / total, 0.000001, 0.999999);
    if (w >= h) {
        float midX = bounds.left + static_cast<float>(w * frac);
        D2D1_RECT_F r1 = D2D1::RectF(bounds.left, bounds.top, midX, bounds.bottom);
        D2D1_RECT_F r2 = D2D1::RectF(midX, bounds.top, bounds.right, bounds.bottom);
        PartitionTreemapRecursive(items, start, split, r1, outRects);
        PartitionTreemapRecursive(items, split, end, r2, outRects);
    } else {
        float midY = bounds.top + static_cast<float>(h * frac);
        D2D1_RECT_F r1 = D2D1::RectF(bounds.left, bounds.top, bounds.right, midY);
        D2D1_RECT_F r2 = D2D1::RectF(bounds.left, midY, bounds.right, bounds.bottom);
        PartitionTreemapRecursive(items, start, split, r1, outRects);
        PartitionTreemapRecursive(items, split, end, r2, outRects);
    }
}

uint32_t GetTreemapCategoryColor(const std::wstring& extLower, bool isDirectory, int depth) {
    if (isDirectory) {
        if (depth == 0) return 0x263238;
        if (depth == 1) return 0x37474F;
        return 0x455A64;
    }
    if (extLower == L".mp4" || extLower == L".mkv" || extLower == L".mov" || extLower == L".avi" ||
        extLower == L".webm" || extLower == L".wmv" || extLower == L".m4v" || extLower == L".ts") {
        return 0x7E57C2; // Video - vivid purple
    }
    if (extLower == L".jpg" || extLower == L".jpeg" || extLower == L".png" || extLower == L".webp" ||
        extLower == L".bmp" || extLower == L".gif" || extLower == L".tif" || extLower == L".tiff" || extLower == L".raw") {
        return 0x00897B; // Images - teal/emerald
    }
    if (extLower == L".safetensors" || extLower == L".ckpt" || extLower == L".onnx" || extLower == L".pt" ||
        extLower == L".pth" || extLower == L".bin" || extLower == L".gguf" || extLower == L".db" || extLower == L".sqlite") {
        return 0xD81B60; // AI Models / Databases - magenta/crimson
    }
    if (extLower == L".zip" || extLower == L".7z" || extLower == L".rar" || extLower == L".tar" ||
        extLower == L".gz" || extLower == L".iso" || extLower == L".cab") {
        return 0xFB8C00; // Archives / Disk images - amber orange
    }
    if (extLower == L".exe" || extLower == L".dll" || extLower == L".msi" || extLower == L".sys" || extLower == L".bat") {
        return 0xE53935; // Executables / Binaries - red
    }
    if (extLower == L".cpp" || extLower == L".h" || extLower == L".hpp" || extLower == L".c" || extLower == L".cs" ||
        extLower == L".py" || extLower == L".js" || extLower == L".ts" || extLower == L".json" || extLower == L".xml" ||
        extLower == L".html" || extLower == L".css" || extLower == L".md" || extLower == L".txt" || extLower == L".log") {
        return 0x1E88E5; // Code / Text - ocean blue
    }
    if (extLower == L".mp3" || extLower == L".wav" || extLower == L".flac" || extLower == L".ogg" ||
        extLower == L".m4a" || extLower == L".aac") {
        return 0x43A047; // Audio - vibrant green
    }
    if (extLower == L".pdf" || extLower == L".doc" || extLower == L".docx" || extLower == L".xls" ||
        extLower == L".xlsx" || extLower == L".ppt" || extLower == L".pptx") {
        return 0x3949AB; // Office / PDF - indigo
    }
    return 0x546E7A; // Other files - slate blue-grey
}

uint32_t ScaleRgbBrightness(uint32_t rgb, float factor) {
    int r = static_cast<int>(((rgb >> 16) & 0xFF) * factor);
    int g = static_cast<int>(((rgb >> 8) & 0xFF) * factor);
    int b = static_cast<int>((rgb & 0xFF) * factor);
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

void DrawFloatingChartHoverPopup(
    ID2D1HwndRenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtBold,
    IDWriteTextFormat* fmtSmall,
    const D2D1_RECT_F& bounds,
    D2D1_POINT_2F mousePt,
    const std::wstring& title,
    const std::wstring& relPath,
    uint64_t sizeBytes,
    uint64_t totalBytes,
    bool isDirectory,
    int depth,
    uint32_t accentColor,
    bool isPending,
    Lang lang) {
    if (!rt || !brush || !fmtBold || !fmtSmall) return;

    const float popW = 316.0f;
    const float popH = 108.0f;
    float px = mousePt.x + 18.0f;
    float py = mousePt.y + 18.0f;

    if (px + popW > bounds.right - 8.0f) {
        px = mousePt.x - popW - 14.0f;
    }
    if (py + popH > bounds.bottom - 8.0f) {
        py = mousePt.y - popH - 14.0f;
    }
    px = std::clamp(px, bounds.left + 6.0f, std::max(bounds.left + 6.0f, bounds.right - popW - 6.0f));
    py = std::clamp(py, bounds.top + 6.0f, std::max(bounds.top + 6.0f, bounds.bottom - popH - 6.0f));

    D2D1_RECT_F popR = D2D1::RectF(px, py, px + popW, py + popH);

    // Subtle multi-layer drop shadow
    brush->SetColor(HexColor(0x0F172A, 0.10f));
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(px + 2.0f, py + 3.0f, px + popW + 4.0f, py + popH + 5.0f), 6.0f, 6.0f), brush);
    brush->SetColor(HexColor(0x0F172A, 0.16f));
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(px + 1.0f, py + 2.0f, px + popW + 2.0f, py + popH + 3.0f), 6.0f, 6.0f), brush);

    // Crisp white popup card (#FFFFFF) with Explorer border (#CBD5E1)
    brush->SetColor(HexColor(0xFFFFFF, 0.98f));
    rt->FillRoundedRectangle(D2D1::RoundedRect(popR, 6.0f, 6.0f), brush);
    brush->SetColor(HexColor(0xCBD5E1));
    rt->DrawRoundedRectangle(D2D1::RoundedRect(popR, 6.0f, 6.0f), brush, 1.3f);

    // Left category/slice color bar
    D2D1_RECT_F stripeR = D2D1::RectF(popR.left + 1.0f, popR.top + 4.0f, popR.left + 6.0f, popR.bottom - 4.0f);
    brush->SetColor(HexColor(accentColor));
    rt->FillRoundedRectangle(D2D1::RoundedRect(stripeR, 2.5f, 2.5f), brush);

    rt->PushAxisAlignedClip(popR, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Row 1: Icon + Item Name + Level/Type Badge
    std::wstring dispTitle = (isDirectory ? L"\xD83D\xDCC1 " : L"\xD83D\xDCC4 ") + (title.empty() ? relPath : title);
    D2D1_RECT_F titleR = D2D1::RectF(popR.left + 12.0f, popR.top + 6.0f, popR.right - 92.0f, popR.top + 26.0f);
    brush->SetColor(HexColor(0x0F172A));
    rt->DrawTextW(dispTitle.c_str(), static_cast<UINT32>(dispTitle.size()), fmtBold, titleR, brush);

    wchar_t badgeBuf[64] = {};
    if (isDirectory) {
        swprintf_s(badgeBuf, lang == Lang::PL ? L"Folder \x2022 Poz. %d" : L"Folder \x2022 Lvl %d", depth + 1);
    } else {
        swprintf_s(badgeBuf, lang == Lang::PL ? L"Plik \x2022 Poz. %d" : L"File \x2022 Lvl %d", depth + 1);
    }
    D2D1_RECT_F badgeR = D2D1::RectF(popR.right - 88.0f, popR.top + 7.0f, popR.right - 8.0f, popR.top + 24.0f);
    brush->SetColor(HexColor(0xF1F5F9));
    rt->FillRoundedRectangle(D2D1::RoundedRect(badgeR, 3.0f, 3.0f), brush);
    brush->SetColor(HexColor(0x475569));
    fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    rt->DrawTextW(badgeBuf, static_cast<UINT32>(wcslen(badgeBuf)), fmtSmall, badgeR, brush);
    fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // Row 2: Relative path
    std::wstring pathLine = relPath;
    if (pathLine.size() > 48) {
        pathLine = L"..." + pathLine.substr(pathLine.size() - 45);
    }
    D2D1_RECT_F pathR = D2D1::RectF(popR.left + 12.0f, popR.top + 26.0f, popR.right - 10.0f, popR.top + 44.0f);
    brush->SetColor(HexColor(0x64748B));
    rt->DrawTextW(pathLine.c_str(), static_cast<UINT32>(pathLine.size()), fmtSmall, pathR, brush);

    // Row 3: Size & Percentage Share
    double pct = (totalBytes > 0) ? (100.0 * static_cast<double>(sizeBytes) / static_cast<double>(totalBytes)) : 0.0;
    pct = std::clamp(pct, 0.0, 100.0);

    std::wstring sizeStr = (isPending && sizeBytes == 0)
        ? (lang == Lang::PL ? L"\x23F3 Skanowanie..." : L"\x23F3 Scanning...")
        : FileEngine::FormatSizeDetailed(sizeBytes);
    D2D1_RECT_F sizeR = D2D1::RectF(popR.left + 12.0f, popR.top + 45.0f, popR.left + 175.0f, popR.top + 66.0f);
    brush->SetColor(HexColor(0x0067B8));
    rt->DrawTextW(sizeStr.c_str(), static_cast<UINT32>(sizeStr.size()), fmtBold, sizeR, brush);

    wchar_t pctBuf[64] = {};
    swprintf_s(pctBuf, lang == Lang::PL ? L"%.2f%% folderu" : L"%.2f%% of folder", pct);
    D2D1_RECT_F pctR = D2D1::RectF(popR.left + 175.0f, popR.top + 45.0f, popR.right - 10.0f, popR.top + 66.0f);
    brush->SetColor(HexColor(0x1E293B));
    fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    rt->DrawTextW(pctBuf, static_cast<UINT32>(wcslen(pctBuf)), fmtBold, pctR, brush);
    fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // Row 4: Mini percentage progress bar
    D2D1_RECT_F barBg = D2D1::RectF(popR.left + 12.0f, popR.top + 69.0f, popR.right - 10.0f, popR.top + 75.0f);
    brush->SetColor(HexColor(0xE2E8F0));
    rt->FillRoundedRectangle(D2D1::RoundedRect(barBg, 3.0f, 3.0f), brush);

    float fillW = static_cast<float>((barBg.right - barBg.left) * (pct * 0.01));
    if (fillW > 2.0f) {
        D2D1_RECT_F barFg = D2D1::RectF(barBg.left, barBg.top, barBg.left + fillW, barBg.bottom);
        brush->SetColor(HexColor(accentColor));
        rt->FillRoundedRectangle(D2D1::RoundedRect(barFg, 3.0f, 3.0f), brush);
    }

    // Row 5: Action Hint
    const wchar_t* hintTxt = (lang == Lang::PL)
        ? L"2\x00D7 Klik = Wejdź / Otwórz   \x2022   PPM = Menu"
        : L"Double-click = Open   \x2022   Right-click = Menu";
    D2D1_RECT_F hintR = D2D1::RectF(popR.left + 12.0f, popR.top + 80.0f, popR.right - 10.0f, popR.bottom - 4.0f);
    brush->SetColor(HexColor(0x64748B));
    rt->DrawTextW(hintTxt, static_cast<UINT32>(wcslen(hintTxt)), fmtSmall, hintR, brush);

    rt->PopAxisAlignedClip();
}

} // namespace

void ExplorerApp::DrawProportionsView(const D2D1_RECT_F& rect) {
    m_treemapHits.clear();

    // Clean light Windows Explorer surface background
    m_solidBrush->SetColor(HexColor(0xF5F6F7));
    m_rt->FillRectangle(rect, m_solidBrush);

    // Compute total known directory size & folder scanning progress
    uint64_t totalBytes = 0;
    int totalFolders = 0;
    int doneFolders = 0;
    for (const auto& it : m_items) {
        totalBytes += it.size;
        if (it.isDirectory) {
            ++totalFolders;
            bool inFlight = m_fileEngine.IsFolderInFlight(it.normKey);
            if (!inFlight && !it.sizeStr.empty() && it.sizeStr != L"...") {
                ++doneFolders;
            }
        }
    }
    bool scanning = m_fileEngine.IsAnyFolderSizeCalculating() || (doneFolders < totalFolders && totalFolders > 0);
    if (scanning) {
        EnsureWorkAnimTimerState();
    }

    float topBarH = 30.0f;
    float scanBannerH = scanning ? 36.0f : 0.0f;
    float bottomBarH = 26.0f;
    D2D1_RECT_F topBarR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + topBarH);
    D2D1_RECT_F scanBannerR = D2D1::RectF(rect.left, topBarR.bottom, rect.right, topBarR.bottom + scanBannerH);
    D2D1_RECT_F botBarR = D2D1::RectF(rect.left, rect.bottom - bottomBarH, rect.right, rect.bottom);
    D2D1_RECT_F canvasR = D2D1::RectF(rect.left + 4.0f, scanBannerR.bottom + 4.0f, rect.right - 4.0f, botBarR.top - 4.0f);

    float animPhase = static_cast<float>(GetTickCount64() % 100000ULL) * 0.001f;
    float pulse = 0.5f + 0.5f * std::sin(animPhase * 2.2f);

    // 1. Top Header & Legend Bar (White Explorer Style)
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRectangle(topBarR, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xDADADA));
    m_rt->DrawLine(D2D1::Point2F(topBarR.left, topBarR.bottom), D2D1::Point2F(topBarR.right, topBarR.bottom), m_solidBrush, 1.0f);

    std::wstring totalTxt = (m_lang == Lang::PL ? L"Proporcje (100% dokładny rozmiar)  \x2022  Razem: " : L"Exact Size Proportions  \x2022  Total: ") +
                            FileEngine::FormatSizeDetailed(totalBytes);

    D2D1_RECT_F hdrTxtR = D2D1::RectF(topBarR.left + 10.0f, topBarR.top, topBarR.left + 390.0f, topBarR.bottom);
    m_solidBrush->SetColor(HexColor(0x1E293B));
    m_rt->DrawTextW(totalTxt.c_str(), static_cast<UINT32>(totalTxt.size()), m_fmtBold, hdrTxtR, m_solidBrush);

    // Rescan button
    D2D1_RECT_F rescanBtnR = D2D1::RectF(topBarR.left + 395.0f, topBarR.top + 4.0f, topBarR.left + 485.0f, topBarR.bottom - 4.0f);
    m_ribbonHits.push_back({ CMD_CALC_FOLDER_SIZES, rescanBtnR });
    bool rescanHov = (m_hoverRibbonBtn == CMD_CALC_FOLDER_SIZES);
    m_solidBrush->SetColor(rescanHov ? HexColor(0x0078D7) : HexColor(0xF1F5F9));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(rescanBtnR, 3.0f, 3.0f), m_solidBrush);
    m_solidBrush->SetColor(rescanHov ? HexColor(0x005A9E) : HexColor(0xCBD5E1));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(rescanBtnR, 3.0f, 3.0f), m_solidBrush, 1.0f);
    m_solidBrush->SetColor(rescanHov ? HexColor(0xFFFFFF) : HexColor(0x1E293B));
    const wchar_t* rescanLbl = (m_lang == Lang::PL) ? L"\x21BB Przelicz" : L"\x21BB Rescan";
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_rt->DrawTextW(rescanLbl, static_cast<UINT32>(wcslen(rescanLbl)), m_fmtSmall, rescanBtnR, m_solidBrush);
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // High-visibility live scanning banner right below topBarR (Light Warm Amber Explorer Banner)
    if (scanning) {
        m_solidBrush->SetColor(HexColor(0xFFFBEB));
        m_rt->FillRectangle(scanBannerR, m_solidBrush);

        // Animated diagonal hazard stripes in the background of the banner
        m_rt->PushAxisAlignedClip(scanBannerR, D2D1_ANTIALIAS_MODE_ALIASED);
        m_solidBrush->SetColor(HexColor(0xF59E0B, 0.10f + 0.05f * pulse));
        float stripeStep = 28.0f;
        float stripeOffset = std::fmod(animPhase * 22.0f, stripeStep);
        for (float sx = scanBannerR.left - 40.0f + stripeOffset; sx < scanBannerR.right + 40.0f; sx += stripeStep) {
            m_rt->DrawLine(
                D2D1::Point2F(sx, scanBannerR.bottom),
                D2D1::Point2F(sx + 18.0f, scanBannerR.top),
                m_solidBrush, 8.0f);
        }

        // Rotating vector spinner on the left
        float spinCx = scanBannerR.left + 20.0f;
        float spinCy = scanBannerR.top + 14.0f;
        float spinR = 8.0f;
        m_solidBrush->SetColor(HexColor(0xD97706, 0.25f));
        m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(spinCx, spinCy), spinR, spinR), m_solidBrush, 2.2f);
        for (int s = 0; s < 5; ++s) {
            float ang = animPhase * 4.2f - s * 0.35f;
            float px1 = spinCx + std::cos(ang) * (spinR - 1.2f);
            float py1 = spinCy + std::sin(ang) * (spinR - 1.2f);
            float px2 = spinCx + std::cos(ang) * (spinR + 1.2f);
            float py2 = spinCy + std::sin(ang) * (spinR + 1.2f);
            m_solidBrush->SetColor(HexColor(0xD97706, 1.0f - s * 0.16f));
            m_rt->DrawLine(D2D1::Point2F(px1, py1), D2D1::Point2F(px2, py2), m_solidBrush, 2.5f);
        }

        int pct = (totalFolders > 0) ? (doneFolders * 100 / totalFolders) : 0;
        std::wstring samplePath = m_fileEngine.GetActiveScanningSamplePath();
        if (samplePath.size() > 75) {
            samplePath = L"..." + samplePath.substr(samplePath.size() - 72);
        }

        wchar_t scanMsg[512] = {};
        if (m_lang == Lang::PL) {
            swprintf_s(scanMsg, L"TRWA SKANOWANIE PODFOLDERÓW (%d / %d gotowe \x2022 %d%%)   \x2022   Dotychczas: %s",
                       doneFolders, totalFolders, pct, FileEngine::FormatSizeDetailed(totalBytes).c_str());
        } else {
            swprintf_s(scanMsg, L"SCANNING SUBFOLDERS (%d / %d done \x2022 %d%%)   \x2022   Calculated so far: %s",
                       doneFolders, totalFolders, pct, FileEngine::FormatSizeDetailed(totalBytes).c_str());
        }
        std::wstring fullScanTxt = scanMsg;
        if (!samplePath.empty()) {
            fullScanTxt += (m_lang == Lang::PL ? L"   \x2022   Teraz: " : L"   \x2022   Now: ") + samplePath;
        }

        D2D1_RECT_F scanTxtR = D2D1::RectF(scanBannerR.left + 38.0f, scanBannerR.top + 2.0f, scanBannerR.right - 12.0f, scanBannerR.top + 24.0f);
        m_solidBrush->SetColor(HexColor(0x92400E));
        m_rt->DrawTextW(fullScanTxt.c_str(), static_cast<UINT32>(fullScanTxt.size()), m_fmtBold, scanTxtR, m_solidBrush);

        // Full-width progress track + animated shimmer bar at the bottom of scanBannerR
        D2D1_RECT_F progTrackR = D2D1::RectF(scanBannerR.left + 10.0f, scanBannerR.bottom - 8.0f, scanBannerR.right - 10.0f, scanBannerR.bottom - 3.0f);
        m_solidBrush->SetColor(HexColor(0xFDE68A));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(progTrackR, 2.5f, 2.5f), m_solidBrush);

        float trackW = std::max(1.0f, progTrackR.right - progTrackR.left);
        float fillFrac = (totalFolders > 0)
            ? std::clamp(static_cast<float>(doneFolders) / static_cast<float>(totalFolders), 0.05f, 0.98f)
            : 0.35f;
        D2D1_RECT_F progFillR = D2D1::RectF(progTrackR.left, progTrackR.top, progTrackR.left + trackW * fillFrac, progTrackR.bottom);
        m_solidBrush->SetColor(HexColor(0xF59E0B));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(progFillR, 2.5f, 2.5f), m_solidBrush);

        // Moving bright highlight wave across the entire progress track
        float wavePos = std::fmod(animPhase * 0.55f, 1.0f);
        float waveW = std::min(160.0f, trackW * 0.25f);
        float waveX = progTrackR.left + (trackW - waveW) * wavePos;
        D2D1_RECT_F waveR = D2D1::RectF(waveX, progTrackR.top, waveX + waveW, progTrackR.bottom);
        m_solidBrush->SetColor(HexColor(0xFFFFFF, 0.75f));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(waveR, 2.5f, 2.5f), m_solidBrush);

        m_rt->PopAxisAlignedClip();

        m_solidBrush->SetColor(HexColor(0xF59E0B, 0.65f + 0.35f * pulse));
        m_rt->DrawLine(D2D1::Point2F(scanBannerR.left, scanBannerR.bottom), D2D1::Point2F(scanBannerR.right, scanBannerR.bottom), m_solidBrush, 1.5f);
    }

    // Category Legend on the right side of topBarR
    struct LegendChip { uint32_t color; const wchar_t* labelPL; const wchar_t* labelEN; };
    const LegendChip chips[] = {
        { 0x7E57C2, L"Wideo",       L"Video"    },
        { 0x00897B, L"Obrazy",      L"Images"   },
        { 0xD81B60, L"Modele/Bazy", L"AI/DB"    },
        { 0xFB8C00, L"Archiwa",     L"Archives" },
        { 0xE53935, L"Programy",    L"Apps"     },
        { 0x1E88E5, L"Kod/Tekst",   L"Code/Txt" },
        { 0x43A047, L"Audio",       L"Audio"    },
    };
    float chipX = topBarR.right - 8.0f;
    for (int i = static_cast<int>(std::size(chips)) - 1; i >= 0; --i) {
        const wchar_t* lbl = (m_lang == Lang::PL) ? chips[i].labelPL : chips[i].labelEN;
        float cw = static_cast<float>(wcslen(lbl)) * 6.5f + 22.0f;
        if (chipX - cw < rescanBtnR.right + 12.0f) break;
        D2D1_RECT_F cr = D2D1::RectF(chipX - cw, topBarR.top + 6.0f, chipX, topBarR.bottom - 6.0f);
        D2D1_RECT_F swR = D2D1::RectF(cr.left + 3.0f, cr.top + 4.0f, cr.left + 13.0f, cr.bottom - 4.0f);
        m_solidBrush->SetColor(HexColor(chips[i].color));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(swR, 2.0f, 2.0f), m_solidBrush);
        D2D1_RECT_F ltR = D2D1::RectF(cr.left + 16.0f, cr.top, cr.right, cr.bottom);
        m_solidBrush->SetColor(HexColor(0x334155));
        m_rt->DrawTextW(lbl, static_cast<UINT32>(wcslen(lbl)), m_fmtSmall, ltR, m_solidBrush);
        chipX -= (cw + 6.0f);
    }

    // 2. Main Nested Squarified Treemap Canvas (100% Exact Byte Proportions)
    m_rt->PushAxisAlignedClip(canvasR, D2D1_ANTIALIAS_MODE_ALIASED);

    if (m_items.empty()) {
        m_solidBrush->SetColor(HexColor(0x64748B));
        const wchar_t* emptyMsg = (m_lang == Lang::PL) ? L"Ten folder jest pusty." : L"This folder is empty.";
        m_fmtCenterSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_rt->DrawTextW(emptyMsg, static_cast<UINT32>(wcslen(emptyMsg)), m_fmtCenterSmall, canvasR, m_solidBrush);
    } else {
        // Helper to draw diagonal animated scanning stripes over any in-flight block
        auto drawScanningOverlay = [&](const D2D1_RECT_F& r, bool showCenteredBadge) {
            float bw = r.right - r.left;
            float bh = r.bottom - r.top;
            if (bw <= 4.0f || bh <= 4.0f) return;

            m_rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
            m_solidBrush->SetColor(HexColor(0xF59E0B, 0.16f + 0.10f * pulse));
            float step = 22.0f;
            float off = std::fmod(animPhase * 18.0f, step);
            for (float x = r.left - bh + off; x < r.right + bh; x += step) {
                m_rt->DrawLine(D2D1::Point2F(x, r.bottom), D2D1::Point2F(x + bh, r.top), m_solidBrush, 5.0f);
            }
            if (showCenteredBadge && bw >= 95.0f && bh >= 38.0f) {
                float pillW = std::min(bw - 10.0f, 145.0f);
                float pillH = 20.0f;
                float cx = (r.left + r.right) * 0.5f;
                float cy = (r.top + r.bottom) * 0.5f;
                D2D1_RECT_F pillR = D2D1::RectF(cx - pillW * 0.5f, cy - pillH * 0.5f, cx + pillW * 0.5f, cy + pillH * 0.5f);
                m_solidBrush->SetColor(HexColor(0xFFFBEB, 0.94f));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(pillR, 4.0f, 4.0f), m_solidBrush);
                m_solidBrush->SetColor(HexColor(0xF59E0B, 0.95f));
                m_rt->DrawRoundedRectangle(D2D1::RoundedRect(pillR, 4.0f, 4.0f), m_solidBrush, 1.2f);

                const wchar_t* scanBadge = (m_lang == Lang::PL) ? L"\x23F3 SKANOWANIE..." : L"\x23F3 SCANNING...";
                m_solidBrush->SetColor(HexColor(0x92400E));
                m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                m_rt->DrawTextW(scanBadge, static_cast<UINT32>(wcslen(scanBadge)), m_fmtSmall, pillR, m_solidBrush);
                m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
            m_rt->PopAxisAlignedClip();

            m_solidBrush->SetColor(HexColor(0xF59E0B, 0.65f + 0.35f * pulse));
            m_rt->DrawRectangle(r, m_solidBrush, 1.6f);
        };

        // Helper to draw a WinDirStat-style 3D cushion/bevel leaf block
        auto drawLeafBlock = [&](const D2D1_RECT_F& r, uint32_t baseColor, const std::wstring& name, uint64_t sizeBytes, bool isPendingFolder) {
            float bw = r.right - r.left;
            float bh = r.bottom - r.top;
            if (bw <= 0.4f || bh <= 0.4f) return;

            D2D1_RECT_F inner = (bw > 2.0f && bh > 2.0f)
                ? D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f)
                : r;
            m_solidBrush->SetColor(HexColor(baseColor));
            m_rt->FillRectangle(inner, m_solidBrush);

            if (bw >= 4.0f && bh >= 4.0f) {
                // Upper-half subtle specular cushion sheen
                D2D1_RECT_F sheenR = D2D1::RectF(inner.left + 1.0f, inner.top + 1.0f, inner.right - 1.0f, inner.top + bh * 0.42f);
                m_solidBrush->SetColor(HexColor(0xFFFFFF, 0.14f));
                m_rt->FillRectangle(sheenR, m_solidBrush);

                // 3D top/left highlight bevel
                m_solidBrush->SetColor(HexColor(ScaleRgbBrightness(baseColor, 1.28f)));
                m_rt->DrawLine(D2D1::Point2F(inner.left, inner.top), D2D1::Point2F(inner.right, inner.top), m_solidBrush, 1.0f);
                m_rt->DrawLine(D2D1::Point2F(inner.left, inner.top), D2D1::Point2F(inner.left, inner.bottom), m_solidBrush, 1.0f);

                // 3D bottom/right shadow bevel
                m_solidBrush->SetColor(HexColor(ScaleRgbBrightness(baseColor, 0.68f)));
                m_rt->DrawLine(D2D1::Point2F(inner.left, inner.bottom), D2D1::Point2F(inner.right, inner.bottom), m_solidBrush, 1.0f);
                m_rt->DrawLine(D2D1::Point2F(inner.right, inner.top), D2D1::Point2F(inner.right, inner.bottom), m_solidBrush, 1.0f);
            }

            if (isPendingFolder) {
                drawScanningOverlay(inner, true);
            }

            if (bw >= 34.0f && bh >= 16.0f) {
                m_rt->PushAxisAlignedClip(inner, D2D1_ANTIALIAS_MODE_ALIASED);
                D2D1_RECT_F txtR = D2D1::RectF(inner.left + 4.0f, inner.top + 2.0f, inner.right - 3.0f, inner.bottom - 2.0f);
                std::wstring label = name;
                if (bh >= 30.0f && bw >= 52.0f) {
                    if (isPendingFolder && sizeBytes == 0) {
                        label += (m_lang == Lang::PL ? L"\n(\x23F3 Skanowanie...)" : L"\n(\x23F3 Scanning...)");
                    } else if (sizeBytes > 0) {
                        label += L"\n" + FileEngine::FormatSizeDetailed(sizeBytes);
                    }
                }
                D2D1_RECT_F shR = D2D1::RectF(txtR.left + 1.0f, txtR.top + 1.0f, txtR.right + 1.0f, txtR.bottom + 1.0f);
                m_solidBrush->SetColor(HexColor(0x000000, 0.55f));
                m_rt->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), m_fmtSmall, shR, m_solidBrush);
                m_solidBrush->SetColor(HexColor(0xFFFFFF, 0.98f));
                m_rt->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), m_fmtSmall, txtR, m_solidBrush);
                m_rt->PopAxisAlignedClip();
            }
        };

        // Recursive function to draw nested folder children up to depth 3 (4 levels total) with exact byte proportions
        std::function<void(const D2D1_RECT_F&, const std::vector<TreemapChildNode>&, const std::wstring&, int, int)> drawSubtreeChildren;
        drawSubtreeChildren = [&](const D2D1_RECT_F& boxR, const std::vector<TreemapChildNode>& children, const std::wstring& relPrefix, int topItemIdx, int depth) {
            float boxW = boxR.right - boxR.left;
            float boxH = boxR.bottom - boxR.top;
            if (boxW <= 2.0f || boxH <= 2.0f || children.empty()) return;

            std::vector<WeightedTreemapInput> inputs;
            inputs.reserve(children.size());
            for (int ci = 0; ci < static_cast<int>(children.size()); ++ci) {
                double cw = (children[ci].size > 0) ? static_cast<double>(children[ci].size) : 1.0;
                inputs.push_back({ ci, cw });
            }

            std::vector<D2D1_RECT_F> childRects(inputs.size());
            PartitionTreemapRecursive(inputs, 0, static_cast<int>(inputs.size()), boxR, childRects);

            for (size_t i = 0; i < inputs.size(); ++i) {
                const TreemapChildNode& c = children[inputs[i].index];
                D2D1_RECT_F cr = childRects[i];
                float cw = cr.right - cr.left;
                float ch = cr.bottom - cr.top;
                if (cw <= 0.5f || ch <= 0.5f) continue;

                uint32_t nodeCol = c.isDirectory
                    ? GetTreemapCategoryColor(L"", true, depth)
                    : (c.fullPath.empty() ? 0x546E7A : GetTreemapCategoryColor(c.ext, false, depth));

                std::wstring childRel = relPrefix + L"\\" + c.name;
                if (!c.fullPath.empty()) {
                    TreemapHitNode hit;
                    hit.rect = cr;
                    hit.topItemIndex = topItemIdx;
                    hit.name = c.name;
                    hit.fullPath = c.fullPath;
                    hit.relPath = childRel;
                    hit.size = c.size;
                    hit.color = nodeCol;
                    hit.isDirectory = c.isDirectory;
                    hit.depth = depth;
                    m_treemapHits.push_back(hit);
                }

                if (!c.isDirectory) {
                    drawLeafBlock(cr, nodeCol, c.name, c.size, false);
                    continue;
                }

                // It's a nested subfolder — check if we can subdivide further (up to depth 3)
                std::vector<TreemapChildNode> subChildren;
                bool hasSub = (depth < 3) && !c.normKey.empty() &&
                              m_fileEngine.GetFolderTreemapChildren(c.normKey, subChildren) &&
                              !subChildren.empty();
                if (!hasSub || cw < 28.0f || ch < 24.0f) {
                    drawLeafBlock(cr, nodeCol, c.name, c.size, false);
                    continue;
                }

                D2D1_RECT_F subBox = D2D1::RectF(cr.left + 0.8f, cr.top + 0.8f, cr.right - 0.8f, cr.bottom - 0.8f);
                m_solidBrush->SetColor(HexColor(depth == 1 ? 0xF8FAFC : 0xF1F5F9));
                m_rt->FillRectangle(subBox, m_solidBrush);
                m_solidBrush->SetColor(HexColor(depth == 1 ? 0x94A3B8 : 0xCBD5E1));
                m_rt->DrawRectangle(subBox, m_solidBrush, 1.0f);

                float hdrH = (ch >= 36.0f) ? 14.0f : 0.0f;
                if (hdrH > 0.0f) {
                    D2D1_RECT_F shdrR = D2D1::RectF(subBox.left, subBox.top, subBox.right, subBox.top + hdrH);
                    m_solidBrush->SetColor(HexColor(depth == 1 ? 0xE2E8F0 : 0xEDF2F7));
                    m_rt->FillRectangle(shdrR, m_solidBrush);

                    m_rt->PushAxisAlignedClip(shdrR, D2D1_ANTIALIAS_MODE_ALIASED);
                    std::wstring subHdr = c.name;
                    if (c.size > 0 && cw >= 90.0f) {
                        subHdr += L" (" + FileEngine::FormatSizeDetailed(c.size) + L")";
                    }
                    D2D1_RECT_F stxtR = D2D1::RectF(shdrR.left + 3.0f, shdrR.top, shdrR.right - 2.0f, shdrR.bottom);
                    m_solidBrush->SetColor(HexColor(0x1E293B));
                    m_rt->DrawTextW(subHdr.c_str(), static_cast<UINT32>(subHdr.size()), m_fmtSmall, stxtR, m_solidBrush);
                    m_rt->PopAxisAlignedClip();
                }

                D2D1_RECT_F subInner = D2D1::RectF(subBox.left + 1.2f, subBox.top + hdrH + 1.0f, subBox.right - 1.2f, subBox.bottom - 1.2f);
                drawSubtreeChildren(subInner, subChildren, childRel, topItemIdx, depth + 1);
            }
        };

        // Build weighted list of top-level items using TRUE byte weights
        double fallbackPendingWeight = (totalBytes > 0)
            ? std::max<double>(1024.0 * 1024.0, (static_cast<double>(totalBytes) / std::max<size_t>(6, m_items.size())) * 0.08)
            : 100.0;

        std::vector<WeightedTreemapInput> topInputs;
        topInputs.reserve(m_items.size());
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            const FileEntry& it = m_items[i];
            bool inFlight = it.isDirectory && (m_fileEngine.IsFolderInFlight(it.normKey) || it.sizeStr.empty() || it.sizeStr == L"...");
            double w = 0.0;
            if (it.size > 0) {
                w = static_cast<double>(it.size);
            } else if (inFlight) {
                w = fallbackPendingWeight;
            } else {
                w = 1.0;
            }
            topInputs.push_back({ i, w });
        }

        std::sort(topInputs.begin(), topInputs.end(), [](const WeightedTreemapInput& a, const WeightedTreemapInput& b) {
            return a.weight > b.weight;
        });

        std::vector<D2D1_RECT_F> topRects(topInputs.size());
        PartitionTreemapRecursive(topInputs, 0, static_cast<int>(topInputs.size()), canvasR, topRects);

        std::vector<TreemapChildNode> children1;

        for (size_t ti = 0; ti < topInputs.size(); ++ti) {
            int itemIdx = topInputs[ti].index;
            const FileEntry& item = m_items[itemIdx];
            D2D1_RECT_F r0 = topRects[ti];
            float w0 = r0.right - r0.left;
            float h0 = r0.bottom - r0.top;
            if (w0 <= 0.5f || h0 <= 0.5f) continue;

            bool folderInFlight = item.isDirectory && (m_fileEngine.IsFolderInFlight(item.normKey) || item.sizeStr.empty() || item.sizeStr == L"...");
            uint32_t topCol = item.isDirectory ? 0x0078D7 : GetTreemapCategoryColor(item.ext, false, 0);

            // Register level-0 hit node first (so deeper children pushed later win reverse hit-test)
            TreemapHitNode hit0;
            hit0.rect = r0;
            hit0.topItemIndex = itemIdx;
            hit0.name = item.displayName;
            hit0.fullPath = item.fullPath;
            hit0.relPath = item.name;
            hit0.size = item.size;
            hit0.color = topCol;
            hit0.isDirectory = item.isDirectory;
            hit0.isInFlight = folderInFlight;
            hit0.depth = 0;
            m_treemapHits.push_back(hit0);

            if (!item.isDirectory) {
                drawLeafBlock(r0, topCol, item.displayName, item.size, false);
                continue;
            }

            bool hasChildren1 = m_fileEngine.GetFolderTreemapChildren(item.normKey, children1) && !children1.empty();
            if (!hasChildren1 || w0 < 24.0f || h0 < 22.0f) {
                uint32_t col = folderInFlight ? 0x64748B : 0x475569;
                drawLeafBlock(r0, col, L"\xD83D\xDCC1 " + item.displayName, item.size, folderInFlight);
                continue;
            }

            // Draw Level-0 Folder Outer Container Frame + Header Strip (White Explorer Theme)
            D2D1_RECT_F outerBox = D2D1::RectF(r0.left + 1.0f, r0.top + 1.0f, r0.right - 1.0f, r0.bottom - 1.0f);
            m_solidBrush->SetColor(HexColor(0xFFFFFF));
            m_rt->FillRectangle(outerBox, m_solidBrush);
            m_solidBrush->SetColor(folderInFlight ? HexColor(0xF59E0B, 0.80f + 0.20f * pulse) : HexColor(0x94A3B8));
            m_rt->DrawRectangle(outerBox, m_solidBrush, folderInFlight ? 1.8f : 1.2f);

            float hdrH0 = (h0 >= 38.0f) ? 16.0f : 0.0f;
            if (hdrH0 > 0.0f) {
                D2D1_RECT_F hdrR = D2D1::RectF(outerBox.left, outerBox.top, outerBox.right, outerBox.top + hdrH0);
                m_solidBrush->SetColor(folderInFlight ? HexColor(0xFEF3C7) : HexColor(0xE2E8F0));
                m_rt->FillRectangle(hdrR, m_solidBrush);

                m_rt->PushAxisAlignedClip(hdrR, D2D1_ANTIALIAS_MODE_ALIASED);
                std::wstring fHdr = L"\xD83D\xDCC1 " + item.displayName;
                if (item.size > 0 && w0 >= 95.0f) {
                    fHdr += L"  (" + FileEngine::FormatSizeDetailed(item.size) + L")";
                }
                if (folderInFlight && w0 >= 140.0f) {
                    fHdr += (m_lang == Lang::PL ? L"  [\x23F3 Skanowanie...]" : L"  [\x23F3 Scanning...]");
                }
                D2D1_RECT_F fTxtR = D2D1::RectF(hdrR.left + 4.0f, hdrR.top, hdrR.right - 3.0f, hdrR.bottom);
                m_solidBrush->SetColor(HexColor(folderInFlight ? 0x92400E : 0x0F172A));
                m_rt->DrawTextW(fHdr.c_str(), static_cast<UINT32>(fHdr.size()), m_fmtBold, fTxtR, m_solidBrush);
                m_rt->PopAxisAlignedClip();
            }

            D2D1_RECT_F inner0 = D2D1::RectF(outerBox.left + 2.0f, outerBox.top + hdrH0 + 1.5f, outerBox.right - 2.0f, outerBox.bottom - 2.0f);
            if (inner0.right - inner0.left > 3.0f && inner0.bottom - inner0.top > 3.0f) {
                drawSubtreeChildren(inner0, children1, item.name, itemIdx, 1);
            }
            if (folderInFlight) {
                drawScanningOverlay(outerBox, false);
            }
        }

        // Highlight selected top-level items
        for (const auto& hn : m_treemapHits) {
            if (hn.depth == 0 && hn.topItemIndex >= 0 && hn.topItemIndex < static_cast<int>(m_items.size()) &&
                m_items[hn.topItemIndex].isSelected) {
                m_solidBrush->SetColor(HexColor(0x0078D7, 0.95f));
                m_rt->DrawRectangle(hn.rect, m_solidBrush, 2.4f);
            }
        }

        // Highlight currently hovered nested or top-level block
        if (m_hoverTreemapHitIndex >= 0 && m_hoverTreemapHitIndex < static_cast<int>(m_treemapHits.size())) {
            const TreemapHitNode& hov = m_treemapHits[m_hoverTreemapHitIndex];
            m_solidBrush->SetColor(HexColor(0xFFFFFF, 0.22f));
            m_rt->FillRectangle(hov.rect, m_solidBrush);
            m_solidBrush->SetColor(HexColor(0x0067B8, 0.98f));
            m_rt->DrawRectangle(hov.rect, m_solidBrush, 2.2f);
        }
    }

    m_rt->PopAxisAlignedClip();

    // 3. Bottom Inspector Bar (Light Explorer Status Style)
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRectangle(botBarR, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xDADADA));
    m_rt->DrawLine(D2D1::Point2F(botBarR.left, botBarR.top), D2D1::Point2F(botBarR.right, botBarR.top), m_solidBrush, 1.0f);

    std::wstring inspectTxt;
    if (m_hoverTreemapHitIndex >= 0 && m_hoverTreemapHitIndex < static_cast<int>(m_treemapHits.size())) {
        const TreemapHitNode& hov = m_treemapHits[m_hoverTreemapHitIndex];
        double pct = (totalBytes > 0) ? (static_cast<double>(hov.size) * 100.0 / static_cast<double>(totalBytes)) : 0.0;
        wchar_t pctBuf[64] = {};
        swprintf_s(pctBuf, L"%.1f%%", pct);

        inspectTxt = (hov.isDirectory ? L"\xD83D\xDCC1 " : L"\xD83D\xDCC4 ") + hov.relPath +
                     L"     \x2022     " + FileEngine::FormatSizeDetailed(hov.size) +
                     L"  (" + pctBuf + L")     \x2022     " +
                     (m_lang == Lang::PL
                         ? L"[2\xD7 Klik = Wejdź / Otwórz   \x2022   PPM = Menu]"
                         : L"[Double-Click = Enter / Open   \x2022   Right-Click = Menu]");
    } else {
        inspectTxt = (m_lang == Lang::PL)
            ? L"Najedź kursorem na dowolny prostokąt, aby sprawdzić ścieżkę i udział procentowy. 2\xD7 Klik wchodzi w kliknięty podfolder lub otwiera plik."
            : L"Hover over any rectangle to inspect its path and size share. Double-click drills down into any subfolder or opens the file.";
    }

    D2D1_RECT_F inspR = D2D1::RectF(botBarR.left + 10.0f, botBarR.top, botBarR.right - 10.0f, botBarR.bottom);
    m_solidBrush->SetColor(HexColor(0x1E293B));
    m_rt->DrawTextW(inspectTxt.c_str(), static_cast<UINT32>(inspectTxt.size()), m_fmtSmall, inspR, m_solidBrush);

    // 4. Floating On-Hover Info Popup Card (DRY shared helper)
    if (m_hoverTreemapHitIndex >= 0 && m_hoverTreemapHitIndex < static_cast<int>(m_treemapHits.size())) {
        const TreemapHitNode& hov = m_treemapHits[m_hoverTreemapHitIndex];
        DrawFloatingChartHoverPopup(
            m_rt, m_solidBrush, m_fmtBold, m_fmtSmall,
            canvasR, m_mousePt,
            hov.name, hov.relPath, hov.size, totalBytes,
            hov.isDirectory, hov.depth, hov.color, hov.isInFlight, m_lang);
    }
}

void ExplorerApp::DrawFullViewerCanvas(const D2D1_RECT_F& rect, bool includeFilmstrip) {
    const Strings& s = GetStrings(m_lang);

    // Dark studio canvas background (#181A1F) so images & videos pop with maximum contrast
    m_solidBrush->SetColor(HexColor(0x181A1F));
    m_rt->FillRectangle(rect, m_solidBrush);

    float stripH = (includeFilmstrip && !m_isFullScreen) ? kFilmstripHeight : 0.0f;
    float hudTopH = 38.0f;
    D2D1_RECT_F canvasR = D2D1::RectF(rect.left, rect.top + hudTopH, rect.right, rect.bottom - stripH);

    int activeIdx = m_focusedIndex;
    if (activeIdx < 0 && !m_items.empty()) activeIdx = 0;

    bool isMediaItem = false;
    bool isAudioOnly = false;
    std::wstring curPath, prevPath, nextPath;
    if (activeIdx >= 0 && activeIdx < static_cast<int>(m_items.size())) {
        const FileEntry& activeItem = m_items[activeIdx];
        if (activeItem.isImage) {
            curPath = activeItem.fullPath;
        } else if (activeItem.isVideo || activeItem.isAudio) {
            isMediaItem = true;
            isAudioOnly = activeItem.isAudio;
            curPath = activeItem.fullPath;
        }
        for (int i = activeIdx - 1; i >= 0; --i) {
            if (m_items[i].isImage) { prevPath = m_items[i].fullPath; break; }
        }
        for (int i = activeIdx + 1; i < static_cast<int>(m_items.size()); ++i) {
            if (m_items[i].isImage) { nextPath = m_items[i].fullPath; break; }
        }
    }

    // 1. Render Full-Resolution Image OR Hardware-Accelerated Video/Audio Player
    m_rt->PushAxisAlignedClip(canvasR, D2D1_ANTIALIAS_MODE_ALIASED);

    uint32_t imgOrigW = 0, imgOrigH = 0;
    float effectiveZoomPct = 100.0f;

    if (isMediaItem && m_mediaPlayer.HasMedia()) {
        float transportH = 52.0f;
        D2D1_RECT_F stageR = D2D1::RectF(canvasR.left, canvasR.top, canvasR.right, std::max(canvasR.top + 40.0f, canvasR.bottom - transportH));

        if (!isAudioOnly && m_mediaPlayer.HasVideo()) {
            ID2D1Bitmap* vidBmp = m_mediaPlayer.GetCurrentVideoFrame(m_rt, imgOrigW, imgOrigH);
            if (!vidBmp) {
                vidBmp = m_imageCache.GetOrQueueThumbnail(m_rt, curPath);
            }
            if (vidBmp) {
                D2D1_SIZE_F bmpSz = vidBmp->GetSize();
                float dispW = (imgOrigW > 0) ? static_cast<float>(imgOrigW) : bmpSz.width;
                float dispH = (imgOrigH > 0) ? static_cast<float>(imgOrigH) : bmpSz.height;

                float availW = std::max(40.0f, stageR.right - stageR.left - 24.0f);
                float availH = std::max(40.0f, stageR.bottom - stageR.top - 24.0f);

                float baseScale = 1.0f;
                if (!m_viewerActualPixels) {
                    baseScale = std::min(1.0f, std::min(availW / std::max(1.0f, dispW), availH / std::max(1.0f, dispH)));
                }
                float finalScale = baseScale * m_viewerZoom;
                effectiveZoomPct = finalScale * 100.0f;

                float drawW = dispW * finalScale;
                float drawH = dispH * finalScale;
                float cx = (stageR.left + stageR.right) * 0.5f + m_viewerPanX;
                float cy = (stageR.top + stageR.bottom) * 0.5f + m_viewerPanY;

                D2D1_RECT_F destR = D2D1::RectF(cx - drawW * 0.5f, cy - drawH * 0.5f, cx + drawW * 0.5f, cy + drawH * 0.5f);
                m_rt->DrawBitmap(vidBmp, destR, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        } else {
            // Audio Studio Card in the center of stageR
            float cardW = std::min(420.0f, (stageR.right - stageR.left) - 32.0f);
            float cardH = 150.0f;
            float cx = (stageR.left + stageR.right) * 0.5f;
            float cy = (stageR.top + stageR.bottom) * 0.5f;
            D2D1_RECT_F cardR = D2D1::RectF(cx - cardW * 0.5f, cy - cardH * 0.5f, cx + cardW * 0.5f, cy + cardH * 0.5f);

            m_solidBrush->SetColor(HexColor(0x232731));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(cardR, 8.0f, 8.0f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(0x38A1F3, 0.6f));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(cardR, 8.0f, 8.0f), m_solidBrush, 1.2f);

            std::wstring noteTitle = L"\x266B   " + m_items[activeIdx].name;
            D2D1_RECT_F titleR = D2D1::RectF(cardR.left + 16.0f, cardR.top + 28.0f, cardR.right - 16.0f, cardR.top + 68.0f);
            m_solidBrush->SetColor(HexColor(0xECEFF4));
            m_fmtCenterSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_rt->DrawTextW(noteTitle.c_str(), static_cast<UINT32>(noteTitle.size()), m_fmtCenterSmall, titleR, m_solidBrush);

            std::wstring timeInfo = FormatMediaTime(m_mediaPlayer.GetCurrentTimeSec()) + L"  /  " + FormatMediaTime(m_mediaPlayer.GetDurationSec());
            D2D1_RECT_F timeR = D2D1::RectF(cardR.left + 16.0f, cardR.top + 74.0f, cardR.right - 16.0f, cardR.bottom - 20.0f);
            m_solidBrush->SetColor(HexColor(0x88C0D0));
            m_rt->DrawTextW(timeInfo.c_str(), static_cast<UINT32>(timeInfo.size()), m_fmtCenterSmall, timeR, m_solidBrush);
        }

        // Media Transport Bar at bottom of canvasR (Play/Pause + Interactive Seek Bar + Timestamp)
        D2D1_RECT_F barR = D2D1::RectF(canvasR.left + 20.0f, canvasR.bottom - 48.0f, canvasR.right - 20.0f, canvasR.bottom - 14.0f);
        m_solidBrush->SetColor(HexColor(0x111317, 0.90f));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(barR, 5.0f, 5.0f), m_solidBrush);

        D2D1_RECT_F playBtnR = D2D1::RectF(barR.left + 8.0f, barR.top + 4.0f, barR.left + 44.0f, barR.bottom - 4.0f);
        m_ribbonHits.push_back({ CMD_MEDIA_PLAY_PAUSE, playBtnR });
        m_solidBrush->SetColor(m_hoverRibbonBtn == CMD_MEDIA_PLAY_PAUSE ? HexColor(0x0078D7) : HexColor(0x2B303C));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(playBtnR, 4.0f, 4.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        const wchar_t* playGlyph = m_mediaPlayer.IsPlaying() ? L"\x23F8" : L"\x25B6";
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(playGlyph, 1, m_fmtSmall, playBtnR, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        // Seek bar track
        double pos = m_mediaPlayer.GetCurrentTimeSec();
        double dur = m_mediaPlayer.GetDurationSec();
        float frac = (dur > 0.05) ? static_cast<float>(std::clamp(pos / dur, 0.0, 1.0)) : 0.0f;

        m_mediaSeekBarRect = D2D1::RectF(playBtnR.right + 14.0f, barR.top + 8.0f, barR.right - 115.0f, barR.bottom - 8.0f);
        float trackY = (m_mediaSeekBarRect.top + m_mediaSeekBarRect.bottom) * 0.5f;
        D2D1_RECT_F trackBg = D2D1::RectF(m_mediaSeekBarRect.left, trackY - 3.0f, m_mediaSeekBarRect.right, trackY + 3.0f);
        m_solidBrush->SetColor(HexColor(0x3B4252));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(trackBg, 3.0f, 3.0f), m_solidBrush);

        float fillX = m_mediaSeekBarRect.left + frac * std::max(0.0f, m_mediaSeekBarRect.right - m_mediaSeekBarRect.left);
        D2D1_RECT_F trackFill = D2D1::RectF(m_mediaSeekBarRect.left, trackY - 3.0f, fillX, trackY + 3.0f);
        m_solidBrush->SetColor(HexColor(0x38A1F3));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(trackFill, 3.0f, 3.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, trackY), 5.5f, 5.5f), m_solidBrush);

        // Timestamp text on right side of transport bar
        std::wstring tsStr = FormatMediaTime(pos) + L" / " + FormatMediaTime(dur);
        D2D1_RECT_F tsR = D2D1::RectF(m_mediaSeekBarRect.right + 10.0f, barR.top, barR.right - 8.0f, barR.bottom);
        m_solidBrush->SetColor(HexColor(0xD8DEE9));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(tsStr.c_str(), static_cast<UINT32>(tsStr.size()), m_fmtSmall, tsR, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    } else if (!curPath.empty() && !isMediaItem) {
        FullImageMeta fullMeta = m_imageCache.GetOrQueueFullImage(m_rt, curPath, prevPath, nextPath);
        ID2D1Bitmap* bmpToDraw = fullMeta.bitmap;
        imgOrigW = fullMeta.origWidth;
        imgOrigH = fullMeta.origHeight;

        if (!bmpToDraw) {
            bmpToDraw = m_imageCache.GetOrQueueThumbnail(m_rt, curPath);
        }

        if (bmpToDraw) {
            D2D1_SIZE_F bmpSz = bmpToDraw->GetSize();
            float dispW = (imgOrigW > 0) ? static_cast<float>(imgOrigW) : bmpSz.width;
            float dispH = (imgOrigH > 0) ? static_cast<float>(imgOrigH) : bmpSz.height;

            float availW = std::max(40.0f, canvasR.right - canvasR.left - 24.0f);
            float availH = std::max(40.0f, canvasR.bottom - canvasR.top - 24.0f);

            float baseScale = 1.0f;
            if (!m_viewerActualPixels) {
                baseScale = std::min(1.0f, std::min(availW / std::max(1.0f, dispW), availH / std::max(1.0f, dispH)));
            }
            float finalScale = baseScale * m_viewerZoom;
            effectiveZoomPct = finalScale * 100.0f;

            float drawW = dispW * finalScale;
            float drawH = dispH * finalScale;
            float cx = (canvasR.left + canvasR.right) * 0.5f + m_viewerPanX;
            float cy = (canvasR.top + canvasR.bottom) * 0.5f + m_viewerPanY;

            D2D1_RECT_F destR = D2D1::RectF(cx - drawW * 0.5f, cy - drawH * 0.5f, cx + drawW * 0.5f, cy + drawH * 0.5f);
            m_rt->DrawBitmap(
                bmpToDraw,
                destR,
                1.0f,
                (finalScale >= 2.0f) ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

            if (activeIdx >= 0 && activeIdx < static_cast<int>(m_items.size()) &&
                m_imageCache.IsPathRotating(m_items[activeIdx].normKey)) {
                float mcx = (canvasR.left + canvasR.right) * 0.5f;
                float mcy = (canvasR.top + canvasR.bottom) * 0.5f;
                D2D1_RECT_F pillR = D2D1::RectF(mcx - 95.0f, mcy - 18.0f, mcx + 95.0f, mcy + 18.0f);
                m_solidBrush->SetColor(HexColor(0x0067B8, 0.94f));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(pillR, 6.0f, 6.0f), m_solidBrush);
                m_solidBrush->SetColor(HexColor(0xFFFFFF));
                const wchar_t* rotTxt = (m_lang == Lang::PL) ? L"\x21BB Obracanie..." : L"\x21BB Rotating...";
                m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                m_rt->DrawTextW(rotTxt, static_cast<UINT32>(wcslen(rotTxt)), m_fmtRegular, pillR, m_solidBrush);
                m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
        }
    } else {
        m_solidBrush->SetColor(HexColor(0x9BA3B0));
        m_fmtCenterSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_rt->DrawTextW(s.viewerNoImages, static_cast<UINT32>(wcslen(s.viewerNoImages)), m_fmtCenterSmall, canvasR, m_solidBrush);
    }

    // Bottom hint overlay inside canvas
    if (!isMediaItem) {
        D2D1_RECT_F hintR = D2D1::RectF(canvasR.left + 12.0f, canvasR.bottom - 24.0f, canvasR.right - 12.0f, canvasR.bottom - 4.0f);
        m_solidBrush->SetColor(HexColor(0x8A94A6));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(s.viewerHelpHint, static_cast<UINT32>(wcslen(s.viewerHelpHint)), m_fmtSmall, hintR, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    m_rt->PopAxisAlignedClip();

    // 2. Top Viewer HUD Bar (File info + Quick Controls: Prev, Next, Delete, Perm Delete, Fit, 1:1, Fullscreen, Back)
    D2D1_RECT_F hudBar = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + hudTopH);
    m_solidBrush->SetColor(HexColor(0x111317));
    m_rt->FillRectangle(hudBar, m_solidBrush);

    if (activeIdx >= 0 && activeIdx < static_cast<int>(m_items.size())) {
        const FileEntry& cur = m_items[activeIdx];
        wchar_t infoBuf[512] = {};
        if (imgOrigW > 0 && imgOrigH > 0) {
            swprintf_s(infoBuf, L"[%d / %d]   %s   \x2022   %u \xD7 %u px   \x2022   %s   \x2022   %.0f%%",
                activeIdx + 1, static_cast<int>(m_items.size()),
                cur.name.c_str(), imgOrigW, imgOrigH, cur.sizeStr.c_str(), effectiveZoomPct);
        } else {
            swprintf_s(infoBuf, L"[%d / %d]   %s   \x2022   %s",
                activeIdx + 1, static_cast<int>(m_items.size()),
                cur.name.c_str(), cur.sizeStr.c_str());
        }
        D2D1_RECT_F infoR = D2D1::RectF(hudBar.left + 14.0f, hudBar.top, hudBar.right - 430.0f, hudBar.bottom);
        m_solidBrush->SetColor(HexColor(0xECEFF4));
        m_rt->DrawTextW(infoBuf, static_cast<UINT32>(wcslen(infoBuf)), m_fmtViewerHud, infoR, m_solidBrush);
    }

    auto drawHudBtn = [&](int cmdId, float rightOffset, float w, const wchar_t* label, uint32_t bgRgb = 0x262B33) {
        D2D1_RECT_F br = D2D1::RectF(hudBar.right - rightOffset - w, hudBar.top + 6.0f, hudBar.right - rightOffset, hudBar.bottom - 6.0f);
        m_ribbonHits.push_back({ cmdId, br });
        m_solidBrush->SetColor(m_hoverRibbonBtn == cmdId ? HexColor(0x3B4252) : HexColor(bgRgb));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xE5E9F0));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), m_fmtSmall, br, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    };

    drawHudBtn(CMD_VIEWER_CLOSE,       8.0f,   56.0f, m_lang == Lang::PL ? L"Zamknij" : L"Close", 0x3B4252);
    drawHudBtn(CMD_VIEWER_FULLSCREEN,  68.0f,  72.0f, m_isFullScreen ? (m_lang == Lang::PL ? L"W oknie" : L"Window") : L"F11 Full");
    drawHudBtn(CMD_VIEWER_ZOOM_100,    144.0f, 42.0f, L"1:1");
    drawHudBtn(CMD_VIEWER_ZOOM_FIT,    190.0f, 48.0f, m_lang == Lang::PL ? L"Dopasuj" : L"Fit");
    drawHudBtn(CMD_VIEWER_DELETE,      244.0f, 78.0f, m_lang == Lang::PL ? L"Usuń (Del)" : L"Del (Bin)", 0x6E2B2B);
    drawHudBtn(CMD_VIEWER_NEXT,        328.0f, 34.0f, L"\x2192");
    drawHudBtn(CMD_VIEWER_PREV,        366.0f, 34.0f, L"\x2190");
    if (activeIdx >= 0 && activeIdx < static_cast<int>(m_items.size()) && m_items[activeIdx].isImage) {
        drawHudBtn(CMD_IMG_TOOL_MENU,  406.0f, 108.0f, m_lang == Lang::PL ? L"\xD83D\xDCD0 Rozmiar / KB \x25BE" : L"\xD83D\xDCD0 Resize / KB \x25BE", 0x1E3A5F);
    }

    // 3. Bottom Thumbnail Filmstrip (in In-Window Full Viewer mode)
    if (stripH > 0.0f) {
        D2D1_RECT_F stripR = D2D1::RectF(rect.left, rect.bottom - stripH, rect.right, rect.bottom);
        m_solidBrush->SetColor(HexColor(0x111317));
        m_rt->FillRectangle(stripR, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x2A2E37));
        m_rt->DrawLine(D2D1::Point2F(stripR.left, stripR.top), D2D1::Point2F(stripR.right, stripR.top), m_solidBrush, 1.0f);

        m_rt->PushAxisAlignedClip(stripR, D2D1_ANTIALIAS_MODE_ALIASED);

        float thumbStep = 74.0f;
        int firstIdx = std::max(0, static_cast<int>(m_filmstripScrollX / thumbStep));
        int visCount = static_cast<int>((stripR.right - stripR.left) / thumbStep) + 3;
        int lastIdx = std::min(static_cast<int>(m_items.size()) - 1, firstIdx + visCount);

        for (int i = firstIdx; i <= lastIdx; ++i) {
            const FileEntry& item = m_items[i];
            float tx = stripR.left + 10.0f + i * thumbStep - m_filmstripScrollX;
            D2D1_RECT_F cellR = D2D1::RectF(tx, stripR.top + 6.0f, tx + 68.0f, stripR.bottom - 6.0f);

            if (i == activeIdx) {
                m_solidBrush->SetColor(HexColor(0x0078D7, 0.45f));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(cellR, 3.0f, 3.0f), m_solidBrush);
                m_solidBrush->SetColor(HexColor(0x38A1F3));
                m_rt->DrawRoundedRectangle(D2D1::RoundedRect(cellR, 3.0f, 3.0f), m_solidBrush, 2.0f);
            }

            D2D1_RECT_F iconR = D2D1::RectF(cellR.left + 6.0f, cellR.top + 4.0f, cellR.right - 6.0f, cellR.bottom - 16.0f);
            if (item.isDirectory) {
                DrawFolderVectorIcon(iconR, false);
            } else if (item.isImage || item.isVideo) {
                ID2D1Bitmap* tb = m_imageCache.GetOrQueueThumbnail(m_rt, item.fullPath);
                if (tb) {
                    D2D1_SIZE_F bsz = tb->GetSize();
                    D2D1_RECT_F dr = PathUtils::FitRectCentered(iconR, bsz.width, bsz.height);
                    m_rt->DrawBitmap(tb, dr);
                }
            } else {
                DrawRibbonIcon(CMD_NONE, iconR);
            }

            D2D1_RECT_F nameR = D2D1::RectF(cellR.left + 2.0f, cellR.bottom - 15.0f, cellR.right - 2.0f, cellR.bottom);
            m_solidBrush->SetColor(HexColor(0xD8DEE9));
            m_rt->DrawTextW(item.displayName.c_str(), static_cast<UINT32>(item.displayName.size()), m_fmtSmall, nameR, m_solidBrush);
        }

        m_rt->PopAxisAlignedClip();
    }
}

void ExplorerApp::DrawPreviewPane(const D2D1_RECT_F& rect) {
    m_contentPane.Draw(
        m_rt,
        m_solidBrush,
        m_fmtRegular,
        m_fmtSmall,
        m_fmtBold,
        m_fmtCenterSmall,
        m_imageCache,
        m_mediaPlayer,
        rect,
        m_draggingPreviewSplitter,
        m_lang,
        m_mousePt,
        m_mediaSeekBarRect);
}

void ExplorerApp::DrawStatusBar(const D2D1_RECT_F& rect) {
    const Strings& s = GetStrings(m_lang);

    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRectangle(rect, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xE5E5E5));
    m_rt->DrawLine(D2D1::Point2F(rect.left, rect.top), D2D1::Point2F(rect.right, rect.top), m_solidBrush, 1.0f);

    uint64_t selBytes = 0;
    int selCount = GetSelectedCount(&selBytes);

    std::wstring statusText = std::wstring(s.statusItems) + std::to_wstring(m_items.size()) + L"   |   ";
    if (selCount > 0) {
        statusText += std::wstring(s.statusSelected) + std::to_wstring(selCount);
        if (selBytes > 0) {
            statusText += L" (" + FileEngine::FormatSizeDetailed(selBytes) + L")";
        }
    }

    // Background delete job state: stays visible until the worker thread has really finished
    size_t pendingDeletes = m_fileEngine.PendingDeleteBatches();
    if (pendingDeletes > 0) {
        int dots = static_cast<int>((GetTickCount64() / 400) % 4);
        statusText += L"     \x2022     \x23F3 " + std::wstring(m_lang == Lang::PL ? L"Usuwanie w tle" : L"Deleting in background") +
                      (pendingDeletes > 1 ? L" (" + std::to_wstring(pendingDeletes) + L")" : L"") + std::wstring(static_cast<size_t>(dots), L'.');
    }

    // Rename job in flight (the worker thread reports back with a verified result)
    if (m_renamePending.load() > 0) {
        int dots = static_cast<int>((GetTickCount64() / 400) % 4);
        statusText += L"     \x2022     \x23F3 " + std::wstring(m_lang == Lang::PL ? L"Zmiana nazwy: " : L"Renaming: ") + m_renamePendingText +
                      std::wstring(static_cast<size_t>(dots), L'.');
    }

    // Background folder merge job in flight
    if (m_mergePending.load() > 0) {
        int dots = static_cast<int>((GetTickCount64() / 400) % 4);
        int moved = m_mergeMovedCount.load();
        statusText += L"     \x2022     \x23F3 " + std::wstring(m_lang == Lang::PL ? L"Scalanie folderów w tle: " : L"Merging folders in background: ") +
                      m_mergePendingText +
                      (moved > 0 ? (L" (" + std::to_wstring(moved) + L")") : L"") +
                      std::wstring(static_cast<size_t>(dots), L'.');
    }

    // Append active status toast (e.g. "Przeniesiono do Kosza (Ctrl+Z aby cofnąć)")
    if (!m_statusToast.empty() && GetTickCount64() < m_statusToastExpireTick) {
        statusText += L"     \x2022     " + m_statusToast;
    }

    D2D1_RECT_F textR = D2D1::RectF(rect.left + 10.0f, rect.top, rect.right - 90.0f, rect.bottom);
    uint32_t statusCol = 0x333333;
    if (m_renamePending.load() > 0 || m_mergePending.load() > 0) {
        statusCol = 0x0067B8;
    } else if (!m_statusToast.empty() && GetTickCount64() < m_statusToastExpireTick) {
        if (m_statusToast[0] == L'\x2717') statusCol = 0xB91C1C;        // failed
        else if (m_statusToast[0] == L'\x2713') statusCol = 0x15803D;   // done
        else if (m_statusToast[0] == L'\x26A0') statusCol = 0xB45309;   // warning
    }
    m_solidBrush->SetColor(HexColor(statusCol));
    m_rt->DrawTextW(statusText.c_str(), static_cast<UINT32>(statusText.size()), m_fmtSmall, textR, m_solidBrush);

    // Bottom-right quick view-mode buttons (matching Win10 screenshot + 3rd Full Viewer button!)
    auto drawBottomViewBtn = [&](int cmdId, float rightOffset, bool active, int iconKind) {
        D2D1_RECT_F br = D2D1::RectF(rect.right - rightOffset - 22.0f, rect.top + 1.0f, rect.right - rightOffset, rect.bottom - 1.0f);
        m_ribbonHits.push_back({ cmdId, br });
        if (active || m_hoverRibbonBtn == cmdId) {
            m_solidBrush->SetColor(active ? HexColor(0xCCE8FF) : HexColor(0xE5F3FB));
            m_rt->FillRectangle(br, m_solidBrush);
            m_solidBrush->SetColor(HexColor(0x70C0E7));
            m_rt->DrawRectangle(br, m_solidBrush, 1.0f);
        }

        m_solidBrush->SetColor(HexColor(0x454545));
        if (iconKind == 0) {
            // Details icon (3 horizontal lines)
            for (int r = 0; r < 3; ++r) {
                float ly = br.top + 6.0f + r * 4.5f;
                m_rt->DrawLine(D2D1::Point2F(br.left + 4.0f, ly), D2D1::Point2F(br.right - 4.0f, ly), m_solidBrush, 1.4f);
            }
        } else if (iconKind == 1) {
            // Large Thumbnails icon (2x2 grid)
            m_rt->DrawRectangle(D2D1::RectF(br.left + 4.0f, br.top + 4.0f, br.left + 10.0f, br.top + 10.0f), m_solidBrush, 1.2f);
            m_rt->DrawRectangle(D2D1::RectF(br.left + 12.0f, br.top + 4.0f, br.right - 4.0f, br.top + 10.0f), m_solidBrush, 1.2f);
            m_rt->DrawRectangle(D2D1::RectF(br.left + 4.0f, br.top + 12.0f, br.left + 10.0f, br.bottom - 4.0f), m_solidBrush, 1.2f);
            m_rt->DrawRectangle(D2D1::RectF(br.left + 12.0f, br.top + 12.0f, br.right - 4.0f, br.bottom - 4.0f), m_solidBrush, 1.2f);
        } else if (iconKind == 3) {
            // Proportions Treemap icon (3 colored blocks)
            m_solidBrush->SetColor(HexColor(0x1E88E5));
            m_rt->FillRectangle(D2D1::RectF(br.left + 3.0f, br.top + 4.0f, br.left + 11.0f, br.bottom - 4.0f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(0x43A047));
            m_rt->FillRectangle(D2D1::RectF(br.left + 12.0f, br.top + 4.0f, br.right - 3.0f, br.top + 11.0f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(0xFB8C00));
            m_rt->FillRectangle(D2D1::RectF(br.left + 12.0f, br.top + 12.0f, br.right - 3.0f, br.bottom - 4.0f), m_solidBrush);
        } else if (iconKind == 4) {
            // Pie Chart icon (circle with sectors)
            D2D1_POINT_2F c = D2D1::Point2F((br.left + br.right) * 0.5f, (br.top + br.bottom) * 0.5f);
            m_solidBrush->SetColor(HexColor(0x1E88E5));
            m_rt->FillEllipse(D2D1::Ellipse(c, 6.5f, 6.5f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(0xFFFFFF));
            m_rt->DrawLine(c, D2D1::Point2F(c.x, c.y - 6.5f), m_solidBrush, 1.3f);
            m_rt->DrawLine(c, D2D1::Point2F(c.x + 5.5f, c.y + 3.2f), m_solidBrush, 1.3f);
            m_rt->FillEllipse(D2D1::Ellipse(c, 2.2f, 2.2f), m_solidBrush);
        } else if (iconKind == 5) {
            // Split View icon (two side-by-side panels)
            D2D1_RECT_F spR = D2D1::RectF(br.left + 3.0f, br.top + 4.0f, br.right - 3.0f, br.bottom - 4.0f);
            float spMid = (spR.left + spR.right) * 0.5f;
            m_rt->DrawRectangle(spR, m_solidBrush, 1.3f);
            m_rt->DrawLine(D2D1::Point2F(spMid, spR.top), D2D1::Point2F(spMid, spR.bottom), m_solidBrush, 1.3f);
        } else if (iconKind == 6) {
            // Right-Side File Content Pane icon (left list + right content lines)
            D2D1_RECT_F spR = D2D1::RectF(br.left + 3.0f, br.top + 4.0f, br.right - 3.0f, br.bottom - 4.0f);
            float spSplit = spR.left + (spR.right - spR.left) * 0.42f;
            m_solidBrush->SetColor(active ? HexColor(0x93C5FD, 0.55f) : HexColor(0xE2E8F0));
            m_rt->FillRectangle(D2D1::RectF(spSplit, spR.top + 0.5f, spR.right - 0.5f, spR.bottom - 0.5f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(0x454545));
            m_rt->DrawRectangle(spR, m_solidBrush, 1.3f);
            m_rt->DrawLine(D2D1::Point2F(spSplit, spR.top), D2D1::Point2F(spSplit, spR.bottom), m_solidBrush, 1.2f);
            for (int ln = 0; ln < 3; ++ln) {
                float ly = spR.top + 3.2f + ln * 3.2f;
                if (ly < spR.bottom - 1.5f) {
                    m_rt->DrawLine(D2D1::Point2F(spSplit + 2.0f, ly), D2D1::Point2F(spR.right - 1.8f, ly), m_solidBrush, 1.1f);
                }
            }
        } else {
            // Full Viewer icon (maximised photo frame with center dot)
            m_rt->DrawRectangle(D2D1::RectF(br.left + 3.0f, br.top + 4.0f, br.right - 3.0f, br.bottom - 4.0f), m_solidBrush, 1.4f);
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F((br.left + br.right) * 0.5f, (br.top + br.bottom) * 0.5f), 2.5f, 2.5f), m_solidBrush);
        }
    };

    bool isThumbs = (m_viewMode == ViewMode::MediumIcons || m_viewMode == ViewMode::LargeIcons ||
                     m_viewMode == ViewMode::ExtraLargeIcons || m_viewMode == ViewMode::JumboIcons);
    drawBottomViewBtn(CMD_TOGGLE_SPLIT_VIEW,  148.0f, m_splitView,                         5);
    drawBottomViewBtn(CMD_TOGGLE_PREVIEWPANE, 124.0f, m_showPreviewPane,                   6);
    drawBottomViewBtn(CMD_VIEW_DETAILS,       100.0f, m_viewMode == ViewMode::Details,     0);
    drawBottomViewBtn(CMD_VIEW_EXTRALARGE,     76.0f, isThumbs,                            1);
    drawBottomViewBtn(CMD_VIEW_PROPORTIONS,    52.0f, m_viewMode == ViewMode::Proportions, 3);
    drawBottomViewBtn(CMD_VIEW_PIECHART,       28.0f, m_viewMode == ViewMode::PieChart,    4);
    drawBottomViewBtn(CMD_VIEW_FULLVIEWER,      4.0f, m_viewMode == ViewMode::FullViewer,  2);
}

void ExplorerApp::DrawOperationToastBanner(const D2D1_RECT_F& mainRect) {
    if (m_viewMode == ViewMode::Hardware || m_viewMode == ViewMode::FullViewer) return;

    const bool pl = (m_lang == Lang::PL);
    const int dots = static_cast<int>((GetTickCount64() / 350) % 4);
    std::wstring text;
    uint32_t accent = 0x0284C7;
    uint32_t bg = 0xF0F9FF;
    uint32_t fg = 0x0F172A;

    if (m_mergePending.load() > 0) {
        int moved = m_mergeMovedCount.load();
        text = L"\x23F3 " + std::wstring(pl ? L"Scalanie folderów w tle: " : L"Merging folders in background: ") +
               m_mergePendingText +
               (moved > 0 ? (pl ? (L"  (przeniesiono " + std::to_wstring(moved) + L" el.)") : (L"  (moved " + std::to_wstring(moved) + L")")) : L"") +
               std::wstring(static_cast<size_t>(dots), L'.');
        accent = 0x0284C7;
        bg = 0xF0F9FF;
        fg = 0x0C4A6E;
    } else if (m_renamePending.load() > 0) {
        text = L"\x23F3 " + std::wstring(pl ? L"Zmiana nazwy: " : L"Renaming: ") + m_renamePendingText +
               std::wstring(static_cast<size_t>(dots), L'.');
        accent = 0x0284C7;
        bg = 0xF0F9FF;
        fg = 0x0C4A6E;
    } else if (!m_statusToast.empty() && GetTickCount64() < m_statusToastExpireTick) {
        text = m_statusToast;
        if (text[0] == L'\x2717') {
            accent = 0xDC2626;
            bg = 0xFEF2F2;
            fg = 0x991B1B;
        } else if (text[0] == L'\x2713') {
            accent = 0x16A34A;
            bg = 0xF0FDF4;
            fg = 0x14532D;
        } else if (text[0] == L'\x26A0') {
            accent = 0xD97706;
            bg = 0xFFFBEB;
            fg = 0x78350F;
        } else {
            accent = 0x0284C7;
            bg = 0xF8FAFC;
            fg = 0x0F172A;
        }
    } else {
        return;
    }

    const float maxW = std::max(260.0f, (mainRect.right - mainRect.left) - 36.0f);
    const float desiredW = std::clamp(static_cast<float>(text.size()) * 7.2f + 36.0f, 280.0f, std::min(760.0f, maxW));
    const float bannerH = 32.0f;
    const float rightEdge = mainRect.right - 22.0f;
    const float bottomEdge = mainRect.bottom - 10.0f;
    if (bottomEdge - bannerH < mainRect.top + 30.0f) return;

    D2D1_RECT_F card = D2D1::RectF(rightEdge - desiredW, bottomEdge - bannerH, rightEdge, bottomEdge);
    D2D1_RECT_F shadow = D2D1::RectF(card.left + 1.0f, card.top + 2.0f, card.right + 1.0f, card.bottom + 2.0f);
    m_solidBrush->SetColor(HexColor(0x0F172A, 0.10f));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(shadow, 5.0f, 5.0f), m_solidBrush);

    m_solidBrush->SetColor(HexColor(bg, 0.97f));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(card, 5.0f, 5.0f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(accent, 0.55f));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(card, 5.0f, 5.0f), m_solidBrush, 1.2f);

    D2D1_RECT_F bar = D2D1::RectF(card.left, card.top, card.left + 5.0f, card.bottom);
    m_solidBrush->SetColor(HexColor(accent));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(bar, 3.0f, 3.0f), m_solidBrush);

    DrawClippedText(text, D2D1::RectF(card.left + 14.0f, card.top + 6.0f, card.right - 12.0f, card.bottom - 4.0f), m_fmtBold, fg);
}

void ExplorerApp::DrawPieChartView(const D2D1_RECT_F& rect) {
    m_pieHits.clear();
    m_treemapHits.clear();

    m_rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Clean light Windows Explorer surface background
    m_solidBrush->SetColor(HexColor(0xF8FAFC));
    m_rt->FillRectangle(rect, m_solidBrush);

    if (m_items.empty()) {
        m_solidBrush->SetColor(HexColor(0x64748B));
        m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        D2D1_RECT_F emptyR = D2D1::RectF(rect.left, (rect.top + rect.bottom) * 0.5f - 15.0f, rect.right, (rect.top + rect.bottom) * 0.5f + 15.0f);
        const wchar_t* msg = (m_lang == Lang::PL) ? L"Ten folder jest pusty" : L"This folder is empty";
        m_rt->DrawTextW(msg, static_cast<UINT32>(wcslen(msg)), m_fmtRegular, emptyR, m_solidBrush);
        m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_rt->PopAxisAlignedClip();
        return;
    }

    // Palette for distinct top-level folders so neighboring pie slices stand out clearly
    static const uint32_t kFolderPalette[] = {
        0x1E88E5, 0x00ACC1, 0x43A047, 0xFB8C00, 0x8E24AA, 0xE53935,
        0x3949AB, 0x00897B, 0x7CB342, 0xF4511E, 0x5E35B1, 0x039BE5
    };

    struct SortItem {
        int index;
        uint64_t bytes;
        bool isPending;
        uint32_t baseColor;
    };
    std::vector<SortItem> sorted;
    sorted.reserve(m_items.size());

    uint64_t totalBytes = 0;
    int totalFolders = 0;
    int doneFolders = 0;

    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        const auto& it = m_items[i];
        bool inFlight = it.isDirectory && !it.isDrive &&
                        (m_fileEngine.IsFolderInFlight(it.normKey) || it.sizeStr.empty() || it.sizeStr == L"...");
        if (it.isDirectory && !it.isDrive) {
            totalFolders++;
            if (!inFlight) doneFolders++;
        }
        uint64_t b = it.size;
        totalBytes += b;
        uint32_t col = it.isDirectory ? kFolderPalette[i % 12] : GetTreemapCategoryColor(it.ext, false, 0);
        sorted.push_back({ i, b, inFlight, col });
    }

    bool anyPending = m_fileEngine.IsAnyFolderSizeCalculating() || (doneFolders < totalFolders && totalFolders > 0);
    if (anyPending) {
        EnsureWorkAnimTimerState();
    }

    std::sort(sorted.begin(), sorted.end(), [](const SortItem& a, const SortItem& b) {
        if (a.bytes != b.bytes) return a.bytes > b.bytes;
        return a.index < b.index;
    });

    // 1. Top Header Bar (28px, White Explorer Style)
    D2D1_RECT_F hdrR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + 28.0f);
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRectangle(hdrR, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xDADADA));
    m_rt->DrawLine(D2D1::Point2F(hdrR.left, hdrR.bottom), D2D1::Point2F(hdrR.right, hdrR.bottom), m_solidBrush, 1.0f);

    std::wstring hdrTitle = (m_lang == Lang::PL ? L"Wykres kołowy (Sunburst 360\x00B0)  \x2022  Razem: " : L"Radial Pie Chart (Sunburst 360\x00B0)  \x2022  Total: ")
                            + FileEngine::FormatSizeDetailed(totalBytes);
    D2D1_RECT_F hdrTxtR = D2D1::RectF(hdrR.left + 10.0f, hdrR.top, hdrR.left + 440.0f, hdrR.bottom);
    m_solidBrush->SetColor(HexColor(0x1E293B));
    m_rt->DrawTextW(hdrTitle.c_str(), static_cast<UINT32>(hdrTitle.size()), m_fmtBold, hdrTxtR, m_solidBrush);

    // [↻ Przelicz] button & [Widok: Proporcje] switch button
    D2D1_RECT_F recalcR = D2D1::RectF(hdrR.left + 395.0f, hdrR.top + 3.0f, hdrR.left + 480.0f, hdrR.bottom - 3.0f);
    m_ribbonHits.push_back({ CMD_CALC_FOLDER_SIZES, recalcR });
    bool recalcHov = (m_hoverRibbonBtn == CMD_CALC_FOLDER_SIZES);
    m_solidBrush->SetColor(recalcHov ? HexColor(0x0078D7) : HexColor(0xF1F5F9));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(recalcR, 3.0f, 3.0f), m_solidBrush);
    m_solidBrush->SetColor(recalcHov ? HexColor(0x005A9E) : HexColor(0xCBD5E1));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(recalcR, 3.0f, 3.0f), m_solidBrush, 1.0f);
    m_solidBrush->SetColor(recalcHov ? HexColor(0xFFFFFF) : HexColor(0x1E293B));
    const wchar_t* recalcLbl = (m_lang == Lang::PL) ? L"\x21BB Przelicz" : L"\x21BB Rescan";
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_rt->DrawTextW(recalcLbl, static_cast<UINT32>(wcslen(recalcLbl)), m_fmtSmall, recalcR, m_solidBrush);

    D2D1_RECT_F propSwitchR = D2D1::RectF(hdrR.left + 486.0f, hdrR.top + 3.0f, hdrR.left + 596.0f, hdrR.bottom - 3.0f);
    m_ribbonHits.push_back({ CMD_VIEW_PROPORTIONS, propSwitchR });
    bool propHov = (m_hoverRibbonBtn == CMD_VIEW_PROPORTIONS);
    m_solidBrush->SetColor(propHov ? HexColor(0x0078D7) : HexColor(0xF1F5F9));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(propSwitchR, 3.0f, 3.0f), m_solidBrush);
    m_solidBrush->SetColor(propHov ? HexColor(0x005A9E) : HexColor(0xCBD5E1));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(propSwitchR, 3.0f, 3.0f), m_solidBrush, 1.0f);
    m_solidBrush->SetColor(propHov ? HexColor(0xFFFFFF) : HexColor(0x1E293B));
    const wchar_t* propLbl = (m_lang == Lang::PL) ? L"\x25A6 Mapa Proporcje" : L"\x25A6 Treemap View";
    m_rt->DrawTextW(propLbl, static_cast<UINT32>(wcslen(propLbl)), m_fmtSmall, propSwitchR, m_solidBrush);
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    float canvasTop = hdrR.bottom + 2.0f;
    float animPhase = static_cast<float>(GetTickCount64() % 100000ULL) * 0.001f;

    // 2. Prominent Animated Scanning Progress Banner when calculating subfolders (Light Amber)
    if (anyPending) {
        D2D1_RECT_F scanR = D2D1::RectF(rect.left + 4.0f, canvasTop, rect.right - 4.0f, canvasTop + 34.0f);
        canvasTop = scanR.bottom + 4.0f;

        m_solidBrush->SetColor(HexColor(0xFFFBEB));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(scanR, 4.0f, 4.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xF59E0B));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(scanR, 4.0f, 4.0f), m_solidBrush, 1.2f);

        float pct = (totalFolders > 0) ? (static_cast<float>(doneFolders) / static_cast<float>(totalFolders)) : 0.0f;
        D2D1_RECT_F barBg = D2D1::RectF(scanR.left + 8.0f, scanR.bottom - 7.5f, scanR.right - 8.0f, scanR.bottom - 2.5f);
        m_solidBrush->SetColor(HexColor(0xFDE68A));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(barBg, 2.0f, 2.0f), m_solidBrush);

        float fillW = std::max(24.0f, (barBg.right - barBg.left) * pct);
        D2D1_RECT_F barFg = D2D1::RectF(barBg.left, barBg.top, barBg.left + fillW, barBg.bottom);
        m_solidBrush->SetColor(HexColor(0xF59E0B));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(barFg, 2.0f, 2.0f), m_solidBrush);

        float spCx = scanR.left + 18.0f;
        float spCy = scanR.top + 13.0f;
        for (int s = 0; s < 8; ++s) {
            float a = animPhase * 7.0f + s * (3.14159265f * 0.25f);
            m_solidBrush->SetColor(HexColor(0xD97706, 0.2f + 0.8f * (s / 8.0f)));
            m_rt->DrawLine(D2D1::Point2F(spCx + std::cos(a) * 3.5f, spCy + std::sin(a) * 3.5f),
                           D2D1::Point2F(spCx + std::cos(a) * 7.5f, spCy + std::sin(a) * 7.5f), m_solidBrush, 1.8f);
        }

        std::wstring samplePath = m_fileEngine.GetActiveScanningSamplePath();
        if (samplePath.size() > 65) samplePath = L"..." + samplePath.substr(samplePath.size() - 62);

        wchar_t scanBuf[512] = {};
        int pctInt = static_cast<int>(pct * 100.0f + 0.5f);
        if (m_lang == Lang::PL) {
            swprintf_s(scanBuf, L"TRWA SKANOWANIE PODFOLDERÓW (%d / %d gotowe \x2022 %d%%)   \x2022   Dotychczas: %s%s%s",
                       doneFolders, totalFolders, pctInt,
                       FileEngine::FormatSizeDetailed(totalBytes).c_str(),
                       samplePath.empty() ? L"" : L"   \x2022   Teraz: ",
                       samplePath.c_str());
        } else {
            swprintf_s(scanBuf, L"SCANNING SUBFOLDERS (%d / %d done \x2022 %d%%)   \x2022   So far: %s%s%s",
                       doneFolders, totalFolders, pctInt,
                       FileEngine::FormatSizeDetailed(totalBytes).c_str(),
                       samplePath.empty() ? L"" : L"   \x2022   Now: ",
                       samplePath.c_str());
        }
        D2D1_RECT_F stR = D2D1::RectF(scanR.left + 32.0f, scanR.top + 2.0f, scanR.right - 10.0f, scanR.top + 22.0f);
        m_solidBrush->SetColor(HexColor(0x92400E));
        m_rt->DrawTextW(scanBuf, static_cast<UINT32>(wcslen(scanBuf)), m_fmtBold, stR, m_solidBrush);
    }

    D2D1_RECT_F bodyR = D2D1::RectF(rect.left + 6.0f, canvasTop, rect.right - 6.0f, rect.bottom - 26.0f);
    float totalW = std::max(100.0f, bodyR.right - bodyR.left);
    float totalH = std::max(100.0f, bodyR.bottom - bodyR.top);

    // Split into Left Sunburst Chart area (58%) and Right Ranked Legend List (42%)
    float chartW = std::max(260.0f, totalW * 0.58f);
    D2D1_RECT_F chartArea = D2D1::RectF(bodyR.left, bodyR.top, bodyR.left + chartW, bodyR.bottom);
    D2D1_RECT_F listArea  = D2D1::RectF(bodyR.left + chartW + 8.0f, bodyR.top, bodyR.right, bodyR.bottom);

    float cx = (chartArea.left + chartArea.right) * 0.5f;
    float cy = (chartArea.top + chartArea.bottom) * 0.5f;
    float maxRadius = std::max(80.0f, std::min(chartW, totalH) * 0.46f);

    float hubR = maxRadius * 0.25f;
    m_pieCenterPt = D2D1::Point2F(cx, cy);
    m_pieCenterRadius = hubR;

    float ringInner[4] = { maxRadius * 0.265f, maxRadius * 0.535f, maxRadius * 0.740f, maxRadius * 0.895f };
    float ringOuter[4] = { maxRadius * 0.525f, maxRadius * 0.730f, maxRadius * 0.885f, maxRadius * 1.000f };

    // Helper to draw an annular pie sector using deg [0..360] where 0 deg = 12 o'clock (-90 deg rad)
    auto drawAnnularSectorDeg = [&](float rIn, float rOut, float startDeg, float sweepDeg, uint32_t fillHex, float fillAlpha, uint32_t strokeHex, float strokeWidth) {
        if (!m_d2dFactory || sweepDeg <= 0.05f || rOut <= rIn) return;
        float spanDeg = std::min(sweepDeg, 359.92f);
        float a0 = (startDeg - 90.0f) * (3.1415926535f / 180.0f);
        float a1 = (startDeg + spanDeg - 90.0f) * (3.1415926535f / 180.0f);

        ID2D1PathGeometry* path = nullptr;
        if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(&path)) && path) {
            ID2D1GeometrySink* sink = nullptr;
            if (SUCCEEDED(path->Open(&sink)) && sink) {
                D2D1_POINT_2F pOut0 = D2D1::Point2F(cx + std::cos(a0) * rOut, cy + std::sin(a0) * rOut);
                D2D1_POINT_2F pOut1 = D2D1::Point2F(cx + std::cos(a1) * rOut, cy + std::sin(a1) * rOut);
                D2D1_POINT_2F pIn1  = D2D1::Point2F(cx + std::cos(a1) * rIn,  cy + std::sin(a1) * rIn);
                D2D1_POINT_2F pIn0  = D2D1::Point2F(cx + std::cos(a0) * rIn,  cy + std::sin(a0) * rIn);

                D2D1_ARC_SIZE arcSz = (spanDeg > 180.0f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;

                sink->BeginFigure(pOut0, D2D1_FIGURE_BEGIN_FILLED);
                sink->AddArc(D2D1::ArcSegment(pOut1, D2D1::SizeF(rOut, rOut), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, arcSz));
                sink->AddLine(pIn1);
                sink->AddArc(D2D1::ArcSegment(pIn0, D2D1::SizeF(rIn, rIn), 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, arcSz));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                sink->Release();
            }
            if (fillAlpha > 0.0f) {
                m_solidBrush->SetColor(HexColor(fillHex, fillAlpha));
                m_rt->FillGeometry(path, m_solidBrush);
            }
            if (strokeWidth > 0.0f) {
                m_solidBrush->SetColor(HexColor(strokeHex, 0.95f));
                m_rt->DrawGeometry(path, m_solidBrush, strokeWidth);
            }
            path->Release();
        }
    };

    // Subtle light background guide rings
    for (int r = 0; r < 4; ++r) {
        m_solidBrush->SetColor(HexColor(0xE2E8F0));
        m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), ringOuter[r], ringOuter[r]), m_solidBrush, 1.0f);
    }

    // Recursive helper for rings 1..3 using m_fileEngine.GetFolderTreemapChildren
    std::function<void(const std::vector<TreemapChildNode>&, int, const std::wstring&, float, float, uint64_t, int, uint32_t)> renderChildRing;
    renderChildRing = [&](const std::vector<TreemapChildNode>& nodes, int topItemIdx, const std::wstring& relPrefix, float startDeg, float sweepDeg, uint64_t parentBytes, int depth, uint32_t parentColor) {
        if (depth > 3 || nodes.empty() || parentBytes == 0 || sweepDeg <= 0.8f) return;
        float rIn  = ringInner[depth];
        float rOut = ringOuter[depth];
        float curDeg = startDeg;

        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto& nd = nodes[i];
            if (nd.size == 0) continue;
            float frac = static_cast<float>(static_cast<double>(nd.size) / static_cast<double>(parentBytes));
            float spanDeg = sweepDeg * frac;
            if (spanDeg < 0.45f) continue;

            uint32_t col = nd.isDirectory ? parentColor : GetTreemapCategoryColor(nd.ext, false, depth);
            float alpha = 0.88f - depth * 0.10f;

            drawAnnularSectorDeg(rIn, rOut, curDeg, spanDeg, col, alpha, 0xFFFFFF, 1.2f);

            std::wstring childRel = relPrefix + L"\\" + nd.name;
            PieSectorHitNode hit;
            hit.startAngleDeg = curDeg;
            hit.sweepAngleDeg = spanDeg;
            hit.innerRadius = rIn;
            hit.outerRadius = rOut;
            hit.topItemIndex = topItemIdx;
            hit.depth = depth;
            hit.color = col;
            hit.size = nd.size;
            hit.isDirectory = nd.isDirectory;
            hit.name = nd.name;
            hit.relPath = childRel;
            hit.fullPath = nd.fullPath;
            m_pieHits.push_back(hit);

            if (spanDeg > 14.0f && (rOut - rIn) >= 18.0f) {
                float midRad = (curDeg + spanDeg * 0.5f - 90.0f) * (3.1415926535f / 180.0f);
                float midR = (rIn + rOut) * 0.5f;
                float lx = cx + std::cos(midRad) * midR;
                float ly = cy + std::sin(midRad) * midR;
                D2D1_RECT_F lblR = D2D1::RectF(lx - 44.0f, ly - 9.0f, lx + 44.0f, ly + 9.0f);
                m_solidBrush->SetColor(HexColor(0xFFFFFF, 0.88f));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(lblR, 3.0f, 3.0f), m_solidBrush);
                m_solidBrush->SetColor(HexColor(0xCBD5E1));
                m_rt->DrawRoundedRectangle(D2D1::RoundedRect(lblR, 3.0f, 3.0f), m_solidBrush, 0.8f);
                m_solidBrush->SetColor(HexColor(0x0F172A));
                m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                m_rt->DrawTextW(nd.name.c_str(), static_cast<UINT32>(nd.name.size()), m_fmtSmall, lblR, m_solidBrush);
                m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }

            if (nd.isDirectory && depth < 3 && !nd.normKey.empty()) {
                std::vector<TreemapChildNode> subChildren;
                if (m_fileEngine.GetFolderTreemapChildren(nd.normKey, subChildren) && !subChildren.empty()) {
                    renderChildRing(subChildren, topItemIdx, childRel, curDeg, spanDeg, nd.size, depth + 1, col);
                }
            }
            curDeg += spanDeg;
        }
    };

    // Render Ring 0 (Top-level items) with 100% true angular proportions [0..360 deg]
    float curDeg = 0.0f;
    uint64_t denomBytes = std::max<uint64_t>(1, totalBytes);

    uint64_t smallAggBytes = 0;
    int smallAggCount = 0;
    int firstSmallIdx = -1;

    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& si = sorted[i];
        const auto& item = m_items[si.index];
        double frac = static_cast<double>(si.bytes) / static_cast<double>(denomBytes);
        float spanDeg = static_cast<float>(frac * 360.0);

        if (spanDeg < 0.65f && i > 0 && (sorted.size() - i) > 1 && !si.isPending) {
            smallAggBytes += si.bytes;
            smallAggCount++;
            if (firstSmallIdx < 0) firstSmallIdx = si.index;
            continue;
        }

        if (si.bytes == 0 && !si.isPending) continue;
        if (spanDeg < 0.5f) spanDeg = 0.5f;
        if (curDeg + spanDeg > 360.0f) spanDeg = std::max(0.1f, 360.0f - curDeg);

        bool isSel = item.isSelected;
        bool isHov = (m_hoverIndex == si.index);
        float rIn  = ringInner[0];
        float rOut = ringOuter[0] + (isHov ? 4.0f : 0.0f);

        drawAnnularSectorDeg(rIn, rOut, curDeg, spanDeg, si.baseColor, isHov ? 1.0f : 0.92f,
                             isSel ? 0x0067B8 : 0xFFFFFF,
                             isSel ? 2.4f : 1.5f);

        PieSectorHitNode hit;
        hit.startAngleDeg = curDeg;
        hit.sweepAngleDeg = spanDeg;
        hit.innerRadius = rIn;
        hit.outerRadius = rOut;
        hit.topItemIndex = si.index;
        hit.depth = 0;
        hit.color = si.baseColor;
        hit.size = si.bytes;
        hit.isDirectory = item.isDirectory;
        hit.isInFlight = si.isPending;
        hit.name = item.displayName;
        hit.relPath = item.name;
        hit.fullPath = item.fullPath;
        m_pieHits.push_back(hit);

        if (spanDeg > 9.0f) {
            float midRad = (curDeg + spanDeg * 0.5f - 90.0f) * (3.1415926535f / 180.0f);
            float midR = (rIn + rOut) * 0.5f;
            float lx = cx + std::cos(midRad) * midR;
            float ly = cy + std::sin(midRad) * midR;
            wchar_t pctBuf[32] = {};
            swprintf_s(pctBuf, L" \x2022 %.0f%%", frac * 100.0);
            std::wstring lbl = item.displayName + pctBuf;
            D2D1_RECT_F lblR = D2D1::RectF(lx - 52.0f, ly - 10.0f, lx + 52.0f, ly + 10.0f);
            m_solidBrush->SetColor(HexColor(0xFFFFFF, 0.90f));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(lblR, 3.5f, 3.5f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(0xCBD5E1));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(lblR, 3.5f, 3.5f), m_solidBrush, 0.9f);
            m_solidBrush->SetColor(HexColor(0x0F172A));
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_rt->DrawTextW(lbl.c_str(), static_cast<UINT32>(lbl.size()), m_fmtSmall, lblR, m_solidBrush);
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        if (item.isDirectory && !item.normKey.empty()) {
            std::vector<TreemapChildNode> children1;
            if (m_fileEngine.GetFolderTreemapChildren(item.normKey, children1) && !children1.empty()) {
                renderChildRing(children1, si.index, item.name, curDeg, spanDeg, si.bytes, 1, si.baseColor);
            }
        }

        curDeg += spanDeg;
    }

    if (smallAggCount > 0 && curDeg < 360.0f) {
        float spanDeg = 360.0f - curDeg;
        drawAnnularSectorDeg(ringInner[0], ringOuter[0], curDeg, spanDeg, 0x94A3B8, 0.85f, 0xFFFFFF, 1.2f);
        PieSectorHitNode hit;
        hit.startAngleDeg = curDeg;
        hit.sweepAngleDeg = spanDeg;
        hit.innerRadius = ringInner[0];
        hit.outerRadius = ringOuter[0];
        hit.topItemIndex = firstSmallIdx;
        hit.depth = 0;
        hit.color = 0x94A3B8;
        hit.size = smallAggBytes;
        hit.name = L"[+" + std::to_wstring(smallAggCount) + (m_lang == Lang::PL ? L" mniejszych]" : L" smaller]");
        hit.relPath = hit.name;
        m_pieHits.push_back(hit);
    }

    // Highlight exact hovered sector on any ring (depth 0..3)
    if (m_hoverPieHitIndex >= 0 && m_hoverPieHitIndex < static_cast<int>(m_pieHits.size())) {
        const auto& ph = m_pieHits[m_hoverPieHitIndex];
        if (!ph.hasLegendRect && ph.outerRadius > ph.innerRadius) {
            drawAnnularSectorDeg(ph.innerRadius, ph.outerRadius, ph.startAngleDeg, ph.sweepAngleDeg,
                                 0xFFFFFF, 0.18f, 0x0F172A, 2.2f);
        }
    }

    // Interactive Center Hub Circle ("Click center to go Up" + Hovered item summary)
    bool hubHover = ( std::hypot(static_cast<float>(m_mousePt.x) - cx, static_cast<float>(m_mousePt.y) - cy) <= hubR );
    m_solidBrush->SetColor(hubHover ? HexColor(0xEFF6FF) : HexColor(0xFFFFFF));
    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), hubR, hubR), m_solidBrush);
    m_solidBrush->SetColor(hubHover ? HexColor(0x0078D7) : HexColor(0xCBD5E1));
    m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), hubR, hubR), m_solidBrush, hubHover ? 2.2f : 1.5f);

    std::wstring centerTitle = PathUtils::GetFileName(m_currentPath);
    if (centerTitle.empty()) centerTitle = m_currentPath;
    std::wstring centerSize = FileEngine::FormatSizeDetailed(totalBytes);
    std::wstring centerSub  = m_lang == Lang::PL ? L"\x2191 Klik = W górę" : L"\x2191 Click = Up";

    if (m_hoverPieHitIndex >= 0 && m_hoverPieHitIndex < static_cast<int>(m_pieHits.size())) {
        const auto& ph = m_pieHits[m_hoverPieHitIndex];
        centerTitle = ph.name;
        centerSize  = FileEngine::FormatSizeDetailed(ph.size);
        double pct = (totalBytes > 0) ? (100.0 * static_cast<double>(ph.size) / static_cast<double>(totalBytes)) : 0.0;
        wchar_t pBuf[64] = {};
        swprintf_s(pBuf, L"%.2f%% folderu", pct);
        centerSub = pBuf;
    } else if (m_hoverIndex >= 0 && m_hoverIndex < static_cast<int>(m_items.size())) {
        const auto& hov = m_items[m_hoverIndex];
        centerTitle = hov.displayName;
        centerSize  = FileEngine::FormatSizeDetailed(hov.size);
        double pct = (totalBytes > 0) ? (100.0 * static_cast<double>(hov.size) / static_cast<double>(totalBytes)) : 0.0;
        wchar_t pBuf[64] = {};
        swprintf_s(pBuf, L"%.2f%% folderu", pct);
        centerSub = pBuf;
    }

    D2D1_RECT_F hubTitleR = D2D1::RectF(cx - hubR * 0.85f, cy - 28.0f, cx + hubR * 0.85f, cy - 6.0f);
    D2D1_RECT_F hubSizeR  = D2D1::RectF(cx - hubR * 0.85f, cy - 6.0f,  cx + hubR * 0.85f, cy + 14.0f);
    D2D1_RECT_F hubSubR   = D2D1::RectF(cx - hubR * 0.85f, cy + 14.0f, cx + hubR * 0.85f, cy + 32.0f);

    m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_solidBrush->SetColor(HexColor(0x0F172A));
    m_rt->DrawTextW(centerTitle.c_str(), static_cast<UINT32>(centerTitle.size()), m_fmtBold, hubTitleR, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0x0067B8));
    m_rt->DrawTextW(centerSize.c_str(), static_cast<UINT32>(centerSize.size()), m_fmtBold, hubSizeR, m_solidBrush);
    m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_solidBrush->SetColor(HexColor(0x64748B));
    m_rt->DrawTextW(centerSub.c_str(), static_cast<UINT32>(centerSub.size()), m_fmtSmall, hubSubR, m_solidBrush);
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // 3. Right-Side Ranked Breakdown & Legend Panel (White Explorer Card)
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(listArea, 5.0f, 5.0f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xD8E0EA));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(listArea, 5.0f, 5.0f), m_solidBrush, 1.0f);

    D2D1_RECT_F listHdrR = D2D1::RectF(listArea.left + 10.0f, listArea.top + 6.0f, listArea.right - 10.0f, listArea.top + 28.0f);
    m_solidBrush->SetColor(HexColor(0x475569));
    const wchar_t* rankTitle = m_lang == Lang::PL
        ? L"RANKING ROZMIARU (2\x00D7 Klik = Wejdź / Otwórz)"
        : L"SIZE RANKING BREAKDOWN (2\x00D7 Click = Open)";
    m_rt->DrawTextW(rankTitle, static_cast<UINT32>(wcslen(rankTitle)), m_fmtBold, listHdrR, m_solidBrush);

    float rowY = listArea.top + 32.0f;
    float rowH = 26.0f;
    int maxRows = std::max(1, static_cast<int>((listArea.bottom - rowY - 6.0f) / rowH));
    int countRows = std::min(static_cast<int>(sorted.size()), maxRows);

    for (int r = 0; r < countRows; ++r) {
        const auto& si = sorted[r];
        const auto& item = m_items[si.index];
        D2D1_RECT_F rowR = D2D1::RectF(listArea.left + 6.0f, rowY, listArea.right - 6.0f, rowY + rowH - 2.0f);

        bool isSel = item.isSelected;
        bool isHov = (m_hoverIndex == si.index);
        if (isSel || isHov) {
            m_solidBrush->SetColor(isSel ? HexColor(0xE0F2FE) : HexColor(0xF1F5F9));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(rowR, 3.0f, 3.0f), m_solidBrush);
            m_solidBrush->SetColor(isSel ? HexColor(0x0078D7) : HexColor(0xCBD5E1));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(rowR, 3.0f, 3.0f), m_solidBrush, 1.0f);
        }

        double frac = (totalBytes > 0) ? (static_cast<double>(si.bytes) / static_cast<double>(totalBytes)) : 0.0;
        float barW = static_cast<float>((rowR.right - rowR.left) * frac);
        if (barW > 2.0f) {
            D2D1_RECT_F pBar = D2D1::RectF(rowR.left, rowR.bottom - 3.0f, rowR.left + barW, rowR.bottom);
            m_solidBrush->SetColor(HexColor(si.baseColor, 0.80f));
            m_rt->FillRectangle(pBar, m_solidBrush);
        }

        D2D1_RECT_F swR = D2D1::RectF(rowR.left + 6.0f, rowR.top + 5.0f, rowR.left + 18.0f, rowR.top + 17.0f);
        m_solidBrush->SetColor(HexColor(si.baseColor));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(swR, 2.5f, 2.5f), m_solidBrush);

        std::wstring prefix = item.isDirectory ? L"\xD83D\xDCC1 " : L"";
        std::wstring dispName = prefix + item.displayName;
        D2D1_RECT_F nameR = D2D1::RectF(rowR.left + 24.0f, rowR.top + 2.0f, rowR.right - 135.0f, rowR.bottom - 2.0f);
        m_solidBrush->SetColor(HexColor(0x0F172A));
        m_rt->DrawTextW(dispName.c_str(), static_cast<UINT32>(dispName.size()), m_fmtSmall, nameR, m_solidBrush);

        wchar_t statBuf[64] = {};
        if (si.isPending && si.bytes == 0) {
            swprintf_s(statBuf, L"\x23F3 ...");
        } else {
            swprintf_s(statBuf, L"%s  (%.1f%%)", FileEngine::FormatSizeDetailed(si.bytes).c_str(), frac * 100.0);
        }
        D2D1_RECT_F statR = D2D1::RectF(rowR.right - 132.0f, rowR.top + 2.0f, rowR.right - 6.0f, rowR.bottom - 2.0f);
        m_solidBrush->SetColor(si.isPending ? HexColor(0xD97706) : HexColor(0x475569));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        m_rt->DrawTextW(statBuf, static_cast<UINT32>(wcslen(statBuf)), m_fmtSmall, statR, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        PieSectorHitNode rowHit;
        rowHit.legendRect = rowR;
        rowHit.hasLegendRect = true;
        rowHit.topItemIndex = si.index;
        rowHit.depth = 0;
        rowHit.color = si.baseColor;
        rowHit.size = si.bytes;
        rowHit.isDirectory = item.isDirectory;
        rowHit.isInFlight = si.isPending;
        rowHit.name = item.displayName;
        rowHit.relPath = item.name;
        rowHit.fullPath = item.fullPath;
        m_pieHits.push_back(rowHit);

        rowY += rowH;
    }

    // 4. Bottom Inspector Bar (22px, Light Explorer Status Style)
    D2D1_RECT_F botR = D2D1::RectF(rect.left, rect.bottom - 22.0f, rect.right, rect.bottom);
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRectangle(botR, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xDADADA));
    m_rt->DrawLine(D2D1::Point2F(botR.left, botR.top), D2D1::Point2F(botR.right, botR.top), m_solidBrush, 1.0f);

    std::wstring botInfo = m_lang == Lang::PL
        ? L"Najedź na wycinek koła lub listę po prawej  \x2022  2\x00D7 Klik = Wejdź w folder / Otwórz plik  \x2022  Kliknij środek koła = W górę"
        : L"Hover any pie sector or list row  \x2022  Double-click = Enter folder / Open file  \x2022  Click center hub = Go Up";
    if (m_hoverPieHitIndex >= 0 && m_hoverPieHitIndex < static_cast<int>(m_pieHits.size())) {
        const auto& ph = m_pieHits[m_hoverPieHitIndex];
        double pct = (totalBytes > 0) ? (100.0 * static_cast<double>(ph.size) / static_cast<double>(totalBytes)) : 0.0;
        wchar_t pBuf[64] = {};
        swprintf_s(pBuf, L" (%.2f%%)", pct);
        botInfo = ph.fullPath + L"   \x2022   " + FileEngine::FormatSizeDetailed(ph.size) + pBuf;
    } else if (m_hoverIndex >= 0 && m_hoverIndex < static_cast<int>(m_items.size())) {
        const auto& hov = m_items[m_hoverIndex];
        double pct = (totalBytes > 0) ? (100.0 * static_cast<double>(hov.size) / static_cast<double>(totalBytes)) : 0.0;
        wchar_t pBuf[64] = {};
        swprintf_s(pBuf, L" (%.2f%%)", pct);
        botInfo = hov.fullPath + L"   \x2022   " + FileEngine::FormatSizeDetailed(hov.size) + pBuf;
    }
    D2D1_RECT_F botTxtR = D2D1::RectF(botR.left + 10.0f, botR.top + 2.0f, botR.right - 10.0f, botR.bottom);
    m_solidBrush->SetColor(HexColor(0x334155));
    m_rt->DrawTextW(botInfo.c_str(), static_cast<UINT32>(botInfo.size()), m_fmtSmall, botTxtR, m_solidBrush);

    // 5. Floating On-Hover Info Popup Card (DRY shared helper)
    if (m_hoverPieHitIndex >= 0 && m_hoverPieHitIndex < static_cast<int>(m_pieHits.size())) {
        const auto& ph = m_pieHits[m_hoverPieHitIndex];
        DrawFloatingChartHoverPopup(
            m_rt, m_solidBrush, m_fmtBold, m_fmtSmall,
            bodyR, m_mousePt,
            ph.name, ph.relPath, ph.size, totalBytes,
            ph.isDirectory, ph.depth, ph.color, ph.isInFlight, m_lang);
    }

    m_rt->PopAxisAlignedClip();
}

void ExplorerApp::DrawCardTinyButton(const D2D1_RECT_F& br, const std::wstring& label, int cmdId, const std::wstring& arg,
                                     int actionIndex, const D2D1_RECT_F* hitClip) {
    D2D1_RECT_F hitRect = br;
    if (hitClip) {
        hitRect.left   = std::max(hitRect.left,   hitClip->left);
        hitRect.top    = std::max(hitRect.top,    hitClip->top);
        hitRect.right  = std::min(hitRect.right,  hitClip->right);
        hitRect.bottom = std::min(hitRect.bottom, hitClip->bottom);
    }
    bool hov = PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, hitRect);
    m_solidBrush->SetColor(hov ? HexColor(0x0078D7) : HexColor(0xF1F5F9));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), m_solidBrush);
    m_solidBrush->SetColor(hov ? HexColor(0x005A9E) : HexColor(0xCBD5E1));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), m_solidBrush, 1.0f);
    m_solidBrush->SetColor(hov ? HexColor(0xFFFFFF) : HexColor(0x1E293B));
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_rt->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), m_fmtSmall, br, m_solidBrush);
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    if (hitRect.right <= hitRect.left || hitRect.bottom <= hitRect.top) return;
    HardwareCardHit bh;
    bh.rect = hitRect;
    bh.cmdId = cmdId;
    bh.actionArg = arg;
    bh.actionIndex = actionIndex;
    bh.label = label;
    m_hwCardHits.push_back(bh);
}

void ExplorerApp::DrawClippedText(const std::wstring& text, const D2D1_RECT_F& r, IDWriteTextFormat* fmt, uint32_t colorHex) {
    m_rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
    m_solidBrush->SetColor(HexColor(colorHex));
    m_rt->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), fmt, r, m_solidBrush);
    m_rt->PopAxisAlignedClip();
}

void ExplorerApp::DrawProgramsView(const D2D1_RECT_F& rect) {
    const bool pl = (m_lang == Lang::PL);

    // (Re)read the registry on first paint, on demand, and periodically while an (un)installer is running
    ULONGLONG now = GetTickCount64();
    bool reload = !m_progLoaded;
    if (m_progRefreshAtTick != 0 && now >= m_progRefreshAtTick) {
        reload = true;
        m_progRefreshAtTick = (--m_progRefreshesLeft > 0) ? now + 4000 : 0;
    }
    if (reload) {
        m_programs.Refresh();
        m_programs.Sort(m_progSortCol, m_progSortAsc);
        m_progLoaded = true;
        if (!m_progSelectedKey.empty() && !m_programs.Find(m_progSelectedKey)) m_progSelectedKey.clear();
    }
    if (m_progRefreshAtTick != 0) EnsureWorkAnimTimerState();

    // Apply the search-box filter
    auto lower = [](std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(towlower(c)); return s; };
    const std::wstring flt = lower(m_progFilter);
    std::vector<const InstalledProgram*> rows;
    uint64_t totalBytes = 0;
    for (const auto& p : m_programs.Items()) {
        if (!flt.empty() && lower(p.name).find(flt) == std::wstring::npos && lower(p.publisher).find(flt) == std::wstring::npos) continue;
        rows.push_back(&p);
        totalBytes += p.sizeBytes;
    }

    m_rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_solidBrush->SetColor(HexColor(0xF3F5F8));
    m_rt->FillRectangle(rect, m_solidBrush);

    // ---- Top bar ----
    D2D1_RECT_F topBar = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + 42.0f);
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRectangle(topBar, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xDADADA));
    m_rt->DrawLine(D2D1::Point2F(topBar.left, topBar.bottom), D2D1::Point2F(topBar.right, topBar.bottom), m_solidBrush, 1.0f);

    wchar_t head[256] = {};
    swprintf_s(head, pl ? L"%s  \x2022  %zu z %zu  \x2022  szac. rozmiar: %s" : L"%s  \x2022  %zu of %zu  \x2022  est. size: %s",
               pl ? L"ZAINSTALOWANE PROGRAMY" : L"INSTALLED PROGRAMS",
               rows.size(), m_programs.Items().size(), FileEngine::FormatSizeDetailed(totalBytes).c_str());
    D2D1_RECT_F addBtn = D2D1::RectF(topBar.right - 12.0f - 150.0f, topBar.top + 8.0f, topBar.right - 12.0f, topBar.top + 32.0f);
    D2D1_RECT_F refreshBtn = D2D1::RectF(addBtn.left - 8.0f - 96.0f, addBtn.top, addBtn.left - 8.0f, addBtn.bottom);
    DrawClippedText(head, D2D1::RectF(topBar.left + 12.0f, topBar.top + 3.0f, refreshBtn.left - 10.0f, topBar.top + 22.0f), m_fmtBold, 0x0F172A);
    std::wstring hint = flt.empty()
        ? (pl ? L"Filtr: wpisz fragment nazwy w polu wyszukiwania (prawy górny róg). Kliknij nagłówek kolumny, aby sortować."
              : L"Filter: type part of a name in the search box (top right). Click a column header to sort.")
        : ((pl ? L"Filtr aktywny: \"" : L"Active filter: \"") + m_progFilter + L"\"");
    DrawClippedText(hint, D2D1::RectF(topBar.left + 12.0f, topBar.top + 22.0f, refreshBtn.left - 10.0f, topBar.bottom - 1.0f), m_fmtSmall, flt.empty() ? 0x64748B : 0x0067B8);
    DrawCardTinyButton(refreshBtn, pl ? L"\x21BB Odśwież" : L"\x21BB Refresh", CMD_PROG_REFRESH, L"");
    DrawCardTinyButton(addBtn, pl ? L"\x2795 Dodaj program…" : L"\x2795 Add program…", CMD_PROG_ADD, L"");

    // ---- Column layout (right-anchored fixed columns, flexible name column) ----
    const float pad = 14.0f;
    float opsW = 112.0f, dateW = 92.0f, sizeW = 84.0f, verW = 112.0f, pubW = 190.0f, gap = 8.0f;
    float opsX = rect.right - pad - opsW;
    float dateX = opsX - gap - dateW;
    float sizeX = dateX - gap - sizeW;
    float verX = sizeX - gap - verW;
    float pubX = verX - gap - pubW;
    float nameX = rect.left + pad + 34.0f;
    if (pubX - gap < nameX + 120.0f) {      // narrow window: drop the publisher column width
        pubW = 90.0f;
        pubX = verX - gap - pubW;
    }
    float nameW = std::max(60.0f, pubX - gap - nameX);

    float hdrTop = topBar.bottom;
    float hdrH = 26.0f;
    m_solidBrush->SetColor(HexColor(0xEEF2F6));
    m_rt->FillRectangle(D2D1::RectF(rect.left, hdrTop, rect.right, hdrTop + hdrH), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xDADADA));
    m_rt->DrawLine(D2D1::Point2F(rect.left, hdrTop + hdrH), D2D1::Point2F(rect.right, hdrTop + hdrH), m_solidBrush, 1.0f);

    auto headerCell = [&](float x, float w, const wchar_t* label, int col, bool rightAlign) {
        D2D1_RECT_F cell = D2D1::RectF(x - 4.0f, hdrTop, x + w + 2.0f, hdrTop + hdrH);
        if (PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, cell)) {
            m_solidBrush->SetColor(HexColor(0xDCEBFC));
            m_rt->FillRectangle(cell, m_solidBrush);
        }
        std::wstring txt = label;
        if (col == m_progSortCol) txt += m_progSortAsc ? L"  \x25B2" : L"  \x25BC";
        if (rightAlign) m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        DrawClippedText(txt, D2D1::RectF(x, hdrTop + 4.0f, x + w, hdrTop + hdrH), m_fmtBold, col == m_progSortCol ? 0x0067B8 : 0x334155);
        m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        HardwareCardHit h;
        h.rect = cell;
        h.cmdId = CMD_PROG_SORT;
        h.actionIndex = col;
        h.label = label;
        m_hwCardHits.push_back(h);
    };
    headerCell(nameX, nameW, pl ? L"Nazwa" : L"Name", 0, false);
    headerCell(pubX, pubW, pl ? L"Wydawca" : L"Publisher", 1, false);
    headerCell(verX, verW, pl ? L"Wersja" : L"Version", 2, false);
    headerCell(sizeX, sizeW, pl ? L"Rozmiar" : L"Size", 3, true);
    headerCell(dateX, dateW, pl ? L"Zainstalowano" : L"Installed", 4, false);
    DrawClippedText(pl ? L"Operacje" : L"Actions", D2D1::RectF(opsX, hdrTop + 4.0f, opsX + opsW, hdrTop + hdrH), m_fmtBold, 0x334155);

    // ---- Scrollable list ----
    const float rowH = 30.0f;
    const float bottomH = 36.0f;
    D2D1_RECT_F listR = D2D1::RectF(rect.left, hdrTop + hdrH + 1.0f, rect.right, rect.bottom - bottomH);
    float viewH = std::max(1.0f, listR.bottom - listR.top);
    float totalH = static_cast<float>(rows.size()) * rowH;
    m_hwMaxScrollY = std::max(0.0f, totalH - viewH + 6.0f);
    m_hwScrollY = std::clamp(m_hwScrollY, 0.0f, m_hwMaxScrollY);

    m_rt->PushAxisAlignedClip(listR, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    static const uint32_t kChipPalette[] = { 0x0078D7, 0x059669, 0x7C3AED, 0xD97706, 0xDB2777, 0x0284C7, 0x4F46E5, 0x0D9488 };
    bool mouseInList = PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, listR);

    for (size_t i = 0; i < rows.size(); ++i) {
        float y = listR.top + static_cast<float>(i) * rowH - m_hwScrollY;
        if (y + rowH < listR.top || y > listR.bottom) continue;
        const InstalledProgram& p = *rows[i];
        D2D1_RECT_F rr = D2D1::RectF(rect.left + 8.0f, y, rect.right - 8.0f, y + rowH - 1.0f);
        bool sel = (p.regKey == m_progSelectedKey);
        bool hov = mouseInList && PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, rr);
        m_solidBrush->SetColor(HexColor(sel ? 0xE0F2FE : (hov ? 0xE8F1FB : (i % 2 == 0 ? 0xFFFFFF : 0xFAFBFC))));
        m_rt->FillRectangle(rr, m_solidBrush);
        if (sel) {
            m_solidBrush->SetColor(HexColor(0x0078D7));
            m_rt->FillRectangle(D2D1::RectF(rr.left, rr.top, rr.left + 3.0f, rr.bottom), m_solidBrush);
        }

        // Letter chip (stable colour per program)
        size_t h = 0;
        for (wchar_t c : p.name) h = h * 131 + static_cast<size_t>(c);
        D2D1_RECT_F chip = D2D1::RectF(rr.left + 10.0f, y + 4.0f, rr.left + 32.0f, y + 25.0f);
        m_solidBrush->SetColor(HexColor(kChipPalette[h % std::size(kChipPalette)]));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(chip, 4.0f, 4.0f), m_solidBrush);
        wchar_t letter[2] = { L'?', 0 };
        for (wchar_t c : p.name) { if (iswalnum(c)) { letter[0] = static_cast<wchar_t>(towupper(c)); break; } }
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(letter, 1, m_fmtSmall, D2D1::RectF(chip.left, chip.top + 1.0f, chip.right, chip.bottom), m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        float ty0 = y + 5.0f, ty1 = y + rowH - 2.0f;
        DrawClippedText(p.name, D2D1::RectF(nameX, ty0, nameX + nameW, ty1), m_fmtSmall, 0x0F172A);
        DrawClippedText(p.publisher, D2D1::RectF(pubX, ty0, pubX + pubW, ty1), m_fmtSmall, 0x475569);
        DrawClippedText(p.version, D2D1::RectF(verX, ty0, verX + verW, ty1), m_fmtSmall, 0x475569);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        DrawClippedText(p.sizeBytes > 0 ? FileEngine::FormatSizeDetailed(p.sizeBytes) : std::wstring(L"\x2014"),
                        D2D1::RectF(sizeX, ty0, sizeX + sizeW, ty1), m_fmtSmall, 0x0067B8);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawClippedText(p.installDate.empty() ? std::wstring(L"\x2014") : p.installDate, D2D1::RectF(dateX, ty0, dateX + dateW, ty1), m_fmtSmall, 0x475569);

        if (p.canUninstall) {
            D2D1_RECT_F ub = D2D1::RectF(opsX, y + 3.0f, opsX + opsW, y + rowH - 4.0f);
            DrawCardTinyButton(ub, pl ? L"\xD83D\xDDD1 Odinstaluj" : L"\xD83D\xDDD1 Uninstall", CMD_PROG_UNINSTALL_ROW, p.regKey, 0, &listR);
        } else {
            DrawClippedText(pl ? L"brak dezinstalatora" : L"no uninstaller", D2D1::RectF(opsX, ty0, opsX + opsW, ty1), m_fmtSmall, 0x94A3B8);
        }

        // Row click = select (registered after the button so the button wins the hit test)
        D2D1_RECT_F hitR = D2D1::RectF(rr.left, std::max(rr.top, listR.top), rr.right, std::min(rr.bottom, listR.bottom));
        if (hitR.bottom > hitR.top) {
            HardwareCardHit rowHit;
            rowHit.rect = hitR;
            rowHit.cmdId = CMD_PROG_SELECT;
            rowHit.actionArg = p.regKey;
            rowHit.label = p.name;
            m_hwCardHits.push_back(rowHit);
        }
    }

    if (rows.empty()) {
        m_fmtCenterSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_solidBrush->SetColor(HexColor(0x64748B));
        const wchar_t* msg = flt.empty() ? (pl ? L"Nie znaleziono zainstalowanych programów." : L"No installed programs found.")
                                         : (pl ? L"Żaden program nie pasuje do filtra." : L"No program matches the filter.");
        m_rt->DrawTextW(msg, static_cast<UINT32>(wcslen(msg)), m_fmtCenterSmall, listR, m_solidBrush);
    }
    m_rt->PopAxisAlignedClip();

    // Thin scrollbar
    if (m_hwMaxScrollY > 0.0f) {
        float trackH = viewH - 4.0f;
        float thumbH = std::max(24.0f, trackH * viewH / (totalH + 6.0f));
        float thumbY = listR.top + 2.0f + (trackH - thumbH) * (m_hwScrollY / m_hwMaxScrollY);
        m_solidBrush->SetColor(HexColor(0xCBD5E1));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.right - 6.0f, thumbY, rect.right - 2.0f, thumbY + thumbH), 2.0f, 2.0f), m_solidBrush);
    }

    // ---- Bottom bar: details of the selected program + actions ----
    D2D1_RECT_F botR = D2D1::RectF(rect.left, rect.bottom - bottomH, rect.right, rect.bottom);
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRectangle(botR, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xDADADA));
    m_rt->DrawLine(D2D1::Point2F(botR.left, botR.top), D2D1::Point2F(botR.right, botR.top), m_solidBrush, 1.0f);

    const InstalledProgram* selProg = m_programs.Find(m_progSelectedKey);
    if (selProg) {
        D2D1_RECT_F uBtn = D2D1::RectF(botR.right - 12.0f - 170.0f, botR.top + 6.0f, botR.right - 12.0f, botR.bottom - 6.0f);
        D2D1_RECT_F fBtn = D2D1::RectF(uBtn.left - 8.0f - 120.0f, uBtn.top, uBtn.left - 8.0f, uBtn.bottom);
        std::wstring info = selProg->name + (selProg->version.empty() ? L"" : (L"  " + selProg->version));
        if (!selProg->installLocation.empty()) info += (pl ? L"   \x2022   Folder: " : L"   \x2022   Folder: ") + selProg->installLocation;
        std::wstring tags;
        if (selProg->isPerUser) tags += pl ? L"  [tylko użytkownik]" : L"  [current user]";
        if (selProg->is32Bit) tags += L"  [32-bit]";
        if (selProg->isMsi) tags += L"  [MSI]";
        DrawClippedText(info + tags, D2D1::RectF(botR.left + 12.0f, botR.top + 3.0f, fBtn.left - 10.0f, botR.bottom - 2.0f), m_fmtSmall, 0x1E293B);
        DrawCardTinyButton(fBtn, pl ? L"\xD83D\xDCC2 Folder instalacji" : L"\xD83D\xDCC2 Install folder", CMD_PROG_OPENLOC, L"");
        DrawCardTinyButton(uBtn, pl ? L"\xD83D\xDDD1 Odinstaluj wybrany" : L"\xD83D\xDDD1 Uninstall selected", CMD_PROG_UNINSTALL, L"");
    } else {
        DrawClippedText(pl ? L"Zaznacz program na liście, aby zobaczyć szczegóły i odinstalować go. \"Dodaj program\" uruchamia wybrany instalator (.exe / .msi / .msix)."
                           : L"Select a program to see details and uninstall it. \"Add program\" runs the installer you pick (.exe / .msi / .msix).",
                        D2D1::RectF(botR.left + 12.0f, botR.top + 8.0f, botR.right - 12.0f, botR.bottom - 2.0f), m_fmtSmall, 0x64748B);
    }

    m_rt->PopAxisAlignedClip();
}

void ExplorerApp::DrawCardShell(float x, float y, float w, float h, const std::wstring& title, uint32_t badgeHex,
                                bool showCopyHint, float extraRightPad) {
    D2D1_RECT_F cardR = D2D1::RectF(x, y, x + w, y + h);
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(cardR, 5.0f, 5.0f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xD8E0EA));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(cardR, 5.0f, 5.0f), m_solidBrush, 1.0f);

    D2D1_RECT_F hdrR = D2D1::RectF(x + 1.0f, y + 1.0f, x + w - 1.0f, y + 28.0f);
    m_solidBrush->SetColor(HexColor(0xF8FAFC));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(hdrR, 4.5f, 4.5f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xE2E8F0));
    m_rt->DrawLine(D2D1::Point2F(x + 1.0f, y + 28.0f), D2D1::Point2F(x + w - 1.0f, y + 28.0f), m_solidBrush, 1.0f);
    m_solidBrush->SetColor(HexColor(badgeHex));
    m_rt->FillRectangle(D2D1::RectF(x + 1.0f, y + 4.0f, x + 5.0f, y + 24.0f), m_solidBrush);

    D2D1_RECT_F titleR = D2D1::RectF(x + 12.0f, y + 4.0f, x + w - (showCopyHint ? 85.0f : 12.0f) - extraRightPad, y + 26.0f);
    DrawClippedText(title, titleR, m_fmtBold, 0x0F172A);

    if (showCopyHint) {
        D2D1_RECT_F copyHintR = D2D1::RectF(x + w - 82.0f, y + 5.0f, x + w - 8.0f, y + 25.0f);
        m_solidBrush->SetColor(HexColor(0x64748B));
        const wchar_t* cpLbl = (m_lang == Lang::PL) ? L"[\xD83D\xDCCB Kopiuj]" : L"[\xD83D\xDCCB Copy]";
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        m_rt->DrawTextW(cpLbl, static_cast<UINT32>(wcslen(cpLbl)), m_fmtSmall, copyHintR, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
}

void ExplorerApp::DrawMonitoringView(const D2D1_RECT_F& rect) {
    const bool pl = (m_lang == Lang::PL);
    static const int64_t kRangeSec[] = { 3600, 6 * 3600, 24 * 3600, 7 * 86400, 30 * 86400 };
    const wchar_t* const rangeLabels[] = {
        pl ? L"1 godz." : L"1 h", pl ? L"6 godz." : L"6 h", pl ? L"24 godz." : L"24 h", pl ? L"7 dni" : L"7 days", pl ? L"30 dni" : L"30 days"
    };
    const float kNaN = std::numeric_limits<float>::quiet_NaN();

    // ---- Data: read what the recorder process wrote (read-only, incremental, safe with many windows) ----
    const int64_t nowTs = static_cast<int64_t>(time(nullptr));
    const ULONGLONG nowTick = GetTickCount64();
    m_monRangeIdx = std::clamp(m_monRangeIdx, 0, 4);
    const int64_t rangeSec = kRangeSec[m_monRangeIdx];
    m_monDaemonRunning = MonitorDaemon::IsRunning();
    if (m_monAutostartMode < 0) m_monAutostartMode = MonitorDaemon::AutostartMode();   // spawns schtasks.exe: only when invalidated
    if (m_monForceReload || m_monLoadedRangeIdx != m_monRangeIdx || m_monLoadTick == 0) {
        m_monSettings = MonitorStore::LoadSettings();
        m_monDataBytes = MonitorStore::TotalBytesOnDisk();
        m_monHistory.Update(nowTs - rangeSec, nowTs);
        m_monSmart = MonitorStore::LoadSmartRange(nowTs - rangeSec, nowTs);
        m_monLoadedRangeIdx = m_monRangeIdx;
        m_monLoadTick = nowTick;
        m_monForceReload = false;
    }
    const int64_t tFrom = nowTs - rangeSec;
    const std::vector<const MonitorSample*>& samples = m_monHistory.Rows();

    // ---- Layout ----
    m_rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_solidBrush->SetColor(HexColor(0xF3F5F8));
    m_rt->FillRectangle(rect, m_solidBrush);

    const float padX = 10.0f, gap = 10.0f;
    const float availW = std::max(320.0f, (rect.right - rect.left) - padX * 2.0f);
    const float halfW = (availW - gap) * 0.5f;
    const float leftX = rect.left + padX;
    const float rightX = leftX + halfW + gap;
    const float startY = rect.top + 8.0f;
    const bool zoomMode = (m_monZoomChart > 0);
    float curY = startY - (zoomMode ? 0.0f : m_hwScrollY);
    const int buckets = std::clamp(static_cast<int>((halfW - 60.0f) / 3.0f), 60, 360);
    const int zoomBuckets = std::clamp(static_cast<int>((availW - 70.0f) / 2.0f), 120, 1600);

    auto fmtClock = [](int64_t ts, bool withDate) -> std::wstring {
        tm t = {};
        time_t tt = static_cast<time_t>(ts);
        localtime_s(&t, &tt);
        wchar_t b[48] = {};
        if (withDate) swprintf_s(b, L"%02d.%02d %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
        else swprintf_s(b, L"%02d:%02d", t.tm_hour, t.tm_min);
        return b;
    };
    const bool dateInLabels = rangeSec > 24 * 3600;

    // ---- Status / control card ----
    {
        const float h = 100.0f;
        DrawCardShell(leftX, curY, availW, h, pl ? L"MONITORING SYSTEMU  \x2022  REJESTRATOR W TLE" : L"SYSTEM MONITORING  \x2022  BACKGROUND RECORDER",
                      m_monDaemonRunning ? 0x059669 : 0x94A3B8, false, 190.0f);
        DrawCardTinyButton(D2D1::RectF(leftX + availW - 178.0f, curY + 4.0f, leftX + availW - 8.0f, curY + 25.0f),
                           m_monDaemonRunning ? (pl ? L"\x23F9 Zatrzymaj nagrywanie" : L"\x23F9 Stop recording")
                                              : (pl ? L"\x23FA Uruchom nagrywanie" : L"\x23FA Start recording"),
                           CMD_MON_TOGGLE, L"");

        D2D1_POINT_2F dot = D2D1::Point2F(leftX + 18.0f, curY + 44.0f);
        m_solidBrush->SetColor(HexColor(m_monDaemonRunning ? 0x10B981 : 0x94A3B8));
        m_rt->FillEllipse(D2D1::Ellipse(dot, 5.0f, 5.0f), m_solidBrush);
        std::wstring status = m_monDaemonRunning
            ? (pl ? L"NAGRYWANIE AKTYWNE  \x2014  jeden proces w tle zapisuje dane dla wszystkich okien Explorer2"
                  : L"RECORDING  \x2014  a single background process records for all Explorer2 windows")
            : (pl ? L"Nagrywanie wyłączone  \x2014  historia nie jest zapisywana (kliknij „Uruchom nagrywanie”)"
                  : L"Recording is off  \x2014  no history is being saved (click \"Start recording\")");
        DrawClippedText(status, D2D1::RectF(leftX + 32.0f, curY + 34.0f, leftX + availW - 10.0f, curY + 54.0f), m_fmtBold,
                        m_monDaemonRunning ? 0x047857 : 0x475569);

        const wchar_t* autoTxt = m_monAutostartMode == 2 ? (pl ? L"wł. (admin)" : L"on (admin)") : (m_monAutostartMode == 1 ? (pl ? L"wł." : L"on") : (pl ? L"wył." : L"off"));
        wchar_t info[400] = {};
        swprintf_s(info, pl ? L"Próbkowanie co %d s  \x2022  przechowywanie %d dni  \x2022  dane: %s  \x2022  autostart: %s  \x2022  tryb: %s  \x2022  próbek w zakresie: %zu"
                            : L"Sampling every %d s  \x2022  retention %d days  \x2022  data: %s  \x2022  autostart: %s  \x2022  mode: %s  \x2022  samples in range: %zu",
                   m_monSettings.intervalSec, m_monSettings.retentionDays, FileEngine::FormatSizeDetailed(m_monDataBytes).c_str(), autoTxt,
                   m_monSettings.elevated ? (pl ? L"administrator" : L"administrator") : (pl ? L"zwykły" : L"standard"), samples.size());
        DrawClippedText(info, D2D1::RectF(leftX + 14.0f, curY + 55.0f, leftX + availW - 10.0f, curY + 71.0f), m_fmtSmall, 0x475569);

        // Range chips
        float chipX = leftX + 14.0f;
        DrawClippedText(pl ? L"Zakres:" : L"Range:", D2D1::RectF(chipX, curY + 76.0f, chipX + 52.0f, curY + 96.0f), m_fmtSmall, 0x64748B);
        chipX += 54.0f;
        for (int i = 0; i < 5; ++i) {
            D2D1_RECT_F cr = D2D1::RectF(chipX, curY + 75.0f, chipX + 74.0f, curY + 95.0f);
            bool act = (i == m_monRangeIdx);
            bool hov = PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, cr);
            m_solidBrush->SetColor(HexColor(act ? 0x0078D7 : (hov ? 0xDCEBFC : 0xF1F5F9)));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(cr, 9.0f, 9.0f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(act ? 0x005A9E : 0xCBD5E1));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(cr, 9.0f, 9.0f), m_solidBrush, 1.0f);
            m_solidBrush->SetColor(HexColor(act ? 0xFFFFFF : 0x1E293B));
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_rt->DrawTextW(rangeLabels[i], static_cast<UINT32>(wcslen(rangeLabels[i])), m_fmtSmall, D2D1::RectF(cr.left, cr.top + 1.0f, cr.right, cr.bottom), m_solidBrush);
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            HardwareCardHit ch;
            ch.rect = cr;
            ch.cmdId = CMD_MON_RANGE;
            ch.actionIndex = i;
            ch.label = rangeLabels[i];
            m_hwCardHits.push_back(ch);
            chipX += 80.0f;
        }
        DrawClippedText(MonitorStore::DataDir(), D2D1::RectF(chipX + 8.0f, curY + 77.0f, leftX + availW - 10.0f, curY + 96.0f), m_fmtSmall, 0x94A3B8);
        curY += h + gap;
    }

    // ---- Chart definitions ------------------------------------------------------------------
    struct ChartDef {
        int id = 0;
        std::wstring title;
        uint32_t badge = 0, color = 0, color2 = 0;
        std::function<float(const MonitorSample&)> a, b;
        bool useMax = false;
        float fixedMax = 0.0f, autoFloor = 0.0f;
        const wchar_t* unit = L"";
        int decimals = 0;
        bool alwaysShow = false;
    };
    std::vector<ChartDef> charts;
    auto addChart = [&](int id, const std::wstring& title, uint32_t badge, uint32_t color,
                        std::function<float(const MonitorSample&)> a, std::function<float(const MonitorSample&)> b, uint32_t color2,
                        bool useMax, float fixedMax, float autoFloor, const wchar_t* unit, int decimals, bool alwaysShow) {
        ChartDef c;
        c.id = id; c.title = title; c.badge = badge; c.color = color; c.color2 = color2;
        c.a = std::move(a); c.b = std::move(b); c.useMax = useMax; c.fixedMax = fixedMax; c.autoFloor = autoFloor;
        c.unit = unit; c.decimals = decimals; c.alwaysShow = alwaysShow;
        charts.push_back(std::move(c));
    };
    const wchar_t* kDeg = L"\x00B0" L"C";

    addChart(1, pl ? L"PROCESOR (CPU %)" : L"PROCESSOR (CPU %)", 0x0078D7, 0x0078D7, [](const MonitorSample& s) { return s.cpuPct; }, nullptr, 0, false, 100.0f, 0.0f, L"%", 0, true);
    addChart(2, pl ? L"PAMIĘĆ RAM (%)" : L"MEMORY (RAM %)", 0x7C3AED, 0x7C3AED, [](const MonitorSample& s) { return s.ramPct; }, nullptr, 0, false, 100.0f, 0.0f, L"%", 0, true);
    addChart(3, pl ? L"PAMIĘĆ ZADEKLAROWANA (COMMIT %)" : L"COMMITTED MEMORY (%)", 0x6D28D9, 0x6D28D9, [](const MonitorSample& s) { return s.commitPct; }, nullptr, 0, false, 100.0f, 0.0f, L"%", 0, false);
    addChart(4, pl ? L"LICZBA PROCESÓW" : L"PROCESS COUNT", 0x475569, 0x475569, [](const MonitorSample& s) { return s.procCount >= 0 ? static_cast<float>(s.procCount) : -1.0f; }, nullptr, 0, true, 0.0f, 100.0f, L"", 0, false);
    addChart(5, pl ? L"GPU — OBCIĄŻENIE (%)" : L"GPU LOAD (%)", 0x059669, 0x059669, [](const MonitorSample& s) { return s.gpuLoadPct; }, nullptr, 0, false, 100.0f, 0.0f, L"%", 0, true);
    addChart(6, pl ? L"GPU — TEMPERATURA (\x00B0" L"C)" : L"GPU TEMPERATURE (\x00B0" L"C)", 0xDC2626, 0xDC2626, [](const MonitorSample& s) { return s.gpuTempC >= 0 ? static_cast<float>(s.gpuTempC) : -1.0f; }, nullptr, 0, true, 0.0f, 60.0f, kDeg, 0, true);
    addChart(7, pl ? L"GPU — POBÓR MOCY (W)" : L"GPU POWER DRAW (W)", 0xEA580C, 0xEA580C, [](const MonitorSample& s) { return s.gpuPowerW; }, nullptr, 0, true, 0.0f, 50.0f, L" W", 0, false);
    addChart(8, pl ? L"GPU — VRAM (MB)" : L"GPU VRAM (MB)", 0x0D9488, 0x0D9488, [](const MonitorSample& s) { return s.gpuVramMB >= 0 ? static_cast<float>(s.gpuVramMB) : -1.0f; }, nullptr, 0, true, 0.0f, 1024.0f, L" MB", 0, false);
    addChart(9, pl ? L"GPU — WENTYLATOR (%)" : L"GPU FAN (%)", 0x0891B2, 0x0891B2, [](const MonitorSample& s) { return s.gpuFanPct >= 0 ? static_cast<float>(s.gpuFanPct) : -1.0f; }, nullptr, 0, false, 100.0f, 0.0f, L"%", 0, false);
    addChart(10, pl ? L"STREFA ACPI — MAX (\x00B0" L"C)" : L"ACPI ZONE MAX (\x00B0" L"C)", 0xD97706, 0xD97706, [](const MonitorSample& s) { return s.acpiTempC >= 0 ? static_cast<float>(s.acpiTempC) : -1.0f; }, nullptr, 0, true, 0.0f, 60.0f, kDeg, 0, true);
    addChart(11, pl ? L"SIEĆ (KB/s)  \x2022  pobieranie / wysyłanie" : L"NETWORK (KB/s)  \x2022  down / up", 0xF59E0B, 0xD97706,
             [](const MonitorSample& s) { return s.rxKBps; }, [](const MonitorSample& s) { return s.txKBps; }, 0x0284C7, false, 0.0f, 100.0f, L"", 0, true);

    // One group of charts per drive that appears in the data: temperature, throughput, busy time
    {
        std::vector<int> diskIdx;
        for (const MonitorSample* sp : samples) {
            for (const auto& d : sp->disks) {
                if (std::find(diskIdx.begin(), diskIdx.end(), d.index) == diskIdx.end()) diskIdx.push_back(d.index);
            }
        }
        std::sort(diskIdx.begin(), diskIdx.end());
        for (int idx : diskIdx) {
            std::wstring label = (pl ? L"DYSK #" : L"DRIVE #") + std::to_wstring(idx);
            for (const auto& pd : m_hwInspector.GetPhysicalDisks()) {
                if (pd.index == idx) { label += L"  \x2022  " + pd.model; break; }
            }
            auto pick = [idx](int field) {
                return [idx, field](const MonitorSample& smp) -> float {
                    for (const auto& d : smp.disks) {
                        if (d.index != idx) continue;
                        switch (field) {
                        case 0: return d.tempC >= 0 ? static_cast<float>(d.tempC) : -1.0f;
                        case 1: return d.readKBps;
                        case 2: return d.writeKBps;
                        default: return d.busyPct;
                        }
                    }
                    return -1.0f;
                };
            };
            addChart(100 + idx * 10, label + L"  (\x00B0" L"C)", 0x0284C7, 0x0284C7, pick(0), nullptr, 0, true, 0.0f, 60.0f, kDeg, 0, false);
            addChart(101 + idx * 10, label + (pl ? L"  (odczyt / zapis KB/s)" : L"  (read / write KB/s)"), 0x0891B2, 0x0891B2, pick(1), pick(2), 0xDB2777, false, 0.0f, 1024.0f, L"", 0, false);
            addChart(102 + idx * 10, label + (pl ? L"  (zajętość %)" : L"  (busy %)"), 0x4F46E5, 0x4F46E5, pick(3), nullptr, 0, false, 100.0f, 0.0f, L"%", 0, false);
        }
    }

    // ---- Series cache: rebuilt only when the data, range or chart width changed ------------------
    auto getSeries = [&](const ChartDef& def, int bucketCount) -> const MonitorSeriesCacheEntry& {
        MonitorSeriesCacheEntry& e = m_monSeriesCache[def.id];
        if (e.version == m_monHistory.Version() && e.range == m_monRangeIdx && e.buckets == bucketCount) return e;
        e.version = m_monHistory.Version();
        e.range = m_monRangeIdx;
        e.buckets = bucketCount;
        auto build = [&](const std::function<float(const MonitorSample&)>& extract, bool useMax) {
            std::vector<float> acc(static_cast<size_t>(bucketCount), 0.0f);
            std::vector<int> cnt(static_cast<size_t>(bucketCount), 0);
            for (const MonitorSample* sp : samples) {
                float v = extract(*sp);
                if (v < 0.0f) continue;   // unknown
                int b = static_cast<int>(static_cast<double>(sp->ts - tFrom) * bucketCount / static_cast<double>(rangeSec));
                b = std::clamp(b, 0, bucketCount - 1);
                if (cnt[b] == 0) acc[b] = v;
                else acc[b] = useMax ? std::max(acc[b], v) : (acc[b] + v);
                ++cnt[b];
            }
            std::vector<float> out(static_cast<size_t>(bucketCount), kNaN);
            for (int i = 0; i < bucketCount; ++i) {
                if (cnt[i] > 0) out[i] = useMax ? acc[i] : acc[i] / static_cast<float>(cnt[i]);
            }
            return out;
        };
        e.a = def.a ? build(def.a, def.useMax) : std::vector<float>();
        e.b = def.b ? build(def.b, def.useMax) : std::vector<float>();
        e.hasData = false;
        for (float v : e.a) if (!std::isnan(v)) { e.hasData = true; break; }
        return e;
    };

    auto niceCeil = [](float v) -> float {
        if (v <= 10.0f) return 10.0f;
        float mag = std::pow(10.0f, std::floor(std::log10(v)));
        float n = v / mag;
        float r = n <= 1.0f ? 1.0f : (n <= 2.0f ? 2.0f : (n <= 2.5f ? 2.5f : (n <= 5.0f ? 5.0f : 10.0f)));
        return r * mag;
    };

    auto drawZoomIcon = [&](const D2D1_RECT_F& r, bool restore, bool hov) {
        m_solidBrush->SetColor(HexColor(hov ? 0xFFFFFF : 0x475569));
        float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
        if (!restore) {
            const float s = 6.0f, k = 3.5f;   // four corner brackets = "fill window"
            for (int sx = -1; sx <= 1; sx += 2) {
                for (int sy = -1; sy <= 1; sy += 2) {
                    D2D1_POINT_2F c = D2D1::Point2F(cx + sx * s, cy + sy * s);
                    m_rt->DrawLine(c, D2D1::Point2F(c.x - sx * k, c.y), m_solidBrush, 1.5f);
                    m_rt->DrawLine(c, D2D1::Point2F(c.x, c.y - sy * k), m_solidBrush, 1.5f);
                }
            }
        } else {
            m_rt->DrawRectangle(D2D1::RectF(cx - 6.0f, cy - 3.0f, cx + 3.0f, cy + 6.0f), m_solidBrush, 1.4f);
            m_rt->DrawRectangle(D2D1::RectF(cx - 3.0f, cy - 6.0f, cx + 6.0f, cy + 3.0f), m_solidBrush, 1.4f);
        }
    };

    auto drawChart = [&](const ChartDef& def, const MonitorSeriesCacheEntry& se, float x, float y, float w, float h, bool zoomed) {
        const float statsW = std::min(zoomed ? 520.0f : 330.0f, w * 0.6f);
        DrawCardShell(x, y, w, h, def.title, def.badge, false, statsW + 34.0f);

        // "Fill window" / "restore" button
        D2D1_RECT_F zb = D2D1::RectF(x + w - 30.0f, y + 4.0f, x + w - 8.0f, y + 25.0f);
        DrawCardTinyButton(zb, L"", CMD_MON_ZOOM, L"", zoomed ? -1 : def.id);
        drawZoomIcon(zb, zoomed, PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, zb));

        D2D1_RECT_F plot = D2D1::RectF(x + 52.0f, y + 36.0f, x + w - 14.0f, y + h - 22.0f);
        const float pw = plot.right - plot.left, ph = plot.bottom - plot.top;
        const std::vector<float>& a = se.a;
        const std::vector<float>* b = def.b ? &se.b : nullptr;
        const int n = static_cast<int>(a.size());

        float mn = 1e30f, mx = -1e30f, sum = 0.0f, last = kNaN, bmx = 0.0f;
        int cnt = 0;
        for (float v : a) {
            if (std::isnan(v)) continue;
            mn = std::min(mn, v); mx = std::max(mx, v); sum += v; last = v; ++cnt;
        }
        if (b) for (float v : *b) if (!std::isnan(v)) bmx = std::max(bmx, v);
        const float scaleMax = def.fixedMax > 0.0f ? def.fixedMax : std::max(def.autoFloor, niceCeil(std::max(mx, bmx) * 1.1f));

        auto fmtVal = [&](float v) -> std::wstring {
            wchar_t buf[48] = {};
            swprintf_s(buf, L"%.*f%s", def.decimals, v, def.unit);
            return buf;
        };

        int hov = -1;
        if (n > 0 && PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, plot)) {
            hov = std::clamp(static_cast<int>((m_mousePt.x - plot.left) / pw * n), 0, n - 1);
        }

        std::wstring stats;
        if (cnt == 0) {
            stats = pl ? L"brak danych" : L"no data";
        } else if (hov >= 0 && !std::isnan(a[hov])) {
            int64_t ts = tFrom + static_cast<int64_t>((static_cast<double>(hov) + 0.5) * rangeSec / n);
            stats = fmtClock(ts, dateInLabels) + L"   " + fmtVal(a[hov]);
            if (b && !std::isnan((*b)[hov])) stats += L"  /  " + fmtVal((*b)[hov]);
        } else {
            stats = (pl ? L"teraz " : L"now ") + fmtVal(last) + (pl ? L"   min " : L"   min ") + fmtVal(mn) +
                    (pl ? L"   śr " : L"   avg ") + fmtVal(sum / static_cast<float>(cnt)) + L"   max " + fmtVal(mx);
        }
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        DrawClippedText(stats, D2D1::RectF(x + w - 36.0f - statsW, y + 7.0f, x + w - 36.0f, y + 26.0f), m_fmtSmall, 0x475569);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        // Plot background + grid
        m_solidBrush->SetColor(HexColor(0xF8FAFC));
        m_rt->FillRectangle(plot, m_solidBrush);
        for (int g = 0; g <= 3; ++g) {
            float gy = plot.bottom - ph * static_cast<float>(g) / 3.0f;
            m_solidBrush->SetColor(HexColor(0xE2E8F0));
            m_rt->DrawLine(D2D1::Point2F(plot.left, gy), D2D1::Point2F(plot.right, gy), m_solidBrush, 0.8f);
            wchar_t lbl[32] = {};
            float gv = scaleMax * static_cast<float>(g) / 3.0f;
            swprintf_s(lbl, scaleMax >= 30.0f ? L"%.0f" : L"%.1f", gv);
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            DrawClippedText(lbl, D2D1::RectF(x + 2.0f, gy - 8.0f, plot.left - 5.0f, gy + 8.0f), m_fmtSmall, 0x94A3B8);
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
        m_solidBrush->SetColor(HexColor(0xCBD5E1));
        m_rt->DrawRectangle(plot, m_solidBrush, 1.0f);

        if (cnt == 0) {
            m_fmtCenterSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_solidBrush->SetColor(HexColor(0x94A3B8));
            const wchar_t* msg = pl ? L"Brak danych w tym zakresie" : L"No data in this range";
            m_rt->DrawTextW(msg, static_cast<UINT32>(wcslen(msg)), m_fmtCenterSmall, plot, m_solidBrush);
        } else {
            m_rt->PushAxisAlignedClip(plot, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            const float bw = pw / static_cast<float>(n);
            auto px = [&](int i) { return plot.left + (static_cast<float>(i) + 0.5f) * bw; };
            auto py = [&](float v) { return plot.bottom - std::clamp(v / scaleMax, 0.0f, 1.0f) * (ph - 2.0f) - 1.0f; };
            auto drawLine = [&](const std::vector<float>& s, uint32_t col, bool fill) {
                for (int i = 0; i < n; ++i) {
                    if (std::isnan(s[i])) continue;
                    if (fill) {
                        m_solidBrush->SetColor(HexColor(col, 0.16f));
                        m_rt->DrawLine(D2D1::Point2F(px(i), py(s[i])), D2D1::Point2F(px(i), plot.bottom), m_solidBrush, std::max(1.0f, bw));
                    }
                    m_solidBrush->SetColor(HexColor(col, 0.95f));
                    if (i > 0 && !std::isnan(s[i - 1])) {
                        m_rt->DrawLine(D2D1::Point2F(px(i - 1), py(s[i - 1])), D2D1::Point2F(px(i), py(s[i])), m_solidBrush, zoomed ? 2.0f : 1.7f);
                    } else {
                        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px(i), py(s[i])), 1.8f, 1.8f), m_solidBrush);
                    }
                }
            };
            drawLine(a, def.color, true);
            if (b) drawLine(*b, def.color2, false);
            if (hov >= 0) {
                m_solidBrush->SetColor(HexColor(0x475569, 0.55f));
                m_rt->DrawLine(D2D1::Point2F(px(hov), plot.top), D2D1::Point2F(px(hov), plot.bottom), m_solidBrush, 1.0f);
            }
            m_rt->PopAxisAlignedClip();
        }

        // Time axis (more ticks when zoomed)
        const int ticks = zoomed ? 6 : 2;
        for (int t = 0; t <= ticks; ++t) {
            float frac = static_cast<float>(t) / static_cast<float>(ticks);
            float tx = plot.left + pw * frac;
            std::wstring lbl = (t == ticks) ? std::wstring(pl ? L"teraz" : L"now") : fmtClock(tFrom + static_cast<int64_t>(rangeSec * static_cast<double>(frac)), dateInLabels);
            D2D1_RECT_F lr = D2D1::RectF(tx - 45.0f, plot.bottom + 3.0f, tx + 45.0f, plot.bottom + 20.0f);
            if (t == 0) lr = D2D1::RectF(tx, plot.bottom + 3.0f, tx + 90.0f, plot.bottom + 20.0f);
            m_fmtSmall->SetTextAlignment(t == 0 ? DWRITE_TEXT_ALIGNMENT_LEADING : (t == ticks ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_CENTER));
            if (t == ticks) lr = D2D1::RectF(tx - 90.0f, plot.bottom + 3.0f, tx, plot.bottom + 20.0f);
            DrawClippedText(lbl, lr, m_fmtSmall, 0x94A3B8);
        }
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    };

    const float cardH = 128.0f;

    if (zoomMode) {
        // ---- Zoomed: one chart fills the whole view ----
        const ChartDef* zoomDef = nullptr;
        for (const auto& c : charts) if (c.id == m_monZoomChart) { zoomDef = &c; break; }
        if (!zoomDef) {
            m_monZoomChart = -1;   // chart vanished (e.g. drive removed)
        } else {
            const MonitorSeriesCacheEntry& se = getSeries(*zoomDef, zoomBuckets);
            const float zh = std::max(260.0f, rect.bottom - curY - 10.0f);
            drawChart(*zoomDef, se, leftX, curY, availW, zh, true);
            curY += zh + gap;
        }
        m_hwMaxScrollY = 0.0f;
        m_hwScrollY = 0.0f;
        m_rt->PopAxisAlignedClip();
        return;
    }

    // ---- Chart grid: two per row; charts without any data (e.g. GPU power on a non-NVIDIA PC) stay hidden ----
    {
        std::vector<const ChartDef*> visible;
        for (const auto& c : charts) {
            const MonitorSeriesCacheEntry& se = getSeries(c, buckets);
            if (c.alwaysShow || se.hasData) visible.push_back(&c);
        }
        for (size_t i = 0; i < visible.size(); i += 2) {
            drawChart(*visible[i], getSeries(*visible[i], buckets), leftX, curY, halfW, cardH, false);
            if (i + 1 < visible.size()) drawChart(*visible[i + 1], getSeries(*visible[i + 1], buckets), rightX, curY, halfW, cardH, false);
            curY += cardH + gap;
        }
    }

    // ---- SMART history card (hourly snapshots per drive) ----
    {
        std::map<int, std::pair<const MonitorSmartPoint*, const MonitorSmartPoint*>> byDisk;   // first, last
        for (const auto& p : m_monSmart) {
            auto it = byDisk.find(p.disk);
            if (it == byDisk.end()) byDisk[p.disk] = { &p, &p };
            else it->second.second = &p;
        }
        const float rowH = 42.0f;
        const float h = 34.0f + static_cast<float>(std::max<size_t>(1, byDisk.size())) * rowH + 8.0f;
        DrawCardShell(leftX, curY, availW, h, pl ? L"SMART — HISTORIA STANU DYSKÓW (migawka co godzinę)" : L"SMART — DRIVE HEALTH HISTORY (hourly snapshot)", 0x0284C7, false);
        float ry = curY + 32.0f;
        bool needAdmin = false;
        if (byDisk.empty()) {
            DrawClippedText(pl ? L"Brak migawek SMART w tym zakresie — uruchom nagrywanie (pierwsza migawka powstaje od razu)." : L"No SMART snapshots in this range — start recording (the first snapshot is written immediately).",
                            D2D1::RectF(leftX + 12.0f, ry + 6.0f, leftX + availW - 12.0f, ry + 26.0f), m_fmtSmall, 0x64748B);
        }
        auto delta = [](int64_t last, int64_t first) -> std::wstring {
            if (last < 0 || first < 0 || last == first) return L"";
            wchar_t b[32] = {};
            swprintf_s(b, L" (%+lld)", static_cast<long long>(last - first));
            return b;
        };
        for (const auto& kv : byDisk) {
            const MonitorSmartPoint& f = *kv.second.first;
            const MonitorSmartPoint& l = *kv.second.second;
            std::wstring model;
            for (const auto& pd : m_hwInspector.GetPhysicalDisks()) if (pd.index == l.disk) { model = pd.model; break; }

            uint32_t hcol = l.health >= 2 ? 0xDC2626 : (l.health == 1 ? 0xD97706 : (l.health == 0 ? 0x059669 : 0x64748B));
            const wchar_t* hname = l.health >= 2 ? (pl ? L"AWARIA" : L"FAILING") : (l.health == 1 ? (pl ? L"OSTRZEŻENIE" : L"WARNING")
                                  : (l.health == 0 ? (pl ? L"ZDROWY" : L"HEALTHY") : (pl ? L"SMART NIEDOSTĘPNY" : L"SMART UNAVAILABLE")));
            m_solidBrush->SetColor(HexColor(hcol));
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(leftX + 18.0f, ry + 11.0f), 5.0f, 5.0f), m_solidBrush);
            std::wstring line1 = (pl ? L"Dysk #" : L"Drive #") + std::to_wstring(l.disk) + (model.empty() ? L"" : (L"  \x2022  " + model));
            DrawClippedText(line1, D2D1::RectF(leftX + 30.0f, ry + 1.0f, leftX + availW * 0.62f, ry + 21.0f), m_fmtBold, 0x0F172A);
            m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            DrawClippedText(hname, D2D1::RectF(leftX + availW * 0.62f, ry + 1.0f, leftX + availW - 14.0f, ry + 21.0f), m_fmtBold, hcol);
            m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

            std::wstring line2;
            auto addPart = [&](const std::wstring& s) { if (!line2.empty()) line2 += L"   \x2022   "; line2 += s; };
            if (l.health < 0) {
                needAdmin = true;
                addPart(pl ? L"odczyt SMART tego dysku wymaga uprawnień administratora (tryb „Rejestrator jako administrator”)" : L"reading SMART of this drive needs administrator rights (\"Recorder as administrator\" mode)");
                if (l.tempC >= 0) addPart((pl ? L"temp. " : L"temp ") + std::to_wstring(l.tempC) + L"\x00B0" L"C");
            } else {
                if (l.tempC >= 0) addPart((pl ? L"temp. " : L"temp ") + std::to_wstring(l.tempC) + L"\x00B0" L"C");
                if (l.lifePct >= 0) addPart((pl ? L"życie " : L"life ") + std::to_wstring(l.lifePct) + L"%" + delta(l.lifePct, f.lifePct));
                if (l.powerOnHours >= 0) addPart((pl ? L"praca " : L"power-on ") + std::to_wstring(l.powerOnHours) + L" h");
                if (l.powerCycles >= 0) addPart((pl ? L"cykle " : L"cycles ") + std::to_wstring(l.powerCycles) + delta(l.powerCycles, f.powerCycles));
                if (l.writtenGB >= 0) addPart((pl ? L"zapis " : L"written ") + FileEngine::FormatSizeDetailed(static_cast<uint64_t>(l.writtenGB) * 1024ull * 1024ull * 1024ull));
                if (l.readGB >= 0) addPart((pl ? L"odczyt " : L"read ") + FileEngine::FormatSizeDetailed(static_cast<uint64_t>(l.readGB) * 1024ull * 1024ull * 1024ull));
                if (l.reallocated >= 0) addPart((pl ? L"realokowane " : L"reallocated ") + std::to_wstring(l.reallocated) + delta(l.reallocated, f.reallocated));
                if (l.pending >= 0) addPart((pl ? L"oczekujące " : L"pending ") + std::to_wstring(l.pending) + delta(l.pending, f.pending));
                if (l.uncorrectable >= 0) addPart((pl ? L"niekorygowalne " : L"uncorrectable ") + std::to_wstring(l.uncorrectable) + delta(l.uncorrectable, f.uncorrectable));
                if (l.crcErrors >= 0) addPart(L"CRC " + std::to_wstring(l.crcErrors) + delta(l.crcErrors, f.crcErrors));
                if (l.unsafeShutdowns >= 0) addPart((pl ? L"nagłe wyłączenia " : L"unsafe shutdowns ") + std::to_wstring(l.unsafeShutdowns) + delta(l.unsafeShutdowns, f.unsafeShutdowns));
                if (l.mediaErrors >= 0) addPart((pl ? L"błędy nośnika " : L"media errors ") + std::to_wstring(l.mediaErrors) + delta(l.mediaErrors, f.mediaErrors));
                if (l.spare >= 0) addPart((pl ? L"zapas " : L"spare ") + std::to_wstring(l.spare) + L"%");
            }
            DrawClippedText(line2, D2D1::RectF(leftX + 30.0f, ry + 21.0f, leftX + availW - 14.0f, ry + 41.0f), m_fmtSmall, 0x475569);
            ry += rowH;
        }
        if (needAdmin && !m_monSettings.elevated) {
            DrawCardTinyButton(D2D1::RectF(leftX + availW - 290.0f, curY + 4.0f, leftX + availW - 8.0f, curY + 25.0f),
                               pl ? L"\xD83D\xDEE1 Włącz rejestrator jako administrator" : L"\xD83D\xDEE1 Record as administrator", CMD_MON_ELEVATED, L"");
        }
        curY += h + gap;
    }

    // ---- Events (gaps, health changes, hot components, growing error counters) + volumes (free-space trend) ----
    struct MonEvent { int64_t ts; int level; std::wstring text; };
    std::vector<MonEvent> events;
    {
        std::map<int, int> lastHealth, lastTempState;
        bool gpuHot = false;
        int64_t prevTs = 0;
        const int64_t gapLimit = static_cast<int64_t>(m_monSettings.intervalSec * 2.5) + 30;
        auto healthName = [&](int hl) -> const wchar_t* {
            return hl >= 2 ? (pl ? L"awaria" : L"failing") : (hl == 1 ? (pl ? L"ostrzeżenie" : L"warning") : (pl ? L"zdrowy" : L"healthy"));
        };
        for (const MonitorSample* sp : samples) {
            const MonitorSample& s = *sp;
            if (prevTs != 0 && s.ts - prevTs > gapLimit) {
                wchar_t b[200] = {};
                swprintf_s(b, pl ? L"Przerwa w nagrywaniu: %lld min (komputer uśpiony / wyłączony lub rejestrator zatrzymany)"
                                 : L"Recording gap: %lld min (computer asleep / off or recorder stopped)",
                           static_cast<long long>((s.ts - prevTs) / 60));
                events.push_back({ s.ts, 1, b });
            }
            for (const auto& d : s.disks) {
                if (d.health >= 0) {
                    auto it = lastHealth.find(d.index);
                    if (it != lastHealth.end() && it->second != d.health) {
                        std::wstring t = (pl ? L"Dysk #" : L"Drive #") + std::to_wstring(d.index) + (pl ? L": stan zdrowia " : L": health ") +
                                         healthName(it->second) + L" \x2192 " + healthName(d.health);
                        events.push_back({ s.ts, d.health >= 2 ? 2 : (d.health == 1 ? 1 : 0), t });
                    }
                    lastHealth[d.index] = d.health;
                }
                if (d.tempC >= 0) {
                    float warn = 60.0f, crit = 70.0f;
                    for (const auto& pd : m_hwInspector.GetPhysicalDisks()) {
                        if (pd.index != d.index) continue;
                        warn = pd.tempWarnC > 0 ? static_cast<float>(pd.tempWarnC) : (pd.isNvme ? 70.0f : (pd.isRotational ? 50.0f : 60.0f));
                        crit = pd.tempCritC > 0 ? static_cast<float>(pd.tempCritC) : (pd.isNvme ? 80.0f : (pd.isRotational ? 60.0f : 70.0f));
                    }
                    int state = d.tempC >= crit ? 2 : (d.tempC >= warn ? 1 : 0);
                    int& last = lastTempState[d.index];
                    if (state > last) {
                        wchar_t b[160] = {};
                        swprintf_s(b, pl ? L"Dysk #%d: temperatura %d\x00B0" L"C (%s)" : L"Drive #%d: temperature %d\x00B0" L"C (%s)",
                                   d.index, d.tempC, state == 2 ? (pl ? L"krytyczna" : L"critical") : (pl ? L"podwyższona" : L"elevated"));
                        events.push_back({ s.ts, state, b });
                    }
                    last = state;
                }
            }
            if (s.gpuTempC >= 85 && !gpuHot) {
                wchar_t b[120] = {};
                swprintf_s(b, pl ? L"GPU: wysoka temperatura %d\x00B0" L"C" : L"GPU: high temperature %d\x00B0" L"C", s.gpuTempC);
                events.push_back({ s.ts, 1, b });
                gpuHot = true;
            } else if (s.gpuTempC >= 0 && s.gpuTempC < 80) {
                gpuHot = false;
            }
            prevTs = s.ts;
        }

        // Error counters that grew between two SMART snapshots
        std::map<int, const MonitorSmartPoint*> prevSmart;
        for (const auto& p : m_monSmart) {
            auto it = prevSmart.find(p.disk);
            if (it != prevSmart.end()) {
                const MonitorSmartPoint& o = *it->second;
                struct Counter { const wchar_t* pl; const wchar_t* en; int64_t now, before; int level; };
                const Counter counters[] = {
                    { L"realokowane sektory", L"reallocated sectors", p.reallocated, o.reallocated, 2 },
                    { L"oczekujące sektory", L"pending sectors", p.pending, o.pending, 2 },
                    { L"niekorygowalne błędy", L"uncorrectable errors", p.uncorrectable, o.uncorrectable, 2 },
                    { L"błędy nośnika", L"media errors", p.mediaErrors, o.mediaErrors, 2 },
                    { L"błędy CRC", L"CRC errors", p.crcErrors, o.crcErrors, 1 },
                    { L"nagłe wyłączenia", L"unsafe shutdowns", p.unsafeShutdowns, o.unsafeShutdowns, 0 },
                };
                for (const auto& c : counters) {
                    if (c.now >= 0 && c.before >= 0 && c.now > c.before) {
                        std::wstring t = (pl ? L"Dysk #" : L"Drive #") + std::to_wstring(p.disk) + L": " + (pl ? c.pl : c.en) +
                                         L" +" + std::to_wstring(c.now - c.before) + L" (" + std::to_wstring(c.now) + L")";
                        events.push_back({ p.ts, c.level, t });
                    }
                }
            }
            prevSmart[p.disk] = &p;
        }

        std::sort(events.begin(), events.end(), [](const MonEvent& a, const MonEvent& b) { return a.ts > b.ts; });   // newest first
        if (events.size() > 14) events.resize(14);
    }

    {
        const size_t evRows = std::max<size_t>(1, events.size());
        const MonitorSample* lastS = samples.empty() ? nullptr : samples.back();
        const size_t volRows = std::max<size_t>(1, lastS ? lastS->volumes.size() : 0);
        const float rowH = 20.0f;
        const float h = 34.0f + static_cast<float>(std::max(evRows, volRows)) * rowH + 8.0f;

        // Events card
        DrawCardShell(leftX, curY, halfW, h, pl ? L"ZDARZENIA W WYBRANYM ZAKRESIE" : L"EVENTS IN THE SELECTED RANGE", 0xD97706, false);
        float ry = curY + 32.0f;
        if (events.empty()) {
            DrawClippedText(pl ? L"Brak zdarzeń — wszystko w normie." : L"No events — everything looks normal.",
                            D2D1::RectF(leftX + 12.0f, ry + 2.0f, leftX + halfW - 10.0f, ry + rowH), m_fmtSmall, 0x64748B);
        }
        for (const auto& ev : events) {
            uint32_t col = ev.level >= 2 ? 0xDC2626 : (ev.level == 1 ? 0xD97706 : 0x059669);
            m_solidBrush->SetColor(HexColor(col));
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(leftX + 16.0f, ry + 10.0f), 4.0f, 4.0f), m_solidBrush);
            DrawClippedText(fmtClock(ev.ts, true), D2D1::RectF(leftX + 28.0f, ry + 2.0f, leftX + 98.0f, ry + rowH), m_fmtSmall, 0x64748B);
            DrawClippedText(ev.text, D2D1::RectF(leftX + 102.0f, ry + 2.0f, leftX + halfW - 8.0f, ry + rowH), m_fmtSmall, 0x1E293B);
            ry += rowH;
        }

        // Volumes card: current fill level and free-space change over the range
        DrawCardShell(rightX, curY, halfW, h, pl ? L"WOLUMINY — WOLNE MIEJSCE (TREND)" : L"VOLUMES — FREE SPACE (TREND)", 0x059669, false);
        float vy = curY + 32.0f;
        if (!lastS || lastS->volumes.empty()) {
            DrawClippedText(pl ? L"Brak danych o woluminach." : L"No volume data.", D2D1::RectF(rightX + 12.0f, vy + 2.0f, rightX + halfW - 10.0f, vy + rowH), m_fmtSmall, 0x64748B);
        } else {
            for (const auto& v : lastS->volumes) {
                float firstFree = v.freeGB;
                for (const MonitorSample* sp : samples) {
                    bool found = false;
                    for (const auto& sv : sp->volumes) if (sv.letter == v.letter) { firstFree = sv.freeGB; found = true; break; }
                    if (found) break;
                }
                float delta = v.freeGB - firstFree;
                uint32_t col = v.usedPct > 90.0f ? 0xDC2626 : (v.usedPct > 75.0f ? 0xD97706 : 0x059669);
                wchar_t b[200] = {};
                swprintf_s(b, pl ? L"zajęte %.1f%%   \x2022   wolne %.1f GB   \x2022   zmiana: %+.1f GB" : L"used %.1f%%   \x2022   free %.1f GB   \x2022   change: %+.1f GB",
                           v.usedPct, v.freeGB, delta);
                wchar_t k[8] = { v.letter, L':', 0 };
                DrawClippedText(k, D2D1::RectF(rightX + 12.0f, vy + 2.0f, rightX + 44.0f, vy + rowH), m_fmtBold, 0x0F172A);
                DrawClippedText(b, D2D1::RectF(rightX + 44.0f, vy + 2.0f, rightX + halfW - 8.0f, vy + rowH), m_fmtSmall, col);
                vy += rowH;
            }
        }
        curY += h + gap;
    }

    const float totalHeight = (curY + m_hwScrollY) - startY;
    const float viewH = std::max(1.0f, rect.bottom - rect.top);
    m_hwMaxScrollY = std::max(0.0f, totalHeight - viewH + 16.0f);
    m_hwScrollY = std::clamp(m_hwScrollY, 0.0f, m_hwMaxScrollY);

    m_rt->PopAxisAlignedClip();
}

// Right-side dock shown for folders inside a git repository: branch, status, changes, commits, branches.
void ExplorerApp::DrawGitDock(const D2D1_RECT_F& dock) {
    m_gitHits.clear();
    const bool pl = (m_lang == Lang::PL);

    // Copy the worker's state only when it changed (version counter), never every frame
    const uint64_t ver = m_git.StateVersion();
    if (ver != m_gitStateVersion) {
        m_gitState = m_git.SnapshotState();
        m_gitOutput = m_git.SnapshotOutput();
        m_gitOutputOk = m_git.LastActionSucceeded();
        m_gitStateVersion = ver;
    }
    const GitRepoState& st = m_gitState;
    const bool busy = m_git.IsBusy();
    const bool loaded = st.valid && st.root == m_gitRoot;

    const float left = dock.left, right = dock.right, top = dock.top;
    const float w = right - left;

    m_rt->PushAxisAlignedClip(dock, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRectangle(dock, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xDADADA));
    m_rt->DrawLine(D2D1::Point2F(left, top), D2D1::Point2F(left, dock.bottom), m_solidBrush, 1.0f);

    auto gitBtn = [&](float bx, float by, float bw, const std::wstring& label, int kind, const std::wstring& arg, int index = 0) {
        D2D1_RECT_F r = D2D1::RectF(bx, by, bx + bw, by + 22.0f);
        bool hov = PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, r);
        m_solidBrush->SetColor(HexColor(hov ? 0x0078D7 : 0xF1F5F9));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(r, 3.0f, 3.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(hov ? 0x005A9E : 0xCBD5E1));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(r, 3.0f, 3.0f), m_solidBrush, 1.0f);
        m_solidBrush->SetColor(HexColor(hov ? 0xFFFFFF : 0x1E293B));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), m_fmtSmall, r, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        GitDockHit h;
        h.rect = r; h.kind = kind; h.arg = arg; h.index = index;
        m_gitHits.push_back(std::move(h));
    };

    // ---- Header: branch, sync state, summary, actions ----
    const float headerH = 82.0f;
    m_solidBrush->SetColor(HexColor(0xF8FAFC));
    m_rt->FillRectangle(D2D1::RectF(left + 1.0f, top, right, top + headerH), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xE2E8F0));
    m_rt->DrawLine(D2D1::Point2F(left + 1.0f, top + headerH), D2D1::Point2F(right, top + headerH), m_solidBrush, 1.0f);

    D2D1_RECT_F badge = D2D1::RectF(left + 10.0f, top + 9.0f, left + 38.0f, top + 25.0f);
    m_solidBrush->SetColor(HexColor(0xF05133));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(badge, 3.0f, 3.0f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_rt->DrawTextW(L"git", 3, m_fmtSmall, badge, m_solidBrush);
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    std::wstring branchTxt = PathUtils::GetFileName(m_gitRoot);
    if (loaded) {
        branchTxt = st.detachedHead ? ((pl ? L"HEAD odłączony @ " : L"detached HEAD @ ") + st.headShort) : st.branch;
    }
    DrawClippedText(branchTxt, D2D1::RectF(left + 46.0f, top + 6.0f, right - 96.0f, top + 28.0f), m_fmtBold, 0x0F172A);
    if (loaded && !st.detachedHead) {
        std::wstring ab;
        if (st.upstream.empty()) ab = pl ? L"brak upstream" : L"no upstream";
        else ab = L"\x2191" + std::to_wstring(st.ahead) + L" \x2193" + std::to_wstring(st.behind);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        DrawClippedText(ab, D2D1::RectF(right - 96.0f, top + 9.0f, right - 10.0f, top + 28.0f), m_fmtSmall,
                        (st.ahead > 0 || st.behind > 0) ? 0xB45309 : 0x64748B);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    std::wstring summary;
    uint32_t summaryCol = 0x475569;
    if (st.gitMissing) {
        summary = pl ? L"Git nie jest zainstalowany" : L"Git is not installed";
        summaryCol = 0xB91C1C;
    } else if (!loaded) {
        summary = st.lastError.empty() ? (pl ? L"Wczytywanie stanu repozytorium…" : L"Reading repository state…") : st.lastError;
    } else if (st.stagedCount + st.modifiedCount + st.untrackedCount + st.conflictCount == 0) {
        summary = pl ? L"\x2713 drzewo robocze czyste" : L"\x2713 working tree clean";
        summaryCol = 0x059669;
    } else {
        auto add = [&](int n, const wchar_t* plTxt, const wchar_t* enTxt) {
            if (n <= 0) return;
            if (!summary.empty()) summary += L"  \x2022  ";
            summary += std::to_wstring(n) + L" " + (pl ? plTxt : enTxt);
        };
        add(st.modifiedCount, L"zmienione", L"modified");
        add(st.stagedCount, L"w indeksie", L"staged");
        add(st.untrackedCount, L"nowe", L"untracked");
        add(st.conflictCount, L"konflikty", L"conflicts");
        if (st.conflictCount > 0) summaryCol = 0xB91C1C;
    }
    if (loaded && st.stashCount > 0) summary += L"  \x2022  stash " + std::to_wstring(st.stashCount);
    DrawClippedText(summary, D2D1::RectF(left + 10.0f, top + 30.0f, right - 10.0f, top + 48.0f), m_fmtSmall, summaryCol);

    float bx = left + 10.0f;
    const float by = top + 52.0f;
    gitBtn(bx, by, 74.0f, pl ? L"\x27F3 Odśwież" : L"\x27F3 Refresh", GH_Refresh, L"");   bx += 80.0f;
    gitBtn(bx, by, 46.0f, L"Fetch", GH_Fetch, L"");                                          bx += 52.0f;
    gitBtn(bx, by, 42.0f, L"Pull", GH_Pull, L"");                                            bx += 48.0f;
    gitBtn(bx, by, 44.0f, L"Push", GH_Push, L"");                                            bx += 50.0f;
    gitBtn(bx, by, 44.0f, L"Diff", GH_DiffStat, L"");
    if (busy) {
        std::wstring bd = m_git.BusyDescription();
        if (bd.size() > 26) bd = bd.substr(0, 25) + L"\x2026";
        if (right - (bx + 52.0f) > 60.0f) {
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            DrawClippedText(L"\x23F3 " + bd, D2D1::RectF(bx + 52.0f, by + 3.0f, right - 8.0f, by + 22.0f), m_fmtSmall, 0xB45309);
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }

    // ---- Problem states: git missing / not readable ----
    if (st.gitMissing || (!loaded && !st.lastError.empty() && !busy)) {
        std::wstring msg = st.gitMissing
            ? (pl ? L"Aby zobaczyć commity i gałęzie zainstaluj Git for Windows (git-scm.com) i otwórz folder ponownie."
                  : L"Install Git for Windows (git-scm.com) to see commits and branches, then reopen the folder.")
            : st.lastError;
        m_rt->PushAxisAlignedClip(D2D1::RectF(left + 10.0f, top + headerH + 10.0f, right - 10.0f, dock.bottom - 10.0f), D2D1_ANTIALIAS_MODE_ALIASED);
        m_solidBrush->SetColor(HexColor(0x64748B));
        m_rt->DrawTextW(msg.c_str(), static_cast<UINT32>(msg.size()), m_fmtSmall, D2D1::RectF(left + 10.0f, top + headerH + 10.0f, right - 10.0f, dock.bottom - 10.0f), m_solidBrush);
        m_rt->PopAxisAlignedClip();
        m_gitMaxScrollY = 0.0f;
        m_rt->PopAxisAlignedClip();
        return;
    }

    // ---- Tabs ----
    const float tabsTop = top + headerH + 2.0f;
    const float tabsH = 26.0f;
    const int changeCount = st.stagedCount + st.modifiedCount + st.untrackedCount + st.conflictCount;
    const std::wstring tabLabels[3] = {
        (pl ? L"Zmiany (" : L"Changes (") + std::to_wstring(loaded ? changeCount : 0) + L")",
        (pl ? L"Commity (" : L"Commits (") + std::to_wstring(loaded ? st.commits.size() : 0) + L")",
        (pl ? L"Gałęzie (" : L"Branches (") + std::to_wstring(loaded ? st.branches.size() : 0) + L")"
    };
    const float tabW = (w - 2.0f) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        D2D1_RECT_F tr = D2D1::RectF(left + 1.0f + i * tabW, tabsTop, left + 1.0f + (i + 1) * tabW, tabsTop + tabsH);
        bool act = (m_gitTab == i);
        bool hov = PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, tr);
        if (hov && !act) {
            m_solidBrush->SetColor(HexColor(0xF1F5F9));
            m_rt->FillRectangle(tr, m_solidBrush);
        }
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        DrawClippedText(tabLabels[i], D2D1::RectF(tr.left, tr.top + 5.0f, tr.right, tr.bottom), act ? m_fmtBold : m_fmtSmall, act ? 0x0067B8 : 0x475569);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        if (act) {
            m_solidBrush->SetColor(HexColor(0x0078D7));
            m_rt->FillRectangle(D2D1::RectF(tr.left + 6.0f, tr.bottom - 2.0f, tr.right - 6.0f, tr.bottom), m_solidBrush);
        }
        GitDockHit h;
        h.rect = tr; h.kind = GH_Tab; h.index = i;
        m_gitHits.push_back(std::move(h));
    }
    m_solidBrush->SetColor(HexColor(0xE2E8F0));
    m_rt->DrawLine(D2D1::Point2F(left + 1.0f, tabsTop + tabsH), D2D1::Point2F(right, tabsTop + tabsH), m_solidBrush, 1.0f);

    // ---- Output panel (result of fetch / pull / push / switch / show commit / diff) ----
    const float bodyTop = tabsTop + tabsH + 1.0f;
    float outH = 0.0f;
    if (m_gitShowOutput && (busy || !m_gitOutput.empty())) {
        outH = std::clamp((dock.bottom - bodyTop) * 0.42f, 120.0f, 280.0f);
    }
    const float listBottom = dock.bottom - outH;
    const D2D1_RECT_F listR = D2D1::RectF(left + 1.0f, bodyTop, right, listBottom);

    // ---- Scrollable list ----
    const float viewH = std::max(1.0f, listBottom - bodyTop);
    float contentH = 0.0f;
    m_rt->PushAxisAlignedClip(listR, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const bool mouseInList = PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, listR);

    auto rowVisible = [&](float y, float h) { return y + h >= listR.top && y <= listR.bottom; };
    auto addRowHit = [&](const D2D1_RECT_F& rr, int kind, const std::wstring& arg) {
        D2D1_RECT_F hr = D2D1::RectF(rr.left, std::max(rr.top, listR.top), rr.right, std::min(rr.bottom, listR.bottom));
        if (hr.bottom <= hr.top) return;
        GitDockHit h;
        h.rect = hr; h.kind = kind; h.arg = arg;
        m_gitHits.push_back(std::move(h));
    };
    auto emptyMsg = [&](const wchar_t* text) {
        DrawClippedText(text, D2D1::RectF(left + 12.0f, bodyTop + 12.0f, right - 10.0f, bodyTop + 50.0f), m_fmtSmall, 0x64748B);
        contentH = 50.0f;
    };

    if (!loaded) {
        emptyMsg(pl ? L"Wczytywanie…" : L"Loading…");
    } else if (m_gitTab == 0) {
        const float rowH = 22.0f;
        if (st.changes.empty()) {
            emptyMsg(pl ? L"Brak zmian — drzewo robocze czyste." : L"No changes — working tree clean.");
        }
        float y = bodyTop - m_gitScrollY;
        for (const GitFileChange& c : st.changes) {
            if (rowVisible(y, rowH)) {
                D2D1_RECT_F rr = D2D1::RectF(left + 2.0f, y, right - 2.0f, y + rowH);
                if (mouseInList && PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, rr)) {
                    m_solidBrush->SetColor(HexColor(0xE8F1FB));
                    m_rt->FillRectangle(rr, m_solidBrush);
                }
                const bool conflict = (c.indexStatus == L'U' || c.workStatus == L'U' || (c.indexStatus == L'A' && c.workStatus == L'A') || (c.indexStatus == L'D' && c.workStatus == L'D'));
                uint32_t col = conflict ? 0xDC2626 : (c.workStatus == L'?' ? 0x94A3B8 :
                               (c.workStatus == L'D' || c.indexStatus == L'D' ? 0xDC2626 :
                               (c.indexStatus == L'A' ? 0x059669 : (c.indexStatus == L'R' ? 0x0284C7 :
                               (c.workStatus != L' ' ? 0xD97706 : 0x059669)))));
                std::wstring code;
                code += (c.indexStatus == L' ') ? L'\x00B7' : c.indexStatus;
                code += (c.workStatus == L' ') ? L'\x00B7' : c.workStatus;
                m_solidBrush->SetColor(HexColor(col, 0.14f));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rr.left + 6.0f, y + 3.0f, rr.left + 32.0f, y + rowH - 3.0f), 3.0f, 3.0f), m_solidBrush);
                m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                DrawClippedText(code, D2D1::RectF(rr.left + 6.0f, y + 3.0f, rr.left + 32.0f, y + rowH), m_fmtBold, col);
                m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                std::wstring p = c.origPath.empty() ? c.path : (c.origPath + L" \x2192 " + c.path);
                DrawClippedText(p, D2D1::RectF(rr.left + 38.0f, y + 3.0f, rr.right - 6.0f, y + rowH), m_fmtSmall, 0x1E293B);
                addRowHit(rr, GH_File, c.path);
            }
            y += rowH;
        }
        if (!st.changes.empty()) contentH = static_cast<float>(st.changes.size()) * rowH;
        if (static_cast<int>(st.changes.size()) < changeCount) {
            std::wstring more = (pl ? L"… oraz " : L"… and ") + std::to_wstring(changeCount - static_cast<int>(st.changes.size())) + (pl ? L" kolejnych plików" : L" more files");
            DrawClippedText(more, D2D1::RectF(left + 12.0f, y + 2.0f, right - 8.0f, y + 20.0f), m_fmtSmall, 0x64748B);
            contentH += 22.0f;
        }
    } else if (m_gitTab == 1) {
        const float rowH = 46.0f;
        if (st.commits.empty()) {
            emptyMsg(pl ? L"Brak commitów w tym repozytorium." : L"No commits in this repository.");
        }
        float y = bodyTop - m_gitScrollY;
        for (const GitCommit& c : st.commits) {
            if (rowVisible(y, rowH)) {
                D2D1_RECT_F rr = D2D1::RectF(left + 2.0f, y, right - 2.0f, y + rowH - 1.0f);
                bool sel = (c.hash == m_gitSelectedCommit);
                bool hov = mouseInList && PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, rr);
                if (sel || hov) {
                    m_solidBrush->SetColor(HexColor(sel ? 0xE0F2FE : 0xF1F5F9));
                    m_rt->FillRectangle(rr, m_solidBrush);
                }
                if (sel) {
                    m_solidBrush->SetColor(HexColor(0x0078D7));
                    m_rt->FillRectangle(D2D1::RectF(rr.left, rr.top, rr.left + 3.0f, rr.bottom), m_solidBrush);
                }
                DrawClippedText(c.subject, D2D1::RectF(rr.left + 10.0f, y + 4.0f, rr.right - 8.0f, y + 24.0f), m_fmtBold, 0x0F172A);
                std::wstring meta = c.author + L"  \x2022  " + c.relDate;
                DrawClippedText(c.shortHash, D2D1::RectF(rr.left + 10.0f, y + 25.0f, rr.left + 70.0f, y + rowH), m_fmtSmall, 0x0067B8);
                DrawClippedText(meta, D2D1::RectF(rr.left + 70.0f, y + 25.0f, c.refs.empty() ? rr.right - 8.0f : rr.left + 70.0f + (rr.right - rr.left - 78.0f) * 0.55f, y + rowH), m_fmtSmall, 0x64748B);
                if (!c.refs.empty()) {
                    float rx0 = rr.left + 70.0f + (rr.right - rr.left - 78.0f) * 0.55f + 4.0f;
                    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                    DrawClippedText(c.refs, D2D1::RectF(rx0, y + 25.0f, rr.right - 8.0f, y + rowH), m_fmtSmall, 0xB45309);
                    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                }
                addRowHit(rr, GH_Commit, c.hash);
            }
            y += rowH;
        }
        if (!st.commits.empty()) contentH = static_cast<float>(st.commits.size()) * rowH;
    } else {
        const float rowH = 26.0f, labelH = 22.0f;
        float y = bodyTop - m_gitScrollY;
        float total = 0.0f;
        bool remoteLabelDone = false, localLabelDone = false;
        if (st.branches.empty()) emptyMsg(pl ? L"Brak gałęzi." : L"No branches.");
        for (const GitBranch& b : st.branches) {
            if (!b.isRemote && !localLabelDone) {
                localLabelDone = true;
                if (rowVisible(y, labelH)) DrawClippedText(pl ? L"LOKALNE" : L"LOCAL", D2D1::RectF(left + 12.0f, y + 4.0f, right - 8.0f, y + labelH), m_fmtBold, 0x64748B);
                y += labelH; total += labelH;
            }
            if (b.isRemote && !remoteLabelDone) {
                remoteLabelDone = true;
                if (rowVisible(y, labelH)) DrawClippedText(pl ? L"ZDALNE" : L"REMOTE", D2D1::RectF(left + 12.0f, y + 4.0f, right - 8.0f, y + labelH), m_fmtBold, 0x64748B);
                y += labelH; total += labelH;
            }
            if (rowVisible(y, rowH)) {
                D2D1_RECT_F rr = D2D1::RectF(left + 2.0f, y, right - 2.0f, y + rowH);
                bool hov = mouseInList && PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, rr);
                if (b.isCurrent || hov) {
                    m_solidBrush->SetColor(HexColor(b.isCurrent ? 0xE6F7EE : 0xF1F5F9));
                    m_rt->FillRectangle(rr, m_solidBrush);
                }
                if (b.isCurrent) {
                    m_solidBrush->SetColor(HexColor(0x10B981));
                    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rr.left + 14.0f, y + rowH * 0.5f), 4.0f, 4.0f), m_solidBrush);
                }
                float trackW = b.track.empty() ? 0.0f : 96.0f;
                DrawClippedText(b.name, D2D1::RectF(rr.left + 26.0f, y + 5.0f, rr.right - 8.0f - trackW, y + rowH), b.isCurrent ? m_fmtBold : m_fmtSmall,
                                b.isRemote ? 0x64748B : 0x0F172A);
                if (!b.track.empty()) {
                    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                    DrawClippedText(b.track, D2D1::RectF(rr.right - 8.0f - trackW, y + 5.0f, rr.right - 8.0f, y + rowH), m_fmtSmall, 0xB45309);
                    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                }
                if (!b.isCurrent) addRowHit(rr, GH_Branch, b.name);
            }
            y += rowH; total += rowH;
        }
        if (total > 0.0f) contentH = total;
    }
    m_rt->PopAxisAlignedClip();

    m_gitMaxScrollY = std::max(0.0f, contentH - viewH + 8.0f);
    m_gitScrollY = std::clamp(m_gitScrollY, 0.0f, m_gitMaxScrollY);
    if (m_gitMaxScrollY > 0.0f) {
        float trackH = viewH - 4.0f;
        float thumbH = std::max(24.0f, trackH * viewH / (contentH + 8.0f));
        float thumbY = bodyTop + 2.0f + (trackH - thumbH) * (m_gitScrollY / m_gitMaxScrollY);
        m_solidBrush->SetColor(HexColor(0xCBD5E1));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(right - 6.0f, thumbY, right - 2.0f, thumbY + thumbH), 2.0f, 2.0f), m_solidBrush);
    }

    // ---- Output panel ----
    if (outH > 0.0f) {
        D2D1_RECT_F op = D2D1::RectF(left + 1.0f, listBottom, right, dock.bottom);
        m_solidBrush->SetColor(HexColor(0xFAFBFC));
        m_rt->FillRectangle(op, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xCBD5E1));
        m_rt->DrawLine(D2D1::Point2F(op.left, op.top), D2D1::Point2F(op.right, op.top), m_solidBrush, 1.0f);

        std::wstring title = busy ? (L"\x23F3 " + m_git.BusyDescription())
                                  : (m_gitOutputOk ? (pl ? L"\x2713 Wynik git" : L"\x2713 git output") : (pl ? L"\x2717 Błąd git" : L"\x2717 git error"));
        float rb = op.right - 8.0f;
        gitBtn(rb - 24.0f, op.top + 4.0f, 24.0f, L"\x2715", GH_CloseOutput, L"");
        rb -= 30.0f;
        gitBtn(rb - 64.0f, op.top + 4.0f, 64.0f, pl ? L"Kopiuj" : L"Copy", GH_CopyOutput, L"");
        rb -= 70.0f;
        if (!m_gitSelectedCommit.empty() && !busy) {
            gitBtn(rb - 84.0f, op.top + 4.0f, 84.0f, pl ? L"Kopiuj hash" : L"Copy hash", GH_CopyHash, m_gitSelectedCommit);
            rb -= 90.0f;
        }
        DrawClippedText(title, D2D1::RectF(op.left + 10.0f, op.top + 6.0f, std::max(op.left + 40.0f, rb), op.top + 26.0f), m_fmtBold,
                        busy ? 0xB45309 : (m_gitOutputOk ? 0x047857 : 0xB91C1C));

        D2D1_RECT_F tx = D2D1::RectF(op.left + 10.0f, op.top + 30.0f, op.right - 10.0f, op.bottom - 6.0f);
        m_rt->PushAxisAlignedClip(tx, D2D1_ANTIALIAS_MODE_ALIASED);
        const std::wstring& shown = m_gitOutput.empty() ? (pl ? std::wstring(L"Trwa wykonywanie…") : std::wstring(L"Working…")) : m_gitOutput;
        m_solidBrush->SetColor(HexColor(m_gitOutputOk || busy ? 0x1E293B : 0xB91C1C));
        m_rt->DrawTextW(shown.c_str(), static_cast<UINT32>(shown.size()), m_fmtSmall, tx, m_solidBrush);
        m_rt->PopAxisAlignedClip();
    }

    m_rt->PopAxisAlignedClip();
}

void ExplorerApp::DrawLanView(const D2D1_RECT_F& rect) {
    const bool pl = (m_lang == Lang::PL);

    // First visit: pick the adapter and kick off a scan + SSDP discovery
    if (!m_lanInit) {
        m_lanInit = true;
        m_lanAdapters = LanScanner::EnumerateAdapters();   // adapters with a gateway first
        m_lanAdapterIdx = 0;
        if (!m_lanAdapters.empty()) {
            m_lan.StartScan(m_lanAdapters[0]);
            m_lan.StartSsdp();
            EnsureWorkAnimTimerState();   // keeps the view repainting while the scan runs
        }
    }

    const bool scanning = m_lan.IsScanning();
    const bool ssdpRun = m_lan.IsSsdpRunning();
    const bool portRun = m_lan.IsPortScanning();
    const std::vector<LanHost> hosts = m_lan.SnapshotHosts();
    const std::vector<SsdpDevice> ssdp = m_lan.SnapshotSsdp();
    int done = 0, total = 0;
    m_lan.GetScanProgress(done, total);

    auto findSsdp = [&](const std::wstring& ip) -> const SsdpDevice* {
        for (const auto& d : ssdp) if (d.ip == ip) return &d;
        return nullptr;
    };

    m_rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_solidBrush->SetColor(HexColor(0xF3F5F8));
    m_rt->FillRectangle(rect, m_solidBrush);

    const float padX = 10.0f, gap = 10.0f;
    const float availW = std::max(420.0f, (rect.right - rect.left) - padX * 2.0f);
    const float leftX = rect.left + padX;
    const float startY = rect.top + 8.0f;
    float curY = startY - m_hwScrollY;
    const float animT = static_cast<float>(GetTickCount64() % 100000ULL) * 0.001f;

    // ---- Control card: adapter, scan / SSDP buttons, progress ----
    {
        const float h = 98.0f;
        const bool autoPing = m_lan.IsPingAutoRefreshEnabled();
        const int pingSec = std::max(1, m_lan.GetPingAutoRefreshIntervalMs() / 1000);
        DrawCardShell(leftX, curY, availW, h, pl ? L"SKANER SIECI LAN  \x2022  ARP / ICMP + SSDP (UPnP)" : L"LAN SCANNER  \x2022  ARP / ICMP + SSDP (UPnP)", 0x0284C7, false, 480.0f);
        DrawCardTinyButton(D2D1::RectF(leftX + availW - 138.0f, curY + 4.0f, leftX + availW - 8.0f, curY + 25.0f),
                           pl ? L"\xD83D\xDCE1 Szukaj SSDP" : L"\xD83D\xDCE1 Find SSDP", CMD_LAN_SSDP, L"");
        DrawCardTinyButton(D2D1::RectF(leftX + availW - 308.0f, curY + 4.0f, leftX + availW - 144.0f, curY + 25.0f),
                           scanning ? (pl ? L"\x23F9 Zatrzymaj skanowanie" : L"\x23F9 Stop scan") : (pl ? L"\xD83D\xDD0D Skanuj sieć ponownie" : L"\xD83D\xDD0D Rescan network"),
                           CMD_LAN_SCAN, L"");
        DrawCardTinyButton(D2D1::RectF(leftX + availW - 386.0f, curY + 4.0f, leftX + availW - 314.0f, curY + 25.0f),
                           L"\x23F1 " + std::to_wstring(pingSec) + L"s",
                           CMD_LAN_PING_INTERVAL, L"");
        DrawCardTinyButton(D2D1::RectF(leftX + availW - 536.0f, curY + 4.0f, leftX + availW - 392.0f, curY + 25.0f),
                           autoPing ? (pl ? L"\xD83D\xDD04 Auto-Ping: WŁ" : L"\xD83D\xDD04 Auto-Ping: ON")
                                    : (pl ? L"\x23F8 Auto-Ping: WYŁ" : L"\x23F8 Auto-Ping: OFF"),
                           CMD_LAN_PING_AUTOREFRESH, L"");

        std::wstring adapterTxt;
        if (m_lanAdapters.empty()) {
            adapterTxt = pl ? L"Brak aktywnych adapterów IPv4 (podłącz się do sieci i kliknij „Skanuj”)" : L"No active IPv4 adapter (connect to a network and click \"Rescan\")";
        } else {
            const LanAdapterInfo& a = m_lanAdapters[static_cast<size_t>(m_lanAdapterIdx) % m_lanAdapters.size()];
            adapterTxt = (pl ? L"Adapter: " : L"Adapter: ") + a.name + L"   \x2022   " + a.ip + L"/" + std::to_wstring(a.prefix) +
                         (a.gateway.empty() ? L"" : ((pl ? L"   \x2022   brama " : L"   \x2022   gateway ") + a.gateway)) +
                         L"   (" + std::to_wstring((m_lanAdapterIdx % static_cast<int>(m_lanAdapters.size())) + 1) + L"/" + std::to_wstring(m_lanAdapters.size()) + L")";
        }
        DrawClippedText(adapterTxt, D2D1::RectF(leftX + 14.0f, curY + 36.0f, leftX + availW - 160.0f, curY + 56.0f), m_fmtBold, 0x0F172A);
        if (m_lanAdapters.size() > 1) {
            DrawCardTinyButton(D2D1::RectF(leftX + availW - 146.0f, curY + 35.0f, leftX + availW - 8.0f, curY + 56.0f),
                               pl ? L"\x25B6 Następny adapter" : L"\x25B6 Next adapter", CMD_LAN_ADAPTER, L"");
        }

        wchar_t st[300] = {};
        if (scanning) {
            swprintf_s(st, pl ? L"Skanowanie… %d / %d adresów  \x2022  znaleziono hostów: %zu  \x2022  SSDP: %zu%s"
                              : L"Scanning… %d / %d addresses  \x2022  hosts found: %zu  \x2022  SSDP: %zu%s",
                       done, total, hosts.size(), ssdp.size(), ssdpRun ? (pl ? L" (szukanie…)" : L" (searching…)") : L"");
        } else {
            swprintf_s(st, pl ? L"Gotowe  \x2022  hostów w sieci: %zu  \x2022  urządzeń SSDP: %zu%s"
                              : L"Done  \x2022  hosts on the network: %zu  \x2022  SSDP devices: %zu%s",
                       hosts.size(), ssdp.size(), ssdpRun ? (pl ? L" (szukanie SSDP…)" : L" (searching SSDP…)") : L"");
        }
        DrawClippedText(st, D2D1::RectF(leftX + 14.0f, curY + 58.0f, leftX + availW - 10.0f, curY + 76.0f), m_fmtSmall, 0x475569);

        // Progress bar (determinate while scanning, shimmering while only SSDP runs)
        D2D1_RECT_F bar = D2D1::RectF(leftX + 14.0f, curY + 82.0f, leftX + availW - 14.0f, curY + 88.0f);
        m_solidBrush->SetColor(HexColor(0xE2E8F0));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(bar, 3.0f, 3.0f), m_solidBrush);
        if (scanning && total > 0) {
            float f = std::clamp(static_cast<float>(done) / static_cast<float>(total), 0.02f, 1.0f);
            m_solidBrush->SetColor(HexColor(0x0078D7));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bar.left, bar.top, bar.left + (bar.right - bar.left) * f, bar.bottom), 3.0f, 3.0f), m_solidBrush);
        } else if (ssdpRun || portRun) {
            float pos = std::fmod(animT * 0.8f, 1.0f);
            float bw = (bar.right - bar.left) * 0.22f;
            float bx = bar.left + ((bar.right - bar.left) - bw) * pos;
            m_solidBrush->SetColor(HexColor(0x0284C7));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bx, bar.top, bx + bw, bar.bottom), 3.0f, 3.0f), m_solidBrush);
        }
        curY += h + gap;
    }

    // ---- Hosts table ----
    {
        const float rowH = 24.0f;
        const float hdrY = 32.0f;
        const float h = hdrY + 24.0f + std::max<size_t>(1, hosts.size()) * rowH + 8.0f;
        DrawCardShell(leftX, curY, availW, h, (pl ? L"URZĄDZENIA W SIECI (" : L"DEVICES ON THE NETWORK (") + std::to_wstring(hosts.size()) + L")", 0x059669, false);

        const float pad = 14.0f;
        float flagsW = 92.0f, portsW = 190.0f, rttW = 56.0f, vendorW = 150.0f, macW = 134.0f, ipW = 112.0f, g = 8.0f;
        float flagsX = leftX + availW - pad - flagsW;
        float portsX = flagsX - g - portsW;
        float rttX = portsX - g - rttW;
        float vendorX = rttX - g - vendorW;
        float macX = vendorX - g - macW;
        float ipX = leftX + pad;
        float nameX = ipX + ipW + g;
        float nameW = macX - g - nameX;
        if (nameW < 90.0f) { portsW = 100.0f; portsX = flagsX - g - portsW; rttX = portsX - g - rttW; vendorX = rttX - g - 110.0f; vendorW = 110.0f; macX = vendorX - g - macW; nameW = std::max(60.0f, macX - g - nameX); }

        D2D1_RECT_F hdr = D2D1::RectF(leftX + 6.0f, curY + hdrY, leftX + availW - 6.0f, curY + hdrY + 22.0f);
        m_solidBrush->SetColor(HexColor(0xEEF2F6));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(hdr, 3.0f, 3.0f), m_solidBrush);
        auto hcell = [&](float x, float w, const wchar_t* t) { DrawClippedText(t, D2D1::RectF(x, hdr.top + 3.0f, x + w, hdr.bottom), m_fmtBold, 0x334155); };
        hcell(ipX, ipW, L"IP");
        hcell(nameX, nameW, pl ? L"Nazwa" : L"Name");
        hcell(macX, macW, L"MAC");
        hcell(vendorX, vendorW, pl ? L"Producent" : L"Vendor");
        hcell(rttX, rttW, L"RTT");
        hcell(portsX, portsW, pl ? L"Otwarte porty" : L"Open ports");
        hcell(flagsX, flagsW, pl ? L"Wykryto" : L"Seen by");

        float ry = curY + hdrY + 24.0f;
        if (hosts.empty()) {
            DrawClippedText(scanning ? (pl ? L"Skanowanie sieci… hosty pojawią się tutaj w miarę odpowiedzi." : L"Scanning… hosts will appear here as they answer.")
                                     : (pl ? L"Nie znaleziono żadnych urządzeń. Sprawdź adapter i kliknij „Skanuj sieć ponownie”." : L"No devices found. Check the adapter and click \"Rescan network\"."),
                            D2D1::RectF(leftX + pad, ry + 3.0f, leftX + availW - pad, ry + rowH), m_fmtSmall, 0x64748B);
        }
        size_t rowIdx = 0;
        for (const auto& hst : hosts) {
            D2D1_RECT_F rr = D2D1::RectF(leftX + 6.0f, ry, leftX + availW - 6.0f, ry + rowH - 1.0f);
            bool sel = (hst.ip == m_lanSelectedIp);
            bool hov = PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, rr);
            m_solidBrush->SetColor(HexColor(sel ? 0xE0F2FE : (hov ? 0xE8F1FB : (rowIdx % 2 == 0 ? 0xFFFFFF : 0xFAFBFC))));
            m_rt->FillRectangle(rr, m_solidBrush);
            if (sel) {
                m_solidBrush->SetColor(HexColor(0x0078D7));
                m_rt->FillRectangle(D2D1::RectF(rr.left, rr.top, rr.left + 3.0f, rr.bottom), m_solidBrush);
            }

            const SsdpDevice* sd = findSsdp(hst.ip);
            std::wstring name = !hst.hostName.empty() ? hst.hostName : (!hst.netbiosName.empty() ? hst.netbiosName : (sd && !sd->friendlyName.empty() ? sd->friendlyName : L""));
            if (hst.isSelf) name = (name.empty() ? std::wstring(pl ? L"ten komputer" : L"this PC") : name + (pl ? L"  (ten komputer)" : L"  (this PC)"));
            if (hst.isGateway) name = (name.empty() ? std::wstring(L"") : name + L"  ") + (pl ? L"[brama / router]" : L"[gateway / router]");
            if (name.empty()) name = L"\x2014";

            std::wstring vendor = hst.vendor;
            if (vendor.empty() && sd && !sd->manufacturer.empty()) vendor = sd->manufacturer;
            if (vendor.empty() && hst.randomizedMac) vendor = pl ? L"prywatny MAC (telefon?)" : L"private MAC (phone?)";
            if (vendor.empty()) vendor = L"\x2014";

            std::wstring ports;
            if (hst.portsScanned) {
                for (uint16_t p : hst.openPorts) {
                    if (!ports.empty()) ports += L", ";
                    std::wstring pn = LanScanner::PortName(p);
                    ports += std::to_wstring(p) + (pn.empty() ? L"" : (L" " + pn));
                }
                if (ports.empty()) ports = pl ? L"brak" : L"none";
            } else {
                ports = L"\x2014";
            }

            std::wstring flags;
            if (hst.viaArp) flags += L"ARP ";
            if (hst.viaPing) flags += L"ICMP ";
            if (hst.viaSsdp) flags += L"SSDP";

            uint32_t nameCol = hst.isSelf ? 0x0067B8 : (hst.isGateway ? 0xB45309 : 0x0F172A);
            float t0 = ry + 4.0f, t1 = ry + rowH;
            DrawClippedText(hst.ip, D2D1::RectF(ipX, t0, ipX + ipW, t1), m_fmtSmall, 0x0F172A);
            DrawClippedText(name, D2D1::RectF(nameX, t0, nameX + nameW, t1), m_fmtSmall, nameCol);
            DrawClippedText(hst.mac.empty() ? std::wstring(L"\x2014") : hst.mac, D2D1::RectF(macX, t0, macX + macW, t1), m_fmtSmall, 0x475569);
            DrawClippedText(vendor, D2D1::RectF(vendorX, t0, vendorX + vendorW, t1), m_fmtSmall, 0x475569);
            wchar_t rtt[64] = {};
            if (hst.rttMs >= 0) {
                if (hst.pingSent > 1 && hst.avgRttMs >= 0) {
                    swprintf_s(rtt, pl ? L"%d ms (śr %d)" : L"%d ms (avg %d)", hst.rttMs, hst.avgRttMs);
                } else {
                    swprintf_s(rtt, L"%d ms", hst.rttMs);
                }
            } else if (hst.pingSent > 0 && hst.pingLost > 0) {
                int lossPct = (hst.pingLost * 100) / std::max(1, hst.pingSent);
                swprintf_s(rtt, pl ? L"timeout (%d%%)" : L"timeout (%d%%)", lossPct);
            } else {
                wcscpy_s(rtt, L"\x2014");
            }
            DrawClippedText(rtt, D2D1::RectF(rttX, t0, rttX + rttW, t1), m_fmtSmall, hst.rttMs < 0 && hst.pingLost > 0 ? 0xDC2626 : (hst.rttMs > 50 ? 0xD97706 : 0x059669));
            DrawClippedText(ports, D2D1::RectF(portsX, t0, portsX + portsW, t1), m_fmtSmall, 0x0067B8);
            DrawClippedText(flags, D2D1::RectF(flagsX, t0, flagsX + flagsW, t1), m_fmtSmall, 0x64748B);

            HardwareCardHit rowHit;
            rowHit.rect = rr;
            rowHit.cmdId = CMD_LAN_SELECT;
            rowHit.actionArg = hst.ip;
            rowHit.label = hst.ip;
            m_hwCardHits.push_back(rowHit);
            ry += rowH;
            ++rowIdx;
        }
        curY += h + gap;
    }

    // ---- Selected host: details + actions ----
    const LanHost* selHost = nullptr;
    for (const auto& hst : hosts) if (hst.ip == m_lanSelectedIp) { selHost = &hst; break; }
    if (selHost) {
        const float h = 34.0f + 4.0f * 20.0f + 40.0f;
        const SsdpDevice* sd = findSsdp(selHost->ip);
        std::wstring title = (pl ? L"WYBRANE URZĄDZENIE  \x2022  " : L"SELECTED DEVICE  \x2022  ") + selHost->ip;
        DrawCardShell(leftX, curY, availW, h, title, 0xD97706, false);

        float ry = curY + 34.0f;
        auto line = [&](const std::wstring& text, uint32_t col, bool bold) {
            DrawClippedText(text, D2D1::RectF(leftX + 14.0f, ry, leftX + availW - 14.0f, ry + 20.0f), bold ? m_fmtBold : m_fmtSmall, col);
            ry += 20.0f;
        };
        std::wstring nm = !selHost->hostName.empty() ? selHost->hostName : (!selHost->netbiosName.empty() ? selHost->netbiosName : (sd ? sd->friendlyName : L""));
        std::wstring l1 = nm.empty() ? std::wstring(pl ? L"(brak nazwy)" : L"(no name)") : nm;
        if (!selHost->netbiosName.empty() && selHost->netbiosName != nm) l1 += L"   NetBIOS: " + selHost->netbiosName;
        line(l1, 0x0F172A, true);
        std::wstring vendor = selHost->vendor.empty() ? (sd ? sd->manufacturer : L"") : selHost->vendor;
        std::wstring l2 = L"MAC " + (selHost->mac.empty() ? std::wstring(L"\x2014") : selHost->mac) + L"   \x2022   " + (pl ? L"producent: " : L"vendor: ") +
                          (vendor.empty() ? std::wstring(selHost->randomizedMac ? (pl ? L"prywatny/losowy MAC (telefon, tablet?)" : L"private/random MAC (phone, tablet?)") : L"\x2014") : vendor);
        if (sd && !sd->modelName.empty()) l2 += L"   \x2022   " + (pl ? std::wstring(L"model: ") : std::wstring(L"model: ")) + sd->modelName;
        line(l2, 0x475569, false);
        wchar_t l3[320] = {};
        if (selHost->pingSent > 0) {
            int lossPct = (selHost->pingLost * 100) / std::max(1, selHost->pingSent);
            swprintf_s(l3,
                pl ? L"RTT: %s   \x2022   min: %d ms   \x2022   śr: %d ms   \x2022   max: %d ms   \x2022   pingi: %d (straty: %d = %d%%)   \x2022   %s%s%s"
                   : L"RTT: %s   \x2022   min: %d ms   \x2022   avg: %d ms   \x2022   max: %d ms   \x2022   pings: %d (lost: %d = %d%%)   \x2022   %s%s%s",
                selHost->rttMs >= 0 ? (std::to_wstring(selHost->rttMs) + L" ms").c_str() : L"timeout",
                std::max(0, selHost->minRttMs), std::max(0, selHost->avgRttMs), std::max(0, selHost->maxRttMs),
                selHost->pingSent, selHost->pingLost, lossPct,
                selHost->viaArp ? L"ARP " : L"", selHost->viaPing ? L"ICMP " : L"", selHost->viaSsdp ? L"SSDP" : L"");
        } else {
            swprintf_s(l3, pl ? L"RTT %s   \x2022   wykryty przez: %s%s%s" : L"RTT %s   \x2022   seen by: %s%s%s",
                       selHost->rttMs >= 0 ? (std::to_wstring(selHost->rttMs) + L" ms").c_str() : L"\x2014",
                       selHost->viaArp ? L"ARP " : L"", selHost->viaPing ? L"ICMP " : L"", selHost->viaSsdp ? L"SSDP" : L"");
        }
        line(l3, 0x475569, false);
        std::wstring l4;
        if (portRun && !selHost->portsScanned) {
            l4 = pl ? L"Skanowanie portów…" : L"Scanning ports…";
        } else if (selHost->portsScanned) {
            for (uint16_t p : selHost->openPorts) {
                if (!l4.empty()) l4 += L"   \x2022   ";
                std::wstring pn = LanScanner::PortName(p);
                l4 += std::to_wstring(p) + (pn.empty() ? L"" : (L"/" + pn));
            }
            l4 = (pl ? L"Otwarte porty: " : L"Open ports: ") + (l4.empty() ? std::wstring(pl ? L"żadne z popularnych" : L"none of the common ones") : l4);
        } else {
            l4 = pl ? L"Porty nie były skanowane — kliknij „Skanuj porty”." : L"Ports not scanned yet — click \"Scan ports\".";
        }
        line(l4, 0x0067B8, false);

        // Action buttons
        float bx = leftX + 14.0f, by = ry + 6.0f;
        auto btn = [&](float w, const std::wstring& label, int cmd, const std::wstring& arg) {
            DrawCardTinyButton(D2D1::RectF(bx, by, bx + w, by + 22.0f), label, cmd, arg);
            bx += w + 8.0f;
        };
        btn(108.0f, pl ? L"\xD83D\xDCE1 Ping teraz" : L"\xD83D\xDCE1 Ping now", CMD_LAN_PING_ONCE, selHost->ip);
        btn(130.0f, portRun ? (pl ? L"Skanowanie…" : L"Scanning…") : (pl ? L"\xD83D\xDD0E Skanuj porty" : L"\xD83D\xDD0E Scan ports"), CMD_LAN_PORTSCAN, selHost->ip);
        bool has80 = false, has443 = false;
        for (uint16_t p : selHost->openPorts) { if (p == 80) has80 = true; if (p == 443) has443 = true; }
        std::wstring url = (has443 && !has80 ? L"https://" : L"http://") + selHost->ip;
        btn(170.0f, pl ? L"\xD83C\xDF10 Otwórz w przeglądarce" : L"\xD83C\xDF10 Open in browser", CMD_LAN_OPEN_WEB, url);
        btn(150.0f, pl ? L"\xD83D\xDCC1 Udziały \\\\" + selHost->ip : L"\xD83D\xDCC1 Shares \\\\" + selHost->ip, CMD_LAN_OPEN_SMB, selHost->ip);
        btn(100.0f, pl ? L"\xD83D\xDCCB Kopiuj IP" : L"\xD83D\xDCCB Copy IP", CMD_LAN_COPY, selHost->ip);
        if (!selHost->mac.empty()) btn(110.0f, pl ? L"\xD83D\xDCCB Kopiuj MAC" : L"\xD83D\xDCCB Copy MAC", CMD_LAN_COPY, selHost->mac);
        curY += h + gap;
    }

    // ---- SSDP / UPnP devices ----
    {
        const float rowH = 44.0f;
        const float h = 34.0f + std::max<size_t>(1, ssdp.size()) * rowH + 8.0f;
        DrawCardShell(leftX, curY, availW, h, (pl ? L"URZĄDZENIA SSDP / UPnP (" : L"SSDP / UPnP DEVICES (") + std::to_wstring(ssdp.size()) + L")", 0x7C3AED, false);
        float ry = curY + 32.0f;
        if (ssdp.empty()) {
            DrawClippedText(ssdpRun ? (pl ? L"Nasłuchiwanie odpowiedzi SSDP…" : L"Listening for SSDP answers…")
                                    : (pl ? L"Brak odpowiedzi SSDP. Kliknij „Szukaj SSDP” (telewizory, NAS-y, drukarki, routery, Chromecast…)." : L"No SSDP answers. Click \"Find SSDP\" (TVs, NAS boxes, printers, routers, Chromecast…)."),
                            D2D1::RectF(leftX + 14.0f, ry + 8.0f, leftX + availW - 14.0f, ry + 28.0f), m_fmtSmall, 0x64748B);
        }
        for (const auto& d : ssdp) {
            D2D1_RECT_F rr = D2D1::RectF(leftX + 6.0f, ry, leftX + availW - 6.0f, ry + rowH - 2.0f);
            bool sel = (d.ip == m_lanSelectedIp);
            bool hov = PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, rr);
            m_solidBrush->SetColor(HexColor(sel ? 0xE0F2FE : (hov ? 0xF1F5F9 : 0xFFFFFF)));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(rr, 4.0f, 4.0f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(sel ? 0x0078D7 : 0xE2E8F0));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(rr, 4.0f, 4.0f), m_solidBrush, 1.0f);

            std::wstring title2 = !d.friendlyName.empty() ? d.friendlyName : (!d.modelName.empty() ? d.modelName : d.server);
            if (title2.empty()) title2 = L"UPnP device";
            std::wstring sub = d.ip;
            if (!d.manufacturer.empty()) sub += L"   \x2022   " + d.manufacturer;
            if (!d.modelName.empty()) sub += L"   \x2022   " + d.modelName;
            if (!d.deviceType.empty()) {
                size_t p = d.deviceType.rfind(L"device:");
                sub += L"   \x2022   " + (p != std::wstring::npos ? d.deviceType.substr(p + 7) : d.deviceType);
            } else if (!d.st.empty()) {
                sub += L"   \x2022   " + d.st;
            }
            if (!d.server.empty()) sub += L"   \x2022   " + d.server;
            const float btnW = 130.0f;
            DrawClippedText(title2, D2D1::RectF(rr.left + 12.0f, rr.top + 3.0f, rr.right - btnW - 24.0f, rr.top + 22.0f), m_fmtBold, 0x0F172A);
            DrawClippedText(sub, D2D1::RectF(rr.left + 12.0f, rr.top + 22.0f, rr.right - btnW - 24.0f, rr.bottom), m_fmtSmall, 0x64748B);
            if (!d.location.empty()) {
                DrawCardTinyButton(D2D1::RectF(rr.right - btnW - 8.0f, rr.top + 10.0f, rr.right - 8.0f, rr.top + 32.0f),
                                   pl ? L"\xD83D\xDCC4 Opis urządzenia" : L"\xD83D\xDCC4 Description", CMD_LAN_OPEN_WEB, d.location);
            }
            HardwareCardHit rowHit;
            rowHit.rect = rr;
            rowHit.cmdId = CMD_LAN_SELECT;
            rowHit.actionArg = d.ip;
            rowHit.label = d.ip;
            m_hwCardHits.push_back(rowHit);
            ry += rowH;
        }
        curY += h + gap;
    }

    const float totalHeight = (curY + m_hwScrollY) - startY;
    const float viewH = std::max(1.0f, rect.bottom - rect.top);
    m_hwMaxScrollY = std::max(0.0f, totalHeight - viewH + 16.0f);
    m_hwScrollY = std::clamp(m_hwScrollY, 0.0f, m_hwMaxScrollY);

    m_rt->PopAxisAlignedClip();
}

void ExplorerApp::DrawLogView(const D2D1_RECT_F& rect) {
    const bool pl = (m_lang == Lang::PL);
    const std::vector<LogEntry> all = AppLog::Snapshot();

    int cInfo = 0, cOk = 0, cWarn = 0, cErr = 0;
    for (const LogEntry& e : all) {
        switch (e.level) {
        case LogLevel::Ok: ++cOk; break;
        case LogLevel::Warn: ++cWarn; break;
        case LogLevel::Error: ++cErr; break;
        default: ++cInfo; break;
        }
    }
    // Newest first, honouring the level filter
    std::vector<const LogEntry*> rows;
    rows.reserve(all.size());
    for (size_t i = all.size(); i-- > 0;) {
        const LogEntry& e = all[i];
        if (m_logFilter == 1 && e.level < LogLevel::Warn) continue;
        if (m_logFilter == 2 && e.level < LogLevel::Error) continue;
        rows.push_back(&e);
    }

    m_rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_solidBrush->SetColor(HexColor(0xF3F5F8));
    m_rt->FillRectangle(rect, m_solidBrush);

    const float padX = 10.0f, gap = 10.0f;
    const float availW = std::max(420.0f, (rect.right - rect.left) - padX * 2.0f);
    const float leftX = rect.left + padX;
    const float startY = rect.top + 8.0f;
    float curY = startY - m_hwScrollY;

    // ---- Header card ----
    {
        const float h = 92.0f;
        DrawCardShell(leftX, curY, availW, h, pl ? L"LOG OPERACJI  \x2022  TA SESJA PROGRAMU" : L"OPERATION LOG  \x2022  THIS RUN", 0x475569, false, 300.0f);
        DrawCardTinyButton(D2D1::RectF(leftX + availW - 118.0f, curY + 4.0f, leftX + availW - 8.0f, curY + 25.0f),
                           pl ? L"\xD83D\xDDD1 Wyczyść widok" : L"\xD83D\xDDD1 Clear view", CMD_LOG_CLEAR, L"");
        DrawCardTinyButton(D2D1::RectF(leftX + availW - 218.0f, curY + 4.0f, leftX + availW - 124.0f, curY + 25.0f),
                           pl ? L"\xD83D\xDCCB Kopiuj" : L"\xD83D\xDCCB Copy", CMD_LOG_COPY, L"");
        const wchar_t* fl = m_logFilter == 0 ? (pl ? L"\x25BC Filtr: wszystko" : L"\x25BC Filter: all")
                          : (m_logFilter == 1 ? (pl ? L"\x25BC Filtr: ostrzeżenia+" : L"\x25BC Filter: warnings+") : (pl ? L"\x25BC Filtr: błędy" : L"\x25BC Filter: errors"));
        DrawCardTinyButton(D2D1::RectF(leftX + availW - 392.0f, curY + 4.0f, leftX + availW - 224.0f, curY + 25.0f), fl, CMD_LOG_LEVEL, L"");

        DrawClippedText((pl ? L"Plik: " : L"File: ") + AppLog::FilePath(),
                        D2D1::RectF(leftX + 14.0f, curY + 34.0f, leftX + availW - 250.0f, curY + 54.0f), m_fmtSmall, 0x0F172A);
        DrawCardTinyButton(D2D1::RectF(leftX + availW - 238.0f, curY + 33.0f, leftX + availW - 128.0f, curY + 54.0f),
                           pl ? L"\xD83D\xDCC4 Otwórz plik" : L"\xD83D\xDCC4 Open file", CMD_LOG_OPEN_FILE, L"");
        DrawCardTinyButton(D2D1::RectF(leftX + availW - 120.0f, curY + 33.0f, leftX + availW - 8.0f, curY + 54.0f),
                           pl ? L"\xD83D\xDCC1 Folder logów" : L"\xD83D\xDCC1 Logs folder", CMD_LOG_OPEN_FOLDER, L"");

        wchar_t cnt[260] = {};
        swprintf_s(cnt, pl ? L"Wpisów: %zu   \x2022   INFO %d   \x2022   OK %d   \x2022   OSTRZEŻENIA %d   \x2022   BŁĘDY %d"
                           : L"Entries: %zu   \x2022   INFO %d   \x2022   OK %d   \x2022   WARNINGS %d   \x2022   ERRORS %d",
                   all.size(), cInfo, cOk, cWarn, cErr);
        DrawClippedText(cnt, D2D1::RectF(leftX + 14.0f, curY + 58.0f, leftX + availW - 10.0f, curY + 76.0f), m_fmtBold, cErr > 0 ? 0xB91C1C : (cWarn > 0 ? 0xB45309 : 0x0F172A));
        DrawClippedText(pl ? L"Każde uruchomienie programu tworzy nowy plik logu. Kliknij wiersz, aby skopiować go do schowka."
                           : L"Every start of the program creates a new log file. Click a row to copy it to the clipboard.",
                        D2D1::RectF(leftX + 14.0f, curY + 74.0f, leftX + availW - 10.0f, curY + 90.0f), m_fmtSmall, 0x64748B);
        curY += h + gap;
    }

    // ---- Entries (virtualised: only the visible rows are drawn) ----
    {
        const float rowH = 22.0f;
        const float h = 34.0f + std::max<size_t>(1, rows.size()) * rowH + 8.0f;
        DrawCardShell(leftX, curY, availW, h, (pl ? L"WPISY (najnowsze na górze)  \x2022  " : L"ENTRIES (newest first)  \x2022  ") + std::to_wstring(rows.size()), 0x64748B, false);

        const float timeW = 92.0f, lvlW = 62.0f, catW = 96.0f;
        const float timeX = leftX + 14.0f, lvlX = timeX + timeW, catX = lvlX + lvlW, msgX = catX + catW;
        const float top0 = curY + 32.0f;

        if (rows.empty()) {
            DrawClippedText(all.empty() ? (pl ? L"Brak wpisów w tej sesji." : L"No entries in this run yet.") : (pl ? L"Brak wpisów dla wybranego filtru." : L"No entries match the filter."),
                            D2D1::RectF(leftX + 14.0f, top0 + 4.0f, leftX + availW - 14.0f, top0 + 24.0f), m_fmtSmall, 0x64748B);
        }
        for (size_t i = 0; i < rows.size(); ++i) {
            const float ry = top0 + static_cast<float>(i) * rowH;
            if (ry + rowH < rect.top) continue;
            if (ry > rect.bottom) break;
            const LogEntry& e = *rows[i];
            D2D1_RECT_F rr = D2D1::RectF(leftX + 6.0f, ry, leftX + availW - 6.0f, ry + rowH - 1.0f);
            const bool hov = PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, rr);
            uint32_t bg = (e.level == LogLevel::Error) ? 0xFEF2F2 : (e.level == LogLevel::Warn ? 0xFFFBEB : ((i & 1) ? 0xF8FAFC : 0xFFFFFF));
            if (hov) bg = 0xE0F2FE;
            m_solidBrush->SetColor(HexColor(bg));
            m_rt->FillRectangle(rr, m_solidBrush);

            uint32_t lvlCol = 0x64748B;
            const wchar_t* lvlTxt = L"INFO";
            switch (e.level) {
            case LogLevel::Ok:    lvlCol = 0x059669; lvlTxt = L"\x2713 OK"; break;
            case LogLevel::Warn:  lvlCol = 0xD97706; lvlTxt = L"\x26A0 WARN"; break;
            case LogLevel::Error: lvlCol = 0xDC2626; lvlTxt = L"\x2717 ERROR"; break;
            default: break;
            }
            wchar_t ts[32] = {};
            swprintf_s(ts, L"%02d:%02d:%02d.%03d", e.time.wHour, e.time.wMinute, e.time.wSecond, e.time.wMilliseconds);
            const float t0 = ry + 3.0f, t1 = ry + rowH;
            DrawClippedText(ts, D2D1::RectF(timeX, t0, timeX + timeW - 4.0f, t1), m_fmtSmall, 0x64748B);
            DrawClippedText(lvlTxt, D2D1::RectF(lvlX, t0, lvlX + lvlW - 4.0f, t1), m_fmtBold, lvlCol);
            DrawClippedText(e.category, D2D1::RectF(catX, t0, catX + catW - 6.0f, t1), m_fmtSmall, 0x0067B8);
            DrawClippedText(e.message, D2D1::RectF(msgX, t0, rr.right - 8.0f, t1), m_fmtSmall, e.level == LogLevel::Error ? 0x991B1B : 0x0F172A);

            HardwareCardHit rowHit;
            rowHit.rect = rr;
            rowHit.copyText = AppLog::FormatLine(e);
            rowHit.label = pl ? L"wpis logu" : L"log line";
            m_hwCardHits.push_back(rowHit);
        }
        curY += h + gap;
    }

    const float totalHeight = (curY + m_hwScrollY) - startY;
    const float viewH = std::max(1.0f, rect.bottom - rect.top);
    m_hwMaxScrollY = std::max(0.0f, totalHeight - viewH + 16.0f);
    m_hwScrollY = std::clamp(m_hwScrollY, 0.0f, m_hwMaxScrollY);

    m_rt->PopAxisAlignedClip();
}

void ExplorerApp::DrawBackupsView(const D2D1_RECT_F& rect) {
    const bool pl = (m_lang == Lang::PL);
    const std::vector<BackupJob> jobs = m_backups.SnapshotJobs();
    const bool busy = m_backups.IsBusy();
    std::wstring rcloneVer;
    const bool rcloneFound = BackupManager::IsRcloneInstalled(&rcloneVer);

    if (!jobs.empty()) {
        bool foundSel = false;
        for (const auto& j : jobs) {
            if (j.id == m_backupSelectedJobId) { foundSel = true; break; }
        }
        if (!foundSel) m_backupSelectedJobId = jobs.front().id;
    }

    m_rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_solidBrush->SetColor(HexColor(0xF3F5F8));
    m_rt->FillRectangle(rect, m_solidBrush);

    const float padX = 10.0f, gap = 10.0f;
    const float availW = std::max(460.0f, (rect.right - rect.left) - padX * 2.0f);
    const float leftX = rect.left + padX;
    const float startY = rect.top + 8.0f;
    float curY = startY - m_hwScrollY;

    // ---- 1. Header Card ----
    {
        const float h = 92.0f;
        DrawCardShell(leftX, curY, availW, h,
            pl ? L"KOPIE ZAPASOWE PRO  \x2022  HARMONOGRAM + RCLONE + AUTO-START GDY CEL DOSTĘPNY"
               : L"PRO BACKUP MANAGER  \x2022  SCHEDULER + RCLONE + WHEN TARGET AVAILABLE",
            0x0284C7, false, 440.0f);

        DrawCardTinyButton(D2D1::RectF(leftX + availW - 128.0f, curY + 4.0f, leftX + availW - 8.0f, curY + 25.0f),
                           pl ? L"\xD83D\xDD0D Symuluj (Dry-Run)" : L"\xD83D\xDD0D Test (Dry-Run)",
                           CMD_BACKUP_DRY_RUN, std::to_wstring(m_backupSelectedJobId));
        DrawCardTinyButton(D2D1::RectF(leftX + availW - 270.0f, curY + 4.0f, leftX + availW - 134.0f, curY + 25.0f),
                           busy ? (pl ? L"\x23F3 Trwa synchronizacja…" : L"\x23F3 Syncing…")
                                : (pl ? L"\x25B6 Uruchom wybrane" : L"\x25B6 Run selected"),
                           CMD_BACKUP_RUN_NOW, std::to_wstring(m_backupSelectedJobId));
        DrawCardTinyButton(D2D1::RectF(leftX + availW - 412.0f, curY + 4.0f, leftX + availW - 276.0f, curY + 25.0f),
                           pl ? L"+ Nowe zadanie kopii" : L"+ New backup job",
                           CMD_BACKUP_ADD_JOB, L"");

        std::wstring engLine = rcloneFound
            ? ((pl ? L"\x2713 Silnik rclone wykryty: " : L"\x2713 rclone engine detected: ") + rcloneVer + (pl ? L"  +  wbudowany silnik przyrostowy C++" : L"  +  built-in C++ incremental engine"))
            : (pl ? L"\x2713 Aktywny wbudowany szybki silnik przyrostowy C++ (dodaj rclone.exe do PATH dla zdalnych chmur remote:path)"
                  : L"\x2713 Built-in fast C++ incremental sync engine active (add rclone.exe to PATH for cloud remotes)");
        DrawClippedText(engLine, D2D1::RectF(leftX + 14.0f, curY + 36.0f, leftX + availW - 14.0f, curY + 56.0f), m_fmtBold, 0x0F172A);

        int activeCount = 0, onlineCount = 0;
        for (const auto& j : jobs) {
            if (j.enabled) ++activeCount;
            if (BackupManager::IsTargetAvailable(j.targetPath)) ++onlineCount;
        }
        std::wstring prog = m_backups.GetProgressStatus();
        wchar_t subLine[380] = {};
        swprintf_s(subLine,
            pl ? L"Zadań ogółem: %zu   \x2022   włączone (auto): %d   \x2022   cele dostępne teraz (online): %d   \x2022   %s"
               : L"Total jobs: %zu   \x2022   enabled (auto): %d   \x2022   targets online now: %d   \x2022   %s",
            jobs.size(), activeCount, onlineCount,
            prog.empty() ? (pl ? L"Gotowe do pracy w tle" : L"Ready for background sync") : prog.c_str());
        DrawClippedText(subLine, D2D1::RectF(leftX + 14.0f, curY + 58.0f, leftX + availW - 14.0f, curY + 78.0f), m_fmtSmall, 0x475569);
        curY += h + gap;
    }

    // ---- 2. Jobs Table Card ----
    {
        const float rowH = 28.0f;
        const float hdrY = 32.0f;
        const float h = hdrY + 24.0f + std::max<size_t>(1, jobs.size()) * rowH + 10.0f;
        DrawCardShell(leftX, curY, availW, h,
            (pl ? L"ZADANIA KOPII ZAPASOWYCH (" : L"BACKUP JOBS (") + std::to_wstring(jobs.size()) + L")",
            0x059669, false);

        const float pad = 14.0f;
        float enW = 68.0f, nameW = 190.0f, schedW = 110.0f, availColW = 116.0f, engW = 86.0f, statW = 210.0f, g = 8.0f;
        float enX = leftX + pad;
        float nameX = enX + enW + g;
        float statX = leftX + availW - pad - statW;
        float engX = statX - g - engW;
        float availColX = engX - g - availColW;
        float schedX = availColX - g - schedW;
        float pathX = nameX + nameW + g;
        float pathW = std::max(120.0f, schedX - g - pathX);

        D2D1_RECT_F hdr = D2D1::RectF(leftX + 6.0f, curY + hdrY, leftX + availW - 6.0f, curY + hdrY + 22.0f);
        m_solidBrush->SetColor(HexColor(0xEEF2F6));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(hdr, 3.0f, 3.0f), m_solidBrush);
        auto hcell = [&](float x, float w, const wchar_t* t) {
            DrawClippedText(t, D2D1::RectF(x, hdr.top + 3.0f, x + w, hdr.bottom), m_fmtBold, 0x334155);
        };
        hcell(enX, enW, pl ? L"Status" : L"Active");
        hcell(nameX, nameW, pl ? L"Nazwa zadania" : L"Job Name");
        hcell(pathX, pathW, pl ? L"Źródło  \x2794  Cel" : L"Source  \x2794  Target");
        hcell(schedX, schedW, pl ? L"Jak często" : L"Frequency");
        hcell(availColX, availColW, pl ? L"Gdy cel dostępny" : L"When available");
        hcell(engX, engW, pl ? L"Silnik" : L"Engine");
        hcell(statX, statW, pl ? L"Ostatni wynik" : L"Last Status");

        float ry = curY + hdrY + 24.0f;
        size_t rowIdx = 0;
        for (const auto& j : jobs) {
            D2D1_RECT_F rr = D2D1::RectF(leftX + 6.0f, ry, leftX + availW - 6.0f, ry + rowH - 1.0f);
            bool sel = (j.id == m_backupSelectedJobId);
            bool hov = PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, rr);
            m_solidBrush->SetColor(HexColor(sel ? 0xE0F2FE : (hov ? 0xE8F1FB : (rowIdx % 2 == 0 ? 0xFFFFFF : 0xFAFBFC))));
            m_rt->FillRectangle(rr, m_solidBrush);
            if (sel) {
                m_solidBrush->SetColor(HexColor(0x0078D7));
                m_rt->FillRectangle(D2D1::RectF(rr.left, rr.top, rr.left + 3.0f, rr.bottom), m_solidBrush);
            }

            std::wstring idStr = std::to_wstring(j.id);
            DrawCardTinyButton(D2D1::RectF(enX, ry + 3.0f, enX + enW - 4.0f, ry + rowH - 4.0f),
                j.enabled ? (pl ? L"\x2713 WŁ" : L"\x2713 ON") : (pl ? L"WYŁ" : L"OFF"),
                CMD_BACKUP_TOGGLE_ENABLE, idStr);

            float t0 = ry + 4.0f, t1 = ry + rowH - 2.0f;
            DrawClippedText(j.name, D2D1::RectF(nameX, t0, nameX + nameW, t1), m_fmtBold, 0x0F172A);
            DrawClippedText(j.sourcePath + L"  \x2794  " + j.targetPath, D2D1::RectF(pathX, t0, pathX + pathW, t1), m_fmtSmall, 0x334155);
            DrawClippedText(BackupManager::FormatScheduleLabel(j.scheduleMinutes, pl), D2D1::RectF(schedX, t0, schedX + schedW, t1), m_fmtSmall, 0x0369A1);
            DrawClippedText(j.whenTargetAvailable ? (pl ? L"\x2713 Auto po podpięciu" : L"\x2713 Auto on connect") : L"\x2014",
                D2D1::RectF(availColX, t0, availColX + availColW, t1), m_fmtSmall, j.whenTargetAvailable ? 0x059669 : 0x64748B);
            DrawClippedText(j.useRclone ? L"rclone" : L"Native C++",
                D2D1::RectF(engX, t0, engX + engW, t1), m_fmtSmall, j.useRclone ? 0x7C3AED : 0x0F172A);
            DrawClippedText(j.lastStatus.empty() ? std::wstring(L"\x2014") : j.lastStatus,
                D2D1::RectF(statX, t0, statX + statW, t1), m_fmtSmall, j.lastRunOk ? 0x059669 : 0xDC2626);

            HardwareCardHit rowHit;
            rowHit.rect = rr;
            rowHit.cmdId = CMD_BACKUP_SELECT_JOB;
            rowHit.actionArg = idStr;
            rowHit.actionIndex = j.id;
            rowHit.label = j.name;
            m_hwCardHits.push_back(rowHit);

            ry += rowH;
            ++rowIdx;
        }
        curY += h + gap;
    }

    // ---- 3. Selected Job Inspector & Pro Editor Card ----
    BackupJob selJob;
    if (m_backups.GetJobById(m_backupSelectedJobId, selJob)) {
        const float h = 216.0f;
        const bool targetOnline = BackupManager::IsTargetAvailable(selJob.targetPath);
        std::wstring idStr = std::to_wstring(selJob.id);

        DrawCardShell(leftX, curY, availW, h,
            (pl ? L"EDYCJA ZADANIA  \x2022  " : L"EDIT BACKUP JOB  \x2022  ") + selJob.name,
            0xD97706, false);

        float ry = curY + 36.0f;
        auto drawPropRow = [&](const std::wstring& label, const std::wstring& val, uint32_t valCol) {
            DrawClippedText(label, D2D1::RectF(leftX + 14.0f, ry, leftX + 190.0f, ry + 20.0f), m_fmtBold, 0x334155);
            DrawClippedText(val, D2D1::RectF(leftX + 195.0f, ry, leftX + availW - 14.0f, ry + 20.0f), m_fmtSmall, valCol);
            ry += 21.0f;
        };

        drawPropRow(pl ? L"Folder źródłowy (Source):" : L"Source Folder:", selJob.sourcePath, 0x0F172A);
        std::wstring targetDesc = selJob.targetPath + L"   " +
            (targetOnline ? (pl ? L"[\x25CF CEL DOSTĘPNY / ONLINE]" : L"[\x25CF TARGET ONLINE]")
                          : (pl ? L"[\x25CB CEL NIEDOSTĘPNY / OFFLINE — oczekuje na podłączenie]" : L"[\x25CB TARGET OFFLINE — waiting for drive]"));
        drawPropRow(pl ? L"Folder docelowy (Target):" : L"Target Folder / Remote:", targetDesc, targetOnline ? 0x059669 : 0xD97706);

        drawPropRow(pl ? L"Filtry Włącz (Include):" : L"Include Filter:", selJob.includeFilter.empty() ? L"*" : selJob.includeFilter, 0x0369A1);
        drawPropRow(pl ? L"Filtry Wyklucz (Exclude):" : L"Exclude Filter:", selJob.excludeFilter.empty() ? (pl ? L"(brak)" : L"(none)") : selJob.excludeFilter, 0xB91C1C);

        std::wstring schedInfo = BackupManager::FormatScheduleLabel(selJob.scheduleMinutes, pl) +
            L"   \x2022   " + (pl ? L"Gdy cel dostępny: " : L"When target available: ") +
            (selJob.whenTargetAvailable ? (pl ? L"TAK (auto-start po podpięciu dysku)" : L"YES (auto-run on drive connect)") : (pl ? L"NIE" : L"NO")) +
            L"   \x2022   " + (pl ? L"Ostatni bieg: " : L"Last run: ") + selJob.lastRunTimeStr;
        drawPropRow(pl ? L"Automatyzacja:" : L"Automation:", schedInfo, 0x334155);

        std::wstring rcloneCmd = BackupManager::BuildRcloneCommandLine(selJob, false);
        drawPropRow(pl ? L"Polecenie rclone / C++:" : L"rclone / Sync Command:", rcloneCmd, 0x64748B);

        float bx = leftX + 14.0f, by = curY + h - 32.0f;
        auto btn = [&](float w, const std::wstring& lbl, int cmd) {
            DrawCardTinyButton(D2D1::RectF(bx, by, bx + w, by + 23.0f), lbl, cmd, idStr);
            bx += w + 8.0f;
        };
        btn(136.0f, pl ? L"\x25B6 Uruchom teraz" : L"\x25B6 Run Now", CMD_BACKUP_RUN_NOW);
        btn(136.0f, pl ? L"\xD83D\xDD0D Test (Dry-Run)" : L"\xD83D\xDD0D Test (Dry-Run)", CMD_BACKUP_DRY_RUN);
        btn(148.0f, pl ? L"\x270E Edytuj ścieżki/nazwę" : L"\x270E Edit Paths/Name", CMD_BACKUP_EDIT_JOB);
        btn(148.0f, pl ? L"\xD83D\xDD0E Edytuj filtry Inc/Exc" : L"\xD83D\xDD0E Edit Inc/Exc Filters", CMD_BACKUP_EDIT_FILTERS);
        btn(138.0f, (pl ? L"\x23F1 " : L"\x23F1 ") + BackupManager::FormatScheduleLabel(selJob.scheduleMinutes, pl), CMD_BACKUP_CYCLE_SCHED);
        btn(158.0f, selJob.whenTargetAvailable ? (pl ? L"\x2713 Gdy cel dostępny: TAK" : L"\x2713 When available: YES")
                                               : (pl ? L"Gdy cel dostępny: NIE" : L"When available: NO"), CMD_BACKUP_TOGGLE_AVAIL);
        btn(118.0f, selJob.useRclone ? L"Silnik: rclone" : L"Silnik: Native C++", CMD_BACKUP_TOGGLE_RCLONE);
        curY += h + gap;
    }

    const float totalHeight = (curY + m_hwScrollY) - startY;
    const float viewH = std::max(1.0f, rect.bottom - rect.top);
    m_hwMaxScrollY = std::max(0.0f, totalHeight - viewH + 16.0f);
    m_hwScrollY = std::clamp(m_hwScrollY, 0.0f, m_hwMaxScrollY);

    m_rt->PopAxisAlignedClip();
}

void ExplorerApp::DrawHardwareProView(const D2D1_RECT_F& rect) {
    m_hwCardHits.clear();
    if (IsBackupsView()) {
        DrawBackupsView(rect);
        return;
    }
    if (IsLogView()) {
        DrawLogView(rect);
        return;
    }
    if (IsLanView()) {
        DrawLanView(rect);
        return;
    }
    if (IsMonitoringView()) {
        DrawMonitoringView(rect);
        return;
    }
    if (IsProgramsView()) {
        DrawProgramsView(rect);
        return;
    }
    if (!m_hwInspector.IsInitialized()) {
        m_hwInspector.InitializeStaticInventory();
    }
    m_hwInspector.ApplyStagedData();
    m_hwSmartInit = true;
    {
        bool selectedExists = false;
        for (const auto& d : m_hwInspector.GetPhysicalDisks()) {
            if (d.index == m_hwSelectedDisk) { selectedExists = true; break; }
        }
        if (!selectedExists) {
            m_hwSelectedDisk = m_hwInspector.GetPhysicalDisks().empty() ? -1 : m_hwInspector.GetPhysicalDisks().front().index;
        }
    }

    const auto& cpu      = m_hwInspector.GetCpu();
    const auto& gpus     = m_hwInspector.GetGpus();
    const auto& displays = m_hwInspector.GetDisplays();
    const auto& ram      = m_hwInspector.GetRam();
    const auto& mobo     = m_hwInspector.GetMotherboard();
    const auto& pDisks   = m_hwInspector.GetPhysicalDisks();
    const auto& volumes  = m_hwInspector.GetVolumes();
    const auto& nets     = m_hwInspector.GetNetworks();
    const auto& sys      = m_hwInspector.GetSystem();

    m_rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Clean White/Light Windows Explorer Workstation Theme (#F3F5F8 background)
    m_solidBrush->SetColor(HexColor(0xF3F5F8));
    m_rt->FillRectangle(rect, m_solidBrush);

    // 1. Top Workstation Control & Status Bar (36px, Crisp White)
    D2D1_RECT_F topBar = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + 36.0f);
    m_solidBrush->SetColor(HexColor(0xFFFFFF));
    m_rt->FillRectangle(topBar, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xDADADA));
    m_rt->DrawLine(D2D1::Point2F(topBar.left, topBar.bottom), D2D1::Point2F(topBar.right, topBar.bottom), m_solidBrush, 1.0f);

    // Live Pulse Indicator
    bool paused = !m_hwLiveRefresh;
    m_solidBrush->SetColor(paused ? HexColor(0xD97706) : HexColor(0x10B981));
    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(topBar.left + 16.0f, topBar.top + 18.0f), 5.0f, 5.0f), m_solidBrush);

    std::wstring wsTitle = (m_lang == Lang::PL ? L"STACJA DIAGNOSTYCZNA SPRZĘTU PRO  \x2022  " : L"PRO HARDWARE & TELEMETRY WORKSTATION  \x2022  ")
                           + sys.computerName + L" (" + mobo.boardManufacturer + L" " + mobo.boardProduct + L")";
    D2D1_RECT_F titleR = D2D1::RectF(topBar.left + 28.0f, topBar.top, topBar.right - 390.0f, topBar.bottom);
    m_solidBrush->SetColor(HexColor(0x0F172A));
    m_rt->DrawTextW(wsTitle.c_str(), static_cast<UINT32>(wsTitle.size()), m_fmtBold, titleR, m_solidBrush);

    // Top-right quick action buttons inside the Hardware view (Explorer Light Style)
    auto drawTopActionBtn = [&](int cmdId, float rightOff, float w, const wchar_t* label, bool isPrimary) {
        D2D1_RECT_F br = D2D1::RectF(topBar.right - rightOff - w, topBar.top + 6.0f, topBar.right - rightOff, topBar.bottom - 6.0f);
        m_ribbonHits.push_back({ cmdId, br });
        bool hov = (m_hoverRibbonBtn == cmdId);
        uint32_t bgHex = hov ? 0x0078D7 : (isPrimary ? 0x0067B8 : 0xF1F5F9);
        uint32_t bdHex = (hov || isPrimary) ? 0x005A9E : 0xCBD5E1;
        uint32_t fgHex = (hov || isPrimary) ? 0xFFFFFF : 0x1E293B;
        m_solidBrush->SetColor(HexColor(bgHex));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.5f, 3.5f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(bdHex));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(br, 3.5f, 3.5f), m_solidBrush, 1.0f);
        m_solidBrush->SetColor(HexColor(fgHex));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), m_fmtSmall, br, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    };

    drawTopActionBtn(CMD_HW_BACK_TO_FILES, 8.0f,   110.0f, m_lang == Lang::PL ? L"\xD83D\xDCC1 Wróć do plików" : L"\xD83D\xDCC1 Back to Files", false);
    drawTopActionBtn(CMD_HW_SAVE_REPORT,   124.0f, 96.0f,  m_lang == Lang::PL ? L"\xD83D\xDCBE Zapisz .TXT" : L"\xD83D\xDCBE Save .TXT", false);
    drawTopActionBtn(CMD_HW_COPY_REPORT,   226.0f, 106.0f, m_lang == Lang::PL ? L"\xD83D\xDCCB Kopiuj raport" : L"\xD83D\xDCCB Copy Report", false);
    drawTopActionBtn(CMD_HW_BENCH_CPU,     338.0f, 96.0f,  m_lang == Lang::PL ? L"\x26A1 Test CPU/RAM" : L"\x26A1 Bench CPU", true);

    // 2. Sub-Tab Filter Strip (28px, Light Slate Strip)
    D2D1_RECT_F subStrip = D2D1::RectF(rect.left, topBar.bottom, rect.right, topBar.bottom + 28.0f);
    m_solidBrush->SetColor(HexColor(0xEEF2F6));
    m_rt->FillRectangle(subStrip, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xDADADA));
    m_rt->DrawLine(D2D1::Point2F(subStrip.left, subStrip.bottom), D2D1::Point2F(subStrip.right, subStrip.bottom), m_solidBrush, 1.0f);

    struct SubDef { int cmd; HardwareSubTab tab; const wchar_t* pl; const wchar_t* en; float w; };
    SubDef subs[] = {
        { CMD_HW_SUB_ALL,     HardwareSubTab::All,         L"Wszystko",       L"All",            76.0f },
        { CMD_HW_SUB_CPU,     HardwareSubTab::Cpu,         L"CPU i Cache",    L"CPU & Cache",    96.0f },
        { CMD_HW_SUB_GPU,     HardwareSubTab::Gpu,         L"GPU i Monitory", L"GPU & Displays", 110.0f },
        { CMD_HW_SUB_RAM,     HardwareSubTab::Ram,         L"RAM (DIMM)",     L"RAM (DIMMs)",    92.0f },
        { CMD_HW_SUB_MOBO,    HardwareSubTab::Motherboard, L"Płyta i BIOS",   L"Board & BIOS",   100.0f },
        { CMD_HW_SUB_DISKS,   HardwareSubTab::Disks,       L"Dyski i SMART",  L"Disks & SMART",  108.0f },
        { CMD_HW_SUB_NET,     HardwareSubTab::Network,     L"Sieć",           L"Network",        70.0f },
        { CMD_HW_SUB_SYSTEM,  HardwareSubTab::System,      L"System i Audio", L"OS & Audio",     108.0f },
        { CMD_HW_SUB_THERMAL, HardwareSubTab::Thermal,     L"\xD83C\xDF21 Temperatury", L"\xD83C\xDF21 Temperatures", 112.0f }
    };
    float sx = subStrip.left + 8.0f;
    for (const auto& sd : subs) {
        D2D1_RECT_F sr = D2D1::RectF(sx, subStrip.top + 3.0f, sx + sd.w, subStrip.bottom - 3.0f);
        m_ribbonHits.push_back({ sd.cmd, sr });
        bool act = (m_hwSubTab == sd.tab);
        bool hov = (m_hoverRibbonBtn == sd.cmd);
        m_solidBrush->SetColor(act ? HexColor(0x0078D7) : (hov ? HexColor(0xE2E8F0) : HexColor(0xFFFFFF)));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(sr, 3.0f, 3.0f), m_solidBrush);
        m_solidBrush->SetColor(act ? HexColor(0x005A9E) : HexColor(0xCBD5E1));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(sr, 3.0f, 3.0f), m_solidBrush, 1.0f);
        m_solidBrush->SetColor(act ? HexColor(0xFFFFFF) : HexColor(0x334155));
        const wchar_t* lbl = (m_lang == Lang::PL) ? sd.pl : sd.en;
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(lbl, static_cast<UINT32>(wcslen(lbl)), m_fmtSmall, sr, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        sx += sd.w + 5.0f;
    }

    // 3. Scrollable Content Area
    D2D1_RECT_F workR = D2D1::RectF(rect.left, subStrip.bottom + 4.0f, rect.right, rect.bottom);
    m_rt->PushAxisAlignedClip(workR, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float startY = workR.top + 4.0f;
    float curY = startY - m_hwScrollY;
    float padX = 10.0f;
    float availW = std::max(300.0f, (workR.right - workR.left) - padX * 2.0f);

    // Helper to draw a real-time 60s oscilloscope graph box (Windows Task Manager Light Style)
    auto drawOscilloscope = [&](const D2D1_RECT_F& boxR, const wchar_t* title, const std::wstring& bigValue,
                                const std::wstring& subValue, const std::vector<float>& hist,
                                float maxScale, uint32_t accentHex) {
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(boxR, 5.0f, 5.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xD8E0EA));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(boxR, 5.0f, 5.0f), m_solidBrush, 1.0f);

        D2D1_RECT_F tR = D2D1::RectF(boxR.left + 10.0f, boxR.top + 5.0f, boxR.right - 90.0f, boxR.top + 22.0f);
        m_solidBrush->SetColor(HexColor(0x475569));
        m_rt->DrawTextW(title, static_cast<UINT32>(wcslen(title)), m_fmtBold, tR, m_solidBrush);

        D2D1_RECT_F vR = D2D1::RectF(boxR.right - 115.0f, boxR.top + 4.0f, boxR.right - 8.0f, boxR.top + 24.0f);
        m_solidBrush->SetColor(HexColor(accentHex));
        m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        m_rt->DrawTextW(bigValue.c_str(), static_cast<UINT32>(bigValue.size()), m_fmtBold, vR, m_solidBrush);
        m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        D2D1_RECT_F subR = D2D1::RectF(boxR.left + 10.0f, boxR.top + 21.0f, boxR.right - 8.0f, boxR.top + 38.0f);
        m_solidBrush->SetColor(HexColor(0x64748B));
        m_rt->DrawTextW(subValue.c_str(), static_cast<UINT32>(subValue.size()), m_fmtSmall, subR, m_solidBrush);

        D2D1_RECT_F plotR = D2D1::RectF(boxR.left + 8.0f, boxR.top + 40.0f, boxR.right - 8.0f, boxR.bottom - 7.0f);
        m_solidBrush->SetColor(HexColor(0xF8FAFC));
        m_rt->FillRectangle(plotR, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xCBD5E1));
        m_rt->DrawRectangle(plotR, m_solidBrush, 1.0f);

        float pw = plotR.right - plotR.left;
        float ph = plotR.bottom - plotR.top;
        m_solidBrush->SetColor(HexColor(0xE2E8F0));
        for (int g = 1; g <= 3; ++g) {
            float gy = plotR.top + ph * (g * 0.25f);
            m_rt->DrawLine(D2D1::Point2F(plotR.left, gy), D2D1::Point2F(plotR.right, gy), m_solidBrush, 0.8f);
        }

        int n = static_cast<int>(hist.size());
        if (n >= 2) {
            float denom = std::max(1.0f, maxScale);
            float stepX = pw / static_cast<float>(n - 1);
            for (int i = 0; i < n - 1; ++i) {
                float v0 = std::clamp(hist[i] / denom, 0.0f, 1.0f);
                float v1 = std::clamp(hist[i + 1] / denom, 0.0f, 1.0f);
                float x0 = plotR.left + i * stepX;
                float x1 = plotR.left + (i + 1) * stepX;
                float y0 = plotR.bottom - v0 * (ph - 2.0f) - 1.0f;
                float y1 = plotR.bottom - v1 * (ph - 2.0f) - 1.0f;

                m_solidBrush->SetColor(HexColor(accentHex, 0.16f));
                m_rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x1, plotR.bottom), m_solidBrush, stepX + 0.5f);

                m_solidBrush->SetColor(HexColor(accentHex, 0.95f));
                m_rt->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), m_solidBrush, 1.8f);
            }
        }
    };

    // Row 1: 4 Real-Time Oscilloscopes (Always shown at top of workstation)
    float oscGap = 8.0f;
    float oscW = (availW - oscGap * 3.0f) * 0.25f;
    float oscH = 104.0f;

    wchar_t cpuVal[32] = {}, cpuSub[128] = {};
    swprintf_s(cpuVal, L"%.1f%%", cpu.totalLoadPct);
    swprintf_s(cpuSub, L"%uR / %uW  \x2022  %u MHz", cpu.physicalCores, cpu.logicalThreads, cpu.baseFreqMHz);
    drawOscilloscope(D2D1::RectF(workR.left + padX, curY, workR.left + padX + oscW, curY + oscH),
                     L"PROCESOR (CPU LIVE 60s)", cpuVal, cpuSub, cpu.loadHistory, 100.0f, 0x0078D7);

    wchar_t ramVal[32] = {}, ramSub[128] = {};
    swprintf_s(ramVal, L"%.1f%%", ram.usedPct);
    swprintf_s(ramSub, L"%s / %s", FileEngine::FormatSizeDetailed(ram.usedBytes).c_str(), FileEngine::FormatSizeDetailed(ram.totalBytes).c_str());
    drawOscilloscope(D2D1::RectF(workR.left + padX + (oscW + oscGap), curY, workR.left + padX + oscW * 2 + oscGap, curY + oscH),
                     L"PAMIĘĆ RAM (LIVE 60s)", ramVal, ramSub, ram.loadHistory, 100.0f, 0x7C3AED);

    wchar_t gpuVal[32] = {}, gpuSub[128] = {};
    uint64_t gVramUsed = gpus.empty() ? 0 : gpus[0].liveVramUsedBytes;
    uint64_t gVramTot  = gpus.empty() ? 0 : gpus[0].dedicatedVramBytes;
    float gLoad = (!gpus.empty() && gpus[0].hasNvmlTelemetry)
                  ? static_cast<float>(gpus[0].gpuLoadPct)
                  : (gVramTot > 0 ? static_cast<float>(100.0 * double(gVramUsed) / double(gVramTot)) : 0.0f);
    swprintf_s(gpuVal, L"%.0f%%", gLoad);
    if (!gpus.empty() && gpus[0].hasNvmlTelemetry) {
        swprintf_s(gpuSub, L"VRAM: %s / %s  \x2022  %u\x00B0" L"C", FileEngine::FormatSizeDetailed(gVramUsed).c_str(), FileEngine::FormatSizeDetailed(gVramTot).c_str(), gpus[0].tempCelsius);
    } else {
        swprintf_s(gpuSub, L"VRAM: %s / %s", FileEngine::FormatSizeDetailed(gVramUsed).c_str(), FileEngine::FormatSizeDetailed(gVramTot).c_str());
    }
    drawOscilloscope(D2D1::RectF(workR.left + padX + (oscW + oscGap) * 2, curY, workR.left + padX + oscW * 3 + oscGap * 2, curY + oscH),
                     L"KARTA GRAFICZNA (GPU/VRAM)", gpuVal, gpuSub, m_hwInspector.GetGpuLoadHistory(), 100.0f, 0x059669);

    double totalRxKB = 0.0, totalTxKB = 0.0;
    for (const auto& n : nets) {
        totalRxKB += n.liveRxBytesPerSec / 1024.0;
        totalTxKB += n.liveTxBytesPerSec / 1024.0;
    }
    wchar_t netVal[64] = {}, netSub[128] = {};
    if (totalRxKB + totalTxKB >= 1024.0) {
        swprintf_s(netVal, L"%.2f MB/s", (totalRxKB + totalTxKB) / 1024.0);
    } else {
        swprintf_s(netVal, L"%.1f KB/s", totalRxKB + totalTxKB);
    }
    swprintf_s(netSub, L"\x2193 RX: %.1f KB/s   \x2191 TX: %.1f KB/s", totalRxKB, totalTxKB);
    const auto& rxHist = m_hwInspector.GetNetRxHistoryKBps();
    float maxNetScale = 512.0f;
    for (float v : rxHist) if (v > maxNetScale) maxNetScale = v * 1.2f;
    drawOscilloscope(D2D1::RectF(workR.left + padX + (oscW + oscGap) * 3, curY, workR.left + padX + availW, curY + oscH),
                     L"SIEĆ (PRZEPUSTOWOŚĆ LIVE)", netVal, netSub, rxHist, maxNetScale, 0xD97706);

    curY += oscH + 10.0f;

    // Helper to render a structured Pro Diagnostic Card with key-value rows (White Explorer Card Style)
    struct KvRow {
        std::wstring key;
        std::wstring val;
        uint32_t valColor = 0x0F172A;
        // Optional tiny action button at the right end of the row (e.g. adapter settings)
        int actionCmd = 0;
        std::wstring actionLabel;
        std::wstring actionArg;
    };

    // Small rounded text button used inside cards; registers a card-hit (must be pushed before the card's own hit)
    auto drawTinyButton = [&](const D2D1_RECT_F& br, const std::wstring& label, int cmdId, const std::wstring& arg, int actionIndex = 0) {
        DrawCardTinyButton(br, label, cmdId, arg, actionIndex);
    };

    auto drawClippedText = [&](const std::wstring& text, const D2D1_RECT_F& r, IDWriteTextFormat* fmt, uint32_t colorHex) {
        DrawClippedText(text, r, fmt, colorHex);
    };

    // Draws the shared white card chrome (border, header strip, accent bar, title). Callers add the content.
    auto drawCardShell = [&](float x, float y, float w, float h, const std::wstring& title, uint32_t badgeHex, bool showCopyHint, float extraRightPad = 0.0f) {
        DrawCardShell(x, y, w, h, title, badgeHex, showCopyHint, extraRightPad);
    };

    auto pushCopyHit = [&](const D2D1_RECT_F& r, const std::wstring& payload, const std::wstring& label) {
        HardwareCardHit hc;
        hc.rect = r;
        hc.cmdId = 0;
        hc.copyText = payload;
        hc.label = label;
        m_hwCardHits.push_back(hc);
    };

    // Helper to render a structured Pro Diagnostic Card with key-value rows (White Explorer Card Style)
    auto drawProCard = [&](float x, float y, float w, const std::wstring& title, uint32_t badgeHex, const std::vector<KvRow>& rows) -> float {
        float rowH = 20.0f;
        float h = 34.0f + static_cast<float>(rows.size()) * rowH + 8.0f;
        D2D1_RECT_F cardR = D2D1::RectF(x, y, x + w, y + h);
        drawCardShell(x, y, w, h, title, badgeHex, true);

        std::wstring clipPayload = L"=== " + title + L" ===\r\n";
        float ry = y + 32.0f;
        float keyW = std::min(195.0f, w * 0.38f);
        const float btnW = 96.0f;

        for (size_t i = 0; i < rows.size(); ++i) {
            if (i % 2 == 1) {
                m_solidBrush->SetColor(HexColor(0xF8FAFC));
                m_rt->FillRectangle(D2D1::RectF(x + 4.0f, ry, x + w - 4.0f, ry + rowH), m_solidBrush);
            }
            bool hasBtn = rows[i].actionCmd != 0;
            float valRight = x + w - 10.0f - (hasBtn ? (btnW + 6.0f) : 0.0f);
            D2D1_RECT_F kR = D2D1::RectF(x + 12.0f, ry + 1.0f, x + keyW, ry + rowH);
            D2D1_RECT_F vR = D2D1::RectF(x + keyW + 4.0f, ry + 1.0f, valRight, ry + rowH);

            drawClippedText(rows[i].key, kR, m_fmtSmall, 0x475569);
            drawClippedText(rows[i].val, vR, m_fmtSmall, rows[i].valColor);

            if (hasBtn) {
                D2D1_RECT_F br = D2D1::RectF(x + w - 10.0f - btnW, ry + 2.0f, x + w - 10.0f, ry + rowH - 2.0f);
                drawTinyButton(br, rows[i].actionLabel, rows[i].actionCmd, rows[i].actionArg);
            }

            clipPayload += rows[i].key + L": " + rows[i].val + L"\r\n";
            ry += rowH;
        }

        pushCopyHit(cardR, clipPayload, title);
        return h;
    };

    float colGap = 10.0f;
    float halfW = (availW - colGap) * 0.5f;
    float leftX = workR.left + padX;
    float rightX = leftX + halfW + colGap;

    bool showAll = (m_hwSubTab == HardwareSubTab::All);
    const bool pl = (m_lang == Lang::PL);

    // =========================================================================
    // Temperature & SMART helpers
    // =========================================================================
    auto tempColor = [](float t, float warn, float crit) -> uint32_t {
        return t >= crit ? 0xDC2626u : (t >= warn ? 0xD97706u : 0x059669u);
    };
    auto diskWarn = [](const HwPhysicalDisk& d) -> float {
        return d.tempWarnC > 0 ? static_cast<float>(d.tempWarnC) : (d.isNvme ? 70.0f : (d.isRotational ? 50.0f : 60.0f));
    };
    auto diskCrit = [](const HwPhysicalDisk& d) -> float {
        return d.tempCritC > 0 ? static_cast<float>(d.tempCritC) : (d.isNvme ? 80.0f : (d.isRotational ? 60.0f : 70.0f));
    };
    auto fmtTemp = [](float t) -> std::wstring {
        wchar_t b[32] = {};
        swprintf_s(b, L"%.0f\x00B0" L"C", t);
        return b;
    };
    auto findDisk = [&](int idx) -> const HwPhysicalDisk* {
        for (const auto& d : pDisks) if (d.index == idx) return &d;
        return nullptr;
    };
    auto drawBadge = [&](const D2D1_RECT_F& r, const std::wstring& text, uint32_t fg, uint32_t bg) {
        m_solidBrush->SetColor(HexColor(bg));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(r, 8.0f, 8.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(fg));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), m_fmtSmall, r, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    };
    auto healthLabel = [&](const HwPhysicalDisk& d, uint32_t& fg, uint32_t& bg) -> std::wstring {
        if (d.smartState == HwSmartState::Ok) {
            if (d.healthLevel >= 2) { fg = 0xB91C1C; bg = 0xFEE2E2; return pl ? L"Awaria!" : L"Failing"; }
            if (d.healthLevel == 1) { fg = 0x92400E; bg = 0xFEF3C7; return pl ? L"Ostrzeżenie" : L"Warning"; }
            fg = 0x047857; bg = 0xD1FAE5; return pl ? L"Zdrowy" : L"Healthy";
        }
        if (d.smartState == HwSmartState::NotQueried) {
            fg = 0x0369A1; bg = 0xE0F2FE;
            return pl ? L"SMART..." : L"SMART...";
        }
        fg = 0x475569; bg = 0xE2E8F0;
        return d.smartState == HwSmartState::NeedsAdmin ? L"SMART: admin" : L"SMART: n/d";
    };
    // Graph source with the leading "no data yet" zeros removed
    auto trimmedHistory = [](const std::vector<float>& h) -> std::vector<float> {
        size_t first = 0;
        while (first < h.size() && h[first] <= 0.0f) ++first;
        return std::vector<float>(h.begin() + static_cast<std::ptrdiff_t>(first), h.end());
    };

    // ---- Physical drive list: click a drive to open its SMART panel ---------------------------
    auto drawDiskListCard = [&](float x, float y, float w) -> float {
        const float rowH = 46.0f;
        size_t n = std::max<size_t>(1, pDisks.size());
        float h = 34.0f + static_cast<float>(n) * rowH + 6.0f;
        std::wstring title = pl ? L"DYSKI FIZYCZNE  \x2022  kliknij dysk aby zobaczyć SMART" : L"PHYSICAL DRIVES  \x2022  click a drive for SMART";
        drawCardShell(x, y, w, h, title, 0x0284C7, true);

        std::wstring payload = L"=== " + title + L" ===\r\n";
        float ry = y + 32.0f;
        if (pDisks.empty()) {
            drawClippedText(pl ? L"Brak danych o dyskach fizycznych" : L"No physical drive data", D2D1::RectF(x + 12.0f, ry + 8.0f, x + w - 12.0f, ry + 28.0f), m_fmtSmall, 0x64748B);
        }
        for (const auto& pd : pDisks) {
            D2D1_RECT_F rr = D2D1::RectF(x + 6.0f, ry, x + w - 6.0f, ry + rowH - 3.0f);
            bool sel = (pd.index == m_hwSelectedDisk);
            bool hov = PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, rr);
            m_solidBrush->SetColor(HexColor(sel ? 0xE0F2FE : (hov ? 0xF1F5F9 : 0xFFFFFF)));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(rr, 4.0f, 4.0f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(sel ? 0x0078D7 : 0xE2E8F0));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(rr, 4.0f, 4.0f), m_solidBrush, sel ? 1.5f : 1.0f);

            // Drive type chip
            const wchar_t* kind = pd.isNvme ? L"NVMe" : (pd.isRotational ? L"HDD" : (pd.busType == L"USB" ? L"USB" : L"SSD"));
            uint32_t kindCol = pd.isNvme ? 0x7C3AED : (pd.isRotational ? 0x64748B : (pd.busType == L"USB" ? 0xD97706 : 0x0284C7));
            D2D1_RECT_F chip = D2D1::RectF(rr.left + 8.0f, rr.top + 9.0f, rr.left + 46.0f, rr.top + 31.0f);
            m_solidBrush->SetColor(HexColor(kindCol));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(chip, 4.0f, 4.0f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(0xFFFFFF));
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_rt->DrawTextW(kind, static_cast<UINT32>(wcslen(kind)), m_fmtSmall, chip, m_solidBrush);
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

            std::wstring line1 = L"#" + std::to_wstring(pd.index) + L"  " + pd.model;
            std::wstring line2 = FileEngine::FormatSizeDetailed(pd.capacityBytes) + L"  \x2022  " + pd.busType +
                                 (pd.firmwareRev.empty() ? L"" : (L"  \x2022  FW " + pd.firmwareRev)) +
                                 (pd.serialNumber.empty() ? L"" : (L"  \x2022  S/N " + pd.serialNumber));
            float textRight = rr.right - 110.0f;
            drawClippedText(line1, D2D1::RectF(rr.left + 54.0f, rr.top + 4.0f, textRight, rr.top + 23.0f), m_fmtBold, 0x0F172A);
            drawClippedText(line2, D2D1::RectF(rr.left + 54.0f, rr.top + 23.0f, textRight, rr.bottom - 1.0f), m_fmtSmall, 0x64748B);

            uint32_t hfg = 0, hbg = 0;
            std::wstring hl = healthLabel(pd, hfg, hbg);
            drawBadge(D2D1::RectF(rr.right - 102.0f, rr.top + 5.0f, rr.right - 8.0f, rr.top + 21.0f), hl, hfg, hbg);
            if (pd.tempC >= 0) {
                float t = static_cast<float>(pd.tempC);
                uint32_t col = tempColor(t, diskWarn(pd), diskCrit(pd));
                drawBadge(D2D1::RectF(rr.right - 102.0f, rr.top + 22.0f, rr.right - 8.0f, rr.top + 38.0f), L"\xD83C\xDF21 " + fmtTemp(t), col, 0xF1F5F9);
            } else {
                drawBadge(D2D1::RectF(rr.right - 102.0f, rr.top + 22.0f, rr.right - 8.0f, rr.top + 38.0f), L"\xD83C\xDF21 \x2014", 0x64748B, 0xF1F5F9);
            }

            HardwareCardHit sh;
            sh.rect = rr;
            sh.cmdId = CMD_HW_SELECT_DISK;
            sh.actionIndex = pd.index;
            sh.label = pd.model;
            m_hwCardHits.push_back(sh);

            payload += L"#" + std::to_wstring(pd.index) + L" " + pd.model + L" | " + line2;
            if (pd.tempC >= 0) payload += L" | " + fmtTemp(static_cast<float>(pd.tempC));
            payload += L" | " + hl + L"\r\n";
            ry += rowH;
        }
        pushCopyHit(D2D1::RectF(x + w - 90.0f, y, x + w, y + 28.0f), payload, title);
        return h;
    };

    // ---- SMART detail panel for the selected drive -------------------------------------------
    auto drawSmartPanel = [&](float x, float y, float w) -> float {
        const HwPhysicalDisk* sd = findDisk(m_hwSelectedDisk);
        if (!sd) return 0.0f;

        std::wstring title = (pl ? L"SMART  \x2022  Dysk #" : L"SMART  \x2022  Drive #") + std::to_wstring(sd->index) + L":  " + sd->model;
        const bool ok = (sd->smartState == HwSmartState::Ok);
        const float topBlockH = 98.0f, tblHdrH = 22.0f, tblRowH = 19.0f;
        float h = ok ? (34.0f + topBlockH + 10.0f + tblHdrH + static_cast<float>(sd->smartAttrs.size()) * tblRowH + 10.0f) : (34.0f + 76.0f);

        drawCardShell(x, y, w, h, title, 0x0284C7, false, 250.0f);

        // Header buttons: refresh + copy
        D2D1_RECT_F refreshBtn = D2D1::RectF(x + w - 126.0f, y + 4.0f, x + w - 8.0f, y + 25.0f);
        drawTinyButton(refreshBtn, pl ? L"\x21BB Odśwież SMART" : L"\x21BB Refresh SMART", CMD_HW_REFRESH_SMART, L"");

        std::wstring payload = L"=== " + title + L" ===\r\n";
        D2D1_RECT_F copyBtn = D2D1::RectF(x + w - 222.0f, y + 4.0f, x + w - 132.0f, y + 25.0f);

        if (!ok) {
            std::wstring msg;
            if (sd->smartState == HwSmartState::NotQueried) {
                msg = pl ? L"Trwa odczyt danych SMART w tle..."
                         : L"Reading SMART data in the background...";
            } else if (sd->smartState == HwSmartState::NeedsAdmin) {
                msg = pl ? L"Odczyt SMART tego dysku wymaga uprawnień administratora (Windows blokuje surowy dostęp do dysku zwykłym procesom).\nTemperatura jest odczytywana nadal, jeśli dysk ją udostępnia."
                         : L"Reading SMART for this drive requires administrator rights (Windows blocks raw disk access for normal processes).\nTemperature is still shown when the drive exposes it.";
            } else {
                msg = pl ? L"Ten dysk (USB / RAID / wirtualny) nie udostępnia danych SMART przez sterownik Windows."
                         : L"This drive (USB / RAID / virtual) does not expose SMART data through the Windows driver.";
            }
            D2D1_RECT_F box = D2D1::RectF(x + 10.0f, y + 36.0f, x + w - 10.0f, y + h - 8.0f);
            m_solidBrush->SetColor(HexColor(0xFFFBEB));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(box, 4.0f, 4.0f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(0xF59E0B));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(box, 4.0f, 4.0f), m_solidBrush, 1.0f);
            bool showAdminBtn = (sd->smartState == HwSmartState::NeedsAdmin) && !HardwareInspector::IsProcessElevated();
            float msgRight = box.right - 10.0f - (showAdminBtn ? 262.0f : 0.0f);
            m_rt->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
            m_solidBrush->SetColor(HexColor(0x92400E));
            m_rt->DrawTextW(msg.c_str(), static_cast<UINT32>(msg.size()), m_fmtSmall, D2D1::RectF(box.left + 10.0f, box.top + 6.0f, msgRight, box.bottom - 4.0f), m_solidBrush);
            m_rt->PopAxisAlignedClip();
            if (showAdminBtn) {
                drawTinyButton(D2D1::RectF(box.right - 262.0f, box.top + 14.0f, box.right - 10.0f, box.top + 38.0f),
                               pl ? L"\xD83D\xDEE1 Uruchom ponownie jako administrator" : L"\xD83D\xDEE1 Restart as administrator",
                               CMD_HW_RELAUNCH_ADMIN, L"");
            }
            return h;
        }

        // ---- Summary tiles (3 x 2) + temperature graph ----
        float y0 = y + 34.0f;
        float tilesW = std::max(360.0f, w * 0.58f);
        float tileGap = 8.0f;
        float tw = (tilesW - 10.0f - tileGap * 2.0f) / 3.0f;
        float th = 44.0f;
        auto drawTile = [&](int col, int row, const std::wstring& label, const std::wstring& value, uint32_t valueColor) {
            D2D1_RECT_F tr = D2D1::RectF(x + 10.0f + col * (tw + tileGap), y0 + row * (th + 8.0f),
                                         x + 10.0f + col * (tw + tileGap) + tw, y0 + row * (th + 8.0f) + th);
            m_solidBrush->SetColor(HexColor(0xF8FAFC));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(tr, 4.0f, 4.0f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(0xE2E8F0));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(tr, 4.0f, 4.0f), m_solidBrush, 1.0f);
            drawClippedText(label, D2D1::RectF(tr.left + 8.0f, tr.top + 3.0f, tr.right - 4.0f, tr.top + 19.0f), m_fmtSmall, 0x64748B);
            drawClippedText(value, D2D1::RectF(tr.left + 8.0f, tr.top + 19.0f, tr.right - 4.0f, tr.bottom - 1.0f), m_fmtBold, valueColor);
            payload += label + L": " + value + L"\r\n";
        };

        uint32_t hfg = 0, hbg = 0;
        std::wstring hl = healthLabel(*sd, hfg, hbg);
        drawTile(0, 0, pl ? L"Stan zdrowia (SMART)" : L"Health (SMART)", hl, hfg);

        std::wstring tempVal = L"\x2014";
        uint32_t tempCol = 0x64748B;
        if (sd->tempC >= 0) {
            tempVal = fmtTemp(static_cast<float>(sd->tempC));
            if (sd->tempPeakC > sd->tempC) tempVal += (pl ? L"   (szczyt " : L"   (peak ") + fmtTemp(static_cast<float>(sd->tempPeakC)) + L")";
            tempCol = tempColor(static_cast<float>(sd->tempC), diskWarn(*sd), diskCrit(*sd));
        }
        drawTile(1, 0, pl ? L"Temperatura" : L"Temperature", tempVal, tempCol);

        std::wstring pohVal = L"\x2014";
        if (sd->powerOnHours >= 0) {
            wchar_t b[96] = {};
            swprintf_s(b, L"%lld h  (%.1f %s)", static_cast<long long>(sd->powerOnHours), sd->powerOnHours / 24.0, pl ? L"dni" : L"days");
            pohVal = b;
        }
        drawTile(2, 0, pl ? L"Czas pracy" : L"Power-on time", pohVal, 0x0F172A);

        drawTile(0, 1, pl ? L"Cykle zasilania" : L"Power cycles",
                 sd->powerCycles >= 0 ? std::to_wstring(sd->powerCycles) : L"\x2014", 0x0F172A);

        std::wstring lifeVal = L"\x2014";
        uint32_t lifeCol = 0x64748B;
        if (sd->lifeRemainingPct >= 0) {
            lifeVal = std::to_wstring(sd->lifeRemainingPct) + L"%";
            lifeCol = sd->lifeRemainingPct > 50 ? 0x059669 : (sd->lifeRemainingPct > 20 ? 0xD97706 : 0xDC2626);
        }
        drawTile(1, 1, sd->isRotational ? (pl ? L"Żywotność SSD (n/d dla HDD)" : L"SSD life (n/a for HDD)")
                                        : (pl ? L"Pozostała żywotność SSD" : L"SSD life remaining"), lifeVal, lifeCol);

        std::wstring ioVal = L"\x2014";
        if (sd->bytesWritten >= 0 || sd->bytesRead >= 0) {
            ioVal.clear();
            if (sd->bytesWritten >= 0) ioVal += L"\x2191 " + FileEngine::FormatSizeDetailed(static_cast<uint64_t>(sd->bytesWritten));
            if (sd->bytesRead >= 0) ioVal += (ioVal.empty() ? L"" : L"   ") + (L"\x2193 " + FileEngine::FormatSizeDetailed(static_cast<uint64_t>(sd->bytesRead)));
        }
        drawTile(2, 1, pl ? L"Zapisano / Odczytano (łącznie)" : L"Total written / read", ioVal, 0x0F172A);

        // Temperature graph (history sampled every ~3 s)
        std::vector<float> th2 = trimmedHistory(sd->tempHistory);
        float peak = static_cast<float>(std::max(sd->tempPeakC, 40));
        D2D1_RECT_F gBox = D2D1::RectF(x + tilesW + 10.0f, y0, x + w - 10.0f, y0 + topBlockH);
        std::wstring gBig = sd->tempC >= 0 ? fmtTemp(static_cast<float>(sd->tempC)) : L"\x2014";
        std::wstring gSub = pl ? L"ostatnie ~3 min  \x2022  skala 0\x2013" : L"last ~3 min  \x2022  scale 0\x2013";
        gSub += std::to_wstring(static_cast<int>(std::max(70.0f, peak * 1.3f))) + L"\x00B0" L"C";
        drawOscilloscope(gBox, pl ? L"TEMPERATURA DYSKU" : L"DRIVE TEMPERATURE", gBig, gSub, th2,
                         std::max(70.0f, peak * 1.3f), tempCol == 0x64748B ? 0x0284C7u : tempCol);

        // ---- Attribute table ----
        float ty = y0 + topBlockH + 10.0f;
        float x0 = x + 12.0f;
        float idX = x0, nameX = x0 + 38.0f;
        float nameW = std::min(310.0f, w * 0.32f);
        float curX = nameX + nameW + 6.0f, worstX = curX + 66.0f, thrX = worstX + 66.0f, rawX = thrX + 66.0f;
        float statusW = 92.0f;
        float rawRight = x + w - 12.0f - statusW;

        D2D1_RECT_F th0 = D2D1::RectF(x + 6.0f, ty, x + w - 6.0f, ty + tblHdrH);
        m_solidBrush->SetColor(HexColor(0xEEF2F6));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(th0, 3.0f, 3.0f), m_solidBrush);
        auto hdrCell = [&](float cx0, float cx1, const wchar_t* t) {
            drawClippedText(t, D2D1::RectF(cx0, ty + 3.0f, cx1, ty + tblHdrH), m_fmtBold, 0x334155);
        };
        hdrCell(idX, nameX, L"ID");
        hdrCell(nameX, curX, pl ? L"Atrybut" : L"Attribute");
        hdrCell(curX, worstX, pl ? L"Aktualna" : L"Current");
        hdrCell(worstX, thrX, pl ? L"Najgorsza" : L"Worst");
        hdrCell(thrX, rawX, pl ? L"Próg" : L"Thresh");
        hdrCell(rawX, rawRight, pl ? L"Wartość surowa (RAW)" : L"Raw value");
        hdrCell(rawRight + 14.0f, x + w - 12.0f, L"Status");
        payload += L"ID\tAttribute\tCurrent\tWorst\tThresh\tRaw\tStatus\r\n";

        float ry = ty + tblHdrH;
        int rowIdx = 0;
        for (const auto& a : sd->smartAttrs) {
            D2D1_RECT_F rowR = D2D1::RectF(x + 6.0f, ry, x + w - 6.0f, ry + tblRowH);
            if (a.status == 2) {
                m_solidBrush->SetColor(HexColor(0xFEE2E2));
                m_rt->FillRectangle(rowR, m_solidBrush);
            } else if (a.status == 1) {
                m_solidBrush->SetColor(HexColor(0xFEF3C7));
                m_rt->FillRectangle(rowR, m_solidBrush);
            } else if (rowIdx % 2 == 1) {
                m_solidBrush->SetColor(HexColor(0xF8FAFC));
                m_rt->FillRectangle(rowR, m_solidBrush);
            }
            std::wstring idTxt = a.id > 0 ? std::to_wstring(a.id) : L"";
            drawClippedText(idTxt, D2D1::RectF(idX, ry + 1.0f, nameX, ry + tblRowH), m_fmtSmall, 0x64748B);
            drawClippedText(a.name, D2D1::RectF(nameX, ry + 1.0f, curX - 4.0f, ry + tblRowH), m_fmtSmall, 0x0F172A);
            std::wstring curTxt, worstTxt, thrTxt;
            if (a.hasNormalized) {
                curTxt = std::to_wstring(a.current);
                worstTxt = std::to_wstring(a.worst);
                thrTxt = std::to_wstring(a.threshold);
                drawClippedText(curTxt, D2D1::RectF(curX, ry + 1.0f, worstX, ry + tblRowH), m_fmtSmall, 0x0F172A);
                drawClippedText(worstTxt, D2D1::RectF(worstX, ry + 1.0f, thrX, ry + tblRowH), m_fmtSmall, 0x475569);
                drawClippedText(thrTxt, D2D1::RectF(thrX, ry + 1.0f, rawX, ry + tblRowH), m_fmtSmall, 0x475569);
            }
            drawClippedText(a.rawText, D2D1::RectF(rawX, ry + 1.0f, rawRight, ry + tblRowH), m_fmtSmall, a.status >= 1 ? 0x92400E : 0x0067B8);

            uint32_t dotCol = a.status == 2 ? 0xDC2626 : (a.status == 1 ? 0xD97706 : (a.status == 0 ? 0x10B981 : 0x94A3B8));
            const wchar_t* stTxt = a.status == 2 ? (pl ? L"Awaria" : L"Failing")
                                 : (a.status == 1 ? (pl ? L"Uwaga" : L"Warning")
                                 : (a.status == 0 ? L"OK" : L""));
            m_solidBrush->SetColor(HexColor(dotCol));
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rawRight + 20.0f, ry + tblRowH * 0.5f), 4.0f, 4.0f), m_solidBrush);
            drawClippedText(stTxt, D2D1::RectF(rawRight + 30.0f, ry + 1.0f, x + w - 12.0f, ry + tblRowH), m_fmtSmall, dotCol);

            payload += idTxt + L"\t" + a.name + L"\t" + curTxt + L"\t" + worstTxt + L"\t" + thrTxt + L"\t" + a.rawText + L"\t" + stTxt + L"\r\n";
            ry += tblRowH;
            ++rowIdx;
        }

        // Copy button (registers a copy hit over the whole report)
        bool copyHov = PathUtils::PtInRectF(m_mousePt.x, m_mousePt.y, copyBtn);
        m_solidBrush->SetColor(HexColor(copyHov ? 0x0078D7 : 0xF1F5F9));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(copyBtn, 3.0f, 3.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(copyHov ? 0x005A9E : 0xCBD5E1));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(copyBtn, 3.0f, 3.0f), m_solidBrush, 1.0f);
        m_solidBrush->SetColor(HexColor(copyHov ? 0xFFFFFF : 0x1E293B));
        const wchar_t* cpLbl = pl ? L"\xD83D\xDCCB Kopiuj" : L"\xD83D\xDCCB Copy";
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(cpLbl, static_cast<UINT32>(wcslen(cpLbl)), m_fmtSmall, copyBtn, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        pushCopyHit(copyBtn, payload, title);
        return h;
    };

    // =========================================================================
    // SECTION T: Temperatures (GPU / ACPI thermal zones / drives) — dashboard + "Temperatury" tab
    // =========================================================================
    if (showAll || m_hwSubTab == HardwareSubTab::Thermal) {
        struct ThermalEntry { std::wstring label; float temp; float warn; float crit; std::wstring extra; };
        std::vector<ThermalEntry> sensors;
        for (size_t i = 0; i < gpus.size(); ++i) {
            if (!gpus[i].hasNvmlTelemetry || gpus[i].tempCelsius == 0) continue;
            wchar_t ex[64] = {};
            swprintf_s(ex, pl ? L"   \x2022   wentylator %u%%" : L"   \x2022   fan %u%%", gpus[i].fanSpeedPct);
            sensors.push_back({ L"GPU: " + gpus[i].name, static_cast<float>(gpus[i].tempCelsius), 80.0f, 92.0f, ex });
        }
        for (const auto& z : m_hwInspector.GetAcpiThermalZones()) {
            sensors.push_back({ z.name, z.tempC, z.warnC, z.critC, L"" });
        }
        for (const auto& pd : pDisks) {
            if (pd.tempC < 0) continue;
            std::wstring ex;
            if (pd.tempPeakC > pd.tempC) ex = (pl ? L"   \x2022   szczyt " : L"   \x2022   peak ") + fmtTemp(static_cast<float>(pd.tempPeakC));
            sensors.push_back({ (pl ? L"Dysk #" : L"Drive #") + std::to_wstring(pd.index) + L": " + pd.model,
                                static_cast<float>(pd.tempC), diskWarn(pd), diskCrit(pd), ex });
        }

        // Thermal tab: live graphs on top (ACPI max, GPU, each drive with a reading)
        if (m_hwSubTab == HardwareSubTab::Thermal) {
            struct GraphSrc { std::wstring title; std::wstring big; std::wstring sub; std::vector<float> hist; uint32_t color; };
            std::vector<GraphSrc> graphs;
            if (!m_hwInspector.GetAcpiThermalZones().empty()) {
                float mx = 0.0f;
                for (const auto& z : m_hwInspector.GetAcpiThermalZones()) mx = std::max(mx, z.tempC);
                graphs.push_back({ pl ? L"STREFA ACPI (MAX)" : L"ACPI ZONE (MAX)", fmtTemp(mx), pl ? L"płyta / obudowa / CPU (przybliżone)" : L"board / chassis / CPU (approx.)",
                                   trimmedHistory(m_hwInspector.GetAcpiTempHistory()), tempColor(mx, 70.0f, 90.0f) });
            }
            for (const auto& g : gpus) {
                if (!g.hasNvmlTelemetry || g.tempCelsius == 0) continue;
                graphs.push_back({ L"GPU", fmtTemp(static_cast<float>(g.tempCelsius)), g.name,
                                   trimmedHistory(m_hwInspector.GetGpuTempHistory()), tempColor(static_cast<float>(g.tempCelsius), 80.0f, 92.0f) });
                break;
            }
            for (const auto& pd : pDisks) {
                if (pd.tempC < 0) continue;
                graphs.push_back({ (pl ? L"DYSK #" : L"DRIVE #") + std::to_wstring(pd.index), fmtTemp(static_cast<float>(pd.tempC)), pd.model,
                                   trimmedHistory(pd.tempHistory), tempColor(static_cast<float>(pd.tempC), diskWarn(pd), diskCrit(pd)) });
            }
            size_t shown = std::min<size_t>(graphs.size(), 8);
            for (size_t gi = 0; gi < shown; ++gi) {
                size_t col = gi % 4, row = gi / 4;
                float gx = workR.left + padX + col * (oscW + oscGap);
                float gy = curY + row * (oscH + oscGap);
                drawOscilloscope(D2D1::RectF(gx, gy, gx + oscW, gy + oscH), graphs[gi].title.c_str(), graphs[gi].big, graphs[gi].sub,
                                 graphs[gi].hist, 100.0f, graphs[gi].color);
            }
            if (shown > 0) {
                size_t rowsUsed = (shown + 3) / 4;
                curY += rowsUsed * (oscH + oscGap) + 2.0f;
            }
        }

        std::vector<KvRow> thRows;
        for (const auto& s : sensors) {
            thRows.push_back({ s.label, fmtTemp(s.temp) + s.extra, tempColor(s.temp, s.warn, s.crit) });
        }
        if (sensors.empty()) {
            thRows.push_back({ pl ? L"Brak czujników" : L"No sensors", pl ? L"Windows nie zgłosił żadnych odczytów temperatury" : L"Windows reported no temperature readings", 0x64748B });
        }
        thRows.push_back({ L"\x2139 CPU", pl ? L"Temperatura rdzeni wymaga sterownika jądra (np. LibreHardwareMonitor)." : L"Core temperature needs a kernel driver (e.g. LibreHardwareMonitor).", 0x64748B });
        thRows.push_back({ L"\x2139 ACPI", pl ? L"Strefy ACPI to czujniki płyty głównej — nie zawsze odpowiadają CPU." : L"ACPI zones are motherboard sensors — not always the CPU die.", 0x64748B });

        float hT = drawProCard(leftX, curY, halfW, pl ? L"TEMPERATURY (CZUJNIKI NA ŻYWO)" : L"TEMPERATURES (LIVE SENSORS)", 0xDC2626, thRows);

        // Right card: horizontal heat bars 0..100°C with warning / critical ticks
        drawCardShell(rightX, curY, halfW, hT, pl ? L"MAPA TEMPERATUR (0–100\x00B0" L"C)" : L"TEMPERATURE MAP (0–100\x00B0" L"C)", 0xD97706, false);
        {
            float by = curY + 34.0f;
            float labelW = std::min(210.0f, halfW * 0.38f);
            float trackL = rightX + 12.0f + labelW + 6.0f;
            float trackR = rightX + halfW - 70.0f;
            for (const auto& s : sensors) {
                if (by + 18.0f > curY + hT - 4.0f) break;
                drawClippedText(s.label, D2D1::RectF(rightX + 12.0f, by, trackL - 6.0f, by + 20.0f), m_fmtSmall, 0x475569);
                D2D1_RECT_F tr = D2D1::RectF(trackL, by + 5.0f, trackR, by + 15.0f);
                m_solidBrush->SetColor(HexColor(0xE2E8F0));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(tr, 4.0f, 4.0f), m_solidBrush);
                uint32_t col = tempColor(s.temp, s.warn, s.crit);
                float frac = std::clamp(s.temp / 100.0f, 0.0f, 1.0f);
                if (frac > 0.01f) {
                    m_solidBrush->SetColor(HexColor(col));
                    m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tr.left, tr.top, tr.left + (tr.right - tr.left) * frac, tr.bottom), 4.0f, 4.0f), m_solidBrush);
                }
                float wx = tr.left + (tr.right - tr.left) * std::clamp(s.warn / 100.0f, 0.0f, 1.0f);
                float cxk = tr.left + (tr.right - tr.left) * std::clamp(s.crit / 100.0f, 0.0f, 1.0f);
                m_solidBrush->SetColor(HexColor(0xD97706, 0.9f));
                m_rt->DrawLine(D2D1::Point2F(wx, tr.top - 2.0f), D2D1::Point2F(wx, tr.bottom + 2.0f), m_solidBrush, 1.2f);
                m_solidBrush->SetColor(HexColor(0xDC2626, 0.9f));
                m_rt->DrawLine(D2D1::Point2F(cxk, tr.top - 2.0f), D2D1::Point2F(cxk, tr.bottom + 2.0f), m_solidBrush, 1.2f);

                m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                m_solidBrush->SetColor(HexColor(col));
                std::wstring tv = fmtTemp(s.temp);
                m_rt->DrawTextW(tv.c_str(), static_cast<UINT32>(tv.size()), m_fmtBold, D2D1::RectF(trackR + 6.0f, by, rightX + halfW - 10.0f, by + 20.0f), m_solidBrush);
                m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                by += 20.0f;
            }
        }
        curY += hT + 10.0f;
    }

    // =========================================================================
    // SECTION A: CPU Architecture, Cache Hierarchy, ISA Flags & Per-Core Heatmap
    // =========================================================================
    if (showAll || m_hwSubTab == HardwareSubTab::Cpu) {
        std::vector<KvRow> cpuRows;
        cpuRows.push_back({ m_lang == Lang::PL ? L"Model procesora" : L"Processor Brand", cpu.brand, 0x0067B8 });
        cpuRows.push_back({ m_lang == Lang::PL ? L"Producent / Gniazdo" : L"Vendor / Socket", cpu.vendor + L"  \x2022  " + cpu.socketDesignation });
        wchar_t topoBuf[128] = {};
        swprintf_s(topoBuf, L"%u fizycznych rdzeni  /  %u wątków logicznych  (%u pakiet, %u NUMA)", cpu.physicalCores, cpu.logicalThreads, cpu.packages, cpu.numaNodes);
        cpuRows.push_back({ m_lang == Lang::PL ? L"Rdzenie / Wątki" : L"Cores / Threads", topoBuf, 0x059669 });
        wchar_t famBuf[128] = {};
        swprintf_s(famBuf, L"%s  \x2022  Family %d, Model %d, Stepping %d", cpu.architecture.c_str(), cpu.family, cpu.model, cpu.stepping);
        cpuRows.push_back({ L"Architektura (CPUID)", famBuf });
        wchar_t clkBuf[128] = {};
        swprintf_s(clkBuf, L"Bazowe: %u MHz   \x2022   Max Boost: %u MHz", cpu.baseFreqMHz, cpu.maxFreqMHz);
        cpuRows.push_back({ m_lang == Lang::PL ? L"Taktowanie" : L"Clock Speeds", clkBuf });
        wchar_t cacheBuf[160] = {};
        swprintf_s(cacheBuf, L"L1D: %u KB (%ux)  |  L1I: %u KB (%ux)  |  L2: %u KB (%ux)  |  L3: %u KB (Linia %u B)",
                   cpu.l1DataKB, cpu.l1DataCount, cpu.l1InstKB, cpu.l1InstCount, cpu.l2KB, cpu.l2Count, cpu.l3KB, cpu.cacheLineBytes);
        cpuRows.push_back({ L"Hierarchia Cache", cacheBuf, 0xB45309 });

        std::wstring isa;
        for (size_t i = 0; i < cpu.isaFeatures.size(); ++i) {
            isa += L"[" + cpu.isaFeatures[i] + L"] ";
        }
        cpuRows.push_back({ L"Instrukcje ISA", isa, 0x6D28D9 });

        if (cpu.benchSingleCoreGops > 0.0) {
            wchar_t bBuf[128] = {};
            swprintf_s(bBuf, L"1-Wątek: %.2f GOPS   \x2022   Wielowątkowo (%uW): %.2f GOPS (MP %.2fx)",
                       cpu.benchSingleCoreGops, cpu.logicalThreads, cpu.benchMultiCoreGops, cpu.benchMpRatio);
            cpuRows.push_back({ L"\x26A1 Wynik Benchmarku CPU", bBuf, 0x0067B8 });
        } else {
            cpuRows.push_back({ L"\x26A1 Benchmark CPU", m_lang == Lang::PL ? L"Kliknij [⚡ Test CPU/RAM] u góry aby zmierzyć GOPS" : L"Click [⚡ Bench CPU] above to measure GOPS", 0x64748B });
        }

        float hLeft = drawProCard(leftX, curY, halfW, m_lang == Lang::PL ? L"PROCESOR (ARCHITEKTURA CPUID & CACHE)" : L"PROCESSOR (CPUID & CACHE TOPOLOGY)", 0x0078D7, cpuRows);

        // Right card: Per-Core Live Utilization Heatmap Grid (White Explorer Card)
        int nCores = static_cast<int>(cpu.coreLoadPct.size());
        int cols = (nCores > 16) ? 8 : 4;
        int rowsCount = std::max(1, (nCores + cols - 1) / cols);
        float cellH = 30.0f;
        float heatH = std::max(hLeft, 44.0f + rowsCount * cellH + 10.0f);

        D2D1_RECT_F heatCard = D2D1::RectF(rightX, curY, rightX + halfW, curY + heatH);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(heatCard, 5.0f, 5.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xD8E0EA));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(heatCard, 5.0f, 5.0f), m_solidBrush, 1.0f);

        D2D1_RECT_F hhR = D2D1::RectF(rightX + 1.0f, curY + 1.0f, rightX + halfW - 1.0f, curY + 28.0f);
        m_solidBrush->SetColor(HexColor(0xF8FAFC));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(hhR, 4.5f, 4.5f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xE2E8F0));
        m_rt->DrawLine(D2D1::Point2F(rightX + 1.0f, curY + 28.0f), D2D1::Point2F(rightX + halfW - 1.0f, curY + 28.0f), m_solidBrush, 1.0f);
        m_solidBrush->SetColor(HexColor(0x059669));
        m_rt->FillRectangle(D2D1::RectF(rightX + 1.0f, curY + 4.0f, rightX + 5.0f, curY + 24.0f), m_solidBrush);

        wchar_t heatTitle[128] = {};
        swprintf_s(heatTitle, L"OBCIĄŻENIE RDZENI NA ŻYWO (%d WĄTKÓW \x2022 %.1f%%)", nCores, cpu.totalLoadPct);
        D2D1_RECT_F htR = D2D1::RectF(rightX + 12.0f, curY + 4.0f, rightX + halfW - 10.0f, curY + 26.0f);
        m_solidBrush->SetColor(HexColor(0x0F172A));
        m_rt->DrawTextW(heatTitle, static_cast<UINT32>(wcslen(heatTitle)), m_fmtBold, htR, m_solidBrush);

        float gridPad = 8.0f;
        float cellW = (halfW - gridPad * 2.0f - (cols - 1) * 4.0f) / static_cast<float>(cols);
        for (int c = 0; c < nCores; ++c) {
            int rIdx = c / cols;
            int cIdx = c % cols;
            float cx0 = rightX + gridPad + cIdx * (cellW + 4.0f);
            float cy0 = curY + 36.0f + rIdx * cellH;
            D2D1_RECT_F cBox = D2D1::RectF(cx0, cy0, cx0 + cellW, cy0 + cellH - 4.0f);

            float pct = std::clamp(cpu.coreLoadPct[c], 0.0f, 100.0f);
            uint32_t barCol = (pct > 85.0f) ? 0xEF4444 : (pct > 55.0f ? 0xF59E0B : (pct > 20.0f ? 0x10B981 : 0x0078D7));

            m_solidBrush->SetColor(HexColor(0xF8FAFC));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(cBox, 3.0f, 3.0f), m_solidBrush);

            float fillW = (cBox.right - cBox.left) * (pct * 0.01f);
            if (fillW > 1.5f) {
                D2D1_RECT_F fBox = D2D1::RectF(cBox.left, cBox.top, cBox.left + fillW, cBox.bottom);
                m_solidBrush->SetColor(HexColor(barCol, 0.22f));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(fBox, 3.0f, 3.0f), m_solidBrush);
            }
            m_solidBrush->SetColor(HexColor(barCol, 0.65f));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(cBox, 3.0f, 3.0f), m_solidBrush, 1.0f);

            wchar_t cLbl[32] = {};
            swprintf_s(cLbl, L"C%d: %.0f%%", c, pct);
            m_solidBrush->SetColor(HexColor(0x0F172A));
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_rt->DrawTextW(cLbl, static_cast<UINT32>(wcslen(cLbl)), m_fmtSmall, cBox, m_solidBrush);
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        curY += std::max(hLeft, heatH) + 10.0f;
    }

    // =========================================================================
    // SECTION B: GPU & Connected Monitors Inspector (DXGI + NVML)
    // =========================================================================
    if (showAll || m_hwSubTab == HardwareSubTab::Gpu) {
        std::vector<KvRow> gpuRows;
        for (size_t i = 0; i < gpus.size(); ++i) {
            const auto& g = gpus[i];
            std::wstring prefix = L"[GPU " + std::to_wstring(i) + L"] ";
            gpuRows.push_back({ prefix + L"Układ graficzny", g.name + L" (" + g.vendorName + L")", 0x059669 });
            gpuRows.push_back({ prefix + L"VRAM Dedykowany (Użycie)",
                                FileEngine::FormatSizeDetailed(g.liveVramUsedBytes) + L" / " + FileEngine::FormatSizeDetailed(g.dedicatedVramBytes) +
                                L"   (Współdzielony: " + FileEngine::FormatSizeDetailed(g.sharedSysMemBytes) + L")", 0x0067B8 });
            wchar_t pciBuf[128] = {};
            swprintf_s(pciBuf, L"Vendor: 0x%04X  \x2022  Device: 0x%04X  \x2022  SubSys: 0x%08X  \x2022  Rev: 0x%02X", g.vendorId, g.deviceId, g.subSysId, g.revision);
            gpuRows.push_back({ prefix + L"Identyfikatory PCI", pciBuf });
            if (!g.driverVersion.empty()) {
                gpuRows.push_back({ prefix + L"Wersja sterownika", g.driverVersion });
            }
            if (g.hasNvmlTelemetry) {
                wchar_t nvBuf[160] = {};
                swprintf_s(nvBuf, L"Rdzeń: %u%% (%u MHz)  \x2022  VRAM: %u MHz  \x2022  Temp: %u\x00B0" L"C  \x2022  Wentylator: %u%%  \x2022  Moc: %.1f W",
                           g.gpuLoadPct, g.coreClockMHz, g.memClockMHz, g.tempCelsius, g.fanSpeedPct, g.powerDrawWatts);
                gpuRows.push_back({ prefix + L"Sensory NVML Live", nvBuf, 0xB45309 });
            }
        }

        std::vector<KvRow> monRows;
        for (size_t i = 0; i < displays.size(); ++i) {
            const auto& d = displays[i];
            wchar_t resBuf[128] = {};
            swprintf_s(resBuf, L"%u \x00D7 %u @ %u Hz (%u-bit)%s",
                       d.width, d.height, d.refreshHz, d.bitsPerPel, d.isPrimary ? L"  [GŁÓWNY]" : L"");
            monRows.push_back({ d.monitorName + L" (" + d.deviceName + L")", resBuf, d.isPrimary ? 0x0067B8u : 0x0F172Au });
            if (!d.gpuAdapterName.empty()) {
                monRows.push_back({ L"   Podłączony do", d.gpuAdapterName, 0x64748B });
            }
        }
        if (monRows.empty()) {
            monRows.push_back({ L"Ekran", L"Brak dodatkowych danych EDID" });
        }

        float hG = drawProCard(leftX,  curY, halfW, m_lang == Lang::PL ? L"KARTY GRAFICZNE (DXGI / VRAM / NVML)" : L"GRAPHICS ADAPTERS (DXGI / VRAM / NVML)", 0x059669, gpuRows);
        float hM = drawProCard(rightX, curY, halfW, m_lang == Lang::PL ? L"MONITORY I MATRYCE (ROZDZIELCZOŚĆ & HZ)" : L"CONNECTED DISPLAYS & REFRESH RATE", 0x0284C7, monRows);
        curY += std::max(hG, hM) + 10.0f;
    }

    // =========================================================================
    // SECTION C: Physical RAM DIMM Slots (SMBIOS Type 17) & Motherboard/BIOS
    // =========================================================================
    if (showAll || m_hwSubTab == HardwareSubTab::Ram || m_hwSubTab == HardwareSubTab::Motherboard) {
        std::vector<KvRow> ramRows;
        wchar_t totRamBuf[160] = {};
        swprintf_s(totRamBuf, L"%s używane z %s (%.1f%%)   \x2022   Wolne: %s",
                   FileEngine::FormatSizeDetailed(ram.usedBytes).c_str(),
                   FileEngine::FormatSizeDetailed(ram.totalBytes).c_str(),
                   ram.usedPct,
                   FileEngine::FormatSizeDetailed(ram.availBytes).c_str());
        ramRows.push_back({ L"Pamięć fizyczna RAM", totRamBuf, 0x7C3AED });
        ramRows.push_back({ L"Pamięć zadeklarowana (Commit)",
                            FileEngine::FormatSizeDetailed(ram.commitTotalBytes) + L" / " + FileEngine::FormatSizeDetailed(ram.commitLimitBytes) +
                            L"   (Szczyt: " + FileEngine::FormatSizeDetailed(ram.commitPeakBytes) + L")" });
        ramRows.push_back({ L"Pula jądra (Paged / Non-Paged)",
                            FileEngine::FormatSizeDetailed(ram.kernelPagedBytes) + L" / " + FileEngine::FormatSizeDetailed(ram.kernelNonPagedBytes) });

        if (ram.benchReadGBps > 0.0) {
            wchar_t rbBuf[128] = {};
            swprintf_s(rbBuf, L"Odczyt: %.2f GB/s   \x2022   Zapis: %.2f GB/s   \x2022   Kopiowanie: %.2f GB/s",
                       ram.benchReadGBps, ram.benchWriteGBps, ram.benchCopyGBps);
            ramRows.push_back({ L"\x26A1 Przepustowość RAM", rbBuf, 0x059669 });
        }

        for (size_t i = 0; i < ram.dimms.size(); ++i) {
            const auto& dm = ram.dimms[i];
            wchar_t dSpec[256] = {};
            swprintf_s(dSpec, L"%s  \x2022  %s @ %u MT/s (Max %u)  \x2022  %s %s",
                       FileEngine::FormatSizeDetailed(dm.sizeBytes).c_str(),
                       dm.memoryType.c_str(), dm.configuredSpeedMHz, dm.speedMHz,
                       dm.manufacturer.c_str(), dm.partNumber.c_str());
            ramRows.push_back({ L"Slot [" + dm.slotLocator + L"]", dSpec, 0x0067B8 });
        }

        std::vector<KvRow> brdRows;
        brdRows.push_back({ L"Producent płyty głównej", mobo.boardManufacturer, 0xB45309 });
        brdRows.push_back({ L"Model płyty głównej", mobo.boardProduct + (mobo.boardVersion.empty() ? L"" : (L" (Rev " + mobo.boardVersion + L")")), 0x0067B8 });
        if (!mobo.boardSerial.empty()) {
            brdRows.push_back({ L"Numer seryjny płyty", mobo.boardSerial });
        }
        brdRows.push_back({ L"Producent UEFI / BIOS", mobo.biosVendor });
        brdRows.push_back({ L"Wersja i Data BIOS", mobo.biosVersion + L"   (" + mobo.biosReleaseDate + L")", 0x059669 });
        brdRows.push_back({ L"System / Produkt", mobo.systemManufacturer + L" " + mobo.systemProductName });
        if (!mobo.systemSku.empty()) {
            brdRows.push_back({ L"SKU / Obudowa", mobo.systemSku + L" / " + mobo.chassisType });
        }

        float hR = drawProCard(leftX,  curY, halfW, m_lang == Lang::PL ? L"PAMIĘĆ RAM & KOŚCI FIZYCZNE (SMBIOS TYPE 17)" : L"PHYSICAL RAM & DIMM SLOTS (SMBIOS TYPE 17)", 0x7C3AED, ramRows);
        float hB = drawProCard(rightX, curY, halfW, m_lang == Lang::PL ? L"PŁYTA GŁÓWNA, CHIPSET & UEFI / BIOS (SMBIOS)" : L"MOTHERBOARD & UEFI / BIOS FIRMWARE", 0xD97706, brdRows);
        curY += std::max(hR, hB) + 10.0f;
    }

    // =========================================================================
    // SECTION D: Physical Disks (NVMe/SATA/USB IOCTL) & Logical Volumes
    // =========================================================================
    if (showAll || m_hwSubTab == HardwareSubTab::Disks) {
        std::vector<KvRow> volRows;
        for (const auto& v : volumes) {
            wchar_t vBuf[256] = {};
            swprintf_s(vBuf, L"[%s]  Użyte: %s / %s (%.1f%%)  \x2022  Wolne: %s",
                       v.fileSystem.c_str(),
                       FileEngine::FormatSizeDetailed(v.usedBytes).c_str(),
                       FileEngine::FormatSizeDetailed(v.totalBytes).c_str(),
                       v.usedPct,
                       FileEngine::FormatSizeDetailed(v.freeBytes).c_str());
            uint32_t col = (v.usedPct > 90.0f) ? 0xDC2626 : (v.usedPct > 75.0f ? 0xB45309 : 0x059669);
            volRows.push_back({ v.rootPath + L" " + v.label + L" (" + v.driveTypeStr + L")", vBuf, col });
        }

        float hP = drawDiskListCard(leftX, curY, halfW);
        float hV = drawProCard(rightX, curY, halfW, m_lang == Lang::PL ? L"PARTYCJE LOGICZNE I SYSTEMY PLIKÓW" : L"LOGICAL VOLUMES & PARTITIONS", 0x059669, volRows);
        curY += std::max(hP, hV) + 10.0f;

        curY += drawSmartPanel(leftX, curY, availW) + 10.0f;
    }

    // =========================================================================
    // SECTION E: Network Interfaces & OS / Power / Audio / Top 10 Processes
    // =========================================================================
    if (showAll || m_hwSubTab == HardwareSubTab::Network || m_hwSubTab == HardwareSubTab::System) {
        std::vector<KvRow> netRows;
        for (const auto& n : nets) {
            std::wstring status = n.isConnected ? L"[AKTYWNA] " : L"[NIEAKTYWNA] ";
            KvRow nameRow = { status + n.friendlyName, n.description, n.isConnected ? 0x059669u : 0x64748Bu };
            nameRow.actionCmd = CMD_HW_NET_SETTINGS;
            nameRow.actionLabel = (m_lang == Lang::PL) ? L"\x2699 Ustawienia" : L"\x2699 Settings";
            nameRow.actionArg = n.friendlyName;
            netRows.push_back(nameRow);
            wchar_t ipBuf[256] = {};
            swprintf_s(ipBuf, L"IPv4: %s   \x2022   Brama: %s   \x2022   DNS: %s",
                       n.ipv4.empty() ? L"-" : n.ipv4.c_str(),
                       n.gateway.empty() ? L"-" : n.gateway.c_str(),
                       n.dnsServers.empty() ? L"-" : n.dnsServers.c_str());
            netRows.push_back({ L"   Adresacja IP / DNS", ipBuf, 0x0067B8 });
            wchar_t trBuf[256] = {};
            swprintf_s(trBuf, L"MAC: %s  \x2022  Link: %llu Mbps  \x2022  RX: %s (%.1f KB/s)  \x2022  TX: %s (%.1f KB/s)",
                       n.macAddress.c_str(),
                       static_cast<unsigned long long>(n.txLinkSpeedBps / 1000000ULL),
                       FileEngine::FormatSizeDetailed(n.totalRxBytes).c_str(), n.liveRxBytesPerSec / 1024.0,
                       FileEngine::FormatSizeDetailed(n.totalTxBytes).c_str(), n.liveTxBytesPerSec / 1024.0);
            netRows.push_back({ L"   Statystyki pakietów", trBuf, 0x475569 });
        }

        std::vector<KvRow> sysRows;
        sysRows.push_back({ L"System operacyjny", sys.osName + L" (" + sys.osBuildStr + L")", 0x0067B8 });
        sysRows.push_back({ L"Czas pracy systemu (Uptime)", sys.uptimeStr, 0x059669 });
        sysRows.push_back({ L"Komputer / Użytkownik", sys.computerName + L"  /  " + sys.userName });
        sysRows.push_back({ L"Zasilanie / Bateria", sys.powerStatusStr });
        wchar_t cntBuf[128] = {};
        swprintf_s(cntBuf, L"Procesy: %u   \x2022   Wątki: %u   \x2022   Uchwyty (Handles): %u", sys.processCount, sys.threadCount, sys.handleCount);
        sysRows.push_back({ L"Obiekty jądra NT", cntBuf });

        if (!sys.audioOutputs.empty()) {
            std::wstring audStr;
            for (size_t i = 0; i < sys.audioOutputs.size(); ++i) {
                if (i > 0) audStr += L"  |  ";
                audStr += sys.audioOutputs[i];
            }
            sysRows.push_back({ L"Wyjścia Audio", audStr, 0xB45309 });
        }

        for (size_t i = 0; i < std::min<size_t>(6, sys.topProcesses.size()); ++i) {
            const auto& tp = sys.topProcesses[i];
            wchar_t pBuf[128] = {};
            swprintf_s(pBuf, L"%s   (PID: %u, Wątki: %u)", FileEngine::FormatSizeDetailed(tp.workingSetBytes).c_str(), tp.pid, tp.threads);
            sysRows.push_back({ L"Top RAM #" + std::to_wstring(i + 1) + L": " + tp.name, pBuf, 0x0F172A });
        }

        float hN = drawProCard(leftX,  curY, halfW, m_lang == Lang::PL ? L"KARTY SIECIOWE (IPv4 / IPv6 / MAC / RUCH LIVE)" : L"NETWORK INTERFACES (IPv4 / MAC / LIVE TRAFFIC)", 0xD97706, netRows);
        float hS = drawProCard(rightX, curY, halfW, m_lang == Lang::PL ? L"SYSTEM WINDOWS, AUDIO & TOP PROCESY RAM" : L"WINDOWS OS, AUDIO & TOP PROCESSES BY RAM", 0xDB2777, sysRows);
        curY += std::max(hN, hS) + 16.0f;
    }

    float totalHeight = (curY + m_hwScrollY) - startY;
    float viewH = std::max(1.0f, workR.bottom - workR.top);
    m_hwMaxScrollY = std::max(0.0f, totalHeight - viewH + 20.0f);

    m_rt->PopAxisAlignedClip();
    m_rt->PopAxisAlignedClip();
}

} // namespace Explorer2
