#pragma once
// ClipStudioClip.h — Clip Studio Paint CSFCHUNK Structural SQLite3 & Tile Decoder (.clip)

namespace Formats {

namespace DetailClipStudio {

inline uint16_t Rd16BE(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t Rd32BE(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}
inline uint64_t Rd64BE(const uint8_t* p) noexcept {
    return (static_cast<uint64_t>(Rd32BE(p)) << 32) | Rd32BE(p + 4);
}
inline uint32_t Rd32LE(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline std::string ToLowerAscii(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

struct SqliteValue {
    enum class Kind : uint8_t { Null, Int, Float, Text, Blob };
    Kind kind = Kind::Null;
    int64_t i64 = 0;
    double f64 = 0.0;
    std::string text;
    std::vector<uint8_t> blob;
};

class SqliteBTreeReader {
public:
    const uint8_t* db = nullptr;
    size_t dbLen = 0;
    uint32_t pageSize = 0;
    uint32_t usableSize = 0;
    uint32_t totalPages = 0;

    bool Init(const uint8_t* data, size_t size) {
        if (!data || size < 100 || std::memcmp(data, "SQLite format 3\0", 16) != 0) return false;
        uint32_t ps = Rd16BE(data + 16);
        if (ps == 1u) ps = 65536u;
        if (ps < 512u || ps > 65536u || (ps & (ps - 1u)) != 0u) return false;
        uint8_t reserved = data[20];
        if (ps <= reserved + 35u || ps - reserved < 480u) return false;
        db = data;
        dbLen = size;
        pageSize = ps;
        usableSize = ps - reserved;
        totalPages = static_cast<uint32_t>(size / ps);
        return totalPages >= 1u;
    }

    static bool ReadVarint(const uint8_t* p, const uint8_t* end, size_t& adv, uint64_t& outVal) {
        if (p >= end) return false;
        uint64_t v = 0;
        const size_t lim = static_cast<size_t>(std::min<ptrdiff_t>(9, end - p));
        for (size_t i = 0; i < lim; ++i) {
            uint8_t b = p[i];
            if (i == 8) {
                v = (v << 8) | b;
                adv = 9;
                outVal = v;
                return true;
            }
            v = (v << 7) | (b & 0x7Fu);
            if ((b & 0x80u) == 0u) {
                adv = i + 1;
                outVal = v;
                return true;
            }
        }
        return false;
    }

    bool ReadTableLeafCellPayload(const uint8_t* cellPtr, const uint8_t* pageEnd, int64_t& outRowId, std::vector<uint8_t>& outPayload) const {
        size_t a1 = 0, a2 = 0;
        uint64_t paySize64 = 0, rowId64 = 0;
        if (!ReadVarint(cellPtr, pageEnd, a1, paySize64)) return false;
        if (!ReadVarint(cellPtr + a1, pageEnd, a2, rowId64)) return false;
        if (paySize64 > 64ull * 1024ull * 1024ull) return false;
        outRowId = static_cast<int64_t>(rowId64);
        const size_t payloadSize = static_cast<size_t>(paySize64);
        const uint8_t* body = cellPtr + a1 + a2;
        if (body > pageEnd) return false;
        const size_t availInPage = static_cast<size_t>(pageEnd - body);

        const uint32_t maxLocal = usableSize - 35u;
        const uint32_t minLocal = ((usableSize - 12u) * 32u / 255u) - 23u;
        size_t localBytes = payloadSize;
        if (payloadSize > maxLocal) {
            const size_t mod = minLocal + (payloadSize - minLocal) % (usableSize - 4u);
            localBytes = (mod <= maxLocal) ? mod : minLocal;
        }

        outPayload.clear();
        if (payloadSize == localBytes) {
            if (localBytes > availInPage) return false;
            outPayload.assign(body, body + localBytes);
            return true;
        }

        if (localBytes + 4u > availInPage) return false;
        outPayload.reserve(payloadSize);
        outPayload.insert(outPayload.end(), body, body + localBytes);

        uint32_t nextOvPage = Rd32BE(body + localBytes);
        uint32_t hops = 0;
        while (outPayload.size() < payloadSize && nextOvPage >= 1u && nextOvPage <= totalPages) {
            if (++hops > totalPages + 1u) return false;
            const size_t ovOff = static_cast<size_t>(nextOvPage - 1u) * pageSize;
            if (ovOff + usableSize > dbLen) return false;
            nextOvPage = Rd32BE(db + ovOff);
            const size_t chunkCap = usableSize - 4u;
            const size_t take = std::min<size_t>(chunkCap, payloadSize - outPayload.size());
            outPayload.insert(outPayload.end(), db + ovOff + 4u, db + ovOff + 4u + take);
        }
        return outPayload.size() == payloadSize;
    }

    static bool ParseRecord(const std::vector<uint8_t>& rec, std::vector<SqliteValue>& outCols) {
        outCols.clear();
        if (rec.empty()) return false;
        const uint8_t* p = rec.data();
        const uint8_t* end = p + rec.size();
        size_t hdrAdv = 0;
        uint64_t hdrSize = 0;
        if (!ReadVarint(p, end, hdrAdv, hdrSize) || hdrSize < hdrAdv || hdrSize > rec.size()) return false;

        std::vector<uint64_t> serialTypes;
        serialTypes.reserve(16);
        size_t hp = hdrAdv;
        while (hp < hdrSize) {
            size_t adv = 0;
            uint64_t st = 0;
            if (!ReadVarint(p + hp, p + hdrSize, adv, st)) return false;
            hp += adv;
            serialTypes.push_back(st);
            if (serialTypes.size() > 512) return false;
        }

        size_t dp = static_cast<size_t>(hdrSize);
        outCols.resize(serialTypes.size());
        for (size_t i = 0; i < serialTypes.size(); ++i) {
            const uint64_t st = serialTypes[i];
            SqliteValue& v = outCols[i];
            if (st == 0u) {
                v.kind = SqliteValue::Kind::Null;
            } else if (st >= 1u && st <= 6u) {
                static constexpr uint8_t kIntLen[7] = { 0, 1, 2, 3, 4, 6, 8 };
                const size_t n = kIntLen[st];
                if (dp + n > rec.size()) return false;
                uint64_t u = 0;
                for (size_t k = 0; k < n; ++k) u = (u << 8) | rec[dp + k];
                if (n < 8 && (rec[dp] & 0x80u) != 0u) {
                    u |= (~0ull) << (n * 8u);
                }
                v.kind = SqliteValue::Kind::Int;
                v.i64 = static_cast<int64_t>(u);
                dp += n;
            } else if (st == 7u) {
                if (dp + 8u > rec.size()) return false;
                uint64_t u = Rd64BE(rec.data() + dp);
                double d = 0.0;
                std::memcpy(&d, &u, 8);
                v.kind = SqliteValue::Kind::Float;
                v.f64 = d;
                dp += 8u;
            } else if (st == 8u) {
                v.kind = SqliteValue::Kind::Int;
                v.i64 = 0;
            } else if (st == 9u) {
                v.kind = SqliteValue::Kind::Int;
                v.i64 = 1;
            } else if (st >= 12u) {
                const size_t len = static_cast<size_t>((st - 12u) / 2u);
                if (dp + len > rec.size()) return false;
                if ((st & 1u) == 0u) {
                    v.kind = SqliteValue::Kind::Blob;
                    v.blob.assign(rec.begin() + static_cast<ptrdiff_t>(dp), rec.begin() + static_cast<ptrdiff_t>(dp + len));
                } else {
                    v.kind = SqliteValue::Kind::Text;
                    v.text.assign(reinterpret_cast<const char*>(rec.data() + dp), len);
                }
                dp += len;
            } else {
                return false;
            }
        }
        return true;
    }

    template <typename RowFn>
    void WalkTableBTree(uint32_t pageNum1Based, RowFn&& onRow, int depth = 0) const {
        if (depth > 16 || pageNum1Based < 1u || pageNum1Based > totalPages) return;
        const size_t pageOff = static_cast<size_t>(pageNum1Based - 1u) * pageSize;
        if (pageOff + usableSize > dbLen) return;
        const uint8_t* page = db + pageOff;
        const uint8_t* pageEnd = page + usableSize;
        const size_t hdrOff = (pageNum1Based == 1u) ? 100u : 0u;
        if (hdrOff + 8u > usableSize) return;

        const uint8_t pageType = page[hdrOff];
        const uint16_t cellCount = Rd16BE(page + hdrOff + 3u);
        if (pageType == 0x05u) { // Interior table B-tree page
            if (hdrOff + 12u + static_cast<size_t>(cellCount) * 2u > usableSize) return;
            const uint32_t rightChild = Rd32BE(page + hdrOff + 8u);
            for (uint16_t i = 0; i < cellCount; ++i) {
                const uint16_t cellOff = Rd16BE(page + hdrOff + 12u + static_cast<size_t>(i) * 2u);
                if (cellOff < hdrOff + 12u || static_cast<size_t>(cellOff) + 4u > usableSize) continue;
                const uint32_t leftChild = Rd32BE(page + cellOff);
                WalkTableBTree(leftChild, onRow, depth + 1);
            }
            WalkTableBTree(rightChild, onRow, depth + 1);
        } else if (pageType == 0x0Du) { // Leaf table B-tree page
            if (hdrOff + 8u + static_cast<size_t>(cellCount) * 2u > usableSize) return;
            std::vector<uint8_t> payload;
            std::vector<SqliteValue> cols;
            for (uint16_t i = 0; i < cellCount; ++i) {
                const uint16_t cellOff = Rd16BE(page + hdrOff + 8u + static_cast<size_t>(i) * 2u);
                if (cellOff < hdrOff + 8u || cellOff >= usableSize) continue;
                int64_t rowId = 0;
                if (ReadTableLeafCellPayload(page + cellOff, pageEnd, rowId, payload) && ParseRecord(payload, cols)) {
                    if (!onRow(rowId, cols)) return;
                }
            }
        }
    }
};

inline std::vector<std::string> ParseCreateTableColumns(std::string_view sql) {
    std::vector<std::string> cols;
    size_t openP = sql.find('(');
    if (openP == std::string_view::npos) return cols;
    int depth = 0;
    size_t segStart = openP + 1;
    for (size_t i = openP; i < sql.size(); ++i) {
        char c = sql[i];
        if (c == '(') {
            if (depth == 0) segStart = i + 1;
            ++depth;
        } else if (c == ')' || (c == ',' && depth == 1)) {
            if (c == ')') --depth;
            if ((c == ',' && depth == 1) || (c == ')' && depth == 0)) {
                std::string_view seg = sql.substr(segStart, i - segStart);
                size_t p = 0;
                while (p < seg.size() && static_cast<unsigned char>(seg[p]) <= 0x20u) ++p;
                if (p < seg.size()) {
                    char quote = 0;
                    if (seg[p] == '"' || seg[p] == '`' || seg[p] == '[') {
                        quote = (seg[p] == '[') ? ']' : seg[p];
                        ++p;
                        size_t e = p;
                        while (e < seg.size() && seg[e] != quote) ++e;
                        std::string name = ToLowerAscii(seg.substr(p, e - p));
                        if (!name.empty()) cols.push_back(std::move(name));
                    } else {
                        size_t e = p;
                        while (e < seg.size() && static_cast<unsigned char>(seg[e]) > 0x20u && seg[e] != '(' && seg[e] != ',') ++e;
                        std::string kw = ToLowerAscii(seg.substr(p, e - p));
                        if (!kw.empty() && kw != "primary" && kw != "unique" && kw != "constraint" && kw != "foreign" && kw != "check") {
                            cols.push_back(std::move(kw));
                        }
                    }
                }
                segStart = i + 1;
                if (c == ')' && depth == 0) break;
            }
        }
    }
    return cols;
}

inline int FindColIndex(const std::vector<std::string>& cols, std::string_view colLower) {
    for (size_t i = 0; i < cols.size(); ++i) {
        if (cols[i] == colLower) return static_cast<int>(i);
    }
    return -1;
}

inline int64_t GetIntCol(const std::vector<SqliteValue>& row, int idx, int64_t rowIdFallback = 0, bool isPk = false) {
    if (idx < 0 || static_cast<size_t>(idx) >= row.size()) return isPk ? rowIdFallback : 0;
    if (row[idx].kind == SqliteValue::Kind::Int) return row[idx].i64;
    if (isPk && row[idx].kind == SqliteValue::Kind::Null) return rowIdFallback;
    return 0;
}

inline std::string GetTextCol(const std::vector<SqliteValue>& row, int idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= row.size()) return {};
    if (row[idx].kind == SqliteValue::Kind::Text) return row[idx].text;
    if (row[idx].kind == SqliteValue::Kind::Blob) {
        return std::string(reinterpret_cast<const char*>(row[idx].blob.data()), row[idx].blob.size());
    }
    return {};
}

// Match UTF-16BE ASCII tag inside Clip Studio Offscreen BlockData stream
inline bool MatchUtf16BeAscii(const uint8_t* p, size_t avail, std::string_view ascii) {
    if (avail < ascii.size() * 2u) return false;
    for (size_t i = 0; i < ascii.size(); ++i) {
        if (p[i * 2] != 0u || p[i * 2 + 1] != static_cast<uint8_t>(ascii[i])) return false;
    }
    return true;
}

// Decode Clip Studio Paint 256x256 tiled Offscreen BlockDataBeginChunk payload onto layer canvas
inline bool CompositeCspOffscreenBlocks(
    const uint8_t* blk,
    size_t blkLen,
    uint32_t canvasW,
    uint32_t canvasH,
    float layerOpacity,
    std::vector<uint8_t>& canvasBgra)
{
    if (!blk || blkLen < 32 || canvasW == 0 || canvasH == 0) return false;
    constexpr uint32_t kTile = 256u;
    const uint32_t tilesX = (canvasW + kTile - 1u) / kTile;
    const uint32_t tilesY = (canvasH + kTile - 1u) / kTile;
    if (tilesX == 0 || tilesY == 0 || tilesX > 256u || tilesY > 256u) return false;

    static constexpr std::string_view kBeginTag = "BlockDataBeginChunk";
    static constexpr std::string_view kEndTag   = "BlockDataEndChunk";

    uint32_t tileOrd = 0;
    size_t decodedTiles = 0;
    size_t pos = 0;
    while (pos + kBeginTag.size() * 2u <= blkLen && tileOrd < tilesX * tilesY) {
        if (!MatchUtf16BeAscii(blk + pos, blkLen - pos, kBeginTag)) {
            ++pos;
            continue;
        }
        // Real CSP BlockDataBeginChunk header: [4-8B len prefix before tag, UTF-16BE tag, 16B tile header: idx, ver, notEmpty, subSize]
        size_t bodyStart = pos + kBeginTag.size() * 2u;
        size_t bodyEnd = blkLen;
        for (size_t s = bodyStart; s + kEndTag.size() * 2u <= blkLen; ++s) {
            if (MatchUtf16BeAscii(blk + s, blkLen - s, kEndTag)) {
                bodyEnd = s;
                break;
            }
        }

        uint32_t curTileIdx = tileOrd++;
        if (bodyStart + 16u <= bodyEnd) {
            uint32_t hdrIdx = Rd32BE(blk + bodyStart);
            if (hdrIdx < tilesX * tilesY) curTileIdx = hdrIdx;
        }

        // Locate zlib stream(s) inside [bodyStart .. bodyEnd]:
        // CSP stores either a combined 256*256*5 (256*256*1 alpha + 256*256*4 BGRX) or 256*256*4 BGRA zlib stream,
        // or two consecutive sub-streams (alpha 65536B + color 262144B).
        std::vector<std::vector<uint8_t>> streams;
        for (size_t z = bodyStart; z + 6u < bodyEnd && streams.size() < 2u; ++z) {
            const uint8_t cmf = blk[z];
            const uint8_t flg = blk[z + 1];
            if ((cmf & 0x0Fu) == 8u && (cmf >> 4) <= 7u && ((static_cast<uint32_t>(cmf) << 8) | flg) % 31u == 0u) {
                // Check if preceded by a big-endian or little-endian compressed byte length
                size_t zCap = bodyEnd - z;
                if (z >= bodyStart + 4u) {
                    uint32_t beLen = Rd32BE(blk + z - 4u);
                    uint32_t leLen = Rd32LE(blk + z - 4u);
                    if (beLen >= 6u && beLen <= bodyEnd - z) zCap = beLen;
                    else if (leLen >= 6u && leLen <= bodyEnd - z) zCap = leLen;
                }
                std::vector<uint8_t> dec;
                if (ZipDeflate::InflateZlib(blk + z, zCap, dec, kTile * kTile * 5u + 256u) &&
                    (dec.size() == kTile * kTile || dec.size() == kTile * kTile * 4u || dec.size() == kTile * kTile * 5u))
                {
                    streams.push_back(std::move(dec));
                    z += (zCap < bodyEnd - z) ? zCap - 1u : 16u;
                }
            }
        }

        const uint8_t* alphaPlane = nullptr;
        const uint8_t* colorPlane = nullptr;
        if (streams.size() == 1u) {
            if (streams[0].size() == kTile * kTile * 5u) {
                alphaPlane = streams[0].data();
                colorPlane = streams[0].data() + kTile * kTile;
            } else if (streams[0].size() == kTile * kTile * 4u) {
                colorPlane = streams[0].data();
            }
        } else if (streams.size() >= 2u) {
            if (streams[0].size() == kTile * kTile && streams[1].size() == kTile * kTile * 4u) {
                alphaPlane = streams[0].data();
                colorPlane = streams[1].data();
            } else if (streams[1].size() == kTile * kTile && streams[0].size() == kTile * kTile * 4u) {
                colorPlane = streams[0].data();
                alphaPlane = streams[1].data();
            }
        }

        if (colorPlane) {
            const uint32_t tx = curTileIdx % tilesX;
            const uint32_t ty = curTileIdx / tilesX;
            const uint32_t baseX = tx * kTile;
            const uint32_t baseY = ty * kTile;
            for (uint32_t py = 0; py < kTile && baseY + py < canvasH; ++py) {
                for (uint32_t px = 0; px < kTile && baseX + px < canvasW; ++px) {
                    const size_t ti = static_cast<size_t>(py) * kTile + px;
                    const uint8_t* cp = colorPlane + ti * 4u;
                    const uint8_t a = alphaPlane ? alphaPlane[ti] : cp[3];
                    if (a == 0u) continue;
                    uint8_t pm[4];
                    WritePremulBGRA(pm, cp[2], cp[1], cp[0], a);
                    DetailLayeredAndHdrImg::BlendPremulBgraPixel(
                        &canvasBgra[(static_cast<size_t>(baseY + py) * canvasW + (baseX + px)) * 4u],
                        pm[0], pm[1], pm[2], pm[3], layerOpacity, DetailLayeredAndHdrImg::LayerBlendOp::SrcOver);
                }
            }
            ++decodedTiles;
        }
        pos = bodyEnd + kEndTag.size() * 2u;
    }
    return decodedTiles > 0u;
}

} // namespace DetailClipStudio

inline bool DecodeClipStudio(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    using namespace DetailClipStudio;
    if (!data || size < 40 || std::memcmp(data, "CSFCHUNK", 8) != 0) return false;

    uint64_t firstOffset = Rd64BE(data + 16);
    size_t pos = (firstOffset >= 24u && firstOffset + 16u <= size && std::memcmp(data + firstOffset, "CHNK", 4) == 0)
                     ? static_cast<size_t>(firstOffset)
                     : 24u;

    struct ExtaBlob {
        const uint8_t* ptr = nullptr;
        size_t len = 0;
    };
    std::unordered_map<std::string, ExtaBlob> extaById;
    const uint8_t* sqlitePtr = nullptr;
    size_t sqliteLen = 0;
    uint32_t headCanvasW = 0, headCanvasH = 0;

    while (pos + 16u <= size) {
        if (std::memcmp(data + pos, "CHNK", 4) != 0) break;
        const char subType[5] = {
            static_cast<char>(data[pos + 4]), static_cast<char>(data[pos + 5]),
            static_cast<char>(data[pos + 6]), static_cast<char>(data[pos + 7]), 0
        };
        const uint64_t ckLen64 = Rd64BE(data + pos + 8);
        pos += 16u;
        if (ckLen64 > size - pos) break;
        const uint8_t* ckData = data + pos;
        const size_t ckLen = static_cast<size_t>(ckLen64);

        if (std::strcmp(subType, "Head") == 0 && ckLen >= 16u) {
            const uint32_t cw = Rd32BE(ckData + 4);
            const uint32_t ch = Rd32BE(ckData + 8);
            if (cw > 0 && cw <= 16384u && ch > 0 && ch <= 16384u) {
                headCanvasW = cw;
                headCanvasH = ch;
            }
        } else if (std::strcmp(subType, "SQLi") == 0 && ckLen >= 100u) {
            sqlitePtr = ckData;
            sqliteLen = ckLen;
        } else if (std::strcmp(subType, "Exta") == 0 && ckLen >= 16u) {
            // Standard CSP CHNKExta: [u64BE externalIdLen][ASCII externalId][u64BE payloadLen][payload]
            uint64_t idLen = Rd64BE(ckData);
            size_t hdrAdv = 8u;
            if ((idLen == 0 || idLen > 256u) && ckLen >= 12u) {
                uint32_t idLen32 = Rd32BE(ckData);
                if (idLen32 > 0 && idLen32 <= 256u && 8u + idLen32 <= ckLen) {
                    idLen = idLen32;
                    hdrAdv = 4u;
                }
            }
            if (idLen > 0 && idLen <= 256u && hdrAdv + idLen <= ckLen) {
                std::string extId(reinterpret_cast<const char*>(ckData + hdrAdv), static_cast<size_t>(idLen));
                while (!extId.empty() && extId.back() == '\0') extId.pop_back();
                size_t payOff = hdrAdv + static_cast<size_t>(idLen);
                size_t payLen = ckLen - payOff;
                if (payLen >= 8u) {
                    uint64_t declSz = Rd64BE(ckData + payOff);
                    if (declSz > 0 && declSz <= payLen - 8u) {
                        payOff += 8u;
                        payLen = static_cast<size_t>(declSz);
                    }
                }
                extaById[extId] = { ckData + payOff, payLen };
            }
        } else if (std::strcmp(subType, "Foot") == 0) {
            break;
        }
        pos += ckLen;
    }

    if (!sqlitePtr || sqliteLen < 100u) return false;
    SqliteBTreeReader sq;
    if (!sq.Init(sqlitePtr, sqliteLen)) return false;

    struct TableSchema {
        uint32_t rootPage = 0;
        std::vector<std::string> cols;
    };
    std::unordered_map<std::string, TableSchema> tables;
    sq.WalkTableBTree(1u, [&](int64_t, const std::vector<SqliteValue>& cols) -> bool {
        if (cols.size() >= 5 && cols[0].kind == SqliteValue::Kind::Text && ToLowerAscii(cols[0].text) == "table") {
            std::string tblName = ToLowerAscii(cols[1].text);
            uint32_t rootPg = (cols[3].kind == SqliteValue::Kind::Int && cols[3].i64 > 0)
                                  ? static_cast<uint32_t>(cols[3].i64) : 0u;
            if (rootPg > 0 && !tblName.empty()) {
                tables[tblName] = { rootPg, ParseCreateTableColumns(cols[4].text) };
            }
        }
        return true;
    });

    uint32_t canvasW = headCanvasW, canvasH = headCanvasH;
    if (auto it = tables.find("canvas"); it != tables.end()) {
        const int cW = FindColIndex(it->second.cols, "canvaswidth");
        const int cH = FindColIndex(it->second.cols, "canvasheight");
        sq.WalkTableBTree(it->second.rootPage, [&](int64_t, const std::vector<SqliteValue>& row) -> bool {
            int64_t wv = GetIntCol(row, cW), hv = GetIntCol(row, cH);
            if (wv == 0 && cW >= 0 && static_cast<size_t>(cW) < row.size() && row[cW].kind == SqliteValue::Kind::Float) {
                wv = static_cast<int64_t>(std::llround(row[cW].f64));
            }
            if (hv == 0 && cH >= 0 && static_cast<size_t>(cH) < row.size() && row[cH].kind == SqliteValue::Kind::Float) {
                hv = static_cast<int64_t>(std::llround(row[cH].f64));
            }
            if (wv > 0 && wv <= 16384 && hv > 0 && hv <= 16384) {
                canvasW = static_cast<uint32_t>(wv);
                canvasH = static_cast<uint32_t>(hv);
                return false;
            }
            return true;
        });
    }

    struct CspLayerRow {
        int64_t id = 0;
        std::string name;
        int64_t visibility = 1;
        int64_t opacity = 256;
        int64_t renderMipmap = 0;
    };
    std::vector<CspLayerRow> layers;
    if (auto it = tables.find("layer"); it != tables.end()) {
        const int cId   = FindColIndex(it->second.cols, "_pw_id");
        const int cName = FindColIndex(it->second.cols, "layername");
        const int cVis  = FindColIndex(it->second.cols, "layervisibility");
        const int cOp   = FindColIndex(it->second.cols, "layeropacity");
        const int cMip  = FindColIndex(it->second.cols, "layerrendermipmap");
        sq.WalkTableBTree(it->second.rootPage, [&](int64_t rowId, const std::vector<SqliteValue>& row) -> bool {
            CspLayerRow lr;
            lr.id = GetIntCol(row, cId, rowId, true);
            lr.name = GetTextCol(row, cName);
            lr.visibility = (cVis >= 0) ? GetIntCol(row, cVis, 1) : 1;
            lr.opacity = (cOp >= 0) ? GetIntCol(row, cOp, 256) : 256;
            lr.renderMipmap = GetIntCol(row, cMip, 0);
            if (!lr.name.empty() && outInfo.layerNames.size() < 64u) {
                outInfo.layerNames.push_back(PathUtils::Utf8ToWide(lr.name));
            }
            layers.push_back(std::move(lr));
            return layers.size() < 512u;
        });
    }

    // 1. Primary path: extract CanvasPreview.ImageData PNG BLOB from SQLite B-tree (with overflow reassembly)
    if (auto it = tables.find("canvaspreview"); it != tables.end()) {
        const int cImg = FindColIndex(it->second.cols, "imagedata");
        std::vector<uint8_t> previewBlob;
        sq.WalkTableBTree(it->second.rootPage, [&](int64_t, const std::vector<SqliteValue>& row) -> bool {
            if (cImg >= 0 && static_cast<size_t>(cImg) < row.size() &&
                row[cImg].kind == SqliteValue::Kind::Blob && row[cImg].blob.size() >= 24u)
            {
                previewBlob = row[cImg].blob;
                return false;
            }
            for (const auto& col : row) {
                if (col.kind == SqliteValue::Kind::Blob && col.blob.size() >= 24u &&
                    std::memcmp(col.blob.data(), "\x89PNG\r\n\x1A\n", 8) == 0)
                {
                    previewBlob = col.blob;
                    return false;
                }
            }
            return true;
        });

        if (!previewBlob.empty()) {
            std::vector<std::wstring> savedLayers = outInfo.layerNames;
            if (DetailPngPure::DecodePngToImageInfo(previewBlob.data(), previewBlob.size(), L"Clip Studio Paint Canvas (.clip)", outInfo) ||
                DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(previewBlob.data(), previewBlob.size(), L"Clip Studio Paint Canvas (.clip)", outInfo))
            {
                outInfo.layerNames = std::move(savedLayers);
                if (canvasW > 0 && canvasH > 0) {
                    outInfo.origWidth = canvasW;
                    outInfo.origHeight = canvasH;
                }
                return true;
            }
            outInfo.layerNames = std::move(savedLayers);
        }
    }

    // 2. Fallback path: assemble tiles from Layer -> Mipmap -> MipmapInfo -> Offscreen -> CHNKExta
    if (!extaById.empty() && canvasW > 0 && canvasH > 0 && canvasW <= 8192u && canvasH <= 8192u) {
        std::unordered_map<int64_t, int64_t> mipToBaseInfo;
        if (auto it = tables.find("mipmap"); it != tables.end()) {
            const int cId = FindColIndex(it->second.cols, "_pw_id");
            const int cBase = FindColIndex(it->second.cols, "basemipmapinfo");
            sq.WalkTableBTree(it->second.rootPage, [&](int64_t rowId, const std::vector<SqliteValue>& row) -> bool {
                mipToBaseInfo[GetIntCol(row, cId, rowId, true)] = GetIntCol(row, cBase);
                return true;
            });
        }

        std::unordered_map<int64_t, int64_t> infoToOffscreen;
        if (auto it = tables.find("mipmapinfo"); it != tables.end()) {
            const int cId = FindColIndex(it->second.cols, "_pw_id");
            const int cOff = FindColIndex(it->second.cols, "offscreen");
            sq.WalkTableBTree(it->second.rootPage, [&](int64_t rowId, const std::vector<SqliteValue>& row) -> bool {
                infoToOffscreen[GetIntCol(row, cId, rowId, true)] = GetIntCol(row, cOff);
                return true;
            });
        }

        std::unordered_map<int64_t, std::string> offscreenToBlockId;
        if (auto it = tables.find("offscreen"); it != tables.end()) {
            const int cId = FindColIndex(it->second.cols, "_pw_id");
            const int cBlk = FindColIndex(it->second.cols, "blockdata");
            sq.WalkTableBTree(it->second.rootPage, [&](int64_t rowId, const std::vector<SqliteValue>& row) -> bool {
                std::string bid = GetTextCol(row, cBlk);
                while (!bid.empty() && bid.back() == '\0') bid.pop_back();
                if (!bid.empty()) offscreenToBlockId[GetIntCol(row, cId, rowId, true)] = std::move(bid);
                return true;
            });
        }

        std::vector<uint8_t> canvas(static_cast<size_t>(canvasW) * canvasH * 4u, 0u);
        size_t composited = 0;

        for (const auto& lr : layers) {
            if ((lr.visibility & 1) == 0 || lr.opacity <= 0 || lr.renderMipmap <= 0) continue;
            auto itM = mipToBaseInfo.find(lr.renderMipmap);
            if (itM == mipToBaseInfo.end()) continue;
            auto itI = infoToOffscreen.find(itM->second);
            if (itI == infoToOffscreen.end()) continue;
            auto itO = offscreenToBlockId.find(itI->second);
            if (itO == offscreenToBlockId.end()) continue;
            auto itE = extaById.find(itO->second);
            if (itE == extaById.end()) continue;

            float opac = std::clamp(static_cast<float>(lr.opacity) / 256.0f, 0.0f, 1.0f);
            if (CompositeCspOffscreenBlocks(itE->second.ptr, itE->second.len, canvasW, canvasH, opac, canvas)) {
                ++composited;
            }
        }

        // If Layer->Mipmap links were absent, walk Offscreen rows directly
        if (composited == 0 && !offscreenToBlockId.empty()) {
            for (const auto& kv : offscreenToBlockId) {
                auto itE = extaById.find(kv.second);
                if (itE != extaById.end() &&
                    CompositeCspOffscreenBlocks(itE->second.ptr, itE->second.len, canvasW, canvasH, 1.0f, canvas))
                {
                    ++composited;
                }
            }
        }

        if (composited > 0u) {
            outInfo.width = canvasW;
            outInfo.height = canvasH;
            outInfo.origWidth = canvasW;
            outInfo.origHeight = canvasH;
            outInfo.channels = 4u;
            outInfo.bitsPerChannel = 8u;
            outInfo.formatName = L"Clip Studio Paint Layered Canvas (.clip)";
            outInfo.bgraPixels = std::move(canvas);
            outInfo.valid = true;
            return true;
        }
    }

    return false;
}

} // namespace Formats
