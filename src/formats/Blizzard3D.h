#pragma once
// Blizzard3D.h — Warcraft III Binary/ASCII Models (.mdx "MDLX", .mdl v800+) & World of Warcraft (.m2 "MD20"/"MD21" + .skin, .wmo "REVM")

namespace Formats {

namespace BlizzDetail {

inline Vec3f BlizzZUpToViewport(float x, float y, float z) {
    return { x, z, -y };
}

inline bool ParseWmoGroupBytes(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 20 || std::memcmp(data, "REVM", 4) != 0) return false;
    size_t pos = 0;
    size_t trisBefore = mesh.tris.size();

    while (pos + 8 <= size) {
        char tag[4];
        uint32_t cSize = 0;
        std::memcpy(tag, data + pos, 4);
        std::memcpy(&cSize, data + pos + 4, 4);
        pos += 8;
        if (cSize > size - pos) break;

        if (std::memcmp(tag, "PGOM", 4) == 0 && cSize >= 68) {
            // MOGP group chunk: 68-byte group header followed by nested sub-chunks (MOVI="IVOM", MOVT="TVOM", ...)
            size_t subPos = pos + 68;
            const size_t subEnd = pos + cSize;
            std::vector<uint16_t> movi;
            std::vector<Vec3f> movt;

            while (subPos + 8 <= subEnd) {
                char stag[4];
                uint32_t sSize = 0;
                std::memcpy(stag, data + subPos, 4);
                std::memcpy(&sSize, data + subPos + 4, 4);
                subPos += 8;
                if (sSize > subEnd - subPos) break;

                if (std::memcmp(stag, "IVOM", 4) == 0 && sSize >= 6 && (sSize % 6) == 0) {
                    size_t cnt = sSize / 2;
                    movi.resize(cnt);
                    std::memcpy(movi.data(), data + subPos, sSize);
                } else if (std::memcmp(stag, "TVOM", 4) == 0 && sSize >= 36 && (sSize % 12) == 0) {
                    size_t cnt = sSize / 12;
                    movt.resize(cnt);
                    for (size_t i = 0; i < cnt; ++i) {
                        float vx = 0.0f, vy = 0.0f, vz = 0.0f;
                        std::memcpy(&vx, data + subPos + i * 12 + 0, 4);
                        std::memcpy(&vy, data + subPos + i * 12 + 4, 4);
                        std::memcpy(&vz, data + subPos + i * 12 + 8, 4);
                        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
                        movt[i] = BlizzZUpToViewport(vx, vy, vz);
                    }
                }
                subPos += sSize;
            }

            if (!movt.empty() && movi.size() >= 3) {
                uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                for (const auto& v : movt) mesh.vertices.push_back(v);
                for (size_t t = 0; t + 2 < movi.size(); t += 3) {
                    uint16_t a = movi[t], b = movi[t + 1], c = movi[t + 2];
                    if (a >= movt.size() || b >= movt.size() || c >= movt.size()) return false;
                    if (a == b || b == c || c == a) continue;
                    mesh.tris.push_back({ baseV + a, baseV + c, baseV + b, {} });
                }
            }
        }
        pos += cSize;
    }

    return mesh.tris.size() > trisBefore;
}

} // namespace BlizzDetail

// Warcraft III Binary Model (.mdx — magic "MDLX")
inline bool ParseWarcraftMdx(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 16 || std::memcmp(data, "MDLX", 4) != 0) return false;
    size_t pos = 4;
    size_t seqCount = 1;

    while (pos + 8 <= size) {
        char tag[4];
        uint32_t chunkSize = 0;
        std::memcpy(tag, data + pos, 4);
        std::memcpy(&chunkSize, data + pos + 4, 4);
        pos += 8;
        if (chunkSize > size - pos) return false;

        if (std::memcmp(tag, "SEQS", 4) == 0 && chunkSize >= 132) {
            seqCount = std::max<size_t>(1, chunkSize / 132);
        } else if (std::memcmp(tag, "GEOS", 4) == 0 && chunkSize >= 20) {
            size_t gPos = pos;
            const size_t gEnd = pos + chunkSize;
            while (gPos + 4 <= gEnd) {
                uint32_t incSize = 0;
                std::memcpy(&incSize, data + gPos, 4);
                if (incSize < 20 || incSize > gEnd - gPos) return false;
                const size_t nextGeoset = gPos + incSize;
                size_t sPos = gPos + 4;

                std::vector<Vec3f> vrtx;
                std::vector<uint16_t> pvtx;

                // Walk ordered Geoset sub-arrays: VRTX -> NRMS -> PTYP -> PCNT -> PVTX
                while (sPos + 8 <= nextGeoset) {
                    char subTag[4];
                    uint32_t cnt = 0;
                    std::memcpy(subTag, data + sPos, 4);
                    std::memcpy(&cnt, data + sPos + 4, 4);
                    if (std::memcmp(subTag, "VRTX", 4) == 0) {
                        sPos += 8;
                        if (cnt == 0 || cnt > 2000000 || static_cast<uint64_t>(cnt) * 12ULL > nextGeoset - sPos) return false;
                        vrtx.resize(cnt);
                        for (uint32_t i = 0; i < cnt; ++i) {
                            float x = 0.0f, y = 0.0f, z = 0.0f;
                            std::memcpy(&x, data + sPos + static_cast<size_t>(i) * 12 + 0, 4);
                            std::memcpy(&y, data + sPos + static_cast<size_t>(i) * 12 + 4, 4);
                            std::memcpy(&z, data + sPos + static_cast<size_t>(i) * 12 + 8, 4);
                            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
                            vrtx[i] = BlizzDetail::BlizzZUpToViewport(x, y, z);
                        }
                        sPos += static_cast<size_t>(cnt) * 12;
                    } else if (std::memcmp(subTag, "NRMS", 4) == 0) {
                        sPos += 8;
                        if (static_cast<uint64_t>(cnt) * 12ULL > nextGeoset - sPos) return false;
                        sPos += static_cast<size_t>(cnt) * 12;
                    } else if (std::memcmp(subTag, "PTYP", 4) == 0 || std::memcmp(subTag, "PCNT", 4) == 0) {
                        sPos += 8;
                        if (static_cast<uint64_t>(cnt) * 4ULL > nextGeoset - sPos) return false;
                        sPos += static_cast<size_t>(cnt) * 4;
                    } else if (std::memcmp(subTag, "PVTX", 4) == 0) {
                        sPos += 8;
                        if (cnt < 3 || (cnt % 3) != 0 || static_cast<uint64_t>(cnt) * 2ULL > nextGeoset - sPos) return false;
                        pvtx.resize(cnt);
                        std::memcpy(pvtx.data(), data + sPos, static_cast<size_t>(cnt) * 2);
                        break; // Remaining Geoset fields (GNDX/MTGC/MATS/UVBS) skipped via nextGeoset
                    } else {
                        break;
                    }
                }

                if (!vrtx.empty() && pvtx.size() >= 3) {
                    uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                    for (const auto& v : vrtx) mesh.vertices.push_back(v);
                    for (size_t t = 0; t + 2 < pvtx.size(); t += 3) {
                        uint16_t a = pvtx[t], b = pvtx[t + 1], c = pvtx[t + 2];
                        if (a >= vrtx.size() || b >= vrtx.size() || c >= vrtx.size()) return false;
                        if (a == b || b == c || c == a) continue;
                        mesh.tris.push_back({ baseV + a, baseV + c, baseV + b, {} });
                    }
                }

                gPos = nextGeoset;
            }
        }
        pos += chunkSize;
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Warcraft III Binary Model (.mdx)";
    mesh.animFrameCount = seqCount;
    return true;
}

// Warcraft III ASCII Model (.mdl — "Version { FormatVersion 800, }" + "Geoset { Vertices ... Faces ... }")
inline bool ParseWarcraftAsciiMdl(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.find("FormatVersion") == std::string_view::npos || sv.find("Geoset") == std::string_view::npos) {
        return false;
    }

    struct Wc3Lex {
        std::string_view s;
        size_t p = 0;
        void Skip() {
            while (p < s.size()) {
                char c = s[p];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') { ++p; continue; }
                if (c == '/' && p + 1 < s.size() && s[p + 1] == '/') {
                    p += 2;
                    while (p < s.size() && s[p] != '\n') ++p;
                    continue;
                }
                break;
            }
        }
        bool Next(std::string_view& tok) {
            Skip();
            if (p >= s.size()) return false;
            char c = s[p];
            if (c == '"') {
                size_t st = ++p;
                while (p < s.size() && s[p] != '"') ++p;
                tok = s.substr(st, p - st);
                if (p < s.size()) ++p;
                return true;
            }
            if (c == '{' || c == '}') {
                tok = s.substr(p++, 1);
                return true;
            }
            size_t st = p;
            while (p < s.size()) {
                char ch = s[p];
                if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == ',' || ch == '{' || ch == '}' || ch == '"') break;
                if (ch == '/' && p + 1 < s.size() && s[p + 1] == '/') break;
                ++p;
            }
            tok = s.substr(st, p - st);
            return !tok.empty();
        }
        bool NextInt(int& v) {
            std::string_view t;
            if (!Next(t) || t.size() >= 64) return false;
            char b[64];
            std::memcpy(b, t.data(), t.size());
            b[t.size()] = '\0';
            char* ep = nullptr;
            long lv = std::strtol(b, &ep, 10);
            if (ep != b + t.size()) return false;
            v = static_cast<int>(lv);
            return true;
        }
        bool NextFloat(float& v) {
            std::string_view t;
            if (!Next(t) || t.size() >= 64) return false;
            char b[64];
            std::memcpy(b, t.data(), t.size());
            b[t.size()] = '\0';
            char* ep = nullptr;
            float fv = std::strtof(b, &ep);
            if (ep != b + t.size() || !std::isfinite(fv)) return false;
            v = fv;
            return true;
        }
        bool SkipBlock() {
            int d = 1;
            std::string_view t;
            while (d > 0 && Next(t)) {
                if (t == "{") ++d;
                else if (t == "}") --d;
            }
            return d == 0;
        }
    };

    Wc3Lex lex{ sv, 0 };
    std::string_view tok;
    int fmtVer = 0;
    size_t seqCount = 1;

    while (lex.Next(tok)) {
        if (tok == "Version") {
            if (!lex.Next(tok) || tok != "{") return false;
            while (lex.Next(tok) && tok != "}") {
                if (tok == "FormatVersion") lex.NextInt(fmtVer);
            }
        } else if (tok == "Sequences") {
            int nSeq = 0;
            if (lex.NextInt(nSeq) && nSeq > 0) seqCount = static_cast<size_t>(nSeq);
            if (lex.Next(tok) && tok == "{") lex.SkipBlock();
        } else if (tok == "Geoset") {
            if (!lex.Next(tok) || tok != "{") return false;
            std::vector<Vec3f> verts;
            std::vector<int> indices;
            int gDepth = 1;
            while (gDepth > 0 && lex.Next(tok)) {
                if (tok == "{") { ++gDepth; continue; }
                if (tok == "}") { --gDepth; continue; }
                if (gDepth == 1 && tok == "Vertices") {
                    int nV = 0;
                    if (!lex.NextInt(nV) || nV <= 0 || nV > 1000000) return false;
                    if (!lex.Next(tok) || tok != "{") return false;
                    verts.reserve(static_cast<size_t>(nV));
                    for (int i = 0; i < nV; ++i) {
                        float x = 0.0f, y = 0.0f, z = 0.0f;
                        if (!lex.Next(tok) || tok != "{" ||
                            !lex.NextFloat(x) || !lex.NextFloat(y) || !lex.NextFloat(z) ||
                            !lex.Next(tok) || tok != "}") {
                            return false;
                        }
                        verts.push_back(BlizzDetail::BlizzZUpToViewport(x, y, z));
                    }
                    if (!lex.Next(tok) || tok != "}") return false;
                } else if (gDepth == 1 && tok == "Faces") {
                    int nGroups = 0, nIdx = 0;
                    if (!lex.NextInt(nGroups) || !lex.NextInt(nIdx) || nIdx < 3) return false;
                    if (!lex.Next(tok) || tok != "{") return false;
                    int fDepth = 1;
                    while (fDepth > 0 && lex.Next(tok)) {
                        if (tok == "{") ++fDepth;
                        else if (tok == "}") --fDepth;
                        else if (tok != "Triangles") {
                            char b[64];
                            if (tok.size() < sizeof(b)) {
                                std::memcpy(b, tok.data(), tok.size());
                                b[tok.size()] = '\0';
                                char* ep = nullptr;
                                long iv = std::strtol(b, &ep, 10);
                                if (ep == b + tok.size()) indices.push_back(static_cast<int>(iv));
                            }
                        }
                    }
                } else if (gDepth == 1) {
                    // Skip Normals, TVertices, VertexGroup, Faces-unrelated blocks
                    std::string_view nx;
                    while (lex.Next(nx)) {
                        if (nx == "{") { lex.SkipBlock(); break; }
                        if (nx == "}") { --gDepth; break; }
                    }
                }
            }

            if (!verts.empty() && indices.size() >= 3) {
                uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                for (const auto& v : verts) mesh.vertices.push_back(v);
                for (size_t t = 0; t + 2 < indices.size(); t += 3) {
                    int a = indices[t], b = indices[t + 1], c = indices[t + 2];
                    if (a < 0 || a >= static_cast<int>(verts.size()) ||
                        b < 0 || b >= static_cast<int>(verts.size()) ||
                        c < 0 || c >= static_cast<int>(verts.size())) {
                        return false;
                    }
                    if (a == b || b == c || c == a) continue;
                    mesh.tris.push_back({
                        baseV + static_cast<uint32_t>(a),
                        baseV + static_cast<uint32_t>(c),
                        baseV + static_cast<uint32_t>(b),
                        {}
                    });
                }
            }
        }
    }

    if (fmtVer < 700 || mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Warcraft III ASCII Model (.mdl)";
    mesh.animFrameCount = seqCount;
    return true;
}

// World of Warcraft Model (.m2 — "MD20" / chunked "MD21" + external .skin or embedded Vanilla/TBC View)
inline bool ParseWowM2(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    if (!data || size < 80) return false;
    const uint8_t* md20 = nullptr;
    size_t md20Size = 0;

    if (std::memcmp(data, "MD20", 4) == 0) {
        md20 = data;
        md20Size = size;
    } else if (std::memcmp(data, "MD21", 4) == 0) {
        size_t pos = 0;
        while (pos + 8 <= size) {
            char tag[4];
            uint32_t cSz = 0;
            std::memcpy(tag, data + pos, 4);
            std::memcpy(&cSz, data + pos + 4, 4);
            pos += 8;
            if (cSz > size - pos) return false;
            if (std::memcmp(tag, "MD21", 4) == 0 && cSz >= 80 && std::memcmp(data + pos, "MD20", 4) == 0) {
                md20 = data + pos;
                md20Size = cSz;
                break;
            }
            pos += cSz;
        }
    }
    if (!md20 || md20Size < 80) return false;

    uint32_t version = 0;
    std::memcpy(&version, md20 + 4, 4);
    if (version < 256 || version > 300) return false;

    uint32_t nSeqs = 0;
    std::memcpy(&nSeqs, md20 + 0x1C, 4);

    // Vertex M2Array offset: 0x44 in Vanilla (256..260), 0x3C in TBC..Modern (261+)
    const size_t vertArrOff = (version <= 260) ? 0x44 : 0x3C;
    const size_t viewFieldOff = vertArrOff + 8;
    if (viewFieldOff + 8 > md20Size) return false;

    uint32_t nVerts = 0, ofsVerts = 0;
    std::memcpy(&nVerts, md20 + vertArrOff, 4);
    std::memcpy(&ofsVerts, md20 + vertArrOff + 4, 4);
    if (nVerts == 0 || nVerts > 4000000) return false;
    if (static_cast<uint64_t>(ofsVerts) + static_cast<uint64_t>(nVerts) * 48ULL > md20Size) return false;

    mesh.vertices.resize(nVerts);
    for (uint32_t i = 0; i < nVerts; ++i) {
        const uint8_t* vp = md20 + ofsVerts + static_cast<size_t>(i) * 48;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        std::memcpy(&x, vp + 0, 4);
        std::memcpy(&y, vp + 4, 4);
        std::memcpy(&z, vp + 8, 4);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
        mesh.vertices[i] = BlizzDetail::BlizzZUpToViewport(x, y, z);
    }

    auto decodeSkinTables = [&](const uint8_t* base, size_t totalSz, size_t hdrArraysOff) -> bool {
        if (hdrArraysOff + 16 > totalSz) return false;
        uint32_t nLookup = 0, ofsLookup = 0, nTrisIdx = 0, ofsTrisIdx = 0;
        std::memcpy(&nLookup, base + hdrArraysOff + 0, 4);
        std::memcpy(&ofsLookup, base + hdrArraysOff + 4, 4);
        std::memcpy(&nTrisIdx, base + hdrArraysOff + 8, 4);
        std::memcpy(&ofsTrisIdx, base + hdrArraysOff + 12, 4);
        if (nLookup == 0 || nTrisIdx < 3 || (nTrisIdx % 3) != 0) return false;
        if (static_cast<uint64_t>(ofsLookup) + static_cast<uint64_t>(nLookup) * 2ULL > totalSz) return false;
        if (static_cast<uint64_t>(ofsTrisIdx) + static_cast<uint64_t>(nTrisIdx) * 2ULL > totalSz) return false;

        for (uint32_t t = 0; t + 2 < nTrisIdx; t += 3) {
            uint16_t la = 0, lb = 0, lc = 0;
            std::memcpy(&la, base + ofsTrisIdx + static_cast<size_t>(t + 0) * 2, 2);
            std::memcpy(&lb, base + ofsTrisIdx + static_cast<size_t>(t + 1) * 2, 2);
            std::memcpy(&lc, base + ofsTrisIdx + static_cast<size_t>(t + 2) * 2, 2);
            if (la >= nLookup || lb >= nLookup || lc >= nLookup) return false;
            uint16_t ga = 0, gb = 0, gc = 0;
            std::memcpy(&ga, base + ofsLookup + static_cast<size_t>(la) * 2, 2);
            std::memcpy(&gb, base + ofsLookup + static_cast<size_t>(lb) * 2, 2);
            std::memcpy(&gc, base + ofsLookup + static_cast<size_t>(lc) * 2, 2);
            if (ga >= nVerts || gb >= nVerts || gc >= nVerts) return false;
            if (ga == gb || gb == gc || gc == ga) continue;
            mesh.tris.push_back({ ga, gc, gb, {} });
        }
        return !mesh.tris.empty();
    };

    bool gotSkin = false;
    // 1. Try companion <stem>00.skin / <stem>.skin file on disk (WotLK v264+ standard)
    if (!fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
        const std::wstring candidates[3] = {
            dir + L"\\" + stem + L"00.skin",
            dir + L"\\" + stem + L"01.skin",
            dir + L"\\" + stem + L".skin"
        };
        for (const auto& sp : candidates) {
            std::vector<uint8_t> skinBytes;
            if (PathUtils::ReadFileBytes(sp, 32 * 1024 * 1024, skinBytes) &&
                skinBytes.size() >= 24 && std::memcmp(skinBytes.data(), "SKIN", 4) == 0)
            {
                if (decodeSkinTables(skinBytes.data(), skinBytes.size(), 4)) {
                    gotSkin = true;
                    break;
                }
            }
        }
    }

    // 2. Try embedded Views M2Array (Vanilla / TBC v256..263)
    if (!gotSkin && version < 264) {
        uint32_t nViews = 0, ofsViews = 0;
        std::memcpy(&nViews, md20 + viewFieldOff, 4);
        std::memcpy(&ofsViews, md20 + viewFieldOff + 4, 4);
        if (nViews > 0 && static_cast<uint64_t>(ofsViews) + 32ULL <= md20Size) {
            size_t subOff = ofsViews;
            if (ofsViews + 20 <= md20Size && std::memcmp(md20 + ofsViews, "SKIN", 4) == 0) subOff += 4;
            gotSkin = decodeSkinTables(md20, md20Size, subOff);
        }
    }

    if (!gotSkin || mesh.tris.empty()) return false;
    mesh.formatName = (std::memcmp(data, "MD21", 4) == 0)
        ? L"World of Warcraft Model MD21 (.m2)"
        : L"World of Warcraft Model MD20 (.m2)";
    mesh.animFrameCount = std::max<size_t>(1, nSeqs);
    return true;
}

// World of Warcraft World Map Object (.wmo — "REVM" Group or Root + sibling _000.wmo groups)
inline bool ParseWowWmo(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    if (!data || size < 20 || std::memcmp(data, "REVM", 4) != 0) return false;

    // 1. Direct WMO Group file ("PGOM" / MOGP present)
    if (BlizzDetail::ParseWmoGroupBytes(data, size, mesh)) {
        mesh.formatName = L"World of Warcraft Map Object (.wmo)";
        return true;
    }

    // 2. WMO Root file ("DHOM" / MOHD present) -> load sibling <stem>_000.wmo, _001.wmo, ...
    size_t pos = 0;
    uint32_t nGroups = 0;
    bool isRoot = false;
    while (pos + 8 <= size) {
        char tag[4];
        uint32_t cSize = 0;
        std::memcpy(tag, data + pos, 4);
        std::memcpy(&cSize, data + pos + 4, 4);
        pos += 8;
        if (cSize > size - pos) break;
        if (std::memcmp(tag, "DHOM", 4) == 0 && cSize >= 8) {
            std::memcpy(&nGroups, data + pos + 4, 4);
            isRoot = true;
            break;
        }
        pos += cSize;
    }

    if (isRoot && nGroups > 0 && !fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
        uint32_t maxProbe = std::min<uint32_t>(nGroups, 64);
        for (uint32_t g = 0; g < maxProbe; ++g) {
            wchar_t suffix[16];
            swprintf_s(suffix, L"_%03u.wmo", g);
            std::vector<uint8_t> gBytes;
            if (PathUtils::ReadFileBytes(dir + L"\\" + stem + suffix, 64 * 1024 * 1024, gBytes) && !gBytes.empty()) {
                BlizzDetail::ParseWmoGroupBytes(gBytes.data(), gBytes.size(), mesh);
            }
        }
        if (!mesh.vertices.empty() && !mesh.tris.empty()) {
            mesh.formatName = L"World of Warcraft Root Map Object (.wmo)";
            return true;
        }
    }
    return false;
}

} // namespace Formats
