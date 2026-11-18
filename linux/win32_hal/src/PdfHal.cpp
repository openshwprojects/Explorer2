// linux/win32_hal/src/PdfHal.cpp
// Linux PDF Page Visual Rasterizer (Poppler pdftoppm + WIC PNG decode + pure-C++ fallback)

#include "PdfViewer.h"
#include "PathUtils.h"
#include "ToolProcess.h"
#include <wincodec.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>

namespace Explorer2::Hal {

namespace {

std::atomic<bool> g_pdfEngineActive{ false };
std::atomic<uint64_t> g_pdfTmpSeq{ 1 };

bool DecodePngBytesViaWic(const std::vector<uint8_t>& pngBytes, std::vector<uint8_t>& outBgra, uint32_t& outW, uint32_t& outH) {
    if (pngBytes.size() < 24) return false;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) || !wic) {
        return false;
    }
    IStream* memStream = SHCreateMemStream(pngBytes.data(), static_cast<UINT>(pngBytes.size()));
    if (!memStream) return false;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wic->CreateDecoderFromStream(memStream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    memStream->Release();
    if (FAILED(hr) || !decoder) return false;

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) return false;

    Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(&conv)) || !conv) return false;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        return false;
    }

    UINT w = 0, h = 0;
    if (FAILED(conv->GetSize(&w, &h)) || w == 0 || h == 0 || w > 8192 || h > 8192) return false;
    outBgra.resize(static_cast<size_t>(w) * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(outBgra.size()), outBgra.data()))) {
        outBgra.clear();
        return false;
    }
    outW = w;
    outH = h;
    return true;
}

void ResampleBgraExactWidth(
    const std::vector<uint8_t>& src, uint32_t srcW, uint32_t srcH,
    uint32_t dstW, std::vector<uint8_t>& dst, uint32_t& outH)
{
    if (srcW == dstW || srcW == 0 || srcH == 0) {
        dst = src;
        outH = srcH;
        return;
    }
    outH = std::max<uint32_t>(1u, static_cast<uint32_t>(std::lround(static_cast<double>(srcH) * dstW / srcW)));
    dst.resize(static_cast<size_t>(dstW) * outH * 4);
    for (uint32_t y = 0; y < outH; ++y) {
        const uint32_t sy = std::min<uint32_t>(srcH - 1, static_cast<uint32_t>((static_cast<uint64_t>(y) * srcH) / outH));
        for (uint32_t x = 0; x < dstW; ++x) {
            const uint32_t sx = std::min<uint32_t>(srcW - 1, static_cast<uint32_t>((static_cast<uint64_t>(x) * srcW) / dstW));
            std::memcpy(&dst[(static_cast<size_t>(y) * dstW + x) * 4],
                        &src[(static_cast<size_t>(sy) * srcW + sx) * 4], 4);
        }
    }
}

void RenderFallbackPdfSheet(
    const PdfDocumentSummary& summary,
    uint32_t actualPage,
    uint32_t targetWidth,
    std::vector<uint8_t>& outBgra,
    uint32_t& outW,
    uint32_t& outH)
{
    const double aspect = (summary.pageWidthPt > 10.0f && summary.pageHeightPt > 10.0f)
        ? (static_cast<double>(summary.pageHeightPt) / static_cast<double>(summary.pageWidthPt))
        : 1.4142;
    outW = std::clamp<uint32_t>(targetWidth, 32u, 4096u);
    outH = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(outW * aspect)), 32u, 4096u);
    outBgra.assign(static_cast<size_t>(outW) * outH * 4, 255); // Pure white page background

    auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || x >= static_cast<int>(outW) || y < 0 || y >= static_cast<int>(outH)) return;
        uint8_t* p = outBgra.data() + (static_cast<size_t>(y) * outW + x) * 4;
        p[0] = b; p[1] = g; p[2] = r; p[3] = 255;
    };

    // Subtle page frame + header rule
    for (uint32_t x = 0; x < outW; ++x) {
        put(static_cast<int>(x), 0, 210, 216, 224);
        put(static_cast<int>(x), static_cast<int>(outH - 1), 210, 216, 224);
    }
    for (uint32_t y = 0; y < outH; ++y) {
        put(0, static_cast<int>(y), 210, 216, 224);
        put(static_cast<int>(outW - 1), static_cast<int>(y), 210, 216, 224);
    }

    std::wstring body;
    if (actualPage < summary.pageTexts.size()) body = summary.pageTexts[actualPage];
    else body = summary.allExtractedText;
    if (body.empty()) body = L"PDF Document Page " + std::to_wstring(actualPage + 1);

    const int margin = std::max<int>(8, static_cast<int>(outW / 14));
    int cx = margin, cy = margin;
    const int cw = std::max(4, static_cast<int>(outW / 56));
    const int ch = std::max(6, cw * 2);
    for (wchar_t wc : body) {
        if (wc == L'\r') continue;
        if (wc == L'\n' || cx + cw > static_cast<int>(outW) - margin) {
            cx = margin;
            cy += ch + 4;
            if (cy + ch > static_cast<int>(outH) - margin) break;
            if (wc == L'\n') continue;
        }
        if (wc != L' ' && wc != L'\t') {
            uint32_t h = static_cast<uint32_t>(wc) * 2654435761u;
            for (int gy = 1; gy < ch - 1; ++gy) {
                for (int gx = 0; gx < cw - 1; ++gx) {
                    if (((h >> ((gy * 3 + gx) & 15)) & 1u) || gy == ch / 2 || gx == 0) {
                        put(cx + gx, cy + gy, 28, 36, 48);
                    }
                }
            }
        }
        cx += cw;
    }
}

} // namespace

bool IsPdfRendererActiveLinux() {
    return g_pdfEngineActive.load(std::memory_order_acquire);
}

void ReleasePdfRendererLinux() {
    g_pdfEngineActive.store(false, std::memory_order_release);
}

bool RenderPdfPageLinux(
    const std::wstring& fullPath,
    uint32_t pageIndex,
    uint32_t targetWidth,
    std::vector<uint8_t>& outBgraPixels,
    uint32_t& outWidth,
    uint32_t& outHeight,
    uint32_t* outTotalPages)
{
    outBgraPixels.clear();
    outWidth = 0;
    outHeight = 0;

    PdfDocumentSummary summary;
    if (!PdfViewer::InspectPdfFile(fullPath, summary) || !summary.valid || summary.isEncrypted) {
        return false;
    }

    const uint32_t totalPages = std::max<uint32_t>(1u, summary.pageCount);
    if (outTotalPages) *outTotalPages = totalPages;

    const uint32_t clampedWidth = std::clamp<uint32_t>(targetWidth ? targetWidth : 800u, 32u, 4096u);
    const uint32_t actualPage = std::min<uint32_t>(pageIndex, totalPages - 1u);

    const std::wstring pdftoppm = ToolProcess::FindExecutable(
        L"pdftoppm", { L"/usr/bin/pdftoppm", L"/bin/pdftoppm", L"/usr/local/bin/pdftoppm" });
    if (!pdftoppm.empty()) {
        const std::string nativePdf = Compat::NativePath(fullPath.c_str());
        char prefixBuf[128] = {};
        std::snprintf(prefixBuf, sizeof(prefixBuf), "/tmp/e2_pdf_%d_%llu",
                      static_cast<int>(::getpid()),
                      static_cast<unsigned long long>(g_pdfTmpSeq.fetch_add(1)));
        const std::string pngFile = std::string(prefixBuf) + ".png";
        const std::wstring prefixW = PathUtils::Utf8ToWide(prefixBuf);
        const std::wstring pageStr = std::to_wstring(actualPage + 1u);

        ToolProcess::Options opts;
        opts.timeoutMs = 15000;
        opts.maxBytes = 4u << 20;
        std::vector<std::wstring> args = {
            L"-png",
            L"-f", pageStr,
            L"-l", pageStr,
            L"-scale-to-x", std::to_wstring(clampedWidth),
            L"-scale-to-y", L"-1",
            L"-singlefile",
            PathUtils::Utf8ToWide(nativePdf),
            prefixW
        };
        std::atomic<bool> stopFlag{ false };
        const ToolProcess::Result res = ToolProcess::Run(pdftoppm, args, opts, stopFlag);
        std::vector<uint8_t> pngBytes;
        const bool readOk = res.ok() &&
                            PathUtils::ReadFileBytes(PathUtils::Utf8ToWide(pngFile), 64 * 1024 * 1024, pngBytes);
        ::unlink(pngFile.c_str());

        if (readOk && !pngBytes.empty()) {
            std::vector<uint8_t> rawBgra;
            uint32_t rawW = 0, rawH = 0;
            if (DecodePngBytesViaWic(pngBytes, rawBgra, rawW, rawH)) {
                ResampleBgraExactWidth(rawBgra, rawW, rawH, clampedWidth, outBgraPixels, outHeight);
                outWidth = clampedWidth;
                g_pdfEngineActive.store(true, std::memory_order_release);
                return true;
            }
        }
    }

    RenderFallbackPdfSheet(summary, actualPage, clampedWidth, outBgraPixels, outWidth, outHeight);
    g_pdfEngineActive.store(true, std::memory_order_release);
    return !outBgraPixels.empty();
}

} // namespace Explorer2::Hal
