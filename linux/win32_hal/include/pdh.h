#pragma once
// linux/win32_hal/include/pdh.h - Performance Data Helper (PDH) Disk I/O & Thermal Zone HAL for Linux.

#include <windows.h>

using PDH_STATUS = LONG;
using PDH_HQUERY = HANDLE;
using PDH_HCOUNTER = HANDLE;

#define PDH_FMT_LONG   0x00000100u
#define PDH_FMT_DOUBLE 0x00000200u

struct PDH_FMT_COUNTERVALUE {
    DWORD CStatus;
    union {
        LONG longValue;
        double doubleValue;
        LONGLONG largeValue;
        LPCSTR AnsiStringValue;
        LPCWSTR WideStringValue;
    };
};

struct PDH_FMT_COUNTERVALUE_ITEM_W {
    LPWSTR szName;
    PDH_FMT_COUNTERVALUE FmtValue;
};

extern "C" {
PDH_STATUS PdhOpenQueryW(LPCWSTR szDataSource, DWORD_PTR dwUserData, PDH_HQUERY* phQuery);
PDH_STATUS PdhAddEnglishCounterW(PDH_HQUERY hQuery, LPCWSTR szFullCounterPath, DWORD_PTR dwUserData, PDH_HCOUNTER* phCounter);
PDH_STATUS PdhCollectQueryData(PDH_HQUERY hQuery);
PDH_STATUS PdhGetFormattedCounterArrayW(PDH_HCOUNTER hCounter, DWORD dwFormat, LPDWORD lpdwBufferSize,
                                        LPDWORD lpdwItemCount, PDH_FMT_COUNTERVALUE_ITEM_W* ItemBuffer);
PDH_STATUS PdhCloseQuery(PDH_HQUERY hQuery);
}
