#pragma once
// linux/win32_hal/include/wininet.h - WinINet FTP client HAL declarations for Linux.

#include <windows.h>

using HINTERNET = void*;
using INTERNET_PORT = WORD;

#define INTERNET_OPEN_TYPE_PRECONFIG 0u
#define INTERNET_OPEN_TYPE_DIRECT    1u
#define INTERNET_SERVICE_FTP         1u
#define INTERNET_SERVICE_HTTP        3u

#define INTERNET_OPTION_CONNECT_TIMEOUT 2u
#define INTERNET_OPTION_SEND_TIMEOUT    5u
#define INTERNET_OPTION_RECEIVE_TIMEOUT 6u

#define INTERNET_FLAG_PASSIVE    0x08000000u
#define INTERNET_FLAG_SECURE     0x00800000u
#define INTERNET_FLAG_RELOAD     0x80000000u
#define INTERNET_FLAG_DONT_CACHE 0x04000000u
#define FTP_TRANSFER_TYPE_ASCII  0x00000001u
#define FTP_TRANSFER_TYPE_BINARY 0x00000002u

extern "C" {
HINTERNET InternetOpenW(LPCWSTR lpszAgent, DWORD dwAccessType, LPCWSTR lpszProxy, LPCWSTR lpszProxyBypass, DWORD dwFlags);
BOOL InternetSetOptionW(HINTERNET hInternet, DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength);
HINTERNET InternetConnectW(HINTERNET hInternet, LPCWSTR lpszServerName, INTERNET_PORT nServerPort,
                           LPCWSTR lpszUserName, LPCWSTR lpszPassword, DWORD dwService, DWORD dwFlags, DWORD_PTR dwContext);
BOOL InternetCloseHandle(HINTERNET hInternet);
BOOL InternetReadFile(HINTERNET hFile, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead, LPDWORD lpdwNumberOfBytesRead);
BOOL InternetWriteFile(HINTERNET hFile, LPCVOID lpBuffer, DWORD dwNumberOfBytesToWrite, LPDWORD lpdwNumberOfBytesWritten);
BOOL InternetFindNextFileW(HINTERNET hFind, LPVOID lpvFindData);

BOOL FtpSetCurrentDirectoryW(HINTERNET hConnect, LPCWSTR lpszDirectory);
HINTERNET FtpFindFirstFileW(HINTERNET hConnect, LPCWSTR lpszSearchFile, WIN32_FIND_DATAW* lpFindFileData, DWORD dwFlags, DWORD_PTR dwContext);
HINTERNET FtpOpenFileW(HINTERNET hConnect, LPCWSTR lpszFileName, DWORD dwAccess, DWORD dwFlags, DWORD_PTR dwContext);
BOOL FtpCreateDirectoryW(HINTERNET hConnect, LPCWSTR lpszDirectory);
BOOL FtpRemoveDirectoryW(HINTERNET hConnect, LPCWSTR lpszDirectory);
BOOL FtpDeleteFileW(HINTERNET hConnect, LPCWSTR lpszFileName);
BOOL FtpRenameFileW(HINTERNET hConnect, LPCWSTR lpszExisting, LPCWSTR lpszNew);
}
