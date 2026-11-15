#pragma once
// Bson.h — Complete Recursive & Multi-Document MongoDB Binary JSON (.bson) Decoder

namespace Formats {

namespace BsonDetail {

inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           static_cast<uint32_t>(p[3]) << 24;
}

inline int32_t RdI32LE(const uint8_t* p) {
    return static_cast<int32_t>(RdU32LE(p));
}

inline uint64_t RdU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(RdU32LE(p)) | (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
}

inline int64_t RdI64LE(const uint8_t* p) {
    return static_cast<int64_t>(RdU64LE(p));
}

// Formats signed Unix epoch milliseconds as "YYYY-MM-DD HH:MM:SS UTC" (all dates incl. pre-1970 & post-2038)
inline std::wstring FormatUtcMillis(int64_t ms) {
    int64_t totalSec = (ms >= 0) ? (ms / 1000LL) : ((ms - 999LL) / 1000LL);
    int64_t days = (totalSec >= 0) ? (totalSec / 86400LL) : ((totalSec - 86399LL) / 86400LL);
    int64_t remSec = totalSec - days * 86400LL;
    int hour = static_cast<int>(remSec / 3600LL);
    int min  = static_cast<int>((remSec % 3600LL) / 60LL);
    int sec  = static_cast<int>(remSec % 60LL);

    // Howard Hinnant civil_from_days
    int64_t z = days + 719468LL;
    int64_t era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    uint64_t doe = static_cast<uint64_t>(z - era * 146097LL);
    uint64_t yoe = (doe - doe / 1460ULL + doe / 36524ULL - doe / 146096ULL) / 365ULL;
    int64_t y = static_cast<int64_t>(yoe) + era * 400LL;
    uint64_t doy = doe - (365ULL * yoe + yoe / 4ULL - yoe / 100ULL);
    uint64_t mp = (5ULL * doy + 2ULL) / 153ULL;
    unsigned d = static_cast<unsigned>(doy - (153ULL * mp + 2ULL) / 5ULL + 1ULL);
    unsigned m = static_cast<unsigned>(mp < 10ULL ? mp + 3ULL : mp - 9ULL);
    y += (m <= 2);

    int64_t fracMs = ms - totalSec * 1000LL;
    wchar_t buf[80] = {};
    if (fracMs > 0) {
        swprintf_s(buf, L"%04lld-%02u-%02u %02d:%02d:%02d.%03lld UTC",
            static_cast<long long>(y), m, d, hour, min, sec, static_cast<long long>(fracMs));
    } else {
        swprintf_s(buf, L"%04lld-%02u-%02u %02d:%02d:%02d UTC",
            static_cast<long long>(y), m, d, hour, min, sec);
    }
    return buf;
}

// IEEE 754-2008 BID128 (Binary Integer Decimal) formatter for BSON Decimal128
inline std::wstring FormatDecimal128(const uint8_t* b) {
    uint64_t lo = RdU64LE(b);
    uint64_t hi = RdU64LE(b + 8);
    bool neg = (hi >> 63) != 0;

    if (((hi >> 58) & 0x1Fu) == 0x1Fu) {
        return (((hi >> 57) & 1u) != 0) ? L"NaN" : (neg ? L"-Infinity" : L"Infinity");
    }

    int exp = 0;
    uint64_t coeffHi = 0;
    if (((hi >> 61) & 0x03u) == 0x03u) {
        exp = static_cast<int>((hi >> 47) & 0x3FFFu) - 6176;
        coeffHi = (1ULL << 49) | (hi & 0x00007FFFFFFFFFFFULL);
    } else {
        exp = static_cast<int>((hi >> 49) & 0x3FFFu) - 6176;
        coeffHi = hi & 0x0001FFFFFFFFFFFFULL;
    }

    // Convert 113-bit integer (coeffHi:lo) to base-10 string via repeated division by 10
    uint32_t words[4] = {
        static_cast<uint32_t>(lo & 0xFFFFFFFFULL),
        static_cast<uint32_t>(lo >> 32),
        static_cast<uint32_t>(coeffHi & 0xFFFFFFFFULL),
        static_cast<uint32_t>(coeffHi >> 32)
    };
    std::string digits;
    for (;;) {
        bool allZero = (words[0] == 0 && words[1] == 0 && words[2] == 0 && words[3] == 0);
        if (allZero) break;
        uint64_t rem = 0;
        for (int i = 3; i >= 0; --i) {
            uint64_t cur = (rem << 32) | words[i];
            words[i] = static_cast<uint32_t>(cur / 10ULL);
            rem = cur % 10ULL;
        }
        digits.push_back(static_cast<char>('0' + rem));
    }
    if (digits.empty()) digits = "0";
    std::reverse(digits.begin(), digits.end());

    std::string res;
    if (neg) res.push_back('-');
    int numDigits = static_cast<int>(digits.size());
    int decPt = numDigits + exp;
    if (exp == 0) {
        res += digits;
    } else if (decPt > 0 && decPt < numDigits) {
        res += digits.substr(0, static_cast<size_t>(decPt)) + "." + digits.substr(static_cast<size_t>(decPt));
    } else if (decPt <= 0 && decPt >= -6) {
        res += "0.";
        res.append(static_cast<size_t>(-decPt), '0');
        res += digits;
    } else {
        res += digits + "E" + (exp > 0 ? "+" : "") + std::to_string(exp);
    }
    return PathUtils::Utf8ToWide(res);
}

inline std::wstring HexLower(const uint8_t* p, size_t n) {
    static const wchar_t kHex[] = L"0123456789abcdef";
    std::wstring s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s.push_back(kHex[p[i] >> 4]);
        s.push_back(kHex[p[i] & 0x0F]);
    }
    return s;
}

inline bool ReadCString(const uint8_t* data, size_t limit, size_t& pos, std::string& out) {
    out.clear();
    size_t st = pos;
    while (pos < limit && data[pos] != 0) ++pos;
    if (pos >= limit) return false;
    out.assign(reinterpret_cast<const char*>(data + st), pos - st);
    ++pos; // consume nul
    return true;
}

inline bool ReadLenUtf8String(const uint8_t* data, size_t limit, size_t& pos, std::wstring& out) {
    if (pos + 4 > limit) return false;
    int32_t slen = RdI32LE(data + pos);
    pos += 4;
    if (slen < 1 || static_cast<size_t>(slen) > limit - pos) return false;
    if (data[pos + static_cast<size_t>(slen) - 1] != 0) return false;
    out = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(slen - 1)));
    pos += static_cast<size_t>(slen);
    return true;
}

struct FlatRow {
    std::wstring path;
    std::wstring typeName;
    std::wstring value;
};

struct ParsedBsonDoc {
    std::vector<std::pair<std::wstring, std::wstring>> topFields;
    std::vector<FlatRow> flatRows;
};

inline bool ParseBsonObjectRecursive(
    const uint8_t* data,
    size_t docStart,
    size_t maxEnd,
    bool isArray,
    const std::wstring& pathPrefix,
    int depth,
    std::vector<FlatRow>& outFlat,
    std::vector<std::pair<std::wstring, std::wstring>>* outTopFields,
    std::wstring* outCompactSummary)
{
    if (depth > 24 || docStart + 5 > maxEnd) return false;
    int32_t docLenI = RdI32LE(data + docStart);
    if (docLenI < 5) return false;
    size_t docLen = static_cast<size_t>(docLenI);
    if (docStart + docLen > maxEnd) return false;
    size_t docEnd = docStart + docLen;
    if (data[docEnd - 1] != 0x00) return false;

    size_t pos = docStart + 4;
    std::wstring summary = isArray ? L"[" : L"{";
    size_t elemIdx = 0;

    while (pos < docEnd - 1) {
        uint8_t etype = data[pos++];
        if (etype == 0x00) {
            return false; // Premature terminator before docEnd - 1
        }
        std::string rawKey;
        if (!ReadCString(data, docEnd - 1, pos, rawKey)) return false;

        std::wstring keyW = PathUtils::Utf8ToWide(rawKey);
        std::wstring fullPath;
        if (isArray) {
            fullPath = pathPrefix + L"[" + keyW + L"]";
        } else {
            fullPath = pathPrefix.empty() ? keyW : (pathPrefix + L"." + keyW);
        }

        std::wstring typeName;
        std::wstring valStr;
        bool isContainer = false;

        switch (etype) {
            case 0x01: { // Double
                if (pos + 8 > docEnd - 1) return false;
                double d = 0.0;
                std::memcpy(&d, data + pos, 8);
                pos += 8;
                typeName = L"double";
                wchar_t b[64] = {};
                swprintf_s(b, L"%.12g", d);
                valStr = b;
                break;
            }
            case 0x02: { // UTF-8 String
                typeName = L"string";
                if (!ReadLenUtf8String(data, docEnd - 1, pos, valStr)) return false;
                break;
            }
            case 0x03:   // Embedded Document
            case 0x04: { // Array
                isContainer = true;
                bool subArr = (etype == 0x04);
                typeName = subArr ? L"array" : L"object";
                if (pos + 4 > docEnd - 1) return false;
                int32_t subLenI = RdI32LE(data + pos);
                if (subLenI < 5 || static_cast<size_t>(subLenI) > (docEnd - 1) - pos) return false;
                std::wstring subSummary;
                if (!ParseBsonObjectRecursive(data, pos, pos + static_cast<size_t>(subLenI),
                        subArr, fullPath, depth + 1, outFlat, nullptr, &subSummary)) {
                    return false;
                }
                valStr = std::move(subSummary);
                pos += static_cast<size_t>(subLenI);
                break;
            }
            case 0x05: { // Binary
                if (pos + 5 > docEnd - 1) return false;
                int32_t blenI = RdI32LE(data + pos);
                uint8_t subType = data[pos + 4];
                pos += 5;
                if (blenI < 0 || static_cast<size_t>(blenI) > (docEnd - 1) - pos) return false;
                size_t blen = static_cast<size_t>(blenI);
                const uint8_t* bp = data + pos;
                pos += blen;
                typeName = L"binary(0x" + HexLower(&subType, 1) + L")";
                if ((subType == 0x04 || subType == 0x03) && blen == 16) {
                    valStr = HexLower(bp, 4) + L"-" + HexLower(bp + 4, 2) + L"-" +
                             HexLower(bp + 6, 2) + L"-" + HexLower(bp + 8, 2) + L"-" + HexLower(bp + 10, 6);
                } else {
                    valStr = L"<" + std::to_wstring(blen) + L" B> 0x" + HexLower(bp, std::min<size_t>(blen, 16));
                    if (blen > 16) valStr += L"...";
                }
                break;
            }
            case 0x06: { // Undefined (deprecated)
                typeName = L"undefined";
                valStr = L"undefined";
                break;
            }
            case 0x07: { // ObjectId (12 bytes)
                if (pos + 12 > docEnd - 1) return false;
                typeName = L"objectId";
                valStr = HexLower(data + pos, 12);
                pos += 12;
                break;
            }
            case 0x08: { // Boolean
                if (pos + 1 > docEnd - 1) return false;
                uint8_t b = data[pos++];
                if (b > 1) return false;
                typeName = L"bool";
                valStr = b ? L"true" : L"false";
                break;
            }
            case 0x09: { // UTC Datetime (int64 ms)
                if (pos + 8 > docEnd - 1) return false;
                int64_t ms = RdI64LE(data + pos);
                pos += 8;
                typeName = L"datetime";
                valStr = FormatUtcMillis(ms);
                break;
            }
            case 0x0A: { // Null
                typeName = L"null";
                valStr = L"null";
                break;
            }
            case 0x0B: { // Regex (cstring pattern + cstring flags)
                std::string pat, flags;
                if (!ReadCString(data, docEnd - 1, pos, pat) || !ReadCString(data, docEnd - 1, pos, flags)) return false;
                typeName = L"regex";
                valStr = L"/" + PathUtils::Utf8ToWide(pat) + L"/" + PathUtils::Utf8ToWide(flags);
                break;
            }
            case 0x0C: { // DBPointer (deprecated: string + 12B oid)
                std::wstring ns;
                if (!ReadLenUtf8String(data, docEnd - 1, pos, ns) || pos + 12 > docEnd - 1) return false;
                typeName = L"dbPointer";
                valStr = ns + L":" + HexLower(data + pos, 12);
                pos += 12;
                break;
            }
            case 0x0D: { // JavaScript code
                typeName = L"javascript";
                if (!ReadLenUtf8String(data, docEnd - 1, pos, valStr)) return false;
                break;
            }
            case 0x0E: { // Symbol
                typeName = L"symbol";
                if (!ReadLenUtf8String(data, docEnd - 1, pos, valStr)) return false;
                break;
            }
            case 0x0F: { // JavaScript w/ Scope
                if (pos + 4 > docEnd - 1) return false;
                int32_t wsLen = RdI32LE(data + pos);
                if (wsLen < 14 || static_cast<size_t>(wsLen) > (docEnd - 1) - pos) return false;
                size_t wsEnd = pos + static_cast<size_t>(wsLen);
                pos += 4;
                std::wstring code;
                if (!ReadLenUtf8String(data, wsEnd, pos, code)) return false;
                std::wstring scopeSum;
                if (!ParseBsonObjectRecursive(data, pos, wsEnd, false, fullPath + L".$scope", depth + 1, outFlat, nullptr, &scopeSum)) {
                    return false;
                }
                pos = wsEnd;
                typeName = L"codeWithScope";
                valStr = code + L" " + scopeSum;
                break;
            }
            case 0x10: { // Int32
                if (pos + 4 > docEnd - 1) return false;
                int32_t v = RdI32LE(data + pos);
                pos += 4;
                typeName = L"int32";
                valStr = std::to_wstring(v);
                break;
            }
            case 0x11: { // Timestamp (uint32 increment, uint32 timestamp)
                if (pos + 8 > docEnd - 1) return false;
                uint32_t inc = RdU32LE(data + pos);
                uint32_t ts  = RdU32LE(data + pos + 4);
                pos += 8;
                typeName = L"timestamp";
                valStr = L"Timestamp(" + std::to_wstring(ts) + L", " + std::to_wstring(inc) + L")";
                break;
            }
            case 0x12: { // Int64
                if (pos + 8 > docEnd - 1) return false;
                int64_t v = RdI64LE(data + pos);
                pos += 8;
                typeName = L"int64";
                valStr = std::to_wstring(v);
                break;
            }
            case 0x13: { // Decimal128
                if (pos + 16 > docEnd - 1) return false;
                typeName = L"decimal128";
                valStr = FormatDecimal128(data + pos);
                pos += 16;
                break;
            }
            case 0xFF: { // MinKey
                typeName = L"minKey";
                valStr = L"MinKey";
                break;
            }
            case 0x7F: { // MaxKey
                typeName = L"maxKey";
                valStr = L"MaxKey";
                break;
            }
            default:
                return false;
        }

        if (!isContainer && outFlat.size() < 2000) {
            outFlat.push_back({ fullPath, typeName, valStr });
        }
        if (outTopFields) {
            outTopFields->push_back({ keyW, valStr });
        }
        if (outCompactSummary) {
            if (elemIdx < 8) {
                if (elemIdx > 0) summary += L", ";
                summary += isArray ? valStr : (keyW + L": " + valStr);
            } else if (elemIdx == 8) {
                summary += L", ...";
            }
        }
        ++elemIdx;
    }

    if (pos != docEnd - 1) return false;
    if (outCompactSummary) {
        summary.push_back(isArray ? L']' : L'}');
        *outCompactSummary = std::move(summary);
    }
    return true;
}

} // namespace BsonDetail

inline bool ParseBsonDocument(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 5) return false;

    std::vector<BsonDetail::ParsedBsonDoc> docs;
    size_t offset = 0;
    while (offset < raw.size() && docs.size() < 500) {
        if (offset + 5 > raw.size()) return false;
        int32_t docLenI = BsonDetail::RdI32LE(raw.data() + offset);
        if (docLenI < 5 || offset + static_cast<size_t>(docLenI) > raw.size()) {
            return false;
        }
        BsonDetail::ParsedBsonDoc d;
        std::wstring docPrefix = (offset == 0) ? L"" : (L"[" + std::to_wstring(docs.size()) + L"]");
        if (!BsonDetail::ParseBsonObjectRecursive(
                raw.data(), offset, offset + static_cast<size_t>(docLenI),
                false, docPrefix, 0, d.flatRows, &d.topFields, nullptr)) {
            return false;
        }
        docs.push_back(std::move(d));
        offset += static_cast<size_t>(docLenI);
    }

    if (docs.empty()) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = (docs.size() > 1)
        ? (L"MongoDB BSON Dump Stream (" + std::to_wstring(docs.size()) + L" documents)")
        : L"Binary JSON Document (.bson)";

    if (docs.size() > 1) {
        // Multi-document BSON stream: Tabular sheet of unioned top-level keys + Flattened Fields sheet
        std::vector<std::wstring> colOrder;
        std::map<std::wstring, size_t> colIndex;
        for (const auto& d : docs) {
            for (const auto& kv : d.topFields) {
                if (colIndex.find(kv.first) == colIndex.end() && colOrder.size() < 128) {
                    colIndex[kv.first] = colOrder.size();
                    colOrder.push_back(kv.first);
                }
            }
        }
        if (colOrder.empty()) colOrder.push_back(L"(empty_doc)");

        SpreadsheetSheet tblSheet;
        tblSheet.name = L"BSON_Documents";
        tblSheet.maxCols = colOrder.size();
        tblSheet.rows.push_back(colOrder);

        for (const auto& d : docs) {
            std::vector<std::wstring> row(colOrder.size());
            for (const auto& kv : d.topFields) {
                auto it = colIndex.find(kv.first);
                if (it != colIndex.end()) row[it->second] = kv.second;
            }
            tblSheet.rows.push_back(std::move(row));
        }
        out.sheets.push_back(std::move(tblSheet));
    }

    SpreadsheetSheet flatSheet;
    flatSheet.name = L"BSON_Fields";
    flatSheet.maxCols = 3;
    flatSheet.rows.push_back({ L"Key", L"BSON Type", L"Value" });
    for (size_t di = 0; di < docs.size() && flatSheet.rows.size() < 2000; ++di) {
        for (const auto& fr : docs[di].flatRows) {
            std::wstring kp = (docs.size() > 1 && di == 0) ? (L"[0]." + fr.path) : fr.path;
            flatSheet.rows.push_back({ std::move(kp), fr.typeName, fr.value });
            if (flatSheet.rows.size() >= 2000) break;
        }
    }
    if (flatSheet.rows.size() == 1) {
        flatSheet.rows.push_back({ L"{}", L"object", L"(empty document)" });
    }
    out.sheets.push_back(std::move(flatSheet));

    out.valid = true;
    return true;
}

} // namespace Formats
