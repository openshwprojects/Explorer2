#pragma once
// linux/win32_hal/include/icmpapi.h - ICMP Echo HAL for Linux.

#include <iphlpapi.h>

#define IP_SUCCESS 0u
#define IP_REQ_TIMED_OUT 11010u

struct IP_OPTION_INFORMATION {
    UCHAR Ttl;
    UCHAR Tos;
    UCHAR Flags;
    UCHAR OptionsSize;
    PUCHAR OptionsData;
};

struct ICMP_ECHO_REPLY {
    IPAddr Address;
    ULONG Status;
    ULONG RoundTripTime;
    USHORT DataSize;
    USHORT Reserved;
    PVOID Data;
    IP_OPTION_INFORMATION Options;
};

extern "C" {
HANDLE IcmpCreateFile();
BOOL IcmpCloseHandle(HANDLE IcmpHandle);
DWORD IcmpSendEcho(HANDLE IcmpHandle, IPAddr DestinationAddress, LPVOID RequestData, WORD RequestSize,
                   IP_OPTION_INFORMATION* RequestOptions, LPVOID ReplyBuffer, DWORD ReplySize, DWORD Timeout);
}
