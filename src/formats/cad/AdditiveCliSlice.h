#pragma once
// AdditiveCliSlice.h — Common Layer Interface (.cli) Laser Powder-Bed 3D Printing Slice Reader
// Supports CLI v2.0 $$ASCII & $$BINARY encodings ($$UNITS, $$LAYER / 127 / 128, $$POLYLINE / 129 / 130,
// $$HATCHES / 131 / 132), pairing internal CW holes with external CCW contours and extruding each layer
// across its computed slice thickness dz into a watertight 3D manufactured part.
#include "../../ByteIO.h"
#include "../../ReaderTextUtils.h"
#include "../common/MeshBuild3D.h"

namespace Formats {

namespace DetailCliSlice {

struct Pt2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct CliContour {
    int dir = 1; // 0 = CW internal hole, 1 = CCW external solid, 2 = open polyline
    std::vector<Pt2> pts;
};

struct CliHatchSeg {
    Pt2 a;
    Pt2 b;
};

struct CliLayer {
    float z = 0.0f;
    std::vector<CliContour> contours;
    std::vector<CliHatchSeg> hatches;
};

inline double SignedArea2D(const std::vector<Pt2>& p) {
    if (p.size() < 3) return 0.0;
    double a = 0.0;
    for (size_t i = 0; i < p.size(); ++i) {
        const Pt2& u = p[i];
        const Pt2& v = p[(i + 1) % p.size()];
        a += static_cast<double>(u.x) * v.y - static_cast<double>(v.x) * u.y;
    }
    return 0.5 * a;
}

inline bool PointInPoly2D(const Pt2& q, const std::vector<Pt2>& poly) {
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Pt2& pi = poly[i];
        const Pt2& pj = poly[j];
        if (((pi.y > q.y) != (pj.y > q.y)) &&
            (q.x < (pj.x - pi.x) * (q.y - pi.y) / ((pj.y - pi.y) + 1e-20f) + pi.x))
        {
            inside = !inside;
        }
    }
    return inside;
}

inline bool PointInTri2D(const Pt2& p, const Pt2& a, const Pt2& b, const Pt2& c) {
    auto cross = [](const Pt2& o, const Pt2& u, const Pt2& v) -> double {
        return static_cast<double>(u.x - o.x) * (v.y - o.y) - static_cast<double>(u.y - o.y) * (v.x - o.x);
    };
    double c0 = cross(a, b, p);
    double c1 = cross(b, c, p);
    double c2 = cross(c, a, p);
    return (c0 >= -1e-9 && c1 >= -1e-9 && c2 >= -1e-9);
}

// Bridges inner CW hole rings into outer CCW ring via rightmost-vertex visibility, then ear-clips to triangles.
inline void TriangulateRingWithHoles(
    std::vector<Pt2> outer,
    std::vector<std::vector<Pt2>> holes,
    std::vector<Pt2>& outMergedPts,
    std::vector<std::array<uint32_t, 3>>& outTris)
{
    outMergedPts.clear();
    outTris.clear();
    if (outer.size() < 3) return;
    if (SignedArea2D(outer) < 0.0) std::reverse(outer.begin(), outer.end());

    // Sort holes by rightmost X descending so rightward visibility rays reach the outer boundary cleanly
    struct HoleOrder {
        size_t idx;
        float maxX;
        size_t maxVert;
    };
    std::vector<HoleOrder> order;
    for (size_t h = 0; h < holes.size(); ++h) {
        auto& hr = holes[h];
        if (hr.size() < 3) continue;
        if (SignedArea2D(hr) > 0.0) std::reverse(hr.begin(), hr.end()); // holes must be CW
        size_t mv = 0;
        for (size_t i = 1; i < hr.size(); ++i) {
            if (hr[i].x > hr[mv].x) mv = i;
        }
        order.push_back({ h, hr[mv].x, mv });
    }
    std::sort(order.begin(), order.end(), [](const HoleOrder& a, const HoleOrder& b) { return a.maxX > b.maxX; });

    for (const auto& ho : order) {
        const auto& hr = holes[ho.idx];
        const Pt2& m = hr[ho.maxVert];

        // Find closest ray intersection with outer polygon to the right of m, then pick visible reflex/endpoint vertex
        double bestX = 1e300;
        size_t bridgeIdx = 0;
        bool foundEdge = false;
        for (size_t i = 0; i < outer.size(); ++i) {
            const Pt2& a = outer[i];
            const Pt2& b = outer[(i + 1) % outer.size()];
            if ((a.y > m.y) == (b.y > m.y)) continue;
            double ix = a.x + (m.y - a.y) * static_cast<double>(b.x - a.x) / static_cast<double>(b.y - a.y);
            if (ix >= m.x - 1e-6 && ix < bestX) {
                bestX = ix;
                bridgeIdx = (a.x >= b.x) ? i : ((i + 1) % outer.size());
                foundEdge = true;
            }
        }
        if (!foundEdge) {
            double bestD2 = 1e300;
            for (size_t i = 0; i < outer.size(); ++i) {
                double dx = outer[i].x - m.x, dy = outer[i].y - m.y;
                double d2 = dx * dx + dy * dy;
                if (d2 < bestD2) { bestD2 = d2; bridgeIdx = i; }
            }
        }

        std::vector<Pt2> combined;
        combined.reserve(outer.size() + hr.size() + 2u);
        for (size_t i = 0; i <= bridgeIdx; ++i) combined.push_back(outer[i]);
        for (size_t k = 0; k < hr.size(); ++k) combined.push_back(hr[(ho.maxVert + k) % hr.size()]);
        combined.push_back(hr[ho.maxVert]);
        for (size_t i = bridgeIdx; i < outer.size(); ++i) combined.push_back(outer[i]);
        outer = std::move(combined);
    }

    outMergedPts = outer;
    if (outMergedPts.size() > 4096) return;

    std::vector<uint32_t> poly(outMergedPts.size());
    for (size_t i = 0; i < outMergedPts.size(); ++i) poly[i] = static_cast<uint32_t>(i);

    auto cross2 = [&](uint32_t ia, uint32_t ib, uint32_t ic) -> double {
        const Pt2& a = outMergedPts[ia];
        const Pt2& b = outMergedPts[ib];
        const Pt2& c = outMergedPts[ic];
        return static_cast<double>(b.x - a.x) * (c.y - a.y) - static_cast<double>(b.y - a.y) * (c.x - a.x);
    };

    size_t guard = poly.size() * poly.size() + 16u;
    while (poly.size() > 3 && guard-- > 0) {
        bool clipped = false;
        const size_t m = poly.size();
        for (size_t i = 0; i < m; ++i) {
            uint32_t prev = poly[(i + m - 1) % m];
            uint32_t curr = poly[i];
            uint32_t next = poly[(i + 1) % m];
            if (cross2(prev, curr, next) <= 1e-10) continue;

            bool hasInside = false;
            const Pt2& pa = outMergedPts[prev];
            const Pt2& pb = outMergedPts[curr];
            const Pt2& pc = outMergedPts[next];
            for (size_t j = 0; j < m; ++j) {
                uint32_t pj = poly[j];
                if (pj == prev || pj == curr || pj == next) continue;
                const Pt2& q = outMergedPts[pj];
                if ((std::fabs(q.x - pa.x) < 1e-6f && std::fabs(q.y - pa.y) < 1e-6f) ||
                    (std::fabs(q.x - pb.x) < 1e-6f && std::fabs(q.y - pb.y) < 1e-6f) ||
                    (std::fabs(q.x - pc.x) < 1e-6f && std::fabs(q.y - pc.y) < 1e-6f))
                {
                    continue;
                }
                if (PointInTri2D(q, pa, pb, pc)) { hasInside = true; break; }
            }
            if (!hasInside) {
                outTris.push_back({ prev, curr, next });
                poly.erase(poly.begin() + static_cast<ptrdiff_t>(i));
                clipped = true;
                break;
            }
        }
        if (!clipped) {
            // Relaxed convex ear pass for bridge-seam collinear vertices
            for (size_t i = 0; i < poly.size(); ++i) {
                uint32_t prev = poly[(i + poly.size() - 1) % poly.size()];
                uint32_t curr = poly[i];
                uint32_t next = poly[(i + 1) % poly.size()];
                if (cross2(prev, curr, next) >= 0.0) {
                    outTris.push_back({ prev, curr, next });
                    poly.erase(poly.begin() + static_cast<ptrdiff_t>(i));
                    clipped = true;
                    break;
                }
            }
            if (!clipped) break;
        }
    }
    if (poly.size() == 3) {
        outTris.push_back({ poly[0], poly[1], poly[2] });
    }
}

inline void CleanClosedRing(std::vector<Pt2>& pts) {
    if (pts.size() >= 2) {
        float dx = pts.front().x - pts.back().x;
        float dy = pts.front().y - pts.back().y;
        if (dx * dx + dy * dy < 1e-10f) pts.pop_back();
    }
}

inline bool ParseCommaFloats(std::string_view sv, std::vector<float>& out) {
    out.clear();
    const char* cur = sv.data();
    const char* const end = cur + sv.size();
    while (cur < end) {
        while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == ',' || *cur == '/')) ++cur;
        if (cur >= end) break;
        const char* s = cur;
        while (cur < end && *cur != ' ' && *cur != '\t' && *cur != ',') ++cur;
        float v = 0.0f;
        if (!ReaderText::ParseFloatSv(std::string_view(s, static_cast<size_t>(cur - s)), v)) return false;
        out.push_back(v);
    }
    return true;
}

} // namespace DetailCliSlice

inline bool ParseAdditiveCliSlice(
    const uint8_t* data,
    size_t size,
    const std::wstring& /*fullPath*/,
    LoadedMesh3D& mesh)
{
    using namespace DetailCliSlice;
    if (!data || size < 24) return false;
    mesh.vertices.clear();
    mesh.tris.clear();

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    size_t hdrStart = sv.find("$$HEADERSTART");
    if (hdrStart == std::string_view::npos || hdrStart > 4096) return false;
    size_t hdrEnd = sv.find("$$HEADEREND", hdrStart + 13);
    if (hdrEnd == std::string_view::npos) return false;

    std::string_view hdr = sv.substr(hdrStart + 13, hdrEnd - (hdrStart + 13));
    bool isBinary = (hdr.find("$$BINARY") != std::string_view::npos);
    float units = 1.0f;
    {
        size_t uPos = hdr.find("$$UNITS/");
        if (uPos != std::string_view::npos) {
            size_t e = uPos + 8;
            while (e < hdr.size() && hdr[e] != '\r' && hdr[e] != '\n' && hdr[e] != ' ' && hdr[e] != '$') ++e;
            float u = 1.0f;
            if (ReaderText::ParseFloatSv(hdr.substr(uPos + 8, e - (uPos + 8)), u) && u > 1e-9f && u < 1e6f) {
                units = u;
            }
        }
    }

    size_t geomPos = hdrEnd + 11;
    // Check if $$GEOMETRYSTART appears right after $$HEADEREND
    {
        std::string_view tail = sv.substr(geomPos, std::min<size_t>(sv.size() - geomPos, 256u));
        size_t gs = tail.find("$$GEOMETRYSTART");
        if (gs != std::string_view::npos) {
            geomPos += gs + 15;
            if (!isBinary) {
                while (geomPos < size && (data[geomPos] == '\r' || data[geomPos] == '\n')) ++geomPos;
            } else if (geomPos < size && (data[geomPos] == '\r' || data[geomPos] == '\n')) {
                // In CLI v2.0 binary mode, some exporters emit \r\n after $$GEOMETRYSTART, others emit uint16 127/128 directly!
                // Note: 127 = 0x007F ('\x7f','\x00'), 128 = 0x0080 ('\x80','\x00'), neither starts with '\r'(0x0D) or '\n'(0x0A)!
                if (data[geomPos] == '\r') ++geomPos;
                if (geomPos < size && data[geomPos] == '\n') ++geomPos;
            }
        } else if (geomPos < size && (data[geomPos] == '\r' || data[geomPos] == '\n')) {
            if (data[geomPos] == '\r') ++geomPos;
            if (geomPos < size && data[geomPos] == '\n') ++geomPos;
        }
    }

    std::vector<CliLayer> layers;

    if (isBinary) {
        size_t p = geomPos;
        while (p + 2 <= size) {
            if (p + 13 <= size && std::memcmp(data + p, "$$GEOMETRYEND", 13) == 0) break;
            uint16_t ci = ByteIO::RdU16LE(data + p);
            p += 2;
            if (ci == 127u) { // START LAYER LONG (float32 z)
                if (p + 4 > size) break;
                float z = ByteIO::RdF32LE(data + p) * units;
                p += 4;
                if (!std::isfinite(z)) return false;
                layers.push_back(CliLayer{ z, {}, {} });
            } else if (ci == 128u) { // START LAYER SHORT (uint16 z)
                if (p + 2 > size) break;
                float z = static_cast<float>(ByteIO::RdU16LE(data + p)) * units;
                p += 2;
                layers.push_back(CliLayer{ z, {}, {} });
            } else if (ci == 129u) { // START POLYLINE SHORT (uint16 id, dir, n, uint16[2*n])
                if (p + 6 > size) break;
                uint16_t dir = ByteIO::RdU16LE(data + p + 2);
                uint16_t n   = ByteIO::RdU16LE(data + p + 4);
                p += 6;
                uint64_t need = static_cast<uint64_t>(n) * 4ULL;
                if (need > size - p) break;
                if (layers.empty()) layers.push_back(CliLayer{ 0.0f, {}, {} });
                CliContour c;
                c.dir = static_cast<int>(dir);
                c.pts.resize(n);
                for (uint16_t i = 0; i < n; ++i) {
                    c.pts[i].x = static_cast<float>(ByteIO::RdU16LE(data + p + static_cast<size_t>(i) * 4u + 0)) * units;
                    c.pts[i].y = static_cast<float>(ByteIO::RdU16LE(data + p + static_cast<size_t>(i) * 4u + 2)) * units;
                }
                p += static_cast<size_t>(need);
                layers.back().contours.push_back(std::move(c));
            } else if (ci == 130u) { // START POLYLINE LONG (int32 id, dir, n, float32[2*n])
                if (p + 12 > size) break;
                int32_t dir = ByteIO::RdI32LE(data + p + 4);
                int32_t n   = ByteIO::RdI32LE(data + p + 8);
                p += 12;
                if (n < 0 || n > 1000000) break;
                uint64_t need = static_cast<uint64_t>(n) * 8ULL;
                if (need > size - p) break;
                if (layers.empty()) layers.push_back(CliLayer{ 0.0f, {}, {} });
                CliContour c;
                c.dir = dir;
                c.pts.resize(static_cast<size_t>(n));
                for (int32_t i = 0; i < n; ++i) {
                    c.pts[static_cast<size_t>(i)].x = ByteIO::RdF32LE(data + p + static_cast<size_t>(i) * 8u + 0) * units;
                    c.pts[static_cast<size_t>(i)].y = ByteIO::RdF32LE(data + p + static_cast<size_t>(i) * 8u + 4) * units;
                }
                p += static_cast<size_t>(need);
                layers.back().contours.push_back(std::move(c));
            } else if (ci == 131u) { // START HATCHES SHORT (uint16 id, n, uint16[4*n])
                if (p + 4 > size) break;
                uint16_t n = ByteIO::RdU16LE(data + p + 2);
                p += 4;
                uint64_t need = static_cast<uint64_t>(n) * 8ULL;
                if (need > size - p) break;
                if (layers.empty()) layers.push_back(CliLayer{ 0.0f, {}, {} });
                for (uint16_t i = 0; i < n; ++i) {
                    const uint8_t* hp = data + p + static_cast<size_t>(i) * 8u;
                    layers.back().hatches.push_back({
                        { static_cast<float>(ByteIO::RdU16LE(hp + 0)) * units, static_cast<float>(ByteIO::RdU16LE(hp + 2)) * units },
                        { static_cast<float>(ByteIO::RdU16LE(hp + 4)) * units, static_cast<float>(ByteIO::RdU16LE(hp + 6)) * units }
                    });
                }
                p += static_cast<size_t>(need);
            } else if (ci == 132u) { // START HATCHES LONG (int32 id, n, float32[4*n])
                if (p + 8 > size) break;
                int32_t n = ByteIO::RdI32LE(data + p + 4);
                p += 8;
                if (n < 0 || n > 1000000) break;
                uint64_t need = static_cast<uint64_t>(n) * 16ULL;
                if (need > size - p) break;
                if (layers.empty()) layers.push_back(CliLayer{ 0.0f, {}, {} });
                for (int32_t i = 0; i < n; ++i) {
                    const uint8_t* hp = data + p + static_cast<size_t>(i) * 16u;
                    layers.back().hatches.push_back({
                        { ByteIO::RdF32LE(hp + 0) * units, ByteIO::RdF32LE(hp + 4) * units },
                        { ByteIO::RdF32LE(hp + 8) * units, ByteIO::RdF32LE(hp + 12) * units }
                    });
                }
                p += static_cast<size_t>(need);
            } else {
                break;
            }
        }
    } else {
        // $$ASCII command parser (commands are separated by $$ or newlines; strip // ... // comments)
        std::string_view gsv = sv.substr(geomPos);
        size_t p = 0;
        std::vector<float> nums;
        while (p < gsv.size()) {
            size_t cmd = gsv.find("$$", p);
            if (cmd == std::string_view::npos) break;
            size_t nextCmd = gsv.find("$$", cmd + 2);
            std::string_view tok = (nextCmd == std::string_view::npos)
                ? gsv.substr(cmd + 2)
                : gsv.substr(cmd + 2, nextCmd - (cmd + 2));
            p = (nextCmd == std::string_view::npos) ? gsv.size() : nextCmd;

            // Strip any // ... // comment inside token
            size_t cmt = tok.find("//");
            if (cmt != std::string_view::npos) tok = tok.substr(0, cmt);
            tok = ReaderText::TrimSv(tok);
            if (tok.starts_with("GEOMETRYEND")) break;

            if (tok.starts_with("LAYER/")) {
                if (ParseCommaFloats(tok.substr(6), nums) && !nums.empty()) {
                    layers.push_back(CliLayer{ nums[0] * units, {}, {} });
                }
            } else if (tok.starts_with("POLYLINE/")) {
                if (ParseCommaFloats(tok.substr(9), nums) && nums.size() >= 3) {
                    int dir = static_cast<int>(std::lround(nums[1]));
                    int n   = static_cast<int>(std::lround(nums[2]));
                    if (n >= 2 && nums.size() >= 3u + static_cast<size_t>(n) * 2u) {
                        if (layers.empty()) layers.push_back(CliLayer{ 0.0f, {}, {} });
                        CliContour c;
                        c.dir = dir;
                        c.pts.resize(static_cast<size_t>(n));
                        for (int i = 0; i < n; ++i) {
                            c.pts[static_cast<size_t>(i)].x = nums[3u + static_cast<size_t>(i) * 2u + 0] * units;
                            c.pts[static_cast<size_t>(i)].y = nums[3u + static_cast<size_t>(i) * 2u + 1] * units;
                        }
                        layers.back().contours.push_back(std::move(c));
                    }
                }
            } else if (tok.starts_with("HATCHES/")) {
                if (ParseCommaFloats(tok.substr(8), nums) && nums.size() >= 2) {
                    int n = static_cast<int>(std::lround(nums[1]));
                    if (n >= 1 && nums.size() >= 2u + static_cast<size_t>(n) * 4u) {
                        if (layers.empty()) layers.push_back(CliLayer{ 0.0f, {}, {} });
                        for (int i = 0; i < n; ++i) {
                            size_t base = 2u + static_cast<size_t>(i) * 4u;
                            layers.back().hatches.push_back({
                                { nums[base + 0] * units, nums[base + 1] * units },
                                { nums[base + 2] * units, nums[base + 3] * units }
                            });
                        }
                    }
                }
            }
        }
    }

    // Remove empty layers and sort by ascending Z
    layers.erase(std::remove_if(layers.begin(), layers.end(), [](const CliLayer& l) {
        return l.contours.empty() && l.hatches.empty();
    }), layers.end());
    if (layers.empty()) return false;

    std::stable_sort(layers.begin(), layers.end(), [](const CliLayer& a, const CliLayer& b) {
        return a.z < b.z;
    });

    // Compute default layer thickness dz
    std::vector<float> diffs;
    for (size_t i = 0; i + 1 < layers.size(); ++i) {
        float d = layers[i + 1].z - layers[i].z;
        if (d > 1e-6f) diffs.push_back(d);
    }
    float defaultDz = 0.05f;
    if (!diffs.empty()) {
        std::sort(diffs.begin(), diffs.end());
        defaultDz = diffs[diffs.size() / 2];
    } else {
        float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
        for (const auto& l : layers) {
            for (const auto& c : l.contours) {
                for (const auto& p : c.pts) {
                    minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                    minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
                }
            }
        }
        if (maxX > minX || maxY > minY) {
            defaultDz = std::max(0.02f, std::max(maxX - minX, maxY - minY) * 0.04f);
        }
    }

    auto mapBuildPt = [](float x, float y, float zPrint) -> Vec3f {
        // Additive build plate XY + Z build height -> Explorer2 Y-up (x, zPrint, -y)
        return { x, zPrint, -y };
    };

    const size_t maxLayersToMesh = 512;
    const size_t layerStep = std::max<size_t>(1, (layers.size() + maxLayersToMesh - 1) / maxLayersToMesh);

    for (size_t li = 0; li < layers.size(); li += layerStep) {
        CliLayer& lay = layers[li];
        float z0 = lay.z;
        float dz = defaultDz * static_cast<float>(layerStep);
        if (li + layerStep < layers.size() && layers[li + layerStep].z > z0 + 1e-6f) {
            dz = std::min(layers[li + layerStep].z - z0, defaultDz * static_cast<float>(layerStep) * 2.5f);
        }
        float z1 = z0 + dz;

        std::vector<std::vector<Pt2>> outers;
        std::vector<double> outerAreas;
        std::vector<std::vector<Pt2>> holes;
        std::vector<std::vector<Pt2>> openPolys;

        for (auto& c : lay.contours) {
            if (c.dir == 2) {
                if (c.pts.size() >= 2) openPolys.push_back(c.pts);
                continue;
            }
            CleanClosedRing(c.pts);
            if (c.pts.size() < 3) continue;
            double area = SignedArea2D(c.pts);
            bool isHole = (c.dir == 0) || (c.dir != 1 && area < 0.0);
            if (isHole) {
                if (area > 0.0) std::reverse(c.pts.begin(), c.pts.end());
                holes.push_back(c.pts);
            } else {
                if (area < 0.0) std::reverse(c.pts.begin(), c.pts.end());
                outerAreas.push_back(std::fabs(area));
                outers.push_back(c.pts);
            }
        }

        // If all contours had dir==0 (some slicers omit dir=1), promote non-enclosed rings to outers
        if (outers.empty() && !holes.empty()) {
            for (auto& h : holes) {
                std::reverse(h.begin(), h.end());
                outerAreas.push_back(std::fabs(SignedArea2D(h)));
                outers.push_back(std::move(h));
            }
            holes.clear();
        }

        std::vector<std::vector<std::vector<Pt2>>> outerHoles(outers.size());
        for (const auto& h : holes) {
            size_t bestOuter = outers.size();
            double bestArea = 1e300;
            for (size_t oi = 0; oi < outers.size(); ++oi) {
                if (PointInPoly2D(h[0], outers[oi]) && outerAreas[oi] < bestArea) {
                    bestArea = outerAreas[oi];
                    bestOuter = oi;
                }
            }
            if (bestOuter < outers.size()) {
                outerHoles[bestOuter].push_back(h);
            }
        }

        auto emitWallRing = [&](const std::vector<Pt2>& ring) {
            const size_t n = ring.size();
            const uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
            for (size_t i = 0; i < n; ++i) {
                mesh.vertices.push_back(mapBuildPt(ring[i].x, ring[i].y, z0));
                mesh.vertices.push_back(mapBuildPt(ring[i].x, ring[i].y, z1));
            }
            for (size_t i = 0; i < n; ++i) {
                size_t j = (i + 1) % n;
                uint32_t b0 = vb + static_cast<uint32_t>(i * 2);
                uint32_t t0 = b0 + 1u;
                uint32_t b1 = vb + static_cast<uint32_t>(j * 2);
                uint32_t t1 = b1 + 1u;
                mesh.tris.push_back({ b0, b1, t1, {} });
                mesh.tris.push_back({ b0, t1, t0, {} });
            }
        };

        for (size_t oi = 0; oi < outers.size(); ++oi) {
            emitWallRing(outers[oi]);
            for (const auto& hr : outerHoles[oi]) emitWallRing(hr);

            std::vector<Pt2> capPts;
            std::vector<std::array<uint32_t, 3>> capTris;
            TriangulateRingWithHoles(outers[oi], outerHoles[oi], capPts, capTris);
            if (!capTris.empty()) {
                uint32_t topBase = static_cast<uint32_t>(mesh.vertices.size());
                for (const auto& p : capPts) mesh.vertices.push_back(mapBuildPt(p.x, p.y, z1));
                for (const auto& t : capTris) {
                    mesh.tris.push_back({ topBase + t[0], topBase + t[1], topBase + t[2], {} });
                }

                uint32_t botBase = static_cast<uint32_t>(mesh.vertices.size());
                for (const auto& p : capPts) mesh.vertices.push_back(mapBuildPt(p.x, p.y, z0));
                for (const auto& t : capTris) {
                    mesh.tris.push_back({ botBase + t[0], botBase + t[2], botBase + t[1], {} });
                }
            }
        }

        const float strutRad = std::max(0.01f, dz * 0.35f);
        for (const auto& op : openPolys) {
            for (size_t k = 0; k + 1 < op.size(); ++k) {
                MeshBuild3D::EmitEdgeStrut(
                    mesh,
                    mapBuildPt(op[k].x, op[k].y, z0 + 0.5f * dz),
                    mapBuildPt(op[k + 1].x, op[k + 1].y, z0 + 0.5f * dz),
                    strutRad);
            }
        }
        if (outers.empty() && !lay.hatches.empty()) {
            for (const auto& hs : lay.hatches) {
                MeshBuild3D::EmitEdgeStrut(
                    mesh,
                    mapBuildPt(hs.a.x, hs.a.y, z0 + 0.5f * dz),
                    mapBuildPt(hs.b.x, hs.b.y, z0 + 0.5f * dz),
                    strutRad);
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = isBinary
        ? L"Common Layer Interface 3D Slice (.cli Binary)"
        : L"Common Layer Interface 3D Slice (.cli ASCII)";
    return true;
}

} // namespace Formats
