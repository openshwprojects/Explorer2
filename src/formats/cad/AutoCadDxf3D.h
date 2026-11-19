#pragma once
// AutoCadDxf3D.h — AutoCAD 3D & Binary DXF (.dxf) Mesh / Polyface / SubDMesh reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace DwgDxfDetail {

// ============================================================================
// Binary DXF Group-Pair Tokenizer ("AutoCAD Binary DXF\r\n\x1A\x00")
// ============================================================================
struct DxfPair {
    int code = 0;
    std::string strVal;
    double numVal = 0.0;
    int64_t intVal = 0;
    bool isNumeric = false;
};

inline bool IsBinaryDxfHeader(const uint8_t* data, size_t size) {
    return data && size >= 24 && std::memcmp(data, "AutoCAD Binary DXF\r\n\x1A\x00", 22) == 0;
}

inline int DxfGroupValueCategory(int gc) {
    // 0 = string, 1 = double (8B), 2 = int16 (2B), 3 = int32 (4B), 4 = int64 (8B), 5 = bool (1B), 6 = binary chunk (1B len + bytes)
    if (gc < 0) return -1;
    if (gc <= 9) return 0;
    if (gc <= 59) return 1;
    if (gc <= 79) return 2;
    if (gc <= 89) return -1;
    if (gc <= 99) return 3;
    if (gc == 100 || gc == 102 || gc == 105) return 0;
    if (gc >= 110 && gc <= 149) return 1;
    if (gc >= 160 && gc <= 169) return 4;
    if (gc >= 170 && gc <= 179) return 2;
    if (gc >= 210 && gc <= 239) return 1;
    if (gc >= 270 && gc <= 289) return 2;
    if (gc >= 290 && gc <= 299) return 5;
    if (gc >= 300 && gc <= 309) return 0;
    if (gc >= 310 && gc <= 319) return 6;
    if (gc >= 320 && gc <= 369) return 0;
    if (gc >= 370 && gc <= 389) return 2;
    if (gc >= 390 && gc <= 399) return 0;
    if (gc >= 400 && gc <= 409) return 2;
    if (gc >= 410 && gc <= 419) return 0;
    if (gc >= 420 && gc <= 429) return 3;
    if (gc >= 430 && gc <= 439) return 0;
    if (gc >= 440 && gc <= 459) return 3;
    if (gc >= 460 && gc <= 469) return 1;
    if (gc >= 470 && gc <= 481) return 0;
    if (gc == 999 || (gc >= 1000 && gc <= 1003) || (gc >= 1005 && gc <= 1009)) return 0;
    if (gc == 1004) return 6;
    if (gc >= 1010 && gc <= 1059) return 1;
    if (gc >= 1060 && gc <= 1070) return 2;
    if (gc == 1071) return 3;
    return 0;
}

inline bool TokenizeBinaryDxf(const uint8_t* data, size_t size, std::vector<DxfPair>& outPairs) {
    outPairs.clear();
    if (!IsBinaryDxfHeader(data, size)) return false;

    // R13+ Binary DXF starts with uint16LE(0) = 0x00,0x00 followed by "SECTION\0" (data[23] == 0).
    // R12 Binary DXF starts with uint8(0) = 0x00 followed by "SECTION\0" (data[23] != 0).
    bool twoByteCodes = (data[22] == 0u && data[23] == 0u);
    size_t pos = 22u;

    while (pos < size && outPairs.size() < 2000000u) {
        int gc = 0;
        if (twoByteCodes) {
            if (pos + 2u > size) break;
            gc = static_cast<int>(RdI16LE(data + pos));
            pos += 2u;
        } else {
            uint8_t b = data[pos++];
            if (b == 0xFFu) {
                if (pos + 2u > size) return false;
                gc = static_cast<int>(RdI16LE(data + pos));
                pos += 2u;
            } else {
                gc = static_cast<int>(b);
            }
        }

        int cat = DxfGroupValueCategory(gc);
        if (cat < 0) return false;

        DxfPair dp;
        dp.code = gc;
        if (cat == 0) { // Null-terminated string
            size_t endS = pos;
            while (endS < size && data[endS] != 0u) ++endS;
            if (endS >= size) return false;
            dp.strVal.assign(reinterpret_cast<const char*>(data + pos), endS - pos);
            pos = endS + 1u;
            outPairs.push_back(std::move(dp));
            if (gc == 0 && dp.strVal == "EOF") break;
        } else if (cat == 1) { // Double
            if (pos + 8u > size) return false;
            dp.numVal = RdF64LE(data + pos);
            dp.isNumeric = true;
            pos += 8u;
            outPairs.push_back(std::move(dp));
        } else if (cat == 2) { // Int16
            if (pos + 2u > size) return false;
            int16_t iv = RdI16LE(data + pos);
            dp.intVal = iv;
            dp.numVal = static_cast<double>(iv);
            dp.isNumeric = true;
            pos += 2u;
            outPairs.push_back(std::move(dp));
        } else if (cat == 3) { // Int32
            if (pos + 4u > size) return false;
            int32_t iv = RdI32LE(data + pos);
            dp.intVal = iv;
            dp.numVal = static_cast<double>(iv);
            dp.isNumeric = true;
            pos += 4u;
            outPairs.push_back(std::move(dp));
        } else if (cat == 4) { // Int64
            if (pos + 8u > size) return false;
            int64_t iv = static_cast<int64_t>(RdU64LE(data + pos));
            dp.intVal = iv;
            dp.numVal = static_cast<double>(iv);
            dp.isNumeric = true;
            pos += 8u;
            outPairs.push_back(std::move(dp));
        } else if (cat == 5) { // Bool (1B)
            if (pos + 1u > size) return false;
            dp.intVal = data[pos++];
            dp.numVal = static_cast<double>(dp.intVal);
            dp.isNumeric = true;
            outPairs.push_back(std::move(dp));
        } else if (cat == 6) { // Binary chunk
            if (pos + 1u > size) return false;
            uint8_t clen = data[pos++];
            if (pos + clen > size) return false;
            pos += clen;
        }
    }
    return !outPairs.empty();
}

inline bool TokenizeAsciiDxf(std::string_view sv, std::vector<DxfPair>& outPairs) {
    outPairs.clear();
    size_t pos = 0;
    auto nextLine = [&](std::string_view& outLn) -> bool {
        if (pos >= sv.size()) return false;
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1u;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        outLn = ln;
        return true;
    };

    std::string_view codeLn, valLn;
    while (outPairs.size() < 2000000u && nextLine(codeLn)) {
        if (codeLn.empty() && outPairs.empty()) continue;
        int gc = -1;
        auto rc = std::from_chars(codeLn.data(), codeLn.data() + codeLn.size(), gc);
        if (rc.ec != std::errc{} || rc.ptr != codeLn.data() + codeLn.size()) return false;
        if (!nextLine(valLn)) break;

        DxfPair dp;
        dp.code = gc;
        int cat = DxfGroupValueCategory(gc);
        if (cat == 0 || cat == 6) {
            dp.strVal.assign(valLn);
            if (gc == 0 && dp.strVal == "EOF") {
                outPairs.push_back(std::move(dp));
                break;
            }
        } else {
            dp.isNumeric = true;
            double dv = 0.0;
            auto rf = std::from_chars(valLn.data(), valLn.data() + valLn.size(), dv);
            if (rf.ec == std::errc{}) {
                dp.numVal = dv;
                dp.intVal = static_cast<int64_t>(std::llround(dv));
            }
        }
        outPairs.push_back(std::move(dp));
    }
    return !outPairs.empty();
}

} // namespace DwgDxfDetail

// ============================================================================
// 2. Quick Detector: Does a .dxf file contain 3D Mesh / Polyface / SubDMesh / Binary 3D entities?
// ============================================================================
inline bool IsLikely3DDxfBuffer(const uint8_t* data, size_t size) {
    if (!data || size < 16) return false;
    if (DwgDxfDetail::IsBinaryDxfHeader(data, size)) {
        std::vector<DwgDxfDetail::DxfPair> pairs;
        if (!DwgDxfDetail::TokenizeBinaryDxf(data, std::min<size_t>(size, 256 * 1024), pairs)) return false;
        for (const auto& p : pairs) {
            if (p.code == 0 && (p.strVal == "3DFACE" || p.strVal == "MESH" || p.strVal == "3DSOLID")) return true;
            if (p.code == 100 && (p.strVal == "AcDbPolyFaceMesh" || p.strVal == "AcDbPolygonMesh" || p.strVal == "AcDbSubDMesh")) return true;
            if ((p.code == 30 || p.code == 31 || p.code == 32 || p.code == 39) && std::fabs(p.numVal) > 1e-4) return true;
        }
        return false;
    }
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 128 * 1024));
    return sv.find("3DFACE") != std::string_view::npos ||
           sv.find("AcDbSubDMesh") != std::string_view::npos ||
           sv.find("AcDbPolyFaceMesh") != std::string_view::npos ||
           sv.find("AcDbPolygonMesh") != std::string_view::npos;
}

// ============================================================================
// 3. AutoCAD 3D & Binary DXF Mesh Parser (.dxf -> LoadedMesh3D)
// ============================================================================
inline bool ParseAutoCadDxf3DMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace DwgDxfDetail;
    if (!data || size < 16) return false;

    std::vector<DxfPair> pairs;
    bool isBinary = IsBinaryDxfHeader(data, size);
    if (isBinary) {
        if (!TokenizeBinaryDxf(data, size, pairs)) return false;
    } else {
        std::string_view sv(reinterpret_cast<const char*>(data), size);
        if (!TokenizeAsciiDxf(sv, pairs)) return false;
    }

    struct RawMeshBlock {
        std::vector<std::array<double, 3>> verts;
        std::vector<std::array<uint32_t, 3>> tris;
    };

    std::unordered_map<std::string, RawMeshBlock> blockDefs;
    RawMeshBlock modelSpace;
    RawMeshBlock* activeTarget = &modelSpace;

    std::string curSection;
    std::string curBlockName;
    bool inBlockHeader = false;

    size_t i = 0;
    while (i < pairs.size()) {
        if (pairs[i].code == 0 && pairs[i].strVal == "SECTION") {
            ++i;
            while (i < pairs.size() && pairs[i].code != 2) ++i;
            if (i < pairs.size()) { curSection = pairs[i].strVal; ++i; }
            activeTarget = &modelSpace;
            continue;
        }
        if (pairs[i].code == 0 && pairs[i].strVal == "ENDSEC") {
            curSection.clear();
            activeTarget = &modelSpace;
            ++i;
            continue;
        }

        if (curSection == "BLOCKS") {
            if (pairs[i].code == 0 && pairs[i].strVal == "BLOCK") {
                curBlockName.clear();
                inBlockHeader = true;
                ++i;
                while (i < pairs.size() && pairs[i].code != 0) {
                    if (pairs[i].code == 2) curBlockName = pairs[i].strVal;
                    ++i;
                }
                inBlockHeader = false;
                activeTarget = curBlockName.empty() ? &modelSpace : &blockDefs[curBlockName];
                continue;
            }
            if (pairs[i].code == 0 && pairs[i].strVal == "ENDBLK") {
                activeTarget = &modelSpace;
                ++i;
                while (i < pairs.size() && pairs[i].code != 0) ++i;
                continue;
            }
        } else if (curSection != "ENTITIES" && !curSection.empty()) {
            ++i;
            continue;
        }
        (void)inBlockHeader;

        if (pairs[i].code != 0) { ++i; continue; }
        std::string ent = pairs[i].strVal;
        ++i;

        // A. 3DFACE / SOLID
        if (ent == "3DFACE" || ent == "SOLID") {
            double x[4] = {}, y[4] = {}, z[4] = {};
            bool has3 = false, has4 = false;
            while (i < pairs.size() && pairs[i].code != 0) {
                int c = pairs[i].code;
                double v = pairs[i].numVal;
                if (c >= 10 && c <= 13) { x[c - 10] = v; if (c == 12) has3 = true; if (c == 13) has4 = true; }
                else if (c >= 20 && c <= 23) { y[c - 20] = v; }
                else if (c >= 30 && c <= 33) { z[c - 30] = v; }
                ++i;
            }
            if (has3) {
                uint32_t b = static_cast<uint32_t>(activeTarget->verts.size());
                activeTarget->verts.push_back({ x[0], y[0], z[0] });
                activeTarget->verts.push_back({ x[1], y[1], z[1] });
                activeTarget->verts.push_back({ x[2], y[2], z[2] });
                activeTarget->tris.push_back({ b + 0, b + 1, b + 2 });
                if (has4 && (std::fabs(x[2] - x[3]) > 1e-6 || std::fabs(y[2] - y[3]) > 1e-6 || std::fabs(z[2] - z[3]) > 1e-6)) {
                    activeTarget->verts.push_back({ x[3], y[3], z[3] });
                    if (ent == "SOLID") {
                        // DXF SOLID swaps corners 3 and 4 (bowtie convention: 0-1-3-2)
                        activeTarget->tris.back() = { b + 0, b + 1, b + 3 };
                        activeTarget->tris.push_back({ b + 0, b + 3, b + 2 });
                    } else {
                        activeTarget->tris.push_back({ b + 0, b + 2, b + 3 });
                    }
                }
            }
        }
        // B. POLYLINE (Polyface Mesh flag & 64, Polygon MxN Mesh flag & 16, or 3D Polyline)
        else if (ent == "POLYLINE") {
            int polyFlags = 0, meshM = 0, meshN = 0;
            while (i < pairs.size() && pairs[i].code != 0) {
                if (pairs[i].code == 70) polyFlags = static_cast<int>(pairs[i].intVal);
                else if (pairs[i].code == 71) meshM = static_cast<int>(pairs[i].intVal);
                else if (pairs[i].code == 72) meshN = static_cast<int>(pairs[i].intVal);
                ++i;
            }

            std::vector<std::array<double, 3>> pfVerts;
            std::vector<std::array<int, 4>> pfFaces;
            while (i < pairs.size() && pairs[i].code == 0 && pairs[i].strVal == "VERTEX") {
                ++i;
                double vx = 0, vy = 0, vz = 0;
                int vFlags = 0;
                int fi[4] = { 0, 0, 0, 0 };
                while (i < pairs.size() && pairs[i].code != 0) {
                    int c = pairs[i].code;
                    if (c == 10) vx = pairs[i].numVal;
                    else if (c == 20) vy = pairs[i].numVal;
                    else if (c == 30) vz = pairs[i].numVal;
                    else if (c == 70) vFlags = static_cast<int>(pairs[i].intVal);
                    else if (c >= 71 && c <= 74) fi[c - 71] = static_cast<int>(pairs[i].intVal);
                    ++i;
                }
                if ((polyFlags & 64) != 0) {
                    if ((vFlags & 128) != 0 && (vFlags & 64) == 0 && (fi[0] != 0 || fi[1] != 0 || fi[2] != 0)) {
                        pfFaces.push_back({ std::abs(fi[0]) - 1, std::abs(fi[1]) - 1, std::abs(fi[2]) - 1, std::abs(fi[3]) - 1 });
                    } else {
                        pfVerts.push_back({ vx, vy, vz });
                    }
                } else {
                    pfVerts.push_back({ vx, vy, vz });
                }
            }
            if (i < pairs.size() && pairs[i].code == 0 && pairs[i].strVal == "SEQEND") {
                ++i;
                while (i < pairs.size() && pairs[i].code != 0) ++i;
            }

            if ((polyFlags & 64) != 0 && !pfVerts.empty() && !pfFaces.empty()) {
                uint32_t b = static_cast<uint32_t>(activeTarget->verts.size());
                activeTarget->verts.insert(activeTarget->verts.end(), pfVerts.begin(), pfVerts.end());
                for (const auto& fc : pfFaces) {
                    if (fc[0] >= 0 && fc[1] >= 0 && fc[2] >= 0 &&
                        static_cast<size_t>(fc[0]) < pfVerts.size() &&
                        static_cast<size_t>(fc[1]) < pfVerts.size() &&
                        static_cast<size_t>(fc[2]) < pfVerts.size())
                    {
                        activeTarget->tris.push_back({
                            b + static_cast<uint32_t>(fc[0]),
                            b + static_cast<uint32_t>(fc[1]),
                            b + static_cast<uint32_t>(fc[2])
                        });
                        if (fc[3] >= 0 && fc[3] != fc[2] && static_cast<size_t>(fc[3]) < pfVerts.size()) {
                            activeTarget->tris.push_back({
                                b + static_cast<uint32_t>(fc[0]),
                                b + static_cast<uint32_t>(fc[2]),
                                b + static_cast<uint32_t>(fc[3])
                            });
                        }
                    }
                }
            } else if ((polyFlags & 16) != 0 && meshM >= 2 && meshN >= 2 &&
                       pfVerts.size() >= static_cast<size_t>(meshM) * static_cast<size_t>(meshN))
            {
                uint32_t b = static_cast<uint32_t>(activeTarget->verts.size());
                activeTarget->verts.insert(activeTarget->verts.end(), pfVerts.begin(), pfVerts.end());
                for (int r = 0; r + 1 < meshM; ++r) {
                    for (int c = 0; c + 1 < meshN; ++c) {
                        uint32_t i00 = b + static_cast<uint32_t>(r * meshN + c);
                        uint32_t i10 = b + static_cast<uint32_t>((r + 1) * meshN + c);
                        uint32_t i11 = b + static_cast<uint32_t>((r + 1) * meshN + (c + 1));
                        uint32_t i01 = b + static_cast<uint32_t>(r * meshN + (c + 1));
                        activeTarget->tris.push_back({ i00, i10, i11 });
                        activeTarget->tris.push_back({ i00, i11, i01 });
                    }
                }
            }
        }
        // C. MESH (AcDbSubDMesh — AutoCAD 2010+ Subdivision / Polygon Mesh)
        else if (ent == "MESH") {
            bool inSubDMesh = false;
            int expectedVerts = -1, expectedFaceInts = -1;
            std::vector<std::array<double, 3>> mVerts;
            std::vector<int32_t> faceStream;
            double curX = 0, curY = 0;
            bool hasXY = false;

            while (i < pairs.size() && pairs[i].code != 0) {
                int c = pairs[i].code;
                if (c == 100 && pairs[i].strVal == "AcDbSubDMesh") {
                    inSubDMesh = true;
                } else if (inSubDMesh) {
                    if (c == 92 && expectedVerts < 0) {
                        expectedVerts = static_cast<int>(pairs[i].intVal);
                    } else if (c == 10 && static_cast<int>(mVerts.size()) < expectedVerts) {
                        curX = pairs[i].numVal; hasXY = true;
                    } else if (c == 20 && hasXY) {
                        curY = pairs[i].numVal;
                    } else if (c == 30 && hasXY) {
                        mVerts.push_back({ curX, curY, pairs[i].numVal });
                        hasXY = false;
                    } else if (c == 93 && expectedFaceInts < 0) {
                        expectedFaceInts = static_cast<int>(pairs[i].intVal);
                    } else if (c == 90 && expectedFaceInts > 0 && static_cast<int>(faceStream.size()) < expectedFaceInts) {
                        faceStream.push_back(static_cast<int32_t>(pairs[i].intVal));
                    }
                }
                ++i;
            }
            if (mVerts.size() >= 3 && !faceStream.empty()) {
                uint32_t b = static_cast<uint32_t>(activeTarget->verts.size());
                activeTarget->verts.insert(activeTarget->verts.end(), mVerts.begin(), mVerts.end());
                size_t fp = 0;
                while (fp < faceStream.size()) {
                    int32_t deg = faceStream[fp++];
                    if (deg < 3 || deg > 256 || fp + static_cast<size_t>(deg) > faceStream.size()) break;
                    int32_t i0 = faceStream[fp];
                    for (int32_t t = 1; t + 1 < deg; ++t) {
                        int32_t i1 = faceStream[fp + static_cast<size_t>(t)];
                        int32_t i2 = faceStream[fp + static_cast<size_t>(t + 1)];
                        if (i0 >= 0 && i1 >= 0 && i2 >= 0 &&
                            static_cast<size_t>(i0) < mVerts.size() &&
                            static_cast<size_t>(i1) < mVerts.size() &&
                            static_cast<size_t>(i2) < mVerts.size())
                        {
                            activeTarget->tris.push_back({
                                b + static_cast<uint32_t>(i0),
                                b + static_cast<uint32_t>(i1),
                                b + static_cast<uint32_t>(i2)
                            });
                        }
                    }
                    fp += static_cast<size_t>(deg);
                }
            }
        }
        // D. INSERT (instantiate named BLOCK with translation, scale, and Z rotation)
        else if (ent == "INSERT") {
            std::string bName;
            double ix = 0, iy = 0, iz = 0, sx = 1.0, sy = 1.0, sz = 1.0, rotDeg = 0.0;
            while (i < pairs.size() && pairs[i].code != 0) {
                int c = pairs[i].code;
                if (c == 2) bName = pairs[i].strVal;
                else if (c == 10) ix = pairs[i].numVal;
                else if (c == 20) iy = pairs[i].numVal;
                else if (c == 30) iz = pairs[i].numVal;
                else if (c == 41) sx = pairs[i].numVal;
                else if (c == 42) sy = pairs[i].numVal;
                else if (c == 43) sz = pairs[i].numVal;
                else if (c == 50) rotDeg = pairs[i].numVal;
                ++i;
            }
            auto itB = blockDefs.find(bName);
            if (itB != blockDefs.end() && !itB->second.verts.empty() && activeTarget != &itB->second) {
                double rad = rotDeg * 3.14159265358979323846 / 180.0;
                double cs = std::cos(rad), sn = std::sin(rad);
                uint32_t base = static_cast<uint32_t>(activeTarget->verts.size());
                for (const auto& v : itB->second.verts) {
                    double vx = v[0] * sx, vy = v[1] * sy, vz = v[2] * sz;
                    activeTarget->verts.push_back({ vx * cs - vy * sn + ix, vx * sn + vy * cs + iy, vz + iz });
                }
                for (const auto& t : itB->second.tris) {
                    activeTarget->tris.push_back({ base + t[0], base + t[1], base + t[2] });
                }
            }
        }
        // E. 3D LINE fallback strut
        else if (ent == "LINE") {
            double x1 = 0, y1 = 0, z1 = 0, x2 = 0, y2 = 0, z2 = 0;
            while (i < pairs.size() && pairs[i].code != 0) {
                int c = pairs[i].code;
                if (c == 10) x1 = pairs[i].numVal; else if (c == 20) y1 = pairs[i].numVal; else if (c == 30) z1 = pairs[i].numVal;
                else if (c == 11) x2 = pairs[i].numVal; else if (c == 21) y2 = pairs[i].numVal; else if (c == 31) z2 = pairs[i].numVal;
                ++i;
            }
            if (std::hypot(x2 - x1, y2 - y1) + std::fabs(z2 - z1) > 1e-5) {
                LoadedMesh3D tmp;
                Detail3D100::AddLineStrut(tmp, { static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(z1) },
                                               { static_cast<float>(x2), static_cast<float>(y2), static_cast<float>(z2) }, 0.04f);
                uint32_t b = static_cast<uint32_t>(activeTarget->verts.size());
                for (const auto& v : tmp.vertices) activeTarget->verts.push_back({ v.x, v.y, v.z });
                for (const auto& t : tmp.tris) activeTarget->tris.push_back({ b + t.i0, b + t.i1, b + t.i2 });
            }
        } else {
            while (i < pairs.size() && pairs[i].code != 0) ++i;
        }
    }

    if (modelSpace.verts.empty() || modelSpace.tris.empty()) return false;
    mesh.vertices.clear();
    mesh.tris.clear();
    mesh.vertices.reserve(modelSpace.verts.size());
    for (const auto& v : modelSpace.verts) {
        if (!ValidCadPt(v[0], v[1], v[2])) return false;
        mesh.vertices.push_back(CadZUpToYUp(v[0], v[1], v[2]));
    }
    mesh.tris.reserve(modelSpace.tris.size());
    for (const auto& t : modelSpace.tris) {
        if (t[0] < mesh.vertices.size() && t[1] < mesh.vertices.size() && t[2] < mesh.vertices.size()) {
            mesh.tris.push_back({ t[0], t[1], t[2], {} });
        }
    }
    mesh.formatName = isBinary
        ? L"AutoCAD Binary 3D Drawing Exchange (.dxf)"
        : L"AutoCAD 3D Drawing Exchange (.dxf)";
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
