#pragma once
// CborDoc.h — RFC 8949 CBOR (.cbor) Binary Object Reader

namespace Formats {
namespace AiDataDetail {

// --- RFC 8949 CBOR Decoder ---
struct CborDecoder {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    size_t totalItems = 0;

    bool ReadArg(uint8_t ai, uint64_t& val, bool& indef) {
        indef = false;
        val = 0;
        if (ai < 24) { val = ai; return true; }
        if (ai == 24) { if (pos + 1 > size) return false; val = data[pos++]; return true; }
        if (ai == 25) { if (pos + 2 > size) return false; val = RdU16BE(data + pos); pos += 2; return true; }
        if (ai == 26) { if (pos + 4 > size) return false; val = RdU32BE(data + pos); pos += 4; return true; }
        if (ai == 27) { if (pos + 8 > size) return false; val = RdU64BE(data + pos); pos += 8; return true; }
        if (ai == 31) { indef = true; return true; }
        return false; // 28..30 reserved/invalid in RFC 8949
    }

    bool Decode(BinTreeNode& out, int depth = 0) {
        if (pos >= size || depth > 48 || ++totalItems > 50000) return false;
        uint8_t ib = data[pos++];
        uint8_t major = ib >> 5;
        uint8_t ai = ib & 0x1Fu;

        uint64_t arg = 0;
        bool indef = false;
        if (!ReadArg(ai, arg, indef)) return false;

        switch (major) {
            case 0: // Unsigned integer
                if (indef) return false;
                out.kind = BinTreeNode::UInt;
                out.typeLabel = L"uint";
                out.uval = arg;
                return true;

            case 1: // Negative integer (-1 - arg)
                if (indef) return false;
                out.kind = BinTreeNode::Int;
                out.typeLabel = L"nint";
                out.ival = (arg <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                    ? (-1 - static_cast<int64_t>(arg))
                    : std::numeric_limits<int64_t>::min();
                return true;

            case 2: // Byte string
                out.kind = BinTreeNode::Bytes;
                out.typeLabel = L"bstr";
                if (!indef) {
                    if (arg > size - pos) return false;
                    out.bytes.assign(data + pos, data + pos + static_cast<size_t>(arg));
                    pos += static_cast<size_t>(arg);
                    return true;
                }
                while (pos < size) {
                    if (data[pos] == 0xFFu) { ++pos; return true; }
                    BinTreeNode chunk;
                    if (!Decode(chunk, depth + 1) || chunk.kind != BinTreeNode::Bytes) return false;
                    out.bytes.insert(out.bytes.end(), chunk.bytes.begin(), chunk.bytes.end());
                }
                return false;

            case 3: // UTF-8 text string
                out.kind = BinTreeNode::Str;
                out.typeLabel = L"tstr";
                if (!indef) {
                    if (arg > size - pos) return false;
                    out.str.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(arg));
                    pos += static_cast<size_t>(arg);
                    return true;
                }
                while (pos < size) {
                    if (data[pos] == 0xFFu) { ++pos; return true; }
                    BinTreeNode chunk;
                    if (!Decode(chunk, depth + 1) || chunk.kind != BinTreeNode::Str) return false;
                    out.str += chunk.str;
                }
                return false;

            case 4: // Array
                out.kind = BinTreeNode::Array;
                out.typeLabel = L"array";
                if (!indef) {
                    if (arg > 65536) return false;
                    out.children.reserve(static_cast<size_t>(std::min<uint64_t>(arg, 256)));
                    for (uint64_t i = 0; i < arg; ++i) {
                        BinTreeNode elem;
                        if (!Decode(elem, depth + 1)) return false;
                        out.children.push_back(std::move(elem));
                    }
                    return true;
                }
                while (pos < size) {
                    if (data[pos] == 0xFFu) { ++pos; return true; }
                    BinTreeNode elem;
                    if (!Decode(elem, depth + 1)) return false;
                    out.children.push_back(std::move(elem));
                }
                return false;

            case 5: // Map
                out.kind = BinTreeNode::Map;
                out.typeLabel = L"map";
                if (!indef) {
                    if (arg > 65536) return false;
                    for (uint64_t i = 0; i < arg; ++i) {
                        BinTreeNode k, v;
                        if (!Decode(k, depth + 1) || !Decode(v, depth + 1)) return false;
                        out.entries.emplace_back(std::move(k), std::move(v));
                    }
                    return true;
                }
                while (pos < size) {
                    if (data[pos] == 0xFFu) { ++pos; return true; }
                    BinTreeNode k, v;
                    if (!Decode(k, depth + 1) || !Decode(v, depth + 1)) return false;
                    out.entries.emplace_back(std::move(k), std::move(v));
                }
                return false;

            case 6: { // Semantic tag
                if (indef) return false;
                BinTreeNode inner;
                if (!Decode(inner, depth + 1)) return false;
                std::wstring tagLbl = L"tag(" + std::to_wstring(arg) + L"):" + inner.typeLabel;
                out = std::move(inner);
                out.typeLabel = tagLbl;
                return true;
            }

            case 7: // Float / Simple
                if (indef) return false; // Unmatched break byte at top level
                if (ai == 20) { out.kind = BinTreeNode::Bool; out.typeLabel = L"bool"; out.bval = false; return true; }
                if (ai == 21) { out.kind = BinTreeNode::Bool; out.typeLabel = L"bool"; out.bval = true;  return true; }
                if (ai == 22) { out.kind = BinTreeNode::Null; out.typeLabel = L"null"; return true; }
                if (ai == 23) { out.kind = BinTreeNode::Null; out.typeLabel = L"undefined"; return true; }
                if (ai == 25) {
                    out.kind = BinTreeNode::Float;
                    out.typeLabel = L"float16";
                    out.fval = static_cast<double>(HalfToFloat(static_cast<uint16_t>(arg)));
                    return true;
                }
                if (ai == 26) {
                    uint32_t u = static_cast<uint32_t>(arg);
                    float f = 0.0f;
                    std::memcpy(&f, &u, 4);
                    out.kind = BinTreeNode::Float;
                    out.typeLabel = L"float32";
                    out.fval = static_cast<double>(f);
                    return true;
                }
                if (ai == 27) {
                    double d = 0.0;
                    std::memcpy(&d, &arg, 8);
                    out.kind = BinTreeNode::Float;
                    out.typeLabel = L"float64";
                    out.fval = d;
                    return true;
                }
                if (ai <= 24) {
                    out.kind = BinTreeNode::UInt;
                    out.typeLabel = L"simple";
                    out.uval = arg;
                    return true;
                }
                return false;
        }
        return false;
    }
};

inline bool ParseCborDocument(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.empty()) return false;
    CborDecoder dec{ raw.data(), raw.size(), 0, 0 };
    std::vector<BinTreeNode> roots;
    while (dec.pos < dec.size && roots.size() < 256) {
        BinTreeNode n;
        if (!dec.Decode(n, 0)) return false;
        roots.push_back(std::move(n));
    }
    if (roots.empty() || dec.pos != dec.size) return false;
    // Reject accidental multi-scalar ASCII text masquerading as CBOR integers/strings
    if (roots.size() > 1 && roots[0].kind != BinTreeNode::Map && roots[0].kind != BinTreeNode::Array) {
        return false;
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"RFC 8949 Concise Binary Object Representation (.cbor)";
    PopulateSheetsFromBinTree(roots, L"CBOR", out.sheets);
    out.valid = !out.sheets.empty();
    return out.valid;
}

} // namespace AiDataDetail

inline bool ParseCborDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return AiDataDetail::ParseCborDocument(raw, out); }

} // namespace Formats
