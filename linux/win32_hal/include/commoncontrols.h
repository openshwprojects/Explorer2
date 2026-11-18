#pragma once
// linux/win32_hal/include/commoncontrols.h - IImageList interface for icon extraction.

#include <unknwn.h>
#include <commctrl.h>

#define SHIL_LARGE 0
#define SHIL_SMALL 1
#define SHIL_EXTRALARGE 2
#define SHIL_SYSSMALL 3
#define SHIL_JUMBO 4

struct IImageList : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIcon(int i, UINT flags, HICON* picon) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetIconSize(int* cx, int* cy) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetImageCount(int* pi) = 0;
};
E2_DECLARE_UUID(IImageList, 0x46eb5926u, 0x582eu, 0x4017u, 0x9fu, 0xdfu, 0xe8u, 0x99u, 0x8du, 0xaau, 0x09u, 0x50u)

HRESULT SHGetImageList(int iImageList, REFIID riid, void** ppvObj);
