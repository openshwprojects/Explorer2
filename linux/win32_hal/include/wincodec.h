#pragma once
// linux/win32_hal/include/wincodec.h - Windows Imaging Component (WIC) HAL for Linux.

#include <objbase.h>

using WICPixelFormatGUID = GUID;
using REFWICPixelFormatGUID = const GUID&;

inline constexpr CLSID CLSID_WICImagingFactory = { 0xcacaf262u, 0x9370u, 0x4615u, { 0xa1u, 0x3bu, 0x9fu, 0x55u, 0x39u, 0xdau, 0x4cu, 0x0au } };
inline constexpr GUID GUID_ContainerFormatBmp  = { 0x0af1d87eu, 0xfcfeu, 0x4188u, { 0xbdu, 0xebu, 0xa7u, 0x90u, 0x64u, 0x71u, 0xcbu, 0xe3u } };
inline constexpr GUID GUID_ContainerFormatPng  = { 0x1b7cfaf4u, 0x713fu, 0x473cu, { 0xbbu, 0xcdu, 0x61u, 0x37u, 0x42u, 0x5fu, 0xaeu, 0xafu } };
inline constexpr GUID GUID_ContainerFormatIco  = { 0xa3a860c4u, 0x338fu, 0x4c17u, { 0x91u, 0x9au, 0xfbu, 0xa4u, 0xb5u, 0x62u, 0x8fu, 0x21u } };
inline constexpr GUID GUID_ContainerFormatJpeg = { 0x19e4a5aau, 0x5662u, 0x4fc5u, { 0xa0u, 0xc0u, 0x17u, 0x58u, 0x02u, 0x8eu, 0x10u, 0x57u } };
inline constexpr GUID GUID_ContainerFormatTiff = { 0x163bcc30u, 0xe2e9u, 0x4f0bu, { 0x96u, 0x1du, 0xa3u, 0xe9u, 0xfdu, 0xb7u, 0x88u, 0xa3u } };
inline constexpr GUID GUID_ContainerFormatGif  = { 0x1f8a5601u, 0x7d4du, 0x4cbdu, { 0x9cu, 0x82u, 0x1bu, 0xc8u, 0xd4u, 0xeeu, 0xb9u, 0xa5u } };

inline constexpr GUID GUID_WICPixelFormat8bppGray   = { 0x6fddc324u, 0x4e03u, 0x4bfeU, { 0xb1u, 0x85u, 0x3du, 0x77u, 0x76u, 0x8du, 0xc9u, 0x08u } };
inline constexpr GUID GUID_WICPixelFormat24bppBGR   = { 0x6fddc324u, 0x4e03u, 0x4bfeU, { 0xb1u, 0x85u, 0x3du, 0x77u, 0x76u, 0x8du, 0xc9u, 0x0cu } };
inline constexpr GUID GUID_WICPixelFormat24bppRGB   = { 0x6fddc324u, 0x4e03u, 0x4bfeU, { 0xb1u, 0x85u, 0x3du, 0x77u, 0x76u, 0x8du, 0xc9u, 0x0du } };
inline constexpr GUID GUID_WICPixelFormat32bppBGR   = { 0x6fddc324u, 0x4e03u, 0x4bfeU, { 0xb1u, 0x85u, 0x3du, 0x77u, 0x76u, 0x8du, 0xc9u, 0x0eu } };
inline constexpr GUID GUID_WICPixelFormat32bppBGRA  = { 0x6fddc324u, 0x4e03u, 0x4bfeU, { 0xb1u, 0x85u, 0x3du, 0x77u, 0x76u, 0x8du, 0xc9u, 0x0fu } };
inline constexpr GUID GUID_WICPixelFormat32bppPBGRA = { 0x6fddc324u, 0x4e03u, 0x4bfeU, { 0xb1u, 0x85u, 0x3du, 0x77u, 0x76u, 0x8du, 0xc9u, 0x10u } };
inline constexpr GUID GUID_WICPixelFormat32bppRGBA  = { 0xf5c7ad2du, 0x6a8du, 0x43ddu, { 0xa7u, 0xa8u, 0xa2u, 0x99u, 0x35u, 0x26u, 0x1au, 0xe9u } };

struct WICRect {
    INT X;
    INT Y;
    INT Width;
    INT Height;
};

enum WICDecodeOptions {
    WICDecodeMetadataCacheOnDemand = 0,
    WICDecodeMetadataCacheOnLoad = 1
};

enum WICBitmapCreateCacheOption {
    WICBitmapNoCache = 0,
    WICBitmapCacheOnDemand = 1,
    WICBitmapCacheOnLoad = 2
};

enum WICBitmapLockFlags {
    WICBitmapLockRead = 1,
    WICBitmapLockWrite = 2
};

enum WICBitmapEncoderCacheOption {
    WICBitmapEncoderCacheInMemory = 0,
    WICBitmapEncoderCacheTempFile = 1,
    WICBitmapEncoderNoCache = 2
};

enum WICBitmapDitherType {
    WICBitmapDitherTypeNone = 0
};

enum WICBitmapPaletteType {
    WICBitmapPaletteTypeCustom = 0,
    WICBitmapPaletteTypeMedianCut = 1
};

enum WICBitmapInterpolationMode {
    WICBitmapInterpolationModeNearestNeighbor = 0,
    WICBitmapInterpolationModeLinear = 1,
    WICBitmapInterpolationModeCubic = 2,
    WICBitmapInterpolationModeFant = 3,
    WICBitmapInterpolationModeHighQualityCubic = 4
};

enum WICBitmapTransformOptions {
    WICBitmapTransformRotate0 = 0,
    WICBitmapTransformRotate90 = 0x1,
    WICBitmapTransformRotate180 = 0x2,
    WICBitmapTransformRotate270 = 0x3,
    WICBitmapTransformFlipHorizontal = 0x8,
    WICBitmapTransformFlipVertical = 0x10
};

struct IWICPalette : public IUnknown {};

struct IWICMetadataQueryReader : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMetadataByName(LPCWSTR wzName, PROPVARIANT* pvarValue) = 0;
};

struct IWICBitmapSource : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetSize(UINT* puiWidth, UINT* puiHeight) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pPixelFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResolution(double* pDpiX, double* pDpiY) { if (pDpiX) *pDpiX = 96.0; if (pDpiY) *pDpiY = 96.0; return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE CopyPalette(IWICPalette*) { return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* prc, UINT cbStride, UINT cbBufferSize, BYTE* pbBuffer) = 0;
};

struct IWICBitmapLock : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetSize(UINT* puiWidth, UINT* puiHeight) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStride(UINT* pcbStride) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDataPointer(UINT* pcbBufferSize, BYTE** ppbData) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pPixelFormat) = 0;
};

struct IWICBitmap : public IWICBitmapSource {
    virtual HRESULT STDMETHODCALLTYPE Lock(const WICRect* prcLock, DWORD flags, IWICBitmapLock** ppILock) = 0;
};

struct IWICBitmapFrameDecode : public IWICBitmapSource {
    virtual HRESULT STDMETHODCALLTYPE GetMetadataQueryReader(IWICMetadataQueryReader** ppIMetadataQueryReader) = 0;
};

struct IWICBitmapDecoder : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetContainerFormat(GUID* pguidContainerFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFrameCount(UINT* pCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFrame(UINT index, IWICBitmapFrameDecode** ppIBitmapFrame) = 0;
};

struct IWICFormatConverter : public IWICBitmapSource {
    virtual HRESULT STDMETHODCALLTYPE Initialize(
        IWICBitmapSource* pISource,
        REFWICPixelFormatGUID dstFormat,
        WICBitmapDitherType dither,
        IWICPalette* pIPalette,
        double alphaThresholdPercent,
        WICBitmapPaletteType paletteTranslate) = 0;
};

struct IWICBitmapScaler : public IWICBitmapSource {
    virtual HRESULT STDMETHODCALLTYPE Initialize(
        IWICBitmapSource* pISource,
        UINT uiWidth,
        UINT uiHeight,
        WICBitmapInterpolationMode mode) = 0;
};

struct IWICBitmapFlipRotator : public IWICBitmapSource {
    virtual HRESULT STDMETHODCALLTYPE Initialize(
        IWICBitmapSource* pISource,
        WICBitmapTransformOptions options) = 0;
};

struct IWICStream : public IStream {
    virtual HRESULT STDMETHODCALLTYPE InitializeFromIStream(IStream* pIStream) = 0;
    virtual HRESULT STDMETHODCALLTYPE InitializeFromFilename(LPCWSTR wzFilename, DWORD dwDesiredAccess) = 0;
    virtual HRESULT STDMETHODCALLTYPE InitializeFromMemory(BYTE* pbBuffer, DWORD cbBufferSize) = 0;
};

struct IWICBitmapFrameEncode : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Initialize(IPropertyBag2* pIEncoderOptions) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSize(UINT uiWidth, UINT uiHeight) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPixelFormat(WICPixelFormatGUID* pPixelFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE WritePixels(UINT lineCount, UINT cbStride, UINT cbBufferSize, BYTE* pbPixels) = 0;
    virtual HRESULT STDMETHODCALLTYPE WriteSource(IWICBitmapSource* pIBitmapSource, WICRect* prc) = 0;
    virtual HRESULT STDMETHODCALLTYPE Commit() = 0;
};

struct IWICBitmapEncoder : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Initialize(IStream* pIStream, WICBitmapEncoderCacheOption cacheOption) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateNewFrame(IWICBitmapFrameEncode** ppIFrameEncode, IPropertyBag2** ppIEncoderOptions) = 0;
    virtual HRESULT STDMETHODCALLTYPE Commit() = 0;
};

struct IWICImagingFactory : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateDecoderFromFilename(
        LPCWSTR wzFilename,
        const GUID* pguidVendor,
        DWORD dwDesiredAccess,
        WICDecodeOptions metadataOptions,
        IWICBitmapDecoder** ppIDecoder) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateDecoderFromStream(
        IStream* pIStream,
        const GUID* pguidVendor,
        WICDecodeOptions metadataOptions,
        IWICBitmapDecoder** ppIDecoder) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateFormatConverter(IWICFormatConverter** ppIFormatConverter) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmapScaler(IWICBitmapScaler** ppIBitmapScaler) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmapFlipRotator(IWICBitmapFlipRotator** ppIBitmapFlipRotator) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateStream(IWICStream** ppIWICStream) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateEncoder(REFGUID guidContainerFormat, const GUID* pguidVendor, IWICBitmapEncoder** ppIEncoder) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateBitmap(
        UINT uiWidth,
        UINT uiHeight,
        REFWICPixelFormatGUID pixelFormat,
        WICBitmapCreateCacheOption option,
        IWICBitmap** ppIBitmap) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateBitmapFromSource(
        IWICBitmapSource* pIBitmapSource,
        WICBitmapCreateCacheOption option,
        IWICBitmap** ppIBitmap) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateBitmapFromMemory(
        UINT uiWidth,
        UINT uiHeight,
        REFWICPixelFormatGUID pixelFormat,
        UINT cbStride,
        UINT cbBufferSize,
        BYTE* pbBuffer,
        IWICBitmap** ppIBitmap) = 0;
};

E2_DECLARE_UUID(IWICMetadataQueryReader, 0x30989668u, 0xe1c9u, 0x4597u, 0xb3u, 0x95u, 0x45u, 0x8eu, 0xedu, 0xb8u, 0x08u, 0xdfu)
E2_DECLARE_UUID(IWICBitmapSource,        0x00000120u, 0xa8f2u, 0x4877u, 0xbau, 0x0au, 0xfdu, 0x2bu, 0x66u, 0x45u, 0xfbu, 0x94u)
E2_DECLARE_UUID(IWICBitmapLock,          0x00000123u, 0xa8f2u, 0x4877u, 0xbau, 0x0au, 0xfdu, 0x2bu, 0x66u, 0x45u, 0xfbu, 0x94u)
E2_DECLARE_UUID(IWICBitmap,              0x00000121u, 0xa8f2u, 0x4877u, 0xbau, 0x0au, 0xfdu, 0x2bu, 0x66u, 0x45u, 0xfbu, 0x94u)
E2_DECLARE_UUID(IWICBitmapFrameDecode,   0x3b16811bu, 0x6a43u, 0x4ec9u, 0xa8u, 0x13u, 0x3du, 0x93u, 0x0cu, 0x13u, 0xb9u, 0x40u)
E2_DECLARE_UUID(IWICBitmapDecoder,       0x9edde9e7u, 0x8deeu, 0x47eau, 0x99u, 0xdfu, 0xe6u, 0xfau, 0xf2u, 0xedu, 0x44u, 0xbfu)
E2_DECLARE_UUID(IWICFormatConverter,     0x00000301u, 0xa8f2u, 0x4877u, 0xbau, 0x0au, 0xfdu, 0x2bu, 0x66u, 0x45u, 0xfbu, 0x94u)
E2_DECLARE_UUID(IWICBitmapScaler,        0x00000302u, 0xa8f2u, 0x4877u, 0xbau, 0x0au, 0xfdu, 0x2bu, 0x66u, 0x45u, 0xfbu, 0x94u)
E2_DECLARE_UUID(IWICBitmapFlipRotator,   0x5009834fu, 0x2d6au, 0x41ceu, 0x9eu, 0x1bu, 0x17u, 0xc5u, 0xafu, 0xf7u, 0xa7u, 0x82u)
E2_DECLARE_UUID(IWICStream,              0x135ff860u, 0x22b7u, 0x4d4fu, 0xb0u, 0xf6u, 0x21u, 0x88u, 0xf6u, 0xdfu, 0x9cu, 0xf4u)
E2_DECLARE_UUID(IWICBitmapFrameEncode,   0x00000105u, 0xa8f2u, 0x4877u, 0xbau, 0x0au, 0xfdu, 0x2bu, 0x66u, 0x45u, 0xfbu, 0x94u)
E2_DECLARE_UUID(IWICBitmapEncoder,       0x00000103u, 0xa8f2u, 0x4877u, 0xbau, 0x0au, 0xfdu, 0x2bu, 0x66u, 0x45u, 0xfbu, 0x94u)
E2_DECLARE_UUID(IWICImagingFactory,      0xec5ec8a9u, 0xc395u, 0x4314u, 0x9cu, 0x77u, 0x54u, 0xd7u, 0xa9u, 0x35u, 0xffu, 0x70u)
