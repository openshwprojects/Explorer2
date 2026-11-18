#pragma once
// linux/win32_hal/include/iphlpapi.h - IP Helper API HAL (Adapters, Interface Stats, ARP Neighbours) for Linux.

#include <winsock2.h>
#include <ws2tcpip.h>

using IPAddr = uint32_t;
using NET_IFINDEX = ULONG;
using IFTYPE = ULONG;

#define MAX_ADAPTER_ADDRESS_LENGTH 8
#define IF_MAX_STRING_SIZE 256
#define IF_MAX_PHYS_ADDRESS_LENGTH 32

#define IF_TYPE_OTHER             1u
#define IF_TYPE_ETHERNET_CSMACD   6u
#define IF_TYPE_SOFTWARE_LOOPBACK 24u
#define IF_TYPE_IEEE80211         71u
#define IF_TYPE_TUNNEL            131u

#define GAA_FLAG_SKIP_UNICAST     0x0001u
#define GAA_FLAG_SKIP_ANYCAST     0x0002u
#define GAA_FLAG_SKIP_MULTICAST   0x0004u
#define GAA_FLAG_SKIP_DNS_SERVER  0x0008u
#define GAA_FLAG_INCLUDE_PREFIX   0x0010u
#define GAA_FLAG_INCLUDE_GATEWAYS 0x0080u

enum IF_OPER_STATUS {
    IfOperStatusUp = 1,
    IfOperStatusDown = 2,
    IfOperStatusTesting = 3,
    IfOperStatusUnknown = 4,
    IfOperStatusDormant = 5,
    IfOperStatusNotPresent = 6,
    IfOperStatusLowerLayerDown = 7
};

struct IP_ADAPTER_UNICAST_ADDRESS_LH {
    ULONG Length;
    DWORD Flags;
    IP_ADAPTER_UNICAST_ADDRESS_LH* Next;
    SOCKET_ADDRESS Address;
    int PrefixOrigin;
    int SuffixOrigin;
    int DadState;
    ULONG ValidLifetime;
    ULONG PreferredLifetime;
    ULONG LeaseLifetime;
    UINT8 OnLinkPrefixLength;
};
using IP_ADAPTER_UNICAST_ADDRESS = IP_ADAPTER_UNICAST_ADDRESS_LH;
using PIP_ADAPTER_UNICAST_ADDRESS = IP_ADAPTER_UNICAST_ADDRESS*;

struct IP_ADAPTER_DNS_SERVER_ADDRESS_XP {
    ULONG Length;
    DWORD Reserved;
    IP_ADAPTER_DNS_SERVER_ADDRESS_XP* Next;
    SOCKET_ADDRESS Address;
};
using IP_ADAPTER_DNS_SERVER_ADDRESS = IP_ADAPTER_DNS_SERVER_ADDRESS_XP;
using PIP_ADAPTER_DNS_SERVER_ADDRESS = IP_ADAPTER_DNS_SERVER_ADDRESS*;

struct IP_ADAPTER_GATEWAY_ADDRESS_LH {
    ULONG Length;
    DWORD Reserved;
    IP_ADAPTER_GATEWAY_ADDRESS_LH* Next;
    SOCKET_ADDRESS Address;
};
using IP_ADAPTER_GATEWAY_ADDRESS = IP_ADAPTER_GATEWAY_ADDRESS_LH;
using PIP_ADAPTER_GATEWAY_ADDRESS = IP_ADAPTER_GATEWAY_ADDRESS*;

struct IP_ADAPTER_ADDRESSES_LH {
    ULONG Length;
    NET_IFINDEX IfIndex;
    IP_ADAPTER_ADDRESSES_LH* Next;
    PCHAR AdapterName;
    PIP_ADAPTER_UNICAST_ADDRESS FirstUnicastAddress;
    void* FirstAnycastAddress;
    void* FirstMulticastAddress;
    PIP_ADAPTER_DNS_SERVER_ADDRESS FirstDnsServerAddress;
    PWCHAR DnsSuffix;
    PWCHAR Description;
    PWCHAR FriendlyName;
    BYTE PhysicalAddress[MAX_ADAPTER_ADDRESS_LENGTH];
    ULONG PhysicalAddressLength;
    DWORD Flags;
    ULONG Mtu;
    IFTYPE IfType;
    IF_OPER_STATUS OperStatus;
    NET_IFINDEX Ipv6IfIndex;
    DWORD ZoneIndices[16];
    void* FirstPrefix;
    ULONG64 TransmitLinkSpeed;
    ULONG64 ReceiveLinkSpeed;
    void* FirstWinsServerAddress;
    PIP_ADAPTER_GATEWAY_ADDRESS FirstGatewayAddress;
};
using IP_ADAPTER_ADDRESSES = IP_ADAPTER_ADDRESSES_LH;
using PIP_ADAPTER_ADDRESSES = IP_ADAPTER_ADDRESSES*;

struct MIB_IF_ROW2 {
    ULONG64 InterfaceLuid;
    NET_IFINDEX InterfaceIndex;
    GUID InterfaceGuid;
    WCHAR Alias[IF_MAX_STRING_SIZE + 1];
    WCHAR Description[IF_MAX_STRING_SIZE + 1];
    ULONG PhysicalAddressLength;
    UCHAR PhysicalAddress[IF_MAX_PHYS_ADDRESS_LENGTH];
    UCHAR PermanentPhysicalAddress[IF_MAX_PHYS_ADDRESS_LENGTH];
    ULONG Mtu;
    IFTYPE Type;
    int TunnelType;
    int MediaType;
    int PhysicalMediumType;
    int AccessType;
    int DirectionType;
    struct {
        BOOLEAN HardwareInterface : 1;
        BOOLEAN FilterInterface : 1;
        BOOLEAN ConnectorPresent : 1;
        BOOLEAN NotAuthenticated : 1;
        BOOLEAN NotMediaConnected : 1;
        BOOLEAN Paused : 1;
        BOOLEAN LowPower : 1;
        BOOLEAN EndPointInterface : 1;
    } InterfaceAndOperStatusFlags;
    IF_OPER_STATUS OperStatus;
    int AdminStatus;
    int MediaConnectState;
    GUID NetworkGuid;
    int ConnectionType;
    ULONG64 TransmitLinkSpeed;
    ULONG64 ReceiveLinkSpeed;
    ULONG64 InOctets;
    ULONG64 InUcastPkts;
    ULONG64 InNUcastPkts;
    ULONG64 InDiscards;
    ULONG64 InErrors;
    ULONG64 InUnknownProtos;
    ULONG64 InUcastOctets;
    ULONG64 InMulticastOctets;
    ULONG64 InBroadcastOctets;
    ULONG64 OutOctets;
    ULONG64 OutUcastPkts;
    ULONG64 OutNUcastPkts;
    ULONG64 OutDiscards;
    ULONG64 OutErrors;
    ULONG64 OutUcastOctets;
    ULONG64 OutMulticastOctets;
    ULONG64 OutBroadcastOctets;
    ULONG64 OutQLen;
};

struct MIB_IF_TABLE2 {
    ULONG NumEntries;
    MIB_IF_ROW2 Table[1];
};
using PMIB_IF_TABLE2 = MIB_IF_TABLE2*;

union SOCKADDR_INET {
    SOCKADDR_IN Ipv4;
    SOCKADDR_IN6 Ipv6;
    USHORT si_family;
};

enum NL_NEIGHBOR_STATE {
    NlnsUnreachable = 0,
    NlnsIncomplete = 1,
    NlnsProbe = 2,
    NlnsDelay = 3,
    NlnsStale = 4,
    NlnsReachable = 5,
    NlnsPermanent = 6
};

struct MIB_IPNET_ROW2 {
    SOCKADDR_INET Address;
    NET_IFINDEX InterfaceIndex;
    ULONG64 InterfaceLuid;
    UCHAR PhysicalAddress[IF_MAX_PHYS_ADDRESS_LENGTH];
    ULONG PhysicalAddressLength;
    NL_NEIGHBOR_STATE State;
    UCHAR Flags;
    ULONG ReachabilityTime;
};

struct MIB_IPNET_TABLE2 {
    ULONG NumEntries;
    MIB_IPNET_ROW2 Table[1];
};
using PMIB_IPNET_TABLE2 = MIB_IPNET_TABLE2*;

extern "C" {
ULONG GetAdaptersAddresses(ULONG Family, ULONG Flags, PVOID Reserved, PIP_ADAPTER_ADDRESSES AdapterAddresses, PULONG SizePointer);
DWORD GetIfTable2(PMIB_IF_TABLE2* Table);
DWORD GetIpNetTable2(USHORT Family, PMIB_IPNET_TABLE2* Table);
VOID FreeMibTable(PVOID Memory);
DWORD SendARP(IPAddr DestIP, IPAddr SrcIP, PVOID pMacAddr, PULONG PhyAddrLen);
}
