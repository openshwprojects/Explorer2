#pragma once
// LibPcap.h — Wireshark / tcpdump libpcap (.pcap) Network Packet Capture Inspector
// Delegates to ExtendedSysSec100.h's unified L2-L7 dissector (Ethernet/SLL/RawIP/Loopback + IPv4/IPv6/ARP + TCP/UDP/ICMP + DNS/HTTP/TLS).

namespace Formats {

bool ParseExtendedSysSecDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out);

inline bool ParseLibPcapDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    return ParseExtendedSysSecDoc(raw, L".pcap", out);
}

} // namespace Formats
