#pragma once
// SpatialAcisSat.h — Spatial ACIS ASCII (.sat) & Binary (.sab) B-Rep 3D CAD reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseSpatialAcisSat(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    if (ext != L".sat" && ext != L".sab") return false;
    using namespace DetailAcisParasolid;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    auto evaluateBRepGraph = [&](const BRepGraph& g) -> bool {
        return DetailAcisParasolid::EvaluateBRepGraph(g, mesh);
    };
        // Shared ACIS Raw Entity Record -> BRepGraph builder (used by BOTH .sat and .sab!)
        struct AcisRawRec {
            std::string type;
            std::vector<int32_t> ptrs;
            std::vector<double> nums;
            std::vector<std::string> words;
            std::vector<double> subNums;
            std::vector<std::string> subWords;
        };

        auto buildAcisGraphAndTriangulate = [&](const std::vector<AcisRawRec>& recs) -> bool {
            if (recs.empty()) return false;
            const int32_t nRecs = static_cast<int32_t>(recs.size());
            auto validRef = [&](int32_t r) -> bool { return r >= 0 && r < nRecs && !recs[static_cast<size_t>(r)].type.empty(); };
            auto findRefByType = [&](const AcisRawRec& rc, const char* want, size_t startSlot = 0) -> int32_t {
                for (size_t i = startSlot; i < rc.ptrs.size(); ++i) {
                    int32_t r = rc.ptrs[i];
                    if (validRef(r) && recs[static_cast<size_t>(r)].type == want) return r;
                }
                return -1;
            };
            auto findRefBySuffix = [&](const AcisRawRec& rc, const char* suffix, size_t startSlot = 0) -> int32_t {
                size_t slen = std::strlen(suffix);
                for (size_t i = startSlot; i < rc.ptrs.size(); ++i) {
                    int32_t r = rc.ptrs[i];
                    if (!validRef(r)) continue;
                    const std::string& t = recs[static_cast<size_t>(r)].type;
                    if (t.size() >= slen && t.compare(t.size() - slen, slen, suffix) == 0) return r;
                }
                return -1;
            };
            auto hasWord = [](const AcisRawRec& rc, const char* w) -> bool {
                for (const auto& s : rc.words) if (s == w) return true;
                return false;
            };
            auto ptrSkipAttrib = [&](const AcisRawRec& rc) -> size_t {
                if (!rc.ptrs.empty()) {
                    int32_t r0 = rc.ptrs[0];
                    if (r0 < 0 || (validRef(r0) && recs[static_cast<size_t>(r0)].type.find("attrib") != std::string::npos)) {
                        return 1;
                    }
                }
                return 0;
            };

            // Detect ACIS 7.0+ leading integer entity IDs on topology records (absent in ACIS <= 6.0 and .sab nums)
            bool hasLeadingEntId = false;
            for (const auto& rc : recs) {
                if ((rc.type == "face" || rc.type == "loop" || rc.type == "lump" || rc.type == "shell") && !rc.nums.empty()) {
                    hasLeadingEntId = true;
                    break;
                }
            }

            BRepGraph g;
            std::unordered_map<int32_t, int32_t> lumpNext, lumpShell, lumpBody;
            std::unordered_map<int32_t, int32_t> shellNext, shellSubshell, shellFace, shellWire, shellLump;
            std::unordered_map<int32_t, int32_t> subshellNext, subshellChild, subshellFace;
            std::unordered_map<int32_t, int32_t> wireNext, wireCoedge;
            std::vector<int32_t> bodyIds;

            for (int32_t idx = 0; idx < nRecs; ++idx) {
                const AcisRawRec& rc = recs[static_cast<size_t>(idx)];
                const std::string& tp = rc.type;
                if (tp.empty()) continue;

                if (tp == "point") {
                    if (rc.nums.size() >= 3) {
                        size_t n = rc.nums.size();
                        g.points[idx] = { rc.nums[n - 3], rc.nums[n - 2], rc.nums[n - 1] };
                    }
                } else if (tp == "vertex") {
                    int32_t ptRef = findRefByType(rc, "point", 0);
                    size_t pBase = ptrSkipAttrib(rc);
                    if (ptRef < 0 && rc.ptrs.size() >= pBase + 2) ptRef = rc.ptrs[pBase + 1];
                    if (ptRef >= 0) g.vertexToPoint[idx] = ptRef;
                } else if (tp == "transform") {
                    size_t base = (hasLeadingEntId || rc.nums.size() == 14) ? 1 : 0;
                    if (rc.nums.size() >= base + 12) {
                        CadXform xf;
                        for (int i = 0; i < 9; ++i) xf.r[i] = rc.nums[base + static_cast<size_t>(i)];
                        xf.t[0] = rc.nums[base + 9];
                        xf.t[1] = rc.nums[base + 10];
                        xf.t[2] = rc.nums[base + 11];
                        xf.scale = (rc.nums.size() >= base + 13) ? rc.nums[base + 12] : 1.0;
                        if (!std::isfinite(xf.scale) || std::fabs(xf.scale) < 1e-12) xf.scale = 1.0;
                        xf.valid = true;
                        g.bodyTransforms[-1000000 - idx] = xf; // temporary transform table keyed by transform id
                    }
                } else if (tp == "straight-curve") {
                    BRepCurve c;
                    c.kind = CurveKind::Straight;
                    size_t base = (hasLeadingEntId || rc.nums.size() == 7 || rc.nums.size() == 9) ? 1 : 0;
                    if (rc.nums.size() >= base + 6) {
                        c.origin = { rc.nums[base + 0], rc.nums[base + 1], rc.nums[base + 2] };
                        c.normal = { rc.nums[base + 3], rc.nums[base + 4], rc.nums[base + 5] };
                    }
                    g.curves[idx] = std::move(c);
                } else if (tp == "ellipse-curve") {
                    BRepCurve c;
                    c.kind = CurveKind::Ellipse;
                    size_t base = (hasLeadingEntId || rc.nums.size() == 11 || rc.nums.size() == 13) ? 1 : 0;
                    if (rc.nums.size() >= base + 10) {
                        c.origin    = { rc.nums[base + 0], rc.nums[base + 1], rc.nums[base + 2] };
                        c.normal    = { rc.nums[base + 3], rc.nums[base + 4], rc.nums[base + 5] };
                        c.majorAxis = { rc.nums[base + 6], rc.nums[base + 7], rc.nums[base + 8] };
                        c.ratio     = rc.nums[base + 9];
                    }
                    g.curves[idx] = std::move(c);
                } else if (tp.find("intcurve") != std::string::npos || tp.find("spline") != std::string::npos) {
                    if (tp.find("curve") != std::string::npos) {
                        BRepCurve c;
                        c.kind = CurveKind::Spline;
                        if (rc.subNums.size() >= 9) {
                            size_t rem3 = rc.subNums.size() % 3;
                            for (size_t k = rem3; k + 2 < rc.subNums.size() && c.ctrlPts.size() < 64; k += 3) {
                                c.ctrlPts.push_back({ rc.subNums[k], rc.subNums[k + 1], rc.subNums[k + 2] });
                            }
                        }
                        g.curves[idx] = std::move(c);
                    } else if (tp.find("surface") != std::string::npos && rc.subNums.size() >= 12) {
                        // ACIS spline-surface { exactsur nubs|nurbs uDeg vDeg [nU nV] knotsU knotsV poles [weights] }
                        bool isRat = false;
                        for (const auto& sw : rc.subWords) {
                            if (sw == "nurbs" || sw == "rat" || sw == "rational") { isRat = true; break; }
                        }
                        if (!std::isfinite(rc.subNums[0]) || !std::isfinite(rc.subNums[1]) ||
                            rc.subNums[0] < 1.0 || rc.subNums[0] > 15.0 ||
                            rc.subNums[1] < 1.0 || rc.subNums[1] > 15.0) {
                            continue;
                        }
                        int uDeg = static_cast<int>(std::llround(rc.subNums[0]));
                        int vDeg = static_cast<int>(std::llround(rc.subNums[1]));
                        if (uDeg >= 1 && uDeg <= 15 && vDeg >= 1 && vDeg <= 15 && rc.subNums.size() >= 6 &&
                            std::isfinite(rc.subNums[2]) && std::isfinite(rc.subNums[3]) &&
                            rc.subNums[2] >= 1.0 && rc.subNums[2] <= 256.0 &&
                            rc.subNums[3] >= 1.0 && rc.subNums[3] <= 256.0) {
                            int nU = static_cast<int>(std::llround(rc.subNums[2]));
                            int nV = static_cast<int>(std::llround(rc.subNums[3]));
                            size_t sp = 4;
                            BRepSurf s;
                            s.kind = SurfKind::Nurbs;
                            s.uDeg = uDeg; s.vDeg = vDeg; s.nU = nU; s.nV = nV;
                            s.isRational = isRat;

                            if (nU > uDeg && nU <= 256 && nV > vDeg && nV <= 256) {
                                size_t fullKu = static_cast<size_t>(nU + uDeg + 1);
                                size_t fullKv = static_cast<size_t>(nV + vDeg + 1);
                                size_t poleCnt = static_cast<size_t>(nU) * static_cast<size_t>(nV);
                                size_t poleFloats = poleCnt * (isRat ? 4u : 3u);

                                // Variant 1: flat full knot vectors [knotsU(nU+uDeg+1)][knotsV(nV+vDeg+1)][poles (x,y,z[,w])]
                                if (sp + fullKu + fullKv + poleCnt * 3u <= rc.subNums.size()) {
                                    s.knotsU.assign(rc.subNums.begin() + static_cast<ptrdiff_t>(sp),
                                                    rc.subNums.begin() + static_cast<ptrdiff_t>(sp + fullKu));
                                    sp += fullKu;
                                    s.knotsV.assign(rc.subNums.begin() + static_cast<ptrdiff_t>(sp),
                                                    rc.subNums.begin() + static_cast<ptrdiff_t>(sp + fullKv));
                                    sp += fullKv;

                                    if (isRat && sp + poleFloats <= rc.subNums.size()) {
                                        s.poles.resize(poleCnt);
                                        s.weights.resize(poleCnt, 1.0);
                                        // Check whether weights are interleaved (x,y,z,w) or planar [xyz...][w...]
                                        for (size_t k = 0; k < poleCnt; ++k) {
                                            s.poles[k] = { rc.subNums[sp + k * 4 + 0], rc.subNums[sp + k * 4 + 1], rc.subNums[sp + k * 4 + 2] };
                                            s.weights[k] = rc.subNums[sp + k * 4 + 3];
                                        }
                                        g.surfaces[idx] = std::move(s);
                                    } else if (sp + poleCnt * 3u <= rc.subNums.size()) {
                                        s.poles.resize(poleCnt);
                                        for (size_t k = 0; k < poleCnt; ++k) {
                                            s.poles[k] = { rc.subNums[sp + k * 3 + 0], rc.subNums[sp + k * 3 + 1], rc.subNums[sp + k * 3 + 2] };
                                        }
                                        g.surfaces[idx] = std::move(s);
                                    }
                                }
                            }
                        }
                    }
                } else if (tp == "plane-surface") {
                    BRepSurf s;
                    s.kind = SurfKind::Plane;
                    size_t base = (hasLeadingEntId || rc.nums.size() == 7 || rc.nums.size() == 10) ? 1 : 0;
                    if (rc.nums.size() >= base + 6) {
                        s.origin = { rc.nums[base + 0], rc.nums[base + 1], rc.nums[base + 2] };
                        s.normal = { rc.nums[base + 3], rc.nums[base + 4], rc.nums[base + 5] };
                        if (rc.nums.size() >= base + 9) {
                            s.uAxis = { rc.nums[base + 6], rc.nums[base + 7], rc.nums[base + 8] };
                        }
                    }
                    g.surfaces[idx] = s;
                } else if (tp == "cone-surface") {
                    BRepSurf s;
                    s.kind = SurfKind::CylinderCone;
                    size_t base = (hasLeadingEntId || rc.nums.size() == 11 || rc.nums.size() == 14) ? 1 : 0;
                    if (rc.nums.size() >= base + 10) {
                        s.origin  = { rc.nums[base + 0], rc.nums[base + 1], rc.nums[base + 2] };
                        s.normal  = { rc.nums[base + 3], rc.nums[base + 4], rc.nums[base + 5] };
                        s.uAxis   = { rc.nums[base + 6], rc.nums[base + 7], rc.nums[base + 8] };
                        s.radius1 = vLen(s.uAxis);
                        s.ratio   = rc.nums[base + 9];
                    }
                    g.surfaces[idx] = s;
                } else if (tp == "sphere-surface") {
                    BRepSurf s;
                    s.kind = SurfKind::Sphere;
                    size_t base = (hasLeadingEntId || rc.nums.size() == 5 || rc.nums.size() == 11) ? 1 : 0;
                    if (rc.nums.size() >= base + 4) {
                        s.origin  = { rc.nums[base + 0], rc.nums[base + 1], rc.nums[base + 2] };
                        s.radius1 = std::fabs(rc.nums[base + 3]);
                    }
                    g.surfaces[idx] = s;
                } else if (tp == "torus-surface") {
                    BRepSurf s;
                    s.kind = SurfKind::Torus;
                    size_t base = (hasLeadingEntId || rc.nums.size() == 9 || rc.nums.size() == 12) ? 1 : 0;
                    if (rc.nums.size() >= base + 8) {
                        s.origin  = { rc.nums[base + 0], rc.nums[base + 1], rc.nums[base + 2] };
                        s.normal  = { rc.nums[base + 3], rc.nums[base + 4], rc.nums[base + 5] };
                        s.radius1 = std::fabs(rc.nums[base + 6]);
                        s.radius2 = rc.nums[base + 7];
                    }
                    g.surfaces[idx] = s;
                } else if (tp == "edge") {
                    BRepEdge ed;
                    std::vector<int32_t> vRefs;
                    for (int32_t r : rc.ptrs) {
                        if (validRef(r) && recs[static_cast<size_t>(r)].type == "vertex") vRefs.push_back(r);
                    }
                    if (vRefs.size() >= 2) { ed.startVert = vRefs[0]; ed.endVert = vRefs[1]; }
                    else if (vRefs.size() == 1) { ed.startVert = vRefs[0]; ed.endVert = vRefs[0]; }
                    ed.curveId = findRefBySuffix(rc, "-curve", 0);
                    ed.reversed = hasWord(rc, "reversed");
                    g.edges[idx] = ed;
                    g.edgeOrder.push_back(idx);
                } else if (tp == "coedge") {
                    BRepCoedge ce;
                    size_t pBase = ptrSkipAttrib(rc);
                    if (rc.ptrs.size() > pBase)     ce.nextCoedge = rc.ptrs[pBase];
                    if (rc.ptrs.size() > pBase + 1) ce.prevCoedge = rc.ptrs[pBase + 1];
                    ce.edgeId = findRefByType(rc, "edge", 0);
                    ce.reversed = hasWord(rc, "reversed");
                    g.coedges[idx] = ce;
                } else if (tp == "loop") {
                    BRepLoop lp;
                    lp.nextLoop    = findRefByType(rc, "loop", 0);
                    lp.firstCoedge = findRefByType(rc, "coedge", 0);
                    lp.ownerFace   = findRefByType(rc, "face", 0);
                    g.loops[idx] = lp;
                } else if (tp == "face") {
                    BRepFace fc;
                    fc.nextFace  = findRefByType(rc, "face", 0);
                    fc.firstLoop = findRefByType(rc, "loop", 0);
                    fc.surfaceId = findRefBySuffix(rc, "-surface", 0);
                    fc.reversed  = hasWord(rc, "reversed");
                    g.faces[idx] = fc;
                    g.faceOrder.push_back(idx);
                } else if (tp == "body") {
                    bodyIds.push_back(idx);
                } else if (tp == "lump") {
                    lumpNext[idx]  = findRefByType(rc, "lump", 0);
                    lumpShell[idx] = findRefByType(rc, "shell", 0);
                    lumpBody[idx]  = findRefByType(rc, "body", 0);
                } else if (tp == "shell") {
                    shellNext[idx]     = findRefByType(rc, "shell", 0);
                    shellSubshell[idx] = findRefByType(rc, "subshell", 0);
                    shellFace[idx]     = findRefByType(rc, "face", 0);
                    shellWire[idx]     = findRefByType(rc, "wire", 0);
                    shellLump[idx]     = findRefByType(rc, "lump", 0);
                } else if (tp == "subshell") {
                    subshellNext[idx]  = findRefByType(rc, "subshell", 2);
                    subshellChild[idx] = findRefByType(rc, "subshell", 3);
                    subshellFace[idx]  = findRefByType(rc, "face", 0);
                } else if (tp == "wire") {
                    wireNext[idx]   = findRefByType(rc, "wire", 0);
                    wireCoedge[idx] = findRefByType(rc, "coedge", 0);
                }
            }

            // Link all loops to their owning faces (both via face.firstLoop->nextLoop and loop.ownerFace)
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
            }

            // Propagate body transforms down to faces and wireframe edges
            auto assignFaceChainBody = [&](int32_t firstF, int32_t bId) {
                int32_t f = firstF;
                std::set<int32_t> seen;
                while (f >= 0 && seen.insert(f).second && seen.size() <= 65536) {
                    auto fit = g.faces.find(f);
                    if (fit == g.faces.end()) break;
                    fit->second.bodyId = bId;
                    f = fit->second.nextFace;
                }
            };
            auto assignWireChainBody = [&](int32_t firstW, int32_t bId) {
                int32_t w = firstW;
                std::set<int32_t> seenW;
                while (w >= 0 && seenW.insert(w).second && seenW.size() <= 4096) {
                    int32_t ce0 = wireCoedge.count(w) ? wireCoedge[w] : -1;
                    int32_t ce = ce0;
                    std::set<int32_t> seenCe;
                    while (ce >= 0 && seenCe.insert(ce).second && seenCe.size() <= 8192) {
                        auto cit = g.coedges.find(ce);
                        if (cit == g.coedges.end()) break;
                        auto eit = g.edges.find(cit->second.edgeId);
                        if (eit != g.edges.end()) eit->second.bodyId = bId;
                        ce = cit->second.nextCoedge;
                        if (ce == ce0) break;
                    }
                    w = wireNext.count(w) ? wireNext[w] : -1;
                }
            };
            auto assignSubshellBody = [&](auto& self, int32_t ss, int32_t bId, int depth) -> void {
                if (ss < 0 || depth > 64) return;
                if (subshellFace.count(ss)) assignFaceChainBody(subshellFace[ss], bId);
                if (subshellChild.count(ss)) self(self, subshellChild[ss], bId, depth + 1);
                if (subshellNext.count(ss))  self(self, subshellNext[ss],  bId, depth + 1);
            };
            auto assignShellChainBody = [&](int32_t firstSh, int32_t bId) {
                int32_t sh = firstSh;
                std::set<int32_t> seenSh;
                while (sh >= 0 && seenSh.insert(sh).second && seenSh.size() <= 4096) {
                    if (shellFace.count(sh))     assignFaceChainBody(shellFace[sh], bId);
                    if (shellSubshell.count(sh)) assignSubshellBody(assignSubshellBody, shellSubshell[sh], bId, 0);
                    if (shellWire.count(sh))     assignWireChainBody(shellWire[sh], bId);
                    sh = shellNext.count(sh) ? shellNext[sh] : -1;
                }
            };

            for (int32_t bId : bodyIds) {
                const AcisRawRec& brc = recs[static_cast<size_t>(bId)];
                int32_t xfRef = findRefByType(brc, "transform", 0);
                if (xfRef >= 0) {
                    auto xit = g.bodyTransforms.find(-1000000 - xfRef);
                    if (xit != g.bodyTransforms.end()) g.bodyTransforms[bId] = xit->second;
                }
                int32_t lmp = findRefByType(brc, "lump", 0);
                std::set<int32_t> seenLmp;
                while (lmp >= 0 && seenLmp.insert(lmp).second && seenLmp.size() <= 4096) {
                    if (lumpShell.count(lmp)) assignShellChainBody(lumpShell[lmp], bId);
                    lmp = lumpNext.count(lmp) ? lumpNext[lmp] : -1;
                }
                int32_t directSh = findRefByType(brc, "shell", 0);
                if (directSh >= 0) assignShellChainBody(directSh, bId);
                int32_t wr = findRefByType(brc, "wire", 0);
                if (wr >= 0) assignWireChainBody(wr, bId);
            }
            // Also propagate upward from lump.owner_body if lump wasn't reached downward
            for (const auto& kv : lumpBody) {
                if (kv.second >= 0 && lumpShell.count(kv.first)) {
                    assignShellChainBody(lumpShell[kv.first], kv.second);
                }
            }
            // Propagate face.bodyId to edges referenced by face loops
            for (const auto& kv : g.faces) {
                if (kv.second.bodyId < 0) continue;
                for (int32_t lid : kv.second.loopIds) {
                    auto lit = g.loops.find(lid);
                    if (lit == g.loops.end() || lit->second.firstCoedge < 0) continue;
                    int32_t ce0 = lit->second.firstCoedge, ce = ce0;
                    std::set<int32_t> seenCe;
                    while (ce >= 0 && seenCe.insert(ce).second && seenCe.size() <= 4096) {
                        auto cit = g.coedges.find(ce);
                        if (cit == g.coedges.end()) break;
                        auto eit = g.edges.find(cit->second.edgeId);
                        if (eit != g.edges.end() && eit->second.bodyId < 0) eit->second.bodyId = kv.second.bodyId;
                        ce = cit->second.nextCoedge;
                        if (ce == ce0) break;
                    }
                }
            }

            return evaluateBRepGraph(g);
        };

        // 1. Spatial ACIS Binary (.sab — "ACIS BinaryFile" / "ASM BinaryFile ")
        bool hasSabMagic = (size >= 15 && (std::memcmp(data, "ACIS BinaryFile", 15) == 0 ||
                                           std::memcmp(data, "ASM BinaryFile", 14) == 0));
        if (ext == L".sab" || hasSabMagic) {
            if (!hasSabMagic || size < 28) return false;
            size_t magicLen = (std::memcmp(data, "ACIS BinaryFile", 15) == 0 ||
                              (size >= 15 && std::memcmp(data, "ASM BinaryFile ", 15) == 0)) ? 15 : 14;
            if (magicLen + 13 > size) return false;

            int32_t acisVer     = static_cast<int32_t>(DetailMeshSciCad::RdU32LE(data + magicLen + 0));
            int32_t numRecords  = static_cast<int32_t>(DetailMeshSciCad::RdU32LE(data + magicLen + 4));
            int32_t numEntities = static_cast<int32_t>(DetailMeshSciCad::RdU32LE(data + magicLen + 8));
            (void)numEntities;
            if (acisVer < 100 || acisVer > 99999 || numRecords < 0) return false;
            mesh.formatName = L"Spatial ACIS Binary Solid (.sab)";

            auto isPrintableAsciiSpan = [&](size_t off, size_t len) -> bool {
                if (len == 0 || len > 255 || off + len > size) return false;
                for (size_t i = 0; i < len; ++i) {
                    uint8_t c = data[off + i];
                    if (c < 0x20u || c > 0x7Eu) return false;
                }
                return true;
            };

            // ACIS <= 207 uses 4x int32 header (16B); ACIS v208+ / ASM BinaryFile may encode hasHistory as 1B TAG_TRUE/TAG_FALSE (0x0A/0x0B) at +12
            size_t p = magicLen + 16;
            if ((data[magicLen + 12] == 0x0Au || data[magicLen + 12] == 0x0Bu) &&
                magicLen + 15 <= size && data[magicLen + 13] == 0x07u &&
                isPrintableAsciiSpan(magicLen + 15, data[magicLen + 14]))
            {
                p = magicLen + 13;
            } else if (magicLen + 16 > size) {
                return false;
            }

            int headerStringsRem = 3;
            int headerDoublesRem = 3;
            bool inHeader = true;

            std::vector<AcisRawRec> recs;
            AcisRawRec cur;
            int32_t nextSeqIdx = 0;
            int32_t explicitIdx = -1;
            int braceDepth = 0;

            auto recordBoolSense = [&](bool isTrue) {
                if (inHeader) return;
                if (braceDepth == 0) {
                    if (cur.type == "face" || cur.type == "coedge" || cur.type == "edge") {
                        bool hasSense = false;
                        for (const auto& w : cur.words) {
                            if (w == "forward" || w == "reversed") { hasSense = true; break; }
                        }
                        if (!hasSense) cur.words.push_back(isTrue ? "forward" : "reversed");
                    }
                } else if (cur.type.find("surface") != std::string::npos && isTrue && cur.subNums.empty()) {
                    // In ACIS/ASM binary spline-surface subtype, rational flag before degree may be TAG_TRUE
                    cur.subWords.push_back("nurbs");
                }
            };

            auto handleStringToken = [&](std::string s) -> bool {
                if (s == "End-of-ACIS-data" || s == "End-of-ASM-data") return false;
                if (inHeader) {
                    if (headerStringsRem > 0) --headerStringsRem;
                    if (headerDoublesRem <= 0 && headerStringsRem <= 0) inHeader = false;
                } else if (braceDepth == 0) {
                    cur.words.push_back(std::move(s));
                } else {
                    cur.subWords.push_back(std::move(s));
                }
                return true;
            };

            while (p < size && recs.size() < 200000) {
                uint8_t tag = data[p++];
                if (tag == 0x01 || tag == 0x02) { // TAG_UCHAR / TAG_CHAR (1B)
                    if (p + 1 > size) break;
                    p += 1;
                } else if (tag == 0x03) { // TAG_SHORT (2B)
                    if (p + 2 > size) break;
                    int16_t sv16 = ::Explorer2::ByteIO::RdI16LE(data + p);
                    p += 2;
                    if (!inHeader && braceDepth > 0) cur.subNums.push_back(static_cast<double>(sv16));
                } else if (tag == 0x04) { // TAG_LONG (4B int32)
                    if (p + 4 > size) break;
                    int32_t iv = static_cast<int32_t>(DetailMeshSciCad::RdU32LE(data + p));
                    p += 4;
                    if (!inHeader) {
                        if (braceDepth > 0) cur.subNums.push_back(static_cast<double>(iv));
                        else if (cur.type.empty()) explicitIdx = (iv < 0) ? -iv : iv;
                        else cur.nums.push_back(static_cast<double>(iv));
                    }
                } else if (tag == 0x05) { // TAG_FLOAT (4B)
                    if (p + 4 > size) break;
                    double dv = static_cast<double>(DetailMeshSciCad::RdF32LE(data + p));
                    p += 4;
                    if (!inHeader) {
                        if (braceDepth == 0) cur.nums.push_back(dv);
                        else                 cur.subNums.push_back(dv);
                    }
                } else if (tag == 0x06) { // TAG_DOUBLE (8B)
                    if (p + 8 > size) break;
                    double dv = DetailMeshSciCad::RdF64LE(data + p);
                    p += 8;
                    if (inHeader) {
                        if (--headerDoublesRem <= 0 && headerStringsRem <= 0) inHeader = false;
                    } else {
                        if (braceDepth == 0) cur.nums.push_back(dv);
                        else                 cur.subNums.push_back(dv);
                    }
                } else if (tag == 0x07) { // TAG_UTF8_U8
                    if (p >= size) break;
                    uint8_t slen = data[p++];
                    if (p + slen > size) break;
                    std::string s(reinterpret_cast<const char*>(data + p), slen);
                    p += slen;
                    if (!handleStringToken(std::move(s))) break;
                } else if (tag == 0x08) { // TAG_UTF8_U16
                    if (p + 2 > size) break;
                    uint16_t slen = DetailMeshSciCad::RdU16LE(data + p); p += 2;
                    if (p + slen > size) break;
                    std::string s(reinterpret_cast<const char*>(data + p), slen);
                    p += slen;
                    if (!handleStringToken(std::move(s))) break;
                } else if (tag == 0x09) { // TAG_UTF8_U32
                    if (p + 4 > size) break;
                    uint32_t slen = DetailMeshSciCad::RdU32LE(data + p); p += 4;
                    if (slen > 4000000u || p + slen > size) break;
                    std::string s(reinterpret_cast<const char*>(data + p), slen);
                    p += slen;
                    if (!handleStringToken(std::move(s))) break;
                } else if (tag == 0x0A) { // TAG_TRUE (0B)
                    recordBoolSense(true);
                } else if (tag == 0x0B) { // TAG_FALSE (0B)
                    recordBoolSense(false);
                } else if (tag == 0x0C) { // TAG_POINTER ($idx, 4B)
                    if (p + 4 > size) break;
                    int32_t ref = static_cast<int32_t>(DetailMeshSciCad::RdU32LE(data + p));
                    p += 4;
                    inHeader = false;
                    if (braceDepth == 0) cur.ptrs.push_back(ref);
                } else if (tag == 0x0D) { // TAG_IDENT (1B len + chars)
                    if (p >= size) break;
                    uint8_t slen = data[p++];
                    if (slen == 0 || p + slen > size) break;
                    std::string id(reinterpret_cast<const char*>(data + p), slen);
                    p += slen;
                    if (id == "End-of-ACIS-data" || id == "End-of-ASM-data") break;
                    if (id == "asmheader" && inHeader) continue;
                    inHeader = false;
                    if (braceDepth == 0) {
                        if (cur.type.empty()) cur.type = std::move(id);
                        else cur.words.push_back(std::move(id));
                    } else {
                        cur.subWords.push_back(std::move(id));
                    }
                } else if (tag == 0x0E) { // TAG_SUBIDENT (1B len + chars)
                    if (p >= size) break;
                    uint8_t slen = data[p++];
                    if (p + slen > size) break;
                    std::string sub(reinterpret_cast<const char*>(data + p), slen);
                    p += slen;
                    inHeader = false;
                    if (braceDepth == 0) {
                        if (cur.type.empty()) cur.type = std::move(sub);
                        else cur.type += "-" + sub;
                    } else {
                        cur.subWords.push_back(std::move(sub));
                    }
                } else if (tag == 0x0F) { // TAG_SUBTYPE_OPEN '{'
                    inHeader = false;
                    ++braceDepth;
                } else if (tag == 0x10) { // TAG_SUBTYPE_CLOSE '}'
                    if (braceDepth > 0) --braceDepth;
                } else if (tag == 0x11) { // TAG_TERMINATOR '#'
                    if (inHeader) {
                        inHeader = false;
                        cur = AcisRawRec{};
                        explicitIdx = -1;
                        continue;
                    }
                    braceDepth = 0;
                    if (!cur.type.empty() && cur.type != "asmheader") {
                        int32_t idx = (explicitIdx >= 0 && explicitIdx < 200000) ? explicitIdx : nextSeqIdx;
                        nextSeqIdx = std::max(nextSeqIdx, idx + 1);
                        if (static_cast<size_t>(idx) >= recs.size()) recs.resize(static_cast<size_t>(idx) + 1);
                        recs[static_cast<size_t>(idx)] = std::move(cur);
                    } else {
                        ++nextSeqIdx;
                    }
                    cur = AcisRawRec{};
                    explicitIdx = -1;
                } else if (tag == 0x12) { // TAG_UTF16_U8 / short wide string (1B char count * 2B)
                    if (p >= size) break;
                    uint8_t wlen = data[p++];
                    if (p + static_cast<size_t>(wlen) * 2u > size) break;
                    p += static_cast<size_t>(wlen) * 2u;
                } else if (tag == 0x13 || tag == 0x14) { // TAG_POSITION / TAG_VECTOR (3 IEEE-754 doubles = 24B)
                    if (p + 24 > size) break;
                    inHeader = false;
                    double d0 = DetailMeshSciCad::RdF64LE(data + p + 0);
                    double d1 = DetailMeshSciCad::RdF64LE(data + p + 8);
                    double d2 = DetailMeshSciCad::RdF64LE(data + p + 16);
                    p += 24;
                    if (braceDepth == 0) {
                        cur.nums.push_back(d0); cur.nums.push_back(d1); cur.nums.push_back(d2);
                    } else {
                        cur.subNums.push_back(d0); cur.subNums.push_back(d1); cur.subNums.push_back(d2);
                    }
                } else if (tag == 0x15) { // TAG_ENUM (4B int32 enum in ACIS v200+/ASM, or 1B-counted ident in legacy writers)
                    if (p >= size) break;
                    uint8_t candLen = data[p];
                    bool isCountedEnumStr = (candLen >= 2 && candLen <= 32 && p + 1 + candLen < size &&
                                             isPrintableAsciiSpan(p + 1, candLen) &&
                                             data[p + 1 + candLen] >= 0x01u && data[p + 1 + candLen] <= 0x1Fu);
                    if (isCountedEnumStr) {
                        ++p;
                        std::string en(reinterpret_cast<const char*>(data + p), candLen);
                        p += candLen;
                        if (!inHeader) {
                            if (braceDepth == 0) cur.words.push_back(std::move(en));
                            else cur.subWords.push_back(std::move(en));
                        }
                    } else if (p + 4 <= size) {
                        int32_t ev = static_cast<int32_t>(DetailMeshSciCad::RdU32LE(data + p));
                        p += 4;
                        if (!inHeader && braceDepth == 0 && (cur.type == "face" || cur.type == "coedge" || cur.type == "edge")) {
                            bool hasSense = false;
                            for (const auto& w : cur.words) if (w == "forward" || w == "reversed") hasSense = true;
                            if (!hasSense && (ev == 0 || ev == 1)) {
                                cur.words.push_back(ev == 0 ? "forward" : "reversed");
                            }
                        }
                    } else {
                        break;
                    }
                } else if (tag == 0x16) { // TAG_INT64 (8B, ACIS/ASM 64-bit)
                    if (p + 8 > size) break;
                    int64_t iv64 = ::Explorer2::ByteIO::RdI64LE(data + p);
                    p += 8;
                    if (!inHeader) {
                        if (braceDepth > 0) cur.subNums.push_back(static_cast<double>(iv64));
                        else cur.nums.push_back(static_cast<double>(iv64));
                    }
                } else if (tag == 0x17) { // TAG_SUBTYPE_REF / entity subtype ID (4B)
                    if (p + 4 > size) break;
                    p += 4;
                } else if (tag == 0x18) { // TAG_UTF16_U16 (2B char count * 2B)
                    if (p + 2 > size) break;
                    uint16_t wlen = DetailMeshSciCad::RdU16LE(data + p); p += 2;
                    if (p + static_cast<size_t>(wlen) * 2u > size) break;
                    p += static_cast<size_t>(wlen) * 2u;
                } else if (tag == 0x19) { // TAG_UTF16_U32 (4B char count * 2B)
                    if (p + 4 > size) break;
                    uint32_t wlen = DetailMeshSciCad::RdU32LE(data + p); p += 4;
                    if (wlen > 2000000u || p + static_cast<size_t>(wlen) * 2u > size) break;
                    p += static_cast<size_t>(wlen) * 2u;
                } else if (tag == 0x1A) { // TAG_TRANSFORM_IDENTITY / zero-byte marker
                    // 0-byte tag
                } else if (tag >= 0x1B && tag <= 0x1F) { // Extended ASM v220+ 4-byte numeric/flag tags
                    if (p + 4 > size) break;
                    p += 4;
                } else {
                    break;
                }
            }
            return buildAcisGraphAndTriangulate(recs);
        }

        // 2. Spatial ACIS ASCII (.sat)
        if (ext == L".sat") {
            // Reject binary / NUL-containing files
            for (size_t i = 0; i < std::min<size_t>(size, 512); ++i) {
                if (data[i] == 0) return false;
            }
            size_t pos = 0;
            auto skipWs = [&]() {
                while (pos < sv.size() && (sv[pos] == ' ' || sv[pos] == '\t' || sv[pos] == '\r' || sv[pos] == '\n')) ++pos;
            };
            skipWs();
            if (pos >= sv.size()) return false;

            // Validate ACIS ASCII header: either "<version 100..99999> <numRec> <numEnt> <flags>" OR "ACIS ..." / "ASM ..."
            bool validHeader = false;
            if (sv[pos] >= '1' && sv[pos] <= '9') {
                size_t nl1 = sv.find('\n', pos);
                std::string_view l1 = sv.substr(pos, (nl1 == std::string_view::npos ? sv.size() : nl1) - pos);
                std::string_view rem = l1, tVer, tRec, tEnt, tFlg;
                int64_t verInt = -1, nRec = -1, nEnt = -1, hFlg = -1;
                if (ReaderText::NextTokenSv(rem, tVer) && ReaderText::ParseInt64Sv(tVer, verInt) &&
                    ReaderText::NextTokenSv(rem, tRec) && ReaderText::ParseInt64Sv(tRec, nRec) &&
                    ReaderText::NextTokenSv(rem, tEnt) && ReaderText::ParseInt64Sv(tEnt, nEnt) &&
                    ReaderText::NextTokenSv(rem, tFlg) && ReaderText::ParseInt64Sv(tFlg, hFlg) &&
                    verInt >= 100 && verInt <= 99999 && nRec >= 0)
                {
                    (void)nEnt; (void)hFlg;
                    validHeader = true;
                    // Advance past header line 1 (counts), plus optional line 2 (product strings) & line 3 (units/tolerances)
                    pos = (nl1 == std::string_view::npos) ? sv.size() : (nl1 + 1);
                    for (int hLine = 0; hLine < 2 && pos < sv.size(); ++hLine) {
                        size_t nl = sv.find('\n', pos);
                        std::string_view hSv = sv.substr(pos, (nl == std::string_view::npos ? sv.size() : nl) - pos);
                        if (hSv.find('#') != std::string_view::npos || hSv.find('$') != std::string_view::npos) break;
                        pos = (nl == std::string_view::npos) ? sv.size() : (nl + 1);
                    }
                }
            } else if (sv.substr(pos).starts_with("ACIS") || sv.substr(pos).starts_with("ASM")) {
                validHeader = true;
                size_t nl = sv.find('\n', pos);
                pos = (nl == std::string_view::npos) ? sv.size() : (nl + 1);
                // Skip optional unit/tolerance header line if it does not contain '#'
                skipWs();
                if (pos < sv.size()) {
                    size_t nl2 = sv.find('\n', pos);
                    std::string_view l2 = sv.substr(pos, (nl2 == std::string_view::npos ? sv.size() : nl2) - pos);
                    if (l2.find('#') == std::string_view::npos && l2.find('$') == std::string_view::npos) {
                        pos = (nl2 == std::string_view::npos) ? sv.size() : (nl2 + 1);
                    }
                }
            }
            if (!validHeader || pos >= sv.size() || sv.find('#', pos) == std::string_view::npos) return false;

            mesh.formatName = L"Spatial ACIS Solid Model (.sat)";

            std::vector<AcisRawRec> recs;
            AcisRawRec cur;
            int32_t nextSeqIdx = 0;
            int32_t explicitIdx = -1;
            bool atRecordStart = true;
            int braceDepth = 0;

            auto isAcisWordChar = [](char c) -> bool {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '-' || c == '_';
            };
            auto isAcisDelim = [](char c) -> bool {
                return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '#' || c == '{' || c == '}';
            };

            while (pos < sv.size() && recs.size() < 200000) {
                skipWs();
                if (pos >= sv.size()) break;
                char ch = sv[pos];

                if (ch == '#') {
                    ++pos;
                    braceDepth = 0;
                    if (!cur.type.empty()) {
                        int32_t idx = (explicitIdx >= 0 && explicitIdx < 200000) ? explicitIdx : nextSeqIdx;
                        nextSeqIdx = std::max(nextSeqIdx, idx + 1);
                        if (static_cast<size_t>(idx) >= recs.size()) recs.resize(static_cast<size_t>(idx) + 1);
                        recs[static_cast<size_t>(idx)] = std::move(cur);
                    }
                    cur = AcisRawRec{};
                    explicitIdx = -1;
                    atRecordStart = true;
                    continue;
                }
                if (ch == '{') { ++braceDepth; ++pos; continue; }
                if (ch == '}') { if (braceDepth > 0) --braceDepth; ++pos; continue; }

                // Counted string literal: @<len> <chars>
                if (ch == '@') {
                    ++pos;
                    size_t dEnd = pos;
                    while (dEnd < sv.size() && sv[dEnd] >= '0' && sv[dEnd] <= '9') ++dEnd;
                    int64_t slen = -1;
                    if (dEnd > pos && ReaderText::ParseInt64Sv(sv.substr(pos, dEnd - pos), slen) && slen >= 0) {
                        pos = dEnd;
                        if (pos < sv.size() && (sv[pos] == ' ' || sv[pos] == '\t')) ++pos;
                        size_t adv = std::min<size_t>(static_cast<size_t>(std::min<int64_t>(slen, 1000000)), sv.size() - pos);
                        std::string lit(sv.substr(pos, adv));
                        pos += adv;
                        if (braceDepth == 0 && (lit == "forward" || lit == "reversed")) {
                            cur.words.push_back(std::move(lit));
                        }
                        atRecordStart = false;
                        continue;
                    }
                }

                // Entity pointer reference: $<int>
                if (ch == '$') {
                    ++pos;
                    size_t dEnd = pos;
                    if (dEnd < sv.size() && (sv[dEnd] == '-' || sv[dEnd] == '+')) ++dEnd;
                    while (dEnd < sv.size() && sv[dEnd] >= '0' && sv[dEnd] <= '9') ++dEnd;
                    int64_t ref = -1;
                    if (dEnd > pos && ReaderText::ParseInt64Sv(sv.substr(pos, dEnd - pos), ref) &&
                        ref >= -2147483647LL && ref <= 2147483647LL)
                    {
                        pos = dEnd;
                        if (braceDepth == 0) cur.ptrs.push_back(static_cast<int32_t>(ref));
                    }
                    atRecordStart = false;
                    continue;
                }

                // Explicit "-<idx>" record number at start of record (e.g. "-0 body ...", "-12 face ...")
                if (atRecordStart && braceDepth == 0 && ch == '-' && pos + 1 < sv.size() &&
                    sv[pos + 1] >= '0' && sv[pos + 1] <= '9')
                {
                    size_t dEnd = pos + 1;
                    while (dEnd < sv.size() && sv[dEnd] >= '0' && sv[dEnd] <= '9') ++dEnd;
                    int64_t rIdx = -1;
                    if (dEnd == sv.size() || sv[dEnd] == ' ' || sv[dEnd] == '\t' || sv[dEnd] == '\r' || sv[dEnd] == '\n') {
                        if (ReaderText::ParseInt64Sv(sv.substr(pos + 1, dEnd - (pos + 1)), rIdx) &&
                            rIdx >= 0 && rIdx < 200000)
                        {
                            explicitIdx = static_cast<int32_t>(rIdx);
                            pos = dEnd;
                            atRecordStart = false;
                            continue;
                        }
                    }
                }

                // Number token (float / int)
                if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.') {
                    size_t tokEnd = pos;
                    while (tokEnd < sv.size() && !isAcisDelim(sv[tokEnd])) ++tokEnd;
                    double dv = 0.0;
                    if (tokEnd > pos && ReaderText::ParseDoubleSv(sv.substr(pos, tokEnd - pos), dv)) {
                        pos = tokEnd;
                        if (std::isfinite(dv)) {
                            if (braceDepth == 0) cur.nums.push_back(dv);
                            else                 cur.subNums.push_back(dv);
                        }
                        atRecordStart = false;
                        continue;
                    }
                }

                // Identifier / keyword token
                if (isAcisWordChar(ch)) {
                    size_t start = pos;
                    while (pos < sv.size() && isAcisWordChar(sv[pos])) ++pos;
                    std::string tok(sv.substr(start, pos - start));
                    if (tok == "End-of-ACIS-data" || tok == "End-of-ASM-data") break;
                    if (braceDepth == 0) {
                        if (cur.type.empty()) cur.type = std::move(tok);
                        else cur.words.push_back(std::move(tok));
                    } else {
                        cur.subWords.push_back(std::move(tok));
                    }
                    atRecordStart = false;
                    continue;
                }

                ++pos;
            }
            return buildAcisGraphAndTriangulate(recs);
        }
    return false;
}

} // namespace Formats
