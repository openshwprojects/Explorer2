#pragma once
// RedisRdb.h — Redis RDB Memory Snapshot (.rdb) Reader

namespace Formats {

// ============================================================================
// #82. Redis RDB Memory Snapshot (.rdb) — v1..v12 + LZF + Ziplist + Listpack + Intset + Streams
// ============================================================================
namespace DetailStructuredDataDoc {

inline bool ReadRdbLen(const uint8_t* data, size_t size, size_t& pos, uint64_t& outLen, bool& outIsEncoded) {
    outIsEncoded = false;
    if (pos >= size) return false;
    uint8_t first = data[pos++];
    uint8_t type = (first >> 6) & 0x03u;
    if (type == 0) {
        outLen = first & 0x3Fu;
        return true;
    }
    if (type == 1) {
        if (pos >= size) return false;
        outLen = (static_cast<uint64_t>(first & 0x3Fu) << 8) | data[pos++];
        return true;
    }
    if (type == 2) {
        if (first == 0x80) {
            if (pos + 4 > size) return false;
            outLen = RdU32BE(data + pos);
            pos += 4;
            return true;
        }
        if (first == 0x81) {
            if (pos + 8 > size) return false;
            outLen = RdU64BE(data + pos);
            pos += 8;
            return true;
        }
        return false;
    }
    outIsEncoded = true;
    outLen = first & 0x3Fu;
    return true;
}

inline std::wstring FormatRdbBytesPreview(const uint8_t* p, size_t n) {
    if (n == 0) return L"";
    size_t show = std::min<size_t>(n, 256u);
    if (IsMostlyPrintableUtf8(p, show)) {
        std::wstring s = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), show));
        if (n > show) s += L"...";
        return s;
    }
    return HexBytes(p, n, 16);
}

inline bool ReadRdbRawBytes(const uint8_t* data, size_t size, size_t& pos, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    uint64_t len = 0;
    bool isEnc = false;
    if (!ReadRdbLen(data, size, pos, len, isEnc)) return false;
    if (!isEnc) {
        if (len > size - pos || len > 64u * 1024u * 1024u) return false;
        size_t n = static_cast<size_t>(len);
        outBytes.assign(data + pos, data + pos + n);
        pos += n;
        return true;
    }
    if (len == 0) { // ENC_INT8
        if (pos + 1 > size) return false;
        std::string s = std::to_string(static_cast<int>(static_cast<int8_t>(data[pos++])));
        outBytes.assign(s.begin(), s.end());
        return true;
    }
    if (len == 1) { // ENC_INT16
        if (pos + 2 > size) return false;
        std::string s = std::to_string(static_cast<int>(static_cast<int16_t>(RdU16LE(data + pos))));
        pos += 2;
        outBytes.assign(s.begin(), s.end());
        return true;
    }
    if (len == 2) { // ENC_INT32
        if (pos + 4 > size) return false;
        std::string s = std::to_string(RdI32LE(data + pos));
        pos += 4;
        outBytes.assign(s.begin(), s.end());
        return true;
    }
    if (len == 3) { // ENC_LZF
        uint64_t clen = 0, ulen = 0;
        bool encA = false, encB = false;
        if (!ReadRdbLen(data, size, pos, clen, encA) || encA ||
            !ReadRdbLen(data, size, pos, ulen, encB) || encB) return false;
        if (clen > size - pos) return false;
        if (!::Explorer2::StreamCodecs::DecompressLibLzf(data + pos, static_cast<size_t>(clen), outBytes, static_cast<size_t>(ulen), 32u * 1024u * 1024u)) return false;
        pos += static_cast<size_t>(clen);
        return true;
    }
    return false;
}

inline bool ReadRdbString(const uint8_t* data, size_t size, size_t& pos, std::wstring& outStr) {
    std::vector<uint8_t> rawBytes;
    if (!ReadRdbRawBytes(data, size, pos, rawBytes)) return false;
    outStr = FormatRdbBytesPreview(rawBytes.data(), rawBytes.size());
    return true;
}

inline std::wstring FormatRdbDoubleVal(double d) {
    if (std::isnan(d)) return L"nan";
    if (std::isinf(d)) return d < 0 ? L"-inf" : L"+inf";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%.12g", d);
    return buf;
}

inline bool DecodeRdbZipmap(const std::vector<uint8_t>& zm, std::vector<std::pair<std::wstring, std::wstring>>& outPairs) {
    outPairs.clear();
    if (zm.size() < 2) return false;
    size_t p = 1; // skip zmlen
    auto readZmLen = [&](uint32_t& outL) -> bool {
        if (p >= zm.size()) return false;
        uint8_t b = zm[p++];
        if (b < 254u) { outL = b; return true; }
        if (b == 254u) {
            if (p + 4 > zm.size()) return false;
            outL = RdU32LE(zm.data() + p);
            p += 4;
            return true;
        }
        return false;
    };
    while (p < zm.size() && zm[p] != 0xFFu && outPairs.size() < 65536) {
        uint32_t kLen = 0, vLen = 0;
        if (!readZmLen(kLen) || kLen > zm.size() - p) return false;
        std::wstring k = FormatRdbBytesPreview(zm.data() + p, kLen);
        p += kLen;
        if (!readZmLen(vLen) || p >= zm.size()) return false;
        uint8_t freeBytes = zm[p++];
        if (static_cast<size_t>(vLen) + freeBytes > zm.size() - p) return false;
        std::wstring v = FormatRdbBytesPreview(zm.data() + p, vLen);
        p += static_cast<size_t>(vLen) + freeBytes;
        outPairs.emplace_back(std::move(k), std::move(v));
    }
    return p < zm.size() && zm[p] == 0xFFu;
}

inline bool DecodeRdbZiplist(const std::vector<uint8_t>& zl, std::vector<std::wstring>& outElems) {
    outElems.clear();
    if (zl.size() < 11) return false;
    uint32_t zlBytes = RdU32LE(zl.data());
    if (zlBytes < 11 || zlBytes > zl.size()) return false;
    size_t p = 10;
    while (p < zlBytes && zl[p] != 0xFFu && outElems.size() < 65536) {
        if (zl[p] < 254u) {
            ++p;
        } else if (zl[p] == 254u) {
            if (p + 5 > zlBytes) return false;
            p += 5;
        } else {
            return false;
        }
        if (p >= zlBytes) return false;
        uint8_t enc = zl[p++];
        uint8_t top2 = (enc >> 6) & 0x03u;
        if (top2 == 0) {
            size_t slen = enc & 0x3Fu;
            if (slen > zlBytes - p) return false;
            outElems.push_back(FormatRdbBytesPreview(zl.data() + p, slen));
            p += slen;
        } else if (top2 == 1) {
            if (p >= zlBytes) return false;
            size_t slen = (static_cast<size_t>(enc & 0x3Fu) << 8) | zl[p++];
            if (slen > zlBytes - p) return false;
            outElems.push_back(FormatRdbBytesPreview(zl.data() + p, slen));
            p += slen;
        } else if (top2 == 2) {
            if (p + 4 > zlBytes) return false;
            size_t slen = RdU32BE(zl.data() + p);
            p += 4;
            if (slen > zlBytes - p) return false;
            outElems.push_back(FormatRdbBytesPreview(zl.data() + p, slen));
            p += slen;
        } else if (enc == 0xC0u) {
            if (p + 2 > zlBytes) return false;
            outElems.push_back(std::to_wstring(static_cast<int16_t>(RdU16LE(zl.data() + p))));
            p += 2;
        } else if (enc == 0xD0u) {
            if (p + 4 > zlBytes) return false;
            outElems.push_back(std::to_wstring(RdI32LE(zl.data() + p)));
            p += 4;
        } else if (enc == 0xE0u) {
            if (p + 8 > zlBytes) return false;
            outElems.push_back(std::to_wstring(static_cast<int64_t>(RdU64LE(zl.data() + p))));
            p += 8;
        } else if (enc == 0xF0u) {
            if (p + 3 > zlBytes) return false;
            int32_t v24 = static_cast<int32_t>(zl[p]) |
                          (static_cast<int32_t>(zl[p + 1]) << 8) |
                          (static_cast<int32_t>(static_cast<int8_t>(zl[p + 2])) << 16);
            outElems.push_back(std::to_wstring(v24));
            p += 3;
        } else if (enc == 0xFEu) {
            if (p + 1 > zlBytes) return false;
            outElems.push_back(std::to_wstring(static_cast<int>(static_cast<int8_t>(zl[p++]))));
        } else if (enc >= 0xF1u && enc <= 0xFDu) {
            outElems.push_back(std::to_wstring(static_cast<int>(enc & 0x0Fu) - 1));
        } else {
            return false;
        }
    }
    return p < zl.size() && zl[p] == 0xFFu;
}

inline bool DecodeRdbIntset(const std::vector<uint8_t>& is, std::vector<std::wstring>& outElems) {
    outElems.clear();
    if (is.size() < 8) return false;
    uint32_t enc = RdU32LE(is.data());
    uint32_t cnt = RdU32LE(is.data() + 4);
    if ((enc != 2u && enc != 4u && enc != 8u) || static_cast<uint64_t>(cnt) * enc > is.size() - 8) return false;
    size_t take = std::min<size_t>(cnt, 65536u);
    outElems.reserve(std::min<size_t>(take, 256u));
    for (size_t i = 0; i < take; ++i) {
        const uint8_t* p = is.data() + 8 + i * enc;
        if (enc == 2u) outElems.push_back(std::to_wstring(static_cast<int16_t>(RdU16LE(p))));
        else if (enc == 4u) outElems.push_back(std::to_wstring(RdI32LE(p)));
        else outElems.push_back(std::to_wstring(static_cast<int64_t>(RdU64LE(p))));
    }
    return true;
}

inline bool DecodeRdbListpack(const std::vector<uint8_t>& lp, std::vector<std::wstring>& outElems) {
    outElems.clear();
    if (lp.size() < 7) return false;
    uint32_t totBytes = RdU32LE(lp.data());
    if (totBytes < 7 || totBytes > lp.size()) return false;
    size_t p = 6;
    while (p < totBytes && lp[p] != 0xFFu && outElems.size() < 65536) {
        size_t elemStart = p;
        uint8_t b0 = lp[p];
        if ((b0 & 0x80u) == 0) {
            outElems.push_back(std::to_wstring(b0 & 0x7Fu));
            p += 1;
        } else if ((b0 & 0xC0u) == 0x80u) {
            size_t slen = b0 & 0x3Fu;
            if (1 + slen > totBytes - p) return false;
            outElems.push_back(FormatRdbBytesPreview(lp.data() + p + 1, slen));
            p += 1 + slen;
        } else if ((b0 & 0xE0u) == 0xC0u) {
            if (p + 2 > totBytes) return false;
            uint32_t u13 = (static_cast<uint32_t>(b0 & 0x1Fu) << 8) | lp[p + 1];
            int64_t v = (u13 >= 4096u) ? (static_cast<int64_t>(u13) - 8192LL) : static_cast<int64_t>(u13);
            outElems.push_back(std::to_wstring(v));
            p += 2;
        } else if ((b0 & 0xF0u) == 0xE0u) {
            if (p + 2 > totBytes) return false;
            size_t slen = (static_cast<size_t>(b0 & 0x0Fu) << 8) | lp[p + 1];
            if (2 + slen > totBytes - p) return false;
            outElems.push_back(FormatRdbBytesPreview(lp.data() + p + 2, slen));
            p += 2 + slen;
        } else if (b0 == 0xF0u) {
            if (p + 5 > totBytes) return false;
            size_t slen = RdU32LE(lp.data() + p + 1);
            if (5 + slen > totBytes - p) return false;
            outElems.push_back(FormatRdbBytesPreview(lp.data() + p + 5, slen));
            p += 5 + slen;
        } else if (b0 == 0xF1u) {
            if (p + 3 > totBytes) return false;
            outElems.push_back(std::to_wstring(static_cast<int16_t>(RdU16LE(lp.data() + p + 1))));
            p += 3;
        } else if (b0 == 0xF2u) {
            if (p + 4 > totBytes) return false;
            int32_t v24 = static_cast<int32_t>(lp[p + 1]) |
                          (static_cast<int32_t>(lp[p + 2]) << 8) |
                          (static_cast<int32_t>(static_cast<int8_t>(lp[p + 3])) << 16);
            outElems.push_back(std::to_wstring(v24));
            p += 4;
        } else if (b0 == 0xF3u) {
            if (p + 5 > totBytes) return false;
            outElems.push_back(std::to_wstring(RdI32LE(lp.data() + p + 1)));
            p += 5;
        } else if (b0 == 0xF4u) {
            if (p + 9 > totBytes) return false;
            outElems.push_back(std::to_wstring(static_cast<int64_t>(RdU64LE(lp.data() + p + 1))));
            p += 9;
        } else {
            return false;
        }
        size_t elemLen = p - elemStart;
        size_t backLenSz = (elemLen <= 127u) ? 1u : (elemLen < 16383u) ? 2u : (elemLen < 2097151u) ? 3u : (elemLen < 268435455u) ? 4u : 5u;
        if (p + backLenSz > totBytes) return false;
        p += backLenSz;
    }
    return p < lp.size() && lp[p] == 0xFFu;
}

inline bool SkipRdbModuleValue(const uint8_t* data, size_t size, size_t& pos) {
    size_t ops = 0;
    while (pos < size && ops++ < 100000) {
        uint64_t op = 0; bool enc = false;
        if (!ReadRdbLen(data, size, pos, op, enc) || enc) return false;
        if (op == 0) return true; // RDB_MODULE_OPCODE_EOF
        if (op == 1 || op == 2) { // SINT / UINT
            uint64_t v = 0; bool e2 = false;
            if (!ReadRdbLen(data, size, pos, v, e2)) return false;
        } else if (op == 3) { // FLOAT (4B)
            if (pos + 4 > size) return false;
            pos += 4;
        } else if (op == 4) { // DOUBLE (8B)
            if (pos + 8 > size) return false;
            pos += 8;
        } else if (op == 5) { // STRING
            std::wstring tmp;
            if (!ReadRdbString(data, size, pos, tmp)) return false;
        } else {
            return false;
        }
    }
    return false;
}

} // namespace DetailStructuredDataDoc

inline bool ParseRedisRdbSnapshot(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 9 || std::memcmp(raw.data(), "REDIS", 5) != 0) return false;
    std::string verStr(reinterpret_cast<const char*>(raw.data() + 5), 4);
    for (char c : verStr) if (c < '0' || c > '9') return false;
    int rdbVer = std::atoi(verStr.c_str());
    if (rdbVer < 1 || rdbVer > 20) return false;

    SpreadsheetSheet keysSh;
    keysSh.name = L"Redis_Keys";
    keysSh.maxCols = 5;
    keysSh.rows.push_back({ L"DB", L"Key", L"Value Type", L"Preview / Elements", L"TTL" });

    SpreadsheetSheet auxSh;
    auxSh.name = L"RDB_Metadata";
    auxSh.maxCols = 2;
    auxSh.rows.push_back({ L"AUX / Header Field", L"Value" });
    auxSh.rows.push_back({ L"RDB Format Version", PathUtils::Utf8ToWide(verStr) });
    auxSh.rows.push_back({ L"Snapshot Size", PathUtils::FormatByteSize(std::max<uint64_t>(out.sourceFileSize, raw.size())) });

    size_t pos = 9;
    uint64_t curDb = 0;
    uint64_t totalKeysDecoded = 0;
    std::wstring pendingTtl = L"Persistent";
    bool parseErr = false;
    bool sawEof = false;

    auto joinPreviewList = [](const std::vector<std::wstring>& elems, size_t maxShow = 4) -> std::wstring {
        std::wstring p;
        size_t lim = std::min<size_t>(elems.size(), maxShow);
        for (size_t i = 0; i < lim; ++i) {
            if (i) p += L", ";
            p += elems[i];
        }
        if (elems.size() > maxShow) p += L" ... (" + std::to_wstring(elems.size()) + L" items)";
        return p;
    };
    auto joinPreviewPairs = [](const std::vector<std::wstring>& elems, wchar_t kvSep, size_t maxPairs = 3) -> std::wstring {
        std::wstring p;
        size_t totalPairs = elems.size() / 2;
        size_t lim = std::min<size_t>(totalPairs, maxPairs);
        for (size_t i = 0; i < lim; ++i) {
            if (i) p += L", ";
            p += elems[i * 2] + kvSep + elems[i * 2 + 1];
        }
        if (totalPairs > maxPairs) p += L" ... (" + std::to_wstring(totalPairs) + L" entries)";
        return p;
    };

    while (pos < raw.size() && totalKeysDecoded < 100000) {
        uint8_t op = raw[pos++];
        if (op == 0xFFu) { sawEof = true; break; }
        if (op == 0xFAu) { // AUX
            std::wstring k, v;
            if (!DetailStructuredDataDoc::ReadRdbString(raw.data(), raw.size(), pos, k) ||
                !DetailStructuredDataDoc::ReadRdbString(raw.data(), raw.size(), pos, v)) { parseErr = true; break; }
            auxSh.rows.push_back({ k, v });
            continue;
        }
        if (op == 0xFEu) { // SELECTDB
            bool enc = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, curDb, enc) || enc) { parseErr = true; break; }
            continue;
        }
        if (op == 0xFBu) { // RESIZEDB
            uint64_t dbSz = 0, expSz = 0; bool encA = false, encB = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, dbSz, encA) || encA ||
                !DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, expSz, encB) || encB) { parseErr = true; break; }
            auxSh.rows.push_back({ L"DB #" + std::to_wstring(curDb) + L" Hash Table Size",
                                   std::to_wstring(dbSz) + L" keys (" + std::to_wstring(expSz) + L" expiring)" });
            continue;
        }
        if (op == 0xFDu) { // EXPIRETIME (sec)
            if (pos + 4 > raw.size()) { parseErr = true; break; }
            pendingTtl = std::to_wstring(DetailStructuredDataDoc::RdU32LE(raw.data() + pos)) + L" s";
            pos += 4;
            continue;
        }
        if (op == 0xFCu) { // EXPIRETIMEMS (ms)
            if (pos + 8 > raw.size()) { parseErr = true; break; }
            pendingTtl = std::to_wstring(DetailStructuredDataDoc::RdU64LE(raw.data() + pos)) + L" ms";
            pos += 8;
            continue;
        }
        if (op == 0xF8u) { // LRU idle
            uint64_t lru = 0; bool enc = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, lru, enc)) { parseErr = true; break; }
            continue;
        }
        if (op == 0xF9u) { // LFU freq
            if (pos + 1 > raw.size()) { parseErr = true; break; }
            ++pos;
            continue;
        }
        if (op == 0xF7u) { // MODULE_AUX
            uint64_t modId = 0, whenOp = 0, when = 0; bool e1 = false, e2 = false, e3 = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, modId, e1) ||
                !DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, whenOp, e2) ||
                !DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, when, e3) ||
                !DetailStructuredDataDoc::SkipRdbModuleValue(raw.data(), raw.size(), pos)) { parseErr = true; break; }
            continue;
        }
        if (op == 0xF6u || op == 0xF5u) { // FUNCTION2 / FUNCTION_PRE_GA
            std::wstring fnCode;
            if (!DetailStructuredDataDoc::ReadRdbString(raw.data(), raw.size(), pos, fnCode)) { parseErr = true; break; }
            auxSh.rows.push_back({ L"Redis Function Library", fnCode });
            continue;
        }
        if (op == 0xF4u) { // SLOT_INFO (Redis 7.4+)
            uint64_t s1 = 0, s2 = 0, s3 = 0; bool e1 = false, e2 = false, e3 = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, s1, e1) ||
                !DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, s2, e2) ||
                !DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, s3, e3)) { parseErr = true; break; }
            continue;
        }

        std::wstring key;
        if (!DetailStructuredDataDoc::ReadRdbString(raw.data(), raw.size(), pos, key)) { parseErr = true; break; }

        std::wstring typeLabel;
        std::wstring preview;

        if (op == 0x00u) { // STRING
            typeLabel = L"STRING";
            if (!DetailStructuredDataDoc::ReadRdbString(raw.data(), raw.size(), pos, preview)) { parseErr = true; break; }
        } else if (op == 0x01u || op == 0x02u) { // LIST / SET
            typeLabel = (op == 0x01u) ? L"LIST" : L"SET";
            uint64_t cnt = 0; bool enc = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, cnt, enc) || enc) { parseErr = true; break; }
            for (uint64_t i = 0; i < cnt; ++i) {
                std::wstring elem;
                if (!DetailStructuredDataDoc::ReadRdbString(raw.data(), raw.size(), pos, elem)) { parseErr = true; break; }
                if (i < 4) { if (i) preview += L", "; preview += elem; }
            }
            if (parseErr) break;
            if (cnt > 4) preview += L" ... (" + std::to_wstring(cnt) + L" items)";
        } else if (op == 0x03u || op == 0x05u) { // ZSET / ZSET_2 (IEEE binary64 double)
            typeLabel = (op == 0x05u) ? L"ZSET_2" : L"ZSET";
            uint64_t cnt = 0; bool enc = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, cnt, enc) || enc) { parseErr = true; break; }
            for (uint64_t i = 0; i < cnt; ++i) {
                std::wstring member, scoreStr;
                if (!DetailStructuredDataDoc::ReadRdbString(raw.data(), raw.size(), pos, member)) { parseErr = true; break; }
                if (op == 0x05u) {
                    if (pos + 8 > raw.size()) { parseErr = true; break; }
                    scoreStr = DetailStructuredDataDoc::FormatRdbDoubleVal(::Explorer2::ByteIO::RdF64LE(raw.data() + pos));
                    pos += 8;
                } else {
                    if (pos >= raw.size()) { parseErr = true; break; }
                    uint8_t slen = raw[pos++];
                    if (slen == 255u) scoreStr = L"-inf";
                    else if (slen == 254u) scoreStr = L"+inf";
                    else if (slen == 253u) scoreStr = L"nan";
                    else {
                        if (slen > raw.size() - pos) { parseErr = true; break; }
                        scoreStr = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + pos), slen));
                        pos += slen;
                    }
                }
                if (i < 3) { if (i) preview += L", "; preview += member + L":" + scoreStr; }
            }
            if (parseErr) break;
            if (cnt > 3) preview += L" ... (" + std::to_wstring(cnt) + L" members)";
        } else if (op == 0x04u) { // HASH
            typeLabel = L"HASH";
            uint64_t cnt = 0; bool enc = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, cnt, enc) || enc) { parseErr = true; break; }
            for (uint64_t i = 0; i < cnt; ++i) {
                std::wstring hk, hv;
                if (!DetailStructuredDataDoc::ReadRdbString(raw.data(), raw.size(), pos, hk) ||
                    !DetailStructuredDataDoc::ReadRdbString(raw.data(), raw.size(), pos, hv)) { parseErr = true; break; }
                if (i < 3) { if (i) preview += L", "; preview += hk + L"=" + hv; }
            }
            if (parseErr) break;
            if (cnt > 3) preview += L" ... (" + std::to_wstring(cnt) + L" fields)";
        } else if (op == 0x07u) { // MODULE_2
            typeLabel = L"MODULE_2";
            uint64_t modId = 0; bool enc = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, modId, enc) ||
                !DetailStructuredDataDoc::SkipRdbModuleValue(raw.data(), raw.size(), pos)) { parseErr = true; break; }
            preview = L"Module ID " + std::to_wstring(modId);
        } else if (op == 0x09u) { // HASH_ZIPMAP
            typeLabel = L"HASH (ZIPMAP)";
            std::vector<uint8_t> zm;
            std::vector<std::pair<std::wstring, std::wstring>> pairs;
            if (!DetailStructuredDataDoc::ReadRdbRawBytes(raw.data(), raw.size(), pos, zm) ||
                !DetailStructuredDataDoc::DecodeRdbZipmap(zm, pairs)) { parseErr = true; break; }
            for (size_t i = 0; i < std::min<size_t>(pairs.size(), 3u); ++i) {
                if (i) preview += L", ";
                preview += pairs[i].first + L"=" + pairs[i].second;
            }
            if (pairs.size() > 3) preview += L" ... (" + std::to_wstring(pairs.size()) + L" fields)";
        } else if (op == 0x0Au || op == 0x0Cu || op == 0x0Du) { // LIST_ZIPLIST / ZSET_ZIPLIST / HASH_ZIPLIST
            std::vector<uint8_t> zl;
            std::vector<std::wstring> elems;
            if (!DetailStructuredDataDoc::ReadRdbRawBytes(raw.data(), raw.size(), pos, zl) ||
                !DetailStructuredDataDoc::DecodeRdbZiplist(zl, elems)) { parseErr = true; break; }
            if (op == 0x0Au) {
                typeLabel = L"LIST (ZIPLIST)";
                preview = joinPreviewList(elems, 4);
            } else if (op == 0x0Cu) {
                typeLabel = L"ZSET (ZIPLIST)";
                preview = joinPreviewPairs(elems, L':', 3);
            } else {
                typeLabel = L"HASH (ZIPLIST)";
                preview = joinPreviewPairs(elems, L'=', 3);
            }
        } else if (op == 0x0Bu) { // SET_INTSET
            typeLabel = L"SET (INTSET)";
            std::vector<uint8_t> is;
            std::vector<std::wstring> elems;
            if (!DetailStructuredDataDoc::ReadRdbRawBytes(raw.data(), raw.size(), pos, is) ||
                !DetailStructuredDataDoc::DecodeRdbIntset(is, elems)) { parseErr = true; break; }
            preview = joinPreviewList(elems, 5);
        } else if (op == 0x0Eu) { // LIST_QUICKLIST
            typeLabel = L"LIST (QUICKLIST)";
            uint64_t nodeCnt = 0; bool enc = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, nodeCnt, enc) || enc) { parseErr = true; break; }
            std::vector<std::wstring> allElems;
            for (uint64_t n = 0; n < nodeCnt; ++n) {
                std::vector<uint8_t> zl;
                std::vector<std::wstring> nodeElems;
                if (!DetailStructuredDataDoc::ReadRdbRawBytes(raw.data(), raw.size(), pos, zl) ||
                    !DetailStructuredDataDoc::DecodeRdbZiplist(zl, nodeElems)) { parseErr = true; break; }
                for (auto& e : nodeElems) {
                    if (allElems.size() < 65536) allElems.push_back(std::move(e));
                }
            }
            if (parseErr) break;
            preview = joinPreviewList(allElems, 4);
        } else if (op == 0x12u) { // LIST_QUICKLIST_2
            typeLabel = L"LIST (QUICKLIST_2)";
            uint64_t nodeCnt = 0; bool enc = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, nodeCnt, enc) || enc) { parseErr = true; break; }
            std::vector<std::wstring> allElems;
            for (uint64_t n = 0; n < nodeCnt; ++n) {
                uint64_t container = 0; bool cEnc = false;
                std::vector<uint8_t> blob;
                if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, container, cEnc) || cEnc ||
                    !DetailStructuredDataDoc::ReadRdbRawBytes(raw.data(), raw.size(), pos, blob)) { parseErr = true; break; }
                if (container == 1u) { // PLAIN
                    if (allElems.size() < 65536) allElems.push_back(DetailStructuredDataDoc::FormatRdbBytesPreview(blob.data(), blob.size()));
                } else if (container == 2u) { // PACKED (listpack)
                    std::vector<std::wstring> nodeElems;
                    if (!DetailStructuredDataDoc::DecodeRdbListpack(blob, nodeElems)) { parseErr = true; break; }
                    for (auto& e : nodeElems) {
                        if (allElems.size() < 65536) allElems.push_back(std::move(e));
                    }
                } else {
                    parseErr = true;
                    break;
                }
            }
            if (parseErr) break;
            preview = joinPreviewList(allElems, 4);
        } else if (op == 0x10u || op == 0x11u || op == 0x14u || op == 0x06u) { // HASH_LISTPACK / ZSET_LISTPACK / SET_LISTPACK
            std::vector<uint8_t> lp;
            std::vector<std::wstring> elems;
            if (!DetailStructuredDataDoc::ReadRdbRawBytes(raw.data(), raw.size(), pos, lp) ||
                !DetailStructuredDataDoc::DecodeRdbListpack(lp, elems)) { parseErr = true; break; }
            if (op == 0x10u) {
                typeLabel = L"HASH (LISTPACK)";
                preview = joinPreviewPairs(elems, L'=', 3);
            } else if (op == 0x11u) {
                typeLabel = L"ZSET (LISTPACK)";
                preview = joinPreviewPairs(elems, L':', 3);
            } else {
                typeLabel = L"SET (LISTPACK)";
                preview = joinPreviewList(elems, 4);
            }
        } else if (op == 0x0Fu || op == 0x13u || op == 0x15u) { // STREAM_LISTPACKS / _2 / _3
            typeLabel = (op == 0x15u) ? L"STREAM (v3)" : ((op == 0x13u) ? L"STREAM (v2)" : L"STREAM");
            uint64_t nLp = 0; bool enc = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, nLp, enc) || enc) { parseErr = true; break; }
            std::vector<std::wstring> sampleFields;
            for (uint64_t i = 0; i < nLp; ++i) {
                std::vector<uint8_t> nodeId, lpBlob;
                if (!DetailStructuredDataDoc::ReadRdbRawBytes(raw.data(), raw.size(), pos, nodeId) ||
                    !DetailStructuredDataDoc::ReadRdbRawBytes(raw.data(), raw.size(), pos, lpBlob)) { parseErr = true; break; }
                if (i == 0) {
                    std::vector<std::wstring> lpElems;
                    if (DetailStructuredDataDoc::DecodeRdbListpack(lpBlob, lpElems)) {
                        for (size_t k = 0; k < std::min<size_t>(lpElems.size(), 8u); ++k) sampleFields.push_back(lpElems[k]);
                    }
                }
            }
            if (parseErr) break;
            uint64_t items = 0, lastMs = 0, lastSeq = 0; bool e1 = false, e2 = false, e3 = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, items, e1) ||
                !DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, lastMs, e2) ||
                !DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, lastSeq, e3)) { parseErr = true; break; }
            if (op >= 0x13u) {
                uint64_t fMs = 0, fSeq = 0, dMs = 0, dSeq = 0, added = 0; bool ex = false;
                if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, fMs, ex) ||
                    !DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, fSeq, ex) ||
                    !DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, dMs, ex) ||
                    !DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, dSeq, ex) ||
                    !DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, added, ex)) { parseErr = true; break; }
            }
            uint64_t cgroups = 0; bool cgEnc = false;
            if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, cgroups, cgEnc) || cgEnc) { parseErr = true; break; }
            std::wstring cgNames;
            for (uint64_t g = 0; g < cgroups; ++g) {
                std::wstring gname;
                uint64_t gMs = 0, gSeq = 0; bool ge = false;
                if (!DetailStructuredDataDoc::ReadRdbString(raw.data(), raw.size(), pos, gname) ||
                    !DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, gMs, ge) ||
                    !DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, gSeq, ge)) { parseErr = true; break; }
                if (g < 3) { if (g) cgNames += L", "; cgNames += gname; }
                if (op >= 0x13u) {
                    uint64_t entriesRead = 0;
                    if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, entriesRead, ge)) { parseErr = true; break; }
                }
                uint64_t pelSz = 0;
                if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, pelSz, ge) || ge) { parseErr = true; break; }
                for (uint64_t pe = 0; pe < pelSz; ++pe) {
                    if (pos + 24 > raw.size()) { parseErr = true; break; }
                    pos += 24; // 16B rawid + 8B delivery_time
                    uint64_t delivCnt = 0;
                    if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, delivCnt, ge)) { parseErr = true; break; }
                }
                if (parseErr) break;
                uint64_t nCons = 0;
                if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, nCons, ge) || ge) { parseErr = true; break; }
                for (uint64_t c = 0; c < nCons; ++c) {
                    std::wstring cname;
                    if (!DetailStructuredDataDoc::ReadRdbString(raw.data(), raw.size(), pos, cname)) { parseErr = true; break; }
                    size_t timeBytes = (op >= 0x15u) ? 16u : 8u;
                    if (pos + timeBytes > raw.size()) { parseErr = true; break; }
                    pos += timeBytes;
                    uint64_t cpelSz = 0;
                    if (!DetailStructuredDataDoc::ReadRdbLen(raw.data(), raw.size(), pos, cpelSz, ge) || ge) { parseErr = true; break; }
                    if (cpelSz > (raw.size() - pos) / 16u) { parseErr = true; break; }
                    pos += static_cast<size_t>(cpelSz) * 16u;
                }
                if (parseErr) break;
            }
            if (parseErr) break;
            preview = std::to_wstring(items) + L" entries, last=" + std::to_wstring(lastMs) + L"-" + std::to_wstring(lastSeq);
            if (!cgNames.empty()) preview += L", groups=[" + cgNames + L"]";
            if (!sampleFields.empty()) preview += L", lp=[" + joinPreviewList(sampleFields, 4) + L"]";
        } else {
            parseErr = true;
            break;
        }

        if (keysSh.rows.size() <= 1000) {
            keysSh.rows.push_back({ std::to_wstring(curDb), key, typeLabel, preview, pendingTtl });
        }
        ++totalKeysDecoded;
        pendingTtl = L"Persistent";
    }

    const bool truncatedAtLimit = (out.sourceFileSize > raw.size());
    if (parseErr && (!truncatedAtLimit || totalKeysDecoded == 0)) {
        return false;
    }
    if (!sawEof && !truncatedAtLimit && totalKeysDecoded == 0 && auxSh.rows.size() <= 3) {
        return false;
    }

    auxSh.rows.push_back({ L"Total Decoded Keys", std::to_wstring(totalKeysDecoded) });
    FinalizeSheetCols(keysSh);
    FinalizeSheetCols(auxSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Redis RDB Memory Snapshot (v" + PathUtils::Utf8ToWide(verStr) + L")";
    out.sheets.push_back(std::move(keysSh));
    out.sheets.push_back(std::move(auxSh));
    out.valid = true;
    return true;
}


} // namespace Formats
