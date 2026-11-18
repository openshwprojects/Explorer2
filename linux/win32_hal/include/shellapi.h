#pragma once
// linux/win32_hal/include/shellapi.h - Win32 Shell API HAL for Linux.

#include <windows.h>

#define SHGFI_ICON 0x000000100u
#define SHGFI_DISPLAYNAME 0x000000200u
#define SHGFI_TYPENAME 0x000000400u
#define SHGFI_ATTRIBUTES 0x000000800u
#define SHGFI_SYSICONINDEX 0x000004000u
#define SHGFI_SMALLICON 0x000000001u
#define SHGFI_LARGEICON 0x000000000u
#define SHGFI_USEFILEATTRIBUTES 0x000000010u

#define SHGSI_ICON 0x000000100u
#define SHGSI_SMALLICON 0x000000001u
#define SHGSI_SYSICONINDEX 0x000004000u

enum SHSTOCKICONID {
    SIID_DOCNOASSOC = 0,
    SIID_APPLICATION = 2,
    SIID_FOLDER = 3,
    SIID_FOLDEROPEN = 4,
    SIID_DRIVEFIXED = 8
};

struct SHFILEINFOW {
    HICON hIcon;
    int iIcon;
    DWORD dwAttributes;
    WCHAR szDisplayName[MAX_PATH];
    WCHAR szTypeName[80];
};

struct SHSTOCKICONINFO {
    DWORD cbSize;
    HICON hIcon;
    int iSysImageIndex;
    int iIcon;
    WCHAR szPath[MAX_PATH];
};

#define SEE_MASK_DEFAULT 0x00000000u
#define SEE_MASK_INVOKEIDLIST 0x0000000Cu
#define SEE_MASK_NOCLOSEPROCESS 0x00000040u
#define SEE_MASK_FLAG_NO_UI 0x00000400u
#define SEE_MASK_NO_CONSOLE 0x00008000u

struct SHELLEXECUTEINFOW {
    DWORD cbSize;
    ULONG fMask;
    HWND hwnd;
    LPCWSTR lpVerb;
    LPCWSTR lpFile;
    LPCWSTR lpParameters;
    LPCWSTR lpDirectory;
    int nShow;
    HINSTANCE hInstApp;
    void* lpIDList;
    LPCWSTR lpClass;
    HKEY hkeyClass;
    DWORD dwHotKey;
    union {
        HANDLE hIcon;
        HANDLE hMonitor;
    };
    HANDLE hProcess;
};

#define FO_MOVE 0x0001u
#define FO_COPY 0x0002u
#define FO_DELETE 0x0003u
#define FO_RENAME 0x0004u
#define FOF_SILENT 0x0004u
#define FOF_NOCONFIRMATION 0x0010u
#define FOF_ALLOWUNDO 0x0040u
#define FOF_NOERRORUI 0x0400u
#define FOF_NOCONFIRMMKDIR 0x0200u
#define FOF_NO_UI (FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR)

struct SHFILEOPSTRUCTW {
    HWND hwnd;
    UINT wFunc;
    LPCWSTR pFrom;
    LPCWSTR pTo;
    WORD fFlags;
    BOOL fAnyOperationsAborted;
    LPVOID hNameMappings;
    LPCWSTR lpszProgressTitle;
};

struct SHQUERYRBINFO {
    DWORD cbSize;
    int64_t i64Size;
    int64_t i64NumItems;
};
#define SHERB_NOCONFIRMATION 0x00000001u
#define SHERB_NOPROGRESSUI   0x00000002u
#define SHERB_NOSOUND        0x00000004u

extern "C" {
LPWSTR* CommandLineToArgvW(LPCWSTR lpCmdLine, int* pNumArgs);
DWORD_PTR SHGetFileInfoW(LPCWSTR pszPath, DWORD dwFileAttributes, SHFILEINFOW* psfi, UINT cbFileInfo, UINT uFlags);
HRESULT SHGetStockIconInfo(SHSTOCKICONID siid, UINT uFlags, SHSTOCKICONINFO* psii);
BOOL ShellExecuteExW(SHELLEXECUTEINFOW* pExecInfo);
HINSTANCE ShellExecuteW(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, int nShowCmd);
int SHFileOperationW(SHFILEOPSTRUCTW* lpFileOp);
HRESULT SHQueryRecycleBinW(LPCWSTR pszRootPath, SHQUERYRBINFO* pSHQueryRBInfo);
HRESULT SHEmptyRecycleBinW(HWND hwnd, LPCWSTR pszRootPath, DWORD dwFlags);
UINT ExtractIconExW(LPCWSTR lpszFile, int nIconIndex, HICON* phiconLarge, HICON* phiconSmall, UINT nIcons);

void DragAcceptFiles(HWND hWnd, BOOL fAccept);
UINT DragQueryFileW(HDROP hDrop, UINT iFile, LPWSTR lpszFile, UINT cch);
BOOL DragQueryPoint(HDROP hDrop, LPPOINT lppt);
void DragFinish(HDROP hDrop);
}
