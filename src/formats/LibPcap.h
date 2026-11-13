#pragma once
// LibPcap.h — Wireshark / tcpdump libpcap (.pcap) Network Packet Capture Inspector

namespace Formats {

inline bool ParseLibPcapDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 24) return false;
    uint32_t magic = 0;
    std::memcpy(&magic, raw.data(), 4);
    bool le = (magic == 0xA1B2C3D4u || magic == 0xA1B23C4Du);
    bool be = (magic == 0xD4C3B2A1u || magic == 0x4D3CB2A1u);
    if (!le && !be) return false;

    auto rdU32 = [&](size_t off) -> uint32_t {
        uint32_t v = 0; std::memcpy(&v, raw.data() + off, 4);
        return be ? _byteswap_ulong(v) : v;
    };
    auto rdU16 = [&](size_t off) -> uint16_t {
        uint16_t v = 0; std::memcpy(&v, raw.data() + off, 2);
        return be ? _byteswap_ushort(v) : v;
    };

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"libpcap Network Packet Capture (.pcap)";

    SpreadsheetSheet sheet;
    sheet.name = L"PCAP_Packets";
    sheet.rows.push_back({ L"Packet #", L"Timestamp (s.us)", L"Captured Len", L"Wire Len", L"Protocol / Frame Info" });

    size_t pos = 24;
    int pktIdx = 1;
    while (pos + 16 <= raw.size() && pktIdx <= 5000) {
        uint32_t tsSec  = rdU32(pos + 0);
        uint32_t tsUsec = rdU32(pos + 4);
        uint32_t inclLen = rdU32(pos + 8);
        uint32_t origLen = rdU32(pos + 12);
        pos += 16;
        if (pos + inclLen > raw.size()) break;

        std::wstring proto = L"RAW Frame";
        if (inclLen >= 14) {
            uint16_t ethType = static_cast<uint16_t>((static_cast<uint16_t>(raw[pos + 12]) << 8) | raw[pos + 13]);
            if (ethType == 0x0800 && inclLen >= 34) {
                uint8_t ipProto = raw[pos + 23];
                wchar_t ipBuf[128] = {};
                swprintf_s(ipBuf, L"IPv4 (%u.%u.%u.%u -> %u.%u.%u.%u) Proto=%u",
                    raw[pos + 26], raw[pos + 27], raw[pos + 28], raw[pos + 29],
                    raw[pos + 30], raw[pos + 31], raw[pos + 32], raw[pos + 33], ipProto);
                proto = ipBuf;
            } else if (ethType == 0x0806) {
                proto = L"ARP Frame";
            } else if (ethType == 0x86DD) {
                proto = L"IPv6 Packet";
            }
        }
        (void)rdU16;
        wchar_t tsBuf[64] = {};
        swprintf_s(tsBuf, L"%u.%06u", tsSec, tsUsec);
        sheet.rows.push_back({
            std::to_wstring(pktIdx++),
            tsBuf,
            std::to_wstring(inclLen) + L" B",
            std::to_wstring(origLen) + L" B",
            proto
        });
        pos += inclLen;
    }

    sheet.maxCols = 5;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
