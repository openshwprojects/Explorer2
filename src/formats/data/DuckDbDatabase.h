#pragma once
// DuckDbDatabase.h — DuckDB Analytical Database (.duckdb) Reader

namespace Formats {
namespace AiDataDetail {

// ============================================================================
// 15. DuckDB Analytical Database (.duckdb — Linked MetaBlocks + BinaryMetadataDeserializer)
// ============================================================================
struct DuckMetaPtr {
    uint64_t blockPtr = UINT64_MAX;
    uint32_t offset = 0;
};

class DuckMetaStream {
public:
    DuckMetaStream(const std::vector<uint8_t>& raw, uint64_t blockAlloc, uint64_t startMetaPtr, uint32_t startOff = 0)
        : m_raw(raw), m_blockAlloc(blockAlloc)
    {
        m_valid = OpenChunk(startMetaPtr, startOff);
    }

    bool Valid() const { return m_valid; }

    bool ReadByte(uint8_t& out) {
        if (!m_valid) return false;
        while (m_pos >= m_end) {
            if (m_nextPtr == UINT64_MAX) {
                m_valid = false;
                return false;
            }
            if (!OpenChunk(m_nextPtr, 0)) {
                m_valid = false;
                return false;
            }
        }
        out = m_raw[m_pos++];
        return true;
    }

    bool ReadBytes(void* dst, size_t n) {
        uint8_t* p = static_cast<uint8_t*>(dst);
        for (size_t i = 0; i < n; ++i) {
            if (!ReadByte(p[i])) return false;
        }
        return true;
    }

    bool ReadU16(uint16_t& out) {
        uint8_t b[2];
        if (!ReadBytes(b, 2)) return false;
        out = RdU16LE(b);
        return true;
    }

    bool ReadU32(uint32_t& out) {
        uint8_t b[4];
        if (!ReadBytes(b, 4)) return false;
        out = RdU32LE(b);
        return true;
    }

    bool ReadU64(uint64_t& out) {
        uint8_t b[8];
        if (!ReadBytes(b, 8)) return false;
        out = RdU64LE(b);
        return true;
    }

    bool ReadBool(bool& out) {
        uint8_t b = 0;
        if (!ReadByte(b) || b > 1u) return false;
        out = (b != 0);
        return true;
    }

    bool ReadVarU64(uint64_t& out) {
        out = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            uint8_t b = 0;
            if (!ReadByte(b)) return false;
            if (shift == 63 && b > 1u) return false;
            out |= static_cast<uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0) return true;
        }
        return false;
    }

    bool ReadVarI64(int64_t& out) {
        uint64_t u = 0;
        if (!ReadVarU64(u)) return false;
        out = static_cast<int64_t>((u >> 1) ^ (~(u & 1ULL) + 1ULL));
        return true;
    }

    bool ReadStr(std::string& out, size_t maxLen = 65536) {
        uint64_t len = 0;
        if (!ReadVarU64(len) || len > maxLen) return false;
        out.resize(static_cast<size_t>(len));
        if (len > 0 && !ReadBytes(out.data(), static_cast<size_t>(len))) return false;
        return true;
    }

private:
    bool OpenChunk(uint64_t metaPtr, uint32_t subOff) {
        if (metaPtr == UINT64_MAX || m_hops >= 4096) return false;
        if (subOff == 0 && !m_visited.insert(metaPtr).second) return false; // cycle detected
        ++m_hops;

        const uint64_t blockId = metaPtr & 0x00FFFFFFFFFFFFFFULL;
        const uint8_t subIdx = static_cast<uint8_t>(metaPtr >> 56);
        if (subIdx >= 64) return false;

        if (m_blockAlloc < 4096 || blockId > (m_raw.size() - 12288ULL) / m_blockAlloc) return false;
        const uint64_t physStart = 12288ULL + blockId * m_blockAlloc;
        if (physStart + 16ULL > m_raw.size()) return false;

        const uint64_t usableStart = physStart + 8ULL; // skip 8B physical block checksum
        const uint64_t usableCap = std::min<uint64_t>(m_blockAlloc - 8ULL, m_raw.size() - usableStart);

        uint64_t subSize = usableCap;
        if (m_blockAlloc >= 65536ULL || subIdx > 0) {
            subSize = ((m_blockAlloc - 8ULL) / 64ULL) & ~7ULL;
            if (subSize < 64ULL) return false;
        }
        const uint64_t chunkStart = usableStart + static_cast<uint64_t>(subIdx) * subSize;
        if (chunkStart + 8ULL > m_raw.size()) return false;
        const uint64_t chunkEnd = std::min<uint64_t>(chunkStart + subSize, m_raw.size());
        if (chunkEnd <= chunkStart + 8ULL) return false;

        m_nextPtr = RdU64LE(m_raw.data() + static_cast<size_t>(chunkStart));
        const uint64_t effOff = (subOff < 8u) ? 8ULL : static_cast<uint64_t>(subOff);
        if (chunkStart + effOff > chunkEnd) return false;

        m_pos = static_cast<size_t>(chunkStart + effOff);
        m_end = static_cast<size_t>(chunkEnd);
        return true;
    }

    const std::vector<uint8_t>& m_raw;
    uint64_t m_blockAlloc = 262144;
    size_t m_pos = 0;
    size_t m_end = 0;
    uint64_t m_nextPtr = UINT64_MAX;
    uint32_t m_hops = 0;
    bool m_valid = false;
    std::set<uint64_t> m_visited;
};

struct DuckColDef {
    std::string name;
    uint8_t typeId = 0;
    uint8_t decWidth = 0;
    uint8_t decScale = 0;
};

struct DuckSegPtr {
    uint64_t rowStart = 0;
    uint64_t tupleCount = 0;
    int64_t blockId = -1;
    uint32_t offset = 0;
    uint8_t compression = 1; // 1=UNCOMPRESSED, 2=CONSTANT, 3=RLE, 6=DICTIONARY
    bool hasNull = false;
    bool hasNoNull = true;
    bool hasConstVal = false;
    std::wstring constVal;
};

struct DuckTableEntry {
    uint8_t catType = 0; // 1=TABLE, 2=SCHEMA, 3=VIEW
    std::string schema;
    std::string name;
    std::string sql;
    bool internal = false;
    std::vector<DuckColDef> cols;
    DuckMetaPtr tablePtr;
    uint64_t totalRows = 0;
    std::vector<std::vector<DuckSegPtr>> colSegments;
};

inline const wchar_t* DuckTypeName(const DuckColDef& c) {
    switch (c.typeId) {
    case 10: return L"BOOLEAN";
    case 11: return L"TINYINT";
    case 12: return L"SMALLINT";
    case 13: return L"INTEGER";
    case 14: return L"BIGINT";
    case 15: return L"DATE";
    case 16: return L"TIME";
    case 17: return L"TIMESTAMP_S";
    case 18: return L"TIMESTAMP_MS";
    case 19: return L"TIMESTAMP";
    case 20: return L"TIMESTAMP_NS";
    case 21: return L"DECIMAL";
    case 22: return L"FLOAT";
    case 23: return L"DOUBLE";
    case 24: return L"CHAR";
    case 25: return L"VARCHAR";
    case 26: return L"BLOB";
    case 27: return L"INTERVAL";
    case 28: return L"UTINYINT";
    case 29: return L"USMALLINT";
    case 30: return L"UINTEGER";
    case 31: return L"UBIGINT";
    case 32: return L"TIMESTAMPTZ";
    case 49: return L"UHUGEINT";
    case 50: return L"HUGEINT";
    case 54: return L"UUID";
    case 100: return L"STRUCT";
    case 101: return L"LIST";
    case 102: return L"MAP";
    default: return L"ANY";
    }
}

inline std::wstring FormatDuckDateDays(int32_t z) {
    // Howard Hinnant civil_from_days (days since 1970-01-01)
    int64_t zz = static_cast<int64_t>(z) + 719468LL;
    const int64_t era = (zz >= 0 ? zz : zz - 146096LL) / 146097LL;
    const uint32_t doe = static_cast<uint32_t>(zz - era * 146097LL);
    const uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int64_t y = static_cast<int64_t>(yoe) + era * 400LL;
    const uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const uint32_t mp = (5u * doy + 2u) / 153u;
    const uint32_t d = doy - (153u * mp + 2u) / 5u + 1u;
    const uint32_t m = mp < 10u ? (mp + 3u) : (mp - 9u);
    y += (m <= 2u) ? 1 : 0;
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%04lld-%02u-%02u", static_cast<long long>(y), m, d);
    return buf;
}

inline std::wstring FormatDuckTimestampUs(int64_t us) {
    int64_t secs = us / 1000000LL;
    int64_t remUs = us % 1000000LL;
    if (remUs < 0) { remUs += 1000000LL; --secs; }
    int64_t days = secs / 86400LL;
    int64_t sod = secs % 86400LL;
    if (sod < 0) { sod += 86400LL; --days; }
    const uint32_t hh = static_cast<uint32_t>(sod / 3600LL);
    const uint32_t mm = static_cast<uint32_t>((sod % 3600LL) / 60LL);
    const uint32_t ss = static_cast<uint32_t>(sod % 60LL);
    wchar_t tbuf[32] = {};
    swprintf_s(tbuf, L" %02u:%02u:%02u", hh, mm, ss);
    return FormatDuckDateDays(static_cast<int32_t>(days)) + tbuf;
}

inline bool ReadDuckMetaPtrObj(DuckMetaStream& ms, DuckMetaPtr& out) {
    out = {};
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return true;
        if (fid == 100) {
            if (!ms.ReadVarU64(out.blockPtr)) return false;
        } else if (fid == 101) {
            uint64_t off = 0;
            if (!ms.ReadVarU64(off)) return false;
            out.offset = static_cast<uint32_t>(off);
        } else {
            return false;
        }
    }
}

inline bool ReadDuckLogicalTypeObj(DuckMetaStream& ms, DuckColDef& col) {
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return true;
        if (fid == 100) {
            uint64_t tid = 0;
            if (!ms.ReadVarU64(tid) || tid > 255) return false;
            col.typeId = static_cast<uint8_t>(tid);
        } else if (fid == 101) {
            // Optional ExtraTypeInfo nullable pointer (1B bool + object)
            bool present = false;
            if (!ms.ReadBool(present)) return false;
            if (present) {
                for (;;) {
                    uint16_t ef = 0;
                    if (!ms.ReadU16(ef)) return false;
                    if (ef == 0xFFFFu) break;
                    if (ef == 100) { uint64_t et = 0; if (!ms.ReadVarU64(et)) return false; }
                    else if (ef == 101) { std::string alias; if (!ms.ReadStr(alias)) return false; }
                    else if (ef == 200) { uint64_t w = 0; if (!ms.ReadVarU64(w)) return false; col.decWidth = static_cast<uint8_t>(w); }
                    else if (ef == 201) { uint64_t sc = 0; if (!ms.ReadVarU64(sc)) return false; col.decScale = static_cast<uint8_t>(sc); }
                    else return false;
                }
            }
        } else {
            return false;
        }
    }
}

inline bool ReadDuckColumnDefObj(DuckMetaStream& ms, DuckColDef& out) {
    out = {};
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return !out.name.empty();
        if (fid == 100) {
            if (!ms.ReadStr(out.name, 4096)) return false;
        } else if (fid == 101) {
            if (!ReadDuckLogicalTypeObj(ms, out)) return false;
        } else if (fid == 102 || fid == 103) {
            uint64_t v = 0;
            if (!ms.ReadVarU64(v)) return false;
        } else if (fid == 104 || fid == 200) {
            bool hasDef = false;
            if (!ms.ReadBool(hasDef) || hasDef) return false;
        } else {
            return false;
        }
    }
}

inline bool ReadDuckCreateInfoObj(DuckMetaStream& ms, DuckTableEntry& ent) {
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return true;
        switch (fid) {
        case 100: {
            uint64_t t = 0;
            if (!ms.ReadVarU64(t) || t > 64) return false;
            ent.catType = static_cast<uint8_t>(t);
            break;
        }
        case 101: {
            std::string cat;
            if (!ms.ReadStr(cat, 4096)) return false;
            break;
        }
        case 102: {
            if (!ms.ReadStr(ent.schema, 4096)) return false;
            break;
        }
        case 103: {
            bool tmp = false;
            if (!ms.ReadBool(tmp)) return false;
            break;
        }
        case 104: {
            if (!ms.ReadBool(ent.internal)) return false;
            break;
        }
        case 105:
        case 108: {
            if (!ms.ReadStr(ent.sql, 65536)) return false;
            break;
        }
        case 106: { // dependencies list (empty or list of {100..0xFFFF})
            uint64_t depCount = 0;
            if (!ms.ReadVarU64(depCount) || depCount > 4096) return false;
            for (uint64_t i = 0; i < depCount; ++i) {
                for (;;) {
                    uint16_t df = 0;
                    if (!ms.ReadU16(df)) return false;
                    if (df == 0xFFFFu) break;
                    if (df == 100) {
                        for (;;) {
                            uint16_t bf = 0;
                            if (!ms.ReadU16(bf)) return false;
                            if (bf == 0xFFFFu) break;
                            if (bf == 100) { uint64_t dt = 0; if (!ms.ReadVarU64(dt)) return false; }
                            else if (bf == 101 || bf == 102) { std::string s; if (!ms.ReadStr(s, 4096)) return false; }
                            else return false;
                        }
                    } else if (df == 101) {
                        std::string cat;
                        if (!ms.ReadStr(cat, 4096)) return false;
                    } else {
                        return false;
                    }
                }
            }
            break;
        }
        case 107: { // optional comment (bool has_val + string)
            bool hasCmt = false;
            if (!ms.ReadBool(hasCmt)) return false;
            if (hasCmt) {
                std::string cmt;
                if (!ms.ReadStr(cmt, 16384)) return false;
            }
            break;
        }
        case 200: { // table / view name
            if (!ms.ReadStr(ent.name, 4096)) return false;
            break;
        }
        case 201: {
            if (ent.catType == 1) {
                // ColumnList object { 100: vector<ColumnDefinition>, 0xFFFF }
                for (;;) {
                    uint16_t cf = 0;
                    if (!ms.ReadU16(cf)) return false;
                    if (cf == 0xFFFFu) break;
                    if (cf == 100) {
                        uint64_t colCount = 0;
                        if (!ms.ReadVarU64(colCount) || colCount > 4096) return false;
                        ent.cols.reserve(static_cast<size_t>(colCount));
                        for (uint64_t c = 0; c < colCount; ++c) {
                            DuckColDef cd;
                            if (!ReadDuckColumnDefObj(ms, cd)) return false;
                            ent.cols.push_back(std::move(cd));
                        }
                    } else {
                        return false;
                    }
                }
            } else if (ent.catType == 3) {
                // View aliases vector<string>
                uint64_t cnt = 0;
                if (!ms.ReadVarU64(cnt) || cnt > 4096) return false;
                for (uint64_t i = 0; i < cnt; ++i) {
                    std::string a;
                    if (!ms.ReadStr(a, 4096)) return false;
                }
            } else {
                return false;
            }
            break;
        }
        case 202: {
            if (ent.catType == 1) {
                // constraints vector<unique_ptr<Constraint>> (support empty or simple NOT NULL / PK)
                uint64_t cnt = 0;
                if (!ms.ReadVarU64(cnt) || cnt > 4096) return false;
                for (uint64_t i = 0; i < cnt; ++i) {
                    bool present = false;
                    if (!ms.ReadBool(present)) return false;
                    if (present) {
                        for (;;) {
                            uint16_t kf = 0;
                            if (!ms.ReadU16(kf)) return false;
                            if (kf == 0xFFFFu) break;
                            if (kf == 100 || kf == 200) { uint64_t v = 0; if (!ms.ReadVarU64(v)) return false; }
                            else if (kf == 201) {
                                uint64_t n = 0; if (!ms.ReadVarU64(n) || n > 1024) return false;
                                for (uint64_t j = 0; j < n; ++j) { std::string s; if (!ms.ReadStr(s, 1024)) return false; }
                            } else return false;
                        }
                    }
                }
            } else if (ent.catType == 3) {
                uint64_t cnt = 0;
                if (!ms.ReadVarU64(cnt) || cnt > 4096) return false;
                for (uint64_t i = 0; i < cnt; ++i) {
                    DuckColDef cd;
                    if (!ReadDuckLogicalTypeObj(ms, cd)) return false;
                    ent.cols.push_back(std::move(cd));
                }
            } else {
                return false;
            }
            break;
        }
        case 204: {
            if (ent.catType != 3) return false;
            uint64_t cnt = 0;
            if (!ms.ReadVarU64(cnt) || cnt > 4096) return false;
            for (uint64_t i = 0; i < cnt; ++i) {
                std::string cn;
                if (!ms.ReadStr(cn, 4096)) return false;
                if (i < ent.cols.size()) ent.cols[static_cast<size_t>(i)].name = std::move(cn);
            }
            break;
        }
        default:
            return false;
        }
    }
}

inline bool ReadDuckBaseStatsVal(DuckMetaStream& ms, const DuckColDef& col, std::wstring& outVal) {
    switch (col.typeId) {
    case 10: { bool b = false; if (!ms.ReadBool(b)) return false; outVal = b ? L"true" : L"false"; return true; }
    case 11: case 12: case 13: case 14: {
        int64_t v = 0; if (!ms.ReadVarI64(v)) return false; outVal = std::to_wstring(v); return true;
    }
    case 15: {
        int64_t v = 0; if (!ms.ReadVarI64(v)) return false; outVal = FormatDuckDateDays(static_cast<int32_t>(v)); return true;
    }
    case 19: case 32: {
        int64_t v = 0; if (!ms.ReadVarI64(v)) return false; outVal = FormatDuckTimestampUs(v); return true;
    }
    case 22: {
        uint32_t u = 0; if (!ms.ReadU32(u)) return false;
        float f = 0.0f; std::memcpy(&f, &u, 4);
        outVal = FmtDouble(f); return true;
    }
    case 23: {
        uint64_t u = 0; if (!ms.ReadU64(u)) return false;
        double d = 0.0; std::memcpy(&d, &u, 8);
        outVal = FmtDouble(d); return true;
    }
    case 24: case 25: case 26: {
        std::string s; if (!ms.ReadStr(s, 4096)) return false;
        outVal = PathUtils::Utf8ToWide(s); return true;
    }
    case 28: case 29: case 30: case 31: {
        uint64_t v = 0; if (!ms.ReadVarU64(v)) return false; outVal = std::to_wstring(v); return true;
    }
    default: {
        std::string s; if (!ms.ReadStr(s, 4096)) return false;
        outVal = PathUtils::Utf8ToWide(s); return true;
    }
    }
}

inline bool ReadDuckBaseStatsObj(DuckMetaStream& ms, const DuckColDef& col, DuckSegPtr& seg) {
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return true;
        if (fid == 100) { if (!ms.ReadBool(seg.hasNull)) return false; }
        else if (fid == 101) { if (!ms.ReadBool(seg.hasNoNull)) return false; }
        else if (fid == 102) { uint64_t dc = 0; if (!ms.ReadVarU64(dc)) return false; }
        else if (fid == 103) {
            for (;;) {
                uint16_t tf = 0;
                if (!ms.ReadU16(tf)) return false;
                if (tf == 0xFFFFu) break;
                if (tf == 200) {
                    bool hasMin = false;
                    if (!ms.ReadBool(hasMin)) return false;
                    seg.hasConstVal = hasMin;
                } else if (tf == 201) {
                    if (!ReadDuckBaseStatsVal(ms, col, seg.constVal)) return false;
                    seg.hasConstVal = true;
                } else if (tf == 202) {
                    bool hasMax = false;
                    if (!ms.ReadBool(hasMax)) return false;
                } else if (tf == 203) {
                    std::wstring dummy;
                    if (!ReadDuckBaseStatsVal(ms, col, dummy)) return false;
                } else {
                    return false;
                }
            }
        } else {
            return false;
        }
    }
}

inline bool ReadDuckDataPointerObj(DuckMetaStream& ms, const DuckColDef& col, DuckSegPtr& seg) {
    seg = {};
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return true;
        if (fid == 100) {
            if (!ms.ReadVarU64(seg.rowStart)) return false;
        } else if (fid == 101) {
            if (!ms.ReadVarU64(seg.tupleCount)) return false;
        } else if (fid == 102) {
            // BlockPointer { 100: block_id (int64 zigzag), 101: offset (uint32 varint), 0xFFFF }
            for (;;) {
                uint16_t bf = 0;
                if (!ms.ReadU16(bf)) return false;
                if (bf == 0xFFFFu) break;
                if (bf == 100) { if (!ms.ReadVarI64(seg.blockId)) return false; }
                else if (bf == 101) { uint64_t off = 0; if (!ms.ReadVarU64(off)) return false; seg.offset = static_cast<uint32_t>(off); }
                else return false;
            }
        } else if (fid == 103) {
            uint64_t comp = 0;
            if (!ms.ReadVarU64(comp) || comp > 32) return false;
            seg.compression = static_cast<uint8_t>(comp);
        } else if (fid == 104) {
            if (!ReadDuckBaseStatsObj(ms, col, seg)) return false;
        } else if (fid == 105) {
            bool hasState = false;
            if (!ms.ReadBool(hasState)) return false;
            if (hasState) {
                for (;;) {
                    uint16_t sf = 0;
                    if (!ms.ReadU16(sf)) return false;
                    if (sf == 0xFFFFu) break;
                    return false;
                }
            }
        } else {
            return false;
        }
    }
}

inline bool ReadDuckColumnDataObj(DuckMetaStream& ms, const DuckColDef& col, std::vector<DuckSegPtr>& outSegs) {
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return true;
        if (fid == 100) {
            uint64_t cnt = 0;
            if (!ms.ReadVarU64(cnt) || cnt > 65536) return false;
            for (uint64_t i = 0; i < cnt; ++i) {
                DuckSegPtr sp;
                if (!ReadDuckDataPointerObj(ms, col, sp)) return false;
                outSegs.push_back(std::move(sp));
            }
        } else {
            return false;
        }
    }
}

inline bool ReadDuckRowGroupObj(const std::vector<uint8_t>& raw, uint64_t blockAlloc, DuckMetaStream& ms, DuckTableEntry& tbl) {
    uint64_t rgRowStart = 0, rgTupleCount = 0;
    std::vector<DuckMetaPtr> colMetaPtrs;
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) break;
        if (fid == 100) {
            if (!ms.ReadVarU64(rgRowStart)) return false;
        } else if (fid == 101) {
            if (!ms.ReadVarU64(rgTupleCount)) return false;
        } else if (fid == 102) {
            uint64_t colCnt = 0;
            if (!ms.ReadVarU64(colCnt) || colCnt > 4096) return false;
            colMetaPtrs.reserve(static_cast<size_t>(colCnt));
            for (uint64_t c = 0; c < colCnt; ++c) {
                DuckMetaPtr mp;
                if (!ReadDuckMetaPtrObj(ms, mp)) return false;
                colMetaPtrs.push_back(mp);
            }
        } else if (fid == 103) {
            uint64_t delCnt = 0;
            if (!ms.ReadVarU64(delCnt) || delCnt > 4096) return false;
            for (uint64_t d = 0; d < delCnt; ++d) {
                DuckMetaPtr mp;
                if (!ReadDuckMetaPtrObj(ms, mp)) return false;
            }
        } else if (fid == 104) {
            bool hasMeta = false;
            if (!ms.ReadBool(hasMeta)) return false;
        } else if (fid == 105) {
            uint64_t extraCnt = 0;
            if (!ms.ReadVarU64(extraCnt) || extraCnt > 65536) return false;
            for (uint64_t e = 0; e < extraCnt; ++e) {
                uint64_t blk = 0;
                if (!ms.ReadVarU64(blk)) return false;
            }
        } else {
            return false;
        }
    }
    (void)rgRowStart;
    (void)rgTupleCount;

    if (tbl.colSegments.size() < tbl.cols.size()) {
        tbl.colSegments.resize(tbl.cols.size());
    }
    const size_t nCols = std::min(colMetaPtrs.size(), tbl.cols.size());
    for (size_t c = 0; c < nCols; ++c) {
        if (colMetaPtrs[c].blockPtr == UINT64_MAX) continue;
        DuckMetaStream cms(raw, blockAlloc, colMetaPtrs[c].blockPtr, colMetaPtrs[c].offset);
        if (!cms.Valid()) return false;
        if (!ReadDuckColumnDataObj(cms, tbl.cols[c], tbl.colSegments[c])) return false;
    }
    return true;
}

inline bool ReadDuckPersistentTableData(const std::vector<uint8_t>& raw, uint64_t blockAlloc, DuckTableEntry& tbl) {
    if (tbl.tablePtr.blockPtr == UINT64_MAX) return true;
    DuckMetaStream ms(raw, blockAlloc, tbl.tablePtr.blockPtr, tbl.tablePtr.offset);
    if (!ms.Valid()) return false;

    DuckMetaPtr rgStreamPtr;
    uint64_t rgCount = 0;
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) break;
        if (fid == 100) {
            if (!ms.ReadVarU64(tbl.totalRows)) return false;
        } else if (fid == 101) {
            // Inline row_groups list: varint count + RowGroup objects
            uint64_t inlineRgCnt = 0;
            if (!ms.ReadVarU64(inlineRgCnt) || inlineRgCnt > 65536) return false;
            for (uint64_t r = 0; r < inlineRgCnt; ++r) {
                if (!ReadDuckRowGroupObj(raw, blockAlloc, ms, tbl)) return false;
            }
        } else if (fid == 102) {
            if (!ReadDuckMetaPtrObj(ms, rgStreamPtr)) return false;
        } else if (fid == 103) {
            if (!ms.ReadVarU64(rgCount) || rgCount > 65536) return false;
        } else {
            return false;
        }
    }

    if (rgStreamPtr.blockPtr != UINT64_MAX && rgCount > 0) {
        DuckMetaStream rgMs(raw, blockAlloc, rgStreamPtr.blockPtr, rgStreamPtr.offset);
        if (!rgMs.Valid()) return false;
        for (uint64_t r = 0; r < rgCount; ++r) {
            if (!ReadDuckRowGroupObj(raw, blockAlloc, rgMs, tbl)) return false;
        }
    }
    return true;
}

inline void DecodeDuckColumnValues(
    const std::vector<uint8_t>& raw,
    uint64_t blockAlloc,
    const DuckColDef& col,
    const std::vector<DuckSegPtr>& segs,
    size_t maxRows,
    std::vector<std::wstring>& outVals)
{
    for (const auto& seg : segs) {
        if (outVals.size() >= maxRows) break;
        const size_t want = static_cast<size_t>(std::min<uint64_t>(seg.tupleCount, maxRows - outVals.size()));
        if (want == 0) continue;

        // CONSTANT compression (2)
        if (seg.compression == 2) {
            const std::wstring v = (seg.hasNull && !seg.hasNoNull && !seg.hasConstVal) ? L"NULL" : seg.constVal;
            for (size_t i = 0; i < want; ++i) outVals.push_back(v);
            continue;
        }

        if (seg.blockId < 0) continue;
        const uint64_t bid = static_cast<uint64_t>(seg.blockId);
        if (bid > (raw.size() - 12288ULL) / blockAlloc) continue;
        const uint64_t segFileOff = 12288ULL + bid * blockAlloc + 8ULL + seg.offset;
        if (segFileOff >= raw.size()) continue;
        const uint8_t* p = raw.data() + static_cast<size_t>(segFileOff);
        const size_t avail = raw.size() - static_cast<size_t>(segFileOff);

        auto decodeFixedElem = [&](const uint8_t* ep, size_t elemSz) -> std::wstring {
            switch (col.typeId) {
            case 10: return ep[0] ? L"true" : L"false";
            case 11: return std::to_wstring(static_cast<int8_t>(ep[0]));
            case 12: return std::to_wstring(RdI16LE(ep));
            case 13: return std::to_wstring(RdI32LE(ep));
            case 14: return std::to_wstring(RdI64LE(ep));
            case 15: return FormatDuckDateDays(RdI32LE(ep));
            case 17: return FormatDuckTimestampUs(RdI64LE(ep) * 1000000LL);
            case 18: return FormatDuckTimestampUs(RdI64LE(ep) * 1000LL);
            case 19:
            case 32: return FormatDuckTimestampUs(RdI64LE(ep));
            case 20: return FormatDuckTimestampUs(RdI64LE(ep) / 1000LL);
            case 21: {
                int64_t rawDec = (elemSz == 2) ? RdI16LE(ep) : ((elemSz == 4) ? RdI32LE(ep) : RdI64LE(ep));
                if (col.decScale == 0) return std::to_wstring(rawDec);
                double sc = std::pow(10.0, static_cast<int>(col.decScale));
                return FmtDouble(static_cast<double>(rawDec) / sc);
            }
            case 22: return FmtDouble(RdF32LE(ep));
            case 23: return FmtDouble(RdF64LE(ep));
            case 28: return std::to_wstring(ep[0]);
            case 29: return std::to_wstring(RdU16LE(ep));
            case 30: return std::to_wstring(RdU32LE(ep));
            case 31: return std::to_wstring(RdU64LE(ep));
            default: return L"";
            }
        };

        size_t fixedSz = 0;
        switch (col.typeId) {
        case 10: case 11: case 28: fixedSz = 1; break;
        case 12: case 29: fixedSz = 2; break;
        case 13: case 15: case 22: case 30: fixedSz = 4; break;
        case 14: case 16: case 17: case 18: case 19: case 20: case 23: case 31: case 32: fixedSz = 8; break;
        case 21: fixedSz = (col.decWidth <= 4) ? 2 : ((col.decWidth <= 9) ? 4 : 8); break;
        default: break;
        }

        // UNCOMPRESSED (1)
        if (seg.compression == 1) {
            if (fixedSz > 0) {
                const uint64_t valBytes = seg.tupleCount * fixedSz;
                const uint8_t* validityMask = (seg.hasNull && valBytes + ((seg.tupleCount + 63ULL) / 64ULL) * 8ULL <= avail)
                    ? (p + static_cast<size_t>(valBytes))
                    : nullptr;
                for (size_t i = 0; i < want && (i + 1) * fixedSz <= avail; ++i) {
                    if (validityMask) {
                        const uint64_t word = RdU64LE(validityMask + (i / 64) * 8);
                        if (((word >> (i % 64)) & 1ULL) == 0) {
                            outVals.push_back(L"NULL");
                            continue;
                        }
                    }
                    outVals.push_back(decodeFixedElem(p + i * fixedSz, fixedSz));
                }
            } else if (col.typeId == 24 || col.typeId == 25 || col.typeId == 26) {
                // Layout A: DuckDB StringUncompressed dictionary-at-end ([u32 dict_size][u32 dict_end][i32 end_offsets[tupleCount]] ... dict at end)
                if (avail >= 8 + want * 4) {
                    const uint32_t dictSize = RdU32LE(p);
                    const uint32_t dictEnd  = RdU32LE(p + 4);
                    if (dictEnd >= 8 + seg.tupleCount * 4 && dictEnd <= avail && dictSize <= dictEnd) {
                        uint32_t prevAbs = 0;
                        bool okDict = true;
                        std::vector<std::wstring> tmpStrs;
                        tmpStrs.reserve(want);
                        for (size_t i = 0; i < want; ++i) {
                            const int32_t rawOff = RdI32LE(p + 8 + i * 4);
                            const uint32_t curAbs = static_cast<uint32_t>(rawOff < 0 ? -rawOff : rawOff);
                            if (curAbs < prevAbs || curAbs > dictSize || curAbs > dictEnd) { okDict = false; break; }
                            const uint32_t sLen = curAbs - prevAbs;
                            const char* sPtr = reinterpret_cast<const char*>(p + dictEnd - curAbs);
                            tmpStrs.push_back(PathUtils::Utf8ToWide(std::string(sPtr, sLen)));
                            prevAbs = curAbs;
                        }
                        if (okDict) {
                            for (auto& s : tmpStrs) outVals.push_back(std::move(s));
                            continue;
                        }
                    }
                }
                // Layout B: DuckDB 16-byte string_t vector ([u32 len][12B inline OR 4B prefix + u64 relOffset])
                if (avail >= want * 16) {
                    for (size_t i = 0; i < want; ++i) {
                        const uint8_t* sp = p + i * 16;
                        const uint32_t sLen = RdU32LE(sp);
                        if (sLen <= 12) {
                            outVals.push_back(PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(sp + 4), sLen)));
                        } else {
                            const uint64_t strOff = RdU64LE(sp + 8);
                            if (strOff <= avail && sLen <= avail - static_cast<size_t>(strOff)) {
                                outVals.push_back(PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(p + strOff), sLen)));
                            } else {
                                outVals.push_back(PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(sp + 4), 4)) + L"...");
                            }
                        }
                    }
                }
            }
        } else if (seg.compression == 3 && fixedSz > 0 && avail >= 4) {
            // RLE compression: [u32 run_count] followed by runs of [u16 count][T value]
            const uint32_t runCount = RdU32LE(p);
            size_t cur = 4;
            for (uint32_t r = 0; r < runCount && outVals.size() < maxRows; ++r) {
                if (cur + 2 + fixedSz > avail) break;
                const uint16_t rep = RdU16LE(p + cur);
                const std::wstring val = decodeFixedElem(p + cur + 2, fixedSz);
                cur += 2 + fixedSz;
                for (uint16_t k = 0; k < rep && outVals.size() < maxRows; ++k) {
                    outVals.push_back(val);
                }
            }
        } else if (seg.compression == 6 && (col.typeId == 24 || col.typeId == 25) && avail >= 12) {
            // DICTIONARY compression: [u32 dict_size][u32 dict_end][u32 dict_count][u32 index[tupleCount]][u32 end_offsets[dict_count]] ...
            const uint32_t dictSize  = RdU32LE(p);
            const uint32_t dictEnd   = RdU32LE(p + 4);
            const uint32_t dictCount = RdU32LE(p + 8);
            const uint64_t idxTableBytes = seg.tupleCount * 4ULL;
            const uint64_t offTableStart = 12ULL + idxTableBytes;
            if (dictEnd <= avail && dictSize <= dictEnd && dictCount <= 65536 && offTableStart + dictCount * 4ULL <= dictEnd) {
                std::vector<std::wstring> dictEntries(dictCount);
                uint32_t prev = 0;
                bool ok = true;
                for (uint32_t d = 0; d < dictCount; ++d) {
                    const uint32_t curAbs = RdU32LE(p + static_cast<size_t>(offTableStart) + d * 4);
                    if (curAbs < prev || curAbs > dictSize) { ok = false; break; }
                    const uint32_t sLen = curAbs - prev;
                    dictEntries[d] = PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(p + dictEnd - curAbs), sLen));
                    prev = curAbs;
                }
                if (ok) {
                    for (size_t i = 0; i < want; ++i) {
                        const uint32_t idx = RdU32LE(p + 12 + i * 4);
                        outVals.push_back(idx < dictEntries.size() ? dictEntries[idx] : L"NULL");
                    }
                }
            }
        }
    }
}

inline bool ParseDuckDb(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    // DuckDB MainHeader at sector 0 (0..4095), DatabaseHeader 0 at 4096, DatabaseHeader 1 at 8192, blocks at 12288+
    if (raw.size() < 12288 + 32 || std::memcmp(raw.data() + 8, "DUCK", 4) != 0) return false;

    const uint64_t fmtVersion = RdU64LE(raw.data() + 12);
    if (fmtVersion == 0 || fmtVersion > 100000) return false;

    std::wstring gitDesc = CleanAsciiFixed(raw.data() + 52, 64);
    std::wstring gitHash = CleanAsciiFixed(raw.data() + 116, 64);
    if (gitDesc.empty()) {
        gitDesc = CleanAsciiFixed(raw.data() + 28, 64);
        gitHash = CleanAsciiFixed(raw.data() + 92, 64);
    }

    auto resolveAlloc = [&](uint64_t hdrAlloc) -> uint64_t {
        if (hdrAlloc >= 4096ULL && hdrAlloc <= 16777216ULL && (hdrAlloc & (hdrAlloc - 1ULL)) == 0) {
            return hdrAlloc;
        }
        if (raw.size() >= 12288ULL + 262144ULL) return 262144ULL;
        const uint64_t dataBytes = raw.size() - 12288ULL;
        if (dataBytes >= 16384ULL && (dataBytes % 16384ULL) == 0) return 16384ULL;
        if (dataBytes >= 4096ULL && (dataBytes % 4096ULL) == 0) return 4096ULL;
        return 262144ULL;
    };

    struct DbSlot {
        bool valid = false;
        size_t sectorOff = 0;
        uint64_t iteration = 0;
        uint64_t metaBlock = UINT64_MAX;
        uint64_t freeList = UINT64_MAX;
        uint64_t blockCount = 0;
        uint64_t blockAlloc = 262144;
        uint64_t vectorSize = 2048;
    };

    auto readSlot = [&](size_t off) -> DbSlot {
        DbSlot s;
        s.sectorOff = off;
        s.iteration  = RdU64LE(raw.data() + off + 8);
        s.metaBlock  = RdU64LE(raw.data() + off + 16);
        s.freeList   = RdU64LE(raw.data() + off + 24);
        s.blockCount = RdU64LE(raw.data() + off + 32);
        const uint64_t rawAlloc = RdU64LE(raw.data() + off + 40);
        s.blockAlloc = resolveAlloc(rawAlloc);
        s.vectorSize = RdU64LE(raw.data() + off + 48);

        if (s.metaBlock == UINT64_MAX) return s;
        if (s.iteration == 0 && s.blockCount == 0) return s;
        const uint64_t physId = s.metaBlock & 0x00FFFFFFFFFFFFFFULL;
        const uint8_t subIdx  = static_cast<uint8_t>(s.metaBlock >> 56);
        if (subIdx >= 64) return s;
        if (physId > (raw.size() - 12288ULL) / s.blockAlloc) return s;
        if (12288ULL + physId * s.blockAlloc + 18ULL > raw.size()) return s;
        s.valid = true;
        return s;
    };

    const DbSlot slot0 = readSlot(4096);
    const DbSlot slot1 = readSlot(8192);
    if (!slot0.valid && !slot1.valid) return false;
    const DbSlot& active = (!slot0.valid) ? slot1 : ((!slot1.valid) ? slot0 : ((slot1.iteration >= slot0.iteration) ? slot1 : slot0));

    // Walk linked MetaBlocks and deserialize the binary Catalog via DuckDB BinaryMetadataDeserializer
    DuckMetaStream rootStream(raw, active.blockAlloc, active.metaBlock, 0);
    if (!rootStream.Valid()) return false;

    std::vector<DuckTableEntry> entries;
    for (size_t entryIdx = 0; entryIdx < 4096; ++entryIdx) {
        uint16_t outerFid = 0;
        if (!rootStream.ReadU16(outerFid)) break;
        if (outerFid == 0xFFFFu) break; // explicit end of checkpoint stream
        if (outerFid != 100u) return false; // reject non-DuckDB / scraped text blocks!

        DuckTableEntry ent;
        if (!ReadDuckCreateInfoObj(rootStream, ent)) return false;

        // Read remaining fields of checkpoint catalog wrapper (101 = MetaBlockPointer table_pointer, terminated by 0xFFFF)
        for (;;) {
            uint16_t wf = 0;
            if (!rootStream.ReadU16(wf)) return false;
            if (wf == 0xFFFFu) break;
            if (wf == 101) {
                if (!ReadDuckMetaPtrObj(rootStream, ent.tablePtr)) return false;
            } else {
                return false;
            }
        }

        if (ent.catType == 0) break; // CatalogType::INVALID sentinel marks end of catalog
        entries.push_back(std::move(ent));
    }

    if (entries.empty()) return false;

    for (auto& ent : entries) {
        if (ent.catType == 1 && ent.tablePtr.blockPtr != UINT64_MAX) {
            if (!ReadDuckPersistentTableData(raw, active.blockAlloc, ent)) return false;
        }
    }

    SpreadsheetSheet catSheet;
    catSheet.name = L"DuckDB_Header_And_Catalog";
    catSheet.rows.push_back({ L"Storage Field / Catalog Object", L"Category", L"Value / Schema Details" });
    catSheet.rows.push_back({ L"Storage Magic", L"MainHeader", L"DUCK" });
    catSheet.rows.push_back({ L"Storage Format Version", L"MainHeader", std::to_wstring(fmtVersion) });
    if (!gitDesc.empty()) catSheet.rows.push_back({ L"Engine Version", L"MainHeader", gitDesc });
    if (!gitHash.empty()) catSheet.rows.push_back({ L"Engine Git Hash", L"MainHeader", gitHash });
    catSheet.rows.push_back({ L"Active Checkpoint Iteration", L"DatabaseHeader", std::to_wstring(active.iteration) });
    catSheet.rows.push_back({ L"Meta Block Pointer", L"DatabaseHeader",
        L"block=" + std::to_wstring(active.metaBlock & 0x00FFFFFFFFFFFFFFULL) +
        L", sub=" + std::to_wstring(active.metaBlock >> 56) });
    catSheet.rows.push_back({ L"Block Count", L"DatabaseHeader", std::to_wstring(active.blockCount) });
    catSheet.rows.push_back({ L"Block Allocation Size", L"DatabaseHeader", PathUtils::FormatByteSize(active.blockAlloc) });
    if (active.vectorSize > 0) {
        catSheet.rows.push_back({ L"Vector Size", L"DatabaseHeader", std::to_wstring(active.vectorSize) });
    }

    std::vector<SpreadsheetSheet> tableSheets;
    for (const auto& ent : entries) {
        const std::wstring schW = ent.schema.empty() ? L"main" : PathUtils::Utf8ToWide(ent.schema);
        const std::wstring nameW = PathUtils::Utf8ToWide(ent.name);
        if (ent.catType == 2) {
            catSheet.rows.push_back({ schW, L"Schema", ent.internal ? L"internal schema" : L"user schema" });
        } else if (ent.catType == 3) {
            std::wstring detail = ent.sql.empty() ? L"VIEW" : PathUtils::Utf8ToWide(ent.sql);
            catSheet.rows.push_back({ schW + L"." + nameW, L"View", std::move(detail) });
        } else if (ent.catType == 1) {
            std::wstring colSummary;
            for (size_t c = 0; c < ent.cols.size(); ++c) {
                if (c > 0) colSummary += L", ";
                colSummary += PathUtils::Utf8ToWide(ent.cols[c].name) + L" " + DuckTypeName(ent.cols[c]);
            }
            std::wstring infoStr = L"(" + colSummary + L") \x2014 " + std::to_wstring(ent.totalRows) + L" rows";
            catSheet.rows.push_back({ schW + L"." + nameW, L"Table", std::move(infoStr) });

            if (!ent.internal && !ent.cols.empty() && tableSheets.size() < 24) {
                SpreadsheetSheet tsh;
                tsh.name = nameW.empty() ? L"Table" : nameW;
                std::vector<std::wstring> hdrRow;
                hdrRow.reserve(ent.cols.size());
                for (const auto& cd : ent.cols) {
                    hdrRow.push_back(PathUtils::Utf8ToWide(cd.name) + L" (" + DuckTypeName(cd) + L")");
                }
                tsh.rows.push_back(std::move(hdrRow));

                const size_t maxPreviewRows = static_cast<size_t>(std::min<uint64_t>(ent.totalRows, 256));
                if (maxPreviewRows > 0 && ent.colSegments.size() == ent.cols.size()) {
                    std::vector<std::vector<std::wstring>> decodedCols(ent.cols.size());
                    size_t rowCap = 0;
                    for (size_t c = 0; c < ent.cols.size(); ++c) {
                        DecodeDuckColumnValues(raw, active.blockAlloc, ent.cols[c], ent.colSegments[c], maxPreviewRows, decodedCols[c]);
                        rowCap = std::max(rowCap, decodedCols[c].size());
                    }
                    for (size_t r = 0; r < rowCap; ++r) {
                        std::vector<std::wstring> row(ent.cols.size());
                        for (size_t c = 0; c < ent.cols.size(); ++c) {
                            if (r < decodedCols[c].size()) row[c] = std::move(decodedCols[c][r]);
                        }
                        tsh.rows.push_back(std::move(row));
                    }
                }
                FinalizeSheetCols(tsh);
                tableSheets.push_back(std::move(tsh));
            }
        }
    }

    FinalizeSheetCols(catSheet);
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"DuckDB Analytical Database (.duckdb)";
    out.sheets.push_back(std::move(catSheet));
    for (auto& ts : tableSheets) out.sheets.push_back(std::move(ts));
    out.valid = true;
    return true;
}

} // namespace AiDataDetail

inline bool ParseDuckDbDatabase(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return AiDataDetail::ParseDuckDb(raw, out); }

} // namespace Formats
