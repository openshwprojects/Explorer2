#pragma once
#include "Types.h"
#include "ShellMenuEmulator.h"
#include <d2d1.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>

namespace Explorer2 {

struct RawImageBuffer {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t origWidth = 0;
    uint32_t origHeight = 0;
    std::vector<uint8_t> bgraPixels;
    bool failed = false;
};

struct FullImageMeta {
    ID2D1Bitmap* bitmap = nullptr;
    uint32_t origWidth = 0;
    uint32_t origHeight = 0;
    bool loading = false;
    bool failed = false;
};

struct FolderPreviewItem {
    std::wstring name;
    std::wstring fullPath;
    std::wstring ext;
    bool isDirectory = false;
    bool isImage = false;
    bool isVideo = false;
};

struct FolderPreviewData {
    bool ready = false;
    int totalCount = 0;
    std::vector<FolderPreviewItem> items; // Up to 4 preview items inside the folder
};

class ImageCache {
public:
    ImageCache();
    ~ImageCache();

    void Initialize(HWND notifyHwnd);
    void Shutdown();
    void DiscardDeviceResources();

    // Increments viewport generation so off-screen queued thumbnails are skipped immediately
    void BumpViewportGeneration();

    // Returns cached thumbnail bitmap if ready, or queues high-priority async decode and returns nullptr
    ID2D1Bitmap* GetOrQueueThumbnail(ID2D1RenderTarget* rt, const std::wstring& path);

    // Returns cached 2x2 folder content preview items if ready, or queues fast async peek
    bool GetOrQueueFolderPreview(const std::wstring& folderPath, const std::wstring& normKey, FolderPreviewData& outPreview);

    // Full-resolution viewer pipeline with automatic N-1 / N+1 prefetching
    FullImageMeta GetOrQueueFullImage(
        ID2D1RenderTarget* rt,
        const std::wstring& currentPath,
        const std::wstring& prevPath,
        const std::wstring& nextPath);

    // Remove a deleted file from caches
    void EvictPath(const std::wstring& path);

    void ClearFullImageCache();

    // Async non-blocking image rotation & transformation with per-item status tracking
    void QueueRotateImages(const std::vector<std::wstring>& paths, bool clockwise);
    void QueueTransformImages(const std::vector<std::wstring>& paths, const ImageTransformParams& params);
    bool IsPathRotating(const std::wstring& normKey) const;
    bool IsAnyImageTransforming() const;
    std::vector<std::wstring> TakeCompletedRotations();
    std::vector<ImageTransformResult> TakeCompletedTransforms();

    // System icon cache (folder, .py, .json, drives, etc.)
    ID2D1Bitmap* GetSystemIconBitmap(ID2D1RenderTarget* rt, int sysIconIndex, bool largeIcon);

private:
    struct ThumbRequest {
        std::wstring path;
        uint64_t generation = 0;
        bool isFolderPreview = false;
        std::wstring normKey;
    };

    void ThumbWorkerLoop();
    void FullImageWorkerLoop();

    static bool DecodeImageFromDiskZeroLock(
        IWICImagingFactory* wicFactory,
        const std::wstring& path,
        uint32_t maxDimension,
        RawImageBuffer& outBuf);

    static bool DecodeShellThumbnail(
        const std::wstring& path,
        uint32_t maxDimension,
        RawImageBuffer& outBuf);

    HWND m_notifyHwnd = nullptr;
    std::atomic<bool> m_running{ false };
    std::atomic<uint64_t> m_generation{ 1 };
    std::atomic<bool> m_thumbNotifyPending{ false };

    // Thumbnail worker pool
    std::vector<std::thread> m_thumbThreads;
    std::mutex m_thumbQueueMutex;
    std::condition_variable m_thumbQueueCv;
    std::vector<ThumbRequest> m_thumbQueue; // LIFO / viewport batch

    // Decoded thumbnail CPU/GPU caches
    std::mutex m_thumbCacheMutex;
    std::unordered_map<std::wstring, RawImageBuffer> m_pendingThumbCpu;
    std::unordered_map<std::wstring, bool> m_inFlightThumb;
    std::unordered_map<std::wstring, ID2D1Bitmap*> m_thumbBitmaps;
    std::unordered_map<std::wstring, bool> m_failedThumbs;
    std::list<std::wstring> m_thumbLruOrder;

    // Folder content preview cache (for Medium / Large / ExtraLarge / Jumbo folder icons)
    std::unordered_map<std::wstring, FolderPreviewData> m_folderPreviewCache;
    std::unordered_map<std::wstring, bool> m_inFlightFolderPreview;

    // Full-resolution viewer worker & cache (stores current + prev + next)
    std::thread m_fullImgThread;
    std::mutex m_fullImgMutex;
    std::condition_variable m_fullImgCv;
    std::deque<std::wstring> m_fullImgQueue;
    std::unordered_map<std::wstring, RawImageBuffer> m_fullImgCpu;
    std::unordered_map<std::wstring, bool> m_fullImgInFlight;
    std::unordered_map<std::wstring, ID2D1Bitmap*> m_fullImgBitmaps;
    std::unordered_map<std::wstring, std::pair<uint32_t, uint32_t>> m_fullImgOrigDims;
    std::unordered_map<std::wstring, bool> m_fullImgFailed;
    std::list<std::wstring> m_fullImgLru;

    // System icon bitmaps
    std::unordered_map<uint64_t, ID2D1Bitmap*> m_sysIconBitmaps;

    // Background rotation & image transformation tracking
    mutable std::mutex m_rotateMutex;
    std::unordered_set<std::wstring> m_rotatingNormKeys;
    std::vector<std::wstring> m_completedRotationPaths;
    std::vector<ImageTransformResult> m_completedTransforms;
};

} // namespace Explorer2
