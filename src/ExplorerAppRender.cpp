#include "ExplorerApp.h"
#include "CommandIds.h"
#include "PathUtils.h"
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
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

        D2D1_RECT_F mainRect = GetMainViewRect();
        DrawMainContent(mainRect);

        if (m_showPreviewPane && m_viewMode != ViewMode::FullViewer) {
            DrawPreviewPane(D2D1::RectF(mainRect.right, bodyTop, w, bodyBottom));
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
        // Classic Windows 10 "Usuń" big X
        m_solidBrush->SetColor(HexColor(0x788596));
        m_rt->DrawLine(D2D1::Point2F(cx - sz * 0.75f, cy - sz * 0.75f), D2D1::Point2F(cx + sz * 0.75f, cy + sz * 0.75f), m_solidBrush, 3.4f);
        m_rt->DrawLine(D2D1::Point2F(cx + sz * 0.75f, cy - sz * 0.75f), D2D1::Point2F(cx - sz * 0.75f, cy + sz * 0.75f), m_solidBrush, 3.4f);
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
        { CMD_TAB_HARDWARE, RibbonTab::Hardware, s.tabHardware, 76.0f,  false }
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
        // WIDOK (View) Ribbon Tab: Panes + Layout (Jumbo 440px, ExtraLarge 256px, Large, Medium, Small, List, Details, FullViewer, FullScreen) + Show/Hide
        float gx = rect.left + 6.0f;
        drawLargeButton(CMD_TOGGLE_NAVPANE, gx, 66.0f, s.btnNavPane, m_showNavPane);
        drawLargeButton(CMD_TOGGLE_PREVIEWPANE, gx + 68.0f, 66.0f, s.btnPreviewPane, m_showPreviewPane);
        drawGroupSeparatorAndTitle(gx, gx + 138.0f, s.grpPanes);

        float lx = gx + 144.0f;
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
        drawSmallRowButton(CMD_TOGGLE_EXT,    sx, body.top + 14.0f, 175.0f, s.chkFileExt,    m_showExtensions);
        drawSmallRowButton(CMD_TOGGLE_HIDDEN, sx, body.top + 40.0f, 175.0f, s.chkHiddenItems, m_showHidden);
        drawGroupSeparatorAndTitle(sx, sx + 182.0f, s.grpShowHide);
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
        drawPill(CMD_HW_SUB_SYSTEM, hx + 96.0f,  body.top + 53.0f, 186.0f, m_lang == Lang::PL ? L"System, Audio, Bateria i Procesy" : L"OS, Audio, Battery & Procs", m_hwSubTab == HardwareSubTab::System);
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

    // Split button "Usuń ▾": top triggers instant Recycle delete, bottom triggers mode menu!
    D2D1_RECT_F delTopR = D2D1::RectF(x, body.top + 4.0f, x + 52.0f, body.top + 44.0f);
    D2D1_RECT_F delBotR = D2D1::RectF(x, body.top + 44.0f, x + 52.0f, body.bottom - 17.0f);
    m_ribbonHits.push_back({ CMD_DELETE_MAIN, delTopR });
    m_ribbonHits.push_back({ CMD_DELETE_DROPDOWN, delBotR });

    if (m_hoverRibbonBtn == CMD_DELETE_MAIN || m_hoverRibbonBtn == CMD_DELETE_DROPDOWN) {
        D2D1_RECT_F fullDelR = D2D1::RectF(x, body.top + 4.0f, x + 52.0f, body.bottom - 17.0f);
        m_solidBrush->SetColor(HexColor(0xE1ECF4));
        m_rt->FillRectangle(fullDelR, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xA4C9E8));
        m_rt->DrawRectangle(fullDelR, m_solidBrush, 1.0f);
        m_rt->DrawLine(D2D1::Point2F(delBotR.left + 4.0f, delBotR.top), D2D1::Point2F(delBotR.right - 4.0f, delBotR.top), m_solidBrush, 1.0f);
    }
    DrawRibbonIcon(CMD_DELETE_MAIN, D2D1::RectF(x + 11.0f, body.top + 8.0f, x + 41.0f, body.top + 38.0f));
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
    drawSmallRowButton(CMD_NONE, x, body.top + 52.0f, 78.0f, s.btnHistory);
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

    // Search bar on the right ("Przeszukaj: Fooocus_Advanced") + Advanced Gear [⚙] button
    float gearW = 28.0f;
    float searchW = std::min(255.0f, std::max(150.0f, (rect.right - rect.left) * 0.21f));
    D2D1_RECT_F gearBtnR = D2D1::RectF(rect.right - gearW - 6.0f, rect.top + 5.0f, rect.right - 6.0f, rect.bottom - 5.0f);
    m_searchBarRect = D2D1::RectF(gearBtnR.left - searchW - 4.0f, rect.top + 5.0f, gearBtnR.left - 4.0f, rect.bottom - 5.0f);

    // Address / Breadcrumb Bar between Up arrow and Refresh / Search bar
    float refreshBtnLeft = m_searchBarRect.left - 34.0f;
    m_addressBarRect = D2D1::RectF(102.0f, rect.top + 5.0f, refreshBtnLeft, rect.bottom - 5.0f);

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
            std::wstring rotTitle = item.displayName + (m_lang == Lang::PL ? L"   [\x21BB Obracanie...]" : L"   [\x21BB Rotating...]");
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

                // Background Image Rotation Overlay ("↻ Obracanie..." / "↻ Rotating...")!
                if (m_imageCache.IsPathRotating(item.normKey)) {
                    m_solidBrush->SetColor(HexColor(0x0F172A, 0.66f));
                    m_rt->FillRectangle(thumbBox, m_solidBrush);

                    float cy = (thumbBox.top + thumbBox.bottom) * 0.5f;
                    D2D1_RECT_F pillR = D2D1::RectF(thumbBox.left + 8.0f, cy - 14.0f, thumbBox.right - 8.0f, cy + 14.0f);
                    m_solidBrush->SetColor(HexColor(0x0067B8, 0.94f));
                    m_rt->FillRoundedRectangle(D2D1::RoundedRect(pillR, 5.0f, 5.0f), m_solidBrush);
                    m_solidBrush->SetColor(HexColor(0xFFFFFF));
                    const wchar_t* rotTxt = (m_lang == Lang::PL) ? L"\x21BB Obracanie..." : L"\x21BB Rotating...";
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

} // namespace

void ExplorerApp::DrawProportionsView(const D2D1_RECT_F& rect) {
    m_treemapHits.clear();

    // Background studio slate
    m_solidBrush->SetColor(HexColor(0x16191F));
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

    // 1. Top Header & Legend Bar
    m_solidBrush->SetColor(HexColor(0x1E222A));
    m_rt->FillRectangle(topBarR, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0x2E3440));
    m_rt->DrawLine(D2D1::Point2F(topBarR.left, topBarR.bottom), D2D1::Point2F(topBarR.right, topBarR.bottom), m_solidBrush, 1.0f);

    std::wstring totalTxt = (m_lang == Lang::PL ? L"Proporcje (100% dokładny rozmiar)  \x2022  Razem: " : L"Exact Size Proportions  \x2022  Total: ") +
                            FileEngine::FormatSizeDetailed(totalBytes);

    D2D1_RECT_F hdrTxtR = D2D1::RectF(topBarR.left + 10.0f, topBarR.top, topBarR.left + 390.0f, topBarR.bottom);
    m_solidBrush->SetColor(HexColor(0xECEFF4));
    m_rt->DrawTextW(totalTxt.c_str(), static_cast<UINT32>(totalTxt.size()), m_fmtBold, hdrTxtR, m_solidBrush);

    // Rescan button
    D2D1_RECT_F rescanBtnR = D2D1::RectF(topBarR.left + 395.0f, topBarR.top + 4.0f, topBarR.left + 485.0f, topBarR.bottom - 4.0f);
    m_ribbonHits.push_back({ CMD_CALC_FOLDER_SIZES, rescanBtnR });
    m_solidBrush->SetColor(m_hoverRibbonBtn == CMD_CALC_FOLDER_SIZES ? HexColor(0x0078D7) : HexColor(0x2E3440));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(rescanBtnR, 3.0f, 3.0f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xECEFF4));
    const wchar_t* rescanLbl = (m_lang == Lang::PL) ? L"\x21BB Przelicz" : L"\x21BB Rescan";
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_rt->DrawTextW(rescanLbl, static_cast<UINT32>(wcslen(rescanLbl)), m_fmtSmall, rescanBtnR, m_solidBrush);
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // High-visibility live scanning banner right below topBarR
    if (scanning) {
        m_solidBrush->SetColor(HexColor(0x2B1F11));
        m_rt->FillRectangle(scanBannerR, m_solidBrush);

        // Animated diagonal hazard stripes in the background of the banner
        m_rt->PushAxisAlignedClip(scanBannerR, D2D1_ANTIALIAS_MODE_ALIASED);
        m_solidBrush->SetColor(HexColor(0xFFA000, 0.09f + 0.05f * pulse));
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
        m_solidBrush->SetColor(HexColor(0xFFCA28, 0.28f));
        m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(spinCx, spinCy), spinR, spinR), m_solidBrush, 2.2f);
        for (int s = 0; s < 5; ++s) {
            float ang = animPhase * 4.2f - s * 0.35f;
            float px1 = spinCx + std::cos(ang) * (spinR - 1.2f);
            float py1 = spinCy + std::sin(ang) * (spinR - 1.2f);
            float px2 = spinCx + std::cos(ang) * (spinR + 1.2f);
            float py2 = spinCy + std::sin(ang) * (spinR + 1.2f);
            m_solidBrush->SetColor(HexColor(0xFFD54F, 1.0f - s * 0.16f));
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
        m_solidBrush->SetColor(HexColor(0xFFE082));
        m_rt->DrawTextW(fullScanTxt.c_str(), static_cast<UINT32>(fullScanTxt.size()), m_fmtBold, scanTxtR, m_solidBrush);

        // Full-width progress track + animated shimmer bar at the bottom of scanBannerR
        D2D1_RECT_F progTrackR = D2D1::RectF(scanBannerR.left + 10.0f, scanBannerR.bottom - 8.0f, scanBannerR.right - 10.0f, scanBannerR.bottom - 3.0f);
        m_solidBrush->SetColor(HexColor(0x16191F));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(progTrackR, 2.5f, 2.5f), m_solidBrush);

        float trackW = std::max(1.0f, progTrackR.right - progTrackR.left);
        float fillFrac = (totalFolders > 0)
            ? std::clamp(static_cast<float>(doneFolders) / static_cast<float>(totalFolders), 0.05f, 0.98f)
            : 0.35f;
        D2D1_RECT_F progFillR = D2D1::RectF(progTrackR.left, progTrackR.top, progTrackR.left + trackW * fillFrac, progTrackR.bottom);
        m_solidBrush->SetColor(HexColor(0xFFB300));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(progFillR, 2.5f, 2.5f), m_solidBrush);

        // Moving bright highlight wave across the entire progress track
        float wavePos = std::fmod(animPhase * 0.55f, 1.0f);
        float waveW = std::min(160.0f, trackW * 0.25f);
        float waveX = progTrackR.left + (trackW - waveW) * wavePos;
        D2D1_RECT_F waveR = D2D1::RectF(waveX, progTrackR.top, waveX + waveW, progTrackR.bottom);
        m_solidBrush->SetColor(HexColor(0xFFF8E1, 0.85f));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(waveR, 2.5f, 2.5f), m_solidBrush);

        m_rt->PopAxisAlignedClip();

        m_solidBrush->SetColor(HexColor(0xFFB300, 0.65f + 0.35f * pulse));
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
        m_solidBrush->SetColor(HexColor(0xD8DEE9));
        m_rt->DrawTextW(lbl, static_cast<UINT32>(wcslen(lbl)), m_fmtSmall, ltR, m_solidBrush);
        chipX -= (cw + 6.0f);
    }

    // 2. Main Nested Squarified Treemap Canvas (100% Exact Byte Proportions)
    m_rt->PushAxisAlignedClip(canvasR, D2D1_ANTIALIAS_MODE_ALIASED);

    if (m_items.empty()) {
        m_solidBrush->SetColor(HexColor(0x9BA3B0));
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
            m_solidBrush->SetColor(HexColor(0xFFA000, 0.16f + 0.10f * pulse));
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
                m_solidBrush->SetColor(HexColor(0x1E160B, 0.88f));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(pillR, 4.0f, 4.0f), m_solidBrush);
                m_solidBrush->SetColor(HexColor(0xFFB300, 0.90f));
                m_rt->DrawRoundedRectangle(D2D1::RoundedRect(pillR, 4.0f, 4.0f), m_solidBrush, 1.2f);

                const wchar_t* scanBadge = (m_lang == Lang::PL) ? L"\x23F3 SKANOWANIE..." : L"\x23F3 SCANNING...";
                m_solidBrush->SetColor(HexColor(0xFFE082));
                m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                m_rt->DrawTextW(scanBadge, static_cast<UINT32>(wcslen(scanBadge)), m_fmtSmall, pillR, m_solidBrush);
                m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
            m_rt->PopAxisAlignedClip();

            m_solidBrush->SetColor(HexColor(0xFFB300, 0.55f + 0.40f * pulse));
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
                m_solidBrush->SetColor(HexColor(0xFFFFFF, 0.12f));
                m_rt->FillRectangle(sheenR, m_solidBrush);

                // 3D top/left highlight bevel
                m_solidBrush->SetColor(HexColor(ScaleRgbBrightness(baseColor, 1.32f)));
                m_rt->DrawLine(D2D1::Point2F(inner.left, inner.top), D2D1::Point2F(inner.right, inner.top), m_solidBrush, 1.0f);
                m_rt->DrawLine(D2D1::Point2F(inner.left, inner.top), D2D1::Point2F(inner.left, inner.bottom), m_solidBrush, 1.0f);

                // 3D bottom/right shadow bevel
                m_solidBrush->SetColor(HexColor(ScaleRgbBrightness(baseColor, 0.62f)));
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
                m_solidBrush->SetColor(HexColor(0x000000, 0.65f));
                m_rt->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), m_fmtSmall, shR, m_solidBrush);
                m_solidBrush->SetColor(HexColor(0xFFFFFF, 0.96f));
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
                // Strictly proportional to true byte size (no artificial floor that inflates 1 KB files!)
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

                std::wstring childRel = relPrefix + L"\\" + c.name;
                if (!c.fullPath.empty()) {
                    TreemapHitNode hit;
                    hit.rect = cr;
                    hit.topItemIndex = topItemIdx;
                    hit.fullPath = c.fullPath;
                    hit.relPath = childRel;
                    hit.size = c.size;
                    hit.isDirectory = c.isDirectory;
                    hit.depth = depth;
                    m_treemapHits.push_back(hit);
                }

                if (!c.isDirectory) {
                    uint32_t col = c.fullPath.empty() ? 0x546E7A : GetTreemapCategoryColor(c.ext, false, depth);
                    drawLeafBlock(cr, col, c.name, c.size, false);
                    continue;
                }

                // It's a nested subfolder — check if we can subdivide further (up to depth 3)
                std::vector<TreemapChildNode> subChildren;
                bool hasSub = (depth < 3) && !c.normKey.empty() &&
                              m_fileEngine.GetFolderTreemapChildren(c.normKey, subChildren) &&
                              !subChildren.empty();
                if (!hasSub || cw < 28.0f || ch < 24.0f) {
                    drawLeafBlock(cr, GetTreemapCategoryColor(L"", true, depth), c.name, c.size, false);
                    continue;
                }

                D2D1_RECT_F subBox = D2D1::RectF(cr.left + 0.8f, cr.top + 0.8f, cr.right - 0.8f, cr.bottom - 0.8f);
                m_solidBrush->SetColor(HexColor(depth == 1 ? 0x1C2128 : 0x181C22));
                m_rt->FillRectangle(subBox, m_solidBrush);
                m_solidBrush->SetColor(HexColor(depth == 1 ? 0x88C0D0 : 0x81A1C1, 0.70f));
                m_rt->DrawRectangle(subBox, m_solidBrush, 1.0f);

                float hdrH = (ch >= 36.0f) ? 14.0f : 0.0f;
                if (hdrH > 0.0f) {
                    D2D1_RECT_F shdrR = D2D1::RectF(subBox.left, subBox.top, subBox.right, subBox.top + hdrH);
                    m_solidBrush->SetColor(HexColor(depth == 1 ? 0x28303B : 0x232A34));
                    m_rt->FillRectangle(shdrR, m_solidBrush);

                    m_rt->PushAxisAlignedClip(shdrR, D2D1_ANTIALIAS_MODE_ALIASED);
                    std::wstring subHdr = c.name;
                    if (c.size > 0 && cw >= 90.0f) {
                        subHdr += L" (" + FileEngine::FormatSizeDetailed(c.size) + L")";
                    }
                    D2D1_RECT_F stxtR = D2D1::RectF(shdrR.left + 3.0f, shdrR.top, shdrR.right - 2.0f, shdrR.bottom);
                    m_solidBrush->SetColor(HexColor(0xBCE4F5));
                    m_rt->DrawTextW(subHdr.c_str(), static_cast<UINT32>(subHdr.size()), m_fmtSmall, stxtR, m_solidBrush);
                    m_rt->PopAxisAlignedClip();
                }

                D2D1_RECT_F subInner = D2D1::RectF(subBox.left + 1.2f, subBox.top + hdrH + 1.0f, subBox.right - 1.2f, subBox.bottom - 1.2f);
                drawSubtreeChildren(subInner, subChildren, childRel, topItemIdx, depth + 1);
            }
        };

        // Build weighted list of top-level items using TRUE byte weights
        // Only folders that are still queued at 0 bytes get a temporary placeholder weight while scanning
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
                w = 1.0; // 0-byte file or empty folder
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

            // Register level-0 hit node first (so deeper children pushed later win reverse hit-test)
            TreemapHitNode hit0;
            hit0.rect = r0;
            hit0.topItemIndex = itemIdx;
            hit0.fullPath = item.fullPath;
            hit0.relPath = item.name;
            hit0.size = item.size;
            hit0.isDirectory = item.isDirectory;
            hit0.depth = 0;
            m_treemapHits.push_back(hit0);

            if (!item.isDirectory) {
                uint32_t col = GetTreemapCategoryColor(item.ext, false, 0);
                drawLeafBlock(r0, col, item.displayName, item.size, false);
                continue;
            }

            bool folderInFlight = m_fileEngine.IsFolderInFlight(item.normKey) || item.sizeStr.empty() || item.sizeStr == L"...";
            bool hasChildren1 = m_fileEngine.GetFolderTreemapChildren(item.normKey, children1) && !children1.empty();
            if (!hasChildren1 || w0 < 24.0f || h0 < 22.0f) {
                uint32_t col = folderInFlight ? 0x2B3846 : 0x37474F;
                drawLeafBlock(r0, col, L"\xD83D\xDCC1 " + item.displayName, item.size, folderInFlight);
                continue;
            }

            // Draw Level-0 Folder Outer Container Frame + Header Strip
            D2D1_RECT_F outerBox = D2D1::RectF(r0.left + 1.0f, r0.top + 1.0f, r0.right - 1.0f, r0.bottom - 1.0f);
            m_solidBrush->SetColor(HexColor(0x222831));
            m_rt->FillRectangle(outerBox, m_solidBrush);
            m_solidBrush->SetColor(folderInFlight ? HexColor(0xFFB300, 0.70f + 0.30f * pulse) : HexColor(0xF6C65B, 0.80f));
            m_rt->DrawRectangle(outerBox, m_solidBrush, folderInFlight ? 1.8f : 1.2f);

            float hdrH0 = (h0 >= 38.0f) ? 16.0f : 0.0f;
            if (hdrH0 > 0.0f) {
                D2D1_RECT_F hdrR = D2D1::RectF(outerBox.left, outerBox.top, outerBox.right, outerBox.top + hdrH0);
                m_solidBrush->SetColor(folderInFlight ? HexColor(0x3E2C14) : HexColor(0x2D3542));
                m_rt->FillRectangle(hdrR, m_solidBrush);

                m_rt->PushAxisAlignedClip(hdrR, D2D1_ANTIALIAS_MODE_ALIASED);
                std::wstring fHdr = item.displayName;
                if (item.size > 0 && w0 >= 95.0f) {
                    fHdr += L"  (" + FileEngine::FormatSizeDetailed(item.size) + L")";
                }
                if (folderInFlight && w0 >= 140.0f) {
                    fHdr += (m_lang == Lang::PL ? L"  [\x23F3 Skanowanie...]" : L"  [\x23F3 Scanning...]");
                }
                D2D1_RECT_F fTxtR = D2D1::RectF(hdrR.left + 4.0f, hdrR.top, hdrR.right - 3.0f, hdrR.bottom);
                m_solidBrush->SetColor(HexColor(folderInFlight ? 0xFFE082 : 0xFFD97D));
                m_rt->DrawTextW(fHdr.c_str(), static_cast<UINT32>(fHdr.size()), m_fmtSmall, fTxtR, m_solidBrush);
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
                m_solidBrush->SetColor(HexColor(0x38A1F3, 0.92f));
                m_rt->DrawRectangle(hn.rect, m_solidBrush, 2.2f);
            }
        }

        // Highlight currently hovered nested or top-level block
        if (m_hoverTreemapHitIndex >= 0 && m_hoverTreemapHitIndex < static_cast<int>(m_treemapHits.size())) {
            const TreemapHitNode& hov = m_treemapHits[m_hoverTreemapHitIndex];
            m_solidBrush->SetColor(HexColor(0xFFFFFF, 0.14f));
            m_rt->FillRectangle(hov.rect, m_solidBrush);
            m_solidBrush->SetColor(HexColor(0xFFFFFF, 0.96f));
            m_rt->DrawRectangle(hov.rect, m_solidBrush, 2.0f);
        }
    }

    m_rt->PopAxisAlignedClip();

    // 3. Bottom Inspector Bar (shows hovered nested block path, exact size, % share, and action hint)
    m_solidBrush->SetColor(HexColor(0x1E222A));
    m_rt->FillRectangle(botBarR, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0x2E3440));
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
    m_solidBrush->SetColor(HexColor(0xECEFF4));
    m_rt->DrawTextW(inspectTxt.c_str(), static_cast<UINT32>(inspectTxt.size()), m_fmtSmall, inspR, m_solidBrush);
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
    m_solidBrush->SetColor(HexColor(0xFAFAFA));
    m_rt->FillRectangle(rect, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xE0E0E0));
    m_rt->DrawLine(D2D1::Point2F(rect.left, rect.top), D2D1::Point2F(rect.left, rect.bottom), m_solidBrush, 1.0f);

    if (m_focusedIndex < 0 || m_focusedIndex >= static_cast<int>(m_items.size())) return;
    const FileEntry& item = m_items[m_focusedIndex];
    if (!item.isImage && !item.isVideo && !item.isAudio) return;

    D2D1_RECT_F imgArea = D2D1::RectF(rect.left + 12.0f, rect.top + 12.0f, rect.right - 12.0f, rect.bottom - 90.0f);

    if ((item.isVideo || item.isAudio) && m_mediaPlayer.HasMedia()) {
        if (item.isVideo && m_mediaPlayer.HasVideo()) {
            uint32_t vw = 0, vh = 0;
            ID2D1Bitmap* vbmp = m_mediaPlayer.GetCurrentVideoFrame(m_rt, vw, vh);
            if (!vbmp) vbmp = m_imageCache.GetOrQueueThumbnail(m_rt, item.fullPath);
            if (vbmp) {
                D2D1_SIZE_F sz = vbmp->GetSize();
                D2D1_RECT_F dr = PathUtils::FitRectCentered(imgArea, sz.width, sz.height);
                m_rt->DrawBitmap(vbmp, dr);
            }
        } else {
            m_solidBrush->SetColor(HexColor(0x232731));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(imgArea, 6.0f, 6.0f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(0xECEFF4));
            std::wstring noteLbl = L"\x266B  " + item.name;
            m_rt->DrawTextW(noteLbl.c_str(), static_cast<UINT32>(noteLbl.size()), m_fmtCenterSmall, imgArea, m_solidBrush);
        }

        // Compact Play/Pause + Seek bar in Preview Pane
        D2D1_RECT_F playBtn = D2D1::RectF(rect.left + 14.0f, rect.bottom - 80.0f, rect.left + 42.0f, rect.bottom - 56.0f);
        m_ribbonHits.push_back({ CMD_MEDIA_PLAY_PAUSE, playBtn });
        m_solidBrush->SetColor(HexColor(0x0078D7));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(playBtn, 3.0f, 3.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xFFFFFF));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(m_mediaPlayer.IsPlaying() ? L"\x23F8" : L"\x25B6", 1, m_fmtSmall, playBtn, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        double pos = m_mediaPlayer.GetCurrentTimeSec();
        double dur = m_mediaPlayer.GetDurationSec();
        float frac = (dur > 0.05) ? static_cast<float>(std::clamp(pos / dur, 0.0, 1.0)) : 0.0f;
        m_mediaSeekBarRect = D2D1::RectF(playBtn.right + 10.0f, playBtn.top + 4.0f, rect.right - 14.0f, playBtn.bottom - 4.0f);
        float ty = (m_mediaSeekBarRect.top + m_mediaSeekBarRect.bottom) * 0.5f;
        m_solidBrush->SetColor(HexColor(0xCCCCCC));
        m_rt->FillRectangle(D2D1::RectF(m_mediaSeekBarRect.left, ty - 2.0f, m_mediaSeekBarRect.right, ty + 2.0f), m_solidBrush);
        float fx = m_mediaSeekBarRect.left + frac * std::max(0.0f, m_mediaSeekBarRect.right - m_mediaSeekBarRect.left);
        m_solidBrush->SetColor(HexColor(0x0078D7));
        m_rt->FillRectangle(D2D1::RectF(m_mediaSeekBarRect.left, ty - 2.0f, fx, ty + 2.0f), m_solidBrush);
    } else if (item.isImage) {
        FullImageMeta meta = m_imageCache.GetOrQueueFullImage(m_rt, item.fullPath, L"", L"");
        ID2D1Bitmap* bmp = meta.bitmap ? meta.bitmap : m_imageCache.GetOrQueueThumbnail(m_rt, item.fullPath);
        if (bmp) {
            D2D1_SIZE_F sz = bmp->GetSize();
            D2D1_RECT_F dr = PathUtils::FitRectCentered(imgArea, sz.width, sz.height);
            m_rt->DrawBitmap(bmp, dr);
        }
    }

    D2D1_RECT_F metaR = D2D1::RectF(rect.left + 14.0f, rect.bottom - 48.0f, rect.right - 14.0f, rect.bottom - 8.0f);
    std::wstring info = item.name + L"  (" + item.sizeStr + L")";
    m_solidBrush->SetColor(HexColor(0x333333));
    m_rt->DrawTextW(info.c_str(), static_cast<UINT32>(info.size()), m_fmtRegular, metaR, m_solidBrush);
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

    // Append active status toast (e.g. "Przeniesiono do Kosza (Ctrl+Z aby cofnąć)")
    if (!m_statusToast.empty() && GetTickCount64() < m_statusToastExpireTick) {
        statusText += L"     \x2022     " + m_statusToast;
    }

    D2D1_RECT_F textR = D2D1::RectF(rect.left + 10.0f, rect.top, rect.right - 90.0f, rect.bottom);
    m_solidBrush->SetColor(HexColor(0x333333));
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
        } else {
            // Full Viewer icon (maximised photo frame with center dot)
            m_rt->DrawRectangle(D2D1::RectF(br.left + 3.0f, br.top + 4.0f, br.right - 3.0f, br.bottom - 4.0f), m_solidBrush, 1.4f);
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F((br.left + br.right) * 0.5f, (br.top + br.bottom) * 0.5f), 2.5f, 2.5f), m_solidBrush);
        }
    };

    bool isThumbs = (m_viewMode == ViewMode::MediumIcons || m_viewMode == ViewMode::LargeIcons ||
                     m_viewMode == ViewMode::ExtraLargeIcons || m_viewMode == ViewMode::JumboIcons);
    drawBottomViewBtn(CMD_VIEW_DETAILS,     100.0f, m_viewMode == ViewMode::Details,     0);
    drawBottomViewBtn(CMD_VIEW_EXTRALARGE,   76.0f, isThumbs,                            1);
    drawBottomViewBtn(CMD_VIEW_PROPORTIONS,  52.0f, m_viewMode == ViewMode::Proportions, 3);
    drawBottomViewBtn(CMD_VIEW_PIECHART,     28.0f, m_viewMode == ViewMode::PieChart,    4);
    drawBottomViewBtn(CMD_VIEW_FULLVIEWER,    4.0f, m_viewMode == ViewMode::FullViewer,  2);
}

void ExplorerApp::DrawPieChartView(const D2D1_RECT_F& rect) {
    m_pieHits.clear();
    m_treemapHits.clear();

    m_rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Dark slate background matching Proportions view
    m_solidBrush->SetColor(HexColor(0x161B24));
    m_rt->FillRectangle(rect, m_solidBrush);

    if (m_items.empty()) {
        m_solidBrush->SetColor(HexColor(0x9BA3B0));
        m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        D2D1_RECT_F emptyR = D2D1::RectF(rect.left, (rect.top + rect.bottom) * 0.5f - 15.0f, rect.right, (rect.top + rect.bottom) * 0.5f + 15.0f);
        const wchar_t* msg = (m_lang == Lang::PL) ? L"Ten folder jest pusty" : L"This folder is empty";
        m_rt->DrawTextW(msg, static_cast<UINT32>(wcslen(msg)), m_fmtRegular, emptyR, m_solidBrush);
        m_fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_rt->PopAxisAlignedClip();
        return;
    }

    auto getExtColor = [](const std::wstring& path, bool isDir, bool isDrive) -> UINT32 {
        if (isDrive) return 0x00897B;
        if (isDir)   return 0x2B6CB0;
        std::wstring ext = PathUtils::GetExtensionLower(path);
        if (ext == L".mp4" || ext == L".mkv" || ext == L".avi" || ext == L".mov" || ext == L".webm" || ext == L".wmv") return 0x7E57C2;
        if (ext == L".jpg" || ext == L".jpeg" || ext == L".png" || ext == L".webp" || ext == L".gif" || ext == L".bmp" || ext == L".tga" || ext == L".psd") return 0x26A69A;
        if (ext == L".zip" || ext == L".rar" || ext == L".7z" || ext == L".tar" || ext == L".gz" || ext == L".iso" || ext == L".vmdk" || ext == L".ova" || ext == L".vhd") return 0xEF6C00;
        if (ext == L".exe" || ext == L".dll" || ext == L".msi" || ext == L".sys" || ext == L".bat") return 0xD32F2F;
        if (ext == L".cpp" || ext == L".h" || ext == L".cs" || ext == L".py" || ext == L".js" || ext == L".ts" || ext == L".json" || ext == L".xml" || ext == L".html" || ext == L".css") return 0x1976D2;
        if (ext == L".mp3" || ext == L".wav" || ext == L".flac" || ext == L".ogg" || ext == L".m4a") return 0xEC407A;
        if (ext == L".pdf" || ext == L".doc" || ext == L".docx" || ext == L".xls" || ext == L".xlsx" || ext == L".ppt" || ext == L".pptx" || ext == L".txt" || ext == L".md") return 0x43A047;
        if (ext == L".safetensors" || ext == L".ckpt" || ext == L".pth" || ext == L".onnx" || ext == L".bin" || ext == L".pak" || ext == L".assets" || ext == L".fbx" || ext == L".obj" || ext == L".blend") return 0xAB47BC;
        return 0x546E7A;
    };

    // Palette for distinct top-level folders so neighboring pie slices stand out clearly
    static const UINT32 kFolderPalette[] = {
        0x1E88E5, 0x00ACC1, 0x43A047, 0xFB8C00, 0x8E24AA, 0xE53935,
        0x3949AB, 0x00897B, 0x7CB342, 0xF4511E, 0x5E35B1, 0x039BE5
    };

    struct SortItem {
        int index;
        uint64_t bytes;
        bool isPending;
        UINT32 baseColor;
    };
    std::vector<SortItem> sorted;
    sorted.reserve(m_items.size());

    uint64_t totalBytes = 0;
    bool anyPending = false;
    int totalFolders = 0;
    int doneFolders = 0;
    std::wstring currentCalcName;

    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        const auto& it = m_items[i];
        bool pend = (it.isDirectory && !it.isDrive && !it.folderSizeReady);
        if (it.isDirectory && !it.isDrive) {
            totalFolders++;
            if (it.folderSizeReady) {
                doneFolders++;
            } else if (it.folderSizeCalculating && currentCalcName.empty()) {
                currentCalcName = it.name;
            }
        }
        if (pend) anyPending = true;
        uint64_t b = it.sizeBytes;
        if (it.isDrive && it.driveTotalBytes > 0) {
            b = (it.driveTotalBytes > it.driveFreeBytes) ? (it.driveTotalBytes - it.driveFreeBytes) : it.driveTotalBytes;
        }
        totalBytes += b;
        UINT32 col = it.isDirectory ? kFolderPalette[i % 12] : getExtColor(it.fullPath, it.isDirectory, it.isDrive);
        sorted.push_back({ i, b, pend, col });
    }

    std::sort(sorted.begin(), sorted.end(), [](const SortItem& a, const SortItem& b) {
        if (a.bytes != b.bytes) return a.bytes > b.bytes;
        return a.index < b.index;
    });

    // 1. Top Header Bar (26px)
    D2D1_RECT_F hdrR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + 26.0f);
    m_solidBrush->SetColor(HexColor(0x0E1218));
    m_rt->FillRectangle(hdrR, m_solidBrush);

    std::wstring hdrTitle = (m_lang == Lang::PL ? L"Wykres kołowy (Sunburst 360\x00B0)  \x2022  Razem: " : L"Radial Pie Chart (Sunburst 360\x00B0)  \x2022  Total: ")
                            + FileEngine::FormatSizeShort(totalBytes);
    D2D1_RECT_F hdrTxtR = D2D1::RectF(hdrR.left + 10.0f, hdrR.top, hdrR.left + 440.0f, hdrR.bottom);
    m_solidBrush->SetColor(HexColor(0xE2E8F0));
    m_rt->DrawTextW(hdrTitle.c_str(), static_cast<UINT32>(hdrTitle.size()), m_fmtBold, hdrTxtR, m_solidBrush);

    // [↻ Przelicz] button & [Widok: Proporcje] switch button
    D2D1_RECT_F recalcR = D2D1::RectF(hdrR.left + 380.0f, hdrR.top + 3.0f, hdrR.left + 462.0f, hdrR.bottom - 3.0f);
    m_ribbonHits.push_back({ CMD_RECALC_FOLDER_SIZES, recalcR });
    m_solidBrush->SetColor(m_hoverRibbonBtn == CMD_RECALC_FOLDER_SIZES ? HexColor(0x3B4A62) : HexColor(0x252F3F));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(recalcR, 3.0f, 3.0f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xE2E8F0));
    const wchar_t* recalcLbl = (m_lang == Lang::PL) ? L"\x21BB Przelicz" : L"\x21BB Rescan";
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_rt->DrawTextW(recalcLbl, static_cast<UINT32>(wcslen(recalcLbl)), m_fmtSmall, recalcR, m_solidBrush);

    D2D1_RECT_F propSwitchR = D2D1::RectF(hdrR.left + 468.0f, hdrR.top + 3.0f, hdrR.left + 572.0f, hdrR.bottom - 3.0f);
    m_ribbonHits.push_back({ CMD_VIEW_PROPORTIONS, propSwitchR });
    m_solidBrush->SetColor(m_hoverRibbonBtn == CMD_VIEW_PROPORTIONS ? HexColor(0x0078D7) : HexColor(0x252F3F));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(propSwitchR, 3.0f, 3.0f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0xE2E8F0));
    const wchar_t* propLbl = (m_lang == Lang::PL) ? L"\x25A6 Mapa Proporcje" : L"\x25A6 Treemap View";
    m_rt->DrawTextW(propLbl, static_cast<UINT32>(wcslen(propLbl)), m_fmtSmall, propSwitchR, m_solidBrush);
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    float canvasTop = hdrR.bottom + 2.0f;
    float animPhase = static_cast<float>(GetTickCount64() % 100000ULL) * 0.001f;

    // 2. Prominent Animated Scanning Progress Banner when calculating subfolders
    if (anyPending) {
        D2D1_RECT_F scanR = D2D1::RectF(rect.left + 4.0f, canvasTop, rect.right - 4.0f, canvasTop + 32.0f);
        canvasTop = scanR.bottom + 4.0f;

        m_solidBrush->SetColor(HexColor(0x2A1F0D));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(scanR, 4.0f, 4.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0xF59E0B));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(scanR, 4.0f, 4.0f), m_solidBrush, 1.2f);

        float pct = (totalFolders > 0) ? (static_cast<float>(doneFolders) / static_cast<float>(totalFolders)) : 0.0f;
        D2D1_RECT_F barBg = D2D1::RectF(scanR.left + 8.0f, scanR.bottom - 7.0f, scanR.right - 8.0f, scanR.bottom - 2.5f);
        m_solidBrush->SetColor(HexColor(0x453214));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(barBg, 2.0f, 2.0f), m_solidBrush);

        float fillW = std::max(24.0f, (barBg.right - barBg.left) * pct);
        D2D1_RECT_F barFg = D2D1::RectF(barBg.left, barBg.top, barBg.left + fillW, barBg.bottom);
        m_solidBrush->SetColor(HexColor(0xFBBF24));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(barFg, 2.0f, 2.0f), m_solidBrush);

        float spCx = scanR.left + 18.0f;
        float spCy = scanR.top + 13.0f;
        for (int s = 0; s < 8; ++s) {
            float a = animPhase * 7.0f + s * (3.14159265f * 0.25f);
            m_solidBrush->SetColor(HexColor(0xFBBF24, 0.2f + 0.8f * (s / 8.0f)));
            m_rt->DrawLine(D2D1::Point2F(spCx + std::cos(a) * 3.5f, spCy + std::sin(a) * 3.5f),
                           D2D1::Point2F(spCx + std::cos(a) * 7.5f, spCy + std::sin(a) * 7.5f), m_solidBrush, 1.8f);
        }

        wchar_t scanBuf[512] = {};
        int pctInt = static_cast<int>(pct * 100.0f + 0.5f);
        if (m_lang == Lang::PL) {
            swprintf_s(scanBuf, L"TRWA SKANOWANIE PODFOLDERÓW (%d / %d gotowe \x2022 %d%%)   \x2022   Dotychczas: %s%s%s",
                       doneFolders, totalFolders, pctInt,
                       FileEngine::FormatSizeShort(totalBytes).c_str(),
                       currentCalcName.empty() ? L"" : L"   \x2022   Teraz: ",
                       currentCalcName.c_str());
        } else {
            swprintf_s(scanBuf, L"SCANNING SUBFOLDERS (%d / %d done \x2022 %d%%)   \x2022   So far: %s%s%s",
                       doneFolders, totalFolders, pctInt,
                       FileEngine::FormatSizeShort(totalBytes).c_str(),
                       currentCalcName.empty() ? L"" : L"   \x2022   Now: ",
                       currentCalcName.c_str());
        }
        D2D1_RECT_F stR = D2D1::RectF(scanR.left + 32.0f, scanR.top + 2.0f, scanR.right - 10.0f, scanR.top + 22.0f);
        m_solidBrush->SetColor(HexColor(0xFDE68A));
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

    // 4 Concentric Rings:
    // Center Hub: 0 .. 0.25 * maxRadius
    // Ring 0 (Top-level items): 0.26 .. 0.52 * maxRadius
    // Ring 1 (Depth 1 children): 0.53 .. 0.73 * maxRadius
    // Ring 2 (Depth 2 children): 0.74 .. 0.89 * maxRadius
    // Ring 3 (Depth 3 children): 0.90 .. 1.00 * maxRadius
    float hubR = maxRadius * 0.25f;
    float ringInner[4] = { maxRadius * 0.265f, maxRadius * 0.535f, maxRadius * 0.740f, maxRadius * 0.895f };
    float ringOuter[4] = { maxRadius * 0.525f, maxRadius * 0.730f, maxRadius * 0.885f, maxRadius * 1.000f };

    // Helper to draw an annular pie sector using ID2D1PathGeometry
    auto drawAnnularSector = [&](float rIn, float rOut, float a0, float a1, UINT32 fillHex, float fillAlpha, UINT32 strokeHex, float strokeWidth) {
        if (!m_d2dFactory || a1 <= a0 + 0.001f || rOut <= rIn) return;
        float span = a1 - a0;
        if (span >= 6.282f) span = 6.282f;
        float endA = a0 + span;

        ID2D1PathGeometry* path = nullptr;
        if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(&path)) && path) {
            ID2D1GeometrySink* sink = nullptr;
            if (SUCCEEDED(path->Open(&sink)) && sink) {
                D2D1_POINT_2F pOut0 = D2D1::Point2F(cx + std::cos(a0) * rOut,   cy + std::sin(a0) * rOut);
                D2D1_POINT_2F pOut1 = D2D1::Point2F(cx + std::cos(endA) * rOut, cy + std::sin(endA) * rOut);
                D2D1_POINT_2F pIn1  = D2D1::Point2F(cx + std::cos(endA) * rIn,  cy + std::sin(endA) * rIn);
                D2D1_POINT_2F pIn0  = D2D1::Point2F(cx + std::cos(a0) * rIn,   cy + std::sin(a0) * rIn);

                D2D1_ARC_SIZE arcSz = (span > 3.14159265f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;

                sink->BeginFigure(pOut0, D2D1_FIGURE_BEGIN_FILLED);
                sink->AddArc(D2D1::ArcSegment(pOut1, D2D1::SizeF(rOut, rOut), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, arcSz));
                sink->AddLine(pIn1);
                sink->AddArc(D2D1::ArcSegment(pIn0, D2D1::SizeF(rIn, rIn), 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, arcSz));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                sink->Release();
            }
            m_solidBrush->SetColor(HexColor(fillHex, fillAlpha));
            m_rt->FillGeometry(path, m_solidBrush);
            if (strokeWidth > 0.0f) {
                m_solidBrush->SetColor(HexColor(strokeHex, 0.9f));
                m_rt->DrawGeometry(path, m_solidBrush, strokeWidth);
            }
            path->Release();
        }
    };

    // Subtle background guide rings
    for (int r = 0; r < 4; ++r) {
        m_solidBrush->SetColor(HexColor(0x1E2634, 0.55f));
        m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), ringOuter[r], ringOuter[r]), m_solidBrush, 1.0f);
    }

    // Recursive helper for rings 1..3
    std::function<void(const std::vector<FolderTreeSizeNode>&, int, float, float, uint64_t, int, UINT32)> renderChildRing;
    renderChildRing = [&](const std::vector<FolderTreeSizeNode>& nodes, int parentItemIdx, float startA, float endA, uint64_t parentBytes, int depth, UINT32 parentColor) {
        if (depth > 3 || nodes.empty() || parentBytes == 0 || endA <= startA + 0.015f) return;
        float rIn  = ringInner[depth];
        float rOut = ringOuter[depth];
        float curA = startA;
        float totalSpan = endA - startA;

        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto& nd = nodes[i];
            if (nd.sizeBytes == 0) continue;
            float frac = static_cast<float>(static_cast<double>(nd.sizeBytes) / static_cast<double>(parentBytes));
            float span = totalSpan * frac;
            if (span < 0.008f) continue; // skip sub-pixel slivers in outer rings

            float nextA = std::min(endA, curA + span);
            UINT32 col = nd.isDirectory ? parentColor : getExtColor(nd.fullPath, nd.isDirectory, false);
            float alpha = 0.88f - depth * 0.10f;

            drawAnnularSector(rIn, rOut, curA, nextA, col, alpha, 0x161B24, 1.0f);

            PieSectorHitNode hit;
            hit.cx = cx;
            hit.cy = cy;
            hit.innerR = rIn;
            hit.outerR = rOut;
            hit.startAngle = curA;
            hit.endAngle = nextA;
            hit.itemIndex = parentItemIdx;
            hit.fullPath = nd.fullPath;
            hit.name = nd.name;
            hit.sizeBytes = nd.sizeBytes;
            hit.isDirectory = nd.isDirectory;
            hit.depth = depth;
            hit.colorHex = col;
            m_pieHits.push_back(std::move(hit));

            // Label on outer ring sector if wide enough
            if (span > 0.24f && (rOut - rIn) >= 18.0f) {
                float midA = (curA + nextA) * 0.5f;
                float midR = (rIn + rOut) * 0.5f;
                float lx = cx + std::cos(midA) * midR;
                float ly = cy + std::sin(midA) * midR;
                D2D1_RECT_F lblR = D2D1::RectF(lx - 42.0f, ly - 9.0f, lx + 42.0f, ly + 9.0f);
                m_solidBrush->SetColor(HexColor(0x0F141C, 0.72f));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(lblR, 2.5f, 2.5f), m_solidBrush);
                m_solidBrush->SetColor(HexColor(0xF1F5F9));
                m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                m_rt->DrawTextW(nd.name.c_str(), static_cast<UINT32>(nd.name.size()), m_fmtSmall, lblR, m_solidBrush);
                m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }

            if (nd.isDirectory && !nd.children.empty() && depth < 3) {
                renderChildRing(nd.children, parentItemIdx, curA, nextA, nd.sizeBytes, depth + 1, col);
            }
            curA = nextA;
        }
    };

    // Render Ring 0 (Top-level items) with 100% true angular proportions
    float curAngle = -1.5707963f; // Start at 12 o'clock (-PI/2)
    float fullCircle = 6.2831853f;
    uint64_t denomBytes = std::max<uint64_t>(1, totalBytes);

    uint64_t smallAggBytes = 0;
    int smallAggCount = 0;
    int firstSmallIdx = -1;

    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& si = sorted[i];
        const auto& item = m_items[si.index];
        double frac = static_cast<double>(si.bytes) / static_cast<double>(denomBytes);
        float span = static_cast<float>(frac * fullCircle);

        if (span < 0.012f && i > 0 && (sorted.size() - i) > 1) {
            // Group very tiny slices (< 0.7 deg) into a single clean Remainder slice
            smallAggBytes += si.bytes;
            smallAggCount++;
            if (firstSmallIdx < 0) firstSmallIdx = si.index;
            continue;
        }

        if (si.bytes == 0 && !si.isPending) continue;
        if (span < 0.008f) span = 0.008f;
        float nextAngle = std::min(-1.5707963f + fullCircle, curAngle + span);

        bool isSel = item.selected;
        bool isHov = (m_hoverIndex == si.index);
        float rIn  = ringInner[0];
        float rOut = ringOuter[0] + (isHov ? 4.0f : 0.0f);

        drawAnnularSector(rIn, rOut, curAngle, nextAngle, si.baseColor, isHov ? 1.0f : 0.92f,
                          isSel ? 0xFFFFFF : (isHov ? 0x7DD3FC : 0x161B24),
                          isSel ? 2.2f : (isHov ? 1.8f : 1.2f));

        PieSectorHitNode hit;
        hit.cx = cx;
        hit.cy = cy;
        hit.innerR = rIn;
        hit.outerR = rOut;
        hit.startAngle = curAngle;
        hit.endAngle = nextAngle;
        hit.itemIndex = si.index;
        hit.fullPath = item.fullPath;
        hit.name = item.name;
        hit.sizeBytes = si.bytes;
        hit.isDirectory = item.isDirectory;
        hit.depth = 0;
        hit.colorHex = si.baseColor;
        m_pieHits.push_back(std::move(hit));

        // Draw label on Ring 0 sector if wide enough
        if (span > 0.16f) {
            float midA = (curAngle + nextAngle) * 0.5f;
            float midR = (rIn + rOut) * 0.5f;
            float lx = cx + std::cos(midA) * midR;
            float ly = cy + std::sin(midA) * midR;
            wchar_t pctBuf[32] = {};
            swprintf_s(pctBuf, L"%.1f%%", frac * 100.0);
            std::wstring lbl = item.name + L"\n" + pctBuf;
            D2D1_RECT_F lblR = D2D1::RectF(lx - 48.0f, ly - 15.0f, lx + 48.0f, ly + 15.0f);
            m_solidBrush->SetColor(HexColor(0x0F141C, 0.78f));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(lblR, 3.0f, 3.0f), m_solidBrush);
            m_solidBrush->SetColor(HexColor(0xFFFFFF));
            m_rt->DrawTextW(lbl.c_str(), static_cast<UINT32>(lbl.size()), m_fmtCenterSmall, lblR, m_solidBrush);
        }

        // Render recursive outer rings (Depth 1..3)
        if (item.isDirectory && item.subTreeReady && !item.subTreeChildren.empty()) {
            renderChildRing(item.subTreeChildren, si.index, curAngle, nextAngle, si.bytes, 1, si.baseColor);
        }

        curAngle = nextAngle;
    }

    // Draw aggregated tiny items sector if any
    if (smallAggCount > 0 && curAngle < -1.5707963f + fullCircle) {
        float nextAngle = -1.5707963f + fullCircle;
        drawAnnularSector(ringInner[0], ringOuter[0], curAngle, nextAngle, 0x475569, 0.85f, 0x161B24, 1.0f);
        PieSectorHitNode hit;
        hit.cx = cx;
        hit.cy = cy;
        hit.innerR = ringInner[0];
        hit.outerR = ringOuter[0];
        hit.startAngle = curAngle;
        hit.endAngle = nextAngle;
        hit.itemIndex = firstSmallIdx;
        hit.name = L"[+" + std::to_wstring(smallAggCount) + (m_lang == Lang::PL ? L" mniejszych]" : L" smaller]");
        hit.sizeBytes = smallAggBytes;
        hit.isDirectory = false;
        hit.depth = 0;
        hit.colorHex = 0x475569;
        m_pieHits.push_back(std::move(hit));
    }

    // Interactive Center Hub Circle ("Click center to go Up" + Hovered item summary)
    bool hubHover = false;
    {
        float dx = static_cast<float>(m_mousePt.x) - cx;
        float dy = static_cast<float>(m_mousePt.y) - cy;
        if (std::sqrt(dx * dx + dy * dy) <= hubR) hubHover = true;
    }
    m_solidBrush->SetColor(hubHover ? HexColor(0x1E293B) : HexColor(0x0F141C));
    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), hubR, hubR), m_solidBrush);
    m_solidBrush->SetColor(hubHover ? HexColor(0x38BDF8) : HexColor(0x334155));
    m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), hubR, hubR), m_solidBrush, hubHover ? 2.2f : 1.5f);

    // Register Center Hub in m_pieHits with itemIndex = -2 (Navigate Up on click!)
    {
        PieSectorHitNode hubHit;
        hubHit.cx = cx;
        hubHit.cy = cy;
        hubHit.innerR = 0.0f;
        hubHit.outerR = hubR;
        hubHit.startAngle = -3.14159265f;
        hubHit.endAngle = 3.14159265f;
        hubHit.itemIndex = -2;
        hubHit.name = m_lang == Lang::PL ? L"W górę (Folder nadrzędny)" : L"Go Up (Parent folder)";
        hubHit.sizeBytes = totalBytes;
        m_pieHits.push_back(std::move(hubHit));
    }

    std::wstring centerTitle = PathUtils::GetFileName(m_currentPath);
    if (centerTitle.empty()) centerTitle = m_currentPath;
    std::wstring centerSize = FileEngine::FormatSizeShort(totalBytes);
    std::wstring centerSub  = m_lang == Lang::PL ? L"\x2191 Klik = W górę" : L"\x2191 Click = Up";

    if (m_hoverIndex >= 0 && m_hoverIndex < static_cast<int>(m_items.size())) {
        const auto& hov = m_items[m_hoverIndex];
        centerTitle = hov.name;
        centerSize  = FileEngine::FormatSizeShort(hov.sizeBytes);
        double pct = (totalBytes > 0) ? (100.0 * static_cast<double>(hov.sizeBytes) / static_cast<double>(totalBytes)) : 0.0;
        wchar_t pBuf[64] = {};
        swprintf_s(pBuf, L"%.2f%% folderu", pct);
        centerSub = pBuf;
    }

    D2D1_RECT_F hubTitleR = D2D1::RectF(cx - hubR * 0.85f, cy - 28.0f, cx + hubR * 0.85f, cy - 6.0f);
    D2D1_RECT_F hubSizeR  = D2D1::RectF(cx - hubR * 0.85f, cy - 6.0f,  cx + hubR * 0.85f, cy + 14.0f);
    D2D1_RECT_F hubSubR   = D2D1::RectF(cx - hubR * 0.85f, cy + 14.0f, cx + hubR * 0.85f, cy + 32.0f);

    m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_solidBrush->SetColor(HexColor(0xF8FAFC));
    m_rt->DrawTextW(centerTitle.c_str(), static_cast<UINT32>(centerTitle.size()), m_fmtBold, hubTitleR, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0x38BDF8));
    m_rt->DrawTextW(centerSize.c_str(), static_cast<UINT32>(centerSize.size()), m_fmtBold, hubSizeR, m_solidBrush);
    m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_solidBrush->SetColor(HexColor(0x94A3B8));
    m_rt->DrawTextW(centerSub.c_str(), static_cast<UINT32>(centerSub.size()), m_fmtSmall, hubSubR, m_solidBrush);
    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // 3. Right-Side Ranked Breakdown & Legend Panel
    m_solidBrush->SetColor(HexColor(0x11161F));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(listArea, 5.0f, 5.0f), m_solidBrush);
    m_solidBrush->SetColor(HexColor(0x263042));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(listArea, 5.0f, 5.0f), m_solidBrush, 1.0f);

    D2D1_RECT_F listHdrR = D2D1::RectF(listArea.left + 10.0f, listArea.top + 6.0f, listArea.right - 10.0f, listArea.top + 28.0f);
    m_solidBrush->SetColor(HexColor(0x94A3B8));
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

        bool isSel = item.selected;
        bool isHov = (m_hoverIndex == si.index);
        if (isSel || isHov) {
            m_solidBrush->SetColor(isSel ? HexColor(0x1E3A5F) : HexColor(0x1C2636));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(rowR, 3.0f, 3.0f), m_solidBrush);
            m_solidBrush->SetColor(isSel ? HexColor(0x38BDF8) : HexColor(0x334155));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(rowR, 3.0f, 3.0f), m_solidBrush, 1.0f);
        }

        // Proportional background bar inside row
        double frac = (totalBytes > 0) ? (static_cast<double>(si.bytes) / static_cast<double>(totalBytes)) : 0.0;
        float barW = static_cast<float>((rowR.right - rowR.left) * frac);
        if (barW > 2.0f) {
            D2D1_RECT_F pBar = D2D1::RectF(rowR.left, rowR.bottom - 3.0f, rowR.left + barW, rowR.bottom);
            m_solidBrush->SetColor(HexColor(si.baseColor, 0.75f));
            m_rt->FillRectangle(pBar, m_solidBrush);
        }

        // Color swatch
        D2D1_RECT_F swR = D2D1::RectF(rowR.left + 6.0f, rowR.top + 5.0f, rowR.left + 18.0f, rowR.top + 17.0f);
        m_solidBrush->SetColor(HexColor(si.baseColor));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(swR, 2.5f, 2.5f), m_solidBrush);

        // Name
        std::wstring prefix = item.isDirectory ? L"\xD83D\xDCC1 " : L"";
        std::wstring dispName = prefix + item.name;
        D2D1_RECT_F nameR = D2D1::RectF(rowR.left + 24.0f, rowR.top + 2.0f, rowR.right - 135.0f, rowR.bottom - 2.0f);
        m_solidBrush->SetColor(HexColor(0xF1F5F9));
        m_rt->DrawTextW(dispName.c_str(), static_cast<UINT32>(dispName.size()), m_fmtSmall, nameR, m_solidBrush);

        // Size & %
        wchar_t statBuf[64] = {};
        if (si.isPending) {
            swprintf_s(statBuf, L"\x23F3 ...");
        } else {
            swprintf_s(statBuf, L"%s  (%.1f%%)", FileEngine::FormatSizeShort(si.bytes).c_str(), frac * 100.0);
        }
        D2D1_RECT_F statR = D2D1::RectF(rowR.right - 132.0f, rowR.top + 2.0f, rowR.right - 6.0f, rowR.bottom - 2.0f);
        m_solidBrush->SetColor(si.isPending ? HexColor(0xFBBF24) : HexColor(0x94A3B8));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        m_rt->DrawTextW(statBuf, static_cast<UINT32>(wcslen(statBuf)), m_fmtSmall, statR, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        // Register legend row for click/double-click hit-testing
        PieSectorHitNode rowHit;
        rowHit.legendRowRect = rowR;
        rowHit.itemIndex = si.index;
        rowHit.fullPath = item.fullPath;
        rowHit.name = item.name;
        rowHit.sizeBytes = si.bytes;
        rowHit.isDirectory = item.isDirectory;
        rowHit.depth = 0;
        rowHit.colorHex = si.baseColor;
        m_pieHits.push_back(std::move(rowHit));

        rowY += rowH;
    }

    // 4. Bottom Inspector Bar (22px)
    D2D1_RECT_F botR = D2D1::RectF(rect.left, rect.bottom - 22.0f, rect.right, rect.bottom);
    m_solidBrush->SetColor(HexColor(0x0E1218));
    m_rt->FillRectangle(botR, m_solidBrush);

    std::wstring botInfo = m_lang == Lang::PL
        ? L"Najedź na wycinek koła lub listę po prawej  \x2022  2\x00D7 Klik = Wejdź w folder / Otwórz plik  \x2022  Kliknij środek koła = W górę"
        : L"Hover any pie sector or list row  \x2022  Double-click = Enter folder / Open file  \x2022  Click center hub = Go Up";
    if (m_hoverIndex >= 0 && m_hoverIndex < static_cast<int>(m_items.size())) {
        const auto& hov = m_items[m_hoverIndex];
        double pct = (totalBytes > 0) ? (100.0 * static_cast<double>(hov.sizeBytes) / static_cast<double>(totalBytes)) : 0.0;
        wchar_t pBuf[64] = {};
        swprintf_s(pBuf, L" (%.2f%%)", pct);
        botInfo = hov.fullPath + L"   \x2022   " + FileEngine::FormatSizeDetailed(hov.sizeBytes) + pBuf;
    }
    D2D1_RECT_F botTxtR = D2D1::RectF(botR.left + 10.0f, botR.top + 2.0f, botR.right - 10.0f, botR.bottom);
    m_solidBrush->SetColor(HexColor(0xCBD5E1));
    m_rt->DrawTextW(botInfo.c_str(), static_cast<UINT32>(botInfo.size()), m_fmtSmall, botTxtR, m_solidBrush);

    m_rt->PopAxisAlignedClip();
}

void ExplorerApp::DrawHardwareProView(const D2D1_RECT_F& rect) {
    m_hwCardHits.clear();
    const auto& snap = m_hwInspector.Snapshot();

    m_rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Deep Pro Dark Workstation Theme (#0B0F17 background)
    m_solidBrush->SetColor(HexColor(0x0B0F17));
    m_rt->FillRectangle(rect, m_solidBrush);

    // 1. Top Workstation Control & Status Bar (36px)
    D2D1_RECT_F topBar = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + 36.0f);
    m_solidBrush->SetColor(HexColor(0x111827));
    m_rt->FillRectangle(topBar, m_solidBrush);
    m_solidBrush->SetColor(HexColor(0x1F2937));
    m_rt->DrawLine(D2D1::Point2F(topBar.left, topBar.bottom), D2D1::Point2F(topBar.right, topBar.bottom), m_solidBrush, 1.0f);

    // Live Pulse Indicator
    bool paused = m_hwInspector.IsPaused();
    m_solidBrush->SetColor(paused ? HexColor(0xF59E0B) : HexColor(0x10B981));
    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(topBar.left + 16.0f, topBar.top + 18.0f), 5.0f, 5.0f), m_solidBrush);

    std::wstring wsTitle = (m_lang == Lang::PL ? L"STACJA DIAGNOSTYCZNA SPRZĘTU PRO  \x2022  " : L"PRO HARDWARE & TELEMETRY WORKSTATION  \x2022  ")
                           + snap.os.computerName + L" (" + snap.board.boardManufacturer + L" " + snap.board.boardProduct + L")";
    D2D1_RECT_F titleR = D2D1::RectF(topBar.left + 28.0f, topBar.top, topBar.right - 390.0f, topBar.bottom);
    m_solidBrush->SetColor(HexColor(0xF8FAFC));
    m_rt->DrawTextW(wsTitle.c_str(), static_cast<UINT32>(wsTitle.size()), m_fmtBold, titleR, m_solidBrush);

    // Top-right quick action buttons inside the Hardware view
    auto drawTopActionBtn = [&](int cmdId, float rightOff, float w, const wchar_t* label, UINT32 bgHex) {
        D2D1_RECT_F br = D2D1::RectF(topBar.right - rightOff - w, topBar.top + 6.0f, topBar.right - rightOff, topBar.bottom - 6.0f);
        m_ribbonHits.push_back({ cmdId, br });
        m_solidBrush->SetColor(m_hoverRibbonBtn == cmdId ? HexColor(0x2563EB) : HexColor(bgHex));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.5f, 3.5f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x3B82F6, 0.6f));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(br, 3.5f, 3.5f), m_solidBrush, 1.0f);
        m_solidBrush->SetColor(HexColor(0xF8FAFC));
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), m_fmtSmall, br, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    };

    drawTopActionBtn(CMD_HW_BACK_TO_FILES, 8.0f,   110.0f, m_lang == Lang::PL ? L"\xD83D\xDCC1 Wróć do plików" : L"\xD83D\xDCC1 Back to Files", 0x1E293B);
    drawTopActionBtn(CMD_HW_SAVE_REPORT,   124.0f, 96.0f,  m_lang == Lang::PL ? L"\xD83D\xDCBE Zapisz .TXT" : L"\xD83D\xDCBE Save .TXT", 0x1E293B);
    drawTopActionBtn(CMD_HW_COPY_REPORT,   226.0f, 106.0f, m_lang == Lang::PL ? L"\xD83D\xDCCB Kopiuj raport" : L"\xD83D\xDCCB Copy Report", 0x1E293B);
    drawTopActionBtn(CMD_HW_BENCH_CPU,     338.0f, 96.0f,  m_lang == Lang::PL ? L"\x26A1 Test CPU/RAM" : L"\x26A1 Bench CPU", 0x1D4ED8);

    // 2. Sub-Tab Filter Strip (28px)
    D2D1_RECT_F subStrip = D2D1::RectF(rect.left, topBar.bottom, rect.right, topBar.bottom + 28.0f);
    m_solidBrush->SetColor(HexColor(0x0F172A));
    m_rt->FillRectangle(subStrip, m_solidBrush);

    struct SubDef { int cmd; HardwareSubTab tab; const wchar_t* pl; const wchar_t* en; float w; };
    SubDef subs[] = {
        { CMD_HW_SUB_ALL,     HardwareSubTab::Overview,          L"Wszystko (Dashboard)", L"All (Dashboard)", 136.0f },
        { CMD_HW_SUB_CPU,     HardwareSubTab::CpuCores,          L"CPU, Rdzenie i Cache", L"CPU, Cores & Cache", 136.0f },
        { CMD_HW_SUB_GPU,     HardwareSubTab::GpuDisplays,       L"GPU, VRAM i Monitory", L"GPU, VRAM & Displays", 142.0f },
        { CMD_HW_SUB_RAM,     HardwareSubTab::MemoryDimms,       L"RAM (Sloty DIMM SMBIOS)", L"RAM (SMBIOS DIMMs)", 156.0f },
        { CMD_HW_SUB_BOARD,   HardwareSubTab::MotherboardBios,   L"Płyta główna i UEFI/BIOS", L"Motherboard & BIOS", 156.0f },
        { CMD_HW_SUB_STORAGE, HardwareSubTab::StorageSmart,      L"Dyski fizyczne NVMe/SATA", L"Physical Disks NVMe/SATA", 156.0f },
        { CMD_HW_SUB_NET,     HardwareSubTab::NetworkInterfaces, L"Sieć i Przepustowość", L"Network & Live Traffic", 142.0f },
        { CMD_HW_SUB_SYS,     HardwareSubTab::SystemPowerProcs,  L"System, Audio i Procesy", L"OS, Audio & Processes", 148.0f }
    };
    float sx = subStrip.left + 8.0f;
    for (const auto& sd : subs) {
        D2D1_RECT_F sr = D2D1::RectF(sx, subStrip.top + 3.0f, sx + sd.w, subStrip.bottom - 3.0f);
        m_ribbonHits.push_back({ sd.cmd, sr });
        bool act = (m_hwSubTab == sd.tab);
        m_solidBrush->SetColor(act ? HexColor(0x0284C7) : (m_hoverRibbonBtn == sd.cmd ? HexColor(0x1E293B) : HexColor(0x131C2E)));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(sr, 3.0f, 3.0f), m_solidBrush);
        m_solidBrush->SetColor(act ? HexColor(0xFFFFFF) : HexColor(0x94A3B8));
        const wchar_t* lbl = (m_lang == Lang::PL) ? sd.pl : sd.en;
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_rt->DrawTextW(lbl, static_cast<UINT32>(wcslen(lbl)), m_fmtSmall, sr, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        sx += sd.w + 5.0f;
    }

    // 3. Scrollable Content Area
    D2D1_RECT_F workR = D2D1::RectF(rect.left, subStrip.bottom + 4.0f, rect.right, rect.bottom);
    m_rt->PushAxisAlignedClip(workR, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float curY = workR.top + 4.0f - m_scrollY;
    float padX = 10.0f;
    float availW = std::max(300.0f, (workR.right - workR.left) - padX * 2.0f);

    // Helper to draw a real-time 60s oscilloscope graph box
    auto drawOscilloscope = [&](const D2D1_RECT_F& boxR, const wchar_t* title, const std::wstring& bigValue,
                                const std::wstring& subValue, const std::array<float, 60>& hist, int cursor,
                                UINT32 accentHex) {
        m_solidBrush->SetColor(HexColor(0x131B2B));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(boxR, 5.0f, 5.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x26334D));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(boxR, 5.0f, 5.0f), m_solidBrush, 1.0f);

        // Header text
        D2D1_RECT_F tR = D2D1::RectF(boxR.left + 10.0f, boxR.top + 5.0f, boxR.right - 90.0f, boxR.top + 22.0f);
        m_solidBrush->SetColor(HexColor(0x94A3B8));
        m_rt->DrawTextW(title, static_cast<UINT32>(wcslen(title)), m_fmtSmall, tR, m_solidBrush);

        D2D1_RECT_F vR = D2D1::RectF(boxR.right - 115.0f, boxR.top + 4.0f, boxR.right - 8.0f, boxR.top + 24.0f);
        m_solidBrush->SetColor(HexColor(accentHex));
        m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        m_rt->DrawTextW(bigValue.c_str(), static_cast<UINT32>(bigValue.size()), m_fmtBold, vR, m_solidBrush);
        m_fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        D2D1_RECT_F subR = D2D1::RectF(boxR.left + 10.0f, boxR.top + 21.0f, boxR.right - 8.0f, boxR.top + 38.0f);
        m_solidBrush->SetColor(HexColor(0xCBD5E1));
        m_rt->DrawTextW(subValue.c_str(), static_cast<UINT32>(subValue.size()), m_fmtSmall, subR, m_solidBrush);

        // Graph plot area
        D2D1_RECT_F plotR = D2D1::RectF(boxR.left + 8.0f, boxR.top + 40.0f, boxR.right - 8.0f, boxR.bottom - 7.0f);
        m_solidBrush->SetColor(HexColor(0x0B101B));
        m_rt->FillRectangle(plotR, m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x1E293B));
        m_rt->DrawRectangle(plotR, m_solidBrush, 1.0f);

        // Subtle grid lines (25%, 50%, 75%)
        float pw = plotR.right - plotR.left;
        float ph = plotR.bottom - plotR.top;
        for (int g = 1; g <= 3; ++g) {
            float gy = plotR.top + ph * (g * 0.25f);
            m_rt->DrawLine(D2D1::Point2F(plotR.left, gy), D2D1::Point2F(plotR.right, gy), m_solidBrush, 0.7f);
        }

        // Waveform lines
        float stepX = pw / 59.0f;
        for (int i = 0; i < 59; ++i) {
            int idx0 = (cursor + i) % 60;
            int idx1 = (cursor + i + 1) % 60;
            float v0 = std::clamp(hist[idx0], 0.0f, 100.0f) * 0.01f;
            float v1 = std::clamp(hist[idx1], 0.0f, 100.0f) * 0.01f;
            float x0 = plotR.left + i * stepX;
            float x1 = plotR.left + (i + 1) * stepX;
            float y0 = plotR.bottom - v0 * (ph - 2.0f) - 1.0f;
            float y1 = plotR.bottom - v1 * (ph - 2.0f) - 1.0f;

            // Vertical fill column under curve
            m_solidBrush->SetColor(HexColor(accentHex, 0.18f));
            m_rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x1, plotR.bottom), m_solidBrush, stepX + 0.5f);

            // Top bright trace
            m_solidBrush->SetColor(HexColor(accentHex, 0.95f));
            m_rt->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), m_solidBrush, 1.8f);
        }
    };

    // Row 1: 4 Real-Time Oscilloscopes (Always shown at top of workstation)
    float oscGap = 8.0f;
    float oscW = (availW - oscGap * 3.0f) * 0.25f;
    float oscH = 104.0f;

    wchar_t cpuVal[32] = {}, cpuSub[128] = {};
    swprintf_s(cpuVal, L"%.1f%%", snap.cpuTotalUsagePct);
    swprintf_s(cpuSub, L"%uR / %uW  \x2022  %u MHz", snap.cpu.physicalCores, snap.cpu.logicalCores, snap.cpu.baseClockMHz);
    drawOscilloscope(D2D1::RectF(workR.left + padX, curY, workR.left + padX + oscW, curY + oscH),
                     L"PROCESOR (CPU LIVE 60s)", cpuVal, cpuSub, snap.historyCpu, snap.historyCursor, 0x38BDF8);

    wchar_t ramVal[32] = {}, ramSub[128] = {};
    swprintf_s(ramVal, L"%.1f%%", snap.ram.usagePct);
    swprintf_s(ramSub, L"%s / %s", FileEngine::FormatSizeShort(snap.ram.usedPhysBytes).c_str(), FileEngine::FormatSizeShort(snap.ram.totalPhysBytes).c_str());
    drawOscilloscope(D2D1::RectF(workR.left + padX + (oscW + oscGap), curY, workR.left + padX + oscW * 2 + oscGap, curY + oscH),
                     L"PAMIĘĆ RAM (LIVE 60s)", ramVal, ramSub, snap.historyRam, snap.historyCursor, 0xA855F7);

    wchar_t gpuVal[32] = {}, gpuSub[128] = {};
    uint64_t gVramUsed = snap.gpus.empty() ? 0 : snap.gpus[0].dedicatedVramUsedBytes;
    uint64_t gVramTot  = snap.gpus.empty() ? 0 : snap.gpus[0].dedicatedVramBytes;
    float gLoad = (!snap.gpus.empty() && snap.gpus[0].hasNvmlTelemetry)
                  ? static_cast<float>(snap.gpus[0].gpuUtilPct)
                  : (gVramTot > 0 ? static_cast<float>(100.0 * double(gVramUsed) / double(gVramTot)) : 0.0f);
    swprintf_s(gpuVal, L"%.0f%%", gLoad);
    if (!snap.gpus.empty() && snap.gpus[0].hasNvmlTelemetry) {
        swprintf_s(gpuSub, L"VRAM: %s / %s  \x2022  %u\x00B0C", FileEngine::FormatSizeShort(gVramUsed).c_str(), FileEngine::FormatSizeShort(gVramTot).c_str(), snap.gpus[0].tempC);
    } else {
        swprintf_s(gpuSub, L"VRAM: %s / %s", FileEngine::FormatSizeShort(gVramUsed).c_str(), FileEngine::FormatSizeShort(gVramTot).c_str());
    }
    drawOscilloscope(D2D1::RectF(workR.left + padX + (oscW + oscGap) * 2, curY, workR.left + padX + oscW * 3 + oscGap * 2, curY + oscH),
                     L"KARTA GRAFICZNA (GPU/VRAM)", gpuVal, gpuSub, snap.historyGpu, snap.historyCursor, 0x10B981);

    double totalRxKB = 0.0, totalTxKB = 0.0;
    for (const auto& n : snap.networks) {
        totalRxKB += n.rxRateKBps;
        totalTxKB += n.txRateKBps;
    }
    wchar_t netVal[64] = {}, netSub[128] = {};
    if (totalRxKB + totalTxKB >= 1024.0) {
        swprintf_s(netVal, L"%.2f MB/s", (totalRxKB + totalTxKB) / 1024.0);
    } else {
        swprintf_s(netVal, L"%.1f KB/s", totalRxKB + totalTxKB);
    }
    swprintf_s(netSub, L"\x2193 RX: %.1f KB/s   \x2191 TX: %.1f KB/s", totalRxKB, totalTxKB);
    drawOscilloscope(D2D1::RectF(workR.left + padX + (oscW + oscGap) * 3, curY, workR.left + padX + availW, curY + oscH),
                     L"SIEĆ (PRZEPUSTOWOŚĆ LIVE)", netVal, netSub, snap.historyNetKBps, snap.historyCursor, 0xF59E0B);

    curY += oscH + 10.0f;

    // Helper to render a structured Pro Diagnostic Card with key-value rows and register click-to-copy
    struct KvRow { std::wstring key; std::wstring val; UINT32 valColor = 0xF1F5F9; };
    auto drawProCard = [&](float x, float y, float w, const std::wstring& title, UINT32 badgeHex, const std::vector<KvRow>& rows) -> float {
        float rowH = 20.0f;
        float h = 34.0f + static_cast<float>(rows.size()) * rowH + 8.0f;
        D2D1_RECT_F cardR = D2D1::RectF(x, y, x + w, y + h);

        m_solidBrush->SetColor(HexColor(0x131B2B));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(cardR, 5.0f, 5.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x26334D));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(cardR, 5.0f, 5.0f), m_solidBrush, 1.0f);

        // Top accent bar on card header
        D2D1_RECT_F hdrR = D2D1::RectF(x, y, x + w, y + 28.0f);
        m_solidBrush->SetColor(HexColor(0x192338));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(hdrR, 5.0f, 5.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(badgeHex));
        m_rt->FillRectangle(D2D1::RectF(x, y + 4.0f, x + 4.0f, y + 24.0f), m_solidBrush);

        D2D1_RECT_F titleR = D2D1::RectF(x + 12.0f, y + 4.0f, x + w - 85.0f, y + 26.0f);
        m_solidBrush->SetColor(HexColor(0xF8FAFC));
        m_rt->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), m_fmtBold, titleR, m_solidBrush);

        D2D1_RECT_F copyHintR = D2D1::RectF(x + w - 82.0f, y + 5.0f, x + w - 8.0f, y + 25.0f);
        m_solidBrush->SetColor(HexColor(0x64748B));
        const wchar_t* cpLbl = (m_lang == Lang::PL) ? L"[\xD83D\xDCCB Kopiuj]" : L"[\xD83D\xDCCB Copy]";
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        m_rt->DrawTextW(cpLbl, static_cast<UINT32>(wcslen(cpLbl)), m_fmtSmall, copyHintR, m_solidBrush);
        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        std::wstring clipPayload = L"=== " + title + L" ===\r\n";
        float ry = y + 32.0f;
        float keyW = std::min(195.0f, w * 0.38f);

        for (size_t i = 0; i < rows.size(); ++i) {
            if (i % 2 == 1) {
                m_solidBrush->SetColor(HexColor(0x162032, 0.55f));
                m_rt->FillRectangle(D2D1::RectF(x + 4.0f, ry, x + w - 4.0f, ry + rowH), m_solidBrush);
            }
            D2D1_RECT_F kR = D2D1::RectF(x + 12.0f, ry + 1.0f, x + keyW, ry + rowH);
            D2D1_RECT_F vR = D2D1::RectF(x + keyW + 4.0f, ry + 1.0f, x + w - 10.0f, ry + rowH);

            m_solidBrush->SetColor(HexColor(0x94A3B8));
            m_rt->DrawTextW(rows[i].key.c_str(), static_cast<UINT32>(rows[i].key.size()), m_fmtSmall, kR, m_solidBrush);

            m_solidBrush->SetColor(HexColor(rows[i].valColor));
            m_rt->DrawTextW(rows[i].val.c_str(), static_cast<UINT32>(rows[i].val.size()), m_fmtSmall, vR, m_solidBrush);

            clipPayload += rows[i].key + L": " + rows[i].val + L"\r\n";
            ry += rowH;
        }

        m_hwCardHits.push_back({ cardR, title, clipPayload });
        return h;
    };

    float colGap = 10.0f;
    float halfW = (availW - colGap) * 0.5f;
    float leftX = workR.left + padX;
    float rightX = leftX + halfW + colGap;

    bool showAll = (m_hwSubTab == HardwareSubTab::Overview);

    // =========================================================================
    // SECTION A: CPU Architecture, Cache Hierarchy, ISA Flags & Per-Core Heatmap
    // =========================================================================
    if (showAll || m_hwSubTab == HardwareSubTab::CpuCores) {
        std::vector<KvRow> cpuRows;
        cpuRows.push_back({ m_lang == Lang::PL ? L"Model procesora" : L"Processor Brand", snap.cpu.brand, 0x38BDF8 });
        cpuRows.push_back({ m_lang == Lang::PL ? L"Producent / Gniazdo" : L"Vendor / Socket", snap.cpu.vendor + L"  \x2022  " + snap.cpu.socketDesignation });
        wchar_t topoBuf[128] = {};
        swprintf_s(topoBuf, L"%u fizycznych rdzeni  /  %u wątków logicznych  (%u pakiet)", snap.cpu.physicalCores, snap.cpu.logicalCores, snap.cpu.packages);
        cpuRows.push_back({ m_lang == Lang::PL ? L"Rdzenie / Wątki" : L"Cores / Threads", topoBuf, 0x4ADE80 });
        wchar_t famBuf[128] = {};
        swprintf_s(famBuf, L"Family %u (0x%X), Model %u (0x%X), Stepping %u", snap.cpu.family, snap.cpu.family, snap.cpu.model, snap.cpu.model, snap.cpu.stepping);
        cpuRows.push_back({ L"Architektura (CPUID)", famBuf });
        wchar_t clkBuf[128] = {};
        swprintf_s(clkBuf, L"Bazowe: %u MHz   \x2022   Max Boost: %u MHz", snap.cpu.baseClockMHz, snap.cpu.maxClockMHz);
        cpuRows.push_back({ m_lang == Lang::PL ? L"Taktowanie" : L"Clock Speeds", clkBuf });
        wchar_t cacheBuf[160] = {};
        swprintf_s(cacheBuf, L"L1D: %u KB  |  L1I: %u KB  |  L2: %u KB  |  L3: %u KB (Linia %u B)",
                   snap.cpu.l1DataKB, snap.cpu.l1InstKB, snap.cpu.l2KB, snap.cpu.l3KB, snap.cpu.cacheLineBytes);
        cpuRows.push_back({ L"Hierarchia Cache", cacheBuf, 0xFBBF24 });

        std::wstring isa;
        if (snap.cpu.sse42)    isa += L"[SSE4.2] ";
        if (snap.cpu.avx)      isa += L"[AVX] ";
        if (snap.cpu.avx2)     isa += L"[AVX2] ";
        if (snap.cpu.avx512f)  isa += L"[AVX-512] ";
        if (snap.cpu.fma3)     isa += L"[FMA3] ";
        if (snap.cpu.aesNi)    isa += L"[AES-NI] ";
        if (snap.cpu.sha)      isa += L"[SHA] ";
        if (snap.cpu.vtX_amdV) isa += L"[VT-x/AMD-V] ";
        if (snap.cpu.hypervisorPresent) isa += L"[Hypervisor]";
        cpuRows.push_back({ L"Instrukcje ISA", isa, 0xC084FC });

        if (snap.cpuBenchRunning) {
            cpuRows.push_back({ L"Benchmark CPU", L"\x23F3 Trwa test wielowątkowy...", 0xFBBF24 });
        } else if (snap.cpuSingleCoreGops > 0.0) {
            wchar_t bBuf[128] = {};
            swprintf_s(bBuf, L"1-Wątek: %.2f GOPS   \x2022   Wielowątkowo (%uW): %.2f GOPS", snap.cpuSingleCoreGops, snap.cpu.logicalCores, snap.cpuMultiCoreGops);
            cpuRows.push_back({ L"\x26A1 Wynik Benchmarku CPU", bBuf, 0x38BDF8 });
        }

        float hLeft = drawProCard(leftX, curY, halfW, m_lang == Lang::PL ? L"PROCESOR (ARCHITEKTURA CPUID & CACHE)" : L"PROCESSOR (CPUID & CACHE TOPOLOGY)", 0x38BDF8, cpuRows);

        // Right card: Per-Core Live Utilization Heatmap Grid
        int nCores = static_cast<int>(snap.perCoreUsagePct.size());
        int cols = (nCores > 16) ? 8 : 4;
        int rowsCount = std::max(1, (nCores + cols - 1) / cols);
        float cellH = 30.0f;
        float heatH = std::max(hLeft, 44.0f + rowsCount * cellH + 10.0f);

        D2D1_RECT_F heatCard = D2D1::RectF(rightX, curY, rightX + halfW, curY + heatH);
        m_solidBrush->SetColor(HexColor(0x131B2B));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(heatCard, 5.0f, 5.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x26334D));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(heatCard, 5.0f, 5.0f), m_solidBrush, 1.0f);

        D2D1_RECT_F hhR = D2D1::RectF(rightX, curY, rightX + halfW, curY + 28.0f);
        m_solidBrush->SetColor(HexColor(0x192338));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(hhR, 5.0f, 5.0f), m_solidBrush);
        m_solidBrush->SetColor(HexColor(0x10B981));
        m_rt->FillRectangle(D2D1::RectF(rightX, curY + 4.0f, rightX + 4.0f, curY + 24.0f), m_solidBrush);

        wchar_t heatTitle[128] = {};
        swprintf_s(heatTitle, L"OBCIĄŻENIE RDZENI NA ŻYWO (%d WĄTKÓW \x2022 %.1f%%)", nCores, snap.cpuTotalUsagePct);
        D2D1_RECT_F htR = D2D1::RectF(rightX + 12.0f, curY + 4.0f, rightX + halfW - 10.0f, curY + 26.0f);
        m_solidBrush->SetColor(HexColor(0xF8FAFC));
        m_rt->DrawTextW(heatTitle, static_cast<UINT32>(wcslen(heatTitle)), m_fmtBold, htR, m_solidBrush);

        float gridPad = 8.0f;
        float cellW = (halfW - gridPad * 2.0f - (cols - 1) * 4.0f) / static_cast<float>(cols);
        for (int c = 0; c < nCores; ++c) {
            int rIdx = c / cols;
            int cIdx = c % cols;
            float cx0 = rightX + gridPad + cIdx * (cellW + 4.0f);
            float cy0 = curY + 36.0f + rIdx * cellH;
            D2D1_RECT_F cBox = D2D1::RectF(cx0, cy0, cx0 + cellW, cy0 + cellH - 4.0f);

            float pct = std::clamp(snap.perCoreUsagePct[c], 0.0f, 100.0f);
            UINT32 barCol = (pct > 85.0f) ? 0xEF4444 : (pct > 55.0f ? 0xF59E0B : (pct > 20.0f ? 0x10B981 : 0x0284C7));

            m_solidBrush->SetColor(HexColor(0x0B101B));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(cBox, 3.0f, 3.0f), m_solidBrush);

            float fillW = (cBox.right - cBox.left) * (pct * 0.01f);
            if (fillW > 1.5f) {
                D2D1_RECT_F fBox = D2D1::RectF(cBox.left, cBox.top, cBox.left + fillW, cBox.bottom);
                m_solidBrush->SetColor(HexColor(barCol, 0.42f));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(fBox, 3.0f, 3.0f), m_solidBrush);
            }
            m_solidBrush->SetColor(HexColor(barCol, 0.75f));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(cBox, 3.0f, 3.0f), m_solidBrush, 1.0f);

            wchar_t cLbl[32] = {};
            swprintf_s(cLbl, L"C%d: %.0f%%", c, pct);
            m_solidBrush->SetColor(HexColor(0xF1F5F9));
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_rt->DrawTextW(cLbl, static_cast<UINT32>(wcslen(cLbl)), m_fmtSmall, cBox, m_solidBrush);
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        curY += std::max(hLeft, heatH) + 10.0f;
    }

    // =========================================================================
    // SECTION B: GPU & Connected Monitors Inspector (DXGI + NVML)
    // =========================================================================
    if (showAll || m_hwSubTab == HardwareSubTab::GpuDisplays) {
        std::vector<KvRow> gpuRows;
        for (size_t i = 0; i < snap.gpus.size(); ++i) {
            const auto& g = snap.gpus[i];
            std::wstring prefix = L"[GPU " + std::to_wstring(i) + L"] ";
            gpuRows.push_back({ prefix + L"Układ graficzny", g.name, 0x10B981 });
            gpuRows.push_back({ prefix + L"VRAM Dedykowany (Użycie)",
                                FileEngine::FormatSizeShort(g.dedicatedVramUsedBytes) + L" / " + FileEngine::FormatSizeShort(g.dedicatedVramBytes) +
                                L"   (Współdzielony: " + FileEngine::FormatSizeShort(g.sharedSystemBytes) + L")", 0x38BDF8 });
            wchar_t pciBuf[128] = {};
            swprintf_s(pciBuf, L"Vendor: 0x%04X  \x2022  Device: 0x%04X  \x2022  Rev: 0x%02X", g.vendorId, g.deviceId, g.revision);
            gpuRows.push_back({ prefix + L"Identyfikatory PCI", pciBuf });
            if (!g.driverVersion.empty()) {
                gpuRows.push_back({ prefix + L"Wersja sterownika", g.driverVersion + (g.driverDate.empty() ? L"" : (L" (" + g.driverDate + L")")) });
            }
            if (g.hasNvmlTelemetry) {
                wchar_t nvBuf[160] = {};
                swprintf_s(nvBuf, L"Rdzeń: %u%% (%u MHz)  \x2022  VRAM: %u MHz  \x2022  Temp: %u\x00B0C  \x2022  Wentylator: %u%%  \x2022  Moc: %.1f W",
                           g.gpuUtilPct, g.coreClockMHz, g.memClockMHz, g.tempC, g.fanSpeedPct, double(g.powerMilliwatts) / 1000.0);
                gpuRows.push_back({ prefix + L"Sensory NVML Live", nvBuf, 0xFBBF24 });
            }
        }

        std::vector<KvRow> monRows;
        for (size_t i = 0; i < snap.displays.size(); ++i) {
            const auto& d = snap.displays[i];
            wchar_t resBuf[128] = {};
            swprintf_s(resBuf, L"%u \x00D7 %u @ %u Hz (%u-bit)%s",
                       d.width, d.height, d.refreshHz, d.bitsPerPel, d.isPrimary ? L"  [GŁÓWNY]" : L"");
            monRows.push_back({ d.monitorName + L" (" + d.deviceName + L")", resBuf, d.isPrimary ? 0x38BDF8 : 0xF1F5F9 });
            if (!d.adapterName.empty()) {
                monRows.push_back({ L"   Podłączony do", d.adapterName, 0x94A3B8 });
            }
        }
        if (monRows.empty()) {
            monRows.push_back({ L"Ekran", L"Brak dodatkowych danych EDID" });
        }

        float hG = drawProCard(leftX,  curY, halfW, m_lang == Lang::PL ? L"KARTY GRAFICZNE (DXGI / VRAM / NVML)" : L"GRAPHICS ADAPTERS (DXGI / VRAM / NVML)", 0x10B981, gpuRows);
        float hM = drawProCard(rightX, curY, halfW, m_lang == Lang::PL ? L"MONITORY I MATRYCE (ROZDZIELCZOŚĆ & HZ)" : L"CONNECTED DISPLAYS & REFRESH RATE", 0x06B6D4, monRows);
        curY += std::max(hG, hM) + 10.0f;
    }

    // =========================================================================
    // SECTION C: Physical RAM DIMM Slots (SMBIOS Type 17) & Motherboard/BIOS
    // =========================================================================
    if (showAll || m_hwSubTab == HardwareSubTab::MemoryDimms || m_hwSubTab == HardwareSubTab::MotherboardBios) {
        std::vector<KvRow> ramRows;
        wchar_t totRamBuf[160] = {};
        swprintf_s(totRamBuf, L"%s używane z %s (%.1f%%)   \x2022   Wolne: %s",
                   FileEngine::FormatSizeShort(snap.ram.usedPhysBytes).c_str(),
                   FileEngine::FormatSizeShort(snap.ram.totalPhysBytes).c_str(),
                   snap.ram.usagePct,
                   FileEngine::FormatSizeShort(snap.ram.availPhysBytes).c_str());
        ramRows.push_back({ L"Pamięć fizyczna RAM", totRamBuf, 0xA855F7 });
        ramRows.push_back({ L"Pamięć zadeklarowana (Commit)",
                            FileEngine::FormatSizeShort(snap.ram.commitTotalBytes) + L" / " + FileEngine::FormatSizeShort(snap.ram.commitLimitBytes) +
                            L"   (Szczyt: " + FileEngine::FormatSizeShort(snap.ram.commitPeakBytes) + L")" });
        ramRows.push_back({ L"Pula jądra (Paged / Non-Paged)",
                            FileEngine::FormatSizeShort(snap.ram.kernelPagedBytes) + L" / " + FileEngine::FormatSizeShort(snap.ram.kernelNonPagedBytes) });

        if (snap.ramBenchRunning) {
            ramRows.push_back({ L"Benchmark RAM", L"\x23F3 Trwa test przepustowości RAM (256 MB)...", 0xFBBF24 });
        } else if (snap.ramReadGBps > 0.0) {
            wchar_t rbBuf[128] = {};
            swprintf_s(rbBuf, L"Odczyt: %.2f GB/s   \x2022   Zapis: %.2f GB/s   \x2022   Kopiowanie: %.2f GB/s",
                       snap.ramReadGBps, snap.ramWriteGBps, snap.ramCopyGBps);
            ramRows.push_back({ L"\x26A1 Przepustowość RAM", rbBuf, 0x4ADE80 });
        }

        for (size_t i = 0; i < snap.ram.dimms.size(); ++i) {
            const auto& dm = snap.ram.dimms[i];
            wchar_t dSpec[256] = {};
            swprintf_s(dSpec, L"%s  \x2022  %s @ %u MT/s (Max %u)  \x2022  %s %s",
                       FileEngine::FormatSizeShort(dm.capacityBytes).c_str(),
                       dm.memoryType.c_str(), dm.configuredSpeedMTs, dm.speedMTs,
                       dm.manufacturer.c_str(), dm.partNumber.c_str());
            ramRows.push_back({ L"Slot [" + dm.slotLocator + L"]", dSpec, 0x38BDF8 });
        }

        std::vector<KvRow> brdRows;
        brdRows.push_back({ L"Producent płyty głównej", snap.board.boardManufacturer, 0xF59E0B });
        brdRows.push_back({ L"Model płyty głównej", snap.board.boardProduct + (snap.board.boardVersion.empty() ? L"" : (L" (Rev " + snap.board.boardVersion + L")")), 0x38BDF8 });
        if (!snap.board.boardSerial.empty()) {
            brdRows.push_back({ L"Numer seryjny płyty", snap.board.boardSerial });
        }
        brdRows.push_back({ L"Producent UEFI / BIOS", snap.board.biosVendor });
        brdRows.push_back({ L"Wersja i Data BIOS", snap.board.biosVersion + L"   (" + snap.board.biosDate + L")", 0x4ADE80 });
        brdRows.push_back({ L"System / Produkt", snap.board.sysManufacturer + L" " + snap.board.sysProductName });
        if (!snap.board.sysSku.empty()) {
            brdRows.push_back({ L"SKU / Rodzina", snap.board.sysSku + L" / " + snap.board.sysFamily });
        }

        float hR = drawProCard(leftX,  curY, halfW, m_lang == Lang::PL ? L"PAMIĘĆ RAM & KOŚCI FIZYCZNE (SMBIOS TYPE 17)" : L"PHYSICAL RAM & DIMM SLOTS (SMBIOS TYPE 17)", 0xA855F7, ramRows);
        float hB = drawProCard(rightX, curY, halfW, m_lang == Lang::PL ? L"PŁYTA GŁÓWNA, CHIPSET & UEFI / BIOS (SMBIOS)" : L"MOTHERBOARD & UEFI / BIOS FIRMWARE", 0xF59E0B, brdRows);
        curY += std::max(hR, hB) + 10.0f;
    }

    // =========================================================================
    // SECTION D: Physical Disks (NVMe/SATA/USB IOCTL) & Logical Volumes
    // =========================================================================
    if (showAll || m_hwSubTab == HardwareSubTab::StorageSmart) {
        std::vector<KvRow> pdRows;
        for (const auto& pd : snap.physicalDisks) {
            std::wstring tag = L"Dysk #" + std::to_wstring(pd.index) + L" [" + pd.busType + L" " + pd.mediaType + L"]";
            pdRows.push_back({ tag, pd.model + L"  (" + FileEngine::FormatSizeShort(pd.sizeBytes) + L")", 0x38BDF8 });
            wchar_t detBuf[256] = {};
            swprintf_s(detBuf, L"FW: %s  \x2022  Sektor: %uB/%uB  \x2022  TRIM: %s  \x2022  NCQ: %s%s%s",
                       pd.firmwareRev.empty() ? L"-" : pd.firmwareRev.c_str(),
                       pd.bytesPerLogicalSector, pd.bytesPerPhysicalSector,
                       pd.trimEnabled ? L"Tak" : L"Nie",
                       pd.ncqSupported ? L"Tak" : L"Nie",
                       pd.serialNumber.empty() ? L"" : L"  \x2022  S/N: ",
                       pd.serialNumber.c_str());
            pdRows.push_back({ L"   Parametry kontrolera", detBuf, 0xCBD5E1 });
        }

        std::vector<KvRow> volRows;
        for (const auto& v : snap.volumes) {
            wchar_t vBuf[256] = {};
            swprintf_s(vBuf, L"[%s]  Użyte: %s / %s (%.1f%%)  \x2022  Wolne: %s",
                       v.fileSystem.c_str(),
                       FileEngine::FormatSizeShort(v.usedBytes).c_str(),
                       FileEngine::FormatSizeShort(v.totalBytes).c_str(),
                       v.usagePct,
                       FileEngine::FormatSizeShort(v.freeBytes).c_str());
            UINT32 col = (v.usagePct > 90.0f) ? 0xEF4444 : (v.usagePct > 75.0f ? 0xFBBF24 : 0x4ADE80);
            volRows.push_back({ v.rootPath + L" " + v.label + L" (" + v.driveType + L")", vBuf, col });
        }

        float hP = drawProCard(leftX,  curY, halfW, m_lang == Lang::PL ? L"FIZYCZNE NOŚNIKI DANYCH (NVMe / SATA / USB IOCTL)" : L"PHYSICAL STORAGE DRIVES (IOCTL)", 0x0EA5E9, pdRows);
        float hV = drawProCard(rightX, curY, halfW, m_lang == Lang::PL ? L"PARTYCJE LOGICZNE I SYSTEMY PLIKÓW" : L"LOGICAL VOLUMES & PARTITIONS", 0x10B981, volRows);
        curY += std::max(hP, hV) + 10.0f;
    }

    // =========================================================================
    // SECTION E: Network Interfaces & OS / Power / Audio / Top 10 Processes
    // =========================================================================
    if (showAll || m_hwSubTab == HardwareSubTab::NetworkInterfaces || m_hwSubTab == HardwareSubTab::SystemPowerProcs) {
        std::vector<KvRow> netRows;
        for (const auto& n : snap.networks) {
            std::wstring status = n.isUp ? L"[AKTYWNA] " : L"[NIEAKTYWNA] ";
            netRows.push_back({ status + n.friendlyName, n.description, n.isUp ? 0x4ADE80 : 0x94A3B8 });
            wchar_t ipBuf[256] = {};
            swprintf_s(ipBuf, L"IPv4: %s   \x2022   Brama: %s   \x2022   DNS: %s",
                       n.ipv4.empty() ? L"-" : n.ipv4.c_str(),
                       n.gateway.empty() ? L"-" : n.gateway.c_str(),
                       n.dnsServers.empty() ? L"-" : n.dnsServers.c_str());
            netRows.push_back({ L"   Adresacja IP / DNS", ipBuf, 0x38BDF8 });
            wchar_t trBuf[256] = {};
            swprintf_s(trBuf, L"MAC: %s  \x2022  Link: %llu Mbps  \x2022  RX: %s (%.1f KB/s)  \x2022  TX: %s (%.1f KB/s)",
                       n.macAddress.c_str(),
                       static_cast<unsigned long long>(n.linkSpeedBitsPerSec / 1000000ULL),
                       FileEngine::FormatSizeShort(n.rxBytesTotal).c_str(), n.rxRateKBps,
                       FileEngine::FormatSizeShort(n.txBytesTotal).c_str(), n.txRateKBps);
            netRows.push_back({ L"   Statystyki pakietów", trBuf, 0xCBD5E1 });
        }

        std::vector<KvRow> sysRows;
        sysRows.push_back({ L"System operacyjny", snap.os.osName + L" (" + snap.os.architecture + L", " + snap.os.buildNumber + L")", 0x38BDF8 });
        uint64_t upSec = snap.os.uptimeSeconds;
        wchar_t upBuf[128] = {};
        swprintf_s(upBuf, L"%llu dni, %llu godz., %llu min, %llu sek.", upSec / 86400ULL, (upSec % 86400ULL) / 3600ULL, (upSec % 3600ULL) / 60ULL, upSec % 60ULL);
        sysRows.push_back({ L"Czas pracy systemu (Uptime)", upBuf, 0x4ADE80 });
        sysRows.push_back({ L"Komputer / Użytkownik", snap.os.computerName + L"  /  " + snap.os.userName });
        wchar_t cntBuf[128] = {};
        swprintf_s(cntBuf, L"Procesy: %u   \x2022   Wątki: %u   \x2022   Uchwyty (Handles): %u", snap.os.processCount, snap.os.threadCount, snap.os.handleCount);
        sysRows.push_back({ L"Obiekty jądra NT", cntBuf });

        if (!snap.audioDevices.empty()) {
            std::wstring audStr;
            for (size_t i = 0; i < snap.audioDevices.size(); ++i) {
                if (i > 0) audStr += L"  |  ";
                audStr += snap.audioDevices[i];
            }
            sysRows.push_back({ L"Urządzenia Audio", audStr, 0xFBBF24 });
        }

        for (size_t i = 0; i < std::min<size_t>(6, snap.topProcesses.size()); ++i) {
            const auto& tp = snap.topProcesses[i];
            wchar_t pBuf[128] = {};
            swprintf_s(pBuf, L"%s   (PID: %u, Wątki: %u)", FileEngine::FormatSizeShort(tp.workingSetBytes).c_str(), tp.pid, tp.threads);
            sysRows.push_back({ L"Top RAM #" + std::to_wstring(i + 1) + L": " + tp.name, pBuf, 0xE2E8F0 });
        }

        float hN = drawProCard(leftX,  curY, halfW, m_lang == Lang::PL ? L"KARTY SIECIOWE (IPv4 / IPv6 / MAC / RUCH LIVE)" : L"NETWORK INTERFACES (IPv4 / MAC / LIVE TRAFFIC)", 0xF59E0B, netRows);
        float hS = drawProCard(rightX, curY, halfW, m_lang == Lang::PL ? L"SYSTEM WINDOWS, AUDIO & TOP PROCESY RAM" : L"WINDOWS OS, AUDIO & TOP PROCESSES BY RAM", 0xEC4899, sysRows);
        curY += std::max(hN, hS) + 16.0f;
    }

    m_rt->PopAxisAlignedClip();
    m_rt->PopAxisAlignedClip();
}

} // namespace Explorer2
