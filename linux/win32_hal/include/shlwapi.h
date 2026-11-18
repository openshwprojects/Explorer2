#pragma once
// linux/win32_hal/include/shlwapi.h - String, Path, and MemStream helpers.

#include <objbase.h>

struct STRRET;
struct ITEMIDLIST;

#define SHACF_DEFAULT              0x00000000u
#define SHACF_FILESYSTEM           0x00000001u
#define SHACF_URLALL               0x00000006u
#define SHACF_FILESYS_ONLY         0x00000010u
#define SHACF_FILESYS_DIRS         0x00000020u
#define SHACF_AUTOSUGGEST_FORCE_ON 0x10000000u
#define SHACF_AUTOAPPEND_FORCE_OFF 0x80000000u

extern "C" {
int StrCmpLogicalW(LPCWSTR psz1, LPCWSTR psz2);
BOOL PathMatchSpecW(LPCWSTR pszFile, LPCWSTR pszSpec);
BOOL PathFileExistsW(LPCWSTR pszPath);
BOOL PathIsDirectoryW(LPCWSTR pszPath);
IStream* SHCreateMemStream(const BYTE* pInit, UINT cbInit);
HRESULT StrRetToBufW(STRRET* pstr, const ITEMIDLIST* pidl, LPWSTR pszBuf, UINT cchBuf);
HRESULT SHLoadIndirectString(LPCWSTR pszSource, LPWSTR pszOutBuf, UINT cchOutBuf, void** ppvReserved);
HRESULT SHAutoComplete(HWND hwndEdit, DWORD dwFlags);
}
