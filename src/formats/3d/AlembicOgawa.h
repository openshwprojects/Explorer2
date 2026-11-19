#pragma once
// AlembicOgawa.h — Alembic Ogawa (.abc) Hierarchical Xform & DAG 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// ============================================================================
// Public Entry Point 2: Alembic Ogawa (.abc) Hierarchical Xform + DAG Instancing
// ============================================================================
inline bool ParseAlembicOgawaComplete(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace Detail3D100;
    using DetailScad::Mat4;

    if (!data || size < 24 || std::memcmp(data, "Ogawa", 5) != 0) return false;
    size_t rootPos = 0;
    if ((data[5] == 0xFF || data[5] == 0x00) && data[6] == 0x00 && data[7] == 0x01) {
        rootPos = 8;
    } else if (data[5] == 0x00 && data[6] == 0xFF && data[7] == 0x00 && data[8] == 0x01) {
        rootPos = 9;
    } else {
        return false;
    }
    uint64_t rootGroupOff = RdU64LE(data + rootPos);
    if (rootGroupOff < rootPos + 8 || rootGroupOff + 8 > size) return false;

    mesh.vertices.clear();
    mesh.tris.clear();
    mesh.formatName = L"Alembic Ogawa 3D Cache (.abc)";

    auto getDataStream = [&](uint64_t childEntry, const uint8_t*& outPtr, size_t& outLen) -> bool {
        if ((childEntry & (1ULL << 63)) == 0) return false;
        uint64_t off = childEntry & ~(1ULL << 63);
        if (off == 0 || off + 8 > size) return false;
        uint64_t bc = RdU64LE(data + static_cast<size_t>(off));
        if (bc > size - static_cast<size_t>(off) - 8) return false;
        outPtr = data + static_cast<size_t>(off) + 8;
        outLen = static_cast<size_t>(bc);
        return true;
    };

    // Reads first sample bytes from either ScalarProperty (no 16B digest or 16B digest) or ArrayProperty
    auto readPropSampleBytes = [&](uint64_t groupOff, const uint8_t*& outRaw, size_t& outRawLen, uint64_t& outExplicitDims) -> bool {
        outRaw = nullptr; outRawLen = 0; outExplicitDims = 0;
        if (groupOff == 0 || (groupOff & (1ULL << 63)) != 0 || groupOff + 8 > size) return false;
        uint64_t nCh = RdU64LE(data + static_cast<size_t>(groupOff));
        if (nCh == 0 || nCh > 65536 || groupOff + 8 + nCh * 8 > size) return false;
        uint64_t ch0 = RdU64LE(data + static_cast<size_t>(groupOff) + 8);
        if (!getDataStream(ch0, outRaw, outRawLen)) return false;
        if (nCh >= 2) {
            uint64_t ch1 = RdU64LE(data + static_cast<size_t>(groupOff) + 16);
            const uint8_t* dimPtr = nullptr; size_t dimLen = 0;
            if (getDataStream(ch1, dimPtr, dimLen) && dimLen >= 24) {
                outExplicitDims = RdU64LE(dimPtr + 16);
            }
        }
        return true;
    };

    auto readTypedArrayProp = [&](uint64_t groupOff, size_t elemByteWidth, const uint8_t*& outBytes, size_t& outCount) -> bool {
        const uint8_t* raw = nullptr; size_t rawLen = 0; uint64_t expDims = 0;
        if (!readPropSampleBytes(groupOff, raw, rawLen, expDims)) return false;
        if (expDims > 0) {
            if (rawLen == 16 + expDims * elemByteWidth) { outBytes = raw + 16; outCount = static_cast<size_t>(expDims); return true; }
            if (rawLen == expDims * elemByteWidth)      { outBytes = raw;      outCount = static_cast<size_t>(expDims); return true; }
        }
        if (rawLen > 16 && ((rawLen - 16) % elemByteWidth) == 0) {
            outBytes = raw + 16;
            outCount = (rawLen - 16) / elemByteWidth;
            return true;
        }
        if (rawLen >= elemByteWidth && (rawLen % elemByteWidth) == 0) {
            outBytes = raw;
            outCount = rawLen / elemByteWidth;
            return true;
        }
        return false;
    };

    auto parseCompoundPropMap = [&](uint64_t gOff, std::unordered_map<std::string, uint64_t>& propMap) -> bool {
        propMap.clear();
        if (gOff == 0 || (gOff & (1ULL << 63)) != 0 || gOff + 8 > size) return false;
        uint64_t nCh = RdU64LE(data + static_cast<size_t>(gOff));
        if (nCh < 2 || nCh > 65536 || gOff + 8 + nCh * 8 > size) return false;
        uint64_t lastCh = RdU64LE(data + static_cast<size_t>(gOff) + 8 + (static_cast<size_t>(nCh) - 1) * 8);
        const uint8_t* hdrPtr = nullptr; size_t hdrLen = 0;
        if (!getDataStream(lastCh, hdrPtr, hdrLen) || hdrLen < 5) return false;

        size_t hp = 0;
        size_t nProps = static_cast<size_t>(nCh) - 1;
        for (size_t i = 0; i < nProps && hp + 4 <= hdrLen; ++i) {
            uint32_t info = RdU32LE(hdrPtr + hp); hp += 4;
            uint32_t propType = info & 0x03u;
            uint32_t szHint   = (info >> 2) & 0x03u;
            bool ok = true;
            auto rdVarU = [&]() -> uint32_t {
                if (szHint == 0 && hp + 1 <= hdrLen) return hdrPtr[hp++];
                if (szHint == 1 && hp + 2 <= hdrLen) { uint32_t v = RdU16LE(hdrPtr + hp); hp += 2; return v; }
                if (szHint == 2 && hp + 4 <= hdrLen) { uint32_t v = RdU32LE(hdrPtr + hp); hp += 4; return v; }
                ok = false; return 0;
            };
            if (propType != 0) {
                bool hasTsIdx     = (info & 0x0100u) != 0;
                bool hasFirstLast = (info & 0x0800u) == 0;
                rdVarU();
                if (hasFirstLast) { rdVarU(); rdVarU(); }
                if (hasTsIdx) rdVarU();
            }
            uint32_t nameSz = rdVarU();
            if (!ok || nameSz == 0 || nameSz > 512 || hp + nameSz + 1 > hdrLen) return false;
            std::string pName(reinterpret_cast<const char*>(hdrPtr + hp), nameSz);
            hp += nameSz;
            uint8_t metaIdx = hdrPtr[hp++];
            if (metaIdx == 0xFFu) {
                uint32_t metaSz = rdVarU();
                if (!ok || hp + metaSz > hdrLen) return false;
                hp += metaSz;
            }
            uint64_t chEntry = RdU64LE(data + static_cast<size_t>(gOff) + 8 + i * 8);
            propMap[pName] = chEntry;
        }
        return !propMap.empty();
    };

    // Evaluates an AbcGeom_Xform_v3 ".xform" CompoundProperty group into local Mat4 + inherits flag
    auto decodeAbcXformGroup = [&](uint64_t xfGroupOff, Mat4& outLocalMat, bool& outInherits) -> bool {
        outLocalMat = Mat4::Identity();
        outInherits = true;
        std::unordered_map<std::string, uint64_t> xfProps;
        if (!parseCompoundPropMap(xfGroupOff, xfProps)) return false;

        auto itInh = xfProps.find(".inherits");
        if (itInh != xfProps.end()) {
            const uint8_t* ib = nullptr; size_t ilen = 0; uint64_t idim = 0;
            if (readPropSampleBytes(itInh->second, ib, ilen, idim)) {
                if (ilen == 17) outInherits = (ib[16] != 0);
                else if (ilen == 1) outInherits = (ib[0] != 0);
            }
        }

        auto itOps  = xfProps.find(".ops");
        auto itVals = xfProps.find(".vals");
        if (itOps == xfProps.end() || itVals == xfProps.end()) return false;

        const uint8_t* opRaw = nullptr; size_t opRawLen = 0; uint64_t opDim = 0;
        const uint8_t* vlRaw = nullptr; size_t vlRawLen = 0; uint64_t vlDim = 0;
        if (!readPropSampleBytes(itOps->second, opRaw, opRawLen, opDim) ||
            !readPropSampleBytes(itVals->second, vlRaw, vlRawLen, vlDim)) return false;

        auto numChansForOp = [](uint8_t opCode) -> size_t {
            switch (opCode & 0x0Fu) {
            case 0: return 3;  // kScaleOperation
            case 1: return 3;  // kTranslateOperation
            case 2: return 4;  // kRotateOperation (axisX, axisY, axisZ, deg)
            case 3: return 16; // kMatrixOperation (row-major M44d)
            case 4: return 1;  // kRotateXOperation
            case 5: return 1;  // kRotateYOperation
            case 6: return 1;  // kRotateZOperation
            default: return 0;
            }
        };

        // Match exact (opBytes, valBytes) combination with or without 16-byte AbcCoreOgawa Murmur3 digest
        const uint8_t* opsPtr = nullptr; size_t nOps = 0;
        const uint8_t* valsPtr = nullptr; size_t nVals = 0;
        for (size_t opSkip : { size_t(16), size_t(0) }) {
            if (opRawLen <= opSkip && !(opSkip == 0 && opRawLen > 0)) continue;
            size_t candOps = opRawLen - opSkip;
            if (candOps == 0 || candOps > 64) continue;
            size_t reqDoubles = 0;
            bool allValid = true;
            for (size_t k = 0; k < candOps; ++k) {
                size_t nc = numChansForOp(opRaw[opSkip + k]);
                if (nc == 0) { allValid = false; break; }
                reqDoubles += nc;
            }
            if (!allValid || reqDoubles == 0) continue;
            if (vlRawLen == 16 + reqDoubles * 8) {
                opsPtr = opRaw + opSkip; nOps = candOps;
                valsPtr = vlRaw + 16; nVals = reqDoubles;
                break;
            }
            if (vlRawLen == reqDoubles * 8) {
                opsPtr = opRaw + opSkip; nOps = candOps;
                valsPtr = vlRaw; nVals = reqDoubles;
                break;
            }
        }
        if (!opsPtr || !valsPtr) return false;

        // In Alembic XformSample, operations in .ops are accumulated from outermost to innermost (same as USD xformOpOrder!)
        Mat4 accum = Mat4::Identity();
        size_t vp = 0;
        for (size_t k = 0; k < nOps && vp < nVals; ++k) {
            uint8_t opType = opsPtr[k] & 0x0Fu;
            Mat4 opM = Mat4::Identity();
            if (opType == 0 && vp + 3 <= nVals) {
                opM = Mat4::Scale(static_cast<float>(RdF64LE(valsPtr + (vp + 0) * 8)),
                                  static_cast<float>(RdF64LE(valsPtr + (vp + 1) * 8)),
                                  static_cast<float>(RdF64LE(valsPtr + (vp + 2) * 8)));
                vp += 3;
            } else if (opType == 1 && vp + 3 <= nVals) {
                opM = Mat4::Translate(static_cast<float>(RdF64LE(valsPtr + (vp + 0) * 8)),
                                      static_cast<float>(RdF64LE(valsPtr + (vp + 1) * 8)),
                                      static_cast<float>(RdF64LE(valsPtr + (vp + 2) * 8)));
                vp += 3;
            } else if (opType == 2 && vp + 4 <= nVals) {
                float ax  = static_cast<float>(RdF64LE(valsPtr + (vp + 0) * 8));
                float ay  = static_cast<float>(RdF64LE(valsPtr + (vp + 1) * 8));
                float az  = static_cast<float>(RdF64LE(valsPtr + (vp + 2) * 8));
                float deg = static_cast<float>(RdF64LE(valsPtr + (vp + 3) * 8));
                opM = Mat4::RotateAxisAngle(deg, ax, ay, az);
                vp += 4;
            } else if (opType == 3 && vp + 16 <= nVals) {
                double d16[16] = {};
                for (int m = 0; m < 16; ++m) d16[m] = RdF64LE(valsPtr + (vp + m) * 8);
                opM = UsdAbcDetail::RowVecMatrix16ToMat4(d16);
                vp += 16;
            } else if (opType == 4 && vp + 1 <= nVals) {
                opM = Mat4::RotateAxisAngle(static_cast<float>(RdF64LE(valsPtr + vp * 8)), 1, 0, 0);
                vp += 1;
            } else if (opType == 5 && vp + 1 <= nVals) {
                opM = Mat4::RotateAxisAngle(static_cast<float>(RdF64LE(valsPtr + vp * 8)), 0, 1, 0);
                vp += 1;
            } else if (opType == 6 && vp + 1 <= nVals) {
                opM = Mat4::RotateAxisAngle(static_cast<float>(RdF64LE(valsPtr + vp * 8)), 0, 0, 1);
                vp += 1;
            }
            accum = Mat4::Mul(accum, opM);
        }
        outLocalMat = accum;
        return true;
    };

    auto emitAbcGeomProps = [&](const std::unordered_map<std::string, uint64_t>& propMap, const Mat4& worldMat) {
        auto itP  = propMap.find("P");
        auto itFI = propMap.find(".faceIndices");
        auto itFC = propMap.find(".faceCounts");
        if (itP == propMap.end() || itFI == propMap.end()) return;

        const uint8_t* pBytes = nullptr;  size_t nVerts = 0;
        const uint8_t* fiBytes = nullptr; size_t nIdx = 0;
        const uint8_t* fcBytes = nullptr; size_t nFaces = 0;
        if (!readTypedArrayProp(itP->second, 12, pBytes, nVerts) ||
            !readTypedArrayProp(itFI->second, 4, fiBytes, nIdx) ||
            nVerts < 3 || nIdx < 3 || nVerts > 2000000 || nIdx > 6000000) return;

        if (itFC != propMap.end()) readTypedArrayProp(itFC->second, 4, fcBytes, nFaces);

        bool flip = (worldMat.Det3x3() < 0.0f);
        uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
        for (size_t v = 0; v < nVerts; ++v) {
            Vec3f lp = {
                RdF32LE(pBytes + v * 12 + 0),
                RdF32LE(pBytes + v * 12 + 4),
                RdF32LE(pBytes + v * 12 + 8)
            };
            mesh.vertices.push_back(worldMat.TransformPoint(lp));
        }

        if (fcBytes && nFaces > 0) {
            size_t k = 0;
            for (size_t f = 0; f < nFaces; ++f) {
                int32_t cnt = static_cast<int32_t>(RdU32LE(fcBytes + f * 4));
                if (cnt >= 3 && k + static_cast<size_t>(cnt) <= nIdx) {
                    int32_t i0 = static_cast<int32_t>(RdU32LE(fiBytes + k * 4));
                    if (i0 >= 0 && static_cast<size_t>(i0) < nVerts) {
                        for (int32_t t = 1; t + 1 < cnt; ++t) {
                            int32_t i1 = static_cast<int32_t>(RdU32LE(fiBytes + (k + t) * 4));
                            int32_t i2 = static_cast<int32_t>(RdU32LE(fiBytes + (k + t + 1) * 4));
                            if (i1 >= 0 && i2 >= 0 && static_cast<size_t>(i1) < nVerts && static_cast<size_t>(i2) < nVerts) {
                                // Alembic PolyMesh uses CW face winding; reverse unless worldMat also flips handedness
                                if (flip) mesh.tris.push_back({ vBase + static_cast<uint32_t>(i0), vBase + static_cast<uint32_t>(i1), vBase + static_cast<uint32_t>(i2), {} });
                                else      mesh.tris.push_back({ vBase + static_cast<uint32_t>(i0), vBase + static_cast<uint32_t>(i2), vBase + static_cast<uint32_t>(i1), {} });
                            }
                        }
                    }
                }
                if (cnt > 0) k += static_cast<size_t>(cnt);
            }
        } else {
            for (size_t k = 0; k + 2 < nIdx; k += 3) {
                uint32_t i0 = RdU32LE(fiBytes + k * 4 + 0);
                uint32_t i1 = RdU32LE(fiBytes + k * 4 + 4);
                uint32_t i2 = RdU32LE(fiBytes + k * 4 + 8);
                if (i0 < nVerts && i1 < nVerts && i2 < nVerts) {
                    if (flip) mesh.tris.push_back({ vBase + i0, vBase + i1, vBase + i2, {} });
                    else      mesh.tris.push_back({ vBase + i0, vBase + i2, vBase + i1, {} });
                }
            }
        }
    };

    size_t totalVisits = 0;
    std::vector<uint64_t> ancestorStack;
    ancestorStack.reserve(40);

    auto walkOgawa = [&](auto& self, uint64_t gOff, const Mat4& parentWorldMat, int depth) -> void {
        if (depth > 32 || totalVisits > 32768 || gOff == 0 || (gOff & (1ULL << 63)) != 0 || gOff + 8 > size) return;
        for (uint64_t anc : ancestorStack) {
            if (anc == gOff) return; // Guard against corrupt cyclic offsets while allowing DAG multi-parent instancing!
        }
        ++totalVisits;
        ancestorStack.push_back(gOff);

        uint64_t nCh = RdU64LE(data + static_cast<size_t>(gOff));
        if (nCh > 0 && nCh <= 65536 && gOff + 8 + nCh * 8 <= size) {
            std::vector<uint64_t> children(static_cast<size_t>(nCh));
            for (size_t i = 0; i < static_cast<size_t>(nCh); ++i) {
                children[i] = RdU64LE(data + static_cast<size_t>(gOff) + 8 + i * 8);
            }

            Mat4 curWorldMat = parentWorldMat;

            // 1. Check if this group is an Object Group whose child[0] is its top-level CompoundProperty (.prop)
            //    containing ".xform", so child Object Groups [1..nCh-2] inherit curWorldMat!
            std::unordered_map<std::string, uint64_t> ownProps;
            if (parseCompoundPropMap(gOff, ownProps)) {
                auto itXf = ownProps.find(".xform");
                if (itXf != ownProps.end()) {
                    Mat4 loc; bool inh = true;
                    if (decodeAbcXformGroup(itXf->second, loc, inh)) {
                        curWorldMat = inh ? Mat4::Mul(parentWorldMat, loc) : loc;
                    }
                }
                emitAbcGeomProps(ownProps, curWorldMat);
                for (const auto& [nm, chOff] : ownProps) {
                    if (nm != ".xform" && chOff != 0 && (chOff & (1ULL << 63)) == 0) {
                        self(self, chOff, curWorldMat, depth + 1);
                    }
                }
            } else {
                // Object Group or Archive Root Group: inspect child[0] first to update curWorldMat if it holds .xform!
                if (!children.empty() && children[0] != 0 && (children[0] & (1ULL << 63)) == 0) {
                    std::unordered_map<std::string, uint64_t> topProps;
                    if (parseCompoundPropMap(children[0], topProps)) {
                        auto itXf = topProps.find(".xform");
                        if (itXf != topProps.end()) {
                            Mat4 loc; bool inh = true;
                            if (decodeAbcXformGroup(itXf->second, loc, inh)) {
                                curWorldMat = inh ? Mat4::Mul(parentWorldMat, loc) : loc;
                            }
                        }
                    }
                }
                for (uint64_t ch : children) {
                    if (ch != 0 && (ch & (1ULL << 63)) == 0) {
                        self(self, ch, curWorldMat, depth + 1);
                    }
                }
            }
        }

        ancestorStack.pop_back();
    };

    walkOgawa(walkOgawa, rootGroupOff, Mat4::Identity(), 0);
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
