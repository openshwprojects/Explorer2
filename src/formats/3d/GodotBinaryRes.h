#pragma once
// GodotBinaryRes.h — Godot Engine 3.x & 4.x Binary Scenes & Mesh Resources (.scn, .res, .mesh).
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace GodotDetail {

// ============================================================================
// Godot Binary Resource / Scene ("RSRC" & "RSCC" — .res, .scn, binary .mesh)
// ============================================================================
struct GdBinReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    bool be = false;
    bool real64 = false;
    uint32_t verMajor = 4;

    bool CanRead(size_t n) const { return pos <= size && n <= size - pos; }
    bool ReadU32(uint32_t& v) { if (!CanRead(4)) return false; v = GdU32(data + pos, be); pos += 4; return true; }
    bool ReadU64(uint64_t& v) { if (!CanRead(8)) return false; v = GdU64(data + pos, be); pos += 8; return true; }
    bool ReadF32(float& v)    { if (!CanRead(4)) return false; v = GdF32(data + pos, be); pos += 4; return true; }
    bool ReadReal(float& v) {
        if (real64) {
            if (!CanRead(8)) return false;
            v = static_cast<float>(GdF64(data + pos, be));
            pos += 8;
            return std::isfinite(v);
        }
        if (!ReadF32(v)) return false;
        return std::isfinite(v);
    }
    bool ReadUnicodeString(std::string& out) {
        uint32_t len = 0;
        if (!ReadU32(len) || len > 1048576 || !CanRead(len)) return false;
        out.assign(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        while (!out.empty() && out.back() == '\0') out.pop_back();
        return true;
    }
};

struct GdBinVal {
    enum Kind { kOther, kInt, kFloat, kStr, kVec3, kAabb, kXform, kRawBytes, kIntArr, kVec3Arr, kSubResIdx, kDict, kArr } kind = kOther;
    int64_t iVal = 0;
    float fVal = 0.0f;
    std::string sVal;
    float f12[12] = {};
    std::vector<uint8_t> rawBytes;
    std::vector<int32_t> intArr;
    std::vector<Vec3f> vec3Arr;
    std::vector<std::pair<std::string, GdBinVal>> dict;
    std::vector<GdBinVal> arr;
};

inline bool ParseGodotBinaryVariant(
    GdBinReader& r,
    const std::vector<std::string>& strTable,
    GdBinVal& out,
    int depth = 0)
{
    if (depth > 24) return false;
    uint32_t typeTag = 0;
    if (!r.ReadU32(typeTag)) return false;
    const bool v4 = (r.verMajor >= 4);

    auto resolveStrOrName = [&](std::string& sOut) -> bool {
        uint32_t v = 0;
        if (!r.ReadU32(v)) return false;
        if (v & 0x80000000u) {
            uint32_t idx = v & 0x7FFFFFFFu;
            if (idx < strTable.size()) sOut = strTable[idx];
            return true;
        }
        r.pos -= 4;
        return r.ReadUnicodeString(sOut);
    };

    if (typeTag == 1) { // NIL
        out.kind = GdBinVal::kOther;
        return true;
    }
    if (typeTag == 2 || typeTag == 3) { // BOOL / INT32
        uint32_t iv = 0;
        if (!r.ReadU32(iv)) return false;
        out.kind = GdBinVal::kInt;
        out.iVal = static_cast<int32_t>(iv);
        return true;
    }
    if (typeTag == 40) { // INT64
        uint64_t iv = 0;
        if (!r.ReadU64(iv)) return false;
        out.kind = GdBinVal::kInt;
        out.iVal = static_cast<int64_t>(iv);
        return true;
    }
    if (typeTag == 4) { // FLOAT (real)
        float fv = 0.0f;
        if (!r.ReadReal(fv)) return false;
        out.kind = GdBinVal::kFloat;
        out.fVal = fv;
        return true;
    }
    if (typeTag == 41) { // DOUBLE
        if (!r.CanRead(8)) return false;
        out.kind = GdBinVal::kFloat;
        out.fVal = static_cast<float>(GdF64(r.data + r.pos, r.be));
        r.pos += 8;
        return std::isfinite(out.fVal);
    }
    if (typeTag == 5 || (v4 && typeTag == 44)) { // STRING / STRING_NAME
        out.kind = GdBinVal::kStr;
        if (v4 && typeTag == 44) return resolveStrOrName(out.sVal);
        return r.ReadUnicodeString(out.sVal);
    }
    if (typeTag == 6) { float a, b; return r.ReadReal(a) && r.ReadReal(b); } // VECTOR2
    if (typeTag == 7 || typeTag == 10 || typeTag == 11 || (v4 && typeTag == 50)) { // RECT2 / PLANE / QUAT / VECTOR4
        float a, b, c, d;
        return r.ReadReal(a) && r.ReadReal(b) && r.ReadReal(c) && r.ReadReal(d);
    }
    if (typeTag == 8) { // VECTOR3
        out.kind = GdBinVal::kVec3;
        return r.ReadReal(out.f12[0]) && r.ReadReal(out.f12[1]) && r.ReadReal(out.f12[2]);
    }
    if (typeTag == 9) { // TRANSFORM2D (6 reals)
        float t;
        for (int i = 0; i < 6; ++i) if (!r.ReadReal(t)) return false;
        return true;
    }
    if (typeTag == 12) { // AABB (6 reals: pos + size)
        out.kind = GdBinVal::kAabb;
        for (int i = 0; i < 6; ++i) if (!r.ReadReal(out.f12[i])) return false;
        return true;
    }
    if (typeTag == 13) { // BASIS (9 reals)
        float t;
        for (int i = 0; i < 9; ++i) if (!r.ReadReal(t)) return false;
        return true;
    }
    if (typeTag == 14) { // TRANSFORM3D (12 reals: 9 basis + 3 origin)
        out.kind = GdBinVal::kXform;
        // Binary RSRC stores Basis rows (row0[3], row1[3], row2[3]) + origin[3]:
        float rf[12] = {};
        for (int i = 0; i < 12; ++i) if (!r.ReadReal(rf[i])) return false;
        // Convert Basis rows + origin into our column-compatible f12 layout used by From12Floats:
        // Basis row0=(m00,m01,m02), row1=(m10,m11,m12), row2=(m20,m21,m22); From12Floats expects cols (m00,m10,m20, m01,m11,m21, m02,m12,m22, ox,oy,oz)
        out.f12[0] = rf[0]; out.f12[1] = rf[3]; out.f12[2] = rf[6];
        out.f12[3] = rf[1]; out.f12[4] = rf[4]; out.f12[5] = rf[7];
        out.f12[6] = rf[2]; out.f12[7] = rf[5]; out.f12[8] = rf[8];
        out.f12[9] = rf[9]; out.f12[10] = rf[10]; out.f12[11] = rf[11];
        return true;
    }
    if (typeTag == 15) { // COLOR (Always 4 float32s in Godot RSRC!)
        float c;
        for (int i = 0; i < 4; ++i) if (!r.ReadF32(c)) return false;
        return true;
    }
    // NODE_PATH (17 in G3, 16 in G4)
    if ((!v4 && typeTag == 17) || (v4 && typeTag == 16)) {
        uint32_t npHead = 0;
        if (!r.ReadU32(npHead)) return false;
        if (npHead & 0x80000000u) {
            uint32_t nNames = npHead & 0x7FFFFFFFu;
            uint32_t nSub = 0, npFlags = 0;
            if (!r.ReadU32(nSub) || !r.ReadU32(npFlags)) return false;
            if ((nNames + nSub) > 4096) return false;
            for (uint32_t i = 0; i < nNames + nSub; ++i) {
                std::string dummy;
                if (!resolveStrOrName(dummy)) return false;
            }
            return true;
        }
        r.pos -= 4;
        std::string s;
        return r.ReadUnicodeString(s);
    }
    // RID (18 in G3, 17 in G4)
    if ((!v4 && typeTag == 18) || (v4 && typeTag == 17)) {
        uint32_t id = 0;
        return r.ReadU32(id);
    }
    // OBJECT (19 in G3, 18 in G4)
    if ((!v4 && typeTag == 19) || (v4 && typeTag == 18)) {
        uint32_t objKind = 0;
        if (!r.ReadU32(objKind)) return false;
        if (objKind == 0) return true; // OBJECT_EMPTY
        if (objKind == 1) { // OBJECT_EXTERNAL_RESOURCE
            std::string t, p;
            return r.ReadUnicodeString(t) && r.ReadUnicodeString(p);
        }
        if (objKind == 2) { // OBJECT_INTERNAL_RESOURCE
            uint32_t idx = 0;
            if (!r.ReadU32(idx)) return false;
            out.kind = GdBinVal::kSubResIdx;
            out.iVal = idx;
            return true;
        }
        if (objKind == 3) { // OBJECT_EXTERNAL_RESOURCE_INDEX
            uint32_t idx = 0;
            return r.ReadU32(idx);
        }
        return false;
    }
    // DICTIONARY (21 in G3, 19 in G4)
    if ((!v4 && typeTag == 21) || (v4 && typeTag == 19)) {
        uint32_t cnt = 0;
        if (!r.ReadU32(cnt)) return false;
        cnt &= 0x7FFFFFFFu;
        if (cnt > 65536) return false;
        out.kind = GdBinVal::kDict;
        out.dict.reserve(cnt);
        for (uint32_t i = 0; i < cnt; ++i) {
            GdBinVal k, v;
            if (!ParseGodotBinaryVariant(r, strTable, k, depth + 1)) return false;
            if (!ParseGodotBinaryVariant(r, strTable, v, depth + 1)) return false;
            out.dict.push_back({ k.sVal, std::move(v) });
        }
        return true;
    }
    // ARRAY (30 in G3, 20 in G4)
    if ((!v4 && typeTag == 30) || (v4 && typeTag == 20)) {
        uint32_t cnt = 0;
        if (!r.ReadU32(cnt)) return false;
        cnt &= 0x7FFFFFFFu;
        if (cnt > 200000) return false;
        out.kind = GdBinVal::kArr;
        out.arr.resize(cnt);
        for (uint32_t i = 0; i < cnt; ++i) {
            if (!ParseGodotBinaryVariant(r, strTable, out.arr[i], depth + 1)) return false;
        }
        return true;
    }
    // RAW_ARRAY / PackedByteArray (31 in G3, 21 in G4)
    if ((!v4 && typeTag == 31) || (v4 && typeTag == 21)) {
        uint32_t len = 0;
        if (!r.ReadU32(len) || len > 128 * 1024 * 1024 || !r.CanRead(len)) return false;
        out.kind = GdBinVal::kRawBytes;
        out.rawBytes.assign(r.data + r.pos, r.data + r.pos + len);
        r.pos += len;
        size_t pad = (4 - (len & 3u)) & 3u;
        if (r.CanRead(pad)) r.pos += pad;
        return true;
    }
    // INT32_ARRAY (32 in G3, 22 in G4)
    if ((!v4 && typeTag == 32) || (v4 && typeTag == 22)) {
        uint32_t len = 0;
        if (!r.ReadU32(len) || len > 16 * 1024 * 1024 || !r.CanRead(static_cast<size_t>(len) * 4)) return false;
        out.kind = GdBinVal::kIntArr;
        out.intArr.resize(len);
        for (uint32_t i = 0; i < len; ++i) out.intArr[i] = static_cast<int32_t>(GdU32(r.data + r.pos + i * 4, r.be));
        r.pos += static_cast<size_t>(len) * 4;
        return true;
    }
    // FLOAT32_ARRAY (33 in G3, 23 in G4)
    if ((!v4 && typeTag == 33) || (v4 && typeTag == 23)) {
        uint32_t len = 0;
        if (!r.ReadU32(len) || len > 16 * 1024 * 1024 || !r.CanRead(static_cast<size_t>(len) * 4)) return false;
        r.pos += static_cast<size_t>(len) * 4;
        return true;
    }
    // STRING_ARRAY (34 in G3, 24 in G4)
    if ((!v4 && typeTag == 34) || (v4 && typeTag == 24)) {
        uint32_t len = 0;
        if (!r.ReadU32(len) || len > 1000000) return false;
        std::string tmp;
        for (uint32_t i = 0; i < len; ++i) if (!r.ReadUnicodeString(tmp)) return false;
        return true;
    }
    // VECTOR3_ARRAY (35 in G3, 25 in G4)
    if ((!v4 && typeTag == 35) || (v4 && typeTag == 25)) {
        uint32_t len = 0;
        size_t elemSz = r.real64 ? 24u : 12u;
        if (!r.ReadU32(len) || len > 4000000 || !r.CanRead(static_cast<size_t>(len) * elemSz)) return false;
        out.kind = GdBinVal::kVec3Arr;
        out.vec3Arr.resize(len);
        for (uint32_t i = 0; i < len; ++i) {
            float x, y, z;
            if (!r.ReadReal(x) || !r.ReadReal(y) || !r.ReadReal(z)) return false;
            out.vec3Arr[i] = { x, y, z };
        }
        return true;
    }
    // COLOR_ARRAY (36 in G3, 26 in G4)
    if ((!v4 && typeTag == 36) || (v4 && typeTag == 26)) {
        uint32_t len = 0;
        if (!r.ReadU32(len) || len > 4000000 || !r.CanRead(static_cast<size_t>(len) * 16)) return false;
        r.pos += static_cast<size_t>(len) * 16;
        return true;
    }
    // VECTOR2_ARRAY (37 in G3, 27 in G4)
    if ((!v4 && typeTag == 37) || (v4 && typeTag == 27)) {
        uint32_t len = 0;
        size_t elemSz = r.real64 ? 16u : 8u;
        if (!r.ReadU32(len) || len > 4000000 || !r.CanRead(static_cast<size_t>(len) * elemSz)) return false;
        r.pos += static_cast<size_t>(len) * elemSz;
        return true;
    }
    if (v4 && (typeTag == 45 || typeTag == 46 || typeTag == 47 || typeTag == 51)) {
        size_t nInts = (typeTag == 45) ? 2u : (typeTag == 47 ? 3u : 4u);
        if (!r.CanRead(nInts * 4)) return false;
        r.pos += nInts * 4;
        return true;
    }
    if (v4 && (typeTag == 48 || typeTag == 49)) { // INT64_ARRAY / FLOAT64_ARRAY
        uint32_t len = 0;
        if (!r.ReadU32(len) || len > 8000000 || !r.CanRead(static_cast<size_t>(len) * 8)) return false;
        r.pos += static_cast<size_t>(len) * 8;
        return true;
    }
    return false;
}

inline bool ExtractGodotBinarySurfaceDict(const GdBinVal& d, GdLocalMesh& outMesh) {
    if (d.kind != GdBinVal::kDict) return false;
    // Check Godot 4 surface dictionary ("vertex_data", "vertex_count", "index_data", "index_count", "aabb")
    const std::vector<uint8_t>* vData = nullptr;
    const std::vector<uint8_t>* iData = nullptr;
    uint32_t vCnt = 0, iCnt = 0;
    float aabb[6] = { -0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f };
    bool hasAabb = false;

    // Also check Godot 3 surface dictionary ("arrays": Array where [0] = VECTOR3_ARRAY and [8 or 12] = INT_ARRAY)
    const GdBinVal* g3Arrays = nullptr;

    for (const auto& kv : d.dict) {
        if (kv.first == "vertex_data" && kv.second.kind == GdBinVal::kRawBytes) vData = &kv.second.rawBytes;
        else if (kv.first == "index_data" && kv.second.kind == GdBinVal::kRawBytes) iData = &kv.second.rawBytes;
        else if (kv.first == "vertex_count" && kv.second.kind == GdBinVal::kInt && kv.second.iVal > 0) vCnt = static_cast<uint32_t>(kv.second.iVal);
        else if (kv.first == "index_count" && kv.second.kind == GdBinVal::kInt && kv.second.iVal > 0) iCnt = static_cast<uint32_t>(kv.second.iVal);
        else if (kv.first == "aabb" && kv.second.kind == GdBinVal::kAabb) {
            for (int k = 0; k < 6; ++k) aabb[k] = kv.second.f12[k];
            hasAabb = true;
        }
        else if (kv.first == "arrays" && kv.second.kind == GdBinVal::kArr) g3Arrays = &kv.second;
    }

    if (vData && vCnt >= 3) {
        static const std::vector<uint8_t> kEmpty;
        return DecodeGodot4SurfaceRawBytes(*vData, vCnt, iData ? *iData : kEmpty, iCnt, aabb, hasAabb, outMesh);
    }

    if (g3Arrays && !g3Arrays->arr.empty() && g3Arrays->arr[0].kind == GdBinVal::kVec3Arr) {
        const auto& va = g3Arrays->arr[0].vec3Arr;
        if (va.size() < 3) return false;
        uint32_t baseV = static_cast<uint32_t>(outMesh.verts.size());
        outMesh.verts.insert(outMesh.verts.end(), va.begin(), va.end());
        const std::vector<int32_t>* ia = nullptr;
        // ARRAY_INDEX = 8 in Godot 3, 12 in Godot 4
        if (g3Arrays->arr.size() > 12 && g3Arrays->arr[12].kind == GdBinVal::kIntArr) ia = &g3Arrays->arr[12].intArr;
        else if (g3Arrays->arr.size() > 8 && g3Arrays->arr[8].kind == GdBinVal::kIntArr) ia = &g3Arrays->arr[8].intArr;
        if (ia && ia->size() >= 3) {
            for (size_t t = 0; t + 2 < ia->size(); t += 3) {
                int32_t i0 = (*ia)[t], i1 = (*ia)[t + 1], i2 = (*ia)[t + 2];
                if (i0 < 0 || i1 < 0 || i2 < 0 ||
                    static_cast<size_t>(i0) >= va.size() ||
                    static_cast<size_t>(i1) >= va.size() ||
                    static_cast<size_t>(i2) >= va.size()) return false;
                if (i0 != i1 && i1 != i2 && i2 != i0) {
                    outMesh.tris.push_back({ baseV + static_cast<uint32_t>(i0), baseV + static_cast<uint32_t>(i1), baseV + static_cast<uint32_t>(i2) });
                }
            }
        } else {
            for (uint32_t t = 0; t + 2 < va.size(); t += 3) outMesh.tris.push_back({ baseV + t, baseV + t + 1, baseV + t + 2 });
        }
        return !outMesh.empty();
    }

    return false;
}

inline bool DecompressGodotRscc(const uint8_t* data, size_t size, std::vector<uint8_t>& outRsrc) {
    outRsrc.clear();
    if (!data || size < 24 || std::memcmp(data, "RSCC", 4) != 0) return false;
    uint32_t compMode  = GdU32(data + 4, false);
    uint32_t blockSize = GdU32(data + 8, false);
    uint32_t readTotal = GdU32(data + 12, false);
    if (blockSize == 0 || blockSize > 16 * 1024 * 1024 || readTotal < 24 || readTotal > 256 * 1024 * 1024) {
        return false;
    }
    uint32_t blockCount = (readTotal + blockSize - 1u) / blockSize;
    if (blockCount == 0 || blockCount > 262144 || 16ULL + static_cast<uint64_t>(blockCount) * 4ULL > size) {
        return false;
    }

    // Godot FileAccessCompressed stores "RSCC" header in place of "RSRC", compressing everything after "RSRC" (or full "RSRC")
    std::vector<uint8_t> rawOut;
    rawOut.reserve(readTotal);
    size_t cPos = 16 + static_cast<size_t>(blockCount) * 4;
    for (uint32_t b = 0; b < blockCount; ++b) {
        uint32_t cSz = GdU32(data + 16 + static_cast<size_t>(b) * 4, false);
        if (cPos + cSz > size) return false;
        uint32_t expectUnp = (b + 1 == blockCount) ? (readTotal - b * blockSize) : blockSize;
        std::vector<uint8_t> chunk;
        if (compMode == 1) { // COMPRESSION_DEFLATE (zlib stream in Godot!)
            if (!ZipDeflate::InflateZlib(data + cPos, cSz, chunk) &&
                !ZipDeflate::InflateRaw(data + cPos, cSz, chunk)) {
                return false;
            }
        } else if (compMode == 2) { // COMPRESSION_ZSTD
            if (!StreamCodecs::DecompressZstdFrame(data + cPos, cSz, chunk, expectUnp + 64)) return false;
        } else if (compMode == 3) { // COMPRESSION_GZIP
            if (!ZipDeflate::InflateGzip(data + cPos, cSz, chunk)) return false;
        } else {
            return false;
        }
        if (chunk.size() != expectUnp) return false;
        rawOut.insert(rawOut.end(), chunk.begin(), chunk.end());
        cPos += cSz;
    }

    if (rawOut.size() >= 4 && std::memcmp(rawOut.data(), "RSRC", 4) == 0) {
        outRsrc = std::move(rawOut);
    } else {
        outRsrc.reserve(4 + rawOut.size());
        outRsrc.push_back('R'); outRsrc.push_back('S'); outRsrc.push_back('R'); outRsrc.push_back('C');
        outRsrc.insert(outRsrc.end(), rawOut.begin(), rawOut.end());
    }
    return true;
}

inline bool ParseGodotBinaryRsrc(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    std::vector<uint8_t> decomp;
    if (size >= 24 && std::memcmp(data, "RSCC", 4) == 0) {
        if (!DecompressGodotRscc(data, size, decomp)) return false;
        data = decomp.data();
        size = decomp.size();
    }
    if (!data || size < 64 || std::memcmp(data, "RSRC", 4) != 0) return false;

    GdBinReader r;
    r.data = data;
    r.size = size;
    r.pos  = 4;
    uint32_t bigEnd = 0;
    if (GdU32(data + 4, false) > 1 && GdU32(data + 4, true) == 1) bigEnd = 1;
    else bigEnd = GdU32(data + 4, false);
    if (bigEnd > 1) return false;
    r.be = (bigEnd != 0);
    r.pos = 8;

    uint32_t real64Flag = 0, verMaj = 0, verMin = 0, verFmt = 0;
    if (!r.ReadU32(real64Flag) || !r.ReadU32(verMaj) || !r.ReadU32(verMin) || !r.ReadU32(verFmt)) return false;
    if (real64Flag > 1 || verMaj < 2 || verMaj > 5 || verFmt < 1 || verFmt > 12) return false;
    r.real64 = (real64Flag != 0);
    r.verMajor = verMaj;

    std::string mainResType;
    uint64_t importMdOfs = 0;
    uint32_t flags = 0;
    if (!r.ReadUnicodeString(mainResType) || !r.ReadU64(importMdOfs) || !r.ReadU32(flags)) return false;

    // Skip UID(8B) + optional script_class string + 9 reserved uint32s (= 44B without script_class, matching Godot 3's 11 reserved uint32s)
    if (!r.CanRead(8)) return false;
    r.pos += 8;
    if (verMaj >= 4 && (flags & 8u) != 0) {
        std::string scriptCls;
        if (!r.ReadUnicodeString(scriptCls)) return false;
    }
    if (!r.CanRead(36)) return false;
    r.pos += 36;

    uint32_t strCount = 0;
    if (!r.ReadU32(strCount) || strCount > 500000) return false;
    std::vector<std::string> strTable(strCount);
    for (uint32_t i = 0; i < strCount; ++i) {
        if (!r.ReadUnicodeString(strTable[i])) return false;
    }

    uint32_t extCount = 0;
    if (!r.ReadU32(extCount) || extCount > 100000) return false;
    for (uint32_t i = 0; i < extCount; ++i) {
        std::string et, ep;
        if (!r.ReadUnicodeString(et) || !r.ReadUnicodeString(ep)) return false;
        if (verMaj >= 4 && (flags & 2u) != 0) {
            uint64_t uid = 0;
            if (!r.ReadU64(uid)) return false;
        }
    }

    uint32_t intCount = 0;
    if (!r.ReadU32(intCount) || intCount == 0 || intCount > 100000) return false;
    struct IntResEntry {
        std::string path;
        uint64_t offset = 0;
        uint32_t localIndex = 0;
    };
    std::vector<IntResEntry> intEntries(intCount);
    for (uint32_t i = 0; i < intCount; ++i) {
        if (!r.ReadUnicodeString(intEntries[i].path) || !r.ReadU64(intEntries[i].offset)) return false;
        if (intEntries[i].offset < r.pos || intEntries[i].offset >= size) return false;
        intEntries[i].localIndex = i + 1;
    }

    std::unordered_map<uint32_t, GdLocalMesh> parsedIntMeshes;
    bool hasBundledScene = false;

    for (uint32_t i = 0; i < intCount; ++i) {
        r.pos = static_cast<size_t>(intEntries[i].offset);
        std::string rType;
        uint32_t propCount = 0;
        if (!r.ReadUnicodeString(rType) || !r.ReadU32(propCount) || propCount > 65536) return false;

        std::vector<std::pair<std::string, GdBinVal>> props;
        props.reserve(propCount);
        for (uint32_t p = 0; p < propCount; ++p) {
            uint32_t nameIdx = 0;
            if (!r.ReadU32(nameIdx) || nameIdx >= strTable.size()) return false;
            GdBinVal val;
            if (!ParseGodotBinaryVariant(r, strTable, val, 0)) return false;
            props.push_back({ strTable[nameIdx], std::move(val) });
        }

        GdLocalMesh lm;
        if (rType == "ArrayMesh" || rType == "ImporterMesh" || rType == "Mesh") {
            for (const auto& pr : props) {
                if ((pr.first == "_surfaces" || pr.first == "surfaces") && pr.second.kind == GdBinVal::kArr) {
                    for (const auto& sEl : pr.second.arr) {
                        if (!ExtractGodotBinarySurfaceDict(sEl, lm)) return false;
                    }
                } else if (pr.first.rfind("surfaces/", 0) == 0 && pr.second.kind == GdBinVal::kDict) {
                    if (!ExtractGodotBinarySurfaceDict(pr.second, lm)) return false;
                }
            }
        } else if (rType == "BoxMesh" || rType == "CubeMesh") {
            float sx = 1.0f, sy = 1.0f, sz = 1.0f;
            for (const auto& pr : props) {
                if (pr.first == "size" && pr.second.kind == GdBinVal::kVec3) {
                    sx = pr.second.f12[0]; sy = pr.second.f12[1]; sz = pr.second.f12[2];
                }
            }
            BuildBoxPrimitive(sx, sy, sz, lm);
        } else if (rType == "SphereMesh" || rType == "CapsuleMesh") {
            float rad = 0.5f, h = 1.0f;
            for (const auto& pr : props) {
                if (pr.first == "radius" && pr.second.kind == GdBinVal::kFloat) rad = pr.second.fVal;
                else if (pr.first == "height" && pr.second.kind == GdBinVal::kFloat) h = pr.second.fVal;
            }
            BuildSpherePrimitive(rad, h, lm);
        } else if (rType == "CylinderMesh") {
            float tr = 0.5f, br = 0.5f, h = 2.0f;
            for (const auto& pr : props) {
                if (pr.first == "top_radius" && pr.second.kind == GdBinVal::kFloat) tr = pr.second.fVal;
                else if (pr.first == "bottom_radius" && pr.second.kind == GdBinVal::kFloat) br = pr.second.fVal;
                else if (pr.first == "height" && pr.second.kind == GdBinVal::kFloat) h = pr.second.fVal;
            }
            BuildCylinderOrConePrimitive(tr, br, h, 16, lm);
        } else if (rType == "PackedScene") {
            // PackedScene "_bundled" Dictionary: "names" (StringArray or Array), "variants" (Array), "nodes" (Int32Array)
            for (const auto& pr : props) {
                if (pr.first == "_bundled" && pr.second.kind == GdBinVal::kDict) {
                    const GdBinVal* variantsVal = nullptr;
                    const std::vector<int32_t>* nodesArr = nullptr;
                    for (const auto& bk : pr.second.dict) {
                        if (bk.first == "variants" && bk.second.kind == GdBinVal::kArr) variantsVal = &bk.second;
                        else if (bk.first == "nodes" && bk.second.kind == GdBinVal::kIntArr) nodesArr = &bk.second.intArr;
                    }
                    if (variantsVal && nodesArr && nodesArr->size() >= 5) {
                        hasBundledScene = true;
                        // Walk PackedScene packed int stream to pair Transform3D + SubResource mesh per node!
                        size_t ni = 0;
                        std::vector<GdXform3D> nodeWorld;
                        while (ni + 5 <= nodesArr->size()) {
                            int32_t parentIdx = (*nodesArr)[ni + 0];
                            int32_t ownerIdx  = (*nodesArr)[ni + 1];
                            (void)ownerIdx;
                            int32_t nPropCnt  = (*nodesArr)[ni + 4];
                            ni += 5;
                            if (nPropCnt < 0 || ni + static_cast<size_t>(nPropCnt) * 2 + 1 > nodesArr->size()) break;

                            GdXform3D localXf{};
                            uint32_t meshSubIdx = 0;
                            for (int32_t pi = 0; pi < nPropCnt; ++pi) {
                                int32_t vIdx = (*nodesArr)[ni + static_cast<size_t>(pi) * 2 + 1];
                                if (vIdx >= 0 && static_cast<size_t>(vIdx) < variantsVal->arr.size()) {
                                    const GdBinVal& vv = variantsVal->arr[static_cast<size_t>(vIdx)];
                                    if (vv.kind == GdBinVal::kXform) localXf = GdXform3D::From12Floats(vv.f12);
                                    else if (vv.kind == GdBinVal::kSubResIdx) meshSubIdx = static_cast<uint32_t>(vv.iVal);
                                }
                            }
                            ni += static_cast<size_t>(nPropCnt) * 2;
                            int32_t nGroups = (*nodesArr)[ni++];
                            if (nGroups < 0 || ni + static_cast<size_t>(nGroups) > nodesArr->size()) break;
                            ni += static_cast<size_t>(nGroups);

                            int32_t realParent = (parentIdx >= 0) ? (parentIdx & 0x7FFFFFFF) : -1;
                            GdXform3D wxf = (realParent >= 0 && static_cast<size_t>(realParent) < nodeWorld.size())
                                ? GdXform3D::Mul(nodeWorld[static_cast<size_t>(realParent)], localXf)
                                : localXf;
                            nodeWorld.push_back(wxf);

                            auto mit = parsedIntMeshes.find(meshSubIdx);
                            if (mit != parsedIntMeshes.end() && !mit->second.empty()) {
                                uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                                for (const auto& v : mit->second.verts) mesh.vertices.push_back(wxf.Apply(v));
                                for (const auto& t : mit->second.tris) mesh.tris.push_back({ baseV + t[0], baseV + t[1], baseV + t[2], {} });
                            }
                        }
                    }
                }
            }
        }

        if (!lm.empty()) {
            parsedIntMeshes[intEntries[i].localIndex] = std::move(lm);
        }
    }

    if (mesh.vertices.empty()) {
        for (uint32_t i = 0; i < intCount; ++i) {
            auto it = parsedIntMeshes.find(intEntries[i].localIndex);
            if (it == parsedIntMeshes.end() || it->second.empty()) continue;
            uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
            for (const auto& v : it->second.verts) mesh.vertices.push_back(v);
            for (const auto& t : it->second.tris) mesh.tris.push_back({ baseV + t[0], baseV + t[1], baseV + t[2], {} });
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = hasBundledScene
        ? L"Godot Engine Binary Scene (.scn)"
        : L"Godot Engine Binary Mesh Resource (.res / .mesh)";
    return true;
}

} // namespace GodotDetail

E2_DRACO_NOINLINE inline bool ParseGodotBinaryRes(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    return GodotDetail::ParseGodotBinaryRsrc(data, size, mesh);
}

} // namespace Formats
