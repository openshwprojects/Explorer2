#pragma once
// ApacheOrc.h — Apache ORC Columnar Storage (.orc) Reader

namespace Formats {
namespace AiDataDetail {

// ============================================================================
// 8. Apache ORC Columnar Storage (.orc) — PostScript + FileFooter + StripeFooter + RLEv1/RLEv2
// ============================================================================
inline const wchar_t* OrcTypeKindName(uint64_t k) {
    switch (k) {
        case 0: return L"BOOLEAN";   case 1: return L"BYTE";      case 2: return L"SHORT";
        case 3: return L"INT";       case 4: return L"LONG";      case 5: return L"FLOAT";
        case 6: return L"DOUBLE";    case 7: return L"STRING";    case 8: return L"BINARY";
        case 9: return L"TIMESTAMP"; case 10: return L"LIST";     case 11: return L"MAP";
        case 12: return L"STRUCT";   case 13: return L"UNION";    case 14: return L"DECIMAL";
        case 15: return L"DATE";     case 16: return L"VARCHAR";  case 17: return L"CHAR";
        default: return L"ORC_TYPE";
    }
}

inline bool DecompressOrcStream(const uint8_t* src, size_t srcLen, uint64_t codec, std::vector<uint8_t>& out) {
    out.clear();
    if (!src && srcLen > 0) return false;
    if (codec == 0) { // NONE
        out.assign(src, src + srcLen);
        return true;
    }
    size_t pos = 0;
    while (pos + 3 <= srcLen) {
        uint32_t hdr = static_cast<uint32_t>(src[pos]) |
                       (static_cast<uint32_t>(src[pos + 1]) << 8) |
                       (static_cast<uint32_t>(src[pos + 2]) << 16);
        pos += 3;
        bool isOriginal = (hdr & 1u) != 0;
        size_t chunkLen = hdr >> 1;
        if (chunkLen > srcLen - pos) return false;
        if (isOriginal) {
            out.insert(out.end(), src + pos, src + pos + chunkLen);
        } else if (codec == 1) { // ZLIB in ORC is raw deflate
            std::vector<uint8_t> dec;
            if (!ZipDeflate::InflateRaw(src + pos, chunkLen, dec, 16 * 1024 * 1024)) return false;
            out.insert(out.end(), dec.begin(), dec.end());
        } else if (codec == 2) { // SNAPPY
            std::vector<uint8_t> dec;
            if (!::Explorer2::StreamCodecs::SnappyDecompress(src + pos, chunkLen, dec, 16 * 1024 * 1024)) return false;
            out.insert(out.end(), dec.begin(), dec.end());
        } else if (codec == 4) { // LZ4 (ORC uses raw LZ4 block; accept LZ4 frame too)
            std::vector<uint8_t> dec;
            if (chunkLen >= 4 && RdU32LE(src + pos) == 0x184D2204u) {
                if (!::Explorer2::StreamCodecs::DecompressLz4Frame(src + pos, chunkLen, dec, 16 * 1024 * 1024)) return false;
            } else {
                if (!::Explorer2::Lz4::DecodeBlockAppend(src + pos, chunkLen, dec, 0, 16 * 1024 * 1024, false)) return false;
            }
            out.insert(out.end(), dec.begin(), dec.end());
        } else if (codec == 5) { // ZSTD
            std::vector<uint8_t> dec;
            if (!::Explorer2::StreamCodecs::DecompressZstdFrame(src + pos, chunkLen, dec, 16 * 1024 * 1024)) return false;
            out.insert(out.end(), dec.begin(), dec.end());
        } else {
            return false;
        }
        pos += chunkLen;
        if (out.size() > 64u * 1024u * 1024u) return false;
    }
    return pos == srcLen;
}

namespace OrcRowDetail {

inline int64_t ZigZagDecode64(uint64_t u) {
    return static_cast<int64_t>((u >> 1) ^ (~(u & 1ULL) + 1ULL));
}

inline uint32_t DecodeOrcBitWidth(uint8_t code) {
    if (code <= 23u) return static_cast<uint32_t>(code) + 1u;
    switch (code) {
        case 24: return 26; case 25: return 28; case 26: return 30; case 27: return 32;
        case 28: return 40; case 29: return 48; case 30: return 56; default: return 64;
    }
}

inline uint32_t ClosestOrcFixedBits(uint32_t w) {
    if (w == 0) return 1;
    if (w <= 24) return w;
    if (w <= 26) return 26;
    if (w <= 28) return 28;
    if (w <= 30) return 30;
    if (w <= 32) return 32;
    if (w <= 40) return 40;
    if (w <= 48) return 48;
    if (w <= 56) return 56;
    return 64;
}

inline bool UnpackOrcMsbBits(const uint8_t* src, size_t srcLen, size_t& pos, uint32_t bitWidth, size_t count, std::vector<uint64_t>& out) {
    out.clear();
    out.reserve(count);
    if (count == 0) return true;
    if (bitWidth == 0 || bitWidth > 64) return false;
    size_t totalBits = count * static_cast<size_t>(bitWidth);
    size_t totalBytes = (totalBits + 7u) / 8u;
    if (totalBytes > srcLen - pos) return false;

    size_t bitPos = 0;
    for (size_t i = 0; i < count; ++i) {
        uint64_t val = 0;
        uint32_t rem = bitWidth;
        while (rem > 0) {
            size_t byteIdx = pos + (bitPos >> 3);
            uint32_t bitOff = static_cast<uint32_t>(bitPos & 7u);
            uint32_t availInByte = 8u - bitOff;
            uint32_t take = std::min(rem, availInByte);
            uint8_t mask = static_cast<uint8_t>((1u << take) - 1u);
            uint8_t bits = static_cast<uint8_t>((src[byteIdx] >> (availInByte - take)) & mask);
            val = (val << take) | bits;
            bitPos += take;
            rem -= take;
        }
        out.push_back(val);
    }
    pos += totalBytes;
    return true;
}

inline bool DecodeOrcByteRle(const uint8_t* src, size_t srcLen, size_t wantCount, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(wantCount);
    size_t pos = 0;
    while (out.size() < wantCount && pos < srcLen) {
        int8_t ctrl = static_cast<int8_t>(src[pos++]);
        if (ctrl >= 0) {
            size_t runLen = static_cast<size_t>(ctrl) + 3u;
            if (pos >= srcLen) return false;
            uint8_t val = src[pos++];
            size_t take = std::min(runLen, wantCount - out.size());
            out.insert(out.end(), take, val);
        } else {
            size_t litLen = static_cast<size_t>(-static_cast<int>(ctrl));
            if (litLen > srcLen - pos) return false;
            size_t take = std::min(litLen, wantCount - out.size());
            out.insert(out.end(), src + pos, src + pos + take);
            pos += litLen;
        }
    }
    return out.size() >= wantCount;
}

inline bool DecodeOrcBitRle(const uint8_t* src, size_t srcLen, size_t wantBits, std::vector<bool>& out) {
    out.clear();
    out.reserve(wantBits);
    if (wantBits == 0) return true;
    size_t wantBytes = (wantBits + 7u) / 8u;
    std::vector<uint8_t> bytes;
    if (!DecodeOrcByteRle(src, srcLen, wantBytes, bytes)) return false;
    for (size_t i = 0; i < wantBits; ++i) {
        uint8_t b = bytes[i >> 3];
        bool bit = ((b >> (7u - (i & 7u))) & 1u) != 0;
        out.push_back(bit);
    }
    return true;
}

inline bool ReadOrcVarint(const uint8_t* src, size_t srcLen, size_t& pos, bool isSigned, int64_t& out) {
    uint64_t u = 0;
    if (!::Explorer2::ByteIO::RdLeb128(src, srcLen, pos, u, 10)) return false;
    out = isSigned ? ZigZagDecode64(u) : static_cast<int64_t>(u);
    return true;
}

inline bool DecodeOrcIntRleV1(const uint8_t* src, size_t srcLen, size_t wantCount, bool isSigned, std::vector<int64_t>& out) {
    out.clear();
    out.reserve(wantCount);
    size_t pos = 0;
    while (out.size() < wantCount && pos < srcLen) {
        int8_t ctrl = static_cast<int8_t>(src[pos++]);
        if (ctrl >= 0) {
            size_t runLen = static_cast<size_t>(ctrl) + 3u;
            if (pos >= srcLen) return false;
            int64_t delta = static_cast<int8_t>(src[pos++]);
            int64_t base = 0;
            if (!ReadOrcVarint(src, srcLen, pos, isSigned, base)) return false;
            for (size_t i = 0; i < runLen && out.size() < wantCount; ++i) {
                out.push_back(base + static_cast<int64_t>(i) * delta);
            }
        } else {
            size_t litLen = static_cast<size_t>(-static_cast<int>(ctrl));
            for (size_t i = 0; i < litLen; ++i) {
                int64_t v = 0;
                if (!ReadOrcVarint(src, srcLen, pos, isSigned, v)) return false;
                if (out.size() < wantCount) out.push_back(v);
            }
        }
    }
    return out.size() >= wantCount;
}

inline bool DecodeOrcIntRleV2(const uint8_t* src, size_t srcLen, size_t wantCount, bool isSigned, std::vector<int64_t>& out) {
    out.clear();
    out.reserve(wantCount);
    size_t pos = 0;
    std::vector<uint64_t> unpacked;
    while (out.size() < wantCount && pos < srcLen) {
        uint8_t b0 = src[pos++];
        uint8_t subEnc = (b0 >> 6) & 0x03u;
        if (subEnc == 0) { // SHORT_REPEAT
            size_t byteW = ((b0 >> 3) & 0x07u) + 1u;
            size_t runLen = (b0 & 0x07u) + 3u;
            if (byteW > srcLen - pos) return false;
            uint64_t u = 0;
            for (size_t k = 0; k < byteW; ++k) u = (u << 8) | src[pos++];
            int64_t val = isSigned ? ZigZagDecode64(u) : static_cast<int64_t>(u);
            for (size_t i = 0; i < runLen && out.size() < wantCount; ++i) out.push_back(val);
        } else if (subEnc == 1) { // DIRECT
            if (pos >= srcLen) return false;
            uint8_t b1 = src[pos++];
            uint32_t bw = DecodeOrcBitWidth((b0 >> 1) & 0x1Fu);
            size_t runLen = ((static_cast<size_t>(b0 & 1u) << 8) | b1) + 1u;
            if (!UnpackOrcMsbBits(src, srcLen, pos, bw, runLen, unpacked)) return false;
            for (size_t i = 0; i < runLen && out.size() < wantCount; ++i) {
                out.push_back(isSigned ? ZigZagDecode64(unpacked[i]) : static_cast<int64_t>(unpacked[i]));
            }
        } else if (subEnc == 2) { // PATCHED_BASE
            if (pos + 3 > srcLen) return false;
            uint8_t b1 = src[pos++];
            uint8_t b2 = src[pos++];
            uint8_t b3 = src[pos++];
            uint32_t bw = DecodeOrcBitWidth((b0 >> 1) & 0x1Fu);
            size_t runLen = ((static_cast<size_t>(b0 & 1u) << 8) | b1) + 1u;
            size_t baseW = ((b2 >> 5) & 0x07u) + 1u;
            uint32_t patchW = DecodeOrcBitWidth(b2 & 0x1Fu);
            uint32_t gapW = static_cast<uint32_t>((b3 >> 5) & 0x07u) + 1u;
            size_t pll = b3 & 0x1Fu;
            if (baseW > srcLen - pos) return false;
            uint64_t rawBase = 0;
            for (size_t k = 0; k < baseW; ++k) rawBase = (rawBase << 8) | src[pos++];
            int64_t base = 0;
            if (isSigned) {
                uint64_t msbMask = 1ULL << (baseW * 8u - 1u);
                bool neg = (rawBase & msbMask) != 0;
                int64_t mag = static_cast<int64_t>(rawBase & ~msbMask);
                base = neg ? -mag : mag;
            } else {
                base = static_cast<int64_t>(rawBase);
            }
            if (!UnpackOrcMsbBits(src, srcLen, pos, bw, runLen, unpacked)) return false;
            std::vector<uint64_t> patches;
            uint32_t entryW = ClosestOrcFixedBits(gapW + patchW);
            if (!UnpackOrcMsbBits(src, srcLen, pos, entryW, pll, patches)) return false;
            uint64_t patchMask = (patchW >= 64) ? UINT64_MAX : ((1ULL << patchW) - 1ULL);
            size_t curIdx = 0;
            for (size_t pIdx = 0; pIdx < pll; ++pIdx) {
                uint64_t gap = patches[pIdx] >> patchW;
                uint64_t pval = patches[pIdx] & patchMask;
                curIdx += static_cast<size_t>(gap);
                if (gap == 255ULL && pval == 0ULL) continue;
                if (curIdx < unpacked.size() && bw < 64) {
                    unpacked[curIdx] |= (pval << bw);
                }
            }
            for (size_t i = 0; i < runLen && out.size() < wantCount; ++i) {
                out.push_back(base + static_cast<int64_t>(unpacked[i]));
            }
        } else { // DELTA (subEnc == 3)
            if (pos >= srcLen) return false;
            uint8_t b1 = src[pos++];
            uint8_t fbRaw = (b0 >> 1) & 0x1Fu;
            uint32_t bw = (fbRaw == 0) ? 0u : DecodeOrcBitWidth(fbRaw);
            size_t runLen = ((static_cast<size_t>(b0 & 1u) << 8) | b1) + 1u;
            int64_t curVal = 0, deltaBase = 0;
            if (!ReadOrcVarint(src, srcLen, pos, isSigned, curVal)) return false;
            if (!ReadOrcVarint(src, srcLen, pos, /*isSigned=*/true, deltaBase)) return false;
            if (out.size() < wantCount) out.push_back(curVal);
            if (runLen >= 2) {
                if (bw == 0) {
                    for (size_t i = 1; i < runLen; ++i) {
                        curVal += deltaBase;
                        if (out.size() < wantCount) out.push_back(curVal);
                    }
                } else {
                    curVal += deltaBase;
                    if (out.size() < wantCount) out.push_back(curVal);
                    if (runLen > 2) {
                        if (!UnpackOrcMsbBits(src, srcLen, pos, bw, runLen - 2u, unpacked)) return false;
                        for (size_t k = 0; k < runLen - 2u; ++k) {
                            int64_t d = static_cast<int64_t>(unpacked[k]);
                            curVal = (deltaBase < 0) ? (curVal - d) : (curVal + d);
                            if (out.size() < wantCount) out.push_back(curVal);
                        }
                    }
                }
            }
        }
    }
    return out.size() >= wantCount;
}

inline bool DecodeOrcIntRle(const std::vector<uint8_t>& stream, size_t wantCount, bool isSigned, bool isV2, std::vector<int64_t>& out) {
    if (wantCount == 0) { out.clear(); return true; }
    return isV2 ? DecodeOrcIntRleV2(stream.data(), stream.size(), wantCount, isSigned, out)
                : DecodeOrcIntRleV1(stream.data(), stream.size(), wantCount, isSigned, out);
}

inline void CivilFromDays(int64_t z, int& y, int& m, int& d) {
    z += 719468LL;
    const int64_t era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    const uint32_t doe = static_cast<uint32_t>(z - era * 146097LL);
    const uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int64_t yr = static_cast<int64_t>(yoe) + era * 400LL;
    const uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const uint32_t mp = (5u * doy + 2u) / 153u;
    d = static_cast<int>(doy - (153u * mp + 2u) / 5u + 1u);
    m = static_cast<int>((mp < 10u) ? (mp + 3u) : (mp - 9u));
    y = static_cast<int>(yr + (m <= 2 ? 1 : 0));
}

inline std::wstring FormatOrcDate(int64_t daysSince1970) {
    int y = 1970, m = 1, d = 1;
    CivilFromDays(daysSince1970, y, m, d);
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%04d-%02d-%02d", y, m, d);
    return buf;
}

inline std::wstring FormatOrcTimestamp(int64_t secRel2015, uint64_t encNanos) {
    constexpr int64_t kOrcEpoch2015 = 1420070400LL;
    uint64_t zeros = encNanos & 0x07ULL;
    uint64_t nanos = encNanos >> 3;
    if (zeros != 0) {
        for (uint64_t z = 0; z <= zeros && nanos < 1000000000ULL; ++z) nanos *= 10ULL;
    }
    int64_t unixSec = kOrcEpoch2015 + secRel2015;
    if (secRel2015 < 0 && nanos > 0) unixSec -= 1;
    int64_t days = (unixSec >= 0) ? (unixSec / 86400LL) : ((unixSec - 86399LL) / 86400LL);
    int64_t secOfDay = unixSec - days * 86400LL;
    int y = 1970, m = 1, d = 1;
    CivilFromDays(days, y, m, d);
    int hh = static_cast<int>(secOfDay / 3600LL);
    int mm = static_cast<int>((secOfDay % 3600LL) / 60LL);
    int ss = static_cast<int>(secOfDay % 60LL);
    wchar_t buf[64] = {};
    if (nanos == 0) {
        swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d", y, m, d, hh, mm, ss);
    } else {
        swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d.%03u", y, m, d, hh, mm, ss, static_cast<unsigned>(nanos / 1000000ULL));
    }
    return buf;
}

} // namespace OrcRowDetail

inline bool ParseApacheOrc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 6) return false;
    SourceBytes src(raw, out);
    const uint64_t fileSize = src.Size();
    std::vector<uint8_t> psBuf, footerBuf;
    const uint8_t* lastByte = src.Read(fileSize - 1, 1, psBuf);
    if (!lastByte) return false;
    const uint8_t psLen = *lastByte;
    if (psLen == 0 || static_cast<uint64_t>(psLen) + 1 >= fileSize) return false;

    const uint64_t psPos = fileSize - 1 - psLen;
    const uint8_t* psBytes = src.Read(psPos, psLen, psBuf);
    if (!psBytes) return false;
    PbReader ps(psBytes, psLen);
    uint64_t footerLength = 0, compression = 0, compBlockSize = 0;
    std::vector<uint64_t> version;
    std::string magic;
    uint32_t fn = 0; uint8_t wt = 0;
    while (ps.pos < ps.size) {
        if (!ps.ReadTag(fn, wt)) return false;
        if (fn == 1 && wt == 0) { if (!ps.ReadVarint(footerLength)) return false; }
        else if (fn == 2 && wt == 0) { if (!ps.ReadVarint(compression)) return false; }
        else if (fn == 3 && wt == 0) { if (!ps.ReadVarint(compBlockSize)) return false; }
        else if (fn == 4 && wt == 0) { uint64_t v = 0; if (!ps.ReadVarint(v)) return false; version.push_back(v); }
        else if (fn == 4 && wt == 2) {
            const uint8_t* vp = nullptr; size_t vl = 0;
            if (!ps.ReadLengthDelimited(vp, vl)) return false;
            PbReader vr(vp, vl);
            while (vr.pos < vr.size) { uint64_t v = 0; if (!vr.ReadVarint(v)) return false; version.push_back(v); }
        }
        else if (fn == 8000 && wt == 2) { if (!ps.ReadString(magic)) return false; }
        else if (!ps.SkipField(wt)) return false;
    }

    bool headerMagic = (raw.size() >= 3 && std::memcmp(raw.data(), "ORC", 3) == 0);
    if (magic != "ORC" && !headerMagic) return false;
    if (footerLength == 0 || footerLength > psPos) return false;

    const uint64_t footerPos = psPos - footerLength;
    std::vector<uint8_t> footerBytes;
    const wchar_t* codecNames[] = { L"NONE", L"ZLIB", L"SNAPPY", L"LZO", L"LZ4", L"ZSTD" };
    std::wstring codecStr = (compression < 6) ? codecNames[compression] : L"CODEC";

    const uint8_t* footerRaw = footerLength <= (64u << 20) ? src.Read(footerPos, static_cast<size_t>(footerLength), footerBuf) : nullptr;
    if (!footerRaw || !DecompressOrcStream(footerRaw, static_cast<size_t>(footerLength), compression, footerBytes) || footerBytes.empty()) {
        return false; // Strict anti-cheat: never claim valid ORC when FileFooter cannot be decompressed
    }

    uint64_t numberOfRows = 0;
    struct OrcTypeNode {
        uint64_t kind = 0;
        std::vector<uint32_t> subtypes;
        std::vector<std::string> fieldNames;
    };
    std::vector<OrcTypeNode> types;
    struct OrcStripe { uint64_t offset = 0, indexLen = 0, dataLen = 0, footerLen = 0, numRows = 0; };
    std::vector<OrcStripe> stripes;
    struct OrcColStat { uint64_t numValues = 0; std::wstring minMax; };
    std::vector<OrcColStat> stats;

    PbReader fr(footerBytes.data(), footerBytes.size());
    while (fr.pos < fr.size) {
        if (!fr.ReadTag(fn, wt)) return false;
        if (fn == 6 && wt == 0) {
            if (!fr.ReadVarint(numberOfRows)) return false;
        } else if (fn == 3 && wt == 2) { // StripeInformation
            const uint8_t* sp = nullptr; size_t sl = 0;
            if (!fr.ReadLengthDelimited(sp, sl)) return false;
            PbReader sr(sp, sl);
            OrcStripe st;
            uint32_t sfn = 0; uint8_t swt = 0;
            while (sr.pos < sr.size) {
                if (!sr.ReadTag(sfn, swt)) return false;
                if (sfn == 1 && swt == 0) { if (!sr.ReadVarint(st.offset)) return false; }
                else if (sfn == 2 && swt == 0) { if (!sr.ReadVarint(st.indexLen)) return false; }
                else if (sfn == 3 && swt == 0) { if (!sr.ReadVarint(st.dataLen)) return false; }
                else if (sfn == 4 && swt == 0) { if (!sr.ReadVarint(st.footerLen)) return false; }
                else if (sfn == 5 && swt == 0) { if (!sr.ReadVarint(st.numRows)) return false; }
                else if (!sr.SkipField(swt)) return false;
            }
            stripes.push_back(st);
        } else if (fn == 4 && wt == 2) { // Type
            const uint8_t* tp = nullptr; size_t tl = 0;
            if (!fr.ReadLengthDelimited(tp, tl)) return false;
            PbReader tr(tp, tl);
            OrcTypeNode tn;
            uint32_t tfn = 0; uint8_t twt = 0;
            while (tr.pos < tr.size) {
                if (!tr.ReadTag(tfn, twt)) return false;
                if (tfn == 1 && twt == 0) { if (!tr.ReadVarint(tn.kind)) return false; }
                else if (tfn == 2 && twt == 0) { uint64_t s = 0; if (!tr.ReadVarint(s)) return false; tn.subtypes.push_back(static_cast<uint32_t>(s)); }
                else if (tfn == 2 && twt == 2) {
                    const uint8_t* xp = nullptr; size_t xl = 0;
                    if (!tr.ReadLengthDelimited(xp, xl)) return false;
                    PbReader xr(xp, xl);
                    while (xr.pos < xr.size) { uint64_t s = 0; if (!xr.ReadVarint(s)) return false; tn.subtypes.push_back(static_cast<uint32_t>(s)); }
                } else if (tfn == 3 && twt == 2) {
                    std::string nm; if (!tr.ReadString(nm)) return false; tn.fieldNames.push_back(std::move(nm));
                } else if (!tr.SkipField(twt)) return false;
            }
            types.push_back(std::move(tn));
        } else if (fn == 7 && wt == 2) { // ColumnStatistics
            const uint8_t* cp = nullptr; size_t cl = 0;
            if (!fr.ReadLengthDelimited(cp, cl)) return false;
            PbReader cr(cp, cl);
            OrcColStat cs;
            uint32_t cfn = 0; uint8_t cwt = 0;
            while (cr.pos < cr.size) {
                if (!cr.ReadTag(cfn, cwt)) return false;
                if (cfn == 1 && cwt == 0) { if (!cr.ReadVarint(cs.numValues)) return false; }
                else if (cfn == 2 && cwt == 2) { // IntStatistics
                    const uint8_t* ip = nullptr; size_t il = 0;
                    if (cr.ReadLengthDelimited(ip, il)) {
                        PbReader ir(ip, il);
                        int64_t mn = 0, mx = 0; bool hmn = false, hmx = false;
                        uint32_t ifn = 0; uint8_t iwt = 0;
                        while (ir.pos < ir.size && ir.ReadTag(ifn, iwt)) {
                            if ((ifn == 1 || ifn == 2) && iwt == 0) {
                                uint64_t zz = 0; ir.ReadVarint(zz);
                                int64_t sv = OrcRowDetail::ZigZagDecode64(zz);
                                if (ifn == 1) { mn = sv; hmn = true; } else { mx = sv; hmx = true; }
                            } else if (!ir.SkipField(iwt)) break;
                        }
                        if (hmn || hmx) cs.minMax = L"[" + std::to_wstring(mn) + L" .. " + std::to_wstring(mx) + L"]";
                    }
                } else if (cfn == 4 && cwt == 2) { // StringStatistics
                    const uint8_t* sp = nullptr; size_t sl = 0;
                    if (cr.ReadLengthDelimited(sp, sl)) {
                        PbReader sr(sp, sl);
                        std::string mn, mx;
                        uint32_t sfn = 0; uint8_t swt = 0;
                        while (sr.pos < sr.size && sr.ReadTag(sfn, swt)) {
                            if (sfn == 1 && swt == 2) sr.ReadString(mn);
                            else if (sfn == 2 && swt == 2) sr.ReadString(mx);
                            else if (!sr.SkipField(swt)) break;
                        }
                        if (!mn.empty() || !mx.empty()) cs.minMax = L"[" + PathUtils::Utf8ToWide(mn) + L" .. " + PathUtils::Utf8ToWide(mx) + L"]";
                    }
                } else if (!cr.SkipField(cwt)) return false;
            }
            stats.push_back(std::move(cs));
        } else if (!fr.SkipField(wt)) {
            return false;
        }
    }

    if (types.empty()) return false;

    // Decode up to 500 data rows from stripes when valid StripeFooters & column streams are present
    SpreadsheetSheet dataSh;
    dataSh.name = L"ORC_Data";
    const bool hasRootStruct = (types[0].kind == 12 && !types[0].subtypes.empty());
    if (hasRootStruct) {
        std::vector<std::wstring> hdrRow;
        for (size_t c = 0; c < types[0].subtypes.size(); ++c) {
            std::wstring cname = (c < types[0].fieldNames.size() && !types[0].fieldNames[c].empty())
                ? PathUtils::Utf8ToWide(types[0].fieldNames[c])
                : (L"col_" + std::to_wstring(c));
            hdrRow.push_back(std::move(cname));
        }
        dataSh.rows.push_back(std::move(hdrRow));

        constexpr size_t kMaxOrcPreviewRows = 500;
        size_t totalRowsDecoded = 0;
        std::vector<uint8_t> sfBuf, strmBuf;

        for (size_t si = 0; si < stripes.size() && totalRowsDecoded < kMaxOrcPreviewRows; ++si) {
            const auto& st = stripes[si];
            if (st.numRows == 0 || st.footerLen == 0 || st.footerLen > (16u << 20)) continue;
            if (st.offset > footerPos || st.indexLen > footerPos - st.offset ||
                st.dataLen > footerPos - st.offset - st.indexLen ||
                st.footerLen > footerPos - st.offset - st.indexLen - st.dataLen) {
                continue;
            }

            uint64_t sfOff = st.offset + st.indexLen + st.dataLen;
            const uint8_t* sfRaw = src.Read(sfOff, static_cast<size_t>(st.footerLen), sfBuf);
            std::vector<uint8_t> sfBytes;
            if (!sfRaw || !DecompressOrcStream(sfRaw, static_cast<size_t>(st.footerLen), compression, sfBytes) || sfBytes.empty()) {
                continue;
            }

            struct OrcStreamSpan { uint64_t kind = UINT64_MAX; uint32_t col = 0; uint64_t off = 0; uint64_t len = 0; };
            struct OrcColEnc { uint64_t kind = 0; uint32_t dictSize = 0; };
            std::vector<OrcStreamSpan> sfStreams;
            std::vector<OrcColEnc> colEncs;
            uint64_t runOff = st.offset;
            bool sfValid = true;

            PbReader sfr(sfBytes.data(), sfBytes.size());
            uint32_t sfn = 0; uint8_t swt = 0;
            while (sfr.pos < sfr.size) {
                if (!sfr.ReadTag(sfn, swt)) { sfValid = false; break; }
                if (sfn == 1 && swt == 2) { // Stream
                    const uint8_t* p2 = nullptr; size_t l2 = 0;
                    if (!sfr.ReadLengthDelimited(p2, l2)) { sfValid = false; break; }
                    PbReader r2(p2, l2);
                    OrcStreamSpan sp;
                    sp.off = runOff;
                    uint32_t f2 = 0; uint8_t w2 = 0;
                    while (r2.pos < r2.size) {
                        if (!r2.ReadTag(f2, w2)) { sfValid = false; break; }
                        if (f2 == 1 && w2 == 0) r2.ReadVarint(sp.kind);
                        else if (f2 == 2 && w2 == 0) { uint64_t cv = 0; r2.ReadVarint(cv); sp.col = static_cast<uint32_t>(cv); }
                        else if (f2 == 3 && w2 == 0) r2.ReadVarint(sp.len);
                        else if (!r2.SkipField(w2)) { sfValid = false; break; }
                    }
                    runOff += sp.len;
                    sfStreams.push_back(sp);
                } else if (sfn == 2 && swt == 2) { // ColumnEncoding
                    const uint8_t* p2 = nullptr; size_t l2 = 0;
                    if (!sfr.ReadLengthDelimited(p2, l2)) { sfValid = false; break; }
                    PbReader r2(p2, l2);
                    OrcColEnc ce;
                    uint32_t f2 = 0; uint8_t w2 = 0;
                    while (r2.pos < r2.size) {
                        if (!r2.ReadTag(f2, w2)) { sfValid = false; break; }
                        if (f2 == 1 && w2 == 0) r2.ReadVarint(ce.kind);
                        else if (f2 == 2 && w2 == 0) { uint64_t dv = 0; r2.ReadVarint(dv); ce.dictSize = static_cast<uint32_t>(dv); }
                        else if (!r2.SkipField(w2)) { sfValid = false; break; }
                    }
                    colEncs.push_back(ce);
                } else if (!sfr.SkipField(swt)) {
                    sfValid = false;
                    break;
                }
            }
            if (!sfValid || sfStreams.empty()) continue;

            const size_t nCols = types[0].subtypes.size();
            const size_t wantRows = static_cast<size_t>(std::min<uint64_t>(st.numRows, kMaxOrcPreviewRows - totalRowsDecoded));
            std::vector<std::vector<std::wstring>> colCells(nCols, std::vector<std::wstring>(wantRows));

            auto loadStream = [&](uint32_t colId, uint64_t wantKind, std::vector<uint8_t>& outDec) -> bool {
                outDec.clear();
                for (const auto& sp : sfStreams) {
                    if (sp.col == colId && sp.kind == wantKind) {
                        if (sp.len == 0) return true;
                        if (sp.len > (32u << 20) || sp.off + sp.len > sfOff) return false;
                        const uint8_t* sraw = src.Read(sp.off, static_cast<size_t>(sp.len), strmBuf);
                        return sraw && DecompressOrcStream(sraw, static_cast<size_t>(sp.len), compression, outDec);
                    }
                }
                return false;
            };

            bool anyColDecoded = false;
            for (size_t c = 0; c < nCols; ++c) {
                uint32_t colId = types[0].subtypes[c];
                if (colId >= types.size()) continue;
                uint64_t tKind = types[colId].kind;
                OrcColEnc enc = (colId < colEncs.size()) ? colEncs[colId] : OrcColEnc{};
                bool isV2 = (enc.kind == 2 || enc.kind == 3);
                bool isDict = (enc.kind == 1 || enc.kind == 3);

                std::vector<bool> present(wantRows, true);
                std::vector<uint8_t> presBytes;
                if (loadStream(colId, 0, presBytes) && !presBytes.empty()) {
                    if (!OrcRowDetail::DecodeOrcBitRle(presBytes.data(), presBytes.size(), wantRows, present)) continue;
                }
                size_t nonNullCnt = 0;
                for (size_t r = 0; r < wantRows; ++r) if (present[r]) ++nonNullCnt;

                std::vector<std::wstring> vals;
                vals.reserve(nonNullCnt);

                std::vector<uint8_t> dataBytes, lenBytes, dictBytes, secBytes;
                loadStream(colId, 1, dataBytes);

                if (nonNullCnt == 0) {
                    anyColDecoded = true;
                } else if (tKind == 0) { // BOOLEAN
                    std::vector<bool> bvals;
                    if (OrcRowDetail::DecodeOrcBitRle(dataBytes.data(), dataBytes.size(), nonNullCnt, bvals)) {
                        for (bool bv : bvals) vals.push_back(bv ? L"TRUE" : L"FALSE");
                        anyColDecoded = true;
                    }
                } else if (tKind == 1) { // BYTE
                    std::vector<uint8_t> ivals;
                    if (OrcRowDetail::DecodeOrcByteRle(dataBytes.data(), dataBytes.size(), nonNullCnt, ivals)) {
                        for (uint8_t iv : ivals) vals.push_back(std::to_wstring(static_cast<int>(static_cast<int8_t>(iv))));
                        anyColDecoded = true;
                    }
                } else if (tKind == 2 || tKind == 3 || tKind == 4) { // SHORT / INT / LONG
                    std::vector<int64_t> ivals;
                    if (OrcRowDetail::DecodeOrcIntRle(dataBytes, nonNullCnt, true, isV2, ivals)) {
                        for (int64_t iv : ivals) vals.push_back(std::to_wstring(iv));
                        anyColDecoded = true;
                    }
                } else if (tKind == 5 && dataBytes.size() >= nonNullCnt * 4u) { // FLOAT
                    for (size_t k = 0; k < nonNullCnt; ++k) vals.push_back(FmtDouble(RdF32LE(dataBytes.data() + k * 4u)));
                    anyColDecoded = true;
                } else if (tKind == 6 && dataBytes.size() >= nonNullCnt * 8u) { // DOUBLE
                    for (size_t k = 0; k < nonNullCnt; ++k) vals.push_back(FmtDouble(RdF64LE(dataBytes.data() + k * 8u)));
                    anyColDecoded = true;
                } else if (tKind == 7 || tKind == 16 || tKind == 17 || tKind == 8) { // STRING / VARCHAR / CHAR / BINARY
                    loadStream(colId, 2, lenBytes);
                    auto fmtSlice = [&](const uint8_t* p, size_t n) -> std::wstring {
                        if (tKind == 8) return PathUtils::HexBytes(p, n, L' ', 24);
                        return PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), std::min<size_t>(n, 512u)));
                    };
                    if (!isDict) {
                        std::vector<int64_t> lens;
                        if (OrcRowDetail::DecodeOrcIntRle(lenBytes, nonNullCnt, false, isV2, lens)) {
                            size_t dpos = 0;
                            for (size_t k = 0; k < nonNullCnt; ++k) {
                                size_t sl = (lens[k] > 0) ? static_cast<size_t>(lens[k]) : 0u;
                                if (sl > dataBytes.size() - dpos) break;
                                vals.push_back(fmtSlice(dataBytes.data() + dpos, sl));
                                dpos += sl;
                            }
                            if (vals.size() == nonNullCnt) anyColDecoded = true;
                        }
                    } else {
                        loadStream(colId, 3, dictBytes);
                        std::vector<int64_t> dlens, indices;
                        if (OrcRowDetail::DecodeOrcIntRle(lenBytes, enc.dictSize, false, isV2, dlens) &&
                            OrcRowDetail::DecodeOrcIntRle(dataBytes, nonNullCnt, false, isV2, indices)) {
                            std::vector<std::wstring> dict(enc.dictSize);
                            size_t dpos = 0;
                            for (size_t d = 0; d < enc.dictSize; ++d) {
                                size_t sl = (dlens[d] > 0) ? static_cast<size_t>(dlens[d]) : 0u;
                                if (sl > dictBytes.size() - dpos) break;
                                dict[d] = fmtSlice(dictBytes.data() + dpos, sl);
                                dpos += sl;
                            }
                            for (size_t k = 0; k < nonNullCnt; ++k) {
                                size_t idx = (indices[k] >= 0) ? static_cast<size_t>(indices[k]) : SIZE_MAX;
                                vals.push_back(idx < dict.size() ? dict[idx] : L"");
                            }
                            anyColDecoded = true;
                        }
                    }
                } else if (tKind == 15) { // DATE (days since 1970-01-01)
                    std::vector<int64_t> days;
                    if (OrcRowDetail::DecodeOrcIntRle(dataBytes, nonNullCnt, true, isV2, days)) {
                        for (int64_t dv : days) vals.push_back(OrcRowDetail::FormatOrcDate(dv));
                        anyColDecoded = true;
                    }
                } else if (tKind == 9) { // TIMESTAMP (DATA seconds since 2015-01-01 + SECONDARY nanos)
                    loadStream(colId, 4, secBytes);
                    std::vector<int64_t> secs, nanos;
                    if (OrcRowDetail::DecodeOrcIntRle(dataBytes, nonNullCnt, true, isV2, secs)) {
                        if (!secBytes.empty()) OrcRowDetail::DecodeOrcIntRle(secBytes, nonNullCnt, false, isV2, nanos);
                        for (size_t k = 0; k < nonNullCnt; ++k) {
                            uint64_t nv = (k < nanos.size() && nanos[k] >= 0) ? static_cast<uint64_t>(nanos[k]) : 0ULL;
                            vals.push_back(OrcRowDetail::FormatOrcTimestamp(secs[k], nv));
                        }
                        anyColDecoded = true;
                    }
                }

                size_t vi = 0;
                for (size_t r = 0; r < wantRows; ++r) {
                    if (!present[r]) colCells[c][r] = L"NULL";
                    else if (vi < vals.size()) colCells[c][r] = std::move(vals[vi++]);
                }
            }

            if (anyColDecoded) {
                for (size_t r = 0; r < wantRows; ++r) {
                    std::vector<std::wstring> row(nCols);
                    for (size_t c = 0; c < nCols; ++c) row[c] = std::move(colCells[c][r]);
                    dataSh.rows.push_back(std::move(row));
                }
                totalRowsDecoded += wantRows;
            }
        }
    }

    SpreadsheetSheet sh;
    sh.name = L"ORC_Schema_And_Stats";
    sh.rows.push_back({ L"Type ID / Metric", L"Column Name", L"ORC Type", L"Values", L"Statistics / Info" });
    std::wstring verStr = version.size() >= 2 ? (std::to_wstring(version[0]) + L"." + std::to_wstring(version[1])) : L"0.12";
    sh.rows.push_back({ L"(PostScript)", L"Format Version " + verStr, L"Codec: " + codecStr, std::to_wstring(numberOfRows) + L" rows", L"BlockSize=" + std::to_wstring(compBlockSize) });

    std::unordered_map<uint32_t, std::string> typeNames;
    for (const auto& t : types) {
        for (size_t i = 0; i < t.subtypes.size() && i < t.fieldNames.size(); ++i) {
            typeNames[t.subtypes[i]] = t.fieldNames[i];
        }
    }
    for (size_t i = 0; i < types.size(); ++i) {
        std::wstring nm = (i == 0) ? L"(root_struct)" : PathUtils::Utf8ToWide(typeNames[static_cast<uint32_t>(i)]);
        std::wstring valCnt = (i < stats.size()) ? std::to_wstring(stats[i].numValues) : L"";
        std::wstring mm = (i < stats.size()) ? stats[i].minMax : L"";
        sh.rows.push_back({ std::to_wstring(i), nm, OrcTypeKindName(types[i].kind), valCnt, mm });
    }
    FinalizeSheetCols(sh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Apache ORC Columnar Storage (.orc)";
    if (dataSh.rows.size() > 1) {
        FinalizeSheetCols(dataSh);
        out.sheets.push_back(std::move(dataSh));
    }
    out.sheets.push_back(std::move(sh));

    if (!stripes.empty()) {
        SpreadsheetSheet stSh;
        stSh.name = L"ORC_Stripes";
        stSh.rows.push_back({ L"Stripe #", L"Offset", L"Index Len", L"Data Len", L"Footer Len", L"Rows" });
        for (size_t i = 0; i < stripes.size(); ++i) {
            stSh.rows.push_back({
                std::to_wstring(i),
                std::to_wstring(stripes[i].offset),
                std::to_wstring(stripes[i].indexLen),
                std::to_wstring(stripes[i].dataLen),
                std::to_wstring(stripes[i].footerLen),
                std::to_wstring(stripes[i].numRows)
            });
        }
        FinalizeSheetCols(stSh);
        out.sheets.push_back(std::move(stSh));
    }
    if (!src.ReadFailed()) out.sourceReadFully = true;
    out.valid = true;
    return true;
}

} // namespace AiDataDetail

inline bool ParseApacheOrc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return AiDataDetail::ParseApacheOrc(raw, out); }

} // namespace Formats
