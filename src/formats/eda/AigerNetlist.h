#pragma once
// AigerNetlist.h — Biere AIGER And-Inverter Graph (.aig / .aag) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

// 3. Biere AIGER And-Inverter Graph (.aig Binary & .aag ASCII)
inline bool ParseAigerNetlistBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    using namespace DetailGateAndDaqEda;
    if (!data || size < 10 || size > 32 * 1024 * 1024) return false;
    bool isBin = (std::memcmp(data, "aig ", 4) == 0);
    bool isAsc = (std::memcmp(data, "aag ", 4) == 0);
    if (!isBin && !isAsc) return false;

    size_t eol0 = 4;
    while (eol0 < size && data[eol0] != '\n') ++eol0;
    if (eol0 >= size) return false;
    std::string_view hdrRem(reinterpret_cast<const char*>(data + 4), eol0 - 4);
    std::string_view tM, tI, tL, tO, tA;
    uint64_t M64 = 0, I64 = 0, L64 = 0, O64 = 0, A64 = 0;
    if (!ReaderText::NextTokenSv(hdrRem, tM) || !ReaderText::ParseUint64Sv(tM, M64) ||
        !ReaderText::NextTokenSv(hdrRem, tI) || !ReaderText::ParseUint64Sv(tI, I64) ||
        !ReaderText::NextTokenSv(hdrRem, tL) || !ReaderText::ParseUint64Sv(tL, L64) ||
        !ReaderText::NextTokenSv(hdrRem, tO) || !ReaderText::ParseUint64Sv(tO, O64) ||
        !ReaderText::NextTokenSv(hdrRem, tA) || !ReaderText::ParseUint64Sv(tA, A64)) {
        return false;
    }
    if (M64 > 500000ULL || O64 > 500000ULL ||
        I64 > M64 || L64 > M64 || A64 > M64 ||
        (I64 + L64 + A64) > M64 || (I64 + L64 + O64 + A64) == 0ULL) {
        return false;
    }
    const uint32_t M = static_cast<uint32_t>(M64);
    const uint32_t I = static_cast<uint32_t>(I64);
    const uint32_t L = static_cast<uint32_t>(L64);
    const uint32_t O = static_cast<uint32_t>(O64);
    const uint32_t A = static_cast<uint32_t>(A64);

    size_t pos = eol0 + 1;
    auto readLineStr = [&](std::string& sOut) -> bool {
        if (pos >= size) return false;
        size_t e = pos;
        while (e < size && data[e] != '\n') ++e;
        if (e >= size && isBin) return false;
        size_t len = e - pos;
        if (len > 0 && data[pos + len - 1] == '\r') --len;
        sOut.assign(reinterpret_cast<const char*>(data + pos), len);
        pos = (e < size) ? (e + 1) : size;
        return true;
    };

    std::vector<uint32_t> inputLits(I);
    std::vector<std::pair<uint32_t, uint32_t>> latches(L); // (latchLit, nextLit)
    std::vector<uint32_t> outputs(O);
    struct AigAnd { uint32_t lhs, rhs0, rhs1; };
    std::vector<AigAnd> ands;
    ands.reserve(std::min<size_t>(A, 2048));

    auto parseU32Tok = [](std::string_view& rem, uint32_t maxLit, uint32_t& outVal) -> bool {
        std::string_view tk;
        uint64_t v = 0;
        if (!ReaderText::NextTokenSv(rem, tk) || !ReaderText::ParseUint64Sv(tk, v) || v > maxLit) return false;
        outVal = static_cast<uint32_t>(v);
        return true;
    };
    const uint32_t maxAllowedLit = 2u * M + 1u;

    if (isBin) {
        for (uint32_t i = 0; i < I; ++i) inputLits[i] = 2u * (i + 1u);
        for (uint32_t i = 0; i < L; ++i) {
            std::string ln;
            if (!readLineStr(ln)) return false;
            std::string_view rem(ln);
            uint32_t nxt = 0;
            if (!parseU32Tok(rem, maxAllowedLit, nxt)) return false;
            latches[i] = { 2u * (I + i + 1u), nxt };
        }
        for (uint32_t i = 0; i < O; ++i) {
            std::string ln;
            if (!readLineStr(ln)) return false;
            std::string_view rem(ln);
            uint32_t ol = 0;
            if (!parseU32Tok(rem, maxAllowedLit, ol)) return false;
            outputs[i] = ol;
        }
        // Binary LEB128 delta-encoded AND gates
        auto readVarint = [&](uint32_t& outV) -> bool {
            outV = 0;
            int shift = 0;
            while (pos < size && shift <= 28) {
                uint8_t b = data[pos++];
                outV |= static_cast<uint32_t>(b & 0x7Fu) << shift;
                if ((b & 0x80u) == 0) return true;
                shift += 7;
            }
            return false;
        };
        for (uint32_t i = 0; i < A; ++i) {
            uint32_t d0 = 0, d1 = 0;
            if (!readVarint(d0) || !readVarint(d1)) return false; // Truncated binary AIGER!
            uint32_t lhs = 2u * (I + L + i + 1u);
            if (d0 == 0 || d0 > lhs) return false;
            uint32_t rhs0 = lhs - d0;
            if (d1 > rhs0) return false;
            uint32_t rhs1 = rhs0 - d1;
            if (ands.size() < 1024) ands.push_back({ lhs, rhs0, rhs1 });
        }
    } else {
        for (uint32_t i = 0; i < I; ++i) {
            std::string ln;
            if (!readLineStr(ln)) return false;
            std::string_view rem(ln);
            uint32_t il = 0;
            if (!parseU32Tok(rem, maxAllowedLit, il)) return false;
            inputLits[i] = il;
        }
        for (uint32_t i = 0; i < L; ++i) {
            std::string ln;
            if (!readLineStr(ln)) return false;
            std::string_view rem(ln);
            uint32_t ll = 0, nxt = 0;
            if (!parseU32Tok(rem, maxAllowedLit, ll) || !parseU32Tok(rem, maxAllowedLit, nxt)) return false;
            latches[i] = { ll, nxt };
        }
        for (uint32_t i = 0; i < O; ++i) {
            std::string ln;
            if (!readLineStr(ln)) return false;
            std::string_view rem(ln);
            uint32_t ol = 0;
            if (!parseU32Tok(rem, maxAllowedLit, ol)) return false;
            outputs[i] = ol;
        }
        for (uint32_t i = 0; i < A; ++i) {
            std::string ln;
            if (!readLineStr(ln)) return false;
            std::string_view rem(ln);
            uint32_t lhs = 0, r0 = 0, r1 = 0;
            if (!parseU32Tok(rem, maxAllowedLit, lhs) ||
                !parseU32Tok(rem, maxAllowedLit, r0) ||
                !parseU32Tok(rem, maxAllowedLit, r1)) return false;
            if (ands.size() < 1024) ands.push_back({ lhs, r0, r1 });
        }
    }

    // Optional AIGER symbol table: i<k> <name>, l<k> <name>, o<k> <name> until 'c'
    std::unordered_map<uint32_t, std::string> inSym, latchSym, outSym;
    while (pos < size) {
        std::string ln;
        if (!readLineStr(ln) || ln.empty()) break;
        if (ln == "c") break;
        if ((ln[0] == 'i' || ln[0] == 'l' || ln[0] == 'o') && ln.size() >= 3) {
            size_t sp = ln.find(' ');
            if (sp != std::string::npos && sp + 1 < ln.size()) {
                uint32_t idx = static_cast<uint32_t>(std::strtoul(ln.c_str() + 1, nullptr, 10));
                std::string nm = ln.substr(sp + 1);
                if (ln[0] == 'i') inSym[idx] = nm;
                else if (ln[0] == 'l') latchSym[idx] = nm;
                else outSym[idx] = nm;
            }
        }
    }

    auto varNet = [](uint32_t lit) -> std::string {
        uint32_t v = lit >> 1;
        return (v == 0) ? "CONST0" : ("v" + std::to_string(v));
    };

    std::vector<GateNode> dagNodes;
    for (uint32_t i = 0; i < I && dagNodes.size() < 512; ++i) {
        GateNode gn;
        gn.instName = inSym.count(i) ? inSym[i] : ("in" + std::to_string(i));
        gn.cellType = "INPUT"; gn.isPrimaryIn = true;
        gn.pins.push_back({ "O", varNet(inputLits[i]), true, false });
        dagNodes.push_back(std::move(gn));
    }
    for (uint32_t i = 0; i < L && dagNodes.size() < 512; ++i) {
        GateNode gn;
        gn.instName = latchSym.count(i) ? latchSym[i] : ("L" + std::to_string(i));
        gn.cellType = "LATCH"; gn.isSeq = true;
        gn.pins.push_back({ "D", varNet(latches[i].second), false, (latches[i].second & 1u) != 0 });
        gn.pins.push_back({ "Q", varNet(latches[i].first), true, false });
        dagNodes.push_back(std::move(gn));
    }
    for (size_t i = 0; i < ands.size() && dagNodes.size() < 512; ++i) {
        GateNode gn;
        gn.instName = "A" + std::to_string(i + 1);
        gn.cellType = "AND2";
        gn.pins.push_back({ "A", varNet(ands[i].rhs0), false, (ands[i].rhs0 & 1u) != 0 });
        gn.pins.push_back({ "B", varNet(ands[i].rhs1), false, (ands[i].rhs1 & 1u) != 0 });
        gn.pins.push_back({ "Y", varNet(ands[i].lhs), true, false });
        dagNodes.push_back(std::move(gn));
    }
    for (uint32_t i = 0; i < O && dagNodes.size() < 512; ++i) {
        GateNode gn;
        gn.instName = outSym.count(i) ? outSym[i] : ("out" + std::to_string(i));
        gn.cellType = "OUTPUT"; gn.isPrimaryOut = true;
        gn.pins.push_back({ "I", varNet(outputs[i]), false, (outputs[i] & 1u) != 0 });
        dagNodes.push_back(std::move(gn));
    }

    wchar_t hdrTitle[160] = {};
    swprintf_s(hdrTitle, L"AIGER %ls Graph (M=%u, I=%u, L=%u, O=%u, A=%u)",
               isBin ? L"Binary (.aig)" : L"ASCII (.aag)", M, I, L, O, A);
    return RenderGateDagToBoard(hdrTitle, dagNodes, out);
}

} // namespace Formats
