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
#include <memory>

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

    // A system icon: a rectangle in one of the shared icon textures
    struct SystemIcon {
        ID2D1Bitmap* texture = nullptr;     // shared by many icons; owned by the cache
        D2D1_RECT_F source = {};            // where the icon is in it, in pixels
        float width = 0.0f;                 // the icon's size in pixels
        float height = 0.0f;
        explicit operator bool() const { return texture != nullptr; }
    };

    // System icon cache (folder, .exe, .dll, .py, .json, drives, etc.)
    // sizeTier: 0 = 16x16 (SHIL_SMALL), 1 = 48x48 (SHIL_EXTRALARGE), 2 = 256x256 (SHIL_JUMBO)
    // Once Initialize has run, an icon not made yet is made by the icon worker: Windows extracts some icons
    // from their files on first use, which can take tens of milliseconds for one icon, too long for a paint.
    // Until it is ready this returns an empty icon (the caller draws a placeholder), and WM_APP_THUMB_READY
    // says when to paint again. Without Initialize the icon is made here and now.
    // The icons of one size share a few large textures: a new icon is copied into a free cell of one, which
    // is far cheaper for the graphics driver than a texture of its own for every icon.
    SystemIcon GetSystemIcon(ID2D1RenderTarget* rt, int sysIconIndex, int sizeTier);
    static void DrawSystemIcon(ID2D1RenderTarget* rt, const SystemIcon& icon, const D2D1_RECT_F& dest, float opacity) {
        if (rt && icon.texture) {
            rt->DrawBitmap(icon.texture, dest, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &icon.source);
        }
    }

    // Non-blocking STA per-file Shell icon index resolver (.exe custom icons, .lnk shortcuts, .msi, .scr, .url)
    int GetOrQueuePerFileIconIndex(const std::wstring& path, int fallbackSysIconIndex);

    // Icons and thumbnails become Direct2D bitmaps while the window paints, a millisecond or two each, plus
    // work the graphics driver does later for every new bitmap. When many are new at once (another folder,
    // another icon size) a paint makes only as many as fit in its budget, at most kUploadsPerPaint, and leaves
    // the rest, drawn as placeholders, for the next paint, so that the window keeps answering input. Called from OnPaint: BeginPaintBudget before drawing, and after it
    // TakePaintBudgetExceeded, which says whether another paint is needed. Without a budget there is no limit.
    void BeginPaintBudget(double milliseconds);
    bool TakePaintBudgetExceeded();

private:
    struct ThumbRequest {
        std::wstring path;
        uint64_t generation = 0;
        bool isFolderPreview = false;
        std::wstring normKey;
    };

    struct IconRequest {
        std::wstring path;
        int fallbackIndex = 0;
        uint64_t generation = 0;
    };

    void ThumbWorkerLoop();
    void FullImageWorkerLoop();
    void IconWorkerLoop();

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

    // System icon pixels (premultiplied BGRA) as the icon worker makes them for GetSystemIcon
    struct IconPixels {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> bgra;
        bool failed = false;
    };
    static bool RenderSystemIconPixels(int sysIconIndex, int sizeTier, IconPixels& out);
    std::unordered_map<uint64_t, IconPixels> m_sysIconPixels;      // made, waiting to be uploaded (m_iconMutex)
    std::unordered_set<uint64_t> m_sysIconInFlight;                // asked for, not made yet (m_iconMutex)
    std::vector<uint64_t> m_sysIconQueue;                          // (m_iconMutex)

    // The shared icon textures, a list of pages per cell size (32, 96 and 256 pixels; an icon goes to the
    // smallest that takes it). A page is a grid of equal cells; every icon keeps a transparent border of one
    // pixel, so that drawing it scaled never picks up a neighbour.
    struct IconPage {
        ID2D1Bitmap* texture = nullptr;
        uint32_t cell = 0;                  // cell size in pixels, border included
        uint32_t columns = 0;
        uint32_t capacity = 0;
        uint32_t used = 0;
    };
    bool PlaceInIconPage(ID2D1RenderTarget* rt, int sizeTier, const IconPixels& pixels, SystemIcon& out);
    std::vector<IconPage> m_iconPages[3];
    std::unordered_map<uint64_t, SystemIcon> m_sysIcons;            // an empty one: Windows has no such icon

    // STA per-file icon resolver
    std::thread m_iconThread;
    std::mutex m_iconMutex;
    std::condition_variable m_iconCv;
    std::vector<IconRequest> m_iconQueue;
    std::unordered_map<std::wstring, int> m_perFileIconCache;
    std::unordered_set<std::wstring> m_inFlightIcons;
    std::atomic<bool> m_iconNotifyPending{ false };

    // True (and noted) once the paint's budget is spent. The first kMinItemsPerPaint new items of a paint are
    // always made, so that a paint whose own drawing already took longer than the budget still makes progress
    // instead of deferring everything and repainting for ever.
    bool OverPaintBudget();
    void NoteUpload() { if (m_paintBudgetDeadline != 0) --m_paintUploadsLeft; }     // a new texture
    void NoteItemMade() { if (m_paintBudgetDeadline != 0) ++m_paintItemsMade; }   // an icon or thumbnail ready to draw
    static constexpr int kUploadsPerPaint = 8;
    static constexpr int kMinItemsPerPaint = 2;
    LONGLONG m_paintBudgetDeadline = 0;     // performance-counter time; 0 = no budget
    int m_paintUploadsLeft = 0;
    int m_paintItemsMade = 0;
    bool m_paintBudgetExceeded = false;

    // Background rotation & image transformation tracking.
    // Rotations and resizes run on threads of their own, which share this block with the cache. Such a
    // thread holds a reference of its own, so one that is still at work when the cache goes away finishes
    // the file it has in hand on memory that is still there, and then stops.
    struct TransformState {
        std::mutex mutex;
        std::unordered_set<std::wstring> busyNormKeys;              // files being rotated or resized
        std::vector<std::wstring> completedPaths;
        std::vector<ImageTransformResult> completedTransforms;
        std::atomic<bool> wanted{ true };                           // false once the cache is shutting down
    };
    std::shared_ptr<TransformState> m_transforms = std::make_shared<TransformState>();
};

} // namespace Explorer2
