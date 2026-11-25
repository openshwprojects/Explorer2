#pragma once
// AnsysFluentCasMesh.h — ANSYS Fluent Case / Mesh (.cas) S-Expression 3D Reader
// Parses Fluent XF sections: (0 "comment"), (2 ND) dimension, (10 / 2010 / 3010 (zoneId firstIdx lastIdx type ND)(...))
// 1-based hexadecimal node blocks, and (13 / 2013 / 3013 (zoneId firstIdx lastIdx bcType elemType)(...))
// face connectivity (mixed 0, linear 2, triangular 3, quadrilateral 4, polygonal 5), extracting exterior & baffle surfaces.
#include "../../ByteIO.h"
#include "../../ReaderTextUtils.h"
#include "../common/MeshBuild3D.h"

namespace Formats {

namespace DetailFluentCas {

inline void SkipFluentWsAndComments(const char*& cur, const char* end) {
    while (cur < end) {
        char c = *cur;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++cur;
        } else if (c == '"') {
            // Skip quoted string (only valid inside comments/headers)
            ++cur;
            while (cur < end && *cur != '"') {
                if (*cur == '\\' && cur + 1 < end) ++cur;
                ++cur;
            }
            if (cur < end) ++cur;
        } else {
            break;
        }
    }
}

// Skips balanced parenthesized S-expression starting just AFTER an opening '(' (depth = 1)
inline void SkipBalancedParenBody(const char*& cur, const char* end) {
    int depth = 1;
    while (cur < end && depth > 0) {
        char c = *cur++;
        if (c == '"') {
            while (cur < end && *cur != '"') {
                if (*cur == '\\' && cur + 1 < end) ++cur;
                ++cur;
            }
            if (cur < end) ++cur;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
        }
    }
}

inline bool NextFluentAtom(const char*& cur, const char* end, std::string_view& out) {
    while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
    if (cur >= end || *cur == '(' || *cur == ')') return false;
    const char* s = cur;
    while (cur < end && *cur != ' ' && *cur != '\t' && *cur != '\r' && *cur != '\n' && *cur != '(' && *cur != ')') {
        ++cur;
    }
    out = std::string_view(s, static_cast<size_t>(cur - s));
    return !out.empty();
}

inline bool ParseHexU64(std::string_view sv, uint64_t& out) {
    if (sv.empty() || sv.size() > 16) return false;
    if (sv.size() > 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X')) sv.remove_prefix(2);
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out, 16);
    return ec == std::errc{} && ptr == sv.data() + sv.size();
}

inline bool ParseDecInt(std::string_view sv, int& out) {
    return ReaderText::ParseIntSv(sv, out);
}

inline bool ParseFluentDouble(const char*& cur, const char* end, double& out) {
    std::string_view tok;
    if (!NextFluentAtom(cur, end, tok) || tok.size() >= 64) return false;
    char buf[64];
    std::memcpy(buf, tok.data(), tok.size());
    buf[tok.size()] = '\0';
    char* next = nullptr;
    out = std::strtod(buf, &next);
    return next != buf && std::isfinite(out);
}

struct FluentFacePoly {
    std::vector<uint32_t> verts; // 0-based indices into nodes
    bool isBoundary = true;
};

} // namespace DetailFluentCas

inline bool ParseAnsysFluentCasMesh(
    const uint8_t* data,
    size_t size,
    const std::wstring& /*fullPath*/,
    LoadedMesh3D& mesh)
{
    using namespace DetailFluentCas;
    if (!data || size < 16) return false;
    mesh.vertices.clear();
    mesh.tris.clear();

    const char* cur = reinterpret_cast<const char*>(data);
    const char* const end = cur + size;
    if (size >= 3 && static_cast<uint8_t>(cur[0]) == 0xEF &&
        static_cast<uint8_t>(cur[1]) == 0xBB && static_cast<uint8_t>(cur[2]) == 0xBF)
    {
        cur += 3;
    }

    int globalDim = 3;
    bool sawFluentSection = false;
    std::vector<Vec3f> nodes;
    std::vector<FluentFacePoly> faces;
    std::vector<std::pair<uint32_t, uint32_t>> edges2D;

    while (cur < end) {
        // Advance to next top-level '('
        while (cur < end && *cur != '(') {
            if (*cur == '"') {
                ++cur;
                while (cur < end && *cur != '"') {
                    if (*cur == '\\' && cur + 1 < end) ++cur;
                    ++cur;
                }
                if (cur < end) ++cur;
            } else {
                ++cur;
            }
        }
        if (cur >= end) break;
        ++cur; // consume '('

        std::string_view secAtom;
        if (!NextFluentAtom(cur, end, secAtom)) {
            SkipBalancedParenBody(cur, end);
            continue;
        }
        int secId = -1;
        if (!ParseDecInt(secAtom, secId) || secId < 0) {
            SkipBalancedParenBody(cur, end);
            continue;
        }

        if (secId == 0) { // Comment section (0 "...")
            sawFluentSection = true;
            SkipBalancedParenBody(cur, end);
            continue;
        }
        if (secId == 2) { // Dimension section (2 2) or (2 3)
            std::string_view dimTok;
            int nd = 3;
            if (NextFluentAtom(cur, end, dimTok) && ParseDecInt(dimTok, nd) && (nd == 2 || nd == 3)) {
                globalDim = nd;
                sawFluentSection = true;
            }
            SkipBalancedParenBody(cur, end);
            continue;
        }

        // Node sections: 10 (ASCII), 2010 (Binary float32LE), 3010 (Binary float64LE)
        if (secId == 10 || secId == 2010 || secId == 3010) {
            sawFluentSection = true;
            while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
            if (cur >= end || *cur != '(') { SkipBalancedParenBody(cur, end); continue; }
            ++cur; // consume '(' of header tuple

            std::vector<uint64_t> hdr;
            std::string_view htok;
            while (NextFluentAtom(cur, end, htok)) {
                uint64_t hv = 0;
                if (ParseHexU64(htok, hv)) hdr.push_back(hv);
            }
            while (cur < end && *cur != ')') ++cur;
            if (cur < end) ++cur; // consume ')' of header tuple

            if (hdr.size() < 3 || hdr[0] == 0) {
                // Zone 0 is declaration only: (10 (0 1 totalNodes 0 [ND]))
                if (hdr.size() >= 3 && hdr[0] == 0 && hdr[2] > 0 && hdr[2] <= 20000000ULL) {
                    nodes.reserve(static_cast<size_t>(hdr[2]));
                }
                SkipBalancedParenBody(cur, end);
                continue;
            }

            uint64_t firstIdx = hdr[1];
            uint64_t lastIdx  = hdr[2];
            int nd = (hdr.size() >= 5 && (hdr[4] == 2 || hdr[4] == 3)) ? static_cast<int>(hdr[4]) : globalDim;
            if (firstIdx == 0 || lastIdx < firstIdx || lastIdx > 20000000ULL) {
                SkipBalancedParenBody(cur, end);
                continue;
            }
            if (nodes.size() < static_cast<size_t>(lastIdx)) {
                nodes.resize(static_cast<size_t>(lastIdx), { 0.0f, 0.0f, 0.0f });
            }
            const size_t count = static_cast<size_t>(lastIdx - firstIdx + 1ULL);

            // Body may be inside current section "(10 (...) ( ... ))" OR immediately follow "(10 (...))(\n...\n)"
            while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
            bool bodyOutside = false;
            if (cur < end && *cur == ')') {
                ++cur; // close outer ')'
                while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
                bodyOutside = true;
            }
            if (cur >= end || *cur != '(') {
                if (!bodyOutside) SkipBalancedParenBody(cur, end);
                continue;
            }
            ++cur; // consume '(' of data body

            if (secId == 10) {
                for (size_t i = 0; i < count; ++i) {
                    double x = 0.0, y = 0.0, z = 0.0;
                    if (!ParseFluentDouble(cur, end, x) || !ParseFluentDouble(cur, end, y)) break;
                    if (nd == 3 && !ParseFluentDouble(cur, end, z)) break;
                    nodes[static_cast<size_t>(firstIdx - 1ULL) + i] = {
                        static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)
                    };
                }
                SkipBalancedParenBody(cur, end);
            } else {
                const size_t elemSz = (secId == 3010) ? 8u : 4u;
                const uint64_t byteLen = static_cast<uint64_t>(count) * static_cast<uint64_t>(nd) * elemSz;
                if (byteLen <= static_cast<uint64_t>(end - cur)) {
                    const uint8_t* bp = reinterpret_cast<const uint8_t*>(cur);
                    for (size_t i = 0; i < count; ++i) {
                        double c[3] = { 0.0, 0.0, 0.0 };
                        for (int k = 0; k < nd; ++k) {
                            const uint8_t* p = bp + (i * static_cast<size_t>(nd) + static_cast<size_t>(k)) * elemSz;
                            c[k] = (elemSz == 8u) ? ByteIO::RdF64LE(p) : static_cast<double>(ByteIO::RdF32LE(p));
                        }
                        nodes[static_cast<size_t>(firstIdx - 1ULL) + i] = {
                            static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])
                        };
                    }
                    cur += static_cast<size_t>(byteLen);
                }
                SkipBalancedParenBody(cur, end);
            }
            if (!bodyOutside) SkipBalancedParenBody(cur, end);
            continue;
        }

        // Face sections: 13 (ASCII), 2013 / 3013 (Binary int32LE)
        if (secId == 13 || secId == 2013 || secId == 3013) {
            sawFluentSection = true;
            while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
            if (cur >= end || *cur != '(') { SkipBalancedParenBody(cur, end); continue; }
            ++cur;

            std::vector<uint64_t> hdr;
            std::string_view htok;
            while (NextFluentAtom(cur, end, htok)) {
                uint64_t hv = 0;
                if (ParseHexU64(htok, hv)) hdr.push_back(hv);
            }
            while (cur < end && *cur != ')') ++cur;
            if (cur < end) ++cur;

            if (hdr.size() < 4 || hdr[0] == 0) {
                SkipBalancedParenBody(cur, end);
                continue;
            }

            uint64_t firstIdx = hdr[1];
            uint64_t lastIdx  = hdr[2];
            uint64_t bcType   = hdr[3];
            uint64_t elemType = (hdr.size() >= 5) ? hdr[4] : 0ULL;
            if (firstIdx == 0 || lastIdx < firstIdx || lastIdx - firstIdx > 20000000ULL) {
                SkipBalancedParenBody(cur, end);
                continue;
            }
            const size_t fCount = static_cast<size_t>(lastIdx - firstIdx + 1ULL);

            while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
            bool bodyOutside = false;
            if (cur < end && *cur == ')') {
                ++cur;
                while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
                bodyOutside = true;
            }
            if (cur >= end || *cur != '(') {
                if (!bodyOutside) SkipBalancedParenBody(cur, end);
                continue;
            }
            ++cur;

            auto recordFace = [&](const uint64_t* vIds, size_t nv, uint64_t c0, uint64_t c1) {
                if (nv == 2) {
                    if (vIds[0] >= 1 && vIds[1] >= 1) {
                        edges2D.emplace_back(static_cast<uint32_t>(vIds[0] - 1), static_cast<uint32_t>(vIds[1] - 1));
                    }
                    return;
                }
                if (nv < 3 || nv > 64) return;
                FluentFacePoly fp;
                fp.verts.reserve(nv);
                for (size_t k = 0; k < nv; ++k) {
                    if (vIds[k] == 0) return;
                    fp.verts.push_back(static_cast<uint32_t>(vIds[k] - 1ULL));
                }
                // bcType == 2 is Fluent's "interior" face zone; boundary zones have c0==0 || c1==0 || bcType != 2
                fp.isBoundary = (bcType != 2ULL) || (c0 == 0ULL) || (c1 == 0ULL);
                faces.push_back(std::move(fp));
            };

            if (secId == 13) {
                std::vector<uint64_t> vBuf;
                for (size_t f = 0; f < fCount; ++f) {
                    size_t nv = 0;
                    if (elemType == 2 || elemType == 3 || elemType == 4) {
                        nv = static_cast<size_t>(elemType);
                    } else if (elemType == 0 || elemType == 5) {
                        std::string_view ntok;
                        uint64_t n64 = 0;
                        if (!NextFluentAtom(cur, end, ntok) || !ParseHexU64(ntok, n64) || n64 < 2 || n64 > 64) break;
                        nv = static_cast<size_t>(n64);
                    } else {
                        break;
                    }
                    vBuf.resize(nv);
                    bool okF = true;
                    for (size_t k = 0; k < nv; ++k) {
                        std::string_view vt;
                        if (!NextFluentAtom(cur, end, vt) || !ParseHexU64(vt, vBuf[k])) { okF = false; break; }
                    }
                    std::string_view c0t, c1t;
                    uint64_t c0 = 0, c1 = 0;
                    if (!okF || !NextFluentAtom(cur, end, c0t) || !ParseHexU64(c0t, c0) ||
                        !NextFluentAtom(cur, end, c1t) || !ParseHexU64(c1t, c1))
                    {
                        break;
                    }
                    recordFace(vBuf.data(), nv, c0, c1);
                }
                SkipBalancedParenBody(cur, end);
            } else {
                const uint8_t* bp = reinterpret_cast<const uint8_t*>(cur);
                size_t remBytes = static_cast<size_t>(end - cur);
                size_t bOff = 0;
                std::vector<uint64_t> vBuf;
                for (size_t f = 0; f < fCount; ++f) {
                    size_t nv = 0;
                    if (elemType == 2 || elemType == 3 || elemType == 4) {
                        nv = static_cast<size_t>(elemType);
                    } else if (elemType == 0 || elemType == 5) {
                        if (bOff + 4 > remBytes) break;
                        int32_t n32 = ByteIO::RdI32LE(bp + bOff);
                        bOff += 4;
                        if (n32 < 2 || n32 > 64) break;
                        nv = static_cast<size_t>(n32);
                    } else {
                        break;
                    }
                    if (bOff + (nv + 2u) * 4u > remBytes) break;
                    vBuf.resize(nv);
                    for (size_t k = 0; k < nv; ++k) {
                        int32_t vi = ByteIO::RdI32LE(bp + bOff + k * 4u);
                        vBuf[k] = (vi > 0) ? static_cast<uint64_t>(vi) : 0ULL;
                    }
                    int32_t c0 = ByteIO::RdI32LE(bp + bOff + nv * 4u);
                    int32_t c1 = ByteIO::RdI32LE(bp + bOff + (nv + 1u) * 4u);
                    bOff += (nv + 2u) * 4u;
                    recordFace(vBuf.data(), nv, c0 > 0 ? static_cast<uint64_t>(c0) : 0ULL, c1 > 0 ? static_cast<uint64_t>(c1) : 0ULL);
                }
                cur += bOff;
                SkipBalancedParenBody(cur, end);
            }
            if (!bodyOutside) SkipBalancedParenBody(cur, end);
            continue;
        }

        SkipBalancedParenBody(cur, end);
    }

    if (!sawFluentSection || nodes.empty()) return false;

    bool hasBoundary = false;
    for (const auto& f : faces) {
        if (f.isBoundary) { hasBoundary = true; break; }
    }

    std::vector<uint32_t> remap(nodes.size(), 0xFFFFFFFFu);
    auto mapNode = [&](uint32_t idx) -> uint32_t {
        if (remap[idx] == 0xFFFFFFFFu) {
            remap[idx] = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(nodes[idx]);
        }
        return remap[idx];
    };

    for (const auto& f : faces) {
        if (hasBoundary && !f.isBoundary) continue;
        bool valid = true;
        for (uint32_t vi : f.verts) {
            if (vi >= nodes.size()) { valid = false; break; }
        }
        if (!valid || f.verts.size() < 3) continue;
        uint32_t i0 = mapNode(f.verts[0]);
        for (size_t k = 1; k + 1 < f.verts.size(); ++k) {
            uint32_t i1 = mapNode(f.verts[k]);
            uint32_t i2 = mapNode(f.verts[k + 1]);
            if (i0 != i1 && i1 != i2 && i0 != i2) {
                mesh.tris.push_back({ i0, i1, i2, {} });
            }
        }
    }

    if (mesh.tris.empty() && !edges2D.empty()) {
        Vec3f lo = nodes[0], hi = nodes[0];
        for (const auto& p : nodes) {
            lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
            hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
        }
        float span = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-3f });
        float rad = span * 0.006f;
        for (const auto& e : edges2D) {
            if (e.first < nodes.size() && e.second < nodes.size()) {
                MeshBuild3D::EmitEdgeStrut(mesh, nodes[e.first], nodes[e.second], rad);
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"ANSYS Fluent Case / CFD Mesh (.cas)";
    return true;
}

} // namespace Formats
