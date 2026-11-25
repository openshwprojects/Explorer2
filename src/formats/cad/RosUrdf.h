#pragma once
// RosUrdf.h — ROS Robot Description Format (.urdf) kinematic tree & visual mesh reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseRosUrdf(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    // #57. ROS Robot Description Format (.urdf — Full Kinematic Tree: <link>, <joint> T*Rz*Ry*Rx, <visual> primitives & external meshes)
    if (ext == L".urdf") {
        if (sv.find("<robot") == std::string_view::npos) return false;
        mesh.formatName = L"ROS Robot Description (.urdf)";

        struct UrdfMat4 {
            float m[4][4] = {
                { 1.0f, 0.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f, 0.0f },
                { 0.0f, 0.0f, 0.0f, 1.0f }
            };
        };

        auto makeUrdfPose = [](float tx, float ty, float tz, float roll, float pitch, float yaw) -> UrdfMat4 {
            // Exact URDF spec: T(tx,ty,tz) * Rz(yaw) * Ry(pitch) * Rx(roll)
            const float cr = std::cos(roll),  sr = std::sin(roll);
            const float cp = std::cos(pitch), sp = std::sin(pitch);
            const float cy = std::cos(yaw),   sy = std::sin(yaw);
            UrdfMat4 r;
            r.m[0][0] = cy * cp;  r.m[0][1] = cy * sp * sr - sy * cr;  r.m[0][2] = cy * sp * cr + sy * sr;  r.m[0][3] = tx;
            r.m[1][0] = sy * cp;  r.m[1][1] = sy * sp * sr + cy * cr;  r.m[1][2] = sy * sp * cr - cy * sr;  r.m[1][3] = ty;
            r.m[2][0] = -sp;      r.m[2][1] = cp * sr;                 r.m[2][2] = cp * cr;                 r.m[2][3] = tz;
            r.m[3][0] = 0.0f;     r.m[3][1] = 0.0f;                    r.m[3][2] = 0.0f;                    r.m[3][3] = 1.0f;
            return r;
        };

        auto mulUrdfMat4 = [](const UrdfMat4& a, const UrdfMat4& b) -> UrdfMat4 {
            UrdfMat4 c;
            for (int r = 0; r < 4; ++r) {
                for (int col = 0; col < 4; ++col) {
                    c.m[r][col] = a.m[r][0] * b.m[0][col] +
                                  a.m[r][1] * b.m[1][col] +
                                  a.m[r][2] * b.m[2][col] +
                                  a.m[r][3] * b.m[3][col];
                }
            }
            return c;
        };

        auto detUrdf3x3 = [](const UrdfMat4& a) -> float {
            return a.m[0][0] * (a.m[1][1] * a.m[2][2] - a.m[1][2] * a.m[2][1]) -
                   a.m[0][1] * (a.m[1][0] * a.m[2][2] - a.m[1][2] * a.m[2][0]) +
                   a.m[0][2] * (a.m[1][0] * a.m[2][1] - a.m[1][1] * a.m[2][0]);
        };

        auto xformUrdfToViewer = [](const UrdfMat4& m, float lx, float ly, float lz) -> Vec3f {
            float wx = m.m[0][0] * lx + m.m[0][1] * ly + m.m[0][2] * lz + m.m[0][3];
            float wy = m.m[1][0] * lx + m.m[1][1] * ly + m.m[1][2] * lz + m.m[1][3];
            float wz = m.m[2][0] * lx + m.m[2][1] * ly + m.m[2][2] * lz + m.m[2][3];
            // Convert ROS right-handed Z-up world (wx, wy, wz) -> right-handed studio Y-up (wx, wz, -wy)
            return { wx, wz, -wy };
        };

        struct UrdfXmlTag {
            std::string_view name;
            std::string_view attrs;
            size_t tagStart = 0;
            size_t afterTag = 0;
            bool isClosing = false;
            bool isSelfClosing = false;
        };

        auto nextUrdfTag = [](std::string_view src, size_t& p, UrdfXmlTag& out) -> bool {
            const size_t n = src.size();
            while (p < n) {
                size_t lt = src.find('<', p);
                if (lt == std::string_view::npos || lt + 1 >= n) { p = n; return false; }
                if (lt + 4 <= n && src.substr(lt, 4) == "<!--") {
                    size_t ec = src.find("-->", lt + 4);
                    p = (ec == std::string_view::npos) ? n : (ec + 3);
                    continue;
                }
                if (src[lt + 1] == '?' || src[lt + 1] == '!') {
                    size_t gt = src.find('>', lt + 2);
                    p = (gt == std::string_view::npos) ? n : (gt + 1);
                    continue;
                }
                size_t gt = lt + 1;
                char q = 0;
                while (gt < n) {
                    char c = src[gt];
                    if (q) { if (c == q) q = 0; }
                    else if (c == '"' || c == '\'') q = c;
                    else if (c == '>') break;
                    ++gt;
                }
                if (gt >= n) { p = n; return false; }
                out = {};
                out.tagStart = lt;
                out.afterTag = gt + 1;
                p = gt + 1;

                std::string_view inner = src.substr(lt + 1, gt - lt - 1);
                while (!inner.empty() && (inner.front() == ' ' || inner.front() == '\t' || inner.front() == '\r' || inner.front() == '\n')) inner.remove_prefix(1);
                while (!inner.empty() && (inner.back() == ' ' || inner.back() == '\t' || inner.back() == '\r' || inner.back() == '\n')) inner.remove_suffix(1);
                if (inner.empty()) continue;
                if (inner.front() == '/') {
                    out.isClosing = true;
                    inner.remove_prefix(1);
                } else if (inner.back() == '/') {
                    out.isSelfClosing = true;
                    inner.remove_suffix(1);
                }
                size_t ne = 0;
                while (ne < inner.size() && inner[ne] != ' ' && inner[ne] != '\t' && inner[ne] != '\r' && inner[ne] != '\n') ++ne;
                std::string_view qn = inner.substr(0, ne);
                size_t col = qn.rfind(':');
                out.name = (col == std::string_view::npos) ? qn : qn.substr(col + 1);
                out.attrs = (ne < inner.size()) ? inner.substr(ne) : std::string_view{};
                return true;
            }
            return false;
        };

        auto findUrdfElementBody = [&](std::string_view src, size_t startAfterOpen, std::string_view elemName, std::string_view& bodyOut, size_t& afterCloseOut) -> bool {
            int depth = 1;
            size_t scan = startAfterOpen;
            UrdfXmlTag t;
            while (nextUrdfTag(src, scan, t)) {
                if (t.name == elemName) {
                    if (t.isClosing) {
                        if (--depth == 0) {
                            bodyOut = src.substr(startAfterOpen, t.tagStart - startAfterOpen);
                            afterCloseOut = t.afterTag;
                            return true;
                        }
                    } else if (!t.isSelfClosing) {
                        ++depth;
                    }
                }
            }
            return false;
        };

        auto getUrdfAttr = [](std::string_view attrs, std::string_view key) -> std::string_view {
            size_t p = 0;
            while (p < attrs.size()) {
                while (p < attrs.size() && (attrs[p] == ' ' || attrs[p] == '\t' || attrs[p] == '\r' || attrs[p] == '\n')) ++p;
                if (p >= attrs.size()) break;
                size_t ks = p;
                while (p < attrs.size() && attrs[p] != '=' && attrs[p] != ' ' && attrs[p] != '\t' && attrs[p] != '\r' && attrs[p] != '\n') ++p;
                std::string_view k = attrs.substr(ks, p - ks);
                while (p < attrs.size() && (attrs[p] == ' ' || attrs[p] == '\t' || attrs[p] == '\r' || attrs[p] == '\n')) ++p;
                if (p >= attrs.size() || attrs[p] != '=') continue;
                ++p;
                while (p < attrs.size() && (attrs[p] == ' ' || attrs[p] == '\t' || attrs[p] == '\r' || attrs[p] == '\n')) ++p;
                if (p >= attrs.size() || (attrs[p] != '"' && attrs[p] != '\'')) continue;
                char q = attrs[p++];
                size_t vs = p;
                size_t ve = attrs.find(q, vs);
                if (ve == std::string_view::npos) break;
                p = ve + 1;
                if (k == key) return attrs.substr(vs, ve - vs);
            }
            return {};
        };

        auto parseUrdfVec3 = [](std::string_view s, float defX, float defY, float defZ, float& ox, float& oy, float& oz) -> bool {
            ox = defX; oy = defY; oz = defZ;
            if (s.empty()) return false;
            const char* c = s.data();
            const char* e = c + s.size();
            float vals[3] = { defX, defY, defZ };
            int got = 0;
            while (c < e && got < 3) {
                while (c < e && (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n')) ++c;
                if (c >= e) break;
                const char* ts = c;
                while (c < e && *c != ' ' && *c != '\t' && *c != '\r' && *c != '\n') ++c;
                size_t len = static_cast<size_t>(c - ts);
                if (len == 0 || len >= 64) return false;
                char buf[64]; std::memcpy(buf, ts, len); buf[len] = '\0';
                char* nx = nullptr;
                double v = std::strtod(buf, &nx);
                if (nx == buf || !std::isfinite(v)) return false;
                vals[got++] = static_cast<float>(v);
            }
            if (got < 3) return false;
            ox = vals[0]; oy = vals[1]; oz = vals[2];
            return true;
        };

        auto parseUrdfFloat = [](std::string_view s, float defV) -> float {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
            if (s.empty() || s.size() >= 64) return defV;
            char buf[64]; std::memcpy(buf, s.data(), s.size()); buf[s.size()] = '\0';
            char* nx = nullptr;
            double v = std::strtod(buf, &nx);
            return (nx != buf && std::isfinite(v)) ? static_cast<float>(v) : defV;
        };

        auto parseOriginTag = [&](std::string_view attrs) -> UrdfMat4 {
            float tx = 0.0f, ty = 0.0f, tz = 0.0f;
            float rr = 0.0f, rp = 0.0f, ry = 0.0f;
            parseUrdfVec3(getUrdfAttr(attrs, "xyz"), 0.0f, 0.0f, 0.0f, tx, ty, tz);
            parseUrdfVec3(getUrdfAttr(attrs, "rpy"), 0.0f, 0.0f, 0.0f, rr, rp, ry);
            return makeUrdfPose(tx, ty, tz, rr, rp, ry);
        };

        struct UrdfVisual {
            enum GeomKind { None = 0, Box, Cylinder, Sphere, ExternalMesh } kind = None;
            UrdfMat4 origin;
            float p0 = 0.0f, p1 = 0.0f, p2 = 0.0f; // Box(bx,by,bz), Cylinder(rad,len), Sphere(rad)
            std::string meshUri;
            float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
            float rgba[4] = { 0.75f, 0.78f, 0.82f, 1.0f };
        };

        struct UrdfLink {
            std::string name;
            std::vector<UrdfVisual> visuals;
        };

        struct UrdfJoint {
            std::string name;
            std::string type;
            std::string parentLink;
            std::string childLink;
            UrdfMat4 origin;
        };

        // Locate <robot> root body
        size_t scanPos = 0;
        UrdfXmlTag rootTag;
        std::string_view robotBody;
        bool foundRobot = false;
        while (nextUrdfTag(sv, scanPos, rootTag)) {
            if (!rootTag.isClosing && rootTag.name == "robot") {
                if (rootTag.isSelfClosing) return false;
                size_t afterRobot = scanPos;
                if (!findUrdfElementBody(sv, scanPos, "robot", robotBody, afterRobot)) {
                    robotBody = sv.substr(scanPos);
                }
                foundRobot = true;
                break;
            }
        }
        if (!foundRobot) return false;

        std::unordered_map<std::string, std::array<float, 4>> namedMaterials;
        std::vector<UrdfLink> links;
        std::unordered_map<std::string, size_t> linkIndexByName;
        std::vector<UrdfJoint> joints;

        size_t rp = 0;
        UrdfXmlTag topTag;
        while (nextUrdfTag(robotBody, rp, topTag)) {
            if (topTag.isClosing) continue;

            if (topTag.name == "material") {
                std::string matName(getUrdfAttr(topTag.attrs, "name"));
                std::string_view matBody;
                if (!topTag.isSelfClosing && findUrdfElementBody(robotBody, rp, "material", matBody, rp)) {
                    size_t mp = 0;
                    UrdfXmlTag mt;
                    while (nextUrdfTag(matBody, mp, mt)) {
                        if (!mt.isClosing && mt.name == "color") {
                            std::string_view rgbaSv = getUrdfAttr(mt.attrs, "rgba");
                            float r = 0.8f, g = 0.8f, b = 0.8f;
                            if (parseUrdfVec3(rgbaSv, 0.8f, 0.8f, 0.8f, r, g, b) && !matName.empty()) {
                                namedMaterials[matName] = { r, g, b, 1.0f };
                            }
                        }
                    }
                }
            } else if (topTag.name == "link") {
                UrdfLink lnk;
                lnk.name.assign(getUrdfAttr(topTag.attrs, "name"));
                std::string_view linkBody;
                if (!topTag.isSelfClosing && findUrdfElementBody(robotBody, rp, "link", linkBody, rp)) {
                    size_t lp = 0;
                    UrdfXmlTag lt;
                    while (nextUrdfTag(linkBody, lp, lt)) {
                        if (lt.isClosing) continue;
                        if (lt.name == "visual" && !lt.isSelfClosing) {
                            std::string_view visBody;
                            if (!findUrdfElementBody(linkBody, lp, "visual", visBody, lp)) break;
                            UrdfVisual vis;
                            size_t vp = 0;
                            UrdfXmlTag vt;
                            while (nextUrdfTag(visBody, vp, vt)) {
                                if (vt.isClosing) continue;
                                if (vt.name == "origin") {
                                    vis.origin = parseOriginTag(vt.attrs);
                                    if (!vt.isSelfClosing) {
                                        std::string_view skip;
                                        findUrdfElementBody(visBody, vp, "origin", skip, vp);
                                    }
                                } else if (vt.name == "geometry" && !vt.isSelfClosing) {
                                    std::string_view geomBody;
                                    if (!findUrdfElementBody(visBody, vp, "geometry", geomBody, vp)) break;
                                    size_t gp = 0;
                                    UrdfXmlTag gt;
                                    while (nextUrdfTag(geomBody, gp, gt)) {
                                        if (gt.isClosing) continue;
                                        if (gt.name == "box") {
                                            float bx = 0.0f, by = 0.0f, bz = 0.0f;
                                            if (parseUrdfVec3(getUrdfAttr(gt.attrs, "size"), 0, 0, 0, bx, by, bz) &&
                                                bx > 0.0f && by > 0.0f && bz > 0.0f) {
                                                vis.kind = UrdfVisual::Box;
                                                vis.p0 = bx; vis.p1 = by; vis.p2 = bz;
                                            }
                                        } else if (gt.name == "cylinder") {
                                            float rad = parseUrdfFloat(getUrdfAttr(gt.attrs, "radius"), 0.0f);
                                            float len = parseUrdfFloat(getUrdfAttr(gt.attrs, "length"), 0.0f);
                                            if (rad > 0.0f && len > 0.0f) {
                                                vis.kind = UrdfVisual::Cylinder;
                                                vis.p0 = rad; vis.p1 = len;
                                            }
                                        } else if (gt.name == "sphere") {
                                            float rad = parseUrdfFloat(getUrdfAttr(gt.attrs, "radius"), 0.0f);
                                            if (rad > 0.0f) {
                                                vis.kind = UrdfVisual::Sphere;
                                                vis.p0 = rad;
                                            }
                                        } else if (gt.name == "mesh") {
                                            std::string_view fn = getUrdfAttr(gt.attrs, "filename");
                                            if (!fn.empty()) {
                                                vis.kind = UrdfVisual::ExternalMesh;
                                                vis.meshUri.assign(fn);
                                                parseUrdfVec3(getUrdfAttr(gt.attrs, "scale"), 1.0f, 1.0f, 1.0f, vis.scaleX, vis.scaleY, vis.scaleZ);
                                            }
                                        }
                                    }
                                } else if (vt.name == "material") {
                                    std::string mref(getUrdfAttr(vt.attrs, "name"));
                                    auto itM = namedMaterials.find(mref);
                                    if (itM != namedMaterials.end()) {
                                        for (int k = 0; k < 4; ++k) vis.rgba[k] = itM->second[k];
                                    }
                                    if (!vt.isSelfClosing) {
                                        std::string_view mBody;
                                        if (findUrdfElementBody(visBody, vp, "material", mBody, vp)) {
                                            size_t cmp = 0;
                                            UrdfXmlTag cmt;
                                            while (nextUrdfTag(mBody, cmp, cmt)) {
                                                if (!cmt.isClosing && cmt.name == "color") {
                                                    parseUrdfVec3(getUrdfAttr(cmt.attrs, "rgba"), vis.rgba[0], vis.rgba[1], vis.rgba[2], vis.rgba[0], vis.rgba[1], vis.rgba[2]);
                                                }
                                            }
                                        }
                                    }
                                } else if (!vt.isSelfClosing) {
                                    std::string_view skip;
                                    if (!findUrdfElementBody(visBody, vp, vt.name, skip, vp)) break;
                                }
                            }
                            if (vis.kind != UrdfVisual::None) {
                                lnk.visuals.push_back(std::move(vis));
                            }
                        } else if (!lt.isSelfClosing) {
                            std::string_view skip;
                            if (!findUrdfElementBody(linkBody, lp, lt.name, skip, lp)) break;
                        }
                    }
                }
                if (!lnk.name.empty() && linkIndexByName.find(lnk.name) == linkIndexByName.end()) {
                    linkIndexByName[lnk.name] = links.size();
                    links.push_back(std::move(lnk));
                }
            } else if (topTag.name == "joint" && !topTag.isSelfClosing) {
                UrdfJoint jnt;
                jnt.name.assign(getUrdfAttr(topTag.attrs, "name"));
                jnt.type.assign(getUrdfAttr(topTag.attrs, "type"));
                std::string_view jBody;
                if (findUrdfElementBody(robotBody, rp, "joint", jBody, rp)) {
                    size_t jp = 0;
                    UrdfXmlTag jt;
                    while (nextUrdfTag(jBody, jp, jt)) {
                        if (jt.isClosing) continue;
                        if (jt.name == "parent") {
                            jnt.parentLink.assign(getUrdfAttr(jt.attrs, "link"));
                        } else if (jt.name == "child") {
                            jnt.childLink.assign(getUrdfAttr(jt.attrs, "link"));
                        } else if (jt.name == "origin") {
                            jnt.origin = parseOriginTag(jt.attrs);
                        }
                        if (!jt.isSelfClosing) {
                            std::string_view skip;
                            if (!findUrdfElementBody(jBody, jp, jt.name, skip, jp)) break;
                        }
                    }
                }
                if (!jnt.parentLink.empty() && !jnt.childLink.empty()) {
                    joints.push_back(std::move(jnt));
                }
            } else if (!topTag.isSelfClosing) {
                std::string_view skip;
                if (!findUrdfElementBody(robotBody, rp, topTag.name, skip, rp)) break;
            }
        }

        if (links.empty()) return false;

        // Build adjacency list parentLinkIdx -> [(childLinkIdx, jointOriginTransform)]
        std::vector<std::vector<std::pair<size_t, UrdfMat4>>> childrenOf(links.size());
        std::vector<bool> isChildLink(links.size(), false);
        for (const auto& j : joints) {
            auto itP = linkIndexByName.find(j.parentLink);
            auto itC = linkIndexByName.find(j.childLink);
            if (itP != linkIndexByName.end() && itC != linkIndexByName.end() && itP->second != itC->second) {
                childrenOf[itP->second].emplace_back(itC->second, j.origin);
                isChildLink[itC->second] = true;
            }
        }

        // Helper to resolve & load external <mesh filename="..."/> (.stl, .obj, .dae, .ply) in local link coords
        std::wstring urdfDir;
        if (!mesh.filePath.empty()) {
            size_t sl = mesh.filePath.find_last_of(L"\\/");
            if (sl != std::wstring::npos) urdfDir = mesh.filePath.substr(0, sl);
        }

        auto loadExternalUrdfMesh = [&](const std::string& rawUri, std::vector<Vec3f>& outV, std::vector<std::array<uint32_t, 3>>& outT) -> bool {
            if (urdfDir.empty() || rawUri.empty()) return false;
            std::string clean = rawUri;
            std::string pkgName;
            if (clean.rfind("package://", 0) == 0) {
                clean.erase(0, 10);
                size_t slash = clean.find('/');
                if (slash == std::string::npos || slash + 1 >= clean.size()) return false;
                pkgName = clean.substr(0, slash);
                clean.erase(0, slash + 1);
            } else if (clean.rfind("file:///", 0) == 0) {
                clean.erase(0, 8);
            } else if (clean.rfind("file://", 0) == 0) {
                clean.erase(0, 7);
            }
            while (!clean.empty() && (clean.front() == '/' || clean.front() == '\\')) clean.erase(clean.begin());
            if (clean.empty() || clean.find(':') != std::string::npos) return false;

            auto utf8ToWide = [](const std::string& s) -> std::wstring {
                if (s.empty()) return {};
                int wn = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
                if (wn <= 0) return {};
                std::wstring out(static_cast<size_t>(wn), L'\0');
                MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), wn);
                return out;
            };

            std::wstring wRel = utf8ToWide(clean);
            for (wchar_t& ch : wRel) { if (ch == L'/') ch = L'\\'; }

            std::vector<std::wstring> candidates;
            candidates.push_back(urdfDir + L"\\" + wRel);
            size_t parentSl = urdfDir.find_last_of(L"\\/");
            if (parentSl != std::wstring::npos) {
                std::wstring parentDir = urdfDir.substr(0, parentSl);
                candidates.push_back(parentDir + L"\\" + wRel);
                if (!pkgName.empty()) {
                    std::wstring wPkg = utf8ToWide(pkgName);
                    candidates.push_back(parentDir + L"\\" + wPkg + L"\\" + wRel);
                    size_t grandSl = parentDir.find_last_of(L"\\/");
                    if (grandSl != std::wstring::npos) {
                        candidates.push_back(parentDir.substr(0, grandSl) + L"\\" + wPkg + L"\\" + wRel);
                    }
                }
            }

            std::vector<uint8_t> extBuf;
            std::wstring matchedPath;
            for (const auto& cand : candidates) {
                HANDLE hf = CreateFileW(cand.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hf != INVALID_HANDLE_VALUE) {
                    LARGE_INTEGER li = {};
                    GetFileSizeEx(hf, &li);
                    if (li.QuadPart > 0 && li.QuadPart <= 64LL * 1024LL * 1024LL) {
                        extBuf.resize(static_cast<size_t>(li.QuadPart));
                        DWORD br = 0;
                        ReadFile(hf, extBuf.data(), static_cast<DWORD>(extBuf.size()), &br, nullptr);
                        extBuf.resize(br);
                        matchedPath = cand;
                    }
                    CloseHandle(hf);
                    if (!extBuf.empty()) break;
                }
            }
            if (extBuf.empty()) return false;

            std::wstring mExt;
            const wchar_t* dot = PathFindExtensionW(matchedPath.c_str());
            if (dot && *dot) mExt = PathUtils::ToLower(dot);

            if (mExt == L".stl") {
                bool isBinStl = false;
                if (extBuf.size() >= 84) {
                    uint32_t tc = DetailMeshSciCad::RdU32LE(extBuf.data() + 80);
                    uint64_t exp = 84ULL + static_cast<uint64_t>(tc) * 50ULL;
                    if (tc > 0 && (exp == extBuf.size() || (extBuf.size() >= exp && std::memcmp(extBuf.data(), "solid", 5) != 0))) {
                        isBinStl = true;
                    }
                }
                if (isBinStl) {
                    uint32_t tc = DetailMeshSciCad::RdU32LE(extBuf.data() + 80);
                    size_t avail = std::min<size_t>(tc, (extBuf.size() - 84) / 50);
                    for (size_t t = 0; t < avail && outT.size() < 40000; ++t) {
                        float fv[12] = {};
                        std::memcpy(fv, extBuf.data() + 84 + t * 50, 48);
                        uint32_t b = static_cast<uint32_t>(outV.size());
                        outV.push_back({ fv[3], fv[4], fv[5] });
                        outV.push_back({ fv[6], fv[7], fv[8] });
                        outV.push_back({ fv[9], fv[10], fv[11] });
                        outT.push_back({ b, b + 1, b + 2 });
                    }
                } else {
                    const char* p = reinterpret_cast<const char*>(extBuf.data());
                    const char* e = p + extBuf.size();
                    while (p < e) {
                        while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
                        if (p + 6 < e && std::memcmp(p, "vertex", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
                            p += 6;
                            char* nx = nullptr;
                            float vx = std::strtof(p, &nx); p = nx;
                            float vy = std::strtof(p, &nx); p = nx;
                            float vz = std::strtof(p, &nx); p = nx;
                            outV.push_back({ vx, vy, vz });
                            if (outV.size() % 3 == 0) {
                                uint32_t b = static_cast<uint32_t>(outV.size() - 3);
                                outT.push_back({ b, b + 1, b + 2 });
                            }
                        }
                        while (p < e && *p != '\n') ++p;
                    }
                }
                return !outV.empty() && !outT.empty();
            } else if (mExt == L".obj") {
                const char* p = reinterpret_cast<const char*>(extBuf.data());
                const char* e = p + extBuf.size();
                std::vector<uint32_t> fp;
                while (p < e) {
                    while (p < e && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
                    if (p >= e) break;
                    if (*p == '\n') { ++p; continue; }
                    const char* le = p;
                    while (le < e && *le != '\n') ++le;
                    if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
                        char* nx = nullptr;
                        float vx = std::strtof(p + 2, &nx);
                        float vy = std::strtof(nx, &nx);
                        float vz = std::strtof(nx, &nx);
                        outV.push_back({ vx, vy, vz });
                    } else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
                        fp.clear();
                        const char* c = p + 2;
                        while (c < le) {
                            while (c < le && (*c == ' ' || *c == '\t' || *c == '\r')) ++c;
                            if (c >= le || *c == '#') break;
                            char* nx = nullptr;
                            long idx = std::strtol(c, &nx, 10);
                            if (nx == c) break;
                            c = nx;
                            while (c < le && *c != ' ' && *c != '\t' && *c != '\r') ++c;
                            int vi = (idx > 0) ? (static_cast<int>(idx) - 1) : (static_cast<int>(outV.size()) + static_cast<int>(idx));
                            if (vi >= 0 && static_cast<size_t>(vi) < outV.size()) fp.push_back(static_cast<uint32_t>(vi));
                        }
                        for (size_t k = 1; k + 1 < fp.size(); ++k) outT.push_back({ fp[0], fp[k], fp[k + 1] });
                    }
                    p = (le < e) ? (le + 1) : e;
                }
                return !outV.empty() && !outT.empty();
            } else if (mExt == L".ply") {
                LoadedMesh3D sub;
                if (Formats::ParsePlyMesh(extBuf.data(), extBuf.size(), sub, false)) {
                    outV = std::move(sub.vertices);
                    outT.reserve(sub.tris.size());
                    for (const auto& tr : sub.tris) outT.push_back({ tr.i0, tr.i1, tr.i2 });
                    return !outV.empty() && !outT.empty();
                }
            } else if (mExt == L".dae") {
                LoadedMesh3D sub;
                std::string_view dSv(reinterpret_cast<const char*>(extBuf.data()), extBuf.size());
                if (Formats::ParseColladaDaeMesh(dSv, sub)) {
                    const bool zUpDae = (dSv.find("Z_UP") != std::string_view::npos);
                    outV.reserve(sub.vertices.size());
                    for (const auto& v : sub.vertices) {
                        if (zUpDae) outV.push_back(v);
                        else        outV.push_back({ v.x, -v.z, v.y });
                    }
                    outT.reserve(sub.tris.size());
                    for (const auto& tr : sub.tris) outT.push_back({ tr.i0, tr.i1, tr.i2 });
                    return !outV.empty() && !outT.empty();
                }
            }
            return false;
        };

        auto emitTransformedVisual = [&](const UrdfVisual& vis, const UrdfMat4& worldFromLink) {
            const UrdfMat4 worldFromVis = mulUrdfMat4(worldFromLink, vis.origin);
            std::vector<Vec3f> localV;
            std::vector<std::array<uint32_t, 3>> localT;
            bool flip = (detUrdf3x3(worldFromVis) < 0.0f);

            if (vis.kind == UrdfVisual::Box) {
                const float hx = vis.p0 * 0.5f, hy = vis.p1 * 0.5f, hz = vis.p2 * 0.5f;
                localV = {
                    { -hx, -hy, -hz }, {  hx, -hy, -hz }, {  hx,  hy, -hz }, { -hx,  hy, -hz },
                    { -hx, -hy,  hz }, {  hx, -hy,  hz }, {  hx,  hy,  hz }, { -hx,  hy,  hz }
                };
                localT = {
                    {{0, 2, 1}}, {{0, 3, 2}},
                    {{4, 5, 6}}, {{4, 6, 7}},
                    {{0, 1, 5}}, {{0, 5, 4}},
                    {{1, 2, 6}}, {{1, 6, 5}},
                    {{2, 3, 7}}, {{2, 7, 6}},
                    {{3, 0, 4}}, {{3, 4, 7}}
                };
            } else if (vis.kind == UrdfVisual::Cylinder) {
                // URDF spec: <cylinder> axis is ALWAYS aligned along local Z-axis [-length/2, +length/2]!
                const float rad = vis.p0;
                const float hz  = vis.p1 * 0.5f;
                constexpr int kSegs = 16;
                localV.reserve(kSegs * 2 + 2);
                for (int i = 0; i < kSegs; ++i) {
                    float ang = static_cast<float>(i) * (6.283185307f / static_cast<float>(kSegs));
                    float cx = rad * std::cos(ang);
                    float cy = rad * std::sin(ang);
                    localV.push_back({ cx, cy, -hz });
                    localV.push_back({ cx, cy,  hz });
                }
                const uint32_t botC = static_cast<uint32_t>(localV.size());
                localV.push_back({ 0.0f, 0.0f, -hz });
                const uint32_t topC = static_cast<uint32_t>(localV.size());
                localV.push_back({ 0.0f, 0.0f,  hz });

                for (int i = 0; i < kSegs; ++i) {
                    int ni = (i + 1) % kSegs;
                    uint32_t b0 = static_cast<uint32_t>(i * 2);
                    uint32_t t0 = b0 + 1;
                    uint32_t b1 = static_cast<uint32_t>(ni * 2);
                    uint32_t t1 = b1 + 1;
                    localT.push_back({ b0, b1, t1 });
                    localT.push_back({ b0, t1, t0 });
                    localT.push_back({ botC, b1, b0 });
                    localT.push_back({ topC, t0, t1 });
                }
            } else if (vis.kind == UrdfVisual::Sphere) {
                const float rad = vis.p0;
                constexpr int kLat = 8;
                constexpr int kLon = 14;
                localV.push_back({ 0.0f, 0.0f, -rad }); // south pole = 0
                for (int ilat = 1; ilat < kLat; ++ilat) {
                    float phi = -1.570796327f + static_cast<float>(ilat) * (3.141592654f / static_cast<float>(kLat));
                    float cz = rad * std::sin(phi);
                    float cr = rad * std::cos(phi);
                    for (int ilon = 0; ilon < kLon; ++ilon) {
                        float th = static_cast<float>(ilon) * (6.283185307f / static_cast<float>(kLon));
                        localV.push_back({ cr * std::cos(th), cr * std::sin(th), cz });
                    }
                }
                const uint32_t northIdx = static_cast<uint32_t>(localV.size());
                localV.push_back({ 0.0f, 0.0f, rad });

                for (int ilon = 0; ilon < kLon; ++ilon) {
                    uint32_t c0 = 1u + static_cast<uint32_t>(ilon);
                    uint32_t c1 = 1u + static_cast<uint32_t>((ilon + 1) % kLon);
                    localT.push_back({ 0u, c1, c0 });
                }
                for (int ilat = 0; ilat + 2 < kLat; ++ilat) {
                    uint32_t rowA = 1u + static_cast<uint32_t>(ilat * kLon);
                    uint32_t rowB = rowA + static_cast<uint32_t>(kLon);
                    for (int ilon = 0; ilon < kLon; ++ilon) {
                        uint32_t nlon = static_cast<uint32_t>((ilon + 1) % kLon);
                        uint32_t a0 = rowA + static_cast<uint32_t>(ilon), a1 = rowA + nlon;
                        uint32_t b0 = rowB + static_cast<uint32_t>(ilon), b1 = rowB + nlon;
                        localT.push_back({ a0, a1, b1 });
                        localT.push_back({ a0, b1, b0 });
                    }
                }
                uint32_t topRing = 1u + static_cast<uint32_t>((kLat - 2) * kLon);
                for (int ilon = 0; ilon < kLon; ++ilon) {
                    uint32_t c0 = topRing + static_cast<uint32_t>(ilon);
                    uint32_t c1 = topRing + static_cast<uint32_t>((ilon + 1) % kLon);
                    localT.push_back({ northIdx, c0, c1 });
                }
            } else if (vis.kind == UrdfVisual::ExternalMesh) {
                if (!loadExternalUrdfMesh(vis.meshUri, localV, localT)) return;
                for (auto& v : localV) {
                    v.x *= vis.scaleX;
                    v.y *= vis.scaleY;
                    v.z *= vis.scaleZ;
                }
                if ((vis.scaleX * vis.scaleY * vis.scaleZ) < 0.0f) flip = !flip;
            }

            if (localV.empty() || localT.empty()) return;
            const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.reserve(mesh.vertices.size() + localV.size());
            for (const auto& lv : localV) {
                mesh.vertices.push_back(xformUrdfToViewer(worldFromVis, lv.x, lv.y, lv.z));
            }
            mesh.tris.reserve(mesh.tris.size() + localT.size());
            for (const auto& tr : localT) {
                if (!flip) mesh.tris.push_back({ base + tr[0], base + tr[1], base + tr[2], {} });
                else       mesh.tris.push_back({ base + tr[0], base + tr[2], base + tr[1], {} });
            }
        };

        std::vector<bool> visited(links.size(), false);
        std::function<void(size_t, const UrdfMat4&, int)> traverseLink = [&](size_t linkIdx, const UrdfMat4& worldFromLink, int depth) {
            if (linkIdx >= links.size() || visited[linkIdx] || depth > 256) return;
            visited[linkIdx] = true;
            for (const auto& vis : links[linkIdx].visuals) {
                emitTransformedVisual(vis, worldFromLink);
            }
            for (const auto& edge : childrenOf[linkIdx]) {
                UrdfMat4 worldFromChild = mulUrdfMat4(worldFromLink, edge.second);
                traverseLink(edge.first, worldFromChild, depth + 1);
            }
        };

        const UrdfMat4 identityMat;
        for (size_t i = 0; i < links.size(); ++i) {
            if (!isChildLink[i]) {
                traverseLink(i, identityMat, 0);
            }
        }
        for (size_t i = 0; i < links.size(); ++i) {
            if (!visited[i]) {
                traverseLink(i, identityMat, 0);
            }
        }

        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

} // namespace Formats
