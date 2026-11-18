// linux/win32_hal/src/NetAndSysHal.cpp
// Linux Networking, Winsock2/IPHelper/ICMP, Process/Memory, PDH Thermal/Disk, DXGI DRM GPU, Audio & USB/TTY HAL.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <wininet.h>
#include <winnetwk.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <mmsystem.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <cfgmgr32.h>
#include <tlhelp32.h>
#include <shobjidl.h>
#include <shlwapi.h>

#include "PathUtils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <ifaddrs.h>
#include <map>
#include <memory>
#include <mutex>
#include <net/if.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <vector>

namespace {

std::string ReadSmallTextFile(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    char buf[4096];
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    ::close(fd);
    if (n <= 0) return {};
    std::string s(buf, static_cast<size_t>(n));
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t' || s.back() == '\0')) {
        s.pop_back();
    }
    return s;
}

int CountPrefixBits(uint32_t maskNetOrder) {
    uint32_t m = ntohl(maskNetOrder);
    int bits = 0;
    while (bits < 32 && (m & (0x80000000u >> bits)) != 0) ++bits;
    return bits;
}

} // namespace

// ==============================================================================================================
// 1. Winsock2 Wide DNS & Address Conversion (ws2tcpip.h)
// ==============================================================================================================
extern "C" {

INT GetAddrInfoW(PCWSTR pNodeName, PCWSTR pServiceName, const ADDRINFOW* pHints, PADDRINFOW* ppResult) {
    if (!ppResult) return EAI_FAIL;
    *ppResult = nullptr;

    std::string nodeUtf8 = pNodeName ? Explorer2::PathUtils::WideToUtf8(pNodeName) : std::string();
    std::string svcUtf8  = pServiceName ? Explorer2::PathUtils::WideToUtf8(pServiceName) : std::string();

    struct addrinfo hints = {};
    if (pHints) {
        hints.ai_flags    = pHints->ai_flags;
        hints.ai_family   = pHints->ai_family;
        hints.ai_socktype = pHints->ai_socktype;
        hints.ai_protocol = pHints->ai_protocol;
    }

    struct addrinfo* res = nullptr;
    const int rc = ::getaddrinfo(
        pNodeName ? nodeUtf8.c_str() : nullptr,
        pServiceName ? svcUtf8.c_str() : nullptr,
        pHints ? &hints : nullptr,
        &res);
    if (rc != 0 || !res) return rc ? rc : EAI_NONAME;

    ADDRINFOW* head = nullptr;
    ADDRINFOW** tail = &head;
    for (struct addrinfo* cur = res; cur; cur = cur->ai_next) {
        auto* node = static_cast<ADDRINFOW*>(std::calloc(1, sizeof(ADDRINFOW)));
        if (!node) break;
        node->ai_flags    = cur->ai_flags;
        node->ai_family   = cur->ai_family;
        node->ai_socktype = cur->ai_socktype;
        node->ai_protocol = cur->ai_protocol;
        node->ai_addrlen  = cur->ai_addrlen;
        if (cur->ai_addr && cur->ai_addrlen > 0) {
            node->ai_addr = static_cast<struct sockaddr*>(std::malloc(cur->ai_addrlen));
            if (node->ai_addr) std::memcpy(node->ai_addr, cur->ai_addr, cur->ai_addrlen);
        }
        if (cur->ai_canonname) {
            std::wstring cw = Explorer2::PathUtils::Utf8ToWide(cur->ai_canonname);
            node->ai_canonname = static_cast<PWSTR>(std::malloc((cw.size() + 1) * sizeof(wchar_t)));
            if (node->ai_canonname) std::wmemcpy(node->ai_canonname, cw.c_str(), cw.size() + 1);
        }
        *tail = node;
        tail = &node->ai_next;
    }
    ::freeaddrinfo(res);
    *ppResult = head;
    return head ? 0 : EAI_MEMORY;
}

VOID FreeAddrInfoW(PADDRINFOW pAddrInfo) {
    while (pAddrInfo) {
        ADDRINFOW* next = pAddrInfo->ai_next;
        std::free(pAddrInfo->ai_addr);
        std::free(pAddrInfo->ai_canonname);
        std::free(pAddrInfo);
        pAddrInfo = next;
    }
}

INT GetNameInfoW(const SOCKADDR* pSockaddr, socklen_t SockaddrLength, PWCHAR pNodeBuffer, DWORD NodeBufferSize,
                 PWCHAR pServiceBuffer, DWORD ServiceBufferSize, INT Flags)
{
    char host[NI_MAXHOST] = {};
    char serv[NI_MAXSERV] = {};
    const int rc = ::getnameinfo(
        pSockaddr, SockaddrLength,
        (pNodeBuffer && NodeBufferSize > 0) ? host : nullptr,
        (pNodeBuffer && NodeBufferSize > 0) ? sizeof(host) : 0,
        (pServiceBuffer && ServiceBufferSize > 0) ? serv : nullptr,
        (pServiceBuffer && ServiceBufferSize > 0) ? sizeof(serv) : 0,
        Flags);
    if (rc != 0) return rc;
    if (pNodeBuffer && NodeBufferSize > 0) {
        std::wstring w = Explorer2::PathUtils::Utf8ToWide(host);
        wcsncpy_s(pNodeBuffer, NodeBufferSize, w.c_str(), _TRUNCATE);
    }
    if (pServiceBuffer && ServiceBufferSize > 0) {
        std::wstring w = Explorer2::PathUtils::Utf8ToWide(serv);
        wcsncpy_s(pServiceBuffer, ServiceBufferSize, w.c_str(), _TRUNCATE);
    }
    return 0;
}

PCWSTR InetNtopW(INT Family, const VOID* pAddr, PWSTR pStringBuf, size_t StringBufSize) {
    if (!pAddr || !pStringBuf || StringBufSize == 0) return nullptr;
    char buf[INET6_ADDRSTRLEN + 8] = {};
    if (!::inet_ntop(Family, pAddr, buf, sizeof(buf))) return nullptr;
    std::wstring w = Explorer2::PathUtils::Utf8ToWide(buf);
    if (w.size() + 1 > StringBufSize) return nullptr;
    std::wmemcpy(pStringBuf, w.c_str(), w.size() + 1);
    return pStringBuf;
}

INT InetPtonW(INT Family, PCWSTR pszAddrString, PVOID pAddrBuf) {
    if (!pszAddrString || !pAddrBuf) return -1;
    const std::string s = Explorer2::PathUtils::WideToUtf8(pszAddrString);
    return ::inet_pton(Family, s.c_str(), pAddrBuf);
}

// ==============================================================================================================
// 2. IP Helper API (GetAdaptersAddresses, GetIfTable2, GetIpNetTable2, SendARP) & ICMP Echo
// ==============================================================================================================

ULONG GetAdaptersAddresses(ULONG Family, ULONG /*Flags*/, PVOID /*Reserved*/,
                           PIP_ADAPTER_ADDRESSES AdapterAddresses, PULONG SizePointer)
{
    if (!SizePointer) return ERROR_INVALID_PARAMETER;

    // Parse default gateways per interface from /proc/net/route
    std::map<std::string, uint32_t> gatewaysNetOrder;
    {
        std::ifstream rf("/proc/net/route");
        std::string line;
        std::getline(rf, line); // header
        while (std::getline(rf, line)) {
            std::istringstream iss(line);
            std::string iface, destHex, gwHex;
            if (!(iss >> iface >> destHex >> gwHex)) continue;
            if (destHex == "00000000") {
                uint32_t gw = static_cast<uint32_t>(std::strtoul(gwHex.c_str(), nullptr, 16));
                if (gw != 0 && gatewaysNetOrder.find(iface) == gatewaysNetOrder.end()) {
                    gatewaysNetOrder[iface] = gw; // /proc/net/route stores IPv4 in host hex corresponding to sin_addr.s_addr on LE
                }
            }
        }
    }

    // Parse IPv4 DNS servers from /etc/resolv.conf
    std::vector<in_addr> dnsServers;
    {
        std::ifstream df("/etc/resolv.conf");
        std::string line;
        while (std::getline(df, line)) {
            std::istringstream iss(line);
            std::string kw, ip;
            if ((iss >> kw >> ip) && kw == "nameserver") {
                in_addr a = {};
                if (::inet_pton(AF_INET, ip.c_str(), &a) == 1) dnsServers.push_back(a);
            }
        }
    }

    struct RawIface {
        std::string name;
        bool up = false;
        bool loopback = false;
        bool wireless = false;
        uint8_t mac[6] = {};
        bool hasMac = false;
        uint64_t speedBps = 0;
        std::vector<std::pair<sockaddr_in, int>> v4;
        std::vector<sockaddr_in6> v6;
    };
    std::map<std::string, RawIface> ifaces;

    struct ifaddrs* ifaList = nullptr;
    if (::getifaddrs(&ifaList) == 0 && ifaList) {
        for (struct ifaddrs* ifa = ifaList; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_name) continue;
            auto& entry = ifaces[ifa->ifa_name];
            entry.name = ifa->ifa_name;
            if (ifa->ifa_flags & IFF_UP) entry.up = true;
            if (ifa->ifa_flags & IFF_LOOPBACK) entry.loopback = true;
            if (!ifa->ifa_addr) continue;
            const int fam = ifa->ifa_addr->sa_family;
            if (fam == AF_INET && (Family == AF_UNSPEC || Family == AF_INET)) {
                sockaddr_in sin = *reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
                int pfx = 24;
                if (ifa->ifa_netmask && ifa->ifa_netmask->sa_family == AF_INET) {
                    pfx = CountPrefixBits(reinterpret_cast<const sockaddr_in*>(ifa->ifa_netmask)->sin_addr.s_addr);
                }
                entry.v4.emplace_back(sin, pfx);
            } else if (fam == AF_INET6 && (Family == AF_UNSPEC || Family == AF_INET6)) {
                entry.v6.push_back(*reinterpret_cast<const sockaddr_in6*>(ifa->ifa_addr));
            }
        }
        ::freeifaddrs(ifaList);
    }

    for (auto& [name, entry] : ifaces) {
        struct stat wst = {};
        if (::stat(("/sys/class/net/" + name + "/wireless").c_str(), &wst) == 0) entry.wireless = true;
        std::string macStr = ReadSmallTextFile("/sys/class/net/" + name + "/address");
        unsigned int m[6] = {};
        if (std::sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
            for (int i = 0; i < 6; ++i) entry.mac[i] = static_cast<uint8_t>(m[i]);
            entry.hasMac = true;
        }
        std::string spd = ReadSmallTextFile("/sys/class/net/" + name + "/speed");
        if (!spd.empty()) {
            long long mbps = std::atoll(spd.c_str());
            if (mbps > 0) entry.speedBps = static_cast<uint64_t>(mbps) * 1000000ULL;
        }
    }

    auto align8 = [](size_t n) -> size_t { return (n + 7u) & ~static_cast<size_t>(7u); };
    size_t needed = 0;
    for (const auto& [name, e] : ifaces) {
        std::wstring wname = Explorer2::PathUtils::Utf8ToWide(name);
        needed += align8(sizeof(IP_ADAPTER_ADDRESSES));
        needed += align8(name.size() + 1);
        needed += align8((wname.size() + 1) * sizeof(wchar_t)) * 2;
        needed += e.v4.size() * (align8(sizeof(IP_ADAPTER_UNICAST_ADDRESS)) + align8(sizeof(sockaddr_in)));
        needed += e.v6.size() * (align8(sizeof(IP_ADAPTER_UNICAST_ADDRESS)) + align8(sizeof(sockaddr_in6)));
        if (gatewaysNetOrder.count(name)) {
            needed += align8(sizeof(IP_ADAPTER_GATEWAY_ADDRESS)) + align8(sizeof(sockaddr_in));
        }
        needed += dnsServers.size() * (align8(sizeof(IP_ADAPTER_DNS_SERVER_ADDRESS)) + align8(sizeof(sockaddr_in)));
    }
    if (needed == 0) {
        *SizePointer = 0;
        return NO_ERROR;
    }
    if (!AdapterAddresses || *SizePointer < needed) {
        *SizePointer = static_cast<ULONG>(needed + 256);
        return ERROR_BUFFER_OVERFLOW;
    }

    std::memset(AdapterAddresses, 0, *SizePointer);
    uint8_t* cur = reinterpret_cast<uint8_t*>(AdapterAddresses);
    auto alloc = [&](size_t sz) -> void* {
        void* p = cur;
        cur += align8(sz);
        return p;
    };

    IP_ADAPTER_ADDRESSES* prevAdapter = nullptr;
    NET_IFINDEX idx = 1;
    for (const auto& [name, e] : ifaces) {
        auto* aa = static_cast<IP_ADAPTER_ADDRESSES*>(alloc(sizeof(IP_ADAPTER_ADDRESSES)));
        aa->Length = sizeof(IP_ADAPTER_ADDRESSES);
        aa->IfIndex = idx++;
        if (prevAdapter) prevAdapter->Next = aa;
        prevAdapter = aa;

        auto* aname = static_cast<char*>(alloc(name.size() + 1));
        std::memcpy(aname, name.c_str(), name.size() + 1);
        aa->AdapterName = aname;

        std::wstring wname = Explorer2::PathUtils::Utf8ToWide(name);
        auto* wfriendly = static_cast<wchar_t*>(alloc((wname.size() + 1) * sizeof(wchar_t)));
        std::wmemcpy(wfriendly, wname.c_str(), wname.size() + 1);
        auto* wdesc = static_cast<wchar_t*>(alloc((wname.size() + 1) * sizeof(wchar_t)));
        std::wmemcpy(wdesc, wname.c_str(), wname.size() + 1);
        aa->FriendlyName = wfriendly;
        aa->Description = wdesc;

        aa->OperStatus = e.up ? IfOperStatusUp : IfOperStatusDown;
        aa->IfType = e.loopback ? IF_TYPE_SOFTWARE_LOOPBACK : (e.wireless ? IF_TYPE_IEEE80211 : IF_TYPE_ETHERNET_CSMACD);
        aa->TransmitLinkSpeed = e.speedBps;
        aa->ReceiveLinkSpeed = e.speedBps;
        if (e.hasMac) {
            std::memcpy(aa->PhysicalAddress, e.mac, 6);
            aa->PhysicalAddressLength = 6;
        }

        IP_ADAPTER_UNICAST_ADDRESS** uTail = &aa->FirstUnicastAddress;
        for (const auto& [sin, pfx] : e.v4) {
            auto* ua = static_cast<IP_ADAPTER_UNICAST_ADDRESS*>(alloc(sizeof(IP_ADAPTER_UNICAST_ADDRESS)));
            auto* sa = static_cast<sockaddr_in*>(alloc(sizeof(sockaddr_in)));
            *sa = sin;
            ua->Length = sizeof(IP_ADAPTER_UNICAST_ADDRESS);
            ua->Address.lpSockaddr = reinterpret_cast<LPSOCKADDR>(sa);
            ua->Address.iSockaddrLength = sizeof(sockaddr_in);
            ua->OnLinkPrefixLength = static_cast<UINT8>(pfx);
            *uTail = ua;
            uTail = &ua->Next;
        }
        for (const auto& sin6 : e.v6) {
            auto* ua = static_cast<IP_ADAPTER_UNICAST_ADDRESS*>(alloc(sizeof(IP_ADAPTER_UNICAST_ADDRESS)));
            auto* sa = static_cast<sockaddr_in6*>(alloc(sizeof(sockaddr_in6)));
            *sa = sin6;
            ua->Length = sizeof(IP_ADAPTER_UNICAST_ADDRESS);
            ua->Address.lpSockaddr = reinterpret_cast<LPSOCKADDR>(sa);
            ua->Address.iSockaddrLength = sizeof(sockaddr_in6);
            ua->OnLinkPrefixLength = 64;
            *uTail = ua;
            uTail = &ua->Next;
        }

        auto gwIt = gatewaysNetOrder.find(name);
        if (gwIt != gatewaysNetOrder.end()) {
            auto* ga = static_cast<IP_ADAPTER_GATEWAY_ADDRESS*>(alloc(sizeof(IP_ADAPTER_GATEWAY_ADDRESS)));
            auto* sa = static_cast<sockaddr_in*>(alloc(sizeof(sockaddr_in)));
            sa->sin_family = AF_INET;
            sa->sin_addr.s_addr = gwIt->second;
            ga->Length = sizeof(IP_ADAPTER_GATEWAY_ADDRESS);
            ga->Address.lpSockaddr = reinterpret_cast<LPSOCKADDR>(sa);
            ga->Address.iSockaddrLength = sizeof(sockaddr_in);
            aa->FirstGatewayAddress = ga;
        }

        IP_ADAPTER_DNS_SERVER_ADDRESS** dTail = &aa->FirstDnsServerAddress;
        for (const auto& dns : dnsServers) {
            auto* da = static_cast<IP_ADAPTER_DNS_SERVER_ADDRESS*>(alloc(sizeof(IP_ADAPTER_DNS_SERVER_ADDRESS)));
            auto* sa = static_cast<sockaddr_in*>(alloc(sizeof(sockaddr_in)));
            sa->sin_family = AF_INET;
            sa->sin_addr = dns;
            da->Length = sizeof(IP_ADAPTER_DNS_SERVER_ADDRESS);
            da->Address.lpSockaddr = reinterpret_cast<LPSOCKADDR>(sa);
            da->Address.iSockaddrLength = sizeof(sockaddr_in);
            *dTail = da;
            dTail = &da->Next;
        }
    }

    *SizePointer = static_cast<ULONG>(cur - reinterpret_cast<uint8_t*>(AdapterAddresses));
    return NO_ERROR;
}

DWORD GetIfTable2(PMIB_IF_TABLE2* Table) {
    if (!Table) return ERROR_INVALID_PARAMETER;
    *Table = nullptr;

    struct RowTmp {
        std::string name;
        uint64_t rxBytes = 0, rxPkts = 0, txBytes = 0, txPkts = 0;
    };
    std::vector<RowTmp> rows;
    std::ifstream f("/proc/net/dev");
    std::string line;
    std::getline(f, line);
    std::getline(f, line);
    while (std::getline(f, line)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        name.erase(0, name.find_first_not_of(" \t"));
        std::istringstream iss(line.substr(colon + 1));
        RowTmp r;
        r.name = name;
        uint64_t dummy = 0;
        if (!(iss >> r.rxBytes >> r.rxPkts >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy >> r.txBytes >> r.txPkts)) continue;
        rows.push_back(std::move(r));
    }

    const size_t bytes = sizeof(MIB_IF_TABLE2) + (rows.empty() ? 0 : (rows.size() - 1) * sizeof(MIB_IF_ROW2));
    auto* tbl = static_cast<MIB_IF_TABLE2*>(std::calloc(1, bytes));
    if (!tbl) return ERROR_NOT_ENOUGH_MEMORY;
    tbl->NumEntries = static_cast<ULONG>(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        MIB_IF_ROW2& dst = tbl->Table[i];
        dst.InterfaceIndex = static_cast<NET_IFINDEX>(i + 1);
        std::wstring wn = Explorer2::PathUtils::Utf8ToWide(rows[i].name);
        wcsncpy_s(dst.Alias, wn.c_str(), _TRUNCATE);
        wcsncpy_s(dst.Description, wn.c_str(), _TRUNCATE);
        dst.Type = (rows[i].name == "lo") ? IF_TYPE_SOFTWARE_LOOPBACK : IF_TYPE_ETHERNET_CSMACD;
        const std::string oper = ReadSmallTextFile("/sys/class/net/" + rows[i].name + "/operstate");
        dst.OperStatus = (oper == "up" || oper == "unknown") ? IfOperStatusUp : IfOperStatusDown;
        dst.InOctets = rows[i].rxBytes;
        dst.InUcastPkts = rows[i].rxPkts;
        dst.OutOctets = rows[i].txBytes;
        dst.OutUcastPkts = rows[i].txPkts;
    }
    *Table = tbl;
    return NO_ERROR;
}

DWORD GetIpNetTable2(USHORT Family, PMIB_IPNET_TABLE2* Table) {
    if (!Table) return ERROR_INVALID_PARAMETER;
    *Table = nullptr;
    if (Family != AF_UNSPEC && Family != AF_INET) return ERROR_NOT_SUPPORTED;

    struct ArpEntry {
        in_addr ip = {};
        uint8_t mac[6] = {};
        int flags = 0;
    };
    std::vector<ArpEntry> entries;
    std::ifstream f("/proc/net/arp");
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string ipStr, hwType, flagsHex, macStr;
        if (!(iss >> ipStr >> hwType >> flagsHex >> macStr)) continue;
        ArpEntry e;
        if (::inet_pton(AF_INET, ipStr.c_str(), &e.ip) != 1) continue;
        e.flags = static_cast<int>(std::strtol(flagsHex.c_str(), nullptr, 16));
        unsigned int m[6] = {};
        if (std::sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) continue;
        for (int i = 0; i < 6; ++i) e.mac[i] = static_cast<uint8_t>(m[i]);
        entries.push_back(e);
    }

    const size_t bytes = sizeof(MIB_IPNET_TABLE2) + (entries.empty() ? 0 : (entries.size() - 1) * sizeof(MIB_IPNET_ROW2));
    auto* tbl = static_cast<MIB_IPNET_TABLE2*>(std::calloc(1, bytes));
    if (!tbl) return ERROR_NOT_ENOUGH_MEMORY;
    tbl->NumEntries = static_cast<ULONG>(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        MIB_IPNET_ROW2& r = tbl->Table[i];
        r.Address.Ipv4.sin_family = AF_INET;
        r.Address.Ipv4.sin_addr = entries[i].ip;
        std::memcpy(r.PhysicalAddress, entries[i].mac, 6);
        r.PhysicalAddressLength = 6;
        r.State = (entries[i].flags & 0x2) ? NlnsReachable : NlnsIncomplete;
    }
    *Table = tbl;
    return NO_ERROR;
}

VOID FreeMibTable(PVOID Memory) {
    std::free(Memory);
}

DWORD SendARP(IPAddr DestIP, IPAddr /*SrcIP*/, PVOID pMacAddr, PULONG PhyAddrLen) {
    if (!pMacAddr || !PhyAddrLen || *PhyAddrLen < 6) return ERROR_INVALID_PARAMETER;

    // 1. Check local interface MACs first (instant for self IP)
    struct ifaddrs* ifaList = nullptr;
    if (::getifaddrs(&ifaList) == 0 && ifaList) {
        std::string matchIf;
        for (struct ifaddrs* ifa = ifaList; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
                if (reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr)->sin_addr.s_addr == DestIP && ifa->ifa_name) {
                    matchIf = ifa->ifa_name;
                    break;
                }
            }
        }
        ::freeifaddrs(ifaList);
        if (!matchIf.empty()) {
            std::string macStr = ReadSmallTextFile("/sys/class/net/" + matchIf + "/address");
            unsigned int m[6] = {};
            if (std::sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
                auto* dst = static_cast<uint8_t*>(pMacAddr);
                for (int i = 0; i < 6; ++i) dst[i] = static_cast<uint8_t>(m[i]);
                *PhyAddrLen = 6;
                return NO_ERROR;
            }
        }
    }

    // 2. Check /proc/net/arp cache
    PMIB_IPNET_TABLE2 tbl = nullptr;
    if (GetIpNetTable2(AF_INET, &tbl) == NO_ERROR && tbl) {
        for (ULONG i = 0; i < tbl->NumEntries; ++i) {
            if (tbl->Table[i].Address.Ipv4.sin_addr.s_addr == DestIP && tbl->Table[i].State == NlnsReachable) {
                std::memcpy(pMacAddr, tbl->Table[i].PhysicalAddress, 6);
                *PhyAddrLen = 6;
                FreeMibTable(tbl);
                return NO_ERROR;
            }
        }
        FreeMibTable(tbl);
    }
    return ERROR_BAD_NET_NAME;
}

HANDLE IcmpCreateFile() {
    int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (fd < 0) fd = ::socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    // Even if unprivileged ICMP sockets are disabled by sysctl, return a sentinel handle so IcmpSendEcho can TCP-probe
    auto* h = new int(fd);
    return reinterpret_cast<HANDLE>(h);
}

BOOL IcmpCloseHandle(HANDLE IcmpHandle) {
    if (!IcmpHandle || IcmpHandle == INVALID_HANDLE_VALUE) return FALSE;
    auto* h = reinterpret_cast<int*>(IcmpHandle);
    if (*h >= 0) ::close(*h);
    delete h;
    return TRUE;
}

DWORD IcmpSendEcho(HANDLE IcmpHandle, IPAddr DestinationAddress, LPVOID RequestData, WORD RequestSize,
                   IP_OPTION_INFORMATION* /*RequestOptions*/, LPVOID ReplyBuffer, DWORD ReplySize, DWORD Timeout)
{
    if (!IcmpHandle || IcmpHandle == INVALID_HANDLE_VALUE || !ReplyBuffer || ReplySize < sizeof(ICMP_ECHO_REPLY)) return 0;
    const int fd = *reinterpret_cast<int*>(IcmpHandle);
    const auto t0 = std::chrono::steady_clock::now();

    if (fd >= 0) {
        // A raw socket receives every ICMP packet of the host, IP header first; an unprivileged ping socket
        // (SOCK_DGRAM) only the replies to its own echo id, without the IP header
        int sockType = SOCK_DGRAM;
        socklen_t typeLen = sizeof(sockType);
        if (::getsockopt(fd, SOL_SOCKET, SO_TYPE, &sockType, &typeLen) != 0) sockType = SOCK_DGRAM;
        const bool rawSocket = (sockType == SOCK_RAW);

        std::vector<uint8_t> pkt(sizeof(icmphdr) + RequestSize, 0);
        auto* hdr = reinterpret_cast<icmphdr*>(pkt.data());
        hdr->type = ICMP_ECHO;
        hdr->code = 0;
        static std::atomic<uint16_t> s_seq{ 1 };
        const uint16_t seq = s_seq.fetch_add(1);
        const uint16_t ident = static_cast<uint16_t>(::getpid() & 0xFFFF);   // a ping socket puts its own id here
        hdr->un.echo.id = htons(ident);
        hdr->un.echo.sequence = htons(seq);
        if (RequestData && RequestSize > 0) std::memcpy(pkt.data() + sizeof(icmphdr), RequestData, RequestSize);

        // Compute RFC 1071 checksum (needed for SOCK_RAW; ignored by SOCK_DGRAM)
        uint32_t sum = 0;
        for (size_t i = 0; i + 1 < pkt.size(); i += 2) sum += (static_cast<uint32_t>(pkt[i]) << 8) | pkt[i + 1];
        if (pkt.size() & 1) sum += static_cast<uint32_t>(pkt.back()) << 8;
        while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
        hdr->checksum = htons(static_cast<uint16_t>(~sum));

        sockaddr_in dst = {};
        dst.sin_family = AF_INET;
        dst.sin_addr.s_addr = DestinationAddress;
        if (::sendto(fd, pkt.data(), pkt.size(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) > 0) {
            // Read until the reply to this very request arrives or the time is up. A late reply to an earlier
            // request (another host of a LAN scan), or on a raw socket any other ICMP packet, is skipped: taken
            // as this host's answer, it made the host look dead.
            const auto deadline = t0 + std::chrono::milliseconds(Timeout);
            for (;;) {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
                if (left <= 0) break;
                struct pollfd pfd = { fd, POLLIN, 0 };
                const int pr = ::poll(&pfd, 1, static_cast<int>(std::min<long long>(left, 60000)));
                if (pr < 0 && errno == EINTR) continue;
                if (pr <= 0) break;
                uint8_t rbuf[1500] = {};
                sockaddr_in from = {};
                socklen_t flen = sizeof(from);
                const ssize_t got = ::recvfrom(fd, rbuf, sizeof(rbuf), MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&from), &flen);
                if (got < 0) {
                    if (errno == EINTR || errno == EAGAIN) continue;
                    break;                                  // a socket error: no answer will come through it
                }
                if (got == 0 || from.sin_addr.s_addr != DestinationAddress) continue;
                size_t off = 0;
                if (rawSocket) {
                    if (got < 20) continue;
                    off = static_cast<size_t>(rbuf[0] & 0x0Fu) * 4u;     // the IP header's own length
                }
                if (static_cast<size_t>(got) < off + sizeof(icmphdr)) continue;
                icmphdr reply = {};
                std::memcpy(&reply, rbuf + off, sizeof(reply));
                if (reply.type != ICMP_ECHOREPLY || ntohs(reply.un.echo.sequence) != seq) continue;
                if (rawSocket && ntohs(reply.un.echo.id) != ident) continue;    // another program's ping
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
                auto* rep = reinterpret_cast<ICMP_ECHO_REPLY*>(ReplyBuffer);
                std::memset(rep, 0, sizeof(*rep));
                rep->Address = DestinationAddress;
                rep->Status = IP_SUCCESS;
                rep->RoundTripTime = static_cast<ULONG>(std::max<long long>(0, ms));
                return 1;
            }
            return 0;
        }
    }

    // Unprivileged fallback when kernel ping_group_range blocks SOCK_DGRAM/SOCK_RAW IPPROTO_ICMP:
    // 1. Loopback (127.0.0.0/8) or own UP interface IPv4 address -> instant local kernel route verify
    const uint32_t hostOrder = ntohl(DestinationAddress);
    bool isLocalSelf = ((hostOrder >> 24) == 127u);
    if (!isLocalSelf) {
        struct ifaddrs* ifaList = nullptr;
        if (::getifaddrs(&ifaList) == 0 && ifaList) {
            for (struct ifaddrs* ifa = ifaList; ifa; ifa = ifa->ifa_next) {
                if ((ifa->ifa_flags & IFF_UP) && ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
                    if (reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr)->sin_addr.s_addr == DestinationAddress) {
                        isLocalSelf = true;
                        break;
                    }
                }
            }
            ::freeifaddrs(ifaList);
        }
    }
    if (isLocalSelf) {
        int ufd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
        if (ufd >= 0) {
            sockaddr_in dst = {};
            dst.sin_family = AF_INET;
            dst.sin_addr.s_addr = DestinationAddress;
            dst.sin_port = htons(9);
            const bool ok = (::connect(ufd, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst)) == 0);
            ::close(ufd);
            if (ok) {
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
                auto* rep = reinterpret_cast<ICMP_ECHO_REPLY*>(ReplyBuffer);
                std::memset(rep, 0, sizeof(*rep));
                rep->Address = DestinationAddress;
                rep->Status = IP_SUCCESS;
                rep->RoundTripTime = static_cast<ULONG>(std::max<long long>(0, ms));
                return 1;
            }
        }
    }

    // 2. Remote LAN host TCP SYN/ACK or SYN/RST probe across common ports (445 SMB, 80 HTTP, 22 SSH)
    static const uint16_t kPorts[3] = { 445, 80, 22 };
    struct pollfd pfds[3] = {};
    int count = 0;
    for (uint16_t port : kPorts) {
        int sfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
        if (sfd < 0) continue;
        sockaddr_in dst = {};
        dst.sin_family = AF_INET;
        dst.sin_addr.s_addr = DestinationAddress;
        dst.sin_port = htons(port);
        int rc = ::connect(sfd, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
        if (rc == 0 || errno == ECONNREFUSED) {
            for (int k = 0; k < count; ++k) ::close(pfds[k].fd);
            ::close(sfd);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            auto* rep = reinterpret_cast<ICMP_ECHO_REPLY*>(ReplyBuffer);
            std::memset(rep, 0, sizeof(*rep));
            rep->Address = DestinationAddress;
            rep->Status = IP_SUCCESS;
            rep->RoundTripTime = static_cast<ULONG>(std::max<long long>(0, ms));
            return 1;
        }
        if (errno == EINPROGRESS) {
            pfds[count].fd = sfd;
            pfds[count].events = POLLOUT;
            ++count;
        } else {
            ::close(sfd);
        }
    }
    bool alive = false;
    if (count > 0) {
        const int waitMs = static_cast<int>(std::clamp<DWORD>(Timeout, 20u, 1500u));
        if (::poll(pfds, static_cast<nfds_t>(count), waitMs) > 0) {
            for (int k = 0; k < count; ++k) {
                if (pfds[k].revents & (POLLOUT | POLLERR | POLLHUP)) {
                    int soErr = 0;
                    socklen_t optLen = sizeof(soErr);
                    if (::getsockopt(pfds[k].fd, SOL_SOCKET, SO_ERROR, &soErr, &optLen) == 0) {
                        if (soErr == 0 || soErr == ECONNREFUSED) {
                            alive = true;
                            break;
                        }
                    }
                }
            }
        }
        for (int k = 0; k < count; ++k) ::close(pfds[k].fd);
    }
    if (alive) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        auto* rep = reinterpret_cast<ICMP_ECHO_REPLY*>(ReplyBuffer);
        std::memset(rep, 0, sizeof(*rep));
        rep->Address = DestinationAddress;
        rep->Status = IP_SUCCESS;
        rep->RoundTripTime = static_cast<ULONG>(std::max<long long>(0, ms));
        return 1;
    }
    return 0;
}

// ==============================================================================================================
// 3. WinINet & WNet Fallbacks (curl handles FTP/FTPS in FtpClient.cpp; SMB/NFS mounts handled in NetShares)
// ==============================================================================================================
HINTERNET InternetOpenW(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD) { return nullptr; }
BOOL InternetSetOptionW(HINTERNET, DWORD, LPVOID, DWORD) { return FALSE; }
HINTERNET InternetConnectW(HINTERNET, LPCWSTR, INTERNET_PORT, LPCWSTR, LPCWSTR, DWORD, DWORD, DWORD_PTR) { return nullptr; }
BOOL InternetCloseHandle(HINTERNET) { return TRUE; }
BOOL InternetReadFile(HINTERNET, LPVOID, DWORD, LPDWORD) { return FALSE; }
BOOL InternetWriteFile(HINTERNET, LPCVOID, DWORD, LPDWORD) { return FALSE; }
BOOL InternetFindNextFileW(HINTERNET, LPVOID) { return FALSE; }
BOOL FtpSetCurrentDirectoryW(HINTERNET, LPCWSTR) { return FALSE; }
HINTERNET FtpFindFirstFileW(HINTERNET, LPCWSTR, WIN32_FIND_DATAW*, DWORD, DWORD_PTR) { return nullptr; }
HINTERNET FtpOpenFileW(HINTERNET, LPCWSTR, DWORD, DWORD, DWORD_PTR) { return nullptr; }
BOOL FtpCreateDirectoryW(HINTERNET, LPCWSTR) { return FALSE; }
BOOL FtpRemoveDirectoryW(HINTERNET, LPCWSTR) { return FALSE; }
BOOL FtpDeleteFileW(HINTERNET, LPCWSTR) { return FALSE; }
BOOL FtpRenameFileW(HINTERNET, LPCWSTR, LPCWSTR) { return FALSE; }

DWORD WNetOpenEnumW(DWORD, DWORD, DWORD, LPNETRESOURCEW, LPHANDLE) { return ERROR_NO_NETWORK; }
DWORD WNetEnumResourceW(HANDLE, LPDWORD, LPVOID, LPDWORD) { return ERROR_NO_MORE_ITEMS; }
DWORD WNetCloseEnum(HANDLE) { return NO_ERROR; }
DWORD WNetGetConnectionW(LPCWSTR, LPWSTR, LPDWORD) { return ERROR_NOT_SUPPORTED; }
DWORD WNetGetLastErrorW(LPDWORD, LPWSTR, DWORD, LPWSTR, DWORD) { return NO_ERROR; }
DWORD WNetAddConnection2W(LPNETRESOURCEW, LPCWSTR, LPCWSTR, DWORD) { return ERROR_NOT_SUPPORTED; }
DWORD WNetCancelConnection2W(LPCWSTR, DWORD, BOOL) { return ERROR_NOT_SUPPORTED; }
DWORD WNetConnectionDialog(HWND, DWORD) { return ERROR_NOT_SUPPORTED; }
DWORD WNetDisconnectDialog(HWND, DWORD) { return ERROR_NOT_SUPPORTED; }

// ==============================================================================================================
// 4. Process & System Performance Counters (psapi.h + tlhelp32.h over /proc)
// ==============================================================================================================
BOOL GetPerformanceInfo(PERFORMANCE_INFORMATION* pInfo, DWORD cb) {
    if (!pInfo || cb < sizeof(PERFORMANCE_INFORMATION)) return FALSE;
    std::memset(pInfo, 0, sizeof(*pInfo));
    pInfo->cb = sizeof(PERFORMANCE_INFORMATION);
    const long pageSz = ::sysconf(_SC_PAGESIZE);
    pInfo->PageSize = (pageSz > 0) ? static_cast<SIZE_T>(pageSz) : 4096u;

    uint64_t memTotalKB = 0, memAvailKB = 0, cachedKB = 0, buffersKB = 0, slabKB = 0, swapTotalKB = 0, swapFreeKB = 0;
    std::ifstream mf("/proc/meminfo");
    std::string k;
    uint64_t v = 0;
    std::string unit;
    while (mf >> k >> v) {
        std::getline(mf, unit);
        if (k == "MemTotal:") memTotalKB = v;
        else if (k == "MemAvailable:") memAvailKB = v;
        else if (k == "Cached:") cachedKB = v;
        else if (k == "Buffers:") buffersKB = v;
        else if (k == "Slab:") slabKB = v;
        else if (k == "SwapTotal:") swapTotalKB = v;
        else if (k == "SwapFree:") swapFreeKB = v;
    }
    const uint64_t pg = pInfo->PageSize;
    pInfo->PhysicalTotal     = static_cast<SIZE_T>((memTotalKB * 1024ULL) / pg);
    pInfo->PhysicalAvailable = static_cast<SIZE_T>((memAvailKB * 1024ULL) / pg);
    pInfo->SystemCache       = static_cast<SIZE_T>(((cachedKB + buffersKB) * 1024ULL) / pg);
    pInfo->KernelTotal       = static_cast<SIZE_T>((slabKB * 1024ULL) / pg);
    pInfo->KernelPaged       = pInfo->KernelTotal;
    const uint64_t usedTotalKB = (memTotalKB > memAvailKB ? (memTotalKB - memAvailKB) : 0) +
                                 (swapTotalKB > swapFreeKB ? (swapTotalKB - swapFreeKB) : 0);
    pInfo->CommitTotal = static_cast<SIZE_T>((usedTotalKB * 1024ULL) / pg);
    pInfo->CommitLimit = static_cast<SIZE_T>(((memTotalKB + swapTotalKB) * 1024ULL) / pg);
    pInfo->CommitPeak  = pInfo->CommitTotal;

    // Processes: the numeric entries of /proc, counted without opening any of them (this runs on the UI thread
    // about twice a second, and opening /proc/<pid>/stat for every process cost milliseconds each time)
    DWORD procs = 0;
    if (DIR* d = ::opendir("/proc")) {
        while (dirent* e = ::readdir(d)) {
            const char* n = e->d_name;
            if (*n < '1' || *n > '9') continue;
            bool numeric = true;
            for (const char* c = n; *c; ++c) {
                if (*c < '0' || *c > '9') { numeric = false; break; }
            }
            if (numeric) ++procs;
        }
        ::closedir(d);
    }
    // Threads: the fourth field of /proc/loadavg is "running/total" scheduling entities, and those are threads
    DWORD threads = 0;
    {
        std::istringstream la(ReadSmallTextFile("/proc/loadavg"));
        std::string load1, load5, load15, entities;
        if (la >> load1 >> load5 >> load15 >> entities) {
            const size_t slash = entities.find('/');
            if (slash != std::string::npos) threads = static_cast<DWORD>(std::strtoul(entities.c_str() + slash + 1, nullptr, 10));
        }
    }
    if (threads == 0) {
        struct sysinfo si = {};
        if (::sysinfo(&si) == 0) threads = static_cast<DWORD>(si.procs);     // the kernel's thread count too
    }
    pInfo->ProcessCount = procs;
    pInfo->ThreadCount = threads ? threads : procs;
    return TRUE;
}

} // extern "C"

namespace {
struct ProcHandleObj : Explorer2::Compat::HandleBase {
    explicit ProcHandleObj(DWORD p) : HandleBase(Explorer2::Compat::HandleKind::Custom), pid(p) {}
    DWORD pid = 0;
};

struct ToolhelpSnapObj : Explorer2::Compat::HandleBase {
    ToolhelpSnapObj() : HandleBase(Explorer2::Compat::HandleKind::Custom) {}
    std::vector<PROCESSENTRY32W> entries;
    size_t index = 0;
};
} // namespace

HANDLE OpenProcess(DWORD /*dwDesiredAccess*/, BOOL /*bInheritHandle*/, DWORD dwProcessId) {
    if (dwProcessId == 0) return nullptr;
    struct stat st = {};
    if (::stat(("/proc/" + std::to_string(dwProcessId)).c_str(), &st) != 0) return nullptr;
    return static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(new ProcHandleObj(dwProcessId)));
}

extern "C" BOOL GetProcessMemoryInfo(HANDLE Process, PROCESS_MEMORY_COUNTERS* ppsmemCounters, DWORD cb) {
    if (!Process || !ppsmemCounters || cb < sizeof(PROCESS_MEMORY_COUNTERS)) return FALSE;
    DWORD pid = 0;
    if (Process == GetCurrentProcess()) {
        pid = GetCurrentProcessId();
    } else {
        auto* b = Explorer2::Compat::BaseOf(Process);
        if (!b || b->kind != Explorer2::Compat::HandleKind::Custom) return FALSE;
        pid = static_cast<ProcHandleObj*>(b)->pid;
    }
    std::ifstream sf("/proc/" + std::to_string(pid) + "/statm");
    uint64_t sizePages = 0, rssPages = 0;
    if (!(sf >> sizePages >> rssPages)) return FALSE;
    const uint64_t pg = static_cast<uint64_t>(::sysconf(_SC_PAGESIZE));
    std::memset(ppsmemCounters, 0, cb);
    ppsmemCounters->cb = cb;
    ppsmemCounters->WorkingSetSize = static_cast<SIZE_T>(rssPages * pg);
    ppsmemCounters->PeakWorkingSetSize = ppsmemCounters->WorkingSetSize;
    ppsmemCounters->PagefileUsage = static_cast<SIZE_T>(sizePages * pg);
    if (cb >= sizeof(PROCESS_MEMORY_COUNTERS_EX)) {
        reinterpret_cast<PROCESS_MEMORY_COUNTERS_EX*>(ppsmemCounters)->PrivateUsage = ppsmemCounters->WorkingSetSize;
    }
    return TRUE;
}

HANDLE CreateToolhelp32Snapshot(DWORD dwFlags, DWORD /*th32ProcessID*/) {
    if (!(dwFlags & TH32CS_SNAPPROCESS)) return INVALID_HANDLE_VALUE;
    auto* snap = new ToolhelpSnapObj();
    if (DIR* d = ::opendir("/proc")) {
        while (dirent* e = ::readdir(d)) {
            if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
            const DWORD pid = static_cast<DWORD>(std::strtoul(e->d_name, nullptr, 10));
            if (pid == 0) continue;
            std::ifstream sf(std::string("/proc/") + e->d_name + "/stat");
            std::string line;
            if (!std::getline(sf, line)) continue;
            const size_t lp = line.find('(');
            const size_t rp = line.rfind(')');
            if (lp == std::string::npos || rp == std::string::npos || rp <= lp) continue;
            std::string comm = line.substr(lp + 1, rp - lp - 1);
            std::istringstream iss(line.substr(rp + 2));
            char state = 0;
            unsigned long ppid = 0;
            long long dummy = 0, numThreads = 1;
            iss >> state >> ppid;
            if (state == 'Z' || state == 'X') continue;
            for (int f = 0; f < 15; ++f) iss >> dummy;
            iss >> numThreads;

            PROCESSENTRY32W pe = {};
            pe.dwSize = sizeof(pe);
            pe.th32ProcessID = pid;
            pe.th32ParentProcessID = static_cast<DWORD>(ppid);
            pe.cntThreads = static_cast<DWORD>(std::max<long long>(1, numThreads));
            std::wstring wcomm = Explorer2::PathUtils::Utf8ToWide(comm);
            wcsncpy_s(pe.szExeFile, wcomm.c_str(), _TRUNCATE);
            snap->entries.push_back(pe);
        }
        ::closedir(d);
    }
    return static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(snap));
}

BOOL Process32FirstW(HANDLE hSnapshot, PROCESSENTRY32W* lppe) {
    auto* b = Explorer2::Compat::BaseOf(hSnapshot);
    if (!b || b->kind != Explorer2::Compat::HandleKind::Custom || !lppe) return FALSE;
    auto* snap = static_cast<ToolhelpSnapObj*>(b);
    snap->index = 0;
    if (snap->entries.empty()) return FALSE;
    *lppe = snap->entries[snap->index++];
    return TRUE;
}

BOOL Process32NextW(HANDLE hSnapshot, PROCESSENTRY32W* lppe) {
    auto* b = Explorer2::Compat::BaseOf(hSnapshot);
    if (!b || b->kind != Explorer2::Compat::HandleKind::Custom || !lppe) return FALSE;
    auto* snap = static_cast<ToolhelpSnapObj*>(b);
    if (snap->index >= snap->entries.size()) return FALSE;
    *lppe = snap->entries[snap->index++];
    return TRUE;
}

// ==============================================================================================================
// 5. DXGI 1.4 Adapter Discovery via Linux /sys/class/drm + Audio via /proc/asound/cards + PDH Thermal/Disk
// ==============================================================================================================
namespace {

struct LinuxGpuEntry {
    std::wstring name;
    UINT vendorId = 0;
    UINT deviceId = 0;
    uint64_t vramTotal = 0;
    std::string drmDeviceDir;
};

std::vector<LinuxGpuEntry> DiscoverLinuxGpus() {
    std::vector<LinuxGpuEntry> out;
    if (DIR* d = ::opendir("/sys/class/drm")) {
        while (dirent* e = ::readdir(d)) {
            std::string nm = e->d_name;
            if (nm.rfind("card", 0) != 0 || nm.find('-') != std::string::npos) continue;
            std::string devDir = "/sys/class/drm/" + nm + "/device";
            std::string venStr = ReadSmallTextFile(devDir + "/vendor");
            std::string devStr = ReadSmallTextFile(devDir + "/device");
            if (venStr.empty() && devStr.empty()) continue;
            LinuxGpuEntry g;
            g.vendorId = static_cast<UINT>(std::strtoul(venStr.c_str(), nullptr, 16));
            g.deviceId = static_cast<UINT>(std::strtoul(devStr.c_str(), nullptr, 16));
            g.drmDeviceDir = devDir;
            std::string vramStr = ReadSmallTextFile(devDir + "/mem_info_vram_total");
            if (!vramStr.empty()) g.vramTotal = std::strtoull(vramStr.c_str(), nullptr, 10);
            wchar_t desc[128] = {};
            const wchar_t* vname = (g.vendorId == 0x10DE) ? L"NVIDIA GPU" :
                                   (g.vendorId == 0x1002 || g.vendorId == 0x1022) ? L"AMD Radeon GPU" :
                                   (g.vendorId == 0x8086) ? L"Intel Graphics" : L"Linux DRM Graphics";
            swprintf_s(desc, L"%s (%04X:%04X)", vname, g.vendorId, g.deviceId);
            g.name = desc;
            out.push_back(std::move(g));
        }
        ::closedir(d);
    }

    // Fallback for WSL2 / CUDA / headless NVIDIA hosts where /sys/class/drm has no card0 but NVML is present:
    if (out.empty()) {
        void* nvml = ::dlopen("libnvidia-ml.so.1", RTLD_LAZY);
        if (!nvml) nvml = ::dlopen("/usr/lib/wsl/lib/libnvidia-ml.so.1", RTLD_LAZY);
        if (nvml) {
            using FnInit   = int(*)();
            using FnCount  = int(*)(unsigned int*);
            using FnGetH   = int(*)(unsigned int, void**);
            using FnName   = int(*)(void*, char*, unsigned int);
            struct NvMem   { unsigned long long total, free, used; };
            using FnMem    = int(*)(void*, NvMem*);
            struct NvPci   { char legacyBusId[16]; unsigned int domain, bus, device, pciDeviceId, pciSubSystemId; char busId[32]; };
            using FnPci    = int(*)(void*, NvPci*);

            auto fnInit  = reinterpret_cast<FnInit>(::dlsym(nvml, "nvmlInit_v2"));
            auto fnCount = reinterpret_cast<FnCount>(::dlsym(nvml, "nvmlDeviceGetCount_v2"));
            auto fnGetH  = reinterpret_cast<FnGetH>(::dlsym(nvml, "nvmlDeviceGetHandleByIndex_v2"));
            auto fnName  = reinterpret_cast<FnName>(::dlsym(nvml, "nvmlDeviceGetName"));
            auto fnMem   = reinterpret_cast<FnMem>(::dlsym(nvml, "nvmlDeviceGetMemoryInfo"));
            auto fnPci   = reinterpret_cast<FnPci>(::dlsym(nvml, "nvmlDeviceGetPciInfo_v3"));

            unsigned int count = 0;
            if (fnInit && fnInit() == 0 && fnCount && fnCount(&count) == 0) {
                for (unsigned int i = 0; i < count; ++i) {
                    void* dev = nullptr;
                    if (!fnGetH || fnGetH(i, &dev) != 0 || !dev) continue;
                    LinuxGpuEntry g;
                    g.vendorId = 0x10DE;
                    g.deviceId = 0x2000 + i;
                    char nameBuf[128] = {};
                    if (fnName && fnName(dev, nameBuf, sizeof(nameBuf)) == 0 && nameBuf[0]) {
                        g.name = Explorer2::PathUtils::Utf8ToWide(nameBuf);
                    } else {
                        g.name = L"NVIDIA GPU";
                    }
                    NvMem mem = {};
                    if (fnMem && fnMem(dev, &mem) == 0) g.vramTotal = mem.total;
                    NvPci pci = {};
                    if (fnPci && fnPci(dev, &pci) == 0 && pci.pciDeviceId != 0) {
                        g.deviceId = (pci.pciDeviceId >> 16) & 0xFFFFu;
                    }
                    out.push_back(std::move(g));
                }
            }
            // Keep nvml loaded (refcount > 0) so HardwareInspector's subsequent LoadLibraryW("nvml.dll") is instant
        }
    }
    return out;
}

class LinuxDxgiAdapter final : public IDXGIAdapter3 {
public:
    explicit LinuxDxgiAdapter(LinuxGpuEntry info) : m_info(std::move(info)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIAdapter1) || riid == __uuidof(IDXGIAdapter3)) {
            *ppv = static_cast<IDXGIAdapter3*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1* pDesc) override {
        if (!pDesc) return E_INVALIDARG;
        std::memset(pDesc, 0, sizeof(*pDesc));
        wcsncpy_s(pDesc->Description, m_info.name.c_str(), _TRUNCATE);
        pDesc->VendorId = m_info.vendorId;
        pDesc->DeviceId = m_info.deviceId;
        pDesc->DedicatedVideoMemory = static_cast<SIZE_T>(m_info.vramTotal);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE QueryVideoMemoryInfo(UINT, DXGI_MEMORY_SEGMENT_GROUP seg, DXGI_QUERY_VIDEO_MEMORY_INFO* out) override {
        if (!out) return E_INVALIDARG;
        std::memset(out, 0, sizeof(*out));
        if (seg == DXGI_MEMORY_SEGMENT_GROUP_LOCAL) {
            out->Budget = m_info.vramTotal;
            std::string usedStr = ReadSmallTextFile(m_info.drmDeviceDir + "/mem_info_vram_used");
            if (!usedStr.empty()) out->CurrentUsage = std::strtoull(usedStr.c_str(), nullptr, 10);
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> m_ref{ 1 };
    LinuxGpuEntry m_info;
};

class LinuxDxgiFactory final : public IDXGIFactory1 {
public:
    LinuxDxgiFactory() : m_gpus(DiscoverLinuxGpus()) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIFactory1)) {
            *ppv = static_cast<IDXGIFactory1*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT idx, IDXGIAdapter1** ppAdapter) override {
        if (!ppAdapter) return E_INVALIDARG;
        if (idx >= m_gpus.size()) { *ppAdapter = nullptr; return DXGI_ERROR_NOT_FOUND; }
        *ppAdapter = new LinuxDxgiAdapter(m_gpus[idx]);
        return S_OK;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    std::vector<LinuxGpuEntry> m_gpus;
};

struct PdhQueryState {
    struct DiskSample { uint64_t rBytes = 0, wBytes = 0, ioMs = 0; };
    std::map<int, DiskSample> prevDisk, curDisk;
    uint64_t prevTick = 0, curTick = 0;
};

struct PdhCounterState {
    PdhQueryState* query = nullptr;
    enum Kind { Thermal, DiskRead, DiskWrite, DiskBusy } kind = Thermal;
};

std::mutex g_pdhMutex;
std::vector<std::unique_ptr<PdhCounterState>> g_pdhCounters;

std::vector<std::wstring> ReadAlsaCards() {
    std::vector<std::wstring> out;
    std::ifstream f("/proc/asound/cards");
    std::string line;
    while (std::getline(f, line)) {
        const size_t dash = line.find(" - ");
        if (dash != std::string::npos) {
            out.push_back(Explorer2::PathUtils::Utf8ToWide(line.substr(dash + 3)));
        }
    }
    return out;
}

} // namespace

extern "C" {

HRESULT CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    if (!ppFactory) return E_POINTER;
    auto* f = new LinuxDxgiFactory();
    HRESULT hr = f->QueryInterface(riid, ppFactory);
    f->Release();
    return hr;
}

UINT waveOutGetNumDevs() {
    return static_cast<UINT>(ReadAlsaCards().size());
}

MMRESULT waveOutGetDevCapsW(UINT_PTR uDeviceID, WAVEOUTCAPSW* pwoc, UINT cbwoc) {
    auto cards = ReadAlsaCards();
    if (!pwoc || cbwoc < sizeof(WAVEOUTCAPSW) || uDeviceID >= cards.size()) return 1;
    std::memset(pwoc, 0, sizeof(*pwoc));
    wcsncpy_s(pwoc->szPname, cards[uDeviceID].c_str(), _TRUNCATE);
    pwoc->wChannels = 2;
    return MMSYSERR_NOERROR;
}

UINT waveInGetNumDevs() { return waveOutGetNumDevs(); }

MMRESULT waveInGetDevCapsW(UINT_PTR uDeviceID, WAVEINCAPSW* pwic, UINT cbwic) {
    auto cards = ReadAlsaCards();
    if (!pwic || cbwic < sizeof(WAVEINCAPSW) || uDeviceID >= cards.size()) return 1;
    std::memset(pwic, 0, sizeof(*pwic));
    wcsncpy_s(pwic->szPname, cards[uDeviceID].c_str(), _TRUNCATE);
    pwic->wChannels = 2;
    return MMSYSERR_NOERROR;
}

PDH_STATUS PdhOpenQueryW(LPCWSTR, DWORD_PTR, PDH_HQUERY* phQuery) {
    if (!phQuery) return ERROR_INVALID_PARAMETER;
    *phQuery = reinterpret_cast<PDH_HQUERY>(new PdhQueryState());
    return ERROR_SUCCESS;
}

PDH_STATUS PdhAddEnglishCounterW(PDH_HQUERY hQuery, LPCWSTR szFullCounterPath, DWORD_PTR, PDH_HCOUNTER* phCounter) {
    if (!hQuery || !szFullCounterPath || !phCounter) return ERROR_INVALID_PARAMETER;
    std::wstring p(szFullCounterPath);
    auto cs = std::make_unique<PdhCounterState>();
    cs->query = reinterpret_cast<PdhQueryState*>(hQuery);
    if (p.find(L"Thermal Zone") != std::wstring::npos) cs->kind = PdhCounterState::Thermal;
    else if (p.find(L"Disk Read Bytes") != std::wstring::npos) cs->kind = PdhCounterState::DiskRead;
    else if (p.find(L"Disk Write Bytes") != std::wstring::npos) cs->kind = PdhCounterState::DiskWrite;
    else cs->kind = PdhCounterState::DiskBusy;

    std::lock_guard<std::mutex> lk(g_pdhMutex);
    *phCounter = reinterpret_cast<PDH_HCOUNTER>(cs.get());
    g_pdhCounters.push_back(std::move(cs));
    return ERROR_SUCCESS;
}

PDH_STATUS PdhCollectQueryData(PDH_HQUERY hQuery) {
    if (!hQuery) return ERROR_INVALID_PARAMETER;
    auto* qs = reinterpret_cast<PdhQueryState*>(hQuery);
    qs->prevDisk = qs->curDisk;
    qs->prevTick = qs->curTick;
    qs->curTick = GetTickCount64();
    qs->curDisk.clear();

    std::ifstream f("/proc/diskstats");
    std::string line;
    int diskIdx = 0;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        int major = 0, minor = 0;
        std::string dev;
        if (!(iss >> major >> minor >> dev)) continue;
        // Whole block devices (sda, nvme0n1, vda) rather than partitions or loop
        if (dev.rfind("loop", 0) == 0 || dev.rfind("ram", 0) == 0) continue;
        const bool isNvme = (dev.rfind("nvme", 0) == 0 && dev.find('p') == std::string::npos);
        const bool isSd   = ((dev.rfind("sd", 0) == 0 || dev.rfind("vd", 0) == 0) && dev.size() == 3);
        if (!isNvme && !isSd) continue;
        uint64_t rdIos=0, rdMrg=0, rdSec=0, rdMs=0, wrIos=0, wrMrg=0, wrSec=0, wrMs=0, inFlight=0, ioMs=0;
        if (iss >> rdIos >> rdMrg >> rdSec >> rdMs >> wrIos >> wrMrg >> wrSec >> wrMs >> inFlight >> ioMs) {
            qs->curDisk[diskIdx++] = { rdSec * 512ULL, wrSec * 512ULL, ioMs };
        }
    }
    return ERROR_SUCCESS;
}

PDH_STATUS PdhGetFormattedCounterArrayW(PDH_HCOUNTER hCounter, DWORD dwFormat, LPDWORD lpdwBufferSize,
                                        LPDWORD lpdwItemCount, PDH_FMT_COUNTERVALUE_ITEM_W* ItemBuffer)
{
    if (!hCounter || !lpdwBufferSize || !lpdwItemCount) return ERROR_INVALID_PARAMETER;
    auto* cs = reinterpret_cast<PdhCounterState*>(hCounter);
    struct ItemData { std::wstring name; double dval = 0.0; LONG lval = 0; };
    std::vector<ItemData> rows;

    if (cs->kind == PdhCounterState::Thermal) {
        if (DIR* d = ::opendir("/sys/class/thermal")) {
            while (dirent* e = ::readdir(d)) {
                std::string nm = e->d_name;
                if (nm.rfind("thermal_zone", 0) != 0) continue;
                std::string tStr = ReadSmallTextFile("/sys/class/thermal/" + nm + "/temp");
                std::string typeStr = ReadSmallTextFile("/sys/class/thermal/" + nm + "/type");
                if (tStr.empty()) continue;
                double milliC = std::atof(tStr.c_str());
                if (milliC > 1000.0) milliC /= 1000.0;
                if (milliC < 1.0 || milliC > 150.0) continue;
                ItemData it;
                it.name = Explorer2::PathUtils::Utf8ToWide(typeStr.empty() ? nm : typeStr);
                it.lval = static_cast<LONG>(milliC + 273.15 + 0.5); // Windows Thermal Zone returns Kelvin!
                it.dval = it.lval;
                rows.push_back(std::move(it));
            }
            ::closedir(d);
        }
    } else if (cs->query && cs->query->prevTick > 0 && cs->query->curTick > cs->query->prevTick) {
        const double dtSec = static_cast<double>(cs->query->curTick - cs->query->prevTick) / 1000.0;
        for (const auto& [idx, cur] : cs->query->curDisk) {
            auto pit = cs->query->prevDisk.find(idx);
            if (pit == cs->query->prevDisk.end()) continue;
            ItemData it;
            it.name = std::to_wstring(idx);
            if (cs->kind == PdhCounterState::DiskRead) {
                it.dval = (cur.rBytes >= pit->second.rBytes) ? ((cur.rBytes - pit->second.rBytes) / dtSec) : 0.0;
            } else if (cs->kind == PdhCounterState::DiskWrite) {
                it.dval = (cur.wBytes >= pit->second.wBytes) ? ((cur.wBytes - pit->second.wBytes) / dtSec) : 0.0;
            } else {
                it.dval = (cur.ioMs >= pit->second.ioMs) ? ((cur.ioMs - pit->second.ioMs) / (dtSec * 10.0)) : 0.0;
            }
            it.lval = static_cast<LONG>(it.dval);
            rows.push_back(std::move(it));
        }
    }

    if (rows.empty()) {
        *lpdwBufferSize = 0;
        *lpdwItemCount = 0;
        return PDH_NO_DATA;
    }

    size_t strChars = 0;
    for (const auto& r : rows) strChars += r.name.size() + 1;
    const size_t needed = rows.size() * sizeof(PDH_FMT_COUNTERVALUE_ITEM_W) + strChars * sizeof(wchar_t);
    *lpdwItemCount = static_cast<DWORD>(rows.size());
    if (!ItemBuffer || *lpdwBufferSize < needed) {
        *lpdwBufferSize = static_cast<DWORD>(needed);
        return PDH_MORE_DATA;
    }

    auto* strPtr = reinterpret_cast<wchar_t*>(reinterpret_cast<uint8_t*>(ItemBuffer) + rows.size() * sizeof(PDH_FMT_COUNTERVALUE_ITEM_W));
    for (size_t i = 0; i < rows.size(); ++i) {
        ItemBuffer[i].szName = strPtr;
        std::wmemcpy(strPtr, rows[i].name.c_str(), rows[i].name.size() + 1);
        strPtr += rows[i].name.size() + 1;
        ItemBuffer[i].FmtValue.CStatus = PDH_CSTATUS_VALID_DATA;
        if (dwFormat & PDH_FMT_LONG) ItemBuffer[i].FmtValue.longValue = rows[i].lval;
        else ItemBuffer[i].FmtValue.doubleValue = rows[i].dval;
    }
    *lpdwBufferSize = static_cast<DWORD>(needed);
    return ERROR_SUCCESS;
}

PDH_STATUS PdhCloseQuery(PDH_HQUERY hQuery) {
    if (!hQuery) return ERROR_INVALID_PARAMETER;
    auto* qs = reinterpret_cast<PdhQueryState*>(hQuery);
    {
        std::lock_guard<std::mutex> lk(g_pdhMutex);
        g_pdhCounters.erase(
            std::remove_if(g_pdhCounters.begin(), g_pdhCounters.end(),
                           [qs](const std::unique_ptr<PdhCounterState>& c) { return c->query == qs; }),
            g_pdhCounters.end());
    }
    delete qs;
    return ERROR_SUCCESS;
}

// ==============================================================================================================
// 6. CfgMgr32 PnP Device Tree & Shell Desktop Helpers
// ==============================================================================================================
CONFIGRET CM_Locate_DevNodeW(PDEVINST pdnDevInst, LPWSTR, ULONG) {
    if (pdnDevInst) *pdnDevInst = 0;
    return CR_NO_SUCH_DEVNODE;
}
CONFIGRET CM_Get_Child(PDEVINST, DEVINST, ULONG) { return CR_NO_SUCH_DEVNODE; }
CONFIGRET CM_Get_Sibling(PDEVINST, DEVINST, ULONG) { return CR_NO_SUCH_DEVNODE; }
CONFIGRET CM_Get_Parent(PDEVINST, DEVINST, ULONG) { return CR_NO_SUCH_DEVNODE; }
CONFIGRET CM_Get_Device_IDW(DEVINST, PWSTR, ULONG, ULONG) { return CR_NO_SUCH_DEVNODE; }
CONFIGRET CM_Get_DevNode_Registry_PropertyW(DEVINST, ULONG, PULONG, PVOID, PULONG, ULONG) { return CR_NO_SUCH_VALUE; }
CONFIGRET CM_Get_DevNode_PropertyW(DEVINST, const DEVPROPKEY*, DEVPROPTYPE*, PBYTE, PULONG, ULONG) { return CR_NO_SUCH_VALUE; }
CONFIGRET CM_Get_DevNode_Status(PULONG, PULONG, DEVINST, ULONG) { return CR_NO_SUCH_DEVNODE; }
CONFIGRET CM_Open_DevNode_Key(DEVINST, REGSAM, ULONG, REGDISPOSITION, PHKEY, ULONG) { return CR_NO_SUCH_DEVNODE; }

HRESULT SHGetDesktopFolder(IShellFolder** ppshf) {
    if (ppshf) *ppshf = nullptr;
    return E_NOTIMPL;
}

HRESULT StrRetToBufW(STRRET* pstr, const ITEMIDLIST*, LPWSTR pszBuf, UINT cchBuf) {
    if (!pstr || !pszBuf || cchBuf == 0) return E_INVALIDARG;
    pszBuf[0] = L'\0';
    return E_NOTIMPL;
}

} // extern "C"
