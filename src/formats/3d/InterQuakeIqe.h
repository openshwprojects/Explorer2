#pragma once
// InterQuakeIqe.h — Inter-Quake Export ASCII Skeletal Model & Animation (.iqe)
// Spec: "# Inter-Quake Export" header, per-mesh `vp` vertex pools, `fm` polygon faces
// (0-based per-mesh or negative relative indices), and `joint` + `pq` skeleton hierarchy.

namespace Formats {

namespace DetailIqe {

inline bool NextIqeToken(std::string_view& line, std::string_view& outTok, bool& outQuoted) {
    outQuoted = false;
    while (!line.empty() && static_cast<unsigned char>(line.front()) <= 0x20u) line.remove_prefix(1u);
    if (line.empty() || line.front() == '#') {
        line = {};
        return false;
    }
    if (line.front() == '"') {
        outQuoted = true;
        line.remove_prefix(1u);
        const size_t q = line.find('"');
        if (q == std::string_view::npos) return false;
        outTok = line.substr(0u, q);
        line.remove_prefix(q + 1u);
        return true;
    }
    size_t e = 0u;
    while (e < line.size() && static_cast<unsigned char>(line[e]) > 0x20u && line[e] != '#') ++e;
    outTok = line.substr(0u, e);
    line.remove_prefix(e);
    return true;
}

struct IqeJoint {
    std::string name;
    int parent = -1;
    Vec3f localPos{ 0.0f, 0.0f, 0.0f };
    struct Quat { float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; } localRot;
    bool hasPose = false;
};

} // namespace DetailIqe

inline bool ParseInterQuakeIqe(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 22u, (64u << 20))) return false;

    size_t linePos = 0u;
    std::string_view rawLine;
    bool sawMagic = false;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        std::string_view trimmed = ReaderText::TrimSv(rawLine);
        if (trimmed.empty()) continue;
        if (!ReaderText::StartsWithCI(trimmed, "# Inter-Quake Export")) return false;
        sawMagic = true;
        break;
    }
    if (!sawMagic) return false;

    outMesh = LoadedMesh3D{};
    size_t meshVertBase = 0u;
    size_t meshCount = 0u;
    std::vector<DetailIqe::IqeJoint> joints;
    bool inAnimation = false;

    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty() || line.front() == '#') continue;
        // Strip trailing ';' used by some IQE exporters
        if (!line.empty() && line.back() == ';') line.remove_suffix(1u);

        std::string_view cmd;
        bool cmdQuoted = false;
        if (!DetailIqe::NextIqeToken(line, cmd, cmdQuoted) || cmdQuoted) return false;

        if (cmd == "mesh") {
            meshVertBase = outMesh.vertices.size();
            ++meshCount;
            inAnimation = false;
        } else if (cmd == "animation" || cmd == "frame") {
            inAnimation = true;
        } else if (cmd == "vp") {
            std::string_view tx, ty, tz;
            bool q0 = false, q1 = false, q2 = false;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (!DetailIqe::NextIqeToken(line, tx, q0) ||
                !DetailIqe::NextIqeToken(line, ty, q1) ||
                !DetailIqe::NextIqeToken(line, tz, q2) ||
                !ReaderText::ParseFloatSv(tx, x) ||
                !ReaderText::ParseFloatSv(ty, y) ||
                !ReaderText::ParseFloatSv(tz, z) ||
                !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                outMesh = {};
                return false;
            }
            if (outMesh.vertices.size() >= 2000000u) { outMesh = {}; return false; }
            outMesh.vertices.push_back({ x, y, z });
        } else if (cmd == "fm") {
            const size_t localCount = outMesh.vertices.size() - meshVertBase;
            std::vector<uint32_t> polyIdx;
            polyIdx.reserve(4u);
            std::string_view idxTok;
            bool q = false;
            while (DetailIqe::NextIqeToken(line, idxTok, q)) {
                if (q) { outMesh = {}; return false; }
                int idx = 0;
                if (!ReaderText::ParseIntSv(idxTok, idx)) { outMesh = {}; return false; }
                int resolved = (idx < 0) ? (static_cast<int>(localCount) + idx) : idx;
                if (resolved < 0 || static_cast<size_t>(resolved) >= localCount) {
                    outMesh = {};
                    return false;
                }
                polyIdx.push_back(static_cast<uint32_t>(meshVertBase + static_cast<size_t>(resolved)));
                if (polyIdx.size() > 256u) { outMesh = {}; return false; }
            }
            if (polyIdx.size() < 3u) { outMesh = {}; return false; }
            // IQE winds faces clockwise by default; fan triangulate
            for (size_t k = 1u; k + 1u < polyIdx.size(); ++k) {
                uint32_t a = polyIdx[0], b = polyIdx[k + 1u], c = polyIdx[k];
                if (a != b && b != c && a != c) {
                    outMesh.tris.push_back({ a, b, c, {} });
                }
            }
        } else if (cmd == "joint") {
            std::string_view jName, parentTok;
            bool qn = false, qp = false;
            if (!DetailIqe::NextIqeToken(line, jName, qn)) { outMesh = {}; return false; }
            int parentIdx = -1;
            if (DetailIqe::NextIqeToken(line, parentTok, qp)) {
                if (!ReaderText::ParseIntSv(parentTok, parentIdx) ||
                    parentIdx < -1 || parentIdx >= static_cast<int>(joints.size())) {
                    outMesh = {};
                    return false;
                }
            }
            if (joints.size() < 4096u) {
                DetailIqe::IqeJoint j{};
                j.name.assign(jName.data(), jName.size());
                j.parent = parentIdx;
                joints.push_back(std::move(j));
            }
        } else if (cmd == "pq" && !inAnimation && !joints.empty()) {
            std::string_view toks[7];
            size_t nt = 0u;
            bool q = false;
            while (nt < 7u && DetailIqe::NextIqeToken(line, toks[nt], q)) ++nt;
            if (nt < 3u) { outMesh = {}; return false; }
            float vals[7] = { 0, 0, 0, 0, 0, 0, 1 };
            for (size_t k = 0u; k < nt; ++k) {
                if (!ReaderText::ParseFloatSv(toks[k], vals[k]) || !std::isfinite(vals[k])) {
                    outMesh = {};
                    return false;
                }
            }
            auto& j = joints.back();
            j.localPos = { vals[0], vals[1], vals[2] };
            if (nt == 6u) {
                const float q2 = vals[3] * vals[3] + vals[4] * vals[4] + vals[5] * vals[5];
                const float qw = (q2 < 1.0f) ? -std::sqrt(1.0f - q2) : 0.0f;
                j.localRot = { vals[3], vals[4], vals[5], qw };
            } else if (nt >= 7u) {
                j.localRot = { vals[3], vals[4], vals[5], vals[6] };
            }
            j.hasPose = true;
        }
    }

    // Pose-only IQE skeletal animation fallback: render world-space joint hierarchy
    if (outMesh.tris.empty() && !joints.empty()) {
        std::vector<Vec3f> worldPos(joints.size(), { 0, 0, 0 });
        std::vector<DetailIqe::IqeJoint::Quat> worldRot(joints.size());
        float span = 0.0f;
        for (size_t i = 0u; i < joints.size(); ++i) {
            const auto& j = joints[i];
            if (j.parent >= 0 && static_cast<size_t>(j.parent) < i) {
                const auto& pr = worldRot[static_cast<size_t>(j.parent)];
                Vec3f rotLocal = MeshBuild3D::QuatRotate(pr, j.localPos);
                worldPos[i] = {
                    worldPos[static_cast<size_t>(j.parent)].x + rotLocal.x,
                    worldPos[static_cast<size_t>(j.parent)].y + rotLocal.y,
                    worldPos[static_cast<size_t>(j.parent)].z + rotLocal.z
                };
                worldRot[i] = MeshBuild3D::QuatMul(pr, j.localRot);
            } else {
                worldPos[i] = j.localPos;
                worldRot[i] = j.localRot;
            }
            span = std::max({ span, std::fabs(worldPos[i].x), std::fabs(worldPos[i].y), std::fabs(worldPos[i].z) });
        }
        const float r = std::max(0.015f, span * 0.025f);
        for (size_t i = 0u; i < joints.size(); ++i) {
            MeshBuild3D::EmitOctahedron(outMesh, worldPos[i], r);
            if (joints[i].parent >= 0 && static_cast<size_t>(joints[i].parent) < i) {
                MeshBuild3D::EmitEdgeStrut(outMesh, worldPos[static_cast<size_t>(joints[i].parent)], worldPos[i], r * 0.45f);
            }
        }
    }

    if (outMesh.vertices.empty() || outMesh.tris.empty()) {
        outMesh = {};
        return false;
    }

    outMesh.formatName = L"Inter-Quake Export (.iqe)";
    (void)meshCount;
    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

} // namespace Formats
