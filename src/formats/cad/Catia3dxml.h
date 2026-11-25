#pragma once
// Catia3dxml.h — Dassault CATIA / SOLIDWORKS 3DXML (.3dxml) reader.
//
// Scoped per `<PolygonalRepType>` / `<PolygonalLOD>` / representation so `<Faces><Face .../></Faces>`
// BEFORE `<Positions>` is properly parsed (supporting `triangles="..."`, `strips="..."`, `fans="..."`,
// multiple `<Face>` elements, multiple `.3drep` files in ZIP archives, and `<ProductStructure>`
// `<Instance3D>` + `<InstanceRep>` + `<RelativeMatrix>` 12-float transforms).
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace DetailCatia3dxml {

struct RepLocalMesh {
    std::vector<Vec3f> verts;
    std::vector<std::array<uint32_t, 3>> tris;
};

inline void ParseIntSeq(std::string_view sv, std::vector<uint32_t>& out, size_t maxVal) {
    out.clear();
    const char* p = sv.data();
    const char* end = p + sv.size();
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')) ++p;
        if (p >= end) break;
        uint32_t val = 0;
        auto r = std::from_chars(p, end, val);
        if (r.ec != std::errc{} || r.ptr == p) {
            while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != ',') ++p;
            continue;
        }
        p = r.ptr;
        if (val < maxVal) out.push_back(val);
        else { out.clear(); return; }
    }
}

// Parses a single `<PolygonalLOD>` or `<PolygonalRepType>` scope (regardless of whether `<Faces>` precedes `<Positions>`!)
inline void ParsePolygonalScope(std::string_view scope, RepLocalMesh& dst) {
    // 1. Collect all `<Positions>...</Positions>` in this representation scope FIRST
    size_t posOpen = scope.find("<Positions");
    if (posOpen == std::string_view::npos) return;

    uint32_t localBase = static_cast<uint32_t>(dst.verts.size());
    size_t gt = scope.find('>', posOpen);
    size_t posClose = (gt != std::string_view::npos) ? scope.find("</Positions>", gt + 1) : std::string_view::npos;
    if (gt == std::string_view::npos || posClose == std::string_view::npos) return;

    std::vector<float> coords;
    CalCryDetail::ParseXmlFloatsSimple(scope.substr(gt + 1, posClose - (gt + 1)), coords);
    if (coords.size() < 9) return;
    for (size_t i = 0; i + 2 < coords.size(); i += 3) {
        dst.verts.push_back({ coords[i], coords[i + 1], coords[i + 2] });
    }
    const size_t nLocalVerts = dst.verts.size() - localBase;
    if (nLocalVerts < 3) return;

    // 2. Scan ALL `<Face ...>` elements across the ENTIRE scope (both before and after `<Positions>`!)
    size_t fPos = 0;
    while ((fPos = scope.find("<Face", fPos)) != std::string_view::npos) {
        if (fPos + 5 < scope.size()) {
            char nextCh = scope[fPos + 5];
            // Skip `<Faces>` container open tag (we want `<Face ...>` leaf elements!)
            if (nextCh != ' ' && nextCh != '\t' && nextCh != '\r' && nextCh != '\n' && nextCh != '/' && nextCh != '>') {
                fPos += 5;
                continue;
            }
        }
        size_t fEnd = scope.find('>', fPos);
        if (fEnd == std::string_view::npos) break;
        std::string_view faceTag = scope.substr(fPos, fEnd - fPos + 1);
        fPos = fEnd + 1;

        auto pushTri = [&](uint32_t a, uint32_t b, uint32_t c) {
            if (a != b && b != c && a != c) {
                dst.tris.push_back({ localBase + a, localBase + b, localBase + c });
            }
        };

        // triangles="i0 i1 i2 ..."
        std::string_view trisAttr = CalCryDetail::ExtractXmlAttr(faceTag, "triangles");
        if (!trisAttr.empty()) {
            std::vector<uint32_t> idxs;
            ParseIntSeq(trisAttr, idxs, nLocalVerts);
            for (size_t k = 0; k + 2 < idxs.size(); k += 3) {
                pushTri(idxs[k], idxs[k + 1], idxs[k + 2]);
            }
        }

        // strips="s0 s1 s2 s3, t0 t1 t2 ..." (comma-separated triangle strips)
        std::string_view stripsAttr = CalCryDetail::ExtractXmlAttr(faceTag, "strips");
        if (!stripsAttr.empty()) {
            size_t sStart = 0;
            while (sStart < stripsAttr.size()) {
                size_t comma = stripsAttr.find(',', sStart);
                std::string_view oneStrip = stripsAttr.substr(sStart, (comma == std::string_view::npos ? stripsAttr.size() : comma) - sStart);
                sStart = (comma == std::string_view::npos) ? stripsAttr.size() : (comma + 1);
                std::vector<uint32_t> idxs;
                ParseIntSeq(oneStrip, idxs, nLocalVerts);
                for (size_t k = 0; k + 2 < idxs.size(); ++k) {
                    if (k & 1u) pushTri(idxs[k + 1], idxs[k], idxs[k + 2]);
                    else        pushTri(idxs[k], idxs[k + 1], idxs[k + 2]);
                }
            }
        }

        // fans="f0 f1 f2 f3, g0 g1 g2 ..." (comma-separated triangle fans)
        std::string_view fansAttr = CalCryDetail::ExtractXmlAttr(faceTag, "fans");
        if (!fansAttr.empty()) {
            size_t fStart = 0;
            while (fStart < fansAttr.size()) {
                size_t comma = fansAttr.find(',', fStart);
                std::string_view oneFan = fansAttr.substr(fStart, (comma == std::string_view::npos ? fansAttr.size() : comma) - fStart);
                fStart = (comma == std::string_view::npos) ? fansAttr.size() : (comma + 1);
                std::vector<uint32_t> idxs;
                ParseIntSeq(oneFan, idxs, nLocalVerts);
                for (size_t k = 1; k + 1 < idxs.size(); ++k) {
                    pushTri(idxs[0], idxs[k], idxs[k + 1]);
                }
            }
        }
    }
}

inline void ExtractRepMeshesFromXml(
    std::string_view sv,
    const std::string& fallbackKey,
    std::unordered_map<std::string, RepLocalMesh>& repById,
    std::vector<RepLocalMesh>& allReps)
{
    if (sv.find("<Positions") == std::string_view::npos) return;

    // Split by `<PolygonalRepType` or `<Representation` if present so each rep gets its own ID & local vertex pool
    bool foundRepTag = false;
    for (const char* repTagName : { "<PolygonalRepType", "<Representation" }) {
        size_t rp = 0;
        const std::string closeName = (std::strcmp(repTagName, "<PolygonalRepType") == 0)
            ? "</PolygonalRepType>" : "</Representation>";
        while ((rp = sv.find(repTagName, rp)) != std::string_view::npos) {
            size_t gt = sv.find('>', rp);
            size_t rc = (gt != std::string_view::npos) ? sv.find(closeName, gt + 1) : std::string_view::npos;
            if (gt == std::string_view::npos || rc == std::string_view::npos) break;
            foundRepTag = true;

            std::string_view openTag = sv.substr(rp, gt - rp + 1);
            std::string repId(CalCryDetail::ExtractXmlAttr(openTag, "id"));
            std::string_view repBody = sv.substr(rp, rc + closeName.size() - rp);

            RepLocalMesh rm{};
            // Within a PolygonalRepType, if multiple `<PolygonalLOD>` exist, take only the highest-detail LOD (first `<PolygonalLOD>`)
            // or parse `<SurfaceAttributes>`/`<PolygonalRepType>` directly when no `<PolygonalLOD>` wrapper exists.
            size_t lodOpen = repBody.find("<PolygonalLOD");
            if (lodOpen != std::string_view::npos) {
                size_t lodClose = repBody.find("</PolygonalLOD>", lodOpen);
                if (lodClose != std::string_view::npos) {
                    ParsePolygonalScope(repBody.substr(lodOpen, lodClose + 15 - lodOpen), rm);
                }
            }
            if (rm.tris.empty()) {
                // Parse each `<SurfaceAttributes>` or the whole repBody if single `<Positions>`
                if (repBody.find("<Positions", repBody.find("<Positions") + 10) == std::string_view::npos) {
                    ParsePolygonalScope(repBody, rm);
                } else {
                    // Multiple sub-blocks with `<Positions>` inside one rep (e.g. multiple `<PolygonalLOD>` or `<BagRepType>`)
                    size_t cur = 0;
                    while (cur < repBody.size()) {
                        size_t nextP = repBody.find("<Positions", cur);
                        if (nextP == std::string_view::npos) break;
                        size_t afterP = repBody.find("</Positions>", nextP);
                        if (afterP == std::string_view::npos) break;
                        size_t nextNextP = repBody.find("<Positions", afterP + 12);
                        // Find enclosing `<PolygonalLOD` or slice `[cur .. nextNextP)`
                        size_t sliceEnd = (nextNextP == std::string_view::npos) ? repBody.size() : nextNextP;
                        ParsePolygonalScope(repBody.substr(cur, sliceEnd - cur), rm);
                        cur = sliceEnd;
                    }
                }
            }

            if (!rm.verts.empty() && !rm.tris.empty()) {
                if (!repId.empty()) repById["#" + repId] = rm;
                if (!fallbackKey.empty() && repById.find(fallbackKey) == repById.end()) repById[fallbackKey] = rm;
                allReps.push_back(std::move(rm));
            }
            rp = rc + closeName.size();
        }
        if (foundRepTag) break;
    }

    if (!foundRepTag) {
        RepLocalMesh rm{};
        ParsePolygonalScope(sv, rm);
        if (!rm.verts.empty() && !rm.tris.empty()) {
            if (!fallbackKey.empty()) repById[fallbackKey] = rm;
            allReps.push_back(std::move(rm));
        }
    }
}

struct Xform3dxml {
    // 3DXML `<RelativeMatrix>`: 12 floats: 3x3 column/row rotation `[m0..m8]` + translation `[m9, m10, m11]`
    // Specifically in Dassault 3DXML spec: 3 axis vectors U=(m0,m1,m2), V=(m3,m4,m5), W=(m6,m7,m8) + T=(m9,m10,m11)
    // so p' = U * p.x + V * p.y + W * p.z + T!
    float u[3] = { 1, 0, 0 };
    float v[3] = { 0, 1, 0 };
    float w[3] = { 0, 0, 1 };
    float t[3] = { 0, 0, 0 };

    static Xform3dxml From12(const float f[12]) {
        Xform3dxml x{};
        x.u[0] = f[0]; x.u[1] = f[1]; x.u[2] = f[2];
        x.v[0] = f[3]; x.v[1] = f[4]; x.v[2] = f[5];
        x.w[0] = f[6]; x.w[1] = f[7]; x.w[2] = f[8];
        x.t[0] = f[9]; x.t[1] = f[10]; x.t[2] = f[11];
        return x;
    }

    Vec3f Apply(const Vec3f& p) const {
        return {
            u[0] * p.x + v[0] * p.y + w[0] * p.z + t[0],
            u[1] * p.x + v[1] * p.y + w[1] * p.z + t[1],
            u[2] * p.x + v[2] * p.y + w[2] * p.z + t[2]
        };
    }

    static Xform3dxml Mul(const Xform3dxml& a, const Xform3dxml& b) {
        Xform3dxml r{};
        auto mulDir = [&](const float d[3], float out[3]) {
            out[0] = a.u[0] * d[0] + a.v[0] * d[1] + a.w[0] * d[2];
            out[1] = a.u[1] * d[0] + a.v[1] * d[1] + a.w[1] * d[2];
            out[2] = a.u[2] * d[0] + a.v[2] * d[1] + a.w[2] * d[2];
        };
        mulDir(b.u, r.u);
        mulDir(b.v, r.v);
        mulDir(b.w, r.w);
        Vec3f bt = a.Apply({ b.t[0], b.t[1], b.t[2] });
        r.t[0] = bt.x; r.t[1] = bt.y; r.t[2] = bt.z;
        return r;
    }

    float Det3() const {
        return u[0] * (v[1] * w[2] - v[2] * w[1])
             - v[0] * (u[1] * w[2] - u[2] * w[1])
             + w[0] * (u[1] * v[2] - u[2] * v[1]);
    }
};

inline void EmitRepTransformed(const RepLocalMesh& rm, const Xform3dxml& xf, LoadedMesh3D& outMesh) {
    if (rm.verts.empty() || rm.tris.empty()) return;
    const bool flip = (xf.Det3() < 0.0f);
    uint32_t base = static_cast<uint32_t>(outMesh.vertices.size());
    outMesh.vertices.reserve(outMesh.vertices.size() + rm.verts.size());
    for (const auto& p : rm.verts) {
        Vec3f tp = xf.Apply(p);
        if (!std::isfinite(tp.x) || !std::isfinite(tp.y) || !std::isfinite(tp.z)) tp = { 0, 0, 0 };
        outMesh.vertices.push_back(tp);
    }
    outMesh.tris.reserve(outMesh.tris.size() + rm.tris.size());
    for (const auto& t : rm.tris) {
        if (flip) outMesh.tris.push_back({ base + t[0], base + t[2], base + t[1], {} });
        else      outMesh.tris.push_back({ base + t[0], base + t[1], base + t[2], {} });
    }
}

inline std::string ExtractSimpleChildText(std::string_view block, std::string_view tagName) {
    std::string open = "<" + std::string(tagName) + ">";
    std::string close = "</" + std::string(tagName) + ">";
    size_t p1 = block.find(open);
    if (p1 == std::string_view::npos) return {};
    p1 += open.size();
    size_t p2 = block.find(close, p1);
    if (p2 == std::string_view::npos) return {};
    std::string_view inner = ReaderText::TrimSv(block.substr(p1, p2 - p1));
    return std::string(inner);
}

} // namespace DetailCatia3dxml

inline bool ParseCatia3dxml(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    using namespace DetailCatia3dxml;
    if (ext != L".3dxml" || !data || size < 16) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    std::unordered_map<std::string, RepLocalMesh> repById;
    std::vector<RepLocalMesh> allReps;
    std::vector<std::string> productXmlDocs;

    if (size >= 4 && data[0] == 'P' && data[1] == 'K') {
        std::vector<ZipDeflate::ArchiveItemMeta> zEntries;
        if (!ZipDeflate::ListZipEntriesFromMemory(data, size, zEntries)) return false;
        for (const auto& ze : zEntries) {
            if (ze.isDirectory) continue;
            std::wstring lname = PathUtils::ToLower(ze.name);
            if (lname.find(L".3drep") == std::wstring::npos &&
                lname.find(L".xml") == std::wstring::npos &&
                lname.find(L".3dxml") == std::wstring::npos) {
                continue;
            }
            std::vector<uint8_t> part;
            if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, part, 128 * 1024 * 1024) || part.empty()) continue;

            std::string keyName;
            for (wchar_t wc : PathUtils::GetFileName(ze.name)) {
                keyName.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(wc))));
            }
            std::string_view psv(reinterpret_cast<const char*>(part.data()), part.size());
            ExtractRepMeshesFromXml(psv, keyName, repById, allReps);
            if (psv.find("<ProductStructure") != std::string_view::npos || psv.find("<Instance3D") != std::string_view::npos) {
                productXmlDocs.emplace_back(psv);
            }
        }
    } else {
        std::string_view sv(reinterpret_cast<const char*>(data), size);
        if (sv.find("3DXML") == std::string_view::npos && sv.find("<Positions") == std::string_view::npos) return false;
        ExtractRepMeshesFromXml(sv, "", repById, allReps);
        if (sv.find("<ProductStructure") != std::string_view::npos || sv.find("<Instance3D") != std::string_view::npos) {
            productXmlDocs.emplace_back(sv);
        }
    }

    if (allReps.empty()) return false;

    // Try instantiating `<ProductStructure>` (`<Reference3D>`, `<Instance3D>` + `<RelativeMatrix>`, `<ReferenceRep>`, `<InstanceRep>`)
    bool instantiatedAny = false;
    for (const std::string& pdoc : productXmlDocs) {
        std::string_view sv(pdoc);
        std::string rootId(CalCryDetail::ExtractXmlAttr(
            sv.substr(sv.find("<ProductStructure"), std::min<size_t>(256, sv.size() - sv.find("<ProductStructure"))), "root"));

        struct ChildInst3D { std::string targetRef; Xform3dxml xf; };
        std::unordered_map<std::string, std::vector<ChildInst3D>> refChildren;
        std::unordered_map<std::string, std::vector<std::string>> refToRepIds;
        std::unordered_map<std::string, std::string> repIdToFileKey;
        std::set<std::string> allRefIds, childRefIds;

        // Parse `<Reference3D id="..."/>`
        size_t p = 0;
        while ((p = sv.find("<Reference3D", p)) != std::string_view::npos) {
            size_t gt = sv.find('>', p);
            if (gt == std::string_view::npos) break;
            std::string id(CalCryDetail::ExtractXmlAttr(sv.substr(p, gt - p + 1), "id"));
            if (!id.empty()) allRefIds.insert(id);
            p = gt + 1;
        }

        // Parse `<ReferenceRep id="..." associatedFile="urn:3DXML:part.3drep"/>`
        p = 0;
        while ((p = sv.find("<ReferenceRep", p)) != std::string_view::npos) {
            size_t gt = sv.find('>', p);
            if (gt == std::string_view::npos) break;
            std::string_view tag = sv.substr(p, gt - p + 1);
            std::string id(CalCryDetail::ExtractXmlAttr(tag, "id"));
            std::string assoc(CalCryDetail::ExtractXmlAttr(tag, "associatedFile"));
            if (assoc.empty()) {
                size_t closeR = sv.find("</ReferenceRep>", gt);
                if (closeR != std::string_view::npos) assoc = ExtractSimpleChildText(sv.substr(gt + 1, closeR - gt - 1), "associatedFile");
            }
            if (!id.empty() && !assoc.empty()) {
                size_t col = assoc.find_last_of(":/\\");
                std::string fn = (col == std::string::npos) ? assoc : assoc.substr(col + 1);
                for (char& c : fn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                repIdToFileKey[id] = fn;
            }
            p = gt + 1;
        }

        // Parse `<InstanceRep>` (`<IsAggregatedBy>ref3d</IsAggregatedBy>`, `<IsInstanceOf>refRep</IsInstanceOf>`)
        p = 0;
        while ((p = sv.find("<InstanceRep", p)) != std::string_view::npos) {
            size_t closeI = sv.find("</InstanceRep>", p);
            if (closeI == std::string_view::npos) break;
            std::string_view blk = sv.substr(p, closeI - p);
            std::string parentRef = ExtractSimpleChildText(blk, "IsAggregatedBy");
            std::string instOf    = ExtractSimpleChildText(blk, "IsInstanceOf");
            if (!parentRef.empty() && !instOf.empty()) {
                refToRepIds[parentRef].push_back(instOf);
            }
            p = closeI + 14;
        }

        // Parse `<Instance3D>` (`<IsAggregatedBy>`, `<IsInstanceOf>`, `<RelativeMatrix>`)
        p = 0;
        while ((p = sv.find("<Instance3D", p)) != std::string_view::npos) {
            size_t closeI = sv.find("</Instance3D>", p);
            if (closeI == std::string_view::npos) break;
            std::string_view blk = sv.substr(p, closeI - p);
            std::string parentRef = ExtractSimpleChildText(blk, "IsAggregatedBy");
            std::string childRef  = ExtractSimpleChildText(blk, "IsInstanceOf");
            std::string matTxt    = ExtractSimpleChildText(blk, "RelativeMatrix");
            Xform3dxml xf{};
            if (!matTxt.empty()) {
                std::vector<float> f12;
                CalCryDetail::ParseXmlFloatsSimple(matTxt, f12);
                if (f12.size() >= 12) xf = Xform3dxml::From12(f12.data());
            }
            if (!parentRef.empty() && !childRef.empty()) {
                refChildren[parentRef].push_back({ childRef, xf });
                childRefIds.insert(childRef);
            }
            p = closeI + 13;
        }

        auto resolveRepMesh = [&](const std::string& repRefId) -> const RepLocalMesh* {
            auto itHash = repById.find(repRefId.front() == '#' ? repRefId : ("#" + repRefId));
            if (itHash != repById.end()) return &itHash->second;
            auto itDirect = repById.find(repRefId);
            if (itDirect != repById.end()) return &itDirect->second;
            auto itFile = repIdToFileKey.find(repRefId);
            if (itFile != repIdToFileKey.end()) {
                auto itM = repById.find(itFile->second);
                if (itM != repById.end()) return &itM->second;
            }
            return nullptr;
        };

        std::function<void(const std::string&, const Xform3dxml&, int)> walkRef =
            [&](const std::string& refId, const Xform3dxml& worldXf, int depth)
        {
            if (depth > 32) return;
            auto rit = refToRepIds.find(refId);
            if (rit != refToRepIds.end()) {
                for (const std::string& repId : rit->second) {
                    if (const RepLocalMesh* rm = resolveRepMesh(repId)) {
                        EmitRepTransformed(*rm, worldXf, mesh);
                        instantiatedAny = true;
                    }
                }
            }
            auto cit = refChildren.find(refId);
            if (cit != refChildren.end()) {
                for (const auto& ch : cit->second) {
                    walkRef(ch.targetRef, Xform3dxml::Mul(worldXf, ch.xf), depth + 1);
                }
            }
        };

        if (!rootId.empty()) {
            walkRef(rootId, Xform3dxml{}, 0);
        } else {
            for (const std::string& rId : allRefIds) {
                if (childRefIds.find(rId) == childRefIds.end()) {
                    walkRef(rId, Xform3dxml{}, 0);
                }
            }
        }
    }

    if (!instantiatedAny) {
        Xform3dxml ident{};
        for (const auto& rm : allReps) {
            EmitRepTransformed(rm, ident, mesh);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Dassault CATIA / SOLIDWORKS 3DXML (.3dxml)";
    return true;
}

} // namespace Formats
