#pragma once
// JtagSvf.h — IEEE 1149.1 ASCII JTAG Serial Vector Format (.svf) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseJtagSvfBoard(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    // #70a. IEEE 1149.1 ASCII JTAG Serial Vector Format (.svf)
    if (ext == L".svf") {
        if (size == 0) return false;
        size_t binProbe = std::min<size_t>(size, 4096);
        for (size_t i = 0; i < binProbe; ++i) {
            if (data[i] == 0u) return false;
        }

        // Strip '!' and '//' comments while preserving statement boundaries (';')
        std::string cleanSvf;
        cleanSvf.reserve(std::min<size_t>(size, 524288));
        for (size_t i = 0; i < size && cleanSvf.size() < 524288; ++i) {
            char c = sv[i];
            if (c == '!' || (c == '/' && i + 1 < size && sv[i + 1] == '/')) {
                while (i < size && sv[i] != '\n') ++i;
                cleanSvf.push_back(' ');
            } else {
                cleanSvf.push_back((c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c);
            }
        }

        auto mapSvfState = [](std::string_view tok, uint8_t& outSt) -> bool {
            if (tok == "RESET")     { outSt = 0x00u; return true; }
            if (tok == "IDLE")      { outSt = 0x01u; return true; }
            if (tok == "DRSELECT")  { outSt = 0x02u; return true; }
            if (tok == "DRCAPTURE") { outSt = 0x03u; return true; }
            if (tok == "DRSHIFT")   { outSt = 0x04u; return true; }
            if (tok == "DREXIT1")   { outSt = 0x05u; return true; }
            if (tok == "DRPAUSE")   { outSt = 0x06u; return true; }
            if (tok == "DREXIT2")   { outSt = 0x07u; return true; }
            if (tok == "DRUPDATE")  { outSt = 0x08u; return true; }
            if (tok == "IRSELECT")  { outSt = 0x09u; return true; }
            if (tok == "IRCAPTURE") { outSt = 0x0Au; return true; }
            if (tok == "IRSHIFT")   { outSt = 0x0Bu; return true; }
            if (tok == "IREXIT1")   { outSt = 0x0Cu; return true; }
            if (tok == "IRPAUSE")   { outSt = 0x0Du; return true; }
            if (tok == "IREXIT2")   { outSt = 0x0Eu; return true; }
            if (tok == "IRUPDATE")  { outSt = 0x0Fu; return true; }
            return false;
        };

        auto parseParenHexBE = [](std::string_view stmt, std::string_view kw, uint32_t numBits, std::vector<uint8_t>& outBe) -> bool {
            size_t kp = 0;
            while ((kp = stmt.find(kw, kp)) != std::string_view::npos) {
                bool leftOk = (kp == 0) || !isalnum(static_cast<unsigned char>(stmt[kp - 1]));
                size_t afterKw = kp + kw.size();
                bool rightOk = (afterKw >= stmt.size()) || !isalnum(static_cast<unsigned char>(stmt[afterKw]));
                if (leftOk && rightOk) {
                    size_t lp = stmt.find('(', afterKw);
                    size_t rp = (lp != std::string_view::npos) ? stmt.find(')', lp + 1) : std::string_view::npos;
                    if (lp != std::string_view::npos && rp != std::string_view::npos && rp > lp) {
                        std::vector<uint8_t> nibs;
                        for (size_t k = lp + 1; k < rp; ++k) {
                            char ch = stmt[k];
                            int nib = (ch >= '0' && ch <= '9') ? (ch - '0') :
                                      (ch >= 'A' && ch <= 'F') ? (ch - 'A' + 10) : -1;
                            if (nib >= 0) nibs.push_back(static_cast<uint8_t>(nib));
                        }
                        const size_t nb = (static_cast<size_t>(numBits) + 7u) / 8u;
                        outBe.assign(nb, 0u);
                        for (size_t g = 0; g < nibs.size() && (g / 2u) < nb; ++g) {
                            uint8_t nib = nibs[nibs.size() - 1u - g];
                            size_t byteIdx = nb - 1u - (g / 2u);
                            if ((g & 1u) == 0u) outBe[byteIdx] |= nib;
                            else outBe[byteIdx] |= static_cast<uint8_t>(nib << 4);
                        }
                        return true;
                    }
                }
                kp = afterKw;
            }
            return false;
        };

        DetailIndustrialAndWaveEda::JtagTapTracker tap;
        uint8_t svfEndIr = 0x01u; // IDLE
        uint8_t svfEndDr = 0x01u; // IDLE
        size_t validSvfCmds = 0;
        uint64_t totalShiftedBits = 0;
        uint32_t lastSirBits = 0, lastSdrBits = 0;
        std::vector<uint8_t> stickySirTdi, stickySdrTdi;

        size_t sPos = 0;
        while (sPos < cleanSvf.size()) {
            size_t semi = cleanSvf.find(';', sPos);
            if (semi == std::string::npos) break;
            std::string_view stmt(cleanSvf.data() + sPos, semi - sPos);
            sPos = semi + 1;

            // Tokenize leading keyword and whitespace-separated words
            std::vector<std::string_view> toks;
            size_t tp = 0;
            while (tp < stmt.size() && toks.size() < 32) {
                while (tp < stmt.size() && (stmt[tp] == ' ' || stmt[tp] == '\t' || stmt[tp] == '\r' || stmt[tp] == '\n')) ++tp;
                if (tp >= stmt.size()) break;
                if (stmt[tp] == '(') {
                    size_t rp = stmt.find(')', tp + 1);
                    if (rp == std::string_view::npos) break;
                    tp = rp + 1;
                    continue;
                }
                size_t te = tp;
                while (te < stmt.size() && stmt[te] != ' ' && stmt[te] != '\t' && stmt[te] != '\r' && stmt[te] != '\n' && stmt[te] != '(') ++te;
                toks.push_back(stmt.substr(tp, te - tp));
                tp = te;
            }
            if (toks.empty()) continue;

            std::string_view cmd = toks[0];
            if (cmd == "SIR" || cmd == "SDR") {
                if (toks.size() < 2) continue;
                uint64_t bits = 0;
                bool numOk = !toks[1].empty();
                for (char c : toks[1]) {
                    if (c < '0' || c > '9') { numOk = false; break; }
                    bits = bits * 10ULL + static_cast<uint64_t>(c - '0');
                    if (bits > 16777216ULL) { numOk = false; break; }
                }
                if (!numOk || bits == 0ULL) continue;
                uint32_t nb32 = static_cast<uint32_t>(bits);
                const size_t nBytes = (static_cast<size_t>(nb32) + 7u) / 8u;

                std::vector<uint8_t> tdiBe, tdoBe, maskBe;
                bool hasTdi = parseParenHexBE(stmt, "TDI", nb32, tdiBe);
                bool hasTdo = parseParenHexBE(stmt, "TDO", nb32, tdoBe);
                bool hasMask = parseParenHexBE(stmt, "MASK", nb32, maskBe);

                if (cmd == "SIR") {
                    if (hasTdi) { stickySirTdi = tdiBe; lastSirBits = nb32; }
                    else if (lastSirBits == nb32 && stickySirTdi.size() == nBytes) tdiBe = stickySirTdi;
                    else tdiBe.assign(nBytes, 0u);
                    tap.ShiftBytesBE(0x0Bu, svfEndIr, nb32, tdiBe.data(),
                        hasTdo ? tdoBe.data() : nullptr, hasMask ? &maskBe : nullptr, true);
                } else {
                    if (hasTdi) { stickySdrTdi = tdiBe; lastSdrBits = nb32; }
                    else if (lastSdrBits == nb32 && stickySdrTdi.size() == nBytes) tdiBe = stickySdrTdi;
                    else tdiBe.assign(nBytes, 0u);
                    tap.ShiftBytesBE(0x04u, svfEndDr, nb32, tdiBe.data(),
                        hasTdo ? tdoBe.data() : nullptr, hasMask ? &maskBe : nullptr, true);
                }
                totalShiftedBits += nb32;
                ++validSvfCmds;
            } else if (cmd == "STATE") {
                bool anySt = false;
                for (size_t i = 1; i < toks.size(); ++i) {
                    uint8_t st = 0;
                    if (mapSvfState(toks[i], st)) {
                        tap.StepTo(st, true);
                        anySt = true;
                    }
                }
                if (anySt) ++validSvfCmds;
            } else if (cmd == "ENDIR" && toks.size() >= 2) {
                uint8_t st = 0;
                if (mapSvfState(toks[1], st)) { svfEndIr = st; ++validSvfCmds; }
            } else if (cmd == "ENDDR" && toks.size() >= 2) {
                uint8_t st = 0;
                if (mapSvfState(toks[1], st)) { svfEndDr = st; ++validSvfCmds; }
            } else if (cmd == "RUNTEST" && toks.size() >= 2) {
                uint8_t runSt = 0x01u;
                size_t idx = 1;
                if (mapSvfState(toks[idx], runSt)) ++idx;
                uint32_t clks = 4u;
                if (idx < toks.size()) {
                    char* ep = nullptr;
                    std::string ns(toks[idx]);
                    unsigned long v = std::strtoul(ns.c_str(), &ep, 10);
                    if (ep != ns.c_str() && v > 0UL) clks = static_cast<uint32_t>(std::min<unsigned long>(v, 8UL));
                }
                tap.EmitIdleClocks(clks, runSt);
                ++validSvfCmds;
            } else if (cmd == "TRST" || cmd == "FREQUENCY" || cmd == "HIR" || cmd == "HDR" ||
                       cmd == "TIR" || cmd == "TDR" || cmd == "PIO" || cmd == "PIOMAP")
            {
                if (cmd == "TRST") tap.StepTo(0x00u, true);
                ++validSvfCmds;
            }
        }

        if (validSvfCmds == 0 || tap.cycles.empty()) return false;

        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"IEEE 1149.1 JTAG Serial Vector (.svf)";
        DetailIndustrialAndWaveEda::RenderJtag4LaneWaveforms(out, tap.cycles,
            L"IEEE 1149.1 SVF (" + std::to_wstring(validSvfCmds) + L" cmds, " + std::to_wstring(totalShiftedBits) + L" bits)");
        return true;
    }
    return false;
}

} // namespace Formats
