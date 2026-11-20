#pragma once
#include "Types.h"
#include "Localization.h"
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

struct IWICBitmapFrameDecode;

namespace Explorer2 {

struct EmulatedShellVerb {
    std::wstring label;
    std::wstring commandTemplate; // e.g. "\"C:\\...\\Code.exe\" \"%1\""
    std::wstring exePath;
    HBITMAP hMenuBmp = nullptr;
    bool forFiles = false;
    bool forFolders = false;
    bool forBackground = false;
};

struct OpenWithApp {
    std::wstring name;
    std::wstring exePath;
    HBITMAP hMenuBmp = nullptr;
    bool forImagesOnly = false;
};

struct SendToTarget {
    std::wstring label;
    std::wstring targetPath;
    HBITMAP hMenuBmp = nullptr;
    bool isDesktopShortcut = false;
    bool isZipArchive = false;
};

enum class ImageTransformKind {
    RotateCW,
    RotateCCW,
    Rotate180,
    FlipH,
    FlipV,
    ScalePercent,      // scalePct (1..400)
    FitMaxDim,         // maxDim (e.g. 1920): downscales longest edge to maxDim preserving aspect ratio
    ExactWidthHeight,  // targetW x targetH
    TargetFileSize,    // targetBytes: iterative quality + resolution solver in RAM so output <= targetBytes
    ConvertFormat,     // forceExt = ".jpg", ".png", ".bmp" with jpegQuality
    Crop               // cropX / cropY / cropW / cropH: the rectangle of the picture that is kept
};

struct ImageTransformParams {
    ImageTransformKind kind = ImageTransformKind::ScalePercent;
    int scalePct = 100;
    uint32_t maxDim = 0;
    uint32_t targetW = 0;
    uint32_t targetH = 0;
    uint64_t targetBytes = 0;
    std::wstring forceExt; // e.g. L".jpg", L".png", L".bmp" (empty = preserve source or use .jpg for TargetFileSize)
    float jpegQuality = 0.92f;
    bool overwriteOriginal = false;
    std::wstring suffix;   // e.g. L"_50pct", L"_1920px", L"_500KB"
    // Crop: in pixels of the picture as the viewer shows it (a photo stored turned is upright there)
    uint32_t cropX = 0;
    uint32_t cropY = 0;
    uint32_t cropW = 0;
    uint32_t cropH = 0;
    // Fail rather than write another format when this one cannot be written (an in-place rotate must not turn a
    // .webp into a .jpg)
    bool sameFormatOnly = false;
};

struct ImageTransformResult {
    bool ok = false;
    std::wstring srcPath;
    std::wstring outPath;
    uint32_t origW = 0;
    uint32_t origH = 0;
    uint32_t newW = 0;
    uint32_t newH = 0;
    uint64_t origBytes = 0;
    uint64_t newBytes = 0;
    float usedQuality = 0.0f;
    bool lossless = false;      // a JPEG turned without being compressed again
    std::wstring errorMsg;
};

class ShellMenuEmulator {
public:
    ShellMenuEmulator();
    ~ShellMenuEmulator();

    // Scans registry verbs, 7-Zip/WinRAR, SendTo folder, and OpenWith apps once at startup on a background thread
    void InitializeAsync();

    bool Has7Zip(std::wstring* out7zGuiExe = nullptr, std::wstring* out7zFmExe = nullptr) const;

    std::vector<EmulatedShellVerb> GetDiscoveredVerbs(bool isFile, bool isFolder, bool isBackground) const;
    std::vector<OpenWithApp> GetOpenWithApps(bool isImage) const;
    std::vector<SendToTarget> GetSendToTargets(Lang lang) const;

    HBITMAP Get7ZipMenuBitmap() const { std::lock_guard<std::mutex> lk(m_mutex); return m_7zMenuBmp; }
    HBITMAP GetTerminalMenuBitmap() const { std::lock_guard<std::mutex> lk(m_mutex); return m_terminalMenuBmp; }
    HBITMAP GetExplorerMenuBitmap() const { std::lock_guard<std::mutex> lk(m_mutex); return m_explorerMenuBmp; }
    HBITMAP GetFolderMenuBitmap() const { std::lock_guard<std::mutex> lk(m_mutex); return m_folderMenuBmp; }

    static void AttachMenuBitmap(HMENU hMenu, UINT itemIdOrPos, bool byPosition, HBITMAP hBmp);

    // Execute a discovered registry command template with "%1" or "%V" substituted by targetPath
    static void ExecuteVerbCommand(const EmulatedShellVerb& verb, const std::wstring& targetPath);

    // Execute 7-Zip action via 7zG.exe / 7zFM.exe
    void Execute7ZipAction(int actionId, const std::vector<std::wstring>& selectedPaths, const std::wstring& currentDir) const;

    // Open "Choose another app..." Windows dialog
    static void ShowOpenWithDialog(HWND hwnd, const std::wstring& filePath);

    // Create a .lnk shortcut in the given directory (or Desktop)
    static bool CreateShortcutFile(const std::wstring& targetFullPath, const std::wstring& linkFilePath);

    // Create a new empty file (.txt, .bmp, etc.) in the current directory
    static bool CreateNewEmptyFile(const std::wstring& parentDir, const std::wstring& baseName, const std::wstring& ext, std::wstring& outCreatedPath);

    // Rotate image 90 degrees CW or CCW losslessly/via WIC
    static bool RotateImageFile(const std::wstring& imagePath, bool clockwise);

    // How a decoded frame's file says its pixels are stored: an ImageTurn code (ImageOrientation.h), 1 when the
    // file says nothing. The viewer and everything that saves a picture make it upright with this.
    static int ReadExifOrientation(IWICBitmapFrameDecode* frame);

    // The quality (1 to 100) a JPEG was compressed at, read from its file bytes; 0 when they do not tell. A photo
    // that has to be compressed again after a turn or a crop is written at about this quality.
    static int EstimateJpegQuality(const uint8_t* data, size_t size);

    // Fast header-only dimension probe (< 0.2 ms, does not decode pixel data)
    static bool ProbeImageDimensions(const std::wstring& imagePath, uint32_t& outW, uint32_t& outH, uint64_t* outFileBytes = nullptr);

    // Integrated Per-Image Tool engine: resize, downscale, target file size solver, flip/rotate, format conversion
    static bool TransformImageFile(const std::wstring& imagePath, const ImageTransformParams& params, ImageTransformResult& outRes);

    // Fallback: Show native Windows IContextMenu if user Shift+Right-Clicks or selects "System Shell Menu"
    static void ShowNativeShellContextMenu(HWND hwnd, const std::vector<std::wstring>& paths, int screenX, int screenY);

private:
    void ScanStartupData();
    void ScanRegistryShellKey(HKEY hRoot, const wchar_t* subKeyPath, bool isFile, bool isFolder, bool isBg);

    mutable std::mutex m_mutex;
    std::atomic<bool> m_ready{ false };
    std::thread m_scanThread;           // the start-up scan; it works on this object, so the destructor waits for it

    std::wstring m_7zGPath;
    std::wstring m_7zFMPath;
    std::wstring m_winRarPath;
    HBITMAP m_7zMenuBmp = nullptr;
    HBITMAP m_terminalMenuBmp = nullptr;
    HBITMAP m_explorerMenuBmp = nullptr;
    HBITMAP m_folderMenuBmp = nullptr;

    std::vector<EmulatedShellVerb> m_verbs;
    std::vector<OpenWithApp> m_openWithApps;
    std::vector<SendToTarget> m_sendToTargets;
};

// 7-Zip Submenu Action IDs
enum SevenZipAction : int {
    SZ_OPEN_ARCHIVE = 1,
    SZ_EXTRACT_FILES = 2,
    SZ_EXTRACT_HERE = 3,
    SZ_EXTRACT_SUBFOLDER = 4,
    SZ_ADD_DIALOG = 5,
    SZ_ADD_7Z = 6,
    SZ_ADD_ZIP = 7
};

} // namespace Explorer2
