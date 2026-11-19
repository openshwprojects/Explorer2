#pragma once
// MayaAsciiMa.h — Autodesk Maya ASCII Scene (.ma) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace DetailMaya {

inline bool ParseMayaAscii(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.find("createNode") == std::string_view::npos && sv.find("setAttr") == std::string_view::npos && sv.rfind("//Maya", 0) != 0) {
        return false;
    }
    mesh.formatName = L"Autodesk Maya ASCII Scene (.ma)";

    struct XformNode {
        std::string parent;
        Vec3f t = { 0, 0, 0 };
        Vec3f r = { 0, 0, 0 };
        Vec3f s = { 1, 1, 1 };
        bool isJointOrLoc = false;
    };
    struct MeshNode {
        std::string parent;
        std::vector<Vec3f> vt;
        std::vector<Vec3f> pt;
        std::vector<MayaEdge> ed;
        std::vector<std::vector<int32_t>> fc;
    };

    std::unordered_map<std::string, XformNode> xforms;
    std::vector<MeshNode> meshes;

    enum CurKind { None, Xf, Me } curKind = None;
    std::string curXfName;

    auto parseSliceRange = [](std::string_view attr, size_t brkPos, size_t& outStart, size_t& outEnd) -> bool {
        size_t rb = attr.find(']', brkPos);
        if (rb == std::string_view::npos) return false;
        std::string_view inner = attr.substr(brkPos + 1, rb - brkPos - 1);
        size_t col = inner.find(':');
        if (col == std::string_view::npos) {
            long idx = std::strtol(std::string(inner).c_str(), nullptr, 10);
            if (idx < 0) return false;
            outStart = outEnd = static_cast<size_t>(idx);
            return true;
        }
        long a = std::strtol(std::string(inner.substr(0, col)).c_str(), nullptr, 10);
        long b = std::strtol(std::string(inner.substr(col + 1)).c_str(), nullptr, 10);
        if (a < 0 || b < a) return false;
        outStart = static_cast<size_t>(a);
        outEnd   = static_cast<size_t>(b);
        return true;
    };

    // Strip // line comments into clean buffer and scan ';' statements
    std::string clean;
    clean.reserve(sv.size());
    for (size_t i = 0; i < sv.size();) {
        if (sv[i] == '/' && i + 1 < sv.size() && sv[i + 1] == '/') {
            while (i < sv.size() && sv[i] != '\n') ++i;
            continue;
        }
        clean.push_back(sv[i++]);
    }

    size_t pos = 0;
    while (pos < clean.size()) {
        while (pos < clean.size() && static_cast<unsigned char>(clean[pos]) <= 32) ++pos;
        if (pos >= clean.size()) break;
        size_t st = pos;
        bool inQ = false;
        while (pos < clean.size()) {
            char c = clean[pos];
            if (c == '"' && (pos == 0 || clean[pos - 1] != '\\')) inQ = !inQ;
            else if (c == ';' && !inQ) break;
            ++pos;
        }
        std::string_view stmt(clean.data() + st, pos - st);
        if (pos < clean.size() && clean[pos] == ';') ++pos;

        // Tokenize command header
        auto nextTok = [](std::string_view s, size_t& p) -> std::string_view {
            while (p < s.size() && static_cast<unsigned char>(s[p]) <= 32) ++p;
            if (p >= s.size()) return {};
            if (s[p] == '"') {
                size_t q0 = ++p;
                while (p < s.size() && s[p] != '"') {
                    if (s[p] == '\\' && p + 1 < s.size()) ++p;
                    ++p;
                }
                std::string_view res = s.substr(q0, p - q0);
                if (p < s.size() && s[p] == '"') ++p;
                return res;
            }
            size_t t0 = p;
            while (p < s.size() && static_cast<unsigned char>(s[p]) > 32) ++p;
            return s.substr(t0, p - t0);
        };

        size_t sp = 0;
        std::string_view cmd = nextTok(stmt, sp);
        if (cmd == "createNode") {
            std::string_view nodeType = nextTok(stmt, sp);
            std::string nodeName, parentName;
            while (sp < stmt.size()) {
                std::string_view flg = nextTok(stmt, sp);
                if (flg == "-n" || flg == "-name") nodeName = std::string(nextTok(stmt, sp));
                else if (flg == "-p" || flg == "-parent") parentName = std::string(nextTok(stmt, sp));
            }
            if (nodeType == "transform" || nodeType == "joint" || nodeType == "locator") {
                if (nodeName.empty()) nodeName = "xf_" + std::to_string(xforms.size());
                curKind = Xf;
                curXfName = nodeName;
                auto& xn = xforms[nodeName];
                xn.parent = parentName;
                if (nodeType == "joint" || nodeType == "locator") xn.isJointOrLoc = true;
            } else if (nodeType == "mesh") {
                curKind = Me;
                meshes.push_back(MeshNode{ parentName, {}, {}, {}, {} });
            } else {
                curKind = None;
            }
        } else if (cmd == "setAttr") {
            std::string_view attrName;
            while (sp < stmt.size()) {
                std::string_view t = nextTok(stmt, sp);
                if (t.empty()) break;
                if (t[0] == '.') { attrName = t; break; }
                if (t == "-s" || t == "-size" || t == "-type" || t == "-ch") { nextTok(stmt, sp); }
            }
            // Skip optional trailing -type "..." right after ".attr"
            size_t valStart = sp;
            while (sp < stmt.size()) {
                size_t save = sp;
                std::string_view t = nextTok(stmt, sp);
                if (t == "-type") { nextTok(stmt, sp); valStart = sp; }
                else { sp = save; break; }
            }

            if (curKind == Xf && !curXfName.empty()) {
                auto& xn = xforms[curXfName];
                if (attrName == ".t" || attrName == ".r" || attrName == ".s") {
                    const char* cur = stmt.data() + valStart;
                    const char* end = stmt.data() + stmt.size();
                    char* ep = nullptr;
                    float x = std::strtof(cur, &ep); if (ep != cur) {
                        cur = ep; float y = std::strtof(cur, &ep); if (ep != cur) {
                            cur = ep; float z = std::strtof(cur, &ep); if (ep != cur) {
                                if (attrName == ".t") xn.t = { x, y, z };
                                if (attrName == ".r") xn.r = { x, y, z };
                                if (attrName == ".s") xn.s = { x, y, z };
                            }
                        }
                    }
                }
            } else if (curKind == Me && !meshes.empty()) {
                MeshNode& mn = meshes.back();
                if (attrName.rfind(".vt[", 0) == 0 || attrName.rfind(".pt[", 0) == 0) {
                    auto& dst = (attrName[1] == 'v') ? mn.vt : mn.pt;
                    size_t sIdx = 0, eIdx = 0;
                    if (parseSliceRange(attrName, 3, sIdx, eIdx) && eIdx < 1000000) {
                        if (dst.size() <= eIdx) dst.resize(eIdx + 1, { 0, 0, 0 });
                        const char* cur = stmt.data() + valStart;
                        const char* end = stmt.data() + stmt.size();
                        for (size_t k = sIdx; k <= eIdx && cur < end; ++k) {
                            char* ep = nullptr;
                            float x = std::strtof(cur, &ep); if (ep == cur) break; cur = ep;
                            float y = std::strtof(cur, &ep); if (ep == cur) break; cur = ep;
                            float z = std::strtof(cur, &ep); if (ep == cur) break; cur = ep;
                            dst[k] = { x, y, z };
                        }
                    }
                } else if (attrName.rfind(".ed[", 0) == 0) {
                    size_t sIdx = 0, eIdx = 0;
                    if (parseSliceRange(attrName, 3, sIdx, eIdx) && eIdx < 2000000) {
                        if (mn.ed.size() <= eIdx) mn.ed.resize(eIdx + 1);
                        const char* cur = stmt.data() + valStart;
                        const char* end = stmt.data() + stmt.size();
                        for (size_t k = sIdx; k <= eIdx && cur < end; ++k) {
                            char* ep = nullptr;
                            long v0 = std::strtol(cur, &ep, 10); if (ep == cur) break; cur = ep;
                            long v1 = std::strtol(cur, &ep, 10); if (ep == cur) break; cur = ep;
                            std::strtol(cur, &ep, 10); if (ep == cur) break; cur = ep; // smooth flag
                            mn.ed[k] = { static_cast<int32_t>(v0), static_cast<int32_t>(v1) };
                        }
                    }
                } else if (attrName.rfind(".fc[", 0) == 0) {
                    size_t p = valStart;
                    while (p < stmt.size() && mn.fc.size() < 2000000) {
                        std::string_view sub = nextTok(stmt, p);
                        if (sub.empty()) break;
                        if (sub == "f") {
                            int cnt = std::atoi(std::string(nextTok(stmt, p)).c_str());
                            std::vector<int32_t> loop;
                            for (int k = 0; k < cnt && k < 512 && p < stmt.size(); ++k) {
                                loop.push_back(static_cast<int32_t>(std::atoi(std::string(nextTok(stmt, p)).c_str())));
                            }
                            if (loop.size() >= 3) mn.fc.push_back(std::move(loop));
                        } else if (sub == "h" || sub == "mf") {
                            int cnt = std::atoi(std::string(nextTok(stmt, p)).c_str());
                            for (int k = 0; k < cnt && p < stmt.size(); ++k) nextTok(stmt, p);
                        } else if (sub == "mu") {
                            nextTok(stmt, p); // uvSet
                            int cnt = std::atoi(std::string(nextTok(stmt, p)).c_str());
                            for (int k = 0; k < cnt && p < stmt.size(); ++k) nextTok(stmt, p);
                        } else if (sub == "mc") {
                            nextTok(stmt, p); // colorSet
                            int cnt = std::atoi(std::string(nextTok(stmt, p)).c_str());
                            for (int k = 0; k < cnt && p < stmt.size(); ++k) nextTok(stmt, p);
                        }
                    }
                }
            }
        }
    }

    std::function<DetailScad::Mat4(const std::string&, int)> getWorldMat = [&](const std::string& nm, int depth) -> DetailScad::Mat4 {
        if (nm.empty() || depth > 24) return DetailScad::Mat4::Identity();
        auto it = xforms.find(nm);
        if (it == xforms.end()) return DetailScad::Mat4::Identity();
        const auto& xf = it->second;
        using DetailScad::Mat4;
        Mat4 loc = Mat4::Mul(Mat4::Translate(xf.t.x, xf.t.y, xf.t.z),
                   Mat4::Mul(Mat4::RotateXYZ(xf.r.x, xf.r.y, xf.r.z), Mat4::Scale(xf.s.x, xf.s.y, xf.s.z)));
        return Mat4::Mul(getWorldMat(xf.parent, depth + 1), loc);
    };

    for (const auto& mn : meshes) {
        DetailScad::Mat4 wm = getWorldMat(mn.parent, 0);
        if (!mn.fc.empty() && !mn.ed.empty()) {
            EmitMayaMeshNode(mn.vt, mn.pt, mn.ed, mn.fc, wm, mesh);
        }
    }

    // Joint / Locator rig hierarchy fallback ONLY when genuine joint/locator nodes with transforms exist
    if (mesh.tris.empty() && xforms.size() >= 2) {
        std::vector<std::pair<Vec3f, Vec3f>> bones;
        for (const auto& [nm, xn] : xforms) {
            if (!xn.parent.empty() && xforms.find(xn.parent) != xforms.end()) {
                Vec3f p0 = getWorldMat(xn.parent, 0).TransformPoint({ 0, 0, 0 });
                Vec3f p1 = getWorldMat(nm, 0).TransformPoint({ 0, 0, 0 });
                bones.push_back({ p0, p1 });
            }
        }
        if (!bones.empty()) {
            for (const auto& b : bones) {
                Detail3D100::AddLineStrut(mesh, b.first, b.second, 0.05f);
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailMaya

inline bool ParseMayaAsciiMa(std::string_view sv, LoadedMesh3D& mesh) {
    return DetailMaya::ParseMayaAscii(sv, mesh);
}

} // namespace Formats
