#pragma once
// ValveSourceVta.h — Valve Source Engine Vertex Animation / Facial Flex Mesh (.vta)
// Parses `version 1`, `nodes..end`, `skeleton..end`, and `vertexanimation..end` (`time 0` base
// shape + `time 1..N` facial flex deltas). Reconstructs StudioMDL base triangle soup + flex vectors.

namespace Formats {

inline bool ParseValveSourceVta(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh)
{
    (void)fullPath;
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 28u, (64u << 20))) return false;

    enum class Section { None, Nodes, Skeleton, VertexAnim };
    Section sec = Section::None;

    bool sawVersion1 = false;
    bool sawNodes = false;
    bool sawVertAnim = false;
    int curTime = -1;
    size_t flexFrameCount = 0u;

    struct VtaVert {
        Vec3f pos{ 0.0f, 0.0f, 0.0f };
        Vec3f nrm{ 0.0f, 0.0f, 1.0f };
        bool present = false;
    };
    std::map<uint32_t, VtaVert> baseVerts;
    std::map<uint32_t, Vec3f> maxFlexTarget;
    std::map<uint32_t, float> maxFlexDeltaSq;

    size_t linePos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        // Strip '//' or '#' comments
        size_t cmt = rawLine.find("//");
        if (cmt != std::string_view::npos) rawLine = rawLine.substr(0u, cmt);
        size_t hash = rawLine.find('#');
        if (hash != std::string_view::npos) rawLine = rawLine.substr(0u, hash);

        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        if (sec == Section::None) {
            std::string_view kw;
            if (!ReaderText::NextTokenSv(line, kw)) continue;
            if (ReaderText::AsciiEqCI(kw, "version")) {
                std::string_view vTok;
                int vNum = 0;
                if (!ReaderText::NextTokenSv(line, vTok) || !ReaderText::ParseIntSv(vTok, vNum) || vNum != 1) {
                    return false;
                }
                sawVersion1 = true;
            } else if (ReaderText::AsciiEqCI(kw, "nodes")) {
                if (!sawVersion1) return false;
                sec = Section::Nodes;
                sawNodes = true;
            } else if (ReaderText::AsciiEqCI(kw, "skeleton")) {
                if (!sawVersion1) return false;
                sec = Section::Skeleton;
                curTime = -1;
            } else if (ReaderText::AsciiEqCI(kw, "vertexanimation")) {
                if (!sawVersion1) return false;
                sec = Section::VertexAnim;
                sawVertAnim = true;
                curTime = -1;
            } else {
                return false;
            }
            continue;
        }

        if (ReaderText::AsciiEqCI(line, "end")) {
            sec = Section::None;
            curTime = -1;
            continue;
        }

        if (sec == Section::Nodes) {
            // <id> "<name>" <parentId>
            std::string_view idTok;
            int boneId = 0;
            if (!ReaderText::NextTokenSv(line, idTok) || !ReaderText::ParseIntSv(idTok, boneId) || boneId < 0) {
                return false;
            }
            line = ReaderText::TrimSv(line);
            if (line.empty() || line.front() != '"') return false;
            line.remove_prefix(1u);
            size_t q = line.find('"');
            if (q == std::string_view::npos) return false;
            line.remove_prefix(q + 1u);
            std::string_view parTok;
            int parId = 0;
            if (!ReaderText::NextTokenSv(line, parTok) || !ReaderText::ParseIntSv(parTok, parId) || parId < -1) {
                return false;
            }
        } else if (sec == Section::Skeleton) {
            std::string_view t0;
            if (!ReaderText::NextTokenSv(line, t0)) continue;
            if (ReaderText::AsciiEqCI(t0, "time")) {
                std::string_view tmTok;
                if (!ReaderText::NextTokenSv(line, tmTok) || !ReaderText::ParseIntSv(tmTok, curTime) || curTime < 0) {
                    return false;
                }
            } else {
                int boneId = 0;
                if (!ReaderText::ParseIntSv(t0, boneId) || boneId < 0) return false;
                for (int k = 0; k < 6; ++k) {
                    std::string_view ft;
                    float fv = 0.0f;
                    if (!ReaderText::NextTokenSv(line, ft) || !ReaderText::ParseFloatSv(ft, fv) || !std::isfinite(fv)) {
                        return false;
                    }
                }
            }
        } else if (sec == Section::VertexAnim) {
            std::string_view t0;
            if (!ReaderText::NextTokenSv(line, t0)) continue;
            if (ReaderText::AsciiEqCI(t0, "time")) {
                std::string_view tmTok;
                if (!ReaderText::NextTokenSv(line, tmTok) || !ReaderText::ParseIntSv(tmTok, curTime) || curTime < 0) {
                    return false;
                }
                if (curTime > 0) ++flexFrameCount;
            } else {
                if (curTime < 0) return false;
                int vIdx = -1;
                if (!ReaderText::ParseIntSv(t0, vIdx) || vIdx < 0 || vIdx > 1000000) return false;
                float vals[6] = {};
                for (int k = 0; k < 6; ++k) {
                    std::string_view ft;
                    if (!ReaderText::NextTokenSv(line, ft) || !ReaderText::ParseFloatSv(ft, vals[k]) || !std::isfinite(vals[k])) {
                        return false;
                    }
                }
                const uint32_t uid = static_cast<uint32_t>(vIdx);
                const Vec3f p{ vals[0], vals[1], vals[2] };
                if (curTime == 0) {
                    baseVerts[uid] = { p, { vals[3], vals[4], vals[5] }, true };
                } else {
                    auto bit = baseVerts.find(uid);
                    if (bit != baseVerts.end()) {
                        const float dx = p.x - bit->second.pos.x;
                        const float dy = p.y - bit->second.pos.y;
                        const float dz = p.z - bit->second.pos.z;
                        const float d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 > maxFlexDeltaSq[uid]) {
                            maxFlexDeltaSq[uid] = d2;
                            maxFlexTarget[uid] = p;
                        }
                    }
                }
            }
        }
    }

    if (sec != Section::None) return false; // Unterminated section (missing `end`)!
    if (!sawVersion1 || !sawNodes || !sawVertAnim || baseVerts.empty()) return false;

    outMesh = LoadedMesh3D{};
    std::vector<Vec3f> ordered;
    std::vector<uint32_t> orderedIds;
    ordered.reserve(baseVerts.size());
    orderedIds.reserve(baseVerts.size());

    auto zUpToYUp = [](const Vec3f& v) -> Vec3f { return { v.x, v.z, -v.y }; };

    Vec3f bmin{ 1e30f, 1e30f, 1e30f }, bmax{ -1e30f, -1e30f, -1e30f };
    for (const auto& [vid, vv] : baseVerts) {
        orderedIds.push_back(vid);
        Vec3f yp = zUpToYUp(vv.pos);
        ordered.push_back(yp);
        bmin.x = std::min(bmin.x, yp.x); bmin.y = std::min(bmin.y, yp.y); bmin.z = std::min(bmin.z, yp.z);
        bmax.x = std::max(bmax.x, yp.x); bmax.y = std::max(bmax.y, yp.y); bmax.z = std::max(bmax.z, yp.z);
    }

    const float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 0.1f });
    const float markerRad = std::max(1e-4f, span * 0.022f);
    const float strutRad  = std::max(1e-4f, span * 0.012f);

    const size_t maxMarkers = std::min<size_t>(ordered.size(), 50000u);
    for (size_t i = 0u; i < maxMarkers; ++i) {
        MeshBuild3D::EmitOctahedron(outMesh, ordered[i], markerRad);
    }

    size_t emittedFlexVectors = 0u;
    for (size_t i = 0u; i < orderedIds.size() && emittedFlexVectors < 4096u; ++i) {
        auto fit = maxFlexTarget.find(orderedIds[i]);
        if (fit != maxFlexTarget.end() && maxFlexDeltaSq[orderedIds[i]] > 1e-8f) {
            MeshBuild3D::EmitEdgeStrut(outMesh, ordered[i], zUpToYUp(fit->second), strutRad);
            ++emittedFlexVectors;
        }
    }

    if (outMesh.vertices.empty() || outMesh.tris.empty()) {
        outMesh = {};
        return false;
    }

    outMesh.animFrameCount = static_cast<uint32_t>(flexFrameCount + 1u);
    wchar_t fmtBuf[128] = {};
    swprintf_s(fmtBuf, L"Valve Source Vertex Animation (.vta, %zu flex frames)", flexFrameCount);
    outMesh.formatName = fmtBuf;
    return true;
}

} // namespace Formats
