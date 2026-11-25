#pragma once
// PaintDotNetPdn.h — Zero-dependency C++20 paint.net Multi-Layer Image (.pdn) reader ([MS-NRBF] + chunked GZip + 14 blend modes).

namespace Formats {

namespace DetailPdn {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU24LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU64BE;

struct NrbfValue {
    enum class Kind : uint8_t { Null, Bool, Int, Float, String, ObjectRef, InlineObjId };
    Kind kind = Kind::Null;
    bool boolVal = false;
    int64_t intVal = 0;
    double floatVal = 0.0;
    std::string strVal;
    int32_t refId = 0;
};

struct NrbfClassSchema {
    int32_t classId = 0;
    std::string className;
    std::vector<std::string> memberNames;
    bool hasTypeInfo = false;
    std::vector<uint8_t> binaryTypes;    // 0=Primitive, 1=String, 2=Object, 3=SystemClass, 4=Class, 5=ObjectArray, 6=StringArray, 7=PrimitiveArray
    std::vector<uint8_t> primSubtypes;   // PrimitiveTypeEnumeration when binaryType == 0 or 7
};

struct NrbfObject {
    enum class ObjKind : uint8_t { ClassInstance, ObjectArray, PrimitiveArray, StringObj };
    ObjKind objKind = ObjKind::ClassInstance;
    int32_t objectId = 0;
    std::string className;
    std::vector<std::pair<std::string, NrbfValue>> fields;
    std::vector<NrbfValue> arrayItems;
    std::string strValue;

    const NrbfValue* FindField(std::string_view key) const {
        for (const auto& kv : fields) {
            if (kv.first == key) return &kv.second;
        }
        // Fallback suffix match for "BaseClass+memberName"
        for (const auto& kv : fields) {
            size_t plus = kv.first.rfind('+');
            if (plus != std::string::npos && std::string_view(kv.first).substr(plus + 1) == key) {
                return &kv.second;
            }
        }
        return nullptr;
    }
};

class NrbfReader {
public:
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    int32_t rootId = 0;
    bool reachedEnd = false;
    std::unordered_map<int32_t, NrbfClassSchema> schemas;
    std::unordered_map<int32_t, NrbfObject> objects;

    NrbfReader(const uint8_t* d, size_t s, size_t startPos) : data(d), size(s), pos(startPos) {}

    bool ReadU8(uint8_t& out) {
        if (pos >= size) return false;
        out = data[pos++];
        return true;
    }
    bool ReadI32(int32_t& out) {
        if (pos + 4 > size) return false;
        out = static_cast<int32_t>(RdU32LE(data + pos));
        pos += 4;
        return true;
    }
    bool ReadI64(int64_t& out) {
        if (pos + 8 > size) return false;
        out = static_cast<int64_t>(RdU64LE(data + pos));
        pos += 8;
        return true;
    }
    bool ReadLPString(std::string& out) {
        uint32_t len = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            uint8_t b = 0;
            if (!ReadU8(b)) return false;
            len |= static_cast<uint32_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0) {
                if (len > size - pos || len > 16u * 1024u * 1024u) return false;
                out.assign(reinterpret_cast<const char*>(data + pos), len);
                pos += len;
                return true;
            }
        }
        return false;
    }

    bool ReadPrimitive(uint8_t primType, NrbfValue& out) {
        out = {};
        switch (primType) {
            case 1: { // Boolean
                uint8_t b = 0;
                if (!ReadU8(b)) return false;
                out.kind = NrbfValue::Kind::Bool;
                out.boolVal = (b != 0);
                out.intVal = out.boolVal ? 1 : 0;
                return true;
            }
            case 2: { // Byte
                uint8_t b = 0;
                if (!ReadU8(b)) return false;
                out.kind = NrbfValue::Kind::Int;
                out.intVal = b;
                return true;
            }
            case 3: { // Char (UTF-8 encoded single codepoint, 1..4 bytes)
                uint8_t b0 = 0;
                if (!ReadU8(b0)) return false;
                size_t extra = 0;
                if ((b0 & 0x80u) == 0) extra = 0;
                else if ((b0 & 0xE0u) == 0xC0u) extra = 1;
                else if ((b0 & 0xF0u) == 0xE0u) extra = 2;
                else if ((b0 & 0xF8u) == 0xF0u) extra = 3;
                if (pos + extra > size) return false;
                pos += extra;
                out.kind = NrbfValue::Kind::Int;
                out.intVal = b0;
                return true;
            }
            case 5: { // Decimal (LPString)
                out.kind = NrbfValue::Kind::String;
                return ReadLPString(out.strVal);
            }
            case 6: { // Double
                if (pos + 8 > size) return false;
                uint64_t u = RdU64LE(data + pos);
                pos += 8;
                double d = 0.0;
                std::memcpy(&d, &u, 8);
                out.kind = NrbfValue::Kind::Float;
                out.floatVal = d;
                return true;
            }
            case 7: { // Int16
                if (pos + 2 > size) return false;
                int16_t v = static_cast<int16_t>(RdU16LE(data + pos));
                pos += 2;
                out.kind = NrbfValue::Kind::Int;
                out.intVal = v;
                return true;
            }
            case 8: { // Int32
                int32_t v = 0;
                if (!ReadI32(v)) return false;
                out.kind = NrbfValue::Kind::Int;
                out.intVal = v;
                return true;
            }
            case 9:   // Int64
            case 12:  // TimeSpan
            case 13: {// DateTime
                int64_t v = 0;
                if (!ReadI64(v)) return false;
                out.kind = NrbfValue::Kind::Int;
                out.intVal = v;
                return true;
            }
            case 10: { // SByte
                uint8_t b = 0;
                if (!ReadU8(b)) return false;
                out.kind = NrbfValue::Kind::Int;
                out.intVal = static_cast<int8_t>(b);
                return true;
            }
            case 11: { // Single
                if (pos + 4 > size) return false;
                uint32_t u = RdU32LE(data + pos);
                pos += 4;
                float f = 0.0f;
                std::memcpy(&f, &u, 4);
                out.kind = NrbfValue::Kind::Float;
                out.floatVal = f;
                return true;
            }
            case 14: { // UInt16
                if (pos + 2 > size) return false;
                uint16_t v = RdU16LE(data + pos);
                pos += 2;
                out.kind = NrbfValue::Kind::Int;
                out.intVal = v;
                return true;
            }
            case 15: { // UInt32
                if (pos + 4 > size) return false;
                uint32_t v = RdU32LE(data + pos);
                pos += 4;
                out.kind = NrbfValue::Kind::Int;
                out.intVal = v;
                return true;
            }
            case 16: { // UInt64
                if (pos + 8 > size) return false;
                uint64_t v = RdU64LE(data + pos);
                pos += 8;
                out.kind = NrbfValue::Kind::Int;
                out.intVal = static_cast<int64_t>(v);
                return true;
            }
            case 17: { // Null
                out.kind = NrbfValue::Kind::Null;
                return true;
            }
            case 18: { // String
                out.kind = NrbfValue::Kind::String;
                return ReadLPString(out.strVal);
            }
            default:
                return false;
        }
    }

    bool SkipPrimitiveArrayBytes(uint8_t primType, uint64_t count) {
        size_t elemSz = 0;
        switch (primType) {
            case 1: case 2: case 10: elemSz = 1; break;
            case 7: case 14:         elemSz = 2; break;
            case 8: case 11: case 15: elemSz = 4; break;
            case 6: case 9: case 12: case 13: case 16: elemSz = 8; break;
            case 3: case 5: case 18: {
                NrbfValue tmp;
                for (uint64_t i = 0; i < count; ++i) {
                    if (!ReadPrimitive(primType, tmp)) return false;
                }
                return true;
            }
            case 17: return true;
            default: return false;
        }
        if (count > (size - pos) / elemSz) return false;
        pos += static_cast<size_t>(count) * elemSz;
        return true;
    }

    bool ReadAdditionalTypeInfo(uint8_t binType, uint8_t& outPrimSub) {
        outPrimSub = 0;
        if (binType == 0 || binType == 7) {
            return ReadU8(outPrimSub);
        }
        if (binType == 3) {
            std::string sysName;
            return ReadLPString(sysName);
        }
        if (binType == 4) {
            std::string clsName;
            int32_t libId = 0;
            return ReadLPString(clsName) && ReadI32(libId);
        }
        if (binType == 1 || binType == 2 || binType == 5 || binType == 6) {
            return true;
        }
        return false;
    }

    bool ReadClassInfo(NrbfClassSchema& schema) {
        int32_t objId = 0, memCount = 0;
        if (!ReadI32(objId) || !ReadLPString(schema.className) || !ReadI32(memCount)) return false;
        if (memCount < 0 || memCount > 65536) return false;
        schema.classId = objId;
        schema.memberNames.resize(static_cast<size_t>(memCount));
        for (int32_t i = 0; i < memCount; ++i) {
            if (!ReadLPString(schema.memberNames[static_cast<size_t>(i)])) return false;
        }
        return true;
    }

    bool ReadMemberTypeInfo(NrbfClassSchema& schema) {
        size_t n = schema.memberNames.size();
        schema.hasTypeInfo = true;
        schema.binaryTypes.resize(n);
        schema.primSubtypes.resize(n, 0);
        for (size_t i = 0; i < n; ++i) {
            if (!ReadU8(schema.binaryTypes[i])) return false;
        }
        for (size_t i = 0; i < n; ++i) {
            if (!ReadAdditionalTypeInfo(schema.binaryTypes[i], schema.primSubtypes[i])) return false;
        }
        return true;
    }

    enum class RecResult : uint8_t { Ok, Library, NullMult, End, Error };

    RecResult ReadRecord(NrbfValue& outVal, uint32_t& outNullCount, int depth) {
        outVal = {};
        outNullCount = 0;
        if (depth > 64) return RecResult::Error;
        uint8_t recType = 0;
        if (!ReadU8(recType)) return RecResult::Error;

        switch (recType) {
            case 0: { // SerializedStreamHeader
                int32_t rId = 0, hId = 0, maj = 0, minv = 0;
                if (!ReadI32(rId) || !ReadI32(hId) || !ReadI32(maj) || !ReadI32(minv)) return RecResult::Error;
                if (maj != 1 || minv != 0) return RecResult::Error;
                rootId = rId;
                return RecResult::Ok;
            }
            case 1:   // ClassWithId
            case 2:   // SystemClassWithMembers
            case 3:   // ClassWithMembers
            case 4:   // SystemClassWithMembersAndTypes
            case 5: { // ClassWithMembersAndTypes
                int32_t objectId = 0;
                NrbfClassSchema localSchema;
                const NrbfClassSchema* schemaPtr = nullptr;

                if (recType == 1) {
                    int32_t metaId = 0;
                    if (!ReadI32(objectId) || !ReadI32(metaId)) return RecResult::Error;
                    auto it = schemas.find(metaId);
                    if (it == schemas.end()) return RecResult::Error;
                    schemaPtr = &it->second;
                } else {
                    if (!ReadClassInfo(localSchema)) return RecResult::Error;
                    objectId = localSchema.classId;
                    if (recType == 4 || recType == 5) {
                        if (!ReadMemberTypeInfo(localSchema)) return RecResult::Error;
                    }
                    if (recType == 3 || recType == 5) {
                        int32_t libId = 0;
                        if (!ReadI32(libId)) return RecResult::Error;
                    }
                    schemas[objectId] = localSchema;
                    schemaPtr = &schemas[objectId];
                }

                // Copy schema fields safely before recursive ReadRecord might insert into `schemas`!
                const std::string clsName = schemaPtr->className;
                const std::vector<std::string> memNames = schemaPtr->memberNames;
                const bool hasTypes = schemaPtr->hasTypeInfo;
                const std::vector<uint8_t> binTypes = schemaPtr->binaryTypes;
                const std::vector<uint8_t> primSubs = schemaPtr->primSubtypes;

                NrbfObject obj;
                obj.objKind = NrbfObject::ObjKind::ClassInstance;
                obj.objectId = objectId;
                obj.className = clsName;
                obj.fields.resize(memNames.size());
                for (size_t i = 0; i < memNames.size(); ++i) obj.fields[i].first = memNames[i];

                size_t idx = 0;
                while (idx < memNames.size()) {
                    uint8_t bType = hasTypes ? binTypes[idx] : 2u;
                    if (bType == 0) {
                        if (!ReadPrimitive(primSubs[idx], obj.fields[idx].second)) return RecResult::Error;
                        ++idx;
                    } else {
                        NrbfValue mv;
                        uint32_t nullMult = 0;
                        RecResult rr = ReadRecord(mv, nullMult, depth + 1);
                        if (rr == RecResult::Library) continue;
                        if (rr == RecResult::NullMult) {
                            if (nullMult > memNames.size() - idx) return RecResult::Error;
                            idx += nullMult;
                            continue;
                        }
                        if (rr != RecResult::Ok) return RecResult::Error;
                        obj.fields[idx].second = std::move(mv);
                        ++idx;
                    }
                }

                objects[objectId] = std::move(obj);
                outVal.kind = NrbfValue::Kind::InlineObjId;
                outVal.refId = objectId;
                return RecResult::Ok;
            }
            case 6: { // BinaryObjectString
                int32_t objId = 0;
                std::string s;
                if (!ReadI32(objId) || !ReadLPString(s)) return RecResult::Error;
                NrbfObject obj;
                obj.objKind = NrbfObject::ObjKind::StringObj;
                obj.objectId = objId;
                obj.strValue = s;
                objects[objId] = std::move(obj);
                outVal.kind = NrbfValue::Kind::String;
                outVal.strVal = std::move(s);
                outVal.refId = objId;
                return RecResult::Ok;
            }
            case 7: { // BinaryArray
                int32_t objId = 0, rank = 0;
                uint8_t arrType = 0;
                if (!ReadI32(objId) || !ReadU8(arrType) || !ReadI32(rank)) return RecResult::Error;
                if (rank < 0 || rank > 16 || arrType > 5) return RecResult::Error;
                uint64_t totalLen = (rank == 0) ? 0ull : 1ull;
                for (int32_t r = 0; r < rank; ++r) {
                    int32_t dim = 0;
                    if (!ReadI32(dim) || dim < 0) return RecResult::Error;
                    totalLen *= static_cast<uint64_t>(dim);
                    if (totalLen > 16ull * 1024ull * 1024ull) return RecResult::Error;
                }
                if (arrType >= 3) { // SingleOffset / JaggedOffset / RectangularOffset
                    for (int32_t r = 0; r < rank; ++r) {
                        int32_t lb = 0;
                        if (!ReadI32(lb)) return RecResult::Error;
                    }
                }
                uint8_t binType = 0, primSub = 0;
                if (!ReadU8(binType) || !ReadAdditionalTypeInfo(binType, primSub)) return RecResult::Error;
                if (binType == 0) {
                    if (!SkipPrimitiveArrayBytes(primSub, totalLen)) return RecResult::Error;
                    NrbfObject obj;
                    obj.objKind = NrbfObject::ObjKind::PrimitiveArray;
                    obj.objectId = objId;
                    objects[objId] = std::move(obj);
                } else {
                    if (!ReadObjectArrayBody(objId, static_cast<size_t>(totalLen), depth + 1)) return RecResult::Error;
                }
                outVal.kind = NrbfValue::Kind::ObjectRef;
                outVal.refId = objId;
                return RecResult::Ok;
            }
            case 8: { // MemberPrimitiveTyped
                uint8_t primType = 0;
                if (!ReadU8(primType) || !ReadPrimitive(primType, outVal)) return RecResult::Error;
                return RecResult::Ok;
            }
            case 9: { // MemberReference
                int32_t idRef = 0;
                if (!ReadI32(idRef)) return RecResult::Error;
                outVal.kind = NrbfValue::Kind::ObjectRef;
                outVal.refId = idRef;
                return RecResult::Ok;
            }
            case 10: { // ObjectNull
                outVal.kind = NrbfValue::Kind::Null;
                return RecResult::Ok;
            }
            case 11: { // MessageEnd
                reachedEnd = true;
                return RecResult::End;
            }
            case 12: { // BinaryLibrary
                int32_t libId = 0;
                std::string libName;
                if (!ReadI32(libId) || !ReadLPString(libName)) return RecResult::Error;
                return RecResult::Library;
            }
            case 13: { // ObjectNullMultiple256
                uint8_t cnt = 0;
                if (!ReadU8(cnt)) return RecResult::Error;
                outNullCount = cnt;
                return RecResult::NullMult;
            }
            case 14: { // ObjectNullMultiple
                int32_t cnt = 0;
                if (!ReadI32(cnt) || cnt < 0) return RecResult::Error;
                outNullCount = static_cast<uint32_t>(cnt);
                return RecResult::NullMult;
            }
            case 15: { // ArraySinglePrimitive
                int32_t objId = 0, length = 0;
                uint8_t primType = 0;
                if (!ReadI32(objId) || !ReadI32(length) || length < 0 || !ReadU8(primType)) return RecResult::Error;
                if (!SkipPrimitiveArrayBytes(primType, static_cast<uint64_t>(length))) return RecResult::Error;
                NrbfObject obj;
                obj.objKind = NrbfObject::ObjKind::PrimitiveArray;
                obj.objectId = objId;
                objects[objId] = std::move(obj);
                outVal.kind = NrbfValue::Kind::ObjectRef;
                outVal.refId = objId;
                return RecResult::Ok;
            }
            case 16:   // ArraySingleObject
            case 17: { // ArraySingleString
                int32_t objId = 0, length = 0;
                if (!ReadI32(objId) || !ReadI32(length) || length < 0 || length > 16 * 1024 * 1024) return RecResult::Error;
                if (!ReadObjectArrayBody(objId, static_cast<size_t>(length), depth + 1)) return RecResult::Error;
                outVal.kind = NrbfValue::Kind::ObjectRef;
                outVal.refId = objId;
                return RecResult::Ok;
            }
            default:
                return RecResult::Error;
        }
    }

    bool ReadObjectArrayBody(int32_t objId, size_t length, int depth) {
        NrbfObject arrObj;
        arrObj.objKind = NrbfObject::ObjKind::ObjectArray;
        arrObj.objectId = objId;
        arrObj.arrayItems.resize(length);

        size_t idx = 0;
        while (idx < length) {
            NrbfValue v;
            uint32_t nullMult = 0;
            RecResult rr = ReadRecord(v, nullMult, depth);
            if (rr == RecResult::Library) continue;
            if (rr == RecResult::NullMult) {
                if (nullMult > length - idx) return false;
                idx += nullMult;
                continue;
            }
            if (rr != RecResult::Ok) return false;
            arrObj.arrayItems[idx++] = std::move(v);
        }
        objects[objId] = std::move(arrObj);
        return true;
    }

    bool ParseStream() {
        size_t guardRecords = 0;
        while (!reachedEnd && pos < size) {
            if (++guardRecords > 1000000u) return false;
            NrbfValue v;
            uint32_t nullMult = 0;
            RecResult rr = ReadRecord(v, nullMult, 0);
            if (rr == RecResult::End) break;
            if (rr == RecResult::Error) return false;
        }
        return reachedEnd && rootId != 0 && objects.find(rootId) != objects.end();
    }

    const NrbfObject* ResolveObj(const NrbfValue* v) const {
        if (!v) return nullptr;
        if (v->kind == NrbfValue::Kind::ObjectRef || v->kind == NrbfValue::Kind::InlineObjId) {
            auto it = objects.find(v->refId);
            return (it != objects.end()) ? &it->second : nullptr;
        }
        return nullptr;
    }

    std::string ResolveString(const NrbfValue* v) const {
        if (!v) return {};
        if (v->kind == NrbfValue::Kind::String) return v->strVal;
        const NrbfObject* o = ResolveObj(v);
        if (o && o->objKind == NrbfObject::ObjKind::StringObj) return o->strValue;
        return {};
    }
};

inline int BlendModeFromClassName(std::string_view clsName) {
    auto hasSub = [&](std::string_view sub) { return clsName.find(sub) != std::string_view::npos; };
    if (hasSub("MultiplyBlendOp"))   return 1;
    if (hasSub("AdditiveBlendOp"))   return 2;
    if (hasSub("ColorBurnBlendOp"))  return 3;
    if (hasSub("ColorDodgeBlendOp")) return 4;
    if (hasSub("ReflectBlendOp"))    return 5;
    if (hasSub("GlowBlendOp"))       return 6;
    if (hasSub("OverlayBlendOp"))    return 7;
    if (hasSub("DifferenceBlendOp")) return 8;
    if (hasSub("NegationBlendOp"))   return 9;
    if (hasSub("LightenBlendOp"))    return 10;
    if (hasSub("DarkenBlendOp"))     return 11;
    if (hasSub("ScreenBlendOp"))     return 12;
    if (hasSub("XorBlendOp"))        return 13;
    return 0; // Normal
}

inline float PdnBlendChannel(float a, float b, int blendMode) {
    switch (blendMode) {
        case 0:  return b; // Normal
        case 1:  return a * b; // Multiply
        case 2:  return std::min(1.0f, a + b); // Additive
        case 3:  return (b <= 1e-6f) ? 0.0f : std::max(0.0f, 1.0f - (1.0f - a) / b); // ColorBurn
        case 4:  return (b >= 1.0f - 1e-6f) ? 1.0f : std::min(1.0f, a / (1.0f - b)); // ColorDodge
        case 5:  return (b >= 1.0f - 1e-6f) ? 1.0f : std::min(1.0f, (a * a) / (1.0f - b)); // Reflect
        case 6:  return (a >= 1.0f - 1e-6f) ? 1.0f : std::min(1.0f, (b * b) / (1.0f - a)); // Glow
        case 7:  return (a < 0.5f) ? (2.0f * a * b) : (1.0f - 2.0f * (1.0f - a) * (1.0f - b)); // Overlay
        case 8:  return std::fabs(a - b); // Difference
        case 9:  return 1.0f - std::fabs(1.0f - a - b); // Negation
        case 10: return std::max(a, b); // Lighten
        case 11: return std::min(a, b); // Darken
        case 12: return a + b - a * b;  // Screen
        case 13: { // Xor (8-bit integer XOR)
            uint8_t ia = static_cast<uint8_t>(std::clamp(std::lround(a * 255.0f), 0l, 255l));
            uint8_t ib = static_cast<uint8_t>(std::clamp(std::lround(b * 255.0f), 0l, 255l));
            return static_cast<float>(ia ^ ib) * (1.0f / 255.0f);
        }
        default: return b;
    }
}

inline uint32_t ParseXmlUIntAttr(std::string_view xml, std::string_view attrName) {
    std::string needle(attrName);
    needle += "=\"";
    size_t p = xml.find(needle);
    if (p == std::string_view::npos) return 0;
    p += needle.size();
    uint32_t v = 0;
    bool any = false;
    while (p < xml.size() && xml[p] >= '0' && xml[p] <= '9') {
        v = v * 10u + static_cast<uint32_t>(xml[p] - '0');
        any = true;
        ++p;
    }
    return any ? v : 0u;
}

} // namespace DetailPdn

inline bool DecodePaintDotNet(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    using namespace DetailPdn;

    if (!data || size < 24 || std::memcmp(data, "PDN3", 4) != 0) return false;
    uint32_t xmlLen = RdU24LE(data + 4);
    if (7ull + xmlLen + 2ull > size) return false;

    std::string_view xmlHeader(reinterpret_cast<const char*>(data + 7), xmlLen);
    uint32_t xmlW = ParseXmlUIntAttr(xmlHeader, "width");
    uint32_t xmlH = ParseXmlUIntAttr(xmlHeader, "height");

    size_t payloadPos = 7u + xmlLen;
    const uint8_t* streamPtr = data;
    size_t streamSize = size;
    size_t nrbfStart = payloadPos;
    std::vector<uint8_t> inflatedStream;

    if (payloadPos + 2 <= size && data[payloadPos] == 0x00 && data[payloadPos + 1] == 0x01 &&
        payloadPos + 19 <= size && data[payloadPos + 2] == 0x00 &&
        RdU32LE(data + payloadPos + 11) == 1u && RdU32LE(data + payloadPos + 15) == 0u)
    {
        // Standard Paint.NET uncompressed indicator 0x0100 (LE bytes 0x00, 0x01)
        nrbfStart = payloadPos + 2u;
    } else if (payloadPos + 4 <= size && data[payloadPos] == 0x00 && data[payloadPos + 1] == 0x00 &&
               data[payloadPos + 2] == 0x1F && data[payloadPos + 3] == 0x8B)
    {
        // Paint.NET GZip-compressed entire NRBF+MemoryBlock stream (0x0000 indicator + GZip)
        if (!ZipDeflate::InflateGzip(data + payloadPos + 2u, size - (payloadPos + 2u), inflatedStream, 256u * 1024u * 1024u)) {
            return false;
        }
        streamPtr = inflatedStream.data();
        streamSize = inflatedStream.size();
        nrbfStart = 0;
    } else if (payloadPos + 2 <= size && data[payloadPos] == 0x1F && data[payloadPos + 1] == 0x8B) {
        if (!ZipDeflate::InflateGzip(data + payloadPos, size - payloadPos, inflatedStream, 256u * 1024u * 1024u)) {
            return false;
        }
        streamPtr = inflatedStream.data();
        streamSize = inflatedStream.size();
        nrbfStart = 0;
    } else if (payloadPos + 17 <= size && data[payloadPos] == 0x00 &&
               RdU32LE(data + payloadPos + 9) == 1u && RdU32LE(data + payloadPos + 13) == 0u)
    {
        nrbfStart = payloadPos;
    } else {
        return false;
    }

    NrbfReader nrbf(streamPtr, streamSize, nrbfStart);
    if (!nrbf.ParseStream()) return false;

    const NrbfObject* docObj = &nrbf.objects[nrbf.rootId];
    uint32_t docW = xmlW;
    uint32_t docH = xmlH;
    if (const NrbfValue* wv = docObj->FindField("width"); wv && wv->intVal > 0) docW = static_cast<uint32_t>(wv->intVal);
    if (const NrbfValue* hv = docObj->FindField("height"); hv && hv->intVal > 0) docH = static_cast<uint32_t>(hv->intVal);
    if (docW == 0 || docH == 0 || docW > 16384 || docH > 16384) return false;
    if (static_cast<uint64_t>(docW) * docH > 64ull * 1024ull * 1024ull) return false;

    const NrbfObject* layersListObj = nrbf.ResolveObj(docObj->FindField("layers"));
    if (!layersListObj) return false;

    const NrbfObject* itemsArrObj = nullptr;
    size_t layerCount = 0;
    if (layersListObj->objKind == NrbfObject::ObjKind::ObjectArray) {
        itemsArrObj = layersListObj;
        layerCount = itemsArrObj->arrayItems.size();
    } else {
        itemsArrObj = nrbf.ResolveObj(layersListObj->FindField("_items"));
        if (!itemsArrObj) itemsArrObj = nrbf.ResolveObj(layersListObj->FindField("items"));
        if (!itemsArrObj || itemsArrObj->objKind != NrbfObject::ObjKind::ObjectArray) return false;
        layerCount = itemsArrObj->arrayItems.size();
        if (const NrbfValue* szVal = layersListObj->FindField("_size"); szVal && szVal->intVal >= 0) {
            layerCount = std::min<size_t>(layerCount, static_cast<size_t>(szVal->intVal));
        } else if (const NrbfValue* szVal2 = layersListObj->FindField("size"); szVal2 && szVal2->intVal >= 0) {
            layerCount = std::min<size_t>(layerCount, static_cast<size_t>(szVal2->intVal));
        }
    }
    if (layerCount == 0 || layerCount > 4096) return false;

    struct PdnLayerMeta {
        std::wstring name;
        bool visible = true;
        bool isBackground = false;
        uint8_t opacity = 255;
        int blendMode = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t stride = 0;
        uint64_t byteLength = 0;
        bool hasOwnMemoryBlock = true;
    };
    std::vector<PdnLayerMeta> layers;
    layers.reserve(layerCount);

    for (size_t i = 0; i < layerCount; ++i) {
        const NrbfObject* layerObj = nrbf.ResolveObj(&itemsArrObj->arrayItems[i]);
        if (!layerObj) continue;

        PdnLayerMeta lm;
        lm.width = docW;
        lm.height = docH;
        if (const NrbfValue* lw = layerObj->FindField("width"); lw && lw->intVal > 0) lm.width = static_cast<uint32_t>(lw->intVal);
        if (const NrbfValue* lh = layerObj->FindField("height"); lh && lh->intVal > 0) lm.height = static_cast<uint32_t>(lh->intVal);
        lm.stride = lm.width * 4u;

        // Layer+properties (base) and BitmapLayer+properties (derived)
        for (const auto& kv : layerObj->fields) {
            if (kv.first.find("properties") == std::string::npos) continue;
            const NrbfObject* propObj = nrbf.ResolveObj(&kv.second);
            if (!propObj) continue;

            if (const NrbfValue* nv = propObj->FindField("name")) {
                std::string s = nrbf.ResolveString(nv);
                if (!s.empty()) lm.name = PathUtils::Utf8ToWide(s);
            }
            if (const NrbfValue* vv = propObj->FindField("visible"); vv && vv->kind == NrbfValue::Kind::Bool) {
                lm.visible = vv->boolVal;
            }
            if (const NrbfValue* bgv = propObj->FindField("isBackground"); bgv && bgv->kind == NrbfValue::Kind::Bool) {
                lm.isBackground = bgv->boolVal;
            }
            if (const NrbfValue* opv = propObj->FindField("opacity"); opv && opv->kind == NrbfValue::Kind::Int) {
                lm.opacity = static_cast<uint8_t>(std::clamp<int64_t>(opv->intVal, 0, 255));
            }
            if (const NrbfValue* bmv = propObj->FindField("blendMode")) {
                if (bmv->kind == NrbfValue::Kind::Int) {
                    lm.blendMode = static_cast<int>(std::clamp<int64_t>(bmv->intVal, 0, 13));
                } else if (const NrbfObject* bmEnum = nrbf.ResolveObj(bmv)) {
                    if (const NrbfValue* ev = bmEnum->FindField("value__")) {
                        lm.blendMode = static_cast<int>(std::clamp<int64_t>(ev->intVal, 0, 13));
                    } else {
                        lm.blendMode = BlendModeFromClassName(bmEnum->className);
                    }
                }
            }
            if (const NrbfValue* bov = propObj->FindField("blendOp")) {
                if (const NrbfObject* boObj = nrbf.ResolveObj(bov)) {
                    lm.blendMode = BlendModeFromClassName(boObj->className);
                }
            }
        }

        const NrbfObject* surfObj = nrbf.ResolveObj(layerObj->FindField("surface"));
        if (surfObj) {
            if (const NrbfValue* stv = surfObj->FindField("stride"); stv && stv->intVal > 0) {
                lm.stride = static_cast<uint32_t>(stv->intVal);
            }
            if (const NrbfValue* swv = surfObj->FindField("width"); swv && swv->intVal > 0) {
                lm.width = static_cast<uint32_t>(swv->intVal);
            }
            if (const NrbfValue* shv = surfObj->FindField("height"); shv && shv->intVal > 0) {
                lm.height = static_cast<uint32_t>(shv->intVal);
            }
            const NrbfObject* scan0Obj = nrbf.ResolveObj(surfObj->FindField("scan0"));
            if (scan0Obj) {
                if (const NrbfValue* l64 = scan0Obj->FindField("length64"); l64 && l64->intVal > 0) {
                    lm.byteLength = static_cast<uint64_t>(l64->intVal);
                } else if (const NrbfValue* l32 = scan0Obj->FindField("length"); l32 && l32->intVal > 0) {
                    lm.byteLength = static_cast<uint64_t>(l32->intVal);
                }
                if (const NrbfValue* hp = scan0Obj->FindField("hasParent"); hp && hp->kind == NrbfValue::Kind::Bool && hp->boolVal) {
                    lm.hasOwnMemoryBlock = false;
                }
            }
        }
        if (lm.byteLength == 0) {
            lm.byteLength = static_cast<uint64_t>(lm.stride) * lm.height;
        }
        if (lm.byteLength == 0 || lm.byteLength > 256ull * 1024ull * 1024ull) return false;
        layers.push_back(std::move(lm));
    }

    if (layers.empty()) return false;

    const size_t totalPixels = static_cast<size_t>(docW) * docH;
    std::vector<float> canvasRgba(totalPixels * 4u, 0.0f);
    size_t memPos = nrbf.pos;

    outInfo.layerNames.clear();
    for (size_t li = 0; li < layers.size(); ++li) {
        const PdnLayerMeta& lm = layers[li];
        if (!lm.name.empty()) {
            outInfo.layerNames.push_back(lm.name);
        } else {
            outInfo.layerNames.push_back(L"Layer " + std::to_wstring(li + 1));
        }

        if (!lm.hasOwnMemoryBlock) continue;
        if (memPos + 5 > streamSize) return false;

        uint8_t formatVersion = streamPtr[memPos];
        uint32_t chunkSize = RdU32BE(streamPtr + memPos + 1);
        memPos += 5;
        if (formatVersion > 1 || chunkSize == 0 || chunkSize > 256u * 1024u * 1024u) return false;

        uint64_t expectedChunks64 = (lm.byteLength + chunkSize - 1ull) / chunkSize;
        if (expectedChunks64 == 0 || expectedChunks64 > 65536ull) return false;
        uint32_t chunkCount = static_cast<uint32_t>(expectedChunks64);

        // Auto-detect optional explicit [uint32BE chunkCount] header vs standard Paint.NET MemoryBlock.cs
        if (memPos + 12 <= streamSize) {
            uint32_t word0 = RdU32BE(streamPtr + memPos);
            if (word0 == chunkCount) {
                if (formatVersion == 0) {
                    bool gzipStd = (memPos + 10 <= streamSize && streamPtr[memPos + 8] == 0x1F && streamPtr[memPos + 9] == 0x8B);
                    bool gzipExplicit = (memPos + 14 <= streamSize && streamPtr[memPos + 12] == 0x1F && streamPtr[memPos + 13] == 0x8B);
                    if (!gzipStd && gzipExplicit) {
                        memPos += 4;
                    }
                } else {
                    uint32_t word1 = RdU32BE(streamPtr + memPos + 4);
                    uint32_t word2 = RdU32BE(streamPtr + memPos + 8);
                    uint32_t firstActual = static_cast<uint32_t>(std::min<uint64_t>(chunkSize, lm.byteLength));
                    if (word1 < chunkCount && word2 == firstActual && word0 >= 1u) {
                        memPos += 4;
                    }
                }
            }
        }

        std::vector<uint8_t> layerBuf;
        if (lm.visible && lm.opacity > 0) {
            layerBuf.assign(static_cast<size_t>(lm.byteLength), 0u);
        }
        std::vector<uint8_t> seenChunk(chunkCount, 0u);
        std::vector<uint8_t> chunkInflated;

        for (uint32_t ci = 0; ci < chunkCount; ++ci) {
            if (memPos + 8 > streamSize) return false;
            uint32_t chunkNum = RdU32BE(streamPtr + memPos);
            uint32_t dataSize = RdU32BE(streamPtr + memPos + 4);
            memPos += 8;
            if (chunkNum >= chunkCount || seenChunk[chunkNum] != 0) return false;
            if (dataSize > streamSize - memPos) return false;
            seenChunk[chunkNum] = 1u;

            uint64_t chunkOff = static_cast<uint64_t>(chunkNum) * chunkSize;
            size_t actualBytes = static_cast<size_t>(std::min<uint64_t>(chunkSize, lm.byteLength - chunkOff));

            if (!layerBuf.empty()) {
                if (formatVersion == 0) {
                    chunkInflated.clear();
                    if (!ZipDeflate::InflateGzip(streamPtr + memPos, dataSize, chunkInflated, actualBytes + 64u)) {
                        return false;
                    }
                    if (chunkInflated.size() < actualBytes) return false;
                    std::memcpy(layerBuf.data() + chunkOff, chunkInflated.data(), actualBytes);
                } else {
                    if (dataSize < actualBytes) return false;
                    std::memcpy(layerBuf.data() + chunkOff, streamPtr + memPos, actualBytes);
                }
            }
            memPos += dataSize;
        }

        if (!lm.visible || lm.opacity == 0 || layerBuf.empty()) continue;

        uint32_t bpp = (lm.width > 0) ? ((lm.stride * 8u) / lm.width) : 32u;
        if (bpp != 32u && bpp != 24u) continue;
        const uint32_t bytesPerPx = bpp / 8u;
        const float layerOpac = static_cast<float>(lm.opacity) * (1.0f / 255.0f);

        uint32_t copyW = std::min(docW, lm.width);
        uint32_t copyH = std::min(docH, lm.height);
        for (uint32_t y = 0; y < copyH; ++y) {
            size_t rowOff = static_cast<size_t>(y) * lm.stride;
            if (rowOff + static_cast<size_t>(copyW) * bytesPerPx > layerBuf.size()) break;
            const uint8_t* sp = layerBuf.data() + rowOff;
            float* dp = &canvasRgba[static_cast<size_t>(y) * docW * 4u];

            for (uint32_t x = 0; x < copyW; ++x) {
                float srcB = static_cast<float>(sp[x * bytesPerPx + 0]) * (1.0f / 255.0f);
                float srcG = static_cast<float>(sp[x * bytesPerPx + 1]) * (1.0f / 255.0f);
                float srcR = static_cast<float>(sp[x * bytesPerPx + 2]) * (1.0f / 255.0f);
                float srcA = ((bytesPerPx == 4u) ? (static_cast<float>(sp[x * bytesPerPx + 3]) * (1.0f / 255.0f)) : 1.0f) * layerOpac;
                if (srcA <= 0.0f) continue;

                float dstR = dp[x * 4u + 0];
                float dstG = dp[x * 4u + 1];
                float dstB = dp[x * 4u + 2];
                float dstA = dp[x * 4u + 3];

                float ab = dstA * srcA;
                float outA = dstA + srcA - ab;
                if (outA <= 1e-7f) {
                    dp[x * 4u + 0] = dp[x * 4u + 1] = dp[x * 4u + 2] = dp[x * 4u + 3] = 0.0f;
                } else {
                    float invOutA = 1.0f / outA;
                    float wDst = dstA - ab;
                    float wSrc = srcA - ab;
                    dp[x * 4u + 0] = std::clamp((wDst * dstR + wSrc * srcR + ab * PdnBlendChannel(dstR, srcR, lm.blendMode)) * invOutA, 0.0f, 1.0f);
                    dp[x * 4u + 1] = std::clamp((wDst * dstG + wSrc * srcG + ab * PdnBlendChannel(dstG, srcG, lm.blendMode)) * invOutA, 0.0f, 1.0f);
                    dp[x * 4u + 2] = std::clamp((wDst * dstB + wSrc * srcB + ab * PdnBlendChannel(dstB, srcB, lm.blendMode)) * invOutA, 0.0f, 1.0f);
                    dp[x * 4u + 3] = std::clamp(outA, 0.0f, 1.0f);
                }
            }
        }
    }

    outInfo.width = docW;
    outInfo.height = docH;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"paint.net Layered Image (.pdn)";
    outInfo.bgraPixels.resize(totalPixels * 4u);
    for (size_t i = 0; i < totalPixels; ++i) {
        uint8_t r = static_cast<uint8_t>(std::clamp(std::lround(canvasRgba[i * 4u + 0] * 255.0f), 0l, 255l));
        uint8_t g = static_cast<uint8_t>(std::clamp(std::lround(canvasRgba[i * 4u + 1] * 255.0f), 0l, 255l));
        uint8_t b = static_cast<uint8_t>(std::clamp(std::lround(canvasRgba[i * 4u + 2] * 255.0f), 0l, 255l));
        uint8_t a = static_cast<uint8_t>(std::clamp(std::lround(canvasRgba[i * 4u + 3] * 255.0f), 0l, 255l));
        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
