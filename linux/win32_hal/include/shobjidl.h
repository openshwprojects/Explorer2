#pragma once
// linux/win32_hal/include/shobjidl.h - Shell COM Interfaces (IShellItem, IFileOperation, IContextMenu, ITaskbarList3, IShellLinkW).

#include <shlobj.h>

enum SIGDN : uint32_t {
    SIGDN_NORMALDISPLAY = 0u,
    SIGDN_FILESYSPATH = 0x80058000u
};

struct IShellItem : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE BindToHandler(void* pbc, REFGUID bhid, REFIID riid, void** ppv) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetParent(IShellItem** ppsi) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayName(SIGDN sigdnName, LPWSTR* ppszName) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAttributes(ULONG sfgaoMask, ULONG* psfgaoAttribs) = 0;
    virtual HRESULT STDMETHODCALLTYPE Compare(IShellItem* psi, DWORD hint, int* piOrder) = 0;
};
E2_DECLARE_UUID(IShellItem, 0x43826d1eu, 0xe718u, 0x42eeu, 0xbcu, 0x55u, 0xa1u, 0xe2u, 0x61u, 0xc3u, 0x7bu, 0xfeu)

HRESULT SHCreateItemFromParsingName(LPCWSTR pszPath, void* pbc, REFIID riid, void** ppv);

#define SIIGBF_RESIZETOFIT 0x00
#define SIIGBF_BIGGERSIZEOK 0x01
#define SIIGBF_MEMORYONLY 0x02
#define SIIGBF_ICONONLY 0x04
#define SIIGBF_THUMBNAILONLY 0x08
#define SIIGBF_INCACHEONLY 0x10
using SIIGBF = int;

struct IShellItemImageFactory : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetImage(SIZE size, SIIGBF flags, HBITMAP* phbm) = 0;
};
E2_DECLARE_UUID(IShellItemImageFactory, 0xbcc18b79u, 0xba16u, 0x442fu, 0x80u, 0xc4u, 0x8au, 0x59u, 0xc3u, 0x0cu, 0x46u, 0x3bu)

// IFileOperation & IFileOperationProgressSink
#define FOFX_ADDUNDORECORD 0x20000000u
#define FOFX_RECYCLEONDELETE 0x00080000u
#define TSF_NORMAL 0u
#define TSF_DELETE_RECYCLE_IF_POSSIBLE 0x0080u

struct FileOperation;
inline constexpr CLSID CLSID_FileOperation = { 0x3ad05575u, 0x8857u, 0x4850u, { 0x92u,0x77u,0x11u,0xb8u,0x5bu,0xdbu,0x8eu,0x09u } };
E2_DECLARE_UUID(FileOperation, 0x3ad05575u, 0x8857u, 0x4850u, 0x92u, 0x77u, 0x11u, 0xb8u, 0x5bu, 0xdbu, 0x8eu, 0x09u)

struct IFileOperationProgressSink : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE StartOperations() { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD, IShellItem*) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem*, HRESULT, IShellItem*) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE ResetTimer() { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE PauseTimer() { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE ResumeTimer() { return S_OK; }
};
E2_DECLARE_UUID(IFileOperationProgressSink, 0x04b0f1a7u, 0x9490u, 0x44bcu, 0x96u, 0xe1u, 0x42u, 0x96u, 0xa3u, 0x12u, 0x52u, 0xe2u)

struct IFileOperation : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Advise(IFileOperationProgressSink* pfops, DWORD* pdwCookie) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unadvise(DWORD dwCookie) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOperationFlags(DWORD dwOperationFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteItem(IShellItem* psiItem, IFileOperationProgressSink* pfopsItem) = 0;
    virtual HRESULT STDMETHODCALLTYPE PerformOperations() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAnyOperationsAborted(BOOL* pfAnyOperationsAborted) = 0;
};
E2_DECLARE_UUID(IFileOperation, 0x947aab5fu, 0x0a5cu, 0x4c13u, 0xb4u, 0xd6u, 0x4bu, 0xf7u, 0x83u, 0x6fu, 0xc9u, 0xf8u)

// ITaskbarList3
enum TBPFLAG {
    TBPF_NOPROGRESS = 0,
    TBPF_INDETERMINATE = 0x1,
    TBPF_NORMAL = 0x2,
    TBPF_ERROR = 0x4,
    TBPF_PAUSED = 0x8
};
inline constexpr CLSID CLSID_TaskbarList = { 0x56FDF344u, 0xFD6Du, 0x11d0u, { 0x95u,0x8Au,0x00u,0x60u,0x97u,0xC9u,0xA0u,0x90u } };

struct ITaskbarList3 : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE HrInit() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProgressValue(HWND hwnd, ULONGLONG ullCompleted, ULONGLONG ullTotal) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProgressState(HWND hwnd, TBPFLAG tbpFlags) = 0;
};
E2_DECLARE_UUID(ITaskbarList3, 0xea1afb91u, 0x9e28u, 0x4b86u, 0x90u, 0xe9u, 0x9eu, 0x9fu, 0x8au, 0x5eu, 0xefu, 0xafu)

// IShellLinkW & IPersistFile
inline constexpr CLSID CLSID_ShellLink = { 0x00021401u, 0x0000u, 0x0000u, { 0xC0u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x46u } };
struct IShellLinkW : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetPath(LPWSTR pszFile, int cch, WIN32_FIND_DATAW* pfd, DWORD fFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPath(LPCWSTR pszFile) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDescription(LPCWSTR pszName) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetWorkingDirectory(LPCWSTR pszDir) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetArguments(LPCWSTR pszArgs) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetIconLocation(LPCWSTR pszIconPath, int iIcon) = 0;
};
E2_DECLARE_UUID(IShellLinkW, 0x000214f9u, 0x0000u, 0x0000u, 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u)

struct IPersistFile : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Load(LPCOLESTR pszFileName, DWORD dwMode) = 0;
    virtual HRESULT STDMETHODCALLTYPE Save(LPCOLESTR pszFileName, BOOL fRemember) = 0;
};
E2_DECLARE_UUID(IPersistFile, 0x0000010bu, 0x0000u, 0x0000u, 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u)

// IShellFolder & IContextMenu
#define CMF_NORMAL 0x00000000u
#define CMF_EXPLORE 0x00000004u
#define GCS_VERBW 0x00000004u
#define CMIC_MASK_UNICODE 0x00004000u
struct CMINVOKECOMMANDINFO {
    DWORD cbSize;
    DWORD fMask;
    HWND hwnd;
    LPCSTR lpVerb;
    LPCSTR lpParameters;
    LPCSTR lpDirectory;
    int nShow;
    DWORD dwHotKey;
    HANDLE hIcon;
};
using LPCMINVOKECOMMANDINFO = CMINVOKECOMMANDINFO*;
struct CMINVOKECOMMANDINFOEX {
    DWORD cbSize;
    DWORD fMask;
    HWND hwnd;
    LPCSTR lpVerb;
    LPCSTR lpParameters;
    LPCSTR lpDirectory;
    int nShow;
    DWORD dwHotKey;
    HANDLE hIcon;
    LPCSTR lpTitle;
    LPCWSTR lpVerbW;
    LPCWSTR lpParametersW;
    LPCWSTR lpDirectoryW;
    LPCWSTR lpTitleW;
    POINT ptInvoke;
};

struct IContextMenu : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE InvokeCommand(CMINVOKECOMMANDINFO* pici) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR idCmd, UINT uType, UINT* pReserved, CHAR* pszName, UINT cchMax) = 0;
};
struct IContextMenu2 : public IContextMenu {
    virtual HRESULT STDMETHODCALLTYPE HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam) = 0;
};
struct IContextMenu3 : public IContextMenu2 {
    virtual HRESULT STDMETHODCALLTYPE HandleMenuMsg2(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) = 0;
};
E2_DECLARE_UUID(IContextMenu,  0x000214e4u, 0x0000u, 0x0000u, 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u)
E2_DECLARE_UUID(IContextMenu2, 0x000214f4u, 0x0000u, 0x0000u, 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u)
E2_DECLARE_UUID(IContextMenu3, 0xbcfce0a0u, 0xec17u, 0x11d0u, 0x8du, 0x10u, 0x00u, 0xa0u, 0xc9u, 0x0fu, 0x27u, 0x19u)
inline constexpr IID IID_IContextMenu = __uuidof(IContextMenu);

#define SHCONTF_FOLDERS       0x00020u
#define SHCONTF_NONFOLDERS    0x00040u
#define SHCONTF_INCLUDEHIDDEN 0x00080u
#define SHGDN_NORMAL          0x0000u

struct STRRET {
    UINT uType;
    union {
        LPWSTR pOleStr;
        UINT uOffset;
        char cStr[260];
    };
};

struct IEnumIDList : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Next(ULONG celt, PITEMID_CHILD* rgelt, ULONG* pceltFetched) = 0;
    virtual HRESULT STDMETHODCALLTYPE Skip(ULONG celt) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reset() = 0;
    virtual HRESULT STDMETHODCALLTYPE Clone(IEnumIDList** ppenum) = 0;
};
E2_DECLARE_UUID(IEnumIDList, 0x000214f2u, 0x0000u, 0x0000u, 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u)

struct IShellFolder : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE ParseDisplayName(HWND hwnd, void* pbc, LPWSTR pszDisplayName, ULONG* pchEaten, PIDLIST_RELATIVE* ppidl, ULONG* pdwAttributes) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumObjects(HWND hwnd, DWORD grfFlags, IEnumIDList** ppenumIDList) = 0;
    virtual HRESULT STDMETHODCALLTYPE BindToObject(const ITEMIDLIST* pidl, void* pbc, REFIID riid, void** ppv) = 0;
    virtual HRESULT STDMETHODCALLTYPE BindToStorage(const ITEMIDLIST* pidl, void* pbc, REFIID riid, void** ppv) = 0;
    virtual HRESULT STDMETHODCALLTYPE CompareIDs(LPARAM lParam, const ITEMIDLIST* pidl1, const ITEMIDLIST* pidl2) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateViewObject(HWND hwndOwner, REFIID riid, void** ppv) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, ULONG* rgfInOut) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUIObjectOf(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT* rgfInOut, void** ppv) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayNameOf(PCUITEMID_CHILD pidl, DWORD uFlags, STRRET* pName) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetNameOf(HWND hwnd, PCUITEMID_CHILD pidl, LPCWSTR pszName, DWORD uFlags, PITEMID_CHILD* ppidlOut) = 0;
};
E2_DECLARE_UUID(IShellFolder, 0x000214e6u, 0x0000u, 0x0000u, 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u)

extern "C" {
HRESULT SHGetDesktopFolder(IShellFolder** ppshf);
HRESULT SHBindToParent(PCIDLIST_ABSOLUTE pidl, REFIID riid, void** ppv, PCUITEMID_CHILD* ppidlLast);
}
