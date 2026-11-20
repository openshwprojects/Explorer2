#pragma once
// GtaCol.h - collision models of the RenderWare-era Grand Theft Auto games (.col): "COLL" (GTA III, Vice City)
// and "COL2" / "COL3" / "COL4" (San Andreas). Included inside namespace Explorer2.
//
// A .col file is a row of models back to back, with no header of its own. Each model: a FourCC, the size of the
// rest of the model (counted from after the size field), a name of 22 bytes, a model id and bounds, then its
// shapes - spheres, axis-aligned boxes and a triangle mesh.
//   COLL: the sections follow one another, each with a uint32 count: spheres (radius, centre, surface), an unused
//         section that is always empty, boxes (min, max, surface), vertices (3 floats), faces (3 uint32 + surface).
//   COL2+: uint16 counts of spheres, boxes and faces and offsets to every section, counted from after the FourCC.
//         Spheres are centre, radius, surface; vertices are 3 int16 in 1/128 units and their number is not stored
//         (the largest index of a face tells it); faces are 3 uint16 + material + light. COL3 adds a shadow mesh.
// The games are Z-up; the viewer is Y-up. Every model is drawn: its mesh, its boxes as cuboids and its spheres as
// low-poly balls. A file with several models (a whole map area, or all the vehicles) puts them side by side, in
// rows, as the placement of the objects in the world (the .ipl files) is not known here. The shadow mesh is drawn
// only for a model with nothing else.

namespace Formats {

namespace GtaColDetail {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI16LE;
using ::Explorer2::ByteIO::RdFiniteF32LE;

inline constexpr size_t kMaxTriangles = 6000000;     // what all the models may add up to
inline constexpr size_t kMaxModels = 200000;

struct ColBox { Vec3f mn, mx; };
struct ColBall { Vec3f c; float r = 0.0f; };

// One model's shapes in the game's coordinates (Z up)
struct ColModel {
    std::vector<Vec3f> verts;
    std::vector<std::array<uint32_t, 3>> faces;
    std::vector<ColBox> boxes;
    std::vector<ColBall> balls;
    Vec3f mn{}, mx{};
    bool hasExtent = false;

    void Grow(const Vec3f& p) {
        if (!hasExtent) { mn = mx = p; hasExtent = true; return; }
        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
    }
    size_t TriangleCost() const { return faces.size() + boxes.size() * 12 + balls.size() * 100; }
};

inline Vec3f RdVec3(const uint8_t* p) { return { RdFiniteF32LE(p), RdFiniteF32LE(p + 4), RdFiniteF32LE(p + 8) }; }

// A sphere or a box is kept only with sane numbers: the game's maps are a few thousand units across
inline bool Sane(float v) { return v > -100000.0f && v < 100000.0f; }
inline bool Sane(const Vec3f& p) { return Sane(p.x) && Sane(p.y) && Sane(p.z); }

inline void AddBall(ColModel& m, const Vec3f& c, float r) {
    if (!Sane(c) || !(r > 0.0f) || r > 10000.0f) return;
    m.balls.push_back({ c, r });
    m.Grow({ c.x - r, c.y - r, c.z - r });
    m.Grow({ c.x + r, c.y + r, c.z + r });
}

inline void AddBox(ColModel& m, const Vec3f& a, const Vec3f& b) {
    if (!Sane(a) || !Sane(b)) return;
    const Vec3f mn = { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) };
    const Vec3f mx = { std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) };
    m.boxes.push_back({ mn, mx });
    m.Grow(mn);
    m.Grow(mx);
}

// Faces whose corners are all among the vertices; the vertices that faces use widen the extent
inline void AddFace(ColModel& m, uint32_t a, uint32_t b, uint32_t c) {
    const size_t n = m.verts.size();
    if (a >= n || b >= n || c >= n || a == b || b == c || a == c) return;
    m.faces.push_back({ a, b, c });
    m.Grow(m.verts[a]);
    m.Grow(m.verts[b]);
    m.Grow(m.verts[c]);
}

// GTA III / Vice City. p..end is the model after its 72-byte header (FourCC, size, name, id, bounds). A section
// that does not fit ends the model: what came before it is kept.
inline void ParseColl(const uint8_t* p, const uint8_t* end, ColModel& m) {
    auto count = [&](uint32_t& n, size_t each) {
        if (end - p < 4) return false;
        n = RdU32LE(p);
        p += 4;
        return static_cast<uint64_t>(n) * each <= static_cast<uint64_t>(end - p);
    };
    uint32_t n = 0;
    if (!count(n, 20)) return;
    for (uint32_t i = 0; i < n; ++i, p += 20) AddBall(m, RdVec3(p + 4), RdFiniteF32LE(p));
    // The unused section: always empty, and the size of an entry is not known - a model with one ends here
    if (!count(n, 1) || n != 0) return;
    if (!count(n, 28)) return;
    for (uint32_t i = 0; i < n; ++i, p += 28) AddBox(m, RdVec3(p), RdVec3(p + 12));
    if (!count(n, 12)) return;
    m.verts.reserve(n);
    for (uint32_t i = 0; i < n; ++i, p += 12) m.verts.push_back(RdVec3(p));
    if (!count(n, 16)) return;
    for (uint32_t i = 0; i < n; ++i, p += 16) AddFace(m, RdU32LE(p), RdU32LE(p + 4), RdU32LE(p + 8));
}

// The int16 vertices of San Andreas, count = the largest index + 1, read at `at`
inline bool ReadPackedVertices(const uint8_t* model, size_t modelSize, size_t at, uint32_t count, ColModel& m) {
    if (at > modelSize || static_cast<uint64_t>(count) * 6 > modelSize - at) return false;
    m.verts.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* v = model + at + static_cast<size_t>(i) * 6;
        m.verts.push_back({ RdI16LE(v) / 128.0f, RdI16LE(v + 2) / 128.0f, RdI16LE(v + 4) / 128.0f });
    }
    return true;
}

// The faces (3 uint16 + material + light, 8 bytes each) of a San Andreas mesh and the vertices they use
inline void ReadPackedMesh(const uint8_t* model, size_t modelSize, size_t facesAt, uint32_t faceCount, size_t vertsAt,
                           ColModel& m) {
    if (faceCount == 0 || facesAt > modelSize || static_cast<uint64_t>(faceCount) * 8 > modelSize - facesAt) return;
    uint32_t vertexCount = 0;
    for (uint32_t i = 0; i < faceCount; ++i) {
        const uint8_t* f = model + facesAt + static_cast<size_t>(i) * 8;
        vertexCount = std::max<uint32_t>(vertexCount, std::max({ RdU16LE(f), RdU16LE(f + 2), RdU16LE(f + 4) }) + 1u);
    }
    if (!ReadPackedVertices(model, modelSize, vertsAt, vertexCount, m)) return;
    for (uint32_t i = 0; i < faceCount; ++i) {
        const uint8_t* f = model + facesAt + static_cast<size_t>(i) * 8;
        AddFace(m, RdU16LE(f), RdU16LE(f + 2), RdU16LE(f + 4));
    }
}

// San Andreas. model..model+modelSize is the whole model from its FourCC; version 2, 3 or 4.
inline bool ParseCol2Plus(const uint8_t* model, size_t modelSize, int version, ColModel& m) {
    const size_t headerSize = version >= 4 ? 124 : version == 3 ? 120 : 108;
    if (modelSize < headerSize) return false;
    const uint8_t* h = model + 72;                     // after FourCC, size, name, id and the 40 bytes of bounds
    const uint32_t spheres = RdU16LE(h);
    const uint32_t boxes = RdU16LE(h + 2);
    const uint32_t faces = RdU16LE(h + 4);
    const uint32_t flags = RdU32LE(h + 8);
    // Offsets count from after the FourCC; 0 means "none"
    auto at = [](uint32_t offset) { return offset ? static_cast<size_t>(offset) + 4 : static_cast<size_t>(0); };
    const size_t spheresAt = at(RdU32LE(h + 12));
    const size_t boxesAt = at(RdU32LE(h + 16));
    const size_t vertsAt = at(RdU32LE(h + 24));
    const size_t facesAt = at(RdU32LE(h + 28));

    if (spheres && spheresAt >= headerSize && spheresAt <= modelSize &&
        static_cast<uint64_t>(spheres) * 20 <= modelSize - spheresAt) {
        for (uint32_t i = 0; i < spheres; ++i) {
            const uint8_t* s = model + spheresAt + static_cast<size_t>(i) * 20;
            AddBall(m, RdVec3(s), RdFiniteF32LE(s + 12));
        }
    }
    if (boxes && boxesAt >= headerSize && boxesAt <= modelSize &&
        static_cast<uint64_t>(boxes) * 28 <= modelSize - boxesAt) {
        for (uint32_t i = 0; i < boxes; ++i) {
            const uint8_t* b = model + boxesAt + static_cast<size_t>(i) * 28;
            AddBox(m, RdVec3(b), RdVec3(b + 12));
        }
    }
    if (faces && facesAt >= headerSize && vertsAt >= headerSize) ReadPackedMesh(model, modelSize, facesAt, faces, vertsAt, m);

    // COL3: the shadow mesh, a simpler copy of the shape - shown only when the model has nothing else
    if (version >= 3 && (flags & 16u) && m.faces.empty() && m.boxes.empty() && m.balls.empty()) {
        const uint32_t shadowFaces = RdU32LE(model + 108);
        const size_t shadowVertsAt = at(RdU32LE(model + 112));
        const size_t shadowFacesAt = at(RdU32LE(model + 116));
        if (shadowFaces && shadowFaces <= 1000000 && shadowVertsAt >= headerSize && shadowFacesAt >= headerSize) {
            m.verts.clear();
            ReadPackedMesh(model, modelSize, shadowFacesAt, shadowFaces, shadowVertsAt, m);
        }
    }
    return true;
}

inline Vec3f ToViewport(const Vec3f& p) { return { p.x, p.z, -p.y }; }

inline void EmitBox(LoadedMesh3D& mesh, const Vec3f& a, const Vec3f& b, const Vec3f& shift) {
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i < 8; ++i) {
        const Vec3f p = { ((i & 1) ? b.x : a.x) + shift.x, ((i & 2) ? b.y : a.y) + shift.y, ((i & 4) ? b.z : a.z) + shift.z };
        mesh.vertices.push_back(ToViewport(p));
    }
    static const uint8_t kQuads[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
    for (const auto& q : kQuads) {
        mesh.tris.push_back({ base + q[0], base + q[1], base + q[2], {} });
        mesh.tris.push_back({ base + q[0], base + q[2], base + q[3], {} });
    }
}

// A ball of 10 slices and 6 stacks (100 triangles)
inline void EmitBall(LoadedMesh3D& mesh, const Vec3f& c, float r, const Vec3f& shift) {
    constexpr int kSlices = 10, kStacks = 6;
    const float kPi = 3.14159265f;
    const uint32_t top = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(ToViewport({ c.x + shift.x, c.y + shift.y, c.z + r + shift.z }));
    for (int st = 1; st < kStacks; ++st) {
        const float phi = kPi * static_cast<float>(st) / kStacks;
        for (int sl = 0; sl < kSlices; ++sl) {
            const float theta = 2.0f * kPi * static_cast<float>(sl) / kSlices;
            mesh.vertices.push_back(ToViewport({ c.x + r * std::sin(phi) * std::cos(theta) + shift.x,
                                                 c.y + r * std::sin(phi) * std::sin(theta) + shift.y,
                                                 c.z + r * std::cos(phi) + shift.z }));
        }
    }
    const uint32_t bottom = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(ToViewport({ c.x + shift.x, c.y + shift.y, c.z - r + shift.z }));
    auto ring = [&](int st, int sl) { return top + 1 + static_cast<uint32_t>((st - 1) * kSlices + (sl % kSlices)); };
    for (int sl = 0; sl < kSlices; ++sl) {
        mesh.tris.push_back({ top, ring(1, sl + 1), ring(1, sl), {} });
        mesh.tris.push_back({ bottom, ring(kStacks - 1, sl), ring(kStacks - 1, sl + 1), {} });
        for (int st = 1; st < kStacks - 1; ++st) {
            mesh.tris.push_back({ ring(st, sl), ring(st, sl + 1), ring(st + 1, sl + 1), {} });
            mesh.tris.push_back({ ring(st, sl), ring(st + 1, sl + 1), ring(st + 1, sl), {} });
        }
    }
}

inline void EmitModel(const ColModel& m, const Vec3f& shift, LoadedMesh3D& mesh) {
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (const Vec3f& v : m.verts) mesh.vertices.push_back(ToViewport({ v.x + shift.x, v.y + shift.y, v.z + shift.z }));
    for (const auto& f : m.faces) mesh.tris.push_back({ base + f[0], base + f[2], base + f[1], {} });
    for (const ColBox& b : m.boxes) EmitBox(mesh, b.mn, b.mx, shift);
    for (const ColBall& s : m.balls) EmitBall(mesh, s.c, s.r, shift);
}

} // namespace GtaColDetail

inline bool ParseGtaCol(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace GtaColDetail;
    if (!data || size < 8) return false;
    std::vector<ColModel> models;
    int newestVersion = 0;
    size_t triangleBudget = kMaxTriangles;
    size_t pos = 0;
    while (pos + 8 <= size && models.size() < kMaxModels) {
        const uint8_t* model = data + pos;
        int version = 0;
        if (std::memcmp(model, "COLL", 4) == 0) version = 1;
        else if (std::memcmp(model, "COL2", 4) == 0) version = 2;
        else if (std::memcmp(model, "COL3", 4) == 0) version = 3;
        else if (std::memcmp(model, "COL4", 4) == 0) version = 4;
        else break;                                    // the end, or the zeros an IMG archive pads a file with
        const uint64_t modelSize = static_cast<uint64_t>(RdU32LE(model + 4)) + 8;
        if (modelSize < 72 || modelSize > size - pos) break;
        ColModel m;
        if (version == 1) ParseColl(model + 72, model + modelSize, m);
        else if (!ParseCol2Plus(model, static_cast<size_t>(modelSize), version, m)) break;
        pos += static_cast<size_t>(modelSize);
        newestVersion = std::max(newestVersion, version);
        const size_t cost = m.TriangleCost();
        if (cost == 0 || !m.hasExtent) continue;       // an empty model (a name only) adds nothing to see
        if (cost > triangleBudget) break;
        triangleBudget -= cost;
        models.push_back(std::move(m));
    }
    if (models.empty()) return false;

    // One model stays where it is; several are put in rows, left to right and then further back, each row about
    // as long as the whole set is deep, with a gap of a tenth of the largest model between neighbours.
    float largest = 1.0f, area = 0.0f;
    for (const ColModel& m : models) {
        const float w = m.mx.x - m.mn.x, d = m.mx.y - m.mn.y;
        largest = std::max({ largest, w, d });
        area += (w + 1.0f) * (d + 1.0f);
    }
    const float gap = largest * 0.1f;
    const float rowLength = std::max(largest, std::sqrt(area) * 1.3f);
    float x = 0.0f, y = 0.0f, rowDepth = 0.0f;
    for (size_t i = 0; i < models.size(); ++i) {
        const ColModel& m = models[i];
        Vec3f shift{};
        if (models.size() > 1) {
            const float w = m.mx.x - m.mn.x, d = m.mx.y - m.mn.y;
            if (x > 0.0f && x + w > rowLength) {
                x = 0.0f;
                y -= rowDepth + gap;
                rowDepth = 0.0f;
            }
            shift = { x - m.mn.x, y - m.mx.y, 0.0f };
            x += w + gap;
            rowDepth = std::max(rowDepth, d);
        }
        EmitModel(m, shift, mesh);
    }
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    static const wchar_t* const kNames[] = { L"", L"COLL", L"COL2", L"COL3", L"COL4" };
    mesh.formatName = std::wstring(L"GTA collision (.col, ") + kNames[newestVersion] + L")";
    if (models.size() > 1) mesh.formatName += L", " + std::to_wstring(models.size()) + L" models";
    return true;
}

} // namespace Formats
