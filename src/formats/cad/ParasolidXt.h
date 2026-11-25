#pragma once
// ParasolidXt.h — Siemens Parasolid XT Text (.x_t) & Neutral Binary (.x_b) B-Rep 3D CAD reader.
#include "../common/MeshFormatCommon.h"
#include "Catia3dxml.h"
#include "SiemensJt.h"
#include "OpenCascadeBrep.h"
#include "SpatialAcisSat.h"

namespace Formats {

inline bool ParseParasolidXt(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    if (ext != L".x_t" && ext != L".x_b") return false;
    using namespace DetailAcisParasolid;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    auto evaluateBRepGraph = [&](const BRepGraph& g) -> bool {
        return DetailAcisParasolid::EvaluateBRepGraph(g, mesh);
    };
        // 3. Siemens Parasolid XT Text (.x_t) & Neutral Binary (.x_b)
        {
            // Check Neutral Binary Parasolid (.x_b): "PS\0" prefix (arch byte at data[3]: 0=Big-Endian IEEE, 1=Little-Endian IEEE)
            // + ASCII header containing "**END_OF_HEADER**" + binary node stream!
            bool hasPsBinPrefix = (size >= 32 && data[0] == 'P' && data[1] == 'S' && data[2] == 0 && (data[3] == 0 || data[3] == 1));
            size_t eohBin = sv.find("**END_OF_HEADER**");
            bool isNeutralBinaryXb = false;
            size_t xbStart = 0;
            bool xbBigEndian = true;

            if (eohBin != std::string_view::npos &&
                (hasPsBinPrefix || sv.find("PARASOLID") != std::string_view::npos ||
                 sv.find("SCH_") != std::string_view::npos || sv.find("**PART") != std::string_view::npos))
            {
                if (hasPsBinPrefix) xbBigEndian = (data[3] == 0);
                size_t afterEoh = eohBin + 17;
                while (afterEoh < size && data[afterEoh] == '*') ++afterEoh;
                while (afterEoh < size && (data[afterEoh] == '\r' || data[afterEoh] == '\n' || data[afterEoh] == ' ')) ++afterEoh;
                bool bodyHasBinaryNul = false;
                for (size_t k = afterEoh; k < std::min<size_t>(size, afterEoh + 64); ++k) {
                    if (data[k] == 0) { bodyHasBinaryNul = true; break; }
                }
                if (hasPsBinPrefix || bodyHasBinaryNul ||
                    (afterEoh + 4 <= size && (data[afterEoh] == 'B' || data[afterEoh] == 'L') && data[afterEoh + 1] == 0))
                {
                    isNeutralBinaryXb = true;
                    if (afterEoh + 2 <= size && (data[afterEoh] == 'B' || data[afterEoh] == 'L') && data[afterEoh + 1] == 0) {
                        xbBigEndian = (data[afterEoh] == 'B');
                        xbStart = afterEoh + 2;
                    } else {
                        if (!hasPsBinPrefix && afterEoh + 2 <= size) {
                            // Infer endianness from first 16-bit nodeType in 1..60
                            uint16_t beNt = DetailMeshSciCad::RdU16BE(data + afterEoh);
                            uint16_t leNt = DetailMeshSciCad::RdU16LE(data + afterEoh);
                            if (leNt >= 1 && leNt <= 60 && (beNt == 0 || beNt > 255)) xbBigEndian = false;
                        }
                        xbStart = afterEoh;
                    }
                }
            }

            if (isNeutralBinaryXb) {
                auto rdU16 = [&](const uint8_t* p) -> uint16_t { return xbBigEndian ? DetailMeshSciCad::RdU16BE(p) : DetailMeshSciCad::RdU16LE(p); };
                auto rdI32 = [&](const uint8_t* p) -> int32_t  { return static_cast<int32_t>(xbBigEndian ? DetailMeshSciCad::RdU32BE(p) : DetailMeshSciCad::RdU32LE(p)); };
                auto rdF64 = [&](const uint8_t* p) -> double {
                    if (!xbBigEndian) return DetailMeshSciCad::RdF64LE(p);
                    uint64_t u = DetailMeshSciCad::RdU64BE(p);
                    double d = 0.0;
                    std::memcpy(&d, &u, 8);
                    return d;
                };

                BRepGraph g;
                std::unordered_map<int32_t, CadXform> xbTransforms;
                std::unordered_map<int32_t, int32_t> bodyXfMap, shellFirstFace;
                size_t bp = xbStart;

                // Neutral Binary Parasolid Node Stream:
                // Each node: [uint16 nodeType][int32 nodeId] followed by schema fields (nodeType == 0 or 999 -> EOF)
                while (bp + 6 <= size && (g.faces.size() + g.points.size() + g.surfaces.size()) < 200000) {
                    uint16_t nt = rdU16(data + bp);
                    int32_t nid = rdI32(data + bp + 2);
                    bp += 6;
                    if (nt == 0 || nt == 999) break;
                    if (nid < 0) return false;

                    if (nt == 12) { // body: [int32 xfId][int32 shellId]
                        if (bp + 8 > size) return false;
                        int32_t xfId = rdI32(data + bp);
                        int32_t shId = rdI32(data + bp + 4);
                        bp += 8;
                        if (xfId >= 0) bodyXfMap[nid] = xfId;
                        (void)shId;
                    } else if (nt == 13) { // transform: double r[9], t[3], scale (104B)
                        if (bp + 104 > size) return false;
                        CadXform xf;
                        for (int i = 0; i < 9; ++i) xf.r[i] = rdF64(data + bp + i * 8);
                        xf.t[0] = rdF64(data + bp + 72);
                        xf.t[1] = rdF64(data + bp + 80);
                        xf.t[2] = rdF64(data + bp + 88);
                        xf.scale = rdF64(data + bp + 96);
                        if (!std::isfinite(xf.scale) || std::fabs(xf.scale) < 1e-12) xf.scale = 1.0;
                        xf.valid = true;
                        xbTransforms[nid] = xf;
                        bp += 104;
                    } else if (nt == 14) { // shell: [int32 firstFaceId]
                        if (bp + 4 > size) return false;
                        shellFirstFace[nid] = rdI32(data + bp);
                        bp += 4;
                    } else if (nt == 15) { // face: [int32 nextFace][int32 firstLoop][int32 surfId][int32 bodyId][uint8 rev]
                        if (bp + 17 > size) return false;
                        BRepFace fc;
                        fc.nextFace  = rdI32(data + bp + 0);
                        fc.firstLoop = rdI32(data + bp + 4);
                        fc.surfaceId = rdI32(data + bp + 8);
                        fc.bodyId    = rdI32(data + bp + 12);
                        fc.reversed  = (data[bp + 16] != 0);
                        bp += 17;
                        g.faces[nid] = fc;
                        g.faceOrder.push_back(nid);
                    } else if (nt == 16) { // loop: [int32 ownerFace][int32 nextLoop][int32 firstFin]
                        if (bp + 12 > size) return false;
                        BRepLoop lp;
                        lp.ownerFace   = rdI32(data + bp + 0);
                        lp.nextLoop    = rdI32(data + bp + 4);
                        lp.firstCoedge = rdI32(data + bp + 8);
                        bp += 12;
                        g.loops[nid] = lp;
                    } else if (nt == 17) { // fin: [int32 nextFin][int32 edgeId][int32 headVert][uint8 rev]
                        if (bp + 13 > size) return false;
                        BRepCoedge ce;
                        ce.nextCoedge = rdI32(data + bp + 0);
                        ce.edgeId     = rdI32(data + bp + 4);
                        ce.headVert   = rdI32(data + bp + 8);
                        ce.reversed   = (data[bp + 12] != 0);
                        bp += 13;
                        g.coedges[nid] = ce;
                    } else if (nt == 18) { // vertex: [int32 pointId]
                        if (bp + 4 > size) return false;
                        g.vertexToPoint[nid] = rdI32(data + bp);
                        bp += 4;
                    } else if (nt == 19) { // edge: [int32 startVert][int32 endVert][int32 curveId][uint8 rev]
                        if (bp + 13 > size) return false;
                        BRepEdge ed;
                        ed.startVert = rdI32(data + bp + 0);
                        ed.endVert   = rdI32(data + bp + 4);
                        ed.curveId   = rdI32(data + bp + 8);
                        ed.reversed  = (data[bp + 12] != 0);
                        bp += 13;
                        g.edges[nid] = ed;
                        g.edgeOrder.push_back(nid);
                    } else if (nt == 29) { // point: double3 (24B)
                        if (bp + 24 > size) return false;
                        g.points[nid] = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        bp += 24;
                    } else if (nt == 30) { // line: origin(24B) + dir(24B)
                        if (bp + 48 > size) return false;
                        BRepCurve c; c.kind = CurveKind::Straight;
                        c.origin = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        c.normal = { rdF64(data + bp + 24), rdF64(data + bp + 32), rdF64(data + bp + 40) };
                        bp += 48;
                        g.curves[nid] = std::move(c);
                    } else if (nt == 31) { // circle: origin(24B) + normal(24B) + uAxis(24B) + rad(8B)
                        if (bp + 80 > size) return false;
                        BRepCurve c; c.kind = CurveKind::Ellipse; c.ratio = 1.0;
                        c.origin = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        c.normal = { rdF64(data + bp + 24), rdF64(data + bp + 32), rdF64(data + bp + 40) };
                        CadVec3 ux = vNorm({ rdF64(data + bp + 48), rdF64(data + bp + 56), rdF64(data + bp + 64) });
                        c.majorAxis = vMul(ux, std::fabs(rdF64(data + bp + 72)));
                        bp += 80;
                        g.curves[nid] = std::move(c);
                    } else if (nt == 32) { // ellipse: origin(24B) + normal(24B) + majorAxis(24B) + ratio(8B)
                        if (bp + 80 > size) return false;
                        BRepCurve c; c.kind = CurveKind::Ellipse;
                        c.origin    = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        c.normal    = { rdF64(data + bp + 24), rdF64(data + bp + 32), rdF64(data + bp + 40) };
                        c.majorAxis = { rdF64(data + bp + 48), rdF64(data + bp + 56), rdF64(data + bp + 64) };
                        c.ratio     = rdF64(data + bp + 72);
                        bp += 80;
                        g.curves[nid] = std::move(c);
                    } else if (nt == 35) { // plane: origin(24B) + normal(24B) + uAxis(24B)
                        if (bp + 72 > size) return false;
                        BRepSurf s; s.kind = SurfKind::Plane;
                        s.origin = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        s.normal = { rdF64(data + bp + 24), rdF64(data + bp + 32), rdF64(data + bp + 40) };
                        s.uAxis  = { rdF64(data + bp + 48), rdF64(data + bp + 56), rdF64(data + bp + 64) };
                        bp += 72;
                        g.surfaces[nid] = s;
                    } else if (nt == 36) { // cylinder: origin(24B) + axis(24B) + radius(8B)
                        if (bp + 56 > size) return false;
                        BRepSurf s; s.kind = SurfKind::CylinderCone;
                        s.origin  = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        s.normal  = { rdF64(data + bp + 24), rdF64(data + bp + 32), rdF64(data + bp + 40) };
                        s.radius1 = std::fabs(rdF64(data + bp + 48));
                        bp += 56;
                        g.surfaces[nid] = s;
                    } else if (nt == 37) { // sphere: origin(24B) + radius(8B)
                        if (bp + 32 > size) return false;
                        BRepSurf s; s.kind = SurfKind::Sphere;
                        s.origin  = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        s.radius1 = std::fabs(rdF64(data + bp + 24));
                        bp += 32;
                        g.surfaces[nid] = s;
                    } else if (nt == 38) { // cone: origin(24B) + axis(24B) + radius(8B) + semiAngleSinOrRatio(8B)
                        if (bp + 64 > size) return false;
                        BRepSurf s; s.kind = SurfKind::CylinderCone;
                        s.origin  = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        s.normal  = { rdF64(data + bp + 24), rdF64(data + bp + 32), rdF64(data + bp + 40) };
                        s.radius1 = std::fabs(rdF64(data + bp + 48));
                        s.ratio   = rdF64(data + bp + 56);
                        bp += 64;
                        g.surfaces[nid] = s;
                    } else if (nt == 39) { // torus: origin(24B) + axis(24B) + majorRad(8B) + minorRad(8B)
                        if (bp + 64 > size) return false;
                        BRepSurf s; s.kind = SurfKind::Torus;
                        s.origin  = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        s.normal  = { rdF64(data + bp + 24), rdF64(data + bp + 32), rdF64(data + bp + 40) };
                        s.radius1 = std::fabs(rdF64(data + bp + 48));
                        s.radius2 = std::fabs(rdF64(data + bp + 56));
                        bp += 64;
                        g.surfaces[nid] = s;
                    } else if (nt == 41) { // b_surface (NURBS): uDeg(u16), vDeg(u16), nU(i32), nV(i32), isRat(u8), knotsU, knotsV, poles
                        if (bp + 13 > size) return false;
                        int uDeg = rdU16(data + bp + 0);
                        int vDeg = rdU16(data + bp + 2);
                        int nU   = rdI32(data + bp + 4);
                        int nV   = rdI32(data + bp + 8);
                        bool isRat = (data[bp + 12] != 0);
                        bp += 13;
                        if (uDeg < 1 || uDeg > 15 || vDeg < 1 || vDeg > 15 ||
                            nU <= uDeg || nU > 256 || nV <= vDeg || nV > 256)
                        {
                            return false;
                        }
                        size_t kuCnt = static_cast<size_t>(nU + uDeg + 1);
                        size_t kvCnt = static_cast<size_t>(nV + vDeg + 1);
                        size_t pCnt  = static_cast<size_t>(nU) * static_cast<size_t>(nV);
                        size_t stride = isRat ? 4u : 3u;
                        size_t need = (kuCnt + kvCnt + pCnt * stride) * 8u;
                        if (bp + need > size) return false;

                        BRepSurf s;
                        s.kind = SurfKind::Nurbs;
                        s.uDeg = uDeg; s.vDeg = vDeg; s.nU = nU; s.nV = nV; s.isRational = isRat;
                        s.knotsU.resize(kuCnt);
                        for (size_t k = 0; k < kuCnt; ++k) { s.knotsU[k] = rdF64(data + bp); bp += 8; }
                        s.knotsV.resize(kvCnt);
                        for (size_t k = 0; k < kvCnt; ++k) { s.knotsV[k] = rdF64(data + bp); bp += 8; }
                        s.poles.resize(pCnt);
                        if (isRat) s.weights.resize(pCnt, 1.0);
                        for (size_t k = 0; k < pCnt; ++k) {
                            s.poles[k] = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                            if (isRat) s.weights[k] = rdF64(data + bp + 24);
                            bp += stride * 8u;
                        }
                        g.surfaces[nid] = std::move(s);
                    } else {
                        return false; // unknown binary node type -> fail cleanly without guessing
                    }
                }

                for (const auto& kv : bodyXfMap) {
                    if (xbTransforms.count(kv.second)) g.bodyTransforms[kv.first] = xbTransforms[kv.second];
                }
                for (auto& kv : g.faces) {
                    int32_t curL = kv.second.firstLoop;
                    std::set<int32_t> seen;
                    while (curL >= 0 && seen.insert(curL).second && seen.size() <= 512) {
                        kv.second.loopIds.push_back(curL);
                        auto lit = g.loops.find(curL);
                        if (lit == g.loops.end()) break;
                        curL = lit->second.nextLoop;
                    }
                }
                if (!evaluateBRepGraph(g)) return false;
                mesh.formatName = L"Siemens Parasolid Binary Solid (.x_b)";
                return true;
            }

            // Text Parasolid (.x_t): reject NUL-containing garbage strictly!
            for (size_t i = 0; i < std::min<size_t>(size, 1024); ++i) {
                if (data[i] == 0) return false;
            }
            if (ext == L".x_b") {
                if (sv.find("SCH_") == std::string_view::npos ||
                    (sv.find("**END_OF_HEADER**") == std::string_view::npos && sv.find("PART") == std::string_view::npos))
                {
                    return false;
                }
            }

            // Locate start of XT body after optional **END_OF_HEADER** preamble
            size_t bodyStart = 0;
            size_t eoh = sv.find("**END_OF_HEADER**");
            if (eoh != std::string_view::npos) {
                size_t nl = sv.find('\n', eoh);
                bodyStart = (nl == std::string_view::npos) ? sv.size() : (nl + 1);
            }

            // Validate Parasolid XT transmit markers
            bool hasXtSchema = (eoh != std::string_view::npos) ||
                               (sv.find("SCH_") != std::string_view::npos) ||
                               (sv.find("PS_") != std::string_view::npos) ||
                               (sv.find("PARASOLID") != std::string_view::npos) ||
                               (sv.find("TRANSMIT") != std::string_view::npos);
            if (!hasXtSchema) return false;

            mesh.formatName = L"Siemens Parasolid XT Solid (.x_t)";

            auto isPkEntityKeyword = [](std::string_view w) -> bool {
                return w == "assembly" || w == "instance" || w == "body" || w == "transform" ||
                       w == "region"   || w == "shell"    || w == "face" || w == "loop"      ||
                       w == "fin"      || w == "halfedge" || w == "edge" || w == "vertex"    ||
                       w == "point"    || w == "line"     || w == "straight" || w == "circle"||
                       w == "ellipse"  || w == "b_curve"  || w == "intersection" ||
                       w == "plane"    || w == "cylinder" || w == "cyl"  || w == "cone"      ||
                       w == "sphere"   || w == "torus"    || w == "b_surface" ||
                       w == "att_def"  || w == "attrib"   || w == "field_names" ||
                       w == "pointer_lis_block" || w == "int_lis_block" || w == "real_lis_block";
            };

            struct PkTok {
                enum Kind { Word, Int, Real, Ref, SensePos, SenseNeg, Sep } kind = Word;
                std::string text;
                int32_t iVal = 0;
                double dVal = 0.0;
                bool lineStart = false;
            };

            std::vector<PkTok> toks;
            size_t p = bodyStart;
            bool atLineStart = true;
            while (p < sv.size() && toks.size() < 500000) {
                char c = sv[p];
                if (c == '\r' || c == '\n') { atLineStart = true; ++p; continue; }
                if (c == ' ' || c == '\t' || c == ',' || c == '(' || c == ')') { ++p; continue; }
                if (c == ';' || c == '#') {
                    // '#<digits>' is an entity ref; standalone '#' or ';' is a record separator
                    if (c == '#' && p + 1 < sv.size() && ((sv[p + 1] >= '0' && sv[p + 1] <= '9') || sv[p + 1] == '-')) {
                        size_t dEnd = p + 1;
                        if (sv[dEnd] == '-') ++dEnd;
                        while (dEnd < sv.size() && sv[dEnd] >= '0' && sv[dEnd] <= '9') ++dEnd;
                        int64_t rv = 0;
                        if (dEnd > p + 1 && ReaderText::ParseInt64Sv(sv.substr(p + 1, dEnd - (p + 1)), rv) &&
                            rv >= -2147483647LL && rv <= 2147483647LL)
                        {
                            toks.push_back({ PkTok::Ref, {}, static_cast<int32_t>(rv), 0.0, atLineStart });
                            atLineStart = false;
                            p = dEnd;
                            continue;
                        }
                    }
                    toks.push_back({ PkTok::Sep, ";", 0, 0.0, atLineStart });
                    atLineStart = true;
                    ++p;
                    continue;
                }
                // Stop at Parasolid end-of-part trailer
                if (c == '*' && sv.substr(p).starts_with("**END_OF_PART**")) break;

                // Quoted string '...' or "..."
                if (c == '\'' || c == '"') {
                    char q = c; ++p;
                    while (p < sv.size() && sv[p] != q) ++p;
                    if (p < sv.size()) ++p;
                    atLineStart = false;
                    continue;
                }

                // Parasolid counted string T<len>:<chars>
                if ((c == 'T' || c == 'U') && p + 2 < sv.size() && sv[p + 1] >= '0' && sv[p + 1] <= '9') {
                    size_t dEnd = p + 1;
                    while (dEnd < sv.size() && sv[dEnd] >= '0' && sv[dEnd] <= '9') ++dEnd;
                    int64_t slen = -1;
                    if (dEnd < sv.size() && sv[dEnd] == ':' &&
                        ReaderText::ParseInt64Sv(sv.substr(p + 1, dEnd - (p + 1)), slen) && slen >= 0)
                    {
                        p = dEnd + 1;
                        size_t adv = std::min<size_t>(static_cast<size_t>(std::min<int64_t>(slen, 10000000)), sv.size() - p);
                        p += adv;
                        atLineStart = false;
                        continue;
                    }
                }

                // Explicit pointer ref $<int> or @<int>
                if ((c == '$' || c == '@') && p + 1 < sv.size() &&
                    ((sv[p + 1] >= '0' && sv[p + 1] <= '9') || sv[p + 1] == '-'))
                {
                    size_t dEnd = p + 1;
                    if (sv[dEnd] == '-') ++dEnd;
                    while (dEnd < sv.size() && sv[dEnd] >= '0' && sv[dEnd] <= '9') ++dEnd;
                    int64_t rv = 0;
                    if (dEnd > p + 1 && ReaderText::ParseInt64Sv(sv.substr(p + 1, dEnd - (p + 1)), rv) &&
                        rv >= -2147483647LL && rv <= 2147483647LL)
                    {
                        toks.push_back({ PkTok::Ref, {}, static_cast<int32_t>(rv), 0.0, atLineStart });
                        atLineStart = false;
                        p = dEnd;
                        continue;
                    }
                }

                // '?' null pointer
                if (c == '?') {
                    toks.push_back({ PkTok::Ref, "?", -1, 0.0, atLineStart });
                    atLineStart = false;
                    ++p;
                    continue;
                }

                // '=' assignment token (for "<id> = <keyword> ...")
                if (c == '=') {
                    toks.push_back({ PkTok::Word, "=", 0, 0.0, atLineStart });
                    atLineStart = false;
                    ++p;
                    continue;
                }

                // Standalone '+' or '-' sense token vs signed number
                if ((c == '+' || c == '-') &&
                    (p + 1 >= sv.size() || !((sv[p + 1] >= '0' && sv[p + 1] <= '9') || sv[p + 1] == '.')))
                {
                    toks.push_back({ (c == '+') ? PkTok::SensePos : PkTok::SenseNeg, std::string(1, c), 0, 0.0, atLineStart });
                    atLineStart = false;
                    ++p;
                    continue;
                }

                // Number (Int vs Real)
                if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
                    size_t nEnd = p;
                    if (sv[nEnd] == '+' || sv[nEnd] == '-') ++nEnd;
                    bool hasDigit = false;
                    while (nEnd < sv.size() && sv[nEnd] >= '0' && sv[nEnd] <= '9') { ++nEnd; hasDigit = true; }
                    if (nEnd < sv.size() && sv[nEnd] == '.') {
                        ++nEnd;
                        while (nEnd < sv.size() && sv[nEnd] >= '0' && sv[nEnd] <= '9') { ++nEnd; hasDigit = true; }
                    }
                    if (hasDigit && nEnd < sv.size() && (sv[nEnd] == 'e' || sv[nEnd] == 'E')) {
                        size_t ePos = nEnd + 1;
                        if (ePos < sv.size() && (sv[ePos] == '+' || sv[ePos] == '-')) ++ePos;
                        if (ePos < sv.size() && sv[ePos] >= '0' && sv[ePos] <= '9') {
                            nEnd = ePos;
                            while (nEnd < sv.size() && sv[nEnd] >= '0' && sv[nEnd] <= '9') ++nEnd;
                        }
                    }
                    double dv = 0.0;
                    if (hasDigit && ReaderText::ParseDoubleSv(sv.substr(p, nEnd - p), dv)) {
                        std::string_view rawNum = sv.substr(p, nEnd - p);
                        bool isFloat = (rawNum.find('.') != std::string_view::npos ||
                                        rawNum.find('e') != std::string_view::npos ||
                                        rawNum.find('E') != std::string_view::npos);
                        if (!isFloat && dv >= -2147483648.0 && dv <= 2147483647.0) {
                            toks.push_back({ PkTok::Int, std::string(rawNum), static_cast<int32_t>(dv), dv, atLineStart });
                        } else {
                            toks.push_back({ PkTok::Real, std::string(rawNum), 0, std::isfinite(dv) ? dv : 0.0, atLineStart });
                        }
                        atLineStart = false;
                        p = nEnd;
                        continue;
                    }
                }

                // Identifier / .T. / .F.
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.') {
                    size_t s0 = p;
                    while (p < sv.size() && ((sv[p] >= 'a' && sv[p] <= 'z') || (sv[p] >= 'A' && sv[p] <= 'Z') ||
                                             (sv[p] >= '0' && sv[p] <= '9') || sv[p] == '_' || sv[p] == '.' || sv[p] == '-'))
                    {
                        ++p;
                    }
                    std::string w(sv.substr(s0, p - s0));
                    for (char& chL : w) if (chL >= 'A' && chL <= 'Z') chL = static_cast<char>(chL - 'A' + 'a');
                    if (w == "forward" || w == ".t.") {
                        toks.push_back({ PkTok::SensePos, w, 0, 0.0, atLineStart });
                    } else if (w == "reversed" || w == ".f.") {
                        toks.push_back({ PkTok::SenseNeg, w, 0, 0.0, atLineStart });
                    } else if (w == "file_end") {
                        break;
                    } else {
                        toks.push_back({ PkTok::Word, std::move(w), 0, 0.0, atLineStart });
                    }
                    atLineStart = false;
                    continue;
                }

                ++p;
            }

            // Segment token stream into PK entity records:
            // Supports BOTH "<keyword> <id> ..." AND "[#|$]<id> = <keyword> ..."
            struct PkEntityRec {
                int32_t id = -1;
                std::string type;
                std::vector<int32_t> refs;
                std::vector<double> nums;
                bool hasSenseNeg = false;
                bool hasSensePos = false;
            };

            auto matchRecHeader = [&](size_t ti, int32_t& outId, std::string& outType, size_t& outAdvance) -> bool {
                if (ti >= toks.size() || !toks[ti].lineStart) return false;
                // Form A: <keyword> <positive_id_or_ref>
                if (toks[ti].kind == PkTok::Word && isPkEntityKeyword(toks[ti].text) && ti + 1 < toks.size()) {
                    const PkTok& t1 = toks[ti + 1];
                    if ((t1.kind == PkTok::Int || t1.kind == PkTok::Ref) && t1.iVal >= 0) {
                        outType = toks[ti].text;
                        outId = t1.iVal;
                        outAdvance = 2;
                        return true;
                    }
                }
                // Form B: <id_or_ref> = <keyword>
                if ((toks[ti].kind == PkTok::Int || toks[ti].kind == PkTok::Ref) && toks[ti].iVal >= 0 &&
                    ti + 2 < toks.size() && toks[ti + 1].kind == PkTok::Word && toks[ti + 1].text == "=" &&
                    toks[ti + 2].kind == PkTok::Word && isPkEntityKeyword(toks[ti + 2].text))
                {
                    outId = toks[ti].iVal;
                    outType = toks[ti + 2].text;
                    outAdvance = 3;
                    return true;
                }
                return false;
            };

            std::unordered_map<int32_t, PkEntityRec> pkMap;
            std::vector<int32_t> pkOrder;
            size_t ti = 0;
            while (ti < toks.size()) {
                int32_t recId = -1;
                std::string recType;
                size_t adv = 0;
                if (!matchRecHeader(ti, recId, recType, adv)) {
                    ++ti;
                    continue;
                }
                ti += adv;
                PkEntityRec rec;
                rec.id = recId;
                rec.type = std::move(recType);

                while (ti < toks.size()) {
                    if (toks[ti].kind == PkTok::Sep) { ++ti; break; }
                    int32_t nextId = -1; std::string nextType; size_t nextAdv = 0;
                    if (matchRecHeader(ti, nextId, nextType, nextAdv)) break;

                    const PkTok& tk = toks[ti++];
                    if (tk.kind == PkTok::Ref) {
                        rec.refs.push_back(tk.iVal);
                    } else if (tk.kind == PkTok::Int) {
                        rec.refs.push_back(tk.iVal);
                        rec.nums.push_back(tk.dVal);
                    } else if (tk.kind == PkTok::Real) {
                        rec.nums.push_back(tk.dVal);
                    } else if (tk.kind == PkTok::SenseNeg) {
                        if (!rec.hasSensePos) rec.hasSenseNeg = true;
                    } else if (tk.kind == PkTok::SensePos) {
                        if (!rec.hasSenseNeg) rec.hasSensePos = true;
                    }
                }
                pkMap[rec.id] = std::move(rec);
                pkOrder.push_back(recId);
            }

            if (pkMap.empty()) return false;

            auto refHasType = [&](int32_t r, const char* t1, const char* t2 = nullptr) -> bool {
                if (r < 0) return false;
                auto it = pkMap.find(r);
                if (it == pkMap.end()) return false;
                if (it->second.type == t1) return true;
                return (t2 != nullptr && it->second.type == t2);
            };
            auto firstRefOf = [&](const PkEntityRec& rc, const char* t1, const char* t2 = nullptr) -> int32_t {
                for (int32_t r : rc.refs) {
                    if (r != rc.id && refHasType(r, t1, t2)) return r;
                }
                return -1;
            };

            BRepGraph g;
            std::unordered_map<int32_t, CadXform> pkTransforms;
            for (int32_t id : pkOrder) {
                auto pkIt = pkMap.find(id);
                if (pkIt == pkMap.end()) continue;
                const PkEntityRec& rc = pkIt->second;
                const std::string& tp = rc.type;

                if (tp == "point") {
                    if (rc.nums.size() >= 3) {
                        size_t n = rc.nums.size();
                        g.points[id] = { rc.nums[n - 3], rc.nums[n - 2], rc.nums[n - 1] };
                    }
                } else if (tp == "vertex") {
                    int32_t ptRef = firstRefOf(rc, "point");
                    if (ptRef >= 0) g.vertexToPoint[id] = ptRef;
                } else if (tp == "transform") {
                    if (rc.nums.size() >= 12) {
                        size_t n = rc.nums.size();
                        size_t base = (n >= 13) ? (n - 13) : (n - 12);
                        CadXform xf;
                        for (int i = 0; i < 9; ++i) xf.r[i] = rc.nums[base + static_cast<size_t>(i)];
                        xf.t[0] = rc.nums[base + 9];
                        xf.t[1] = rc.nums[base + 10];
                        xf.t[2] = rc.nums[base + 11];
                        xf.scale = (n >= 13) ? rc.nums[base + 12] : 1.0;
                        if (!std::isfinite(xf.scale) || std::fabs(xf.scale) < 1e-12) xf.scale = 1.0;
                        xf.valid = true;
                        pkTransforms[id] = xf;
                    }
                } else if (tp == "line" || tp == "straight") {
                    BRepCurve c;
                    c.kind = CurveKind::Straight;
                    if (rc.nums.size() >= 6) {
                        size_t b = rc.nums.size() - 6;
                        c.origin = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        c.normal = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                    }
                    g.curves[id] = std::move(c);
                } else if (tp == "circle") {
                    BRepCurve c;
                    c.kind = CurveKind::Ellipse;
                    c.ratio = 1.0;
                    if (rc.nums.size() >= 10) {
                        size_t b = rc.nums.size() - 10;
                        c.origin = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        c.normal = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                        CadVec3 xAx = vNorm({ rc.nums[b + 6], rc.nums[b + 7], rc.nums[b + 8] });
                        double rad = std::fabs(rc.nums[b + 9]);
                        c.majorAxis = vMul(xAx, rad);
                    } else if (rc.nums.size() >= 7) {
                        size_t b = rc.nums.size() - 7;
                        c.origin = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        c.normal = vNorm({ rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] });
                        double rad = std::fabs(rc.nums[b + 6]);
                        CadVec3 ref = (std::fabs(c.normal.z) < 0.9) ? CadVec3{ 0, 0, 1 } : CadVec3{ 1, 0, 0 };
                        c.majorAxis = vMul(vNorm(vCross(ref, c.normal)), rad);
                    }
                    g.curves[id] = std::move(c);
                } else if (tp == "ellipse") {
                    BRepCurve c;
                    c.kind = CurveKind::Ellipse;
                    if (rc.nums.size() >= 11) {
                        size_t b = rc.nums.size() - 11;
                        c.origin = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        c.normal = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                        CadVec3 xAx = vNorm({ rc.nums[b + 6], rc.nums[b + 7], rc.nums[b + 8] });
                        double rMaj = std::fabs(rc.nums[b + 9]);
                        double rMin = std::fabs(rc.nums[b + 10]);
                        c.majorAxis = vMul(xAx, rMaj);
                        c.ratio = (rMaj > 1e-9) ? (rMin / rMaj) : 1.0;
                    }
                    g.curves[id] = std::move(c);
                } else if (tp == "plane") {
                    BRepSurf s;
                    s.kind = SurfKind::Plane;
                    if (rc.nums.size() >= 9) {
                        size_t b = rc.nums.size() - 9;
                        s.origin = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        s.normal = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                        s.uAxis  = { rc.nums[b + 6], rc.nums[b + 7], rc.nums[b + 8] };
                    } else if (rc.nums.size() >= 6) {
                        size_t b = rc.nums.size() - 6;
                        s.origin = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        s.normal = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                    }
                    g.surfaces[id] = s;
                } else if (tp == "cylinder" || tp == "cyl" || tp == "cone") {
                    BRepSurf s;
                    s.kind = SurfKind::CylinderCone;
                    if (rc.nums.size() >= 7) {
                        // First 6 reals of cylinder/cone after links: origin(3), axis(3), radius(1)
                        size_t b = (rc.nums.size() >= 10) ? (rc.nums.size() - 10) : (rc.nums.size() - 7);
                        s.origin  = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        s.normal  = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                        s.radius1 = std::fabs(rc.nums[b + 6]);
                    }
                    g.surfaces[id] = s;
                } else if (tp == "sphere") {
                    BRepSurf s;
                    s.kind = SurfKind::Sphere;
                    if (rc.nums.size() >= 4) {
                        size_t b = (rc.nums.size() >= 10) ? (rc.nums.size() - 10) : (rc.nums.size() - 4);
                        s.origin  = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        s.radius1 = std::fabs(rc.nums[b + 3]);
                    }
                    g.surfaces[id] = s;
                } else if (tp == "torus") {
                    BRepSurf s;
                    s.kind = SurfKind::Torus;
                    if (rc.nums.size() >= 8) {
                        size_t b = rc.nums.size() - 8;
                        s.origin  = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        s.normal  = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                        s.radius1 = std::fabs(rc.nums[b + 6]);
                        s.radius2 = std::fabs(rc.nums[b + 7]);
                    }
                    g.surfaces[id] = s;
                } else if (tp == "b_surface") {
                    // Parasolid XT b_surface: [uDeg, vDeg, nU, nV, vertexDim(3|4), knotsU(nU+uDeg+1), knotsV(nV+vDeg+1), poles(nU*nV*vertexDim)]
                    if (rc.nums.size() >= 16) {
                        int uDeg = static_cast<int>(std::llround(rc.nums[0]));
                        int vDeg = static_cast<int>(std::llround(rc.nums[1]));
                        int nU   = static_cast<int>(std::llround(rc.nums[2]));
                        int nV   = static_cast<int>(std::llround(rc.nums[3]));
                        int vDim = static_cast<int>(std::llround(rc.nums[4]));
                        if (uDeg >= 1 && uDeg <= 15 && vDeg >= 1 && vDeg <= 15 &&
                            nU > uDeg && nU <= 256 && nV > vDeg && nV <= 256 && (vDim == 3 || vDim == 4))
                        {
                            size_t kuCnt = static_cast<size_t>(nU + uDeg + 1);
                            size_t kvCnt = static_cast<size_t>(nV + vDeg + 1);
                            size_t pCnt  = static_cast<size_t>(nU) * static_cast<size_t>(nV);
                            size_t sp = 5;
                            if (sp + kuCnt + kvCnt + pCnt * static_cast<size_t>(vDim) <= rc.nums.size()) {
                                BRepSurf s;
                                s.kind = SurfKind::Nurbs;
                                s.uDeg = uDeg; s.vDeg = vDeg; s.nU = nU; s.nV = nV;
                                s.isRational = (vDim == 4);
                                s.knotsU.assign(rc.nums.begin() + static_cast<ptrdiff_t>(sp),
                                                rc.nums.begin() + static_cast<ptrdiff_t>(sp + kuCnt));
                                sp += kuCnt;
                                s.knotsV.assign(rc.nums.begin() + static_cast<ptrdiff_t>(sp),
                                                rc.nums.begin() + static_cast<ptrdiff_t>(sp + kvCnt));
                                sp += kvCnt;
                                s.poles.resize(pCnt);
                                if (s.isRational) s.weights.resize(pCnt, 1.0);
                                for (size_t k = 0; k < pCnt; ++k) {
                                    s.poles[k] = {
                                        rc.nums[sp + k * static_cast<size_t>(vDim) + 0],
                                        rc.nums[sp + k * static_cast<size_t>(vDim) + 1],
                                        rc.nums[sp + k * static_cast<size_t>(vDim) + 2]
                                    };
                                    if (s.isRational) s.weights[k] = rc.nums[sp + k * 4u + 3u];
                                }
                                g.surfaces[id] = std::move(s);
                            }
                        }
                    }
                }
            }

            // Second pass: topology entities (edges, fins, loops, faces, bodies)
            for (int32_t id : pkOrder) {
                auto pkIt = pkMap.find(id);
                if (pkIt == pkMap.end()) continue;
                const PkEntityRec& rc = pkIt->second;
                const std::string& tp = rc.type;

                if (tp == "edge") {
                    BRepEdge ed;
                    std::vector<int32_t> vRefs;
                    for (int32_t r : rc.refs) {
                        if (refHasType(r, "vertex")) vRefs.push_back(r);
                        else if (ed.curveId < 0 && g.curves.count(r)) ed.curveId = r;
                        else if (ed.bodyId < 0 && refHasType(r, "body")) ed.bodyId = r;
                    }
                    if (vRefs.size() >= 2) { ed.startVert = vRefs[0]; ed.endVert = vRefs[1]; }
                    else if (vRefs.size() == 1) { ed.startVert = vRefs[0]; ed.endVert = vRefs[0]; }
                    ed.reversed = rc.hasSenseNeg;
                    g.edges[id] = ed;
                    g.edgeOrder.push_back(id);
                } else if (tp == "fin" || tp == "halfedge") {
                    BRepCoedge ce;
                    ce.nextCoedge = firstRefOf(rc, "fin", "halfedge");
                    ce.edgeId     = firstRefOf(rc, "edge");
                    ce.headVert   = firstRefOf(rc, "vertex");
                    ce.reversed   = rc.hasSenseNeg;
                    g.coedges[id] = ce;
                } else if (tp == "loop") {
                    BRepLoop lp;
                    lp.ownerFace   = firstRefOf(rc, "face");
                    lp.firstCoedge = firstRefOf(rc, "fin", "halfedge");
                    lp.nextLoop    = firstRefOf(rc, "loop");
                    if (lp.firstCoedge < 0) {
                        for (int32_t r : rc.refs) {
                            if (refHasType(r, "edge")) lp.directEdges.push_back(r);
                        }
                    }
                    g.loops[id] = std::move(lp);
                } else if (tp == "face") {
                    BRepFace fc;
                    fc.nextFace = firstRefOf(rc, "face");
                    fc.firstLoop = firstRefOf(rc, "loop");
                    for (int32_t r : rc.refs) {
                        if (fc.surfaceId < 0 && g.surfaces.count(r)) fc.surfaceId = r;
                        if (fc.bodyId < 0 && refHasType(r, "body")) fc.bodyId = r;
                    }
                    fc.reversed = rc.hasSenseNeg;
                    g.faces[id] = std::move(fc);
                    g.faceOrder.push_back(id);
                } else if (tp == "body") {
                    int32_t xfRef = firstRefOf(rc, "transform");
                    if (xfRef >= 0 && pkTransforms.count(xfRef)) {
                        g.bodyTransforms[id] = pkTransforms[xfRef];
                    }
                }
            }

            // Link loops <-> faces bidirectionally
            for (auto& kv : g.faces) {
                BRepFace& fc = kv.second;
                int32_t curL = fc.firstLoop;
                std::set<int32_t> seen;
                while (curL >= 0 && seen.insert(curL).second && seen.size() <= 512) {
                    fc.loopIds.push_back(curL);
                    auto lit = g.loops.find(curL);
                    if (lit == g.loops.end()) break;
                    curL = lit->second.nextLoop;
                }
            }
            for (const auto& kv : g.loops) {
                if (kv.second.ownerFace >= 0) {
                    auto fit = g.faces.find(kv.second.ownerFace);
                    if (fit != g.faces.end()) {
                        auto& lids = fit->second.loopIds;
                        if (std::find(lids.begin(), lids.end(), kv.first) == lids.end()) {
                            lids.push_back(kv.first);
                        }
                    }
                }
                // Populate edge start/end vertices from PK fin ring when edge only pointed to fins
                if (kv.second.firstCoedge >= 0) {
                    std::vector<int32_t> ring;
                    int32_t cur = kv.second.firstCoedge;
                    std::set<int32_t> seen;
                    while (cur >= 0 && seen.insert(cur).second && ring.size() < 4096) {
                        auto cit = g.coedges.find(cur);
                        if (cit == g.coedges.end()) break;
                        ring.push_back(cur);
                        cur = cit->second.nextCoedge;
                        if (cur == kv.second.firstCoedge) break;
                    }
                    for (size_t i = 0; i < ring.size(); ++i) {
                        auto ceIt = g.coedges.find(ring[i]);
                        auto pvIt = g.coedges.find(ring[(i + ring.size() - 1) % ring.size()]);
                        if (ceIt == g.coedges.end() || pvIt == g.coedges.end()) continue;
                        const BRepCoedge& ce = ceIt->second;
                        const BRepCoedge& pv = pvIt->second;
                        if (ce.headVert >= 0 && pv.headVert >= 0 && ce.edgeId >= 0) {
                            auto eit = g.edges.find(ce.edgeId);
                            if (eit != g.edges.end() && eit->second.startVert < 0 && eit->second.endVert < 0) {
                                if (!ce.reversed) { eit->second.startVert = pv.headVert; eit->second.endVert = ce.headVert; }
                                else              { eit->second.startVert = ce.headVert; eit->second.endVert = pv.headVert; }
                            }
                        }
                    }
                }
            }

            return evaluateBRepGraph(g);
        }
}

bool ParseCadKernelArchiveOrStream(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    if (ext == L".3dxml") return ParseCatia3dxml(data, size, ext, mesh);
    if (ext == L".jt")    return ParseSiemensJt(data, size, ext, mesh);
    if (ext == L".brep")  return ParseOpenCascadeBrep(data, size, ext, mesh);
    if (ext == L".sat" || ext == L".sab") return ParseSpatialAcisSat(data, size, ext, mesh);
    if (ext == L".x_t" || ext == L".x_b") return ParseParasolidXt(data, size, ext, mesh);
    return false;
}

} // namespace Formats
