#pragma once
// MotorolaSrec.h — Motorola S-Record Firmware Image (.srec / .s19 / .mot) Inspector

namespace Formats {

inline bool ParseMotorolaSrecBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.size() < 10 || sv[0] != 'S') return false;
    out.kind = EdaDocKind::GerberLayer;
    out.formatTitle = L"Motorola S-Record Firmware (.srec)";

    auto hexByte = [](std::string_view s, size_t off) -> uint32_t {
        if (off + 2 > s.size()) return 0;
        char b[3] = { s[off], s[off + 1], 0 };
        return static_cast<uint32_t>(std::strtoul(b, nullptr, 16));
    };

    size_t recordCount = 0;
    size_t pos = 0;
    while (pos < sv.size() && recordCount < 4096) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.size() < 8 || ln[0] != 'S') continue;

        char stype = ln[1];
        uint32_t count = hexByte(ln, 2);
        if (stype == '1' || stype == '2' || stype == '3') {
            int addrBytes = (stype == '1') ? 2 : ((stype == '2') ? 3 : 4);
            uint32_t addr = 0;
            for (int i = 0; i < addrBytes; ++i) {
                addr = (addr << 8) | hexByte(ln, 4 + i * 2);
            }
            float rowY = -static_cast<float>((recordCount / 4) * 3.0f);
            float colX = static_cast<float>((recordCount % 4) * 18.0f);
            out.traces.push_back({ colX, rowY, colX + 14.0f, rowY, 1.2f, EdaLayerId::BottomCopper, 0 });
            if (recordCount < 32) {
                wchar_t addrBuf[64] = {};
                swprintf_s(addrBuf, L"S%c @0x%06X (%uB)", stype, addr, count);
                out.labels.push_back({ colX + 7.0f, rowY + 1.2f, 0.9f, EdaLayerId::TopSilk, addrBuf });
            }
            ++recordCount;
        }
    }
    return !out.traces.empty();
}

} // namespace Formats
