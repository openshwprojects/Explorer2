#pragma once
// PcapNgCapture.h — Wireshark PCAP-NG Next Generation Capture (.pcapng) Inspector

namespace Formats {

inline bool ParsePcapNgCapture(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 24) return false;

    const uint32_t magicLE = DetailBinarySysDoc::RdU32LE(raw.data());

    // Branch A: PCAP-NG (Section Header Block 0x0A0D0D0A)
    if (magicLE == 0x0A0D0D0Au) {
        if (raw.size() < 28) return false;
        const uint32_t bom = DetailBinarySysDoc::RdU32LE(raw.data() + 8);
        if (bom != 0x1A2B3C4Du && bom != 0x4D3C2B1Au) return false;
        const bool isBE = (bom == 0x4D3C2B1Au);

        auto rd16 = [&](size_t o) -> uint16_t { return isBE ? DetailBinarySysDoc::RdU16BE(raw.data() + o) : DetailBinarySysDoc::RdU16LE(raw.data() + o); };
        auto rd32 = [&](size_t o) -> uint32_t { return isBE ? DetailBinarySysDoc::RdU32BE(raw.data() + o) : DetailBinarySysDoc::RdU32LE(raw.data() + o); };

        struct IfaceDesc { uint16_t linkType = 1; std::wstring name; double tsScaleSec = 1e-6; };
        std::vector<IfaceDesc> ifaces;
        std::wstring shbHardware, shbOs, shbApp;

        SpreadsheetSheet pktSh;
        pktSh.name = L"Packets";
        pktSh.maxCols = 8;
        pktSh.rows.push_back({ L"No.", L"Timestamp (s)", L"Interface", L"Protocol", L"Source", L"Destination", L"Length", L"Info" });

        SpreadsheetSheet sumSh;
        sumSh.name = L"Capture_Summary";
        sumSh.maxCols = 2;
        sumSh.rows.push_back({ L"Capture Property", L"Value" });

        size_t pos = 0;
        int pktNo = 1;
        size_t blockCount = 0;

        while (pos + 12 <= raw.size() && blockCount < 100000) {
            const uint32_t btype = rd32(pos + 0);
            const uint32_t blen  = rd32(pos + 4);
            if (blen < 12 || pos + blen > raw.size()) break;
            ++blockCount;

            auto walkOptions = [&](size_t optStart, size_t optEnd, const auto& onOpt) {
                size_t op = optStart;
                while (op + 4 <= optEnd) {
                    const uint16_t oc = rd16(op + 0);
                    const uint16_t ol = rd16(op + 2);
                    op += 4;
                    if (oc == 0) break;
                    if (op + ol > optEnd) break;
                    onOpt(oc, raw.data() + op, ol);
                    op += (static_cast<size_t>(ol) + 3u) & ~size_t(3u);
                }
            };

            if (btype == 0x0A0D0D0Au && blen >= 28) { // SHB
                ifaces.clear();
                walkOptions(pos + 24, pos + blen - 4, [&](uint16_t oc, const uint8_t* od, uint16_t ol) {
                    std::wstring val = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(od), ol));
                    if (oc == 2) shbHardware = val;
                    else if (oc == 3) shbOs = val;
                    else if (oc == 4) shbApp = val;
                });
            } else if (btype == 0x00000001u && blen >= 20) { // IDB
                IfaceDesc idb;
                idb.linkType = rd16(pos + 8);
                idb.name = L"eth" + std::to_wstring(ifaces.size());
                walkOptions(pos + 16, pos + blen - 4, [&](uint16_t oc, const uint8_t* od, uint16_t ol) {
                    if (oc == 2 && ol > 0) {
                        idb.name = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(od), strnlen(reinterpret_cast<const char*>(od), ol)));
                    } else if (oc == 9 && ol >= 1) {
                        const uint8_t r = od[0];
                        if ((r & 0x80u) == 0 && r <= 12) idb.tsScaleSec = std::pow(10.0, -static_cast<int>(r));
                        else if ((r & 0x80u) != 0 && (r & 0x7Fu) <= 32) idb.tsScaleSec = std::pow(2.0, -static_cast<int>(r & 0x7Fu));
                    }
                });
                sumSh.rows.push_back({ L"Interface #" + std::to_wstring(ifaces.size()), idb.name + L" (LinkType " + std::to_wstring(idb.linkType) + L")" });
                ifaces.push_back(std::move(idb));
            } else if (btype == 0x00000006u && blen >= 32 && pktSh.rows.size() <= 5000) { // EPB
                const uint32_t ifId    = rd32(pos + 8);
                const uint32_t tsHigh  = rd32(pos + 12);
                const uint32_t tsLow   = rd32(pos + 16);
                const uint32_t capLen  = rd32(pos + 20);
                const uint32_t origLen = rd32(pos + 24);
                if (28ULL + capLen + 4ULL <= blen) {
                    const uint16_t lt = (ifId < ifaces.size()) ? ifaces[ifId].linkType : 1;
                    const std::wstring ifName = (ifId < ifaces.size()) ? ifaces[ifId].name : (L"if" + std::to_wstring(ifId));
                    const double scale = (ifId < ifaces.size()) ? ifaces[ifId].tsScaleSec : 1e-6;
                    const uint64_t tsRaw = (static_cast<uint64_t>(tsHigh) << 32) | tsLow;
                    const double tsSec = static_cast<double>(tsRaw) * scale;
                    wchar_t tsBuf[64] = {};
                    if (!std::isfinite(tsSec) || std::fabs(tsSec) >= 1e12) swprintf_s(tsBuf, L"%.6g", tsSec);
                    else swprintf_s(tsBuf, L"%.6f", tsSec);

                    std::wstring proto, src, dst, info;
                    DetailPcap::DissectPacketL2L4(lt, raw.data() + pos + 28, capLen, proto, src, dst, info);
                    pktSh.rows.push_back({
                        std::to_wstring(pktNo++), tsBuf, ifName, proto, src, dst, std::to_wstring(origLen), info
                    });
                }
            } else if (btype == 0x00000003u && blen >= 16 && pktSh.rows.size() <= 5000) { // SPB
                const uint32_t origLen = rd32(pos + 8);
                const uint32_t capLen  = std::min<uint32_t>(origLen, blen - 16);
                const uint16_t lt = !ifaces.empty() ? ifaces[0].linkType : 1;
                const std::wstring ifName = !ifaces.empty() ? ifaces[0].name : L"if0";
                std::wstring proto, src, dst, info;
                DetailPcap::DissectPacketL2L4(lt, raw.data() + pos + 12, capLen, proto, src, dst, info);
                pktSh.rows.push_back({
                    std::to_wstring(pktNo++), L"0.000000", ifName, proto, src, dst, std::to_wstring(origLen), info
                });
            }

            pos += (static_cast<size_t>(blen) + 3u) & ~size_t(3u);
        }

        if (blockCount == 0) return false;
        if (!shbHardware.empty()) sumSh.rows.push_back({ L"Capture Hardware", shbHardware });
        if (!shbOs.empty())       sumSh.rows.push_back({ L"Capture OS", shbOs });
        if (!shbApp.empty())      sumSh.rows.push_back({ L"Capture Application", shbApp });
        sumSh.rows.push_back({ L"Total Blocks / Packets", std::to_wstring(blockCount) + L" / " + std::to_wstring(pktSh.rows.size() - 1) });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Wireshark PCAP-NG Capture (.pcapng)";
        if (pktSh.rows.size() > 1) out.sheets.push_back(std::move(pktSh));
        out.sheets.push_back(std::move(sumSh));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
