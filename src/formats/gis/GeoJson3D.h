#pragma once
// GeoJson3D.h — 3D GeoJSON (.geojson — 3D coordinates & extruded buildings) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace BspGeoDetail {

inline bool IsLikely3DGeoJsonBuffer(const uint8_t* data, size_t size) {
    if (!data || size < 24) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 65536));
    if (sv.find("\"coordinates\"") == std::string_view::npos) return false;
    if (sv.find("\"height\"") != std::string_view::npos || sv.find("\"extrude\"") != std::string_view::npos) return true;

    // Check if first numeric coordinate tuple [x, y, z] has >= 3 numbers!
    size_t cp = sv.find("\"coordinates\"");
    size_t p = sv.find('[', cp);
    while (p != std::string_view::npos && p < sv.size()) {
        ++p;
        while (p < sv.size() && std::isspace(static_cast<unsigned char>(sv[p]))) ++p;
        if (p < sv.size() && sv[p] == '[') continue;
        // Inside innermost [num, num, ...]
        size_t closeB = sv.find(']', p);
        if (closeB == std::string_view::npos) break;
        std::string_view tuple = sv.substr(p, closeB - p);
        int commas = 0;
        for (char c : tuple) if (c == ',') ++commas;
        return commas >= 2;
    }
    return false;
}

// 3D GeoJSON (a .geojson routed to the 3D view): the parsed document's features. Longitude / latitude are projected
// to local metres around the data's centre (projected coordinates are taken as metres already, relative to their
// centre); z is the third coordinate. A flat polygon whose feature has a height ("height" / "render_height", else
// "building:levels" x 3 m, above "min_height" / "render_min_height") is extruded like a building footprint, and
// one with "extrude": true from its own elevation down to the ground (KML style); other polygons are filled in
// their own plane (ear-clipped, holes kept); lines become struts, points markers.
inline bool ParseGeoJson3DMesh(std::string_view sv, LoadedMesh3D& mesh) {
    using JVal = ::Explorer2::Json::Value;
    JVal root;
    ::Explorer2::Json::Limits limits;
    limits.maxNodes = 6000000;
    if (!::Explorer2::Json::ParseDocument(sv, root, limits) || !root.IsObject()) return false;
    mesh.vertices.clear();
    mesh.tris.clear();

    struct Pos { double x = 0.0, y = 0.0, z = 0.0; };
    using Ring = std::vector<Pos>;
    struct Item {
        int kind = 0; // 0 polygon (rings[0] outer, the rest holes), 1 line, 2 points
        std::vector<Ring> rings;
        double height = -1.0, minHeight = 0.0;
        bool extrudeToGround = false;
    };
    std::vector<Item> items;
    size_t positions = 0;
    constexpr size_t kMaxPositions = 4000000;

    auto readPos = [](const JVal& p, Pos& out) {
        if (!p.IsArray() || p.arrVal.size() < 2 || !p.arrVal[0].IsNumber() || !p.arrVal[1].IsNumber()) return false;
        out.x = p.arrVal[0].numVal;
        out.y = p.arrVal[1].numVal;
        out.z = (p.arrVal.size() >= 3 && p.arrVal[2].IsNumber()) ? p.arrVal[2].numVal : 0.0;
        return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
    };
    auto readRing = [&](const JVal& arr, bool closed) {
        Ring ring;
        if (!arr.IsArray()) return ring;
        Pos p;
        for (const auto& c : arr.arrVal) {
            if (positions >= kMaxPositions) break;
            if (readPos(c, p)) { ring.push_back(p); ++positions; }
        }
        if (closed && ring.size() >= 2 && ring.front().x == ring.back().x && ring.front().y == ring.back().y && ring.front().z == ring.back().z) {
            ring.pop_back();
        }
        return ring;
    };
    auto propNumber = [](const JVal* props, std::initializer_list<const char*> keys, double& out) {
        if (!props || !props->IsObject()) return false;
        for (const char* k : keys) {
            const JVal* v = props->Find(k);
            if (!v) continue;
            if (v->IsNumber() && std::isfinite(v->numVal)) { out = v->numVal; return true; }
            if (v->IsString()) { // "12", "12 m"
                char* e = nullptr;
                double d = std::strtod(v->strVal.c_str(), &e);
                if (e != v->strVal.c_str() && std::isfinite(d)) { out = d; return true; }
            }
        }
        return false;
    };
    auto addGeometry = [&](auto& self, const JVal& g, double height, double minHeight, bool extrude, int nesting) -> void {
        if (!g.IsObject() || nesting > 8) return;
        std::string type = g.GetString("type");
        if (type == "GeometryCollection") {
            if (const JVal* gs = g.Find("geometries"); gs && gs->IsArray()) {
                for (const auto& sub : gs->arrVal) self(self, sub, height, minHeight, extrude, nesting + 1);
            }
            return;
        }
        const JVal* coords = g.Find("coordinates");
        if (!coords || !coords->IsArray()) return;
        auto addPolygon = [&](const JVal& poly) {
            if (!poly.IsArray()) return;
            Item it;
            it.height = height;
            it.minHeight = minHeight;
            it.extrudeToGround = extrude;
            for (const auto& r : poly.arrVal) it.rings.push_back(readRing(r, true));
            if (!it.rings.empty() && it.rings[0].size() >= 3) items.push_back(std::move(it));
        };
        if (type == "Polygon") {
            addPolygon(*coords);
        } else if (type == "MultiPolygon") {
            for (const auto& poly : coords->arrVal) addPolygon(poly);
        } else if (type == "LineString" || type == "MultiLineString") {
            Item it;
            it.kind = 1;
            if (type == "LineString") it.rings.push_back(readRing(*coords, false));
            else for (const auto& l : coords->arrVal) it.rings.push_back(readRing(l, false));
            items.push_back(std::move(it));
        } else if (type == "Point" || type == "MultiPoint") {
            Item it;
            it.kind = 2;
            Ring pts;
            Pos p;
            if (type == "Point") {
                if (readPos(*coords, p)) pts.push_back(p);
            } else {
                pts = readRing(*coords, false);
            }
            it.rings.push_back(std::move(pts));
            items.push_back(std::move(it));
        }
    };
    auto addFeature = [&](const JVal& f) {
        if (!f.IsObject()) return;
        if (f.GetString("type") != "Feature") {
            addGeometry(addGeometry, f, -1.0, 0.0, false, 0);
            return;
        }
        const JVal* props = f.Find("properties");
        double height = -1.0, minHeight = 0.0, levels = 0.0;
        if (!propNumber(props, { "height", "render_height" }, height)) {
            if (propNumber(props, { "building:levels", "levels" }, levels) && levels > 0.0) height = levels * 3.0;
        }
        if (!propNumber(props, { "min_height", "render_min_height" }, minHeight)) {
            double minLevel = 0.0;
            if (propNumber(props, { "building:min_level", "min_level" }, minLevel)) minHeight = minLevel * 3.0;
        }
        bool extrude = false; // KML-style: extend the geometry down to the ground
        if (const JVal* ex = props ? props->Find("extrude") : nullptr) {
            extrude = (ex->IsBool() && ex->boolVal) || (ex->IsNumber() && ex->numVal != 0.0) ||
                      (ex->IsString() && (ex->strVal == "1" || ex->strVal == "true"));
        }
        if (const JVal* g = f.Find("geometry")) addGeometry(addGeometry, *g, height, minHeight, extrude, 0);
    };
    if (root.GetString("type") == "FeatureCollection") {
        const JVal* fs = root.Find("features");
        if (!fs || !fs->IsArray()) return false;
        for (const auto& f : fs->arrVal) addFeature(f);
    } else {
        addFeature(root);
    }

    // Extent, and whether the coordinates are longitude / latitude
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300, maxZ = 0.0;
    for (const auto& it : items) {
        for (const auto& r : it.rings) {
            for (const auto& p : r) {
                minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
                maxZ = std::max(maxZ, std::fabs(p.z));
            }
        }
        maxZ = std::max(maxZ, it.height);
    }
    if (minX > maxX) return false;
    const bool geographic = minX >= -180.0 && maxX <= 180.0 && minY >= -90.0 && maxY <= 90.0;
    const double cx = (minX + maxX) * 0.5, cy = (minY + maxY) * 0.5;
    const double kPi = 3.14159265358979323846;
    const double mPerX = geographic ? 111320.0 * std::cos(cy * kPi / 180.0) : 1.0;
    const double mPerY = geographic ? 110574.0 : 1.0;
    auto toMesh = [&](const Pos& p, double up) -> Vec3f { // east, up, -north
        return { static_cast<float>((p.x - cx) * mPerX), static_cast<float>(up), static_cast<float>(-(p.y - cy) * mPerY) };
    };
    const double span = std::max({ (maxX - minX) * mPerX, (maxY - minY) * mPerY, maxZ, 1e-3 });
    const Vec3f up = { 0.0f, 1.0f, 0.0f }, down = { 0.0f, -1.0f, 0.0f };
    // Signed area in (east, north): > 0 for counter-clockwise
    auto signedArea = [&](const Ring& r) {
        double a = 0.0;
        for (size_t i = 0; i < r.size(); ++i) {
            const Pos& p = r[i];
            const Pos& q = r[(i + 1) % r.size()];
            a += (p.x - cx) * mPerX * (q.y - cy) * mPerY - (q.x - cx) * mPerX * (p.y - cy) * mPerY;
        }
        return 0.5 * a;
    };

    constexpr size_t kMaxTris = 3000000;
    for (auto& it : items) {
        if (mesh.tris.size() >= kMaxTris) break;
        if (it.kind == 1) {
            const float rad = static_cast<float>(span * 0.002);
            for (const auto& line : it.rings) {
                for (size_t k = 0; k + 1 < line.size(); ++k) {
                    MeshBuild3D::EmitEdgeStrut(mesh, toMesh(line[k], line[k].z), toMesh(line[k + 1], line[k + 1].z), rad);
                }
            }
            continue;
        }
        if (it.kind == 2) {
            for (const auto& p : it.rings[0]) MeshBuild3D::EmitOctahedron(mesh, toMesh(p, p.z), static_cast<float>(span * 0.004));
            continue;
        }
        Ring& outerRing = it.rings[0];
        bool flat = true;
        for (const auto& p : outerRing) flat = flat && p.z == outerRing[0].z;
        const double z0 = outerRing[0].z;
        double bottom = 0.0, top = 0.0;
        bool extrude = false;
        if (flat && it.height > 0.0 && it.height > it.minHeight) { // building: height above its footprint
            bottom = z0 + it.minHeight;
            top = z0 + it.height;
            extrude = true;
        } else if (flat && it.extrudeToGround && z0 > 0.0) {      // "extrude": the footprint's elevation down to the ground
            top = z0;
            extrude = true;
        }
        if (extrude) {
            // Outer ring counter-clockwise, holes clockwise, so every wall faces away from the solid
            if (signedArea(outerRing) < 0.0) std::reverse(outerRing.begin(), outerRing.end());
            std::vector<const Ring*> rings = { &outerRing };
            for (size_t h = 1; h < it.rings.size(); ++h) {
                if (it.rings[h].size() < 3) continue;
                if (signedArea(it.rings[h]) > 0.0) std::reverse(it.rings[h].begin(), it.rings[h].end());
                rings.push_back(&it.rings[h]);
            }
            auto ringsAt = [&](double h, std::vector<Vec3f>& outer3D, std::vector<std::vector<Vec3f>>& holes3D) {
                for (const auto& p : outerRing) outer3D.push_back(toMesh(p, h));
                for (size_t r = 1; r < rings.size(); ++r) {
                    holes3D.emplace_back();
                    for (const auto& p : *rings[r]) holes3D.back().push_back(toMesh(p, h));
                }
            };
            // Roof (and a floor for a part above the ground) first: their vertices, ring by ring, are the walls' corners
            std::vector<Vec3f> cap;
            std::vector<std::vector<Vec3f>> capHoles;
            const uint32_t roofBase = static_cast<uint32_t>(mesh.vertices.size());
            ringsAt(top, cap, capHoles);
            MeshBuild3D::EmitPlanarPolygon(mesh, cap, capHoles, &up);
            const uint32_t floorBase = static_cast<uint32_t>(mesh.vertices.size());
            if (bottom > 0.0) {
                cap.clear();
                capHoles.clear();
                ringsAt(bottom, cap, capHoles);
                MeshBuild3D::EmitPlanarPolygon(mesh, cap, capHoles, &down);
            } else {
                for (const Ring* r : rings) {
                    for (const auto& p : *r) mesh.vertices.push_back(toMesh(p, bottom));
                }
            }
            uint32_t first = 0;
            for (const Ring* r : rings) {
                const uint32_t n = static_cast<uint32_t>(r->size());
                for (uint32_t k = 0; k < n; ++k) {
                    const uint32_t a = first + k, b = first + (k + 1) % n;
                    mesh.tris.push_back({ floorBase + a, floorBase + b, roofBase + b, {} });
                    mesh.tris.push_back({ floorBase + a, roofBase + b, roofBase + a, {} });
                }
                first += n;
            }
        } else {
            std::vector<Vec3f> outer3D;
            std::vector<std::vector<Vec3f>> holes3D;
            for (const auto& p : outerRing) outer3D.push_back(toMesh(p, p.z));
            for (size_t r = 1; r < it.rings.size(); ++r) {
                holes3D.emplace_back();
                for (const auto& p : it.rings[r]) holes3D.back().push_back(toMesh(p, p.z));
            }
            MeshBuild3D::EmitPlanarPolygon(mesh, outer3D, holes3D, flat ? &up : nullptr);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"3D GeoJSON Feature Scene (.geojson)";
    return true;
}

} // namespace BspGeoDetail
} // namespace Formats
