#pragma once
#include "Types.h"
#include <d2d1.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfmediaengine.h>
#include <wrl/client.h>
#include <string>

namespace Explorer2 {

class MediaPlayer {
public:
    MediaPlayer();
    ~MediaPlayer();

    bool Initialize(HWND notifyHwnd);
    void Shutdown();
    void DiscardDeviceResources();

    // Loads and starts playing a video or audio file (zero disk lock for files <= 128MB!)
    bool OpenAndPlay(const std::wstring& path);
    void CloseMedia();

    void TogglePlayPause();
    bool IsPlaying() const;
    bool HasMedia() const { return !m_currentPath.empty(); }
    bool HasVideo() const;
    const std::wstring& GetCurrentPath() const { return m_currentPath; }

    double GetCurrentTimeSec() const;
    double GetDurationSec() const;
    void SeekToSeconds(double sec);
    void SeekRelative(double deltaSec);

    // Updates and returns the current video frame as a Direct2D bitmap
    ID2D1Bitmap* GetCurrentVideoFrame(ID2D1RenderTarget* rt, uint32_t& outW, uint32_t& outH);

private:
    void RecreateEngine();

    HWND m_notifyHwnd = nullptr;
    bool m_mfInitialized = false;
    std::wstring m_currentPath;

    Microsoft::WRL::ComPtr<IMFMediaEngine> m_engine;
    Microsoft::WRL::ComPtr<IMFMediaEngineEx> m_engineEx;
    Microsoft::WRL::ComPtr<IMFByteStream> m_activeByteStream;
    Microsoft::WRL::ComPtr<IWICImagingFactory> m_wicFactory;
    Microsoft::WRL::ComPtr<IWICBitmap> m_wicBitmap;
    ID2D1Bitmap* m_d2dBitmap = nullptr;
    uint32_t m_videoW = 0;
    uint32_t m_videoH = 0;
};

} // namespace Explorer2
