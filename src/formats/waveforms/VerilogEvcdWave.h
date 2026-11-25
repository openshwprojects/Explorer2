#pragma once
// VerilogEvcdWave.h — IEEE 1364 Extended Value Change Dump (.evcd)
// Parses $timescale, $scope/$upscope, $var port (<size> or [msb:lsb]), $enddefinitions,
// $dumpports, #<timestamp>, and directional port value changes:
//   p<port_state><0_strength><1_strength><id_code>
// Translates IEEE 1364 15-state directional port codes (D/U/N/Z/d/u input, L/H/X/T/l/h output,
// 0/1/?/F bidirectional) and annotates [IN] / [OUT] / [IO] direction per port lane.

namespace Formats {

namespace DetailVerilogEvcd {

struct EvcdPortVar {
    std::string idCode;
    std::string fullName;
    int bitWidth = 1;
    bool sawInDrive  = false;
    bool sawOutDrive = false;
    std::vector<std::pair<uint64_t, std::string>> changes;
};

inline bool MapEvcdStateChar(char evCh, char& outBit, bool& isIn, bool& isOut) {
    switch (evCh) {
    // Testbench input drive states
    case 'D': case 'd': outBit = '0'; isIn = true;  return true;
    case 'U': case 'u': outBit = '1'; isIn = true;  return true;
    case 'N':           outBit = 'x'; isIn = true;  return true;
    case 'Z':           outBit = 'z'; isIn = true;  return true;
    // DUT output drive states
    case 'L': case 'l': outBit = '0'; isOut = true; return true;
    case 'H': case 'h': outBit = '1'; isOut = true; return true;
    case 'X':           outBit = 'x'; isOut = true; return true;
    case 'T':           outBit = 'z'; isOut = true; return true;
    // Bidirectional / resolved / floating states
    case '0':           outBit = '0'; isIn = true; isOut = true; return true;
    case '1':           outBit = '1'; isIn = true; isOut = true; return true;
    case 'F': case 'f': outBit = 'z'; return true;
    case '?': case 'A': case 'B': case 'C': case 'a': case 'b': case 'c':
        outBit = 'x'; isIn = true; isOut = true; return true;
    default:
        return false;
    }
}

inline bool IsStrengthDigits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '7') return false;
    }
    return true;
}

inline bool ParseBracketRangeWidth(std::string_view tok, int& outWidth) {
    // Parses "[msb:lsb]" or "[idx]"
    if (tok.size() < 3 || tok.front() != '[' || tok.back() != ']') return false;
    std::string_view inner = tok.substr(1, tok.size() - 2);
    size_t colon = inner.find(':');
    if (colon == std::string_view::npos) {
        int idx = 0;
        if (!ReaderText::ParseIntSv(inner, idx) || idx < 0 || idx > 65535) return false;
        outWidth = 1;
        return true;
    }
    int msb = 0, lsb = 0;
    if (!ReaderText::ParseIntSv(inner.substr(0, colon), msb) ||
        !ReaderText::ParseIntSv(inner.substr(colon + 1), lsb)) {
        return false;
    }
    int64_t w = std::abs(static_cast<int64_t>(msb) - static_cast<int64_t>(lsb)) + 1;
    if (w < 1 || w > 1024) return false;
    outWidth = static_cast<int>(w);
    return true;
}

inline bool NextWsToken(std::string_view sv, size_t& pos, std::string_view& outTok) {
    while (pos < sv.size() && static_cast<unsigned char>(sv[pos]) <= 0x20u) ++pos;
    if (pos >= sv.size()) return false;
    size_t start = pos;
    while (pos < sv.size() && static_cast<unsigned char>(sv[pos]) > 0x20u) ++pos;
    outTok = sv.substr(start, pos - start);
    return true;
}

} // namespace DetailVerilogEvcd

inline bool ParseVerilogEvcdWave(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 24u, (64u << 20))) return false;

    std::vector<std::string> scopeStack;
    std::vector<DetailVerilogEvcd::EvcdPortVar> ports;
    std::unordered_map<std::string, size_t> idToPortIdx;
    std::string timescaleStr;

    bool inHeader = true;
    bool sawEndDefinitions = false;
    size_t pos = 0;
    uint64_t curTime = 0;
    uint64_t minTime = UINT64_MAX;
    uint64_t maxTime = 0;
    size_t totalPortChanges = 0;

    std::string_view tok;
    while (DetailVerilogEvcd::NextWsToken(sv, pos, tok)) {
        if (tok == "$comment") {
            bool closed = false;
            std::string_view sub;
            while (DetailVerilogEvcd::NextWsToken(sv, pos, sub)) {
                if (sub == "$end") { closed = true; break; }
            }
            if (!closed) return false;
            continue;
        }

        if (inHeader) {
            if (tok == "$date" || tok == "$version") {
                bool closed = false;
                std::string_view sub;
                while (DetailVerilogEvcd::NextWsToken(sv, pos, sub)) {
                    if (sub == "$end") { closed = true; break; }
                }
                if (!closed) return false;
            } else if (tok == "$timescale") {
                bool closed = false;
                std::string_view sub;
                while (DetailVerilogEvcd::NextWsToken(sv, pos, sub)) {
                    if (sub == "$end") { closed = true; break; }
                    if (!timescaleStr.empty()) timescaleStr.push_back(' ');
                    timescaleStr.append(sub.data(), sub.size());
                }
                if (!closed) return false;
            } else if (tok == "$scope") {
                std::string_view scType, scName, endTok;
                if (!DetailVerilogEvcd::NextWsToken(sv, pos, scType) ||
                    !DetailVerilogEvcd::NextWsToken(sv, pos, scName) ||
                    !DetailVerilogEvcd::NextWsToken(sv, pos, endTok) ||
                    endTok != "$end") {
                    return false;
                }
                scopeStack.emplace_back(scName);
            } else if (tok == "$upscope") {
                std::string_view endTok;
                if (!DetailVerilogEvcd::NextWsToken(sv, pos, endTok) || endTok != "$end") return false;
                if (!scopeStack.empty()) scopeStack.pop_back();
            } else if (tok == "$var") {
                std::string_view varType, sizeOrRange, idTok;
                if (!DetailVerilogEvcd::NextWsToken(sv, pos, varType) ||
                    !DetailVerilogEvcd::NextWsToken(sv, pos, sizeOrRange) ||
                    !DetailVerilogEvcd::NextWsToken(sv, pos, idTok)) {
                    return false;
                }
                if (varType != "port") return false; // EVCD requires '$var port ...'

                int width = 1;
                if (sizeOrRange.front() == '[') {
                    if (!DetailVerilogEvcd::ParseBracketRangeWidth(sizeOrRange, width)) return false;
                } else {
                    if (!ReaderText::ParseIntSv(sizeOrRange, width) || width < 1 || width > 1024) return false;
                }

                std::string refName;
                bool closed = false;
                std::string_view sub;
                while (DetailVerilogEvcd::NextWsToken(sv, pos, sub)) {
                    if (sub == "$end") { closed = true; break; }
                    if (!refName.empty() && sub.front() != '[') refName.push_back(' ');
                    refName.append(sub.data(), sub.size());
                }
                if (!closed || refName.empty() || idTok.empty()) return false;

                std::string hier;
                for (const auto& sc : scopeStack) {
                    hier += sc;
                    hier.push_back('.');
                }
                hier += refName;

                std::string idKey(idTok);
                auto it = idToPortIdx.find(idKey);
                if (it == idToPortIdx.end()) {
                    if (ports.size() >= 4096) return false;
                    size_t idx = ports.size();
                    DetailVerilogEvcd::EvcdPortVar pv;
                    pv.idCode = idKey;
                    pv.fullName = std::move(hier);
                    pv.bitWidth = width;
                    ports.push_back(std::move(pv));
                    idToPortIdx[idKey] = idx;
                }
            } else if (tok == "$enddefinitions") {
                std::string_view endTok;
                if (!DetailVerilogEvcd::NextWsToken(sv, pos, endTok) || endTok != "$end") return false;
                inHeader = false;
                sawEndDefinitions = true;
            } else {
                return false;
            }
            continue;
        }

        // Simulation value-change body
        if (tok == "$dumpports" || tok == "$dumpportson" || tok == "$dumpportsoff" ||
            tok == "$dumpportsall" || tok == "$dumpportsflush") {
            continue;
        }
        if (tok == "$end") {
            continue;
        }
        if (tok == "$vcdclose") {
            std::string_view tTok, endTok;
            if (!DetailVerilogEvcd::NextWsToken(sv, pos, tTok) ||
                !DetailVerilogEvcd::NextWsToken(sv, pos, endTok) ||
                endTok != "$end") {
                return false;
            }
            uint64_t closeT = 0;
            auto r = std::from_chars(tTok.data(), tTok.data() + tTok.size(), closeT);
            if (r.ec == std::errc{}) maxTime = std::max(maxTime, closeT);
            continue;
        }
        if (tok.front() == '#') {
            std::string_view numSv = tok.substr(1);
            if (numSv.empty()) return false;
            uint64_t ts = 0;
            auto r = std::from_chars(numSv.data(), numSv.data() + numSv.size(), ts);
            if (r.ec != std::errc{} || r.ptr != numSv.data() + numSv.size()) return false;
            curTime = ts;
            minTime = std::min(minTime, curTime);
            maxTime = std::max(maxTime, curTime);
            continue;
        }
        if (tok.front() == 'p' && tok.size() >= 2) {
            // p<port_value> <0_strength> <1_strength> <id_code>
            std::string_view valChars = tok.substr(1);
            std::string_view s0Tok, s1Tok, idTok;
            if (!DetailVerilogEvcd::NextWsToken(sv, pos, s0Tok) ||
                !DetailVerilogEvcd::NextWsToken(sv, pos, s1Tok) ||
                !DetailVerilogEvcd::NextWsToken(sv, pos, idTok)) {
                return false;
            }
            if (!DetailVerilogEvcd::IsStrengthDigits(s0Tok) ||
                !DetailVerilogEvcd::IsStrengthDigits(s1Tok)) {
                return false;
            }
            auto it = idToPortIdx.find(std::string(idTok));
            if (it == idToPortIdx.end()) return false;
            auto& pv = ports[it->second];

            std::string mapped;
            mapped.reserve(static_cast<size_t>(pv.bitWidth));
            for (char ch : valChars) {
                char bit = 'x';
                bool isIn = false, isOut = false;
                if (!DetailVerilogEvcd::MapEvcdStateChar(ch, bit, isIn, isOut)) return false;
                pv.sawInDrive  = pv.sawInDrive  || isIn;
                pv.sawOutDrive = pv.sawOutDrive || isOut;
                mapped.push_back(bit);
            }
            if (mapped.empty()) return false;

            // IEEE 1364-2005 18.3.2: single-char port value replicates across full vector width
            if (mapped.size() == 1 && pv.bitWidth > 1) {
                mapped.assign(static_cast<size_t>(pv.bitWidth), mapped[0]);
            } else if (static_cast<int>(mapped.size()) < pv.bitWidth) {
                char pad = (mapped.front() == 'x' || mapped.front() == 'z') ? mapped.front() : '0';
                mapped.insert(0, static_cast<size_t>(pv.bitWidth) - mapped.size(), pad);
            } else if (static_cast<int>(mapped.size()) > pv.bitWidth) {
                mapped.erase(0, mapped.size() - static_cast<size_t>(pv.bitWidth));
            }

            minTime = std::min(minTime, curTime);
            maxTime = std::max(maxTime, curTime);
            if (pv.changes.size() < 65536) {
                pv.changes.emplace_back(curTime, std::move(mapped));
            }
            ++totalPortChanges;
            continue;
        }

        return false; // Unexpected token in EVCD stream
    }

    if (!sawEndDefinitions || ports.empty() || totalPortChanges == 0) return false;
    if (minTime > maxTime) { minTime = 0; maxTime = 100; }

    std::vector<DetailHwBin::DecodedWaveSignal> sigs;
    sigs.reserve(ports.size());
    for (auto& pv : ports) {
        DetailHwBin::DecodedWaveSignal ws;
        std::string dirTag = (pv.sawInDrive && pv.sawOutDrive) ? " [IO]"
                           : (pv.sawOutDrive ? " [OUT]" : (pv.sawInDrive ? " [IN]" : ""));
        ws.name = PathUtils::Utf8ToWide(pv.fullName + dirTag);
        ws.bitWidth = pv.bitWidth;
        ws.isReal = false;
        ws.changes = std::move(pv.changes);
        sigs.push_back(std::move(ws));
    }

    out = ParsedEdaBoard{};
    std::wstring tsWide = timescaleStr.empty() ? L"ticks" : PathUtils::Utf8ToWide(timescaleStr);
    wchar_t title[220] = {};
    swprintf_s(title, L"IEEE 1364 Extended VCD (.evcd, %zu ports, %zu events, %llu..%llu @ %ls)",
               ports.size(), totalPortChanges,
               static_cast<unsigned long long>(minTime),
               static_cast<unsigned long long>(maxTime),
               tsWide.c_str());
    if (!DetailHwBin::RenderWaveSignalsToBoard(sigs, minTime, maxTime, title, out)) return false;
    out.netCount = ports.size();
    return true;
}

} // namespace Formats
