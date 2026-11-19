#pragma once
// MessagePackDoc.h — MessagePack (.msgpack / .mpk) Binary Object Reader

namespace Formats {
namespace AiDataDetail {

// --- MessagePack Decoder ---
struct MsgPackDecoder {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    size_t totalItems = 0;

    bool Decode(BinTreeNode& out, int depth = 0) {
        if (pos >= size || depth > 48 || ++totalItems > 50000) return false;
        uint8_t b = data[pos++];

        if (b <= 0x7Fu) {
            out.kind = BinTreeNode::UInt; out.typeLabel = L"fixint"; out.uval = b; return true;
        }
        if (b >= 0xE0u) {
            out.kind = BinTreeNode::Int; out.typeLabel = L"nfixint"; out.ival = static_cast<int8_t>(b); return true;
        }
        if ((b & 0xF0u) == 0x80u) return DecodeMap(b & 0x0Fu, out, depth);
        if ((b & 0xF0u) == 0x90u) return DecodeArray(b & 0x0Fu, out, depth);
        if ((b & 0xE0u) == 0xA0u) return DecodeStr(b & 0x1Fu, out);

        switch (b) {
            case 0xC0: out.kind = BinTreeNode::Null; out.typeLabel = L"nil"; return true;
            case 0xC1: return false; // Never-used opcode in MessagePack spec!
            case 0xC2: out.kind = BinTreeNode::Bool; out.typeLabel = L"bool"; out.bval = false; return true;
            case 0xC3: out.kind = BinTreeNode::Bool; out.typeLabel = L"bool"; out.bval = true;  return true;

            case 0xC4: if (pos + 1 > size) return false; return DecodeBin(data[pos++], out);
            case 0xC5: if (pos + 2 > size) return false; { uint16_t n = RdU16BE(data + pos); pos += 2; return DecodeBin(n, out); }
            case 0xC6: if (pos + 4 > size) return false; { uint32_t n = RdU32BE(data + pos); pos += 4; return DecodeBin(n, out); }

            case 0xC7: if (pos + 1 > size) return false; { uint8_t n = data[pos++]; return DecodeExt(n, out); }
            case 0xC8: if (pos + 2 > size) return false; { uint16_t n = RdU16BE(data + pos); pos += 2; return DecodeExt(n, out); }
            case 0xC9: if (pos + 4 > size) return false; { uint32_t n = RdU32BE(data + pos); pos += 4; return DecodeExt(n, out); }

            case 0xCA:
                if (pos + 4 > size) return false;
                out.kind = BinTreeNode::Float; out.typeLabel = L"float32"; out.fval = RdF32BE(data + pos); pos += 4; return true;
            case 0xCB:
                if (pos + 8 > size) return false;
                out.kind = BinTreeNode::Float; out.typeLabel = L"float64"; out.fval = RdF64BE(data + pos); pos += 8; return true;

            case 0xCC: if (pos + 1 > size) return false; out.kind = BinTreeNode::UInt; out.typeLabel = L"uint8";  out.uval = data[pos++]; return true;
            case 0xCD: if (pos + 2 > size) return false; out.kind = BinTreeNode::UInt; out.typeLabel = L"uint16"; out.uval = RdU16BE(data + pos); pos += 2; return true;
            case 0xCE: if (pos + 4 > size) return false; out.kind = BinTreeNode::UInt; out.typeLabel = L"uint32"; out.uval = RdU32BE(data + pos); pos += 4; return true;
            case 0xCF: if (pos + 8 > size) return false; out.kind = BinTreeNode::UInt; out.typeLabel = L"uint64"; out.uval = RdU64BE(data + pos); pos += 8; return true;

            case 0xD0: if (pos + 1 > size) return false; out.kind = BinTreeNode::Int; out.typeLabel = L"int8";  out.ival = static_cast<int8_t>(data[pos++]); return true;
            case 0xD1: if (pos + 2 > size) return false; out.kind = BinTreeNode::Int; out.typeLabel = L"int16"; out.ival = RdI16BE(data + pos); pos += 2; return true;
            case 0xD2: if (pos + 4 > size) return false; out.kind = BinTreeNode::Int; out.typeLabel = L"int32"; out.ival = RdI32BE(data + pos); pos += 4; return true;
            case 0xD3: if (pos + 8 > size) return false; out.kind = BinTreeNode::Int; out.typeLabel = L"int64"; out.ival = RdI64BE(data + pos); pos += 8; return true;

            case 0xD4: return DecodeExt(1, out);
            case 0xD5: return DecodeExt(2, out);
            case 0xD6: return DecodeExt(4, out);
            case 0xD7: return DecodeExt(8, out);
            case 0xD8: return DecodeExt(16, out);

            case 0xD9: if (pos + 1 > size) return false; return DecodeStr(data[pos++], out);
            case 0xDA: if (pos + 2 > size) return false; { uint16_t n = RdU16BE(data + pos); pos += 2; return DecodeStr(n, out); }
            case 0xDB: if (pos + 4 > size) return false; { uint32_t n = RdU32BE(data + pos); pos += 4; return DecodeStr(n, out); }

            case 0xDC: if (pos + 2 > size) return false; { uint16_t n = RdU16BE(data + pos); pos += 2; return DecodeArray(n, out, depth); }
            case 0xDD: if (pos + 4 > size) return false; { uint32_t n = RdU32BE(data + pos); pos += 4; return DecodeArray(n, out, depth); }

            case 0xDE: if (pos + 2 > size) return false; { uint16_t n = RdU16BE(data + pos); pos += 2; return DecodeMap(n, out, depth); }
            case 0xDF: if (pos + 4 > size) return false; { uint32_t n = RdU32BE(data + pos); pos += 4; return DecodeMap(n, out, depth); }
        }
        return false;
    }

    bool DecodeStr(uint32_t n, BinTreeNode& out) {
        if (n > size - pos) return false;
        out.kind = BinTreeNode::Str;
        out.typeLabel = L"str";
        out.str.assign(reinterpret_cast<const char*>(data + pos), n);
        pos += n;
        return true;
    }

    bool DecodeBin(uint32_t n, BinTreeNode& out) {
        if (n > size - pos) return false;
        out.kind = BinTreeNode::Bytes;
        out.typeLabel = L"bin";
        out.bytes.assign(data + pos, data + pos + n);
        pos += n;
        return true;
    }

    bool DecodeExt(uint32_t n, BinTreeNode& out) {
        if (pos >= size || n > size - pos - 1) return false;
        int8_t extType = static_cast<int8_t>(data[pos++]);
        out.kind = BinTreeNode::Bytes;
        out.typeLabel = L"ext(" + std::to_wstring(extType) + L")";
        out.bytes.assign(data + pos, data + pos + n);
        pos += n;
        return true;
    }

    bool DecodeArray(uint32_t n, BinTreeNode& out, int depth) {
        if (n > 65536) return false;
        out.kind = BinTreeNode::Array;
        out.typeLabel = L"array";
        out.children.reserve(std::min<uint32_t>(n, 256));
        for (uint32_t i = 0; i < n; ++i) {
            BinTreeNode e;
            if (!Decode(e, depth + 1)) return false;
            out.children.push_back(std::move(e));
        }
        return true;
    }

    bool DecodeMap(uint32_t n, BinTreeNode& out, int depth) {
        if (n > 65536) return false;
        out.kind = BinTreeNode::Map;
        out.typeLabel = L"map";
        for (uint32_t i = 0; i < n; ++i) {
            BinTreeNode k, v;
            if (!Decode(k, depth + 1) || !Decode(v, depth + 1)) return false;
            out.entries.emplace_back(std::move(k), std::move(v));
        }
        return true;
    }
};

inline bool ParseMsgPackDocument(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.empty()) return false;
    MsgPackDecoder dec{ raw.data(), raw.size(), 0, 0 };
    std::vector<BinTreeNode> roots;
    while (dec.pos < dec.size && roots.size() < 256) {
        BinTreeNode n;
        if (!dec.Decode(n, 0)) return false;
        roots.push_back(std::move(n));
    }
    if (roots.empty() || dec.pos != dec.size) return false;
    // Plain ASCII text consists of bytes 0x20..0x7E which are positive fixints in MsgPack; reject multi-fixint ASCII!
    if (roots.size() > 1 && roots[0].kind != BinTreeNode::Map && roots[0].kind != BinTreeNode::Array) {
        return false;
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"MessagePack Binary Object (" + extLower + L")";
    PopulateSheetsFromBinTree(roots, L"MsgPack", out.sheets);
    out.valid = !out.sheets.empty();
    return out.valid;
}

} // namespace AiDataDetail

inline bool ParseMessagePackDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) { return AiDataDetail::ParseMsgPackDocument(raw, extLower, out); }

} // namespace Formats
