#pragma once
// LightWaveSceneLws.h — LightWave 3D Scene (.lws) hierarchy & .lwo layer assembler.
#include "../common/MeshFormatCommon.h"
#include "LightWaveLwo.h"

namespace Formats {

// ============================================================================
// 69b. LightWave Scene (.lws) and AutoCAD R11/R12 (.dwg AC1009 fallback)
// ============================================================================
namespace DetailCadDcc {

// LightWave Scene (.lws - ASCII "LWSC" v1..v5). A scene holds no geometry of its own: every object is a separate
// .lwo file named on its LoadObjectLayer / LoadObject line. Each one is found next to the scene or in the content
// directory, read with ParseLightWaveLwoLayer (just the layer the scene asks for), and placed with the item's
// motion at the first key: position, heading/pitch/bank, scale and pivot, through the whole parent chain.
// Cameras, lights, nulls and bones have no geometry and are drawn as markers, with the motion paths. An object
// whose file cannot be found or read is named in the status line and left out - never stood in for by a shape.
namespace DetailLws {

struct Key { float time = 0.0f; float val = 0.0f; };

struct Item {
    enum Kind { Obj, Null, Cam, Light, Bone } kind = Obj;
    uint32_t id = 0;
    uint32_t parentId = 0;
    bool hasParent = false;
    int layer = 1;                 // LoadObjectLayer layer (1-based)
    std::string objPath;           // object file as written in the scene
    Vec3f pivot = { 0, 0, 0 };
    Vec3f boneRest = { 0, 0, 0 };
    bool hasBoneRest = false;
    std::vector<Key> chan[9];      // X, Y, Z, H, P, B (radians), SX, SY, SZ
};

inline std::string_view TrimLws(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && static_cast<unsigned char>(s[a]) <= 32) ++a;
    while (b > a && static_cast<unsigned char>(s[b - 1]) <= 32) --b;
    return s.substr(a, b - a);
}

inline bool IsHex8(std::string_view t) {
    if (t.size() != 8) return false;
    for (char c : t) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

// Splits "<token> <rest>" (rest trimmed); empty token when s is empty
inline std::string_view SplitFirst(std::string_view s, std::string_view& rest) {
    s = TrimLws(s);
    size_t sp = 0;
    while (sp < s.size() && static_cast<unsigned char>(s[sp]) > 32) ++sp;
    rest = TrimLws(s.substr(sp));
    return s.substr(0, sp);
}

inline bool IsBareInteger(std::string_view s) {
    s = TrimLws(s);
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}

inline int ParseFloats(std::string_view s, float* out, int maxCount) {
    std::string tmp(s);
    const char* p = tmp.c_str();
    int n = 0;
    while (n < maxCount) {
        char* endp = nullptr;
        double v = std::strtod(p, &endp);
        if (endp == p) break;
        out[n++] = std::isfinite(v) ? static_cast<float>(v) : 0.0f;
        p = endp;
    }
    return n;
}

inline float SampleKeys(const std::vector<Key>& keys, float t, float fallback) {
    if (keys.empty()) return fallback;
    if (t <= keys.front().time || keys.size() == 1) return keys.front().val;
    if (t >= keys.back().time) return keys.back().val;
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (t >= keys[i].time && t <= keys[i + 1].time) {
            float dt = keys[i + 1].time - keys[i].time;
            float u = (dt > 1e-6f) ? ((t - keys[i].time) / dt) : 0.0f;
            return keys[i].val + (keys[i + 1].val - keys[i].val) * u;
        }
    }
    return keys.back().val;
}

inline bool IsExistingFile(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Finds the object file a scene names. LightWave writes paths relative to the content directory (usually the
// folder above "Scenes"), sometimes absolute, with '/', '\' or the old "Objects:box.lwo" form. Tried in order:
// the absolute path, the path under the scene folder and up to three folders above it, then the bare file name in
// the scene folder and in "Objects" next to it and above it. Network (UNC) and device paths and ".." are refused:
// opening a downloaded scene must not reach out to other machines or wander outside its folders.
inline std::wstring ResolveObjectPath(const std::wstring& sceneDir, const std::string& raw, std::wstring& whyNot) {
    std::wstring p = PathUtils::Trim(PathUtils::Utf8ToWide(raw));
    if (p.size() >= 2 && p.front() == L'"' && p.back() == L'"') p = p.substr(1, p.size() - 2);
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i] == L'/') p[i] = L'\\';
        else if (p[i] == L':' && i != 1) p[i] = L'\\';     // "Objects:box.lwo" (not the "C:" of a drive)
    }
    if (p.empty()) { whyNot = L"brak nazwy pliku"; return {}; }
    if (p.rfind(L"\\\\", 0) == 0) { whyNot = L"ścieżka sieciowa"; return {}; }
    {
        std::wstring padded = L"\\" + p + L"\\";
        if (padded.find(L"\\..\\") != std::wstring::npos) { whyNot = L"ścieżka poza folderem"; return {}; }
    }
    if (PathUtils::GetExtensionLower(p) != L".lwo") { whyNot = L"nie jest plikiem .lwo"; return {}; }
    if (sceneDir.empty()) { whyNot = L"nie znaleziono"; return {}; }

    const bool isAbsolute = p.size() >= 3 && p[1] == L':' && p[2] == L'\\';
    std::vector<std::wstring> candidates;
    if (isAbsolute) candidates.push_back(p);
    std::vector<std::wstring> dirs = { sceneDir };
    for (int up = 0; up < 3; ++up) {
        std::wstring parent = PathUtils::GetParentPath(dirs.back());
        if (parent.empty() || parent == dirs.back()) break;
        dirs.push_back(parent);
    }
    if (!isAbsolute) {
        std::wstring rel = p;
        while (!rel.empty() && rel.front() == L'\\') rel.erase(0, 1);
        for (const auto& d : dirs) candidates.push_back(PathUtils::TrimTrailingSlashes(d) + L"\\" + rel);
    }
    const std::wstring fileName = PathUtils::GetFileName(p);
    candidates.push_back(PathUtils::TrimTrailingSlashes(sceneDir) + L"\\" + fileName);
    for (const auto& d : dirs) candidates.push_back(PathUtils::TrimTrailingSlashes(d) + L"\\Objects\\" + fileName);

    for (const auto& c : candidates) {
        if (c.rfind(L"\\\\", 0) == 0) continue;
        if (IsExistingFile(c)) return c;
    }
    whyNot = L"nie znaleziono";
    return {};
}

} // namespace DetailLws

inline bool ParseLightWaveScene(std::string_view sv, const std::wstring& scenePath, LoadedMesh3D& mesh) {
    using namespace Detail3D100;
    using DetailLws::Item;
    using DetailLws::Key;
    using M4 = DetailScad::Mat4;
    std::string_view trimmed = DetailStep::TrimSv(sv);
    if (trimmed.rfind("LWSC", 0) != 0) return false;
    mesh.formatName = L"LightWave 3D Scene (.lws)";

    // ---- 1. Read the scene: items, their object files, pivots, parents and motion channels ----------------------
    std::vector<std::string_view> lines;
    for (size_t pos = 0; pos < sv.size() && lines.size() < 4000000;) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        lines.push_back(DetailLws::TrimLws(sv.substr(pos, eol - pos)));
        pos = eol + 1;
    }
    int version = 3;
    for (size_t i = 1; i < lines.size() && i < 4; ++i) {
        if (DetailLws::IsBareInteger(lines[i])) { version = std::atoi(std::string(lines[i]).c_str()); break; }
    }
    // LightWave 6+ (scene version 3 and later) keeps rotations in radians, older scenes in degrees
    const float rotToRad = (version >= 3) ? 1.0f : 0.017453292519943295f;

    std::vector<Item> items;
    uint32_t objCount = 0, camCount = 0, lightCount = 0, boneCount = 0;
    uint32_t lastObjectId = 0;
    bool haveObject = false;
    int curChan = -1;

    for (size_t li = 0; li < lines.size() && items.size() < 4096; ++li) {
        std::string_view ln = lines[li];
        if (ln.empty()) continue;
        std::string_view rest;
        const std::string_view kw = DetailLws::SplitFirst(ln, rest);

        if (kw == "LoadObjectLayer" || kw == "LoadObject" || kw == "AddNullObject") {
            Item it;
            it.kind = (kw == "AddNullObject") ? Item::Null : Item::Obj;
            it.id = 0x10000000u | objCount++;
            if (kw == "LoadObjectLayer") {
                std::string_view after;
                std::string_view layerTok = DetailLws::SplitFirst(rest, after);
                it.layer = std::max(1, std::atoi(std::string(layerTok).c_str()));
                rest = after;
            }
            // LightWave 9+ writes the item ID before the file / null name: "LoadObjectLayer 1 10000000 Objects/a.lwo"
            std::string_view after;
            std::string_view first = DetailLws::SplitFirst(rest, after);
            if (DetailLws::IsHex8(first) && !after.empty()) {
                it.id = static_cast<uint32_t>(std::strtoul(std::string(first).c_str(), nullptr, 16));
                rest = after;
            }
            if (it.kind == Item::Obj) it.objPath = std::string(rest);
            lastObjectId = it.id;
            haveObject = true;
            items.push_back(std::move(it));
            curChan = -1;
        } else if (kw == "AddCamera" || kw == "AddLight" || kw == "AddBone") {
            Item it;
            if (kw == "AddCamera") { it.kind = Item::Cam; it.id = 0x30000000u | camCount++; }
            else if (kw == "AddLight") { it.kind = Item::Light; it.id = 0x20000000u | lightCount++; }
            else {
                it.kind = Item::Bone;
                it.id = 0x40000000u | boneCount++;
                if (haveObject) { it.parentId = lastObjectId; it.hasParent = true; }   // a bone belongs to its object
            }
            std::string_view after;
            std::string_view first = DetailLws::SplitFirst(rest, after);
            if (DetailLws::IsHex8(first)) it.id = static_cast<uint32_t>(std::strtoul(std::string(first).c_str(), nullptr, 16));
            items.push_back(std::move(it));
            curChan = -1;
        } else if (!items.empty()) {
            Item& cur = items.back();
            float f[9] = {};
            if (kw == "PivotPosition" || kw == "PivotPoint") {
                if (DetailLws::ParseFloats(rest, f, 3) == 3) cur.pivot = { f[0], f[1], f[2] };
            } else if (kw == "BoneRestPosition") {
                if (DetailLws::ParseFloats(rest, f, 3) == 3) { cur.boneRest = { f[0], f[1], f[2] }; cur.hasBoneRest = true; }
            } else if (kw == "ParentItem") {
                cur.parentId = static_cast<uint32_t>(std::strtoul(std::string(rest).c_str(), nullptr, 16));
                cur.hasParent = true;
            } else if (kw == "ParentObject") {
                int idx1 = std::atoi(std::string(rest).c_str());
                if (idx1 >= 1) { cur.parentId = 0x10000000u | static_cast<uint32_t>(idx1 - 1); cur.hasParent = true; }
            } else if (kw == "Channel") {
                curChan = std::atoi(std::string(rest).c_str());
            } else if (kw == "}") {
                curChan = -1;      // the channel's envelope ends; later envelopes (light intensity, ...) are not motion
            } else if (kw == "Key" && curChan >= 0 && curChan < 9) {
                if (DetailLws::ParseFloats(rest, f, 2) == 2) {
                    cur.chan[curChan].push_back({ f[1], (curChan >= 3 && curChan <= 5) ? f[0] * rotToRad : f[0] });
                }
            } else if ((kw == "ObjectMotion" || kw == "CameraMotion" || kw == "LightMotion" || kw == "BoneMotion") &&
                       li + 2 < lines.size() && DetailLws::IsBareInteger(lines[li + 1]) && DetailLws::IsBareInteger(lines[li + 2])) {
                // LightWave 5 motion block: channel count, key count, then per key a line of channel values
                // (X Y Z H P B SX SY SZ, rotations in degrees) and a line "frame linear tension continuity bias"
                const int nch = std::min(9, std::atoi(std::string(lines[li + 1]).c_str()));
                const int nkeys = std::atoi(std::string(lines[li + 2]).c_str());
                size_t k = li + 3;
                for (int key = 0; key < nkeys && key < 100000 && k + 1 < lines.size(); ++key, k += 2) {
                    float vals[9] = {};
                    float frame[1] = {};
                    if (DetailLws::ParseFloats(lines[k], vals, nch) != nch || DetailLws::ParseFloats(lines[k + 1], frame, 1) != 1) break;
                    for (int c = 0; c < nch; ++c) {
                        cur.chan[c].push_back({ frame[0], (c >= 3 && c <= 5) ? vals[c] * rotToRad : vals[c] });
                    }
                }
                li = k - 1;
            }
        }
    }
    if (items.empty()) return false;
    for (auto& it : items) {
        for (auto& ch : it.chan) {
            std::stable_sort(ch.begin(), ch.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
            mesh.animFrameCount = std::max(mesh.animFrameCount, ch.size());
        }
    }

    std::unordered_map<uint32_t, size_t> idMap;
    for (size_t i = 0; i < items.size(); ++i) idMap.emplace(items[i].id, i);

    // ---- 2. Item transforms: parent * T(position) * Ry(heading) * Rx(pitch) * Rz(bank) * S(scale) ------------------
    // LightWave is left-handed (+Y up, +Z forward); positive heading turns +Z towards +X, positive pitch tilts +Z
    // down, positive bank turns +X towards +Y. The object's geometry hangs off this frame shifted by -pivot; children
    // follow the frame itself (not the parent's pivot), as in Layout.
    constexpr float kRadToDeg = 57.29577951308232f;
    auto localMatrix = [&](const Item& it, float t) -> M4 {
        const Vec3f rest = it.hasBoneRest ? it.boneRest : Vec3f{ 0, 0, 0 };
        const float x = DetailLws::SampleKeys(it.chan[0], t, rest.x);
        const float y = DetailLws::SampleKeys(it.chan[1], t, rest.y);
        const float z = DetailLws::SampleKeys(it.chan[2], t, rest.z);
        const float h = DetailLws::SampleKeys(it.chan[3], t, 0.0f) * kRadToDeg;
        const float p = DetailLws::SampleKeys(it.chan[4], t, 0.0f) * kRadToDeg;
        const float b = DetailLws::SampleKeys(it.chan[5], t, 0.0f) * kRadToDeg;
        const float sx = DetailLws::SampleKeys(it.chan[6], t, 1.0f);
        const float sy = DetailLws::SampleKeys(it.chan[7], t, 1.0f);
        const float sz = DetailLws::SampleKeys(it.chan[8], t, 1.0f);
        M4 r = M4::Mul(M4::RotateAxisAngle(h, 0, 1, 0), M4::Mul(M4::RotateAxisAngle(p, 1, 0, 0), M4::RotateAxisAngle(b, 0, 0, 1)));
        return M4::Mul(M4::Translate(x, y, z), M4::Mul(r, M4::Scale(sx, sy, sz)));
    };
    std::function<M4(size_t, float, int)> worldMatrix = [&](size_t idx, float t, int depth) -> M4 {
        const Item& it = items[idx];
        M4 local = localMatrix(it, t);
        if (it.hasParent && depth < 32) {
            auto pit = idMap.find(it.parentId);
            if (pit != idMap.end() && pit->second != idx) return M4::Mul(worldMatrix(pit->second, t, depth + 1), local);
        }
        return local;
    };
    float t0 = 1e30f;
    for (const auto& it : items) for (const auto& ch : it.chan) if (!ch.empty()) t0 = std::min(t0, ch.front().time);
    if (t0 > 1e29f) t0 = 0.0f;

    // ---- 3. Objects: read each referenced .lwo layer once, place a copy per item ----------------------------------
    const std::wstring sceneDir = scenePath.empty() ? std::wstring() : PathUtils::GetParentPath(scenePath);
    struct ObjGeom { bool ok = false; std::wstring why; std::vector<Vec3f> v; std::vector<MeshTriangle> t; };
    std::map<std::wstring, ObjGeom> cache;
    constexpr size_t kMaxVerts = 2000000, kMaxTris = 4000000;
    size_t objItems = 0, objPlaced = 0;
    bool cut = false;
    std::vector<std::wstring> problems;
    auto addProblem = [&](const std::string& raw, const std::wstring& why) {
        std::wstring s = PathUtils::Utf8ToWide(raw) + L" (" + why + L")";
        if (std::find(problems.begin(), problems.end(), s) == problems.end()) problems.push_back(s);
    };

    for (size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        if (it.kind != Item::Obj) continue;
        ++objItems;
        std::wstring whyNot;
        const std::wstring file = DetailLws::ResolveObjectPath(sceneDir, it.objPath, whyNot);
        if (file.empty()) { addProblem(it.objPath, whyNot); continue; }
        const std::wstring key = PathUtils::NormalizePathKey(file) + L"|" + std::to_wstring(it.layer);
        auto ci = cache.find(key);
        if (ci == cache.end()) {
            ObjGeom g;
            std::vector<uint8_t> bytes;
            if (!PathUtils::ReadFileBytes(file, 128 * 1024 * 1024, bytes) || bytes.empty()) {
                g.why = L"nie można odczytać";
            } else {
                LoadedMesh3D om;
                int layers = 0;
                bool ok = ParseLightWaveLwoLayer(bytes.data(), bytes.size(), om, it.layer - 1, &layers);
                if (!ok && layers == 1 && it.layer != 1) {
                    om = LoadedMesh3D{};
                    ok = ParseLightWaveLwoLayer(bytes.data(), bytes.size(), om, -1);   // single-layer file
                }
                if (ok) { g.ok = true; g.v = std::move(om.vertices); g.t = std::move(om.tris); }
                else g.why = (layers > 1) ? (L"brak warstwy " + std::to_wstring(it.layer)) : L"uszkodzony plik .lwo";
            }
            ci = cache.emplace(key, std::move(g)).first;
        }
        const ObjGeom& g = ci->second;
        if (!g.ok) { addProblem(it.objPath, g.why); continue; }
        if (mesh.vertices.size() + g.v.size() > kMaxVerts || mesh.tris.size() + g.t.size() > kMaxTris) { cut = true; continue; }

        const M4 place = M4::Mul(worldMatrix(i, t0, 0), M4::Translate(-it.pivot.x, -it.pivot.y, -it.pivot.z));
        const bool flip = place.Det3x3() < 0.0f;
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (const Vec3f& v : g.v) mesh.vertices.push_back(place.TransformPoint(v));
        for (const MeshTriangle& t : g.t) {
            if (flip) mesh.tris.push_back({ base + t.i0, base + t.i2, base + t.i1, {} });
            else      mesh.tris.push_back({ base + t.i0, base + t.i1, base + t.i2, {} });
        }
        ++objPlaced;
    }

    // ---- 4. Markers for items without geometry, parent links and motion paths -----------------------------------
    auto originAt = [&](size_t idx, float t) -> Vec3f { return worldMatrix(idx, t, 0).TransformPoint({ 0, 0, 0 }); };
    Vec3f mn = originAt(0, t0), mx = mn;
    auto grow = [&](const Vec3f& p) {
        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
    };
    for (const Vec3f& v : mesh.vertices) grow(v);
    for (size_t i = 0; i < items.size(); ++i) {
        grow(originAt(i, t0));
        for (int c = 0; c < 3; ++c) for (const auto& k : items[i].chan[c]) grow(originAt(i, k.time));
    }
    const float span = std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 1e-3f });
    const float gizmoSz = span * 0.06f;
    const float strutR = span * 0.008f;

    for (size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        const M4 w = worldMatrix(i, t0, 0);
        const Vec3f p0 = w.TransformPoint({ 0, 0, 0 });
        auto at = [&](float lx, float ly, float lz) { return w.TransformPoint({ lx, ly, lz }); };

        if (it.kind == Item::Cam) {
            // Viewing pyramid along the camera's own +Z
            Vec3f corners[4] = {
                at(-gizmoSz, -gizmoSz * 0.7f, gizmoSz * 1.6f), at(gizmoSz, -gizmoSz * 0.7f, gizmoSz * 1.6f),
                at(gizmoSz, gizmoSz * 0.7f, gizmoSz * 1.6f),   at(-gizmoSz, gizmoSz * 0.7f, gizmoSz * 1.6f)
            };
            for (int c = 0; c < 4; ++c) {
                AddLineStrut(mesh, p0, corners[c], strutR);
                AddLineStrut(mesh, corners[c], corners[(c + 1) & 3], strutR);
            }
        } else if (it.kind == Item::Light || it.kind == Item::Null || it.kind == Item::Bone) {
            const float s = (it.kind == Item::Light) ? gizmoSz : gizmoSz * 0.5f;
            AddLineStrut(mesh, at(-s, 0, 0), at(s, 0, 0), strutR);
            AddLineStrut(mesh, at(0, -s, 0), at(0, s, 0), strutR);
            AddLineStrut(mesh, at(0, 0, -s), at(0, 0, s), strutR);
        }

        if (it.hasParent) {
            auto pit = idMap.find(it.parentId);
            if (pit != idMap.end() && pit->second != i) AddLineStrut(mesh, originAt(pit->second, t0), p0, strutR * 1.2f);
        }

        float tMin = 1e30f, tMax = -1e30f;
        for (int c = 0; c < 3; ++c) {
            for (const auto& k : it.chan[c]) { tMin = std::min(tMin, k.time); tMax = std::max(tMax, k.time); }
        }
        if (tMax > tMin + 1e-5f) {
            const int steps = 24;
            Vec3f prev = originAt(i, tMin);
            for (int s = 1; s <= steps; ++s) {
                const float tm = tMin + (tMax - tMin) * (static_cast<float>(s) / static_cast<float>(steps));
                const Vec3f curP = originAt(i, tm);
                AddLineStrut(mesh, prev, curP, strutR);
                prev = curP;
            }
        }
    }

    // ---- 5. Status: how many objects were placed, and which could not be ----------------------------------------
    std::wstring status = L"Obiekty sceny: " + std::to_wstring(objPlaced) + L"/" + std::to_wstring(objItems);
    if (!problems.empty()) {
        status += L"; brak: ";
        for (size_t k = 0; k < problems.size() && k < 3; ++k) status += (k ? L", " : L"") + problems[k];
        if (problems.size() > 3) status += L" i " + std::to_wstring(problems.size() - 3) + L" innych";
    }
    if (cut) status += L"; pokazano część (za duża scena)";
    mesh.statusMessage = status;
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailCadDcc

inline bool ParseLightWaveSceneLws(std::string_view sv, const std::wstring& scenePath, LoadedMesh3D& mesh) {
    return DetailCadDcc::ParseLightWaveScene(sv, scenePath, mesh);
}

} // namespace Formats
