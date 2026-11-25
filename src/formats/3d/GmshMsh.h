#pragma once
#include "../common/MeshBuild3D.h"
// GmshMsh.h — Gmsh Finite Element Mesh (.msh v1 / v2.2 / v4.0 / v4.1) 3D Parser
// Supports both ASCII (fileType=0) and Binary (fileType=1, LE & BE), arbitrary sparse
// node tags, 1st/2nd-order 2D & 3D elements, and 3D interior shared-face culling.

namespace Formats {

namespace DetailGmsh {

inline int NodesPerGmshElementType(int elmType) {
    switch (elmType) {
    case 1:  return 2;  // 2-node line
    case 2:  return 3;  // 3-node triangle
    case 3:  return 4;  // 4-node quadrangle
    case 4:  return 4;  // 4-node 3D tetrahedron
    case 5:  return 8;  // 8-node 3D hexahedron
    case 6:  return 6;  // 6-node 3D prism / wedge
    case 7:  return 5;  // 5-node 3D pyramid
    case 8:  return 3;  // 3-node 2nd-order line
    case 9:  return 6;  // 6-node 2nd-order triangle
    case 10: return 9;  // 9-node 2nd-order quadrangle
    case 11: return 10; // 10-node 2nd-order tetrahedron
    case 12: return 27; // 27-node 2nd-order hexahedron
    case 13: return 18; // 18-node 2nd-order prism
    case 14: return 14; // 14-node 2nd-order pyramid
    case 15: return 1;  // 1-node point
    case 16: return 8;  // 8-node serendipity quadrangle
    case 17: return 20; // 20-node serendipity hexahedron
    case 18: return 15; // 15-node serendipity prism
    case 19: return 13; // 13-node serendipity pyramid
    default: return -1;
    }
}

inline uint32_t Swap32Gmsh(uint32_t v) {
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0xFF000000u) >> 24);
}

inline uint64_t Swap64Gmsh(uint64_t v) {
    return (static_cast<uint64_t>(Swap32Gmsh(static_cast<uint32_t>(v & 0xFFFFFFFFULL))) << 32) |
            static_cast<uint64_t>(Swap32Gmsh(static_cast<uint32_t>(v >> 32)));
}

inline bool ReadBinI32Gmsh(const uint8_t*& bp, const uint8_t* bend, bool bigEndian, int32_t& out) {
    if (static_cast<size_t>(bend - bp) < 4) return false;
    uint32_t u = 0;
    std::memcpy(&u, bp, 4);
    bp += 4;
    if (bigEndian) u = Swap32Gmsh(u);
    out = static_cast<int32_t>(u);
    return true;
}

inline bool ReadBinTagGmsh(const uint8_t*& bp, const uint8_t* bend, bool bigEndian, size_t tagBytes, uint64_t& out) {
    if (tagBytes == 4) {
        int32_t v = 0;
        if (!ReadBinI32Gmsh(bp, bend, bigEndian, v) || v < 0) return false;
        out = static_cast<uint64_t>(v);
        return true;
    }
    if (static_cast<size_t>(bend - bp) < 8) return false;
    uint64_t u = 0;
    std::memcpy(&u, bp, 8);
    bp += 8;
    if (bigEndian) u = Swap64Gmsh(u);
    out = u;
    return true;
}

inline bool ReadBinF64Gmsh(const uint8_t*& bp, const uint8_t* bend, bool bigEndian, double& out) {
    if (static_cast<size_t>(bend - bp) < 8) return false;
    uint64_t u = 0;
    std::memcpy(&u, bp, 8);
    bp += 8;
    if (bigEndian) u = Swap64Gmsh(u);
    std::memcpy(&out, &u, 8);
    return std::isfinite(out);
}

inline void SkipAsciiWsGmsh(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
}

template <class Int>
inline bool NextAsciiIntGmsh(const char*& p, const char* end, Int& out) {
    SkipAsciiWsGmsh(p, end);
    if (p >= end) return false;
    const char* s = p;
    while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') ++p;
    auto [ptr, ec] = std::from_chars(s, p, out);
    return ec == std::errc{} && ptr == p;
}
inline bool NextAsciiU64Gmsh(const char*& p, const char* end, uint64_t& out) { return NextAsciiIntGmsh(p, end, out); }

inline bool NextAsciiI64Gmsh(const char*& p, const char* end, int64_t& out) { return NextAsciiIntGmsh(p, end, out); }

inline bool NextAsciiDoubleGmsh(const char*& p, const char* end, double& out) {
    SkipAsciiWsGmsh(p, end);
    if (p >= end) return false;
    const char* s = p;
    while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') ++p;
    size_t len = static_cast<size_t>(p - s);
    if (len == 0 || len >= 64) return false;
    char buf[64];
    std::memcpy(buf, s, len);
    buf[len] = '\0';
    char* next = nullptr;
    out = std::strtod(buf, &next);
    return next != buf && std::isfinite(out);
}

inline bool ReadLineGmsh(const uint8_t*& bp, const uint8_t* bend, std::string_view& outLine) {
    if (bp >= bend) return false;
    const uint8_t* nl = static_cast<const uint8_t*>(std::memchr(bp, '\n', static_cast<size_t>(bend - bp)));
    const uint8_t* le = nl ? nl : bend;
    size_t len = static_cast<size_t>(le - bp);
    if (len > 0 && bp[len - 1] == '\r') --len;
    outLine = std::string_view(reinterpret_cast<const char*>(bp), len);
    bp = nl ? (nl + 1) : bend;
    return true;
}

using GmshFaceMap = FemSolidSurface::FaceMap;

inline void RecordGmshFace(GmshFaceMap& map, const uint32_t* fverts, uint8_t n, bool volumetric) {
    FemSolidSurface::RecordFace(map, fverts, n, volumetric);
}

inline void EmitGmshElement(
    int elmType, const uint32_t* v, int npe,
    GmshFaceMap& faceMap,
    std::vector<std::pair<uint32_t, uint32_t>>& lineSegs)
{
    switch (elmType) {
    case 1: // 2-node line
    case 8: // 3-node 2nd-order line (primary endpoints 0, 1)
        if (npe >= 2) lineSegs.emplace_back(v[0], v[1]);
        break;
    case 2: // 3-node triangle
    case 9: // 6-node 2nd-order triangle (corner nodes 0, 1, 2)
        if (npe >= 3) RecordGmshFace(faceMap, v, 3, false);
        break;
    case 3:  // 4-node quadrangle
    case 10: // 9-node 2nd-order quadrangle
    case 16: // 8-node serendipity quadrangle
        if (npe >= 4) RecordGmshFace(faceMap, v, 4, false);
        break;
    case 4:  // 4-node 3D tetrahedron
    case 11: // 10-node 2nd-order tetrahedron
        if (npe >= 4) {
            const uint32_t f0[3] = { v[0], v[2], v[1] };
            const uint32_t f1[3] = { v[0], v[1], v[3] };
            const uint32_t f2[3] = { v[1], v[2], v[3] };
            const uint32_t f3[3] = { v[2], v[0], v[3] };
            RecordGmshFace(faceMap, f0, 3, true);
            RecordGmshFace(faceMap, f1, 3, true);
            RecordGmshFace(faceMap, f2, 3, true);
            RecordGmshFace(faceMap, f3, 3, true);
        }
        break;
    case 5:  // 8-node 3D hexahedron
    case 12: // 27-node 2nd-order hexahedron
    case 17: // 20-node serendipity hexahedron
        if (npe >= 8) {
            const uint32_t q0[4] = { v[0], v[3], v[2], v[1] };
            const uint32_t q1[4] = { v[4], v[5], v[6], v[7] };
            const uint32_t q2[4] = { v[0], v[1], v[5], v[4] };
            const uint32_t q3[4] = { v[1], v[2], v[6], v[5] };
            const uint32_t q4[4] = { v[2], v[3], v[7], v[6] };
            const uint32_t q5[4] = { v[3], v[0], v[4], v[7] };
            RecordGmshFace(faceMap, q0, 4, true);
            RecordGmshFace(faceMap, q1, 4, true);
            RecordGmshFace(faceMap, q2, 4, true);
            RecordGmshFace(faceMap, q3, 4, true);
            RecordGmshFace(faceMap, q4, 4, true);
            RecordGmshFace(faceMap, q5, 4, true);
        }
        break;
    case 6:  // 6-node 3D prism / wedge
    case 13: // 18-node 2nd-order prism
    case 18: // 15-node serendipity prism
        if (npe >= 6) {
            const uint32_t t0[3] = { v[0], v[2], v[1] };
            const uint32_t t1[3] = { v[3], v[4], v[5] };
            const uint32_t q0[4] = { v[0], v[1], v[4], v[3] };
            const uint32_t q1[4] = { v[1], v[2], v[5], v[4] };
            const uint32_t q2[4] = { v[2], v[0], v[3], v[5] };
            RecordGmshFace(faceMap, t0, 3, true);
            RecordGmshFace(faceMap, t1, 3, true);
            RecordGmshFace(faceMap, q0, 4, true);
            RecordGmshFace(faceMap, q1, 4, true);
            RecordGmshFace(faceMap, q2, 4, true);
        }
        break;
    case 7:  // 5-node 3D pyramid
    case 14: // 14-node 2nd-order pyramid
    case 19: // 13-node serendipity pyramid
        if (npe >= 5) {
            const uint32_t q0[4] = { v[0], v[3], v[2], v[1] };
            const uint32_t t0[3] = { v[0], v[1], v[4] };
            const uint32_t t1[3] = { v[1], v[2], v[4] };
            const uint32_t t2[3] = { v[2], v[3], v[4] };
            const uint32_t t3[3] = { v[3], v[0], v[4] };
            RecordGmshFace(faceMap, q0, 4, true);
            RecordGmshFace(faceMap, t0, 3, true);
            RecordGmshFace(faceMap, t1, 3, true);
            RecordGmshFace(faceMap, t2, 3, true);
            RecordGmshFace(faceMap, t3, 3, true);
        }
        break;
    default:
        break;
    }
}

} // namespace DetailGmsh

inline bool ParseGmshMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 16) return false;
    std::string_view fullSv(reinterpret_cast<const char*>(data), size);
    if (fullSv.find("$Nodes") == std::string_view::npos && fullSv.find("$NOD") == std::string_view::npos) {
        return false;
    }

    double version = 2.2;
    int majorVer = 2;
    int fileType = 0; // 0 = ASCII, 1 = Binary
    size_t dataSize = 8;
    bool bigEndian = false;

    const uint8_t* bp = data;
    const uint8_t* const bend = data + size;

    std::unordered_map<uint64_t, uint32_t> nodeTagToIdx;
    DetailGmsh::GmshFaceMap faceMap;
    std::vector<std::pair<uint32_t, uint32_t>> lineSegs;

    while (bp < bend) {
        std::string_view line;
        if (!DetailGmsh::ReadLineGmsh(bp, bend, line)) break;
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
        if (line.empty()) continue;

        if (line == "$MeshFormat") {
            std::string_view fmtLine;
            if (!DetailGmsh::ReadLineGmsh(bp, bend, fmtLine)) return false;
            const char* fp = fmtLine.data();
            const char* fe = fp + fmtLine.size();
            int64_t ft = 0, ds = 8;
            if (!DetailGmsh::NextAsciiDoubleGmsh(fp, fe, version) ||
                !DetailGmsh::NextAsciiI64Gmsh(fp, fe, ft) ||
                !DetailGmsh::NextAsciiI64Gmsh(fp, fe, ds)) {
                return false;
            }
            if (ft != 0 && ft != 1) return false;
            if (ds != 4 && ds != 8) return false;
            fileType = static_cast<int>(ft);
            dataSize = static_cast<size_t>(ds);
            majorVer = static_cast<int>(std::floor(version + 1e-6));
            if (majorVer != 2 && majorVer != 4 && majorVer != 1) return false;

            if (fileType == 1) {
                if (static_cast<size_t>(bend - bp) < 4) return false;
                uint32_t one = 0;
                std::memcpy(&one, bp, 4);
                bp += 4;
                if (one == 1u) bigEndian = false;
                else if (one == 0x01000000u) bigEndian = true;
                else return false;
                if (bp < bend && *bp == '\r') ++bp;
                if (bp < bend && *bp == '\n') ++bp;
            }
            std::string_view endFmt;
            if (!DetailGmsh::ReadLineGmsh(bp, bend, endFmt)) return false;
        } else if (line == "$Nodes" || line == "$NOD") {
            if (line == "$NOD") majorVer = 1;
            const char* endTag = (line == "$NOD") ? "$ENDNOD" : "$EndNodes";

            if (majorVer <= 2) {
                std::string_view cntLine;
                if (!DetailGmsh::ReadLineGmsh(bp, bend, cntLine)) return false;
                const char* cp = cntLine.data();
                const char* ce = cp + cntLine.size();
                uint64_t numNodes = 0;
                if (!DetailGmsh::NextAsciiU64Gmsh(cp, ce, numNodes) || numNodes == 0 || numNodes > 25000000ULL) {
                    return false;
                }
                mesh.vertices.reserve(mesh.vertices.size() + static_cast<size_t>(std::min<uint64_t>(numNodes, 1000000)));
                nodeTagToIdx.reserve(static_cast<size_t>(std::min<uint64_t>(numNodes, 1000000)));

                if (fileType == 1) {
                    for (uint64_t i = 0; i < numNodes; ++i) {
                        int32_t tag = 0;
                        double x = 0.0, y = 0.0, z = 0.0;
                        if (!DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, tag) ||
                            !DetailGmsh::ReadBinF64Gmsh(bp, bend, bigEndian, x) ||
                            !DetailGmsh::ReadBinF64Gmsh(bp, bend, bigEndian, y) ||
                            !DetailGmsh::ReadBinF64Gmsh(bp, bend, bigEndian, z)) {
                            return false;
                        }
                        if (tag < 0) return false;
                        uint32_t dense = static_cast<uint32_t>(mesh.vertices.size());
                        mesh.vertices.push_back({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) });
                        nodeTagToIdx[static_cast<uint64_t>(tag)] = dense;
                    }
                    if (bp < bend && *bp == '\r') ++bp;
                    if (bp < bend && *bp == '\n') ++bp;
                } else {
                    for (uint64_t i = 0; i < numNodes; ++i) {
                        std::string_view nl;
                        if (!DetailGmsh::ReadLineGmsh(bp, bend, nl)) return false;
                        if (nl.find(endTag) != std::string_view::npos) break;
                        const char* p = nl.data();
                        const char* e = p + nl.size();
                        uint64_t tag = 0;
                        double x = 0.0, y = 0.0, z = 0.0;
                        if (!DetailGmsh::NextAsciiU64Gmsh(p, e, tag) ||
                            !DetailGmsh::NextAsciiDoubleGmsh(p, e, x) ||
                            !DetailGmsh::NextAsciiDoubleGmsh(p, e, y) ||
                            !DetailGmsh::NextAsciiDoubleGmsh(p, e, z)) {
                            return false;
                        }
                        uint32_t dense = static_cast<uint32_t>(mesh.vertices.size());
                        mesh.vertices.push_back({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) });
                        nodeTagToIdx[tag] = dense;
                    }
                }
            } else {
                // Gmsh MSH 4.0 / 4.1 $Nodes
                const bool is41 = (version >= 4.05);
                if (fileType == 1) {
                    uint64_t numBlocks = 0, totalNodes = 0, minTag = 0, maxTag = 0;
                    if (!DetailGmsh::ReadBinTagGmsh(bp, bend, bigEndian, dataSize, numBlocks) ||
                        !DetailGmsh::ReadBinTagGmsh(bp, bend, bigEndian, dataSize, totalNodes)) {
                        return false;
                    }
                    if (is41) {
                        if (!DetailGmsh::ReadBinTagGmsh(bp, bend, bigEndian, dataSize, minTag) ||
                            !DetailGmsh::ReadBinTagGmsh(bp, bend, bigEndian, dataSize, maxTag)) {
                            return false;
                        }
                    }
                    if (totalNodes == 0 || totalNodes > 25000000ULL || numBlocks > 5000000ULL) return false;
                    mesh.vertices.reserve(mesh.vertices.size() + static_cast<size_t>(std::min<uint64_t>(totalNodes, 1000000)));
                    nodeTagToIdx.reserve(static_cast<size_t>(std::min<uint64_t>(totalNodes, 1000000)));

                    std::vector<uint64_t> blockTags;
                    for (uint64_t b = 0; b < numBlocks; ++b) {
                        int32_t h0 = 0, h1 = 0, parametric = 0;
                        uint64_t nodesInBlock = 0;
                        if (!DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, h0) ||
                            !DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, h1) ||
                            !DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, parametric) ||
                            !DetailGmsh::ReadBinTagGmsh(bp, bend, bigEndian, dataSize, nodesInBlock)) {
                            return false;
                        }
                        if (nodesInBlock > totalNodes) return false;
                        int32_t entityDim = is41 ? h0 : h1;
                        size_t extraParam = (parametric != 0) ? static_cast<size_t>(std::clamp<int32_t>(entityDim, 0, 3)) : 0;

                        if (is41) {
                            blockTags.resize(static_cast<size_t>(nodesInBlock));
                            for (size_t i = 0; i < static_cast<size_t>(nodesInBlock); ++i) {
                                if (!DetailGmsh::ReadBinTagGmsh(bp, bend, bigEndian, dataSize, blockTags[i])) return false;
                            }
                            for (size_t i = 0; i < static_cast<size_t>(nodesInBlock); ++i) {
                                double x = 0.0, y = 0.0, z = 0.0, dummy = 0.0;
                                if (!DetailGmsh::ReadBinF64Gmsh(bp, bend, bigEndian, x) ||
                                    !DetailGmsh::ReadBinF64Gmsh(bp, bend, bigEndian, y) ||
                                    !DetailGmsh::ReadBinF64Gmsh(bp, bend, bigEndian, z)) {
                                    return false;
                                }
                                for (size_t p = 0; p < extraParam; ++p) {
                                    if (!DetailGmsh::ReadBinF64Gmsh(bp, bend, bigEndian, dummy)) return false;
                                }
                                uint32_t dense = static_cast<uint32_t>(mesh.vertices.size());
                                mesh.vertices.push_back({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) });
                                nodeTagToIdx[blockTags[i]] = dense;
                            }
                        } else {
                            for (size_t i = 0; i < static_cast<size_t>(nodesInBlock); ++i) {
                                int32_t tag40 = 0;
                                double x = 0.0, y = 0.0, z = 0.0, dummy = 0.0;
                                if (!DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, tag40) || tag40 < 0 ||
                                    !DetailGmsh::ReadBinF64Gmsh(bp, bend, bigEndian, x) ||
                                    !DetailGmsh::ReadBinF64Gmsh(bp, bend, bigEndian, y) ||
                                    !DetailGmsh::ReadBinF64Gmsh(bp, bend, bigEndian, z)) {
                                    return false;
                                }
                                for (size_t p = 0; p < extraParam; ++p) {
                                    if (!DetailGmsh::ReadBinF64Gmsh(bp, bend, bigEndian, dummy)) return false;
                                }
                                uint32_t dense = static_cast<uint32_t>(mesh.vertices.size());
                                mesh.vertices.push_back({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) });
                                nodeTagToIdx[static_cast<uint64_t>(tag40)] = dense;
                            }
                        }
                    }
                    if (bp < bend && *bp == '\r') ++bp;
                    if (bp < bend && *bp == '\n') ++bp;
                } else {
                    // MSH 4.0 / 4.1 ASCII $Nodes: locate $EndNodes and stream tokens cleanly
                    std::string_view rem(reinterpret_cast<const char*>(bp), static_cast<size_t>(bend - bp));
                    size_t endPos = rem.find("$EndNodes");
                    if (endPos == std::string_view::npos) return false;
                    const char* ap = rem.data();
                    const char* const ae = ap + endPos;

                    // Header line has 4 ints in MSH 4.1 or 2 ints in MSH 4.0
                    const char* hdrNl = static_cast<const char*>(std::memchr(ap, '\n', static_cast<size_t>(ae - ap)));
                    if (!hdrNl) return false;
                    const char* hp = ap;
                    uint64_t numBlocks = 0, totalNodes = 0, minTag = 0, maxTag = 0;
                    if (!DetailGmsh::NextAsciiU64Gmsh(hp, hdrNl, numBlocks) ||
                        !DetailGmsh::NextAsciiU64Gmsh(hp, hdrNl, totalNodes)) {
                        return false;
                    }
                    bool hdrHas4 = DetailGmsh::NextAsciiU64Gmsh(hp, hdrNl, minTag) &&
                                   DetailGmsh::NextAsciiU64Gmsh(hp, hdrNl, maxTag);
                    ap = hdrNl + 1;

                    if (totalNodes == 0 || totalNodes > 25000000ULL || numBlocks > 5000000ULL) return false;
                    mesh.vertices.reserve(mesh.vertices.size() + static_cast<size_t>(std::min<uint64_t>(totalNodes, 1000000)));
                    nodeTagToIdx.reserve(static_cast<size_t>(std::min<uint64_t>(totalNodes, 1000000)));

                    std::vector<uint64_t> blockTags;
                    for (uint64_t b = 0; b < numBlocks; ++b) {
                        int64_t h0 = 0, h1 = 0, parametric = 0;
                        uint64_t nodesInBlock = 0;
                        if (!DetailGmsh::NextAsciiI64Gmsh(ap, ae, h0) ||
                            !DetailGmsh::NextAsciiI64Gmsh(ap, ae, h1) ||
                            !DetailGmsh::NextAsciiI64Gmsh(ap, ae, parametric) ||
                            !DetailGmsh::NextAsciiU64Gmsh(ap, ae, nodesInBlock)) {
                            return false;
                        }
                        if (nodesInBlock > totalNodes) return false;
                        int64_t entityDim = hdrHas4 ? h0 : h1;
                        size_t extraParam = (parametric != 0) ? static_cast<size_t>(std::clamp<int64_t>(entityDim, 0, 3)) : 0;

                        if (hdrHas4) {
                            blockTags.resize(static_cast<size_t>(nodesInBlock));
                            for (size_t i = 0; i < static_cast<size_t>(nodesInBlock); ++i) {
                                if (!DetailGmsh::NextAsciiU64Gmsh(ap, ae, blockTags[i])) return false;
                            }
                            for (size_t i = 0; i < static_cast<size_t>(nodesInBlock); ++i) {
                                double x = 0.0, y = 0.0, z = 0.0, dummy = 0.0;
                                if (!DetailGmsh::NextAsciiDoubleGmsh(ap, ae, x) ||
                                    !DetailGmsh::NextAsciiDoubleGmsh(ap, ae, y) ||
                                    !DetailGmsh::NextAsciiDoubleGmsh(ap, ae, z)) {
                                    return false;
                                }
                                for (size_t p = 0; p < extraParam; ++p) {
                                    if (!DetailGmsh::NextAsciiDoubleGmsh(ap, ae, dummy)) return false;
                                }
                                uint32_t dense = static_cast<uint32_t>(mesh.vertices.size());
                                mesh.vertices.push_back({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) });
                                nodeTagToIdx[blockTags[i]] = dense;
                            }
                        } else {
                            for (size_t i = 0; i < static_cast<size_t>(nodesInBlock); ++i) {
                                uint64_t tag = 0;
                                double x = 0.0, y = 0.0, z = 0.0, dummy = 0.0;
                                if (!DetailGmsh::NextAsciiU64Gmsh(ap, ae, tag) ||
                                    !DetailGmsh::NextAsciiDoubleGmsh(ap, ae, x) ||
                                    !DetailGmsh::NextAsciiDoubleGmsh(ap, ae, y) ||
                                    !DetailGmsh::NextAsciiDoubleGmsh(ap, ae, z)) {
                                    return false;
                                }
                                for (size_t p = 0; p < extraParam; ++p) {
                                    if (!DetailGmsh::NextAsciiDoubleGmsh(ap, ae, dummy)) return false;
                                }
                                uint32_t dense = static_cast<uint32_t>(mesh.vertices.size());
                                mesh.vertices.push_back({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) });
                                nodeTagToIdx[tag] = dense;
                            }
                        }
                    }
                    bp += endPos;
                }
            }

            // Advance past $EndNodes / $ENDNOD if not already consumed
            while (bp < bend) {
                std::string_view el;
                if (!DetailGmsh::ReadLineGmsh(bp, bend, el)) break;
                if (el.find(endTag) != std::string_view::npos) break;
            }
        } else if (line == "$Elements" || line == "$ELM") {
            const char* endTag = (line == "$ELM") ? "$ENDELM" : "$EndElements";
            if (nodeTagToIdx.empty() || mesh.vertices.empty()) return false;

            if (majorVer <= 2) {
                std::string_view cntLine;
                if (!DetailGmsh::ReadLineGmsh(bp, bend, cntLine)) return false;
                const char* cp = cntLine.data();
                const char* ce = cp + cntLine.size();
                uint64_t numElems = 0;
                if (!DetailGmsh::NextAsciiU64Gmsh(cp, ce, numElems) || numElems == 0 || numElems > 50000000ULL) {
                    return false;
                }
                faceMap.reserve(static_cast<size_t>(std::min<uint64_t>(numElems * 2, 1000000)));

                if (fileType == 1) {
                    uint64_t totalRead = 0;
                    uint32_t mapped[32] = {};
                    while (totalRead < numElems) {
                        int32_t elmType = 0, numInBlock = 0, numTags = 0;
                        if (!DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, elmType) ||
                            !DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, numInBlock) ||
                            !DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, numTags)) {
                            return false;
                        }
                        int npe = DetailGmsh::NodesPerGmshElementType(elmType);
                        if (npe <= 0 || npe > 32 || numInBlock <= 0 || numTags < 0 || numTags > 64 ||
                            totalRead + static_cast<uint64_t>(numInBlock) > numElems) {
                            return false;
                        }
                        for (int32_t e = 0; e < numInBlock; ++e) {
                            int32_t elmTag = 0, tagDummy = 0;
                            if (!DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, elmTag)) return false;
                            for (int32_t t = 0; t < numTags; ++t) {
                                if (!DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, tagDummy)) return false;
                            }
                            bool allFound = true;
                            for (int k = 0; k < npe; ++k) {
                                int32_t nt = 0;
                                if (!DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, nt) || nt < 0) return false;
                                auto it = nodeTagToIdx.find(static_cast<uint64_t>(nt));
                                if (it == nodeTagToIdx.end()) { allFound = false; }
                                else { mapped[k] = it->second; }
                            }
                            if (allFound) {
                                DetailGmsh::EmitGmshElement(elmType, mapped, npe, faceMap, lineSegs);
                            }
                        }
                        totalRead += static_cast<uint64_t>(numInBlock);
                    }
                    if (bp < bend && *bp == '\r') ++bp;
                    if (bp < bend && *bp == '\n') ++bp;
                } else {
                    uint32_t mapped[32] = {};
                    for (uint64_t e = 0; e < numElems; ++e) {
                        std::string_view eln;
                        if (!DetailGmsh::ReadLineGmsh(bp, bend, eln)) break;
                        if (eln.find(endTag) != std::string_view::npos) break;
                        const char* p = eln.data();
                        const char* pe = p + eln.size();
                        int64_t elmId = 0, elmType = 0;
                        if (!DetailGmsh::NextAsciiI64Gmsh(p, pe, elmId) ||
                            !DetailGmsh::NextAsciiI64Gmsh(p, pe, elmType)) {
                            continue;
                        }
                        int npe = DetailGmsh::NodesPerGmshElementType(static_cast<int>(elmType));
                        if (npe <= 0 || npe > 32) continue;

                        if (majorVer == 1) {
                            // Gmsh 1.0: elm-number elm-type reg-phys reg-elem number-of-nodes node-number-list
                            int64_t rp = 0, re = 0, nn = 0;
                            if (!DetailGmsh::NextAsciiI64Gmsh(p, pe, rp) ||
                                !DetailGmsh::NextAsciiI64Gmsh(p, pe, re) ||
                                !DetailGmsh::NextAsciiI64Gmsh(p, pe, nn)) {
                                continue;
                            }
                        } else {
                            int64_t numTags = 0, td = 0;
                            if (!DetailGmsh::NextAsciiI64Gmsh(p, pe, numTags) || numTags < 0 || numTags > 64) continue;
                            for (int64_t t = 0; t < numTags; ++t) {
                                if (!DetailGmsh::NextAsciiI64Gmsh(p, pe, td)) break;
                            }
                        }

                        bool allFound = true;
                        for (int k = 0; k < npe; ++k) {
                            uint64_t nt = 0;
                            if (!DetailGmsh::NextAsciiU64Gmsh(p, pe, nt)) { allFound = false; break; }
                            auto it = nodeTagToIdx.find(nt);
                            if (it == nodeTagToIdx.end()) { allFound = false; }
                            else { mapped[k] = it->second; }
                        }
                        if (allFound) {
                            DetailGmsh::EmitGmshElement(static_cast<int>(elmType), mapped, npe, faceMap, lineSegs);
                        }
                    }
                }
            } else {
                // Gmsh MSH 4.0 / 4.1 $Elements
                const bool is41 = (version >= 4.05);
                if (fileType == 1) {
                    uint64_t numBlocks = 0, totalElems = 0, minTag = 0, maxTag = 0;
                    if (!DetailGmsh::ReadBinTagGmsh(bp, bend, bigEndian, dataSize, numBlocks) ||
                        !DetailGmsh::ReadBinTagGmsh(bp, bend, bigEndian, dataSize, totalElems)) {
                        return false;
                    }
                    if (is41) {
                        if (!DetailGmsh::ReadBinTagGmsh(bp, bend, bigEndian, dataSize, minTag) ||
                            !DetailGmsh::ReadBinTagGmsh(bp, bend, bigEndian, dataSize, maxTag)) {
                            return false;
                        }
                    }
                    if (totalElems == 0 || totalElems > 50000000ULL || numBlocks > 5000000ULL) return false;
                    faceMap.reserve(static_cast<size_t>(std::min<uint64_t>(totalElems * 2, 1000000)));

                    uint32_t mapped[32] = {};
                    for (uint64_t b = 0; b < numBlocks; ++b) {
                        int32_t h0 = 0, h1 = 0, elmType = 0;
                        uint64_t elemsInBlock = 0;
                        if (!DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, h0) ||
                            !DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, h1) ||
                            !DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, elmType) ||
                            !DetailGmsh::ReadBinTagGmsh(bp, bend, bigEndian, dataSize, elemsInBlock)) {
                            return false;
                        }
                        int npe = DetailGmsh::NodesPerGmshElementType(elmType);
                        if (npe <= 0 || npe > 32 || elemsInBlock > totalElems) return false;

                        for (uint64_t e = 0; e < elemsInBlock; ++e) {
                            uint64_t elmTag = 0;
                            if (is41) {
                                if (!DetailGmsh::ReadBinTagGmsh(bp, bend, bigEndian, dataSize, elmTag)) return false;
                            } else {
                                int32_t et40 = 0;
                                if (!DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, et40)) return false;
                            }
                            bool allFound = true;
                            for (int k = 0; k < npe; ++k) {
                                uint64_t nt = 0;
                                if (is41) {
                                    if (!DetailGmsh::ReadBinTagGmsh(bp, bend, bigEndian, dataSize, nt)) return false;
                                } else {
                                    int32_t nt40 = 0;
                                    if (!DetailGmsh::ReadBinI32Gmsh(bp, bend, bigEndian, nt40) || nt40 < 0) return false;
                                    nt = static_cast<uint64_t>(nt40);
                                }
                                auto it = nodeTagToIdx.find(nt);
                                if (it == nodeTagToIdx.end()) allFound = false;
                                else mapped[k] = it->second;
                            }
                            if (allFound) {
                                DetailGmsh::EmitGmshElement(elmType, mapped, npe, faceMap, lineSegs);
                            }
                        }
                    }
                    if (bp < bend && *bp == '\r') ++bp;
                    if (bp < bend && *bp == '\n') ++bp;
                } else {
                    std::string_view rem(reinterpret_cast<const char*>(bp), static_cast<size_t>(bend - bp));
                    size_t endPos = rem.find("$EndElements");
                    if (endPos == std::string_view::npos) return false;
                    const char* ap = rem.data();
                    const char* const ae = ap + endPos;

                    const char* hdrNl = static_cast<const char*>(std::memchr(ap, '\n', static_cast<size_t>(ae - ap)));
                    if (!hdrNl) return false;
                    const char* hp = ap;
                    uint64_t numBlocks = 0, totalElems = 0, minTag = 0, maxTag = 0;
                    if (!DetailGmsh::NextAsciiU64Gmsh(hp, hdrNl, numBlocks) ||
                        !DetailGmsh::NextAsciiU64Gmsh(hp, hdrNl, totalElems)) {
                        return false;
                    }
                    (void)DetailGmsh::NextAsciiU64Gmsh(hp, hdrNl, minTag);
                    (void)DetailGmsh::NextAsciiU64Gmsh(hp, hdrNl, maxTag);
                    ap = hdrNl + 1;

                    if (totalElems == 0 || totalElems > 50000000ULL || numBlocks > 5000000ULL) return false;
                    faceMap.reserve(static_cast<size_t>(std::min<uint64_t>(totalElems * 2, 1000000)));

                    uint32_t mapped[32] = {};
                    for (uint64_t b = 0; b < numBlocks; ++b) {
                        int64_t h0 = 0, h1 = 0, elmType = 0;
                        uint64_t elemsInBlock = 0;
                        if (!DetailGmsh::NextAsciiI64Gmsh(ap, ae, h0) ||
                            !DetailGmsh::NextAsciiI64Gmsh(ap, ae, h1) ||
                            !DetailGmsh::NextAsciiI64Gmsh(ap, ae, elmType) ||
                            !DetailGmsh::NextAsciiU64Gmsh(ap, ae, elemsInBlock)) {
                            return false;
                        }
                        int npe = DetailGmsh::NodesPerGmshElementType(static_cast<int>(elmType));
                        if (npe <= 0 || npe > 32 || elemsInBlock > totalElems) return false;

                        for (uint64_t e = 0; e < elemsInBlock; ++e) {
                            uint64_t elmTag = 0;
                            if (!DetailGmsh::NextAsciiU64Gmsh(ap, ae, elmTag)) return false;
                            bool allFound = true;
                            for (int k = 0; k < npe; ++k) {
                                uint64_t nt = 0;
                                if (!DetailGmsh::NextAsciiU64Gmsh(ap, ae, nt)) return false;
                                auto it = nodeTagToIdx.find(nt);
                                if (it == nodeTagToIdx.end()) allFound = false;
                                else mapped[k] = it->second;
                            }
                            if (allFound) {
                                DetailGmsh::EmitGmshElement(static_cast<int>(elmType), mapped, npe, faceMap, lineSegs);
                            }
                        }
                    }
                    bp += endPos;
                }
            }

            while (bp < bend) {
                std::string_view el;
                if (!DetailGmsh::ReadLineGmsh(bp, bend, el)) break;
                if (el.find(endTag) != std::string_view::npos) break;
            }
        }
    }

    mesh.formatName = (majorVer == 4)
        ? ((fileType == 1) ? L"Gmsh 4.x FEM Mesh (.msh Binary)" : L"Gmsh 4.x FEM Mesh (.msh)")
        : ((fileType == 1) ? L"Gmsh 2.x FEM Mesh (.msh Binary)" : L"Gmsh Finite Element Mesh (.msh)");

    FemSolidSurface::EmitBoundaryFacesDirect(faceMap, mesh);

    if (mesh.tris.empty() && !lineSegs.empty() && !mesh.vertices.empty()) {
        std::vector<Vec3f> baseVerts = std::move(mesh.vertices);
        mesh.vertices.clear();
        Vec3f bmin = baseVerts[0], bmax = baseVerts[0];
        for (const auto& v : baseVerts) {
            bmin.x = std::min(bmin.x, v.x); bmin.y = std::min(bmin.y, v.y); bmin.z = std::min(bmin.z, v.z);
            bmax.x = std::max(bmax.x, v.x); bmax.y = std::max(bmax.y, v.y); bmax.z = std::max(bmax.z, v.z);
        }
        float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 1e-4f });
        float rad = span * 0.008f;
        for (const auto& seg : lineSegs) {
            if (seg.first < baseVerts.size() && seg.second < baseVerts.size()) {
                MeshBuild3D::EmitEdgeStrut(mesh, baseVerts[seg.first], baseVerts[seg.second], rad);
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
