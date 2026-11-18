#pragma once
// linux/win32_hal/include/ws2tcpip.h - Wide-character Winsock2 DNS & address helpers for Linux.

#include <winsock2.h>

struct ADDRINFOW {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    PWSTR ai_canonname;
    struct sockaddr* ai_addr;
    ADDRINFOW* ai_next;
};
using PADDRINFOW = ADDRINFOW*;

extern "C" {
INT GetAddrInfoW(PCWSTR pNodeName, PCWSTR pServiceName, const ADDRINFOW* pHints, PADDRINFOW* ppResult);
VOID FreeAddrInfoW(PADDRINFOW pAddrInfo);
INT GetNameInfoW(const SOCKADDR* pSockaddr, socklen_t SockaddrLength, PWCHAR pNodeBuffer, DWORD NodeBufferSize,
                 PWCHAR pServiceBuffer, DWORD ServiceBufferSize, INT Flags);
PCWSTR InetNtopW(INT Family, const VOID* pAddr, PWSTR pStringBuf, size_t StringBufSize);
INT InetPtonW(INT Family, PCWSTR pszAddrString, PVOID pAddrBuf);
}
