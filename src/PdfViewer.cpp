#include "PdfViewer.h"
#include "ReaderGuard.h"
#include "ZipDeflate.h"
#include "PathUtils.h"
#include "FileEngine.h"
#include <wincodec.h>
#include <shcore.h>
#include <shellapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Data.Pdf.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <system_error>
#include <thread>

#pragma comment(lib, "WindowsApp.lib")
#pragma comment(lib, "Shcore.lib")

namespace Explorer2 {

namespace {

static D2D1_COLOR_F PdfColor(uint32_t rgb, float a = 1.0f) {
    return PathUtils::ColorFromRgb(rgb, a);
}

// Decodes a PDF literal string (...) or hex string <...> into UTF-16
std::wstring DecodePdfStringToken(const char* s, size_t len) {
    if (len == 0) return L"";
    std::vector<uint8_t> raw;
    raw.reserve(len);

    if (s[0] == '<' && len >= 2) {
        // Hex string <...>
        int hi = -1;
        for (size_t i = 1; i < len && s[i] != '>'; ++i) {
            char c = s[i];
            int nib = -1;
            if (c >= '0' && c <= '9') nib = c - '0';
            else if (c >= 'a' && c <= 'f') nib = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') nib = 10 + (c - 'A');
            if (nib >= 0) {
                if (hi < 0) hi = nib;
                else {
                    raw.push_back(static_cast<uint8_t>((hi << 4) | nib));
                    hi = -1;
                }
            }
        }
        if (hi >= 0) raw.push_back(static_cast<uint8_t>(hi << 4));
    } else {
        // Literal string (...) or raw content inside parentheses
        size_t i = (s[0] == '(') ? 1 : 0;
        size_t end = (len > 0 && s[len - 1] == ')') ? (len - 1) : len;
        while (i < end) {
            char c = s[i++];
            if (c == '\\' && i < end) {
                char n = s[i++];
                if (n == 'n') raw.push_back('\n');
                else if (n == 'r') raw.push_back('\r');
                else if (n == 't') raw.push_back('\t');
                else if (n == 'b') raw.push_back('\b');
                else if (n == 'f') raw.push_back('\f');
                else if (n == '(' || n == ')' || n == '\\') raw.push_back(static_cast<uint8_t>(n));
                else if (n >= '0' && n <= '7') {
                    int oct = n - '0';
                    if (i < end && s[i] >= '0' && s[i] <= '7') oct = (oct * 8) + (s[i++] - '0');
                    if (i < end && s[i] >= '0' && s[i] <= '7') oct = (oct * 8) + (s[i++] - '0');
                    raw.push_back(static_cast<uint8_t>(oct & 0xFF));
                } else if (n == '\r' || n == '\n') {
                    if (n == '\r' && i < end && s[i] == '\n') ++i;
                } else {
                    raw.push_back(static_cast<uint8_t>(n));
                }
            } else {
                raw.push_back(static_cast<uint8_t>(c));
            }
        }
    }

    if (raw.empty()) return L"";

    // Check UTF-16BE BOM (0xFE 0xFF)
    if (raw.size() >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        std::wstring out;
        out.reserve((raw.size() - 2) / 2);
        for (size_t i = 2; i + 1 < raw.size(); i += 2) {
            wchar_t wc = static_cast<wchar_t>((static_cast<uint16_t>(raw[i]) << 8) | raw[i + 1]);
            if (wc != 0) out.push_back(wc);
        }
        return out;
    }

    // Try UTF-8 first, fallback to Win-1250 / Latin-1 (PDFDocEncoding)
    std::string str(reinterpret_cast<const char*>(raw.data()), raw.size());
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (wlen > 0) {
        std::wstring out(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), out.data(), wlen);
        return out;
    }
    wlen = MultiByteToWideChar(1250, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (wlen > 0) {
        std::wstring out(wlen, L'\0');
        MultiByteToWideChar(1250, 0, str.data(), static_cast<int>(str.size()), out.data(), wlen);
        return out;
    }
    return PathUtils::Utf8ToWide(str);
}

// Extracts a dictionary string value like `/Title (Hello)` or `/Title <FEFF...>`
std::wstring ExtractPdfDictString(const char* data, size_t size, const char* key) {
    size_t klen = std::strlen(key);
    if (size <= klen + 2) return L"";
    for (size_t i = 0; i + klen + 2 <= size; ++i) {
        if (std::memcmp(data + i, key, klen) == 0) {
            char nextCh = data[i + klen];
            if (nextCh != ' ' && nextCh != '\t' && nextCh != '\r' && nextCh != '\n' && nextCh != '(' && nextCh != '<') {
                continue;
            }
            size_t p = i + klen;
            while (p < size && (data[p] == ' ' || data[p] == '\t' || data[p] == '\r' || data[p] == '\n')) ++p;
            if (p >= size) break;
            if (data[p] == '(') {
                size_t start = p;
                int depth = 0;
                while (p < size) {
                    if (data[p] == '\\') {
                        p += 2;
                        continue;
                    }
                    if (data[p] == '(') ++depth;
                    else if (data[p] == ')') {
                        --depth;
                        if (depth == 0) {
                            ++p;
                            break;
                        }
                    }
                    ++p;
                }
                std::wstring val = DecodePdfStringToken(data + start, p - start);
                if (!val.empty()) return val;
            } else if (data[p] == '<' && (p + 1 < size && data[p + 1] != '<')) {
                size_t start = p;
                while (p < size && data[p] != '>') ++p;
                if (p < size) ++p;
                std::wstring val = DecodePdfStringToken(data + start, p - start);
                if (!val.empty()) return val;
            }
        }
    }
    return L"";
}

// Parses a decompressed PDF content stream for BT ... ET text operators (Tj, TJ, Td, TD, T*, ', ")
std::wstring ExtractTextFromPdfContentStream(const uint8_t* streamData, size_t streamLen) {
    const char* s = reinterpret_cast<const char*>(streamData);
    std::wstring out;
    size_t i = 0;
    bool inTextObj = false;

    while (i < streamLen) {
        // Skip whitespace
        while (i < streamLen && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        if (i >= streamLen) break;

        if (!inTextObj) {
            if (i + 2 <= streamLen && s[i] == 'B' && s[i + 1] == 'T' &&
                (i + 2 == streamLen || s[i + 2] == ' ' || s[i + 2] == '\r' || s[i + 2] == '\n' || s[i + 2] == '\t' || s[i + 2] == '/'))
            {
                inTextObj = true;
                i += 2;
                continue;
            }
            ++i;
            continue;
        }

        if (i + 2 <= streamLen && s[i] == 'E' && s[i + 1] == 'T' &&
            (i + 2 == streamLen || s[i + 2] == ' ' || s[i + 2] == '\r' || s[i + 2] == '\n' || s[i + 2] == '\t'))
        {
            inTextObj = false;
            if (!out.empty() && out.back() != L'\n') out += L"\r\n";
            i += 2;
            continue;
        }

        if (s[i] == '(') {
            size_t start = i;
            int depth = 0;
            while (i < streamLen) {
                if (s[i] == '\\') { i += 2; continue; }
                if (s[i] == '(') ++depth;
                else if (s[i] == ')') {
                    --depth;
                    if (depth == 0) { ++i; break; }
                }
                ++i;
            }
            std::wstring piece = DecodePdfStringToken(s + start, i - start);
            if (!piece.empty()) out += piece;
            continue;
        }

        if (s[i] == '[') {
            // TJ array: [(Hello) -120 (World)]
            ++i;
            while (i < streamLen && s[i] != ']') {
                if (s[i] == '(') {
                    size_t start = i;
                    int depth = 0;
                    while (i < streamLen) {
                        if (s[i] == '\\') { i += 2; continue; }
                        if (s[i] == '(') ++depth;
                        else if (s[i] == ')') {
                            --depth;
                            if (depth == 0) { ++i; break; }
                        }
                        ++i;
                    }
                    out += DecodePdfStringToken(s + start, i - start);
                } else if (s[i] == '<' && (i + 1 < streamLen && s[i + 1] != '<')) {
                    size_t start = i;
                    while (i < streamLen && s[i] != '>') ++i;
                    if (i < streamLen) ++i;
                    out += DecodePdfStringToken(s + start, i - start);
                } else if (s[i] == '-' && i + 1 < streamLen && s[i + 1] >= '0' && s[i + 1] <= '9') {
                    // Large negative kerning (< -150) inside TJ array represents a word space
                    double kern = std::strtod(s + i, nullptr);
                    if (kern < -150.0 && !out.empty() && out.back() != L' ') {
                        out.push_back(L' ');
                    }
                    while (i < streamLen && (s[i] == '-' || s[i] == '.' || (s[i] >= '0' && s[i] <= '9'))) ++i;
                } else {
                    ++i;
                }
            }
            if (i < streamLen && s[i] == ']') ++i;
            continue;
        }

        // Line-break operators: T*, Td, TD, '
        if (s[i] == 'T' && i + 1 < streamLen) {
            char op2 = s[i + 1];
            if (op2 == '*' || op2 == 'd' || op2 == 'D') {
                if (!out.empty() && out.back() != L'\n') out += L"\r\n";
                i += 2;
                continue;
            }
        }
        if (s[i] == '\'') {
            if (!out.empty() && out.back() != L'\n') out += L"\r\n";
            ++i;
            continue;
        }

        ++i;
    }
    return out;
}

// Renders one page with Windows.Data.Pdf. Runs on the worker thread of RenderPdfPageToBgra; every WinRT and
// COM object it creates is released again before it returns. Throws when Windows turns the document down.
bool RenderPageInThisApartment(
    const std::wstring& fullPath,
    uint32_t pageIndex,
    uint32_t targetWidth,
    std::vector<uint8_t>& outBgraPixels,
    uint32_t& outWidth,
    uint32_t& outHeight,
    uint32_t* outTotalPages)
{
    auto storageFile = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(fullPath).get();
    if (!storageFile) return false;

    auto pdfDoc = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(storageFile).get();
    if (!pdfDoc) return false;

    uint32_t count = pdfDoc.PageCount();
    if (outTotalPages) *outTotalPages = count;
    if (count == 0) return false;

    uint32_t clampedIdx = std::min(pageIndex, count - 1);
    auto page = pdfDoc.GetPage(clampedIdx);
    auto pageSize = page.Size();

    uint32_t reqW = std::clamp(targetWidth, 256u, 2400u);
    float aspect = (pageSize.Width > 1.0f) ? (pageSize.Height / pageSize.Width) : 1.4142f;
    uint32_t reqH = std::max(64u, static_cast<uint32_t>(std::round(reqW * aspect)));

    winrt::Windows::Storage::Streams::InMemoryRandomAccessStream ras;
    winrt::Windows::Data::Pdf::PdfPageRenderOptions renderOpts;
    renderOpts.DestinationWidth(reqW);
    renderOpts.DestinationHeight(reqH);

    page.RenderToStreamAsync(ras, renderOpts).get();
    ras.Seek(0);

    Microsoft::WRL::ComPtr<IStream> comStream;
    if (FAILED(CreateStreamOverRandomAccessStream(winrt::get_unknown(ras), IID_PPV_ARGS(&comStream))) || !comStream) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory)))) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wicFactory->CreateDecoderFromStream(comStream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory->CreateFormatConverter(&converter))) {
        return false;
    }

    if (FAILED(converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom)))
    {
        return false;
    }

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    if (w == 0 || h == 0) return false;

    outBgraPixels.resize(static_cast<size_t>(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(outBgraPixels.size()), outBgraPixels.data()))) {
        return false;
    }
    outWidth = w;
    outHeight = h;
    return true;
}

// Windows' PDF library (Windows.Data.Pdf) is used from one thread of its own. The thread enters the
// multithreaded apartment before the first page, stays in it for every page after that, and on Stop()
// drops everything that points into the library, has Windows unload it, and only then leaves the apartment.
// Loading, using and unloading the library thus happen in one place and in one apartment, and the library
// does not stay loaded until the process ends, which would be too late (see PdfViewer::ReleaseRenderer).
//
// A caller waits for its page: Run returns when the task is done, and one caller is served at a time.
class RenderThread {
public:
    // Runs task on the render thread and waits until it is done. False if the task could not be run.
    bool Run(const std::function<void()>& task) {
        std::lock_guard<std::mutex> oneCaller(m_callMutex);
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_thread.joinable()) {
            m_stop = false;
            try {
                m_thread = std::thread(&RenderThread::Loop, this);
            } catch (const std::system_error&) {
                return false;       // no thread to be had
            }
        }
        m_task = &task;
        m_taskRan = false;
        m_taskFinished = false;
        m_wake.notify_all();
        m_done.wait(lock, [this]() { return m_taskFinished; });
        m_task = nullptr;
        return m_taskRan;
    }

    // Unloads the library and ends the thread. Does nothing if no page was ever asked for.
    void Stop() {
        std::lock_guard<std::mutex> oneCaller(m_callMutex);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_thread.joinable()) return;
            m_stop = true;
        }
        m_wake.notify_all();
        m_thread.join();
    }

private:
    // Has Windows unload the PDF library, and makes sure that it did. One request is not enough: for a
    // moment after a page was rendered the library still has work of its own going on, says that it is in
    // use, and Windows leaves it loaded. So the request is repeated until the library is gone. If it says
    // that it is free and is still there after that, something other than this program's PDF view holds
    // it, and asking again would not change that.
    static void UnloadPdfLibrary() {
        int freeButStillLoaded = 0;
        for (int attempt = 0; attempt < 200 && freeButStillLoaded < 3; ++attempt) {
            CoFreeUnusedLibrariesEx(0, 0);
            HMODULE library = GetModuleHandleW(L"Windows.Data.Pdf.dll");
            if (!library) return;
            typedef HRESULT (STDAPICALLTYPE *CanUnloadNow)();
            const CanUnloadNow canUnloadNow = reinterpret_cast<CanUnloadNow>(GetProcAddress(library, "DllCanUnloadNow"));
            if (canUnloadNow && canUnloadNow() == S_OK) ++freeButStillLoaded;
            else freeButStillLoaded = 0;
            Sleep(10);
        }
        AppLog::Warn(L"PDF", L"Windows' PDF library could not be unloaded; the program may end with an error code");
    }

    void Loop() {
        bool inApartment = false;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            inApartment = true;
        } catch (...) {
        }

        std::unique_lock<std::mutex> lock(m_mutex);
        for (;;) {
            m_wake.wait(lock, [this]() { return m_stop || (m_task && !m_taskFinished); });
            if (m_task && !m_taskFinished) {
                const std::function<void()>* task = m_task;
                lock.unlock();
                if (inApartment) (*task)();     // the task catches what it throws
                lock.lock();
                m_taskRan = inApartment;
                m_taskFinished = true;
                m_done.notify_all();
                continue;
            }
            break;      // m_stop
        }
        lock.unlock();

        if (inApartment) {
            // Nothing of ours points into the library now: the pages' objects are gone and the class
            // factories C++/WinRT keeps are dropped here. The library therefore reports that it can go,
            // and Windows unloads it, in a process that is still whole.
            winrt::clear_factory_cache();
            UnloadPdfLibrary();
            winrt::uninit_apartment();
        }
    }

    std::mutex m_callMutex;                 // one caller of Run or Stop at a time
    std::mutex m_mutex;                     // guards everything below
    std::condition_variable m_wake;         // to the thread: a task or a stop request is waiting
    std::condition_variable m_done;         // to the caller: the task is finished
    std::thread m_thread;
    const std::function<void()>* m_task = nullptr;
    bool m_taskRan = false;
    bool m_taskFinished = false;
    bool m_stop = false;
};

// Never destroyed: a std::thread that is still joinable must not be destroyed when the process ends
RenderThread& Renderer() {
    static RenderThread* renderer = new RenderThread();
    return *renderer;
}

} // namespace

PdfViewer::~PdfViewer() {
    Shutdown();
}

bool PdfViewer::IsPdfExtension(const std::wstring& extLower) {
    return extLower == L".pdf" || extLower == L".ai";
}

bool PdfViewer::InspectPdfFile(const std::wstring& fullPath, PdfDocumentSummary& outSummary) try {
    outSummary = {};
    std::vector<uint8_t> bytes;
    if (!PathUtils::ReadFileBytes(fullPath, 64ULL * 1024 * 1024, bytes) || bytes.size() < 8) {
        return false;
    }

    const char* data = reinterpret_cast<const char*>(bytes.data());
    size_t size = bytes.size();

    // Find %PDF- header within the first 1024 bytes
    size_t pdfHeaderPos = std::string::npos;
    for (size_t i = 0; i + 8 <= std::min<size_t>(size, 1024); ++i) {
        if (std::memcmp(data + i, "%PDF-", 5) == 0) {
            pdfHeaderPos = i;
            break;
        }
    }
    if (pdfHeaderPos == std::string::npos) return false;

    std::string verStr(data + pdfHeaderPos + 5, 3);
    outSummary.pdfVersion = PathUtils::Utf8ToWide(verStr);
    outSummary.title    = ExtractPdfDictString(data, size, "/Title");
    outSummary.author   = ExtractPdfDictString(data, size, "/Author");
    outSummary.creator  = ExtractPdfDictString(data, size, "/Creator");
    outSummary.producer = ExtractPdfDictString(data, size, "/Producer");

    // Count `/Type /Page` (not `/Type /Pages`), check `/Encrypt`, and parse `/MediaBox`
    uint32_t pageCount = 0;
    for (size_t i = 0; i + 12 < size; ++i) {
        if (data[i] == '/') {
            if (std::memcmp(data + i, "/Encrypt", 8) == 0) {
                char c = data[i + 8];
                if (c == ' ' || c == '\r' || c == '\n' || c == '/' || c == '<') {
                    outSummary.isEncrypted = true;
                }
            } else if (std::memcmp(data + i, "/Type", 5) == 0) {
                size_t p = i + 5;
                while (p < size && (data[p] == ' ' || data[p] == '\t' || data[p] == '\r' || data[p] == '\n')) ++p;
                if (p + 5 < size && std::memcmp(data + p, "/Page", 5) == 0) {
                    char after = data[p + 5];
                    if (after != 's' && after != 'S' && (after == ' ' || after == '\t' || after == '\r' || after == '\n' || after == '/' || after == '>')) {
                        ++pageCount;
                    }
                }
            } else if (std::memcmp(data + i, "/MediaBox", 9) == 0) {
                size_t p = i + 9;
                while (p < size && data[p] != '[' && data[p] != '\r' && data[p] != '\n') ++p;
                if (p < size && data[p] == '[') {
                    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                    if (sscanf_s(data + p + 1, "%f %f %f %f", &x0, &y0, &x1, &y1) == 4) {
                        float w = std::abs(x1 - x0);
                        float h = std::abs(y1 - y0);
                        if (w > 10.0f && h > 10.0f) {
                            outSummary.pageWidthPt = w;
                            outSummary.pageHeightPt = h;
                        }
                    }
                }
            }
        }
    }

    // Scan streams (`stream` ... `endstream`) and extract text from uncompressed or `/FlateDecode` streams.
    // Unpacking is limited per stream and per document: this runs when a file is merely selected, so a
    // document made of streams that unpack to hundreds of megabytes must not hold the program up.
    const size_t kUnpackLimitPerStream = 32u * 1024 * 1024;
    size_t unpackBudget = 64u * 1024 * 1024;
    size_t pos = 0;
    while (pos + 16 < size && outSummary.pageTexts.size() < 200) {
        const void* found = std::memchr(data + pos, 's', size - pos - 10);
        if (!found) break;
        size_t sPos = static_cast<const char*>(found) - data;
        pos = sPos + 1;

        if (std::memcmp(data + sPos, "stream", 6) != 0) continue;
        if (sPos > 0 && data[sPos - 1] == 'd') continue; // skip "endstream"

        size_t dataStart = sPos + 6;
        if (dataStart < size && data[dataStart] == '\r') ++dataStart;
        if (dataStart < size && data[dataStart] == '\n') ++dataStart;
        else if (sPos + 6 < size && data[sPos + 6] != '\n') continue;

        // Find matching "endstream"
        size_t endPos = dataStart;
        bool foundEnd = false;
        while (endPos + 9 <= size) {
            if (data[endPos] == 'e' && std::memcmp(data + endPos, "endstream", 9) == 0) {
                foundEnd = true;
                break;
            }
            ++endPos;
        }
        if (!foundEnd) break;
        pos = endPos + 9;

        size_t rawLen = endPos - dataStart;
        while (rawLen > 0 && (data[dataStart + rawLen - 1] == '\r' || data[dataStart + rawLen - 1] == '\n')) {
            --rawLen;
        }
        if (rawLen == 0 || rawLen > 16 * 1024 * 1024) continue;

        // Check dictionary preceding `stream` for `/FlateDecode` or `/Subtype /Image`
        size_t dictLookback = (sPos > 256) ? (sPos - 256) : 0;
        std::string dictHdr(data + dictLookback, sPos - dictLookback);
        if (dictHdr.find("/Subtype /Image") != std::string::npos ||
            dictHdr.find("/Subtype/Image") != std::string::npos) {
            continue;
        }

        const uint8_t* streamBytes = bytes.data() + dataStart;
        std::vector<uint8_t> decompressed;
        const uint8_t* parsePtr = streamBytes;
        size_t parseLen = rawLen;

        if (dictHdr.find("FlateDecode") != std::string::npos) {
            if (unpackBudget == 0) break;
            bool unpacked = ZipDeflate::InflateZlib(streamBytes, rawLen, decompressed, std::min(unpackBudget, kUnpackLimitPerStream));
            unpackBudget -= std::min(unpackBudget, decompressed.size());
            if (!unpacked && unpackBudget > 0) {
                unpacked = ZipDeflate::InflateRaw(streamBytes, rawLen, decompressed, std::min(unpackBudget, kUnpackLimitPerStream));
                unpackBudget -= std::min(unpackBudget, decompressed.size());
            }
            if (!unpacked) continue;
            parsePtr = decompressed.data();
            parseLen = decompressed.size();
        }

        std::wstring streamText = ExtractTextFromPdfContentStream(parsePtr, parseLen);
        if (!streamText.empty()) {
            outSummary.pageTexts.push_back(streamText);
            if (!outSummary.allExtractedText.empty()) {
                outSummary.allExtractedText += L"\r\n";
            }
            outSummary.allExtractedText += streamText;
        }
    }

    if (pageCount == 0 && !outSummary.pageTexts.empty()) {
        pageCount = static_cast<uint32_t>(outSummary.pageTexts.size());
    }
    if (pageCount == 0) pageCount = 1;
    outSummary.pageCount = pageCount;
    outSummary.valid = true;
    return true;
} E2_READER_CATCH(L"PDF reader", fullPath, { outSummary = PdfDocumentSummary(); return false; })

bool PdfViewer::RenderPdfPageToBgra(
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
    if (fullPath.empty()) return false;

    bool ok = false;
    const bool ran = Renderer().Run([&]() {
        try {
            ok = RenderPageInThisApartment(fullPath, pageIndex, targetWidth, outBgraPixels, outWidth, outHeight, outTotalPages);
        } catch (...) {
            ok = false;     // Windows turned the document down (damaged, encrypted, not a PDF)
        }
    });
    if (!ran || !ok) {
        outBgraPixels.clear();
        outWidth = 0;
        outHeight = 0;
        return false;
    }
    return true;
}

void PdfViewer::ReleaseRenderer() {
    Renderer().Stop();
}

void PdfViewer::RenderPagePicture(const std::wstring& fullPath, uint32_t page, PagePicture& out) {
    out = PagePicture();
    out.page = page;
    uint32_t pageCount = 0;
    if (RenderPdfPageToBgra(fullPath, page, 1100, out.bgra, out.width, out.height, &pageCount) && pageCount > 0) {
        out.pageCount = pageCount;
        out.page = std::min(page, pageCount - 1);   // for a page past the end Windows draws the last one
    }
}

void PdfViewer::Initialize(HWND parentHwnd) {
    m_parentHwnd = parentHwnd;
}

void PdfViewer::Shutdown() {
    Clear();
}

void PdfViewer::Clear() {
    m_loadedPath.clear();
    m_summary = {};
    m_currentPage = 0;
    m_zoom = 1.0f;
    m_panX = 0.0f;
    m_panY = 0.0f;
    m_textScrollY = 0.0f;
    m_pageBgra.clear();
    m_pageBmpW = 0;
    m_pageBmpH = 0;
    m_picturePage = 0xFFFFFFFFu;
    m_pageRequested = false;
    m_bitmapUploadedPage = 0xFFFFFFFFu;
    m_bitmapUploadedPath.clear();
    m_d2dPageBitmap.Reset();
}

void PdfViewer::ApplyPagePicture(PagePicture&& picture) {
    if (picture.pageCount > 0) {
        m_summary.pageCount = picture.pageCount;
        if (m_currentPage >= m_summary.pageCount) {
            m_currentPage = m_summary.pageCount - 1;
        }
    }
    m_pageBgra = std::move(picture.bgra);
    m_pageBmpW = picture.width;
    m_pageBmpH = picture.height;
    m_picturePage = picture.page;
    m_d2dPageBitmap.Reset();
    m_bitmapUploadedPage = 0xFFFFFFFFu;
}

void PdfViewer::LoadCurrentPageBitmap() {
    if (m_loadedPath.empty()) {
        m_pageBgra.clear();
        m_pageBmpW = 0;
        m_pageBmpH = 0;
        m_picturePage = 0xFFFFFFFFu;
        m_d2dPageBitmap.Reset();
        m_bitmapUploadedPage = 0xFFFFFFFFu;
        return;
    }
    PagePicture picture;
    RenderPagePicture(m_loadedPath, m_currentPage, picture);
    ApplyPagePicture(std::move(picture));
}

void PdfViewer::RequestCurrentPage() {
    if (m_pagesDrawnByOwner) {
        m_pageRequested = true;     // the owner has it drawn and hands it back through ShowPagePicture
        return;
    }
    LoadCurrentPageBitmap();
}

bool PdfViewer::TakePageRequest(uint32_t& outPage) {
    if (!m_pageRequested) return false;
    m_pageRequested = false;
    if (m_loadedPath.empty()) return false;
    outPage = m_currentPage;
    return true;
}

bool PdfViewer::TurnPage(int direction) {
    if (direction > 0 && m_currentPage + 1 < m_summary.pageCount) {
        ++m_currentPage;
    } else if (direction < 0 && m_currentPage > 0) {
        --m_currentPage;
    } else {
        return false;
    }
    m_panY = 0.0f;
    m_textScrollY = 0.0f;
    RequestCurrentPage();
    return true;
}

void PdfViewer::LoadFile(const std::wstring& fullPath, bool forceReload) {
    if (!forceReload && m_loadedPath == fullPath && m_summary.valid) {
        return;
    }
    PdfDocumentSummary summary;
    InspectPdfFile(fullPath, summary);
    PagePicture firstPage;
    RenderPagePicture(fullPath, 0, firstPage);
    ShowLoadedDocument(fullPath, std::move(summary), std::move(firstPage));
}

void PdfViewer::ShowLoadedDocument(const std::wstring& fullPath, PdfDocumentSummary&& summary, PagePicture&& firstPage) {
    Clear();
    m_loadedPath = fullPath;
    m_summary = std::move(summary);
    ApplyPagePicture(std::move(firstPage));
}

void PdfViewer::ShowPagePicture(const std::wstring& fullPath, PagePicture&& picture) {
    if (!IsShowing(fullPath)) return;
    uint32_t wanted = m_currentPage;
    if (picture.pageCount > 0 && wanted >= picture.pageCount) wanted = picture.pageCount - 1;
    if (picture.page != wanted) return;     // turned again since: the picture of that page is on its way
    ApplyPagePicture(std::move(picture));
}

void PdfViewer::Draw(
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
        const wchar_t* err = pl ? L"Nie udało się odczytać pliku PDF." : L"Failed to read PDF document.";
        brush->SetColor(PdfColor(0x64748B));
        rt->DrawText(err, static_cast<UINT32>(wcslen(err)), fmtRegular,
            D2D1::RectF(rect.left + 16.0f, rect.top + 24.0f, rect.right - 16.0f, rect.bottom), brush);
        return;
    }

    // 1. Top PDF Toolbar (32px)
    float barH = 32.0f;
    D2D1_RECT_F barR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + barH);
    brush->SetColor(PdfColor(0xF8FAFC));
    rt->FillRectangle(barR, brush);
    brush->SetColor(PdfColor(0xE2E8F0));
    rt->DrawLine(D2D1::Point2F(barR.left, barR.bottom - 0.5f), D2D1::Point2F(barR.right, barR.bottom - 0.5f), brush, 1.0f);

    float bx = barR.left + 8.0f;
    auto addBtn = [&](int id, float w, const wchar_t* label, bool active = false) {
        D2D1_RECT_F r = D2D1::RectF(bx, barR.top + 4.0f, bx + w, barR.bottom - 4.0f);
        m_toolbarHits.push_back({ id, r });
        PathUtils::DrawToolbarButton(rt, brush, fmtSmall, r, label, active, m_hoverBtnId == id);
        bx += w + 4.0f;
    };

    addBtn(BTN_PREV_PAGE, 28.0f, L"\x25C0");
    wchar_t pageLbl[64] = {};
    swprintf_s(pageLbl, pl ? L"Str. %u / %u" : L"Page %u / %u", m_currentPage + 1, std::max(1u, m_summary.pageCount));
    D2D1_RECT_F pgR = D2D1::RectF(bx, barR.top + 4.0f, bx + 78.0f, barR.bottom - 4.0f);
    brush->SetColor(PdfColor(0x1E293B));
    fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    rt->DrawText(pageLbl, static_cast<UINT32>(wcslen(pageLbl)), fmtBold, pgR, brush);
    fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    bx += 82.0f;
    addBtn(BTN_NEXT_PAGE, 28.0f, L"\x25B6");

    bx += 4.0f;
    addBtn(BTN_ZOOM_OUT, 26.0f, L"\x2212");
    wchar_t zoomLbl[32] = {};
    swprintf_s(zoomLbl, L"%d%%", static_cast<int>(std::round(m_zoom * 100.0f)));
    addBtn(BTN_ZOOM_FIT, 48.0f, zoomLbl, std::abs(m_zoom - 1.0f) < 0.02f && m_panX == 0.0f && m_panY == 0.0f);
    addBtn(BTN_ZOOM_IN,  26.0f, L"+");

    bx += 4.0f;
    addBtn(BTN_TOGGLE_MODE, pl ? 78.0f : 74.0f,
        m_textMode ? (pl ? L"\xD83D\xDCC4 Strona" : L"\xD83D\xDCC4 Page") : (pl ? L"\xD83D\xDCDD Tekst" : L"\xD83D\xDCDD Text"),
        m_textMode);
    addBtn(BTN_COPY_TEXT, pl ? 68.0f : 62.0f, pl ? L"Kopiuj" : L"Copy");
    addBtn(BTN_OPEN_EXTERNAL, pl ? 68.0f : 62.0f, pl ? L"Otwórz \x2197" : L"Open \x2197");

    // 2. Metadata Info Strip (22px)
    float infoH = 22.0f;
    D2D1_RECT_F infoR = D2D1::RectF(rect.left, barR.bottom, rect.right, barR.bottom + infoH);
    brush->SetColor(PdfColor(0xEFF6FF));
    rt->FillRectangle(infoR, brush);
    brush->SetColor(PdfColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(infoR.left, infoR.bottom - 0.5f), D2D1::Point2F(infoR.right, infoR.bottom - 0.5f), brush, 1.0f);

    float mmW = m_summary.pageWidthPt * 0.352778f;
    float mmH = m_summary.pageHeightPt * 0.352778f;
    std::wstring metaLine = L"PDF " + m_summary.pdfVersion;
    wchar_t dimBuf[96] = {};
    swprintf_s(dimBuf, L"   \x2022   %.0f \xD7 %.0f mm", mmW, mmH);
    metaLine += dimBuf;
    if (!m_summary.title.empty()) metaLine += L"   \x2022   " + m_summary.title;
    else if (!m_summary.author.empty()) metaLine += L"   \x2022   " + m_summary.author;
    else if (!m_summary.producer.empty()) metaLine += L"   \x2022   " + m_summary.producer;

    brush->SetColor(PdfColor(0x1E3A8A));
    rt->DrawText(metaLine.c_str(), static_cast<UINT32>(metaLine.size()), fmtSmall,
        D2D1::RectF(infoR.left + 8.0f, infoR.top + 2.0f, infoR.right - 8.0f, infoR.bottom), brush);

    // 3. Main PDF Viewport
    m_viewportRect = D2D1::RectF(rect.left, infoR.bottom, rect.right, rect.bottom);
    rt->PushAxisAlignedClip(m_viewportRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (m_textMode) {
        brush->SetColor(PdfColor(0xFFFFFF));
        rt->FillRectangle(m_viewportRect, brush);

        std::wstring textBody;
        if (!m_summary.title.empty())    textBody += (pl ? L"Tytuł: " : L"Title: ") + m_summary.title + L"\r\n";
        if (!m_summary.author.empty())   textBody += (pl ? L"Autor: " : L"Author: ") + m_summary.author + L"\r\n";
        if (!m_summary.creator.empty())  textBody += (pl ? L"Aplikacja: " : L"Creator: ") + m_summary.creator + L"\r\n";
        if (!m_summary.producer.empty()) textBody += (pl ? L"Generator PDF: " : L"Producer: ") + m_summary.producer + L"\r\n";
        if (!textBody.empty()) textBody += L"────────────────────────────────────────────────────\r\n";

        if (m_currentPage < m_summary.pageTexts.size() && !m_summary.pageTexts[m_currentPage].empty()) {
            textBody += m_summary.pageTexts[m_currentPage];
        } else if (!m_summary.allExtractedText.empty()) {
            textBody += m_summary.allExtractedText;
        } else {
            textBody += pl
                ? L"(Strona zawiera grafikę wektorową/skan lub czcionki osadzone — przełącz na widok [📄 Strona], aby zobaczyć pełny render wizualny PDF)."
                : L"(Page contains vector graphics/scan or embedded subset fonts — switch to [📄 Page] view for full visual PDF rendering).";
        }

        D2D1_RECT_F txtR = D2D1::RectF(
            m_viewportRect.left + 14.0f,
            m_viewportRect.top + 10.0f - m_textScrollY,
            m_viewportRect.right - 14.0f,
            m_viewportRect.top + 10000.0f);
        brush->SetColor(PdfColor(0x0F172A));
        rt->DrawText(textBody.c_str(), static_cast<UINT32>(textBody.size()), fmtRegular, txtR, brush);
    } else {
        // Visual PDF Page Canvas (dark slate background + drop-shadowed white page sheet)
        brush->SetColor(PdfColor(0xCBD5E1));
        rt->FillRectangle(m_viewportRect, brush);

        if (m_lastRenderTarget != rt) {
            m_d2dPageBitmap.Reset();
            m_lastRenderTarget = rt;
            m_bitmapUploadedPage = 0xFFFFFFFFu;
        }
        if ((!m_d2dPageBitmap || m_bitmapUploadedPage != m_picturePage || m_bitmapUploadedPath != m_loadedPath) &&
            !m_pageBgra.empty() && m_pageBmpW > 0 && m_pageBmpH > 0)
        {
            D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            rt->CreateBitmap(
                D2D1::SizeU(m_pageBmpW, m_pageBmpH),
                m_pageBgra.data(),
                m_pageBmpW * 4,
                &props,
                &m_d2dPageBitmap);
            m_bitmapUploadedPage = m_picturePage;
            m_bitmapUploadedPath = m_loadedPath;
        }

        float viewW = std::max(40.0f, m_viewportRect.right - m_viewportRect.left - 24.0f);
        float viewH = std::max(40.0f, m_viewportRect.bottom - m_viewportRect.top - 24.0f);
        float aspect = (m_pageBmpW > 0 && m_pageBmpH > 0)
            ? (static_cast<float>(m_pageBmpH) / static_cast<float>(m_pageBmpW))
            : ((m_summary.pageWidthPt > 1.0f) ? (m_summary.pageHeightPt / m_summary.pageWidthPt) : 1.4142f);

        float baseW = std::min(viewW, viewH / std::max(0.2f, aspect));
        float baseH = baseW * aspect;
        float drawW = baseW * m_zoom;
        float drawH = baseH * m_zoom;

        float cx = (m_viewportRect.left + m_viewportRect.right) * 0.5f + m_panX;
        float cy = (m_viewportRect.top + m_viewportRect.bottom) * 0.5f + m_panY;
        D2D1_RECT_F pageR = D2D1::RectF(cx - drawW * 0.5f, cy - drawH * 0.5f, cx + drawW * 0.5f, cy + drawH * 0.5f);

        // Drop shadow + white paper sheet
        brush->SetColor(PdfColor(0x64748B, 0.45f));
        rt->FillRectangle(D2D1::RectF(pageR.left + 4.0f, pageR.top + 5.0f, pageR.right + 4.0f, pageR.bottom + 5.0f), brush);
        brush->SetColor(PdfColor(0xFFFFFF));
        rt->FillRectangle(pageR, brush);

        if (m_d2dPageBitmap) {
            rt->DrawBitmap(m_d2dPageBitmap.Get(), pageR, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            // Native vector paper sheet fallback when Windows.Data.Pdf bitmap is not available
            D2D1_RECT_F padR = D2D1::RectF(pageR.left + 18.0f, pageR.top + 18.0f, pageR.right - 18.0f, pageR.bottom - 18.0f);
            std::wstring fallbackTxt = (m_currentPage < m_summary.pageTexts.size() && !m_summary.pageTexts[m_currentPage].empty())
                ? m_summary.pageTexts[m_currentPage]
                : m_summary.allExtractedText;
            if (fallbackTxt.empty()) {
                fallbackTxt = pl ? L"[Dokument PDF — Strona " + std::to_wstring(m_currentPage + 1) + L"]"
                                 : L"[PDF Document — Page " + std::to_wstring(m_currentPage + 1) + L"]";
            }
            brush->SetColor(PdfColor(0x0F172A));
            rt->DrawText(fallbackTxt.c_str(), static_cast<UINT32>(fallbackTxt.size()), fmtSmall, padR, brush);
        }

        brush->SetColor(PdfColor(0x94A3B8));
        rt->DrawRectangle(pageR, brush, 1.0f);

        // The page was turned and its picture is still being drawn: the previous one stays meanwhile
        if (m_picturePage != m_currentPage) {
            wchar_t note[64] = {};
            swprintf_s(note, pl ? L"Rysowanie strony %u\x2026" : L"Drawing page %u\x2026", m_currentPage + 1);
            const float noteW = 150.0f;
            const float ncx = (m_viewportRect.left + m_viewportRect.right) * 0.5f;
            D2D1_RECT_F noteR = D2D1::RectF(ncx - noteW * 0.5f, m_viewportRect.top + 10.0f, ncx + noteW * 0.5f, m_viewportRect.top + 34.0f);
            brush->SetColor(PdfColor(0x0F172A, 0.78f));
            rt->FillRoundedRectangle(D2D1::RoundedRect(noteR, 11.0f, 11.0f), brush);
            brush->SetColor(PdfColor(0xFFFFFF));
            fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            rt->DrawText(note, static_cast<UINT32>(wcslen(note)), fmtSmall,
                D2D1::RectF(noteR.left, noteR.top + 4.0f, noteR.right, noteR.bottom), brush);
            fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }

    rt->PopAxisAlignedClip();
}

bool PdfViewer::OnMouseDown(float x, float y, bool rightButton, Lang lang, std::wstring* outStatusToast) {
    if (rightButton) return false;
    const bool pl = (lang == Lang::PL);

    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            switch (hit.id) {
            case BTN_PREV_PAGE:
                TurnPage(-1);
                return true;
            case BTN_NEXT_PAGE:
                TurnPage(+1);
                return true;
            case BTN_ZOOM_OUT:
                m_zoom = std::clamp(m_zoom / 1.25f, 0.25f, 4.0f);
                return true;
            case BTN_ZOOM_FIT:
                m_zoom = 1.0f;
                m_panX = 0.0f;
                m_panY = 0.0f;
                return true;
            case BTN_ZOOM_IN:
                m_zoom = std::clamp(m_zoom * 1.25f, 0.25f, 4.0f);
                return true;
            case BTN_TOGGLE_MODE:
                m_textMode = !m_textMode;
                m_textScrollY = 0.0f;
                return true;
            case BTN_COPY_TEXT: {
                std::wstring toCopy = (m_currentPage < m_summary.pageTexts.size() && !m_summary.pageTexts[m_currentPage].empty())
                    ? m_summary.pageTexts[m_currentPage]
                    : m_summary.allExtractedText;
                if (toCopy.empty()) toCopy = m_loadedPath;
                FileEngine::CopyTextToClipboard(m_parentHwnd, toCopy);
                if (outStatusToast) {
                    *outStatusToast = pl ? L"\x2713 Skopiowano tekst z PDF do schowka" : L"\x2713 Copied PDF text to clipboard";
                }
                return true;
            }
            case BTN_OPEN_EXTERNAL:
                if (!m_loadedPath.empty()) {
                    ShellExecuteW(m_parentHwnd, L"open", m_loadedPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                return true;
            }
        }
    }

    if (!m_textMode && PathUtils::PtInRectF(x, y, m_viewportRect)) {
        m_draggingPan = true;
        m_lastMousePt = { static_cast<LONG>(x), static_cast<LONG>(y) };
        return true;
    }
    return false;
}

bool PdfViewer::OnMouseMove(float x, float y, WPARAM keyFlags) {
    (void)keyFlags;
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

    if (m_draggingPan) {
        m_panX += (x - static_cast<float>(m_lastMousePt.x));
        m_panY += (y - static_cast<float>(m_lastMousePt.y));
        m_lastMousePt = { static_cast<LONG>(x), static_cast<LONG>(y) };
        return true;
    }
    return changed;
}

bool PdfViewer::OnMouseUp() {
    if (m_draggingPan) {
        m_draggingPan = false;
        return true;
    }
    return false;
}

bool PdfViewer::OnMouseWheel(short delta, float x, float y) {
    if (!PathUtils::PtInRectF(x, y, m_viewportRect)) return false;
    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;

    if (m_textMode) {
        m_textScrollY = std::max(0.0f, m_textScrollY - notches * 48.0f);
        return true;
    }

    bool ctrlHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrlHeld) {
        float factor = (notches > 0) ? 1.15f : (1.0f / 1.15f);
        m_zoom = std::clamp(m_zoom * factor, 0.25f, 4.0f);
        return true;
    }

    // If zoomed in, scroll vertically; otherwise flip pages with mouse wheel
    if (m_zoom > 1.05f) {
        m_panY += notches * 48.0f;
    } else {
        TurnPage(notches < 0 ? +1 : -1);
    }
    return true;
}

} // namespace Explorer2
