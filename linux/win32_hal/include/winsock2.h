#pragma once
// linux/win32_hal/include/winsock2.h - Winsock2 HAL wrapping POSIX BSD sockets on Linux.

#include <windows.h>
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using SOCKET = int;
using u_long = unsigned long;
using SOCKADDR = struct sockaddr;
using PSOCKADDR = struct sockaddr*;
using LPSOCKADDR = struct sockaddr*;
using SOCKADDR_IN = struct sockaddr_in;
using SOCKADDR_IN6 = struct sockaddr_in6;
using SOCKADDR_STORAGE = struct sockaddr_storage;

inline constexpr SOCKET INVALID_SOCKET = -1;
inline constexpr int SOCKET_ERROR = -1;

#define SD_RECEIVE SHUT_RD
#define SD_SEND    SHUT_WR
#define SD_BOTH    SHUT_RDWR

#define WSAEWOULDBLOCK   EWOULDBLOCK
#define WSAEINPROGRESS   EINPROGRESS
#define WSAEALREADY      EALREADY
#define WSAENOTSOCK      EBADF
#define WSAEMSGSIZE      EMSGSIZE
#define WSAEOPNOTSUPP    EOPNOTSUPP
#define WSAEAFNOSUPPORT  EAFNOSUPPORT
#define WSAEADDRINUSE    EADDRINUSE
#define WSAEADDRNOTAVAIL EADDRNOTAVAIL
#define WSAENETDOWN      ENETDOWN
#define WSAENETUNREACH   ENETUNREACH
#define WSAENETRESET     ENETRESET
#define WSAECONNABORTED  ECONNABORTED
#define WSAECONNRESET    ECONNRESET
#define WSAENOBUFS       ENOBUFS
#define WSAEISCONN       EISCONN
#define WSAENOTCONN      ENOTCONN
#define WSAETIMEDOUT     ETIMEDOUT
#define WSAECONNREFUSED  ECONNREFUSED
#define WSAEHOSTDOWN     EHOSTDOWN
#define WSAEHOSTUNREACH  EHOSTUNREACH

struct WSADATA {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char* lpVendorInfo;
};
using LPWSADATA = WSADATA*;

struct SOCKET_ADDRESS {
    LPSOCKADDR lpSockaddr;
    INT iSockaddrLength;
};

inline int WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData) {
    if (lpWSAData) {
        std::memset(lpWSAData, 0, sizeof(*lpWSAData));
        lpWSAData->wVersion = wVersionRequested;
        lpWSAData->wHighVersion = MAKEWORD(2, 2);
    }
    return 0;
}

inline int WSACleanup() { return 0; }
inline int WSAGetLastError() { return errno; }
inline void WSASetLastError(int err) { errno = err; }
inline int closesocket(SOCKET s) { return (s >= 0) ? ::close(s) : 0; }

inline int ioctlsocket(SOCKET s, long cmd, u_long* argp) {
    if (s < 0 || !argp) { errno = EBADF; return SOCKET_ERROR; }
    if (cmd == static_cast<long>(FIONBIO)) {
        int fl = ::fcntl(s, F_GETFL, 0);
        if (fl < 0) return SOCKET_ERROR;
        fl = (*argp != 0) ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK);
        return (::fcntl(s, F_SETFL, fl) == 0) ? 0 : SOCKET_ERROR;
    }
    return ::ioctl(s, static_cast<unsigned long>(cmd), argp);
}

// Winsock passes `char*` and `int*` to getsockopt / recvfrom; provide type-safe C++ overloads on Linux
inline int getsockopt(SOCKET s, int level, int optname, char* optval, int* optlen) {
    socklen_t sl = optlen ? static_cast<socklen_t>(*optlen) : 0;
    const int rc = ::getsockopt(s, level, optname, optval, optlen ? &sl : nullptr);
    if (optlen) *optlen = static_cast<int>(sl);
    return rc;
}

inline ssize_t recvfrom(SOCKET s, void* buf, size_t len, int flags, struct sockaddr* src_addr, int* addrlen) {
    socklen_t sl = addrlen ? static_cast<socklen_t>(*addrlen) : 0;
    const ssize_t rc = ::recvfrom(s, buf, len, flags, src_addr, addrlen ? &sl : nullptr);
    if (addrlen) *addrlen = static_cast<int>(sl);
    return rc;
}

// Helper for Winsock callers that pass nfds == 0 to select()
inline int E2SelectWinsock(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout) {
    if (nfds <= 0) {
        int maxFd = -1;
        for (int fd = 0; fd < FD_SETSIZE; ++fd) {
            if ((readfds && FD_ISSET(fd, readfds)) ||
                (writefds && FD_ISSET(fd, writefds)) ||
                (exceptfds && FD_ISSET(fd, exceptfds)))
            {
                maxFd = fd;
            }
        }
        nfds = (maxFd >= 0) ? (maxFd + 1) : 0;
    }
    return ::select(nfds, readfds, writefds, exceptfds, timeout);
}
