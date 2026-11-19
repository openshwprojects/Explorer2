#pragma once
// XilinxXsvf.h — Xilinx XAPP503 Binary JTAG Scan Vector (.xsvf) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseXilinxXsvfBoard(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    // #70b. Xilinx XAPP503 Binary JTAG Scan Vector (.xsvf)
    if (ext == L".xsvf") {
        if (size == 0) return false;

        auto bytesForBits = [](uint32_t bits) -> size_t {
            return (static_cast<size_t>(bits) + 7u) / 8u;
        };
        auto popcntByte = [](uint8_t v) -> uint32_t {
            v = static_cast<uint8_t>((v & 0x55u) + ((v >> 1) & 0x55u));
            v = static_cast<uint8_t>((v & 0x33u) + ((v >> 2) & 0x33u));
            return static_cast<uint32_t>((v + (v >> 4)) & 0x0Fu);
        };

        DetailEda200::JtagTapTracker tap;
        uint32_t xsdrSizeBits = 0;
        uint8_t xrepeat = 32;
        uint8_t xendir = 0; // 0 = Run-Test/Idle (0x01), 1 = Pause-IR (0x0D)
        uint8_t xenddr = 0; // 0 = Run-Test/Idle (0x01), 1 = Pause-DR (0x06)
        uint32_t dataMaskOnes = 0;
        std::vector<uint8_t> tdoMask;
        size_t shiftOrStateCmds = 0;
        size_t totalOpcodes = 0;
        uint64_t totalShiftedBits = 0;
        std::string firstComment;

        size_t pos = 0;
        while (pos < size) {
            uint8_t op = data[pos++];
            ++totalOpcodes;

            if (op == 0x00u) { // XCOMPLETE — clean end of XAPP503 stream
                break;
            } else if (op == 0x01u) { // XTDOMASK
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb > size - pos) return false;
                tdoMask.assign(data + pos, data + pos + nb);
                pos += nb;
            } else if (op == 0x02u) { // XSIR (1B lengthBits + TDI)
                if (1u > size - pos) return false;
                uint8_t lenBits = data[pos++];
                if (lenBits == 0u) return false;
                size_t nb = bytesForBits(lenBits);
                if (nb > size - pos) return false;
                tap.ShiftBytesBE(0x0Bu, xendir ? 0x0Du : 0x01u, lenBits, data + pos, nullptr, nullptr, true);
                pos += nb;
                totalShiftedBits += lenBits;
                ++shiftOrStateCmds;
            } else if (op == 0x03u) { // XSDR
                if (xsdrSizeBits == 0u) return false;
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb > size - pos) return false;
                tap.ShiftBytesBE(0x04u, xenddr ? 0x06u : 0x01u, xsdrSizeBits, data + pos, nullptr, nullptr, true);
                pos += nb;
                totalShiftedBits += xsdrSizeBits;
                ++shiftOrStateCmds;
            } else if (op == 0x04u) { // XRUNTEST (4B uint32 BE)
                if (4u > size - pos) return false;
                uint32_t runClks = DetailEda200::RdU32BE(data + pos);
                pos += 4u;
                if (runClks > 0u) {
                    tap.EmitIdleClocks(runClks, 0x01u);
                }
            } else if (op == 0x07u) { // XREPEAT (1B)
                if (1u > size - pos) return false;
                xrepeat = data[pos++];
                (void)xrepeat;
            } else if (op == 0x08u) { // XSDRSIZE (4B uint32 BE)
                if (4u > size - pos) return false;
                xsdrSizeBits = DetailEda200::RdU32BE(data + pos);
                pos += 4u;
                if (xsdrSizeBits > 16777216u) return false;
            } else if (op == 0x09u) { // XSDRTDO (TDI + TDO expected)
                if (xsdrSizeBits == 0u) return false;
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb > (size - pos) / 2u) return false;
                tap.ShiftBytesBE(0x04u, xenddr ? 0x06u : 0x01u, xsdrSizeBits,
                    data + pos, data + pos + nb, &tdoMask, true);
                pos += nb * 2u;
                totalShiftedBits += xsdrSizeBits;
                ++shiftOrStateCmds;
            } else if (op == 0x0Au) { // XSETSDRMASKS (addrMask + dataMask)
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb > (size - pos) / 2u) return false;
                const uint8_t* dMask = data + pos + nb;
                dataMaskOnes = 0;
                for (size_t i = 0; i < nb; ++i) {
                    uint8_t mb = dMask[i];
                    if (i == 0 && (xsdrSizeBits & 7u) != 0u) {
                        mb &= static_cast<uint8_t>((1u << (xsdrSizeBits & 7u)) - 1u);
                    }
                    dataMaskOnes += popcntByte(mb);
                }
                pos += nb * 2u;
            } else if (op == 0x0Bu) { // XSDRINC (startVal + 1B numTimes + numTimes * bytes(dataMaskOnes))
                if (xsdrSizeBits == 0u) return false;
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb + 1u > size - pos) return false;
                const uint8_t* startVal = data + pos;
                pos += nb;
                uint8_t numTimes = data[pos++];
                size_t incBytes = bytesForBits(dataMaskOnes);
                if (incBytes > 0u && static_cast<size_t>(numTimes) > (size - pos) / incBytes) return false;
                tap.ShiftBytesBE(0x04u, xenddr ? 0x06u : 0x01u, xsdrSizeBits, startVal, nullptr, nullptr, true);
                pos += static_cast<size_t>(numTimes) * incBytes;
                totalShiftedBits += static_cast<uint64_t>(xsdrSizeBits) * (1ULL + static_cast<uint64_t>(numTimes));
                ++shiftOrStateCmds;
            } else if (op == 0x0Cu || op == 0x0Du || op == 0x0Eu) { // XSDRB, XSDRC, XSDRE
                if (xsdrSizeBits == 0u) return false;
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb > size - pos) return false;
                bool exitOnLast = (op == 0x0Eu);
                tap.ShiftBytesBE(0x04u, xenddr ? 0x06u : 0x01u, xsdrSizeBits, data + pos, nullptr, nullptr, exitOnLast);
                pos += nb;
                totalShiftedBits += xsdrSizeBits;
                ++shiftOrStateCmds;
            } else if (op == 0x0Fu || op == 0x10u || op == 0x11u) { // XSDRTDOB, XSDRTDOC, XSDRTDOE
                if (xsdrSizeBits == 0u) return false;
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb > (size - pos) / 2u) return false;
                bool exitOnLast = (op == 0x11u);
                tap.ShiftBytesBE(0x04u, xenddr ? 0x06u : 0x01u, xsdrSizeBits,
                    data + pos, data + pos + nb, &tdoMask, exitOnLast);
                pos += nb * 2u;
                totalShiftedBits += xsdrSizeBits;
                ++shiftOrStateCmds;
            } else if (op == 0x12u) { // XSTATE (1B TAP state 0x00..0x0F)
                if (1u > size - pos) return false;
                uint8_t st = data[pos++];
                if (st > 0x0Fu) return false;
                tap.StepTo(st, true);
                ++shiftOrStateCmds;
            } else if (op == 0x13u) { // XENDIR (1B: 0 or 1)
                if (1u > size - pos) return false;
                uint8_t v = data[pos++];
                if (v > 1u) return false;
                xendir = v;
            } else if (op == 0x14u) { // XENDDR (1B: 0 or 1)
                if (1u > size - pos) return false;
                uint8_t v = data[pos++];
                if (v > 1u) return false;
                xenddr = v;
            } else if (op == 0x15u) { // XSIR2 (2B uint16 BE lengthBits + TDI)
                if (2u > size - pos) return false;
                uint16_t lenBits = DetailEda200::RdU16BE(data + pos);
                pos += 2u;
                if (lenBits == 0u) return false;
                size_t nb = bytesForBits(lenBits);
                if (nb > size - pos) return false;
                tap.ShiftBytesBE(0x0Bu, xendir ? 0x0Du : 0x01u, lenBits, data + pos, nullptr, nullptr, true);
                pos += nb;
                totalShiftedBits += lenBits;
                ++shiftOrStateCmds;
            } else if (op == 0x16u) { // XCOMMENT (NUL-terminated ASCII string)
                size_t nulPos = pos;
                while (nulPos < size && data[nulPos] != 0u) ++nulPos;
                if (nulPos >= size) return false;
                if (firstComment.empty() && nulPos > pos) {
                    size_t take = std::min<size_t>(nulPos - pos, 48u);
                    firstComment.assign(reinterpret_cast<const char*>(data + pos), take);
                }
                pos = nulPos + 1u;
            } else if (op == 0x17u) { // XWAIT (1B waitState + 1B endState + 4B uint32 BE waitTimeUs)
                if (6u > size - pos) return false;
                uint8_t waitSt = data[pos];
                uint8_t endSt  = data[pos + 1];
                uint32_t waitUs = DetailEda200::RdU32BE(data + pos + 2);
                pos += 6u;
                if (waitSt > 0x0Fu || endSt > 0x0Fu) return false;
                tap.EmitIdleClocks(waitUs > 0u ? 4u : 1u, waitSt);
                tap.StepTo(endSt, false);
                ++shiftOrStateCmds;
            } else if (op == 0x18u) { // XWAITSTATE (Lattice/ispVM: 1B waitState + 1B endState + 4B clocks + 4B usec)
                if (10u > size - pos) return false;
                uint8_t waitSt = data[pos];
                uint8_t endSt  = data[pos + 1];
                uint32_t clks  = DetailEda200::RdU32BE(data + pos + 2);
                pos += 10u;
                if (waitSt > 0x0Fu || endSt > 0x0Fu) return false;
                tap.EmitIdleClocks(std::max<uint32_t>(clks, 1u), waitSt);
                tap.StepTo(endSt, false);
                ++shiftOrStateCmds;
            } else {
                // Unknown or reserved opcode (0x05, 0x06, >= 0x19) -> strictly reject
                return false;
            }
        }

        if (shiftOrStateCmds == 0 || tap.cycles.empty()) return false;

        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"Xilinx XAPP503 Binary JTAG Scan Vector (.xsvf)";
        std::wstring summary = L"XAPP503 XSVF (" + std::to_wstring(totalOpcodes) + L" ops, " +
                               std::to_wstring(totalShiftedBits) + L" bits)";
        if (!firstComment.empty()) {
            summary += L" - " + PathUtils::Utf8ToWide(firstComment);
        }
        DetailEda200::RenderJtag4LaneWaveforms(out, tap.cycles, summary);
        return true;
    }
    return false;
}

} // namespace Formats
