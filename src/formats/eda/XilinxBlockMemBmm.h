#pragma once
// XilinxBlockMemBmm.h — Xilinx ISE / Vivado data2mem FPGA Block RAM Memory Map (.bmm) reader
// Parses ADDRESS_MAP, ADDRESS_SPACE [0xStart:0xEnd], and BUS_BLOCK BRAM bit-lane placements
// (<inst> [msb:lsb] PLACED = X<col>Y<row>;), rendering a physical FPGA BRAM floorplan + bus routing.

namespace Formats {

inline bool ParseXilinxBlockMemBmm(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    if (!data || size < 32 || size > 16u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    // Strip // and /* ... */ comments and normalize ';' into standalone tokens
    std::string clean;
    clean.reserve(sv.size() + 64u);
    for (size_t i = 0; i < sv.size(); ) {
        if (i + 1u < sv.size() && sv[i] == '/' && sv[i + 1u] == '/') {
            i += 2u;
            while (i < sv.size() && sv[i] != '\n') ++i;
            continue;
        }
        if (i + 1u < sv.size() && sv[i] == '/' && sv[i + 1u] == '*') {
            i += 2u;
            bool closed = false;
            while (i + 1u < sv.size()) {
                if (sv[i] == '*' && sv[i + 1u] == '/') { i += 2u; closed = true; break; }
                ++i;
            }
            if (!closed) return false;
            clean.push_back(' ');
            continue;
        }
        if (sv[i] == ';') {
            clean += " ; ";
            ++i;
        } else {
            const char ch = sv[i++];
            clean.push_back((ch == '\r' || ch == '\n') ? ' ' : ch);
        }
    }

    std::vector<std::string_view> toks;
    {
        std::string_view rem(clean), t;
        while (ReaderText::NextTokenSv(rem, t)) {
            toks.push_back(t);
            if (toks.size() > 1000000u) return false;
        }
    }

    auto parseHexOrDec64 = [](std::string_view s, uint64_t& out) -> bool {
        s = ReaderText::TrimSv(s);
        if (s.empty()) return false;
        int base = 10;
        if (s.size() > 2u && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s.remove_prefix(2u);
        }
        if (s.empty()) return false;
        uint64_t val = 0;
        for (char c : s) {
            if (c == '_') continue;
            int d = -1;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else return false;
            val = val * static_cast<uint64_t>(base) + static_cast<uint64_t>(d);
        }
        out = val;
        return true;
    };

    auto parseBracketPair = [&](std::string_view s, uint64_t& a, uint64_t& b) -> bool {
        if (s.size() < 5u || s.front() != '[' || s.back() != ']') return false;
        const size_t col = s.find(':');
        if (col == std::string_view::npos) return false;
        return parseHexOrDec64(s.substr(1u, col - 1u), a) &&
               parseHexOrDec64(s.substr(col + 1u, s.size() - col - 2u), b);
    };

    struct BramTile {
        std::string instPath;
        int msb = 0;
        int lsb = 0;
        bool hasPlaced = false;
        int gridX = 0;
        int gridY = 0;
    };
    struct AddrSpace {
        std::string name;
        std::string ramKind;
        uint64_t addrStart = 0;
        uint64_t addrEnd = 0;
        std::vector<BramTile> tiles;
    };

    std::vector<AddrSpace> spaces;
    int mapDepth = 0;
    bool inSpace = false;
    bool inBusBlock = false;
    AddrSpace curSpace;

    for (size_t i = 0; i < toks.size(); ) {
        const std::string_view kw = toks[i];
        if (ReaderText::AsciiEqCI(kw, "ADDRESS_MAP")) {
            if (inSpace || i + 3u >= toks.size()) return false;
            ++mapDepth;
            i += 4u;
        } else if (ReaderText::AsciiEqCI(kw, "END_ADDRESS_MAP")) {
            if (inSpace || mapDepth <= 0) return false;
            --mapDepth;
            ++i;
            if (i < toks.size() && toks[i] == ";") ++i;
        } else if (ReaderText::AsciiEqCI(kw, "ADDRESS_SPACE")) {
            if (inSpace || i + 3u >= toks.size()) return false;
            curSpace = AddrSpace{};
            curSpace.name    = std::string(toks[i + 1u]);
            curSpace.ramKind = std::string(toks[i + 2u]);
            if (!parseBracketPair(toks[i + 3u], curSpace.addrStart, curSpace.addrEnd) ||
                curSpace.addrEnd < curSpace.addrStart) {
                return false;
            }
            inSpace = true;
            i += 4u;
        } else if (ReaderText::AsciiEqCI(kw, "END_ADDRESS_SPACE")) {
            if (!inSpace || inBusBlock) return false;
            inSpace = false;
            spaces.push_back(std::move(curSpace));
            ++i;
            if (i < toks.size() && toks[i] == ";") ++i;
        } else if (ReaderText::AsciiEqCI(kw, "BUS_BLOCK")) {
            if (!inSpace || inBusBlock) return false;
            inBusBlock = true;
            ++i;
        } else if (ReaderText::AsciiEqCI(kw, "END_BUS_BLOCK")) {
            if (!inBusBlock) return false;
            inBusBlock = false;
            ++i;
            if (i < toks.size() && toks[i] == ";") ++i;
        } else if (inBusBlock) {
            // <instPath> [<msb>:<lsb>] [PLACED = X<c>Y<r>] ... ;
            if (i + 2u >= toks.size()) return false;
            BramTile bt;
            bt.instPath = std::string(toks[i++]);
            uint64_t hi = 0, lo = 0;
            if (!parseBracketPair(toks[i++], hi, lo) || hi < lo || hi > 1023ULL) return false;
            bt.msb = static_cast<int>(hi);
            bt.lsb = static_cast<int>(lo);

            while (i < toks.size() && toks[i] != ";") {
                if (ReaderText::AsciiEqCI(toks[i], "PLACED")) {
                    ++i;
                    if (i < toks.size() && toks[i] == "=") ++i;
                    if (i >= toks.size()) return false;
                    std::string_view xy = toks[i++];
                    if (xy.size() >= 4u && (xy[0] == 'X' || xy[0] == 'x')) {
                        const size_t yp = xy.find_first_of("Yy", 1u);
                        int gx = 0, gy = 0;
                        if (yp != std::string_view::npos &&
                            ReaderText::ParseIntSv(xy.substr(1u, yp - 1u), gx) &&
                            ReaderText::ParseIntSv(xy.substr(yp + 1u), gy))
                        {
                            bt.hasPlaced = true;
                            bt.gridX = gx;
                            bt.gridY = gy;
                        } else {
                            return false;
                        }
                    } else {
                        return false;
                    }
                } else {
                    ++i;
                }
            }
            if (i >= toks.size() || toks[i] != ";") return false;
            ++i; // ';'
            curSpace.tiles.push_back(std::move(bt));
        } else {
            ++i;
        }
    }

    if (inSpace || inBusBlock || mapDepth != 0 || spaces.empty()) return false;

    size_t totalTiles = 0;
    for (const auto& sp : spaces) totalTiles += sp.tiles.size();
    if (totalTiles == 0u) return false;

    outBoard.kind = EdaDocKind::KiCadPcb;
    outBoard.formatTitle = L"Xilinx FPGA Block Memory Map (.bmm)";
    outBoard.netCount = totalTiles;

    for (size_t si = 0; si < spaces.size(); ++si) {
        const auto& sp = spaces[si];
        const float busX = -14.0f;
        const float baseY = static_cast<float>(si) * -36.0f;

        wchar_t spHdr[160] = {};
        swprintf_s(spHdr, L"%S (%S) [0x%llX..0x%llX]",
                   sp.name.c_str(), sp.ramKind.c_str(),
                   static_cast<unsigned long long>(sp.addrStart),
                   static_cast<unsigned long long>(sp.addrEnd));
        outBoard.labels.push_back({ busX, baseY + 8.0f, 1.15f, EdaLayerId::TopSilk, spHdr });

        // Memory bus spine bar
        outBoard.traces.push_back({ busX, baseY + 5.0f, busX, baseY - 22.0f, 0.55f, EdaLayerId::InnerCopper, static_cast<int>(si + 1) });

        for (size_t ti = 0; ti < sp.tiles.size(); ++ti) {
            const auto& bt = sp.tiles[ti];
            const float tx = bt.hasPlaced ? (static_cast<float>(bt.gridX) * 16.0f + 8.0f)
                                          : (static_cast<float>(ti % 4u) * 18.0f + 8.0f);
            const float ty = bt.hasPlaced ? (baseY - static_cast<float>(bt.gridY) * 11.0f)
                                          : (baseY - static_cast<float>(ti / 4u) * 11.0f);

            DrawEdaBoxRect(outBoard, tx - 6.0f, ty - 3.8f, tx + 6.0f, ty + 3.8f, 0.25f, EdaLayerId::TopSilk, static_cast<int>(ti + 1));
            outBoard.pads.push_back({ tx - 5.2f, ty, 1.2f, 1.2f, 0.0f, 0.0f, 1u, EdaLayerId::TopCopper, L"DI", L"DATA" });
            outBoard.traces.push_back({ busX, ty, tx - 5.2f, ty, 0.28f, EdaLayerId::TopCopper, static_cast<int>(ti + 1) });

            wchar_t sliceLbl[96] = {};
            swprintf_s(sliceLbl, L"[%d:%d]", bt.msb, bt.lsb);
            outBoard.labels.push_back({ tx - 3.5f, ty + 0.5f, 0.9f, EdaLayerId::TopSilk, sliceLbl });

            EdaComponent3D comp;
            wchar_t refBuf[48] = {};
            if (bt.hasPlaced) swprintf_s(refBuf, L"RAMB_X%dY%d", bt.gridX, bt.gridY);
            else              swprintf_s(refBuf, L"RAMB_%zu_%zu", si, ti);
            comp.refDes = refBuf;
            comp.value  = PathUtils::Utf8ToWide(bt.instPath) + L" " + sliceLbl;
            comp.footprint = PathUtils::Utf8ToWide(sp.ramKind);
            comp.x = tx;
            comp.y = ty;
            comp.widthMm  = 12.0f;
            comp.lengthMm = 7.6f;
            comp.heightMm = 1.2f;
            comp.category = EdaPartCategory::IC_Package;
            comp.pinCount = (bt.msb - bt.lsb + 1);
            outBoard.components.push_back(std::move(comp));
        }
    }

    return !outBoard.components.empty();
}

} // namespace Formats
