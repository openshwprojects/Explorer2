#pragma once
// GdsiiStream.h — Calma GDSII Binary IC Layout Stream (.gds / .gds2) Parser

namespace Formats {

inline bool ParseGdsiiStreamBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (size < 16) return false;
    auto rdU16BE = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>((static_cast<uint16_t>(data[off]) << 8) | data[off + 1]);
    };
    auto rdI32BE = [&](size_t off) -> int32_t {
        uint32_t u = (static_cast<uint32_t>(data[off]) << 24) |
                     (static_cast<uint32_t>(data[off + 1]) << 16) |
                     (static_cast<uint32_t>(data[off + 2]) << 8) |
                     static_cast<uint32_t>(data[off + 3]);
        return static_cast<int32_t>(u);
    };

    // GDSII starts with HEADER record: length >= 6, recordType = 0x00, dataType = 0x02
    uint16_t firstLen = rdU16BE(0);
    if (firstLen < 4 || data[2] != 0x00 || data[3] != 0x02) return false;

    out.kind = EdaDocKind::GerberLayer;
    out.formatTitle = L"GDSII IC Photomask Stream (.gds)";

    int curLayer = 1;
    size_t pos = 0;
    while (pos + 4 <= size && out.traces.size() < 45000) {
        uint16_t recLen = rdU16BE(pos);
        if (recLen < 4 || pos + recLen > size) break;
        uint8_t recType  = data[pos + 2];
        uint8_t dataType = data[pos + 3];

        if (recType == 0x0D && recLen >= 6) { // LAYER
            curLayer = static_cast<int>(rdU16BE(pos + 4));
        } else if (recType == 0x06 && dataType == 0x06 && recLen > 4) { // STRNAME
            std::string sname(reinterpret_cast<const char*>(data + pos + 4), recLen - 4);
            while (!sname.empty() && sname.back() == '\0') sname.pop_back();
            out.labels.push_back({ 0.0f, 0.0f, 1.2f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(sname) });
        } else if (recType == 0x10 && dataType == 0x03 && recLen >= 12) { // XY (4-byte signed integer pairs)
            size_t numPairs = (recLen - 4) / 8;
            EdaLayerId lid = (curLayer % 3 == 0) ? EdaLayerId::EdgeCuts
                           : ((curLayer % 2 == 0) ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper);
            for (size_t i = 0; i + 1 < numPairs; ++i) {
                float x0 = rdI32BE(pos + 4 + i * 8) * 0.01f;
                float y0 = rdI32BE(pos + 8 + i * 8) * 0.01f;
                float x1 = rdI32BE(pos + 4 + (i + 1) * 8) * 0.01f;
                float y1 = rdI32BE(pos + 8 + (i + 1) * 8) * 0.01f;
                out.traces.push_back({ x0, y0, x1, y1, 0.25f, lid, curLayer });
            }
        }
        pos += recLen;
    }
    return !out.traces.empty();
}

} // namespace Formats
