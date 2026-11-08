#include "FileContentPane.h"
#include "CommandIds.h"
#include "PathUtils.h"
#include "FileEngine.h"
#include "FtpClient.h"
#include <shellapi.h>
#include <algorithm>
#include <cmath>

namespace Explorer2 {

static D2D1_COLOR_F FcpColor(uint32_t rgb, float a = 1.0f) {
    return PathUtils::ColorFromRgb(rgb, a);
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
    m_notepad.Initialize(parentHwnd, hInstance, dwriteFactory);
    m_hex.Initialize(parentHwnd, dwriteFactory);
    m_model3D.Initialize(parentHwnd, d2dFactory);
    m_eda.Initialize(parentHwnd, d2dFactory);
    m_office.Initialize(parentHwnd);
    m_archive.Initialize(parentHwnd);
}

void FileContentPane::Shutdown() {
    m_notepad.Shutdown();
    m_hex.Shutdown();
    m_model3D.Shutdown();
    m_eda.Shutdown();
    m_office.Shutdown();
    m_archive.Shutdown();
}

void FileContentPane::HideNativeControls() {
    m_notepad.HideWindow();
}

void FileContentPane::LoadDirectoryPreview(const std::wstring& folderPath, bool forceReload) {
    if (!forceReload && m_dirPreviewPath == folderPath && !folderPath.empty()) {
        return;
    }
    m_dirPreviewPath = folderPath;
    m_dirPreviewItems.clear();
    m_dirFolderCount = 0;
    m_dirFileCount = 0;
    m_dirTotalFileBytes = 0;
    m_dirScrollY = 0.0f;
    m_dirSelectedIdx = -1;
    m_dirHoverIdx = -1;

    if (folderPath.empty() || folderPath == L"THIS_PC") return;

    std::wstring searchPattern = PathUtils::EnsureTrailingSlash(folderPath) + L"*";
    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileExW(
        searchPattern.c_str(),
        FindExInfoBasic,
        &fd,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == L'.') {
            if (fd.cFileName[1] == L'\0' || (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0')) {
                continue;
            }
        }
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        uint64_t sz = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        if (isDir) {
            ++m_dirFolderCount;
        } else {
            ++m_dirFileCount;
            m_dirTotalFileBytes += sz;
        }

        if (m_dirPreviewItems.size() < 2500) {
            DirPreviewEntry item;
            item.name = fd.cFileName;
            item.fullPath = PathUtils::EnsureTrailingSlash(folderPath) + item.name;
            item.isDirectory = isDir;
            item.size = sz;
            if (!isDir) {
                item.ext = PathUtils::GetExtension(item.name);
                item.isImage = IsImageExtension(item.ext);
                item.sizeStr = FileEngine::FormatSizeKB(sz);
            }
            item.dateStr = FileEngine::FormatFileTime(fd.ftLastWriteTime);
            m_dirPreviewItems.push_back(std::move(item));
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    std::sort(m_dirPreviewItems.begin(), m_dirPreviewItems.end(),
        [](const DirPreviewEntry& a, const DirPreviewEntry& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
            return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });
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
    if (m_userTabMode == FileContentTabMode::EdaPcb) {
        return ResolvedContentKind::EdaViewport;
    }
    if (m_userTabMode == FileContentTabMode::Office) {
        return ResolvedContentKind::OfficeDocument;
    }
    if (m_userTabMode == FileContentTabMode::Archive) {
        return ResolvedContentKind::ArchiveSubExplorer;
    }

    // Auto detection based on file type & content
    const std::wstring& ext = m_currentEntry.ext;
    if (EdaViewer::IsEdaExtension(ext)) {
        return ResolvedContentKind::EdaViewport;
    }
    if (Model3DViewer::Is3DModelExtension(ext)) {
        return ResolvedContentKind::Model3DViewport;
    }
    if (SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(ext)) {
        return ResolvedContentKind::OfficeDocument;
    }
    if (ArchiveExplorer::IsArchiveExtension(ext)) {
        return ResolvedContentKind::ArchiveSubExplorer;
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

    if (!m_hasEntry) return;

    if (m_currentEntry.isDirectory || m_currentEntry.isDrive) {
        LoadDirectoryPreview(m_currentEntry.fullPath, false);
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
    case ResolvedContentKind::EdaViewport:
        m_eda.LoadFile(m_currentEntry.fullPath, false);
        break;
    case ResolvedContentKind::OfficeDocument:
        m_office.LoadFile(m_currentEntry.fullPath, false);
        break;
    case ResolvedContentKind::ArchiveSubExplorer:
        m_archive.LoadFile(m_currentEntry.fullPath, false);
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
        m_inspectingChildOfFolder = false;
        m_resolvedKind = ResolvedContentKind::None;
        HideNativeControls();
        return;
    }

    bool changedPath = (!m_hasEntry ||
                        (m_inspectingChildOfFolder ? m_rootFolderEntry.fullPath : m_currentEntry.fullPath) != entry->fullPath);
    m_hasEntry = true;
    if (changedPath) {
        m_currentEntry = *entry;
        m_rootFolderEntry = *entry;
        m_inspectingChildOfFolder = false;
        m_userTabMode = FileContentTabMode::Auto;

        // If the selected entry is a virtual file inside an archive (e.g. C:\path\test.zip\src\main.cpp)
        // or a remote FTP file (ftp://...), extract/download it to a temp preview file so all viewers work seamlessly!
        if (!m_currentEntry.isDirectory && !m_currentEntry.isDrive) {
            if (FtpClient::IsFtpPath(m_currentEntry.fullPath)) {
                std::wstring tempFile;
                if (FtpClient::DownloadFtpFileToTempCache(m_currentEntry.fullPath, tempFile)) {
                    m_currentEntry.fullPath = tempFile;
                }
            } else {
                std::wstring arcFile, innerPath;
                if (ArchiveExplorer::SplitVirtualArchivePath(m_currentEntry.fullPath, arcFile, innerPath) && !innerPath.empty()) {
                    std::wstring tempFile;
                    if (ArchiveExplorer::ExtractSingleEntryToTempFile(arcFile, innerPath, tempFile)) {
                        m_currentEntry.fullPath = tempFile;
                    }
                }
            }
        }
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
        bool isArchiveExt = ArchiveExplorer::IsArchiveExtension(m_currentEntry.ext);
        if (isArchiveExt || m_resolvedKind == ResolvedContentKind::ArchiveSubExplorer) {
            drawTabPill(HBTN_TAB_ARCHIVE, pl ? 62.0f : 54.0f, pl ? L"Archiwum" : L"Archive", m_resolvedKind == ResolvedContentKind::ArchiveSubExplorer);
        }
        bool isOfficeExt = SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(m_currentEntry.ext);
        if (isOfficeExt || m_resolvedKind == ResolvedContentKind::OfficeDocument) {
            drawTabPill(HBTN_TAB_OFFICE, pl ? 58.0f : 52.0f, pl ? L"Tabela/Doc" : L"Sheet/Doc", m_resolvedKind == ResolvedContentKind::OfficeDocument);
        }
        bool isEdaExt = EdaViewer::IsEdaExtension(m_currentEntry.ext);
        if (isEdaExt || m_resolvedKind == ResolvedContentKind::EdaViewport) {
            drawTabPill(HBTN_TAB_EDA, 52.0f, L"PCB/EDA", m_resolvedKind == ResolvedContentKind::EdaViewport);
        }
        bool is3DExt = Model3DViewer::Is3DModelExtension(m_currentEntry.ext);
        if (is3DExt || m_resolvedKind == ResolvedContentKind::Model3DViewport) {
            drawTabPill(HBTN_TAB_3D, 40.0f, L"3D", m_resolvedKind == ResolvedContentKind::Model3DViewport);
        }
        drawTabPill(HBTN_TAB_HEX,     44.0f, L"Hex", m_resolvedKind == ResolvedContentKind::HexInspector);
        drawTabPill(HBTN_TAB_NOTEPAD, pl ? 64.0f : 56.0f, pl ? L"Notatnik" : L"Text", m_resolvedKind == ResolvedContentKind::NotepadEditor);
        drawTabPill(HBTN_TAB_AUTO,    44.0f, L"Auto", m_userTabMode == FileContentTabMode::Auto);
        if (m_inspectingChildOfFolder) {
            bool fromArc = ArchiveExplorer::IsArchiveExtension(m_rootFolderEntry.ext);
            drawTabPill(HBTN_DIR_BACK_TO_FOLDER, pl ? 78.0f : 72.0f,
                fromArc ? (pl ? L"\x2190 Archiwum" : L"\x2190 Archive") : (pl ? L"\x2190 Folder" : L"\x2190 Folder"), false);
        }
    } else if (m_hasEntry && (m_currentEntry.isDirectory || m_currentEntry.isDrive)) {
        drawTabPill(HBTN_DIR_ENTER, pl ? 98.0f : 92.0f, pl ? L"Otwórz folder \x2794" : L"Open folder \x2794", false);
        if (m_inspectingChildOfFolder) {
            drawTabPill(HBTN_DIR_BACK_TO_FOLDER, pl ? 68.0f : 62.0f, pl ? L"\x2191 Wstecz" : L"\x2191 Back", false);
        }
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
            ? L"Zaznacz dowolny plik lub folder na liście po lewej stronie,\naby zobaczyć jego zawartość (Notatnik, Hex, 3D, podgląd folderu lub multimedia)."
            : L"Select any file or folder in the list on the left\nto view its content (Notepad, Hex, 3D, Folder Sub-Explorer, or Media).";
        D2D1_RECT_F msgR = D2D1::RectF(m_bodyRect.left + 20.0f, m_bodyRect.top + 40.0f, m_bodyRect.right - 20.0f, m_bodyRect.bottom - 40.0f);
        brush->SetColor(FcpColor(0x64748B));
        rt->DrawTextW(emptyMsg.c_str(), static_cast<UINT32>(emptyMsg.size()), fmtCenterSmall, msgR, brush);
        break;
    }

    case ResolvedContentKind::DirectoryInfo: {
        // Folder Summary Banner (30px) + Column Header (22px) + Scrollable Folder Content List
        float bannerH = 30.0f;
        float colHdrH = 22.0f;
        D2D1_RECT_F bannerR = D2D1::RectF(m_bodyRect.left, m_bodyRect.top, m_bodyRect.right, m_bodyRect.top + bannerH);
        brush->SetColor(FcpColor(0xF8FAFC));
        rt->FillRectangle(bannerR, brush);
        brush->SetColor(FcpColor(0xE2E8F0));
        rt->DrawLine(D2D1::Point2F(bannerR.left, bannerR.bottom - 0.5f), D2D1::Point2F(bannerR.right, bannerR.bottom - 0.5f), brush, 1.0f);

        wchar_t sumBuf[256] = {};
        std::wstring bytesStr = PathUtils::FormatByteSize(m_dirTotalFileBytes);
        swprintf_s(sumBuf,
            pl ? L"Zawartość: %zu folderów, %zu plików   \x2022   Rozmiar plików: %s   (2\xD7klik = otwórz)"
               : L"Contents: %zu folders, %zu files   \x2022   Files size: %s   (double-click = open)",
            m_dirFolderCount, m_dirFileCount, bytesStr.c_str());
        brush->SetColor(FcpColor(0x334155));
        rt->DrawTextW(sumBuf, static_cast<UINT32>(wcslen(sumBuf)), fmtSmall,
            D2D1::RectF(bannerR.left + 10.0f, bannerR.top, bannerR.right - 10.0f, bannerR.bottom), brush);

        D2D1_RECT_F colR = D2D1::RectF(m_bodyRect.left, bannerR.bottom, m_bodyRect.right, bannerR.bottom + colHdrH);
        brush->SetColor(FcpColor(0xF1F5F9));
        rt->FillRectangle(colR, brush);
        brush->SetColor(FcpColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(colR.left, colR.bottom - 0.5f), D2D1::Point2F(colR.right, colR.bottom - 0.5f), brush, 1.0f);

        float scrollW = 12.0f;
        float listRight = m_bodyRect.right - scrollW;
        float sizeColW = 78.0f;
        float dateColW = 108.0f;
        float nameColRight = std::max(colR.left + 80.0f, listRight - sizeColW - dateColW);

        brush->SetColor(FcpColor(0x475569));
        rt->DrawTextW(pl ? L"Nazwa" : L"Name", pl ? 5 : 4, fmtBold,
            D2D1::RectF(colR.left + 10.0f, colR.top, nameColRight - 4.0f, colR.bottom), brush);
        if (nameColRight + dateColW <= listRight) {
            rt->DrawTextW(pl ? L"Data modyfikacji" : L"Date modified", pl ? 16 : 13, fmtBold,
                D2D1::RectF(nameColRight + 4.0f, colR.top, nameColRight + dateColW - 4.0f, colR.bottom), brush);
        }
        fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        rt->DrawTextW(pl ? L"Rozmiar" : L"Size", pl ? 7 : 4, fmtBold,
            D2D1::RectF(listRight - sizeColW, colR.top, listRight - 8.0f, colR.bottom), brush);
        fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        m_dirListRect = D2D1::RectF(m_bodyRect.left, colR.bottom, listRight, m_bodyRect.bottom);
        float rowH = 22.0f;
        float listH = std::max(1.0f, m_dirListRect.bottom - m_dirListRect.top);
        float totalContentH = static_cast<float>(m_dirPreviewItems.size()) * rowH;
        float maxScroll = std::max(0.0f, totalContentH - listH);
        m_dirScrollY = std::clamp(m_dirScrollY, 0.0f, maxScroll);

        if (m_dirPreviewItems.empty()) {
            const wchar_t* emptyDirLbl = pl ? L"(Ten folder jest pusty)" : L"(This folder is empty)";
            brush->SetColor(FcpColor(0x94A3B8));
            rt->DrawTextW(emptyDirLbl, static_cast<UINT32>(wcslen(emptyDirLbl)), fmtCenterSmall, m_dirListRect, brush);
        } else {
            rt->PushAxisAlignedClip(m_dirListRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            int firstIdx = std::max(0, static_cast<int>(m_dirScrollY / rowH));
            int lastIdx = std::min(static_cast<int>(m_dirPreviewItems.size()) - 1,
                                   static_cast<int>((m_dirScrollY + listH) / rowH) + 1);
            for (int i = firstIdx; i <= lastIdx; ++i) {
                const auto& item = m_dirPreviewItems[i];
                float ry = m_dirListRect.top + i * rowH - m_dirScrollY;
                D2D1_RECT_F rowR = D2D1::RectF(m_dirListRect.left, ry, m_dirListRect.right, ry + rowH);

                if (i == m_dirSelectedIdx) {
                    brush->SetColor(FcpColor(0xCCE8FF));
                    rt->FillRectangle(rowR, brush);
                    brush->SetColor(FcpColor(0x0078D7));
                    rt->DrawRectangle(D2D1::RectF(rowR.left + 0.5f, rowR.top + 0.5f, rowR.right - 0.5f, rowR.bottom - 0.5f), brush, 1.0f);
                } else if (i == m_dirHoverIdx) {
                    brush->SetColor(FcpColor(0xE5F3FF));
                    rt->FillRectangle(rowR, brush);
                } else if (i % 2 == 1) {
                    brush->SetColor(FcpColor(0xF8FAFC));
                    rt->FillRectangle(rowR, brush);
                }

                // Draw small folder or file badge
                D2D1_RECT_F icoR = D2D1::RectF(rowR.left + 8.0f, rowR.top + 4.0f, rowR.left + 21.0f, rowR.bottom - 4.0f);
                if (item.isDirectory) {
                    brush->SetColor(FcpColor(0xF59E0B));
                    rt->FillRoundedRectangle(D2D1::RoundedRect(icoR, 2.0f, 2.0f), brush);
                } else {
                    brush->SetColor(item.isImage ? FcpColor(0x10B981) : FcpColor(0x3B82F6));
                    rt->DrawRoundedRectangle(D2D1::RoundedRect(icoR, 1.5f, 1.5f), brush, 1.2f);
                }

                brush->SetColor(FcpColor(0x0F172A));
                rt->DrawTextW(item.name.c_str(), static_cast<UINT32>(item.name.size()), fmtRegular,
                    D2D1::RectF(rowR.left + 26.0f, rowR.top, nameColRight - 4.0f, rowR.bottom), brush);

                if (nameColRight + dateColW <= listRight) {
                    brush->SetColor(FcpColor(0x64748B));
                    rt->DrawTextW(item.dateStr.c_str(), static_cast<UINT32>(item.dateStr.size()), fmtSmall,
                        D2D1::RectF(nameColRight + 4.0f, rowR.top, nameColRight + dateColW - 4.0f, rowR.bottom), brush);
                }

                if (!item.isDirectory && !item.sizeStr.empty()) {
                    brush->SetColor(FcpColor(0x475569));
                    fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                    rt->DrawTextW(item.sizeStr.c_str(), static_cast<UINT32>(item.sizeStr.size()), fmtSmall,
                        D2D1::RectF(listRight - sizeColW, rowR.top, listRight - 8.0f, rowR.bottom), brush);
                    fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                }
            }
            rt->PopAxisAlignedClip();
        }

        // Scrollbar for Folder Content View
        m_dirScrollTrackRect = D2D1::RectF(listRight, colR.bottom, m_bodyRect.right, m_bodyRect.bottom);
        brush->SetColor(FcpColor(0xF1F5F9));
        rt->FillRectangle(m_dirScrollTrackRect, brush);
        if (totalContentH > listH) {
            float thumbH = std::max(24.0f, listH * (listH / totalContentH));
            float frac = (maxScroll > 0.0f) ? (m_dirScrollY / maxScroll) : 0.0f;
            float thumbTop = m_dirScrollTrackRect.top + frac * (listH - thumbH);
            m_dirScrollThumbRect = D2D1::RectF(m_dirScrollTrackRect.left + 2.0f, thumbTop, m_dirScrollTrackRect.right - 2.0f, thumbTop + thumbH);
            brush->SetColor(m_dirDraggingScroll ? FcpColor(0x0078D7) : FcpColor(0x94A3B8));
            rt->FillRoundedRectangle(D2D1::RoundedRect(m_dirScrollThumbRect, 3.0f, 3.0f), brush);
        } else {
            m_dirScrollThumbRect = {};
        }
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

    case ResolvedContentKind::EdaViewport:
        m_eda.Draw(rt, brush, fmtRegular, fmtSmall, fmtBold, m_bodyRect, lang, mousePt);
        break;

    case ResolvedContentKind::OfficeDocument:
        m_office.Draw(rt, brush, fmtRegular, fmtSmall, fmtBold, m_bodyRect, lang, mousePt);
        break;

    case ResolvedContentKind::ArchiveSubExplorer:
        m_archive.Draw(rt, brush, fmtRegular, fmtSmall, fmtBold, m_bodyRect, lang, mousePt);
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
                case HBTN_TAB_EDA:
                    m_userTabMode = FileContentTabMode::EdaPcb;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_TAB_OFFICE:
                    m_userTabMode = FileContentTabMode::Office;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_TAB_ARCHIVE:
                    m_userTabMode = FileContentTabMode::Archive;
                    ActivateResolvedViewer();
                    return true;
                case HBTN_IMG_TOOLS:
                    if (outTriggerCmdId) *outTriggerCmdId = CMD_IMG_TOOL_MENU;
                    return true;
                case HBTN_MEDIA_PLAY_PAUSE:
                    if (outTriggerCmdId) *outTriggerCmdId = CMD_MEDIA_PLAY_PAUSE;
                    return true;
                case HBTN_DIR_ENTER:
                    if (!m_currentEntry.fullPath.empty()) {
                        m_pendingNavigatePath = m_currentEntry.fullPath;
                    }
                    return true;
                case HBTN_DIR_BACK_TO_FOLDER:
                    m_currentEntry = m_rootFolderEntry;
                    m_inspectingChildOfFolder = false;
                    m_userTabMode = FileContentTabMode::Auto;
                    ActivateResolvedViewer();
                    return true;
                }
            }
        }
    }

    switch (m_resolvedKind) {
    case ResolvedContentKind::DirectoryInfo: {
        if (!rightButton && PathUtils::PtInRectF(x, y, m_dirScrollTrackRect)) {
            if (PathUtils::PtInRectF(x, y, m_dirScrollThumbRect)) {
                m_dirDraggingScroll = true;
                m_dirScrollDragOffset = y - m_dirScrollThumbRect.top;
            } else {
                float rowH = 22.0f;
                float listH = std::max(1.0f, m_dirListRect.bottom - m_dirListRect.top);
                float maxScroll = std::max(0.0f, static_cast<float>(m_dirPreviewItems.size()) * rowH - listH);
                float frac = std::clamp((y - m_dirScrollTrackRect.top) / listH, 0.0f, 1.0f);
                m_dirScrollY = frac * maxScroll;
            }
            return true;
        }
        if (!rightButton && PathUtils::PtInRectF(x, y, m_dirListRect)) {
            float rowH = 22.0f;
            int idx = static_cast<int>((y - m_dirListRect.top + m_dirScrollY) / rowH);
            if (idx >= 0 && idx < static_cast<int>(m_dirPreviewItems.size())) {
                ULONGLONG now = GetTickCount64();
                bool isDbl = (idx == m_dirLastClickIdx && (now - m_dirLastClickTick) <= GetDoubleClickTime());
                m_dirSelectedIdx = idx;
                m_dirLastClickIdx = idx;
                m_dirLastClickTick = now;

                if (isDbl) {
                    const auto& item = m_dirPreviewItems[idx];
                    if (item.isDirectory) {
                        // Enter subfolder inside the right preview pane (and user can also click [Otwórz folder ->] to navigate main explorer)
                        m_currentEntry.name = item.name;
                        m_currentEntry.fullPath = item.fullPath;
                        m_currentEntry.isDirectory = true;
                        m_inspectingChildOfFolder = true;
                        ActivateResolvedViewer();
                    } else {
                        // Preview the file right inside the File Content Pane (with [<- Folder] button to return!)
                        FileEntry childEntry = {};
                        childEntry.name = item.name;
                        childEntry.displayName = item.name;
                        childEntry.fullPath = item.fullPath;
                        childEntry.ext = item.ext;
                        childEntry.size = item.size;
                        childEntry.sizeStr = item.sizeStr;
                        childEntry.dateStr = item.dateStr;
                        childEntry.isDirectory = false;
                        childEntry.isImage = item.isImage;
                        childEntry.isVideo = IsVideoExtension(item.ext);
                        childEntry.isAudio = IsAudioExtension(item.ext);
                        m_currentEntry = std::move(childEntry);
                        m_inspectingChildOfFolder = true;
                        m_userTabMode = FileContentTabMode::Auto;
                        ActivateResolvedViewer();
                    }
                }
            }
            return true;
        }
        break;
    }
    case ResolvedContentKind::NotepadEditor:
        if (!rightButton && m_notepad.OnMouseDown(x, y, lang, outStatusToast)) return true;
        break;
    case ResolvedContentKind::HexInspector:
        if (!rightButton && m_hex.OnMouseDown(x, y, lang, outStatusToast)) return true;
        break;
    case ResolvedContentKind::Model3DViewport:
        if (m_model3D.OnMouseDown(x, y, rightButton, lang, outStatusToast)) return true;
        break;
    case ResolvedContentKind::EdaViewport:
        if (m_eda.OnMouseDown(x, y, rightButton, lang, outStatusToast)) return true;
        break;
    case ResolvedContentKind::OfficeDocument:
        if (m_office.OnMouseDown(x, y, rightButton, lang, outStatusToast)) return true;
        break;
    case ResolvedContentKind::ArchiveSubExplorer: {
        if (m_archive.OnMouseDown(x, y, rightButton, lang, outStatusToast)) {
            std::wstring navArc = m_archive.TakePendingNavigateArchivePath();
            if (!navArc.empty()) {
                m_pendingNavigatePath = navArc;
            }
            std::wstring prevTemp = m_archive.TakePendingPreviewFilePath();
            if (!prevTemp.empty()) {
                FileEntry childEntry = {};
                childEntry.name = PathUtils::GetFileName(prevTemp);
                childEntry.displayName = childEntry.name;
                childEntry.fullPath = prevTemp;
                childEntry.ext = PathUtils::GetExtension(prevTemp);
                WIN32_FILE_ATTRIBUTE_DATA fad = {};
                if (GetFileAttributesExW(prevTemp.c_str(), GetFileExInfoStandard, &fad)) {
                    childEntry.size = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                    childEntry.sizeStr = FileEngine::FormatSizeKB(childEntry.size);
                }
                childEntry.isDirectory = false;
                childEntry.isImage = IsImageExtension(childEntry.ext);
                childEntry.isVideo = IsVideoExtension(childEntry.ext);
                childEntry.isAudio = IsAudioExtension(childEntry.ext);
                m_currentEntry = std::move(childEntry);
                m_inspectingChildOfFolder = true;
                m_userTabMode = FileContentTabMode::Auto;
                ActivateResolvedViewer();
            }
            return true;
        }
        break;
    }
    default:
        break;
    }

    return true;
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
    case ResolvedContentKind::DirectoryInfo: {
        if (m_dirDraggingScroll) {
            float rowH = 22.0f;
            float listH = std::max(1.0f, m_dirListRect.bottom - m_dirListRect.top);
            float maxScroll = std::max(0.0f, static_cast<float>(m_dirPreviewItems.size()) * rowH - listH);
            float thumbH = m_dirScrollThumbRect.bottom - m_dirScrollThumbRect.top;
            float usable = std::max(1.0f, listH - thumbH);
            float frac = std::clamp((y - m_dirScrollDragOffset - m_dirScrollTrackRect.top) / usable, 0.0f, 1.0f);
            m_dirScrollY = frac * maxScroll;
            return true;
        }
        int newRowHov = -1;
        if (PathUtils::PtInRectF(x, y, m_dirListRect)) {
            int idx = static_cast<int>((y - m_dirListRect.top + m_dirScrollY) / 22.0f);
            if (idx >= 0 && idx < static_cast<int>(m_dirPreviewItems.size())) {
                newRowHov = idx;
            }
        }
        if (newRowHov != m_dirHoverIdx) {
            m_dirHoverIdx = newRowHov;
            changed = true;
        }
        break;
    }
    case ResolvedContentKind::NotepadEditor:
        if (m_notepad.OnMouseMove(x, y)) changed = true;
        break;
    case ResolvedContentKind::HexInspector:
        if (m_hex.OnMouseMove(x, y)) changed = true;
        break;
    case ResolvedContentKind::Model3DViewport:
        if (m_model3D.OnMouseMove(x, y, keyFlags)) changed = true;
        break;
    case ResolvedContentKind::EdaViewport:
        if (m_eda.OnMouseMove(x, y, keyFlags)) changed = true;
        break;
    case ResolvedContentKind::OfficeDocument:
        if (m_office.OnMouseMove(x, y)) changed = true;
        break;
    case ResolvedContentKind::ArchiveSubExplorer:
        if (m_archive.OnMouseMove(x, y)) changed = true;
        break;
    default:
        break;
    }
    return changed;
}

bool FileContentPane::OnMouseUp() {
    bool changed = false;
    if (m_dirDraggingScroll) {
        m_dirDraggingScroll = false;
        changed = true;
    }
    if (m_notepad.OnMouseUp()) changed = true;
    if (m_hex.OnMouseUp()) changed = true;
    if (m_model3D.OnMouseUp()) changed = true;
    if (m_eda.OnMouseUp()) changed = true;
    if (m_office.OnMouseUp()) changed = true;
    if (m_archive.OnMouseUp()) changed = true;
    return changed;
}

bool FileContentPane::OnMouseWheel(short delta, float x, float y) {
    if (!PathUtils::PtInRectF(x, y, m_paneRect)) return false;
    if (m_resolvedKind == ResolvedContentKind::DirectoryInfo) {
        int notches = delta / WHEEL_DELTA;
        if (notches == 0) notches = (delta > 0) ? 1 : -1;
        float rowH = 22.0f;
        float listH = std::max(1.0f, m_dirListRect.bottom - m_dirListRect.top);
        float maxScroll = std::max(0.0f, static_cast<float>(m_dirPreviewItems.size()) * rowH - listH);
        m_dirScrollY = std::clamp(m_dirScrollY - notches * rowH * 3.0f, 0.0f, maxScroll);
        return true;
    }
    if (m_resolvedKind == ResolvedContentKind::NotepadEditor) {
        return m_notepad.OnMouseWheel(delta, x, y);
    }
    if (m_resolvedKind == ResolvedContentKind::HexInspector) {
        return m_hex.OnMouseWheel(delta, x, y);
    }
    if (m_resolvedKind == ResolvedContentKind::Model3DViewport) {
        return m_model3D.OnMouseWheel(delta, x, y);
    }
    if (m_resolvedKind == ResolvedContentKind::EdaViewport) {
        return m_eda.OnMouseWheel(delta, x, y);
    }
    if (m_resolvedKind == ResolvedContentKind::OfficeDocument) {
        return m_office.OnMouseWheel(delta, x, y);
    }
    if (m_resolvedKind == ResolvedContentKind::ArchiveSubExplorer) {
        return m_archive.OnMouseWheel(delta, x, y);
    }
    return false;
}

} // namespace Explorer2
