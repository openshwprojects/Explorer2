#pragma once
// linux/win32_hal/include/commctrl.h - Common Controls (Subclassing & ImageList) HAL.

#include <windows.h>

using SUBCLASSPROC = LRESULT (CALLBACK*)(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

BOOL SetWindowSubclass(HWND hWnd, SUBCLASSPROC pfnSubclass, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
BOOL RemoveWindowSubclass(HWND hWnd, SUBCLASSPROC pfnSubclass, UINT_PTR uIdSubclass);
LRESULT DefSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

struct _IMAGELIST;
using HIMAGELIST = _IMAGELIST*;

#define ILD_NORMAL 0x00000000u
#define ILD_TRANSPARENT 0x00000001u
#define ILD_IMAGE 0x00000020u

HICON ImageList_GetIcon(HIMAGELIST himl, int i, UINT flags);
int ImageList_GetImageCount(HIMAGELIST himl);
BOOL ImageList_GetIconSize(HIMAGELIST himl, int* cx, int* cy);
