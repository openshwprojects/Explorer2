#pragma once
// Amf.h — Additive Manufacturing File Format (.amf, ISO/ASTM 52915) XML + PK-ZIP 3D Mesh Parser

namespace Formats {

namespace DetailPkZip {

inline uint16_t RdU16(const uint8_t* p) {
    uint16_t v = 0;
    std::memcpy(&v, p, 2);
    return v;
}

inline uint32_t RdU32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

struct HuffTable15 {
    std::vector<uint16_t> lut; // 32768 entries: (codeLen << 9) | sym
    bool empty = true;

    bool Build(const uint8_t* lengths, size_t numSyms) {
        lut.assign(32768u, 0u);
        uint32_t blCount[16] = {};
        uint32_t totalNonZero = 0;
        for (size_t i = 0; i < numSyms; ++i) {
            uint8_t l = lengths[i];
            if (l > 15u) return false;
            if (l > 0u) {
                ++blCount[l];
                ++totalNonZero;
            }
        }
        if (totalNonZero == 0u) {
            empty = true;
            return true;
        }
        empty = false;

        uint32_t nextCode[16] = {};
        uint32_t code = 0;
        for (uint32_t bits = 1; bits <= 15; ++bits) {
            code = (code + blCount[bits - 1]) << 1;
            if (code + blCount[bits] > (1u << bits)) return false; // oversubscribed tree
            nextCode[bits] = code;
        }

        for (size_t sym = 0; sym < numSyms; ++sym) {
            uint32_t len = lengths[sym];
            if (len == 0u) continue;
            uint32_t c = nextCode[len]++;
            // Bit-reverse c across len bits for LSB-first bitstream lookup
            uint32_t rev = 0;
            for (uint32_t b = 0; b < len; ++b) {
                rev = (rev << 1) | ((c >> b) & 1u);
            }
            uint16_t packed = static_cast<uint16_t>((len << 9) | (sym & 0x1FFu));
            uint32_t step = 1u << len;
            for (uint32_t idx = rev; idx < 32768u; idx += step) {
                lut[idx] = packed;
            }
        }
        return true;
    }
};

// Bounds-safe RFC 1951 raw Deflate decompressor
inline bool InflateRaw(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t maxOutBytes) {
    out.clear();
    uint64_t bitBuf = 0;
    int bitCount = 0;
    size_t bytePos = 0;

    auto ensureBits = [&](int need) {
        while (bitCount < need && bytePos < srcLen) {
            bitBuf |= static_cast<uint64_t>(src[bytePos++]) << bitCount;
            bitCount += 8;
        }
    };
    auto readBits = [&](int n, uint32_t& dst) -> bool {
        if (n == 0) { dst = 0; return true; }
        ensureBits(n);
        if (bitCount < n) return false;
        dst = static_cast<uint32_t>(bitBuf & ((1ULL << n) - 1ULL));
        bitBuf >>= n;
        bitCount -= n;
        return true;
    };
    auto decodeSym = [&](const HuffTable15& ht, uint32_t& symOut) -> bool {
        if (ht.empty) return false;
        ensureBits(15);
        uint16_t entry = ht.lut[static_cast<size_t>(bitBuf & 0x7FFFu)];
        uint32_t len = entry >> 9;
        if (len == 0u || bitCount < static_cast<int>(len)) return false;
        bitBuf >>= len;
        bitCount -= static_cast<int>(len);
        symOut = entry & 0x1FFu;
        return true;
    };

    static constexpr uint16_t kLenBase[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static constexpr uint8_t kLenExtra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    static constexpr uint16_t kDistBase[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
    };
    static constexpr uint8_t kDistExtra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    static constexpr uint8_t kClOrder[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    HuffTable15 litTree, distTree, clTree;
    uint32_t bfinal = 0;
    while (bfinal == 0u) {
        uint32_t btype = 0;
        if (!readBits(1, bfinal) || !readBits(2, btype)) return false;

        if (btype == 0u) {
            // Uncompressed stored block
            int remBits = bitCount & 7;
            bitBuf >>= remBits;
            bitCount -= remBits;
            size_t bufferedBytes = static_cast<size_t>(bitCount / 8);
            if (bufferedBytes > bytePos) return false;
            bytePos -= bufferedBytes;
            bitBuf = 0;
            bitCount = 0;

            if (bytePos + 4 > srcLen) return false;
            uint16_t len  = RdU16(src + bytePos);
            uint16_t nlen = RdU16(src + bytePos + 2);
            bytePos += 4;
            if ((len ^ 0xFFFFu) != nlen) return false;
            if (bytePos + len > srcLen || out.size() + len > maxOutBytes) return false;
            out.insert(out.end(), src + bytePos, src + bytePos + len);
            bytePos += len;
            continue;
        }

        if (btype == 1u) {
            uint8_t litLens[288];
            for (size_t i = 0;   i <= 143; ++i) litLens[i] = 8;
            for (size_t i = 144; i <= 255; ++i) litLens[i] = 9;
            for (size_t i = 256; i <= 279; ++i) litLens[i] = 7;
            for (size_t i = 280; i <= 287; ++i) litLens[i] = 8;
            uint8_t distLens[32];
            for (size_t i = 0; i < 32; ++i) distLens[i] = 5;
            if (!litTree.Build(litLens, 288) || !distTree.Build(distLens, 32)) return false;
        } else if (btype == 2u) {
            uint32_t hlit = 0, hdist = 0, hclen = 0;
            if (!readBits(5, hlit) || !readBits(5, hdist) || !readBits(4, hclen)) return false;
            hlit  += 257u;
            hdist += 1u;
            hclen += 4u;
            if (hlit > 286u || hdist > 32u) return false;

            uint8_t clLens[19] = {};
            for (uint32_t i = 0; i < hclen; ++i) {
                uint32_t v = 0;
                if (!readBits(3, v)) return false;
                clLens[kClOrder[i]] = static_cast<uint8_t>(v);
            }
            if (!clTree.Build(clLens, 19) || clTree.empty) return false;

            uint8_t allLens[320] = {};
            uint32_t totalCodes = hlit + hdist;
            uint32_t idx = 0;
            while (idx < totalCodes) {
                uint32_t sym = 0;
                if (!decodeSym(clTree, sym)) return false;
                if (sym <= 15u) {
                    allLens[idx++] = static_cast<uint8_t>(sym);
                } else if (sym == 16u) {
                    if (idx == 0u) return false;
                    uint32_t rep = 0;
                    if (!readBits(2, rep)) return false;
                    rep += 3u;
                    if (idx + rep > totalCodes) return false;
                    uint8_t prev = allLens[idx - 1];
                    for (uint32_t k = 0; k < rep; ++k) allLens[idx++] = prev;
                } else if (sym == 17u) {
                    uint32_t rep = 0;
                    if (!readBits(3, rep)) return false;
                    rep += 3u;
                    if (idx + rep > totalCodes) return false;
                    for (uint32_t k = 0; k < rep; ++k) allLens[idx++] = 0;
                } else if (sym == 18u) {
                    uint32_t rep = 0;
                    if (!readBits(7, rep)) return false;
                    rep += 11u;
                    if (idx + rep > totalCodes) return false;
                    for (uint32_t k = 0; k < rep; ++k) allLens[idx++] = 0;
                } else {
                    return false;
                }
            }
            if (!litTree.Build(allLens, hlit) || litTree.empty) return false;
            if (!distTree.Build(allLens + hlit, hdist)) return false;
        } else {
            return false;
        }

        // Decode Huffman-compressed literals & LZ77 back-references
        for (;;) {
            uint32_t sym = 0;
            if (!decodeSym(litTree, sym)) return false;
            if (sym < 256u) {
                if (out.size() >= maxOutBytes) return false;
                out.push_back(static_cast<uint8_t>(sym));
            } else if (sym == 256u) {
                break;
            } else if (sym <= 285u) {
                uint32_t lenIdx = sym - 257u;
                uint32_t extra = 0;
                if (!readBits(kLenExtra[lenIdx], extra)) return false;
                size_t matchLen = static_cast<size_t>(kLenBase[lenIdx]) + static_cast<size_t>(extra);

                uint32_t distSym = 0;
                if (!decodeSym(distTree, distSym) || distSym >= 30u) return false;
                uint32_t distExtra = 0;
                if (!readBits(kDistExtra[distSym], distExtra)) return false;
                size_t dist = static_cast<size_t>(kDistBase[distSym]) + static_cast<size_t>(distExtra);

                if (dist == 0 || dist > out.size() || out.size() + matchLen > maxOutBytes) return false;
                for (size_t k = 0; k < matchLen; ++k) {
                    out.push_back(out[out.size() - dist]);
                }
            } else {
                return false;
            }
        }
    }
    return true;
}

struct ZipEntryInfo {
    std::string name;
    uint16_t method = 0;
    uint32_t compSize = 0;
    uint32_t uncompSize = 0;
    uint32_t localHeaderOffset = 0;
};

inline std::string NormalizeZipPath(std::string_view p) {
    while (!p.empty() && (p.front() == '/' || p.front() == '\\')) p.remove_prefix(1);
    std::string out;
    out.reserve(p.size());
    for (char c : p) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        out.push_back(c);
    }
    return out;
}

inline bool ListZipEntries(const uint8_t* data, size_t size, std::vector<ZipEntryInfo>& entries) {
    entries.clear();
    if (!data || size < 30 || std::memcmp(data, "PK\x03\x04", 4) != 0) return false;

    // 1. Prefer Central Directory via End-Of-Central-Directory record (0x06054B50)
    size_t searchSpan = std::min<size_t>(size, 65557u);
    size_t minOff = size - searchSpan;
    size_t eocdOff = SIZE_MAX;
    if (size >= 22) {
        for (size_t pos = size - 22; ; --pos) {
            if (RdU32(data + pos) == 0x06054B50u) {
                uint16_t commentLen = RdU16(data + pos + 20);
                if (pos + 22 + commentLen <= size) {
                    eocdOff = pos;
                    break;
                }
            }
            if (pos == minOff) break;
        }
    }

    if (eocdOff != SIZE_MAX) {
        uint16_t totalEntries = RdU16(data + eocdOff + 10);
        uint32_t cdSize       = RdU32(data + eocdOff + 12);
        uint32_t cdOffset     = RdU32(data + eocdOff + 16);
        if (static_cast<uint64_t>(cdOffset) + static_cast<uint64_t>(cdSize) <= size) {
            size_t cur = cdOffset;
            size_t cdEnd = cdOffset + cdSize;
            for (uint16_t i = 0; i < totalEntries && cur + 46 <= cdEnd; ++i) {
                if (RdU32(data + cur) != 0x02014B50u) break;
                uint16_t method    = RdU16(data + cur + 10);
                uint32_t compSz    = RdU32(data + cur + 20);
                uint32_t uncompSz  = RdU32(data + cur + 24);
                uint16_t nameLen   = RdU16(data + cur + 28);
                uint16_t extraLen  = RdU16(data + cur + 30);
                uint16_t commLen   = RdU16(data + cur + 32);
                uint32_t localOff  = RdU32(data + cur + 42);
                if (cur + 46 + nameLen + extraLen + commLen > cdEnd) break;

                ZipEntryInfo info;
                info.name.assign(reinterpret_cast<const char*>(data + cur + 46), nameLen);
                info.method = method;
                info.compSize = compSz;
                info.uncompSize = uncompSz;
                info.localHeaderOffset = localOff;
                entries.push_back(std::move(info));

                cur += 46 + static_cast<size_t>(nameLen) + static_cast<size_t>(extraLen) + static_cast<size_t>(commLen);
            }
            if (!entries.empty()) return true;
        }
    }

    // 2. Fallback: walk sequential Local File Headers (0x04034B50)
    size_t cur = 0;
    while (cur + 30 <= size && entries.size() < 8192) {
        if (RdU32(data + cur) != 0x04034B50u) break;
        uint16_t flags    = RdU16(data + cur + 6);
        uint16_t method   = RdU16(data + cur + 8);
        uint32_t compSz   = RdU32(data + cur + 18);
        uint32_t uncompSz = RdU32(data + cur + 22);
        uint16_t nameLen  = RdU16(data + cur + 26);
        uint16_t extraLen = RdU16(data + cur + 28);
        if ((flags & 0x08u) != 0 && compSz == 0) break; // data descriptor requires Central Directory
        size_t dataStart = cur + 30 + static_cast<size_t>(nameLen) + static_cast<size_t>(extraLen);
        if (dataStart > size || compSz > size - dataStart) break;

        ZipEntryInfo info;
        info.name.assign(reinterpret_cast<const char*>(data + cur + 30), nameLen);
        info.method = method;
        info.compSize = compSz;
        info.uncompSize = uncompSz;
        info.localHeaderOffset = static_cast<uint32_t>(cur);
        entries.push_back(std::move(info));

        cur = dataStart + compSz;
    }
    return !entries.empty();
}

inline bool ExtractZipEntry(
    const uint8_t* data,
    size_t size,
    const ZipEntryInfo& entry,
    std::vector<uint8_t>& out,
    size_t maxOutBytes = 64ULL * 1024ULL * 1024ULL)
{
    size_t loc = entry.localHeaderOffset;
    if (loc + 30 > size || RdU32(data + loc) != 0x04034B50u) return false;
    uint16_t method   = RdU16(data + loc + 8);
    uint16_t nameLen  = RdU16(data + loc + 26);
    uint16_t extraLen = RdU16(data + loc + 28);
    if (entry.method != 0) method = entry.method;

    size_t payloadOff = loc + 30 + static_cast<size_t>(nameLen) + static_cast<size_t>(extraLen);
    if (payloadOff > size) return false;
    uint32_t compSz = entry.compSize;
    if (compSz == 0) compSz = RdU32(data + loc + 18);
    if (static_cast<size_t>(compSz) > size - payloadOff) return false;

    if (method == 0u) {
        if (compSz > maxOutBytes) return false;
        out.assign(data + payloadOff, data + payloadOff + compSz);
        return true;
    }
    if (method == 8u) {
        if (entry.uncompSize > 0 && entry.uncompSize <= maxOutBytes) {
            out.reserve(entry.uncompSize);
        }
        return InflateRaw(data + payloadOff, compSz, out, maxOutBytes);
    }
    return false;
}

} // namespace DetailPkZip

namespace DetailAmf {

struct XmlTag {
    std::string_view localName;
    std::string_view attrs;
    size_t tagStart = 0;
    size_t afterTag = 0;
    bool isClosing = false;
    bool isSelfClosing = false;
};

inline bool IsWs(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline bool IsNameChar(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
           (u >= '0' && u <= '9') || c == '_' || c == '-' || c == ':' || c == '.';
}

inline std::string_view TrimWs(std::string_view s) {
    while (!s.empty() && IsWs(s.front())) s.remove_prefix(1);
    while (!s.empty() && IsWs(s.back())) s.remove_suffix(1);
    return s;
}

inline bool NextTag(std::string_view sv, size_t& pos, XmlTag& out) {
    const size_t n = sv.size();
    while (pos < n) {
        size_t lt = sv.find('<', pos);
        if (lt == std::string_view::npos || lt + 1 >= n) {
            pos = n;
            return false;
        }
        if (lt + 4 <= n && sv.substr(lt, 4) == "<!--") {
            size_t endC = sv.find("-->", lt + 4);
            pos = (endC == std::string_view::npos) ? n : (endC + 3);
            continue;
        }
        if (lt + 9 <= n && sv.substr(lt, 9) == "<![CDATA[") {
            size_t endCd = sv.find("]]>", lt + 9);
            pos = (endCd == std::string_view::npos) ? n : (endCd + 3);
            continue;
        }
        if (sv[lt + 1] == '?') {
            size_t endPi = sv.find("?>", lt + 2);
            pos = (endPi == std::string_view::npos) ? n : (endPi + 2);
            continue;
        }
        if (sv[lt + 1] == '!') {
            size_t gt = sv.find('>', lt + 2);
            pos = (gt == std::string_view::npos) ? n : (gt + 1);
            continue;
        }

        size_t gt = lt + 1;
        char quote = 0;
        while (gt < n) {
            char c = sv[gt];
            if (quote != 0) {
                if (c == quote) quote = 0;
            } else {
                if (c == '"' || c == '\'') quote = c;
                else if (c == '>') break;
            }
            ++gt;
        }
        if (gt >= n) {
            pos = n;
            return false;
        }

        size_t cur = lt + 1;
        bool closing = false;
        if (cur < gt && sv[cur] == '/') {
            closing = true;
            ++cur;
        }
        while (cur < gt && IsWs(sv[cur])) ++cur;
        size_t nameStart = cur;
        while (cur < gt && IsNameChar(sv[cur])) ++cur;
        if (cur == nameStart) {
            pos = gt + 1;
            continue;
        }

        std::string_view rawName = sv.substr(nameStart, cur - nameStart);
        size_t colon = rawName.rfind(':');
        std::string_view localName = (colon == std::string_view::npos) ? rawName : rawName.substr(colon + 1);

        size_t attrEnd = gt;
        bool selfClosing = false;
        if (!closing) {
            size_t back = gt;
            while (back > cur && IsWs(sv[back - 1])) --back;
            if (back > cur && sv[back - 1] == '/') {
                selfClosing = true;
                attrEnd = back - 1;
            }
        }

        out.localName = localName;
        out.attrs = (cur <= attrEnd) ? sv.substr(cur, attrEnd - cur) : std::string_view{};
        out.tagStart = lt;
        out.afterTag = gt + 1;
        out.isClosing = closing;
        out.isSelfClosing = selfClosing;
        pos = gt + 1;
        return true;
    }
    return false;
}

inline bool FindElementBody(
    std::string_view sv,
    size_t afterOpenPos,
    std::string_view localName,
    std::string_view& bodyOut,
    size_t& afterClosePos)
{
    size_t cur = afterOpenPos;
    int depth = 1;
    XmlTag t;
    while (NextTag(sv, cur, t)) {
        if (t.localName == localName) {
            if (t.isClosing) {
                --depth;
                if (depth == 0) {
                    bodyOut = sv.substr(afterOpenPos, t.tagStart - afterOpenPos);
                    afterClosePos = cur;
                    return true;
                }
            } else if (!t.isSelfClosing) {
                ++depth;
            }
        }
    }
    return false;
}

inline std::string_view GetAttr(std::string_view attrs, std::string_view wantName) {
    size_t i = 0;
    const size_t n = attrs.size();
    while (i < n) {
        while (i < n && IsWs(attrs[i])) ++i;
        size_t nameStart = i;
        while (i < n && IsNameChar(attrs[i])) ++i;
        if (i == nameStart) { ++i; continue; }
        std::string_view rawName = attrs.substr(nameStart, i - nameStart);
        while (i < n && IsWs(attrs[i])) ++i;
        if (i >= n || attrs[i] != '=') continue;
        ++i;
        while (i < n && IsWs(attrs[i])) ++i;
        if (i >= n || (attrs[i] != '"' && attrs[i] != '\'')) continue;
        char q = attrs[i++];
        size_t valStart = i;
        while (i < n && attrs[i] != q) ++i;
        std::string_view val = attrs.substr(valStart, i - valStart);
        if (i < n && attrs[i] == q) ++i;

        if (rawName == wantName) return val;
        size_t colon = rawName.rfind(':');
        if (colon != std::string_view::npos && rawName.substr(colon + 1) == wantName) {
            return val;
        }
    }
    return {};
}

inline float ParseFloatSv(std::string_view s, float defVal = 0.0f) {
    s = TrimWs(s);
    if (s.empty()) return defVal;
    if (s.front() == '+') s.remove_prefix(1);
    if (s.empty()) return defVal;
    float v = defVal;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{} || !std::isfinite(v)) return defVal;
    return v;
}

inline bool ParseU32Sv(std::string_view s, uint32_t& out) {
    s = TrimWs(s);
    if (s.empty()) return false;
    if (s.front() == '+') s.remove_prefix(1);
    if (s.empty()) return false;
    uint32_t v = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{}) return false;
    out = v;
    return true;
}

struct AmfMat4 {
    std::array<float, 16> m = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    static AmfMat4 Identity() { return AmfMat4{}; }

    static AmfMat4 FromEulerDegAndTranslation(float rxDeg, float ryDeg, float rzDeg, float dx, float dy, float dz) {
        constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
        float cx = std::cos(rxDeg * kDeg2Rad), sx = std::sin(rxDeg * kDeg2Rad);
        float cy = std::cos(ryDeg * kDeg2Rad), sy = std::sin(ryDeg * kDeg2Rad);
        float cz = std::cos(rzDeg * kDeg2Rad), sz = std::sin(rzDeg * kDeg2Rad);

        // R = Rz * Ry * Rx (ISO/ASTM 52915 X-then-Y-then-Z rotation + delta translation)
        AmfMat4 r;
        r.m[0]  = cz * cy;
        r.m[1]  = cz * sy * sx - sz * cx;
        r.m[2]  = cz * sy * cx + sz * sx;
        r.m[3]  = dx;

        r.m[4]  = sz * cy;
        r.m[5]  = sz * sy * sx + cz * cx;
        r.m[6]  = sz * sy * cx - cz * sx;
        r.m[7]  = dy;

        r.m[8]  = -sy;
        r.m[9]  = cy * sx;
        r.m[10] = cy * cx;
        r.m[11] = dz;
        return r;
    }

    static AmfMat4 Mul(const AmfMat4& a, const AmfMat4& b) {
        AmfMat4 r;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                r.m[row * 4 + col] =
                    a.m[row * 4 + 0] * b.m[0 * 4 + col] +
                    a.m[row * 4 + 1] * b.m[1 * 4 + col] +
                    a.m[row * 4 + 2] * b.m[2 * 4 + col] +
                    a.m[row * 4 + 3] * b.m[3 * 4 + col];
            }
        }
        return r;
    }

    Vec3f TransformPoint(const Vec3f& v) const {
        return {
            m[0] * v.x + m[1] * v.y + m[2]  * v.z + m[3],
            m[4] * v.x + m[5] * v.y + m[6]  * v.z + m[7],
            m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11]
        };
    }
};

struct AmfObject {
    std::string id;
    std::vector<Vec3f> vertices; // Native AMF Z-up coordinates
    std::vector<std::array<uint32_t, 3>> tris;
};

struct AmfInstance {
    std::string targetId;
    AmfMat4 localXf;
};

struct AmfConstellation {
    std::string id;
    std::vector<AmfInstance> instances;
};

} // namespace DetailAmf

inline bool ParseAmfMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace DetailAmf;
    if (!data || size < 12) return false;

    std::vector<uint8_t> unzippedXml;
    std::string_view sv;

    // Per ISO/ASTM 52915, .amf files are either uncompressed XML or a PK-ZIP archive containing an XML .amf stream
    if (size >= 4 && std::memcmp(data, "PK\x03\x04", 4) == 0) {
        std::vector<DetailPkZip::ZipEntryInfo> entries;
        if (!DetailPkZip::ListZipEntries(data, size, entries) || entries.empty()) {
            return false;
        }
        const DetailPkZip::ZipEntryInfo* chosen = nullptr;
        for (const auto& e : entries) {
            if (e.name.empty() || e.name.back() == '/' || e.name.back() == '\\') continue;
            std::string norm = DetailPkZip::NormalizeZipPath(e.name);
            if (norm.size() >= 4 && norm.substr(norm.size() - 4) == ".amf") {
                chosen = &e;
                break;
            }
            if (!chosen) chosen = &e;
        }
        if (!chosen) return false;
        if (!DetailPkZip::ExtractZipEntry(data, size, *chosen, unzippedXml) || unzippedXml.empty()) {
            return false;
        }
        sv = std::string_view(reinterpret_cast<const char*>(unzippedXml.data()), unzippedXml.size());
    } else {
        sv = std::string_view(reinterpret_cast<const char*>(data), size);
    }

    if (sv.find("<amf") == std::string_view::npos && sv.find(":amf") == std::string_view::npos) {
        return false;
    }
    mesh.formatName = L"Additive Manufacturing (.amf)";

    // Locate <amf> root element body
    std::string_view amfBody = sv;
    {
        size_t p = 0;
        XmlTag rt;
        while (NextTag(sv, p, rt)) {
            if (!rt.isClosing && rt.localName == "amf" && !rt.isSelfClosing) {
                std::string_view inner;
                size_t after = 0;
                if (FindElementBody(sv, p, "amf", inner, after)) {
                    amfBody = inner;
                }
                break;
            }
        }
    }

    std::vector<AmfObject> objects;
    std::unordered_map<std::string, size_t> objectById;
    std::vector<AmfConstellation> constellations;
    std::unordered_map<std::string, size_t> constelById;

    size_t pos = 0;
    XmlTag tag;
    while (NextTag(amfBody, pos, tag)) {
        if (tag.isClosing) continue;

        if (tag.localName == "object" && !tag.isSelfClosing) {
            std::string objId(GetAttr(tag.attrs, "id"));
            std::string_view objBody;
            if (!FindElementBody(amfBody, pos, "object", objBody, pos)) break;

            AmfObject obj;
            obj.id = objId;

            size_t op = 0;
            XmlTag ot;
            while (NextTag(objBody, op, ot)) {
                if (ot.isClosing) continue;
                if (ot.localName == "mesh" && !ot.isSelfClosing) {
                    std::string_view meshBody;
                    if (!FindElementBody(objBody, op, "mesh", meshBody, op)) break;

                    const uint32_t meshBaseVert = static_cast<uint32_t>(obj.vertices.size());
                    size_t mp = 0;
                    XmlTag mt;
                    while (NextTag(meshBody, mp, mt)) {
                        if (mt.isClosing) continue;
                        if (mt.localName == "vertices" && !mt.isSelfClosing) {
                            std::string_view vertsBody;
                            if (!FindElementBody(meshBody, mp, "vertices", vertsBody, mp)) break;
                            size_t vp = 0;
                            XmlTag vt;
                            while (NextTag(vertsBody, vp, vt)) {
                                if (vt.isClosing) continue;
                                if (vt.localName == "vertex" && !vt.isSelfClosing) {
                                    std::string_view vBody;
                                    if (!FindElementBody(vertsBody, vp, "vertex", vBody, vp)) break;
                                    size_t cp = 0;
                                    XmlTag ct;
                                    while (NextTag(vBody, cp, ct)) {
                                        if (ct.isClosing) continue;
                                        if (ct.localName == "coordinates" && !ct.isSelfClosing) {
                                            std::string_view cBody;
                                            if (!FindElementBody(vBody, cp, "coordinates", cBody, cp)) break;
                                            float x = 0.0f, y = 0.0f, z = 0.0f;
                                            size_t xp = 0;
                                            XmlTag xt;
                                            while (NextTag(cBody, xp, xt)) {
                                                if (xt.isClosing || xt.isSelfClosing) continue;
                                                std::string_view valTxt;
                                                if (!FindElementBody(cBody, xp, xt.localName, valTxt, xp)) break;
                                                if (xt.localName == "x") x = ParseFloatSv(valTxt);
                                                else if (xt.localName == "y") y = ParseFloatSv(valTxt);
                                                else if (xt.localName == "z") z = ParseFloatSv(valTxt);
                                            }
                                            if (obj.vertices.size() < 4000000) {
                                                obj.vertices.push_back({ x, y, z });
                                            }
                                        } else if (!ct.isSelfClosing) {
                                            std::string_view skip;
                                            FindElementBody(vBody, cp, ct.localName, skip, cp);
                                        }
                                    }
                                } else if (!vt.isSelfClosing) {
                                    std::string_view skip;
                                    FindElementBody(vertsBody, vp, vt.localName, skip, vp);
                                }
                            }
                        } else if (mt.localName == "volume" && !mt.isSelfClosing) {
                            std::string_view volBody;
                            if (!FindElementBody(meshBody, mp, "volume", volBody, mp)) break;
                            const uint32_t localVertCount = static_cast<uint32_t>(obj.vertices.size()) - meshBaseVert;
                            size_t tp = 0;
                            XmlTag tt;
                            while (NextTag(volBody, tp, tt)) {
                                if (tt.isClosing) continue;
                                if (tt.localName == "triangle" && !tt.isSelfClosing) {
                                    std::string_view tBody;
                                    if (!FindElementBody(volBody, tp, "triangle", tBody, tp)) break;
                                    uint32_t v1 = 0, v2 = 0, v3 = 0;
                                    bool has1 = false, has2 = false, has3 = false;
                                    size_t ip = 0;
                                    XmlTag it;
                                    while (NextTag(tBody, ip, it)) {
                                        if (it.isClosing || it.isSelfClosing) continue;
                                        std::string_view valTxt;
                                        if (!FindElementBody(tBody, ip, it.localName, valTxt, ip)) break;
                                        if (it.localName == "v1" || it.localName == "p1") has1 = ParseU32Sv(valTxt, v1);
                                        else if (it.localName == "v2" || it.localName == "p2") has2 = ParseU32Sv(valTxt, v2);
                                        else if (it.localName == "v3" || it.localName == "p3") has3 = ParseU32Sv(valTxt, v3);
                                    }
                                    if (has1 && has2 && has3 &&
                                        v1 < localVertCount && v2 < localVertCount && v3 < localVertCount &&
                                        v1 != v2 && v2 != v3 && v1 != v3 &&
                                        obj.tris.size() < 4000000) {
                                        obj.tris.push_back({ meshBaseVert + v1, meshBaseVert + v2, meshBaseVert + v3 });
                                    }
                                } else if (!tt.isSelfClosing) {
                                    std::string_view skip;
                                    FindElementBody(volBody, tp, tt.localName, skip, tp);
                                }
                            }
                        } else if (!mt.isSelfClosing) {
                            std::string_view skip;
                            FindElementBody(meshBody, mp, mt.localName, skip, mp);
                        }
                    }
                } else if (!ot.isSelfClosing) {
                    std::string_view skip;
                    FindElementBody(objBody, op, ot.localName, skip, op);
                }
            }

            if (!obj.vertices.empty() && !obj.tris.empty()) {
                size_t idx = objects.size();
                if (!obj.id.empty()) objectById[obj.id] = idx;
                objects.push_back(std::move(obj));
            }
        } else if (tag.localName == "constellation" && !tag.isSelfClosing) {
            std::string cId(GetAttr(tag.attrs, "id"));
            std::string_view cBody;
            if (!FindElementBody(amfBody, pos, "constellation", cBody, pos)) break;

            AmfConstellation con;
            con.id = cId;

            size_t cp = 0;
            XmlTag ct;
            while (NextTag(cBody, cp, ct)) {
                if (ct.isClosing) continue;
                if (ct.localName == "instance") {
                    std::string targetId(GetAttr(ct.attrs, "objectid"));
                    float dx = ParseFloatSv(GetAttr(ct.attrs, "deltax"), 0.0f);
                    float dy = ParseFloatSv(GetAttr(ct.attrs, "deltay"), 0.0f);
                    float dz = ParseFloatSv(GetAttr(ct.attrs, "deltaz"), 0.0f);
                    float rx = ParseFloatSv(GetAttr(ct.attrs, "rx"), 0.0f);
                    float ry = ParseFloatSv(GetAttr(ct.attrs, "ry"), 0.0f);
                    float rz = ParseFloatSv(GetAttr(ct.attrs, "rz"), 0.0f);

                    if (!ct.isSelfClosing) {
                        std::string_view instBody;
                        if (!FindElementBody(cBody, cp, "instance", instBody, cp)) break;
                        size_t ip = 0;
                        XmlTag it;
                        while (NextTag(instBody, ip, it)) {
                            if (it.isClosing || it.isSelfClosing) continue;
                            std::string_view valTxt;
                            if (!FindElementBody(instBody, ip, it.localName, valTxt, ip)) break;
                            if (it.localName == "deltax") dx = ParseFloatSv(valTxt, dx);
                            else if (it.localName == "deltay") dy = ParseFloatSv(valTxt, dy);
                            else if (it.localName == "deltaz") dz = ParseFloatSv(valTxt, dz);
                            else if (it.localName == "rx") rx = ParseFloatSv(valTxt, rx);
                            else if (it.localName == "ry") ry = ParseFloatSv(valTxt, ry);
                            else if (it.localName == "rz") rz = ParseFloatSv(valTxt, rz);
                        }
                    }

                    if (!targetId.empty()) {
                        AmfInstance inst;
                        inst.targetId = std::move(targetId);
                        inst.localXf = AmfMat4::FromEulerDegAndTranslation(rx, ry, rz, dx, dy, dz);
                        con.instances.push_back(std::move(inst));
                    }
                } else if (!ct.isSelfClosing) {
                    std::string_view skip;
                    FindElementBody(cBody, cp, ct.localName, skip, cp);
                }
            }

            if (!con.instances.empty()) {
                size_t idx = constellations.size();
                if (!con.id.empty()) constelById[con.id] = idx;
                constellations.push_back(std::move(con));
            }
        } else if (!tag.isSelfClosing) {
            std::string_view skip;
            if (!FindElementBody(amfBody, pos, tag.localName, skip, pos)) break;
        }
    }

    if (objects.empty()) return false;

    constexpr size_t kMaxVerts = 4000000;
    constexpr size_t kMaxTris  = 4000000;

    auto emitObject = [&](const AmfObject& obj, const AmfMat4& worldXf) {
        if (mesh.vertices.size() + obj.vertices.size() > kMaxVerts ||
            mesh.tris.size() + obj.tris.size() > kMaxTris) {
            return;
        }
        uint32_t baseOff = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.reserve(mesh.vertices.size() + obj.vertices.size());
        for (const Vec3f& v : obj.vertices) {
            Vec3f wp = worldXf.TransformPoint(v);
            // Convert AMF Z-up (x, y, z) -> viewer Y-up (x, z, -y)
            Vec3f yp{ wp.x, wp.z, -wp.y };
            if (!std::isfinite(yp.x) || !std::isfinite(yp.y) || !std::isfinite(yp.z)) {
                yp = { 0.0f, 0.0f, 0.0f };
            }
            mesh.vertices.push_back(yp);
        }
        mesh.tris.reserve(mesh.tris.size() + obj.tris.size());
        for (const auto& t : obj.tris) {
            mesh.tris.push_back({ baseOff + t[0], baseOff + t[1], baseOff + t[2], {} });
        }
    };

    if (!constellations.empty()) {
        std::set<std::string> childConstelIds;
        for (const auto& c : constellations) {
            for (const auto& inst : c.instances) {
                if (constelById.find(inst.targetId) != constelById.end()) {
                    childConstelIds.insert(inst.targetId);
                }
            }
        }

        std::vector<size_t> activeStack;
        std::function<void(size_t, const AmfMat4&, int)> expandConstellation =
            [&](size_t cIdx, const AmfMat4& parentXf, int depth) {
                if (depth > 16 || cIdx >= constellations.size()) return;
                if (std::find(activeStack.begin(), activeStack.end(), cIdx) != activeStack.end()) return;
                activeStack.push_back(cIdx);

                for (const AmfInstance& inst : constellations[cIdx].instances) {
                    AmfMat4 curXf = AmfMat4::Mul(parentXf, inst.localXf);
                    auto itObj = objectById.find(inst.targetId);
                    if (itObj != objectById.end()) {
                        emitObject(objects[itObj->second], curXf);
                    } else {
                        auto itCon = constelById.find(inst.targetId);
                        if (itCon != constelById.end()) {
                            expandConstellation(itCon->second, curXf, depth + 1);
                        }
                    }
                }
                activeStack.pop_back();
            };

        bool anyRoot = false;
        for (size_t i = 0; i < constellations.size(); ++i) {
            if (constellations[i].id.empty() || childConstelIds.find(constellations[i].id) == childConstelIds.end()) {
                expandConstellation(i, AmfMat4::Identity(), 0);
                anyRoot = true;
            }
        }
        if (!anyRoot) {
            expandConstellation(0, AmfMat4::Identity(), 0);
        }
    }

    // Fallback: if no <constellation> instances produced geometry, instantiate all <object>s directly
    if (mesh.tris.empty()) {
        mesh.vertices.clear();
        for (const AmfObject& obj : objects) {
            emitObject(obj, AmfMat4::Identity());
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) {
        mesh.vertices.clear();
        mesh.tris.clear();
        return false;
    }
    return true;
}

} // namespace Formats
