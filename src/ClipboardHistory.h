#pragma once
// ClipboardHistory.h - what was copied while Explorer2 runs: text, lists of files and pictures (the "Schowek" /
// "Clipboard" tab).
//
// The window is told about every change (WM_CLIPBOARDUPDATE: AddClipboardFormatListener on Windows, the X11 layer's
// own poll on Linux) and keeps what came. The history lives in memory only and is gone when the program closes:
// a clipboard often holds what should not end up in a file. Content a password manager marks as private is not
// kept at all (Windows: "ExcludeClipboardContentFromMonitorProcessing", "CanIncludeInClipboardHistory" = 0,
// "Clipboard Viewer Ignore"; KDE / KeePassXC on Linux: "x-kde-passwordManagerHint" = "secret").
//
// ClipboardHistory is the list itself and has no platform code, so that the self-tests can check it anywhere.
// ClipboardIo reads and writes the clipboard through the Win32 calls, which the Linux build's X11 layer provides.
#include "FileEngine.h"
#include "Localization.h"
#include "PathUtils.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Explorer2 {

struct ClipboardEntry {
    enum class Kind { Text, Files, Image };
    Kind kind = Kind::Text;
    uint64_t id = 0;
    SYSTEMTIME time = {};               // local time it was copied
    std::wstring text;                  // Text
    std::vector<std::wstring> files;    // Files
    bool cut = false;                   // Files: cut (to be moved) rather than copied
    uint32_t width = 0, height = 0;     // Image: its size in pixels
    std::vector<uint8_t> pixels;        // Image: BGRA, straight alpha, rows from the top
    std::vector<uint8_t> thumb;         // Image: a premultiplied BGRA preview of at most kThumbMax x kThumbMax
    uint32_t thumbW = 0, thumbH = 0;
    bool pinned = false;
    uint64_t fingerprint = 0;           // tells the same content copied again
};

class ClipboardHistory {
public:
    static constexpr size_t kMaxEntries = 60;
    static constexpr size_t kMaxImages = 12;
    static constexpr size_t kMaxImageBytes = 320ull << 20;      // all pictures together
    static constexpr size_t kMaxOneImagePixels = 40u << 20;     // a larger picture is not kept
    static constexpr size_t kMaxTextChars = 4u << 20;           // nor is a longer text
    static constexpr uint32_t kThumbMax = 160;

    // Newest first
    const std::vector<ClipboardEntry>& Entries() const { return m_entries; }
    const ClipboardEntry* Find(uint64_t id) const {
        for (const auto& e : m_entries) if (e.id == id) return &e;
        return nullptr;
    }

    // Adds what came. The same content as an entry that is already there brings that entry to the top instead.
    // Returns the id of the entry that is on top now; 0 when nothing was added (empty, or too large to keep).
    uint64_t Add(ClipboardEntry e) {
        if (e.kind == ClipboardEntry::Kind::Text && (e.text.empty() || e.text.size() > kMaxTextChars)) return 0;
        if (e.kind == ClipboardEntry::Kind::Files && e.files.empty()) return 0;
        if (e.kind == ClipboardEntry::Kind::Image &&
            (e.width == 0 || e.height == 0 || static_cast<size_t>(e.width) * e.height > kMaxOneImagePixels ||
             e.pixels.size() < static_cast<size_t>(e.width) * e.height * 4)) {
            return 0;
        }
        e.fingerprint = Fingerprint(e);
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].fingerprint == e.fingerprint && SameContent(m_entries[i], e)) {
                ClipboardEntry again = std::move(m_entries[i]);
                again.time = e.time;
                m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
                m_entries.insert(m_entries.begin(), std::move(again));
                return m_entries.front().id;
            }
        }
        e.id = m_nextId++;
        if (e.kind == ClipboardEntry::Kind::Image && e.thumb.empty()) MakeThumbnail(e);
        m_entries.insert(m_entries.begin(), std::move(e));
        Trim();
        return m_entries.front().id;
    }

    bool Remove(uint64_t id) {
        const auto it = std::find_if(m_entries.begin(), m_entries.end(), [id](const ClipboardEntry& e) { return e.id == id; });
        if (it == m_entries.end()) return false;
        m_entries.erase(it);
        return true;
    }
    bool TogglePin(uint64_t id) {
        for (auto& e : m_entries) {
            if (e.id == id) { e.pinned = !e.pinned; return true; }
        }
        return false;
    }
    // Everything that is not pinned
    size_t ClearUnpinned() {
        const size_t before = m_entries.size();
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [](const ClipboardEntry& e) { return !e.pinned; }), m_entries.end());
        return before - m_entries.size();
    }

    // FNV-1a over the kind and the content
    static uint64_t Fingerprint(const ClipboardEntry& e) {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](const void* p, size_t n) {
            const auto* b = static_cast<const uint8_t*>(p);
            for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
        };
        const int kind = static_cast<int>(e.kind);
        mix(&kind, sizeof(kind));
        switch (e.kind) {
        case ClipboardEntry::Kind::Text:
            mix(e.text.data(), e.text.size() * sizeof(wchar_t));
            break;
        case ClipboardEntry::Kind::Files:
            for (const auto& f : e.files) { mix(f.data(), f.size() * sizeof(wchar_t)); mix(L"\n", sizeof(wchar_t)); }
            mix(&e.cut, sizeof(e.cut));
            break;
        case ClipboardEntry::Kind::Image:
            mix(&e.width, sizeof(e.width));
            mix(&e.height, sizeof(e.height));
            mix(e.pixels.data(), e.pixels.size());
            break;
        }
        return h;
    }

    // A preview of at most kThumbMax on each side, box-filtered, with premultiplied alpha (as Direct2D draws it)
    static void MakeThumbnail(ClipboardEntry& e) {
        e.thumb.clear();
        e.thumbW = e.thumbH = 0;
        if (e.width == 0 || e.height == 0 || e.pixels.size() < static_cast<size_t>(e.width) * e.height * 4) return;
        const double scale = std::min(1.0, std::min(static_cast<double>(kThumbMax) / e.width, static_cast<double>(kThumbMax) / e.height));
        const uint32_t tw = std::max(1u, static_cast<uint32_t>(e.width * scale + 0.5));
        const uint32_t th = std::max(1u, static_cast<uint32_t>(e.height * scale + 0.5));
        e.thumb.assign(static_cast<size_t>(tw) * th * 4, 0);
        for (uint32_t ty = 0; ty < th; ++ty) {
            const uint32_t y0 = static_cast<uint32_t>(static_cast<uint64_t>(ty) * e.height / th);
            const uint32_t y1 = std::max(y0 + 1, static_cast<uint32_t>(static_cast<uint64_t>(ty + 1) * e.height / th));
            for (uint32_t tx = 0; tx < tw; ++tx) {
                const uint32_t x0 = static_cast<uint32_t>(static_cast<uint64_t>(tx) * e.width / tw);
                const uint32_t x1 = std::max(x0 + 1, static_cast<uint32_t>(static_cast<uint64_t>(tx + 1) * e.width / tw));
                uint64_t sum[4] = {};
                uint64_t n = 0;
                // at most 8 x 8 samples of the box: enough for a preview, quick for a large screenshot
                const uint32_t sy = std::max(1u, (y1 - y0) / 8), sx = std::max(1u, (x1 - x0) / 8);
                for (uint32_t y = y0; y < y1; y += sy) {
                    const uint8_t* row = &e.pixels[(static_cast<size_t>(y) * e.width) * 4];
                    for (uint32_t x = x0; x < x1; x += sx) {
                        const uint8_t* p = row + static_cast<size_t>(x) * 4;
                        const uint32_t a = p[3];
                        sum[0] += p[0] * a; sum[1] += p[1] * a; sum[2] += p[2] * a; sum[3] += a;
                        ++n;
                    }
                }
                uint8_t* d = &e.thumb[(static_cast<size_t>(ty) * tw + tx) * 4];
                d[0] = static_cast<uint8_t>(sum[0] / (255 * n));
                d[1] = static_cast<uint8_t>(sum[1] / (255 * n));
                d[2] = static_cast<uint8_t>(sum[2] / (255 * n));
                d[3] = static_cast<uint8_t>(sum[3] / n);
            }
        }
        e.thumbW = tw;
        e.thumbH = th;
    }

private:
    static bool SameContent(const ClipboardEntry& a, const ClipboardEntry& b) {
        if (a.kind != b.kind) return false;
        switch (a.kind) {
        case ClipboardEntry::Kind::Text:  return a.text == b.text;
        case ClipboardEntry::Kind::Files: return a.files == b.files && a.cut == b.cut;
        case ClipboardEntry::Kind::Image: return a.width == b.width && a.height == b.height && a.pixels == b.pixels;
        }
        return false;
    }

    // The oldest entries that are not pinned go first: beyond kMaxEntries, and pictures beyond kMaxImages or
    // kMaxImageBytes
    void Trim() {
        size_t images = 0, imageBytes = 0;
        for (const auto& e : m_entries) {
            if (e.kind == ClipboardEntry::Kind::Image) { ++images; imageBytes += e.pixels.size(); }
        }
        for (size_t i = m_entries.size(); i-- > 1;) {           // the newest entry always stays
            ClipboardEntry& e = m_entries[i];
            if (e.pinned) continue;
            const bool tooMany = m_entries.size() > kMaxEntries;
            const bool tooManyImages = e.kind == ClipboardEntry::Kind::Image && (images > kMaxImages || imageBytes > kMaxImageBytes);
            if (!tooMany && !tooManyImages) continue;
            if (e.kind == ClipboardEntry::Kind::Image) { --images; imageBytes -= e.pixels.size(); }
            m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    std::vector<ClipboardEntry> m_entries;
    uint64_t m_nextId = 1;
};

// ---- Reading and writing the clipboard -------------------------------------------------------------------------
namespace ClipboardIo {
namespace Detail {

inline std::vector<uint8_t> HGlobalBytes(HANDLE h) {
    std::vector<uint8_t> out;
    if (!h) return out;
    const SIZE_T n = GlobalSize(h);
    if (const auto* p = static_cast<const uint8_t*>(GlobalLock(h))) {
        out.assign(p, p + n);
        GlobalUnlock(h);
    }
    return out;
}

inline HGLOBAL HGlobalOf(const void* data, size_t n) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, std::max<size_t>(1, n));
    if (!h) return nullptr;
    if (void* p = GlobalLock(h)) {
        if (n) std::memcpy(p, data, n);
        GlobalUnlock(h);
    }
    return h;
}

// A picture file in memory (PNG, BMP...) as straight BGRA pixels
inline bool DecodePicture(const uint8_t* data, size_t size, uint32_t& w, uint32_t& h, std::vector<uint8_t>& pixels) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    UINT fw = 0, fh = 0;
    if (!data || size < 8 || size > 0x7FFFFFF0u ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) || !wic ||
        FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data), static_cast<DWORD>(size))) ||
        FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder ||
        FAILED(decoder->GetFrame(0, &frame)) || !frame || FAILED(frame->GetSize(&fw, &fh)) || fw == 0 || fh == 0 ||
        static_cast<size_t>(fw) * fh > ClipboardHistory::kMaxOneImagePixels ||
        FAILED(wic->CreateFormatConverter(&conv)) || !conv ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        return false;
    }
    pixels.resize(static_cast<size_t>(fw) * fh * 4);
    if (FAILED(conv->CopyPixels(nullptr, fw * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return false;
    w = fw;
    h = fh;
    return true;
}

// Straight BGRA pixels as a PNG file in memory
inline bool EncodePng(const ClipboardEntry& e, std::vector<uint8_t>& out) {
    using Microsoft::WRL::ComPtr;
    out.clear();
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> bag;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (e.kind != ClipboardEntry::Kind::Image || e.pixels.empty() ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) || !wic ||
        FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) || !stream ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) || !encoder ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, &bag)) || !frame || FAILED(frame->Initialize(bag.Get())) ||
        FAILED(frame->SetSize(e.width, e.height)) || FAILED(frame->SetPixelFormat(&format)) ||
        FAILED(frame->WritePixels(e.height, e.width * 4, static_cast<UINT>(e.pixels.size()), const_cast<BYTE*>(e.pixels.data()))) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        return false;
    }
    STATSTG stat = {};
    HGLOBAL mem = nullptr;
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || FAILED(GetHGlobalFromStream(stream.Get(), &mem)) || !mem) return false;
    if (const auto* p = static_cast<const uint8_t*>(GlobalLock(mem))) {
        out.assign(p, p + static_cast<size_t>(stat.cbSize.QuadPart));
        GlobalUnlock(mem);
    }
    return !out.empty();
}

#ifdef _WIN32
// A packed DIB (CF_DIB / CF_DIBV5: header, colour table or masks, pixels) as a BMP file, which WIC reads in every
// bit depth
inline bool DibAsBmpFile(const std::vector<uint8_t>& dib, std::vector<uint8_t>& bmp) {
    if (dib.size() < sizeof(BITMAPINFOHEADER)) return false;
    BITMAPINFOHEADER bih = {};
    std::memcpy(&bih, dib.data(), sizeof(bih));
    if (bih.biSize < sizeof(BITMAPINFOHEADER) || bih.biSize > dib.size()) return false;
    size_t colors = 0;
    if (bih.biBitCount <= 8) colors = bih.biClrUsed ? bih.biClrUsed : (size_t(1) << bih.biBitCount);
    size_t masks = (bih.biSize == sizeof(BITMAPINFOHEADER) && bih.biCompression == BI_BITFIELDS) ? 12 : 0;
    const size_t pixelsAt = bih.biSize + masks + colors * 4;
    if (pixelsAt > dib.size()) return false;
    BITMAPFILEHEADER bfh = {};
    bfh.bfType = 0x4D42;
    bfh.bfSize = static_cast<DWORD>(sizeof(bfh) + dib.size());
    bfh.bfOffBits = static_cast<DWORD>(sizeof(bfh) + pixelsAt);
    bmp.resize(sizeof(bfh) + dib.size());
    std::memcpy(bmp.data(), &bfh, sizeof(bfh));
    std::memcpy(bmp.data() + sizeof(bfh), dib.data(), dib.size());
    return true;
}

// The pixels as a CF_DIB: 32-bit, rows from the bottom, as every program that pastes a picture reads it
inline HGLOBAL DibOf(const ClipboardEntry& e) {
    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(bih);
    bih.biWidth = static_cast<LONG>(e.width);
    bih.biHeight = static_cast<LONG>(e.height);
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = e.width * e.height * 4;
    std::vector<uint8_t> dib(sizeof(bih) + static_cast<size_t>(bih.biSizeImage));
    std::memcpy(dib.data(), &bih, sizeof(bih));
    for (uint32_t y = 0; y < e.height; ++y) {
        std::memcpy(dib.data() + sizeof(bih) + static_cast<size_t>(e.height - 1 - y) * e.width * 4,
                    e.pixels.data() + static_cast<size_t>(y) * e.width * 4, static_cast<size_t>(e.width) * 4);
    }
    return HGlobalOf(dib.data(), dib.size());
}
#endif

// The name the PNG format goes by on this platform's clipboard
inline UINT PngFormat() {
#ifdef _WIN32
    static const UINT s_fmt = RegisterClipboardFormatW(L"PNG");
#else
    static const UINT s_fmt = RegisterClipboardFormatW(L"image/png");
#endif
    return s_fmt;
}

} // namespace Detail

// Whether a password manager (or another program) asked for what is on the clipboard now not to be kept
inline bool ClipboardIsPrivate(HWND hwnd) {
#ifdef _WIN32
    static const UINT s_exclude = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    static const UINT s_viewerIgnore = RegisterClipboardFormatW(L"Clipboard Viewer Ignore");
    static const UINT s_canInclude = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    if (IsClipboardFormatAvailable(s_exclude) || IsClipboardFormatAvailable(s_viewerIgnore)) return true;
    if (IsClipboardFormatAvailable(s_canInclude) && OpenClipboard(hwnd)) {
        const std::vector<uint8_t> v = Detail::HGlobalBytes(GetClipboardData(s_canInclude));
        CloseClipboard();
        DWORD allowed = 1;
        if (v.size() >= sizeof(DWORD)) std::memcpy(&allowed, v.data(), sizeof(DWORD));
        if (allowed == 0) return true;
    }
    return false;
#else
    static const UINT s_hint = RegisterClipboardFormatW(L"x-kde-passwordManagerHint");
    if (!IsClipboardFormatAvailable(s_hint) || !OpenClipboard(hwnd)) return false;
    const std::vector<uint8_t> v = Detail::HGlobalBytes(GetClipboardData(s_hint));
    CloseClipboard();
    const std::string hint(v.begin(), v.end());
    return hint.rfind("secret", 0) == 0;
#endif
}

// What the clipboard holds now, as an entry: a list of files, else text, else a picture. False when it holds
// none of these, or content marked private (*outPrivate is then true).
inline bool Capture(HWND hwnd, ClipboardEntry& out, bool* outPrivate = nullptr) {
    out = ClipboardEntry();
    if (outPrivate) *outPrivate = false;
    if (ClipboardIsPrivate(hwnd)) {
        if (outPrivate) *outPrivate = true;
        return false;
    }
    GetLocalTime(&out.time);
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        std::vector<std::wstring> paths;
        bool move = false;
        if (FileEngine::ReadFilesFromClipboard(hwnd, paths, move) && !paths.empty()) {
            out.kind = ClipboardEntry::Kind::Files;
            out.files = std::move(paths);
            out.cut = move;
            return true;
        }
    }
    if (IsClipboardFormatAvailable(CF_UNICODETEXT) && OpenClipboard(hwnd)) {
        const std::vector<uint8_t> raw = Detail::HGlobalBytes(GetClipboardData(CF_UNICODETEXT));
        CloseClipboard();
        const size_t chars = raw.size() / sizeof(wchar_t);
        if (chars > 0) {
            std::wstring text(reinterpret_cast<const wchar_t*>(raw.data()), chars);
            text.resize(wcsnlen(text.c_str(), text.size()));
            if (!text.empty() && text.size() <= ClipboardHistory::kMaxTextChars) {
                out.kind = ClipboardEntry::Kind::Text;
                out.text = std::move(text);
                return true;
            }
        }
    }
    // A picture: PNG keeps transparency; otherwise the DIB every Windows program puts there
    std::vector<uint8_t> picture;
    if (IsClipboardFormatAvailable(Detail::PngFormat()) && OpenClipboard(hwnd)) {
        picture = Detail::HGlobalBytes(GetClipboardData(Detail::PngFormat()));
        CloseClipboard();
    }
#ifdef _WIN32
    if (picture.empty()) {
        const UINT dibFormat = IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5 : (IsClipboardFormatAvailable(CF_DIB) ? CF_DIB : 0);
        if (dibFormat && OpenClipboard(hwnd)) {
            const std::vector<uint8_t> dib = Detail::HGlobalBytes(GetClipboardData(dibFormat));
            CloseClipboard();
            Detail::DibAsBmpFile(dib, picture);
        }
    }
#endif
    if (!picture.empty() && Detail::DecodePicture(picture.data(), picture.size(), out.width, out.height, out.pixels)) {
        out.kind = ClipboardEntry::Kind::Image;
        return true;
    }
    return false;
}

// Puts an entry back on the clipboard, as it was copied
inline bool Restore(HWND hwnd, const ClipboardEntry& e) {
    switch (e.kind) {
    case ClipboardEntry::Kind::Text:
        return FileEngine::CopyTextToClipboard(hwnd, e.text);
    case ClipboardEntry::Kind::Files:
        return FileEngine::CopyFilesToClipboard(hwnd, e.files, e.cut);
    case ClipboardEntry::Kind::Image: {
        std::vector<uint8_t> png;
        Detail::EncodePng(e, png);
        if (!OpenClipboard(hwnd)) return false;
        EmptyClipboard();
        bool any = false;
#ifdef _WIN32
        if (HGLOBAL dib = Detail::DibOf(e)) { SetClipboardData(CF_DIB, dib); any = true; }
#endif
        if (!png.empty()) {
            if (HGLOBAL h = Detail::HGlobalOf(png.data(), png.size())) { SetClipboardData(Detail::PngFormat(), h); any = true; }
        }
        CloseClipboard();
        return any;
    }
    }
    return false;
}

// A file name for an entry saved into a folder: "Schowek 2026-11-20 15.42.07.txt", with " (2)" and so on
// while that name is taken
inline std::wstring FileNameFor(const ClipboardEntry& e, Lang lang, const std::wstring& folder) {
    wchar_t stamp[64] = {};
    swprintf_s(stamp, L"%04u-%02u-%02u %02u.%02u.%02u", e.time.wYear, e.time.wMonth, e.time.wDay, e.time.wHour, e.time.wMinute, e.time.wSecond);
    const std::wstring stem = std::wstring(lang == Lang::PL ? L"Schowek " : L"Clipboard ") + stamp;
    const std::wstring ext = (e.kind == ClipboardEntry::Kind::Image) ? L".png" : L".txt";
    const std::wstring dir = PathUtils::EnsureTrailingSlash(folder);
    std::wstring path = dir + stem + ext;
    for (int n = 2; n < 1000 && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; ++n) {
        path = dir + stem + L" (" + std::to_wstring(n) + L")" + ext;
    }
    return path;
}

// Saves a text (UTF-8) or a picture (PNG) entry as a new file in a folder
inline bool SaveAsFile(const ClipboardEntry& e, const std::wstring& folder, Lang lang, std::wstring& outPath) {
    outPath.clear();
    if (e.kind == ClipboardEntry::Kind::Files || folder.empty()) return false;
    const std::wstring path = FileNameFor(e, lang, folder);
    bool ok = false;
    if (e.kind == ClipboardEntry::Kind::Text) {
        std::string utf8 = PathUtils::WideToUtf8(e.text);
        ok = PathUtils::WriteFileBytes(path, utf8.data(), utf8.size());
    } else {
        std::vector<uint8_t> png;
        ok = Detail::EncodePng(e, png) && PathUtils::WriteFileBytes(path, png.data(), png.size());
    }
    if (ok) outPath = path;
    return ok;
}

} // namespace ClipboardIo
} // namespace Explorer2
