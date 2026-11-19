#pragma once
// MayaBinaryMb.h — Autodesk Maya Binary IFF Scene (.mb — FOR4 / FOR8) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace DetailMaya {

inline bool ParseMayaBinary(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 20) return false;
    const bool isFor4 = (std::memcmp(data, "FOR4", 4) == 0);
    const bool isFor8 = (std::memcmp(data, "FOR8", 4) == 0);
    if (!isFor4 && !isFor8) return false;

    const size_t grpHdrOff = isFor8 ? 16 : 8;
    if (grpHdrOff + 4 > size || std::memcmp(data + grpHdrOff, "Maya", 4) != 0) return false;
    mesh.formatName = L"Autodesk Maya Binary Scene (.mb)";

    auto walkIff = [&](auto& self, size_t start, size_t end, std::string_view groupType, int depth) -> void {
        if (depth > 24) return;
        std::vector<Vec3f> vrts;
        std::vector<MayaEdge> edges;
        std::vector<std::vector<int32_t>> faces;

        size_t p = start;
        const size_t hdrSz = isFor8 ? 16 : 8;
        const size_t align = isFor8 ? 8 : 4;

        while (p + hdrSz <= end) {
            char tag[5] = {};
            std::memcpy(tag, data + p, 4);
            uint64_t cLen = isFor8 ? Detail3D100::RdU64BE(data + p + 8) : Detail3D100::RdU32BE(data + p + 4);
            p += hdrSz;
            if (cLen > end - p) break;
            size_t cEnd = p + static_cast<size_t>(cLen);

            bool isGroup = (std::memcmp(tag, "FOR4", 4) == 0 || std::memcmp(tag, "FOR8", 4) == 0 ||
                            std::memcmp(tag, "LIST", 4) == 0 || std::memcmp(tag, "LIS8", 4) == 0 ||
                            std::memcmp(tag, "CAT4", 4) == 0 || std::memcmp(tag, "CAT8", 4) == 0 ||
                            std::memcmp(tag, "PROP", 4) == 0 || std::memcmp(tag, "PRO8", 4) == 0);
            if (isGroup) {
                size_t subTypeSpan = isFor8 ? 8 : 4;
                if (p + subTypeSpan <= cEnd) {
                    std::string_view subType(reinterpret_cast<const char*>(data + p), 4);
                    self(self, p + subTypeSpan, cEnd, subType, depth + 1);
                }
            } else if (groupType == "MESH" || groupType == "DMSH") {
                if (std::memcmp(tag, "VRTS", 4) == 0 && cLen >= 36 && (cLen % 12) == 0) {
                    size_t nv = static_cast<size_t>(cLen) / 12;
                    if (nv <= 1000000) {
                        vrts.resize(nv);
                        for (size_t i = 0; i < nv; ++i) {
                            vrts[i] = {
                                Detail3D100::RdF32BE(data + p + i * 12 + 0),
                                Detail3D100::RdF32BE(data + p + i * 12 + 4),
                                Detail3D100::RdF32BE(data + p + i * 12 + 8)
                            };
                        }
                    }
                } else if (std::memcmp(tag, "EDGE", 4) == 0 && cLen >= 9) {
                    // Maya .mb EDGE records are either 12 bytes (int32 v0, int32 v1, int32 flag) or 9 bytes (int32 v0, int32 v1, uint8 flag)
                    size_t stride = ((cLen % 12) == 0) ? 12 : (((cLen % 9) == 0) ? 9 : 0);
                    if (stride == 12 && (cLen % 9) == 0 && !vrts.empty()) {
                        // Disambiguate 36-byte chunk: check if stride 12 produces valid vertex indices
                        for (size_t k = 0; k < cLen; k += 12) {
                            int32_t a = static_cast<int32_t>(Detail3D100::RdU32BE(data + p + k));
                            int32_t b = static_cast<int32_t>(Detail3D100::RdU32BE(data + p + k + 4));
                            if (a < 0 || b < 0 || static_cast<size_t>(a) >= vrts.size() || static_cast<size_t>(b) >= vrts.size()) {
                                stride = 9;
                                break;
                            }
                        }
                    }
                    if (stride > 0) {
                        size_t ne = static_cast<size_t>(cLen) / stride;
                        edges.resize(ne);
                        for (size_t i = 0; i < ne; ++i) {
                            edges[i].v0 = static_cast<int32_t>(Detail3D100::RdU32BE(data + p + i * stride + 0));
                            edges[i].v1 = static_cast<int32_t>(Detail3D100::RdU32BE(data + p + i * stride + 4));
                        }
                    }
                } else if ((std::memcmp(tag, "FACE", 4) == 0 || std::memcmp(tag, "POLY", 4) == 0) && cLen >= 12 && (cLen % 4) == 0) {
                    size_t nInts = static_cast<size_t>(cLen) / 4;
                    // Format A: [count, e0, e1, ..., count, e0, ...]
                    bool countPrefixedOk = true;
                    size_t idx = 0;
                    std::vector<std::vector<int32_t>> parsedA;
                    while (idx < nInts) {
                        int32_t cnt = static_cast<int32_t>(Detail3D100::RdU32BE(data + p + idx * 4));
                        if (cnt < 3 || cnt > 256 || idx + 1 + static_cast<size_t>(cnt) > nInts) {
                            countPrefixedOk = false;
                            break;
                        }
                        std::vector<int32_t> loop(cnt);
                        for (int32_t k = 0; k < cnt; ++k) {
                            loop[k] = static_cast<int32_t>(Detail3D100::RdU32BE(data + p + (idx + 1 + k) * 4));
                        }
                        parsedA.push_back(std::move(loop));
                        idx += 1 + static_cast<size_t>(cnt);
                    }
                    if (countPrefixedOk && !parsedA.empty()) {
                        for (auto& lp : parsedA) faces.push_back(std::move(lp));
                    } else {
                        // Format B: Maya packed polyFaces stream (bit 30 = reversed edge, bit 31 = end of loop)
                        std::vector<int32_t> curLoop;
                        for (size_t k = 0; k < nInts; ++k) {
                            uint32_t w = Detail3D100::RdU32BE(data + p + k * 4);
                            bool endLoop = (w & 0x80000000u) != 0;
                            bool revEdge = (w & 0x40000000u) != 0;
                            int32_t eIdx = static_cast<int32_t>(w & 0x3FFFFFFFu);
                            curLoop.push_back(revEdge ? (-eIdx - 1) : eIdx);
                            if (endLoop) {
                                if (curLoop.size() >= 3) faces.push_back(std::move(curLoop));
                                curLoop.clear();
                            }
                        }
                    }
                }
            }

            size_t padded = (static_cast<size_t>(cLen) + (align - 1)) & ~(align - 1);
            if (padded > end - p) break;
            p += padded;
        }

        if (!vrts.empty() && !edges.empty() && !faces.empty()) {
            EmitMayaMeshNode(vrts, {}, edges, faces, DetailScad::Mat4::Identity(), mesh);
        }
    };

    uint64_t rootLen = isFor8 ? Detail3D100::RdU64BE(data + 8) : Detail3D100::RdU32BE(data + 4);
    size_t rootEnd = std::min<size_t>(size, (isFor8 ? 16 : 8) + static_cast<size_t>(rootLen));
    size_t bodyStart = isFor8 ? 24 : 12;
    if (bodyStart < rootEnd) {
        walkIff(walkIff, bodyStart, rootEnd, "Maya", 0);
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailMaya


inline bool ParseMayaBinaryMb(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    return DetailMaya::ParseMayaBinary(data, size, mesh);
}

} // namespace Formats
