#include "MediaPlayer.h"
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

using Microsoft::WRL::ComPtr;

namespace Explorer2 {

class MediaEngineNotify final : public IMFMediaEngineNotify {
public:
    explicit MediaEngineNotify(IMFMediaEngine* engine, HWND hwnd)
        : m_engine(engine), m_hwnd(hwnd) {}

    void SetEngine(IMFMediaEngine* engine) { m_engine = engine; }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) {
            *ppv = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_refCount);
    }

    STDMETHODIMP_(ULONG) Release() override {
        ULONG c = InterlockedDecrement(&m_refCount);
        if (c == 0) delete this;
        return c;
    }

    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR param1, DWORD param2) override {
        (void)param1; (void)param2;
        if (event == MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA || event == MF_MEDIA_ENGINE_EVENT_CANPLAY) {
            if (m_engine) {
                m_engine->Play();
            }
            if (m_hwnd) {
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
        } else if (event == MF_MEDIA_ENGINE_EVENT_FIRSTFRAMEREADY ||
                   event == MF_MEDIA_ENGINE_EVENT_TIMEUPDATE ||
                   event == MF_MEDIA_ENGINE_EVENT_ENDED) {
            if (m_hwnd) {
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
        }
        return S_OK;
    }

private:
    ULONG m_refCount = 1;
    IMFMediaEngine* m_engine = nullptr;
    HWND m_hwnd = nullptr;
};

MediaPlayer::MediaPlayer() = default;

MediaPlayer::~MediaPlayer() {
    Shutdown();
}

bool MediaPlayer::Initialize(HWND notifyHwnd) {
    m_notifyHwnd = notifyHwnd;
    if (!m_mfInitialized) {
        if (SUCCEEDED(MFStartup(MF_VERSION))) {
            m_mfInitialized = true;
        }
    }
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_wicFactory));
    RecreateEngine();
    return m_engine != nullptr;
}

void MediaPlayer::RecreateEngine() {
    if (m_engine) {
        m_engine->Shutdown();
        m_engine.Reset();
        m_engineEx.Reset();
    }

    ComPtr<IMFMediaEngineClassFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) || !factory) {
        return;
    }

    ComPtr<IMFAttributes> attrs;
    MFCreateAttributes(&attrs, 3);
    auto* notify = new MediaEngineNotify(nullptr, m_notifyHwnd);
    attrs->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify);
    attrs->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);

    if (SUCCEEDED(factory->CreateInstance(0, attrs.Get(), &m_engine)) && m_engine) {
        notify->SetEngine(m_engine.Get());
        m_engine.As(&m_engineEx);
        m_engine->SetLoop(TRUE);
    }
    notify->Release();
}

void MediaPlayer::Shutdown() {
    CloseMedia();
    DiscardDeviceResources();
    if (m_engine) {
        m_engine->Shutdown();
        m_engine.Reset();
        m_engineEx.Reset();
    }
    m_wicFactory.Reset();
    if (m_mfInitialized) {
        MFShutdown();
        m_mfInitialized = false;
    }
}

void MediaPlayer::DiscardDeviceResources() {
    if (m_d2dBitmap) {
        m_d2dBitmap->Release();
        m_d2dBitmap = nullptr;
    }
    m_wicBitmap.Reset();
    m_videoW = 0;
    m_videoH = 0;
}

void MediaPlayer::CloseMedia() {
    m_currentPath.clear();
    if (m_engine) {
        m_engine->Pause();
        m_engine->SetSource(nullptr);
    }
    if (m_activeByteStream) {
        m_activeByteStream->Close();
        m_activeByteStream.Reset();
    }
    DiscardDeviceResources();
}

bool MediaPlayer::OpenAndPlay(const std::wstring& path) {
    if (path.empty()) return false;
    if (m_currentPath == path && m_engine) {
        return true;
    }

    CloseMedia();
    if (!m_engine) {
        RecreateEngine();
        if (!m_engine) return false;
    }

    m_currentPath = path;

    // ZERO-LOCK IN-MEMORY STREAMING FOR FILES <= 128 MB:
    // Read bytes into an in-memory IStream and close the disk file handle immediately!
    HANDLE hFile = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    bool loadedFromMemory = false;
    if (hFile != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER fsz = {};
        if (GetFileSizeEx(hFile, &fsz) && fsz.QuadPart > 0 && fsz.QuadPart <= (128LL * 1024 * 1024) && m_engineEx) {
            std::vector<uint8_t> bytes(static_cast<size_t>(fsz.QuadPart));
            DWORD readBytes = 0;
            BOOL ok = ReadFile(hFile, bytes.data(), static_cast<DWORD>(bytes.size()), &readBytes, nullptr);
            CloseHandle(hFile); // Disk handle closed immediately!
            hFile = INVALID_HANDLE_VALUE;

            if (ok && readBytes > 0) {
                ComPtr<IStream> memStream;
                memStream.Attach(SHCreateMemStream(bytes.data(), readBytes));
                if (memStream) {
                    if (SUCCEEDED(MFCreateMFByteStreamOnStream(memStream.Get(), &m_activeByteStream)) && m_activeByteStream) {
                        BSTR bstrUrl = SysAllocString(path.c_str());
                        if (SUCCEEDED(m_engineEx->SetSourceFromByteStream(m_activeByteStream.Get(), bstrUrl))) {
                            loadedFromMemory = true;
                        }
                        SysFreeString(bstrUrl);
                    }
                }
            }
        }
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
        }
    }

    if (!loadedFromMemory) {
        BSTR bstrUrl = SysAllocString(path.c_str());
        m_engine->SetSource(bstrUrl);
        SysFreeString(bstrUrl);
    }

    m_engine->Load();
    m_engine->Play();
    return true;
}

void MediaPlayer::TogglePlayPause() {
    if (!m_engine) return;
    if (m_engine->IsPaused()) {
        m_engine->Play();
    } else {
        m_engine->Pause();
    }
}

bool MediaPlayer::IsPlaying() const {
    return m_engine && !m_engine->IsPaused() && !m_engine->IsEnded();
}

bool MediaPlayer::HasVideo() const {
    return m_engine && (m_engine->HasVideo() != FALSE);
}

double MediaPlayer::GetCurrentTimeSec() const {
    return m_engine ? m_engine->GetCurrentTime() : 0.0;
}

double MediaPlayer::GetDurationSec() const {
    if (!m_engine) return 0.0;
    double d = m_engine->GetDuration();
    return std::isnan(d) || std::isinf(d) ? 0.0 : d;
}

void MediaPlayer::SeekToSeconds(double sec) {
    if (!m_engine) return;
    double dur = GetDurationSec();
    if (dur > 0.0) {
        sec = std::clamp(sec, 0.0, dur);
        m_engine->SetCurrentTime(sec);
    }
}

void MediaPlayer::SeekRelative(double deltaSec) {
    SeekToSeconds(GetCurrentTimeSec() + deltaSec);
}

ID2D1Bitmap* MediaPlayer::GetCurrentVideoFrame(ID2D1RenderTarget* rt, uint32_t& outW, uint32_t& outH) {
    outW = m_videoW;
    outH = m_videoH;
    if (!rt || !m_engine || !m_wicFactory || !m_engine->HasVideo()) {
        return nullptr;
    }

    DWORD nw = 0, nh = 0;
    if (FAILED(m_engine->GetNativeVideoSize(&nw, &nh)) || nw == 0 || nh == 0) {
        return m_d2dBitmap;
    }

    // Cap decode transfer buffer at 1920x1080 for smooth 60fps CPU->GPU copy
    uint32_t targetW = nw;
    uint32_t targetH = nh;
    if (targetW > 1920) {
        targetH = std::max(1u, (targetH * 1920) / targetW);
        targetW = 1920;
    }

    if (targetW != m_videoW || targetH != m_videoH || !m_wicBitmap || !m_d2dBitmap) {
        DiscardDeviceResources();
        m_videoW = targetW;
        m_videoH = targetH;
        m_wicFactory->CreateBitmap(targetW, targetH, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &m_wicBitmap);
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        rt->CreateBitmap(D2D1::SizeU(targetW, targetH), nullptr, 0, &props, &m_d2dBitmap);
    }

    LONGLONG pts = 0;
    if (m_engine->OnVideoStreamTick(&pts) == S_OK && m_wicBitmap && m_d2dBitmap) {
        RECT dstRect = { 0, 0, static_cast<LONG>(m_videoW), static_cast<LONG>(m_videoH) };
        MFARGB bg = { 0, 0, 0, 255 };
        if (SUCCEEDED(m_engine->TransferVideoFrame(m_wicBitmap.Get(), nullptr, &dstRect, &bg))) {
            ComPtr<IWICBitmapLock> lock;
            WICRect lockRect = { 0, 0, static_cast<INT>(m_videoW), static_cast<INT>(m_videoH) };
            if (SUCCEEDED(m_wicBitmap->Lock(&lockRect, WICBitmapLockRead, &lock)) && lock) {
                UINT cbBufferSize = 0;
                UINT cbStride = 0;
                BYTE* pv = nullptr;
                if (SUCCEEDED(lock->GetStride(&cbStride)) &&
                    SUCCEEDED(lock->GetDataPointer(&cbBufferSize, &pv)) && pv) {
                    D2D1_RECT_U destU = D2D1::RectU(0, 0, m_videoW, m_videoH);
                    m_d2dBitmap->CopyFromMemory(&destU, pv, cbStride);
                }
            }
        }
    }

    outW = nw;
    outH = nh;
    return m_d2dBitmap;
}

} // namespace Explorer2
