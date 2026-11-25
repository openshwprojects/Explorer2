#pragma once
// ProcreateImage.h — Procreate Document.archive (bplist00 NSKeyedArchiver) & LZFSE/LZ4 Tile Compositor (.procreate)

namespace Formats {

namespace DetailProcreate {

inline uint16_t Rd16BE(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t Rd32BE(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}
inline uint64_t Rd64BE(const uint8_t* p) noexcept {
    return (static_cast<uint64_t>(Rd32BE(p)) << 32) | Rd32BE(p + 4);
}
inline uint64_t RdVarBE(const uint8_t* p, size_t nBytes) noexcept {
    uint64_t v = 0;
    for (size_t i = 0; i < nBytes; ++i) v = (v << 8) | p[i];
    return v;
}

struct BPlistObj {
    enum class Type : uint8_t { Null, Bool, Int, Real, Data, String, Uid, Array, Dict };
    Type type = Type::Null;
    bool bVal = false;
    int64_t iVal = 0;
    double fVal = 0.0;
    std::string sVal;
    std::vector<uint8_t> dVal;
    uint64_t uid = 0;
    std::vector<uint64_t> arr;
    std::vector<std::pair<uint64_t, uint64_t>> dict;
};

class BPlistArchive {
public:
    std::vector<BPlistObj> objs;
    uint64_t topObj = 0;

    bool Parse(const uint8_t* buf, size_t len) {
        if (!buf || len < 40 || std::memcmp(buf, "bplist0", 7) != 0) return false;
        const uint8_t* tr = buf + len - 32;
        const uint8_t offSize = tr[6];
        const uint8_t refSize = tr[7];
        const uint64_t numObjects = Rd64BE(tr + 8);
        topObj = Rd64BE(tr + 16);
        const uint64_t offTableStart = Rd64BE(tr + 24);

        if ((offSize != 1 && offSize != 2 && offSize != 4 && offSize != 8) ||
            (refSize != 1 && refSize != 2 && refSize != 4 && refSize != 8))
        {
            return false;
        }
        if (numObjects == 0 || numObjects > 100000 || topObj >= numObjects) return false;
        if (offTableStart < 8 || offTableStart > len - 32) return false;
        if (numObjects * offSize > (len - 32) - offTableStart) return false;

        objs.resize(static_cast<size_t>(numObjects));
        for (size_t idx = 0; idx < static_cast<size_t>(numObjects); ++idx) {
            const uint64_t objOff = RdVarBE(buf + offTableStart + idx * offSize, offSize);
            if (objOff < 8 || objOff >= offTableStart) return false;
            if (!DecodeObjectAt(buf, offTableStart, static_cast<size_t>(objOff), refSize, objs[idx])) return false;
        }
        return true;
    }

    const BPlistObj* Get(uint64_t idx) const {
        return (idx < objs.size()) ? &objs[static_cast<size_t>(idx)] : nullptr;
    }

    const BPlistObj* DerefKeyed(const BPlistObj* o) const {
        if (!o) return nullptr;
        if (o->type == BPlistObj::Type::Uid) {
            return (o->uid < objs.size()) ? &objs[static_cast<size_t>(o->uid)] : nullptr;
        }
        return o;
    }

    const BPlistObj* FindDictVal(const BPlistObj* dictObj, std::string_view key) const {
        dictObj = DerefKeyed(dictObj);
        if (!dictObj || dictObj->type != BPlistObj::Type::Dict) return nullptr;

        for (const auto& kv : dictObj->dict) {
            const BPlistObj* k = Get(kv.first);
            if (k && k->type == BPlistObj::Type::String && k->sVal == key) {
                return DerefKeyed(Get(kv.second));
            }
        }
        // Handle NSDictionary archived via NS.keys / NS.objects
        const BPlistObj* nsKeys = nullptr;
        const BPlistObj* nsObjs = nullptr;
        for (const auto& kv : dictObj->dict) {
            const BPlistObj* k = Get(kv.first);
            if (!k || k->type != BPlistObj::Type::String) continue;
            if (k->sVal == "NS.keys") nsKeys = DerefKeyed(Get(kv.second));
            else if (k->sVal == "NS.objects") nsObjs = DerefKeyed(Get(kv.second));
        }
        if (nsKeys && nsObjs && nsKeys->type == BPlistObj::Type::Array && nsObjs->type == BPlistObj::Type::Array) {
            const size_t n = std::min(nsKeys->arr.size(), nsObjs->arr.size());
            for (size_t i = 0; i < n; ++i) {
                const BPlistObj* k = DerefKeyed(Get(nsKeys->arr[i]));
                if (k && k->type == BPlistObj::Type::String && k->sVal == key) {
                    return DerefKeyed(Get(nsObjs->arr[i]));
                }
            }
        }
        return nullptr;
    }

    const BPlistObj* ResolveKeyedRoot() const {
        const BPlistObj* top = Get(topObj);
        if (!top || top->type != BPlistObj::Type::Dict) return nullptr;
        const BPlistObj* objectsArr = FindDictVal(top, "$objects");
        const BPlistObj* topMap = FindDictVal(top, "$top");
        if (!objectsArr || objectsArr->type != BPlistObj::Type::Array || !topMap) return top;

        // Re-map UID indices through $objects array
        const BPlistObj* rootRef = nullptr;
        for (const auto& kv : topMap->dict) {
            const BPlistObj* k = Get(kv.first);
            if (k && k->type == BPlistObj::Type::String && k->sVal == "root") {
                rootRef = Get(kv.second);
                break;
            }
        }
        if (!rootRef && !topMap->dict.empty()) rootRef = Get(topMap->dict[0].second);
        return DerefArchiverUid(objectsArr, rootRef);
    }

    const BPlistObj* DerefArchiverUid(const BPlistObj* objectsArr, const BPlistObj* v) const {
        if (!v) return nullptr;
        if (v->type == BPlistObj::Type::Uid && objectsArr && objectsArr->type == BPlistObj::Type::Array) {
            if (v->uid < objectsArr->arr.size()) {
                return Get(objectsArr->arr[static_cast<size_t>(v->uid)]);
            }
            return nullptr;
        }
        return v;
    }

    const BPlistObj* FindArchiverVal(const BPlistObj* objectsArr, const BPlistObj* dictObj, std::string_view key) const {
        dictObj = DerefArchiverUid(objectsArr, dictObj);
        if (!dictObj || dictObj->type != BPlistObj::Type::Dict) return nullptr;
        for (const auto& kv : dictObj->dict) {
            const BPlistObj* k = DerefArchiverUid(objectsArr, Get(kv.first));
            if (k && k->type == BPlistObj::Type::String && k->sVal == key) {
                return DerefArchiverUid(objectsArr, Get(kv.second));
            }
        }
        const BPlistObj* nsKeys = nullptr;
        const BPlistObj* nsObjs = nullptr;
        for (const auto& kv : dictObj->dict) {
            const BPlistObj* k = DerefArchiverUid(objectsArr, Get(kv.first));
            if (!k || k->type != BPlistObj::Type::String) continue;
            if (k->sVal == "NS.keys") nsKeys = DerefArchiverUid(objectsArr, Get(kv.second));
            else if (k->sVal == "NS.objects") nsObjs = DerefArchiverUid(objectsArr, Get(kv.second));
        }
        if (nsKeys && nsObjs && nsKeys->type == BPlistObj::Type::Array && nsObjs->type == BPlistObj::Type::Array) {
            const size_t n = std::min(nsKeys->arr.size(), nsObjs->arr.size());
            for (size_t i = 0; i < n; ++i) {
                const BPlistObj* k = DerefArchiverUid(objectsArr, Get(nsKeys->arr[i]));
                if (k && k->type == BPlistObj::Type::String && k->sVal == key) {
                    return DerefArchiverUid(objectsArr, Get(nsObjs->arr[i]));
                }
            }
        }
        return nullptr;
    }

    std::string ExtractString(const BPlistObj* objectsArr, const BPlistObj* v) const {
        v = DerefArchiverUid(objectsArr, v);
        if (!v) return {};
        if (v->type == BPlistObj::Type::String) return v->sVal;
        if (v->type == BPlistObj::Type::Dict) {
            if (const BPlistObj* ns = FindArchiverVal(objectsArr, v, "NS.string")) {
                if (ns->type == BPlistObj::Type::String) return ns->sVal;
            }
            if (const BPlistObj* nb = FindArchiverVal(objectsArr, v, "NS.bytes")) {
                if (nb->type == BPlistObj::Type::Data) {
                    return std::string(reinterpret_cast<const char*>(nb->dVal.data()), nb->dVal.size());
                }
            }
        }
        return {};
    }

private:
    static bool ReadCountHeader(const uint8_t* buf, size_t endOff, size_t& pos, uint8_t lowNibble, uint64_t& outCount) {
        if (lowNibble != 0x0Fu) {
            outCount = lowNibble;
            return true;
        }
        if (pos >= endOff) return false;
        const uint8_t intMarker = buf[pos++];
        if ((intMarker & 0xF0u) != 0x10u) return false;
        const size_t intBytes = size_t{1} << (intMarker & 0x0Fu);
        if (intBytes > 8u || pos + intBytes > endOff) return false;
        outCount = RdVarBE(buf + pos, intBytes);
        pos += intBytes;
        return true;
    }

    static bool DecodeObjectAt(const uint8_t* buf, size_t endOff, size_t pos, uint8_t refSize, BPlistObj& out) {
        if (pos >= endOff) return false;
        const uint8_t marker = buf[pos++];
        const uint8_t hi = marker & 0xF0u;
        const uint8_t lo = marker & 0x0Fu;

        if (hi == 0x00u) {
            if (lo == 0x08u) { out.type = BPlistObj::Type::Bool; out.bVal = false; return true; }
            if (lo == 0x09u) { out.type = BPlistObj::Type::Bool; out.bVal = true;  return true; }
            out.type = BPlistObj::Type::Null;
            return true;
        }
        if (hi == 0x10u) {
            const size_t n = size_t{1} << lo;
            if (n > 16u || pos + n > endOff) return false;
            out.type = BPlistObj::Type::Int;
            out.iVal = (n <= 8u) ? static_cast<int64_t>(RdVarBE(buf + pos, n))
                                 : static_cast<int64_t>(Rd64BE(buf + pos + n - 8u));
            return true;
        }
        if (hi == 0x20u) {
            const size_t n = size_t{1} << lo;
            if ((n != 4u && n != 8u) || pos + n > endOff) return false;
            out.type = BPlistObj::Type::Real;
            if (n == 4u) {
                uint32_t u = Rd32BE(buf + pos);
                float f = 0.0f;
                std::memcpy(&f, &u, 4);
                out.fVal = f;
            } else {
                uint64_t u = Rd64BE(buf + pos);
                double d = 0.0;
                std::memcpy(&d, &u, 8);
                out.fVal = d;
            }
            return true;
        }
        if (hi == 0x30u) {
            if (pos + 8u > endOff) return false;
            out.type = BPlistObj::Type::Real;
            uint64_t u = Rd64BE(buf + pos);
            std::memcpy(&out.fVal, &u, 8);
            return true;
        }
        if (hi == 0x40u) {
            uint64_t cnt = 0;
            if (!ReadCountHeader(buf, endOff, pos, lo, cnt) || cnt > endOff - pos) return false;
            out.type = BPlistObj::Type::Data;
            out.dVal.assign(buf + pos, buf + pos + static_cast<size_t>(cnt));
            return true;
        }
        if (hi == 0x50u) {
            uint64_t cnt = 0;
            if (!ReadCountHeader(buf, endOff, pos, lo, cnt) || cnt > endOff - pos) return false;
            out.type = BPlistObj::Type::String;
            out.sVal.assign(reinterpret_cast<const char*>(buf + pos), static_cast<size_t>(cnt));
            return true;
        }
        if (hi == 0x60u) {
            uint64_t cnt = 0;
            if (!ReadCountHeader(buf, endOff, pos, lo, cnt) || cnt > (endOff - pos) / 2u) return false;
            out.type = BPlistObj::Type::String;
            out.sVal.reserve(static_cast<size_t>(cnt));
            for (size_t i = 0; i < static_cast<size_t>(cnt); ++i) {
                uint16_t ch = Rd16BE(buf + pos + i * 2u);
                if (ch < 0x80u) {
                    out.sVal.push_back(static_cast<char>(ch));
                } else if (ch < 0x800u) {
                    out.sVal.push_back(static_cast<char>(0xC0u | (ch >> 6)));
                    out.sVal.push_back(static_cast<char>(0x80u | (ch & 0x3Fu)));
                } else {
                    out.sVal.push_back(static_cast<char>(0xE0u | (ch >> 12)));
                    out.sVal.push_back(static_cast<char>(0x80u | ((ch >> 6) & 0x3Fu)));
                    out.sVal.push_back(static_cast<char>(0x80u | (ch & 0x3Fu)));
                }
            }
            return true;
        }
        if (hi == 0x80u) {
            const size_t n = static_cast<size_t>(lo) + 1u;
            if (n > 8u || pos + n > endOff) return false;
            out.type = BPlistObj::Type::Uid;
            out.uid = RdVarBE(buf + pos, n);
            return true;
        }
        if (hi == 0xA0u || hi == 0xC0u) {
            uint64_t cnt = 0;
            if (!ReadCountHeader(buf, endOff, pos, lo, cnt) || cnt > (endOff - pos) / refSize) return false;
            out.type = BPlistObj::Type::Array;
            out.arr.resize(static_cast<size_t>(cnt));
            for (size_t i = 0; i < static_cast<size_t>(cnt); ++i) {
                out.arr[i] = RdVarBE(buf + pos + i * refSize, refSize);
            }
            return true;
        }
        if (hi == 0xD0u) {
            uint64_t cnt = 0;
            if (!ReadCountHeader(buf, endOff, pos, lo, cnt) || cnt > (endOff - pos) / (2u * refSize)) return false;
            out.type = BPlistObj::Type::Dict;
            out.dict.resize(static_cast<size_t>(cnt));
            const size_t valBase = pos + static_cast<size_t>(cnt) * refSize;
            for (size_t i = 0; i < static_cast<size_t>(cnt); ++i) {
                out.dict[i].first  = RdVarBE(buf + pos + i * refSize, refSize);
                out.dict[i].second = RdVarBE(buf + valBase + i * refSize, refSize);
            }
            return true;
        }
        return false;
    }
};

struct SilicaLayerSpec {
    std::wstring uuidLower;
    std::wstring displayName;
    bool hidden = false;
    float opacity = 1.0f;
    int blend = 0;
};

struct SilicaDocMeta {
    bool parsed = false;
    int width = 0;
    int height = 0;
    int tileSize = 0;
    int orientation = 0;
    bool flippedHorizontally = false;
    bool flippedVertically = false;
    bool backgroundHidden = true;
    uint8_t bgRgba[4] = { 255, 255, 255, 255 };
    std::wstring compositeUuidLower;
    std::vector<SilicaLayerSpec> layersTopToBottom;
};

inline bool ParseCgSizeString(std::string_view sv, int& outW, int& outH) {
    size_t b0 = sv.find('{');
    size_t b1 = (b0 != std::string_view::npos) ? sv.find('}', b0 + 1) : std::string_view::npos;
    if (b0 == std::string_view::npos || b1 == std::string_view::npos || b1 <= b0 + 1) return false;
    std::string_view inner = sv.substr(b0 + 1, b1 - b0 - 1);
    size_t comma = inner.find(',');
    if (comma == std::string_view::npos) return false;
    int w = DetailArtFont::ParseIntOr(std::string(DetailArtFont::TrimAsciiView(inner.substr(0, comma))), 0);
    int h = DetailArtFont::ParseIntOr(std::string(DetailArtFont::TrimAsciiView(inner.substr(comma + 1))), 0);
    if (w <= 0 || w > 16384 || h <= 0 || h > 16384) return false;
    outW = w;
    outH = h;
    return true;
}

inline void CollectSilicaLayersRecursive(
    const BPlistArchive& arc,
    const BPlistObj* objectsArr,
    const BPlistObj* container,
    bool parentHidden,
    float parentOpacity,
    const std::wstring& compositeUuidLower,
    std::vector<SilicaLayerSpec>& outLayers,
    int depth = 0)
{
    if (!container || depth > 8 || outLayers.size() >= 512) return;
    const BPlistObj* arr = container;
    if (arr->type == BPlistObj::Type::Dict) {
        if (const BPlistObj* nsObjs = arc.FindArchiverVal(objectsArr, arr, "NS.objects")) {
            arr = nsObjs;
        }
    }
    if (!arr || arr->type != BPlistObj::Type::Array) return;

    for (uint64_t itemRef : arr->arr) {
        const BPlistObj* item = arc.Get(itemRef);
        item = arc.DerefArchiverUid(objectsArr, item);
        if (!item || item->type != BPlistObj::Type::Dict) continue;

        bool isHidden = parentHidden;
        if (const BPlistObj* hObj = arc.FindArchiverVal(objectsArr, item, "hidden")) {
            if (hObj->type == BPlistObj::Type::Bool) isHidden = isHidden || hObj->bVal;
            else if (hObj->type == BPlistObj::Type::Int) isHidden = isHidden || (hObj->iVal != 0);
        }
        if (const BPlistObj* vObj = arc.FindArchiverVal(objectsArr, item, "isHidden")) {
            if (vObj->type == BPlistObj::Type::Bool) isHidden = isHidden || vObj->bVal;
            else if (vObj->type == BPlistObj::Type::Int) isHidden = isHidden || (vObj->iVal != 0);
        }

        float opacity = 1.0f;
        if (const BPlistObj* opObj = arc.FindArchiverVal(objectsArr, item, "opacity")) {
            if (opObj->type == BPlistObj::Type::Real) opacity = static_cast<float>(opObj->fVal);
            else if (opObj->type == BPlistObj::Type::Int) opacity = static_cast<float>(opObj->iVal);
        }
        opacity = std::clamp(opacity, 0.0f, 1.0f) * parentOpacity;

        // SilicaGroup nested children
        if (const BPlistObj* children = arc.FindArchiverVal(objectsArr, item, "children")) {
            CollectSilicaLayersRecursive(arc, objectsArr, children, isHidden, opacity, compositeUuidLower, outLayers, depth + 1);
            continue;
        }

        std::string uuidStr = arc.ExtractString(objectsArr, arc.FindArchiverVal(objectsArr, item, "UUID"));
        if (uuidStr.empty()) continue;
        std::wstring uuidLow = PathUtils::ToLower(PathUtils::Utf8ToWide(uuidStr));
        if (!compositeUuidLower.empty() && uuidLow == compositeUuidLower) {
            continue; // Exclude separate composite layer so layers are never double-blended!
        }

        std::string nameStr = arc.ExtractString(objectsArr, arc.FindArchiverVal(objectsArr, item, "name"));
        int blendMode = 0;
        if (const BPlistObj* bObj = arc.FindArchiverVal(objectsArr, item, "extendedBlend")) {
            if (bObj->type == BPlistObj::Type::Int) blendMode = static_cast<int>(bObj->iVal);
        } else if (const BPlistObj* bObj2 = arc.FindArchiverVal(objectsArr, item, "blend")) {
            if (bObj2->type == BPlistObj::Type::Int) blendMode = static_cast<int>(bObj2->iVal);
        }

        SilicaLayerSpec spec;
        spec.uuidLower = std::move(uuidLow);
        spec.displayName = nameStr.empty() ? PathUtils::Utf8ToWide(uuidStr) : PathUtils::Utf8ToWide(nameStr);
        spec.hidden = isHidden;
        spec.opacity = opacity;
        spec.blend = blendMode;
        outLayers.push_back(std::move(spec));
    }
}

inline bool ParseSilicaDocumentArchive(const uint8_t* bytes, size_t len, SilicaDocMeta& meta) {
    BPlistArchive arc;
    if (!arc.Parse(bytes, len)) return false;

    const BPlistObj* top = arc.Get(arc.topObj);
    const BPlistObj* objectsArr = top ? arc.FindDictVal(top, "$objects") : nullptr;
    const BPlistObj* root = arc.ResolveKeyedRoot();
    if (!root || root->type != BPlistObj::Type::Dict) return false;

    std::string sizeStr = arc.ExtractString(objectsArr, arc.FindArchiverVal(objectsArr, root, "size"));
    if (!sizeStr.empty()) {
        ParseCgSizeString(sizeStr, meta.width, meta.height);
    }

    if (const BPlistObj* tsObj = arc.FindArchiverVal(objectsArr, root, "tileSize")) {
        if (tsObj->type == BPlistObj::Type::Int && tsObj->iVal >= 4 && tsObj->iVal <= 2048) {
            meta.tileSize = static_cast<int>(tsObj->iVal);
        }
    }
    if (const BPlistObj* orObj = arc.FindArchiverVal(objectsArr, root, "orientation")) {
        if (orObj->type == BPlistObj::Type::Int) meta.orientation = static_cast<int>(orObj->iVal);
    }
    if (const BPlistObj* fhObj = arc.FindArchiverVal(objectsArr, root, "flippedHorizontally")) {
        meta.flippedHorizontally = (fhObj->type == BPlistObj::Type::Bool) ? fhObj->bVal : (fhObj->iVal != 0);
    }
    if (const BPlistObj* fvObj = arc.FindArchiverVal(objectsArr, root, "flippedVertically")) {
        meta.flippedVertically = (fvObj->type == BPlistObj::Type::Bool) ? fvObj->bVal : (fvObj->iVal != 0);
    }

    if (const BPlistObj* bghObj = arc.FindArchiverVal(objectsArr, root, "backgroundHidden")) {
        meta.backgroundHidden = (bghObj->type == BPlistObj::Type::Bool) ? bghObj->bVal : (bghObj->iVal != 0);
    } else if (arc.FindArchiverVal(objectsArr, root, "backgroundColor") != nullptr) {
        meta.backgroundHidden = false;
    }

    if (const BPlistObj* bgcObj = arc.FindArchiverVal(objectsArr, root, "backgroundColor")) {
        const std::vector<uint8_t>* rawCol = nullptr;
        if (bgcObj->type == BPlistObj::Type::Data) rawCol = &bgcObj->dVal;
        else if (bgcObj->type == BPlistObj::Type::Dict) {
            if (const BPlistObj* nb = arc.FindArchiverVal(objectsArr, bgcObj, "NS.bytes")) {
                if (nb->type == BPlistObj::Type::Data) rawCol = &nb->dVal;
            }
        }
        if (rawCol && rawCol->size() >= 16u) {
            float rgba[4] = {};
            std::memcpy(rgba, rawCol->data(), 16);
            for (int c = 0; c < 4; ++c) {
                float v = std::isfinite(rgba[c]) ? std::clamp(rgba[c], 0.0f, 1.0f) : 1.0f;
                meta.bgRgba[c] = static_cast<uint8_t>(std::lround(v * 255.0f));
            }
        }
    }

    if (const BPlistObj* compObj = arc.FindArchiverVal(objectsArr, root, "composite")) {
        std::string cu = arc.ExtractString(objectsArr, arc.FindArchiverVal(objectsArr, compObj, "UUID"));
        if (!cu.empty()) meta.compositeUuidLower = PathUtils::ToLower(PathUtils::Utf8ToWide(cu));
    }

    if (const BPlistObj* layersObj = arc.FindArchiverVal(objectsArr, root, "unwrappedLayers")) {
        CollectSilicaLayersRecursive(arc, objectsArr, layersObj, false, 1.0f, meta.compositeUuidLower, meta.layersTopToBottom);
    }
    if (meta.layersTopToBottom.empty()) {
        if (const BPlistObj* layersObj = arc.FindArchiverVal(objectsArr, root, "layers")) {
            CollectSilicaLayersRecursive(arc, objectsArr, layersObj, false, 1.0f, meta.compositeUuidLower, meta.layersTopToBottom);
        }
    }

    meta.parsed = (meta.width > 0 && meta.height > 0) || !meta.layersTopToBottom.empty() || !meta.compositeUuidLower.empty();
    return meta.parsed;
}

inline DetailLayeredAndHdrImg::LayerBlendOp MapProcreateBlend(int blend) {
    using DetailLayeredAndHdrImg::LayerBlendOp;
    switch (blend) {
        case 1:  return LayerBlendOp::Multiply;
        case 2:  return LayerBlendOp::Screen;
        case 3:  return LayerBlendOp::Add;
        case 4:  return LayerBlendOp::Lighten;
        case 5:  return LayerBlendOp::Exclusion;
        case 6:  return LayerBlendOp::Difference;
        case 7:  return LayerBlendOp::Subtract;
        case 10: return LayerBlendOp::Darken;
        case 11: return LayerBlendOp::Overlay;
        case 12: return LayerBlendOp::HardLight;
        case 13: return LayerBlendOp::SoftLight;
        case 14: return LayerBlendOp::ColorDodge;
        case 15: return LayerBlendOp::ColorBurn;
        default: return LayerBlendOp::SrcOver;
    }
}

inline bool IsValidSquareTileBytes(size_t nBytes, int expectedTileSize, int& outSide) {
    if ((nBytes & 3u) != 0u || nBytes < 16u) return false;
    const size_t px = nBytes / 4u;
    if (expectedTileSize > 0) {
        if (px == static_cast<size_t>(expectedTileSize) * expectedTileSize) {
            outSide = expectedTileSize;
            return true;
        }
        return false;
    }
    const int side = static_cast<int>(std::llround(std::sqrt(static_cast<double>(px))));
    if (side >= 4 && side <= 1024 && (side & (side - 1)) == 0 && static_cast<size_t>(side) * side == px) {
        outSide = side;
        return true;
    }
    return false;
}

inline int UnpackProcreateTile(
    const uint8_t* zipData,
    size_t zipSize,
    const ZipDeflate::ArchiveItemMeta& entry,
    bool isLz4Ext,
    int expectedTileSize,
    std::vector<uint8_t>& outRgba)
{
    std::vector<uint8_t> raw;
    if (!ZipDeflate::ExtractZipEntryFromMemory(zipData, zipSize, entry, raw) || raw.size() < 4u) return 0;

    constexpr size_t kMaxTileBytes = 1024u * 1024u * 4u;
    const size_t expectedBytes = (expectedTileSize > 0)
                                     ? static_cast<size_t>(expectedTileSize) * expectedTileSize * 4u
                                     : 0u;
    int side = 0;

    // 1. Apple LZFSE / LZVN stream ("bvx1", "bvx2", "bvxn", "bvx-") used by Procreate 5+ .chunk files
    const bool hasBvxMagic = (raw.size() >= 4u && raw[0] == 'b' && raw[1] == 'v' && raw[2] == 'x' &&
                              (raw[3] == '1' || raw[3] == '2' || raw[3] == 'n' || raw[3] == '-'));
    if (hasBvxMagic) {
        if (::Explorer2::Lzfse::Decompress(raw.data(), raw.size(), outRgba, expectedBytes ? expectedBytes : kMaxTileBytes) &&
            IsValidSquareTileBytes(outRgba.size(), expectedTileSize, side))
        {
            return side;
        }
        return 0; // Never treat failed bvx stream as raw RGBA!
    }

    // 2. Standard LZ4 frame (04 22 4D 18)
    if (raw.size() >= 7u && raw[0] == 0x04u && raw[1] == 0x22u && raw[2] == 0x4Du && raw[3] == 0x18u) {
        if (::Explorer2::StreamCodecs::DecompressLz4Frame(raw.data(), raw.size(), outRgba, kMaxTileBytes) &&
            IsValidSquareTileBytes(outRgba.size(), expectedTileSize, side))
        {
            return side;
        }
        return 0;
    }

    // 3. Zlib stream
    if ((raw[0] & 0x0Fu) == 8u && (raw[0] >> 4) <= 7u && ((static_cast<uint32_t>(raw[0]) << 8) | raw[1]) % 31u == 0u) {
        if (ZipDeflate::InflateZlib(raw.data(), raw.size(), outRgba, kMaxTileBytes) &&
            IsValidSquareTileBytes(outRgba.size(), expectedTileSize, side))
        {
            return side;
        }
    }

    // 4. Raw LZ4 block (Procreate legacy .lz4 / .chunk)
    if (expectedBytes > 0u) {
        if (::Explorer2::Lz4::DecodeBlockExact(raw.data(), raw.size(), expectedBytes, outRgba) &&
            IsValidSquareTileBytes(outRgba.size(), expectedTileSize, side))
        {
            return side;
        }
    } else {
        outRgba.clear();
        if (::Explorer2::Lz4::DecodeBlockAppend(raw.data(), raw.size(), outRgba, 0, kMaxTileBytes, false) &&
            IsValidSquareTileBytes(outRgba.size(), 0, side))
        {
            return side;
        }
    }

    // 5. Headerless raw LZVN block (Procreate 5.x small tiles when expectedBytes is known from Document.archive)
    if (expectedBytes > 0u && !isLz4Ext) {
        if (::Explorer2::Lzfse::DecompressRawLzvn(raw.data(), raw.size(), static_cast<uint32_t>(expectedBytes), outRgba, expectedBytes) &&
            IsValidSquareTileBytes(outRgba.size(), expectedTileSize, side))
        {
            return side;
        }
    }

    // 6. Strictly validated uncompressed tile ONLY if Document.archive declared exact tileSize and size matches 1:1
    if (!isLz4Ext && expectedBytes > 0u && raw.size() == expectedBytes) {
        outRgba = std::move(raw);
        return expectedTileSize;
    }

    outRgba.clear();
    return 0;
}

inline void ApplyProcreateTransforms(
    std::vector<uint8_t>& pixels,
    uint32_t& w,
    uint32_t& h,
    bool flipH,
    bool flipV,
    int orientation)
{
    if (flipH && w > 1u) {
        for (uint32_t y = 0; y < h; ++y) {
            uint8_t* row = pixels.data() + static_cast<size_t>(y) * w * 4u;
            for (uint32_t x = 0; x < w / 2u; ++x) {
                uint8_t tmp[4];
                std::memcpy(tmp, row + x * 4u, 4);
                std::memcpy(row + x * 4u, row + (w - 1u - x) * 4u, 4);
                std::memcpy(row + (w - 1u - x) * 4u, tmp, 4);
            }
        }
    }
    if (flipV && h > 1u) {
        const size_t rowBytes = static_cast<size_t>(w) * 4u;
        std::vector<uint8_t> tmp(rowBytes);
        for (uint32_t y = 0; y < h / 2u; ++y) {
            uint8_t* r0 = pixels.data() + static_cast<size_t>(y) * rowBytes;
            uint8_t* r1 = pixels.data() + static_cast<size_t>(h - 1u - y) * rowBytes;
            std::memcpy(tmp.data(), r0, rowBytes);
            std::memcpy(r0, r1, rowBytes);
            std::memcpy(r1, tmp.data(), rowBytes);
        }
    }
    // Procreate Silica orientation: 0/1 = 0 deg, 2 = 180 deg, 3 = 90 deg CCW, 4 = 90 deg CW
    if (orientation == 2 && w > 0 && h > 0) {
        const size_t n = static_cast<size_t>(w) * h;
        for (size_t i = 0; i < n / 2u; ++i) {
            uint8_t tmp[4];
            std::memcpy(tmp, &pixels[i * 4u], 4);
            std::memcpy(&pixels[i * 4u], &pixels[(n - 1u - i) * 4u], 4);
            std::memcpy(&pixels[(n - 1u - i) * 4u], tmp, 4);
        }
    } else if ((orientation == 3 || orientation == 4) && w > 0 && h > 0) {
        const uint32_t dstW = h, dstH = w;
        std::vector<uint8_t> rot(pixels.size());
        for (uint32_t sy = 0; sy < h; ++sy) {
            for (uint32_t sx = 0; sx < w; ++sx) {
                const uint32_t dx = (orientation == 4) ? (h - 1u - sy) : sy;
                const uint32_t dy = (orientation == 4) ? sx : (w - 1u - sx);
                std::memcpy(
                    &rot[(static_cast<size_t>(dy) * dstW + dx) * 4u],
                    &pixels[(static_cast<size_t>(sy) * w + sx) * 4u],
                    4);
            }
        }
        pixels = std::move(rot);
        w = dstW;
        h = dstH;
    }
}

inline bool CompositeProcreateChunks(
    const uint8_t* zipData,
    size_t zipSize,
    const std::vector<ZipDeflate::ArchiveItemMeta>& entries,
    ExtendedImageInfo& outInfo)
{
    SilicaDocMeta doc;
    for (const auto& e : entries) {
        std::wstring nl = PathUtils::ToLower(e.pathInArchive);
        for (wchar_t& ch : nl) if (ch == L'\\') ch = L'/';
        if (nl == L"document.archive") {
            std::vector<uint8_t> docBytes;
            if (ZipDeflate::ExtractZipEntryFromMemory(zipData, zipSize, e, docBytes) && docBytes.size() >= 40u) {
                ParseSilicaDocumentArchive(docBytes.data(), docBytes.size(), doc);
            }
            break;
        }
    }

    struct ChunkRef {
        std::wstring uuidLower;
        int col = 0;
        int row = 0;
        bool isLz4Ext = false;
        const ZipDeflate::ArchiveItemMeta* meta = nullptr;
    };
    std::vector<ChunkRef> chunks;
    std::vector<std::wstring> discoveredUuids;
    int maxCol = 0, maxRow = 0;

    for (const auto& e : entries) {
        if (e.isDirectory) continue;
        std::wstring p = e.pathInArchive;
        for (wchar_t& ch : p) if (ch == L'\\') ch = L'/';
        std::wstring pl = PathUtils::ToLower(p);
        bool isChunk = (pl.size() > 6u && pl.substr(pl.size() - 6u) == L".chunk");
        bool isLz4   = (!isChunk && pl.size() > 4u && pl.substr(pl.size() - 4u) == L".lz4");
        if (!isChunk && !isLz4) continue;

        size_t slash = pl.find(L'/');
        if (slash == std::wstring::npos || slash == 0) continue;
        std::wstring uuidLow = pl.substr(0, slash);
        const size_t extLen = isChunk ? 6u : 4u;
        if (pl.size() <= slash + 1u + extLen) continue;
        std::wstring leaf = pl.substr(slash + 1u, pl.size() - slash - 1u - extLen);
        size_t tilde = leaf.find(L'~');
        if (tilde == std::wstring::npos || tilde == 0 || tilde + 1u >= leaf.size()) continue;

        int col = _wtoi(leaf.substr(0, tilde).c_str());
        int row = _wtoi(leaf.substr(tilde + 1u).c_str());
        if (col < 0 || col > 256 || row < 0 || row > 256) continue;
        maxCol = std::max(maxCol, col);
        maxRow = std::max(maxRow, row);
        if (std::find(discoveredUuids.begin(), discoveredUuids.end(), uuidLow) == discoveredUuids.end()) {
            discoveredUuids.push_back(uuidLow);
        }
        chunks.push_back({ std::move(uuidLow), col, row, isLz4, &e });
    }
    if (chunks.empty()) return false;

    int tileSize = doc.tileSize;
    if (tileSize <= 0) {
        std::vector<uint8_t> probe;
        for (const auto& ck : chunks) {
            tileSize = UnpackProcreateTile(zipData, zipSize, *ck.meta, ck.isLz4Ext, 0, probe);
            if (tileSize > 0) break;
        }
    }
    if (tileSize <= 0 || tileSize > 2048) return false;

    int imgW = doc.width;
    int imgH = doc.height;
    if (imgW <= 0 || imgH <= 0) {
        imgW = (maxCol + 1) * tileSize;
        imgH = (maxRow + 1) * tileSize;
    }
    if (imgW <= 0 || imgH <= 0 || imgW > 16384 || imgH > 16384) return false;

    // Build ordered render list (bottom-to-top) excluding composite layer
    std::vector<SilicaLayerSpec> renderOrderBottomUp;
    if (!doc.layersTopToBottom.empty()) {
        for (const auto& l : doc.layersTopToBottom) {
            if (outInfo.layerNames.size() < 64u) outInfo.layerNames.push_back(l.displayName);
        }
        for (auto it = doc.layersTopToBottom.rbegin(); it != doc.layersTopToBottom.rend(); ++it) {
            if (!it->hidden && it->opacity > 0.0f) {
                renderOrderBottomUp.push_back(*it);
            }
        }
    } else {
        for (const auto& u : discoveredUuids) {
            if ((!doc.compositeUuidLower.empty() && u == doc.compositeUuidLower) ||
                u.find(L"composite") != std::wstring::npos)
            {
                continue;
            }
            if (outInfo.layerNames.size() < 64u) outInfo.layerNames.push_back(u);
            renderOrderBottomUp.push_back({ u, u, false, 1.0f, 0 });
        }
    }

    std::vector<uint8_t> canvas(static_cast<size_t>(imgW) * imgH * 4u, 0u);
    if (!doc.backgroundHidden) {
        uint8_t bgPm[4];
        WritePremulBGRA(bgPm, doc.bgRgba[0], doc.bgRgba[1], doc.bgRgba[2], doc.bgRgba[3]);
        const size_t totalPx = static_cast<size_t>(imgW) * imgH;
        for (size_t i = 0; i < totalPx; ++i) {
            std::memcpy(&canvas[i * 4u], bgPm, 4);
        }
    }

    auto blitLayer = [&](const SilicaLayerSpec& lyr) -> size_t {
        size_t count = 0;
        const auto blendOp = MapProcreateBlend(lyr.blend);
        for (const auto& ck : chunks) {
            if (ck.uuidLower != lyr.uuidLower) continue;
            std::vector<uint8_t> tileRgba;
            if (UnpackProcreateTile(zipData, zipSize, *ck.meta, ck.isLz4Ext, tileSize, tileRgba) != tileSize) continue;

            // Detect whether tile stores straight RGBA (any RGB > A) or Procreate native premultiplied RGBA
            bool hasUnpremulSample = false;
            const size_t nPx = static_cast<size_t>(tileSize) * tileSize;
            for (size_t i = 0; i < nPx; ++i) {
                const uint8_t a = tileRgba[i * 4u + 3u];
                if (a < 255u && (tileRgba[i * 4u] > a || tileRgba[i * 4u + 1u] > a || tileRgba[i * 4u + 2u] > a)) {
                    hasUnpremulSample = true;
                    break;
                }
            }

            ++count;
            const int baseX = ck.col * tileSize;
            const int baseY = ck.row * tileSize;
            for (int ty = 0; ty < tileSize; ++ty) {
                const int dy = baseY + ty;
                if (dy < 0 || dy >= imgH) continue;
                for (int tx = 0; tx < tileSize; ++tx) {
                    const int dx = baseX + tx;
                    if (dx < 0 || dx >= imgW) continue;
                    const uint8_t* sp = &tileRgba[(static_cast<size_t>(ty) * tileSize + tx) * 4u];
                    if (sp[3] == 0u) continue;
                    uint8_t pm[4];
                    if (hasUnpremulSample) {
                        WritePremulBGRA(pm, sp[0], sp[1], sp[2], sp[3]);
                    } else {
                        pm[0] = sp[2]; // premul B
                        pm[1] = sp[1]; // premul G
                        pm[2] = sp[0]; // premul R
                        pm[3] = sp[3]; // A
                    }
                    DetailLayeredAndHdrImg::BlendPremulBgraPixel(
                        &canvas[(static_cast<size_t>(dy) * imgW + dx) * 4u],
                        pm[0], pm[1], pm[2], pm[3], lyr.opacity, blendOp);
                }
            }
        }
        return count;
    };

    size_t blitted = 0;
    for (const auto& lyr : renderOrderBottomUp) {
        blitted += blitLayer(lyr);
    }

    // Use standalone composite layer ONLY when 0 regular layers produced tiles
    if (blitted == 0 && !doc.compositeUuidLower.empty()) {
        SilicaLayerSpec compSpec{ doc.compositeUuidLower, L"Composite", false, 1.0f, 0 };
        blitted += blitLayer(compSpec);
    }
    if (blitted == 0) return false;

    uint32_t outW = static_cast<uint32_t>(imgW);
    uint32_t outH = static_cast<uint32_t>(imgH);
    ApplyProcreateTransforms(canvas, outW, outH, doc.flippedHorizontally, doc.flippedVertically, doc.orientation);

    const size_t reportedLayers = !doc.layersTopToBottom.empty() ? doc.layersTopToBottom.size() : renderOrderBottomUp.size();
    outInfo.width = outW;
    outInfo.height = outH;
    outInfo.origWidth = outW;
    outInfo.origHeight = outH;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Procreate Silica Tile Artwork (.procreate, " + std::to_wstring(reportedLayers) + L" layers)";
    outInfo.bgraPixels = std::move(canvas);
    outInfo.valid = true;
    return true;
}

} // namespace DetailProcreate

inline bool DecodeProcreateImage(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 30 || data[0] != 'P' || data[1] != 'K') return false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;
    if (DetailProcreate::CompositeProcreateChunks(data, size, entries, outInfo)) {
        return true;
    }
    return DetailArtFont::DecodeZipArtworkFallback(data, size, entries, L"Procreate Artwork (.procreate)", outInfo);
}

} // namespace Formats
