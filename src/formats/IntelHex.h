#pragma once
// IntelHex.h — Intel HEX Firmware Memory Map (.hex / .ihex) Inspector

namespace Formats {

inline bool ParseIntelHexBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.empty() || sv.find(':') == std::string_view::npos) return false;
    out.kind = EdaDocKind::GerberLayer;
    out.formatTitle = L"Intel HEX Firmware Image (.hex)";

    auto hexByte = [](std::string_view s, size_t off) -> uint32_t {
        if (off + 2 > s.size()) return 0;
        char b[3] = { s[off], s[off + 1], 0 };
        return static_cast<uint32_t>(std::strtoul(b, nullptr, 16));
    };

    uint32_t extHigh = 0;
    size_t recordCount = 0;
    size_t totalBytes = 0;

    size_t pos = 0;
    while (pos < sv.size() && recordCount < 4096) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.size() < 11 || ln[0] != ':') continue;

        uint32_t byteLen = hexByte(ln, 1);
        uint32_t addr16 = (hexByte(ln, 3) << 8) | hexByte(ln, 5);
        uint32_t recType = hexByte(ln, 7);
        if (recType == 0x04 && byteLen == 2 && ln.size() >= 13) {
            extHigh = ((hexByte(ln, 9) << 8) | hexByte(ln, 11)) << 16;
        } else if (recType == 0x00 && byteLen > 0) {
            uint32_t fullAddr = extHigh | addr16;
            float rowY = -static_cast<float>((recordCount / 4) * 3.0f);
            float colX = static_cast<float>((recordCount % 4) * 18.0f);
            out.traces.push_back({ colX, rowY, colX + 14.0f, rowY, 1.2f, EdaLayerId::TopCopper, 0 });
            if (recordCount < 32) {
                wchar_t addrBuf[64] = {};
                swprintf_s(addrBuf, L"0x%08X (%uB)", fullAddr, byteLen);
                out.labels.push_back({ colX + 7.0f, rowY + 1.2f, 0.9f, EdaLayerId::TopSilk, addrBuf });
            }
            totalBytes += byteLen;
            ++recordCount;
        }
    }
    out.netCount = static_cast<int>(totalBytes);
    return !out.traces.empty();
}

} // namespace Formats
