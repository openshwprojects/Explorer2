#pragma once
// RadGrannyGr2.h — RAD Game Tools Granny 3D Model Container (.gr2 v6 & v7, 32-bit & 64-bit)
//
// Full spec-accurate metadata-driven implementation (zero `size % 32 == 0` section guessing):
//   - Supports single-section (`secArrCount >= 1`) and multi-section `.gr2` files.
//   - Decompresses uncompressed (`compression == 0`) and LZ/LZF-compressed (`compression == 1`) sections.
//   - Applies `GrannyPointerFixup` cross-section pointer relocation tables.
//   - Walks `RootObject` via `GrannyDataTypeDefinition` reflection:
//     `Meshes -> PrimaryVertexData -> Vertices` (exact `VertexType` stride & `Position` field offset)
//     + `PrimaryTopology -> Groups / Indices / Indices16` + optional `Models -> InitialPlacement`!

namespace Formats {

namespace DetailGrannyGr2 {

static constexpr uint8_t kMagicV6_32LE[16] = {
    0xB8, 0x67, 0xB0, 0xCA, 0xF8, 0x6D, 0xB1, 0x0F,
    0x84, 0x72, 0x8C, 0x7E, 0x5E, 0x19, 0x00, 0x1E
};
static constexpr uint8_t kMagicV7_32LE[16] = {
    0x29, 0xDE, 0x6C, 0xC0, 0xBA, 0xA4, 0x53, 0x2B,
    0x25, 0xF5, 0xB7, 0xA5, 0xF6, 0x66, 0xE2, 0xEE
};
static constexpr uint8_t kMagicV7Alt_32LE[16] = {
    0x29, 0x75, 0x31, 0x82, 0xBA, 0x02, 0x11, 0x77,
    0x25, 0x3A, 0x60, 0x2F, 0xF6, 0x6A, 0x8C, 0x2E
};
static constexpr uint8_t kMagicV7_64LE[16] = {
    0xE5, 0x9B, 0x49, 0x5E, 0x6F, 0x63, 0x1F, 0x14,
    0x1E, 0x13, 0xEB, 0xA9, 0x90, 0xBE, 0xED, 0xC4
};

struct SecPtr {
    uint32_t sec = 0;
    uint32_t off = 0;
    bool valid = false;
};

struct Gr2Section {
    uint32_t compression  = 0u;
    uint32_t dataOff      = 0u;
    uint32_t dataSize     = 0u;
    uint32_t expandedSize = 0u;
    uint32_t first16Bit   = 0u;
    uint32_t first8Bit    = 0u;
    uint32_t fixupOff     = 0u;
    uint32_t fixupCount   = 0u;
    std::vector<uint8_t> bytes;
    std::unordered_map<uint32_t, SecPtr> fixups;
};

// GrannyMemberType enum (granny.h)
enum Gr2MemberType : int32_t {
    EndMember                     = 0,
    InlineMember                  = 1,
    ReferenceMember               = 2,
    ReferenceToArrayMember        = 3,
    ArrayOfReferencesMember       = 4,
    VariantReferenceMember        = 5,
    UnsupportedSwitchMember       = 6,
    ReferenceToVariantArrayMember = 7,
    StringMember                  = 8,
    TransformMember               = 9,
    Real32Member                  = 10,
    Int8Member                    = 11,
    UInt8Member                   = 12,
    BinormalInt8Member            = 13,
    NormalUInt8Member             = 14,
    Int16Member                   = 15,
    UInt16Member                  = 16,
    BinormalInt16Member           = 17,
    NormalUInt16Member            = 18,
    Int32Member                   = 19,
    UInt32Member                  = 20,
    Real16Member                  = 21,
    EmptyReferenceMember          = 22
};

struct Gr2FieldInfo {
    int32_t type = 0;
    std::string name;
    SecPtr refType;
    int32_t arrayWidth = 1;
    size_t byteOffset = 0;
    size_t byteSize = 0;
};

struct Gr2StructLayout {
    std::vector<Gr2FieldInfo> fields;
    size_t totalStride = 0;
};

// Decompresses a Granny LZ / Oodle0 (`compression == 1`) byte-window / LZF section:
// Supports standard Granny LZ77 byte-token stream (36B Oodle0 parameter header or 0B header + control bytes:
//   bit 7 == 0 -> literal run of (ctrl + 1) bytes;
//   bit 7 == 1 -> back-reference: len = ((ctrl >> 4) & 0x07) + 3 (if 10, + next byte), offset = ((ctrl & 0x0F) << 8 | nextByte) + 1).
inline bool DecompressGrannyLzSection(
    const uint8_t* src,
    size_t srcLen,
    size_t expandedSize,
    std::vector<uint8_t>& out)
{
    out.clear();
    if (!src || srcLen == 0 || expandedSize == 0 || expandedSize > (128u << 20)) return false;

    auto runTokenLz = [&](const uint8_t* p, size_t len) -> bool {
        out.clear();
        out.reserve(expandedSize);
        size_t i = 0;
        while (i < len && out.size() < expandedSize) {
            uint8_t c = p[i++];
            if ((c & 0x80u) == 0u) {
                size_t lit = static_cast<size_t>(c) + 1u;
                if (lit > len - i || out.size() + lit > expandedSize) return false;
                out.insert(out.end(), p + i, p + i + lit);
                i += lit;
            } else {
                size_t matchLen = static_cast<size_t>((c >> 4u) & 0x07u) + 3u;
                if (matchLen == 10u) {
                    if (i >= len) return false;
                    matchLen += p[i++];
                }
                if (i >= len) return false;
                size_t backOff = (static_cast<size_t>(c & 0x0Fu) << 8u) | static_cast<size_t>(p[i++]);
                backOff += 1u;
                if (backOff > out.size() || out.size() + matchLen > expandedSize) return false;
                for (size_t k = 0; k < matchLen; ++k) {
                    out.push_back(out[out.size() - backOff]);
                }
            }
        }
        return i == len && out.size() == expandedSize;
    };

    // 1. Try 36-byte Granny Oodle0 parameter header (`3 * 12B` stream stop counts) + LZ token stream
    if (srcLen > 36u && runTokenLz(src + 36u, srcLen - 36u)) return true;
    // 2. Try direct LZ token stream without 36-byte header
    if (runTokenLz(src, srcLen)) return true;
    // 3. Try LibLZF stream
    if (::Explorer2::StreamCodecs::DecompressLibLzf(src, srcLen, out, expandedSize, 128u << 20) &&
        out.size() == expandedSize) {
        return true;
    }
    out.clear();
    return false;
}

struct Gr2ReaderCtx {
    std::vector<Gr2Section> secs;
    size_t ptrSize = 4u; // 4 for 32-bit GR2, 8 for 64-bit GR2

    bool InBounds(SecPtr p, size_t len) const {
        if (!p.valid || p.sec >= secs.size()) return false;
        const auto& b = secs[p.sec].bytes;
        return p.off <= b.size() && len <= b.size() - p.off;
    }

    const uint8_t* Ptr(SecPtr p) const {
        return secs[p.sec].bytes.data() + p.off;
    }

    SecPtr ResolvePtr(uint32_t fromSec, uint32_t fromOff) const {
        if (fromSec >= secs.size()) return {};
        const auto& fm = secs[fromSec].fixups;
        auto it = fm.find(fromOff);
        if (it == fm.end()) return {};
        SecPtr dst = it->second;
        if (dst.sec >= secs.size() || dst.off > secs[dst.sec].bytes.size()) return {};
        return dst;
    }

    std::string ReadCString(uint32_t fromSec, uint32_t ptrFieldOff) const {
        SecPtr sp = ResolvePtr(fromSec, ptrFieldOff);
        if (!sp.valid || sp.sec >= secs.size()) return {};
        const auto& b = secs[sp.sec].bytes;
        if (sp.off >= b.size()) return {};
        size_t end = sp.off;
        while (end < b.size() && b[end] != 0 && (end - sp.off) < 256u) ++end;
        return std::string(reinterpret_cast<const char*>(b.data() + sp.off), end - sp.off);
    }

    bool ComputeTypeLayout(SecPtr typeDef, Gr2StructLayout& outLayout, int depth = 0) const {
        outLayout = {};
        if (!typeDef.valid || depth > 16) return false;

        // sizeof(granny_data_type_definition): 32 bytes in 32-bit GR2, 48 bytes in 64-bit GR2
        const size_t entryStride = (ptrSize == 8u) ? 48u : 32u;
        const size_t nameFieldOff = (ptrSize == 8u) ? 8u : 4u;
        const size_t refFieldOff  = (ptrSize == 8u) ? 16u : 8u;
        const size_t arrWidthOff  = (ptrSize == 8u) ? 24u : 12u;

        size_t curFieldPos = typeDef.off;
        size_t runningByteOff = 0u;
        size_t fieldGuard = 0u;

        while (fieldGuard++ < 256u) {
            SecPtr ep{ typeDef.sec, static_cast<uint32_t>(curFieldPos), true };
            if (!InBounds(ep, entryStride)) return false;
            const uint8_t* fp = Ptr(ep);
            int32_t mType = ByteIO::RdI32LE(fp + 0u);
            if (mType == EndMember) break;
            if (mType < 1 || mType > 22) return false;

            Gr2FieldInfo fi{};
            fi.type = mType;
            fi.name = ReadCString(typeDef.sec, static_cast<uint32_t>(curFieldPos + nameFieldOff));
            fi.refType = ResolvePtr(typeDef.sec, static_cast<uint32_t>(curFieldPos + refFieldOff));
            int32_t aw = ByteIO::RdI32LE(fp + arrWidthOff);
            fi.arrayWidth = (aw <= 0) ? 1 : aw;
            if (fi.arrayWidth > 4096) return false;

            size_t elemSz = 0u;
            switch (mType) {
                case InlineMember: {
                    Gr2StructLayout sub{};
                    if (!ComputeTypeLayout(fi.refType, sub, depth + 1)) return false;
                    elemSz = sub.totalStride * static_cast<size_t>(fi.arrayWidth);
                    break;
                }
                case ReferenceMember:
                case StringMember:
                case EmptyReferenceMember:
                    elemSz = ptrSize * static_cast<size_t>(fi.arrayWidth);
                    break;
                case ReferenceToArrayMember:
                case ArrayOfReferencesMember:
                    // int32 count + (4B pad on 64-bit) + void* ptr
                    elemSz = ((ptrSize == 8u) ? 16u : 8u) * static_cast<size_t>(fi.arrayWidth);
                    break;
                case VariantReferenceMember:
                    // void* type + void* data
                    elemSz = (2u * ptrSize) * static_cast<size_t>(fi.arrayWidth);
                    break;
                case ReferenceToVariantArrayMember:
                    // void* type + int32 count + (4B pad on 64-bit) + void* ptr
                    elemSz = ((ptrSize == 8u) ? 24u : 12u) * static_cast<size_t>(fi.arrayWidth);
                    break;
                case TransformMember:
                    elemSz = 68u * static_cast<size_t>(fi.arrayWidth);
                    break;
                case Real32Member:
                case Int32Member:
                case UInt32Member:
                    elemSz = 4u * static_cast<size_t>(fi.arrayWidth);
                    break;
                case Int8Member:
                case UInt8Member:
                case BinormalInt8Member:
                case NormalUInt8Member:
                    elemSz = 1u * static_cast<size_t>(fi.arrayWidth);
                    break;
                case Int16Member:
                case UInt16Member:
                case BinormalInt16Member:
                case NormalUInt16Member:
                case Real16Member:
                    elemSz = 2u * static_cast<size_t>(fi.arrayWidth);
                    break;
                default:
                    return false;
            }

            fi.byteOffset = runningByteOff;
            fi.byteSize = elemSz;
            runningByteOff += elemSz;
            outLayout.fields.push_back(std::move(fi));
            curFieldPos += entryStride;
        }

        outLayout.totalStride = runningByteOff;
        return !outLayout.fields.empty() && outLayout.totalStride > 0u;
    }

    const Gr2FieldInfo* FindField(const Gr2StructLayout& ly, std::string_view name) const {
        for (const auto& f : ly.fields) {
            if (ReaderText::AsciiEqCI(f.name, name)) return &f;
        }
        return nullptr;
    }

    // Decodes a single Granny `Mesh` object instance (`obj` with layout `meshLayout`), applying optional `xfPos`/`xfRot`/`xfScale`
    bool DecodeMeshInstance(
        SecPtr meshObj,
        const Gr2StructLayout& meshLayout,
        const float* modelMat4x4,
        LoadedMesh3D& outMesh) const
    {
        if (!InBounds(meshObj, meshLayout.totalStride)) return false;

        const Gr2FieldInfo* fPv = FindField(meshLayout, "PrimaryVertexData");
        const Gr2FieldInfo* fPt = FindField(meshLayout, "PrimaryTopology");
        if (!fPv || !fPt || fPv->type != ReferenceMember || fPt->type != ReferenceMember) return false;

        SecPtr pvObj = ResolvePtr(meshObj.sec, static_cast<uint32_t>(meshObj.off + fPv->byteOffset));
        SecPtr ptObj = ResolvePtr(meshObj.sec, static_cast<uint32_t>(meshObj.off + fPt->byteOffset));
        if (!pvObj.valid || !ptObj.valid) return false;

        Gr2StructLayout pvLayout{}, ptLayout{};
        if (!ComputeTypeLayout(fPv->refType, pvLayout) || !ComputeTypeLayout(fPt->refType, ptLayout)) return false;
        if (!InBounds(pvObj, pvLayout.totalStride) || !InBounds(ptObj, ptLayout.totalStride)) return false;

        // 1. PrimaryVertexData -> Vertices (`ReferenceToVariantArrayMember = 7`: typePtr, int32 count, dataPtr)
        const Gr2FieldInfo* fVerts = FindField(pvLayout, "Vertices");
        if (!fVerts || fVerts->type != ReferenceToVariantArrayMember) return false;

        const uint32_t vFieldAbs = static_cast<uint32_t>(pvObj.off + fVerts->byteOffset);
        SecPtr vTypePtr = ResolvePtr(pvObj.sec, vFieldAbs);
        const int32_t vCount = ByteIO::RdI32LE(Ptr({ pvObj.sec, static_cast<uint32_t>(vFieldAbs + ptrSize), true }));
        const uint32_t vDataPtrOff = vFieldAbs + ((ptrSize == 8u) ? 16u : 8u);
        SecPtr vDataPtr = ResolvePtr(pvObj.sec, vDataPtrOff);

        if (!vTypePtr.valid || !vDataPtr.valid || vCount < 3 || vCount > 2000000) return false;

        Gr2StructLayout vtxLayout{};
        if (!ComputeTypeLayout(vTypePtr, vtxLayout) || vtxLayout.totalStride < 6u) return false;
        const uint64_t totalVtxBytes = static_cast<uint64_t>(vCount) * vtxLayout.totalStride;
        if (!InBounds(vDataPtr, static_cast<size_t>(totalVtxBytes))) return false;

        const Gr2FieldInfo* fPos = FindField(vtxLayout, "Position");
        if (!fPos || fPos->arrayWidth < 3 || (fPos->type != Real32Member && fPos->type != Real16Member)) return false;

        const uint32_t baseV = static_cast<uint32_t>(outMesh.vertices.size());
        const uint8_t* vRaw = Ptr(vDataPtr);
        for (int32_t i = 0; i < vCount; ++i) {
            const uint8_t* pp = vRaw + static_cast<size_t>(i) * vtxLayout.totalStride + fPos->byteOffset;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (fPos->type == Real32Member) {
                x = ByteIO::RdF32LE(pp + 0u);
                y = ByteIO::RdF32LE(pp + 4u);
                z = ByteIO::RdF32LE(pp + 8u);
            } else {
                x = ByteIO::HalfToFloat(ByteIO::RdU16LE(pp + 0u));
                y = ByteIO::HalfToFloat(ByteIO::RdU16LE(pp + 2u));
                z = ByteIO::HalfToFloat(ByteIO::RdU16LE(pp + 4u));
            }
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                std::fabs(x) > 1e8f || std::fabs(y) > 1e8f || std::fabs(z) > 1e8f) {
                return false;
            }
            if (modelMat4x4) {
                float tx = modelMat4x4[0] * x + modelMat4x4[1] * y + modelMat4x4[2]  * z + modelMat4x4[3];
                float ty = modelMat4x4[4] * x + modelMat4x4[5] * y + modelMat4x4[6]  * z + modelMat4x4[7];
                float tz = modelMat4x4[8] * x + modelMat4x4[9] * y + modelMat4x4[10] * z + modelMat4x4[11];
                x = tx; y = ty; z = tz;
            }
            outMesh.vertices.push_back({ x, y, z });
        }

        // 2. PrimaryTopology -> Indices (Int32) or Indices16 (UInt16/Int16) + optional Groups
        const Gr2FieldInfo* fIdx32 = FindField(ptLayout, "Indices");
        const Gr2FieldInfo* fIdx16 = FindField(ptLayout, "Indices16");

        bool use16 = false;
        int32_t idxCount = 0;
        SecPtr idxDataPtr{};

        if (fIdx32 && fIdx32->type == ReferenceToArrayMember) {
            uint32_t fa = static_cast<uint32_t>(ptObj.off + fIdx32->byteOffset);
            int32_t cnt = ByteIO::RdI32LE(Ptr({ ptObj.sec, fa, true }));
            SecPtr dp = ResolvePtr(ptObj.sec, fa + ((ptrSize == 8u) ? 8u : 4u));
            if (cnt >= 3 && dp.valid && InBounds(dp, static_cast<size_t>(cnt) * 4u)) {
                idxCount = cnt; idxDataPtr = dp; use16 = false;
            }
        }
        if (idxCount == 0 && fIdx16 && fIdx16->type == ReferenceToArrayMember) {
            uint32_t fa = static_cast<uint32_t>(ptObj.sec < secs.size() ? ptObj.off + fIdx16->byteOffset : 0u);
            int32_t cnt = ByteIO::RdI32LE(Ptr({ ptObj.sec, fa, true }));
            SecPtr dp = ResolvePtr(ptObj.sec, fa + ((ptrSize == 8u) ? 8u : 4u));
            if (cnt >= 3 && dp.valid && InBounds(dp, static_cast<size_t>(cnt) * 2u)) {
                idxCount = cnt; idxDataPtr = dp; use16 = true;
            }
        }
        if (idxCount < 3 || !idxDataPtr.valid) return false;

        const uint8_t* ip = Ptr(idxDataPtr);
        auto rdIdx = [&](size_t k) -> uint32_t {
            return use16 ? static_cast<uint32_t>(ByteIO::RdU16LE(ip + k * 2u))
                         : ByteIO::RdU32LE(ip + k * 4u);
        };

        // Check optional `Groups` (`granny_tri_material_group`: MaterialIndex, TriFirst, TriCount)
        std::vector<std::pair<int32_t, int32_t>> triRanges;
        const Gr2FieldInfo* fGroups = FindField(ptLayout, "Groups");
        if (fGroups && fGroups->type == ReferenceToArrayMember && fGroups->refType.valid) {
            uint32_t ga = static_cast<uint32_t>(ptObj.off + fGroups->byteOffset);
            int32_t gCnt = ByteIO::RdI32LE(Ptr({ ptObj.sec, ga, true }));
            SecPtr gdp = ResolvePtr(ptObj.sec, ga + ((ptrSize == 8u) ? 8u : 4u));
            Gr2StructLayout grpLayout{};
            if (gCnt > 0 && gCnt <= 65536 && gdp.valid && ComputeTypeLayout(fGroups->refType, grpLayout) &&
                InBounds(gdp, static_cast<size_t>(gCnt) * grpLayout.totalStride))
            {
                const Gr2FieldInfo* fTf = FindField(grpLayout, "TriFirst");
                const Gr2FieldInfo* fTc = FindField(grpLayout, "TriCount");
                if (fTf && fTc && fTf->type == Int32Member && fTc->type == Int32Member) {
                    const uint8_t* gp = Ptr(gdp);
                    for (int32_t g = 0; g < gCnt; ++g) {
                        int32_t tf = ByteIO::RdI32LE(gp + static_cast<size_t>(g) * grpLayout.totalStride + fTf->byteOffset);
                        int32_t tc = ByteIO::RdI32LE(gp + static_cast<size_t>(g) * grpLayout.totalStride + fTc->byteOffset);
                        if (tf >= 0 && tc > 0 && static_cast<int64_t>(tf + tc) * 3LL <= idxCount) {
                            triRanges.push_back({ tf, tc });
                        }
                    }
                }
            }
        }
        if (triRanges.empty()) {
            triRanges.push_back({ 0, idxCount / 3 });
        }

        size_t tBefore = outMesh.tris.size();
        for (const auto& [triFirst, triCnt] : triRanges) {
            for (int32_t t = 0; t < triCnt; ++t) {
                const size_t k = static_cast<size_t>(triFirst + t) * 3u;
                uint32_t i0 = rdIdx(k + 0u);
                uint32_t i1 = rdIdx(k + 1u);
                uint32_t i2 = rdIdx(k + 2u);
                if (i0 >= static_cast<uint32_t>(vCount) ||
                    i1 >= static_cast<uint32_t>(vCount) ||
                    i2 >= static_cast<uint32_t>(vCount)) {
                    return false;
                }
                if (i0 != i1 && i1 != i2 && i0 != i2) {
                    outMesh.tris.push_back({ baseV + i0, baseV + i1, baseV + i2, {} });
                }
            }
        }
        return outMesh.tris.size() > tBefore;
    }
};

// Converts Granny `granny_transform` (68B: uint32 flags, float pos[3], float orient[4] xyzw, float scaleShear[9]) to row-major 4x4
inline void DecodeGrannyTransform68(const uint8_t* tp, float outMat[16]) {
    const uint32_t flags = ByteIO::RdU32LE(tp + 0u);
    float px = 0.0f, py = 0.0f, pz = 0.0f;
    if (flags & 0x01u) {
        px = ByteIO::RdF32LE(tp + 4u);
        py = ByteIO::RdF32LE(tp + 8u);
        pz = ByteIO::RdF32LE(tp + 12u);
    }
    float r[9] = { 1,0,0, 0,1,0, 0,0,1 };
    if (flags & 0x02u) {
        float qx = ByteIO::RdF32LE(tp + 16u);
        float qy = ByteIO::RdF32LE(tp + 20u);
        float qz = ByteIO::RdF32LE(tp + 24u);
        float qw = ByteIO::RdF32LE(tp + 28u);
        float n = std::sqrt(qx*qx + qy*qy + qz*qz + qw*qw);
        if (n > 1e-6f) { qx /= n; qy /= n; qz /= n; qw /= n; }
        r[0] = 1.0f - 2.0f * (qy*qy + qz*qz); r[1] = 2.0f * (qx*qy - qw*qz);         r[2] = 2.0f * (qx*qz + qw*qy);
        r[3] = 2.0f * (qx*qy + qw*qz);         r[4] = 1.0f - 2.0f * (qx*qx + qz*qz); r[5] = 2.0f * (qy*qz - qw*qx);
        r[6] = 2.0f * (qx*qz - qw*qy);         r[7] = 2.0f * (qy*qz + qw*qx);         r[8] = 1.0f - 2.0f * (qx*qx + qy*qy);
    }
    if (flags & 0x04u) {
        float ss[9] = {};
        for (int i = 0; i < 9; ++i) ss[i] = ByteIO::RdF32LE(tp + 32u + static_cast<size_t>(i) * 4u);
        float c[9] = {};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                c[row * 3 + col] = r[row * 3 + 0] * ss[0 * 3 + col] + r[row * 3 + 1] * ss[1 * 3 + col] + r[row * 3 + 2] * ss[2 * 3 + col];
        std::memcpy(r, c, sizeof(r));
    }
    outMat[0] = r[0]; outMat[1] = r[1]; outMat[2]  = r[2]; outMat[3]  = px;
    outMat[4] = r[3]; outMat[5] = r[4]; outMat[6]  = r[5]; outMat[7]  = py;
    outMat[8] = r[6]; outMat[9] = r[7]; outMat[10] = r[8]; outMat[11] = pz;
    outMat[12] = 0.0f; outMat[13] = 0.0f; outMat[14] = 0.0f; outMat[15] = 1.0f;
}

} // namespace DetailGrannyGr2

inline bool ParseRadGrannyGr2(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    using namespace DetailGrannyGr2;
    if (!data || size < 0x58u || size > (128u << 20)) return false;

    const bool is32 = (std::memcmp(data, kMagicV6_32LE, 16u) == 0 ||
                       std::memcmp(data, kMagicV7_32LE, 16u) == 0 ||
                       std::memcmp(data, kMagicV7Alt_32LE, 16u) == 0);
    const bool is64 = (!is32 && std::memcmp(data, kMagicV7_64LE, 16u) == 0);
    if (!is32 && !is64) return false;

    const uint32_t hdrTotalSize = ByteIO::RdU32LE(data + 0x10u);
    const uint32_t fileFormat   = ByteIO::RdU32LE(data + 0x14u);
    if (fileFormat != 0u || hdrTotalSize < 0x44u || hdrTotalSize > size) return false;

    // GrannyFileHeader begins at 0x20
    const uint32_t version     = ByteIO::RdU32LE(data + 0x20u);
    const uint32_t totalBytes  = ByteIO::RdU32LE(data + 0x24u);
    const uint32_t secArrRel   = ByteIO::RdU32LE(data + 0x2Cu); // relative to 0x20
    const uint32_t secArrCount = ByteIO::RdU32LE(data + 0x30u);

    const uint32_t rootTypeSec = ByteIO::RdU32LE(data + 0x34u);
    const uint32_t rootTypeOff = ByteIO::RdU32LE(data + 0x38u);
    const uint32_t rootObjSec  = ByteIO::RdU32LE(data + 0x3Cu);
    const uint32_t rootObjOff  = ByteIO::RdU32LE(data + 0x40u);

    if ((version != 6u && version != 7u) || totalBytes > size || totalBytes < 0x58u) return false;
    if (secArrCount < 1u || secArrCount > 256u) return false;

    const uint64_t secTableAbs = 0x20ULL + secArrRel;
    const uint64_t secTableEnd = secTableAbs + static_cast<uint64_t>(secArrCount) * 44ULL;
    if (secTableEnd > size) return false;

    Gr2ReaderCtx ctx{};
    ctx.ptrSize = is64 ? 8u : 4u;
    ctx.secs.resize(secArrCount);

    for (uint32_t si = 0u; si < secArrCount; ++si) {
        const uint8_t* sp = data + static_cast<size_t>(secTableAbs) + static_cast<size_t>(si) * 44u;
        auto& sec = ctx.secs[si];
        sec.compression  = ByteIO::RdU32LE(sp + 0x00u);
        sec.dataOff      = ByteIO::RdU32LE(sp + 0x04u);
        sec.dataSize     = ByteIO::RdU32LE(sp + 0x08u);
        sec.expandedSize = ByteIO::RdU32LE(sp + 0x0Cu);
        sec.first16Bit   = ByteIO::RdU32LE(sp + 0x14u);
        sec.first8Bit    = ByteIO::RdU32LE(sp + 0x18u);
        sec.fixupOff     = ByteIO::RdU32LE(sp + 0x1Cu);
        sec.fixupCount   = ByteIO::RdU32LE(sp + 0x20u);

        if (sec.expandedSize > (128u << 20)) return false;
        if (sec.dataSize > 0u) {
            if (sec.dataOff >= size || sec.dataSize > size - sec.dataOff) return false;
        }

        if (sec.compression == 0u) {
            const size_t allocSz = std::max<size_t>(sec.dataSize, sec.expandedSize);
            sec.bytes.assign(allocSz, 0u);
            if (sec.dataSize > 0u) {
                std::memcpy(sec.bytes.data(), data + sec.dataOff, sec.dataSize);
            }
        } else if (sec.compression == 1u) {
            if (sec.expandedSize == 0u) continue;
            if (!DecompressGrannyLzSection(data + sec.dataOff, sec.dataSize, sec.expandedSize, sec.bytes)) {
                return false;
            }
        } else {
            return false; // Oodle1 (2) / unsupported proprietary compression rejected cleanly
        }
    }

    // Load pointer fixup tables (`GrannyPointerFixup`: 12 bytes each: fromOffset, toSection, toOffset)
    for (uint32_t si = 0u; si < secArrCount; ++si) {
        auto& sec = ctx.secs[si];
        if (sec.fixupCount == 0u) continue;
        if (sec.fixupCount > 4000000u || sec.fixupOff >= size ||
            static_cast<uint64_t>(sec.fixupCount) * 12ULL > size - sec.fixupOff) {
            return false;
        }
        sec.fixups.reserve(sec.fixupCount);
        const uint8_t* fp = data + sec.fixupOff;
        for (uint32_t k = 0u; k < sec.fixupCount; ++k) {
            const uint32_t fromOff = ByteIO::RdU32LE(fp + static_cast<size_t>(k) * 12u + 0u);
            const uint32_t toSec   = ByteIO::RdU32LE(fp + static_cast<size_t>(k) * 12u + 4u);
            const uint32_t toOff   = ByteIO::RdU32LE(fp + static_cast<size_t>(k) * 12u + 8u);
            if (fromOff + ctx.ptrSize > sec.bytes.size() ||
                toSec >= secArrCount || toOff > ctx.secs[toSec].bytes.size()) {
                return false;
            }
            sec.fixups[fromOff] = { toSec, toOff, true };
        }
    }

    if (rootTypeSec >= secArrCount || rootObjSec >= secArrCount) return false;
    SecPtr rootType{ rootTypeSec, rootTypeOff, true };
    SecPtr rootObj { rootObjSec,  rootObjOff,  true };

    Gr2StructLayout rootLayout{};
    if (!ctx.ComputeTypeLayout(rootType, rootLayout) || !ctx.InBounds(rootObj, rootLayout.totalStride)) {
        return false;
    }

    outMesh = LoadedMesh3D{};

    // Check if RootObject itself IS a Mesh (single-mesh root export)
    if (ctx.FindField(rootLayout, "PrimaryVertexData") && ctx.FindField(rootLayout, "PrimaryTopology")) {
        if (!ctx.DecodeMeshInstance(rootObj, rootLayout, nullptr, outMesh)) {
            outMesh = {};
            return false;
        }
    } else {
        // Check `Models` first for `InitialPlacement` transforms mapped onto `MeshBindings -> Mesh`
        std::unordered_map<uint64_t, std::array<float, 16>> meshTransforms;
        auto makeKey = [](SecPtr p) -> uint64_t { return (static_cast<uint64_t>(p.sec) << 32u) | p.off; };

        const Gr2FieldInfo* fModels = ctx.FindField(rootLayout, "Models");
        if (fModels && fModels->type == ArrayOfReferencesMember && fModels->refType.valid) {
            uint32_t ma = static_cast<uint32_t>(rootObj.off + fModels->byteOffset);
            int32_t mCnt = ByteIO::RdI32LE(ctx.Ptr({ rootObj.sec, ma, true }));
            SecPtr mArr = ctx.ResolvePtr(rootObj.sec, ma + ((ctx.ptrSize == 8u) ? 8u : 4u));
            Gr2StructLayout mdlLayout{};
            if (mCnt > 0 && mCnt <= 4096 && mArr.valid && ctx.ComputeTypeLayout(fModels->refType, mdlLayout) &&
                ctx.InBounds(mArr, static_cast<size_t>(mCnt) * ctx.ptrSize))
            {
                const Gr2FieldInfo* fPlace = ctx.FindField(mdlLayout, "InitialPlacement");
                const Gr2FieldInfo* fBinds = ctx.FindField(mdlLayout, "MeshBindings");
                Gr2StructLayout bindLayout{};
                bool hasBindLayout = fBinds && fBinds->type == ReferenceToArrayMember &&
                                     ctx.ComputeTypeLayout(fBinds->refType, bindLayout);
                for (int32_t m = 0; m < mCnt && hasBindLayout; ++m) {
                    SecPtr mObj = ctx.ResolvePtr(mArr.sec, static_cast<uint32_t>(mArr.off + static_cast<size_t>(m) * ctx.ptrSize));
                    if (!mObj.valid || !ctx.InBounds(mObj, mdlLayout.totalStride)) continue;
                    std::array<float, 16> xf = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
                    if (fPlace && fPlace->type == TransformMember) {
                        DecodeGrannyTransform68(ctx.Ptr({ mObj.sec, static_cast<uint32_t>(mObj.off + fPlace->byteOffset), true }), xf.data());
                    }
                    uint32_t ba = static_cast<uint32_t>(mObj.off + fBinds->byteOffset);
                    int32_t bCnt = ByteIO::RdI32LE(ctx.Ptr({ mObj.sec, ba, true }));
                    SecPtr bArr = ctx.ResolvePtr(mObj.sec, ba + ((ctx.ptrSize == 8u) ? 8u : 4u));
                    const Gr2FieldInfo* fMeshRef = ctx.FindField(bindLayout, "Mesh");
                    if (fMeshRef && fMeshRef->type == ReferenceMember && bCnt > 0 && bCnt <= 4096 && bArr.valid &&
                        ctx.InBounds(bArr, static_cast<size_t>(bCnt) * bindLayout.totalStride))
                    {
                        for (int32_t b = 0; b < bCnt; ++b) {
                            uint32_t refOff = static_cast<uint32_t>(bArr.off + static_cast<size_t>(b) * bindLayout.totalStride + fMeshRef->byteOffset);
                            SecPtr msh = ctx.ResolvePtr(bArr.sec, refOff);
                            if (msh.valid) meshTransforms[makeKey(msh)] = xf;
                        }
                    }
                }
            }
        }

        // Walk `Meshes` (`ArrayOfReferencesMember = 4` or `ReferenceToArrayMember = 3`)
        const Gr2FieldInfo* fMeshes = ctx.FindField(rootLayout, "Meshes");
        if (!fMeshes || !fMeshes->refType.valid) return false;

        Gr2StructLayout meshLayout{};
        if (!ctx.ComputeTypeLayout(fMeshes->refType, meshLayout)) return false;

        const uint32_t ma = static_cast<uint32_t>(rootObj.off + fMeshes->byteOffset);
        const int32_t meshCount = ByteIO::RdI32LE(ctx.Ptr({ rootObj.sec, ma, true }));
        SecPtr meshArr = ctx.ResolvePtr(rootObj.sec, ma + ((ctx.ptrSize == 8u) ? 8u : 4u));
        if (meshCount <= 0 || meshCount > 16384 || !meshArr.valid) return false;

        if (fMeshes->type == ArrayOfReferencesMember) {
            if (!ctx.InBounds(meshArr, static_cast<size_t>(meshCount) * ctx.ptrSize)) return false;
            for (int32_t mi = 0; mi < meshCount; ++mi) {
                SecPtr mObj = ctx.ResolvePtr(meshArr.sec, static_cast<uint32_t>(meshArr.off + static_cast<size_t>(mi) * ctx.ptrSize));
                if (!mObj.valid) continue;
                auto xit = meshTransforms.find(makeKey(mObj));
                if (!ctx.DecodeMeshInstance(mObj, meshLayout, xit != meshTransforms.end() ? xit->second.data() : nullptr, outMesh)) {
                    outMesh = {};
                    return false;
                }
            }
        } else if (fMeshes->type == ReferenceToArrayMember) {
            if (!ctx.InBounds(meshArr, static_cast<size_t>(meshCount) * meshLayout.totalStride)) return false;
            for (int32_t mi = 0; mi < meshCount; ++mi) {
                SecPtr mObj{ meshArr.sec, static_cast<uint32_t>(meshArr.off + static_cast<size_t>(mi) * meshLayout.totalStride), true };
                auto xit = meshTransforms.find(makeKey(mObj));
                if (!ctx.DecodeMeshInstance(mObj, meshLayout, xit != meshTransforms.end() ? xit->second.data() : nullptr, outMesh)) {
                    outMesh = {};
                    return false;
                }
            }
        } else {
            return false;
        }
    }

    if (outMesh.vertices.empty() || outMesh.tris.empty()) {
        outMesh = {};
        return false;
    }

    wchar_t fmtBuf[96] = {};
    swprintf_s(fmtBuf, L"RAD Granny 3D Model (.gr2 v%u)", version);
    outMesh.formatName = fmtBuf;
    return true;
}

} // namespace Formats
