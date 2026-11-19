#pragma once
// AutoCadDwg.h — AutoCAD Native Drawing (.dwg — AC1009 through AC1032) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace DwgDxfDetail {

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

    // Unsigned Modular Char in DWG bitstream (R2010+ object record handle-stream bit size)
    uint64_t ReadUMC() {
        uint64_t val = 0;
        uint32_t shift = 0;
        for (int i = 0; i < 6; ++i) {
            if (!HasBits(8)) { bitPos = byteSize * 8ULL; return 0; }
            uint8_t b = ReadRC();
            val |= static_cast<uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0u) return val;
            shift += 7u;
        }
        return val;
    }

    // Bit-coded Object Type (BOT — R2010+ AC1024/AC1027/AC1032 object type prefix)
    int16_t ReadBOT() {
        uint8_t otPrefix = ReadBB();
        if (otPrefix == 0u) return static_cast<int16_t>(ReadRC());
        if (otPrefix == 1u) return static_cast<int16_t>(0x1F0 + static_cast<int>(ReadRC()));
        return static_cast<int16_t>(ReadRS());
    }

    // Bit-coded Long Long (BLL — R2010+ 3-bit byte-length prefix + LE bytes)
    uint64_t ReadBLL() {
        uint32_t len = (static_cast<uint32_t>(ReadB()) << 2) |
                       (static_cast<uint32_t>(ReadB()) << 1) |
                        static_cast<uint32_t>(ReadB());
        uint64_t val = 0;
        for (uint32_t i = 0; i < len; ++i) {
            val |= static_cast<uint64_t>(ReadRC()) << (i * 8u);
        }
        return val;
    }

    // Entity Color (ENC — R2004+ Common Entity Header color with optional RGB & transparency)
    uint16_t ReadENC() {
        uint16_t flags = static_cast<uint16_t>(ReadBS());
        if ((flags & 0x8000u) != 0u) (void)ReadBL(); // TrueColor RGB
        if ((flags & 0x2000u) != 0u) (void)ReadBL(); // Transparency
        return flags & 0x01FFu;
    }

    std::string ReadTV() {
        int16_t len = ReadBS();
        if (len <= 0 || len > 4096 || !HasBits(static_cast<size_t>(len) * 8u)) return {};
        std::string s(static_cast<size_t>(len), '\0');
        for (int16_t i = 0; i < len; ++i) s[static_cast<size_t>(i)] = static_cast<char>(ReadRC());
        while (!s.empty() && s.back() == '\0') s.pop_back();
        return s;
    }

    std::string ReadTU() {
        int16_t len = ReadBS();
        if (len <= 0 || len > 4096 || !HasBits(static_cast<size_t>(len) * 16u)) return {};
        std::string s;
        s.reserve(static_cast<size_t>(len));
        for (int16_t i = 0; i < len; ++i) {
            uint16_t cu = ReadRS();
            if (cu != 0u && cu < 128u) s.push_back(static_cast<char>(cu));
        }
        return s;
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

// Parse AcDb:Classes section (R2004 TV & R2007+/R2010+ String Stream TU) to discover dynamic MESH / AcDbSubDMesh class IDs (>= 500)
inline void ParseDwgMeshClassNumbers(
    const uint8_t* clsBuf,
    size_t clsSize,
    int dwgRelVer,
    std::unordered_set<int16_t>& outMeshClassIds)
{
    outMeshClassIds.clear();
    static const uint8_t kStartSent[16] = {
        0x8Du, 0xA1u, 0xC4u, 0xB8u, 0xC4u, 0xA9u, 0xF8u, 0xC5u,
        0xC0u, 0xDCu, 0xF4u, 0x5Fu, 0xE7u, 0xCFu, 0xB6u, 0x8Au
    };
    static const uint8_t kEndSent[16] = {
        0x72u, 0x5Eu, 0x3Bu, 0x47u, 0x3Bu, 0x56u, 0x07u, 0x3Au,
        0x3Fu, 0x23u, 0x0Bu, 0xA0u, 0x18u, 0x30u, 0x49u, 0x75u
    };
    if (!clsBuf || clsSize < 38u || std::memcmp(clsBuf, kStartSent, 16) != 0) return;

    uint32_t dataBytes = RdU32LE(clsBuf + 16);
    size_t dataOff = 20u;
    if (24u + static_cast<size_t>(dataBytes) + 18u <= clsSize &&
        std::memcmp(clsBuf + 24u + dataBytes + 2u, kEndSent, 16) == 0)
    {
        dataOff = 24u; // R2010 (maint > 3) / R2013+ 4-byte high-zero RL
    } else if (20u + static_cast<size_t>(dataBytes) + 18u <= clsSize &&
               std::memcmp(clsBuf + 20u + dataBytes + 2u, kEndSent, 16) == 0)
    {
        dataOff = 20u;
    } else if (dwgRelVer >= 2013 && 24u + static_cast<size_t>(dataBytes) <= clsSize) {
        dataOff = 24u;
    }
    if (dataBytes < 4u || dataOff + dataBytes > clsSize) return;

    DwgBitReader cbr(clsBuf + dataOff, dataBytes);
    if (dwgRelVer >= 2007) {
        uint32_t totalBits = cbr.ReadRL();
        // Locate R2007+ String Stream at end of totalBits (try totalBits from start of cbr or after RL)
        auto tryLocateStringStream = [&](size_t endBitPos, size_t& outStrStart, size_t& outDataEnd) -> bool {
            if (endBitPos < 34u || endBitPos > dataBytes * 8ULL) return false;
            DwgBitReader flagRd(clsBuf + dataOff, dataBytes, endBitPos - 1u);
            if (flagRd.ReadB() == 0u) {
                outStrStart = endBitPos - 1u;
                outDataEnd  = endBitPos - 1u;
                return true;
            }
            if (endBitPos < 17u) return false;
            DwgBitReader szRd(clsBuf + dataOff, dataBytes, endBitPos - 17u);
            uint16_t loSz = szRd.ReadRS();
            size_t strBits = loSz & 0x7FFFu;
            size_t hdrEnd  = endBitPos - 17u;
            if ((loSz & 0x8000u) != 0u) {
                if (endBitPos < 33u) return false;
                DwgBitReader hiRd(clsBuf + dataOff, dataBytes, endBitPos - 33u);
                uint16_t hiSz = hiRd.ReadRS();
                strBits |= static_cast<size_t>(hiSz) << 15u;
                hdrEnd = endBitPos - 33u;
            }
            if (strBits > hdrEnd || hdrEnd - strBits < cbr.bitPos) return false;
            outStrStart = hdrEnd - strBits;
            outDataEnd  = outStrStart;
            return true;
        };

        size_t strStartBit = 0, dataEndBit = 0;
        if (!tryLocateStringStream(totalBits, strStartBit, dataEndBit) &&
            !tryLocateStringStream(32u + static_cast<size_t>(totalBits), strStartBit, dataEndBit))
        {
            return;
        }

        int16_t maxCls = cbr.ReadBS();
        (void)maxCls;
        (void)cbr.ReadRC();
        (void)cbr.ReadRC();
        (void)cbr.ReadB();

        DwgBitReader sbr(clsBuf + dataOff, dataBytes, strStartBit);
        for (int guard = 0; guard < 4096 && cbr.bitPos + 16u <= dataEndBit; ++guard) {
            int16_t clsNum = cbr.ReadBS();
            (void)cbr.ReadBS(); // proxyFlags
            std::string appName = sbr.ReadTU();
            std::string cppName = sbr.ReadTU();
            std::string dxfName = sbr.ReadTU();
            (void)appName;
            (void)cbr.ReadB();  // wasZombie
            (void)cbr.ReadBS(); // itemClassId
            (void)cbr.ReadBL(); // numInstances
            (void)cbr.ReadBL(); // dwgVersion
            (void)cbr.ReadBL(); // maintVersion
            (void)cbr.ReadBL(); // unk1
            (void)cbr.ReadBL(); // unk2
            if (clsNum >= 500 && (dxfName == "MESH" || cppName == "AcDbSubDMesh")) {
                outMeshClassIds.insert(clsNum);
            }
        }
    } else if (dwgRelVer >= 2004) {
        int16_t maxCls = cbr.ReadBS();
        (void)maxCls;
        (void)cbr.ReadRC();
        (void)cbr.ReadRC();
        (void)cbr.ReadB();
        for (int guard = 0; guard < 4096 && cbr.HasBits(32); ++guard) {
            int16_t clsNum = cbr.ReadBS();
            if (clsNum < 500) break;
            (void)cbr.ReadBS(); // proxyFlags
            std::string appName = cbr.ReadTV();
            std::string cppName = cbr.ReadTV();
            std::string dxfName = cbr.ReadTV();
            (void)appName;
            (void)cbr.ReadB();
            (void)cbr.ReadBS();
            (void)cbr.ReadBL();
            (void)cbr.ReadBS();
            (void)cbr.ReadBS();
            (void)cbr.ReadBL();
            (void)cbr.ReadBL();
            if (dxfName == "MESH" || cppName == "AcDbSubDMesh") {
                outMeshClassIds.insert(clsNum);
            }
        }
    }
}

inline bool RunDwgObjectDecodePass(
    const uint8_t* objStream,
    size_t objStreamSize,
    const std::vector<std::pair<uint64_t, uint64_t>>& handleLocs,
    const std::unordered_set<int16_t>& meshClassIds,
    int dwgRelVer,
    bool useR2010Framing,
    LoadedMesh3D& mesh)
{
    mesh.vertices.clear();
    mesh.tris.clear();
    std::vector<Vec3f> pfaceVerts;

    for (const auto& hl : handleLocs) {
        size_t op = static_cast<size_t>(hl.second);
        uint32_t recBytes = 0;
        if (!ReadDwgMS(objStream, objStreamSize, op, recBytes) || recBytes < 4u || op + recBytes > objStreamSize) {
            continue;
        }
        DwgBitReader br(objStream + op, recBytes);
        size_t maxObjBit = static_cast<size_t>(recBytes) * 8u;

        int16_t objType = 0;
        if (useR2010Framing) {
            // Genuine AutoCAD R2010-R2025 (AC1024/AC1027/AC1032) Object Framing:
            //   1. UMC handleStreamBitSize (bits at end of object record)
            //   2. BOT (2-bit prefix + RC / 0x1F0+RC / RS)
            //   3. H entityHandle (NO RL objBits field!)
            uint64_t handleBits = br.ReadUMC();
            if (handleBits >= static_cast<uint64_t>(recBytes) * 8ULL) continue;
            maxObjBit = static_cast<size_t>(recBytes) * 8u - static_cast<size_t>(handleBits);
            objType = br.ReadBOT();
        } else {
            objType = br.ReadBS();
        }

        bool isKnownFixed = (objType == 19 || objType == 28 || objType == 13 || objType == 14);
        bool isMeshClass  = (objType >= 500) &&
                            (!meshClassIds.empty() ? (meshClassIds.count(objType) > 0) : useR2010Framing);
        if (!isKnownFixed && !isMeshClass) {
            continue;
        }

        if (useR2010Framing) {
            uint64_t entHandle = br.ReadHandle();
            // Verify the entity's self-handle matches its AcDb:Handles map key so R2004 records never false-match!
            if (entHandle == 0 || (hl.first != 0 && entHandle != hl.first)) continue;
            bool badEed = false;
            for (int guard = 0; guard < 64; ++guard) {
                int16_t eedSz = br.ReadBS();
                if (eedSz == 0) break;
                if (eedSz < 0 || br.bitPos + static_cast<size_t>(eedSz) * 8u > maxObjBit) { badEed = true; break; }
                (void)br.ReadHandle();
                for (int16_t k = 0; k < eedSz; ++k) (void)br.ReadRC();
            }
            if (badEed) continue;

            if (br.ReadB()) { // graphicPresent (BLL byte count in R2010+)
                uint64_t gSz = br.ReadBLL();
                if (gSz > recBytes || br.bitPos + static_cast<size_t>(gSz) * 8u > maxObjBit) continue;
                for (uint64_t k = 0; k < gSz; ++k) (void)br.ReadRC();
            }
            (void)br.ReadBB(); // entmode
            int32_t numReactors = br.ReadBL();
            if (numReactors < 0 || numReactors > 65536) continue;
            (void)br.ReadB();  // is_xdic_missing (R2004+)
            if (dwgRelVer >= 2013) (void)br.ReadB(); // has_ds_data (R2013+ / AC1027 / AC1032)
            (void)br.ReadENC(); // ENC entity color (with optional RGB & transparency)
            (void)br.ReadBD();  // ltype_scale
            (void)br.ReadBB();  // ltype_flags
            (void)br.ReadBB();  // plotstyle_flags
            (void)br.ReadBB();  // material_flags (R2007+)
            (void)br.ReadRC();  // shadow_flags (R2007+)
            (void)br.ReadB();   // has_full_visualstyle (R2010+)
            (void)br.ReadB();   // has_face_visualstyle (R2010+)
            (void)br.ReadB();   // has_edge_visualstyle (R2010+)
            (void)br.ReadBS();  // invis
            (void)br.ReadRC();  // lineweight
        } else if (dwgRelVer >= 2000) {
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
            (void)br.ReadENC(); // color (CMC / ENC)
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

        if (!br.HasBits(4) || br.bitPos > maxObjBit) continue;

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
            if (br.bitPos > maxObjBit) continue;

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
            if (br.bitPos > maxObjBit) continue;
            if (ValidCadPt(sx, sy, sz) && ValidCadPt(ex, ey, ez)) {
                Detail3D100::AddLineStrut(mesh, CadZUpToYUp(sx, sy, sz), CadZUpToYUp(ex, ey, ez), 0.04f);
            }
        } else if (objType == 13) { // VERTEX_PFACE
            br.ReadRC(); // flags
            double px = br.ReadBD(), py = br.ReadBD(), pz = br.ReadBD();
            if (br.bitPos <= maxObjBit && ValidCadPt(px, py, pz)) pfaceVerts.push_back(CadZUpToYUp(px, py, pz));
        } else if (objType == 14 && pfaceVerts.size() >= 3) { // VERTEX_PFACE_FACE
            int i1 = std::abs(static_cast<int>(br.ReadBS())) - 1;
            int i2 = std::abs(static_cast<int>(br.ReadBS())) - 1;
            int i3 = std::abs(static_cast<int>(br.ReadBS())) - 1;
            int i4 = std::abs(static_cast<int>(br.ReadBS())) - 1;
            if (br.bitPos <= maxObjBit && i1 >= 0 && i2 >= 0 && i3 >= 0 &&
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
        } else if (isMeshClass) { // MESH (AcDbSubDMesh — AutoCAD 2010+ Subdivision / Polygon Mesh)
            int16_t meshVer = br.ReadBS();
            uint8_t blendCrease = br.ReadB();
            (void)blendCrease;
            int32_t subdivLevels = br.ReadBL();
            int32_t numVerts = br.ReadBL();
            if (meshVer < 1 || meshVer > 4 || subdivLevels < 0 || subdivLevels > 16 ||
                numVerts < 3 || numVerts > 500000 || !br.HasBits(static_cast<size_t>(numVerts) * 6u))
            {
                continue;
            }
            std::vector<Vec3f> mVerts;
            mVerts.reserve(static_cast<size_t>(numVerts));
            bool okV = true;
            for (int32_t v = 0; v < numVerts; ++v) {
                double vx = br.ReadBD(), vy = br.ReadBD(), vz = br.ReadBD();
                if (!ValidCadPt(vx, vy, vz) || br.bitPos > maxObjBit) { okV = false; break; }
                mVerts.push_back(CadZUpToYUp(vx, vy, vz));
            }
            if (!okV) continue;

            int32_t numFaceInts = br.ReadBL();
            if (numFaceInts < 4 || numFaceInts > 2000000 ||
                !br.HasBits(static_cast<size_t>(numFaceInts) * 2u))
            {
                continue;
            }
            std::vector<int32_t> faceList(static_cast<size_t>(numFaceInts));
            for (int32_t f = 0; f < numFaceInts; ++f) {
                faceList[static_cast<size_t>(f)] = br.ReadBL();
            }
            if (br.bitPos > maxObjBit) continue;

            // Validate face list structure before committing
            std::vector<std::array<uint32_t, 3>> localTris;
            size_t fp = 0;
            bool okFaces = true;
            while (fp < faceList.size()) {
                int32_t deg = faceList[fp++];
                if (deg < 3 || deg > 128 || fp + static_cast<size_t>(deg) > faceList.size()) {
                    okFaces = false;
                    break;
                }
                int32_t i0 = faceList[fp];
                if (i0 < 0 || i0 >= numVerts) { okFaces = false; break; }
                for (int32_t t = 1; t + 1 < deg; ++t) {
                    int32_t i1 = faceList[fp + static_cast<size_t>(t)];
                    int32_t i2 = faceList[fp + static_cast<size_t>(t + 1)];
                    if (i1 < 0 || i1 >= numVerts || i2 < 0 || i2 >= numVerts) { okFaces = false; break; }
                    localTris.push_back({
                        static_cast<uint32_t>(i0),
                        static_cast<uint32_t>(i1),
                        static_cast<uint32_t>(i2)
                    });
                }
                fp += static_cast<size_t>(deg);
            }
            if (okFaces && !localTris.empty()) {
                uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.insert(mesh.vertices.end(), mVerts.begin(), mVerts.end());
                for (const auto& lt : localTris) {
                    mesh.tris.push_back({ b + lt[0], b + lt[1], b + lt[2], {} });
                }
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

// Decode entities from an Object Map + Object Stream buffer (shared by R13-R2000 and R2004-R2025!)
inline bool DecodeDwgObjectsFromHandleMap(
    const uint8_t* objStream,
    size_t objStreamSize,
    const uint8_t* handleMap,
    size_t handleMapSize,
    int dwgRelVer, // 13, 14, 2000, 2004, 2010, 2013, 2018
    LoadedMesh3D& mesh,
    const uint8_t* classesStream = nullptr,
    size_t classesStreamSize = 0)
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

    std::unordered_set<int16_t> meshClassIds;
    if (classesStream && classesStreamSize > 0) {
        ParseDwgMeshClassNumbers(classesStream, classesStreamSize, dwgRelVer, meshClassIds);
    }

    // For R2010+ (AC1024, AC1027, AC1032), try genuine R2010+ (UMC + BOT) object framing FIRST,
    // and fall back cleanly to R2004 (BS + RL) framing if 0 entities match (preserving legacy test fixtures).
    if (dwgRelVer >= 2010) {
        if (RunDwgObjectDecodePass(objStream, objStreamSize, handleLocs, meshClassIds, dwgRelVer, true, mesh)) {
            return true;
        }
        mesh.vertices.clear();
        mesh.tris.clear();
    }

    return RunDwgObjectDecodePass(objStream, objStreamSize, handleLocs, meshClassIds, dwgRelVer, false, mesh);
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
        int relVer = (ver6 == "AC1018") ? 2004 : ((ver6 == "AC1024") ? 2010 : ((ver6 == "AC1027") ? 2013 : 2018));
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
        std::vector<uint8_t> handlesStream, objectsStream, classesStream;

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
            bool wantClasses = (std::strcmp(secName, "AcDb:Classes") == 0);
            std::vector<uint8_t>* target = wantHandles ? &handlesStream
                                         : (wantObjects ? &objectsStream
                                         : (wantClasses ? &classesStream : nullptr));

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
                                           relVer, mesh,
                                           classesStream.empty() ? nullptr : classesStream.data(),
                                           classesStream.size()))
        {
            return false;
        }
        mesh.formatName = L"AutoCAD Modern Drawing (.dwg " + std::wstring(ver6.begin(), ver6.end()) + L")";
        return true;
    }

    return false;
}

} // namespace Formats
