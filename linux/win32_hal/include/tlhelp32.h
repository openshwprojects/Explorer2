#pragma once
#include <windows.h>

#define TH32CS_SNAPPROCESS 0x00000002u

struct PROCESSENTRY32W {
    DWORD dwSize;
    DWORD cntUsage;
    DWORD th32ProcessID;
    ULONG_PTR th32DefaultHeapID;
    DWORD th32ModuleID;
    DWORD cntThreads;
    DWORD th32ParentProcessID;
    LONG pcPriClassBase;
    DWORD dwFlags;
    WCHAR szExeFile[MAX_PATH];
};

HANDLE CreateToolhelp32Snapshot(DWORD dwFlags, DWORD th32ProcessID);
BOOL Process32FirstW(HANDLE hSnapshot, PROCESSENTRY32W* lppe);
BOOL Process32NextW(HANDLE hSnapshot, PROCESSENTRY32W* lppe);
