#pragma once
// SocketCanLog.h — Linux SocketCAN can-utils candump Log (.candump) reader
// Supports:
//   1. Standard candump -l / -L logfile format:
//      (1700000100.012345) can0 7E8#04410C1AF8000000 [R|T]
//      (1700000100.025000) can0 1F0##10102030405060708090A0B0C  (CAN FD with flags nibble)
//      (1700000100.030000) vcan0 18DAF110#064100BE3FA81300      (29-bit extended ID)
//      (1700000100.035000) can0 123#R4                          (Remote Transmission Request)
//   2. Human-readable terminal candump output:
//      (1700000100.012345)  can0  7E8   [8]  04 41 0C 1A F8 00 00 00
//      can0  18DAF110  [8]  06 41 00 BE 3F A8 13 00
// Included inside namespace Explorer2 in EdaReadersVlsiWave.cpp (do NOT put #include <...> here).

namespace Formats {

namespace DetailSocketCan {

inline bool ParseInterfaceChannel(std::string_view iface, bool hasTimestampOrHash, uint16_t& outChannel) {
    if (iface.empty() || iface.size() > 16) return false;
    for (char c : iface) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-') return false;
    }

    // Extract trailing digits as channel number (can0 -> 0+1=1, vcan2 -> 3)
    size_t digitPos = iface.size();
    while (digitPos > 0 && std::isdigit(static_cast<unsigned char>(iface[digitPos - 1]))) {
        --digitPos;
    }
    std::string_view stem = iface.substr(0, digitPos);
    const bool knownPrefix =
        ReaderText::AsciiEqCI(stem, "can")    ||
        ReaderText::AsciiEqCI(stem, "vcan")   ||
        ReaderText::AsciiEqCI(stem, "slcan")  ||
        ReaderText::AsciiEqCI(stem, "vxcan")  ||
        ReaderText::AsciiEqCI(stem, "elmcan") ||
        ReaderText::AsciiEqCI(stem, "pcan")   ||
        ReaderText::AsciiEqCI(stem, "mcp")    ||
        ReaderText::AsciiEqCI(stem, "canfd");

    if (!knownPrefix && !hasTimestampOrHash) return false;

    uint64_t chIdx = 0;
    if (digitPos < iface.size() && ReaderText::ParseUint64Sv(iface.substr(digitPos), chIdx, 10) && chIdx < 255) {
        outChannel = static_cast<uint16_t>(chIdx + 1u);
    } else {
        outChannel = 1;
    }
    return true;
}

inline bool ParseCanIdHex(std::string_view hexId, uint32_t& outId, bool& outExt) {
    if (hexId.empty() || hexId.size() > 8) return false;
    for (char c : hexId) {
        if (ReaderText::HexVal(c) < 0) return false;
    }
    uint64_t v = 0;
    if (!ReaderText::ParseUint64Sv(hexId, v, 16)) return false;
    if (v > 0x1FFFFFFFULL) return false;
    outId = static_cast<uint32_t>(v);
    outExt = (hexId.size() > 3) || (v > 0x7FFULL);
    if (!outExt && v > 0x7FFULL) return false;
    return true;
}

inline bool ParseCompactPayload(std::string_view frameTok, DetailCan::CanFrame& frame) {
    const size_t hashPos = frameTok.find('#');
    if (hashPos == std::string_view::npos || hashPos == 0) return false;

    if (!ParseCanIdHex(frameTok.substr(0, hashPos), frame.canId, frame.extId)) return false;

    std::string_view rhs = frameTok.substr(hashPos + 1);
    if (!rhs.empty() && rhs.front() == '#') {
        // CAN FD: <id>##<flagsHexNibble><hexBytes>
        frame.isFd = true;
        rhs.remove_prefix(1);
        if (rhs.empty() || ReaderText::HexVal(rhs.front()) < 0) return false;
        rhs.remove_prefix(1); // consume 4-bit CAN FD flags nibble
    } else if (!rhs.empty() && (rhs.front() == 'R' || rhs.front() == 'r')) {
        // Classic CAN RTR: <id>#R or <id>#R<dlc>
        rhs.remove_prefix(1);
        if (!rhs.empty()) {
            if (rhs.size() != 1) return false;
            const int dlc = ReaderText::HexVal(rhs[0]);
            if (dlc < 0 || dlc > 8) return false;
            frame.dlcBytes = static_cast<uint8_t>(dlc);
        }
        return true;
    }

    // Optional dot separators (some candump variants write DE.AD.BE.EF)
    std::string hexClean;
    hexClean.reserve(rhs.size());
    for (char c : rhs) {
        if (c == '.') continue;
        if (ReaderText::HexVal(c) < 0) return false;
        hexClean.push_back(c);
    }

    const size_t maxBytes = frame.isFd ? 64u : 8u;
    if ((hexClean.size() & 1u) != 0 || (hexClean.size() / 2u) > maxBytes) return false;

    frame.dlcBytes = static_cast<uint8_t>(hexClean.size() / 2u);
    for (size_t i = 0; i < frame.dlcBytes; ++i) {
        const int hi = ReaderText::HexVal(hexClean[i * 2u]);
        const int lo = ReaderText::HexVal(hexClean[i * 2u + 1u]);
        frame.data[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

} // namespace DetailSocketCan

inline bool ParseSocketCanLog(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 8 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    std::vector<DetailCan::CanFrame> frames;
    double syntheticTs = 0.0;

    size_t pos = 0;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;

        DetailCan::CanFrame f{};
        bool hasExplicitTs = false;

        // Optional leading timestamp `(1700000100.012345)`
        if (line.front() == '(') {
            const size_t rp = line.find(')');
            if (rp == std::string_view::npos || rp < 2) return false;
            std::string_view tsInner = ReaderText::TrimSv(line.substr(1, rp - 1));
            if (!ReaderText::ParseDoubleSv(tsInner, f.timestamp) || !std::isfinite(f.timestamp) || f.timestamp < 0.0) {
                return false;
            }
            hasExplicitTs = true;
            line = ReaderText::TrimSv(line.substr(rp + 1));
        } else {
            f.timestamp = syntheticTs;
            syntheticTs += 0.010;
        }

        std::string_view rem = line, ifaceTok, secondTok;
        if (!ReaderText::NextTokenSv(rem, ifaceTok) || !ReaderText::NextTokenSv(rem, secondTok)) {
            return false;
        }

        const bool isCompactHash = (secondTok.find('#') != std::string_view::npos);
        if (!DetailSocketCan::ParseInterfaceChannel(ifaceTok, hasExplicitTs || isCompactHash, f.channel)) {
            return false;
        }

        if (isCompactHash) {
            if (!DetailSocketCan::ParseCompactPayload(secondTok, f)) return false;
            std::string_view dirTok, extraTok;
            if (ReaderText::NextTokenSv(rem, dirTok)) {
                if (dirTok == "T" || dirTok == "Tx" || dirTok == "tx") f.isTx = true;
                else if (dirTok != "R" && dirTok != "Rx" && dirTok != "rx") return false;
                if (ReaderText::NextTokenSv(rem, extraTok)) return false;
            }
        } else {
            // Human-readable terminal candump format: <iface> <canIdHex> [len] <B0> <B1> ...
            if (!DetailSocketCan::ParseCanIdHex(secondTok, f.canId, f.extId)) return false;

            std::string_view lenTok;
            if (!ReaderText::NextTokenSv(rem, lenTok)) return false;
            // Handle `[8]` or `[08]` or `[ 8]`
            std::string bracketStr(lenTok);
            while (!bracketStr.empty() && bracketStr.back() != ']') {
                std::string_view nxt;
                if (!ReaderText::NextTokenSv(rem, nxt)) return false;
                bracketStr.append(nxt);
                if (bracketStr.size() > 8) return false;
            }
            if (bracketStr.size() < 3 || bracketStr.front() != '[' || bracketStr.back() != ']') return false;
            uint64_t byteLen = 0;
            if (!ReaderText::ParseUint64Sv(
                    ReaderText::TrimSv(std::string_view(bracketStr).substr(1, bracketStr.size() - 2)), byteLen, 10) ||
                byteLen > 64ULL) {
                return false;
            }
            f.dlcBytes = static_cast<uint8_t>(byteLen);
            if (byteLen > 8ULL) f.isFd = true;

            std::string_view bTok;
            if (ReaderText::NextTokenSv(rem, bTok)) {
                if (ReaderText::AsciiEqCI(bTok, "remote") || ReaderText::AsciiEqCI(bTok, "rtr")) {
                    // Remote request frame has no data bytes
                } else {
                    size_t bi = 0;
                    do {
                        // Stop at optional trailing ASCII dump `'....'`
                        if (!bTok.empty() && (bTok.front() == '\'' || bTok.front() == '|')) break;
                        if (bTok.size() != 2 || bi >= byteLen) return false;
                        const int hi = ReaderText::HexVal(bTok[0]);
                        const int lo = ReaderText::HexVal(bTok[1]);
                        if (hi < 0 || lo < 0) return false;
                        f.data[bi++] = static_cast<uint8_t>((hi << 4) | lo);
                    } while (ReaderText::NextTokenSv(rem, bTok));
                    if (bi != byteLen) return false;
                }
            } else if (byteLen != 0) {
                return false;
            }
        }

        if (frames.size() < 32768) {
            frames.push_back(f);
        }
    }

    if (frames.empty()) return false;
    return DetailCan::RenderCanFramesToBoard(frames, L"SocketCAN candump Log", out);
}

} // namespace Formats
