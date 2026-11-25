#pragma once
// AbaqusInpMesh.h — Abaqus / CalculiX Finite Element Input Deck (.inp) 3D Mesh Reader
// Parses *NODE, *ELEMENT (TYPE=C3D4, C3D6, C3D8/R/I/H, C3D10, C3D15, C3D20/R, C3D5, S3/R, STRI3,
// S4/R, S8R, CPS3/4, CPE3/4, CAX3/4, M3D3/4, B31/T3D2), *PART / *END PART, and *ASSEMBLY -> *INSTANCE
// (with 3D translation & axis-angle rotation transforms), extracting exterior boundary surfaces.
#include "../../ReaderTextUtils.h"
#include "../common/FemSolidSurfaceCommon.h"

namespace Formats {

namespace DetailAbaqusInp {

enum class AbaqusElemKind : uint8_t {
    Unknown = 0,
    Tet4,      // C3D4, C3D10, DC3D4, DC3D10
    Pyr5,      // C3D5
    Wedge6,    // C3D6, C3D15, DC3D6
    Hex8,      // C3D8*, C3D20*, DC3D8
    Tri3,      // S3*, STRI*, CPS3, CPE3, CAX3, M3D3, R3D3
    Quad4,     // S4*, S8*, CPS4*, CPE4*, CAX4*, M3D4*, R3D4
    Beam2      // B31, B32, T3D2, B21
};

struct AbaqusElementRec {
    AbaqusElemKind kind = AbaqusElemKind::Unknown;
    uint32_t cornerIds[8] = {};
    uint8_t numCorners = 0;
};

struct AbaqusPartData {
    std::string nameUpper;
    std::unordered_map<int64_t, uint32_t> idToLocal;
    std::vector<Vec3f> nodes;
    std::vector<AbaqusElementRec> elems;
};

struct AbaqusInstance {
    std::string partUpper;
    Vec3f trans = { 0.0f, 0.0f, 0.0f };
    bool hasRot = false;
    Vec3f rotA = { 0.0f, 0.0f, 0.0f };
    Vec3f rotB = { 0.0f, 0.0f, 1.0f };
    float rotDeg = 0.0f;
    // Inline instance nodes/elements (when *INSTANCE has no PART= or adds local mesh)
    AbaqusPartData inlinePart;

    Vec3f TransformPoint(const Vec3f& p) const {
        Vec3f q = { p.x + trans.x, p.y + trans.y, p.z + trans.z };
        if (!hasRot || std::fabs(rotDeg) < 1e-6f) return q;
        float ax = rotB.x - rotA.x, ay = rotB.y - rotA.y, az = rotB.z - rotA.z;
        float len = std::sqrt(ax * ax + ay * ay + az * az);
        if (len < 1e-7f) return q;
        ax /= len; ay /= len; az /= len;
        float rad = rotDeg * (3.14159265358979323846f / 180.0f);
        float c = std::cos(rad), s = std::sin(rad), ic = 1.0f - c;
        float rx = q.x - rotA.x, ry = q.y - rotA.y, rz = q.z - rotA.z;
        float dot = ax * rx + ay * ry + az * rz;
        float cx = ay * rz - az * ry;
        float cy = az * rx - ax * rz;
        float cz = ax * ry - ay * rx;
        return {
            rotA.x + rx * c + cx * s + ax * dot * ic,
            rotA.y + ry * c + cy * s + ay * dot * ic,
            rotA.z + rz * c + cz * s + az * dot * ic
        };
    }
};

inline AbaqusElemKind ClassifyAbaqusElementType(std::string_view rawType, uint8_t& outMinNodes, uint8_t& outCorners) {
    std::string t = ReaderText::ToUpperAscii(ReaderText::TrimSv(rawType));
    // Strip quotes if any
    if (t.size() >= 2 && (t.front() == '"' || t.front() == '\'') && t.back() == t.front()) {
        t = t.substr(1, t.size() - 2);
    }
    if (t.starts_with("C3D4") || t.starts_with("C3D10") || t.starts_with("DC3D4") || t.starts_with("DC3D10") ||
        t.starts_with("FC3D4") || t.starts_with("AC3D4") || t.starts_with("AC3D10"))
    {
        outMinNodes = t.find("10") != std::string::npos ? 10 : 4;
        outCorners = 4;
        return AbaqusElemKind::Tet4;
    }
    if (t.starts_with("C3D5") || t.starts_with("DC3D5") || t.starts_with("FC3D5")) {
        outMinNodes = 5; outCorners = 5;
        return AbaqusElemKind::Pyr5;
    }
    if (t.starts_with("C3D6") || t.starts_with("C3D15") || t.starts_with("DC3D6") || t.starts_with("DC3D15") ||
        t.starts_with("SC6") || t.starts_with("FC3D6"))
    {
        outMinNodes = t.find("15") != std::string::npos ? 15 : 6;
        outCorners = 6;
        return AbaqusElemKind::Wedge6;
    }
    if (t.starts_with("C3D8") || t.starts_with("C3D20") || t.starts_with("DC3D8") || t.starts_with("DC3D20") ||
        t.starts_with("SC8") || t.starts_with("FC3D8") || t.starts_with("EC3D8"))
    {
        outMinNodes = t.find("20") != std::string::npos ? 20 : 8;
        outCorners = 8;
        return AbaqusElemKind::Hex8;
    }
    if (t.starts_with("S3") || t.starts_with("STRI3") || t.starts_with("STRI65") ||
        t.starts_with("CPS3") || t.starts_with("CPE3") || t.starts_with("CAX3") ||
        t.starts_with("CPS6") || t.starts_with("CPE6") || t.starts_with("CAX6") ||
        t.starts_with("M3D3") || t.starts_with("M3D6") || t.starts_with("R3D3") ||
        t.starts_with("DS3")  || t.starts_with("DS6")  || t.starts_with("SFM3D3"))
    {
        outMinNodes = (t.find('6') != std::string::npos) ? 6 : 3;
        outCorners = 3;
        return AbaqusElemKind::Tri3;
    }
    if (t.starts_with("S4") || t.starts_with("S8") || t.starts_with("S9") ||
        t.starts_with("CPS4") || t.starts_with("CPE4") || t.starts_with("CAX4") ||
        t.starts_with("CPS8") || t.starts_with("CPE8") || t.starts_with("CAX8") ||
        t.starts_with("M3D4") || t.starts_with("M3D8") || t.starts_with("R3D4") ||
        t.starts_with("DS4")  || t.starts_with("DS8")  || t.starts_with("SFM3D4"))
    {
        outMinNodes = (t.find('8') != std::string::npos) ? 8 : 4;
        outCorners = 4;
        return AbaqusElemKind::Quad4;
    }
    if (t.starts_with("B31") || t.starts_with("B32") || t.starts_with("B21") || t.starts_with("B22") ||
        t.starts_with("T3D2") || t.starts_with("T3D3") || t.starts_with("T2D2") || t.starts_with("CONN3D2"))
    {
        outMinNodes = 2; outCorners = 2;
        return AbaqusElemKind::Beam2;
    }
    outMinNodes = 0; outCorners = 0;
    return AbaqusElemKind::Unknown;
}

inline std::string GetKeywordParamUpper(std::string_view kwLine, std::string_view keyUpper) {
    size_t pos = 0;
    while (pos < kwLine.size()) {
        size_t comma = kwLine.find(',', pos);
        std::string_view seg = (comma == std::string_view::npos)
            ? kwLine.substr(pos)
            : kwLine.substr(pos, comma - pos);
        pos = (comma == std::string_view::npos) ? kwLine.size() : (comma + 1);
        size_t eq = seg.find('=');
        if (eq == std::string_view::npos) continue;
        std::string k = ReaderText::ToUpperAscii(ReaderText::TrimSv(seg.substr(0, eq)));
        if (k == keyUpper) {
            return ReaderText::ToUpperAscii(ReaderText::TrimSv(seg.substr(eq + 1)));
        }
    }
    return {};
}

inline void SplitCsvFields(std::string_view ln, std::vector<std::string_view>& out) {
    out.clear();
    size_t pos = 0;
    while (pos <= ln.size()) {
        size_t comma = ln.find(',', pos);
        std::string_view f = (comma == std::string_view::npos) ? ln.substr(pos) : ln.substr(pos, comma - pos);
        f = ReaderText::TrimSv(f);
        if (!f.empty()) out.push_back(f);
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
}

inline void AppendPartToBuilder(
    const AbaqusPartData& part,
    const AbaqusInstance* inst,
    std::vector<Vec3f>& globalNodes,
    FemSolidSurface::FemSurfaceBuilder& builder)
{
    if (part.nodes.empty() || part.elems.empty()) return;
    const uint32_t baseIdx = static_cast<uint32_t>(globalNodes.size());
    globalNodes.reserve(globalNodes.size() + part.nodes.size());
    for (const Vec3f& p : part.nodes) {
        globalNodes.push_back(inst ? inst->TransformPoint(p) : p);
    }

    auto mapId = [&](uint32_t rawNodeId, uint32_t& outGlobal) -> bool {
        auto it = part.idToLocal.find(static_cast<int64_t>(rawNodeId));
        if (it == part.idToLocal.end()) return false;
        outGlobal = baseIdx + it->second;
        return true;
    };

    for (const auto& el : part.elems) {
        uint32_t g[8] = {};
        bool ok = true;
        for (uint8_t k = 0; k < el.numCorners; ++k) {
            if (!mapId(el.cornerIds[k], g[k])) { ok = false; break; }
        }
        if (!ok) continue;
        switch (el.kind) {
        case AbaqusElemKind::Tet4:   builder.AddTet4(g); break;
        case AbaqusElemKind::Pyr5:   builder.AddPyramid5(g); break;
        case AbaqusElemKind::Wedge6: builder.AddWedge6(g); break;
        case AbaqusElemKind::Hex8:   builder.AddHex8(g); break;
        case AbaqusElemKind::Tri3:   builder.AddTriShell(g[0], g[1], g[2]); break;
        case AbaqusElemKind::Quad4:  builder.AddQuadShell(g[0], g[1], g[2], g[3]); break;
        case AbaqusElemKind::Beam2:  builder.AddBeamEdge(g[0], g[1]); break;
        default: break;
        }
    }
}

} // namespace DetailAbaqusInp

inline bool ParseAbaqusInpMesh(
    const uint8_t* data,
    size_t size,
    const std::wstring& /*fullPath*/,
    LoadedMesh3D& mesh)
{
    using namespace DetailAbaqusInp;
    if (!data || size < 16) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.find('\0') != std::string_view::npos) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    AbaqusPartData rootPart;
    std::unordered_map<std::string, AbaqusPartData> namedParts;
    std::vector<AbaqusInstance> instances;

    AbaqusPartData* activePart = &rootPart;
    AbaqusInstance* activeInstance = nullptr;
    int instanceDataLineIdx = -1;

    enum class SectionMode { None, Node, Element, InstanceHeader };
    SectionMode secMode = SectionMode::None;
    AbaqusElemKind curElemKind = AbaqusElemKind::Unknown;
    uint8_t curElemMinNodes = 0;
    uint8_t curElemCorners  = 0;
    std::vector<int64_t> pendingElemInts;
    bool pendingElemCont = false;

    auto flushPendingElement = [&]() {
        if (curElemKind != AbaqusElemKind::Unknown &&
            pendingElemInts.size() >= static_cast<size_t>(curElemCorners) + 1u && activePart)
        {
            AbaqusElementRec rec;
            rec.kind = curElemKind;
            rec.numCorners = curElemCorners;
            for (uint8_t k = 0; k < curElemCorners; ++k) {
                rec.cornerIds[k] = static_cast<uint32_t>(pendingElemInts[1u + k]);
            }
            activePart->elems.push_back(rec);
        }
        pendingElemInts.clear();
        pendingElemCont = false;
    };

    size_t pos = 0;
    std::string_view rawLine;
    std::vector<std::string_view> fields;
    bool sawAbaqusKeyword = false;

    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        std::string_view ln = ReaderText::TrimSv(rawLine);
        if (ln.empty() || ln.starts_with("**")) continue;

        if (ln.front() == '*') {
            flushPendingElement();
            size_t comma = ln.find(',');
            std::string kw = ReaderText::ToUpperAscii(ReaderText::TrimSv(
                (comma == std::string_view::npos) ? ln.substr(1) : ln.substr(1, comma - 1)));

            if (kw == "PART") {
                sawAbaqusKeyword = true;
                std::string pname = GetKeywordParamUpper(ln, "NAME");
                if (pname.empty()) pname = "DEFAULT_PART";
                auto& p = namedParts[pname];
                p.nameUpper = pname;
                activePart = &p;
                activeInstance = nullptr;
                secMode = SectionMode::None;
            } else if (kw == "END PART" || kw == "ENDPART") {
                activePart = &rootPart;
                secMode = SectionMode::None;
            } else if (kw == "ASSEMBLY") {
                sawAbaqusKeyword = true;
                activePart = &rootPart;
                secMode = SectionMode::None;
            } else if (kw == "END ASSEMBLY" || kw == "ENDASSEMBLY") {
                activeInstance = nullptr;
                activePart = &rootPart;
                secMode = SectionMode::None;
            } else if (kw == "INSTANCE") {
                sawAbaqusKeyword = true;
                AbaqusInstance inst;
                inst.partUpper = GetKeywordParamUpper(ln, "PART");
                instances.push_back(std::move(inst));
                activeInstance = &instances.back();
                activePart = &activeInstance->inlinePart;
                instanceDataLineIdx = 0;
                secMode = SectionMode::InstanceHeader;
            } else if (kw == "END INSTANCE" || kw == "ENDINSTANCE") {
                activeInstance = nullptr;
                activePart = &rootPart;
                secMode = SectionMode::None;
            } else if (kw == "NODE") {
                sawAbaqusKeyword = true;
                secMode = SectionMode::Node;
            } else if (kw == "ELEMENT") {
                sawAbaqusKeyword = true;
                std::string etype = GetKeywordParamUpper(ln, "TYPE");
                curElemKind = ClassifyAbaqusElementType(etype, curElemMinNodes, curElemCorners);
                secMode = (curElemKind != AbaqusElemKind::Unknown) ? SectionMode::Element : SectionMode::None;
            } else {
                secMode = SectionMode::None;
            }
            continue;
        }

        if (secMode == SectionMode::InstanceHeader && activeInstance) {
            SplitCsvFields(ln, fields);
            if (instanceDataLineIdx == 0 && fields.size() >= 3) {
                float tx = 0, ty = 0, tz = 0;
                if (ReaderText::ParseFloatSv(fields[0], tx) &&
                    ReaderText::ParseFloatSv(fields[1], ty) &&
                    ReaderText::ParseFloatSv(fields[2], tz))
                {
                    activeInstance->trans = { tx, ty, tz };
                }
                ++instanceDataLineIdx;
            } else if (instanceDataLineIdx == 1 && fields.size() >= 7) {
                float ax = 0, ay = 0, az = 0, bx = 0, by = 0, bz = 1, deg = 0;
                if (ReaderText::ParseFloatSv(fields[0], ax) && ReaderText::ParseFloatSv(fields[1], ay) &&
                    ReaderText::ParseFloatSv(fields[2], az) && ReaderText::ParseFloatSv(fields[3], bx) &&
                    ReaderText::ParseFloatSv(fields[4], by) && ReaderText::ParseFloatSv(fields[5], bz) &&
                    ReaderText::ParseFloatSv(fields[6], deg))
                {
                    activeInstance->hasRot = true;
                    activeInstance->rotA = { ax, ay, az };
                    activeInstance->rotB = { bx, by, bz };
                    activeInstance->rotDeg = deg;
                }
                ++instanceDataLineIdx;
            }
        } else if (secMode == SectionMode::Node && activePart) {
            SplitCsvFields(ln, fields);
            if (fields.size() >= 3) {
                int64_t nid = 0;
                float x = 0.0f, y = 0.0f, z = 0.0f;
                if (ReaderText::ParseInt64Sv(fields[0], nid) && nid > 0 &&
                    ReaderText::ParseFloatSv(fields[1], x) &&
                    ReaderText::ParseFloatSv(fields[2], y))
                {
                    if (fields.size() >= 4) (void)ReaderText::ParseFloatSv(fields[3], z);
                    auto [it, inserted] = activePart->idToLocal.try_emplace(nid, static_cast<uint32_t>(activePart->nodes.size()));
                    if (inserted) {
                        activePart->nodes.push_back({ x, y, z });
                    } else {
                        activePart->nodes[it->second] = { x, y, z };
                    }
                }
            }
        } else if (secMode == SectionMode::Element && activePart) {
            const bool endsWithComma = (!ln.empty() && ln.back() == ',');
            if (!pendingElemCont && !pendingElemInts.empty() &&
                pendingElemInts.size() >= static_cast<size_t>(curElemMinNodes) + 1u)
            {
                flushPendingElement();
            }
            SplitCsvFields(ln, fields);
            for (std::string_view f : fields) {
                int64_t v = 0;
                if (ReaderText::ParseInt64Sv(f, v) && v > 0) pendingElemInts.push_back(v);
            }
            pendingElemCont = endsWithComma;
            if (!pendingElemCont && pendingElemInts.size() >= static_cast<size_t>(curElemMinNodes) + 1u) {
                flushPendingElement();
            }
        }
    }
    flushPendingElement();

    if (!sawAbaqusKeyword) return false;

    std::vector<Vec3f> globalNodes;
    FemSolidSurface::FemSurfaceBuilder builder;

    if (!instances.empty()) {
        for (const auto& inst : instances) {
            if (!inst.partUpper.empty()) {
                auto it = namedParts.find(inst.partUpper);
                if (it != namedParts.end()) {
                    AppendPartToBuilder(it->second, &inst, globalNodes, builder);
                }
            }
            if (!inst.inlinePart.elems.empty()) {
                AppendPartToBuilder(inst.inlinePart, &inst, globalNodes, builder);
            } else if (!inst.inlinePart.nodes.empty() && !rootPart.elems.empty()) {
                // Dependent/independent assembly mesh where elements are in assembly root
            }
        }
    }
    if (!rootPart.elems.empty()) {
        AppendPartToBuilder(rootPart, nullptr, globalNodes, builder);
    }
    if (globalNodes.empty() && !namedParts.empty()) {
        for (const auto& kv : namedParts) {
            AppendPartToBuilder(kv.second, nullptr, globalNodes, builder);
        }
    }

    if (!builder.EmitBoundaryToMesh(globalNodes, mesh)) return false;
    mesh.formatName = L"Abaqus / CalculiX FEM Input Deck (.inp)";
    return true;
}

} // namespace Formats
