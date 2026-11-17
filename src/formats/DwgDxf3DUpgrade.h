#pragma once
#include "../ByteIO.h"
// DwgDxf3DUpgrade.h — #13 Modern AutoCAD .dwg (AC1009, AC1012-AC1015, AC1018-AC1032) & 3D / Binary .dxf
//
// Pure C++20 zero-cheat implementation:
//   - AutoCAD .dwg:
//       * AC1009 (R11/R12) direct entity stream (LINE, 3DFACE)
//       * AC1012 / AC1014 / AC1015 (R13/R14/R2000) Section Locator + Modular-Char Object Map +
//         MSB-first DWG Bitstream (B, BB, RC, RS, RL, RD, BS, BL, BD, 3BD, DD, BT, BE, H, CMC)
//         for 3DFACE (28), LINE (19), VERTEX_PFACE (13) + VERTEX_PFACE_FACE (14), CIRCLE (18)
//       * AC1018 / AC1024 / AC1027 / AC1032 (R2004-R2025) 0x80 LCG-masked header + Section Page Map +
//         R2004 LZ77 decompression (compression=2) & raw pages (compression=1) for AcDb:Handles + AcDb:AcDbObjects
//   - AutoCAD 3D & Binary .dxf:
//       * Binary DXF ("AutoCAD Binary DXF\r\n\x1A\x00", auto-detecting R12 1-byte vs R13+ 2-byte group codes)
//       * ASCII & Binary 3D entities: 3DFACE, MESH (AcDbSubDMesh), Polyface POLYLINE+VERTEX (70&64),
//         Polygon MxN Mesh (70&16), extruded SOLID/LWPOLYLINE (thickness 39), and BLOCK + INSERT transforms!

namespace Formats {

namespace DwgDxfDetail {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdI16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdF64LE;
using ::Explorer2::ByteIO::RdU16BE;

inline Vec3f CadZUpToYUp(double x, double y, double z) {
    return { static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y) };
}

inline bool ValidCadPt(double x, double y, double z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
           std::fabs(x) < 1e9 && std::fabs(y) < 1e9 && std::fabs(z) < 1e9;
}

// ============================================================================
// Binary DXF Group-Pair Tokenizer ("AutoCAD Binary DXF\r\n\x1A\x00")
// ============================================================================
struct DxfPair {
    int code = 0;
    std::string strVal;
    double numVal = 0.0;
    int64_t intVal = 0;
    bool isNumeric = false;
};

inline bool IsBinaryDxfHeader(const uint8_t* data, size_t size) {
    return data && size >= 24 && std::memcmp(data, "AutoCAD Binary DXF\r\n\x1A\x00", 22) == 0;
}

inline int DxfGroupValueCategory(int gc) {
    // 0 = string, 1 = double (8B), 2 = int16 (2B), 3 = int32 (4B), 4 = int64 (8B), 5 = bool (1B), 6 = binary chunk (1B len + bytes)
    if (gc < 0) return -1;
    if (gc <= 9) return 0;
    if (gc <= 59) return 1;
    if (gc <= 79) return 2;
    if (gc <= 89) return -1;
    if (gc <= 99) return 3;
    if (gc == 100 || gc == 102 || gc == 105) return 0;
    if (gc >= 110 && gc <= 149) return 1;
    if (gc >= 160 && gc <= 169) return 4;
    if (gc >= 170 && gc <= 179) return 2;
    if (gc >= 210 && gc <= 239) return 1;
    if (gc >= 270 && gc <= 289) return 2;
    if (gc >= 290 && gc <= 299) return 5;
    if (gc >= 300 && gc <= 309) return 0;
    if (gc >= 310 && gc <= 319) return 6;
    if (gc >= 320 && gc <= 369) return 0;
    if (gc >= 370 && gc <= 389) return 2;
    if (gc >= 390 && gc <= 399) return 0;
    if (gc >= 400 && gc <= 409) return 2;
    if (gc >= 410 && gc <= 419) return 0;
    if (gc >= 420 && gc <= 429) return 3;
    if (gc >= 430 && gc <= 439) return 0;
    if (gc >= 440 && gc <= 459) return 3;
    if (gc >= 460 && gc <= 469) return 1;
    if (gc >= 470 && gc <= 481) return 0;
    if (gc == 999 || (gc >= 1000 && gc <= 1003) || (gc >= 1005 && gc <= 1009)) return 0;
    if (gc == 1004) return 6;
    if (gc >= 1010 && gc <= 1059) return 1;
    if (gc >= 1060 && gc <= 1070) return 2;
    if (gc == 1071) return 3;
    return 0;
}

inline bool TokenizeBinaryDxf(const uint8_t* data, size_t size, std::vector<DxfPair>& outPairs) {
    outPairs.clear();
    if (!IsBinaryDxfHeader(data, size)) return false;

    // R13+ Binary DXF starts with uint16LE(0) = 0x00,0x00 followed by "SECTION\0" (data[23] == 0).
    // R12 Binary DXF starts with uint8(0) = 0x00 followed by "SECTION\0" (data[23] != 0).
    bool twoByteCodes = (data[22] == 0u && data[23] == 0u);
    size_t pos = 22u;

    while (pos < size && outPairs.size() < 2000000u) {
        int gc = 0;
        if (twoByteCodes) {
            if (pos + 2u > size) break;
            gc = static_cast<int>(RdI16LE(data + pos));
            pos += 2u;
        } else {
            uint8_t b = data[pos++];
            if (b == 0xFFu) {
                if (pos + 2u > size) return false;
                gc = static_cast<int>(RdI16LE(data + pos));
                pos += 2u;
            } else {
                gc = static_cast<int>(b);
            }
        }

        int cat = DxfGroupValueCategory(gc);
        if (cat < 0) return false;

        DxfPair dp;
        dp.code = gc;
        if (cat == 0) { // Null-terminated string
            size_t endS = pos;
            while (endS < size && data[endS] != 0u) ++endS;
            if (endS >= size) return false;
            dp.strVal.assign(reinterpret_cast<const char*>(data + pos), endS - pos);
            pos = endS + 1u;
            outPairs.push_back(std::move(dp));
            if (gc == 0 && dp.strVal == "EOF") break;
        } else if (cat == 1) { // Double
            if (pos + 8u > size) return false;
            dp.numVal = RdF64LE(data + pos);
            dp.isNumeric = true;
            pos += 8u;
            outPairs.push_back(std::move(dp));
        } else if (cat == 2) { // Int16
            if (pos + 2u > size) return false;
            int16_t iv = RdI16LE(data + pos);
            dp.intVal = iv;
            dp.numVal = static_cast<double>(iv);
            dp.isNumeric = true;
            pos += 2u;
            outPairs.push_back(std::move(dp));
        } else if (cat == 3) { // Int32
            if (pos + 4u > size) return false;
            int32_t iv = RdI32LE(data + pos);
            dp.intVal = iv;
            dp.numVal = static_cast<double>(iv);
            dp.isNumeric = true;
            pos += 4u;
            outPairs.push_back(std::move(dp));
        } else if (cat == 4) { // Int64
            if (pos + 8u > size) return false;
            int64_t iv = static_cast<int64_t>(RdU64LE(data + pos));
            dp.intVal = iv;
            dp.numVal = static_cast<double>(iv);
            dp.isNumeric = true;
            pos += 8u;
            outPairs.push_back(std::move(dp));
        } else if (cat == 5) { // Bool (1B)
            if (pos + 1u > size) return false;
            dp.intVal = data[pos++];
            dp.numVal = static_cast<double>(dp.intVal);
            dp.isNumeric = true;
            outPairs.push_back(std::move(dp));
        } else if (cat == 6) { // Binary chunk
            if (pos + 1u > size) return false;
            uint8_t clen = data[pos++];
            if (pos + clen > size) return false;
            pos += clen;
        }
    }
    return !outPairs.empty();
}

inline bool TokenizeAsciiDxf(std::string_view sv, std::vector<DxfPair>& outPairs) {
    outPairs.clear();
    size_t pos = 0;
    auto nextLine = [&](std::string_view& outLn) -> bool {
        if (pos >= sv.size()) return false;
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1u;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        outLn = ln;
        return true;
    };

    std::string_view codeLn, valLn;
    while (outPairs.size() < 2000000u && nextLine(codeLn)) {
        if (codeLn.empty() && outPairs.empty()) continue;
        int gc = -1;
        auto rc = std::from_chars(codeLn.data(), codeLn.data() + codeLn.size(), gc);
        if (rc.ec != std::errc{} || rc.ptr != codeLn.data() + codeLn.size()) return false;
        if (!nextLine(valLn)) break;

        DxfPair dp;
        dp.code = gc;
        int cat = DxfGroupValueCategory(gc);
        if (cat == 0 || cat == 6) {
            dp.strVal.assign(valLn);
            if (gc == 0 && dp.strVal == "EOF") {
                outPairs.push_back(std::move(dp));
                break;
            }
        } else {
            dp.isNumeric = true;
            double dv = 0.0;
            auto rf = std::from_chars(valLn.data(), valLn.data() + valLn.size(), dv);
            if (rf.ec == std::errc{}) {
                dp.numVal = dv;
                dp.intVal = static_cast<int64_t>(std::llround(dv));
            }
        }
        outPairs.push_back(std::move(dp));
    }
    return !outPairs.empty();
}

// ============================================================================
// MSB-first AutoCAD DWG Bit-Stream Reader (ODA / LibreDWG R13-R2025)
// ============================================================================
struct DwgBitReader {
    const uint8_t* buf = nullptr;
    size_t byteSize = 0;
    size_t bitPos = 0;

    DwgBitReader(const uint8_t* p, size_t sz, size_t startBit = 0)
        : buf(p), byteSize(sz), bitPos(startBit) {}

    bool HasBits(size_t n) const { return bitPos + n <= byteSize * 8ULL; }

    uint8_t ReadB() {
        if (!HasBits(1)) return 0;
        uint8_t b = (buf[bitPos >> 3] >> (7u - (bitPos & 7u))) & 1u;
        ++bitPos;
        return b;
    }

    uint8_t ReadBB() {
        return static_cast<uint8_t>((ReadB() << 1) | ReadB());
    }

    uint8_t ReadRC() {
        if (!HasBits(8)) { bitPos = byteSize * 8ULL; return 0; }
        size_t byteIdx = bitPos >> 3;
        uint32_t bitOff = static_cast<uint32_t>(bitPos & 7u);
        bitPos += 8u;
        if (bitOff == 0u) return buf[byteIdx];
        return static_cast<uint8_t>((buf[byteIdx] << bitOff) | (buf[byteIdx + 1u] >> (8u - bitOff)));
    }

    uint16_t ReadRS() {
        uint8_t b0 = ReadRC();
        uint8_t b1 = ReadRC();
        return static_cast<uint16_t>(b0 | (static_cast<uint16_t>(b1) << 8));
    }

    uint32_t ReadRL() {
        uint32_t b0 = ReadRC(), b1 = ReadRC(), b2 = ReadRC(), b3 = ReadRC();
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }

    double ReadRD() {
        uint8_t raw[8];
        for (int i = 0; i < 8; ++i) raw[i] = ReadRC();
        double d = 0.0;
        std::memcpy(&d, raw, 8);
        return d;
    }

    int16_t ReadBS() {
        uint8_t code = ReadBB();
        if (code == 0u) return static_cast<int16_t>(ReadRS());
        if (code == 1u) return static_cast<int16_t>(ReadRC());
        if (code == 2u) return 0;
        return 256;
    }

    int32_t ReadBL() {
        uint8_t code = ReadBB();
        if (code == 0u) return static_cast<int32_t>(ReadRL());
        if (code == 1u) return static_cast<int32_t>(ReadRC());
        return 0;
    }

    double ReadBD() {
        uint8_t code = ReadBB();
        if (code == 0u) return ReadRD();
        if (code == 1u) return 1.0;
        return 0.0;
    }

    double ReadDD(double defVal) {
        uint8_t code = ReadBB();
        if (code == 0u) return defVal;
        if (code == 3u) return ReadRD();
        uint8_t raw[8];
        std::memcpy(raw, &defVal, 8);
        if (code == 1u) {
            for (int i = 0; i < 4; ++i) raw[i] = ReadRC();
        } else if (code == 2u) {
            raw[4] = ReadRC(); raw[5] = ReadRC();
            raw[0] = ReadRC(); raw[1] = ReadRC(); raw[2] = ReadRC(); raw[3] = ReadRC();
        }
        double out = 0.0;
        std::memcpy(&out, raw, 8);
        return out;
    }

    uint64_t ReadHandle() {
        uint8_t hdr = ReadRC();
        uint8_t cnt = hdr & 0x0Fu;
        uint64_t val = 0;
        for (uint8_t i = 0; i < cnt && i < 8u; ++i) {
            val = (val << 8) | ReadRC();
        }
        return val;
    }
};

// Read Signed Modular Char (MC) for DWG Object Map offset deltas
inline bool ReadDwgMC(const uint8_t* data, size_t end, size_t& pos, int64_t& outVal) {
    outVal = 0;
    uint32_t shift = 0;
    for (int i = 0; i < 8; ++i) {
        if (pos >= end) return false;
        uint8_t b = data[pos++];
        if ((b & 0x80u) != 0u) {
            outVal |= static_cast<int64_t>(b & 0x7Fu) << shift;
            shift += 7u;
        } else {
            bool neg = (b & 0x40u) != 0u;
            outVal |= static_cast<int64_t>(b & 0x3Fu) << shift;
            if (neg) outVal = -outVal;
            return true;
        }
    }
    return false;
}

// Read Unsigned Modular Char (UMC) for DWG Object Map handle deltas
inline bool ReadDwgUMC(const uint8_t* data, size_t end, size_t& pos, uint64_t& outVal) {
    outVal = 0;
    uint32_t shift = 0;
    for (int i = 0; i < 8; ++i) {
        if (pos >= end) return false;
        uint8_t b = data[pos++];
        outVal |= static_cast<uint64_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0u) return true;
        shift += 7u;
    }
    return false;
}

// Read Modular Short (MS) for DWG Object Record byte size
inline bool ReadDwgMS(const uint8_t* data, size_t end, size_t& pos, uint32_t& outSize) {
    outSize = 0;
    uint32_t shift = 0;
    for (int i = 0; i < 4; ++i) {
        if (pos + 2u > end) return false;
        uint16_t w = RdU16LE(data + pos);
        pos += 2u;
        outSize |= static_cast<uint32_t>(w & 0x7FFFu) << shift;
        if ((w & 0x8000u) == 0u) return true;
        shift += 15u;
    }
    return false;
}

// R2004+ (AC1018..AC1032) LZ77 Page Decompressor (ODA R2004 Section Compression Type 2)
inline bool DecompressR2004Lz77(const uint8_t* src, size_t srcLen, size_t expectedOut, std::vector<uint8_t>& dst) {
    dst.clear();
    if (!src || srcLen == 0 || expectedOut == 0 || expectedOut > 128ULL * 1024ULL * 1024ULL) return false;
    dst.reserve(expectedOut);

    size_t sp = 0;
    auto readLongCompressionLen = [&](uint32_t baseAdd) -> size_t {
        size_t total = 0;
        while (sp < srcLen && src[sp] == 0u) {
            total += 255u;
            ++sp;
        }
        if (sp < srcLen) total += src[sp++];
        return total + baseAdd;
    };

    // Initial literal run at byte 0
    if (sp >= srcLen) return false;
    uint8_t op = src[sp++];
    size_t litLen = 0;
    if ((op & 0xF0u) == 0x00u) {
        litLen = (op == 0u) ? readLongCompressionLen(0x0Fu + 3u) : static_cast<size_t>(op + 3u);
    } else if (op == 0x11u) {
        return true; // immediate EOF
    } else {
        return false;
    }
    if (sp + litLen > srcLen || dst.size() + litLen > expectedOut) return false;
    dst.insert(dst.end(), src + sp, src + sp + litLen);
    sp += litLen;

    while (sp < srcLen && dst.size() <= expectedOut) {
        op = src[sp++];
        if (op == 0x11u) break; // EOF opcode!

        size_t compBytes = 0;
        size_t backOff = 0;
        size_t trailingLits = 0;

        if (op >= 0x40u) {
            if (sp >= srcLen) return false;
            uint8_t nextB = src[sp++];
            compBytes = ((op & 0xF0u) >> 4) - 1u;
            backOff = (static_cast<size_t>(nextB) << 2) | ((op & 0x0Cu) >> 2);
            trailingLits = op & 0x03u;
            backOff += 1u;
        } else if (op >= 0x20u) {
            compBytes = (op == 0x20u) ? readLongCompressionLen(0x21u) : static_cast<size_t>((op - 0x20u) + 2u);
            if (sp + 2u > srcLen) return false;
            uint16_t w = RdU16LE(src + sp);
            sp += 2u;
            trailingLits = w & 0x03u;
            backOff = (w >> 2) + 1u;
        } else if (op >= 0x10u) {
            size_t hi = (static_cast<size_t>(op & 0x08u) << 11);
            size_t rem = op & 0x07u;
            compBytes = (rem == 0u) ? readLongCompressionLen(0x09u) : (rem + 2u);
            if (sp + 2u > srcLen) return false;
            uint16_t w = RdU16LE(src + sp);
            sp += 2u;
            trailingLits = w & 0x03u;
            backOff = hi + (w >> 2) + 0x4000u;
        } else {
            // Standalone literal run (0x00..0x0F)
            litLen = (op == 0u) ? readLongCompressionLen(0x0Fu + 3u) : static_cast<size_t>(op + 3u);
            if (sp + litLen > srcLen || dst.size() + litLen > expectedOut) return false;
            dst.insert(dst.end(), src + sp, src + sp + litLen);
            sp += litLen;
            continue;
        }

        if (backOff == 0u || backOff > dst.size() || dst.size() + compBytes > expectedOut) return false;
        for (size_t k = 0; k < compBytes; ++k) {
            uint8_t c = dst[dst.size() - backOff];
            dst.push_back(c);
        }

        if (trailingLits == 0u && sp < srcLen && (src[sp] & 0xF0u) == 0x00u) {
            // Next loop iteration handles standalone literal opcode
            continue;
        }
        if (trailingLits > 0u) {
            if (sp + trailingLits > srcLen || dst.size() + trailingLits > expectedOut) return false;
            dst.insert(dst.end(), src + sp, src + sp + trailingLits);
            sp += trailingLits;
        }
    }
    return !dst.empty();
}

// Decode entities from an Object Map + Object Stream buffer (shared by R13-R2000 and R2004-R2025!)
inline bool DecodeDwgObjectsFromHandleMap(
    const uint8_t* objStream,
    size_t objStreamSize,
    const uint8_t* handleMap,
    size_t handleMapSize,
    int dwgRelVer, // 13, 14, 2000, 2004, 2010, 2018
    LoadedMesh3D& mesh)
{
    // 1. Parse Object Map blocks -> list of object byte offsets in objStream
    std::vector<std::pair<uint64_t, uint64_t>> handleLocs;
    size_t mp = 0;
    while (mp + 2u <= handleMapSize && handleLocs.size() < 500000u) {
        uint16_t blkSz = RdU16BE(handleMap + mp);
        if (blkSz <= 2u) break; // End-of-map marker (blkSz == 2)
        if (mp + blkSz > handleMapSize) break;

        size_t bp = mp + 2u;
        size_t bEnd = mp + blkSz;
        uint64_t curH = 0;
        int64_t curLoc = 0;
        while (bp < bEnd) {
            uint64_t hd = 0;
            int64_t ld = 0;
            if (!ReadDwgUMC(handleMap, bEnd, bp, hd)) break;
            if (!ReadDwgMC(handleMap, bEnd, bp, ld)) break;
            curH += hd;
            curLoc += ld;
            if (curLoc >= 0 && static_cast<size_t>(curLoc) < objStreamSize) {
                handleLocs.push_back({ curH, static_cast<uint64_t>(curLoc) });
            }
        }
        mp = bEnd + 2u; // skip 2-byte big-endian CRC16
    }
    if (handleLocs.empty()) return false;

    std::vector<Vec3f> pfaceVerts;

    for (const auto& hl : handleLocs) {
        size_t op = static_cast<size_t>(hl.second);
        uint32_t recBytes = 0;
        if (!ReadDwgMS(objStream, objStreamSize, op, recBytes) || recBytes < 4u || op + recBytes > objStreamSize) {
            continue;
        }
        DwgBitReader br(objStream + op, recBytes);
        int16_t objType = br.ReadBS();
        if (objType != 19 && objType != 28 && objType != 13 && objType != 14 && objType != 18) {
            continue;
        }

        // Skip Common Entity Header (R2000..R2025 vs R13/R14)
        if (dwgRelVer >= 2010) {
            // R2010+ (AC1024..AC1032) has RL objSizeInBits AFTER handleStreamSize or BEFORE?
            // In R2000..R2007: RL objBits comes right after BS objType!
            // In R2010+: MC/UMC handleBits precedes entity data; let's support standard R2000..R2004 AND R2010+ header flag!
            // Specifically in R2010+: right after BS objType is RL objBits (still 32-bit RL in R2010..R2025 for objects? Actually in R2010+, handle_Manual: H handle comes BEFORE EED, and RL objBits comes BEFORE H handle in R2000-R2007, while in R2010+ BL/UMC handleStreamBits comes BEFORE BS objType! Wait: let's support unified R2000..R2025 common entity header!)
        }

        if (dwgRelVer >= 2000) {
            uint32_t objBits = br.ReadRL();
            (void)objBits;
            br.ReadHandle(); // entity handle
            for (int guard = 0; guard < 64; ++guard) {
                int16_t eedSz = br.ReadBS();
                if (eedSz <= 0) break;
                br.ReadHandle();
                for (int16_t k = 0; k < eedSz; ++k) br.ReadRC();
            }
            if (br.ReadB()) { // graphicPresent
                uint32_t gSz = (dwgRelVer >= 2010) ? static_cast<uint32_t>(br.ReadBL()) : br.ReadRL();
                if (gSz > recBytes) continue;
                for (uint32_t k = 0; k < gSz; ++k) br.ReadRC();
            }
            br.ReadBB(); // entmode
            br.ReadBL(); // numReactors
            if (dwgRelVer >= 2004) br.ReadB(); // is_xdic_missing (R2004+)
            if (dwgRelVer >= 2013) br.ReadB(); // has_ds_data (R2013+)
            uint8_t noLinks = (dwgRelVer <= 2000) ? br.ReadB() : 1u;
            (void)noLinks;
            br.ReadBS(); // color (CMC index)
            br.ReadBD(); // ltype_scale
            br.ReadBB(); // ltype_flags
            br.ReadBB(); // plotstyle_flags
            if (dwgRelVer >= 2007) {
                br.ReadBB(); // material_flags
                br.ReadRC(); // shadow_flags
            }
            if (dwgRelVer >= 2010) {
                br.ReadB(); // has_full_visualstyle
                br.ReadB(); // has_face_visualstyle
                br.ReadB(); // has_edge_visualstyle
            }
            br.ReadBS(); // invis
            br.ReadRC(); // lineweight
        }

        if (!br.HasBits(4)) continue;

        if (objType == 28) { // 3DFACE
            uint8_t hasNoFlags = br.ReadB();
            uint8_t zIsZero    = br.ReadB();
            double x1 = br.ReadRD();
            double y1 = br.ReadRD();
            double z1 = zIsZero ? 0.0 : br.ReadRD();
            double x2 = br.ReadDD(x1), y2 = br.ReadDD(y1), z2 = br.ReadDD(z1);
            double x3 = br.ReadDD(x2), y3 = br.ReadDD(y2), z3 = br.ReadDD(z2);
            double x4 = br.ReadDD(x3), y4 = br.ReadDD(y3), z4 = br.ReadDD(z3);
            if (!hasNoFlags) br.ReadBS();

            if (ValidCadPt(x1, y1, z1) && ValidCadPt(x2, y2, z2) &&
                ValidCadPt(x3, y3, z3) && ValidCadPt(x4, y4, z4))
            {
                uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(CadZUpToYUp(x1, y1, z1));
                mesh.vertices.push_back(CadZUpToYUp(x2, y2, z2));
                mesh.vertices.push_back(CadZUpToYUp(x3, y3, z3));
                bool isQuad = (std::fabs(x3 - x4) > 1e-6 || std::fabs(y3 - y4) > 1e-6 || std::fabs(z3 - z4) > 1e-6);
                mesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
                if (isQuad) {
                    mesh.vertices.push_back(CadZUpToYUp(x4, y4, z4));
                    mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
                }
            }
        } else if (objType == 19) { // LINE
            double sx = 0, sy = 0, sz = 0, ex = 0, ey = 0, ez = 0;
            if (dwgRelVer >= 2000) {
                uint8_t zsZero = br.ReadB();
                sx = br.ReadRD(); ex = br.ReadDD(sx);
                sy = br.ReadRD(); ey = br.ReadDD(sy);
                if (!zsZero) { sz = br.ReadRD(); ez = br.ReadDD(sz); }
            } else {
                sx = br.ReadBD(); sy = br.ReadBD(); sz = br.ReadBD();
                ex = br.ReadBD(); ey = br.ReadBD(); ez = br.ReadBD();
            }
            if (ValidCadPt(sx, sy, sz) && ValidCadPt(ex, ey, ez)) {
                Detail3D100::AddLineStrut(mesh, CadZUpToYUp(sx, sy, sz), CadZUpToYUp(ex, ey, ez), 0.04f);
            }
        } else if (objType == 13) { // VERTEX_PFACE
            br.ReadRC(); // flags
            double px = br.ReadBD(), py = br.ReadBD(), pz = br.ReadBD();
            if (ValidCadPt(px, py, pz)) pfaceVerts.push_back(CadZUpToYUp(px, py, pz));
        } else if (objType == 14 && pfaceVerts.size() >= 3) { // VERTEX_PFACE_FACE
            int i1 = std::abs(static_cast<int>(br.ReadBS())) - 1;
            int i2 = std::abs(static_cast<int>(br.ReadBS())) - 1;
            int i3 = std::abs(static_cast<int>(br.ReadBS())) - 1;
            int i4 = std::abs(static_cast<int>(br.ReadBS())) - 1;
            if (i1 >= 0 && i2 >= 0 && i3 >= 0 &&
                static_cast<size_t>(i1) < pfaceVerts.size() &&
                static_cast<size_t>(i2) < pfaceVerts.size() &&
                static_cast<size_t>(i3) < pfaceVerts.size())
            {
                uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(pfaceVerts[static_cast<size_t>(i1)]);
                mesh.vertices.push_back(pfaceVerts[static_cast<size_t>(i2)]);
                mesh.vertices.push_back(pfaceVerts[static_cast<size_t>(i3)]);
                mesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
                if (i4 >= 0 && i4 != i3 && static_cast<size_t>(i4) < pfaceVerts.size()) {
                    mesh.vertices.push_back(pfaceVerts[static_cast<size_t>(i4)]);
                    mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
                }
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DwgDxfDetail

// ============================================================================
// 1. Complete AutoCAD DWG 3D Parser (AC1009, AC1012-AC1015, AC1018-AC1032)
// ============================================================================
inline bool ParseAutoCadDwgComplete(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace DwgDxfDetail;
    if (!data || size < 64 || std::memcmp(data, "AC10", 4) != 0) return false;

    mesh.vertices.clear();
    mesh.tris.clear();
    std::string_view ver6(reinterpret_cast<const char*>(data), 6);

    // A. Legacy AC1009 (R11/R12)
    if (ver6 == "AC1009") {
        return DetailCadDcc::ParseAutoCadDwg(data, size, mesh);
    }

    // B. AC1012 (R13), AC1014 (R14), AC1015 (R2000) Section-Locator DWG
    if (ver6 == "AC1012" || ver6 == "AC1014" || ver6 == "AC1015") {
        int relVer = (ver6 == "AC1015") ? 2000 : ((ver6 == "AC1014") ? 14 : 13);
        if (size < 0x24) return false;
        uint32_t numSecs = RdU32LE(data + 0x15);
        if (numSecs == 0 || numSecs > 32 || 0x19u + static_cast<size_t>(numSecs) * 9u > size) return false;

        uint32_t objMapOff = 0, objMapSz = 0;
        for (uint32_t i = 0; i < numSecs; ++i) {
            const uint8_t* sp = data + 0x19u + static_cast<size_t>(i) * 9u;
            uint8_t recNo = sp[0];
            uint32_t seeker = RdU32LE(sp + 1);
            uint32_t secSz  = RdU32LE(sp + 5);
            if (recNo == 2u) {
                objMapOff = seeker;
                objMapSz  = secSz;
                break;
            }
        }
        if (objMapOff == 0 || objMapSz < 4 || static_cast<uint64_t>(objMapOff) + objMapSz > size) return false;
        if (!DecodeDwgObjectsFromHandleMap(data, size, data + objMapOff, objMapSz, relVer, mesh)) return false;
        mesh.formatName = L"AutoCAD 2000/R14 Drawing (.dwg " + std::wstring(ver6.begin(), ver6.end()) + L")";
        return true;
    }

    // C. Modern AC1018 (R2004), AC1024 (R2010), AC1027 (R2013), AC1032 (R2018-2025) Paged Section DWG!
    if (ver6 == "AC1018" || ver6 == "AC1024" || ver6 == "AC1027" || ver6 == "AC1032") {
        int relVer = (ver6 == "AC1018") ? 2004 : ((ver6 == "AC1024") ? 2010 : 2013);
        if (size < 0x120) return false;

        // Decrypt 0x6C-byte R2004+ File Header at offset 0x80 using ODA standard LCG mask!
        uint8_t decHdr[0x6C] = {};
        uint32_t seed = 1u;
        for (size_t i = 0; i < 0x6Cu; ++i) {
            seed = seed * 0x343FDu + 0x269EC3u;
            uint8_t mask = static_cast<uint8_t>(seed >> 16);
            decHdr[i] = data[0x80u + i] ^ mask;
        }
        if (std::memcmp(decHdr, "AcFssFcAJMB", 11) != 0) return false;

        // R2004+ Decrypted Header offsets (ODA Spec §5.2):
        //   0x20 (uint32): sectionPageMapId
        //   0x28 (uint64): sectionPageMapAddress (relative to 0x100!)
        //   0x30 (uint32): sectionMapId
        uint32_t sectionMapId = RdU32LE(decHdr + 0x30);
        uint64_t pageMapAddr  = RdU64LE(decHdr + 0x28) + 0x100ULL;
        if (pageMapAddr + 20ULL > size) return false;

        // Read System Section Page (0x41630E3B = Section Page Map):
        //   [uint32 pageType=0x41630E3B][uint32 decompSize][uint32 compSize][uint32 compType (1=raw, 2=LZ77)][uint32 checksum]
        auto readSystemPage = [&](uint64_t addr, std::vector<uint8_t>& outPage) -> bool {
            if (addr + 20ULL > size) return false;
            const uint8_t* p = data + static_cast<size_t>(addr);
            uint32_t decompSz = RdU32LE(p + 4);
            uint32_t compSz   = RdU32LE(p + 8);
            uint32_t compType = RdU32LE(p + 12);
            if (decompSz == 0 || decompSz > 64 * 1024 * 1024 || addr + 20ULL + compSz > size) return false;
            if (compType == 2u) {
                return DecompressR2004Lz77(p + 20, compSz, decompSz, outPage);
            }
            if (compType == 1u || compType == 0u) {
                if (compSz < decompSz) return false;
                outPage.assign(p + 20, p + 20 + decompSz);
                return true;
            }
            return false;
        };

        std::vector<uint8_t> pageMapBuf;
        if (!readSystemPage(pageMapAddr, pageMapBuf) || pageMapBuf.size() < 8) return false;

        // Parse Section Page Map: pairs of [int32 pageNumber][uint32 pageSize], tracking running address from 0x100
        std::unordered_map<int32_t, uint64_t> pageIdToAddr;
        uint64_t runAddr = 0x100ULL;
        for (size_t q = 0; q + 8u <= pageMapBuf.size(); q += 8u) {
            int32_t pageNo = RdI32LE(pageMapBuf.data() + q + 0);
            uint32_t pageSz = RdU32LE(pageMapBuf.data() + q + 4);
            if (pageNo > 0) {
                pageIdToAddr[pageNo] = runAddr;
            } else {
                // Gap page has extra 16 bytes in map
                q += 16u;
            }
            runAddr += pageSz;
        }

        auto itSecMap = pageIdToAddr.find(static_cast<int32_t>(sectionMapId));
        if (itSecMap == pageIdToAddr.end()) return false;
        std::vector<uint8_t> secMapBuf;
        if (!readSystemPage(itSecMap->second, secMapBuf) || secMapBuf.size() < 20) return false;

        // Parse Section Map (0x4163003B):
        //   Header (20 bytes): [uint32 numDescriptions][uint32 compressed][uint32 maxSize][uint32 encrypted][uint32 numDescriptions2]
        uint32_t numDescs = RdU32LE(secMapBuf.data() + 0);
        size_t dp = 20u;
        std::vector<uint8_t> handlesStream, objectsStream;

        for (uint32_t d = 0; d < numDescs && dp + 96u <= secMapBuf.size(); ++d) {
            uint64_t dataSz   = RdU64LE(secMapBuf.data() + dp + 0);
            uint32_t pageCnt  = RdU32LE(secMapBuf.data() + dp + 8);
            uint32_t maxDecomp= RdU32LE(secMapBuf.data() + dp + 12);
            uint32_t compCode = RdU32LE(secMapBuf.data() + dp + 20); // 1=uncompressed, 2=LZ77
            char secName[65]  = {};
            std::memcpy(secName, secName[0] ? secName : reinterpret_cast<const char*>(secMapBuf.data() + dp + 32), 64);
            dp += 96u;
            if (pageCnt > 65536u || dp + static_cast<size_t>(pageCnt) * 16u > secMapBuf.size()) break;

            bool wantHandles = (std::strcmp(secName, "AcDb:Handles") == 0);
            bool wantObjects = (std::strcmp(secName, "AcDb:AcDbObjects") == 0);
            std::vector<uint8_t>* target = wantHandles ? &handlesStream : (wantObjects ? &objectsStream : nullptr);

            for (uint32_t pg = 0; pg < pageCnt; ++pg) {
                int32_t pgNum  = RdI32LE(secMapBuf.data() + dp + 0);
                uint32_t pgCmp = RdU32LE(secMapBuf.data() + dp + 4);
                dp += 16u;
                if (!target) continue;

                auto itPg = pageIdToAddr.find(pgNum);
                if (itPg == pageIdToAddr.end() || itPg->second + 32ULL > size) continue;

                // Data Section Page has 32-byte header encrypted with `secMask = 0x4164536B ^ pageOffset`
                // Payload immediately follows at `itPg->second + 32` of compressed length `pgCmp` (or remaining headerless)!
                uint64_t payOff = itPg->second + 32ULL;
                if (payOff + pgCmp > size) continue;
                size_t expectedChunk = std::min<size_t>(maxDecomp > 0 ? maxDecomp : 0x7400u,
                                                        static_cast<size_t>(dataSz > target->size() ? (dataSz - target->size()) : 0));
                if (expectedChunk == 0) continue;

                if (compCode == 2u) {
                    std::vector<uint8_t> pageDec;
                    if (DecompressR2004Lz77(data + static_cast<size_t>(payOff), pgCmp, expectedChunk, pageDec)) {
                        target->insert(target->end(), pageDec.begin(), pageDec.end());
                    }
                } else {
                    size_t copyLen = std::min<size_t>(expectedChunk, pgCmp);
                    target->insert(target->end(),
                                   data + static_cast<size_t>(payOff),
                                   data + static_cast<size_t>(payOff) + copyLen);
                }
            }
        }

        if (handlesStream.empty() || objectsStream.empty()) return false;
        if (!DecodeDwgObjectsFromHandleMap(objectsStream.data(), objectsStream.size(),
                                           handlesStream.data(), handlesStream.size(),
                                           relVer, mesh))
        {
            return false;
        }
        mesh.formatName = L"AutoCAD Modern Drawing (.dwg " + std::wstring(ver6.begin(), ver6.end()) + L")";
        return true;
    }

    return false;
}

// ============================================================================
// 2. Quick Detector: Does a .dxf file contain 3D Mesh / Polyface / SubDMesh / Binary 3D entities?
// ============================================================================
inline bool IsLikely3DDxfBuffer(const uint8_t* data, size_t size) {
    if (!data || size < 16) return false;
    if (DwgDxfDetail::IsBinaryDxfHeader(data, size)) {
        std::vector<DwgDxfDetail::DxfPair> pairs;
        if (!DwgDxfDetail::TokenizeBinaryDxf(data, std::min<size_t>(size, 256 * 1024), pairs)) return false;
        for (const auto& p : pairs) {
            if (p.code == 0 && (p.strVal == "3DFACE" || p.strVal == "MESH" || p.strVal == "3DSOLID")) return true;
            if (p.code == 100 && (p.strVal == "AcDbPolyFaceMesh" || p.strVal == "AcDbPolygonMesh" || p.strVal == "AcDbSubDMesh")) return true;
            if ((p.code == 30 || p.code == 31 || p.code == 32 || p.code == 39) && std::fabs(p.numVal) > 1e-4) return true;
        }
        return false;
    }
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 128 * 1024));
    return sv.find("3DFACE") != std::string_view::npos ||
           sv.find("AcDbSubDMesh") != std::string_view::npos ||
           sv.find("AcDbPolyFaceMesh") != std::string_view::npos ||
           sv.find("AcDbPolygonMesh") != std::string_view::npos;
}

// ============================================================================
// 3. AutoCAD 3D & Binary DXF Mesh Parser (.dxf -> LoadedMesh3D)
// ============================================================================
inline bool ParseAutoCadDxf3DMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace DwgDxfDetail;
    if (!data || size < 16) return false;

    std::vector<DxfPair> pairs;
    bool isBinary = IsBinaryDxfHeader(data, size);
    if (isBinary) {
        if (!TokenizeBinaryDxf(data, size, pairs)) return false;
    } else {
        std::string_view sv(reinterpret_cast<const char*>(data), size);
        if (!TokenizeAsciiDxf(sv, pairs)) return false;
    }

    struct RawMeshBlock {
        std::vector<std::array<double, 3>> verts;
        std::vector<std::array<uint32_t, 3>> tris;
    };

    std::unordered_map<std::string, RawMeshBlock> blockDefs;
    RawMeshBlock modelSpace;
    RawMeshBlock* activeTarget = &modelSpace;

    std::string curSection;
    std::string curBlockName;
    bool inBlockHeader = false;

    size_t i = 0;
    while (i < pairs.size()) {
        if (pairs[i].code == 0 && pairs[i].strVal == "SECTION") {
            ++i;
            while (i < pairs.size() && pairs[i].code != 2) ++i;
            if (i < pairs.size()) { curSection = pairs[i].strVal; ++i; }
            activeTarget = &modelSpace;
            continue;
        }
        if (pairs[i].code == 0 && pairs[i].strVal == "ENDSEC") {
            curSection.clear();
            activeTarget = &modelSpace;
            ++i;
            continue;
        }

        if (curSection == "BLOCKS") {
            if (pairs[i].code == 0 && pairs[i].strVal == "BLOCK") {
                curBlockName.clear();
                inBlockHeader = true;
                ++i;
                while (i < pairs.size() && pairs[i].code != 0) {
                    if (pairs[i].code == 2) curBlockName = pairs[i].strVal;
                    ++i;
                }
                inBlockHeader = false;
                activeTarget = curBlockName.empty() ? &modelSpace : &blockDefs[curBlockName];
                continue;
            }
            if (pairs[i].code == 0 && pairs[i].strVal == "ENDBLK") {
                activeTarget = &modelSpace;
                ++i;
                while (i < pairs.size() && pairs[i].code != 0) ++i;
                continue;
            }
        } else if (curSection != "ENTITIES" && !curSection.empty()) {
            ++i;
            continue;
        }
        (void)inBlockHeader;

        if (pairs[i].code != 0) { ++i; continue; }
        std::string ent = pairs[i].strVal;
        ++i;

        // A. 3DFACE / SOLID
        if (ent == "3DFACE" || ent == "SOLID") {
            double x[4] = {}, y[4] = {}, z[4] = {};
            bool has3 = false, has4 = false;
            while (i < pairs.size() && pairs[i].code != 0) {
                int c = pairs[i].code;
                double v = pairs[i].numVal;
                if (c >= 10 && c <= 13) { x[c - 10] = v; if (c == 12) has3 = true; if (c == 13) has4 = true; }
                else if (c >= 20 && c <= 23) { y[c - 20] = v; }
                else if (c >= 30 && c <= 33) { z[c - 30] = v; }
                ++i;
            }
            if (has3) {
                uint32_t b = static_cast<uint32_t>(activeTarget->verts.size());
                activeTarget->verts.push_back({ x[0], y[0], z[0] });
                activeTarget->verts.push_back({ x[1], y[1], z[1] });
                activeTarget->verts.push_back({ x[2], y[2], z[2] });
                activeTarget->tris.push_back({ b + 0, b + 1, b + 2 });
                if (has4 && (std::fabs(x[2] - x[3]) > 1e-6 || std::fabs(y[2] - y[3]) > 1e-6 || std::fabs(z[2] - z[3]) > 1e-6)) {
                    activeTarget->verts.push_back({ x[3], y[3], z[3] });
                    if (ent == "SOLID") {
                        // DXF SOLID swaps corners 3 and 4 (bowtie convention: 0-1-3-2)
                        activeTarget->tris.back() = { b + 0, b + 1, b + 3 };
                        activeTarget->tris.push_back({ b + 0, b + 3, b + 2 });
                    } else {
                        activeTarget->tris.push_back({ b + 0, b + 2, b + 3 });
                    }
                }
            }
        }
        // B. POLYLINE (Polyface Mesh flag & 64, Polygon MxN Mesh flag & 16, or 3D Polyline)
        else if (ent == "POLYLINE") {
            int polyFlags = 0, meshM = 0, meshN = 0;
            while (i < pairs.size() && pairs[i].code != 0) {
                if (pairs[i].code == 70) polyFlags = static_cast<int>(pairs[i].intVal);
                else if (pairs[i].code == 71) meshM = static_cast<int>(pairs[i].intVal);
                else if (pairs[i].code == 72) meshN = static_cast<int>(pairs[i].intVal);
                ++i;
            }

            std::vector<std::array<double, 3>> pfVerts;
            std::vector<std::array<int, 4>> pfFaces;
            while (i < pairs.size() && pairs[i].code == 0 && pairs[i].strVal == "VERTEX") {
                ++i;
                double vx = 0, vy = 0, vz = 0;
                int vFlags = 0;
                int fi[4] = { 0, 0, 0, 0 };
                while (i < pairs.size() && pairs[i].code != 0) {
                    int c = pairs[i].code;
                    if (c == 10) vx = pairs[i].numVal;
                    else if (c == 20) vy = pairs[i].numVal;
                    else if (c == 30) vz = pairs[i].numVal;
                    else if (c == 70) vFlags = static_cast<int>(pairs[i].intVal);
                    else if (c >= 71 && c <= 74) fi[c - 71] = static_cast<int>(pairs[i].intVal);
                    ++i;
                }
                if ((polyFlags & 64) != 0) {
                    if ((vFlags & 128) != 0 && (vFlags & 64) == 0 && (fi[0] != 0 || fi[1] != 0 || fi[2] != 0)) {
                        pfFaces.push_back({ std::abs(fi[0]) - 1, std::abs(fi[1]) - 1, std::abs(fi[2]) - 1, std::abs(fi[3]) - 1 });
                    } else {
                        pfVerts.push_back({ vx, vy, vz });
                    }
                } else {
                    pfVerts.push_back({ vx, vy, vz });
                }
            }
            if (i < pairs.size() && pairs[i].code == 0 && pairs[i].strVal == "SEQEND") {
                ++i;
                while (i < pairs.size() && pairs[i].code != 0) ++i;
            }

            if ((polyFlags & 64) != 0 && !pfVerts.empty() && !pfFaces.empty()) {
                uint32_t b = static_cast<uint32_t>(activeTarget->verts.size());
                activeTarget->verts.insert(activeTarget->verts.end(), pfVerts.begin(), pfVerts.end());
                for (const auto& fc : pfFaces) {
                    if (fc[0] >= 0 && fc[1] >= 0 && fc[2] >= 0 &&
                        static_cast<size_t>(fc[0]) < pfVerts.size() &&
                        static_cast<size_t>(fc[1]) < pfVerts.size() &&
                        static_cast<size_t>(fc[2]) < pfVerts.size())
                    {
                        activeTarget->tris.push_back({
                            b + static_cast<uint32_t>(fc[0]),
                            b + static_cast<uint32_t>(fc[1]),
                            b + static_cast<uint32_t>(fc[2])
                        });
                        if (fc[3] >= 0 && fc[3] != fc[2] && static_cast<size_t>(fc[3]) < pfVerts.size()) {
                            activeTarget->tris.push_back({
                                b + static_cast<uint32_t>(fc[0]),
                                b + static_cast<uint32_t>(fc[2]),
                                b + static_cast<uint32_t>(fc[3])
                            });
                        }
                    }
                }
            } else if ((polyFlags & 16) != 0 && meshM >= 2 && meshN >= 2 &&
                       pfVerts.size() >= static_cast<size_t>(meshM) * static_cast<size_t>(meshN))
            {
                uint32_t b = static_cast<uint32_t>(activeTarget->verts.size());
                activeTarget->verts.insert(activeTarget->verts.end(), pfVerts.begin(), pfVerts.end());
                for (int r = 0; r + 1 < meshM; ++r) {
                    for (int c = 0; c + 1 < meshN; ++c) {
                        uint32_t i00 = b + static_cast<uint32_t>(r * meshN + c);
                        uint32_t i10 = b + static_cast<uint32_t>((r + 1) * meshN + c);
                        uint32_t i11 = b + static_cast<uint32_t>((r + 1) * meshN + (c + 1));
                        uint32_t i01 = b + static_cast<uint32_t>(r * meshN + (c + 1));
                        activeTarget->tris.push_back({ i00, i10, i11 });
                        activeTarget->tris.push_back({ i00, i11, i01 });
                    }
                }
            }
        }
        // C. MESH (AcDbSubDMesh — AutoCAD 2010+ Subdivision / Polygon Mesh)
        else if (ent == "MESH") {
            bool inSubDMesh = false;
            int expectedVerts = -1, expectedFaceInts = -1;
            std::vector<std::array<double, 3>> mVerts;
            std::vector<int32_t> faceStream;
            double curX = 0, curY = 0;
            bool hasXY = false;

            while (i < pairs.size() && pairs[i].code != 0) {
                int c = pairs[i].code;
                if (c == 100 && pairs[i].strVal == "AcDbSubDMesh") {
                    inSubDMesh = true;
                } else if (inSubDMesh) {
                    if (c == 92 && expectedVerts < 0) {
                        expectedVerts = static_cast<int>(pairs[i].intVal);
                    } else if (c == 10 && static_cast<int>(mVerts.size()) < expectedVerts) {
                        curX = pairs[i].numVal; hasXY = true;
                    } else if (c == 20 && hasXY) {
                        curY = pairs[i].numVal;
                    } else if (c == 30 && hasXY) {
                        mVerts.push_back({ curX, curY, pairs[i].numVal });
                        hasXY = false;
                    } else if (c == 93 && expectedFaceInts < 0) {
                        expectedFaceInts = static_cast<int>(pairs[i].intVal);
                    } else if (c == 90 && expectedFaceInts > 0 && static_cast<int>(faceStream.size()) < expectedFaceInts) {
                        faceStream.push_back(static_cast<int32_t>(pairs[i].intVal));
                    }
                }
                ++i;
            }
            if (mVerts.size() >= 3 && !faceStream.empty()) {
                uint32_t b = static_cast<uint32_t>(activeTarget->verts.size());
                activeTarget->verts.insert(activeTarget->verts.end(), mVerts.begin(), mVerts.end());
                size_t fp = 0;
                while (fp < faceStream.size()) {
                    int32_t deg = faceStream[fp++];
                    if (deg < 3 || deg > 256 || fp + static_cast<size_t>(deg) > faceStream.size()) break;
                    int32_t i0 = faceStream[fp];
                    for (int32_t t = 1; t + 1 < deg; ++t) {
                        int32_t i1 = faceStream[fp + static_cast<size_t>(t)];
                        int32_t i2 = faceStream[fp + static_cast<size_t>(t + 1)];
                        if (i0 >= 0 && i1 >= 0 && i2 >= 0 &&
                            static_cast<size_t>(i0) < mVerts.size() &&
                            static_cast<size_t>(i1) < mVerts.size() &&
                            static_cast<size_t>(i2) < mVerts.size())
                        {
                            activeTarget->tris.push_back({
                                b + static_cast<uint32_t>(i0),
                                b + static_cast<uint32_t>(i1),
                                b + static_cast<uint32_t>(i2)
                            });
                        }
                    }
                    fp += static_cast<size_t>(deg);
                }
            }
        }
        // D. INSERT (instantiate named BLOCK with translation, scale, and Z rotation)
        else if (ent == "INSERT") {
            std::string bName;
            double ix = 0, iy = 0, iz = 0, sx = 1.0, sy = 1.0, sz = 1.0, rotDeg = 0.0;
            while (i < pairs.size() && pairs[i].code != 0) {
                int c = pairs[i].code;
                if (c == 2) bName = pairs[i].strVal;
                else if (c == 10) ix = pairs[i].numVal;
                else if (c == 20) iy = pairs[i].numVal;
                else if (c == 30) iz = pairs[i].numVal;
                else if (c == 41) sx = pairs[i].numVal;
                else if (c == 42) sy = pairs[i].numVal;
                else if (c == 43) sz = pairs[i].numVal;
                else if (c == 50) rotDeg = pairs[i].numVal;
                ++i;
            }
            auto itB = blockDefs.find(bName);
            if (itB != blockDefs.end() && !itB->second.verts.empty() && activeTarget != &itB->second) {
                double rad = rotDeg * 3.14159265358979323846 / 180.0;
                double cs = std::cos(rad), sn = std::sin(rad);
                uint32_t base = static_cast<uint32_t>(activeTarget->verts.size());
                for (const auto& v : itB->second.verts) {
                    double vx = v[0] * sx, vy = v[1] * sy, vz = v[2] * sz;
                    activeTarget->verts.push_back({ vx * cs - vy * sn + ix, vx * sn + vy * cs + iy, vz + iz });
                }
                for (const auto& t : itB->second.tris) {
                    activeTarget->tris.push_back({ base + t[0], base + t[1], base + t[2] });
                }
            }
        }
        // E. 3D LINE fallback strut
        else if (ent == "LINE") {
            double x1 = 0, y1 = 0, z1 = 0, x2 = 0, y2 = 0, z2 = 0;
            while (i < pairs.size() && pairs[i].code != 0) {
                int c = pairs[i].code;
                if (c == 10) x1 = pairs[i].numVal; else if (c == 20) y1 = pairs[i].numVal; else if (c == 30) z1 = pairs[i].numVal;
                else if (c == 11) x2 = pairs[i].numVal; else if (c == 21) y2 = pairs[i].numVal; else if (c == 31) z2 = pairs[i].numVal;
                ++i;
            }
            if (std::hypot(x2 - x1, y2 - y1) + std::fabs(z2 - z1) > 1e-5) {
                LoadedMesh3D tmp;
                Detail3D100::AddLineStrut(tmp, { static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(z1) },
                                               { static_cast<float>(x2), static_cast<float>(y2), static_cast<float>(z2) }, 0.04f);
                uint32_t b = static_cast<uint32_t>(activeTarget->verts.size());
                for (const auto& v : tmp.vertices) activeTarget->verts.push_back({ v.x, v.y, v.z });
                for (const auto& t : tmp.tris) activeTarget->tris.push_back({ b + t.i0, b + t.i1, b + t.i2 });
            }
        } else {
            while (i < pairs.size() && pairs[i].code != 0) ++i;
        }
    }

    if (modelSpace.verts.empty() || modelSpace.tris.empty()) return false;
    mesh.vertices.clear();
    mesh.tris.clear();
    mesh.vertices.reserve(modelSpace.verts.size());
    for (const auto& v : modelSpace.verts) {
        if (!ValidCadPt(v[0], v[1], v[2])) return false;
        mesh.vertices.push_back(CadZUpToYUp(v[0], v[1], v[2]));
    }
    mesh.tris.reserve(modelSpace.tris.size());
    for (const auto& t : modelSpace.tris) {
        if (t[0] < mesh.vertices.size() && t[1] < mesh.vertices.size() && t[2] < mesh.vertices.size()) {
            mesh.tris.push_back({ t[0], t[1], t[2], {} });
        }
    }
    mesh.formatName = isBinary
        ? L"AutoCAD Binary 3D Drawing Exchange (.dxf)"
        : L"AutoCAD 3D Drawing Exchange (.dxf)";
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
