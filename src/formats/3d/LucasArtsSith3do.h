#pragma once
// LucasArtsSith3do.h — LucasArts Sith Engine 3D Mesh (.3do — Star Wars Jedi Knight / Indiana Jones)
// Parses `SECTION: HEADER` (`3DO 2.1`–`2.3`), `SECTION: GEOMETRYDEF` (`GEOSET 0` -> `MESH k` ->
// `VERTICES` + `FACES`), and `SECTION: HIERARCHYDEF` (`HIERARCHY NODES` parent/pivot transforms).

namespace Formats {

namespace DetailSith3do {

struct SithMeshPart {
    std::vector<Vec3f> verts;
    std::vector<std::array<uint32_t, 3>> tris;
};

struct SithHierNode {
    int parent = -1;
    int meshIdx = -1;
    Vec3f pos{ 0.0f, 0.0f, 0.0f };
    Vec3f pivot{ 0.0f, 0.0f, 0.0f };
};

inline bool NextSithTok(std::string_view& s, std::string_view& outTok) {
    while (!s.empty() && (static_cast<unsigned char>(s.front()) <= 0x20u || s.front() == ',' || s.front() == ':')) {
        s.remove_prefix(1u);
    }
    if (s.empty() || s.front() == '#') { s = {}; return false; }
    size_t e = 0u;
    while (e < s.size() && static_cast<unsigned char>(s[e]) > 0x20u && s[e] != ',' && s[e] != ':' && s[e] != '#') {
        ++e;
    }
    outTok = s.substr(0u, e);
    s.remove_prefix(e);
    return true;
}

} // namespace DetailSith3do

inline bool ParseLucasArtsSith3do(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 32u, (64u << 20))) return false;

    bool sawHeaderSection = false;
    bool saw3doVersion    = false;
    bool sawGeomSection   = false;
    std::string curSection;

    std::vector<DetailSith3do::SithMeshPart> meshes;
    int activeMesh = -1;
    enum class SubTable { None, Verts, TexVerts, Faces, HierNodes };
    SubTable sub = SubTable::None;
    int subRemaining = 0;
    std::vector<DetailSith3do::SithHierNode> hierNodes;

    size_t linePos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        size_t hash = rawLine.find('#');
        if (hash != std::string_view::npos) rawLine = rawLine.substr(0u, hash);
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        if (ReaderText::StartsWithCI(line, "SECTION")) {
            std::string_view rest = line.substr(7u);
            std::string_view secName;
            if (!DetailSith3do::NextSithTok(rest, secName)) return false;
            curSection = ReaderText::ToUpperAscii(secName);
            if (curSection == "HEADER") sawHeaderSection = true;
            if (curSection == "GEOMETRYDEF") sawGeomSection = true;
            sub = SubTable::None;
            subRemaining = 0;
            continue;
        }

        if (curSection == "HEADER") {
            std::string_view t0, t1;
            std::string_view rest = line;
            if (DetailSith3do::NextSithTok(rest, t0) && ReaderText::AsciiEqCI(t0, "3DO") &&
                DetailSith3do::NextSithTok(rest, t1)) {
                float ver = 0.0f;
                if (!ReaderText::ParseFloatSv(t1, ver) || ver < 2.0f || ver > 3.0f) return false;
                saw3doVersion = true;
            }
            continue;
        }

        if (subRemaining > 0) {
            std::string_view rest = line;
            if (sub == SubTable::Verts && activeMesh >= 0) {
                // <idx>: <x> <y> <z> <intensity>
                std::string_view idxTok, tx, ty, tz;
                float x = 0, y = 0, z = 0;
                if (!DetailSith3do::NextSithTok(rest, idxTok) ||
                    !DetailSith3do::NextSithTok(rest, tx) ||
                    !DetailSith3do::NextSithTok(rest, ty) ||
                    !DetailSith3do::NextSithTok(rest, tz) ||
                    !ReaderText::ParseFloatSv(tx, x) ||
                    !ReaderText::ParseFloatSv(ty, y) ||
                    !ReaderText::ParseFloatSv(tz, z) ||
                    !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                    return false;
                }
                meshes[static_cast<size_t>(activeMesh)].verts.push_back({ x, y, z });
                --subRemaining;
                continue;
            }
            if (sub == SubTable::TexVerts) {
                --subRemaining;
                continue;
            }
            if (sub == SubTable::Faces && activeMesh >= 0) {
                // <num>: <mat> <type> <geo> <light> <tex> <extralight> <numVerts>  <v0>,<tv0> <v1>,<tv1> ...
                std::string_view hdrToks[8];
                for (int k = 0; k < 8; ++k) {
                    if (!DetailSith3do::NextSithTok(rest, hdrToks[k])) return false;
                }
                int nFaceVerts = 0;
                if (!ReaderText::ParseIntSv(hdrToks[7], nFaceVerts) || nFaceVerts < 3 || nFaceVerts > 64) {
                    return false;
                }
                auto& mp = meshes[static_cast<size_t>(activeMesh)];
                std::vector<uint32_t> fIdx(static_cast<size_t>(nFaceVerts));
                for (int k = 0; k < nFaceVerts; ++k) {
                    std::string_view vTok, tvTok;
                    int vi = -1;
                    if (!DetailSith3do::NextSithTok(rest, vTok) ||
                        !DetailSith3do::NextSithTok(rest, tvTok) ||
                        !ReaderText::ParseIntSv(vTok, vi) ||
                        vi < 0 || static_cast<size_t>(vi) >= mp.verts.size()) {
                        return false;
                    }
                    fIdx[static_cast<size_t>(k)] = static_cast<uint32_t>(vi);
                }
                for (int k = 1; k + 1 < nFaceVerts; ++k) {
                    mp.tris.push_back({ fIdx[0], fIdx[static_cast<size_t>(k)], fIdx[static_cast<size_t>(k + 1)] });
                }
                --subRemaining;
                continue;
            }
            if (sub == SubTable::HierNodes) {
                // <num>: <flags> <type> <mesh> <parent> <child> <sibling> <numChildren> <x> <y> <z> <pitch> <yaw> <roll> <pivx> <pivy> <pivz> <hnname>
                std::string_view toks[17];
                for (int k = 0; k < 17; ++k) {
                    if (!DetailSith3do::NextSithTok(rest, toks[k])) return false;
                }
                DetailSith3do::SithHierNode hn{};
                if (!ReaderText::ParseIntSv(toks[3], hn.meshIdx) ||
                    !ReaderText::ParseIntSv(toks[4], hn.parent)  ||
                    !ReaderText::ParseFloatSv(toks[8],  hn.pos.x) ||
                    !ReaderText::ParseFloatSv(toks[9],  hn.pos.y) ||
                    !ReaderText::ParseFloatSv(toks[10], hn.pos.z) ||
                    !ReaderText::ParseFloatSv(toks[14], hn.pivot.x) ||
                    !ReaderText::ParseFloatSv(toks[15], hn.pivot.y) ||
                    !ReaderText::ParseFloatSv(toks[16], hn.pivot.z)) {
                    return false;
                }
                hierNodes.push_back(hn);
                --subRemaining;
                continue;
            }
        }

        std::string_view rest = line;
        std::string_view kw;
        if (!DetailSith3do::NextSithTok(rest, kw)) continue;

        if (curSection == "GEOMETRYDEF") {
            if (ReaderText::AsciiEqCI(kw, "MESH")) {
                std::string_view idxTok;
                int mIdx = -1;
                if (DetailSith3do::NextSithTok(rest, idxTok) && ReaderText::ParseIntSv(idxTok, mIdx) &&
                    mIdx >= 0 && mIdx < 4096) {
                    if (static_cast<size_t>(mIdx) >= meshes.size()) meshes.resize(static_cast<size_t>(mIdx) + 1u);
                    activeMesh = mIdx;
                }
            } else if (ReaderText::AsciiEqCI(kw, "VERTICES") && activeMesh >= 0) {
                std::string_view cTok;
                if (!DetailSith3do::NextSithTok(rest, cTok) || !ReaderText::ParseIntSv(cTok, subRemaining) ||
                    subRemaining <= 0 || subRemaining > 500000) {
                    return false;
                }
                sub = SubTable::Verts;
            } else if (ReaderText::AsciiEqCI(kw, "TEXTURE")) {
                std::string_view kw2, cTok;
                if (DetailSith3do::NextSithTok(rest, kw2) && ReaderText::AsciiEqCI(kw2, "VERTICES") &&
                    DetailSith3do::NextSithTok(rest, cTok) && ReaderText::ParseIntSv(cTok, subRemaining) &&
                    subRemaining >= 0) {
                    sub = (subRemaining > 0) ? SubTable::TexVerts : SubTable::None;
                }
            } else if (ReaderText::AsciiEqCI(kw, "FACES") && activeMesh >= 0) {
                std::string_view cTok;
                if (!DetailSith3do::NextSithTok(rest, cTok) || !ReaderText::ParseIntSv(cTok, subRemaining) ||
                    subRemaining < 0 || subRemaining > 500000) {
                    return false;
                }
                sub = (subRemaining > 0) ? SubTable::Faces : SubTable::None;
            }
        } else if (curSection == "HIERARCHYDEF") {
            if (ReaderText::AsciiEqCI(kw, "HIERARCHY")) {
                std::string_view kw2, cTok;
                if (DetailSith3do::NextSithTok(rest, kw2) && ReaderText::AsciiEqCI(kw2, "NODES") &&
                    DetailSith3do::NextSithTok(rest, cTok) && ReaderText::ParseIntSv(cTok, subRemaining) &&
                    subRemaining > 0 && subRemaining <= 4096) {
                    sub = SubTable::HierNodes;
                }
            }
        }
    }

    if (!sawHeaderSection || !saw3doVersion || !sawGeomSection || subRemaining != 0) return false;

    // Compute world node offsets from HIERARCHY NODES if present
    std::unordered_map<int, Vec3f> meshWorldOffset;
    if (!hierNodes.empty()) {
        std::vector<Vec3f> nodeWorld(hierNodes.size(), { 0, 0, 0 });
        for (size_t i = 0u; i < hierNodes.size(); ++i) {
            Vec3f base{ 0, 0, 0 };
            int p = hierNodes[i].parent;
            if (p >= 0 && static_cast<size_t>(p) < i) base = nodeWorld[static_cast<size_t>(p)];
            nodeWorld[i] = {
                base.x + hierNodes[i].pos.x,
                base.y + hierNodes[i].pos.y,
                base.z + hierNodes[i].pos.z
            };
            if (hierNodes[i].meshIdx >= 0) {
                meshWorldOffset[hierNodes[i].meshIdx] = {
                    nodeWorld[i].x + hierNodes[i].pivot.x,
                    nodeWorld[i].y + hierNodes[i].pivot.y,
                    nodeWorld[i].z + hierNodes[i].pivot.z
                };
            }
        }
    }

    outMesh = LoadedMesh3D{};
    for (size_t mi = 0u; mi < meshes.size(); ++mi) {
        const auto& mp = meshes[mi];
        if (mp.verts.empty() || mp.tris.empty()) continue;
        Vec3f off{ 0, 0, 0 };
        auto it = meshWorldOffset.find(static_cast<int>(mi));
        if (it != meshWorldOffset.end()) off = it->second;

        const uint32_t baseV = static_cast<uint32_t>(outMesh.vertices.size());
        for (const auto& v : mp.verts) {
            outMesh.vertices.push_back({ v.x + off.x, v.y + off.y, v.z + off.z });
        }
        for (const auto& t : mp.tris) {
            if (t[0] != t[1] && t[1] != t[2] && t[0] != t[2]) {
                outMesh.tris.push_back({ baseV + t[0], baseV + t[1], baseV + t[2], {} });
            }
        }
    }

    if (outMesh.vertices.empty() || outMesh.tris.empty()) {
        outMesh = {};
        return false;
    }

    outMesh.formatName = L"LucasArts Sith Engine Model (.3do)";
    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

} // namespace Formats
