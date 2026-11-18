#pragma once
// linux/win32_hal/include/shlobj.h - Shell Folders, PIDLs, Browse Dialog & Clipboard formats.

#include <objbase.h>
#include <shellapi.h>

struct SHITEMID {
    USHORT cb;
    BYTE abID[1];
};
struct ITEMIDLIST {
    SHITEMID mkid;
};
using LPITEMIDLIST = ITEMIDLIST*;
using LPCITEMIDLIST = const ITEMIDLIST*;
using PIDLIST_ABSOLUTE = ITEMIDLIST*;
using PCIDLIST_ABSOLUTE = const ITEMIDLIST*;
using PIDLIST_RELATIVE = ITEMIDLIST*;
using PITEMID_CHILD = ITEMIDLIST*;
using PCUITEMID_CHILD = const ITEMIDLIST*;
using PCUITEMID_CHILD_ARRAY = const ITEMIDLIST* const*;

using KNOWNFOLDERID = GUID;
using REFKNOWNFOLDERID = const KNOWNFOLDERID&;

inline constexpr KNOWNFOLDERID FOLDERID_Desktop       = { 0xB4BFCC3Au, 0xDB2Cu, 0x424Cu, { 0xB0u,0x29u,0x7Fu,0xE9u,0x9Au,0x87u,0xC6u,0x41u } };
inline constexpr KNOWNFOLDERID FOLDERID_Documents     = { 0xFDD39AD0u, 0x238Fu, 0x46AFu, { 0xADu,0xB4u,0x6Cu,0x85u,0x48u,0x03u,0x69u,0xC7u } };
inline constexpr KNOWNFOLDERID FOLDERID_Downloads     = { 0x374DE290u, 0x123Fu, 0x4565u, { 0x91u,0x64u,0x39u,0xC4u,0x92u,0x5Eu,0x46u,0x7Bu } };
inline constexpr KNOWNFOLDERID FOLDERID_Pictures      = { 0x33E28130u, 0x4E1Eu, 0x4676u, { 0x83u,0x5Au,0x98u,0x39u,0x5Cu,0x3Bu,0xC3u,0xBBu } };
inline constexpr KNOWNFOLDERID FOLDERID_LocalAppData  = { 0xF1B32785u, 0x6FBAu, 0x4FCFu, { 0x9Du,0x55u,0x7Bu,0x8Eu,0x7Fu,0x15u,0x70u,0x91u } };
inline constexpr KNOWNFOLDERID FOLDERID_RoamingAppData= { 0x3EB685DBu, 0x65F9u, 0x4CF6u, { 0xA0u,0x3Au,0xE3u,0xEFu,0x65u,0x72u,0x9Fu,0x3Du } };
inline constexpr KNOWNFOLDERID FOLDERID_Profile       = { 0x5E6C858Fu, 0x0E22u, 0x4760u, { 0x9Au,0xFEu,0xEAu,0x33u,0x17u,0xB6u,0x71u,0x73u } };
inline constexpr KNOWNFOLDERID FOLDERID_SendTo        = { 0x8983036Cu, 0x27C0u, 0x404Bu, { 0x8Fu,0x08u,0x10u,0x2Du,0x10u,0xDCu,0xFDu,0x74u } };
inline constexpr KNOWNFOLDERID FOLDERID_Music         = { 0x4BD8D571u, 0x6D19u, 0x48D3u, { 0xBEu,0x97u,0x42u,0x22u,0x20u,0x08u,0x0Eu,0x43u } };
inline constexpr KNOWNFOLDERID FOLDERID_Videos        = { 0x18989B1Du, 0x99B5u, 0x455Bu, { 0x84u,0x1Cu,0xABu,0x7Cu,0x74u,0xE4u,0xDDu,0xFCu } };

#define CSIDL_DESKTOP 0x0000
#define CSIDL_PERSONAL 0x0005
#define CSIDL_BITBUCKET 0x000a
#define CSIDL_DRIVES 0x0011
#define CSIDL_LOCAL_APPDATA 0x001c

#define CFSTR_PREFERREDDROPEFFECT L"Preferred DropEffect"

struct DROPFILES {
    DWORD pFiles;
    POINT pt;
    BOOL fNC;
    BOOL fWide;
};

#define BIF_RETURNONLYFSDIRS 0x00000001u
#define BIF_NEWDIALOGSTYLE   0x00000040u
#define BIF_USENEWUI         (BIF_NEWDIALOGSTYLE | 0x00000010u)

using BFFCALLBACK = int (CALLBACK*)(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData);
struct BROWSEINFOW {
    HWND hwndOwner;
    PCIDLIST_ABSOLUTE pidlRoot;
    LPWSTR pszDisplayName;
    LPCWSTR lpszTitle;
    UINT ulFlags;
    BFFCALLBACK lpfn;
    LPARAM lParam;
    int iImage;
};

HRESULT SHGetKnownFolderPath(REFKNOWNFOLDERID rfid, DWORD dwFlags, HANDLE hToken, PWSTR* ppszPath);
int SHCreateDirectoryExW(HWND hwnd, LPCWSTR pszPath, const SECURITY_ATTRIBUTES* psa);
HRESULT SHGetSpecialFolderLocation(HWND hwnd, int csidl, PIDLIST_ABSOLUTE* ppidl);
HRESULT SHParseDisplayName(LPCWSTR pszName, void* pbc, PIDLIST_ABSOLUTE* ppidl, ULONG sfgaoIn, ULONG* psfgaoOut);
PIDLIST_ABSOLUTE ILCreateFromPathW(LPCWSTR pszPath);
PIDLIST_ABSOLUTE ILClone(PCIDLIST_ABSOLUTE pidl);
BOOL ILRemoveLastID(LPITEMIDLIST pidl);
BOOL ILIsParent(PCIDLIST_ABSOLUTE pidl1, PCIDLIST_ABSOLUTE pidl2, BOOL fImmediate);
PCUITEMID_CHILD ILFindLastID(PCIDLIST_ABSOLUTE pidl);
void ILFree(LPITEMIDLIST pidl);
HRESULT SHCreateDataObject(PCIDLIST_ABSOLUTE pidlFolder, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, IDataObject* pdtInner, REFIID riid, void** ppv);
HRESULT SHDoDragDrop(HWND hwnd, IDataObject* pdata, IDropSource* pdsrc, DWORD dwEffect, DWORD* pdwEffect);
PIDLIST_ABSOLUTE SHBrowseForFolderW(BROWSEINFOW* lpbi);
BOOL SHGetPathFromIDListW(PCIDLIST_ABSOLUTE pidl, LPWSTR pszPath);
BOOL IsUserAnAdmin();

#include <shobjidl.h>
