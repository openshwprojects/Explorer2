#pragma once
// linux/win32_hal/include/commdlg.h - Open/Save File Dialogs HAL.

#include <windows.h>

#define OFN_READONLY 0x00000001u
#define OFN_OVERWRITEPROMPT 0x00000002u
#define OFN_HIDEREADONLY 0x00000004u
#define OFN_NOCHANGEDIR 0x00000008u
#define OFN_PATHMUSTEXIST 0x00000800u
#define OFN_FILEMUSTEXIST 0x00001000u
#define OFN_EXPLORER 0x00080000u

using LPOFNHOOKPROC = UINT_PTR (CALLBACK*)(HWND, UINT, WPARAM, LPARAM);

struct OPENFILENAMEW {
    DWORD lStructSize;
    HWND hwndOwner;
    HINSTANCE hInstance;
    LPCWSTR lpstrFilter;
    LPWSTR lpstrCustomFilter;
    DWORD nMaxCustFilter;
    DWORD nFilterIndex;
    LPWSTR lpstrFile;
    DWORD nMaxFile;
    LPWSTR lpstrFileTitle;
    DWORD nMaxFileTitle;
    LPCWSTR lpstrInitialDir;
    LPCWSTR lpstrTitle;
    DWORD Flags;
    WORD nFileOffset;
    WORD nFileExtension;
    LPCWSTR lpstrDefExt;
    LPARAM lCustData;
    LPOFNHOOKPROC lpfnHook;
    LPCWSTR lpTemplateName;
    void* pvReserved;
    DWORD dwReserved;
    DWORD FlagsEx;
};
using LPOPENFILENAMEW = OPENFILENAMEW*;

BOOL GetOpenFileNameW(LPOPENFILENAMEW lpofn);
BOOL GetSaveFileNameW(LPOPENFILENAMEW lpofn);
DWORD CommDlgExtendedError();
