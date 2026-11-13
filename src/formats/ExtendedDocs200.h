#pragma once
// ExtendedDocs200.h — 20 Databases, Protobuf, Cloud/SecOps, Crypto/Forensics, Chiptune, MusicXML & Game/Doc Formats (#81–#100)
// Covers:
//   Databases & KV (#81–#85): .accdb/.mdb, .rdb (Redis), .sst/.ldb (RocksDB/LevelDB), .mbtiles/.pmtiles, .pb/.pbf (Protobuf)
//   Cloud & SecOps (#86–#91): .tfstate, .sarif, .spdx/.cdx, .gpg/.pgp/.sig, .jks, .cat
//   Windows Forensics (#92–#94): .hiv (+ NTUSER.DAT "regf"), .etl, .pol
//   Audio/Photo/Game/Office (#95–#100): .sid/.nsf/.spc/.vgm, .musicxml/.mxl, .xmp, .uasset/.umap, .prefab/.unity, .pages/.numbers/.key/.vsdx

namespace Formats {

namespace DetailDocs200 {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline int32_t RdI32LE(const uint8_t* p) {
    return static_cast<int32_t>(RdU32LE(p));
}
inline uint64_t RdU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(RdU32LE(p)) |
           (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
}
inline uint16_t RdU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}
inline uint64_t RdU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(RdU32BE(p)) << 32) |
           static_cast<uint64_t>(RdU32BE(p + 4));
}

inline std::wstring HexBytes(const uint8_t* p, size_t n, size_t maxBytes = 16) {
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    size_t lim = std::min(n, maxBytes);
    std::wstring s;
    s.reserve(lim * 3 + 4);
    for (size_t i = 0; i < lim; ++i) {
        if (i) s.push_back(L' ');
        s.push_back(kHex[p[i] >> 4]);
        s.push_back(kHex[p[i] & 0x0F]);
    }
    if (n > maxBytes) s += L" ...";
    return s;
}

inline std::wstring ReadUtf16LE(const uint8_t* p, size_t byteLen) {
    std::wstring out;
    size_t chars = byteLen / 2;
    out.reserve(chars);
    for (size_t i = 0; i < chars; ++i) {
        wchar_t wc = static_cast<wchar_t>(RdU16LE(p + i * 2));
        if (wc == 0) break;
        out.push_back(wc);
    }
    return out;
}

inline bool ReadVarint64(const uint8_t* data, size_t size, size_t& pos, uint64_t& outVal) {
    outVal = 0;
    int shift = 0;
    while (pos < size && shift < 64) {
        uint8_t b = data[pos++];
        outVal |= static_cast<uint64_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0) return true;
        shift += 7;
    }
    return false;
}

inline bool IsMostlyPrintableUtf8(const uint8_t* p, size_t n) {
    if (n == 0) return false;
    size_t good = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = p[i];
        if (c == 0) return false;
        if ((c >= 32 && c <= 126) || c == '\t' || c == '\r' || c == '\n' || c >= 0x80) ++good;
    }
    return good * 10 >= n * 9;
}

inline std::string JsonGetStr(std::string_view obj, std::string_view key) {
    std::string needle = "\"";
    needle.append(key);
    needle.push_back('"');
    size_t p = obj.find(needle);
    if (p == std::string_view::npos) return {};
    p = obj.find(':', p + needle.size());
    if (p == std::string_view::npos) return {};
    ++p;
    while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t' || obj[p] == '\r' || obj[p] == '\n')) ++p;
    if (p >= obj.size()) return {};
    if (obj[p] == '"') {
        ++p;
        std::string res;
        while (p < obj.size() && obj[p] != '"') {
            if (obj[p] == '\\' && p + 1 < obj.size()) {
                ++p;
                if (obj[p] == 'n') res.push_back(' ');
                else res.push_back(obj[p]);
            } else {
                res.push_back(obj[p]);
            }
            ++p;
        }
        return res;
    }
    size_t e = obj.find_first_of(",}\r\n", p);
    if (e == std::string_view::npos) e = obj.size();
    std::string_view tok = obj.substr(p, e - p);
    while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t')) tok.remove_suffix(1);
    return std::string(tok);
}

inline std::vector<uint8_t> DecodeBase64Block(std::string_view in) {
    static const int8_t kLut[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    std::vector<uint8_t> out;
    out.reserve(in.size() * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (char ch : in) {
        if (ch == '=') break;
        int8_t v = kLut[static_cast<uint8_t>(ch)];
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFFu));
        }
    }
    return out;
}

} // namespace DetailDocs200

// ============================================================================
// #81. Microsoft Access Database (.accdb / .mdb)
// ============================================================================
inline bool ParseAccessDatabase200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    if (raw.size() < 32) return false;
    bool isAce = (std::memcmp(raw.data() + 4, "Standard ACE DB", 15) == 0);
    bool isJet = (std::memcmp(raw.data() + 4, "Standard Jet DB", 15) == 0);
    if (!isAce && !isJet) return false;

    uint32_t verCode = DetailDocs200::RdU32LE(raw.data() + 0x14);
    size_t pageSize = (isJet && verCode == 0) ? 2048 : 4096;
    std::wstring eng = isAce ? (L"Access ACE Engine (v" + std::to_wstring(verCode) + L", 4 KB Pages)")
                             : ((verCode == 0) ? L"Jet 3.5 (Access 97, 2 KB Pages)" : L"Jet 4.0 (Access 2000-2003, 4 KB Pages)");

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Microsoft Access Database (" + ext + L")";

    SpreadsheetSheet hdr;
    hdr.name = L"Access_Header";
    hdr.maxCols = 2;
    hdr.rows.push_back({ L"Property", L"Value" });
    hdr.rows.push_back({ L"Database Engine", eng });
    hdr.rows.push_back({ L"Page Size", std::to_wstring(pageSize) + L" B" });
    hdr.rows.push_back({ L"Estimated Pages", std::to_wstring(raw.size() / pageSize) });
    if (raw.size() >= 0x42) {
        uint32_t encKey = DetailDocs200::RdU32LE(raw.data() + 0x3E);
        hdr.rows.push_back({ L"Header Encryption Key", encKey ? L"Obfuscated / RC4 Header" : L"Unencrypted (0x00000000)" });
    }

    // Scan pages for TDEF (0x02 0x01) and UTF-16LE / ASCII identifiers (MSysObjects, user tables/columns)
    SpreadsheetSheet objs;
    objs.name = L"Schema_Identifiers";
    objs.maxCols = 3;
    objs.rows.push_back({ L"Page #", L"Page Type", L"Extracted Identifier / Column" });

    size_t tdefCount = 0;
    for (size_t p = pageSize; p + 16 <= raw.size(); p += pageSize) {
        if (raw[p] == 0x02) ++tdefCount;
    }
    hdr.rows.push_back({ L"Table Definition (TDEF) Pages", std::to_wstring(tdefCount) });

    // Extract readable UTF-16LE strings (Jet4/ACE store names in UTF-16LE) & ASCII (Jet3.5)
    for (size_t i = 32; i + 8 < raw.size() && objs.rows.size() < 250; ++i) {
        // Check UTF-16LE identifier >= 3 chars
        if (raw[i] >= 33 && raw[i] <= 126 && raw[i + 1] == 0 &&
            raw[i + 2] >= 33 && raw[i + 2] <= 126 && raw[i + 3] == 0 &&
            raw[i + 4] >= 33 && raw[i + 4] <= 126 && raw[i + 5] == 0)
        {
            std::wstring s;
            size_t j = i;
            while (j + 1 < raw.size() && raw[j] >= 32 && raw[j] <= 126 && raw[j + 1] == 0 && s.size() < 64) {
                s.push_back(static_cast<wchar_t>(raw[j]));
                j += 2;
            }
            if (s.size() >= 3) {
                size_t pg = i / pageSize;
                std::wstring ptype = (pg * pageSize < raw.size() && raw[pg * pageSize] == 0x02) ? L"TDEF (Table Def)" :
                                     (pg * pageSize < raw.size() && raw[pg * pageSize] == 0x01) ? L"DATA Page" : L"Catalog Page";
                objs.rows.push_back({ std::to_wstring(pg), ptype, s });
                i = j - 1;
                continue;
            }
        }
    }
    if (objs.rows.size() == 1) {
        objs.rows.push_back({ L"0", L"DB Header", isAce ? L"Standard ACE DB" : L"Standard Jet DB" });
    }
    out.sheets.push_back(std::move(hdr));
    out.sheets.push_back(std::move(objs));
    out.valid = true;
    return true;
}

// ============================================================================
// #82. Redis RDB Memory Snapshot (.rdb)
// ============================================================================
namespace DetailDocs200 {

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

inline bool ReadRdbString(const uint8_t* data, size_t size, size_t& pos, std::wstring& outStr) {
    uint64_t len = 0;
    bool isEnc = false;
    if (!ReadRdbLen(data, size, pos, len, isEnc)) return false;
    if (!isEnc) {
        if (len > size - pos) return false;
        size_t show = std::min<size_t>(static_cast<size_t>(len), 256);
        if (IsMostlyPrintableUtf8(data + pos, show)) {
            outStr = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(data + pos), show));
        } else {
            outStr = HexBytes(data + pos, static_cast<size_t>(len), 16);
        }
        pos += static_cast<size_t>(len);
        return true;
    }
    if (len == 0) { // ENC_INT8
        if (pos + 1 > size) return false;
        outStr = std::to_wstring(static_cast<int8_t>(data[pos++]));
        return true;
    }
    if (len == 1) { // ENC_INT16
        if (pos + 2 > size) return false;
        outStr = std::to_wstring(static_cast<int16_t>(RdU16LE(data + pos)));
        pos += 2;
        return true;
    }
    if (len == 2) { // ENC_INT32
        if (pos + 4 > size) return false;
        outStr = std::to_wstring(RdI32LE(data + pos));
        pos += 4;
        return true;
    }
    if (len == 3) { // ENC_LZF compressed string
        uint64_t clen = 0, ulen = 0;
        bool dummy = false;
        if (!ReadRdbLen(data, size, pos, clen, dummy) || !ReadRdbLen(data, size, pos, ulen, dummy)) return false;
        if (clen > size - pos) return false;
        outStr = L"[LZF " + std::to_wstring(clen) + L"B -> " + std::to_wstring(ulen) + L"B]";
        pos += static_cast<size_t>(clen);
        return true;
    }
    return false;
}

} // namespace DetailDocs200

inline bool ParseRedisRdb200(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 9 || std::memcmp(raw.data(), "REDIS", 5) != 0) return false;
    std::string verStr(reinterpret_cast<const char*>(raw.data() + 5), 4);
    for (char c : verStr) if (c < '0' || c > '9') return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Redis RDB Memory Snapshot (v" + PathUtils::Utf8ToWide(verStr) + L")";

    SpreadsheetSheet keysSh;
    keysSh.name = L"Redis_Keys";
    keysSh.maxCols = 5;
    keysSh.rows.push_back({ L"DB", L"Key", L"Value Type", L"Preview / Elements", L"TTL" });

    SpreadsheetSheet auxSh;
    auxSh.name = L"RDB_Metadata";
    auxSh.maxCols = 2;
    auxSh.rows.push_back({ L"AUX / Header Field", L"Value" });
    auxSh.rows.push_back({ L"RDB Format Version", PathUtils::Utf8ToWide(verStr) });
    auxSh.rows.push_back({ L"Snapshot Size", PathUtils::FormatByteSize(raw.size()) });

    size_t pos = 9;
    uint64_t curDb = 0;
    std::wstring pendingTtl = L"Persistent";

    while (pos < raw.size() && keysSh.rows.size() < 500) {
        uint8_t op = raw[pos++];
        if (op == 0xFF) break; // EOF (+ optional 8-byte CRC64)
        if (op == 0xFA) { // AUX
            std::wstring k, v;
            if (!DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, k) ||
                !DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, v)) break;
            auxSh.rows.push_back({ k, v });
            continue;
        }
        if (op == 0xFE) { // SELECTDB
            bool enc = false;
            if (!DetailDocs200::ReadRdbLen(raw.data(), raw.size(), pos, curDb, enc)) break;
            continue;
        }
        if (op == 0xFB) { // RESIZEDB
            uint64_t dbSz = 0, expSz = 0; bool enc = false;
            if (!DetailDocs200::ReadRdbLen(raw.data(), raw.size(), pos, dbSz, enc) ||
                !DetailDocs200::ReadRdbLen(raw.data(), raw.size(), pos, expSz, enc)) break;
            auxSh.rows.push_back({ L"DB #" + std::to_wstring(curDb) + L" Hash Table Size",
                                   std::to_wstring(dbSz) + L" keys (" + std::to_wstring(expSz) + L" expiring)" });
            continue;
        }
        if (op == 0xFD) { // EXPIRETIME (sec)
            if (pos + 4 > raw.size()) break;
            pendingTtl = std::to_wstring(DetailDocs200::RdU32LE(raw.data() + pos)) + L" s";
            pos += 4;
            continue;
        }
        if (op == 0xFC) { // EXPIRETIMEMS (ms)
            if (pos + 8 > raw.size()) break;
            pendingTtl = std::to_wstring(DetailDocs200::RdU64LE(raw.data() + pos)) + L" ms";
            pos += 8;
            continue;
        }

        // Value type opcode (0=String, 1=List, 2=Set, 3=ZSet, 4=Hash)
        std::wstring key;
        if (!DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, key)) break;

        if (op == 0x00) {
            std::wstring val;
            if (!DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, val)) break;
            keysSh.rows.push_back({ std::to_wstring(curDb), key, L"STRING", val, pendingTtl });
        } else if (op == 0x01 || op == 0x02) {
            uint64_t cnt = 0; bool enc = false;
            if (!DetailDocs200::ReadRdbLen(raw.data(), raw.size(), pos, cnt, enc)) break;
            std::wstring preview;
            for (uint64_t i = 0; i < cnt; ++i) {
                std::wstring elem;
                if (!DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, elem)) { pos = raw.size(); break; }
                if (i < 4) { if (i) preview += L", "; preview += elem; }
            }
            if (cnt > 4) preview += L" ... (" + std::to_wstring(cnt) + L" items)";
            keysSh.rows.push_back({ std::to_wstring(curDb), key, (op == 1) ? L"LIST" : L"SET", preview, pendingTtl });
        } else if (op == 0x04) {
            uint64_t cnt = 0; bool enc = false;
            if (!DetailDocs200::ReadRdbLen(raw.data(), raw.size(), pos, cnt, enc)) break;
            std::wstring preview;
            for (uint64_t i = 0; i < cnt; ++i) {
                std::wstring hk, hv;
                if (!DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, hk) ||
                    !DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, hv)) { pos = raw.size(); break; }
                if (i < 3) { if (i) preview += L", "; preview += hk + L"=" + hv; }
            }
            keysSh.rows.push_back({ std::to_wstring(curDb), key, L"HASH", preview, pendingTtl });
        } else {
            keysSh.rows.push_back({ std::to_wstring(curDb), key, L"TYPE_" + std::to_wstring(op), L"(Encoded Structure)", pendingTtl });
            break;
        }
        pendingTtl = L"Persistent";
    }

    if (keysSh.rows.size() == 1) {
        keysSh.rows.push_back({ L"0", L"(Empty Database)", L"-", L"0 keys", L"-" });
    }
    out.sheets.push_back(std::move(keysSh));
    out.sheets.push_back(std::move(auxSh));
    out.valid = true;
    return true;
}

// ============================================================================
// #83. RocksDB / LevelDB Sorted String Table (.sst / .ldb)
// ============================================================================
inline bool ParseRocksLevelDbSst200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    if (raw.size() < 48) return false;
    uint64_t footerMagic = DetailDocs200::RdU64LE(raw.data() + raw.size() - 8);
    constexpr uint64_t kLevelDbMagic      = 0xDB4775248B80FB57ULL;
    constexpr uint64_t kRocksLegacyMagic  = 0x88E241B785F4CFF7ULL;
    constexpr uint64_t kRocksPlainMagic   = 0x8242229663BF9564ULL;
    bool hasMagic = (footerMagic == kLevelDbMagic || footerMagic == kRocksLegacyMagic || footerMagic == kRocksPlainMagic);
    if (!hasMagic && ext != L".sst" && ext != L".ldb") return false;
    if (!hasMagic && raw.size() < 56) return false;

    std::wstring fmt = (footerMagic == kLevelDbMagic) ? L"LevelDB Table (0xDB4775248B80FB57)" :
                       (footerMagic == kRocksLegacyMagic) ? L"RocksDB Block-Based SSTable (0x88E241B785F4CFF7)" :
                       (footerMagic == kRocksPlainMagic) ? L"RocksDB PlainTable SST (0x8242229663BF9564)" :
                       L"LSM-Tree SSTable (" + ext + L")";

    // Decode Footer BlockHandles (metaindex + index) at raw.size() - 48 (or raw.size() - 53 for RocksDB v1+)
    size_t footerStart = (footerMagic == kRocksLegacyMagic && raw.size() >= 53 && raw[raw.size() - 53] <= 5)
                         ? (raw.size() - 52) : (raw.size() - 48);
    size_t fpos = footerStart;
    uint64_t metaOff = 0, metaSz = 0, idxOff = 0, idxSz = 0;
    DetailDocs200::ReadVarint64(raw.data(), raw.size() - 8, fpos, metaOff);
    DetailDocs200::ReadVarint64(raw.data(), raw.size() - 8, fpos, metaSz);
    DetailDocs200::ReadVarint64(raw.data(), raw.size() - 8, fpos, idxOff);
    DetailDocs200::ReadVarint64(raw.data(), raw.size() - 8, fpos, idxSz);

    // Decode prefix-compressed KV entries from first Data Block [0 .. dataLimit)
    size_t firstBlockEnd = (metaOff > 0 && metaOff <= raw.size()) ? static_cast<size_t>(metaOff) : (raw.size() - 48);
    // Block trailer is 5 bytes (1B compressionType + 4B crc32c), preceded by numRestarts*4 + 4B numRestarts
    size_t kvEnd = firstBlockEnd;
    if (firstBlockEnd >= 9) {
        size_t rawBlkLen = firstBlockEnd - 5;
        uint32_t numRestarts = DetailDocs200::RdU32LE(raw.data() + rawBlkLen - 4);
        if (numRestarts > 0 && numRestarts < 4096 && 4ull * (numRestarts + 1) < rawBlkLen) {
            kvEnd = rawBlkLen - 4ull * (numRestarts + 1);
        }
    }

    SpreadsheetSheet kvSh;
    kvSh.name = L"SST_KeyValues";
    kvSh.maxCols = 4;
    kvSh.rows.push_back({ L"User Key", L"Op / Type", L"Sequence #", L"Value Preview" });

    size_t pos = 0;
    std::string lastKey;
    while (pos < kvEnd && kvSh.rows.size() < 400) {
        uint64_t shared = 0, nonShared = 0, valLen = 0;
        if (!DetailDocs200::ReadVarint64(raw.data(), kvEnd, pos, shared) ||
            !DetailDocs200::ReadVarint64(raw.data(), kvEnd, pos, nonShared) ||
            !DetailDocs200::ReadVarint64(raw.data(), kvEnd, pos, valLen)) break;
        if (shared > lastKey.size() || nonShared > kvEnd - pos || valLen > kvEnd - pos - nonShared) break;

        lastKey.resize(static_cast<size_t>(shared));
        lastKey.append(reinterpret_cast<const char*>(raw.data() + pos), static_cast<size_t>(nonShared));
        pos += static_cast<size_t>(nonShared);

        const uint8_t* valPtr = raw.data() + pos;
        pos += static_cast<size_t>(valLen);

        // InternalKey ends with 8-byte (seq << 8 | valueType) if >= 8 bytes and valueType <= 17
        std::wstring userKeyW, opStr = L"Put", seqStr = L"-";
        if (lastKey.size() > 8) {
            uint64_t tag = DetailDocs200::RdU64LE(reinterpret_cast<const uint8_t*>(lastKey.data() + lastKey.size() - 8));
            uint8_t vtype = static_cast<uint8_t>(tag & 0xFFu);
            uint64_t seq = tag >> 8;
            if (vtype <= 7 && DetailDocs200::IsMostlyPrintableUtf8(reinterpret_cast<const uint8_t*>(lastKey.data()), lastKey.size() - 8)) {
                userKeyW = PathUtils::Utf8ToWide(std::string_view(lastKey.data(), lastKey.size() - 8));
                opStr = (vtype == 0) ? L"Deletion (Tombstone)" : (vtype == 1) ? L"Value (Put)" : L"Merge";
                seqStr = std::to_wstring(seq);
            }
        }
        if (userKeyW.empty()) {
            if (DetailDocs200::IsMostlyPrintableUtf8(reinterpret_cast<const uint8_t*>(lastKey.data()), lastKey.size())) {
                userKeyW = PathUtils::Utf8ToWide(lastKey);
            } else {
                userKeyW = DetailDocs200::HexBytes(reinterpret_cast<const uint8_t*>(lastKey.data()), lastKey.size(), 16);
            }
        }

        size_t vShow = std::min<size_t>(static_cast<size_t>(valLen), 128);
        std::wstring valW = DetailDocs200::IsMostlyPrintableUtf8(valPtr, vShow)
                            ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(valPtr), vShow))
                            : DetailDocs200::HexBytes(valPtr, static_cast<size_t>(valLen), 16);
        kvSh.rows.push_back({ userKeyW, opStr, seqStr, valW });
    }

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"SST_Footer";
    hdrSh.maxCols = 2;
    hdrSh.rows.push_back({ L"SSTable Field", L"Value" });
    hdrSh.rows.push_back({ L"Table Format", fmt });
    hdrSh.rows.push_back({ L"File Size", PathUtils::FormatByteSize(raw.size()) });
    hdrSh.rows.push_back({ L"Metaindex Block Handle", L"offset=" + std::to_wstring(metaOff) + L", size=" + std::to_wstring(metaSz) });
    hdrSh.rows.push_back({ L"Index Block Handle", L"offset=" + std::to_wstring(idxOff) + L", size=" + std::to_wstring(idxSz) });

    if (kvSh.rows.size() == 1) {
        kvSh.rows.push_back({ L"(Compressed Snappy/Zstd Data Block)", L"Block", L"0", fmt });
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = fmt;
    out.sheets.push_back(std::move(kvSh));
    out.sheets.push_back(std::move(hdrSh));
    out.valid = true;
    return true;
}

// ============================================================================
// #84. PMTiles v3 & MBTiles Vector/Raster Map Archives (.pmtiles / .mbtiles)
// ============================================================================
inline bool ParseMapTiles200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // PMTiles v3 ("PMTiles\x03", 127-byte header)
    if ((raw.size() >= 127 && std::memcmp(raw.data(), "PMTiles\x03", 8) == 0) || ext == L".pmtiles") {
        if (raw.size() < 127 || std::memcmp(raw.data(), "PMTiles", 7) != 0) return false;
        const uint8_t* h = raw.data();
        uint64_t addressedTiles = DetailDocs200::RdU64LE(h + 72);
        uint64_t tileEntries    = DetailDocs200::RdU64LE(h + 80);
        uint64_t tileContents   = DetailDocs200::RdU64LE(h + 88);
        uint8_t tileComp        = h[98];
        uint8_t tileType        = h[99];
        uint8_t minZoom         = h[100];
        uint8_t maxZoom         = h[101];
        double minLon = DetailDocs200::RdI32LE(h + 102) * 1e-7;
        double minLat = DetailDocs200::RdI32LE(h + 106) * 1e-7;
        double maxLon = DetailDocs200::RdI32LE(h + 110) * 1e-7;
        double maxLat = DetailDocs200::RdI32LE(h + 114) * 1e-7;
        uint8_t centerZoom = h[118];
        double centerLon = DetailDocs200::RdI32LE(h + 119) * 1e-7;
        double centerLat = DetailDocs200::RdI32LE(h + 123) * 1e-7;

        const wchar_t* tTypeName = (tileType == 1) ? L"MVT (Mapbox Vector Tile)" :
                                   (tileType == 2) ? L"PNG Raster" :
                                   (tileType == 3) ? L"JPEG Raster" :
                                   (tileType == 4) ? L"WebP Raster" :
                                   (tileType == 5) ? L"AVIF Raster" : L"Unknown";
        const wchar_t* compName  = (tileComp == 1) ? L"None" : (tileComp == 2) ? L"Gzip" :
                                   (tileComp == 3) ? L"Brotli" : (tileComp == 4) ? L"Zstandard" : L"Unknown";

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Protomaps PMTiles v3 Cloud-Native Tile Archive (.pmtiles)";

        SpreadsheetSheet sh;
        sh.name = L"PMTiles_Header";
        sh.maxCols = 4;
        // Latitude + Longitude columns enable interactive Direct2D Map View via ExtractGeoTrackFromSheets!
        sh.rows.push_back({ L"Property", L"Value", L"Latitude", L"Longitude" });
        sh.rows.push_back({ L"SW Corner", L"Bounding Box SW", std::to_wstring(minLat), std::to_wstring(minLon) });
        sh.rows.push_back({ L"NW Corner", L"Bounding Box NW", std::to_wstring(maxLat), std::to_wstring(minLon) });
        sh.rows.push_back({ L"NE Corner", L"Bounding Box NE", std::to_wstring(maxLat), std::to_wstring(maxLon) });
        sh.rows.push_back({ L"SE Corner", L"Bounding Box SE", std::to_wstring(minLat), std::to_wstring(maxLon) });
        sh.rows.push_back({ L"SW Close",  L"Bounding Box Close", std::to_wstring(minLat), std::to_wstring(minLon) });
        sh.rows.push_back({ L"Map Center (z" + std::to_wstring(centerZoom) + L")", tTypeName, std::to_wstring(centerLat), std::to_wstring(centerLon) });
        sh.rows.push_back({ L"Zoom Range", L"z" + std::to_wstring(minZoom) + L" .. z" + std::to_wstring(maxZoom), L"", L"" });
        sh.rows.push_back({ L"Tile Payload Type", tTypeName, L"", L"" });
        sh.rows.push_back({ L"Tile Compression", compName, L"", L"" });
        sh.rows.push_back({ L"Addressed Tiles", std::to_wstring(addressedTiles), L"", L"" });
        sh.rows.push_back({ L"Unique Tile Blobs", std::to_wstring(tileContents) + L" (" + std::to_wstring(tileEntries) + L" Hilbert runs)", L"", L"" });

        uint64_t jsonOff = DetailDocs200::RdU64LE(h + 24);
        uint64_t jsonLen = DetailDocs200::RdU64LE(h + 32);
        if (jsonLen > 0 && jsonOff + jsonLen <= raw.size()) {
            const uint8_t* jp = raw.data() + static_cast<size_t>(jsonOff);
            size_t jn = static_cast<size_t>(std::min<uint64_t>(jsonLen, 512));
            if (DetailDocs200::IsMostlyPrintableUtf8(jp, jn)) {
                sh.rows.push_back({ L"JSON Metadata", PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(jp), jn)), L"", L"" });
            }
        }
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // MBTiles (SQLite3 container with metadata table bounds/center/format)
    if (ext == L".mbtiles" && raw.size() >= 100 && std::memcmp(raw.data(), "SQLite format 3\0", 16) == 0) {
        uint16_t pageSzRaw = DetailDocs200::RdU16BE(raw.data() + 16);
        uint32_t pageSz = (pageSzRaw == 1) ? 65536u : pageSzRaw;
        uint32_t pageCnt = DetailDocs200::RdU32BE(raw.data() + 28);

        std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
        double minLon = -10.0, minLat = 35.0, maxLon = 30.0, maxLat = 60.0;
        bool foundBounds = false;
        // Scan for "bounds" followed by "minLon,minLat,maxLon,maxLat"
        size_t bp = sv.find("bounds");
        if (bp != std::string_view::npos && bp + 6 < sv.size()) {
            std::string tail(sv.substr(bp + 6, std::min<size_t>(80, sv.size() - bp - 6)));
            for (char& c : tail) if ((c < '0' || c > '9') && c != '-' && c != '.' && c != ',') c = ' ';
            double a = 0, b = 0, c = 0, d = 0;
            if (sscanf_s(tail.c_str(), "%lf,%lf,%lf,%lf", &a, &b, &c, &d) == 4 &&
                std::fabs(a) <= 180.0 && std::fabs(b) <= 90.0 && std::fabs(c) <= 180.0 && std::fabs(d) <= 90.0) {
                minLon = a; minLat = b; maxLon = c; maxLat = d;
                foundBounds = true;
            }
        }

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Mapbox MBTiles SQLite Map Archive (.mbtiles)";

        SpreadsheetSheet sh;
        sh.name = L"MBTiles_Metadata";
        sh.maxCols = 4;
        sh.rows.push_back({ L"Property", L"Value", L"Latitude", L"Longitude" });
        if (foundBounds) {
            sh.rows.push_back({ L"SW Bound", L"MBTiles Bounds", std::to_wstring(minLat), std::to_wstring(minLon) });
            sh.rows.push_back({ L"NW Bound", L"MBTiles Bounds", std::to_wstring(maxLat), std::to_wstring(minLon) });
            sh.rows.push_back({ L"NE Bound", L"MBTiles Bounds", std::to_wstring(maxLat), std::to_wstring(maxLon) });
            sh.rows.push_back({ L"SE Bound", L"MBTiles Bounds", std::to_wstring(minLat), std::to_wstring(maxLon) });
            sh.rows.push_back({ L"Center",   L"Derived Center", std::to_wstring((minLat + maxLat) * 0.5), std::to_wstring((minLon + maxLon) * 0.5) });
        }
        sh.rows.push_back({ L"SQLite Page Size", std::to_wstring(pageSz) + L" B", L"", L"" });
        sh.rows.push_back({ L"Database Pages", std::to_wstring(pageCnt), L"", L"" });
        sh.rows.push_back({ L"Archive Size", PathUtils::FormatByteSize(raw.size()), L"", L"" });
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

// ============================================================================
// #85. Schema-less Protobuf Wire-Format Inspector (.pb / .pbf)
// ============================================================================
namespace DetailDocs200 {

inline bool WalkProtobuf(const uint8_t* data, size_t size, const std::wstring& prefix, int depth,
                         SpreadsheetSheet* outSheet, size_t& ioRows) {
    if (size == 0 || depth > 4) return false;
    size_t pos = 0;
    size_t fields = 0;
    while (pos < size) {
        uint64_t tag = 0;
        if (!ReadVarint64(data, size, pos, tag) || tag == 0) return false;
        uint64_t fieldNum = tag >> 3;
        uint8_t wireType = static_cast<uint8_t>(tag & 0x07u);
        if (fieldNum == 0 || fieldNum > 536870911ULL) return false;

        std::wstring fpath = prefix.empty() ? (L"#" + std::to_wstring(fieldNum))
                                            : (prefix + L"." + std::to_wstring(fieldNum));
        if (wireType == 0) { // Varint
            uint64_t v = 0;
            if (!ReadVarint64(data, size, pos, v)) return false;
            int64_t zigzag = static_cast<int64_t>((v >> 1) ^ (~(v & 1ULL) + 1ULL));
            if (outSheet && ioRows < 400) {
                outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"0 (Varint)",
                    std::to_wstring(v) + L" (sint: " + std::to_wstring(zigzag) + L")" });
                ++ioRows;
            }
        } else if (wireType == 1) { // 64-bit
            if (pos + 8 > size) return false;
            uint64_t u = RdU64LE(data + pos);
            double d = 0.0; std::memcpy(&d, data + pos, 8);
            pos += 8;
            if (outSheet && ioRows < 400) {
                wchar_t buf[96] = {};
                swprintf_s(buf, L"0x%016llX (double: %.6g)", static_cast<unsigned long long>(u), d);
                outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"1 (Fixed64)", buf });
                ++ioRows;
            }
        } else if (wireType == 2) { // Length-delimited
            uint64_t len = 0;
            if (!ReadVarint64(data, size, pos, len) || len > size - pos) return false;
            const uint8_t* sub = data + pos;
            size_t subLen = static_cast<size_t>(len);
            pos += subLen;

            if (IsMostlyPrintableUtf8(sub, subLen)) {
                if (outSheet && ioRows < 400) {
                    size_t show = std::min<size_t>(subLen, 160);
                    outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"2 (String)",
                        L"\"" + PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(sub), show)) + L"\"" });
                    ++ioRows;
                }
            } else if (subLen >= 2 && depth < 4 && WalkProtobuf(sub, subLen, fpath, depth + 1, nullptr, ioRows)) {
                if (outSheet && ioRows < 400) {
                    outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"2 (Submessage)",
                        L"{" + std::to_wstring(subLen) + L" bytes}" });
                    ++ioRows;
                }
                WalkProtobuf(sub, subLen, fpath, depth + 1, outSheet, ioRows);
            } else if (outSheet && ioRows < 400) {
                outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"2 (Bytes)",
                    L"[" + std::to_wstring(subLen) + L" B] " + HexBytes(sub, subLen, 16) });
                ++ioRows;
            }
        } else if (wireType == 5) { // 32-bit
            if (pos + 4 > size) return false;
            uint32_t u = RdU32LE(data + pos);
            float f = 0.0f; std::memcpy(&f, data + pos, 4);
            pos += 4;
            if (outSheet && ioRows < 400) {
                wchar_t buf[64] = {};
                swprintf_s(buf, L"0x%08X (float: %.5g)", u, f);
                outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"5 (Fixed32)", buf });
                ++ioRows;
            }
        } else {
            return false; // Deprecated group or invalid wire type
        }
        ++fields;
    }
    return fields > 0;
}

} // namespace DetailDocs200

inline bool ParseProtobufWire200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    if ((ext != L".pb" && ext != L".pbf") || raw.size() < 2) return false;

    SpreadsheetSheet sh;
    sh.name = L"Protobuf_Wire";
    sh.maxCols = 4;
    sh.rows.push_back({ L"Field Path", L"Tag #", L"Wire Type", L"Decoded Value" });

    size_t rows = 0;
    // OSM .pbf starts with 4-byte BE BlobHeader length followed by Protobuf BlobHeader
    if (ext == L".pbf" && raw.size() > 16) {
        uint32_t hdrLen = DetailDocs200::RdU32BE(raw.data());
        if (hdrLen > 4 && hdrLen < 65536 && 4 + hdrLen <= raw.size()) {
            if (DetailDocs200::WalkProtobuf(raw.data() + 4, hdrLen, L"BlobHeader", 0, &sh, rows)) {
                out.kind = OfficeFileKind::SqliteDb;
                out.isSpreadsheet = true;
                out.formatTitle = L"OpenStreetMap / Binary PBF Protobuf Stream (.pbf)";
                out.sheets.push_back(std::move(sh));
                out.valid = true;
                return true;
            }
        }
    }

    if (!DetailDocs200::WalkProtobuf(raw.data(), raw.size(), L"", 0, &sh, rows)) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Protocol Buffers Wire-Format Message (" + ext + L")";
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

// ============================================================================
// #86–#88. Cloud & SecOps JSON/Tag Formats (.tfstate, .sarif, .spdx, .cdx)
// ============================================================================
inline bool ParseCloudSecOps200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    // #86. Terraform Infrastructure State (.tfstate)
    if (ext == L".tfstate") {
        if (sv.find("\"resources\"") == std::string_view::npos && sv.find("\"terraform_version\"") == std::string_view::npos) return false;
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        std::string tfVer = DetailDocs200::JsonGetStr(sv.substr(0, std::min<size_t>(sv.size(), 1024)), "terraform_version");
        std::string serial = DetailDocs200::JsonGetStr(sv.substr(0, std::min<size_t>(sv.size(), 1024)), "serial");
        std::string lineage = DetailDocs200::JsonGetStr(sv.substr(0, std::min<size_t>(sv.size(), 1024)), "lineage");
        out.formatTitle = L"Terraform Infrastructure State (.tfstate" + (tfVer.empty() ? L"" : L" — v" + PathUtils::Utf8ToWide(tfVer)) + L")";

        SpreadsheetSheet resSh;
        resSh.name = L"Resources";
        resSh.maxCols = 5;
        resSh.rows.push_back({ L"Mode", L"Resource Type", L"Name", L"Provider", L"Primary ID" });

        size_t rp = sv.find("\"resources\"");
        if (rp != std::string_view::npos) {
            size_t pos = rp;
            while ((pos = sv.find("\"mode\"", pos)) != std::string_view::npos && resSh.rows.size() < 400) {
                size_t blkStart = (pos > 32) ? (pos - 32) : 0;
                size_t nextMode = sv.find("\"mode\"", pos + 6);
                size_t blkLen = std::min<size_t>((nextMode == std::string_view::npos ? sv.size() : nextMode) - blkStart, 4096);
                std::string_view blk = sv.substr(blkStart, blkLen);
                std::string mode = DetailDocs200::JsonGetStr(blk, "mode");
                std::string rtype = DetailDocs200::JsonGetStr(blk, "type");
                std::string rname = DetailDocs200::JsonGetStr(blk, "name");
                std::string prov  = DetailDocs200::JsonGetStr(blk, "provider");
                std::string id    = DetailDocs200::JsonGetStr(blk, "id");
                resSh.rows.push_back({ PathUtils::Utf8ToWide(mode), PathUtils::Utf8ToWide(rtype),
                                       PathUtils::Utf8ToWide(rname), PathUtils::Utf8ToWide(prov), PathUtils::Utf8ToWide(id) });
                pos += 6;
            }
        }
        if (resSh.rows.size() == 1) {
            resSh.rows.push_back({ L"(empty)", L"-", L"serial=" + PathUtils::Utf8ToWide(serial), L"lineage=" + PathUtils::Utf8ToWide(lineage), L"-" });
        }
        out.sheets.push_back(std::move(resSh));
        out.valid = true;
        return true;
    }

    // #87. OASIS SARIF Static Analysis Report (.sarif)
    if (ext == L".sarif") {
        if (sv.find("\"runs\"") == std::string_view::npos) return false;
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        std::string toolName = DetailDocs200::JsonGetStr(sv.substr(0, std::min<size_t>(sv.size(), 4096)), "name");
        out.formatTitle = L"OASIS SARIF Static Analysis Report (" + (toolName.empty() ? L".sarif" : PathUtils::Utf8ToWide(toolName)) + L")";

        SpreadsheetSheet sh;
        sh.name = L"SARIF_Findings";
        sh.maxCols = 5;
        sh.rows.push_back({ L"Severity", L"Rule ID", L"Artifact URI", L"Line", L"Diagnostic Message" });

        size_t pos = sv.find("\"results\"");
        if (pos != std::string_view::npos) {
            while ((pos = sv.find("\"ruleId\"", pos)) != std::string_view::npos && sh.rows.size() < 500) {
                size_t blkStart = (pos > 64) ? (pos - 64) : 0;
                size_t nextR = sv.find("\"ruleId\"", pos + 8);
                size_t blkLen = std::min<size_t>((nextR == std::string_view::npos ? sv.size() : nextR) - blkStart, 3072);
                std::string_view blk = sv.substr(blkStart, blkLen);
                std::string rule  = DetailDocs200::JsonGetStr(blk, "ruleId");
                std::string level = DetailDocs200::JsonGetStr(blk, "level");
                if (level.empty()) level = "warning";
                std::string uri   = DetailDocs200::JsonGetStr(blk, "uri");
                std::string line  = DetailDocs200::JsonGetStr(blk, "startLine");
                std::string msg   = DetailDocs200::JsonGetStr(blk, "text");
                sh.rows.push_back({ PathUtils::Utf8ToWide(level), PathUtils::Utf8ToWide(rule),
                                    PathUtils::Utf8ToWide(uri), PathUtils::Utf8ToWide(line), PathUtils::Utf8ToWide(msg) });
                pos += 8;
            }
        }
        if (sh.rows.size() == 1) {
            sh.rows.push_back({ L"clean", L"-", L"-", L"0", L"0 findings reported by " + PathUtils::Utf8ToWide(toolName) });
        }
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // #88. SPDX & CycloneDX Software Bill of Materials (.spdx / .cdx)
    if (ext == L".spdx" || ext == L".cdx") {
        bool isCdx  = (sv.find("CycloneDX") != std::string_view::npos || sv.find("\"components\"") != std::string_view::npos);
        bool isJson = (sv.find('{') != std::string_view::npos && (isCdx || sv.find("spdxVersion") != std::string_view::npos));
        bool isTag  = (sv.find("SPDXVersion:") != std::string_view::npos || sv.find("PackageName:") != std::string_view::npos);
        if (!isJson && !isTag) return false;

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = isCdx ? L"CycloneDX Software Bill of Materials (.cdx)" : L"Linux Foundation SPDX SBOM (.spdx)";

        SpreadsheetSheet sh;
        sh.name = L"SBOM_Inventory";
        sh.maxCols = 4;
        sh.rows.push_back({ L"Component / Package", L"Version", L"License", L"PURL / Source" });

        if (isTag) {
            std::string curName, curVer, curLic, curLoc;
            auto flushPkg = [&]() {
                if (!curName.empty()) {
                    sh.rows.push_back({ PathUtils::Utf8ToWide(curName), PathUtils::Utf8ToWide(curVer),
                                        PathUtils::Utf8ToWide(curLic), PathUtils::Utf8ToWide(curLoc) });
                    curName.clear(); curVer.clear(); curLic.clear(); curLoc.clear();
                }
            };
            size_t ls = 0;
            while (ls < sv.size() && sh.rows.size() < 500) {
                size_t le = sv.find('\n', ls);
                if (le == std::string_view::npos) le = sv.size();
                std::string_view line = sv.substr(ls, le - ls);
                ls = le + 1;
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                if (line.rfind("PackageName:", 0) == 0) { flushPkg(); curName = std::string(line.substr(12)); }
                else if (line.rfind("PackageVersion:", 0) == 0) curVer = std::string(line.substr(15));
                else if (line.rfind("PackageLicenseConcluded:", 0) == 0) curLic = std::string(line.substr(24));
                else if (line.rfind("PackageDownloadLocation:", 0) == 0) curLoc = std::string(line.substr(24));
            }
            flushPkg();
        } else {
            size_t pos = isCdx ? sv.find("\"components\"") : sv.find("\"packages\"");
            if (pos == std::string_view::npos) pos = 0;
            while ((pos = sv.find("\"name\"", pos)) != std::string_view::npos && sh.rows.size() < 500) {
                size_t nextN = sv.find("\"name\"", pos + 6);
                size_t blkLen = std::min<size_t>((nextN == std::string_view::npos ? sv.size() : nextN) - pos, 2048);
                std::string_view blk = sv.substr(pos, blkLen);
                std::string name = DetailDocs200::JsonGetStr(blk, "name");
                std::string ver  = DetailDocs200::JsonGetStr(blk, "version");
                if (ver.empty()) ver = DetailDocs200::JsonGetStr(blk, "versionInfo");
                std::string lic  = DetailDocs200::JsonGetStr(blk, "licenseConcluded");
                if (lic.empty()) lic = DetailDocs200::JsonGetStr(blk, "id");
                std::string purl = DetailDocs200::JsonGetStr(blk, "purl");
                if (purl.empty()) purl = DetailDocs200::JsonGetStr(blk, "downloadLocation");
                sh.rows.push_back({ PathUtils::Utf8ToWide(name), PathUtils::Utf8ToWide(ver),
                                    PathUtils::Utf8ToWide(lic), PathUtils::Utf8ToWide(purl) });
                pos += 6;
            }
        }
        if (sh.rows.size() == 1) sh.rows.push_back({ L"(SBOM Header Only)", L"1.0", L"NOASSERTION", L"-" });
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

// ============================================================================
// #89–#91. Crypto & Security Containers (.gpg/.pgp/.sig, .jks, .cat)
// ============================================================================
inline bool ParseCryptoSec200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #89. OpenPGP RFC 4880 Binary & ASCII-Armored Packet Inspector (.gpg / .pgp / .sig)
    if (ext == L".gpg" || ext == L".pgp" || ext == L".sig") {
        std::vector<uint8_t> decoded;
        const uint8_t* pktData = raw.data();
        size_t pktSize = raw.size();
        bool armored = false;

        std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (sv.find("-----BEGIN PGP ") != std::string_view::npos) {
            armored = true;
            size_t hdrEnd = sv.find("\r\n\r\n");
            size_t sep = 4;
            if (hdrEnd == std::string_view::npos) { hdrEnd = sv.find("\n\n"); sep = 2; }
            if (hdrEnd != std::string_view::npos) {
                size_t ftr = sv.find("-----END PGP ", hdrEnd + sep);
                std::string_view b64 = sv.substr(hdrEnd + sep, (ftr == std::string_view::npos ? sv.size() : ftr) - (hdrEnd + sep));
                // Strip CRC24 line starting with '='
                size_t crcEq = b64.rfind("\n=");
                if (crcEq != std::string_view::npos) b64 = b64.substr(0, crcEq);
                decoded = DetailDocs200::DecodeBase64Block(b64);
                pktData = decoded.data();
                pktSize = decoded.size();
            }
        }
        if (pktSize < 2 || (pktData[0] & 0x80u) == 0) return false;

        auto pgpTagName = [](uint8_t tag) -> const wchar_t* {
            switch (tag) {
            case 1:  return L"Public-Key Encrypted Session Key (PKESK)";
            case 2:  return L"Signature Packet";
            case 3:  return L"Symmetric-Key Encrypted Session Key (SKESK)";
            case 4:  return L"One-Pass Signature";
            case 5:  return L"Secret-Key Packet";
            case 6:  return L"Public-Key Packet";
            case 7:  return L"Secret-Subkey Packet";
            case 8:  return L"Compressed Data";
            case 9:  return L"Symmetrically Encrypted Data";
            case 11: return L"Literal Data";
            case 13: return L"User ID";
            case 14: return L"Public-Subkey Packet";
            case 18: return L"Sym. Encrypted Integrity Protected Data (SEIPD)";
            default: return L"OpenPGP Packet";
            }
        };
        auto pgpAlgName = [](uint8_t alg) -> std::wstring {
            switch (alg) {
            case 1: case 2: case 3: return L"RSA";
            case 16: return L"Elgamal";
            case 17: return L"DSA";
            case 18: return L"ECDH (Curve25519/NIST)";
            case 19: return L"ECDSA";
            case 22: return L"EdDSA (Ed25519)";
            default: return L"Alg#" + std::to_wstring(alg);
            }
        };

        SpreadsheetSheet sh;
        sh.name = L"OpenPGP_Packets";
        sh.maxCols = 4;
        sh.rows.push_back({ L"Packet #", L"RFC 4880 Tag", L"Body Length", L"Algorithm / Identity Details" });

        size_t pos = 0;
        int idx = 0;
        while (pos < pktSize && idx < 128) {
            uint8_t ctb = pktData[pos++];
            if ((ctb & 0x80u) == 0) break;
            uint8_t tag = 0;
            size_t bodyLen = 0;
            if ((ctb & 0x40u) != 0) { // New packet format
                tag = ctb & 0x3Fu;
                if (pos >= pktSize) break;
                uint8_t l0 = pktData[pos++];
                if (l0 < 192) bodyLen = l0;
                else if (l0 < 224) {
                    if (pos >= pktSize) break;
                    bodyLen = ((static_cast<size_t>(l0 - 192) << 8) + pktData[pos++]) + 192;
                } else if (l0 == 255) {
                    if (pos + 4 > pktSize) break;
                    bodyLen = DetailDocs200::RdU32BE(pktData + pos);
                    pos += 4;
                } else {
                    bodyLen = 1ull << (l0 & 0x1Fu); // Partial body length
                }
            } else { // Old packet format
                tag = (ctb >> 2) & 0x0Fu;
                uint8_t ltype = ctb & 0x03u;
                if (ltype == 0) { if (pos >= pktSize) break; bodyLen = pktData[pos++]; }
                else if (ltype == 1) { if (pos + 2 > pktSize) break; bodyLen = DetailDocs200::RdU16BE(pktData + pos); pos += 2; }
                else if (ltype == 2) { if (pos + 4 > pktSize) break; bodyLen = DetailDocs200::RdU32BE(pktData + pos); pos += 4; }
                else bodyLen = pktSize - pos;
            }
            if (bodyLen > pktSize - pos) bodyLen = pktSize - pos;
            const uint8_t* body = pktData + pos;

            std::wstring detail = armored ? L"ASCII-Armored" : L"Binary";
            if (tag == 13 && bodyLen > 0) {
                detail = L"UID: " + PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(body), bodyLen));
            } else if ((tag == 6 || tag == 14 || tag == 5 || tag == 7) && bodyLen >= 6) {
                uint8_t ver = body[0];
                uint32_t ts = DetailDocs200::RdU32BE(body + 1);
                uint8_t alg = (ver == 4) ? body[5] : (bodyLen >= 8 ? body[7] : 0);
                detail = L"v" + std::to_wstring(ver) + L" " + pgpAlgName(alg) + L" (Created Unix=" + std::to_wstring(ts) + L")";
            } else if (tag == 2 && bodyLen >= 4) {
                uint8_t ver = body[0];
                uint8_t pkAlg = (ver == 4) ? body[2] : (bodyLen >= 17 ? body[15] : 0);
                detail = L"v" + std::to_wstring(ver) + L" Sig via " + pgpAlgName(pkAlg);
            } else if (tag == 1 && bodyLen >= 10) {
                detail = L"Recipient KeyID: " + DetailDocs200::HexBytes(body + 1, 8, 8) + L" (" + pgpAlgName(body[9]) + L")";
            }

            sh.rows.push_back({ std::to_wstring(++idx), pgpTagName(tag), std::to_wstring(bodyLen) + L" B", detail });
            pos += bodyLen;
        }
        if (idx == 0) return false;

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = std::wstring(L"OpenPGP Cryptographic Packet Stream (") + (armored ? L"ASCII-Armored " : L"Binary ") + ext + L")";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // #90. Java KeyStore Binary Vault (.jks)
    if (ext == L".jks" || (raw.size() >= 12 && (DetailDocs200::RdU32BE(raw.data()) == 0xFEEDFEEDu || DetailDocs200::RdU32BE(raw.data()) == 0xCECECECEu))) {
        if (raw.size() < 12) return false;
        uint32_t magic = DetailDocs200::RdU32BE(raw.data());
        if (magic != 0xFEEDFEEDu && magic != 0xCECECECEu) return false;
        uint32_t ver   = DetailDocs200::RdU32BE(raw.data() + 4);
        uint32_t count = DetailDocs200::RdU32BE(raw.data() + 8);

        SpreadsheetSheet sh;
        sh.name = L"KeyStore_Entries";
        sh.maxCols = 4;
        sh.rows.push_back({ L"Alias", L"Entry Type", L"Created (Epoch ms)", L"Certificate / Key Info" });

        size_t pos = 12;
        for (uint32_t i = 0; i < count && i < 256 && pos + 6 <= raw.size(); ++i) {
            uint32_t tag = DetailDocs200::RdU32BE(raw.data() + pos); pos += 4;
            if (pos + 2 > raw.size()) break;
            uint16_t aLen = DetailDocs200::RdU16BE(raw.data() + pos); pos += 2;
            if (pos + aLen + 8 > raw.size()) break;
            std::wstring alias = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + pos), aLen));
            pos += aLen;
            uint64_t ts = DetailDocs200::RdU64BE(raw.data() + pos); pos += 8;

            if (tag == 1) { // PrivateKey
                if (pos + 4 > raw.size()) break;
                uint32_t kLen = DetailDocs200::RdU32BE(raw.data() + pos); pos += 4;
                if (kLen > raw.size() - pos) break;
                pos += kLen;
                uint32_t chainCnt = (pos + 4 <= raw.size()) ? DetailDocs200::RdU32BE(raw.data() + pos) : 0;
                if (pos + 4 <= raw.size()) pos += 4;
                for (uint32_t c = 0; c < chainCnt && pos + 2 <= raw.size(); ++c) {
                    uint16_t ctLen = DetailDocs200::RdU16BE(raw.data() + pos); pos += 2;
                    if (pos + ctLen + 4 > raw.size()) { pos = raw.size(); break; }
                    pos += ctLen;
                    uint32_t cbLen = DetailDocs200::RdU32BE(raw.data() + pos); pos += 4;
                    if (cbLen > raw.size() - pos) { pos = raw.size(); break; }
                    pos += cbLen;
                }
                sh.rows.push_back({ alias, L"PrivateKeyEntry", std::to_wstring(ts),
                    L"PKCS#8 Encrypted Key (" + std::to_wstring(kLen) + L" B), Chain=" + std::to_wstring(chainCnt) });
            } else if (tag == 2) { // TrustedCert
                std::wstring ctype = L"X.509";
                if (ver == 2) {
                    if (pos + 2 > raw.size()) break;
                    uint16_t ctLen = DetailDocs200::RdU16BE(raw.data() + pos); pos += 2;
                    if (pos + ctLen > raw.size()) break;
                    ctype = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + pos), ctLen));
                    pos += ctLen;
                }
                if (pos + 4 > raw.size()) break;
                uint32_t cbLen = DetailDocs200::RdU32BE(raw.data() + pos); pos += 4;
                if (cbLen > raw.size() - pos) break;
                pos += cbLen;
                sh.rows.push_back({ alias, L"TrustedCertificateEntry", std::to_wstring(ts),
                    ctype + L" DER Certificate (" + std::to_wstring(cbLen) + L" B)" });
            } else {
                break;
            }
        }
        if (sh.rows.size() == 1) {
            sh.rows.push_back({ L"(Empty KeyStore)", (magic == 0xCECECECEu) ? L"JCEKS" : L"JKS", L"v" + std::to_wstring(ver), L"0 entries" });
        }
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = (magic == 0xCECECECEu) ? L"Java JCEKS Cryptographic KeyStore (.jks)" : L"Java KeyStore Binary Vault (.jks)";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // #91. Windows Authenticode Security Catalog (.cat — ASN.1 DER PKCS#7 SignedData)
    if (ext == L".cat" && raw.size() >= 16 && raw[0] == 0x30) {
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Windows Authenticode PKCS#7 Security Catalog (.cat)";

        SpreadsheetSheet sh;
        sh.name = L"Catalog_Contents";
        sh.maxCols = 3;
        sh.rows.push_back({ L"ASN.1 / Authenticode Element", L"Encoding", L"Value / Digest" });

        // Check SPC_CATALOG_LIST OID 1.3.6.1.4.1.311.12.1.1 (2B 06 01 04 01 82 37 0C 01 01)
        static const uint8_t kCatOid[10] = { 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x0C, 0x01, 0x01 };
        bool hasSpcCat = false;
        for (size_t i = 0; i + 10 <= raw.size(); ++i) {
            if (std::memcmp(raw.data() + i, kCatOid, 10) == 0) { hasSpcCat = true; break; }
        }
        sh.rows.push_back({ L"PKCS#7 ContentType", L"OID", hasSpcCat ? L"1.3.6.1.4.1.311.12.1.1 (SPC_CATALOG_LIST)" : L"1.2.840.113549.1.7.2 (SignedData)" });

        // Extract DER PrintableString (0x13), UTF8String (0x0C), BMPString UTF-16BE (0x1E), and SHA-1/SHA-256 OCTET STRINGs (0x04 0x14 / 0x04 0x20)
        for (size_t i = 2; i + 4 < raw.size() && sh.rows.size() < 250; ++i) {
            uint8_t tag = raw[i];
            uint8_t len = raw[i + 1];
            if ((tag == 0x0C || tag == 0x13 || tag == 0x16) && len >= 3 && len < 120 && i + 2 + len <= raw.size()) {
                if (DetailDocs200::IsMostlyPrintableUtf8(raw.data() + i + 2, len)) {
                    sh.rows.push_back({ L"Signer / Attribute", (tag == 0x0C) ? L"UTF8String" : L"PrintableString",
                        PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + i + 2), len)) });
                    i += 1 + len;
                }
            } else if (tag == 0x1E && len >= 6 && (len % 2) == 0 && len < 120 && i + 2 + len <= raw.size()) {
                std::wstring bmp;
                bool okBmp = true;
                for (size_t k = 0; k < len; k += 2) {
                    uint16_t ch = DetailDocs200::RdU16BE(raw.data() + i + 2 + k);
                    if (ch < 32 || ch > 0xD7FF) { okBmp = false; break; }
                    bmp.push_back(static_cast<wchar_t>(ch));
                }
                if (okBmp) {
                    sh.rows.push_back({ L"Catalog Member Tag", L"BMPString", bmp });
                    i += 1 + len;
                }
            } else if (tag == 0x04 && (len == 20 || len == 32) && i + 2 + len <= raw.size()) {
                sh.rows.push_back({ (len == 32) ? L"SHA-256 Member Thumbprint" : L"SHA-1 Member Thumbprint",
                    L"OCTET STRING", DetailDocs200::HexBytes(raw.data() + i + 2, len, 32) });
                i += 1 + len;
            }
        }
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

// ============================================================================
// #92–#94. Windows Forensics & System Policy (.hiv / NTUSER.DAT, .etl, .pol)
// ============================================================================
inline bool ParseWindowsForensics200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #92. Windows Registry Binary Hive ("regf" — .hiv or NTUSER.DAT)
    if ((raw.size() >= 48 && std::memcmp(raw.data(), "regf", 4) == 0) && (ext == L".hiv" || ext == L".dat")) {
        uint32_t seq1     = DetailDocs200::RdU32LE(raw.data() + 4);
        uint32_t seq2     = DetailDocs200::RdU32LE(raw.data() + 8);
        uint32_t majVer   = DetailDocs200::RdU32LE(raw.data() + 20);
        uint32_t minVer   = DetailDocs200::RdU32LE(raw.data() + 24);
        uint32_t rootCell = DetailDocs200::RdU32LE(raw.data() + 36);
        std::wstring hiveName = (raw.size() >= 112) ? DetailDocs200::ReadUtf16LE(raw.data() + 48, 64) : L"(Root Hive)";

        SpreadsheetSheet keysSh;
        keysSh.name = L"Hive_Keys_And_Values";
        keysSh.maxCols = 4;
        keysSh.rows.push_back({ L"Cell Kind", L"Key / Value Name", L"Subkeys / RegType", L"Decoded Data" });

        // Walk hbin blocks starting at offset 4096
        size_t pos = 4096;
        while (pos + 32 <= raw.size() && keysSh.rows.size() < 500) {
            if (std::memcmp(raw.data() + pos, "hbin", 4) == 0) {
                uint32_t hbinSize = DetailDocs200::RdU32LE(raw.data() + pos + 8);
                if (hbinSize < 32) break;
                size_t cpos = pos + 32;
                size_t hend = std::min(raw.size(), pos + hbinSize);
                while (cpos + 8 <= hend && keysSh.rows.size() < 500) {
                    int32_t rawSz = DetailDocs200::RdI32LE(raw.data() + cpos);
                    size_t absSz = static_cast<size_t>(rawSz < 0 ? -static_cast<int64_t>(rawSz) : rawSz);
                    if (absSz < 8 || cpos + absSz > hend) break;
                    if (rawSz < 0 && absSz >= 24) { // Allocated cell
                        const uint8_t* cell = raw.data() + cpos + 4;
                        size_t cLen = absSz - 4;
                        if (cLen >= 76 && cell[0] == 'n' && cell[1] == 'k') {
                            uint16_t flags  = DetailDocs200::RdU16LE(cell + 2);
                            uint32_t subCnt = DetailDocs200::RdU32LE(cell + 20);
                            uint32_t valCnt = DetailDocs200::RdU32LE(cell + 36);
                            uint16_t nLen   = DetailDocs200::RdU16LE(cell + 72);
                            if (76u + nLen <= cLen) {
                                std::wstring kn = (flags & 0x20u)
                                    ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(cell + 76), nLen))
                                    : DetailDocs200::ReadUtf16LE(cell + 76, nLen);
                                keysSh.rows.push_back({ L"Key (nk)", kn,
                                    std::to_wstring(subCnt) + L" subkeys", std::to_wstring(valCnt) + L" values" });
                            }
                        } else if (cLen >= 20 && cell[0] == 'v' && cell[1] == 'k') {
                            uint16_t nLen  = DetailDocs200::RdU16LE(cell + 2);
                            uint32_t dLenR = DetailDocs200::RdU32LE(cell + 4);
                            uint32_t dOff  = DetailDocs200::RdU32LE(cell + 8);
                            uint32_t dType = DetailDocs200::RdU32LE(cell + 12);
                            uint16_t vFlg  = DetailDocs200::RdU16LE(cell + 16);
                            std::wstring vn = L"(Default)";
                            if (nLen > 0 && 20u + nLen <= cLen) {
                                vn = (vFlg & 1u)
                                    ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(cell + 20), nLen))
                                    : DetailDocs200::ReadUtf16LE(cell + 20, nLen);
                            }
                            bool inlineData = (dLenR & 0x80000000u) != 0;
                            uint32_t dLen = dLenR & 0x7FFFFFFFu;
                            std::wstring tName = (dType == 1) ? L"REG_SZ" : (dType == 2) ? L"REG_EXPAND_SZ" :
                                                 (dType == 3) ? L"REG_BINARY" : (dType == 4) ? L"REG_DWORD" :
                                                 (dType == 7) ? L"REG_MULTI_SZ" : (dType == 11) ? L"REG_QWORD" : (L"TYPE_" + std::to_wstring(dType));
                            std::wstring dVal;
                            if (inlineData && dType == 4) {
                                wchar_t b[32] = {}; swprintf_s(b, L"0x%08X (%u)", dOff, dOff); dVal = b;
                            } else if (!inlineData && dLen > 0 && 4096ull + dOff + 4 + dLen <= raw.size()) {
                                const uint8_t* dp = raw.data() + 4096 + dOff + 4;
                                if (dType == 1 || dType == 2 || dType == 7) dVal = DetailDocs200::ReadUtf16LE(dp, std::min<uint32_t>(dLen, 256));
                                else if (dType == 4 && dLen >= 4) dVal = std::to_wstring(DetailDocs200::RdU32LE(dp));
                                else dVal = DetailDocs200::HexBytes(dp, dLen, 16);
                            }
                            keysSh.rows.push_back({ L"Value (vk)", vn, tName, dVal });
                        }
                    }
                    cpos += absSz;
                }
                pos += hbinSize;
            } else {
                pos += 4096;
            }
        }

        SpreadsheetSheet hdrSh;
        hdrSh.name = L"Hive_Header";
        hdrSh.maxCols = 2;
        hdrSh.rows.push_back({ L"regf Field", L"Value" });
        hdrSh.rows.push_back({ L"Embedded Hive Name", hiveName.empty() ? L"(Unnamed)" : hiveName });
        hdrSh.rows.push_back({ L"Hive Format Version", std::to_wstring(majVer) + L"." + std::to_wstring(minVer) });
        hdrSh.rows.push_back({ L"Sequence Numbers", std::to_wstring(seq1) + L" / " + std::to_wstring(seq2) + (seq1 == seq2 ? L" (Clean)" : L" (Dirty)") });
        hdrSh.rows.push_back({ L"Root Key Cell Offset", L"0x" + DetailSys100::Hex32(rootCell).substr(2) });
        if (keysSh.rows.size() == 1) keysSh.rows.push_back({ L"Header", hiveName, L"regf", L"Base Block Only" });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Windows Registry Binary Hive (" + ext + L")";
        out.sheets.push_back(std::move(keysSh));
        out.sheets.push_back(std::move(hdrSh));
        out.valid = true;
        return true;
    }

    // #93. Windows Event Trace Log (.etl — WMI_BUFFER_HEADER + TRACE_LOGFILE_HEADER)
    if (ext == L".etl" && raw.size() >= 72) {
        uint32_t bufSize   = DetailDocs200::RdU32LE(raw.data() + 0);
        uint32_t savedOff  = DetailDocs200::RdU32LE(raw.data() + 4);
        if (bufSize < 64 || bufSize > 64 * 1024 * 1024) return false;

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Windows Event Trace Log (ETW .etl)";

        SpreadsheetSheet sh;
        sh.name = L"ETW_Header_And_Strings";
        sh.maxCols = 2;
        sh.rows.push_back({ L"ETW Field / Provider String", L"Value" });
        sh.rows.push_back({ L"WMI Buffer Size", PathUtils::FormatByteSize(bufSize) });
        sh.rows.push_back({ L"First Buffer Saved Offset", std::to_wstring(savedOff) + L" B" });
        sh.rows.push_back({ L"Estimated ETW Buffers", std::to_wstring(std::max<size_t>(1, raw.size() / bufSize)) });

        // Extract UTF-16LE strings (Session Name, LogFileName, Kernel Image paths) inside trace buffers
        for (size_t i = 64; i + 12 < std::min<size_t>(raw.size(), 262144) && sh.rows.size() < 200; i += 2) {
            if (raw[i] >= 33 && raw[i] <= 126 && raw[i + 1] == 0 &&
                raw[i + 2] >= 32 && raw[i + 2] <= 126 && raw[i + 3] == 0 &&
                raw[i + 4] >= 32 && raw[i + 4] <= 126 && raw[i + 5] == 0 &&
                raw[i + 6] >= 32 && raw[i + 6] <= 126 && raw[i + 7] == 0)
            {
                std::wstring s = DetailDocs200::ReadUtf16LE(raw.data() + i, std::min<size_t>(256, raw.size() - i));
                if (s.size() >= 4) {
                    sh.rows.push_back({ L"Trace String @ " + std::to_wstring(i), s });
                    i += s.size() * 2;
                }
            }
        }
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // #94. Windows Group Policy Registry.pol ("PReg\x01\x00\x00\x00")
    if ((raw.size() >= 8 && std::memcmp(raw.data(), "PReg\x01\x00\x00\x00", 8) == 0) || ext == L".pol") {
        if (raw.size() < 8 || std::memcmp(raw.data(), "PReg", 4) != 0) return false;
        uint32_t ver = DetailDocs200::RdU32LE(raw.data() + 4);

        SpreadsheetSheet sh;
        sh.name = L"GroupPolicy_Rules";
        sh.maxCols = 4;
        sh.rows.push_back({ L"Registry Key", L"Value Name", L"Reg Type", L"Policy Data" });

        size_t pos = 8;
        while (pos + 16 <= raw.size() && sh.rows.size() < 500) {
            if (DetailDocs200::RdU16LE(raw.data() + pos) != 0x005Bu) { pos += 2; continue; } // '['
            pos += 2;
            std::wstring key = DetailDocs200::ReadUtf16LE(raw.data() + pos, raw.size() - pos);
            pos += (key.size() + 1) * 2;
            if (pos + 2 > raw.size() || DetailDocs200::RdU16LE(raw.data() + pos) != 0x003Bu) break; // ';'
            pos += 2;
            std::wstring val = DetailDocs200::ReadUtf16LE(raw.data() + pos, raw.size() - pos);
            pos += (val.size() + 1) * 2;
            if (pos + 10 > raw.size() || DetailDocs200::RdU16LE(raw.data() + pos) != 0x003Bu) break; // ';'
            pos += 2;
            uint32_t rType = DetailDocs200::RdU32LE(raw.data() + pos); pos += 4;
            if (pos + 6 > raw.size() || DetailDocs200::RdU16LE(raw.data() + pos) != 0x003Bu) break; // ';'
            pos += 2;
            uint32_t rSize = DetailDocs200::RdU32LE(raw.data() + pos); pos += 4;
            if (pos + 2 > raw.size() || DetailDocs200::RdU16LE(raw.data() + pos) != 0x003Bu) break; // ';'
            pos += 2;
            if (rSize > raw.size() - pos) break;

            std::wstring tStr = (rType == 1) ? L"REG_SZ" : (rType == 2) ? L"REG_EXPAND_SZ" :
                                (rType == 4) ? L"REG_DWORD" : (rType == 11) ? L"REG_QWORD" : L"REG_BINARY";
            std::wstring dStr;
            if (rType == 4 && rSize >= 4) dStr = std::to_wstring(DetailDocs200::RdU32LE(raw.data() + pos));
            else if (rType == 1 || rType == 2) dStr = DetailDocs200::ReadUtf16LE(raw.data() + pos, rSize);
            else dStr = DetailDocs200::HexBytes(raw.data() + pos, rSize, 16);

            pos += rSize;
            if (pos + 2 <= raw.size() && DetailDocs200::RdU16LE(raw.data() + pos) == 0x005Du) pos += 2; // ']'
            sh.rows.push_back({ key, val, tStr, dStr });
        }
        if (sh.rows.size() == 1) sh.rows.push_back({ L"(Empty PReg v" + std::to_wstring(ver) + L")", L"-", L"-", L"-" });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Windows Group Policy Registry.pol (PReg v" + std::to_wstring(ver) + L")";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

// ============================================================================
// #95–#100. Chiptune, MusicXML, XMP, Unreal, Unity & iWork/Visio Formats
// ============================================================================
inline bool ParseMediaGameOffice200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    auto trimNullAscii = [](const uint8_t* p, size_t maxLen) -> std::wstring {
        size_t n = 0;
        while (n < maxLen && p[n] != 0) ++n;
        return PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n));
    };

    // #95. Retro Chiptune Music (.sid / .nsf / .spc / .vgm)
    if (ext == L".sid" || ext == L".nsf" || ext == L".spc" || ext == L".vgm") {
        SpreadsheetSheet sh;
        sh.name = L"Chiptune_Header";
        sh.maxCols = 2;
        sh.rows.push_back({ L"Chiptune Field", L"Value" });

        if (ext == L".sid" && raw.size() >= 0x76 && (std::memcmp(raw.data(), "PSID", 4) == 0 || std::memcmp(raw.data(), "RSID", 4) == 0)) {
            out.formatTitle = L"Commodore 64 MOS 6581/8580 SID Music (.sid)";
            sh.rows.push_back({ L"Container Magic", trimNullAscii(raw.data(), 4) + L" v" + std::to_wstring(DetailDocs200::RdU16BE(raw.data() + 4)) });
            sh.rows.push_back({ L"Title", trimNullAscii(raw.data() + 0x16, 32) });
            sh.rows.push_back({ L"Author", trimNullAscii(raw.data() + 0x36, 32) });
            sh.rows.push_back({ L"Released", trimNullAscii(raw.data() + 0x56, 32) });
            sh.rows.push_back({ L"Sub-Songs", std::to_wstring(DetailDocs200::RdU16BE(raw.data() + 0x0E)) + L" (Start #" + std::to_wstring(DetailDocs200::RdU16BE(raw.data() + 0x10)) + L")" });
            wchar_t addrs[96] = {};
            swprintf_s(addrs, L"Load=$%04X, Init=$%04X, Play=$%04X",
                DetailDocs200::RdU16BE(raw.data() + 0x08), DetailDocs200::RdU16BE(raw.data() + 0x0A), DetailDocs200::RdU16BE(raw.data() + 0x0C));
            sh.rows.push_back({ L"6510 Addresses", addrs });
        } else if (ext == L".nsf" && raw.size() >= 0x80 && std::memcmp(raw.data(), "NESM\x1A", 5) == 0) {
            out.formatTitle = L"Nintendo NES 2A03 / Expansion Chiptune (.nsf)";
            sh.rows.push_back({ L"Title", trimNullAscii(raw.data() + 0x0E, 32) });
            sh.rows.push_back({ L"Artist", trimNullAscii(raw.data() + 0x2E, 32) });
            sh.rows.push_back({ L"Copyright", trimNullAscii(raw.data() + 0x4E, 32) });
            sh.rows.push_back({ L"Total Songs", std::to_wstring(raw[6]) + L" (Start #" + std::to_wstring(raw[7]) + L")" });
            uint8_t exp = raw[0x7B];
            std::wstring chips = L"2A03 APU";
            if (exp & 0x01) chips += L" + Konami VRC6";
            if (exp & 0x02) chips += L" + Konami VRC7";
            if (exp & 0x04) chips += L" + FDS";
            if (exp & 0x08) chips += L" + MMC5";
            if (exp & 0x10) chips += L" + Namco 163";
            if (exp & 0x20) chips += L" + Sunsoft 5B";
            sh.rows.push_back({ L"Sound Chips", chips });
        } else if (ext == L".spc" && raw.size() >= 0x100 && std::memcmp(raw.data(), "SNES-SPC700", 11) == 0) {
            out.formatTitle = L"Super Nintendo Sony SPC700 S-DSP State (.spc)";
            sh.rows.push_back({ L"Song Title", trimNullAscii(raw.data() + 0x2E, 32) });
            sh.rows.push_back({ L"Game Title", trimNullAscii(raw.data() + 0x4E, 32) });
            sh.rows.push_back({ L"Artist", trimNullAscii(raw.data() + 0xB1, 32) });
            sh.rows.push_back({ L"Dumper", trimNullAscii(raw.data() + 0x6E, 16) });
            sh.rows.push_back({ L"Comments", trimNullAscii(raw.data() + 0x7E, 32) });
            sh.rows.push_back({ L"SPC700 PC Register", DetailSys100::Hex32(DetailDocs200::RdU16LE(raw.data() + 0x25)) });
        } else if (ext == L".vgm" && raw.size() >= 0x40 && std::memcmp(raw.data(), "Vgm ", 4) == 0) {
            out.formatTitle = L"Video Game Music Chip Register Log (.vgm)";
            uint32_t verBcd  = DetailDocs200::RdU32LE(raw.data() + 0x08);
            uint32_t snClk   = DetailDocs200::RdU32LE(raw.data() + 0x0C);
            uint32_t ym2413  = DetailDocs200::RdU32LE(raw.data() + 0x10);
            uint32_t gd3Rel  = DetailDocs200::RdU32LE(raw.data() + 0x14);
            uint32_t samples = DetailDocs200::RdU32LE(raw.data() + 0x18);
            uint32_t ym2612  = DetailDocs200::RdU32LE(raw.data() + 0x2C);
            wchar_t vbuf[32] = {}; swprintf_s(vbuf, L"%X.%02X", (verBcd >> 8) & 0xFFu, verBcd & 0xFFu);
            sh.rows.push_back({ L"VGM Version", vbuf });
            sh.rows.push_back({ L"Duration (@44.1 kHz)", std::to_wstring(samples / 44100) + L"." + std::to_wstring((samples % 44100) / 441) + L" s" });
            if (snClk)  sh.rows.push_back({ L"TI SN76489 PSG Clock", std::to_wstring(snClk) + L" Hz" });
            if (ym2612) sh.rows.push_back({ L"Yamaha YM2612 FM Clock", std::to_wstring(ym2612) + L" Hz" });
            if (ym2413) sh.rows.push_back({ L"Yamaha YM2413 OPLL Clock", std::to_wstring(ym2413) + L" Hz" });
            if (gd3Rel > 0 && 0x14ull + gd3Rel + 12 < raw.size() && std::memcmp(raw.data() + 0x14 + gd3Rel, "Gd3 ", 4) == 0) {
                size_t gp = 0x14 + gd3Rel + 12;
                static const wchar_t* kGd3Names[] = { L"Track (EN)", L"Track (JP)", L"Game (EN)", L"Game (JP)", L"System (EN)", L"System (JP)", L"Author (EN)" };
                for (int t = 0; t < 7 && gp + 2 <= raw.size(); ++t) {
                    std::wstring s = DetailDocs200::ReadUtf16LE(raw.data() + gp, raw.size() - gp);
                    gp += (s.size() + 1) * 2;
                    if (!s.empty()) sh.rows.push_back({ kGd3Names[t], s });
                }
            }
        } else {
            return false;
        }
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // #96. MusicXML & Compressed MusicXML Score (.musicxml / .mxl)
    if (ext == L".musicxml" || ext == L".mxl") {
        std::vector<uint8_t> xmlBuf;
        std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (raw.size() >= 4 && raw[0] == 'P' && raw[1] == 'K') {
            std::vector<ZipDeflate::ArchiveItemMeta> entries;
            if (ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries)) {
                for (const auto& e : entries) {
                    std::wstring eExt = PathUtils::GetExtension(e.pathInArchive);
                    if ((eExt == L".xml" || eExt == L".musicxml") && e.pathInArchive.find(L"META-INF") == std::wstring::npos) {
                        if (ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, xmlBuf) && !xmlBuf.empty()) {
                            sv = std::string_view(reinterpret_cast<const char*>(xmlBuf.data()), xmlBuf.size());
                            break;
                        }
                    }
                }
            }
        }
        if (sv.find("<score-partwise") == std::string_view::npos && sv.find("<score-timewise") == std::string_view::npos) return false;

        auto xmlTagVal = [&](std::string_view src, std::string_view tag) -> std::string {
            std::string open = "<" + std::string(tag) + ">";
            std::string close = "</" + std::string(tag) + ">";
            size_t p = src.find(open);
            if (p == std::string_view::npos) return {};
            p += open.size();
            size_t e = src.find(close, p);
            return (e == std::string_view::npos) ? std::string{} : std::string(src.substr(p, e - p));
        };

        std::string title = xmlTagVal(sv, "work-title");
        if (title.empty()) title = xmlTagVal(sv, "movement-title");
        std::string beats = xmlTagVal(sv, "beats");
        std::string beatType = xmlTagVal(sv, "beat-type");

        SpreadsheetSheet notesSh;
        notesSh.name = L"Score_Notes";
        notesSh.maxCols = 5;
        notesSh.rows.push_back({ L"Measure", L"Pitch", L"Duration Type", L"Time Sig", L"Lyric / Part" });

        std::wstring timeSig = beats.empty() ? L"4/4" : PathUtils::Utf8ToWide(beats + "/" + beatType);
        size_t mp = 0;
        int measIdx = 0;
        while ((mp = sv.find("<measure", mp)) != std::string_view::npos && notesSh.rows.size() < 400) {
            ++measIdx;
            size_t me = sv.find("</measure>", mp);
            if (me == std::string_view::npos) me = sv.size();
            std::string_view mblk = sv.substr(mp, me - mp);
            size_t np = 0;
            while ((np = mblk.find("<note", np)) != std::string_view::npos && notesSh.rows.size() < 400) {
                size_t ne = mblk.find("</note>", np);
                if (ne == std::string_view::npos) break;
                std::string_view nblk = mblk.substr(np, ne - np);
                std::string step = xmlTagVal(nblk, "step");
                std::string alter = xmlTagVal(nblk, "alter");
                std::string oct  = xmlTagVal(nblk, "octave");
                std::string ntype = xmlTagVal(nblk, "type");
                std::string lyr  = xmlTagVal(nblk, "text");
                std::wstring pitch = step.empty() ? L"Rest"
                    : PathUtils::Utf8ToWide(step + (alter == "1" ? "#" : (alter == "-1" ? "b" : "")) + oct);
                notesSh.rows.push_back({ std::to_wstring(measIdx), pitch,
                    PathUtils::Utf8ToWide(ntype.empty() ? "quarter" : ntype), timeSig, PathUtils::Utf8ToWide(lyr) });
                np = ne + 7;
            }
            mp = (me == sv.size()) ? sv.size() : (me + 10);
        }
        if (notesSh.rows.size() == 1) notesSh.rows.push_back({ L"1", L"C4", L"whole", timeSig, PathUtils::Utf8ToWide(title) });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"MusicXML Musical Score (" + (title.empty() ? ext : PathUtils::Utf8ToWide(title)) + L")";
        out.sheets.push_back(std::move(notesSh));
        out.valid = true;
        return true;
    }

    // #97. Adobe XMP / Camera Raw Develop Sidecar (.xmp)
    if (ext == L".xmp") {
        std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (sv.find("xmpmeta") == std::string_view::npos && sv.find("rdf:RDF") == std::string_view::npos) return false;

        SpreadsheetSheet sh;
        sh.name = L"XMP_CameraRaw";
        sh.maxCols = 3;
        sh.rows.push_back({ L"Namespace", L"Property", L"Value" });

        static const char* kPrefixes[] = { "crs:", "exif:", "tiff:", "aux:", "xmp:", "photoshop:", "dc:" };
        for (const char* pfx : kPrefixes) {
            size_t plen = std::strlen(pfx);
            size_t p = 0;
            while ((p = sv.find(pfx, p)) != std::string_view::npos && sh.rows.size() < 300) {
                size_t kStart = p + plen;
                size_t kEnd = kStart;
                while (kEnd < sv.size() && (isalnum(static_cast<unsigned char>(sv[kEnd])) || sv[kEnd] == '_')) ++kEnd;
                if (kEnd > kStart && kEnd + 2 < sv.size()) {
                    std::string_view prop = sv.substr(kStart, kEnd - kStart);
                    if (sv[kEnd] == '=' && (sv[kEnd + 1] == '"' || sv[kEnd + 1] == '\'')) {
                        char q = sv[kEnd + 1];
                        size_t vEnd = sv.find(q, kEnd + 2);
                        if (vEnd != std::string_view::npos) {
                            sh.rows.push_back({ PathUtils::Utf8ToWide(std::string_view(pfx, plen - 1)),
                                PathUtils::Utf8ToWide(prop), PathUtils::Utf8ToWide(sv.substr(kEnd + 2, vEnd - kEnd - 2)) });
                            p = vEnd + 1;
                            continue;
                        }
                    } else if (sv[kEnd] == '>') {
                        size_t close = sv.find("</", kEnd + 1);
                        if (close != std::string_view::npos && close - (kEnd + 1) < 200 && sv.substr(kEnd + 1, close - kEnd - 1).find('<') == std::string_view::npos) {
                            sh.rows.push_back({ PathUtils::Utf8ToWide(std::string_view(pfx, plen - 1)),
                                PathUtils::Utf8ToWide(prop), PathUtils::Trim(PathUtils::Utf8ToWide(sv.substr(kEnd + 1, close - kEnd - 1))) });
                            p = close + 2;
                            continue;
                        }
                    }
                }
                p = kEnd + 1;
            }
        }
        if (sh.rows.size() == 1) sh.rows.push_back({ L"xmp", L"xmpmeta", L"Valid RDF/XML Sidecar" });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Adobe XMP / Lightroom Camera Raw Sidecar (.xmp)";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // #98. Unreal Engine 4/5 Asset Package (.uasset / .umap — magic 0x9E2A83C1)
    if (ext == L".uasset" || ext == L".umap" || (raw.size() >= 24 && DetailDocs200::RdU32LE(raw.data()) == 0x9E2A83C1u)) {
        if (raw.size() < 24) return false;
        uint32_t tag = DetailDocs200::RdU32LE(raw.data());
        if (tag != 0x9E2A83C1u && tag != 0xC1832A9Eu) return false;

        int32_t legVer  = DetailDocs200::RdI32LE(raw.data() + 4);
        int32_t ue4Ver  = DetailDocs200::RdI32LE(raw.data() + 12);
        size_t pos = 16;
        int32_t ue5Ver = 0;
        if (legVer <= -8 && pos + 4 <= raw.size()) { ue5Ver = DetailDocs200::RdI32LE(raw.data() + pos); pos += 4; }
        if (pos + 8 <= raw.size()) pos += 4; // FileVersionLicenseeUE
        // Skip CustomVersionContainer (count * 20 bytes)
        if (legVer <= -2 && pos + 4 <= raw.size()) {
            uint32_t custCnt = DetailDocs200::RdU32LE(raw.data() + pos); pos += 4;
            if (custCnt < 4096 && pos + custCnt * 20ull <= raw.size()) pos += custCnt * 20ull;
        }
        uint32_t totalHdrSz = (pos + 4 <= raw.size()) ? DetailDocs200::RdU32LE(raw.data() + pos) : 0;
        if (pos + 4 <= raw.size()) pos += 4;

        auto readFString = [&](size_t& p) -> std::wstring {
            if (p + 4 > raw.size()) return {};
            int32_t slen = DetailDocs200::RdI32LE(raw.data() + p); p += 4;
            if (slen > 0 && slen < 1024 && p + static_cast<size_t>(slen) <= raw.size()) {
                std::wstring s = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + p), slen - 1));
                p += static_cast<size_t>(slen);
                return s;
            }
            if (slen < 0 && -slen < 1024 && p + static_cast<size_t>(-slen) * 2 <= raw.size()) {
                size_t bytes = static_cast<size_t>(-slen) * 2;
                std::wstring s = DetailDocs200::ReadUtf16LE(raw.data() + p, bytes);
                p += bytes;
                return s;
            }
            return {};
        };

        std::wstring folderName = readFString(pos);
        uint32_t pkgFlags = (pos + 4 <= raw.size()) ? DetailDocs200::RdU32LE(raw.data() + pos) : 0;
        if (pos + 4 <= raw.size()) pos += 4;
        uint32_t nameCnt  = (pos + 4 <= raw.size()) ? DetailDocs200::RdU32LE(raw.data() + pos) : 0;
        uint32_t nameOff  = (pos + 8 <= raw.size()) ? DetailDocs200::RdU32LE(raw.data() + pos + 4) : 0;

        SpreadsheetSheet hdrSh;
        hdrSh.name = L"Package_Summary";
        hdrSh.maxCols = 2;
        hdrSh.rows.push_back({ L"FPackageFileSummary Field", L"Value" });
        hdrSh.rows.push_back({ L"Engine Generation", (legVer <= -8) ? (L"Unreal Engine 5 (v" + std::to_wstring(ue5Ver) + L")")
                                                                    : (L"Unreal Engine 4 (v" + std::to_wstring(ue4Ver) + L")") });
        hdrSh.rows.push_back({ L"Package Folder", folderName.empty() ? L"None" : folderName });
        hdrSh.rows.push_back({ L"Total Header Size", std::to_wstring(totalHdrSz) + L" B" });
        hdrSh.rows.push_back({ L"Package Flags", DetailSys100::Hex32(pkgFlags) });
        hdrSh.rows.push_back({ L"NameMap Count", std::to_wstring(nameCnt) + L" @ offset " + std::to_wstring(nameOff) });

        SpreadsheetSheet nmSh;
        nmSh.name = L"NameMap";
        nmSh.maxCols = 2;
        nmSh.rows.push_back({ L"Name Index", L"FName Identifier" });
        if (nameCnt > 0 && nameOff > 0 && nameOff < raw.size()) {
            size_t np = nameOff;
            for (uint32_t i = 0; i < std::min<uint32_t>(nameCnt, 300) && np + 4 <= raw.size(); ++i) {
                std::wstring nm = readFString(np);
                if (nm.empty()) break;
                if (ue4Ver >= 504 && np + 4 <= raw.size()) np += 4; // NonCasePreservingHash + CasePreservingHash (2x uint16)
                nmSh.rows.push_back({ std::to_wstring(i), nm });
            }
        }
        if (nmSh.rows.size() == 1) nmSh.rows.push_back({ L"0", L"(Zen Store / Unindexed)" });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = (ext == L".umap") ? L"Unreal Engine Level Map (.umap)" : L"Unreal Engine Asset Package (.uasset)";
        out.sheets.push_back(std::move(hdrSh));
        out.sheets.push_back(std::move(nmSh));
        out.valid = true;
        return true;
    }

    // #99. Unity YAML Prefab & Scene Hierarchy (.prefab / .unity)
    if (ext == L".prefab" || ext == L".unity") {
        std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (sv.find("--- !u!") == std::string_view::npos && sv.find("%YAML") == std::string_view::npos) return false;

        auto unityClassName = [](int cid) -> std::wstring {
            switch (cid) {
            case 1:   return L"GameObject";
            case 4:   return L"Transform";
            case 20:  return L"Camera";
            case 23:  return L"MeshRenderer";
            case 33:  return L"MeshFilter";
            case 54:  return L"Rigidbody";
            case 65:  return L"BoxCollider";
            case 82:  return L"AudioSource";
            case 104: return L"RenderSettings";
            case 108: return L"Light";
            case 114: return L"MonoBehaviour";
            case 137: return L"SkinnedMeshRenderer";
            case 212: return L"SpriteRenderer";
            case 222: return L"CanvasRenderer";
            case 223: return L"Canvas";
            case 224: return L"RectTransform";
            default:  return L"ClassID_" + std::to_wstring(cid);
            }
        };

        SpreadsheetSheet sh;
        sh.name = L"Unity_Hierarchy";
        sh.maxCols = 4;
        sh.rows.push_back({ L"FileID", L"Unity Class", L"m_Name", L"Position / Reference" });

        size_t pos = 0;
        while ((pos = sv.find("--- !u!", pos)) != std::string_view::npos && sh.rows.size() < 500) {
            pos += 7;
            int cid = std::atoi(std::string(sv.substr(pos, std::min<size_t>(12, sv.size() - pos))).c_str());
            size_t amp = sv.find('&', pos);
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) break;
            std::string fid = (amp != std::string_view::npos && amp < eol)
                              ? std::string(sv.substr(amp + 1, eol - amp - 1)) : "0";
            if (!fid.empty() && fid.back() == '\r') fid.pop_back();

            size_t nextObj = sv.find("--- !u!", eol);
            std::string_view blk = sv.substr(eol, (nextObj == std::string_view::npos ? sv.size() : nextObj) - eol);

            auto yamlField = [&](std::string_view key) -> std::string {
                size_t kp = blk.find(key);
                if (kp == std::string_view::npos) return {};
                kp += key.size();
                size_t ke = blk.find('\n', kp);
                std::string_view v = blk.substr(kp, (ke == std::string_view::npos ? blk.size() : ke) - kp);
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
                while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.remove_suffix(1);
                return std::string(v);
            };

            std::string mName = yamlField("m_Name:");
            std::string detail = yamlField("m_LocalPosition:");
            if (detail.empty()) detail = yamlField("m_GameObject:");
            sh.rows.push_back({ PathUtils::Utf8ToWide(fid), unityClassName(cid),
                                PathUtils::Utf8ToWide(mName), PathUtils::Utf8ToWide(detail) });
            pos = eol;
        }
        if (sh.rows.size() == 1) sh.rows.push_back({ L"1", L"Scene", L"Empty", L"%YAML 1.1" });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = (ext == L".unity") ? L"Unity Engine YAML Scene (.unity)" : L"Unity Engine YAML Prefab (.prefab)";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // #100. Apple iWork (.pages / .numbers / .key) & Microsoft Visio (.vsdx) ZIP Packages
    if ((ext == L".pages" || ext == L".numbers" || ext == L".key" || ext == L".vsdx") &&
        raw.size() >= 4 && raw[0] == 'P' && raw[1] == 'K')
    {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries)) return false;

        SpreadsheetSheet sh;
        sh.name = (ext == L".vsdx") ? L"Visio_Shapes" : L"Package_And_Text";
        sh.maxCols = 3;
        sh.rows.push_back({ L"Part / Page", L"Shape / Component", L"Text / Size" });

        for (const auto& e : entries) {
            if (ext == L".vsdx" && e.pathInArchive.find(L"visio/pages/page") != std::wstring::npos &&
                PathUtils::GetExtension(e.pathInArchive) == L".xml")
            {
                std::vector<uint8_t> pgBytes;
                if (ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, pgBytes) && !pgBytes.empty()) {
                    std::string_view psv(reinterpret_cast<const char*>(pgBytes.data()), pgBytes.size());
                    size_t tp = 0;
                    int sIdx = 0;
                    while ((tp = psv.find("<Text", tp)) != std::string_view::npos && sh.rows.size() < 300) {
                        size_t gt = psv.find('>', tp);
                        size_t te = (gt != std::string_view::npos) ? psv.find("</Text>", gt + 1) : std::string_view::npos;
                        if (te == std::string_view::npos) break;
                        // Strip inner <cp .../> tags
                        std::string rawTxt(psv.substr(gt + 1, te - gt - 1));
                        std::string clean;
                        bool inTag = false;
                        for (char c : rawTxt) {
                            if (c == '<') inTag = true;
                            else if (c == '>') inTag = false;
                            else if (!inTag) clean.push_back(c);
                        }
                        std::wstring wtxt = PathUtils::Trim(PathUtils::Utf8ToWide(clean));
                        if (!wtxt.empty()) {
                            sh.rows.push_back({ e.name, L"Shape #" + std::to_wstring(++sIdx), wtxt });
                        }
                        tp = te + 7;
                    }
                }
            } else if (ext != L".vsdx" && (PathUtils::GetExtension(e.pathInArchive) == L".iwa" || PathUtils::GetExtension(e.pathInArchive) == L".xml")) {
                std::vector<uint8_t> partBytes;
                std::wstring preview = PathUtils::FormatByteSize(e.uncompressedSize);
                if (e.uncompressedSize <= 512 * 1024 && ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, partBytes)) {
                    // Extract first readable UTF-8 run >= 5 chars from .iwa / .xml
                    std::string run;
                    for (uint8_t b : partBytes) {
                        if (b >= 32 && b <= 126) run.push_back(static_cast<char>(b));
                        else {
                            if (run.size() >= 6) { preview = PathUtils::Utf8ToWide(run.substr(0, 120)); break; }
                            run.clear();
                        }
                    }
                }
                sh.rows.push_back({ e.pathInArchive, L"iWork Stream", preview });
            }
        }

        // Also list package manifest items if no shape text was extracted yet
        if (sh.rows.size() == 1) {
            for (const auto& e : entries) {
                if (!e.isDirectory && sh.rows.size() < 200) {
                    sh.rows.push_back({ e.pathInArchive, L"ZIP Entry", PathUtils::FormatByteSize(e.uncompressedSize) });
                }
            }
        }

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = (ext == L".vsdx")    ? L"Microsoft Visio Drawing Package (.vsdx)" :
                          (ext == L".pages")   ? L"Apple Pages Document Package (.pages)" :
                          (ext == L".numbers") ? L"Apple Numbers Spreadsheet Package (.numbers)" :
                                                 L"Apple Keynote Presentation Package (.key)";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

// ============================================================================
// Master Dispatcher for ExtendedDocs200 (#81–#100)
// ============================================================================
inline bool ParseExtendedDocs200(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".accdb" || extLower == L".mdb") return ParseAccessDatabase200(raw, extLower, out);
    if (extLower == L".rdb") return ParseRedisRdb200(raw, out);
    if (extLower == L".sst" || extLower == L".ldb") return ParseRocksLevelDbSst200(raw, extLower, out);
    if (extLower == L".pmtiles" || extLower == L".mbtiles") return ParseMapTiles200(raw, extLower, out);
    if (extLower == L".pb" || extLower == L".pbf") return ParseProtobufWire200(raw, extLower, out);
    if (extLower == L".tfstate" || extLower == L".sarif" || extLower == L".spdx" || extLower == L".cdx") {
        return ParseCloudSecOps200(raw, extLower, out);
    }
    if (extLower == L".gpg" || extLower == L".pgp" || extLower == L".sig" || extLower == L".jks" || extLower == L".cat") {
        return ParseCryptoSec200(raw, extLower, out);
    }
    if (extLower == L".hiv" || extLower == L".dat" || extLower == L".etl" || extLower == L".pol") {
        return ParseWindowsForensics200(raw, extLower, out);
    }
    if (extLower == L".sid" || extLower == L".nsf" || extLower == L".spc" || extLower == L".vgm" ||
        extLower == L".musicxml" || extLower == L".mxl" || extLower == L".xmp" ||
        extLower == L".uasset" || extLower == L".umap" || extLower == L".prefab" || extLower == L".unity" ||
        extLower == L".pages" || extLower == L".numbers" || extLower == L".key" || extLower == L".vsdx")
    {
        return ParseMediaGameOffice200(raw, extLower, out);
    }
    return false;
}

} // namespace Formats
