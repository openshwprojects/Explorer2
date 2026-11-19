#pragma once
// LibPcap.h — Wireshark / tcpdump Classic libpcap (.pcap) Network Packet Capture Inspector

namespace Formats {

inline bool ParseLibPcapDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 24) return false;

    const uint32_t magicLE = DetailSys100::RdU32LE(raw.data());

    // Branch B: Classic libpcap (.pcap)
    const bool pcapLE = (magicLE == 0xA1B2C3D4u || magicLE == 0xA1B23C4Du);
    const bool pcapBE = (magicLE == 0xD4C3B2A1u || magicLE == 0x4D3CB2A1u);
    if (!pcapLE && !pcapBE) return false;

    auto rd32 = [&](size_t o) -> uint32_t { return pcapBE ? DetailSys100::RdU32BE(raw.data() + o) : DetailSys100::RdU32LE(raw.data() + o); };
    const uint16_t linkType = static_cast<uint16_t>(rd32(20) & 0xFFFFu);

    SpreadsheetSheet pktSh;
    pktSh.name = L"Packets";
    pktSh.maxCols = 8;
    pktSh.rows.push_back({ L"No.", L"Timestamp (s)", L"Interface", L"Protocol", L"Source", L"Destination", L"Length", L"Info" });

    size_t pos = 24;
    int pktNo = 1;
    while (pos + 16 <= raw.size() && pktNo <= 5000) {
        const uint32_t tsSec   = rd32(pos + 0);
        const uint32_t tsSub   = rd32(pos + 4);
        const uint32_t inclLen = rd32(pos + 8);
        const uint32_t origLen = rd32(pos + 12);
        pos += 16;
        if (pos + inclLen > raw.size()) break;

        wchar_t tsBuf[48] = {};
        swprintf_s(tsBuf, L"%u.%06u", tsSec, tsSub);
        std::wstring proto, src, dst, info;
        DetailPcap::DissectPacketL2L4(linkType, raw.data() + pos, inclLen, proto, src, dst, info);
        pktSh.rows.push_back({
            std::to_wstring(pktNo++), tsBuf, L"pcap0", proto, src, dst, std::to_wstring(origLen), info
        });
        pos += inclLen;
    }

    if (pktSh.rows.size() <= 1) return false;
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"libpcap Network Packet Capture (.pcap)";
    out.sheets.push_back(std::move(pktSh));
    out.valid = true;
    return true;
}


} // namespace Formats
