#pragma once
// linux/win32_hal/include/dxgi1_4.h - DXGI 1.1..1.4 GPU Adapter & VRAM Query HAL for Linux.

#include <unknwn.h>

#define DXGI_ERROR_NOT_FOUND (static_cast<HRESULT>(0x887A0002u))
#define DXGI_ADAPTER_FLAG_SOFTWARE 2u

struct LUID {
    DWORD LowPart;
    LONG HighPart;
};

struct DXGI_ADAPTER_DESC1 {
    WCHAR Description[128];
    UINT VendorId;
    UINT DeviceId;
    UINT SubSysId;
    UINT Revision;
    SIZE_T DedicatedVideoMemory;
    SIZE_T DedicatedSystemMemory;
    SIZE_T SharedSystemMemory;
    LUID AdapterLuid;
    UINT Flags;
};

enum DXGI_MEMORY_SEGMENT_GROUP {
    DXGI_MEMORY_SEGMENT_GROUP_LOCAL = 0,
    DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL = 1
};

struct DXGI_QUERY_VIDEO_MEMORY_INFO {
    UINT64 Budget;
    UINT64 CurrentUsage;
    UINT64 AvailableForReservation;
    UINT64 CurrentReservation;
};

struct IDXGIAdapter1 : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1* pDesc) = 0;
};
E2_DECLARE_UUID(IDXGIAdapter1, 0x29038f61u, 0x3839u, 0x4626u, 0x91u, 0xfdu, 0x08u, 0x68u, 0x79u, 0x01u, 0x1au, 0x05u)

struct IDXGIAdapter3 : public IDXGIAdapter1 {
    virtual HRESULT STDMETHODCALLTYPE QueryVideoMemoryInfo(UINT NodeIndex, DXGI_MEMORY_SEGMENT_GROUP MemorySegmentGroup,
                                                           DXGI_QUERY_VIDEO_MEMORY_INFO* pVideoMemoryInfo) = 0;
};
E2_DECLARE_UUID(IDXGIAdapter3, 0x645967a4u, 0x1392u, 0x4310u, 0xa7u, 0x98u, 0x80u, 0x53u, 0xceu, 0x3eu, 0x93u, 0xfdu)

struct IDXGIFactory1 : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT Adapter, IDXGIAdapter1** ppAdapter) = 0;
};
E2_DECLARE_UUID(IDXGIFactory1, 0x770aae78u, 0xf26fu, 0x4dbau, 0xa8u, 0x29u, 0x25u, 0x3cu, 0x83u, 0xd1u, 0xb3u, 0x87u)

extern "C" HRESULT CreateDXGIFactory1(REFIID riid, void** ppFactory);
