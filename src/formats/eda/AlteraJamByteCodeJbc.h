#pragma once
// AlteraJamByteCodeJbc.h — Altera / Intel STAPL Jam Byte-Code v1/v2 (.jbc) Binary Reader & Disassembler
// Parses big-endian Jam Byte-Code headers (`"JBC\0"` / `"JBC\x01"` and `"JAM\0"` / `"JAM\x01"`):
//   - Section offset directory (`action_table`, `proc_table`, `string_table`, `sym_table`, `data_section`,
//     `code_section`, `note_strings`, `action_count`, `proc_count`)
//   - Mandatory trailing 16-bit CCITT CRC (`x^16 + x^12 + x^5 + 1`, `0x1021` / reflected `0x8408`) integrity check
//   - Action -> Procedure linked list (`first_proc_index` -> `next_proc` chain with optional/recommended flags)
//   - JBC VM bytecode disassembler (`(opcode >> 6) & 3` immediate operand count: `PSHL 0x40`, `CALL 0x43`,
//     `SINT 0x47` STATE, `DSS 0x15` / `DSSC 0x16` / `DS 0x51` / `DSC 0x82` DRSCAN, and
//     `ISS 0x17` / `ISSC 0x18` / `IS 0x52` / `ISC 0x83` IRSCAN, `WAIT 0x84`, `RET 0x11`, `EXIT 0x21`)
// Renders the Action -> Procedure hierarchy DAG + 4-lane IEEE 1149.1 JTAG `TCK`/`TMS`/`TDI`/`TDO` scan waveforms!
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailAlteraJbc {

// Altera `jbimain.c` `jbi_check_crc` uses reflected CCITT-16 (`0x8408`, init `0xFFFF`, inverted `~crc` at end),
// while some JBC tools emit direct CCITT-16 (`0x1021`, init `0xFFFF`). Accept either valid CCITT-16 match!
inline uint16_t ComputeJbiCrcReflected(const uint8_t* data, size_t len) noexcept {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? static_cast<uint16_t>((crc >> 1) ^ 0x8408u) : static_cast<uint16_t>(crc >> 1);
        }
    }
    return static_cast<uint16_t>(~crc);
}

inline uint16_t ComputeCcitt16Direct(const uint8_t* data, size_t len) noexcept {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(static_cast<uint16_t>(data[i]) << 8);
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 0x8000u) ? static_cast<uint16_t>((crc << 1) ^ 0x1021u) : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

inline std::string ReadJbcStringAt(const uint8_t* data, size_t size, size_t strTableOff, uint32_t relOff) {
    if (relOff == 0xFFFFFFFFu || strTableOff >= size || relOff >= size - strTableOff) return {};
    const size_t p0 = strTableOff + relOff;
    size_t p = p0;
    while (p < size && (p - p0) < 256u && data[p] != 0u) {
        if (data[p] < 0x20u || data[p] > 0x7Eu) return {};
        ++p;
    }
    if (p >= size || data[p] != 0u) return {};
    return std::string(reinterpret_cast<const char*>(data + p0), p - p0);
}

struct JbcScanEvent {
    bool isIr = false;
    uint32_t bitLen = 0;
    std::vector<uint8_t> tdiBytes;
    std::vector<uint8_t> tdoBytes;
};

struct JbcProcInfo {
    std::string name;
    uint32_t nextProc = 0xFFFFFFFFu;
    uint8_t attrs = 0u;
    uint32_t codeRelOff = 0u;
    size_t irScans = 0u;
    size_t drScans = 0u;
    uint64_t totalScanBits = 0ULL;
    std::vector<uint32_t> calledProcIndices;
    std::vector<JbcScanEvent> scans;
};

struct JbcActionInfo {
    std::string name;
    std::string desc;
    uint32_t firstProc = 0xFFFFFFFFu;
    std::vector<uint32_t> procChain;
};

} // namespace DetailAlteraJbc

inline bool ParseAlteraJamByteCodeJbc(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    using namespace DetailAlteraJbc;

    if (!data || size < 54u || size > (32u << 20)) return false;

    const uint32_t firstWord = ByteIO::RdU32BE(data);
    const uint32_t sig24 = (firstWord >> 8) & 0x00FFFFFFu;
    const uint32_t fmtVer = firstWord & 0xFFu;
    // Accept `"JBC\0"` (`0x4A424300`), `"JBC\x01"` (`0x4A424301`), `"JAM\0"` (`0x4A414D00`), and `"JAM\x01"` (`0x4A414D01`)
    if ((sig24 != 0x4A4243u && sig24 != 0x4A414Du) || fmtVer > 1u) {
        return false;
    }

    // Verify 16-bit CRC stored in the final 2 bytes of the `.jbc` file (`data[size - 2 .. size - 1]`)
    const uint16_t storedCrcBE = ByteIO::RdU16BE(data + size - 2u);
    const uint16_t storedCrcLE = ByteIO::RdU16LE(data + size - 2u);
    const uint16_t calcJbi     = ComputeJbiCrcReflected(data, size - 2u);
    const uint16_t calcCcitt   = ComputeCcitt16Direct(data, size - 2u);
    if (storedCrcBE != calcJbi && storedCrcLE != calcJbi &&
        storedCrcBE != calcCcitt && storedCrcLE != calcCcitt) {
        return false;
    }

    const size_t bodyEnd = size - 2u;

    // Decode big-endian JBC v1 (fmtVer == 0, delta == 0) / v2 (fmtVer == 1, delta == 8) header offsets (`jbimain.c` / `altera.c`):
    // In v2 (`fmtVer == 1`, 52-byte header):
    //   0x04: device_family / note or action_table -> support both standard 52-byte Altera v2 layout
    //         (`delta = fmtVer * 8`: +0x0C action_table, +0x10 proc_table, +0x14 string_table, +0x18 sym_table,
    //          +0x1C data_section, +0x20 code_section, +0x24 debug_section, +0x28 action_count, +0x2C proc_count, +0x30 sym_count,
    //          with +0x04 note_strings) AND compact direct 48-byte layout (+0x04 action_table, +0x08 proc_table,
    //          +0x0C string_table, +0x10 sym_table, +0x14 data_section, +0x18 code_section, +0x1C note_strings,
    //          +0x20 action_count, +0x24 proc_count)!
    size_t actionTable = 0, procTable = 0, stringTable = 0, symTable = 0, dataSec = 0, codeSec = 0, noteStrings = 0;
    uint32_t actionCount = 0, procCount = 0;

    auto validateHeaderLayout = [&](size_t actT, size_t prcT, size_t strT, size_t datS, size_t codS,
                                    size_t noteS, uint32_t actC, uint32_t prcC) -> bool {
        if (actC > 512u || prcC > 4096u || (actC == 0u && prcC == 0u)) return false;
        if (actT < 32u || actT > bodyEnd || prcT < 32u || prcT > bodyEnd) return false;
        if (strT < 32u || strT > bodyEnd || codS < 32u || codS > bodyEnd) return false;
        if (datS > bodyEnd || noteS > bodyEnd) return false;
        if (actT + static_cast<size_t>(actC) * 12u > bodyEnd) return false;
        if (prcT + static_cast<size_t>(prcC) * 13u > bodyEnd) return false;
        // First action or proc name must resolve to a valid printable NUL-terminated string in `stringTable`
        if (actC > 0u) {
            const uint32_t nmOff = ByteIO::RdU32BE(data + actT);
            if (ReadJbcStringAt(data, bodyEnd, strT, nmOff).empty()) return false;
        }
        if (prcC > 0u) {
            const uint32_t nmOff = ByteIO::RdU32BE(data + prcT);
            if (ReadJbcStringAt(data, bodyEnd, strT, nmOff).empty()) return false;
        }
        return true;
    };

    bool headerOk = false;
    // Layout 1: Altera `altera.c` / `jbimain.c` v2 (`delta = fmtVer * 8`)
    if (bodyEnd >= 52u) {
        const size_t d = static_cast<size_t>(fmtVer) * 8u;
        if (44u + d <= bodyEnd) {
            const size_t nNote = (fmtVer == 1u) ? ByteIO::RdU32BE(data + 8u) : ByteIO::RdU32BE(data + 12u);
            const size_t nAct  = ByteIO::RdU32BE(data + 4u + d);
            const size_t nPrc  = ByteIO::RdU32BE(data + 8u + d);
            const size_t nStr  = ByteIO::RdU32BE(data + 12u + d);
            const size_t nSym  = ByteIO::RdU32BE(data + 16u + d);
            const size_t nDat  = ByteIO::RdU32BE(data + 20u + d);
            const size_t nCod  = ByteIO::RdU32BE(data + 24u + d);
            const uint32_t cAct = ByteIO::RdU32BE(data + 32u + d);
            const uint32_t cPrc = ByteIO::RdU32BE(data + 36u + d);
            if (validateHeaderLayout(nAct, nPrc, nStr, nDat, nCod, nNote, cAct, cPrc)) {
                actionTable = nAct; procTable = nPrc; stringTable = nStr; symTable = nSym;
                dataSec = nDat; codeSec = nCod; noteStrings = nNote;
                actionCount = cAct; procCount = cPrc;
                headerOk = true;
            }
        }
    }
    // Layout 2: Direct 48-byte big-endian table directory (`0x04` action_table, `0x08` proc_table, `0x0C` string_table,
    //           `0x10` sym_table, `0x14` data_section, `0x18` code_section, `0x1C` note_strings, `0x20` action_count, `0x24` proc_count)
    if (!headerOk && bodyEnd >= 44u) {
        const size_t nAct  = ByteIO::RdU32BE(data + 0x04u);
        const size_t nPrc  = ByteIO::RdU32BE(data + 0x08u);
        const size_t nStr  = ByteIO::RdU32BE(data + 0x0Cu);
        const size_t nSym  = ByteIO::RdU32BE(data + 0x10u);
        const size_t nDat  = ByteIO::RdU32BE(data + 0x14u);
        const size_t nCod  = ByteIO::RdU32BE(data + 0x18u);
        const size_t nNote = ByteIO::RdU32BE(data + 0x1Cu);
        const uint32_t cAct = ByteIO::RdU32BE(data + 0x20u);
        const uint32_t cPrc = ByteIO::RdU32BE(data + 0x24u);
        if (validateHeaderLayout(nAct, nPrc, nStr, nDat, nCod, nNote, cAct, cPrc)) {
            actionTable = nAct; procTable = nPrc; stringTable = nStr; symTable = nSym;
            dataSec = nDat; codeSec = nCod; noteStrings = nNote;
            actionCount = cAct; procCount = cPrc;
            headerOk = true;
        }
    }
    if (!headerOk) return false;
    (void)symTable;

    // Extract optional `NOTE` key/value pairs (`key\0value\0...`) if `noteStrings` is populated
    std::string deviceName;
    if (noteStrings >= 32u && noteStrings < actionTable && noteStrings < bodyEnd) {
        size_t np = noteStrings;
        for (int ni = 0; ni < 32 && np + 2u < std::min(actionTable, bodyEnd); ++ni) {
            if (data[np] == 0u) break;
            std::string k = ReadJbcStringAt(data, bodyEnd, np, 0u);
            if (k.empty()) break;
            np += k.size() + 1u;
            std::string v = ReadJbcStringAt(data, bodyEnd, np, 0u);
            np += v.size() + 1u;
            if (ReaderText::AsciiEqCI(k, "DEVICE") || ReaderText::AsciiEqCI(k, "PART") || ReaderText::AsciiEqCI(k, "TARGET")) {
                deviceName = v;
            }
        }
    }

    // Parse Procedure Table (`13` bytes per entry: `[nameOff:4BE][nextProc:4BE][attrs:1][codeOff:4BE]`)
    std::vector<JbcProcInfo> procs(procCount);
    for (uint32_t pi = 0u; pi < procCount; ++pi) {
        const uint8_t* ep = data + procTable + static_cast<size_t>(pi) * 13u;
        const uint32_t nmOff = ByteIO::RdU32BE(ep + 0u);
        procs[pi].name       = ReadJbcStringAt(data, bodyEnd, stringTable, nmOff);
        if (procs[pi].name.empty()) procs[pi].name = "PROC_" + std::to_string(pi);
        procs[pi].nextProc   = ByteIO::RdU32BE(ep + 4u);
        procs[pi].attrs      = ep[8u];
        procs[pi].codeRelOff = ByteIO::RdU32BE(ep + 9u);
    }

    // Parse Action Table (`12` bytes per entry: `[nameOff:4BE][descOff:4BE][firstProcIdx:4BE]`)
    std::vector<JbcActionInfo> actions(actionCount);
    for (uint32_t ai = 0u; ai < actionCount; ++ai) {
        const uint8_t* ap = data + actionTable + static_cast<size_t>(ai) * 12u;
        actions[ai].name      = ReadJbcStringAt(data, bodyEnd, stringTable, ByteIO::RdU32BE(ap + 0u));
        actions[ai].desc      = ReadJbcStringAt(data, bodyEnd, stringTable, ByteIO::RdU32BE(ap + 4u));
        actions[ai].firstProc = ByteIO::RdU32BE(ap + 8u);
        if (actions[ai].name.empty()) actions[ai].name = "ACT_" + std::to_string(ai);

        // Follow `firstProc -> nextProc` linked list across `procs` (cycle-safe)
        uint32_t cur = actions[ai].firstProc;
        for (size_t step = 0u; cur < procCount && step < procCount; ++step) {
            actions[ai].procChain.push_back(cur);
            cur = procs[cur].nextProc;
        }
    }

    // Disassemble each procedure's bytecode inside `[codeSec .. codeLimit)`!
    // In Altera JBC VM, `(opcode >> 6) & 3` gives the exact count (`0..3`) of 32-bit BE immediate operands!
    std::vector<size_t> sortedBounds;
    for (const auto& pr : procs) {
        if (codeSec + pr.codeRelOff <= bodyEnd) sortedBounds.push_back(codeSec + pr.codeRelOff);
    }
    sortedBounds.push_back(bodyEnd);
    std::sort(sortedBounds.begin(), sortedBounds.end());
    sortedBounds.erase(std::unique(sortedBounds.begin(), sortedBounds.end()), sortedBounds.end());

    for (uint32_t pi = 0u; pi < procCount; ++pi) {
        JbcProcInfo& pr = procs[pi];
        const size_t startPc = codeSec + pr.codeRelOff;
        if (startPc >= bodyEnd) continue;
        auto ub = std::upper_bound(sortedBounds.begin(), sortedBounds.end(), startPc);
        const size_t endPc = (ub != sortedBounds.end()) ? std::min(*ub, bodyEnd) : bodyEnd;

        std::vector<uint32_t> litStack;
        size_t pc = startPc;
        size_t steps = 0u;
        while (pc < endPc && steps++ < 16384u) {
            const uint8_t op = data[pc];
            const size_t argCnt = static_cast<size_t>((op >> 6) & 0x3u);
            if (pc + 1u + argCnt * 4u > endPc) break;

            uint32_t args[3] = {};
            for (size_t a = 0; a < argCnt; ++a) {
                args[a] = ByteIO::RdU32BE(data + pc + 1u + a * 4u);
            }
            pc += 1u + argCnt * 4u;

            if (op == 0x40u) { // PSHL <imm32>
                if (litStack.size() >= 16u) litStack.erase(litStack.begin());
                litStack.push_back(args[0]);
            } else if (op == 0x01u && !litStack.empty()) { // DUP
                litStack.push_back(litStack.back());
            } else if (op == 0x43u) { // CALL <procIdx> (or bytecode offset)
                uint32_t targetIdx = args[0];
                if (targetIdx >= procCount) {
                    for (uint32_t k = 0; k < procCount; ++k) {
                        if (procs[k].codeRelOff == args[0]) { targetIdx = k; break; }
                    }
                }
                if (targetIdx < procCount && targetIdx != pi) {
                    pr.calledProcIndices.push_back(targetIdx);
                }
            } else if (op == 0x17u || op == 0x18u || op == 0x52u || op == 0x83u ||
                       op == 0x15u || op == 0x16u || op == 0x51u || op == 0x82u)
            {
                // IRSCAN (`ISS=0x17`, `ISSC=0x18`, `IS=0x52`, `ISC=0x83`) or
                // DRSCAN (`DSS=0x15`, `DSSC=0x16`, `DS=0x51`, `DSC=0x82`)
                const bool isIr = (op == 0x17u || op == 0x18u || op == 0x52u || op == 0x83u);
                uint32_t bitLen = isIr ? 10u : 32u;
                uint32_t tdiVal = isIr ? 0x06u : 0xA55AA55Au;

                if (!litStack.empty()) {
                    // For short stack scans (`ISS`/`DSS`), top of stack holds `[tdiVal, bitLen]` or `[count]`
                    if ((op == 0x17u || op == 0x15u) && litStack.size() >= 2u) {
                        bitLen = std::clamp<uint32_t>(litStack[litStack.size() - 2u], 1u, 65536u);
                        tdiVal = litStack.back();
                        litStack.resize(litStack.size() - 2u);
                    } else {
                        bitLen = std::clamp<uint32_t>(litStack.back(), 1u, 65536u);
                        litStack.pop_back();
                    }
                }

                if (isIr) ++pr.irScans;
                else      ++pr.drScans;
                pr.totalScanBits += bitLen;

                if (pr.scans.size() < 24u) {
                    JbcScanEvent ev;
                    ev.isIr = isIr;
                    ev.bitLen = std::min<uint32_t>(bitLen, 32u);
                    const size_t nb = (ev.bitLen + 7u) / 8u;
                    ev.tdiBytes.resize(nb, 0u);
                    // Read TDI bytes from `data_section + args[0]` if `argCnt >= 1` and inside `dataSec .. codeSec`
                    if (argCnt >= 1u && dataSec > 0u && dataSec + args[0] + nb <= codeSec) {
                        std::memcpy(ev.tdiBytes.data(), data + dataSec + args[0], nb);
                    } else {
                        for (size_t b = 0; b < nb; ++b) {
                            ev.tdiBytes[nb - 1u - b] = static_cast<uint8_t>((tdiVal >> (b * 8u)) & 0xFFu);
                        }
                    }
                    if (argCnt >= 2u && dataSec > 0u && dataSec + args[1] + nb <= codeSec) {
                        ev.tdoBytes.resize(nb);
                        std::memcpy(ev.tdoBytes.data(), data + dataSec + args[1], nb);
                    }
                    pr.scans.push_back(std::move(ev));
                }
            } else if (op == 0x11u || op == 0x21u) { // RET (0x11) or EXIT (0x21)
                break;
            }
        }
    }

    outBoard = ParsedEdaBoard{};
    outBoard.kind = EdaDocKind::KiCadSch;

    // 1. Render Action -> Procedure DAG (`y >= 28` mm)
    const float dagTopY = 66.0f;
    const float actX = -38.0f, procX = 14.0f;
    const size_t nProcs = std::min<size_t>(procs.size(), 24u);
    const size_t nActs  = std::min<size_t>(actions.size(), 16u);

    for (size_t pi = 0; pi < nProcs; ++pi) {
        const float py = dagTopY - static_cast<float>(pi) * 6.8f;
        const float pw = 30.0f, ph = 4.6f;
        DrawEdaBoxRect(outBoard, procX, py - ph * 0.5f, procX + pw, py + ph * 0.5f, 0.28f, EdaLayerId::InnerCopper, 0);

        wchar_t pLbl[180] = {};
        swprintf_s(pLbl, L"%hs (+0x%X | IR:%zu DR:%zu)",
                   procs[pi].name.c_str(), procs[pi].codeRelOff, procs[pi].irScans, procs[pi].drScans);
        outBoard.labels.push_back({ procX + 0.9f, py - 0.35f, 0.76f, EdaLayerId::SchLabel, pLbl });
        outBoard.pads.push_back({ procX, py, 0.85f, 0.85f, 0.0f, 0.0f, 1u, EdaLayerId::InnerCopper, PathUtils::Utf8ToWide(procs[pi].name), L"JBC_PROC" });

        EdaComponent3D pc;
        pc.refDes = L"P_" + std::to_wstring(pi);
        pc.value = PathUtils::Utf8ToWide(procs[pi].name);
        pc.footprint = L"JBC_PROCEDURE";
        pc.x = procX + pw * 0.5f; pc.y = py;
        pc.widthMm = pw; pc.lengthMm = ph; pc.heightMm = 1.1f;
        pc.pinCount = 2;
        pc.category = EdaPartCategory::IC_Package;
        outBoard.components.push_back(std::move(pc));
    }

    size_t callEdges = 0u;
    for (size_t ai = 0; ai < nActs; ++ai) {
        const float ay = dagTopY - static_cast<float>(ai) * 8.4f;
        const float aw = 22.0f, ah = 5.0f;
        DrawEdaBoxRect(outBoard, actX, ay - ah * 0.5f, actX + aw, ay + ah * 0.5f, 0.32f, EdaLayerId::TopCopper, 0);
        outBoard.labels.push_back({ actX + 0.9f, ay - 0.35f, 0.85f, EdaLayerId::SchLabel, L"ACTION " + PathUtils::Utf8ToWide(actions[ai].name) });
        outBoard.pads.push_back({ actX + aw, ay, 0.90f, 0.90f, 0.0f, 0.0f, 1u, EdaLayerId::TopCopper, PathUtils::Utf8ToWide(actions[ai].name), L"JBC_ACTION" });

        EdaComponent3D ac;
        ac.refDes = L"A_" + std::to_wstring(ai);
        ac.value = PathUtils::Utf8ToWide(actions[ai].name);
        ac.footprint = L"JBC_ACTION";
        ac.x = actX + aw * 0.5f; ac.y = ay;
        ac.widthMm = aw; ac.lengthMm = ah; ac.heightMm = 1.2f;
        ac.pinCount = std::max<int>(1, static_cast<int>(actions[ai].procChain.size()));
        ac.category = EdaPartCategory::Connector;
        outBoard.components.push_back(std::move(ac));

        for (uint32_t targetPi : actions[ai].procChain) {
            if (targetPi >= nProcs) continue;
            const float py = dagTopY - static_cast<float>(targetPi) * 6.8f;
            const float midX = -6.0f + static_cast<float>(callEdges % 7u) * 1.4f;
            const int netId = static_cast<int>(10u + ai);
            outBoard.traces.push_back({ actX + aw, ay, midX, ay, 0.22f, EdaLayerId::SchWire, netId });
            outBoard.traces.push_back({ midX, ay, midX, py, 0.22f, EdaLayerId::SchWire, netId });
            outBoard.traces.push_back({ midX, py, procX, py, 0.22f, EdaLayerId::SchWire, netId });
            outBoard.traces.push_back({ procX - 1.1f, py + 0.55f, procX, py, 0.22f, EdaLayerId::SchWire, netId });
            outBoard.traces.push_back({ procX - 1.1f, py - 0.55f, procX, py, 0.22f, EdaLayerId::SchWire, netId });
            ++callEdges;
        }
    }

    // Disassembled `CALL 0x43` procedure-to-procedure arcs
    for (size_t pi = 0; pi < nProcs; ++pi) {
        for (uint32_t callee : procs[pi].calledProcIndices) {
            if (callee >= nProcs || callee == pi) continue;
            const float y0 = dagTopY - static_cast<float>(pi) * 6.8f;
            const float y1 = dagTopY - static_cast<float>(callee) * 6.8f;
            const float rx = procX + 30.0f + 2.5f + static_cast<float>(pi % 4u) * 1.2f;
            outBoard.traces.push_back({ procX + 30.0f, y0, rx, y0, 0.18f, EdaLayerId::InnerCopper, 5 });
            outBoard.traces.push_back({ rx, y0, rx, y1, 0.18f, EdaLayerId::InnerCopper, 5 });
            outBoard.traces.push_back({ rx, y1, procX + 30.0f, y1, 0.18f, EdaLayerId::InnerCopper, 5 });
            ++callEdges;
        }
    }

    // 2. Render 4-lane JTAG `TCK`/`TMS`/`TDI`/`TDO` waveforms from disassembled `IRSCAN`/`DRSCAN` opcodes!
    DetailIndustrialAndWaveEda::JtagTapTracker tap;
    tap.StepTo(0x00u, true);
    tap.StepTo(0x01u, false);

    size_t totalIr = 0u, totalDr = 0u;
    for (const auto& pr : procs) {
        totalIr += pr.irScans;
        totalDr += pr.drScans;
        for (const auto& sc : pr.scans) {
            if (tap.cycles.size() >= DetailIndustrialAndWaveEda::JtagTapTracker::kMaxVisualCycles) break;
            tap.ShiftBytesBE(
                sc.isIr ? 0x0Bu : 0x04u,
                0x01u,
                sc.bitLen,
                sc.tdiBytes.empty() ? nullptr : sc.tdiBytes.data(),
                sc.tdoBytes.empty() ? nullptr : sc.tdoBytes.data(),
                nullptr,
                true);
        }
    }
    if (!tap.cycles.empty()) {
        wchar_t wSum[160] = {};
        swprintf_s(wSum, L"JBC Disassembled JTAG Scans (%zu SIR, %zu SDR, CRC 0x%04X OK)",
                   totalIr, totalDr, storedCrcBE);
        DetailIndustrialAndWaveEda::RenderJtag4LaneWaveforms(outBoard, tap.cycles, wSum);
    }

    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"Altera Jam STAPL Byte-Code v%u (.jbc%ls — %u actions, %u procs, %zu SIR/%zu SDR)",
               fmtVer + 1u,
               deviceName.empty() ? L"" : (L" " + PathUtils::Utf8ToWide(deviceName)).c_str(),
               actionCount, procCount, totalIr, totalDr);
    outBoard.formatTitle = titleBuf;
    outBoard.netCount = std::max<size_t>(4u, callEdges + 4u);
    return !outBoard.traces.empty() || !outBoard.components.empty();
}

} // namespace Formats
