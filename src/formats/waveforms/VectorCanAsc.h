#pragma once
// VectorCanAsc.h — Vector CAN / CAN-FD ASCII Trace Log (.asc / .can) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseVectorCanAscBoard(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 8) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    // 3. Vector CAN / CAN-FD ASCII Log (.asc / .can)
    if (extLower == L".asc" || extLower == L".can") {
        bool baseDec = false;
        std::vector<DetailCan::CanFrame> frames;
        size_t pos = 0;
        while (pos < sv.size() && frames.size() < 20000) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;
            while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
            if (ln.empty() || ln[0] == ';' || ln.rfind("//", 0) == 0) continue;

            if (DetailEda100::ContainsAsciiI(ln, "base dec")) { baseDec = true; continue; }
            if (DetailEda100::ContainsAsciiI(ln, "base hex")) { baseDec = false; continue; }
            if (!((ln[0] >= '0' && ln[0] <= '9') || ln[0] == '.')) continue;

            std::vector<std::string_view> toks;
            size_t tp = 0;
            while (tp < ln.size() && toks.size() < 80) {
                while (tp < ln.size() && (ln[tp] == ' ' || ln[tp] == '\t')) ++tp;
                size_t te = ln.find_first_of(" \t", tp);
                if (te == std::string_view::npos) te = ln.size();
                if (te > tp) toks.push_back(ln.substr(tp, te - tp));
                tp = te;
            }
            if (toks.size() < 6) continue;

            char tbuf[32] = {};
            std::memcpy(tbuf, toks[0].data(), std::min<size_t>(toks[0].size(), 31));
            double ts = std::strtod(tbuf, nullptr);

            // CANFD line: <ts> CANFD <ch> Rx|Tx <id> [<sym>] <brs> <esi> <dlc> <dataLen> <b0> ...
            if (DetailEda100::ContainsAsciiI(toks[1], "CANFD") && toks.size() >= 10) {
                DetailCan::CanFrame f;
                f.timestamp = ts;
                f.isFd = true;
                f.channel = static_cast<uint16_t>(std::atoi(std::string(toks[2]).c_str()));
                f.isTx = DetailEda100::ContainsAsciiI(toks[3], "Tx");
                std::string idStr(toks[4]);
                if (!idStr.empty() && (idStr.back() == 'x' || idStr.back() == 'X')) {
                    f.extId = true;
                    idStr.pop_back();
                }
                f.canId = static_cast<uint32_t>(std::strtoul(idStr.c_str(), nullptr, baseDec ? 10 : 16)) & 0x1FFFFFFFu;
                // Find dataLen token (toks[9] when no symbolic name, or toks[10] with symbolic name)
                size_t dlIdx = 9;
                if (toks.size() > 10 && toks[5].size() > 1 && !isdigit(static_cast<unsigned char>(toks[5][0]))) dlIdx = 10;
                if (dlIdx < toks.size()) {
                    int dlen = std::clamp(std::atoi(std::string(toks[dlIdx]).c_str()), 0, 64);
                    f.dlcBytes = static_cast<uint8_t>(std::min<size_t>(dlen, toks.size() - dlIdx - 1));
                    for (uint8_t b = 0; b < f.dlcBytes; ++b) {
                        f.data[b] = static_cast<uint8_t>(std::strtoul(std::string(toks[dlIdx + 1 + b]).c_str(), nullptr, baseDec ? 10 : 16));
                    }
                    frames.push_back(f);
                }
                continue;
            }

            // Classic CAN frame: <ts> <ch> <id>[x] Rx|Tx d <dlc> <b0> ...
            if ((DetailEda100::ContainsAsciiI(toks[3], "Rx") || DetailEda100::ContainsAsciiI(toks[3], "Tx")) &&
                (toks[4] == "d" || toks[4] == "D"))
            {
                DetailCan::CanFrame f;
                f.timestamp = ts;
                f.channel = static_cast<uint16_t>(std::atoi(std::string(toks[1]).c_str()));
                std::string idStr(toks[2]);
                if (!idStr.empty() && (idStr.back() == 'x' || idStr.back() == 'X')) {
                    f.extId = true;
                    idStr.pop_back();
                }
                char* idEnd = nullptr;
                unsigned long parsedId = std::strtoul(idStr.c_str(), &idEnd, baseDec ? 10 : 16);
                if (idEnd == idStr.c_str()) continue;
                f.canId = static_cast<uint32_t>(parsedId) & 0x1FFFFFFFu;
                f.isTx = DetailEda100::ContainsAsciiI(toks[3], "Tx");
                int dlc = std::clamp(std::atoi(std::string(toks[5]).c_str()), 0, 64);
                f.dlcBytes = static_cast<uint8_t>(std::min<size_t>(dlc, toks.size() - 6));
                for (uint8_t b = 0; b < f.dlcBytes; ++b) {
                    f.data[b] = static_cast<uint8_t>(std::strtoul(std::string(toks[6 + b]).c_str(), nullptr, baseDec ? 10 : 16));
                }
                frames.push_back(f);
            }
        }
        return DetailCan::RenderCanFramesToBoard(frames, L"Vector CAN ASCII Trace (.asc)", out);
    }
    return false;
}

} // namespace Formats
