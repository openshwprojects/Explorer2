#include "ImageCache.h"
#include "ShellMenuEmulator.h"
#include "PathUtils.h"
#include <wrl/client.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <algorithm>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")

using Microsoft::WRL::ComPtr;

namespace Explorer2 {

constexpr size_t kMaxCachedThumbnails = 600;
constexpr size_t kMaxCachedFullImages = 6;

ImageCache::ImageCache() = default;

ImageCache::~ImageCache() {
    Shutdown();
}

void ImageCache::Initialize(HWND notifyHwnd) {
    m_notifyHwnd = notifyHwnd;
    m_running = true;

    unsigned int hwThreads = std::thread::hardware_concurrency();
    unsigned int numThumbWorkers = std::max(2u, std::min(8u, hwThreads > 1 ? hwThreads - 1 : 2u));

    for (unsigned int i = 0; i < numThumbWorkers; ++i) {
        m_thumbThreads.emplace_back(&ImageCache::ThumbWorkerLoop, this);
    }

    m_fullImgThread = std::thread(&ImageCache::FullImageWorkerLoop, this);
}

void ImageCache::Shutdown() {
    if (m_running.exchange(false)) {
        m_thumbQueueCv.notify_all();
        m_fullImgCv.notify_all();

        for (auto& t : m_thumbThreads) {
            if (t.joinable()) t.join();
        }
        m_thumbThreads.clear();

        if (m_fullImgThread.joinable()) {
            m_fullImgThread.join();
        }
    }
    DiscardDeviceResources();
}

void ImageCache::DiscardDeviceResources() {
    {
        std::lock_guard<std::mutex> lock(m_thumbCacheMutex);
        for (auto& kv : m_thumbBitmaps) {
            if (kv.second) kv.second->Release();
        }
        m_thumbBitmaps.clear();
        m_thumbLruOrder.clear();
        m_inFlightThumb.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_fullImgMutex);
        for (auto& kv : m_fullImgBitmaps) {
            if (kv.second) kv.second->Release();
        }
        m_fullImgBitmaps.clear();
        m_fullImgLru.clear();
        m_fullImgInFlight.clear();
    }
    for (auto& kv : m_sysIconBitmaps) {
        if (kv.second) kv.second->Release();
    }
    m_sysIconBitmaps.clear();
}

void ImageCache::BumpViewportGeneration() {
    uint64_t newGen = m_generation.fetch_add(1, std::memory_order_relaxed) + 1;
    std::lock_guard<std::mutex> qLock(m_thumbQueueMutex);
    if (!m_thumbQueue.empty()) {
        std::lock_guard<std::mutex> cLock(m_thumbCacheMutex);
        for (const auto& req : m_thumbQueue) {
            if (req.generation + 1 < newGen) {
                if (req.isFolderPreview) m_inFlightFolderPreview.erase(req.normKey);
                else m_inFlightThumb.erase(req.path);
            }
        }
        m_thumbQueue.erase(
            std::remove_if(m_thumbQueue.begin(), m_thumbQueue.end(),
                           [newGen](const ThumbRequest& r) { return r.generation + 1 < newGen; }),
            m_thumbQueue.end());
    }
}

bool ImageCache::DecodeImageFromDiskZeroLock(
    IWICImagingFactory* wicFactory,
    const std::wstring& path,
    uint32_t maxDimension,
    RawImageBuffer& outBuf)
{
    outBuf = {};
    if (!wicFactory || path.empty()) {
        outBuf.failed = true;
        return false;
    }

    // Open with full sharing (READ | WRITE | DELETE), copy compressed bytes into RAM,
    // and CLOSE THE FILE HANDLE IMMEDIATELY before decoding!
    HANDLE hFile = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
        outBuf.failed = true;
        return false;
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0 || fileSize.QuadPart > (256LL * 1024 * 1024)) {
        CloseHandle(hFile);
        outBuf.failed = true;
        return false;
    }

    std::vector<uint8_t> fileBytes(static_cast<size_t>(fileSize.QuadPart));
    DWORD bytesRead = 0;
    BOOL readOk = ReadFile(hFile, fileBytes.data(), static_cast<DWORD>(fileBytes.size()), &bytesRead, nullptr);
    CloseHandle(hFile);

    if (!readOk || bytesRead == 0) {
        outBuf.failed = true;
        return false;
    }

    ComPtr<IWICStream> stream;
    if (FAILED(wicFactory->CreateStream(&stream)) || !stream) {
        outBuf.failed = true;
        return false;
    }

    if (FAILED(stream->InitializeFromMemory(fileBytes.data(), bytesRead))) {
        outBuf.failed = true;
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wicFactory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder) {
        outBuf.failed = true;
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) {
        outBuf.failed = true;
        return false;
    }

    UINT origW = 0, origH = 0;
    frame->GetSize(&origW, &origH);
    if (origW == 0 || origH == 0) {
        outBuf.failed = true;
        return false;
    }

    outBuf.origWidth = origW;
    outBuf.origHeight = origH;

    UINT targetW = origW;
    UINT targetH = origH;
    if (maxDimension > 0 && (origW > maxDimension || origH > maxDimension)) {
        if (origW >= origH) {
            targetW = maxDimension;
            targetH = std::max(1u, static_cast<UINT>((static_cast<uint64_t>(origH) * maxDimension) / origW));
        } else {
            targetH = maxDimension;
            targetW = std::max(1u, static_cast<UINT>((static_cast<uint64_t>(origW) * maxDimension) / origH));
        }
    }

    IWICBitmapSource* sourceToConvert = frame.Get();
    ComPtr<IWICBitmapScaler> scaler;

    if (targetW != origW || targetH != origH) {
        if (SUCCEEDED(wicFactory->CreateBitmapScaler(&scaler)) && scaler) {
            WICBitmapInterpolationMode interp = (maxDimension <= 300)
                ? WICBitmapInterpolationModeLinear
                : WICBitmapInterpolationModeFant;
            if (SUCCEEDED(scaler->Initialize(frame.Get(), targetW, targetH, interp))) {
                sourceToConvert = scaler.Get();
            }
        }
    }

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory->CreateFormatConverter(&converter)) || !converter) {
        outBuf.failed = true;
        return false;
    }

    if (FAILED(converter->Initialize(
            sourceToConvert,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeMedianCut))) {
        outBuf.failed = true;
        return false;
    }

    outBuf.width = targetW;
    outBuf.height = targetH;
    UINT stride = targetW * 4;
    UINT bufSize = stride * targetH;
    outBuf.bgraPixels.resize(bufSize);

    if (FAILED(converter->CopyPixels(nullptr, stride, bufSize, outBuf.bgraPixels.data()))) {
        outBuf.bgraPixels.clear();
        outBuf.failed = true;
        return false;
    }

    return true;
}

bool ImageCache::DecodeShellThumbnail(
    const std::wstring& path,
    uint32_t maxDimension,
    RawImageBuffer& outBuf)
{
    outBuf = {};
    ComPtr<IShellItemImageFactory> imgFactory;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&imgFactory))) || !imgFactory) {
        outBuf.failed = true;
        return false;
    }

    SIZE sz = { static_cast<LONG>(maxDimension), static_cast<LONG>(maxDimension) };
    HBITMAP hbm = nullptr;
    if (FAILED(imgFactory->GetImage(sz, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &hbm)) || !hbm) {
        outBuf.failed = true;
        return false;
    }

    BITMAP bm = {};
    GetObjectW(hbm, sizeof(bm), &bm);
    if (bm.bmWidth <= 0 || bm.bmHeight <= 0) {
        DeleteObject(hbm);
        outBuf.failed = true;
        return false;
    }

    int w = bm.bmWidth;
    int h = bm.bmHeight;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    outBuf.width = static_cast<uint32_t>(w);
    outBuf.height = static_cast<uint32_t>(h);
    outBuf.origWidth = outBuf.width;
    outBuf.origHeight = outBuf.height;
    outBuf.bgraPixels.resize(static_cast<size_t>(w * h * 4));

    HDC hdc = GetDC(nullptr);
    GetDIBits(hdc, hbm, 0, h, outBuf.bgraPixels.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    DeleteObject(hbm);

    // Ensure opaque alpha if shell thumbnail returned 0 alpha channel
    for (size_t i = 0; i < outBuf.bgraPixels.size(); i += 4) {
        outBuf.bgraPixels[i + 3] = 255;
    }
    return true;
}

void ImageCache::ThumbWorkerLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<IWICImagingFactory> wicFactory;
    CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory));

    while (m_running) {
        ThumbRequest req;
        {
            std::unique_lock<std::mutex> lock(m_thumbQueueMutex);
            m_thumbQueueCv.wait(lock, [this]() { return !m_running || !m_thumbQueue.empty(); });
            if (!m_running) break;

            req = std::move(m_thumbQueue.back());
            m_thumbQueue.pop_back();
        }

        uint64_t curGen = m_generation.load(std::memory_order_relaxed);
        if (req.generation + 2 < curGen) {
            std::lock_guard<std::mutex> cLock(m_thumbCacheMutex);
            if (req.isFolderPreview) m_inFlightFolderPreview.erase(req.normKey);
            else m_inFlightThumb.erase(req.path);
            continue;
        }

        if (req.isFolderPreview) {
            FolderPreviewData prevData;
            prevData.ready = true;

            std::vector<FolderPreviewItem> mediaItems;
            std::vector<FolderPreviewItem> fileItems;
            std::vector<FolderPreviewItem> subDirItems;

            auto scanDirForPreview = [&](const std::wstring& dirToScan, bool countTotal, bool allowSubDirs) {
                std::wstring prefix = PathUtils::EnsureTrailingSlash(dirToScan);
                std::wstring pattern = prefix + L"*";
                WIN32_FIND_DATAW ffd;
                HANDLE hFind = FindFirstFileExW(
                    pattern.c_str(),
                    FindExInfoBasic,
                    &ffd,
                    FindExSearchNameMatch,
                    nullptr,
                    FIND_FIRST_EX_LARGE_FETCH);
                if (hFind == INVALID_HANDLE_VALUE) return;

                int scanned = 0;
                do {
                    const wchar_t* name = ffd.cFileName;
                    if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) continue;
                    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
                    if (_wcsicmp(name, L"desktop.ini") == 0 || _wcsicmp(name, L"thumbs.db") == 0) continue;

                    if (countTotal) ++prevData.totalCount;
                    ++scanned;

                    bool isDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    if (isDir) {
                        if (allowSubDirs && subDirItems.size() < 4) {
                            FolderPreviewItem fp;
                            fp.name = name;
                            fp.fullPath = prefix + name;
                            fp.isDirectory = true;
                            subDirItems.push_back(std::move(fp));
                        }
                    } else {
                        const wchar_t* dot = wcsrchr(name, L'.');
                        std::wstring ext = dot ? PathUtils::ToLower(dot) : L"";
                        bool isImg = IsImageExtension(ext);
                        bool isVid = !isImg && IsVideoExtension(ext);
                        if ((isImg || isVid) && mediaItems.size() < 4) {
                            FolderPreviewItem fp;
                            fp.name = name;
                            fp.fullPath = prefix + name;
                            fp.ext = std::move(ext);
                            fp.isImage = isImg;
                            fp.isVideo = isVid;
                            mediaItems.push_back(std::move(fp));
                        } else if (!isImg && !isVid && fileItems.size() < 4) {
                            FolderPreviewItem fp;
                            fp.name = name;
                            fp.fullPath = prefix + name;
                            fp.ext = std::move(ext);
                            fileItems.push_back(std::move(fp));
                        }
                    }
                } while (scanned < 120 && FindNextFileW(hFind, &ffd));
                FindClose(hFind);
            };

            scanDirForPreview(req.path, true, true);

            // If fewer than 4 images in root, peek 1 level into the first 2 subfolders for images!
            for (size_t s = 0; s < subDirItems.size() && s < 2 && mediaItems.size() < 4; ++s) {
                scanDirForPreview(subDirItems[s].fullPath, false, false);
            }

            for (auto& m : mediaItems) {
                if (prevData.items.size() < 4) prevData.items.push_back(std::move(m));
            }
            for (auto& f : fileItems) {
                if (prevData.items.size() < 4) prevData.items.push_back(std::move(f));
            }
            for (auto& d : subDirItems) {
                if (prevData.items.size() < 4) prevData.items.push_back(std::move(d));
            }

            {
                std::lock_guard<std::mutex> cLock(m_thumbCacheMutex);
                m_inFlightFolderPreview.erase(req.normKey);
                m_folderPreviewCache[req.normKey] = std::move(prevData);
            }
            if (m_notifyHwnd && !m_thumbNotifyPending.exchange(true, std::memory_order_relaxed)) {
                PostMessageW(m_notifyHwnd, WM_APP_THUMB_READY, 0, 0);
            }
            continue;
        }

        RawImageBuffer decoded;
        const wchar_t* dot = wcsrchr(req.path.c_str(), L'.');
        std::wstring ext = dot ? PathUtils::ToLower(dot) : L"";
        if (IsVideoExtension(ext)) {
            DecodeShellThumbnail(req.path, 384, decoded);
        } else {
            DecodeImageFromDiskZeroLock(wicFactory.Get(), req.path, 384, decoded);
        }

        {
            std::lock_guard<std::mutex> cLock(m_thumbCacheMutex);
            m_inFlightThumb.erase(req.path);
            if (decoded.failed) {
                m_failedThumbs[req.path] = true;
            } else {
                m_pendingThumbCpu[req.path] = std::move(decoded);
            }
        }

        if (m_notifyHwnd && !m_thumbNotifyPending.exchange(true, std::memory_order_relaxed)) {
            PostMessageW(m_notifyHwnd, WM_APP_THUMB_READY, 0, 0);
        }
    }

    CoUninitialize();
}

ID2D1Bitmap* ImageCache::GetOrQueueThumbnail(ID2D1RenderTarget* rt, const std::wstring& path) {
    if (!rt || path.empty()) return nullptr;
    m_thumbNotifyPending.store(false, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(m_thumbCacheMutex);

        auto cpuIt = m_pendingThumbCpu.find(path);
        if (cpuIt != m_pendingThumbCpu.end()) {
            const RawImageBuffer& buf = cpuIt->second;
            if (!buf.failed && !buf.bgraPixels.empty()) {
                D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                ID2D1Bitmap* bmp = nullptr;
                if (SUCCEEDED(rt->CreateBitmap(
                        D2D1::SizeU(buf.width, buf.height),
                        buf.bgraPixels.data(),
                        buf.width * 4,
                        &props,
                        &bmp)) && bmp)
                {
                    while (m_thumbLruOrder.size() >= kMaxCachedThumbnails) {
                        std::wstring oldest = m_thumbLruOrder.front();
                        m_thumbLruOrder.pop_front();
                        auto oldIt = m_thumbBitmaps.find(oldest);
                        if (oldIt != m_thumbBitmaps.end()) {
                            if (oldIt->second) oldIt->second->Release();
                            m_thumbBitmaps.erase(oldIt);
                        }
                    }
                    m_thumbBitmaps[path] = bmp;
                    m_thumbLruOrder.push_back(path);
                }
            }
            m_pendingThumbCpu.erase(cpuIt);
        }

        auto bmpIt = m_thumbBitmaps.find(path);
        if (bmpIt != m_thumbBitmaps.end()) {
            return bmpIt->second;
        }

        if (m_failedThumbs.find(path) != m_failedThumbs.end() ||
            m_inFlightThumb.find(path) != m_inFlightThumb.end()) {
            return nullptr;
        }

        m_inFlightThumb[path] = true;
    }

    {
        std::lock_guard<std::mutex> qLock(m_thumbQueueMutex);
        m_thumbQueue.push_back(ThumbRequest{ path, m_generation.load(std::memory_order_relaxed), false, L"" });
    }
    m_thumbQueueCv.notify_one();
    return nullptr;
}

bool ImageCache::GetOrQueueFolderPreview(const std::wstring& folderPath, const std::wstring& normKey, FolderPreviewData& outPreview) {
    if (folderPath.empty() || normKey.empty() || folderPath == L"THIS_PC") return false;
    m_thumbNotifyPending.store(false, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(m_thumbCacheMutex);
        auto it = m_folderPreviewCache.find(normKey);
        if (it != m_folderPreviewCache.end()) {
            outPreview = it->second;
            return true;
        }
        if (m_inFlightFolderPreview.find(normKey) != m_inFlightFolderPreview.end()) {
            return false;
        }
        m_inFlightFolderPreview[normKey] = true;
    }

    {
        std::lock_guard<std::mutex> qLock(m_thumbQueueMutex);
        m_thumbQueue.push_back(ThumbRequest{ folderPath, m_generation.load(std::memory_order_relaxed), true, normKey });
    }
    m_thumbQueueCv.notify_one();
    return false;
}

void ImageCache::FullImageWorkerLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<IWICImagingFactory> wicFactory;
    CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory));

    while (m_running) {
        std::wstring path;
        {
            std::unique_lock<std::mutex> lock(m_fullImgMutex);
            m_fullImgCv.wait(lock, [this]() { return !m_running || !m_fullImgQueue.empty(); });
            if (!m_running) break;

            path = std::move(m_fullImgQueue.front());
            m_fullImgQueue.pop_front();
        }

        RawImageBuffer decoded;
        DecodeImageFromDiskZeroLock(wicFactory.Get(), path, 4096, decoded);

        {
            std::lock_guard<std::mutex> lock(m_fullImgMutex);
            m_fullImgInFlight.erase(path);
            if (decoded.failed) {
                m_fullImgFailed[path] = true;
            } else {
                m_fullImgOrigDims[path] = { decoded.origWidth, decoded.origHeight };
                m_fullImgCpu[path] = std::move(decoded);
            }
        }

        if (m_notifyHwnd) {
            PostMessageW(m_notifyHwnd, WM_APP_FULLIMG_READY, 0, 0);
        }
    }

    CoUninitialize();
}

FullImageMeta ImageCache::GetOrQueueFullImage(
    ID2D1RenderTarget* rt,
    const std::wstring& currentPath,
    const std::wstring& prevPath,
    const std::wstring& nextPath)
{
    FullImageMeta meta = {};
    if (!rt || currentPath.empty()) return meta;

    std::lock_guard<std::mutex> lock(m_fullImgMutex);

    for (auto it = m_fullImgCpu.begin(); it != m_fullImgCpu.end(); ) {
        const std::wstring& p = it->first;
        const RawImageBuffer& buf = it->second;
        if (!buf.failed && !buf.bgraPixels.empty() && m_fullImgBitmaps.find(p) == m_fullImgBitmaps.end()) {
            D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            ID2D1Bitmap* bmp = nullptr;
            if (SUCCEEDED(rt->CreateBitmap(
                    D2D1::SizeU(buf.width, buf.height),
                    buf.bgraPixels.data(),
                    buf.width * 4,
                    &props,
                    &bmp)) && bmp)
            {
                while (m_fullImgLru.size() >= kMaxCachedFullImages) {
                    std::wstring oldest = m_fullImgLru.front();
                    m_fullImgLru.pop_front();
                    auto oldIt = m_fullImgBitmaps.find(oldest);
                    if (oldIt != m_fullImgBitmaps.end()) {
                        if (oldIt->second) oldIt->second->Release();
                        m_fullImgBitmaps.erase(oldIt);
                    }
                }
                m_fullImgBitmaps[p] = bmp;
                m_fullImgLru.push_back(p);
            }
        }
        it = m_fullImgCpu.erase(it);
    }

    auto queueIfNeeded = [&](const std::wstring& p, bool highPriority) {
        if (p.empty()) return;
        if (m_fullImgBitmaps.find(p) != m_fullImgBitmaps.end()) return;
        if (m_fullImgFailed.find(p) != m_fullImgFailed.end()) return;
        if (m_fullImgInFlight.find(p) != m_fullImgInFlight.end()) return;

        m_fullImgInFlight[p] = true;
        if (highPriority) {
            m_fullImgQueue.push_front(p);
        } else {
            m_fullImgQueue.push_back(p);
        }
        m_fullImgCv.notify_one();
    };

    queueIfNeeded(currentPath, true);
    queueIfNeeded(nextPath, false);
    queueIfNeeded(prevPath, false);

    auto bmpIt = m_fullImgBitmaps.find(currentPath);
    if (bmpIt != m_fullImgBitmaps.end()) {
        meta.bitmap = bmpIt->second;
        auto dimIt = m_fullImgOrigDims.find(currentPath);
        if (dimIt != m_fullImgOrigDims.end()) {
            meta.origWidth = dimIt->second.first;
            meta.origHeight = dimIt->second.second;
        }
        return meta;
    }

    if (m_fullImgFailed.find(currentPath) != m_fullImgFailed.end()) {
        meta.failed = true;
        return meta;
    }

    meta.loading = true;
    return meta;
}

void ImageCache::EvictPath(const std::wstring& path) {
    {
        std::lock_guard<std::mutex> lock(m_thumbCacheMutex);
        auto it = m_thumbBitmaps.find(path);
        if (it != m_thumbBitmaps.end()) {
            if (it->second) it->second->Release();
            m_thumbBitmaps.erase(it);
        }
        m_pendingThumbCpu.erase(path);
        m_failedThumbs.erase(path);
        m_thumbLruOrder.remove(path);
    }
    {
        std::lock_guard<std::mutex> lock(m_fullImgMutex);
        auto it = m_fullImgBitmaps.find(path);
        if (it != m_fullImgBitmaps.end()) {
            if (it->second) it->second->Release();
            m_fullImgBitmaps.erase(it);
        }
        m_fullImgCpu.erase(path);
        m_fullImgFailed.erase(path);
        m_fullImgOrigDims.erase(path);
        m_fullImgLru.remove(path);
    }
}

void ImageCache::ClearFullImageCache() {
    std::lock_guard<std::mutex> lock(m_fullImgMutex);
    for (auto& kv : m_fullImgBitmaps) {
        if (kv.second) kv.second->Release();
    }
    m_fullImgBitmaps.clear();
    m_fullImgCpu.clear();
    m_fullImgFailed.clear();
    m_fullImgOrigDims.clear();
    m_fullImgLru.clear();
    m_fullImgQueue.clear();
    m_fullImgInFlight.clear();
}

void ImageCache::QueueRotateImages(const std::vector<std::wstring>& paths, bool clockwise) {
    if (paths.empty()) return;

    std::vector<std::wstring> toRotate;
    {
        std::lock_guard<std::mutex> lock(m_rotateMutex);
        for (const auto& p : paths) {
            std::wstring nk = PathUtils::NormalizePathKey(p);
            if (!nk.empty() && m_rotatingNormKeys.find(nk) == m_rotatingNormKeys.end()) {
                m_rotatingNormKeys.insert(nk);
                toRotate.push_back(p);
            }
        }
    }
    if (toRotate.empty()) return;

    HWND notifyHwnd = m_notifyHwnd;
    std::thread([this, toRotate = std::move(toRotate), clockwise, notifyHwnd]() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (const auto& imgPath : toRotate) {
            if (!m_running) break;
            ShellMenuEmulator::RotateImageFile(imgPath, clockwise);

            std::wstring nk = PathUtils::NormalizePathKey(imgPath);
            {
                std::lock_guard<std::mutex> lock(m_rotateMutex);
                m_rotatingNormKeys.erase(nk);
                m_completedRotationPaths.push_back(imgPath);
            }
            if (notifyHwnd && m_running) {
                PostMessageW(notifyHwnd, WM_APP_IMAGE_ROTATED, 0, 0);
            }
        }
        CoUninitialize();
    }).detach();
}

bool ImageCache::IsPathRotating(const std::wstring& normKey) const {
    if (normKey.empty()) return false;
    std::lock_guard<std::mutex> lock(m_rotateMutex);
    return m_rotatingNormKeys.find(normKey) != m_rotatingNormKeys.end();
}

std::vector<std::wstring> ImageCache::TakeCompletedRotations() {
    std::vector<std::wstring> done;
    {
        std::lock_guard<std::mutex> lock(m_rotateMutex);
        done.swap(m_completedRotationPaths);
    }
    for (const auto& p : done) {
        EvictPath(p);
    }
    return done;
}

ID2D1Bitmap* ImageCache::GetSystemIconBitmap(ID2D1RenderTarget* rt, int sysIconIndex, bool largeIcon) {
    if (!rt || sysIconIndex < 0) return nullptr;

    uint64_t key = (static_cast<uint64_t>(sysIconIndex) << 1) | (largeIcon ? 1ULL : 0ULL);
    auto it = m_sysIconBitmaps.find(key);
    if (it != m_sysIconBitmaps.end()) {
        return it->second;
    }

    ComPtr<IImageList> imgList;
    int shil = largeIcon ? SHIL_EXTRALARGE : SHIL_SMALL;
    if (FAILED(SHGetImageList(shil, IID_PPV_ARGS(&imgList))) || !imgList) {
        return nullptr;
    }

    HICON hIcon = nullptr;
    if (FAILED(imgList->GetIcon(sysIconIndex, ILD_TRANSPARENT, &hIcon)) || !hIcon) {
        return nullptr;
    }

    ICONINFO iconInfo = {};
    if (!GetIconInfo(hIcon, &iconInfo)) {
        DestroyIcon(hIcon);
        return nullptr;
    }

    BITMAP bm = {};
    GetObjectW(iconInfo.hbmColor ? iconInfo.hbmColor : iconInfo.hbmMask, sizeof(bm), &bm);
    int w = bm.bmWidth > 0 ? bm.bmWidth : (largeIcon ? 48 : 16);
    int h = bm.bmHeight > 0 ? bm.bmHeight : (largeIcon ? 48 : 16);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<uint8_t> pixels(static_cast<size_t>(w * h * 4), 0);
    HDC hdc = GetDC(nullptr);
    if (iconInfo.hbmColor) {
        GetDIBits(hdc, iconInfo.hbmColor, 0, h, pixels.data(), &bmi, DIB_RGB_COLORS);
    } else {
        void* bits = nullptr;
        HBITMAP hDib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (hDib && bits) {
            HDC memDc = CreateCompatibleDC(hdc);
            HGDIOBJ oldObj = SelectObject(memDc, hDib);
            DrawIconEx(memDc, 0, 0, hIcon, w, h, 0, nullptr, DI_NORMAL);
            memcpy(pixels.data(), bits, pixels.size());
            SelectObject(memDc, oldObj);
            DeleteDC(memDc);
            DeleteObject(hDib);
        }
    }
    ReleaseDC(nullptr, hdc);

    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
    if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
    DestroyIcon(hIcon);

    bool hasAlpha = false;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        if (pixels[i + 3] != 0) {
            hasAlpha = true;
            break;
        }
    }
    for (size_t i = 0; i < pixels.size(); i += 4) {
        uint8_t a = hasAlpha ? pixels[i + 3] : 255;
        pixels[i + 0] = static_cast<uint8_t>((pixels[i + 0] * a) / 255);
        pixels[i + 1] = static_cast<uint8_t>((pixels[i + 1] * a) / 255);
        pixels[i + 2] = static_cast<uint8_t>((pixels[i + 2] * a) / 255);
        pixels[i + 3] = a;
    }

    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ID2D1Bitmap* bmp = nullptr;
    rt->CreateBitmap(D2D1::SizeU(w, h), pixels.data(), w * 4, &props, &bmp);
    m_sysIconBitmaps[key] = bmp;
    return bmp;
}

} // namespace Explorer2
