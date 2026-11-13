#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Types.h"
#include "Localization.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <vector>

namespace Explorer2 {

struct PdfDocumentSummary {
    std::wstring pdfVersion;     // e.g. L"1.7"
    std::wstring title;
    std::wstring author;
    std::wstring creator;
    std::wstring producer;
    uint32_t pageCount = 0;
    float pageWidthPt  = 595.0f; // Default A4 595 x 842 pt
    float pageHeightPt = 842.0f;
    bool isEncrypted   = false;
    bool valid         = false;
    std::vector<std::wstring> pageTexts; // Extracted text per page (0-indexed)
    std::wstring allExtractedText;
};

class PdfViewer {
public:
    PdfViewer() = default;
    ~PdfViewer();

    static bool IsPdfExtension(const std::wstring& extLower);

    // Fast native C++ PDF binary inspector (parses header, metadata, page count, MediaBox, and FlateDecode text streams)
    static bool InspectPdfFile(const std::wstring& fullPath, PdfDocumentSummary& outSummary);

    // Renders a specific 0-based page of a PDF file to a BGRA32 pixel buffer (via Windows.Data.Pdf on MTA thread)
    static bool RenderPdfPageToBgra(
        const std::wstring& fullPath,
        uint32_t pageIndex,
        uint32_t targetWidth,
        std::vector<uint8_t>& outBgraPixels,
        uint32_t& outWidth,
        uint32_t& outHeight,
        uint32_t* outTotalPages = nullptr);

    // Lets go of Windows' PDF library (Windows.Data.Pdf.dll), which RenderPdfPageToBgra loads on first use.
    // Has to be called before the process ends, once no more pages will be rendered. Left loaded, the
    // library frees its graphics device while the process is being torn down, which Windows answers with
    // exception 0x87A: the program then ends with an error although everything was saved and closed.
    // Does nothing when no page was rendered. Rendering again afterwards loads the library again.
    static void ReleaseRenderer();

    // One page as Windows' PDF library draws it for the view. Can be made on any thread; it waits for the
    // picture.
    struct PagePicture {
        uint32_t page = 0;              // 0-based page the picture shows
        std::vector<uint8_t> bgra;      // empty when Windows could not draw the page (the view shows its text)
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t pageCount = 0;         // pages as Windows counts them; 0 when it could not open the document
    };
    static void RenderPagePicture(const std::wstring& fullPath, uint32_t page, PagePicture& out);

    void Initialize(HWND parentHwnd);
    void Shutdown();

    void LoadFile(const std::wstring& fullPath, bool forceReload = false);
    void Clear();

    // A document read on another thread (InspectPdfFile, then RenderPagePicture of its first page) goes
    // into the view, which then shows what LoadFile would have shown.
    void ShowLoadedDocument(const std::wstring& fullPath, PdfDocumentSummary&& summary, PagePicture&& firstPage);
    // A page drawn on another thread. Ignored unless it is the page the view is on, of the document shown.
    void ShowPagePicture(const std::wstring& fullPath, PagePicture&& picture);

    // When set, turning a page does not draw the new page on the calling thread: the view keeps the picture
    // it has (with a note that the page is being drawn) until the new one arrives through ShowPagePicture.
    // The owner, which has the page drawn elsewhere, learns of the wish from TakePageRequest.
    void SetPagesDrawnByOwner(bool byOwner) { m_pagesDrawnByOwner = byOwner; }
    bool TakePageRequest(uint32_t& outPage);
    // The page the view is on has no picture yet (it was turned to, and its picture has not arrived)
    bool IsWaitingForPagePicture() const { return !m_loadedPath.empty() && m_summary.valid && m_picturePage != m_currentPage; }

    // Turns to the next (+1) or the previous (-1) page, as the page buttons do. False at either end.
    bool TurnPage(int direction);

    bool IsShowing(const std::wstring& fullPath) const { return !m_loadedPath.empty() && m_loadedPath == fullPath; }
    const std::wstring& LoadedPath() const { return m_loadedPath; }
    uint32_t PageCount() const { return m_summary.pageCount; }
    uint32_t CurrentPage() const { return m_currentPage; }
    uint32_t PicturePage() const { return m_picturePage; }
    uint32_t PictureWidth() const { return m_pageBmpW; }

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
    bool OnMouseMove(float x, float y, WPARAM keyFlags);
    bool OnMouseUp();
    bool OnMouseWheel(short delta, float x, float y);

private:
    void LoadCurrentPageBitmap();
    void ApplyPagePicture(PagePicture&& picture);
    void RequestCurrentPage();      // the page was turned: draw it now, or leave that to the owner

    enum BtnId : int {
        BTN_NONE = 0,
        BTN_PREV_PAGE = 1,
        BTN_NEXT_PAGE = 2,
        BTN_ZOOM_OUT  = 3,
        BTN_ZOOM_FIT  = 4,
        BTN_ZOOM_IN   = 5,
        BTN_TOGGLE_MODE = 6,
        BTN_COPY_TEXT = 7,
        BTN_OPEN_EXTERNAL = 8
    };

    struct HitRect {
        int id = 0;
        D2D1_RECT_F rect = {};
    };

    HWND m_parentHwnd = nullptr;
    std::wstring m_loadedPath;
    PdfDocumentSummary m_summary;

    uint32_t m_currentPage = 0; // 0-based
    bool m_textMode = false;    // false = Visual Page Render, true = Extracted Text & Metadata View
    float m_zoom = 1.0f;
    float m_panX = 0.0f;
    float m_panY = 0.0f;
    float m_textScrollY = 0.0f;

    bool m_draggingPan = false;
    POINT m_lastMousePt = {};
    int m_hoverBtnId = 0;

    bool m_pagesDrawnByOwner = false;
    bool m_pageRequested = false;
    uint32_t m_picturePage = 0xFFFFFFFFu;   // the page whose picture m_pageBgra holds

    // Rendered BGRA32 page buffer + cached Direct2D bitmap
    std::vector<uint8_t> m_pageBgra;
    uint32_t m_pageBmpW = 0;
    uint32_t m_pageBmpH = 0;
    uint32_t m_bitmapUploadedPage = 0xFFFFFFFFu;
    std::wstring m_bitmapUploadedPath;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_d2dPageBitmap;
    ID2D1RenderTarget* m_lastRenderTarget = nullptr;

    D2D1_RECT_F m_viewportRect = {};
    std::vector<HitRect> m_toolbarHits;
};

} // namespace Explorer2
