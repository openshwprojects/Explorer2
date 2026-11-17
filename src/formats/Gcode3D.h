#pragma once
#include "MeshBuild3D.h"
// Gcode3D.h — 3D printer / CNC G-code (.gcode / .nc) toolpaths. Words are parsed per line (";" and "( )"
// comments removed) with exact command numbers (G1 is not G10), absolute / relative positioning (G90 / G91,
// M82 / M83 for the extruder), millimetres / inches (G21 / G20), position resets (G92) and arcs (G2 / G3 with
// I / J centre offsets). In printer files only extruding moves form the part (travel moves are left out);
// files without any E word are CNC programs, where every feed move (G1 / G2 / G3) cuts. Long programs are
// sampled evenly over the whole job instead of being cut off after the first layers.

namespace Formats {

inline bool ParseGcodeToolpathMesh(std::string_view sv, LoadedMesh3D& mesh) {
    mesh.formatName = L"G-Code 3D Toolpath (.gcode)";

    struct Seg { float ax, ay, az, bx, by, bz; };
    std::vector<Seg> segs;
    segs.reserve(4096);
    double x = 0.0, y = 0.0, z = 0.0, e = 0.0;
    bool absXYZ = true, absE = true;
    double unit = 1.0; // mm per program unit
    bool anyE = false;
    int lastMotion = -1;
    size_t moveCommands = 0;
    std::vector<Seg> feedSegs; // CNC: all feed moves, used when the file has no E words at all

    auto parseNum = [](std::string_view s, size_t& i, double& out) {
        size_t st = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.')) ++i;
        if (i == st) return false;
        std::string_view num = s.substr(st, i - st);
        if (!num.empty() && num.front() == '+') num.remove_prefix(1);
        auto res = std::from_chars(num.data(), num.data() + num.size(), out);
        return res.ec == std::errc{} && std::isfinite(out);
    };
    auto addSeg = [&](std::vector<Seg>& dst, double ax, double ay, double az, double bx, double by, double bz) {
        if (dst.size() >= 4000000) return;
        // Printer Z-up -> viewer Y-up
        dst.push_back({ static_cast<float>(ax), static_cast<float>(az), static_cast<float>(-ay),
                        static_cast<float>(bx), static_cast<float>(bz), static_cast<float>(-by) });
    };

    for (size_t pos = 0; pos < sv.size();) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        size_t semi = ln.find(';');
        if (semi != std::string_view::npos) ln = ln.substr(0, semi);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        // M117 / M118 carry display text, not words
        if (ln.size() >= 4 && (ln[0] == 'M' || ln[0] == 'm') && (ln.substr(1, 3) == "117" || ln.substr(1, 3) == "118")) continue;

        // Words: letter + number; "(comment)" skipped; N line numbers and *checksums ignored.
        // Motion (G0-G3) is modal: a line with coordinates only repeats the last motion command.
        int motion = -1;
        bool setPos = false, home = false;
        bool hasX = false, hasY = false, hasZ = false, hasE = false, hasI = false, hasJ = false;
        double vx = 0, vy = 0, vz = 0, ve = 0, vi = 0, vj = 0;
        for (size_t i = 0; i < ln.size();) {
            char c = ln[i];
            if (c == '(') { size_t close = ln.find(')', i); i = (close == std::string_view::npos) ? ln.size() : close + 1; continue; }
            if (c == '*') break;
            char up = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (up < 'A' || up > 'Z') { ++i; continue; }
            ++i;
            while (i < ln.size() && (ln[i] == ' ' || ln[i] == '\t')) ++i;
            double v = 0.0;
            if (!parseNum(ln, i, v)) continue;
            if (up == 'G') {
                if (v != std::floor(v)) continue; // G28.1, G38.2, ...: not drawn
                int g = static_cast<int>(v);
                if (g >= 0 && g <= 3) motion = g;
                else if (g == 90) absXYZ = true;
                else if (g == 91) absXYZ = false;
                else if (g == 20) unit = 25.4;
                else if (g == 21) unit = 1.0;
                else if (g == 92) setPos = true;
                else if (g == 28) home = true;
            } else if (up == 'M') {
                if (v == 82) absE = true;
                else if (v == 83) absE = false;
            }
            else if (up == 'X') { hasX = true; vx = v; }
            else if (up == 'Y') { hasY = true; vy = v; }
            else if (up == 'Z') { hasZ = true; vz = v; }
            else if (up == 'E') { hasE = true; ve = v; }
            else if (up == 'I') { hasI = true; vi = v; }
            else if (up == 'J') { hasJ = true; vj = v; }
        }
        if (setPos) {
            // G92: set the current position (no move)
            if (hasX) x = vx * unit;
            if (hasY) y = vy * unit;
            if (hasZ) z = vz * unit;
            if (hasE) e = ve;
            continue;
        }
        if (home) {
            // G28: the named axes (all when none is named) go to the machine origin
            bool all = !(hasX || hasY || hasZ);
            if (all || hasX) x = 0.0;
            if (all || hasY) y = 0.0;
            if (all || hasZ) z = 0.0;
            continue;
        }
        if (motion >= 0) lastMotion = motion;
        else if (hasX || hasY || hasZ || hasE) motion = lastMotion;
        int gCode = motion;
        if (gCode < 0 || !(hasX || hasY || hasZ || hasE)) continue;
        ++moveCommands;
        double nx = hasX ? (absXYZ ? vx * unit : x + vx * unit) : x;
        double ny = hasY ? (absXYZ ? vy * unit : y + vy * unit) : y;
        double nz = hasZ ? (absXYZ ? vz * unit : z + vz * unit) : z;
        bool extruding = false;
        if (hasE) {
            anyE = true;
            double ne = absE ? ve : e + ve;
            extruding = ne > e + 1e-9;
            e = ne;
        }
        // A G2 / G3 whose end point is its start point is a full circle
        bool moved = (nx != x || ny != y || nz != z) || ((gCode == 2 || gCode == 3) && (hasI || hasJ));
        if (moved) {
            if (gCode == 2 || gCode == 3) {
                // Arc in the XY plane around (x+I, y+J); clockwise for G2
                double cx = x + (hasI ? vi * unit : 0.0), cy = y + (hasJ ? vj * unit : 0.0);
                double r = std::hypot(x - cx, y - cy);
                double a0 = std::atan2(y - cy, x - cx), a1 = std::atan2(ny - cy, nx - cx);
                double sweep = a1 - a0;
                if (gCode == 2) { if (sweep >= 0) sweep -= 2 * 3.14159265358979; }
                else { if (sweep <= 0) sweep += 2 * 3.14159265358979; }
                int steps = std::clamp(static_cast<int>(std::fabs(sweep) / (3.14159265358979 / 18.0)) + 1, 1, 72);
                double px = x, py = y, pz = z;
                for (int s = 1; s <= steps && r > 1e-9; ++s) {
                    double t = static_cast<double>(s) / steps;
                    double qx = (s == steps) ? nx : cx + r * std::cos(a0 + sweep * t);
                    double qy = (s == steps) ? ny : cy + r * std::sin(a0 + sweep * t);
                    double qz = z + (nz - z) * t;
                    if (extruding) addSeg(segs, px, py, pz, qx, qy, qz);
                    addSeg(feedSegs, px, py, pz, qx, qy, qz);
                    px = qx; py = qy; pz = qz;
                }
            } else {
                if (extruding) addSeg(segs, x, y, z, nx, ny, nz);
                if (gCode != 0) addSeg(feedSegs, x, y, z, nx, ny, nz);
            }
        }
        x = nx; y = ny; z = nz;
    }

    // Printer files: extrusion only. CNC files (no E anywhere): every feed move.
    std::vector<Seg>& path = anyE ? segs : feedSegs;
    if (path.empty()) return false;

    // Strut thickness from the part size
    float minX = path[0].ax, maxX = minX, minY = path[0].ay, maxY = minY, minZ = path[0].az, maxZ = minZ;
    for (const auto& s : path) {
        minX = std::min({ minX, s.ax, s.bx }); maxX = std::max({ maxX, s.ax, s.bx });
        minY = std::min({ minY, s.ay, s.by }); maxY = std::max({ maxY, s.ay, s.by });
        minZ = std::min({ minZ, s.az, s.bz }); maxZ = std::max({ maxZ, s.az, s.bz });
    }
    float span = std::max({ maxX - minX, maxY - minY, maxZ - minZ, 1e-3f });
    float rad = std::max(span * 0.0025f, 0.02f);

    // Even sampling over the whole job keeps every layer visible within the triangle budget
    constexpr size_t kMaxSegments = 60000;
    size_t stride = (path.size() + kMaxSegments - 1) / kMaxSegments;
    for (size_t i = 0; i < path.size(); i += stride) {
        const Seg& s = path[i];
        MeshBuild3D::EmitEdgeStrut(mesh, Vec3f{ s.ax, s.ay, s.az }, Vec3f{ s.bx, s.by, s.bz }, rad);
    }
    (void)moveCommands;
    return !mesh.tris.empty();
}

} // namespace Formats
