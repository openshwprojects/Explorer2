#pragma once
// IfcBim.h — Industry Foundation Classes (.ifc, ISO 10303-21) BIM 3D Geometry Decoder.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// ============================================================================
// 63. Industry Foundation Classes (.ifc — ISO 10303-21 BIM Geometry Decoder)
// ============================================================================
inline bool ParseIfcBimMesh(std::string_view sv, LoadedMesh3D& mesh) {
    using namespace DetailStep;
    using DetailScad::Mat4;

    if (sv.find("ISO-10303-21") == std::string_view::npos && sv.find("IFC") == std::string_view::npos) {
        return false;
    }
    mesh.formatName = L"IFC Building Information Model (.ifc)";

    // 1. Parse #id = IFCENTITY(...); statements across DATA; ... ENDSEC;
    struct IfcEnt {
        std::string type;
        std::string_view params;
    };
    std::unordered_map<int, IfcEnt> ents;
    ents.reserve(1024);

    size_t pos = 0;
    size_t dataPos = sv.find("DATA;");
    if (dataPos != std::string_view::npos) pos = dataPos + 5;

    while (pos < sv.size() && ents.size() < 500000) {
        while (pos < sv.size()) {
            unsigned char ch = static_cast<unsigned char>(sv[pos]);
            if (ch <= 32) { ++pos; continue; }
            if (ch == '/' && pos + 1 < sv.size() && sv[pos + 1] == '*') {
                size_t ce = sv.find("*/", pos + 2);
                pos = (ce == std::string_view::npos) ? sv.size() : (ce + 2);
                continue;
            }
            break;
        }
        if (pos >= sv.size()) break;
        if (sv.compare(pos, 7, "ENDSEC;") == 0) break;

        size_t st = pos;
        bool inStr = false;
        while (pos < sv.size()) {
            char c = sv[pos];
            if (inStr) {
                if (c == '\'') {
                    if (pos + 1 < sv.size() && sv[pos + 1] == '\'') pos += 2;
                    else { inStr = false; ++pos; }
                } else ++pos;
                continue;
            }
            if (c == '\'') { inStr = true; ++pos; continue; }
            if (c == '/' && pos + 1 < sv.size() && sv[pos + 1] == '*') {
                size_t ce = sv.find("*/", pos + 2);
                pos = (ce == std::string_view::npos) ? sv.size() : (ce + 2);
                continue;
            }
            if (c == ';') break;
            ++pos;
        }
        std::string_view stmt = TrimSv(sv.substr(st, pos - st));
        if (pos < sv.size() && sv[pos] == ';') ++pos;

        if (stmt.size() < 6 || stmt[0] != '#') continue;
        size_t eq = stmt.find('=');
        if (eq == std::string_view::npos) continue;
        int id = ParseRefId(stmt.substr(0, eq));
        if (id <= 0) continue;
        std::string_view rhs = TrimSv(stmt.substr(eq + 1));
        size_t lp = rhs.find('(');
        if (lp == std::string_view::npos) continue;
        std::string tname = UpperAscii(rhs.substr(0, lp));
        if (!tname.empty()) {
            ents[id] = { std::move(tname), TrimSv(rhs.substr(lp)) };
        }
    }
    if (ents.empty()) return false;

    // 2. Coordinate, Direction, Axis2Placement & LocalPlacement Evaluators
    std::unordered_map<int, Vec3f> ptCache;
    std::unordered_map<int, Vec3f> dirCache;
    std::unordered_map<int, Mat4> matCache;

    auto getPt = [&](int id, Vec3f& out) -> bool {
        if (id <= 0) return false;
        auto cit = ptCache.find(id);
        if (cit != ptCache.end()) { out = cit->second; return true; }
        auto it = ents.find(id);
        if (it == ents.end() || it->second.type != "IFCCARTESIANPOINT") return false;
        std::vector<std::string_view> args;
        SplitStepArgs(it->second.params, args);
        if (args.empty()) return false;
        std::vector<double> c;
        ParseFloatTuple(args[0], c);
        if (c.size() < 2) return false;
        out = { static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c.size() >= 3 ? c[2] : 0.0) };
        ptCache[id] = out;
        return true;
    };

    auto getDir = [&](int id, Vec3f& out) -> bool {
        if (id <= 0) return false;
        auto cit = dirCache.find(id);
        if (cit != dirCache.end()) { out = cit->second; return true; }
        auto it = ents.find(id);
        if (it == ents.end() || it->second.type != "IFCDIRECTION") return false;
        std::vector<std::string_view> args;
        SplitStepArgs(it->second.params, args);
        if (args.empty()) return false;
        std::vector<double> c;
        ParseFloatTuple(args[0], c);
        if (c.size() < 2) return false;
        out = StepNorm({ static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c.size() >= 3 ? c[2] : 0.0) });
        dirCache[id] = out;
        return true;
    };

    auto buildBasisMat = [](Vec3f origin, Vec3f xAx, Vec3f yAx, Vec3f zAx) -> Mat4 {
        Mat4 m;
        m.m[0] = xAx.x; m.m[1] = yAx.x; m.m[2]  = zAx.x; m.m[3]  = origin.x;
        m.m[4] = xAx.y; m.m[5] = yAx.y; m.m[6]  = zAx.y; m.m[7]  = origin.y;
        m.m[8] = xAx.z; m.m[9] = yAx.z; m.m[10] = zAx.z; m.m[11] = origin.z;
        return m;
    };

    std::function<Mat4(int, int)> getTransform = [&](int id, int depth) -> Mat4 {
        if (id <= 0 || depth > 24) return Mat4::Identity();
        auto cit = matCache.find(id);
        if (cit != matCache.end()) return cit->second;
        auto it = ents.find(id);
        if (it == ents.end()) return Mat4::Identity();
        const auto& e = it->second;
        std::vector<std::string_view> args;
        SplitStepArgs(e.params, args);

        Mat4 res = Mat4::Identity();
        if (e.type == "IFCAXIS2PLACEMENT3D") {
            Vec3f org = { 0, 0, 0 }, zAx = { 0, 0, 1 };
            if (!args.empty()) getPt(ParseRefId(args[0]), org);
            if (args.size() >= 2) getDir(ParseRefId(args[1]), zAx);
            zAx = StepNorm(zAx, { 0, 0, 1 });
            Vec3f ref = (std::fabs(zAx.z) < 0.9f) ? Vec3f{ 0, 0, 1 } : Vec3f{ 1, 0, 0 };
            if (args.size() >= 3) {
                Vec3f rd;
                if (getDir(ParseRefId(args[2]), rd)) ref = rd;
            }
            Vec3f proj = StepSub(ref, StepScale(zAx, StepDot(ref, zAx)));
            if (StepLen(proj) < 1e-6f) {
                Vec3f alt = (std::fabs(zAx.x) < 0.9f) ? Vec3f{ 1, 0, 0 } : Vec3f{ 0, 1, 0 };
                proj = StepSub(alt, StepScale(zAx, StepDot(alt, zAx)));
            }
            Vec3f xAx = StepNorm(proj, { 1, 0, 0 });
            Vec3f yAx = StepNorm(StepCross(zAx, xAx), { 0, 1, 0 });
            res = buildBasisMat(org, xAx, yAx, zAx);
        } else if (e.type == "IFCAXIS2PLACEMENT2D") {
            Vec3f org = { 0, 0, 0 }, xAx = { 1, 0, 0 };
            if (!args.empty()) getPt(ParseRefId(args[0]), org);
            if (args.size() >= 2) {
                Vec3f rd;
                if (getDir(ParseRefId(args[1]), rd)) xAx = StepNorm({ rd.x, rd.y, 0.0f }, { 1, 0, 0 });
            }
            Vec3f yAx = { -xAx.y, xAx.x, 0.0f };
            res = buildBasisMat({ org.x, org.y, 0.0f }, xAx, yAx, { 0, 0, 1 });
        } else if (e.type == "IFCLOCALPLACEMENT") {
            Mat4 parentMat = (args.size() >= 1) ? getTransform(ParseRefId(args[0]), depth + 1) : Mat4::Identity();
            Mat4 relMat    = (args.size() >= 2) ? getTransform(ParseRefId(args[1]), depth + 1) : Mat4::Identity();
            res = Mat4::Mul(parentMat, relMat);
        } else if (e.type.find("IFCCARTESIANTRANSFORMATIONOPERATOR3D") == 0) {
            // (Axis1, Axis2, LocalOrigin, Scale, Axis3 [, Scale2, Scale3])
            Vec3f xAx = { 1, 0, 0 }, yAx = { 0, 1, 0 }, zAx = { 0, 0, 1 }, org = { 0, 0, 0 };
            if (args.size() >= 1) getDir(ParseRefId(args[0]), xAx);
            if (args.size() >= 2) getDir(ParseRefId(args[1]), yAx);
            if (args.size() >= 3) getPt(ParseRefId(args[2]), org);
            float s1 = (args.size() >= 4) ? static_cast<float>(ParseStepDouble(args[3], 1.0)) : 1.0f;
            if (s1 <= 0.0f) s1 = 1.0f;
            if (args.size() >= 5 && ! getDir(ParseRefId(args[4]), zAx)) zAx = StepNorm(StepCross(xAx, yAx), { 0, 0, 1 });
            float s2 = (args.size() >= 6) ? static_cast<float>(ParseStepDouble(args[5], s1)) : s1;
            float s3 = (args.size() >= 7) ? static_cast<float>(ParseStepDouble(args[6], s1)) : s1;
            if (s2 <= 0.0f) s2 = s1;
            if (s3 <= 0.0f) s3 = s1;
            res = buildBasisMat(org, StepScale(xAx, s1), StepScale(yAx, s2), StepScale(zAx, s3));
        }
        matCache[id] = res;
        return res;
    };

    // 3. Point lists (IFCCARTESIANPOINTLIST3D & IFCCARTESIANPOINTLIST2D)
    std::unordered_map<int, std::vector<Vec3f>> ptLists;
    auto getPointList = [&](int id) -> const std::vector<Vec3f>* {
        auto cit = ptLists.find(id);
        if (cit != ptLists.end()) return &cit->second;
        auto it = ents.find(id);
        if (it == ents.end() || (it->second.type != "IFCCARTESIANPOINTLIST3D" && it->second.type != "IFCCARTESIANPOINTLIST2D")) return nullptr;
        std::vector<std::string_view> args;
        SplitStepArgs(it->second.params, args);
        if (args.empty()) return nullptr;
        std::vector<std::string_view> tuples;
        SplitStepArgs(args[0], tuples);
        std::vector<Vec3f> pts;
        pts.reserve(tuples.size());
        for (auto t : tuples) {
            std::vector<double> c;
            ParseFloatTuple(t, c);
            if (c.size() >= 2) {
                pts.push_back({ static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c.size() >= 3 ? c[2] : 0.0) });
            }
        }
        ptLists[id] = std::move(pts);
        return &ptLists[id];
    };

    // 4. 2D Curve & Profile Evaluators (for IFCEXTRUDEDAREASOLID)
    std::function<std::vector<std::pair<float, float>>(int, int)> evalCurve2D = [&](int cid, int depth) -> std::vector<std::pair<float, float>> {
        std::vector<std::pair<float, float>> out;
        if (cid <= 0 || depth > 12) return out;
        auto it = ents.find(cid);
        if (it == ents.end()) return out;
        const auto& ce = it->second;
        std::vector<std::string_view> args;
        SplitStepArgs(ce.params, args);

        if (ce.type == "IFCPOLYLINE" && !args.empty()) {
            std::vector<int> refs;
            ParseRefList(args[0], refs);
            for (int r : refs) {
                Vec3f p;
                if (getPt(r, p)) out.push_back({ p.x, p.y });
            }
        } else if (ce.type == "IFCINDEXEDPOLYCURVE" && !args.empty()) {
            const auto* plist = getPointList(ParseRefId(args[0]));
            if (plist) {
                for (const auto& p : *plist) out.push_back({ p.x, p.y });
            }
        } else if (ce.type == "IFCCOMPOSITECURVE" && !args.empty()) {
            std::vector<int> segIds;
            ParseRefList(args[0], segIds);
            for (int sid : segIds) {
                auto sit = ents.find(sid);
                if (sit == ents.end()) continue;
                std::vector<std::string_view> sArgs;
                SplitStepArgs(sit->second.params, sArgs);
                if (sit->second.type == "IFCCOMPOSITECURVESEGMENT" && sArgs.size() >= 3) {
                    bool sameSense = ParseStepBool(sArgs[1], true);
                    auto sub = evalCurve2D(ParseRefId(sArgs[2]), depth + 1);
                    if (!sameSense) std::reverse(sub.begin(), sub.end());
                    for (const auto& p : sub) {
                        if (out.empty() || std::hypot(p.first - out.back().first, p.second - out.back().second) > 1e-5f) {
                            out.push_back(p);
                        }
                    }
                }
            }
        } else if (ce.type == "IFCTRIMMEDCURVE" && args.size() >= 4) {
            int basisId = ParseRefId(args[0]);
            auto bit = ents.find(basisId);
            if (bit != ents.end() && bit->second.type == "IFCCIRCLE") {
                std::vector<std::string_view> cArgs;
                SplitStepArgs(bit->second.params, cArgs);
                if (cArgs.size() >= 2) {
                    Mat4 cMat = getTransform(ParseRefId(cArgs[0]), 0);
                    float rad = static_cast<float>(ParseStepDouble(cArgs[1], 1.0));
                    double p1 = ParseStepDouble(args[1], 0.0) * 3.141592653589793 / 180.0;
                    double p2 = ParseStepDouble(args[2], 360.0) * 3.141592653589793 / 180.0;
                    bool sense = ParseStepBool(args[3], true);
                    double delta = p2 - p1;
                    if (sense) { while (delta <= 1e-5) delta += 6.283185307179586; }
                    else       { while (delta >= -1e-5) delta -= 6.283185307179586; }
                    for (int k = 0; k <= 12; ++k) {
                        double a = p1 + delta * (static_cast<double>(k) / 12.0);
                        Vec3f pt = cMat.TransformPoint({ static_cast<float>(std::cos(a)) * rad, static_cast<float>(std::sin(a)) * rad, 0.0f });
                        out.push_back({ pt.x, pt.y });
                    }
                }
            } else {
                Vec3f t1, t2;
                int r1 = ParseRefId(args[1]), r2 = ParseRefId(args[2]);
                if (getPt(r1, t1) && getPt(r2, t2)) {
                    out.push_back({ t1.x, t1.y });
                    out.push_back({ t2.x, t2.y });
                }
            }
        }
        if (out.size() >= 2 && std::hypot(out.front().first - out.back().first, out.front().second - out.back().second) <= 1e-4f) {
            out.pop_back();
        }
        return out;
    };

    struct Profile2D {
        std::vector<std::pair<float, float>> outer;
        std::vector<std::vector<std::pair<float, float>>> voids;
    };

    auto evalProfile2D = [&](int profId) -> Profile2D {
        Profile2D prof;
        auto it = ents.find(profId);
        if (it == ents.end()) return prof;
        const auto& pe = it->second;
        std::vector<std::string_view> args;
        SplitStepArgs(pe.params, args);

        Mat4 pos2D = (args.size() >= 3) ? getTransform(ParseRefId(args[2]), 0) : Mat4::Identity();
        auto xformRing = [&](std::vector<std::pair<float, float>>& ring) {
            for (auto& pt : ring) {
                Vec3f p3 = pos2D.TransformPoint({ pt.first, pt.second, 0.0f });
                pt = { p3.x, p3.y };
            }
        };

        if ((pe.type == "IFCRECTANGLEPROFILEDEF" || pe.type == "IFCRECTANGLEHOLLOWPROFILEDEF") && args.size() >= 5) {
            float hx = 0.5f * static_cast<float>(ParseStepDouble(args[3], 1.0));
            float hy = 0.5f * static_cast<float>(ParseStepDouble(args[4], 1.0));
            if (hx > 1e-6f && hy > 1e-6f) {
                prof.outer = { {-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy} };
                xformRing(prof.outer);
                if (pe.type == "IFCRECTANGLEHOLLOWPROFILEDEF" && args.size() >= 6) {
                    float wt = static_cast<float>(ParseStepDouble(args[5], 0.0));
                    if (wt > 1e-6f && hx - wt > 1e-5f && hy - wt > 1e-5f) {
                        std::vector<std::pair<float, float>> inner = {
                            {-(hx - wt), -(hy - wt)}, {hx - wt, -(hy - wt)}, {hx - wt, hy - wt}, {-(hx - wt), hy - wt}
                        };
                        xformRing(inner);
                        prof.voids.push_back(std::move(inner));
                    }
                }
            }
        } else if ((pe.type == "IFCCIRCLEPROFILEDEF" || pe.type == "IFCCIRCLEHOLLOWPROFILEDEF") && args.size() >= 4) {
            float rad = static_cast<float>(ParseStepDouble(args[3], 1.0));
            if (rad > 1e-6f) {
                const int segs = 20;
                for (int i = 0; i < segs; ++i) {
                    float a = static_cast<float>(i) * 6.2831853f / static_cast<float>(segs);
                    prof.outer.push_back({ std::cos(a) * rad, std::sin(a) * rad });
                }
                xformRing(prof.outer);
                if (pe.type == "IFCCIRCLEHOLLOWPROFILEDEF" && args.size() >= 5) {
                    float wt = static_cast<float>(ParseStepDouble(args[4], 0.0));
                    float ir = rad - wt;
                    if (wt > 1e-6f && ir > 1e-5f) {
                        std::vector<std::pair<float, float>> inner;
                        for (int i = 0; i < segs; ++i) {
                            float a = static_cast<float>(i) * 6.2831853f / static_cast<float>(segs);
                            inner.push_back({ std::cos(a) * ir, std::sin(a) * ir });
                        }
                        xformRing(inner);
                        prof.voids.push_back(std::move(inner));
                    }
                }
            }
        } else if (pe.type == "IFCISHAPEPROFILEDEF" && args.size() >= 7) {
            float hw = 0.5f * static_cast<float>(ParseStepDouble(args[3], 1.0));
            float hd = 0.5f * static_cast<float>(ParseStepDouble(args[4], 1.0));
            float hweb = 0.5f * static_cast<float>(ParseStepDouble(args[5], 0.1));
            float tf = static_cast<float>(ParseStepDouble(args[6], 0.1));
            if (hw > hweb && hd > tf) {
                prof.outer = {
                    {-hw, -hd}, {hw, -hd}, {hw, -hd + tf}, {hweb, -hd + tf},
                    {hweb, hd - tf}, {hw, hd - tf}, {hw, hd}, {-hw, hd},
                    {-hw, hd - tf}, {-hweb, hd - tf}, {-hweb, -hd + tf}, {-hw, -hd + tf}
                };
                xformRing(prof.outer);
            }
        } else if ((pe.type == "IFCARBITRARYCLOSEDPROFILEDEF" || pe.type == "IFCARBITRARYPROFILEDEFWITHVOIDS") && args.size() >= 3) {
            prof.outer = evalCurve2D(ParseRefId(args[2]), 0);
            if (pe.type == "IFCARBITRARYPROFILEDEFWITHVOIDS" && args.size() >= 4) {
                std::vector<int> voidCurveIds;
                ParseRefList(args[3], voidCurveIds);
                for (int vid : voidCurveIds) {
                    auto vr = evalCurve2D(vid, 0);
                    if (vr.size() >= 3) prof.voids.push_back(std::move(vr));
                }
            }
        }
        return prof;
    };

    // 5. Representation Geometry Item Evaluator (in IFC Z-up space)
    struct IfcGeomPart {
        std::vector<Vec3f> verts;
        std::vector<MeshTriangle> tris;
    };
    std::unordered_map<int, IfcGeomPart> itemCache;
    std::set<int> consumedInHigherEntity;

    auto appendFaceRingToPart = [](IfcGeomPart& part, const std::vector<Vec3f>& outer, const std::vector<std::vector<Vec3f>>& holes, bool rev) {
        if (outer.size() < 3) return;
        Vec3f nrm = { 0, 0, 0 };
        for (size_t i = 0; i < outer.size(); ++i) {
            const Vec3f& c = outer[i];
            const Vec3f& n = outer[(i + 1) % outer.size()];
            nrm.x += (c.y - n.y) * (c.z + n.z);
            nrm.y += (c.z - n.z) * (c.x + n.x);
            nrm.z += (c.x - n.x) * (c.y + n.y);
        }
        nrm = StepNorm(nrm, { 0, 0, 1 });
        Vec3f ref = (std::fabs(nrm.z) < 0.9f) ? Vec3f{ 0, 0, 1 } : Vec3f{ 1, 0, 0 };
        Vec3f uAx = StepNorm(StepCross(ref, nrm), { 1, 0, 0 });
        Vec3f vAx = StepCross(nrm, uAx);
        Vec3f org = outer[0];

        std::vector<std::pair<float, float>> o2;
        std::vector<uint32_t> oi;
        for (const auto& p : outer) {
            Vec3f d = StepSub(p, org);
            o2.push_back({ StepDot(d, uAx), StepDot(d, vAx) });
            oi.push_back(static_cast<uint32_t>(part.verts.size()));
            part.verts.push_back(p);
        }
        std::vector<std::vector<std::pair<float, float>>> h2;
        std::vector<std::vector<uint32_t>> hi;
        for (const auto& h : holes) {
            if (h.size() < 3) continue;
            std::vector<std::pair<float, float>> r2;
            std::vector<uint32_t> ri;
            for (const auto& p : h) {
                Vec3f d = StepSub(p, org);
                r2.push_back({ StepDot(d, uAx), StepDot(d, vAx) });
                ri.push_back(static_cast<uint32_t>(part.verts.size()));
                part.verts.push_back(p);
            }
            h2.push_back(std::move(r2));
            hi.push_back(std::move(ri));
        }
        EarClip2DWithHoles(o2, oi, h2, hi, rev, part.tris);
    };

    auto evalIfcFace = [&](int faceId, IfcGeomPart& outPart) {
        auto fit = ents.find(faceId);
        if (fit == ents.end() || (fit->second.type != "IFCFACE" && fit->second.type != "IFCFACESURFACE")) return;
        consumedInHigherEntity.insert(faceId);
        std::vector<std::string_view> fArgs;
        SplitStepArgs(fit->second.params, fArgs);
        if (fArgs.empty()) return;
        std::vector<int> boundIds;
        ParseRefList(fArgs[0], boundIds);

        std::vector<Vec3f> outer;
        std::vector<std::vector<Vec3f>> holes;
        for (int bid : boundIds) {
            auto bit = ents.find(bid);
            if (bit == ents.end()) continue;
            consumedInHigherEntity.insert(bid);
            std::vector<std::string_view> bArgs;
            SplitStepArgs(bit->second.params, bArgs);
            if (bArgs.empty()) continue;
            int loopId = ParseRefId(bArgs[0]);
            bool ori = (bArgs.size() >= 2) ? ParseStepBool(bArgs[1], true) : true;
            auto lit = ents.find(loopId);
            if (lit == ents.end() || lit->second.type != "IFCPOLYLOOP") continue;
            consumedInHigherEntity.insert(loopId);
            std::vector<std::string_view> lArgs;
            SplitStepArgs(lit->second.params, lArgs);
            if (lArgs.empty()) continue;
            std::vector<int> ptIds;
            ParseRefList(lArgs[0], ptIds);
            std::vector<Vec3f> ring;
            for (int pid : ptIds) {
                Vec3f p;
                if (getPt(pid, p)) ring.push_back(p);
            }
            if (!ori) std::reverse(ring.begin(), ring.end());
            if (ring.size() < 3) continue;
            if ((bit->second.type == "IFCFACEOUTERBOUND" || outer.empty()) && outer.empty()) outer = std::move(ring);
            else holes.push_back(std::move(ring));
        }
        appendFaceRingToPart(outPart, outer, holes, false);
    };

    std::function<const IfcGeomPart*(int, int)> evalGeomItem = [&](int itemId, int depth) -> const IfcGeomPart* {
        if (itemId <= 0 || depth > 20) return nullptr;
        auto cit = itemCache.find(itemId);
        if (cit != itemCache.end()) return &cit->second;
        auto it = ents.find(itemId);
        if (it == ents.end()) return nullptr;
        const auto& ie = it->second;
        std::vector<std::string_view> args;
        SplitStepArgs(ie.params, args);

        IfcGeomPart part;

        if (ie.type == "IFCFACETEDBREP" || ie.type == "IFCFACETEDBREPWITHVOIDS" ||
            ie.type == "IFCSHELLBASEDSURFACEMODEL" || ie.type == "IFCFACEBASEDSURFACEMODEL")
        {
            consumedInHigherEntity.insert(itemId);
            std::vector<int> shellRefs;
            for (auto a : args) {
                std::vector<int> sub;
                ParseRefList(a, sub);
                shellRefs.insert(shellRefs.end(), sub.begin(), sub.end());
            }
            for (int sid : shellRefs) {
                auto sit = ents.find(sid);
                if (sit == ents.end()) continue;
                consumedInHigherEntity.insert(sid);
                if (sit->second.type == "IFCFACE") {
                    evalIfcFace(sid, part);
                } else {
                    std::vector<std::string_view> sArgs;
                    SplitStepArgs(sit->second.params, sArgs);
                    if (!sArgs.empty()) {
                        std::vector<int> faceIds;
                        ParseRefList(sArgs[0], faceIds);
                        for (int fid : faceIds) evalIfcFace(fid, part);
                    }
                }
            }
        } else if (ie.type == "IFCCLOSEDSHELL" || ie.type == "IFCOPENSHELL" || ie.type == "IFCCONNECTEDFACESET") {
            consumedInHigherEntity.insert(itemId);
            if (!args.empty()) {
                std::vector<int> faceIds;
                ParseRefList(args[0], faceIds);
                for (int fid : faceIds) evalIfcFace(fid, part);
            }
        } else if (ie.type == "IFCTRIANGULATEDFACESET" && args.size() >= 4) {
            consumedInHigherEntity.insert(itemId);
            const auto* plist = getPointList(ParseRefId(args[0]));
            if (plist && !plist->empty()) {
                part.verts = *plist;
                std::vector<double> pnIndex;
                if (args.size() >= 5 && !args[4].empty() && args[4].front() == '(') ParseFloatTuple(args[4], pnIndex);
                auto mapV = [&](int oneBased) -> int {
                    int r = oneBased - 1;
                    if (!pnIndex.empty()) {
                        if (r < 0 || static_cast<size_t>(r) >= pnIndex.size()) return -1;
                        r = static_cast<int>(pnIndex[r]) - 1;
                    }
                    return (r >= 0 && static_cast<size_t>(r) < part.verts.size()) ? r : -1;
                };
                std::vector<std::string_view> trisSv;
                SplitStepArgs(args[3], trisSv);
                for (auto ts : trisSv) {
                    std::vector<double> ti;
                    ParseFloatTuple(ts, ti);
                    if (ti.size() >= 3) {
                        int i0 = mapV(static_cast<int>(ti[0]));
                        int i1 = mapV(static_cast<int>(ti[1]));
                        int i2 = mapV(static_cast<int>(ti[2]));
                        if (i0 >= 0 && i1 >= 0 && i2 >= 0) {
                            part.tris.push_back({ static_cast<uint32_t>(i0), static_cast<uint32_t>(i1), static_cast<uint32_t>(i2), {} });
                        }
                    }
                }
            }
        } else if (ie.type == "IFCPOLYGONALFACESET" && args.size() >= 3) {
            consumedInHigherEntity.insert(itemId);
            const auto* plist = getPointList(ParseRefId(args[0]));
            if (plist && !plist->empty()) {
                std::vector<double> pnIndex;
                if (args.size() >= 4 && !args[3].empty() && args[3].front() == '(') ParseFloatTuple(args[3], pnIndex);
                auto mapPt = [&](int oneBased, Vec3f& outP) -> bool {
                    int r = oneBased - 1;
                    if (!pnIndex.empty()) {
                        if (r < 0 || static_cast<size_t>(r) >= pnIndex.size()) return false;
                        r = static_cast<int>(pnIndex[r]) - 1;
                    }
                    if (r < 0 || static_cast<size_t>(r) >= plist->size()) return false;
                    outP = (*plist)[r];
                    return true;
                };
                std::vector<int> faceRefs;
                ParseRefList(args[2], faceRefs);
                for (int pfId : faceRefs) {
                    auto pfit = ents.find(pfId);
                    if (pfit == ents.end()) continue;
                    consumedInHigherEntity.insert(pfId);
                    std::vector<std::string_view> pfArgs;
                    SplitStepArgs(pfit->second.params, pfArgs);
                    if (pfArgs.empty()) continue;
                    std::vector<double> idxD;
                    ParseFloatTuple(pfArgs[0], idxD);
                    std::vector<Vec3f> outer;
                    for (double d : idxD) {
                        Vec3f p;
                        if (mapPt(static_cast<int>(d), p)) outer.push_back(p);
                    }
                    std::vector<std::vector<Vec3f>> holes;
                    if (pfit->second.type == "IFCINDEXEDPOLYGONALFACEWITHVOIDS" && pfArgs.size() >= 2) {
                        std::vector<std::string_view> hLoops;
                        SplitStepArgs(pfArgs[1], hLoops);
                        for (auto hl : hLoops) {
                            std::vector<double> hd;
                            ParseFloatTuple(hl, hd);
                            std::vector<Vec3f> hr;
                            for (double d : hd) {
                                Vec3f p;
                                if (mapPt(static_cast<int>(d), p)) hr.push_back(p);
                            }
                            if (hr.size() >= 3) holes.push_back(std::move(hr));
                        }
                    }
                    appendFaceRingToPart(part, outer, holes, false);
                }
            }
        } else if (ie.type == "IFCEXTRUDEDAREASOLID" && args.size() >= 4) {
            consumedInHigherEntity.insert(itemId);
            Profile2D prof = evalProfile2D(ParseRefId(args[0]));
            Mat4 posMat = getTransform(ParseRefId(args[1]), 0);
            Vec3f extDir = { 0, 0, 1 };
            getDir(ParseRefId(args[2]), extDir);
            extDir = StepNorm(extDir, { 0, 0, 1 });
            float depthVal = static_cast<float>(ParseStepDouble(args[3], 0.0));

            if (prof.outer.size() >= 3 && depthVal > 1e-6f) {
                Vec3f sweep = StepScale(extDir, depthVal);
                bool flipMat = (posMat.Det3x3() < 0.0f);

                // Ensure CCW 2D outer ring
                auto area2 = [](const std::vector<std::pair<float, float>>& r) {
                    double a = 0.0;
                    for (size_t i = 0; i < r.size(); ++i) {
                        size_t j = (i + 1) % r.size();
                        a += static_cast<double>(r[i].first) * r[j].second - static_cast<double>(r[j].first) * r[i].second;
                    }
                    return a;
                };
                if (area2(prof.outer) < 0.0) std::reverse(prof.outer.begin(), prof.outer.end());
                for (auto& vr : prof.voids) {
                    if (area2(vr) > 0.0) std::reverse(vr.begin(), vr.end());
                }

                auto emitSideWall = [&](const std::vector<std::pair<float, float>>& ring2D) {
                    size_t n = ring2D.size();
                    uint32_t b = static_cast<uint32_t>(part.verts.size());
                    for (size_t i = 0; i < n; ++i) {
                        Vec3f p0 = { ring2D[i].first, ring2D[i].second, 0.0f };
                        Vec3f p1 = StepAdd(p0, sweep);
                        part.verts.push_back(posMat.TransformPoint(p0));
                        part.verts.push_back(posMat.TransformPoint(p1));
                    }
                    for (size_t i = 0; i < n; ++i) {
                        size_t j = (i + 1) % n;
                        uint32_t b0 = b + static_cast<uint32_t>(i * 2), t0 = b0 + 1;
                        uint32_t b1 = b + static_cast<uint32_t>(j * 2), t1 = b1 + 1;
                        if (flipMat) {
                            part.tris.push_back({ b0, t1, b1, {} });
                            part.tris.push_back({ b0, t0, t1, {} });
                        } else {
                            part.tris.push_back({ b0, b1, t1, {} });
                            part.tris.push_back({ b0, t1, t0, {} });
                        }
                    }
                };
                emitSideWall(prof.outer);
                for (const auto& vr : prof.voids) emitSideWall(vr);

                // Bottom and Top caps with hole rings
                for (int cap = 0; cap < 2; ++cap) {
                    Vec3f off = (cap == 0) ? Vec3f{ 0, 0, 0 } : sweep;
                    std::vector<uint32_t> oIdx;
                    for (const auto& p : prof.outer) {
                        oIdx.push_back(static_cast<uint32_t>(part.verts.size()));
                        part.verts.push_back(posMat.TransformPoint(StepAdd({ p.first, p.second, 0.0f }, off)));
                    }
                    std::vector<std::vector<uint32_t>> hIdx;
                    for (const auto& vr : prof.voids) {
                        std::vector<uint32_t> vi;
                        for (const auto& p : vr) {
                            vi.push_back(static_cast<uint32_t>(part.verts.size()));
                            part.verts.push_back(posMat.TransformPoint(StepAdd({ p.first, p.second, 0.0f }, off)));
                        }
                        hIdx.push_back(std::move(vi));
                    }
                    EarClip2DWithHoles(prof.outer, oIdx, prof.voids, hIdx, (cap == 0) ^ flipMat, part.tris);
                }
            }
        } else if ((ie.type == "IFCBOOLEANCLIPPINGRESULT" || ie.type == "IFCBOOLEANRESULT") && args.size() >= 3) {
            consumedInHigherEntity.insert(itemId);
            const auto* op1 = evalGeomItem(ParseRefId(args[1]), depth + 1);
            if (op1) part = *op1;
            if (args[0].find("UNION") != std::string_view::npos) {
                const auto* op2 = evalGeomItem(ParseRefId(args[2]), depth + 1);
                if (op2) {
                    uint32_t b = static_cast<uint32_t>(part.verts.size());
                    part.verts.insert(part.verts.end(), op2->verts.begin(), op2->verts.end());
                    for (const auto& t : op2->tris) part.tris.push_back({ b + t.i0, b + t.i1, b + t.i2, {} });
                }
            }
        } else if (ie.type == "IFCMAPPEDITEM" && args.size() >= 2) {
            consumedInHigherEntity.insert(itemId);
            int repMapId = ParseRefId(args[0]);
            Mat4 targetMat = getTransform(ParseRefId(args[1]), depth + 1);
            auto rmit = ents.find(repMapId);
            if (rmit != ents.end() && rmit->second.type == "IFCREPRESENTATIONMAP") {
                std::vector<std::string_view> rmArgs;
                SplitStepArgs(rmit->second.params, rmArgs);
                if (rmArgs.size() >= 2) {
                    Mat4 origMat = getTransform(ParseRefId(rmArgs[0]), depth + 1);
                    Mat4 combined = Mat4::Mul(targetMat, origMat);
                    bool flip = (combined.Det3x3() < 0.0f);
                    int mappedRepId = ParseRefId(rmArgs[1]);
                    auto srit = ents.find(mappedRepId);
                    if (srit != ents.end() && srit->second.type == "IFCSHAPEREPRESENTATION") {
                        consumedInHigherEntity.insert(mappedRepId);
                        std::vector<std::string_view> srArgs;
                        SplitStepArgs(srit->second.params, srArgs);
                        if (srArgs.size() >= 4) {
                            std::vector<int> subItems;
                            ParseRefList(srArgs[3], subItems);
                            for (int subId : subItems) {
                                const auto* sp = evalGeomItem(subId, depth + 1);
                                if (!sp) continue;
                                uint32_t b = static_cast<uint32_t>(part.verts.size());
                                for (const auto& v : sp->verts) part.verts.push_back(combined.TransformPoint(v));
                                for (const auto& t : sp->tris) {
                                    if (flip) part.tris.push_back({ b + t.i0, b + t.i2, b + t.i1, {} });
                                    else      part.tris.push_back({ b + t.i0, b + t.i1, b + t.i2, {} });
                                }
                            }
                        }
                    }
                }
            }
        }

        itemCache[itemId] = std::move(part);
        return &itemCache[itemId];
    };

    auto emitPartToMesh = [&](const IfcGeomPart& part, const Mat4& worldMat) {
        if (part.verts.empty() || part.tris.empty()) return;
        bool flip = (worldMat.Det3x3() < 0.0f);
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (const auto& lv : part.verts) {
            Vec3f w = worldMat.TransformPoint(lv);
            mesh.vertices.push_back({ w.x, w.z, -w.y }); // IFC Z-up -> Explorer2 Y-up
        }
        for (const auto& t : part.tris) {
            if (flip) mesh.tris.push_back({ base + t.i0, base + t.i2, base + t.i1, {} });
            else      mesh.tris.push_back({ base + t.i0, base + t.i1, base + t.i2, {} });
        }
    };

    // 6. Instantiate IFCPRODUCT elements with their chained IFCLOCALPLACEMENT world matrices
    std::set<int> instancedShapeReps;
    for (const auto& [id, ent] : ents) {
        if (ent.type.rfind("IFC", 0) != 0) continue;
        std::vector<std::string_view> args;
        SplitStepArgs(ent.params, args);
        if (args.size() < 7) continue;

        // In IFC2x3 / IFC4, IfcProduct has ObjectPlacement at arg[5] and Representation (IfcProductDefinitionShape) at arg[6]
        int repDefId = ParseRefId(args[6]);
        auto rdit = ents.find(repDefId);
        if (rdit == ents.end() || rdit->second.type != "IFCPRODUCTDEFINITIONSHAPE") continue;

        Mat4 worldMat = getTransform(ParseRefId(args[5]), 0);
        std::vector<std::string_view> rdArgs;
        SplitStepArgs(rdit->second.params, rdArgs);
        if (rdArgs.size() < 3) continue;
        std::vector<int> srIds;
        ParseRefList(rdArgs[2], srIds);
        for (int srId : srIds) {
            auto srit = ents.find(srId);
            if (srit == ents.end() || srit->second.type != "IFCSHAPEREPRESENTATION") continue;
            instancedShapeReps.insert(srId);
            std::vector<std::string_view> srArgs;
            SplitStepArgs(srit->second.params, srArgs);
            if (srArgs.size() < 4) continue;
            std::vector<int> itemIds;
            ParseRefList(srArgs[3], itemIds);
            for (int itemId : itemIds) {
                const auto* part = evalGeomItem(itemId, 0);
                if (part) emitPartToMesh(*part, worldMat);
            }
        }
    }

    // 7. Emit any standalone / unreferenced representation items, faces, or polyloops (e.g. minimal IFC snippets)
    for (const auto& [id, ent] : ents) {
        if (consumedInHigherEntity.find(id) != consumedInHigherEntity.end()) continue;
        if (ent.type == "IFCEXTRUDEDAREASOLID" || ent.type == "IFCTRIANGULATEDFACESET" ||
            ent.type == "IFCPOLYGONALFACESET"  || ent.type == "IFCFACETEDBREP" ||
            ent.type == "IFCFACETEDBREPWITHVOIDS" || ent.type == "IFCSHELLBASEDSURFACEMODEL" ||
            ent.type == "IFCFACEBASEDSURFACEMODEL" || ent.type == "IFCMAPPEDITEM")
        {
            const auto* part = evalGeomItem(id, 0);
            if (part) emitPartToMesh(*part, Mat4::Identity());
        }
    }
    for (const auto& [id, ent] : ents) {
        if (ent.type == "IFCFACE" && consumedInHigherEntity.find(id) == consumedInHigherEntity.end()) {
            IfcGeomPart p;
            evalIfcFace(id, p);
            emitPartToMesh(p, Mat4::Identity());
        }
    }
    for (const auto& [id, ent] : ents) {
        if (ent.type == "IFCPOLYLOOP" && consumedInHigherEntity.find(id) == consumedInHigherEntity.end()) {
            std::vector<std::string_view> lArgs;
            SplitStepArgs(ent.params, lArgs);
            if (!lArgs.empty()) {
                std::vector<int> ptIds;
                ParseRefList(lArgs[0], ptIds);
                std::vector<Vec3f> ring;
                for (int pid : ptIds) {
                    Vec3f pt;
                    if (getPt(pid, pt)) ring.push_back(pt);
                }
                if (ring.size() >= 3) {
                    IfcGeomPart p;
                    appendFaceRingToPart(p, ring, {}, false);
                    emitPartToMesh(p, Mat4::Identity());
                }
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
