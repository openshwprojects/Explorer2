#pragma once
#include "../../ZipDeflate.h"
#include "../../XmlTagScan.h"
// Amf.h — Additive Manufacturing File Format (.amf, ISO/ASTM 52915) XML + PK-ZIP 3D Mesh Parser

namespace Formats {

namespace DetailPkZip {

// Part names compare without leading slashes, with '/' separators and ASCII case folded
inline std::string NormalizeZipPath(std::string_view p) {
    while (!p.empty() && (p.front() == '/' || p.front() == '\\')) p.remove_prefix(1);
    std::string out;
    out.reserve(p.size());
    for (char c : p) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        out.push_back(c);
    }
    return out;
}

// Directory of an in-memory ZIP (OPC package, zipped AMF) read through ZipDeflate, with entries looked up by
// NormalizeZipPath(name)
struct ZipIndex {
    const uint8_t* data = nullptr;
    size_t size = 0;
    std::vector<::Explorer2::ZipDeflate::ArchiveItemMeta> entries;
    std::vector<std::string> names; // UTF-8, as stored in the archive
    std::unordered_map<std::string, size_t> byKey;

    bool Open(const uint8_t* d, size_t n) {
        data = d;
        size = n;
        entries.clear();
        names.clear();
        byKey.clear();
        if (!::Explorer2::ZipDeflate::ListZipEntriesFromMemory(d, n, entries)) return false;
        names.reserve(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            names.push_back(PathUtils::WideToUtf8(entries[i].pathInArchive));
            if (!entries[i].isDirectory) byKey.emplace(NormalizeZipPath(names.back()), i);
        }
        return !entries.empty();
    }

    bool Read(std::string_view rawPath, std::vector<uint8_t>& out, size_t maxBytes = 64ULL * 1024ULL * 1024ULL) const {
        out.clear();
        auto it = byKey.find(NormalizeZipPath(rawPath));
        if (it == byKey.end()) return false;
        return ::Explorer2::ZipDeflate::ExtractZipEntryFromMemory(data, size, entries[it->second], out, maxBytes) && !out.empty();
    }
};

} // namespace DetailPkZip

namespace DetailAmf {

// XML tag scanning lives in XmlTagScan.h (shared with the other XML-based 3D readers)
using ::Explorer2::XmlTagScan::XmlTag;
using ::Explorer2::XmlTagScan::IsWs;
using ::Explorer2::XmlTagScan::IsNameChar;
using ::Explorer2::XmlTagScan::TrimWs;
using ::Explorer2::XmlTagScan::NextTag;
using ::Explorer2::XmlTagScan::FindElementBody;
using ::Explorer2::XmlTagScan::GetAttr;

inline float ParseFloatSv(std::string_view s, float defVal = 0.0f) {
    s = TrimWs(s);
    if (s.empty()) return defVal;
    if (s.front() == '+') s.remove_prefix(1);
    if (s.empty()) return defVal;
    float v = defVal;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{} || !std::isfinite(v)) return defVal;
    return v;
}

inline bool ParseU32Sv(std::string_view s, uint32_t& out) {
    s = TrimWs(s);
    if (s.empty()) return false;
    if (s.front() == '+') s.remove_prefix(1);
    if (s.empty()) return false;
    uint32_t v = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{}) return false;
    out = v;
    return true;
}

struct AmfMat4 {
    std::array<float, 16> m = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    static AmfMat4 Identity() { return AmfMat4{}; }

    static AmfMat4 FromEulerDegAndTranslation(float rxDeg, float ryDeg, float rzDeg, float dx, float dy, float dz) {
        constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
        float cx = std::cos(rxDeg * kDeg2Rad), sx = std::sin(rxDeg * kDeg2Rad);
        float cy = std::cos(ryDeg * kDeg2Rad), sy = std::sin(ryDeg * kDeg2Rad);
        float cz = std::cos(rzDeg * kDeg2Rad), sz = std::sin(rzDeg * kDeg2Rad);

        // R = Rz * Ry * Rx (ISO/ASTM 52915 X-then-Y-then-Z rotation + delta translation)
        AmfMat4 r;
        r.m[0]  = cz * cy;
        r.m[1]  = cz * sy * sx - sz * cx;
        r.m[2]  = cz * sy * cx + sz * sx;
        r.m[3]  = dx;

        r.m[4]  = sz * cy;
        r.m[5]  = sz * sy * sx + cz * cx;
        r.m[6]  = sz * sy * cx - cz * sx;
        r.m[7]  = dy;

        r.m[8]  = -sy;
        r.m[9]  = cy * sx;
        r.m[10] = cy * cx;
        r.m[11] = dz;
        return r;
    }

    static AmfMat4 Mul(const AmfMat4& a, const AmfMat4& b) {
        AmfMat4 r;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                r.m[row * 4 + col] =
                    a.m[row * 4 + 0] * b.m[0 * 4 + col] +
                    a.m[row * 4 + 1] * b.m[1 * 4 + col] +
                    a.m[row * 4 + 2] * b.m[2 * 4 + col] +
                    a.m[row * 4 + 3] * b.m[3 * 4 + col];
            }
        }
        return r;
    }

    Vec3f TransformPoint(const Vec3f& v) const {
        return {
            m[0] * v.x + m[1] * v.y + m[2]  * v.z + m[3],
            m[4] * v.x + m[5] * v.y + m[6]  * v.z + m[7],
            m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11]
        };
    }
};

struct AmfObject {
    std::string id;
    std::vector<Vec3f> vertices; // Native AMF Z-up coordinates
    std::vector<std::array<uint32_t, 3>> tris;
};

struct AmfInstance {
    std::string targetId;
    AmfMat4 localXf;
};

struct AmfConstellation {
    std::string id;
    std::vector<AmfInstance> instances;
};

} // namespace DetailAmf

inline bool ParseAmfMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace DetailAmf;
    if (!data || size < 12) return false;

    std::vector<uint8_t> unzippedXml;
    std::string_view sv;

    // Per ISO/ASTM 52915, .amf files are either uncompressed XML or a PK-ZIP archive containing an XML .amf stream
    if (size >= 4 && std::memcmp(data, "PK\x03\x04", 4) == 0) {
        DetailPkZip::ZipIndex zip;
        if (!zip.Open(data, size)) return false;
        const std::string* chosen = nullptr; // the .amf entry, else the first file
        for (size_t i = 0; i < zip.entries.size(); ++i) {
            if (zip.entries[i].isDirectory) continue;
            std::string norm = DetailPkZip::NormalizeZipPath(zip.names[i]);
            if (norm.size() >= 4 && norm.substr(norm.size() - 4) == ".amf") {
                chosen = &zip.names[i];
                break;
            }
            if (!chosen) chosen = &zip.names[i];
        }
        if (!chosen || !zip.Read(*chosen, unzippedXml)) return false;
        sv = std::string_view(reinterpret_cast<const char*>(unzippedXml.data()), unzippedXml.size());
    } else {
        sv = std::string_view(reinterpret_cast<const char*>(data), size);
    }

    if (sv.find("<amf") == std::string_view::npos && sv.find(":amf") == std::string_view::npos) {
        return false;
    }
    mesh.formatName = L"Additive Manufacturing (.amf)";

    // Locate <amf> root element body
    std::string_view amfBody = sv;
    {
        size_t p = 0;
        XmlTag rt;
        while (NextTag(sv, p, rt)) {
            if (!rt.isClosing && rt.localName == "amf" && !rt.isSelfClosing) {
                std::string_view inner;
                size_t after = 0;
                if (FindElementBody(sv, p, "amf", inner, after)) {
                    amfBody = inner;
                }
                break;
            }
        }
    }

    std::vector<AmfObject> objects;
    std::unordered_map<std::string, size_t> objectById;
    std::vector<AmfConstellation> constellations;
    std::unordered_map<std::string, size_t> constelById;

    size_t pos = 0;
    XmlTag tag;
    while (NextTag(amfBody, pos, tag)) {
        if (tag.isClosing) continue;

        if (tag.localName == "object" && !tag.isSelfClosing) {
            std::string objId(GetAttr(tag.attrs, "id"));
            std::string_view objBody;
            if (!FindElementBody(amfBody, pos, "object", objBody, pos)) break;

            AmfObject obj;
            obj.id = objId;

            size_t op = 0;
            XmlTag ot;
            while (NextTag(objBody, op, ot)) {
                if (ot.isClosing) continue;
                if (ot.localName == "mesh" && !ot.isSelfClosing) {
                    std::string_view meshBody;
                    if (!FindElementBody(objBody, op, "mesh", meshBody, op)) break;

                    const uint32_t meshBaseVert = static_cast<uint32_t>(obj.vertices.size());
                    size_t mp = 0;
                    XmlTag mt;
                    while (NextTag(meshBody, mp, mt)) {
                        if (mt.isClosing) continue;
                        if (mt.localName == "vertices" && !mt.isSelfClosing) {
                            std::string_view vertsBody;
                            if (!FindElementBody(meshBody, mp, "vertices", vertsBody, mp)) break;
                            size_t vp = 0;
                            XmlTag vt;
                            while (NextTag(vertsBody, vp, vt)) {
                                if (vt.isClosing) continue;
                                if (vt.localName == "vertex" && !vt.isSelfClosing) {
                                    std::string_view vBody;
                                    if (!FindElementBody(vertsBody, vp, "vertex", vBody, vp)) break;
                                    size_t cp = 0;
                                    XmlTag ct;
                                    while (NextTag(vBody, cp, ct)) {
                                        if (ct.isClosing) continue;
                                        if (ct.localName == "coordinates" && !ct.isSelfClosing) {
                                            std::string_view cBody;
                                            if (!FindElementBody(vBody, cp, "coordinates", cBody, cp)) break;
                                            float x = 0.0f, y = 0.0f, z = 0.0f;
                                            size_t xp = 0;
                                            XmlTag xt;
                                            while (NextTag(cBody, xp, xt)) {
                                                if (xt.isClosing || xt.isSelfClosing) continue;
                                                std::string_view valTxt;
                                                if (!FindElementBody(cBody, xp, xt.localName, valTxt, xp)) break;
                                                if (xt.localName == "x") x = ParseFloatSv(valTxt);
                                                else if (xt.localName == "y") y = ParseFloatSv(valTxt);
                                                else if (xt.localName == "z") z = ParseFloatSv(valTxt);
                                            }
                                            if (obj.vertices.size() < 4000000) {
                                                obj.vertices.push_back({ x, y, z });
                                            }
                                        } else if (!ct.isSelfClosing) {
                                            std::string_view skip;
                                            FindElementBody(vBody, cp, ct.localName, skip, cp);
                                        }
                                    }
                                } else if (!vt.isSelfClosing) {
                                    std::string_view skip;
                                    FindElementBody(vertsBody, vp, vt.localName, skip, vp);
                                }
                            }
                        } else if (mt.localName == "volume" && !mt.isSelfClosing) {
                            std::string_view volBody;
                            if (!FindElementBody(meshBody, mp, "volume", volBody, mp)) break;
                            const uint32_t localVertCount = static_cast<uint32_t>(obj.vertices.size()) - meshBaseVert;
                            size_t tp = 0;
                            XmlTag tt;
                            while (NextTag(volBody, tp, tt)) {
                                if (tt.isClosing) continue;
                                if (tt.localName == "triangle" && !tt.isSelfClosing) {
                                    std::string_view tBody;
                                    if (!FindElementBody(volBody, tp, "triangle", tBody, tp)) break;
                                    uint32_t v1 = 0, v2 = 0, v3 = 0;
                                    bool has1 = false, has2 = false, has3 = false;
                                    size_t ip = 0;
                                    XmlTag it;
                                    while (NextTag(tBody, ip, it)) {
                                        if (it.isClosing || it.isSelfClosing) continue;
                                        std::string_view valTxt;
                                        if (!FindElementBody(tBody, ip, it.localName, valTxt, ip)) break;
                                        if (it.localName == "v1" || it.localName == "p1") has1 = ParseU32Sv(valTxt, v1);
                                        else if (it.localName == "v2" || it.localName == "p2") has2 = ParseU32Sv(valTxt, v2);
                                        else if (it.localName == "v3" || it.localName == "p3") has3 = ParseU32Sv(valTxt, v3);
                                    }
                                    if (has1 && has2 && has3 &&
                                        v1 < localVertCount && v2 < localVertCount && v3 < localVertCount &&
                                        v1 != v2 && v2 != v3 && v1 != v3 &&
                                        obj.tris.size() < 4000000) {
                                        obj.tris.push_back({ meshBaseVert + v1, meshBaseVert + v2, meshBaseVert + v3 });
                                    }
                                } else if (!tt.isSelfClosing) {
                                    std::string_view skip;
                                    FindElementBody(volBody, tp, tt.localName, skip, tp);
                                }
                            }
                        } else if (!mt.isSelfClosing) {
                            std::string_view skip;
                            FindElementBody(meshBody, mp, mt.localName, skip, mp);
                        }
                    }
                } else if (!ot.isSelfClosing) {
                    std::string_view skip;
                    FindElementBody(objBody, op, ot.localName, skip, op);
                }
            }

            if (!obj.vertices.empty() && !obj.tris.empty()) {
                size_t idx = objects.size();
                if (!obj.id.empty()) objectById[obj.id] = idx;
                objects.push_back(std::move(obj));
            }
        } else if (tag.localName == "constellation" && !tag.isSelfClosing) {
            std::string cId(GetAttr(tag.attrs, "id"));
            std::string_view cBody;
            if (!FindElementBody(amfBody, pos, "constellation", cBody, pos)) break;

            AmfConstellation con;
            con.id = cId;

            size_t cp = 0;
            XmlTag ct;
            while (NextTag(cBody, cp, ct)) {
                if (ct.isClosing) continue;
                if (ct.localName == "instance") {
                    std::string targetId(GetAttr(ct.attrs, "objectid"));
                    float dx = ParseFloatSv(GetAttr(ct.attrs, "deltax"), 0.0f);
                    float dy = ParseFloatSv(GetAttr(ct.attrs, "deltay"), 0.0f);
                    float dz = ParseFloatSv(GetAttr(ct.attrs, "deltaz"), 0.0f);
                    float rx = ParseFloatSv(GetAttr(ct.attrs, "rx"), 0.0f);
                    float ry = ParseFloatSv(GetAttr(ct.attrs, "ry"), 0.0f);
                    float rz = ParseFloatSv(GetAttr(ct.attrs, "rz"), 0.0f);

                    if (!ct.isSelfClosing) {
                        std::string_view instBody;
                        if (!FindElementBody(cBody, cp, "instance", instBody, cp)) break;
                        size_t ip = 0;
                        XmlTag it;
                        while (NextTag(instBody, ip, it)) {
                            if (it.isClosing || it.isSelfClosing) continue;
                            std::string_view valTxt;
                            if (!FindElementBody(instBody, ip, it.localName, valTxt, ip)) break;
                            if (it.localName == "deltax") dx = ParseFloatSv(valTxt, dx);
                            else if (it.localName == "deltay") dy = ParseFloatSv(valTxt, dy);
                            else if (it.localName == "deltaz") dz = ParseFloatSv(valTxt, dz);
                            else if (it.localName == "rx") rx = ParseFloatSv(valTxt, rx);
                            else if (it.localName == "ry") ry = ParseFloatSv(valTxt, ry);
                            else if (it.localName == "rz") rz = ParseFloatSv(valTxt, rz);
                        }
                    }

                    if (!targetId.empty()) {
                        AmfInstance inst;
                        inst.targetId = std::move(targetId);
                        inst.localXf = AmfMat4::FromEulerDegAndTranslation(rx, ry, rz, dx, dy, dz);
                        con.instances.push_back(std::move(inst));
                    }
                } else if (!ct.isSelfClosing) {
                    std::string_view skip;
                    FindElementBody(cBody, cp, ct.localName, skip, cp);
                }
            }

            if (!con.instances.empty()) {
                size_t idx = constellations.size();
                if (!con.id.empty()) constelById[con.id] = idx;
                constellations.push_back(std::move(con));
            }
        } else if (!tag.isSelfClosing) {
            std::string_view skip;
            if (!FindElementBody(amfBody, pos, tag.localName, skip, pos)) break;
        }
    }

    if (objects.empty()) return false;

    constexpr size_t kMaxVerts = 4000000;
    constexpr size_t kMaxTris  = 4000000;

    auto emitObject = [&](const AmfObject& obj, const AmfMat4& worldXf) {
        if (mesh.vertices.size() + obj.vertices.size() > kMaxVerts ||
            mesh.tris.size() + obj.tris.size() > kMaxTris) {
            return;
        }
        uint32_t baseOff = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.reserve(mesh.vertices.size() + obj.vertices.size());
        for (const Vec3f& v : obj.vertices) {
            Vec3f wp = worldXf.TransformPoint(v);
            // Convert AMF Z-up (x, y, z) -> viewer Y-up (x, z, -y)
            Vec3f yp{ wp.x, wp.z, -wp.y };
            if (!std::isfinite(yp.x) || !std::isfinite(yp.y) || !std::isfinite(yp.z)) {
                yp = { 0.0f, 0.0f, 0.0f };
            }
            mesh.vertices.push_back(yp);
        }
        mesh.tris.reserve(mesh.tris.size() + obj.tris.size());
        for (const auto& t : obj.tris) {
            mesh.tris.push_back({ baseOff + t[0], baseOff + t[1], baseOff + t[2], {} });
        }
    };

    if (!constellations.empty()) {
        std::set<std::string> childConstelIds;
        for (const auto& c : constellations) {
            for (const auto& inst : c.instances) {
                if (constelById.find(inst.targetId) != constelById.end()) {
                    childConstelIds.insert(inst.targetId);
                }
            }
        }

        std::vector<size_t> activeStack;
        std::function<void(size_t, const AmfMat4&, int)> expandConstellation =
            [&](size_t cIdx, const AmfMat4& parentXf, int depth) {
                if (depth > 16 || cIdx >= constellations.size()) return;
                if (std::find(activeStack.begin(), activeStack.end(), cIdx) != activeStack.end()) return;
                activeStack.push_back(cIdx);

                for (const AmfInstance& inst : constellations[cIdx].instances) {
                    AmfMat4 curXf = AmfMat4::Mul(parentXf, inst.localXf);
                    auto itObj = objectById.find(inst.targetId);
                    if (itObj != objectById.end()) {
                        emitObject(objects[itObj->second], curXf);
                    } else {
                        auto itCon = constelById.find(inst.targetId);
                        if (itCon != constelById.end()) {
                            expandConstellation(itCon->second, curXf, depth + 1);
                        }
                    }
                }
                activeStack.pop_back();
            };

        bool anyRoot = false;
        for (size_t i = 0; i < constellations.size(); ++i) {
            if (constellations[i].id.empty() || childConstelIds.find(constellations[i].id) == childConstelIds.end()) {
                expandConstellation(i, AmfMat4::Identity(), 0);
                anyRoot = true;
            }
        }
        if (!anyRoot) {
            expandConstellation(0, AmfMat4::Identity(), 0);
        }
    }

    // Fallback: if no <constellation> instances produced geometry, instantiate all <object>s directly
    if (mesh.tris.empty()) {
        mesh.vertices.clear();
        for (const AmfObject& obj : objects) {
            emitObject(obj, AmfMat4::Identity());
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) {
        mesh.vertices.clear();
        mesh.tris.clear();
        return false;
    }
    return true;
}

} // namespace Formats
