#pragma once
// ApacheIonBinary.h — Amazon Ion 1.0 Binary & Text (.ion, .10n) Reader

namespace Formats {
namespace DetailApacheIon {

inline const char* SystemSymbolName(uint64_t sid) {
    switch (sid) {
        case 0: return "$0";
        case 1: return "$ion";
        case 2: return "$ion_1_0";
        case 3: return "$ion_symbol_table";
        case 4: return "name";
        case 5: return "version";
        case 6: return "imports";
        case 7: return "symbols";
        case 8: return "max_id";
        case 9: return "$ion_shared_symbol_table";
        default: return nullptr;
    }
}

struct IonSymTab {
    std::vector<std::string> localSyms; // index 0 corresponds to symbol ID 10

    void Reset() { localSyms.clear(); }

    std::string Resolve(uint64_t sid) const {
        if (const char* sys = SystemSymbolName(sid)) return sys;
        if (sid >= 10 && sid - 10 < localSyms.size() && !localSyms[static_cast<size_t>(sid - 10)].empty()) {
            return localSyms[static_cast<size_t>(sid - 10)];
        }
        return "$" + std::to_string(sid);
    }
};

// Ion VarUInt: 7 bits per byte, big-endian; MSB (0x80) is SET on the FINAL byte (stop bit).
inline bool ReadIonVarUInt(const uint8_t* d, size_t end, size_t& pos, uint64_t& out) {
    out = 0;
    for (int i = 0; i < 10; ++i) {
        if (pos >= end) return false;
        uint8_t b = d[pos++];
        out = (out << 7) | (b & 0x7Fu);
        if ((b & 0x80u) != 0) return true;
    }
    return false;
}

// Ion VarInt: sign-magnitude; bit 6 (0x40) of first byte = sign, bit 7 (0x80) of last byte = stop bit.
inline bool ReadIonVarInt(const uint8_t* d, size_t end, size_t& pos, int64_t& out, bool* outNegZero = nullptr) {
    if (pos >= end) return false;
    uint8_t b0 = d[pos++];
    bool neg = (b0 & 0x40u) != 0;
    uint64_t mag = b0 & 0x3Fu;
    if ((b0 & 0x80u) == 0) {
        for (int i = 0; i < 9; ++i) {
            if (pos >= end) return false;
            uint8_t b = d[pos++];
            mag = (mag << 7) | (b & 0x7Fu);
            if ((b & 0x80u) != 0) goto done;
        }
        return false;
    }
done:
    if (outNegZero) *outNegZero = (neg && mag == 0);
    out = neg ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
    return true;
}

inline uint64_t ReadIonFixedUInt(const uint8_t* p, size_t len) {
    uint64_t v = 0;
    for (size_t i = 0; i < len && i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

// Ion signed fixed Int (sign-magnitude: MSB of first byte is sign)
inline bool ReadIonSignedMag(const uint8_t* p, size_t len, bool& neg, uint64_t& mag) {
    neg = false;
    mag = 0;
    if (len == 0) return true;
    if (len > 8) return false;
    neg = (p[0] & 0x80u) != 0;
    mag = p[0] & 0x7Fu;
    for (size_t i = 1; i < len; ++i) mag = (mag << 8) | p[i];
    return true;
}

inline std::string FormatIonDecimal(int64_t exp, bool neg, uint64_t mag) {
    if (mag == 0) {
        std::string z = neg ? "-0" : "0";
        if (exp == 0) return z + ".";
        return z + "d" + std::to_string(exp);
    }
    std::string digits = std::to_string(mag);
    std::string sign = neg ? "-" : "";
    int64_t n = static_cast<int64_t>(digits.size());
    int64_t dotPos = n + exp;
    if (exp == 0) return sign + digits + ".";
    if (dotPos > 0 && dotPos < n) {
        return sign + digits.substr(0, static_cast<size_t>(dotPos)) + "." + digits.substr(static_cast<size_t>(dotPos));
    }
    if (dotPos <= 0 && dotPos >= -6) {
        return sign + "0." + std::string(static_cast<size_t>(-dotPos), '0') + digits;
    }
    return sign + digits + "d" + std::to_string(exp);
}

inline std::string DecodeIonTimestamp(const uint8_t* p, size_t len) {
    size_t pos = 0;
    int64_t offsetMin = 0;
    bool negZeroOff = false;
    if (!ReadIonVarInt(p, len, pos, offsetMin, &negZeroOff)) return "";
    uint64_t year = 0;
    if (!ReadIonVarUInt(p, len, pos, year)) return "";
    char buf[128] = {};
    if (pos >= len) {
        std::snprintf(buf, sizeof(buf), "%04lluT", static_cast<unsigned long long>(year));
        return buf;
    }
    uint64_t month = 1, day = 1, hour = 0, minute = 0, second = 0;
    bool hasDay = false, hasHm = false, hasSec = false;
    if (!ReadIonVarUInt(p, len, pos, month)) return "";
    if (pos < len) {
        if (!ReadIonVarUInt(p, len, pos, day)) return "";
        hasDay = true;
    }
    if (pos < len) {
        if (!ReadIonVarUInt(p, len, pos, hour) || !ReadIonVarUInt(p, len, pos, minute)) return "";
        hasHm = true;
    }
    if (pos < len) {
        if (!ReadIonVarUInt(p, len, pos, second)) return "";
        hasSec = true;
    }
    std::string fracStr;
    if (pos < len) {
        int64_t fexp = 0;
        if (ReadIonVarInt(p, len, pos, fexp) && fexp < 0 && fexp >= -9) {
            bool fneg = false; uint64_t fmag = 0;
            if (ReadIonSignedMag(p + pos, len - pos, fneg, fmag) && !fneg && fmag > 0) {
                std::string fd = std::to_string(fmag);
                size_t width = static_cast<size_t>(-fexp);
                if (fd.size() < width) fd.insert(0, width - fd.size(), '0');
                fracStr = "." + fd;
            }
        }
    }

    if (!hasDay) {
        std::snprintf(buf, sizeof(buf), "%04llu-%02lluT",
            static_cast<unsigned long long>(year), static_cast<unsigned long long>(month));
        return buf;
    }
    if (!hasHm) {
        std::snprintf(buf, sizeof(buf), "%04llu-%02llu-%02llu",
            static_cast<unsigned long long>(year), static_cast<unsigned long long>(month), static_cast<unsigned long long>(day));
        return buf;
    }

    // Ion binary timestamps store UTC fields; add offsetMin to obtain local display time
    if (offsetMin != 0 && !negZeroOff && offsetMin >= -1440 && offsetMin <= 1440) {
        // Convert UTC Y-M-D H:M to epoch minutes, add offsetMin, convert back
        int64_t y = static_cast<int64_t>(year);
        int64_t m = static_cast<int64_t>(month);
        int64_t d2 = static_cast<int64_t>(day);
        int64_t yAdj = y - (m <= 2 ? 1 : 0);
        int64_t era = (yAdj >= 0 ? yAdj : yAdj - 399) / 400;
        uint32_t yoe = static_cast<uint32_t>(yAdj - era * 400);
        uint32_t doy = static_cast<uint32_t>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d2 - 1);
        uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        int64_t days = era * 146097 + static_cast<int64_t>(doe) - 719468;
        int64_t totMin = days * 1440 + static_cast<int64_t>(hour) * 60 + static_cast<int64_t>(minute) + offsetMin;
        int64_t lDays = (totMin >= 0) ? (totMin / 1440) : ((totMin - 1439) / 1440);
        int64_t remMin = totMin - lDays * 1440;
        hour = static_cast<uint64_t>(remMin / 60);
        minute = static_cast<uint64_t>(remMin % 60);
        int64_t z = lDays + 719468;
        int64_t era2 = (z >= 0 ? z : z - 146096) / 146097;
        uint32_t doe2 = static_cast<uint32_t>(z - era2 * 146097);
        uint32_t yoe2 = (doe2 - doe2 / 1460 + doe2 / 36524 - doe2 / 146096) / 365;
        int64_t y2 = static_cast<int64_t>(yoe2) + era2 * 400;
        uint32_t doy2 = doe2 - (365 * yoe2 + yoe2 / 4 - yoe2 / 100);
        uint32_t mp2 = (5 * doy2 + 2) / 153;
        day = doy2 - (153 * mp2 + 2) / 5 + 1;
        uint32_t m2 = mp2 + (mp2 < 10 ? 3 : -9);
        year = static_cast<uint64_t>(y2 + (m2 <= 2 ? 1 : 0));
        month = m2;
    }

    std::string tzSuffix = negZeroOff ? "-00:00" : "Z";
    if (!negZeroOff && offsetMin != 0) {
        char tzb[16] = {};
        int64_t ao = std::llabs(offsetMin);
        std::snprintf(tzb, sizeof(tzb), "%c%02lld:%02lld", offsetMin < 0 ? '-' : '+', ao / 60, ao % 60);
        tzSuffix = tzb;
    }

    if (hasSec) {
        std::snprintf(buf, sizeof(buf), "%04llu-%02llu-%02lluT%02llu:%02llu:%02llu",
            static_cast<unsigned long long>(year), static_cast<unsigned long long>(month),
            static_cast<unsigned long long>(day), static_cast<unsigned long long>(hour),
            static_cast<unsigned long long>(minute), static_cast<unsigned long long>(second));
        return std::string(buf) + fracStr + tzSuffix;
    }
    std::snprintf(buf, sizeof(buf), "%04llu-%02llu-%02lluT%02llu:%02llu",
        static_cast<unsigned long long>(year), static_cast<unsigned long long>(month),
        static_cast<unsigned long long>(day), static_cast<unsigned long long>(hour),
        static_cast<unsigned long long>(minute));
    return std::string(buf) + tzSuffix;
}

// Returns: 1 = decoded value, 0 = NOP pad skipped, -1 = error
inline int DecodeIonBinaryValue(
    const uint8_t* d,
    size_t end,
    size_t& pos,
    IonSymTab& symTab,
    AiDataDetail::BinTreeNode& outNode,
    int depth,
    std::vector<uint64_t>* outAnnots = nullptr)
{
    if (depth > 32 || pos >= end) return -1;
    uint8_t td = d[pos++];
    uint8_t tc = (td >> 4) & 0x0Fu;
    uint8_t ln = td & 0x0Fu;

    if (tc == 0) {
        if (ln == 15) {
            outNode = {};
            outNode.kind = AiDataDetail::BinTreeNode::Null;
            outNode.typeLabel = L"null";
            return 1;
        }
        uint64_t padLen = ln;
        if (ln == 14 && !ReadIonVarUInt(d, end, pos, padLen)) return -1;
        if (padLen > end - pos) return -1;
        pos += static_cast<size_t>(padLen);
        return 0; // NOP pad
    }

    if (tc == 15) return -1; // reserved

    if (ln == 15) {
        static const wchar_t* kNullNames[16] = {
            L"null", L"null.bool", L"null.int", L"null.int", L"null.float",
            L"null.decimal", L"null.timestamp", L"null.symbol", L"null.string",
            L"null.clob", L"null.blob", L"null.list", L"null.sexp", L"null.struct", L"null", L"null"
        };
        outNode = {};
        outNode.kind = AiDataDetail::BinTreeNode::Null;
        outNode.typeLabel = kNullNames[tc];
        return 1;
    }

    if (tc == 1) { // bool
        if (ln > 1) return -1;
        outNode = {};
        outNode.kind = AiDataDetail::BinTreeNode::Bool;
        outNode.typeLabel = L"bool";
        outNode.bval = (ln == 1);
        return 1;
    }

    uint64_t payLen = ln;
    if (ln == 14 || (tc == 13 && ln == 1)) { // ln==14 or ordered struct 0xD1 uses VarUInt length
        if (!ReadIonVarUInt(d, end, pos, payLen)) return -1;
    }
    if (payLen > end - pos) return -1;
    const size_t valStart = pos;
    const size_t valEnd   = pos + static_cast<size_t>(payLen);
    pos = valEnd;

    outNode = {};
    switch (tc) {
        case 2: { // posInt
            if (payLen > 8) return -1;
            outNode.kind = AiDataDetail::BinTreeNode::UInt;
            outNode.typeLabel = L"int";
            outNode.uval = ReadIonFixedUInt(d + valStart, static_cast<size_t>(payLen));
            return 1;
        }
        case 3: { // negInt
            if (payLen == 0 || payLen > 8) return -1;
            uint64_t mag = ReadIonFixedUInt(d + valStart, static_cast<size_t>(payLen));
            outNode.kind = AiDataDetail::BinTreeNode::Int;
            outNode.typeLabel = L"int";
            outNode.ival = -static_cast<int64_t>(mag);
            return 1;
        }
        case 4: { // float
            outNode.kind = AiDataDetail::BinTreeNode::Float;
            outNode.typeLabel = (payLen == 4) ? L"float32" : L"float64";
            if (payLen == 0) outNode.fval = 0.0;
            else if (payLen == 4) outNode.fval = AiDataDetail::RdF32BE(d + valStart);
            else if (payLen == 8) outNode.fval = AiDataDetail::RdF64BE(d + valStart);
            else return -1;
            return 1;
        }
        case 5: { // decimal
            outNode.kind = AiDataDetail::BinTreeNode::Str;
            outNode.typeLabel = L"decimal";
            if (payLen == 0) {
                outNode.str = "0.";
                return 1;
            }
            size_t dp = valStart;
            int64_t exp = 0;
            if (!ReadIonVarInt(d, valEnd, dp, exp)) return -1;
            bool neg = false; uint64_t mag = 0;
            if (!ReadIonSignedMag(d + dp, valEnd - dp, neg, mag)) return -1;
            outNode.str = FormatIonDecimal(exp, neg, mag);
            return 1;
        }
        case 6: { // timestamp
            outNode.kind = AiDataDetail::BinTreeNode::Str;
            outNode.typeLabel = L"timestamp";
            outNode.str = DecodeIonTimestamp(d + valStart, static_cast<size_t>(payLen));
            return outNode.str.empty() ? -1 : 1;
        }
        case 7: { // symbol
            if (payLen > 8) return -1;
            uint64_t sid = ReadIonFixedUInt(d + valStart, static_cast<size_t>(payLen));
            outNode.kind = AiDataDetail::BinTreeNode::Str;
            outNode.typeLabel = L"symbol";
            outNode.uval = sid;
            outNode.str = symTab.Resolve(sid);
            return 1;
        }
        case 8: { // string
            outNode.kind = AiDataDetail::BinTreeNode::Str;
            outNode.typeLabel = L"string";
            outNode.str.assign(reinterpret_cast<const char*>(d + valStart), static_cast<size_t>(payLen));
            return 1;
        }
        case 9: { // clob
            if (DetailStructuredDataDoc::IsMostlyPrintableUtf8(d + valStart, std::min<size_t>(static_cast<size_t>(payLen), 128))) {
                outNode.kind = AiDataDetail::BinTreeNode::Str;
                outNode.typeLabel = L"clob";
                outNode.str.assign(reinterpret_cast<const char*>(d + valStart), std::min<size_t>(static_cast<size_t>(payLen), 512));
            } else {
                outNode.kind = AiDataDetail::BinTreeNode::Bytes;
                outNode.typeLabel = L"clob";
                outNode.bytes.assign(d + valStart, d + valStart + std::min<size_t>(static_cast<size_t>(payLen), 64));
            }
            return 1;
        }
        case 10: { // blob
            outNode.kind = AiDataDetail::BinTreeNode::Bytes;
            outNode.typeLabel = L"blob";
            outNode.bytes.assign(d + valStart, d + valStart + std::min<size_t>(static_cast<size_t>(payLen), 64));
            return 1;
        }
        case 11:
        case 12: { // list / sexp
            outNode.kind = AiDataDetail::BinTreeNode::Array;
            outNode.typeLabel = (tc == 11) ? L"list" : L"sexp";
            size_t cp = valStart;
            while (cp < valEnd && outNode.children.size() < 4096) {
                AiDataDetail::BinTreeNode child;
                int rc = DecodeIonBinaryValue(d, valEnd, cp, symTab, child, depth + 1, nullptr);
                if (rc < 0) return -1;
                if (rc > 0) outNode.children.push_back(std::move(child));
            }
            return 1;
        }
        case 13: { // struct
            outNode.kind = AiDataDetail::BinTreeNode::Map;
            outNode.typeLabel = L"struct";
            size_t cp = valStart;
            while (cp < valEnd && outNode.entries.size() < 4096) {
                uint64_t fsid = 0;
                if (!ReadIonVarUInt(d, valEnd, cp, fsid)) return -1;
                AiDataDetail::BinTreeNode val;
                int rc = DecodeIonBinaryValue(d, valEnd, cp, symTab, val, depth + 1, nullptr);
                if (rc < 0) return -1;
                if (rc == 0) continue; // NOP pad inside struct
                AiDataDetail::BinTreeNode keyNode;
                keyNode.kind = AiDataDetail::BinTreeNode::Str;
                keyNode.uval = fsid;
                keyNode.str = symTab.Resolve(fsid);
                outNode.entries.emplace_back(std::move(keyNode), std::move(val));
            }
            return 1;
        }
        case 14: { // annotation wrapper
            size_t cp = valStart;
            uint64_t annotLen = 0;
            if (!ReadIonVarUInt(d, valEnd, cp, annotLen) || annotLen == 0 || annotLen > valEnd - cp) return -1;
            const size_t annotEnd = cp + static_cast<size_t>(annotLen);
            std::vector<uint64_t> annots;
            while (cp < annotEnd) {
                uint64_t asid = 0;
                if (!ReadIonVarUInt(d, annotEnd, cp, asid)) return -1;
                annots.push_back(asid);
            }
            if (cp >= valEnd) return -1;
            int rc = DecodeIonBinaryValue(d, valEnd, cp, symTab, outNode, depth + 1, nullptr);
            if (rc <= 0 || cp != valEnd) return -1;
            if (outAnnots) *outAnnots = annots;
            if (!annots.empty()) {
                std::wstring pfx;
                for (uint64_t a : annots) {
                    pfx += PathUtils::Utf8ToWide(symTab.Resolve(a)) + L"::";
                }
                outNode.typeLabel = pfx + outNode.typeLabel;
            }
            return 1;
        }
        default:
            return -1;
    }
}

inline void ApplyLocalSymbolTableStruct(const AiDataDetail::BinTreeNode& stNode, IonSymTab& symTab) {
    if (stNode.kind != AiDataDetail::BinTreeNode::Map) return;
    bool appendMode = false;
    std::vector<std::string> importedPlaceholders;
    std::vector<std::string> newSyms;

    for (const auto& kv : stNode.entries) {
        const std::string& fname = kv.first.str;
        const uint64_t fsid = kv.first.uval;
        if (fsid == 6 || fname == "imports") {
            if (kv.second.uval == 3 || kv.second.str == "$ion_symbol_table") {
                appendMode = true;
            } else if (kv.second.kind == AiDataDetail::BinTreeNode::Array) {
                for (const auto& imp : kv.second.children) {
                    if (imp.kind != AiDataDetail::BinTreeNode::Map) continue;
                    std::string impName = "shared";
                    uint64_t maxId = 0;
                    for (const auto& ikv : imp.entries) {
                        if (ikv.first.uval == 4 || ikv.first.str == "name") impName = ikv.second.str;
                        else if ((ikv.first.uval == 8 || ikv.first.str == "max_id") && ikv.second.kind == AiDataDetail::BinTreeNode::UInt) maxId = ikv.second.uval;
                    }
                    if (impName != "$ion" && maxId <= 4096) {
                        for (uint64_t k = 1; k <= maxId; ++k) {
                            importedPlaceholders.push_back(impName + "#" + std::to_string(k));
                        }
                    }
                }
            }
        } else if ((fsid == 7 || fname == "symbols") && kv.second.kind == AiDataDetail::BinTreeNode::Array) {
            for (const auto& elem : kv.second.children) {
                newSyms.push_back(elem.kind == AiDataDetail::BinTreeNode::Str ? elem.str : std::string{});
            }
        }
    }

    if (!appendMode) {
        symTab.Reset();
        for (auto& s : importedPlaceholders) symTab.localSyms.push_back(std::move(s));
    }
    for (auto& s : newSyms) symTab.localSyms.push_back(std::move(s));
}

// Lightweight recursive-descent Ion Text parser (handles comments, annotations, unquoted symbols, structs, lists, sexps, blobs)
struct IonTextParser {
    std::string_view sv;
    size_t pos = 0;

    void SkipWsAndComments() {
        while (pos < sv.size()) {
            char c = sv[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++pos; continue; }
            if (c == '/' && pos + 1 < sv.size() && sv[pos + 1] == '/') {
                pos += 2;
                while (pos < sv.size() && sv[pos] != '\n') ++pos;
                continue;
            }
            if (c == '/' && pos + 1 < sv.size() && sv[pos + 1] == '*') {
                pos += 2;
                while (pos + 1 < sv.size() && !(sv[pos] == '*' && sv[pos + 1] == '/')) ++pos;
                if (pos + 1 < sv.size()) pos += 2;
                continue;
            }
            break;
        }
    }

    static bool IsIdentStart(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$';
    }
    static bool IsIdentCont(char c) {
        return IsIdentStart(c) || (c >= '0' && c <= '9');
    }

    bool ReadQuoted(char q, std::string& out) {
        out.clear();
        if (pos >= sv.size() || sv[pos] != q) return false;
        ++pos;
        while (pos < sv.size()) {
            char c = sv[pos++];
            if (c == q) return true;
            if (c == '\\' && pos < sv.size()) {
                char esc = sv[pos++];
                if (esc == 'n') out.push_back('\n');
                else if (esc == 'r') out.push_back('\r');
                else if (esc == 't') out.push_back('\t');
                else out.push_back(esc);
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool ParseTokenOrString(std::string& out, bool& wasString) {
        SkipWsAndComments();
        wasString = false;
        if (pos >= sv.size()) return false;
        if (sv[pos] == '"') { wasString = true; return ReadQuoted('"', out); }
        if (pos + 2 < sv.size() && sv.substr(pos, 3) == "'''") {
            wasString = true;
            pos += 3;
            size_t endQ = sv.find("'''", pos);
            if (endQ == std::string_view::npos) return false;
            out.assign(sv.substr(pos, endQ - pos));
            pos = endQ + 3;
            return true;
        }
        if (sv[pos] == '\'') return ReadQuoted('\'', out);
        if (IsIdentStart(sv[pos])) {
            size_t s = pos;
            while (pos < sv.size() && IsIdentCont(sv[pos])) ++pos;
            out.assign(sv.substr(s, pos - s));
            return true;
        }
        return false;
    }

    bool ParseValue(AiDataDetail::BinTreeNode& out, int depth) {
        if (depth > 32) return false;
        SkipWsAndComments();
        if (pos >= sv.size()) return false;

        std::wstring annotPfx;
        // Check for annotations: ident:: or 'sym'::
        for (int a = 0; a < 8; ++a) {
            size_t save = pos;
            std::string tok;
            bool wasStr = false;
            if (ParseTokenOrString(tok, wasStr) && !wasStr) {
                SkipWsAndComments();
                if (pos + 1 < sv.size() && sv[pos] == ':' && sv[pos + 1] == ':') {
                    pos += 2;
                    annotPfx += PathUtils::Utf8ToWide(tok) + L"::";
                    SkipWsAndComments();
                    continue;
                }
            }
            pos = save;
            break;
        }

        out = {};
        char c = sv[pos];
        if (c == '{' && pos + 1 < sv.size() && sv[pos + 1] == '{') { // blob / clob {{ ... }}
            pos += 2;
            size_t close = sv.find("}}", pos);
            if (close == std::string_view::npos) return false;
            std::string inner(sv.substr(pos, close - pos));
            pos = close + 2;
            out.kind = AiDataDetail::BinTreeNode::Str;
            out.typeLabel = annotPfx + L"lob";
            out.str = PathUtils::WideToUtf8(PathUtils::Trim(PathUtils::Utf8ToWide(inner)));
            return true;
        }
        if (c == '{') { // struct
            ++pos;
            out.kind = AiDataDetail::BinTreeNode::Map;
            out.typeLabel = annotPfx + L"struct";
            while (pos < sv.size() && out.entries.size() < 4096) {
                SkipWsAndComments();
                if (pos < sv.size() && sv[pos] == '}') { ++pos; return true; }
                std::string k;
                bool dummy = false;
                if (!ParseTokenOrString(k, dummy)) return false;
                SkipWsAndComments();
                if (pos >= sv.size() || sv[pos] != ':') return false;
                ++pos;
                AiDataDetail::BinTreeNode v;
                if (!ParseValue(v, depth + 1)) return false;
                AiDataDetail::BinTreeNode kn;
                kn.kind = AiDataDetail::BinTreeNode::Str;
                kn.str = std::move(k);
                out.entries.emplace_back(std::move(kn), std::move(v));
                SkipWsAndComments();
                if (pos < sv.size() && sv[pos] == ',') ++pos;
            }
            return false;
        }
        if (c == '[' || c == '(') { // list / sexp
            char closeCh = (c == '[') ? ']' : ')';
            ++pos;
            out.kind = AiDataDetail::BinTreeNode::Array;
            out.typeLabel = annotPfx + ((c == '[') ? L"list" : L"sexp");
            while (pos < sv.size() && out.children.size() < 4096) {
                SkipWsAndComments();
                if (pos < sv.size() && sv[pos] == closeCh) { ++pos; return true; }
                AiDataDetail::BinTreeNode elem;
                if (!ParseValue(elem, depth + 1)) return false;
                out.children.push_back(std::move(elem));
                SkipWsAndComments();
                if (c == '[' && pos < sv.size() && sv[pos] == ',') ++pos;
            }
            return false;
        }
        if (c == '"' || c == '\'') {
            std::string s;
            bool wasStr = false;
            if (!ParseTokenOrString(s, wasStr)) return false;
            out.kind = AiDataDetail::BinTreeNode::Str;
            out.typeLabel = annotPfx + (wasStr ? L"string" : L"symbol");
            out.str = std::move(s);
            return true;
        }
        if (IsIdentStart(c)) {
            std::string s;
            bool dummy = false;
            if (!ParseTokenOrString(s, dummy)) return false;
            if (s == "true" || s == "false") {
                out.kind = AiDataDetail::BinTreeNode::Bool;
                out.typeLabel = annotPfx + L"bool";
                out.bval = (s == "true");
            } else if (s == "null" || s.rfind("null.", 0) == 0) {
                out.kind = AiDataDetail::BinTreeNode::Null;
                out.typeLabel = annotPfx + PathUtils::Utf8ToWide(s);
            } else {
                out.kind = AiDataDetail::BinTreeNode::Str;
                out.typeLabel = annotPfx + L"symbol";
                out.str = std::move(s);
            }
            return true;
        }
        // Numeric, decimal, or timestamp literal
        size_t s = pos;
        while (pos < sv.size()) {
            char ch = sv[pos];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
                ch == ',' || ch == '}' || ch == ']' || ch == ')') break;
            if (ch == '/' && pos + 1 < sv.size() && (sv[pos + 1] == '/' || sv[pos + 1] == '*')) break;
            ++pos;
        }
        if (pos == s) return false;
        std::string lit(sv.substr(s, pos - s));
        if (lit.find('T') != std::string::npos || (lit.size() == 10 && lit[4] == '-' && lit[7] == '-')) {
            out.kind = AiDataDetail::BinTreeNode::Str;
            out.typeLabel = annotPfx + L"timestamp";
            out.str = std::move(lit);
            return true;
        }
        if (lit.find('d') != std::string::npos || lit.find('D') != std::string::npos) {
            out.kind = AiDataDetail::BinTreeNode::Str;
            out.typeLabel = annotPfx + L"decimal";
            out.str = std::move(lit);
            return true;
        }
        char* endPtr = nullptr;
        if (lit.find('.') == std::string::npos && lit.find('e') == std::string::npos && lit.find('E') == std::string::npos) {
            long long iv = std::strtoll(lit.c_str(), &endPtr, 0);
            if (endPtr == lit.c_str() + lit.size()) {
                out.kind = AiDataDetail::BinTreeNode::Int;
                out.typeLabel = annotPfx + L"int";
                out.ival = iv;
                return true;
            }
        }
        double dv = std::strtod(lit.c_str(), &endPtr);
        if (endPtr == lit.c_str() + lit.size()) {
            out.kind = AiDataDetail::BinTreeNode::Float;
            out.typeLabel = annotPfx + L"float";
            out.fval = dv;
            return true;
        }
        return false;
    }
};

} // namespace DetailApacheIon

inline bool ParseApacheIonDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // 1. Amazon Ion 1.0 Binary (BVM: 0xE0 0x01 0x00 0xEA)
    if (raw.size() >= 4 && raw[0] == 0xE0u && raw[1] == 0x01u && raw[2] == 0x00u && raw[3] == 0xEAu) {
        DetailApacheIon::IonSymTab symTab;
        std::vector<AiDataDetail::BinTreeNode> roots;
        size_t pos = 0;
        while (pos < raw.size() && roots.size() < 2048) {
            if (pos + 4 <= raw.size() && raw[pos] == 0xE0u && raw[pos + 1] == 0x01u &&
                raw[pos + 2] == 0x00u && raw[pos + 3] == 0xEAu)
            {
                symTab.Reset();
                pos += 4;
                continue;
            }
            AiDataDetail::BinTreeNode node;
            std::vector<uint64_t> annots;
            int rc = DetailApacheIon::DecodeIonBinaryValue(raw.data(), raw.size(), pos, symTab, node, 0, &annots);
            if (rc < 0) {
                if (roots.empty() && symTab.localSyms.empty()) return false;
                break;
            }
            if (rc == 0) continue; // NOP pad
            if (!annots.empty() && annots[0] == 3u) { // $ion_symbol_table
                DetailApacheIon::ApplyLocalSymbolTableStruct(node, symTab);
                continue;
            }
            roots.push_back(std::move(node));
        }

        if (roots.empty() && symTab.localSyms.empty()) return false;

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Amazon Ion 1.0 Binary Document (" + extLower + L")";
        if (!roots.empty()) {
            AiDataDetail::PopulateSheetsFromBinTree(roots, L"Ion", out.sheets);
        }
        SpreadsheetSheet symSh;
        symSh.name = L"Ion_Symbols";
        symSh.rows.push_back({ L"Symbol ID", L"Scope", L"Symbol Text" });
        for (uint64_t i = 1; i <= 9; ++i) {
            symSh.rows.push_back({ L"$" + std::to_wstring(i), L"System ($ion_1_0)", PathUtils::Utf8ToWide(DetailApacheIon::SystemSymbolName(i)) });
        }
        for (size_t i = 0; i < symTab.localSyms.size(); ++i) {
            symSh.rows.push_back({ L"$" + std::to_wstring(10 + i), L"Local ($ion_symbol_table)", PathUtils::Utf8ToWide(symTab.localSyms[i]) });
        }
        FinalizeSheetCols(symSh);
        out.sheets.push_back(std::move(symSh));
        out.valid = true;
        return true;
    }

    // 2. Amazon Ion Text
    std::string_view sv;
    if (!Formats::FormatPrologue::PrepareUtf8TextNoNul(raw.data(), raw.size(), sv, 2u)) return false;
    DetailApacheIon::IonTextParser tp{ sv, 0 };
    std::vector<AiDataDetail::BinTreeNode> roots;
    while (tp.pos < tp.sv.size() && roots.size() < 2048) {
        tp.SkipWsAndComments();
        if (tp.pos >= tp.sv.size()) break;
        AiDataDetail::BinTreeNode v;
        if (!tp.ParseValue(v, 0)) {
            if (roots.empty()) return false;
            break;
        }
        if (v.kind == AiDataDetail::BinTreeNode::Str && v.str == "$ion_1_0") continue;
        if (v.typeLabel.rfind(L"$ion_symbol_table::", 0) == 0) continue;
        roots.push_back(std::move(v));
    }
    if (roots.empty()) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Amazon Ion Text Document (" + extLower + L")";
    AiDataDetail::PopulateSheetsFromBinTree(roots, L"Ion", out.sheets);
    out.valid = true;
    return true;
}

} // namespace Formats
