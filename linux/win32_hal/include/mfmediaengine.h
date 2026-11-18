#pragma once
#include <mfapi.h>

inline constexpr CLSID CLSID_MFMediaEngineClassFactory = { 0xb44392dau, 0x499bu, 0x446bu, { 0xa4u,0xcbu,0x00u,0x5fu,0xeau,0xd0u,0xe6u,0xd5u } };
inline constexpr GUID MF_MEDIA_ENGINE_CALLBACK = { 0xc60381b8u, 0x83a4u, 0x41f8u, { 0xa3u,0xd0u,0xdeu,0x05u,0x07u,0x68u,0x49u,0xa9u } };
inline constexpr GUID MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT = { 0x5066893cu, 0x8cf9u, 0x42bcu, { 0x8bu,0x8au,0x47u,0x22u,0x12u,0xe5u,0x27u,0x26u } };

#define MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA 10
#define MF_MEDIA_ENGINE_EVENT_CANPLAY 14
#define MF_MEDIA_ENGINE_EVENT_TIMEUPDATE 17
#define MF_MEDIA_ENGINE_EVENT_ENDED 19
#define MF_MEDIA_ENGINE_EVENT_ERROR 3
#define MF_MEDIA_ENGINE_EVENT_FIRSTFRAMEREADY 1009

struct MFVideoNormalizedRect {
    float left;
    float top;
    float right;
    float bottom;
};

struct MFARGB {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbAlpha;
};

struct IMFMediaEngineNotify : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE EventNotify(DWORD event, DWORD_PTR param1, DWORD param2) = 0;
};
E2_DECLARE_UUID(IMFMediaEngineNotify, 0xfee7c112u, 0xe776u, 0x42b5u, 0x9bu, 0xbcu, 0x00u, 0x48u, 0x52u, 0x4eu, 0x2bu, 0xd5u)

struct IMFMediaEngine : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetSource(BSTR pUrl) = 0;
    virtual HRESULT STDMETHODCALLTYPE Load() = 0;
    virtual HRESULT STDMETHODCALLTYPE Play() = 0;
    virtual HRESULT STDMETHODCALLTYPE Pause() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetLoop(BOOL Loop) = 0;
    virtual BOOL STDMETHODCALLTYPE IsPaused() = 0;
    virtual BOOL STDMETHODCALLTYPE IsEnded() = 0;
    virtual BOOL STDMETHODCALLTYPE HasVideo() = 0;
    virtual BOOL STDMETHODCALLTYPE HasAudio() = 0;
    virtual double STDMETHODCALLTYPE GetDuration() = 0;
    virtual double STDMETHODCALLTYPE GetCurrentTime() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCurrentTime(double seekTime) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNativeVideoSize(DWORD* cx, DWORD* cy) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnVideoStreamTick(LONGLONG* pPts) = 0;
    virtual HRESULT STDMETHODCALLTYPE TransferVideoFrame(IUnknown* pDstSurf, const MFVideoNormalizedRect* pSrc, const RECT* pDst, const MFARGB* pBorderClr) = 0;
    virtual HRESULT STDMETHODCALLTYPE Shutdown() = 0;
};
E2_DECLARE_UUID(IMFMediaEngine, 0x98a1b0bbu, 0x03ebu, 0x4935u, 0xaeu, 0x7cu, 0x93u, 0xc1u, 0xfau, 0x0eu, 0x1cu, 0x93u)

struct IMFMediaEngineEx : public IMFMediaEngine {
    virtual HRESULT STDMETHODCALLTYPE SetSourceFromByteStream(IMFByteStream* pByteStream, BSTR pURL) = 0;
};
E2_DECLARE_UUID(IMFMediaEngineEx, 0x83015eadu, 0xb1e6u, 0x40d0u, 0xa9u, 0x8au, 0x37u, 0x14u, 0x5fu, 0xfcu, 0x1au, 0xd1u)

struct IMFMediaEngineClassFactory : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(DWORD dwFlags, IMFAttributes* pAttr, IMFMediaEngine** ppPlayer) = 0;
};
E2_DECLARE_UUID(IMFMediaEngineClassFactory, 0x4d645aceu, 0x26aau, 0x4688u, 0x9bu, 0xe1u, 0xdfu, 0x35u, 0x16u, 0x99u, 0x0bu, 0x93u)
