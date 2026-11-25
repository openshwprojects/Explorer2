#pragma once
// AutodeskDwfCad.h — Autodesk Design Web Format v6 (.dwf) & Design Web Format XPS (.dwfx) 3D CAD Reader
// Extracts embedded .w3d Hoops Stream Format (W3D / HSF) 3D scene streams from DWF ZIP / DWFX OPC packages
// and decodes binary opcodes: TK_Open_Segment, TK_Close_Segment, TK_Reopen_Segment, TK_Modelling_Matrix,
// TK_Shell (uncompressed & 8/16-bit bounding-box quantized vertices/faces), TK_Polytriangle, TK_Polygon, TK_Line.
#include "../../ByteIO.h"
#include "../../ZipDeflate.h"
#include "../common/MeshBuild3D.h"

namespace Formats {

namespace DetailDwfW3d {

struct HsfMat4 {
    float m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    static HsfMat4 Multiply(const HsfMat4& local, const HsfMat4& parent) {
        // HOOPS 3DGS uses row-vector convention: v' = v * local * parent
        HsfMat4 r{};
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                r.m[row * 4 + col] =
                    local.m[row * 4 + 0] * parent.m[0 * 4 + col] +
                    local.m[row * 4 + 1] * parent.m[1 * 4 + col] +
                    local.m[row * 4 + 2] * parent.m[2 * 4 + col] +
                    local.m[row * 4 + 3] * parent.m[3 * 4 + col];
            }
        }
        return r;
    }

    Vec3f TransformCadToYUp(float x, float y, float z) const {
        float wx = x * m[0] + y * m[4] + z * m[8]  + m[12];
        float wy = x * m[1] + y * m[5] + z * m[9]  + m[13];
        float wz = x * m[2] + y * m[6] + z * m[10] + m[14];
        float ww = x * m[3] + y * m[7] + z * m[11] + m[15];
        if (std::isfinite(ww) && std::fabs(ww) > 1e-6f && std::fabs(ww - 1.0f) > 1e-5f) {
            float invW = 1.0f / ww;
            wx *= invW; wy *= invW; wz *= invW;
        }
        return { wx, wz, -wy };
    }
};

inline bool ReadHsfString(const uint8_t* data, size_t size, size_t& pos) {
    if (pos >= size) return false;
    uint32_t len = data[pos++];
    if (len == 254u) {
        if (pos + 2 > size) return false;
        len = ByteIO::RdU16LE(data + pos);
        pos += 2;
    } else if (len == 255u) {
        if (pos + 4 > size) return false;
        len = ByteIO::RdU32LE(data + pos);
        pos += 4;
    }
    if (len > size - pos) return false;
    pos += len;
    return true;
}

inline bool ReadHsfPoints(
    const uint8_t* data, size_t size, size_t& pos,
    uint8_t subop, int32_t ptCount, const float segBBox[6], bool hasSegBBox,
    std::vector<Vec3f>& outPts)
{
    if (ptCount <= 0 || ptCount > 5000000) return false;
    const size_t n = static_cast<size_t>(ptCount);
    outPts.resize(n);

    if ((subop & 0x01u) == 0u) {
        const uint64_t bytes = static_cast<uint64_t>(n) * 12ULL;
        if (bytes > size - pos) return false;
        for (size_t i = 0; i < n; ++i) {
            float x = ByteIO::RdF32LE(data + pos + i * 12u + 0);
            float y = ByteIO::RdF32LE(data + pos + i * 12u + 4);
            float z = ByteIO::RdF32LE(data + pos + i * 12u + 8);
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            outPts[i] = { x, y, z };
        }
        pos += static_cast<size_t>(bytes);
        return true;
    }

    // Quantized vertices (CS_TRIVIAL = 1: optional explicit 6-float bbox + bitsPerSample + packed samples)
    if (pos + 2 > size) return false;
    uint8_t scheme = data[pos++];
    float bbox[6] = { -1, -1, -1, 1, 1, 1 };
    if (hasSegBBox) {
        for (int k = 0; k < 6; ++k) bbox[k] = segBBox[k];
    }
    // In HSF TK_Shell / TK_PolyPolypoint, explicit 24B bounding box precedes bitsPerSample unlessTKSH_BOUNDING_INHERITED
    uint8_t bps = 0;
    if ((scheme == 1u || scheme == 0u) && pos + 25u <= size) {
        // Check if next byte is 8/16 directly or if 24-byte float bbox precedes bps
        uint8_t candDirectBps = data[pos];
        bool bboxLooksValid = true;
        float candBox[6];
        for (int k = 0; k < 6; ++k) {
            candBox[k] = ByteIO::RdF32LE(data + pos + static_cast<size_t>(k) * 4u);
            if (!std::isfinite(candBox[k]) || std::fabs(candBox[k]) > 1e9f) { bboxLooksValid = false; break; }
        }
        if (bboxLooksValid &&
            candBox[3] >= candBox[0] && candBox[4] >= candBox[1] && candBox[5] >= candBox[2] &&
            (data[pos + 24] == 8u || data[pos + 24] == 16u || data[pos + 24] == 24u || data[pos + 24] == 32u))
        {
            for (int k = 0; k < 6; ++k) bbox[k] = candBox[k];
            pos += 24u;
            bps = data[pos++];
        } else if (candDirectBps == 8u || candDirectBps == 16u) {
            bps = data[pos++];
        } else {
            return false;
        }
    } else if (pos < size && (data[pos] == 8u || data[pos] == 16u)) {
        bps = data[pos++];
    } else {
        return false;
    }

    const float dx = bbox[3] - bbox[0];
    const float dy = bbox[4] - bbox[1];
    const float dz = bbox[5] - bbox[2];
    if (bps == 8u) {
        uint64_t need = static_cast<uint64_t>(n) * 3ULL;
        if (need > size - pos) return false;
        const float inv = 1.0f / 255.0f;
        for (size_t i = 0; i < n; ++i) {
            outPts[i] = {
                bbox[0] + static_cast<float>(data[pos + i * 3u + 0]) * inv * dx,
                bbox[1] + static_cast<float>(data[pos + i * 3u + 1]) * inv * dy,
                bbox[2] + static_cast<float>(data[pos + i * 3u + 2]) * inv * dz
            };
        }
        pos += static_cast<size_t>(need);
        return true;
    }
    if (bps == 16u) {
        uint64_t need = static_cast<uint64_t>(n) * 6ULL;
        if (need > size - pos) return false;
        const float inv = 1.0f / 65535.0f;
        for (size_t i = 0; i < n; ++i) {
            uint16_t qx = ByteIO::RdU16LE(data + pos + i * 6u + 0);
            uint16_t qy = ByteIO::RdU16LE(data + pos + i * 6u + 2);
            uint16_t qz = ByteIO::RdU16LE(data + pos + i * 6u + 4);
            outPts[i] = {
                bbox[0] + static_cast<float>(qx) * inv * dx,
                bbox[1] + static_cast<float>(qy) * inv * dy,
                bbox[2] + static_cast<float>(qz) * inv * dz
            };
        }
        pos += static_cast<size_t>(need);
        return true;
    }
    return false;
}

inline bool SkipShellOptionals(const uint8_t* data, size_t size, size_t& pos, size_t ptCount, size_t faceCount) {
    for (int guard = 0; guard < 64 && pos < size; ++guard) {
        uint8_t opt = data[pos++];
        if (opt == 0u) return true; // OPT_TERMINATE
        // Common uncompressed vertex/face attributes:
        // 1 = OPT_ALL_NORMALS_POLAR (2B/pt) or uncompressed (12B/pt): subop byte follows
        if (opt == 1u || opt == 3u) { // normals
            if (pos >= size) return false;
            uint8_t nsub = data[pos++];
            size_t bpp = (nsub == 0u) ? 12u : 2u;
            if (ptCount * bpp > size - pos) return false;
            pos += ptCount * bpp;
        } else if (opt == 5u) { // parameters (UVs)
            if (pos + 2 > size) return false;
            uint8_t psub = data[pos++];
            uint8_t width = data[pos++];
            if (width == 0 || width > 8) width = 2;
            size_t bpp = (psub == 0u) ? (static_cast<size_t>(width) * 4u) : (static_cast<size_t>(width) * 2u);
            if (ptCount * bpp > size - pos) return false;
            pos += ptCount * bpp;
        } else if (opt == 15u) { // face normals
            if (faceCount * 12u > size - pos) return false;
            pos += faceCount * 12u;
        } else {
            return false;
        }
    }
    return false;
}

inline bool ParseW3dOpcodes(
    const uint8_t* data, size_t size, size_t startPos,
    std::vector<HsfMat4>& matStack,
    std::unordered_map<int32_t, HsfMat4>& taggedSegments,
    int32_t& nextTagId,
    LoadedMesh3D& mesh,
    int depth = 0)
{
    if (depth > 8) return false;
    size_t pos = startPos;
    float curBBox[6] = {};
    bool hasCurBBox = false;
    std::vector<std::pair<Vec3f, Vec3f>> lines;
    int32_t lastOpenedSegTag = -1;

    while (pos < size) {
        uint8_t op = data[pos++];
        if (op == '!' || op == 0x00u) break; // TKE_Termination
        if (op == '\r' || op == '\n' || op == ' ' || op == '\t') continue;

        if (op == ';') { // TKE_Comment
            while (pos < size && data[pos] != '\n' && data[pos] != '\r') ++pos;
            continue;
        }
        if (op == '(') { // TKE_Open_Segment
            if (!ReadHsfString(data, size, pos)) break;
            HsfMat4 top = matStack.empty() ? HsfMat4{} : matStack.back();
            matStack.push_back(top);
            lastOpenedSegTag = -1;
            continue;
        }
        if (op == ')') { // TKE_Close_Segment
            if (matStack.size() > 1) matStack.pop_back();
            hasCurBBox = false;
            continue;
        }
        if (op == '?') { // TKE_Tag
            int32_t tid = nextTagId++;
            if (!matStack.empty()) taggedSegments[tid] = matStack.back();
            lastOpenedSegTag = tid;
            continue;
        }
        if (op == '*') { // TKE_Reopen_Segment
            if (pos + 4 > size) break;
            int32_t segId = ByteIO::RdI32LE(data + pos);
            pos += 4;
            HsfMat4 top = matStack.empty() ? HsfMat4{} : matStack.back();
            auto it = taggedSegments.find(segId);
            matStack.push_back(it != taggedSegments.end() ? it->second : top);
            lastOpenedSegTag = segId;
            continue;
        }
        if (op == '&' || op == '/') { // TKE_Include_Segment / Style_Segment
            if (pos + 4 > size) break;
            pos += 4;
            continue;
        }
        if (op == '$') { // TKE_Start_Compression (zlib block up to TKE_Stop_Compression or stream end)
            std::vector<uint8_t> inflated;
            if (ZipDeflate::InflateZlib(data + pos, size - pos, inflated, 128 * 1024 * 1024) && !inflated.empty()) {
                ParseW3dOpcodes(inflated.data(), inflated.size(), 0, matStack, taggedSegments, nextTagId, mesh, depth + 1);
            }
            break;
        }
        if (op == '%') { // TKE_Stop_Compression
            break;
        }
        if (op == 'M') { // TKE_Modelling_Matrix (16 float32LE)
            if (pos + 64 > size) break;
            HsfMat4 local{};
            bool okM = true;
            for (int k = 0; k < 16; ++k) {
                local.m[k] = ByteIO::RdF32LE(data + pos + static_cast<size_t>(k) * 4u);
                if (!std::isfinite(local.m[k])) { okM = false; break; }
            }
            pos += 64;
            if (okM && !matStack.empty()) {
                HsfMat4 parent = (matStack.size() >= 2) ? matStack[matStack.size() - 2] : HsfMat4{};
                matStack.back() = HsfMat4::Multiply(local, parent);
                if (lastOpenedSegTag >= 0) taggedSegments[lastOpenedSegTag] = matStack.back();
            }
            continue;
        }
        if (op == 'B' || op == 'b') { // TKE_Bounding / TKE_Bounding_Info
            if (op == 'b') {
                if (pos + 24 > size) break;
                for (int k = 0; k < 6; ++k) curBBox[k] = ByteIO::RdF32LE(data + pos + static_cast<size_t>(k) * 4u);
                hasCurBBox = true;
                pos += 24;
            } else {
                if (pos >= size) break;
                uint8_t btype = data[pos++];
                if (btype == 0u && pos + 24 <= size) {
                    for (int k = 0; k < 6; ++k) curBBox[k] = ByteIO::RdF32LE(data + pos + static_cast<size_t>(k) * 4u);
                    hasCurBBox = true;
                    pos += 24;
                } else if (btype == 1u && pos + 16 <= size) {
                    float cx = ByteIO::RdF32LE(data + pos + 0);
                    float cy = ByteIO::RdF32LE(data + pos + 4);
                    float cz = ByteIO::RdF32LE(data + pos + 8);
                    float r  = std::fabs(ByteIO::RdF32LE(data + pos + 12));
                    curBBox[0] = cx - r; curBBox[1] = cy - r; curBBox[2] = cz - r;
                    curBBox[3] = cx + r; curBBox[4] = cy + r; curBBox[5] = cz + r;
                    hasCurBBox = true;
                    pos += 16;
                } else {
                    break;
                }
            }
            continue;
        }
        if (op == 'c') { // TKE_Color_RGB: uint16/uint8 channels + uint8 type + 3 float32
            if (pos + 14 <= size) {
                pos += 14;
                continue;
            }
            break;
        }
        if (op == 'L') { // TKE_Line: 2 * 3 float32
            if (pos + 24 > size) break;
            const HsfMat4& xf = matStack.empty() ? HsfMat4{} : matStack.back();
            Vec3f a = xf.TransformCadToYUp(
                ByteIO::RdF32LE(data + pos + 0), ByteIO::RdF32LE(data + pos + 4), ByteIO::RdF32LE(data + pos + 8));
            Vec3f b = xf.TransformCadToYUp(
                ByteIO::RdF32LE(data + pos + 12), ByteIO::RdF32LE(data + pos + 16), ByteIO::RdF32LE(data + pos + 20));
            pos += 24;
            lines.emplace_back(a, b);
            continue;
        }
        if (op == '+' || op == 0x09u) { // TKE_Polypoint / TKE_Point
            if (op == 0x09u) {
                if (pos + 12 > size) break;
                const HsfMat4& xf = matStack.empty() ? HsfMat4{} : matStack.back();
                Vec3f p = xf.TransformCadToYUp(
                    ByteIO::RdF32LE(data + pos + 0), ByteIO::RdF32LE(data + pos + 4), ByteIO::RdF32LE(data + pos + 8));
                pos += 12;
                MeshBuild3D::EmitOctahedron(mesh, p, 0.05f);
                continue;
            }
        }
        if (op == 'P' || op == 'p' || op == 0x14u) { // TKE_Polygon ('P'), TKE_Polyline ('p'), TKE_Polytriangle (0x14)
            if (pos + 5 > size) break;
            uint8_t subop = data[pos++];
            if (subop & 0x40u) { if (pos + 2 > size) break; pos += 2; } // expanded subop2
            int32_t cnt = ByteIO::RdI32LE(data + pos);
            pos += 4;
            std::vector<Vec3f> pts;
            if (!ReadHsfPoints(data, size, pos, subop, cnt, curBBox, hasCurBBox, pts)) break;
            const HsfMat4& xf = matStack.empty() ? HsfMat4{} : matStack.back();

            if (op == 0x14u && pts.size() >= 3) {
                uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
                for (const auto& p : pts) mesh.vertices.push_back(xf.TransformCadToYUp(p.x, p.y, p.z));
                for (uint32_t k = 0; k + 2u < static_cast<uint32_t>(pts.size()); ++k) {
                    uint32_t i0 = vb + k, i1 = vb + k + 1u, i2 = vb + k + 2u;
                    if (k & 1u) mesh.tris.push_back({ i1, i0, i2, {} });
                    else        mesh.tris.push_back({ i0, i1, i2, {} });
                }
            } else if (op == 'P' && pts.size() >= 3) {
                uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
                for (const auto& p : pts) mesh.vertices.push_back(xf.TransformCadToYUp(p.x, p.y, p.z));
                for (uint32_t k = 1; k + 1u < static_cast<uint32_t>(pts.size()); ++k) {
                    mesh.tris.push_back({ vb, vb + k, vb + k + 1u, {} });
                }
            } else if (op == 'p' && pts.size() >= 2) {
                for (size_t k = 0; k + 1 < pts.size(); ++k) {
                    lines.emplace_back(
                        xf.TransformCadToYUp(pts[k].x, pts[k].y, pts[k].z),
                        xf.TransformCadToYUp(pts[k + 1].x, pts[k + 1].y, pts[k + 1].z));
                }
            }
            continue;
        }
        if (op == 'S') { // TKE_Shell ('S' = 0x53)
            if (pos + 6 > size) break;
            uint8_t subop = data[pos++];
            if (subop & 0x40u) { if (pos + 2 > size) break; pos += 2; } // expanded subop2
            uint8_t lod = data[pos++];
            (void)lod;
            if (subop & 0x10u) { // TKSH_BOUNDING_ONLY
                if (pos + 24 > size) break;
                pos += 24;
                continue;
            }
            int32_t ptCount = ByteIO::RdI32LE(data + pos);
            pos += 4;
            std::vector<Vec3f> pts;
            if (!ReadHsfPoints(data, size, pos, subop, ptCount, curBBox, hasCurBBox, pts)) break;

            if (pos + 4 > size) break;
            int32_t flistLen = ByteIO::RdI32LE(data + pos);
            pos += 4;
            if (flistLen < 0 || flistLen > 20000000) break;

            std::vector<int32_t> flist(static_cast<size_t>(flistLen));
            if ((subop & 0x02u) == 0u) {
                uint64_t fBytes = static_cast<uint64_t>(flistLen) * 4ULL;
                if (fBytes > size - pos) break;
                for (int32_t i = 0; i < flistLen; ++i) {
                    flist[static_cast<size_t>(i)] = ByteIO::RdI32LE(data + pos + static_cast<size_t>(i) * 4u);
                }
                pos += static_cast<size_t>(fBytes);
            } else {
                // CS_TRIVIAL quantized face list: [uint8 scheme][uint8 bps][samples]
                if (pos + 2 > size) break;
                uint8_t fScheme = data[pos++];
                uint8_t fBps    = data[pos++];
                (void)fScheme;
                if (fBps == 8u) {
                    if (static_cast<size_t>(flistLen) > size - pos) break;
                    for (int32_t i = 0; i < flistLen; ++i) flist[static_cast<size_t>(i)] = data[pos++];
                } else if (fBps == 16u) {
                    if (static_cast<uint64_t>(flistLen) * 2ULL > size - pos) break;
                    for (int32_t i = 0; i < flistLen; ++i) {
                        flist[static_cast<size_t>(i)] = ByteIO::RdU16LE(data + pos + static_cast<size_t>(i) * 2u);
                    }
                    pos += static_cast<size_t>(flistLen) * 2u;
                } else if (fBps == 32u) {
                    if (static_cast<uint64_t>(flistLen) * 4ULL > size - pos) break;
                    for (int32_t i = 0; i < flistLen; ++i) {
                        flist[static_cast<size_t>(i)] = ByteIO::RdI32LE(data + pos + static_cast<size_t>(i) * 4u);
                    }
                    pos += static_cast<size_t>(flistLen) * 4u;
                } else {
                    break;
                }
            }

            const HsfMat4& xf = matStack.empty() ? HsfMat4{} : matStack.back();
            uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
            for (const auto& p : pts) mesh.vertices.push_back(xf.TransformCadToYUp(p.x, p.y, p.z));

            size_t faceCnt = 0;
            size_t fi = 0;
            while (fi < flist.size()) {
                int32_t deg = flist[fi++];
                if (deg == 0) continue;
                if (deg > 0) {
                    size_t n = static_cast<size_t>(deg);
                    if (fi + n > flist.size()) break;
                    ++faceCnt;
                    if (n >= 3) {
                        int32_t i0 = flist[fi];
                        if (i0 >= 0 && static_cast<size_t>(i0) < pts.size()) {
                            for (size_t k = 1; k + 1 < n; ++k) {
                                int32_t i1 = flist[fi + k];
                                int32_t i2 = flist[fi + k + 1];
                                if (i1 >= 0 && i2 >= 0 &&
                                    static_cast<size_t>(i1) < pts.size() && static_cast<size_t>(i2) < pts.size())
                                {
                                    mesh.tris.push_back({
                                        vb + static_cast<uint32_t>(i0),
                                        vb + static_cast<uint32_t>(i1),
                                        vb + static_cast<uint32_t>(i2),
                                        {}
                                    });
                                }
                            }
                        }
                    }
                    fi += n;
                } else {
                    // Negative degree in HOOPS flist: hole ring on preceding face (skip ring indices)
                    size_t n = static_cast<size_t>(-deg);
                    if (fi + n > flist.size()) break;
                    fi += n;
                }
            }

            if (subop & 0x04u) {
                if (!SkipShellOptionals(data, size, pos, pts.size(), faceCnt)) break;
            }
            continue;
        }

        // Unrecognized opcode without length framing: stop scanning this stream cleanly
        break;
    }

    if (mesh.tris.empty() && !lines.empty()) {
        Vec3f lo = lines[0].first, hi = lines[0].first;
        for (const auto& seg : lines) {
            for (const Vec3f& p : { seg.first, seg.second }) {
                lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
                hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
            }
        }
        float span = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-3f });
        float rad = span * 0.006f;
        for (const auto& seg : lines) {
            MeshBuild3D::EmitEdgeStrut(mesh, seg.first, seg.second, rad);
        }
    }
    return !mesh.tris.empty();
}

inline bool ParseHsfW3dStream(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 12) return false;
    size_t p = 0;
    while (p < size && (data[p] == ' ' || data[p] == '\t' || data[p] == '\r' || data[p] == '\n')) ++p;
    if (p + 8 > size) return false;
    if (std::memcmp(data + p, "(W3D V", 6) != 0 && std::memcmp(data + p, "(HSF V", 6) != 0) {
        return false;
    }
    const uint8_t* closeParen = static_cast<const uint8_t*>(std::memchr(data + p, ')', std::min<size_t>(size - p, 128u)));
    if (!closeParen) return false;
    size_t bodyStart = static_cast<size_t>((closeParen - data) + 1);

    std::vector<HsfMat4> matStack(1, HsfMat4{});
    std::unordered_map<int32_t, HsfMat4> taggedSegments;
    int32_t nextTagId = 0;
    return ParseW3dOpcodes(data, size, bodyStart, matStack, taggedSegments, nextTagId, mesh);
}

} // namespace DetailDwfW3d

inline bool ParseAutodeskDwfCad(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    if (!data || size < 14) return false;
    mesh.vertices.clear();
    mesh.tris.clear();

    const bool isDwfx = (!fullPath.empty() && fullPath.size() >= 5 &&
                         PathUtils::ToLower(fullPath.substr(fullPath.size() - 5)) == L".dwfx");

    // 1. Direct HSF / W3D stream
    if (DetailDwfW3d::ParseHsfW3dStream(data, size, mesh)) {
        mesh.formatName = L"Autodesk DWF Hoops 3D Stream (.w3d)";
        return true;
    }

    // 2. DWF v6 header "(DWF V06.xx)" followed immediately by PK\x03\x04 ZIP archive, or standard PK\x03\x04 ZIP / DWFX OPC
    const uint8_t* zipPtr = nullptr;
    size_t zipLen = 0;
    if (size >= 30 && data[0] == 'P' && data[1] == 'K' && data[2] == 0x03u && data[3] == 0x04u) {
        zipPtr = data;
        zipLen = size;
    } else if (size >= 42 && std::memcmp(data, "(DWF V", 6) == 0) {
        // DWF v6.00+ prepends a 12-byte ASCII "(DWF V06.01)" header before the PKZIP archive!
        for (size_t off = 10; off + 30 <= size && off <= 64; ++off) {
            if (data[off] == 'P' && data[off + 1] == 'K' && data[off + 2] == 0x03u && data[off + 3] == 0x04u) {
                zipPtr = data + off;
                zipLen = size - off;
                break;
            }
        }
    }
    if (!zipPtr) return false;

    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(zipPtr, zipLen, entries) || entries.empty()) return false;

    bool anyParsed = false;
    for (const auto& ze : entries) {
        if (ze.isDirectory) continue;
        std::wstring low = PathUtils::ToLower(ze.pathInArchive);
        if (low.size() < 4) continue;
        const bool isW3dPart = (low.compare(low.size() - 4, 4, L".w3d") == 0 ||
                                low.compare(low.size() - 4, 4, L".hsf") == 0);
        if (!isW3dPart) continue;

        std::vector<uint8_t> raw;
        if (!ZipDeflate::ExtractZipEntryFromMemory(zipPtr, zipLen, ze, raw, 128 * 1024 * 1024) || raw.empty()) {
            continue;
        }
        if (DetailDwfW3d::ParseHsfW3dStream(raw.data(), raw.size(), mesh)) {
            anyParsed = true;
        }
    }

    if (!anyParsed || mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = isDwfx
        ? L"Autodesk Design Web Format XPS 3D (.dwfx)"
        : L"Autodesk Design Web Format 3D (.dwf)";
    return true;
}

} // namespace Formats
