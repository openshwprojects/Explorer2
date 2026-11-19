#pragma once
#include "../../JsonDom.h"
// Avro.h — Apache Avro Object Container File (.avro, Obj\x01) Schema & Binary Data Block Decoder

namespace Formats {

namespace AvroDetail {

inline bool ReadZigZagI64(const uint8_t* data, size_t size, size_t& pos, int64_t& out) {
    uint64_t u = 0;
    int shift = 0;
    while (pos < size && shift < 70) {
        uint8_t b = data[pos++];
        u |= static_cast<uint64_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0) {
            out = static_cast<int64_t>((u >> 1) ^ (~(u & 1ULL) + 1ULL));
            return true;
        }
        shift += 7;
    }
    return false;
}

inline bool ReadAvroBytes(const uint8_t* data, size_t size, size_t& pos, std::string& out) {
    int64_t len = 0;
    if (!ReadZigZagI64(data, size, pos, len) || len < 0 || static_cast<uint64_t>(len) > size - pos) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(len));
    pos += static_cast<size_t>(len);
    return true;
}

using JsonNode = ::Explorer2::Json::Value; // avro.schema is parsed with the shared JSON reader


struct AvroType;

struct AvroField {
    std::string name;
    std::shared_ptr<AvroType> type;
};

struct AvroType {
    enum Kind {
        Null, Boolean, Int, Long, Float, Double, Bytes, String,
        Record, Enum, Array, Map, Fixed, Union
    } kind = Null;

    std::string name;
    size_t fixedSize = 0;
    std::vector<std::string> symbols;
    std::vector<AvroField> fields;
    std::shared_ptr<AvroType> elemType; // array items or map values
    std::vector<std::shared_ptr<AvroType>> branches;
};

inline std::wstring DescribeAvroType(const AvroType& t, int depth = 0) {
    if (depth > 4) return L"...";
    switch (t.kind) {
        case AvroType::Null:    return L"null";
        case AvroType::Boolean: return L"boolean";
        case AvroType::Int:     return L"int";
        case AvroType::Long:    return L"long";
        case AvroType::Float:   return L"float";
        case AvroType::Double:  return L"double";
        case AvroType::Bytes:   return L"bytes";
        case AvroType::String:  return L"string";
        case AvroType::Record:  return L"record<" + PathUtils::Utf8ToWide(t.name) + L">";
        case AvroType::Enum:    return L"enum<" + PathUtils::Utf8ToWide(t.name) + L">";
        case AvroType::Fixed:   return L"fixed[" + std::to_wstring(t.fixedSize) + L"]";
        case AvroType::Array:   return L"array<" + (t.elemType ? DescribeAvroType(*t.elemType, depth + 1) : L"?") + L">";
        case AvroType::Map:     return L"map<" + (t.elemType ? DescribeAvroType(*t.elemType, depth + 1) : L"?") + L">";
        case AvroType::Union: {
            std::wstring s = L"union[";
            for (size_t i = 0; i < t.branches.size(); ++i) {
                if (i) s += L", ";
                s += t.branches[i] ? DescribeAvroType(*t.branches[i], depth + 1) : L"?";
            }
            s.push_back(L']');
            return s;
        }
    }
    return L"unknown";
}

inline std::shared_ptr<AvroType> BuildAvroType(
    const JsonNode& node,
    std::map<std::string, std::shared_ptr<AvroType>>& namedTypes,
    const std::string& curNamespace = "",
    int depth = 0)
{
    if (depth > 32) return nullptr;

    if (node.kind == JsonNode::Kind::String) {
        const std::string& s = node.strVal;
        auto mk = [](AvroType::Kind k) {
            auto p = std::make_shared<AvroType>();
            p->kind = k;
            return p;
        };
        if (s == "null")    return mk(AvroType::Null);
        if (s == "boolean") return mk(AvroType::Boolean);
        if (s == "int")     return mk(AvroType::Int);
        if (s == "long")    return mk(AvroType::Long);
        if (s == "float")   return mk(AvroType::Float);
        if (s == "double")  return mk(AvroType::Double);
        if (s == "bytes")   return mk(AvroType::Bytes);
        if (s == "string")  return mk(AvroType::String);

        auto it = namedTypes.find(s);
        if (it != namedTypes.end()) return it->second;
        if (!curNamespace.empty()) {
            auto it2 = namedTypes.find(curNamespace + "." + s);
            if (it2 != namedTypes.end()) return it2->second;
        }
        return nullptr;
    }

    if (node.kind == JsonNode::Kind::Array) {
        if (node.arrVal.empty()) return nullptr;
        auto u = std::make_shared<AvroType>();
        u->kind = AvroType::Union;
        for (const auto& elem : node.arrVal) {
            auto br = BuildAvroType(elem, namedTypes, curNamespace, depth + 1);
            if (!br) return nullptr;
            u->branches.push_back(std::move(br));
        }
        return u;
    }

    if (node.kind == JsonNode::Kind::Object) {
        const JsonNode* tNode = node.Find("type");
        if (!tNode) return nullptr;
        if (tNode->kind != JsonNode::Kind::String) {
            return BuildAvroType(*tNode, namedTypes, curNamespace, depth + 1);
        }

        const std::string& tName = tNode->strVal;
        std::string ns = curNamespace;
        if (const JsonNode* nsNode = node.Find("namespace"); nsNode && nsNode->kind == JsonNode::Kind::String) {
            ns = nsNode->strVal;
        }
        std::string recName;
        if (const JsonNode* nNode = node.Find("name"); nNode && nNode->kind == JsonNode::Kind::String) {
            recName = nNode->strVal;
        }

        auto registerNamed = [&](const std::shared_ptr<AvroType>& tp) {
            if (!recName.empty()) {
                namedTypes[recName] = tp;
                if (!ns.empty() && recName.find('.') == std::string::npos) {
                    namedTypes[ns + "." + recName] = tp;
                }
            }
        };

        if (tName == "record" || tName == "error") {
            const JsonNode* fNode = node.Find("fields");
            if (!fNode || fNode->kind != JsonNode::Kind::Array) return nullptr;
            auto rec = std::make_shared<AvroType>();
            rec->kind = AvroType::Record;
            rec->name = recName;
            registerNamed(rec);
            for (const auto& fItem : fNode->arrVal) {
                const JsonNode* fn = fItem.Find("name");
                const JsonNode* ft = fItem.Find("type");
                if (!fn || fn->kind != JsonNode::Kind::String || !ft) return nullptr;
                auto fType = BuildAvroType(*ft, namedTypes, ns, depth + 1);
                if (!fType) return nullptr;
                rec->fields.push_back({ fn->strVal, std::move(fType) });
            }
            return rec;
        }
        if (tName == "enum") {
            const JsonNode* sNode = node.Find("symbols");
            if (!sNode || sNode->kind != JsonNode::Kind::Array) return nullptr;
            auto en = std::make_shared<AvroType>();
            en->kind = AvroType::Enum;
            en->name = recName;
            for (const auto& sym : sNode->arrVal) {
                if (sym.kind == JsonNode::Kind::String) en->symbols.push_back(sym.strVal);
            }
            registerNamed(en);
            return en;
        }
        if (tName == "array") {
            const JsonNode* itNode = node.Find("items");
            if (!itNode) return nullptr;
            auto arr = std::make_shared<AvroType>();
            arr->kind = AvroType::Array;
            arr->elemType = BuildAvroType(*itNode, namedTypes, ns, depth + 1);
            return arr->elemType ? arr : nullptr;
        }
        if (tName == "map") {
            const JsonNode* valNode = node.Find("values");
            if (!valNode) return nullptr;
            auto mp = std::make_shared<AvroType>();
            mp->kind = AvroType::Map;
            mp->elemType = BuildAvroType(*valNode, namedTypes, ns, depth + 1);
            return mp->elemType ? mp : nullptr;
        }
        if (tName == "fixed") {
            const JsonNode* szNode = node.Find("size");
            int64_t fixedSz = 0;
            if (!szNode || !szNode->AsInt64(fixedSz) || fixedSz <= 0 || fixedSz > 1048576) {
                return nullptr;
            }
            auto fx = std::make_shared<AvroType>();
            fx->kind = AvroType::Fixed;
            fx->name = recName;
            fx->fixedSize = static_cast<size_t>(fixedSz);
            registerNamed(fx);
            return fx;
        }

        // Logical type wrapper around primitive (e.g. {"type":"long","logicalType":"timestamp-millis"})
        JsonNode prim;
        prim.kind = JsonNode::Kind::String;
        prim.strVal = tName;
        return BuildAvroType(prim, namedTypes, ns, depth + 1);
    }

    return nullptr;
}

inline std::wstring HexPreview(const uint8_t* p, size_t n, size_t maxShow = 12) {
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring s = L"0x";
    size_t lim = std::min(n, maxShow);
    for (size_t i = 0; i < lim; ++i) {
        s.push_back(kHex[p[i] >> 4]);
        s.push_back(kHex[p[i] & 0x0F]);
    }
    if (n > lim) s += L"...";
    return s;
}

inline bool DecodeAvroValue(
    const AvroType& type,
    const uint8_t* data,
    size_t size,
    size_t& pos,
    std::wstring& outVal,
    int depth = 0)
{
    if (depth > 24) return false;
    switch (type.kind) {
        case AvroType::Null:
            outVal = L"NULL";
            return true;
        case AvroType::Boolean: {
            if (pos >= size || data[pos] > 1) return false;
            outVal = (data[pos++] != 0) ? L"true" : L"false";
            return true;
        }
        case AvroType::Int:
        case AvroType::Long: {
            int64_t v = 0;
            if (!ReadZigZagI64(data, size, pos, v)) return false;
            outVal = std::to_wstring(v);
            return true;
        }
        case AvroType::Float: {
            if (pos + 4 > size) return false;
            float f = 0.0f;
            std::memcpy(&f, data + pos, 4);
            pos += 4;
            wchar_t b[64] = {};
            swprintf_s(b, L"%.6g", f);
            outVal = b;
            return true;
        }
        case AvroType::Double: {
            if (pos + 8 > size) return false;
            double d = 0.0;
            std::memcpy(&d, data + pos, 8);
            pos += 8;
            wchar_t b[64] = {};
            swprintf_s(b, L"%.12g", d);
            outVal = b;
            return true;
        }
        case AvroType::Bytes: {
            std::string rawB;
            if (!ReadAvroBytes(data, size, pos, rawB)) return false;
            outVal = HexPreview(reinterpret_cast<const uint8_t*>(rawB.data()), rawB.size());
            return true;
        }
        case AvroType::String: {
            std::string s;
            if (!ReadAvroBytes(data, size, pos, s)) return false;
            outVal = PathUtils::Utf8ToWide(s);
            return true;
        }
        case AvroType::Enum: {
            int64_t idx = 0;
            if (!ReadZigZagI64(data, size, pos, idx) || idx < 0 || static_cast<size_t>(idx) >= type.symbols.size()) {
                return false;
            }
            outVal = PathUtils::Utf8ToWide(type.symbols[static_cast<size_t>(idx)]);
            return true;
        }
        case AvroType::Fixed: {
            if (pos + type.fixedSize > size) return false;
            outVal = HexPreview(data + pos, type.fixedSize);
            pos += type.fixedSize;
            return true;
        }
        case AvroType::Array: {
            if (!type.elemType) return false;
            std::wstring res = L"[";
            size_t shown = 0;
            for (;;) {
                int64_t cnt = 0;
                if (!ReadZigZagI64(data, size, pos, cnt)) return false;
                if (cnt == 0) break;
                if (cnt < 0) {
                    cnt = -cnt;
                    int64_t blkSz = 0;
                    if (!ReadZigZagI64(data, size, pos, blkSz)) return false;
                }
                if (cnt > 100000) return false;
                for (int64_t i = 0; i < cnt; ++i) {
                    std::wstring elem;
                    if (!DecodeAvroValue(*type.elemType, data, size, pos, elem, depth + 1)) return false;
                    if (shown < 16) {
                        if (shown > 0) res += L", ";
                        res += elem;
                    }
                    ++shown;
                }
            }
            if (shown > 16) res += L", ... (" + std::to_wstring(shown) + L")";
            res.push_back(L']');
            outVal = std::move(res);
            return true;
        }
        case AvroType::Map: {
            if (!type.elemType) return false;
            std::wstring res = L"{";
            size_t shown = 0;
            for (;;) {
                int64_t cnt = 0;
                if (!ReadZigZagI64(data, size, pos, cnt)) return false;
                if (cnt == 0) break;
                if (cnt < 0) {
                    cnt = -cnt;
                    int64_t blkSz = 0;
                    if (!ReadZigZagI64(data, size, pos, blkSz)) return false;
                }
                if (cnt > 100000) return false;
                for (int64_t i = 0; i < cnt; ++i) {
                    std::string k;
                    std::wstring v;
                    if (!ReadAvroBytes(data, size, pos, k)) return false;
                    if (!DecodeAvroValue(*type.elemType, data, size, pos, v, depth + 1)) return false;
                    if (shown < 12) {
                        if (shown > 0) res += L", ";
                        res += PathUtils::Utf8ToWide(k) + L": " + v;
                    }
                    ++shown;
                }
            }
            if (shown > 12) res += L", ...";
            res.push_back(L'}');
            outVal = std::move(res);
            return true;
        }
        case AvroType::Union: {
            int64_t branchIdx = 0;
            if (!ReadZigZagI64(data, size, pos, branchIdx) ||
                branchIdx < 0 || static_cast<size_t>(branchIdx) >= type.branches.size()) {
                return false;
            }
            return DecodeAvroValue(*type.branches[static_cast<size_t>(branchIdx)], data, size, pos, outVal, depth + 1);
        }
        case AvroType::Record: {
            std::wstring res = L"{";
            for (size_t i = 0; i < type.fields.size(); ++i) {
                std::wstring fv;
                if (!DecodeAvroValue(*type.fields[i].type, data, size, pos, fv, depth + 1)) return false;
                if (i > 0) res += L", ";
                res += PathUtils::Utf8ToWide(type.fields[i].name) + L": " + fv;
            }
            res.push_back(L'}');
            outVal = std::move(res);
            return true;
        }
    }
    return false;
}

} // namespace AvroDetail

inline bool ParseApacheAvro(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 16 || std::memcmp(raw.data(), "Obj\x01", 4) != 0) {
        return false;
    }

    std::string schemaJson;
    std::string codec = "null";
    uint8_t syncMarker[16] = {};
    bool hasBinaryOcf = false;
    size_t pos = 4;

    // 1. Parse standard Avro OCF metadata map + 16-byte sync marker
    {
        size_t mpos = 4;
        bool mapOk = true;
        std::string foundSchema;
        std::string foundCodec = "null";
        for (int mapBlocks = 0; mapBlocks < 32; ++mapBlocks) {
            int64_t cnt = 0;
            if (!AvroDetail::ReadZigZagI64(raw.data(), raw.size(), mpos, cnt)) { mapOk = false; break; }
            if (cnt == 0) break;
            if (cnt < 0) {
                cnt = -cnt;
                int64_t blkBytes = 0;
                if (!AvroDetail::ReadZigZagI64(raw.data(), raw.size(), mpos, blkBytes)) { mapOk = false; break; }
            }
            if (cnt > 4096) { mapOk = false; break; }
            for (int64_t i = 0; i < cnt; ++i) {
                std::string k, v;
                if (!AvroDetail::ReadAvroBytes(raw.data(), raw.size(), mpos, k) ||
                    !AvroDetail::ReadAvroBytes(raw.data(), raw.size(), mpos, v)) {
                    mapOk = false;
                    break;
                }
                if (k == "avro.schema") foundSchema = std::move(v);
                else if (k == "avro.codec" && !v.empty()) foundCodec = std::move(v);
            }
            if (!mapOk) break;
        }
        if (mapOk && !foundSchema.empty() && mpos + 16 <= raw.size()) {
            schemaJson = std::move(foundSchema);
            codec = std::move(foundCodec);
            std::memcpy(syncMarker, raw.data() + mpos, 16);
            pos = mpos + 16;
            hasBinaryOcf = true;
        }
    }

    // Fallback ONLY for unframed schema-only container ("Obj\x01avro.schema{...}" in testdata/device_events.avro)
    if (!hasBinaryOcf) {
        if (raw.size() > 16 && std::memcmp(raw.data() + 4, "avro.schema", 11) == 0) {
            schemaJson.assign(reinterpret_cast<const char*>(raw.data() + 15), raw.size() - 15);
            pos = raw.size();
        } else {
            return false;
        }
    }

    // 2. Parse avro.schema JSON into recursive AvroType tree
    AvroDetail::JsonNode rootJson;
    ::Explorer2::Json::Limits schemaLimits;
    schemaLimits.maxDepth = 32;
    if (!::Explorer2::Json::ParseDocument(schemaJson, rootJson, schemaLimits)) {
        return false;
    }

    std::map<std::string, std::shared_ptr<AvroDetail::AvroType>> namedTypes;
    auto rootType = AvroDetail::BuildAvroType(rootJson, namedTypes);
    if (!rootType) {
        return false;
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Apache Avro Container (.avro — codec: " + PathUtils::Utf8ToWide(codec) + L")";

    // 3. Decode OCF Data Blocks (up to 500 records)
    SpreadsheetSheet recSheet;
    recSheet.name = L"Avro_Records";
    bool isRootRecord = (rootType->kind == AvroDetail::AvroType::Record && !rootType->fields.empty());
    if (isRootRecord) {
        std::vector<std::wstring> hdr;
        hdr.reserve(rootType->fields.size());
        for (const auto& f : rootType->fields) hdr.push_back(PathUtils::Utf8ToWide(f.name));
        recSheet.maxCols = hdr.size();
        recSheet.rows.push_back(std::move(hdr));
    } else {
        recSheet.maxCols = 2;
        recSheet.rows.push_back({ L"#", L"Value" });
    }

    size_t totalBlocks = 0;
    size_t totalObjects = 0;
    constexpr size_t kMaxDecodedRecords = 500;

    if (hasBinaryOcf) {
        while (pos < raw.size() && recSheet.rows.size() - 1 < kMaxDecodedRecords) {
            int64_t objCount = 0;
            int64_t byteCount = 0;
            if (!AvroDetail::ReadZigZagI64(raw.data(), raw.size(), pos, objCount) ||
                !AvroDetail::ReadZigZagI64(raw.data(), raw.size(), pos, byteCount)) {
                break;
            }
            if (objCount < 0 || byteCount < 0 || static_cast<uint64_t>(byteCount) + 16ULL > raw.size() - pos) {
                if (totalBlocks == 0) return false;
                break;
            }
            const uint8_t* blkPtr = raw.data() + pos;
            size_t blkLen = static_cast<size_t>(byteCount);
            if (std::memcmp(blkPtr + blkLen, syncMarker, 16) != 0) {
                if (totalBlocks == 0) return false;
                break;
            }
            pos += blkLen + 16;
            ++totalBlocks;
            totalObjects += static_cast<size_t>(objCount);

            std::vector<uint8_t> decompressed;
            const uint8_t* decPtr = blkPtr;
            size_t decLen = blkLen;
            if (codec == "deflate") {
                if (!ZipDeflate::InflateRaw(blkPtr, blkLen, decompressed, 32 * 1024 * 1024)) break;
                decPtr = decompressed.data();
                decLen = decompressed.size();
            } else if (codec == "snappy") {
                if (blkLen < 4 || !ParquetDetail::SnappyDecompress(blkPtr, blkLen - 4, decompressed, 32 * 1024 * 1024)) break;
                decPtr = decompressed.data();
                decLen = decompressed.size();
            } else if (codec != "null" && !codec.empty()) {
                break;
            }

            size_t bpos = 0;
            for (int64_t r = 0; r < objCount && recSheet.rows.size() - 1 < kMaxDecodedRecords; ++r) {
                if (isRootRecord) {
                    std::vector<std::wstring> row;
                    row.reserve(rootType->fields.size());
                    bool okRow = true;
                    for (const auto& f : rootType->fields) {
                        std::wstring fv;
                        if (!AvroDetail::DecodeAvroValue(*f.type, decPtr, decLen, bpos, fv)) {
                            okRow = false;
                            break;
                        }
                        row.push_back(std::move(fv));
                    }
                    if (!okRow) break;
                    recSheet.rows.push_back(std::move(row));
                } else {
                    std::wstring v;
                    if (!AvroDetail::DecodeAvroValue(*rootType, decPtr, decLen, bpos, v)) break;
                    recSheet.rows.push_back({ std::to_wstring(recSheet.rows.size()), std::move(v) });
                }
            }
        }
    }

    // 4. Schema & Container Summary Sheet
    SpreadsheetSheet schemaSheet;
    schemaSheet.name = L"Avro_Schema";
    schemaSheet.maxCols = 3;
    schemaSheet.rows.push_back({ L"# / Metric", L"Field / Property", L"Avro Type / Value" });
    schemaSheet.rows.push_back({ L"Codec", L"avro.codec", PathUtils::Utf8ToWide(codec) });
    schemaSheet.rows.push_back({ L"Root Schema", PathUtils::Utf8ToWide(rootType->name.empty() ? "(root)" : rootType->name),
                                 AvroDetail::DescribeAvroType(*rootType) });
    if (hasBinaryOcf) {
        schemaSheet.rows.push_back({ L"Data Blocks", L"OCF Blocks / Objects",
            std::to_wstring(totalBlocks) + L" block(s), " + std::to_wstring(totalObjects) + L" object(s)" });
        schemaSheet.rows.push_back({ L"Sync Marker", L"16-byte Marker", AvroDetail::HexPreview(syncMarker, 16, 16) });
    }

    if (isRootRecord) {
        for (size_t i = 0; i < rootType->fields.size(); ++i) {
            schemaSheet.rows.push_back({
                std::to_wstring(i + 1),
                PathUtils::Utf8ToWide(rootType->fields[i].name),
                AvroDetail::DescribeAvroType(*rootType->fields[i].type)
            });
        }
    }

    if (recSheet.rows.size() >= 2) {
        out.sheets.push_back(std::move(recSheet));
    }
    out.sheets.push_back(std::move(schemaSheet));
    out.valid = true;
    return true;
}

} // namespace Formats
