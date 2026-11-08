#include "FileContentPane.h"
#include "CommandIds.h"
#include "PathUtils.h"
#include "FileEngine.h"
#include <algorithm>
#include <cmath>

namespace Explorer2 {

static D2D1_COLOR_F FcpColor(uint32_t rgb, float a = 1.0f) {
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        a);
}

FileContentPane::~FileContentPane() {
    Shutdown();
}

void FileContentPane::Initialize(
    HWND parentHwnd,
    HINSTANCE hInstance,
    ID2D1Factory* d2dFactory,
    IDWriteFactory* dwriteFactory)
{
    m_parentHwnd = parentHwnd;
    m_notepad.Initialize(parentHwnd, hInstance);
    m_hex.Initialize(parentHwnd, dwriteFactory);
    m_model3D.Initialize(parentHwnd, d2dFactory);
}

void FileContentPane::Shutdown() {
    m_notepad.Shutdown();
    m_hex.Shutdown();
    m_model3D.Shutdown();
}

void FileContentPane::HideNativeControls() {
    m_notepad.HideWindow();
}

ResolvedContentKind FileContentPane::ResolveKindForCurrentEntry() const {
    if (!m_hasEntry || m_currentEntry.fullPath.empty()) {
        return ResolvedContentKind::None;
    }
    if (m_currentEntry.isDirectory || m_currentEntry.isDrive) {
        return ResolvedContentKind::DirectoryInfo;
    }

    if (m_userTabMode == FileContentTabMode::Notepad) {
        return ResolvedContentKind::NotepadEditor;
    }
    if (m_userTabMode == FileContentTabMode::Hex) {
        return ResolvedContentKind::HexInspector;
    }
    if (m_userTabMode == FileContentTabMode::Model3D) {
        return ResolvedContentKind::Model3DViewport;
    }

    // Auto detection based on file type & content
    const std::wstring& ext = m_currentEntry.ext;
    if (Model3DViewer::Is3DModelExtension(ext)) {
        return ResolvedContentKind::Model3DViewport;
    }
    if (m_currentEntry.isImage) {
        return ResolvedContentKind::ImagePreview;
    }
    if (m_currentEntry.isVideo || m_currentEntry.isAudio) {
        return ResolvedContentKind::MediaPreview;
    }
    if (NotepadViewer::IsLikelyTextFile(m_currentEntry.fullPath, ext, m_currentEntry.size)) {
        return ResolvedContentKind::NotepadEditor;
    }
    return ResolvedContentKind::HexInspector;
}

void FileContentPane::ActivateResolvedViewer() {
    m_resolvedKind = ResolveKindForCurrentEntry();
    if (m_resolvedKind != ResolvedContentKind::NotepadEditor) {
        m_notepad.HideWindow();
    }

    if (!m_hasEntry || m_currentEntry.isDirectory || m_currentEntry.isDrive) {
        return;
    }

    switch (m_resolvedKind) {
    case ResolvedContentKind::NotepadEditor:
        m_notepad.LoadFile(m_currentEntry.fullPath, false);
        break;
    case ResolvedContentKind::HexInspector:
        m_hex.LoadFile(m_currentEntry.fullPath, false);
        break;
    case ResolvedContentKind::Model3DViewport:
        m_model3D.LoadFile(m_currentEntry.fullPath, false);
        break;
    default:
        break;
    }
}

void FileContentPane::SyncToSelection(const FileEntry* entry, bool paneVisible) {
    if (!paneVisible) {
        HideNativeControls();
        return;
    }
    if (!entry) {
        m_hasEntry = false;
        m_currentEntry = {};
        m_resolvedKind = ResolvedContentKind::None;
        HideNativeControls();
        return;
    }

    bool changedPath = (!m_hasEntry || m_currentEntry.fullPath != entry->fullPath);
    m_hasEntry = true;
    m_currentEntry = *entry;
    if (changedPath) {
        m_userTabMode = FileContentTabMode::Auto;
    }
    ActivateResolvedViewer();
}

void FileContentPane::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    IDWriteTextFormat* fmtCenterSmall,
    ImageCache& imageCache,
    MediaPlayer& mediaPlayer,
    const D2D1_RECT_F& rect,
    bool draggingSplitter,
    Lang lang,
    D2D1_POINT_2F mousePt,
    D2D1_RECT_F& outMediaSeekBarRect)
{
    m_paneRect = rect;
    m_headerHits.clear();
    const bool pl = (lang == Lang::PL);

    if (rect.right <= rect.left + 40.0f || rect.bottom <= rect.top + 40.0f) {
        HideNativeControls();
        return;
    }

    // 1. Left Splitter Bar (5px draggable grip) + Pane Background
    float splitW = 5.0f;
    D2D1_RECT_F splitR = D2D1::RectF(rect.left, rect.top, rect.left + splitW, rect.bottom);
    D2D1_RECT_F innerR = D2D1::RectF(rect.left + splitW, rect.top, rect.right, rect.bottom);

    brush->SetColor(draggingSplitter ? FcpColor(0xCCE8FF) : FcpColor(0xF1F5F9));
    rt->FillRectangle(splitR, brush);
    brush->SetColor(draggingSplitter ? FcpColor(0x0078D7) : FcpColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(splitR.left + 0.5f, splitR.top), D2D1::Point2F(splitR.left + 0.5f, splitR.bottom), brush, 1.0f);
    rt->DrawLine(D2D1::Point2F(splitR.right - 0.5f, splitR.top), D2D1::Point2F(splitR.right - 0.5f, splitR.bottom), brush, 1.0f);
    float gripCy = (splitR.top + splitR.bottom) * 0.5f;
    float gripCx = (splitR.left + splitR.right) * 0.5f;
    brush->SetColor(FcpColor(0x64748B));
    for (int g = -2; g <= 2; ++g) {
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(gripCx, gripCy + g * 5.0f), 1.2f, 1.2f), brush);
    }

    brush->SetColor(FcpColor(0xFFFFFF));
    rt->FillRectangle(innerR, brush);

    // 2. Top Header Bar (34px: File title on left + Mode Switcher Tabs [Auto] [Notatnik] [Hex] [3D] [X] on right)
    float hdrH = 34.0f;
    D2D1_RECT_F hdrR = D2D1::RectF(innerR.left, innerR.top, innerR.right, innerR.top + hdrH);
    brush->SetColor(FcpColor(0xEFF6FF));
    rt->FillRectangle(hdrR, brush);
    brush->SetColor(FcpColor(0x0078D7));
    rt->FillRectangle(D2D1::RectF(hdrR.left, hdrR.top, hdrR.right, hdrR.top + 2.5f), brush);
    brush->SetColor(FcpColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(hdrR.left, hdrR.bottom - 0.5f), D2D1::Point2F(hdrR.right, hdrR.bottom - 0.5f), brush, 1.0f);

    float rx = hdrR.right - 5.0f;
    auto drawTabPill = [&](int id, float w, const wchar_t* label, bool active, bool isClose = false) {
        rx -= w;
        D2D1_RECT_F br = D2D1::RectF(rx, hdrR.top + 6.0f, rx + w, hdrR.bottom - 5.0f);
        m_headerHits.push_back({ id, br });
        bool hov = (m_hoverHeaderBtn == id);
        if (isClose) {
            if (hov) {
                brush->SetColor(FcpColor(0xFEE2E2));
                rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush);
            }
            brush->SetColor(hov ? FcpColor(0xDC2626) : FcpColor(0x475569));
        } else {
            brush->SetColor(active ? FcpColor(0x0078D7) : (hov ? FcpColor(0xDBEAFE) : FcpColor(0xFFFFFF)));
            rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush);
            brush->SetColor(active ? FcpColor(0x005A9E) : FcpColor(0x93C5FD));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush, 1.0f);
            brush->SetColor(active ? FcpColor(0xFFFFFF) : FcpColor(0x1E3A8A));
        }
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), fmtSmall, br, brush);
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        rx -= 3.0f;
    };

    drawTabPill(HBTN_CLOSE_PANE, 22.0f, L"\x2715", false, true);
    if (m_hasEntry && !m_currentEntry.isDirectory && !m_currentEntry.isDrive) {
        bool is3DExt = Model3DViewer::Is3DModelExtension(m_currentEntry.ext);
        if (is3DExt || m_resolvedKind == ResolvedContentKind::Model3DViewport) {
            drawTabPill(HBTN_TAB_3D, 40.0f, L"3D", m_resolvedKind == ResolvedContentKind::Model3DViewport);
        }
        drawTabPill(HBTN_TAB_HEX,     44.0f, L"Hex", m_resolvedKind == ResolvedContentKind::HexInspector);
        drawTabPill(HBTN_TAB_NOTEPAD, pl ? 64.0f : 56.0f, pl ? L"Notatnik" : L"Text", m_resolvedKind == ResolvedContentKind::NotepadEditor);
        drawTabPill(HBTN_TAB_AUTO,    44.0f, L"Auto", m_userTabMode == FileContentTabMode::Auto);
    }

    std::wstring hdrTitle;
    if (!m_hasEntry) {
        hdrTitle = pl ? L"Podgląd zawartości pliku" : L"File Content Inspector";
    } else if (m_currentEntry.isDirectory || m_currentEntry.isDrive) {
        hdrTitle = L"\xD83D\xDCC1 " + m_currentEntry.name;
    } else {
        hdrTitle = L"\xD83D\xDCC4 " + m_currentEntry.name + L"  (" + m_currentEntry.sizeStr + L")";
    }
    D2D1_RECT_F titleR = D2D1::RectF(hdrR.left + 8.0f, hdrR.top + 2.0f, std::max(hdrR.left + 20.0f, rx - 4.0f), hdrR.bottom);
    brush->SetColor(FcpColor(0x0F172A));
    rt->DrawTextW(hdrTitle.c_str(), static_cast<UINT32>(hdrTitle.size()), fmtBold, titleR, brush);

    m_bodyRect = D2D1::RectF(innerR.left, hdrR.bottom, innerR.right, innerR.bottom);

    // 3. Dispatch Body Rendering to the active specialized viewer
    if (m_resolvedKind != ResolvedContentKind::NotepadEditor) {
        m_notepad.HideWindow();
    }

    switch (m_resolvedKind) {
    case ResolvedContentKind::None: {
        std::wstring emptyMsg = pl
            ? L"Zaznacz dowolny plik na liście po lewej stronie,\naby otworzyć go w Notatniku, widoku Hex, podglądzie 3D lub podglądzie multimediów."
            : L"Select any file in the list on the left\nto open it in Notepad, Hex Viewer, 3D Viewer, or Media Preview.";
        D2D1_RECT_F msgR = D2D1::RectF(m_bodyRect.left + 20.0f, m_bodyRect.top + 40.0f, m_bodyRect.right - 20.0f, m_bodyRect.bottom - 40.0f);
        brush->SetColor(FcpColor(0x64748B));
        rt->DrawTextW(emptyMsg.c_str(), static_cast<UINT32>(emptyMsg.size()), fmtCenterSmall, msgR, brush);
        break;
    }

    case ResolvedContentKind::DirectoryInfo: {
        D2D1_RECT_F cardR = D2D1::RectF(m_bodyRect.left + 16.0f, m_bodyRect.top + 18.0f, m_bodyRect.right - 16.0f, std::min(m_bodyRect.bottom - 18.0f, m_bodyRect.top + 210.0f));
        brush->SetColor(FcpColor(0xF8FAFC));
        rt->FillRoundedRectangle(D2D1::RoundedRect(cardR, 6.0f, 6.0f), brush);
        brush->SetColor(FcpColor(0xE2E8F0));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(cardR, 6.0f, 6.0f), brush, 1.0f);

        std::wstring line1 = L"\xD83D\xDCC1  " + m_currentEntry.name;
        std::wstring line2 = (pl ? L"Ścieżka: " : L"Path: ") + m_currentEntry.fullPath;
        std::wstring line3 = (pl ? L"Typ: " : L"Type: ") + m_currentEntry.typeName;
        std::wstring line4 = (pl ? L"Data modyfikacji: " : L"Date modified: ") + m_currentEntry.dateStr;
        std::wstring line5 = (pl ? L"Rozmiar: " : L"Size: ") + (m_currentEntry.sizeStr.empty() ? (pl ? L"(Kliknij Oblicz rozmiary folderów)" : L"(Click Calculate folder sizes)") : m_currentEntry.sizeStr);

        brush->SetColor(FcpColor(0x0F172A));
        rt->DrawTextW(line1.c_str(), static_cast<UINT32>(line1.size()), fmtBold,
            D2D1::RectF(cardR.left + 14.0f, cardR.top + 12.0f, cardR.right - 14.0f, cardR.top + 38.0f), brush);
        brush->SetColor(FcpColor(0x334155));
        rt->DrawTextW(line2.c_str(), static_cast<UINT32>(line2.size()), fmtSmall,
            D2D1::RectF(cardR.left + 14.0f, cardR.top + 44.0f, cardR.right - 14.0f, cardR.top + 68.0f), brush);
        rt->DrawTextW(line3.c_str(), static_cast<UINT32>(line3.size()), fmtSmall,
            D2D1::RectF(cardR.left + 14.0f, cardR.top + 72.0f, cardR.right - 14.0f, cardR.top + 96.0f), brush);
        rt->DrawTextW(line4.c_str(), static_cast<UINT32>(line4.size()), fmtSmall,
            D2D1::RectF(cardR.left + 14.0f, cardR.top + 100.0f, cardR.right - 14.0f, cardR.top + 124.0f), brush);
        rt->DrawTextW(line5.c_str(), static_cast<UINT32>(line5.size()), fmtSmall,
            D2D1::RectF(cardR.left + 14.0f, cardR.top + 128.0f, cardR.right - 14.0f, cardR.top + 152.0f), brush);
        break;
    }

    case ResolvedContentKind::NotepadEditor:
        m_notepad.Draw(rt, brush, fmtRegular, fmtSmall, fmtBold, m_bodyRect, lang, mousePt);
        break;

    case ResolvedContentKind::HexInspector:
        m_hex.Draw(rt, brush, fmtRegular, fmtSmall, fmtBold, m_bodyRect, lang, mousePt);
        break;

    case ResolvedContentKind::Model3DViewport:
        m_model3D.Draw(rt, brush, fmtRegular, fmtSmall, fmtBold, m_bodyRect, lang, mousePt);
        break;

    case ResolvedContentKind::ImagePreview: {
        D2D1_RECT_F imgArea = D2D1::RectF(m_bodyRect.left + 12.0f, m_bodyRect.top + 12.0f, m_bodyRect.right - 12.0f, std::max(m_bodyRect.top + 40.0f, m_bodyRect.bottom - 52.0f));
        FullImageMeta meta = imageCache.GetOrQueueFullImage(rt, m_currentEntry.fullPath, L"", L"");
        ID2D1Bitmap* bmp = meta.bitmap ? meta.bitmap : imageCache.GetOrQueueThumbnail(rt, m_currentEntry.fullPath);
        if (bmp) {
            D2D1_SIZE_F sz = bmp->GetSize();
            D2D1_RECT_F dr = PathUtils::FitRectCentered(imgArea, sz.width, sz.height);
            rt->DrawBitmap(bmp, dr);
        }

        D2D1_RECT_F botBar = D2D1::RectF(m_bodyRect.left, m_bodyRect.bottom - 42.0f, m_bodyRect.right, m_bodyRect.bottom);
        brush->SetColor(FcpColor(0xF8FAFC));
        rt->FillRectangle(botBar, brush);
        brush->SetColor(FcpColor(0xE2E8F0));
        rt->DrawLine(D2D1::Point2F(botBar.left, botBar.top), D2D1::Point2F(botBar.right, botBar.top), brush, 1.0f);

        D2D1_RECT_F toolBtn = D2D1::RectF(botBar.left + 10.0f, botBar.top + 7.0f, std::min(botBar.right - 10.0f, botBar.left + 178.0f), botBar.bottom - 7.0f);
        m_headerHits.push_back({ HBTN_IMG_TOOLS, toolBtn });
        bool hovTool = (m_hoverHeaderBtn == HBTN_IMG_TOOLS);
        brush->SetColor(hovTool ? FcpColor(0xDBEAFE) : FcpColor(0xEFF6FF));
        rt->FillRoundedRectangle(D2D1::RoundedRect(toolBtn, 3.5f, 3.5f), brush);
        brush->SetColor(hovTool ? FcpColor(0x0067B8) : FcpColor(0x93C5FD));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(toolBtn, 3.5f, 3.5f), brush, 1.0f);
        brush->SetColor(FcpColor(0x1D4ED8));
        const wchar_t* toolLbl = pl ? L"\xD83D\xDCD0 Narzędzia obrazu \x25BE" : L"\xD83D\xDCD0 Image Tools \x25BE";
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(toolLbl, static_cast<UINT32>(wcslen(toolLbl)), fmtSmall, toolBtn, brush);
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        if (meta.origWidth > 0 && meta.origHeight > 0) {
            wchar_t dimBuf[96] = {};
            swprintf_s(dimBuf, L"%u \xD7 %u px   \x2022   %s", meta.origWidth, meta.origHeight, m_currentEntry.sizeStr.c_str());
            D2D1_RECT_F dimR = D2D1::RectF(toolBtn.right + 10.0f, toolBtn.top, botBar.right - 10.0f, toolBtn.bottom);
            brush->SetColor(FcpColor(0x475569));
            rt->DrawTextW(dimBuf, static_cast<UINT32>(wcslen(dimBuf)), fmtSmall, dimR, brush);
        }
        break;
    }

    case ResolvedContentKind::MediaPreview: {
        D2D1_RECT_F imgArea = D2D1::RectF(m_bodyRect.left + 12.0f, m_bodyRect.top + 12.0f, m_bodyRect.right - 12.0f, std::max(m_bodyRect.top + 40.0f, m_bodyRect.bottom - 56.0f));
        if (mediaPlayer.HasMedia()) {
            if (m_currentEntry.isVideo && mediaPlayer.HasVideo()) {
                uint32_t vw = 0, vh = 0;
                ID2D1Bitmap* vbmp = mediaPlayer.GetCurrentVideoFrame(rt, vw, vh);
                if (!vbmp) vbmp = imageCache.GetOrQueueThumbnail(rt, m_currentEntry.fullPath);
                if (vbmp) {
                    D2D1_SIZE_F sz = vbmp->GetSize();
                    D2D1_RECT_F dr = PathUtils::FitRectCentered(imgArea, sz.width, sz.height);
                    rt->DrawBitmap(vbmp, dr);
                }
            } else {
                brush->SetColor(FcpColor(0x1E293B));
                rt->FillRoundedRectangle(D2D1::RoundedRect(imgArea, 6.0f, 6.0f), brush);
                brush->SetColor(FcpColor(0xF8FAFC));
                std::wstring noteLbl = L"\x266B  " + m_currentEntry.name;
                rt->DrawTextW(noteLbl.c_str(), static_cast<UINT32>(noteLbl.size()), fmtCenterSmall, imgArea, brush);
            }

            D2D1_RECT_F playBtn = D2D1::RectF(m_bodyRect.left + 14.0f, m_bodyRect.bottom - 42.0f, m_bodyRect.left + 44.0f, m_bodyRect.bottom - 16.0f);
            m_headerHits.push_back({ HBTN_MEDIA_PLAY_PAUSE, playBtn });
            brush->SetColor(FcpColor(0x0078D7));
            rt->FillRoundedRectangle(D2D1::RoundedRect(playBtn, 3.0f, 3.0f), brush);
            brush->SetColor(FcpColor(0xFFFFFF));
            fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            rt->DrawTextW(mediaPlayer.IsPlaying() ? L"\x23F8" : L"\x25B6", 1, fmtSmall, playBtn, brush);
            fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

            double pos = mediaPlayer.GetCurrentTimeSec();
            double dur = mediaPlayer.GetDurationSec();
            float frac = (dur > 0.05) ? static_cast<float>(std::clamp(pos / dur, 0.0, 1.0)) : 0.0f;
            outMediaSeekBarRect = D2D1::RectF(playBtn.right + 10.0f, playBtn.top + 4.0f, m_bodyRect.right - 14.0f, playBtn.bottom - 4.0f);
            float ty = (outMediaSeekBarRect.top + outMediaSeekBarRect.bottom) * 0.5f;
            brush->SetColor(FcpColor(0xCBD5E1));
            rt->FillRectangle(D2D1::RectF(outMediaSeekBarRect.left, ty - 2.0f, outMediaSeekBarRect.right, ty + 2.0f), brush);
            float fx = outMediaSeekBarRect.left + frac * std::max(0.0f, outMediaSeekBarRect.right - outMediaSeekBarRect.left);
            brush->SetColor(FcpColor(0x0078D7));
            rt->FillRectangle(D2D1::RectF(outMediaSeekBarRect.left, ty - 2.0f, fx, ty + 2.0f), brush);
        }
        break;
    }
    }
}

bool FileContentPane::OnMouseDown(
    float x,
    float y,
    bool rightButton,
    Lang lang,
    bool* outCloseRequested,
    int* outTriggerCmdId,
    std::wstring* outStatusToast)
{
    if (outCloseRequested) *outCloseRequested = false;
    if (outTriggerCmdId) *outTriggerCmdId = 0;

    if (!PathUtils::PtInRectF(x, y, m_paneRect)) {
        return false;
    }

    if (!rightButton) {
        for (const auto& hit : m_headerHits) {
            if (PathUtils::PtInRectF(x, y, hit.rect)) {
                switch (hit.id) {
                case HBTN_CLOSE_PANE:
                    if (outCloseRequested) *outCloseRequested = true;
                    return true;
                case HBTN_TAB_AUTO:
                    m_userTabMode = FileContentTabMode::Auto;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_TAB_NOTEPAD:
                    m_userTabMode = FileContentTabMode::Notepad;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_TAB_HEX:
                    m_userTabMode = FileContentTabMode::Hex;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_TAB_3D:
                    m_userTabMode = FileContentTabMode::Model3D;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_IMG_TOOLS:
                    if (outTriggerCmdId) *outTriggerCmdId = CMD_IMG_TOOL_MENU;
                    return true;
                case HBTN_MEDIA_PLAY_PAUSE:
                    if (outTriggerCmdId) *outTriggerCmdId = CMD_MEDIA_PLAY_PAUSE;
                    return true;
                }
            }
        }
    }

    switch (m_resolvedKind) {
    case ResolvedContentKind::NotepadEditor:
        if (!rightButton && m_notepad.OnMouseDown(x, y, lang, outStatusToast)) return true;
        break;
    case ResolvedContentKind::HexInspector:
        if (!rightButton && m_hex.OnMouseDown(x, y, lang, outStatusToast)) return true;
        break;
    case ResolvedContentKind::Model3DViewport:
        if (m_model3D.OnMouseDown(x, y, rightButton, lang, outStatusToast)) return true;
        break;
    default:
        break;
    }

    return true; // Consume click inside the right-side FileContentPane so it doesn't deselect the file in the list!
}

bool FileContentPane::OnMouseMove(float x, float y, WPARAM keyFlags) {
    bool changed = false;
    int newHov = 0;
    for (const auto& hit : m_headerHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newHov = hit.id;
            break;
        }
    }
    if (newHov != m_hoverHeaderBtn) {
        m_hoverHeaderBtn = newHov;
        changed = true;
    }

    switch (m_resolvedKind) {
    case ResolvedContentKind::NotepadEditor:
        if (m_notepad.OnMouseMove(x, y)) changed = true;
        break;
    case ResolvedContentKind::HexInspector:
        if (m_hex.OnMouseMove(x, y)) changed = true;
        break;
    case ResolvedContentKind::Model3DViewport:
        if (m_model3D.OnMouseMove(x, y, keyFlags)) changed = true;
        break;
    default:
        break;
    }
    return changed;
}

bool FileContentPane::OnMouseUp() {
    bool changed = false;
    if (m_hex.OnMouseUp()) changed = true;
    if (m_model3D.OnMouseUp()) changed = true;
    return changed;
}

bool FileContentPane::OnMouseWheel(short delta, float x, float y) {
    if (!PathUtils::PtInRectF(x, y, m_paneRect)) return false;
    if (m_resolvedKind == ResolvedContentKind::HexInspector) {
        return m_hex.OnMouseWheel(delta, x, y);
    }
    if (m_resolvedKind == ResolvedContentKind::Model3DViewport) {
        return m_model3D.OnMouseWheel(delta, x, y);
    }
    return false;
}

} // namespace Explorer2
