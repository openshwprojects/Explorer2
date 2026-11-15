#pragma once
// ThreeMf.h — 3D Manufacturing Format (.3mf) Multi-Object, Production Sub-Model & Build-Transform Parser

namespace Formats {

namespace Detail3Mf {

struct Mat4 {
    // Row-major 4x4 matrix for column-vector multiplication: v' = M * [x, y, z, 1]^T
    std::array<float, 16> m = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    static Mat4 Identity() { return Mat4{}; }

    // Parses 3MF 12-float transform: "m00 m01 m02 m10 m11 m12 m20 m21 m22 m30 m31 m32"
    // where (x', y', z') = (x*m00 + y*m10 + z*m20 + m30, x*m01 + y*m11 + z*m21 + m31, x*m02 + y*m12 + z*m22 + m32)
    static Mat4 Parse3MfTransform(std::string_view sv, float unitScale = 1.0f) {
        if (sv.empty()) return Identity();
        float vals[12] = {};
        int count = 0;
        const char* p = sv.data();
        const char* end = p + sv.size();
        while (p < end && count < 12) {
            while (p < end && DetailAmf::IsWs(*p)) ++p;
            if (p >= end) break;
            if (*p == '+') ++p;
            float v = 0.0f;
            auto res = std::from_chars(p, end, v);
            if (res.ec != std::errc{} || res.ptr == p || !std::isfinite(v)) return Identity();
            vals[count++] = v;
            p = res.ptr;
        }
        if (count != 12) return Identity();

        Mat4 r;
        // Row 0: x' = x*m00 + y*m10 + z*m20 + m30
        r.m[0]  = vals[0];
        r.m[1]  = vals[3];
        r.m[2]  = vals[6];
        r.m[3]  = vals[9] * unitScale;
        // Row 1: y' = x*m01 + y*m11 + z*m21 + m31
        r.m[4]  = vals[1];
        r.m[5]  = vals[4];
        r.m[6]  = vals[7];
        r.m[7]  = vals[10] * unitScale;
        // Row 2: z' = x*m02 + y*m12 + z*m22 + m32
        r.m[8]  = vals[2];
        r.m[9]  = vals[5];
        r.m[10] = vals[8];
        r.m[11] = vals[11] * unitScale;
        return r;
    }

    static Mat4 Mul(const Mat4& a, const Mat4& b) {
        Mat4 r;
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

inline float ParseUnitScaleMm(std::string_view unit) {
    unit = DetailAmf::TrimWs(unit);
    if (unit == "micron")     return 0.001f;
    if (unit == "millimeter") return 1.0f;
    if (unit == "centimeter") return 10.0f;
    if (unit == "inch")       return 25.4f;
    if (unit == "foot")       return 304.8f;
    if (unit == "meter")      return 1000.0f;
    return 1.0f;
}

inline std::string StripLeadingSlashesKeepCase(std::string_view p) {
    p = DetailAmf::TrimWs(p);
    while (!p.empty() && (p.front() == '/' || p.front() == '\\')) p.remove_prefix(1);
    std::string out;
    out.reserve(p.size());
    for (char c : p) {
        out.push_back(c == '\\' ? '/' : c);
    }
    return out;
}

inline std::string ResolveOpcPartPath(std::string_view baseModelPath, std::string_view refPath) {
    refPath = DetailAmf::TrimWs(refPath);
    if (refPath.empty()) return std::string(baseModelPath);
    if (refPath.front() == '/' || refPath.front() == '\\') {
        return StripLeadingSlashesKeepCase(refPath);
    }
    size_t slash = baseModelPath.rfind('/');
    if (slash == std::string_view::npos) {
        return StripLeadingSlashesKeepCase(refPath);
    }
    std::string combined(baseModelPath.substr(0, slash + 1));
    combined.append(StripLeadingSlashesKeepCase(refPath));
    return combined;
}

struct ComponentRef {
    std::string modelPath; // normalized key (lowercase, no leading '/')
    std::string rawPartPath;
    std::string objectId;
    Mat4 transform;
};

struct ObjectDef {
    std::string id;
    std::vector<Vec3f> vertices; // Scaled to millimeters, in 3MF Z-up space
    std::vector<std::array<uint32_t, 3>> tris;
    std::vector<ComponentRef> components;
};

struct BuildItem {
    std::string modelPath;
    std::string rawPartPath;
    std::string objectId;
    Mat4 transform;
};

struct ModelPart {
    bool valid = false;
    float unitScale = 1.0f;
    std::vector<std::string> objectOrder;
    std::unordered_map<std::string, ObjectDef> objects;
    std::vector<BuildItem> buildItems;
};

inline void ParseModelXml(std::string_view sv, std::string_view partPath, ModelPart& out) {
    using namespace DetailAmf;
    if (sv.find("<model") == std::string_view::npos && sv.find(":model") == std::string_view::npos) {
        return;
    }

    std::string_view modelBody = sv;
    size_t p = 0;
    XmlTag rt;
    while (NextTag(sv, p, rt)) {
        if (!rt.isClosing && rt.localName == "model") {
            out.unitScale = ParseUnitScaleMm(GetAttr(rt.attrs, "unit"));
            if (!rt.isSelfClosing) {
                std::string_view inner;
                size_t endM = 0;
                if (FindElementBody(sv, p, "model", inner, endM)) {
                    modelBody = inner;
                }
            }
            break;
        }
    }

    const float uScale = out.unitScale;
    size_t pos = 0;
    XmlTag tag;
    while (NextTag(modelBody, pos, tag)) {
        if (tag.isClosing) continue;

        if (tag.localName == "resources" && !tag.isSelfClosing) {
            std::string_view resBody;
            if (!FindElementBody(modelBody, pos, "resources", resBody, pos)) break;

            size_t rp = 0;
            XmlTag rtag;
            while (NextTag(resBody, rp, rtag)) {
                if (rtag.isClosing) continue;
                if (rtag.localName == "object" && !rtag.isSelfClosing) {
                    std::string objId(GetAttr(rtag.attrs, "id"));
                    std::string_view objBody;
                    if (!FindElementBody(resBody, rp, "object", objBody, rp)) break;
                    if (objId.empty()) continue;

                    ObjectDef obj;
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
                                        if (!vt.isClosing && vt.localName == "vertex") {
                                            float x = ParseFloatSv(GetAttr(vt.attrs, "x")) * uScale;
                                            float y = ParseFloatSv(GetAttr(vt.attrs, "y")) * uScale;
                                            float z = ParseFloatSv(GetAttr(vt.attrs, "z")) * uScale;
                                            if (obj.vertices.size() < 4000000) {
                                                obj.vertices.push_back({ x, y, z });
                                            }
                                            if (!vt.isSelfClosing) {
                                                std::string_view dummy;
                                                FindElementBody(vertsBody, vp, "vertex", dummy, vp);
                                            }
                                        }
                                    }
                                } else if (mt.localName == "triangles" && !mt.isSelfClosing) {
                                    std::string_view trisBody;
                                    if (!FindElementBody(meshBody, mp, "triangles", trisBody, mp)) break;
                                    const uint32_t localVertCount = static_cast<uint32_t>(obj.vertices.size()) - meshBaseVert;
                                    size_t tp = 0;
                                    XmlTag tt;
                                    while (NextTag(trisBody, tp, tt)) {
                                        if (!tt.isClosing && tt.localName == "triangle") {
                                            uint32_t v1 = 0, v2 = 0, v3 = 0;
                                            if (ParseU32Sv(GetAttr(tt.attrs, "v1"), v1) &&
                                                ParseU32Sv(GetAttr(tt.attrs, "v2"), v2) &&
                                                ParseU32Sv(GetAttr(tt.attrs, "v3"), v3) &&
                                                v1 < localVertCount && v2 < localVertCount && v3 < localVertCount &&
                                                v1 != v2 && v2 != v3 && v1 != v3 &&
                                                obj.tris.size() < 4000000) {
                                                obj.tris.push_back({ meshBaseVert + v1, meshBaseVert + v2, meshBaseVert + v3 });
                                            }
                                            if (!tt.isSelfClosing) {
                                                std::string_view dummy;
                                                FindElementBody(trisBody, tp, "triangle", dummy, tp);
                                            }
                                        }
                                    }
                                } else if (!mt.isSelfClosing) {
                                    std::string_view skip;
                                    FindElementBody(meshBody, mp, mt.localName, skip, mp);
                                }
                            }
                        } else if (ot.localName == "components" && !ot.isSelfClosing) {
                            std::string_view compBody;
                            if (!FindElementBody(objBody, op, "components", compBody, op)) break;
                            size_t cp = 0;
                            XmlTag ct;
                            while (NextTag(compBody, cp, ct)) {
                                if (!ct.isClosing && ct.localName == "component") {
                                    std::string refObj(GetAttr(ct.attrs, "objectid"));
                                    std::string_view refPath = GetAttr(ct.attrs, "path"); // matches both p:path and path!
                                    std::string resolvedRaw = ResolveOpcPartPath(partPath, refPath);
                                    if (!refObj.empty()) {
                                        ComponentRef cr;
                                        cr.rawPartPath = resolvedRaw;
                                        cr.modelPath = DetailPkZip::NormalizeZipPath(resolvedRaw);
                                        cr.objectId = std::move(refObj);
                                        cr.transform = Mat4::Parse3MfTransform(GetAttr(ct.attrs, "transform"), uScale);
                                        obj.components.push_back(std::move(cr));
                                    }
                                    if (!ct.isSelfClosing) {
                                        std::string_view dummy;
                                        FindElementBody(compBody, cp, "component", dummy, cp);
                                    }
                                }
                            }
                        } else if (!ot.isSelfClosing) {
                            std::string_view skip;
                            FindElementBody(objBody, op, ot.localName, skip, op);
                        }
                    }

                    out.objectOrder.push_back(obj.id);
                    out.objects[obj.id] = std::move(obj);
                } else if (!rtag.isSelfClosing) {
                    std::string_view skip;
                    FindElementBody(resBody, rp, rtag.localName, skip, rp);
                }
            }
        } else if (tag.localName == "build" && !tag.isSelfClosing) {
            std::string_view bldBody;
            if (!FindElementBody(modelBody, pos, "build", bldBody, pos)) break;
            size_t bp = 0;
            XmlTag bt;
            while (NextTag(bldBody, bp, bt)) {
                if (!bt.isClosing && bt.localName == "item") {
                    std::string refObj(GetAttr(bt.attrs, "objectid"));
                    std::string_view refPath = GetAttr(bt.attrs, "path"); // matches p:path and path
                    std::string resolvedRaw = ResolveOpcPartPath(partPath, refPath);
                    if (!refObj.empty()) {
                        BuildItem bi;
                        bi.rawPartPath = resolvedRaw;
                        bi.modelPath = DetailPkZip::NormalizeZipPath(resolvedRaw);
                        bi.objectId = std::move(refObj);
                        bi.transform = Mat4::Parse3MfTransform(GetAttr(bt.attrs, "transform"), uScale);
                        out.buildItems.push_back(std::move(bi));
                    }
                    if (!bt.isSelfClosing) {
                        std::string_view dummy;
                        FindElementBody(bldBody, bp, "item", dummy, bp);
                    }
                }
            }
        } else if (!tag.isSelfClosing) {
            std::string_view skip;
            if (!FindElementBody(modelBody, pos, tag.localName, skip, pos)) break;
        }
    }

    out.valid = !out.objects.empty();
}

} // namespace Detail3Mf

inline bool ParseThreeMfMesh(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    using namespace Detail3Mf;
    if (!data || size < 30 || std::memcmp(data, "PK\x03\x04", 4) != 0) {
        return false;
    }

    std::vector<DetailPkZip::ZipEntryInfo> zipEntries;
    DetailPkZip::ListZipEntries(data, size, zipEntries);

    auto readPartFromZip = [&](std::string_view rawPath, std::vector<uint8_t>& outBytes) -> bool {
        outBytes.clear();
        std::string cleanRel = StripLeadingSlashesKeepCase(rawPath);
        if (cleanRel.empty()) return false;
        std::string wantNorm = DetailPkZip::NormalizeZipPath(cleanRel);

        // 1. Try ZipDeflate::ExtractZipFileByName (stripping leading '/')
        if (!fullPath.empty()) {
            int wLen = MultiByteToWideChar(CP_UTF8, 0, cleanRel.data(), static_cast<int>(cleanRel.size()), nullptr, 0);
            if (wLen > 0) {
                std::wstring wideRel(static_cast<size_t>(wLen), L'\0');
                MultiByteToWideChar(CP_UTF8, 0, cleanRel.data(), static_cast<int>(cleanRel.size()), wideRel.data(), wLen);
                if (ZipDeflate::ExtractZipFileByName(fullPath, wideRel, outBytes) && !outBytes.empty()) {
                    return true;
                }
            }
        }

        // 2. Try case-insensitive normalized match in DetailPkZip in-memory directory
        for (const auto& e : zipEntries) {
            if (DetailPkZip::NormalizeZipPath(e.name) == wantNorm) {
                if (DetailPkZip::ExtractZipEntry(data, size, e, outBytes) && !outBytes.empty()) {
                    return true;
                }
            }
        }
        return false;
    };

    // Locate root .model part:
    // 1) "3D/3dmodel.model"
    // 2) "_rels/.rels" Target pointing to 3dmodel / .model
    // 3) First ".model" entry in the ZIP archive
    std::string rootRawPath = "3D/3dmodel.model";
    std::vector<uint8_t> rootXml;
    if (!readPartFromZip(rootRawPath, rootXml)) {
        std::vector<uint8_t> relsXml;
        if (readPartFromZip("_rels/.rels", relsXml) && !relsXml.empty()) {
            std::string_view rsv(reinterpret_cast<const char*>(relsXml.data()), relsXml.size());
            size_t rp = 0;
            DetailAmf::XmlTag rtag;
            while (DetailAmf::NextTag(rsv, rp, rtag)) {
                if (!rtag.isClosing && rtag.localName == "Relationship") {
                    std::string_view target = DetailAmf::GetAttr(rtag.attrs, "Target");
                    std::string_view type   = DetailAmf::GetAttr(rtag.attrs, "Type");
                    std::string normTarget  = DetailPkZip::NormalizeZipPath(target);
                    if (!normTarget.empty() &&
                        (type.find("3dmodel") != std::string_view::npos ||
                         (normTarget.size() >= 6 && normTarget.substr(normTarget.size() - 6) == ".model"))) {
                        std::string cand = StripLeadingSlashesKeepCase(target);
                        if (readPartFromZip(cand, rootXml)) {
                            rootRawPath = cand;
                            break;
                        }
                    }
                }
            }
        }
        if (rootXml.empty()) {
            for (const auto& e : zipEntries) {
                std::string norm = DetailPkZip::NormalizeZipPath(e.name);
                if (norm.size() >= 6 && norm.substr(norm.size() - 6) == ".model") {
                    std::string cand = StripLeadingSlashesKeepCase(e.name);
                    if (readPartFromZip(cand, rootXml)) {
                        rootRawPath = cand;
                        break;
                    }
                }
            }
        }
    }

    if (rootXml.empty()) return false;
    mesh.formatName = L"3D Manufacturing Format (.3mf)";

    std::map<std::string, ModelPart> partCache;

    auto getOrLoadPart = [&](std::string_view rawPath, const std::string& normKey) -> const ModelPart* {
        auto it = partCache.find(normKey);
        if (it != partCache.end()) {
            return it->second.valid ? &it->second : nullptr;
        }
        ModelPart& slot = partCache[normKey]; // insert first to prevent re-attempting missing files
        std::vector<uint8_t> xmlBytes;
        if (!readPartFromZip(rawPath, xmlBytes) || xmlBytes.empty()) {
            return nullptr;
        }
        std::string_view sv(reinterpret_cast<const char*>(xmlBytes.data()), xmlBytes.size());
        ParseModelXml(sv, rawPath, slot);
        return slot.valid ? &slot : nullptr;
    };

    std::string rootNormKey = DetailPkZip::NormalizeZipPath(rootRawPath);
    {
        ModelPart rootPart;
        std::string_view rsv(reinterpret_cast<const char*>(rootXml.data()), rootXml.size());
        ParseModelXml(rsv, rootRawPath, rootPart);
        partCache[rootNormKey] = std::move(rootPart);
    }

    const ModelPart* rootPartPtr = partCache[rootNormKey].valid ? &partCache[rootNormKey] : nullptr;
    if (!rootPartPtr) return false;

    constexpr size_t kMaxVerts = 4000000;
    constexpr size_t kMaxTris  = 4000000;

    std::function<void(std::string_view, const std::string&, const std::string&, const Mat4&, int)> instantiateObj =
        [&](std::string_view rawPath, const std::string& normKey, const std::string& objId, const Mat4& worldMat, int depth) {
            if (depth > 16) return;
            const ModelPart* mp = getOrLoadPart(rawPath, normKey);
            if (!mp) return;
            auto itObj = mp->objects.find(objId);
            if (itObj == mp->objects.end()) return;
            const ObjectDef& obj = itObj->second;

            if (!obj.vertices.empty() && !obj.tris.empty()) {
                if (mesh.vertices.size() + obj.vertices.size() <= kMaxVerts &&
                    mesh.tris.size() + obj.tris.size() <= kMaxTris) {
                    uint32_t baseOff = static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.reserve(mesh.vertices.size() + obj.vertices.size());
                    for (const Vec3f& lv : obj.vertices) {
                        Vec3f wp = worldMat.TransformPoint(lv);
                        // Convert 3MF Z-up (x, y, z) -> viewer Y-up (x, z, -y)
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
                }
            }

            const std::vector<ComponentRef> comps = obj.components;
            for (const ComponentRef& comp : comps) {
                Mat4 childWorld = Mat4::Mul(worldMat, comp.transform);
                instantiateObj(comp.rawPartPath, comp.modelPath, comp.objectId, childWorld, depth + 1);
            }
        };

    // Note: getOrLoadPart may rehash partCache when sub-models are loaded, so copy root build items first!
    const std::vector<BuildItem> rootBuild = partCache[rootNormKey].buildItems;
    const std::vector<std::string> rootObjOrder = partCache[rootNormKey].objectOrder;

    for (const BuildItem& bi : rootBuild) {
        instantiateObj(bi.rawPartPath, bi.modelPath, bi.objectId, bi.transform, 0);
    }

    // Fallback: if <build> had no <item>s (or instantiated nothing), instantiate all top-level <object>s
    if (mesh.tris.empty()) {
        mesh.vertices.clear();
        for (const std::string& oid : rootObjOrder) {
            instantiateObj(rootRawPath, rootNormKey, oid, Mat4::Identity(), 0);
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
