#pragma once
// TetGenSmesh.h — TetGen Piecewise Linear Complex (.smesh) 3D surface mesh reader
// Supports 0-based, 1-based, and arbitrary integer node IDs, optional companion .node files
// when the .smesh node header specifies 0 inline nodes, triangle facets, and N-gon ear-clipping.

namespace Formats {

namespace DetailTetGenSmesh {

inline bool NextNonCommentLine(std::string_view sv, size_t& pos, std::string_view& outLine) {
    std::string_view raw;
    while (ReaderText::NextLineSv(sv, pos, raw)) {
        const size_t hp = raw.find('#');
        if (hp != std::string_view::npos) raw = raw.substr(0, hp);
        raw = ReaderText::TrimSv(raw);
        if (!raw.empty()) {
            outLine = raw;
            return true;
        }
    }
    return false;
}

inline bool ReadNodeSection(
    std::string_view sv,
    size_t& pos,
    int numNodes,
    int dim,
    std::vector<Vec3f>& outVerts,
    std::unordered_map<int, uint32_t>& outIdMap)
{
    if (numNodes < 3 || numNodes > 2000000 || (dim != 3 && dim != 2)) return false;
    outVerts.reserve(static_cast<size_t>(numNodes));
    outIdMap.reserve(static_cast<size_t>(numNodes) * 2u);

    for (int i = 0; i < numNodes; ++i) {
        std::string_view line;
        if (!NextNonCommentLine(sv, pos, line)) return false;
        std::string_view tIdx, tX, tY, tZ;
        int nodeId = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!ReaderText::NextTokenSv(line, tIdx) || !ReaderText::ParseIntSv(tIdx, nodeId)) return false;
        if (!ReaderText::NextTokenSv(line, tX)   || !ReaderText::ParseFloatSv(tX, x))     return false;
        if (!ReaderText::NextTokenSv(line, tY)   || !ReaderText::ParseFloatSv(tY, y))     return false;
        if (dim == 3) {
            if (!ReaderText::NextTokenSv(line, tZ) || !ReaderText::ParseFloatSv(tZ, z))   return false;
        }
        const uint32_t vIdx = static_cast<uint32_t>(outVerts.size());
        outVerts.push_back({ x, y, z });
        outIdMap[nodeId] = vIdx;
    }
    return true;
}

} // namespace DetailTetGenSmesh

inline bool ParseTetGenSmesh(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    if (!data || size < 12 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    size_t pos = 0;
    std::string_view hdrLine;
    if (!DetailTetGenSmesh::NextNonCommentLine(sv, pos, hdrLine)) return false;

    std::string_view tCount, tDim;
    int numNodes = -1, dim = 3;
    if (!ReaderText::NextTokenSv(hdrLine, tCount) || !ReaderText::ParseIntSv(tCount, numNodes)) return false;
    if (ReaderText::NextTokenSv(hdrLine, tDim)) {
        if (!ReaderText::ParseIntSv(tDim, dim)) return false;
    }

    std::unordered_map<int, uint32_t> idMap;
    if (numNodes == 0) {
        // TetGen external companion <stem>.node file
        if (fullPath.size() < 7) return false;
        const std::wstring nodePath = fullPath.substr(0, fullPath.size() - 6) + L".node";
        std::vector<uint8_t> nodeBytes;
        if (!PathUtils::ReadFileBytes(nodePath, 64ULL * 1024 * 1024, nodeBytes) || nodeBytes.empty()) return false;
        std::string_view nsv = ReaderText::StripUtf8Bom(
            std::string_view(reinterpret_cast<const char*>(nodeBytes.data()), nodeBytes.size()));
        if (nsv.find('\0') != std::string_view::npos) return false;
        size_t npos = 0;
        std::string_view nHdr;
        if (!DetailTetGenSmesh::NextNonCommentLine(nsv, npos, nHdr)) return false;
        std::string_view tnCount, tnDim;
        int extNodes = 0, extDim = 3;
        if (!ReaderText::NextTokenSv(nHdr, tnCount) || !ReaderText::ParseIntSv(tnCount, extNodes)) return false;
        if (ReaderText::NextTokenSv(nHdr, tnDim) && !ReaderText::ParseIntSv(tnDim, extDim)) return false;
        if (!DetailTetGenSmesh::ReadNodeSection(nsv, npos, extNodes, extDim, mesh.vertices, idMap)) return false;
    } else {
        if (!DetailTetGenSmesh::ReadNodeSection(sv, pos, numNodes, dim, mesh.vertices, idMap)) return false;
    }

    std::string_view facetHdr;
    if (!DetailTetGenSmesh::NextNonCommentLine(sv, pos, facetHdr)) return false;
    std::string_view tFacets;
    int numFacets = 0;
    if (!ReaderText::NextTokenSv(facetHdr, tFacets) || !ReaderText::ParseIntSv(tFacets, numFacets)) return false;
    if (numFacets < 1 || numFacets > 4000000) return false;

    for (int f = 0; f < numFacets; ++f) {
        std::string_view fline;
        if (!DetailTetGenSmesh::NextNonCommentLine(sv, pos, fline)) return false;
        std::string_view tCorners;
        int corners = 0;
        if (!ReaderText::NextTokenSv(fline, tCorners) || !ReaderText::ParseIntSv(tCorners, corners)) return false;
        if (corners < 3 || corners > 4096) return false;

        std::vector<uint32_t> ring;
        ring.reserve(static_cast<size_t>(corners));
        for (int c = 0; c < corners; ++c) {
            std::string_view tv;
            int nid = 0;
            if (!ReaderText::NextTokenSv(fline, tv) || !ReaderText::ParseIntSv(tv, nid)) return false;
            auto it = idMap.find(nid);
            if (it == idMap.end()) return false;
            ring.push_back(it->second);
        }

        if (ring.size() == 3) {
            if (ring[0] != ring[1] && ring[1] != ring[2] && ring[0] != ring[2]) {
                mesh.tris.push_back({ ring[0], ring[1], ring[2], {} });
            }
        } else {
            std::vector<Vec3f> polyPts;
            polyPts.reserve(ring.size());
            for (uint32_t vi : ring) polyPts.push_back(mesh.vertices[vi]);
            MeshBuild3D::EmitPlanarPolygon(mesh, polyPts, {});
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"TetGen PLC Surface Mesh (SMESH)";
    return true;
}

} // namespace Formats
