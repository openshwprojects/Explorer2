#pragma once
#include <objbase.h>

#define MF_VERSION 0x00020070u
#define MFSTARTUP_NOSOCKET 0x1u
#define MFSTARTUP_LITE MFSTARTUP_NOSOCKET

inline constexpr GUID MFVideoFormat_NV12 = { 0x3231564Eu, 0x0000u, 0x0010u, { 0x80u,0x00u,0x00u,0xaau,0x00u,0x38u,0x9bu,0x71u } };
inline constexpr GUID MFVideoFormat_RGB32 = { 0x00000016u, 0x0000u, 0x0010u, { 0x80u,0x00u,0x00u,0xaau,0x00u,0x38u,0x9bu,0x71u } };

struct IMFByteStream : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Close() { return S_OK; }
};
E2_DECLARE_UUID(IMFByteStream, 0xad4c1b00u, 0x4bf7u, 0x422fu, 0x91u, 0x75u, 0x75u, 0x66u, 0x93u, 0xd9u, 0x13u, 0x0du)

struct IMFAttributes : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetUnknown(REFGUID guidKey, IUnknown* pUnknown) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetUINT32(REFGUID guidKey, UINT32 unValue) = 0;
};
E2_DECLARE_UUID(IMFAttributes, 0x2cd2d921u, 0xc447u, 0x44a7u, 0xa1u, 0x3cu, 0x4au, 0xdau, 0xbfu, 0xc2u, 0x47u, 0xe3u)

HRESULT MFStartup(ULONG Version, DWORD dwFlags = 0);
HRESULT MFShutdown();
HRESULT MFCreateAttributes(IMFAttributes** ppMFAttributes, UINT32 cInitialSize);
HRESULT MFCreateMFByteStreamOnStream(IStream* pStream, IMFByteStream** ppByteStream);
