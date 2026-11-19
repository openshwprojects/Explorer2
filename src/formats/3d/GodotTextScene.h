#pragma once
// GodotTextScene.h — Godot Engine 3.x & 4.x Text 3D Scenes & Mesh Resources (.tscn, .tres).
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace GodotDetail {

inline void ParseFloatListInParens(std::string_view sv, size_t openParen, std::vector<float>& outFloats) {
    outFloats.clear();
    if (openParen >= sv.size() || sv[openParen] != '(') return;
    size_t closeParen = sv.find(')', openParen + 1);
    if (closeParen == std::string_view::npos) return;
    std::string_view body = sv.substr(openParen + 1, closeParen - openParen - 1);
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n' || body[i] == ',')) ++i;
        if (i >= body.size()) break;
        size_t st = i;
        while (i < body.size() && body[i] != ' ' && body[i] != '\t' && body[i] != '\r' && body[i] != '\n' && body[i] != ',') ++i;
        float v = 0.0f;
        auto res = std::from_chars(body.data() + st, body.data() + i, v);
        if (res.ec == std::errc{} && std::isfinite(v)) outFloats.push_back(v);
    }
}

inline void ParseIntListInParens(std::string_view sv, size_t openParen, std::vector<int32_t>& outInts) {
    outInts.clear();
    if (openParen >= sv.size() || sv[openParen] != '(') return;
    size_t closeParen = sv.find(')', openParen + 1);
    if (closeParen == std::string_view::npos) return;
    std::string_view body = sv.substr(openParen + 1, closeParen - openParen - 1);
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n' || body[i] == ',')) ++i;
        if (i >= body.size()) break;
        size_t st = i;
        while (i < body.size() && body[i] != ' ' && body[i] != '\t' && body[i] != '\r' && body[i] != '\n' && body[i] != ',') ++i;
        int32_t v = 0;
        auto res = std::from_chars(body.data() + st, body.data() + i, v);
        if (res.ec == std::errc{}) outInts.push_back(v);
    }
}

inline bool ExtractPackedByteArray(std::string_view sec, std::string_view key, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    size_t kp = sec.find(key);
    if (kp == std::string_view::npos) return false;
    size_t pba = sec.find("ByteArray(", kp);
    if (pba == std::string_view::npos) return false;
    size_t openP = pba + 9; // points at '('
    size_t closeP = sec.find(')', openP + 1);
    if (closeP == std::string_view::npos) return false;
    std::string_view inner = sec.substr(openP + 1, closeP - openP - 1);
    size_t q1 = inner.find('"');
    if (q1 != std::string_view::npos) {
        size_t q2 = inner.find('"', q1 + 1);
        if (q2 == std::string_view::npos) return false;
        return DecodeBase64Bytes(inner.substr(q1 + 1, q2 - q1 - 1), outBytes);
    }
    std::vector<int32_t> ints;
    ParseIntListInParens(sec, openP, ints);
    for (int32_t b : ints) {
        if (b < 0 || b > 255) return false;
        outBytes.push_back(static_cast<uint8_t>(b));
    }
    return !outBytes.empty();
}

inline bool ExtractKeyUint32(std::string_view sec, std::string_view key, uint32_t& outVal) {
    size_t kp = sec.find(key);
    if (kp == std::string_view::npos) return false;
    size_t sep = sec.find_first_of(":=", kp + key.size());
    if (sep == std::string_view::npos) return false;
    size_t i = sep + 1;
    while (i < sec.size() && (sec[i] == ' ' || sec[i] == '\t')) ++i;
    size_t st = i;
    while (i < sec.size() && sec[i] >= '0' && sec[i] <= '9') ++i;
    if (st == i) return false;
    auto res = std::from_chars(sec.data() + st, sec.data() + i, outVal);
    return res.ec == std::errc{};
}

inline float ExtractKeyFloatDef(std::string_view sec, std::string_view key, float defVal) {
    size_t kp = sec.find(key);
    if (kp == std::string_view::npos) return defVal;
    size_t eq = sec.find('=', kp + key.size());
    if (eq == std::string_view::npos || eq > sec.find('\n', kp)) return defVal;
    size_t i = eq + 1;
    while (i < sec.size() && (sec[i] == ' ' || sec[i] == '\t')) ++i;
    size_t st = i;
    while (i < sec.size() && ((sec[i] >= '0' && sec[i] <= '9') || sec[i] == '-' || sec[i] == '+' || sec[i] == '.' || sec[i] == 'e' || sec[i] == 'E')) ++i;
    float v = defVal;
    if (st < i && std::from_chars(sec.data() + st, sec.data() + i, v).ec == std::errc{} && std::isfinite(v)) return v;
    return defVal;
}

inline bool ParseMeshFromTextSection(std::string_view rType, std::string_view body, GdLocalMesh& outMesh) {
    // 1. Godot 4 ArrayMesh / ImporterMesh with PackedByteArray("base64") vertex_data + index_data
    if (body.find("vertex_data") != std::string_view::npos) {
        size_t cur = 0;
        bool anySurf = false;
        while (cur < body.size()) {
            size_t vdPos = body.find("\"vertex_data\"", cur);
            if (vdPos == std::string_view::npos) vdPos = body.find("vertex_data", cur);
            if (vdPos == std::string_view::npos) break;
            // Find enclosing '{' ... '}' dictionary block for this surface
            size_t openBrace = body.rfind('{', vdPos);
            size_t closeBrace = body.find('}', vdPos);
            if (openBrace == std::string_view::npos || closeBrace == std::string_view::npos || closeBrace <= openBrace) {
                break;
            }
            std::string_view surfDict = body.substr(openBrace, closeBrace - openBrace + 1);
            uint32_t vCnt = 0, iCnt = 0;
            std::vector<uint8_t> vBytes, iBytes;
            bool hasVCnt = ExtractKeyUint32(surfDict, "\"vertex_count\"", vCnt) || ExtractKeyUint32(surfDict, "vertex_count", vCnt);
            ExtractKeyUint32(surfDict, "\"index_count\"", iCnt) || ExtractKeyUint32(surfDict, "index_count", iCnt);
            bool hasVData = ExtractPackedByteArray(surfDict, "vertex_data", vBytes);
            ExtractPackedByteArray(surfDict, "index_data", iBytes);

            float aabb[6] = { -0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f };
            bool hasAabb = false;
            size_t aabbPos = surfDict.find("AABB(");
            if (aabbPos != std::string_view::npos) {
                std::vector<float> af;
                ParseFloatListInParens(surfDict, aabbPos + 4, af);
                if (af.size() == 6) {
                    for (int k = 0; k < 6; ++k) aabb[k] = af[static_cast<size_t>(k)];
                    hasAabb = true;
                }
            }

            if (!hasVCnt || !hasVData || !DecodeGodot4SurfaceRawBytes(vBytes, vCnt, iBytes, iCnt, aabb, hasAabb, outMesh)) {
                return false;
            }
            anySurf = true;
            cur = closeBrace + 1;
        }
        if (anySurf) return !outMesh.empty();
    }

    // 2. Godot 3 / Godot 4 PoolVector3Array / PackedVector3Array + PoolIntArray / PackedInt32Array
    size_t v3Pos = body.find("Vector3Array(");
    if (v3Pos != std::string_view::npos) {
        size_t openV = v3Pos + 12;
        std::vector<float> vf;
        ParseFloatListInParens(body, openV, vf);
        if (vf.size() < 9 || (vf.size() % 3) != 0) return false;
        uint32_t baseV = static_cast<uint32_t>(outMesh.verts.size());
        uint32_t nVerts = static_cast<uint32_t>(vf.size() / 3);
        for (uint32_t i = 0; i < nVerts; ++i) {
            outMesh.verts.push_back({ vf[i * 3 + 0], vf[i * 3 + 1], vf[i * 3 + 2] });
        }

        size_t iPos = body.find("Int32Array(", openV);
        if (iPos != std::string_view::npos) {
            iPos += 10;
        } else {
            iPos = body.find("IntArray(", openV);
            if (iPos != std::string_view::npos) iPos += 8;
        }

        if (iPos != std::string_view::npos) {
            std::vector<int32_t> idx;
            ParseIntListInParens(body, iPos, idx);
            if (idx.size() < 3) return false;
            for (size_t t = 0; t + 2 < idx.size(); t += 3) {
                if (idx[t] < 0 || idx[t + 1] < 0 || idx[t + 2] < 0) return false;
                uint32_t i0 = static_cast<uint32_t>(idx[t]);
                uint32_t i1 = static_cast<uint32_t>(idx[t + 1]);
                uint32_t i2 = static_cast<uint32_t>(idx[t + 2]);
                if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts) return false;
                if (i0 != i1 && i1 != i2 && i2 != i0) outMesh.tris.push_back({ baseV + i0, baseV + i1, baseV + i2 });
            }
        } else {
            for (uint32_t t = 0; t + 2 < nVerts; t += 3) {
                outMesh.tris.push_back({ baseV + t, baseV + t + 1, baseV + t + 2 });
            }
        }
        return !outMesh.empty();
    }

    // 3. Parameterized Godot Primitive Meshes (BoxMesh/CubeMesh, SphereMesh, CylinderMesh, PrismMesh, PlaneMesh/QuadMesh)
    if (rType == "BoxMesh" || rType == "CubeMesh") {
        float sx = 1.0f, sy = 1.0f, sz = 1.0f;
        size_t sp = body.find("size");
        if (sp != std::string_view::npos) {
            size_t vp = body.find("Vector3(", sp);
            if (vp != std::string_view::npos) {
                std::vector<float> sf;
                ParseFloatListInParens(body, vp + 7, sf);
                if (sf.size() == 3) { sx = sf[0]; sy = sf[1]; sz = sf[2]; }
            }
        }
        BuildBoxPrimitive(sx, sy, sz, outMesh);
        return !outMesh.empty();
    }
    if (rType == "SphereMesh" || rType == "CapsuleMesh") {
        float rad = ExtractKeyFloatDef(body, "radius", 0.5f);
        float h   = ExtractKeyFloatDef(body, "height", rad * 2.0f);
        BuildSpherePrimitive(rad, h, outMesh);
        return !outMesh.empty();
    }
    if (rType == "CylinderMesh") {
        float tr = ExtractKeyFloatDef(body, "top_radius", 0.5f);
        float br = ExtractKeyFloatDef(body, "bottom_radius", 0.5f);
        float h  = ExtractKeyFloatDef(body, "height", 2.0f);
        BuildCylinderOrConePrimitive(tr, br, h, 16, outMesh);
        return !outMesh.empty();
    }
    if (rType == "PlaneMesh" || rType == "QuadMesh") {
        BuildBoxPrimitive(2.0f, 0.02f, 2.0f, outMesh);
        return !outMesh.empty();
    }

    return false;
}

inline std::string ExtractAttrQuoted(std::string_view tag, std::string_view key) {
    std::string pat(key);
    pat += "=\"";
    size_t p = tag.find(pat);
    if (p == std::string_view::npos) return {};
    p += pat.size();
    size_t q = tag.find('"', p);
    if (q == std::string_view::npos) return {};
    return std::string(tag.substr(p, q - p));
}

// Parse Godot Text Scene (.tscn) or Text Resource (.tres / text .mesh)
inline bool ParseGodotTextMeshOrScene(std::string_view sv, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    size_t firstBracket = sv.find('[');
    if (firstBracket == std::string_view::npos) return false;
    size_t firstClose = sv.find(']', firstBracket + 1);
    if (firstClose == std::string_view::npos) return false;
    std::string_view rootTag = sv.substr(firstBracket + 1, firstClose - firstBracket - 1);
    const bool isScene = (rootTag.rfind("gd_scene", 0) == 0);
    const bool isRes   = (rootTag.rfind("gd_resource", 0) == 0);
    if (!isScene && !isRes) return false;

    struct Section {
        std::string_view header;
        std::string_view body;
    };
    std::vector<Section> sections;
    size_t pos = firstBracket;
    while (pos < sv.size()) {
        size_t closeB = sv.find(']', pos + 1);
        if (closeB == std::string_view::npos) break;
        std::string_view hdr = sv.substr(pos + 1, closeB - pos - 1);
        // Find next "\n[" section boundary at start of line
        size_t nextSec = std::string_view::npos;
        size_t scan = closeB + 1;
        while (scan < sv.size()) {
            size_t nl = sv.find('\n', scan);
            if (nl == std::string_view::npos) break;
            size_t p = nl + 1;
            while (p < sv.size() && (sv[p] == ' ' || sv[p] == '\t')) ++p;
            if (p < sv.size() && sv[p] == '[') {
                nextSec = p;
                break;
            }
            scan = nl + 1;
        }
        std::string_view body = (nextSec == std::string_view::npos)
            ? sv.substr(closeB + 1)
            : sv.substr(closeB + 1, nextSec - (closeB + 1));
        sections.push_back({ hdr, body });
        if (nextSec == std::string_view::npos) break;
        pos = nextSec;
    }

    if (isRes) {
        std::string rootType = ExtractAttrQuoted(rootTag, "type");
        GdLocalMesh lm;
        for (const auto& sec : sections) {
            if (sec.header.rfind("resource", 0) == 0 || sec.header.rfind("gd_resource", 0) == 0) {
                ParseMeshFromTextSection(rootType, sec.body, lm);
            } else if (sec.header.rfind("sub_resource", 0) == 0) {
                std::string st = ExtractAttrQuoted(sec.header, "type");
                ParseMeshFromTextSection(st, sec.body, lm);
            }
        }
        if (lm.empty()) return false;
        mesh.vertices = std::move(lm.verts);
        for (const auto& t : lm.tris) mesh.tris.push_back({ t[0], t[1], t[2], {} });
        mesh.formatName = L"Godot Engine Text Mesh Resource (.tres)";
        return true;
    }

    // Scene (.tscn): collect sub_resources, optional ext_resources, and instance [node] hierarchy with Transform3D!
    std::unordered_map<std::string, GdLocalMesh> subMeshes;
    std::unordered_map<std::string, GdLocalMesh> extMeshes;

    struct SceneNode {
        std::string path;
        std::string parentPath;
        GdXform3D localXf{};
        std::string subResId;
        std::string extResId;
    };
    std::vector<SceneNode> nodes;

    for (const auto& sec : sections) {
        if (sec.header.rfind("sub_resource", 0) == 0) {
            std::string sType = ExtractAttrQuoted(sec.header, "type");
            std::string sId   = ExtractAttrQuoted(sec.header, "id");
            GdLocalMesh lm;
            if (!sId.empty() && ParseMeshFromTextSection(sType, sec.body, lm)) {
                subMeshes[sId] = std::move(lm);
            }
        } else if (sec.header.rfind("ext_resource", 0) == 0 && !fullPath.empty()) {
            std::string eId   = ExtractAttrQuoted(sec.header, "id");
            std::string ePath = ExtractAttrQuoted(sec.header, "path");
            if (!eId.empty() && !ePath.empty()) {
                if (ePath.rfind("res://", 0) == 0) ePath.erase(0, 6);
                std::wstring relW = PathUtils::Utf8ToWide(ePath);
                for (wchar_t& wc : relW) if (wc == L'/') wc = L'\\';
                std::wstring dir = PathUtils::GetParentPath(fullPath);
                std::wstring cand = dir + L"\\" + relW;
                if (GetFileAttributesW(cand.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    cand = dir + L"\\" + PathUtils::GetFileName(relW);
                }
                if (_wcsicmp(cand.c_str(), fullPath.c_str()) != 0 &&
                    GetFileAttributesW(cand.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    LoadedMesh3D extLoaded;
                    if (MeshReaders::InspectMeshFile(cand, extLoaded) && extLoaded.loadedOk) {
                        GdLocalMesh lm;
                        lm.verts = std::move(extLoaded.vertices);
                        for (const auto& t : extLoaded.tris) lm.tris.push_back({ t.i0, t.i1, t.i2 });
                        extMeshes[eId] = std::move(lm);
                    }
                }
            }
        } else if (sec.header.rfind("node", 0) == 0) {
            SceneNode sn;
            std::string nName   = ExtractAttrQuoted(sec.header, "name");
            std::string nParent = ExtractAttrQuoted(sec.header, "parent");
            if (nParent.empty()) {
                sn.path = ".";
            } else if (nParent == ".") {
                sn.path = nName;
                sn.parentPath = ".";
            } else {
                sn.path = nParent + "/" + nName;
                sn.parentPath = nParent;
            }

            size_t tfPos = sec.body.find("Transform3D(");
            size_t tfLen = 11;
            if (tfPos == std::string_view::npos) {
                tfPos = sec.body.find("Transform(");
                tfLen = 9;
            }
            if (tfPos != std::string_view::npos) {
                std::vector<float> tf;
                ParseFloatListInParens(sec.body, tfPos + tfLen, tf);
                if (tf.size() == 12) {
                    sn.localXf = GdXform3D::From12Floats(tf.data());
                }
            }

            // mesh = SubResource("...") or SubResource( 1 ) or ExtResource("...")
            size_t mp = sec.body.find("mesh =");
            if (mp == std::string_view::npos) mp = sec.body.find("mesh=");
            if (mp != std::string_view::npos) {
                size_t lineEnd = sec.body.find('\n', mp);
                std::string_view mLine = sec.body.substr(mp, (lineEnd == std::string_view::npos) ? std::string_view::npos : (lineEnd - mp));
                auto extractRefId = [](std::string_view ml, std::string_view fn) -> std::string {
                    size_t p = ml.find(fn);
                    if (p == std::string_view::npos) return {};
                    size_t op = ml.find('(', p);
                    size_t cp = ml.find(')', op == std::string_view::npos ? p : op);
                    if (op == std::string_view::npos || cp == std::string_view::npos || cp <= op + 1) return {};
                    std::string_view inside = ml.substr(op + 1, cp - op - 1);
                    while (!inside.empty() && (inside.front() == ' ' || inside.front() == '"')) inside.remove_prefix(1);
                    while (!inside.empty() && (inside.back() == ' ' || inside.back() == '"')) inside.remove_suffix(1);
                    return std::string(inside);
                };
                sn.subResId = extractRefId(mLine, "SubResource");
                sn.extResId = extractRefId(mLine, "ExtResource");
            }
            nodes.push_back(std::move(sn));
        }
    }

    std::unordered_map<std::string, GdXform3D> worldByPath;
    for (const auto& sn : nodes) {
        GdXform3D wxf = sn.localXf;
        if (!sn.parentPath.empty()) {
            auto pit = worldByPath.find(sn.parentPath);
            if (pit != worldByPath.end()) wxf = GdXform3D::Mul(pit->second, sn.localXf);
        }
        worldByPath[sn.path] = wxf;

        const GdLocalMesh* srcMesh = nullptr;
        if (!sn.subResId.empty()) {
            auto it = subMeshes.find(sn.subResId);
            if (it != subMeshes.end()) srcMesh = &it->second;
        } else if (!sn.extResId.empty()) {
            auto it = extMeshes.find(sn.extResId);
            if (it != extMeshes.end()) srcMesh = &it->second;
        }
        if (srcMesh && !srcMesh->empty()) {
            uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
            for (const auto& v : srcMesh->verts) mesh.vertices.push_back(wxf.Apply(v));
            for (const auto& t : srcMesh->tris) mesh.tris.push_back({ baseV + t[0], baseV + t[1], baseV + t[2], {} });
        }
    }

    // If scene had sub_resource meshes not explicitly bound (e.g. CSG/MultiMesh), emit unreferenced sub_resources once
    if (mesh.vertices.empty() && !subMeshes.empty()) {
        for (const auto& kv : subMeshes) {
            if (kv.second.empty()) continue;
            uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
            for (const auto& v : kv.second.verts) mesh.vertices.push_back(v);
            for (const auto& t : kv.second.tris) mesh.tris.push_back({ baseV + t[0], baseV + t[1], baseV + t[2], {} });
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Godot Engine 3D Scene (.tscn)";
    return true;
}

} // namespace GodotDetail

E2_DRACO_NOINLINE inline bool ParseGodotTextScene(std::string_view sv, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    if (!GodotDetail::IsLikelyGodotTextHeader(reinterpret_cast<const uint8_t*>(sv.data()), sv.size())) return false;
    return GodotDetail::ParseGodotTextMeshOrScene(sv, fullPath, mesh);
}

} // namespace Formats
