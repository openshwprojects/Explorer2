#pragma once
// linux/win32_hal/include/winnetwk.h - Windows Networking (WNet) Share Enumeration HAL for Linux.

#include <windows.h>

#define RESOURCE_CONNECTED  0x00000001u
#define RESOURCE_GLOBALNET  0x00000002u
#define RESOURCE_REMEMBERED 0x00000003u

#define RESOURCETYPE_ANY   0x00000000u
#define RESOURCETYPE_DISK  0x00000001u
#define RESOURCETYPE_PRINT 0x00000002u

#define RESOURCEDISPLAYTYPE_GENERIC 0x00000000u
#define RESOURCEDISPLAYTYPE_DOMAIN  0x00000001u
#define RESOURCEDISPLAYTYPE_SERVER  0x00000002u
#define RESOURCEDISPLAYTYPE_SHARE   0x00000003u

#define RESOURCEUSAGE_CONNECTABLE 0x00000001u
#define RESOURCEUSAGE_CONTAINER   0x00000002u

#define CONNECT_UPDATE_PROFILE 0x00000001u

struct NETRESOURCEW {
    DWORD dwScope;
    DWORD dwType;
    DWORD dwDisplayType;
    DWORD dwUsage;
    LPWSTR lpLocalName;
    LPWSTR lpRemoteName;
    LPWSTR lpComment;
    LPWSTR lpProvider;
};
using LPNETRESOURCEW = NETRESOURCEW*;

extern "C" {
DWORD WNetOpenEnumW(DWORD dwScope, DWORD dwType, DWORD dwUsage, LPNETRESOURCEW lpNetResource, LPHANDLE lphEnum);
DWORD WNetEnumResourceW(HANDLE hEnum, LPDWORD lpcCount, LPVOID lpBuffer, LPDWORD lpBufferSize);
DWORD WNetCloseEnum(HANDLE hEnum);
DWORD WNetGetConnectionW(LPCWSTR lpLocalName, LPWSTR lpRemoteName, LPDWORD lpnLength);
DWORD WNetGetLastErrorW(LPDWORD lpError, LPWSTR lpErrorBuf, DWORD nErrorBufSize, LPWSTR lpNameBuf, DWORD nNameBufSize);
DWORD WNetAddConnection2W(LPNETRESOURCEW lpNetResource, LPCWSTR lpPassword, LPCWSTR lpUserName, DWORD dwFlags);
DWORD WNetCancelConnection2W(LPCWSTR lpName, DWORD dwFlags, BOOL fForce);
DWORD WNetConnectionDialog(HWND hwnd, DWORD dwType);
DWORD WNetDisconnectDialog(HWND hwnd, DWORD dwType);
}
