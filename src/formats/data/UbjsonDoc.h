#pragma once
// UbjsonDoc.h — Universal Binary JSON Draft-12 (.ubj, .ubjson) Reader

namespace Formats {
namespace DetailUbjson {

inline bool ReadUbjsonInt(const uint8_t* d, size_t sz, size_t& pos, uint8_t marker, int64_t& outVal) {
    switch (marker) {
        case 'i':
            if (pos + 1 > sz) return false;
            outVal = static_cast<int8_t>(d[pos++]);
            return true;
        case 'U':
            if (pos + 1 > sz) return false;
            outVal = d[pos++];
            return true;
        case 'I':
            if (pos + 2 > sz) return false;
            outVal = AiDataDetail::RdI16BE(d + pos);
            pos += 2;
            return true;
        case 'l':
            if (pos + 4 > sz) return false;
            outVal = AiDataDetail::RdI32BE(d + pos);
            pos += 4;
            return true;
        case 'L':
            if (pos + 8 > sz) return false;
            outVal = AiDataDetail::RdI64BE(d + pos);
            pos += 8;
            return true;
        default:
            return false;
    }
}

inline bool ReadUbjsonLength(const uint8_t* d, size_t sz, size_t& pos, size_t& outLen) {
    if (pos >= sz) return false;
    uint8_t m = d[pos++];
    int64_t v = 0;
    if (!ReadUbjsonInt(d, sz, pos, m, v) || v < 0 || v > 64LL * 1024LL * 1024LL) return false;
    outLen = static_cast<size_t>(v);
    return true;
}

inline bool IsValidUbjsonTypeMarker(uint8_t m) {
    switch (m) {
        case 'Z': case 'N': case 'T': case 'F':
        case 'i': case 'U': case 'I': case 'l': case 'L':
        case 'd': case 'D': case 'H': case 'C': case 'S':
        case '[': case '{':
            return true;
        default:
            return false;
    }
}

inline bool DecodeUbjsonPayload(
    const uint8_t* d,
    size_t sz,
    size_t& pos,
    uint8_t marker,
    AiDataDetail::BinTreeNode& outNode,
    int depth);

inline bool DecodeUbjsonValue(
    const uint8_t* d,
    size_t sz,
    size_t& pos,
    AiDataDetail::BinTreeNode& outNode,
    int depth)
{
    if (depth > 32) return false;
    while (pos < sz) {
        uint8_t m = d[pos++];
        if (m == 'N') continue; // No-Op marker
        return DecodeUbjsonPayload(d, sz, pos, m, outNode, depth);
    }
    return false;
}

inline bool DecodeUbjsonPayload(
    const uint8_t* d,
    size_t sz,
    size_t& pos,
    uint8_t marker,
    AiDataDetail::BinTreeNode& outNode,
    int depth)
{
    if (depth > 32) return false;
    outNode = {};
    switch (marker) {
        case 'Z':
            outNode.kind = AiDataDetail::BinTreeNode::Null;
            outNode.typeLabel = L"null (Z)";
            return true;
        case 'N':
            outNode.kind = AiDataDetail::BinTreeNode::Null;
            outNode.typeLabel = L"noop (N)";
            return true;
        case 'T':
        case 'F':
            outNode.kind = AiDataDetail::BinTreeNode::Bool;
            outNode.typeLabel = L"bool";
            outNode.bval = (marker == 'T');
            return true;
        case 'i':
        case 'U':
        case 'I':
        case 'l':
        case 'L': {
            int64_t iv = 0;
            if (!ReadUbjsonInt(d, sz, pos, marker, iv)) return false;
            if (marker == 'U') {
                outNode.kind = AiDataDetail::BinTreeNode::UInt;
                outNode.typeLabel = L"uint8";
                outNode.uval = static_cast<uint64_t>(iv);
            } else {
                outNode.kind = AiDataDetail::BinTreeNode::Int;
                outNode.typeLabel = (marker == 'i') ? L"int8" : (marker == 'I' ? L"int16" : (marker == 'l' ? L"int32" : L"int64"));
                outNode.ival = iv;
            }
            return true;
        }
        case 'd':
            if (pos + 4 > sz) return false;
            outNode.kind = AiDataDetail::BinTreeNode::Float;
            outNode.typeLabel = L"float32";
            outNode.fval = AiDataDetail::RdF32BE(d + pos);
            pos += 4;
            return true;
        case 'D':
            if (pos + 8 > sz) return false;
            outNode.kind = AiDataDetail::BinTreeNode::Float;
            outNode.typeLabel = L"float64";
            outNode.fval = AiDataDetail::RdF64BE(d + pos);
            pos += 8;
            return true;
        case 'C':
            if (pos + 1 > sz || d[pos] > 0x7Fu) return false;
            outNode.kind = AiDataDetail::BinTreeNode::Str;
            outNode.typeLabel = L"char";
            outNode.str.assign(1, static_cast<char>(d[pos++]));
            return true;
        case 'S':
        case 'H': {
            size_t slen = 0;
            if (!ReadUbjsonLength(d, sz, pos, slen) || slen > sz - pos) return false;
            outNode.kind = AiDataDetail::BinTreeNode::Str;
            outNode.typeLabel = (marker == 'H') ? L"high_precision" : L"string";
            outNode.str.assign(reinterpret_cast<const char*>(d + pos), slen);
            pos += slen;
            return true;
        }
        case '[': {
            uint8_t fixedType = 0;
            bool hasCount = false;
            size_t count = 0;
            if (pos < sz && d[pos] == '$') {
                ++pos;
                if (pos >= sz || !IsValidUbjsonTypeMarker(d[pos])) return false;
                fixedType = d[pos++];
            }
            if (pos < sz && d[pos] == '#') {
                ++pos;
                if (!ReadUbjsonLength(d, sz, pos, count)) return false;
                hasCount = true;
            }
            if (fixedType != 0 && !hasCount) return false; // UBJSON spec: $ requires #

            // Optimize byte arrays [$U#N] or [$i#N] into compact Bytes node if N > 16
            if (hasCount && (fixedType == 'U' || fixedType == 'i') && count > 16) {
                if (count > sz - pos) return false;
                outNode.kind = AiDataDetail::BinTreeNode::Bytes;
                outNode.typeLabel = L"bytes[$" + std::wstring(1, static_cast<wchar_t>(fixedType)) + L"#" + std::to_wstring(count) + L"]";
                outNode.bytes.assign(d + pos, d + pos + std::min<size_t>(count, 64));
                pos += count;
                return true;
            }

            outNode.kind = AiDataDetail::BinTreeNode::Array;
            outNode.typeLabel = fixedType ? (L"array[$" + std::wstring(1, static_cast<wchar_t>(fixedType)) + L"]") : L"array";
            if (hasCount) {
                if ((fixedType == 'Z' || fixedType == 'N' || fixedType == 'T' || fixedType == 'F') && count > 4096) {
                    count = 4096;
                }
                for (size_t i = 0; i < count; ++i) {
                    AiDataDetail::BinTreeNode elem;
                    bool ok = fixedType ? DecodeUbjsonPayload(d, sz, pos, fixedType, elem, depth + 1)
                                        : DecodeUbjsonValue(d, sz, pos, elem, depth + 1);
                    if (!ok) return false;
                    if (outNode.children.size() < 4096) outNode.children.push_back(std::move(elem));
                }
                return true;
            }
            while (pos < sz) {
                if (d[pos] == 'N') { ++pos; continue; }
                if (d[pos] == ']') { ++pos; return true; }
                AiDataDetail::BinTreeNode elem;
                if (!DecodeUbjsonValue(d, sz, pos, elem, depth + 1)) return false;
                if (outNode.children.size() < 4096) outNode.children.push_back(std::move(elem));
            }
            return false;
        }
        case '{': {
            uint8_t fixedType = 0;
            bool hasCount = false;
            size_t count = 0;
            if (pos < sz && d[pos] == '$') {
                ++pos;
                if (pos >= sz || !IsValidUbjsonTypeMarker(d[pos])) return false;
                fixedType = d[pos++];
            }
            if (pos < sz && d[pos] == '#') {
                ++pos;
                if (!ReadUbjsonLength(d, sz, pos, count)) return false;
                hasCount = true;
            }
            if (fixedType != 0 && !hasCount) return false;

            outNode.kind = AiDataDetail::BinTreeNode::Map;
            outNode.typeLabel = fixedType ? (L"object[$" + std::wstring(1, static_cast<wchar_t>(fixedType)) + L"]") : L"object";
            auto readPair = [&]() -> bool {
                size_t klen = 0;
                if (!ReadUbjsonLength(d, sz, pos, klen) || klen > sz - pos || klen > 65536) return false;
                AiDataDetail::BinTreeNode keyNode;
                keyNode.kind = AiDataDetail::BinTreeNode::Str;
                keyNode.str.assign(reinterpret_cast<const char*>(d + pos), klen);
                pos += klen;

                AiDataDetail::BinTreeNode valNode;
                bool ok = fixedType ? DecodeUbjsonPayload(d, sz, pos, fixedType, valNode, depth + 1)
                                    : DecodeUbjsonValue(d, sz, pos, valNode, depth + 1);
                if (!ok) return false;
                if (outNode.entries.size() < 4096) outNode.entries.emplace_back(std::move(keyNode), std::move(valNode));
                return true;
            };
            if (hasCount) {
                if (count > 1000000) return false;
                for (size_t i = 0; i < count; ++i) {
                    if (!readPair()) return false;
                }
                return true;
            }
            while (pos < sz) {
                if (d[pos] == 'N') { ++pos; continue; }
                if (d[pos] == '}') { ++pos; return true; }
                if (!readPair()) return false;
            }
            return false;
        }
        default:
            return false;
    }
}

} // namespace DetailUbjson

inline bool ParseUbjsonDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.empty()) return false;
    size_t pos = 0;
    AiDataDetail::BinTreeNode root;
    if (!DetailUbjson::DecodeUbjsonValue(raw.data(), raw.size(), pos, root, 0)) return false;
    while (pos < raw.size() && raw[pos] == 'N') ++pos;
    // Reject trailing garbage if root is a scalar and >4 unparsed bytes remain
    if (pos < raw.size() && root.kind != AiDataDetail::BinTreeNode::Map && root.kind != AiDataDetail::BinTreeNode::Array) {
        return false;
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Universal Binary JSON Document (" + extLower + L")";
    std::vector<AiDataDetail::BinTreeNode> roots;
    roots.push_back(std::move(root));
    AiDataDetail::PopulateSheetsFromBinTree(roots, L"UBJSON", out.sheets);
    out.valid = true;
    return true;
}

} // namespace Formats
