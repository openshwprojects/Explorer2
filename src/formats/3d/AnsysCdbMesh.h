#pragma once
// AnsysCdbMesh.h — ANSYS Blocked Finite Element Archive (.cdb) 3D Mesh Reader
// Parses NBLOCK fixed-width FORTRAN (e.g. (3i8,6e20.13) / (3i9,6e21.13)) & free-format N,id,x,y,z node blocks,
// plus EBLOCK 19-int SOLID & compact 11-int / E / EN element connectivity (tet, pyramid, wedge, hex, quad, tri),
// extracting the exterior 3D boundary surface.
#include "../../ReaderTextUtils.h"
#include "../common/FemSolidSurfaceCommon.h"

namespace Formats {

namespace DetailAnsysCdb {

// Parses FORTRAN format descriptor "(3i8,6e20.13)" -> numIntCols=3, intWidth=8, floatWidth=20
inline bool ParseFortranNblockFmt(std::string_view ln, int& outIntCount, int& outIntWidth, int& outFloatWidth) {
    ln = ReaderText::TrimSv(ln);
    if (ln.size() < 6 || ln.front() != '(') return false;
    std::string up = ReaderText::ToUpperAscii(ln.substr(1));
    size_t iPos = up.find('I');
    if (iPos == std::string::npos || iPos == 0) return false;
    int ic = 0, iw = 0, fw = 0;
    if (!ReaderText::ParseIntSv(std::string_view(up).substr(0, iPos), ic) || ic < 1 || ic > 8) return false;
    size_t comma = up.find(',', iPos + 1);
    if (comma == std::string::npos) return false;
    if (!ReaderText::ParseIntSv(std::string_view(up).substr(iPos + 1, comma - (iPos + 1)), iw) || iw < 1 || iw > 32) return false;

    size_t ePos = up.find_first_of("EFGD", comma + 1);
    if (ePos == std::string::npos) return false;
    size_t dot = up.find_first_of(".)", ePos + 1);
    if (dot == std::string::npos) return false;
    if (!ReaderText::ParseIntSv(std::string_view(up).substr(ePos + 1, dot - (ePos + 1)), fw) || fw < 4 || fw > 40) return false;

    outIntCount = ic;
    outIntWidth = iw;
    outFloatWidth = fw;
    return true;
}

// Parses FORTRAN format descriptor "(19i9)" / "(19i10)" -> intCount, intWidth
inline bool ParseFortranEblockFmt(std::string_view ln, int& outIntCount, int& outIntWidth) {
    ln = ReaderText::TrimSv(ln);
    if (ln.size() < 4 || ln.front() != '(') return false;
    std::string up = ReaderText::ToUpperAscii(ln.substr(1));
    size_t iPos = up.find('I');
    if (iPos == std::string::npos || iPos == 0) return false;
    size_t endP = up.find_first_of(",)", iPos + 1);
    if (endP == std::string::npos) return false;
    int ic = 0, iw = 0;
    if (!ReaderText::ParseIntSv(std::string_view(up).substr(0, iPos), ic) || ic < 1 || ic > 64) return false;
    if (!ReaderText::ParseIntSv(std::string_view(up).substr(iPos + 1, endP - (iPos + 1)), iw) || iw < 1 || iw > 24) return false;
    outIntCount = ic;
    outIntWidth = iw;
    return true;
}

inline bool ParseAnsysFloat(std::string_view sv, float& out) {
    sv = ReaderText::TrimSv(sv);
    if (sv.empty() || sv.size() >= 64) return false;
    char buf[68];
    size_t j = 0;
    for (size_t i = 0; i < sv.size() && j + 2 < sizeof(buf); ++i) {
        char c = sv[i];
        if (c == 'D' || c == 'd') c = 'E';
        // Handle FORTRAN exponentless scientific notation like "1.2345-03"
        if ((c == '+' || c == '-') && i > 0 && sv[i - 1] != 'e' && sv[i - 1] != 'E' &&
            sv[i - 1] != 'd' && sv[i - 1] != 'D' && sv[i - 1] != ' ')
        {
            buf[j++] = 'E';
        }
        buf[j++] = c;
    }
    buf[j] = '\0';
    char* endp = nullptr;
    double d = std::strtod(buf, &endp);
    if (endp == buf || !std::isfinite(d)) return false;
    out = static_cast<float>(d);
    return true;
}

inline void ParseFixedWidthInts(std::string_view ln, int width, std::vector<int64_t>& out) {
    out.clear();
    if (width > 0) {
        for (size_t off = 0; off < ln.size(); off += static_cast<size_t>(width)) {
            size_t len = std::min<size_t>(static_cast<size_t>(width), ln.size() - off);
            std::string_view sl = ReaderText::TrimSv(ln.substr(off, len));
            if (sl.empty()) continue;
            int64_t v = 0;
            if (ReaderText::ParseInt64Sv(sl, v)) out.push_back(v);
            else { out.clear(); break; }
        }
    }
    if (out.empty()) {
        std::string_view rem = ln, tok;
        while (ReaderText::NextTokenSv(rem, tok)) {
            int64_t v = 0;
            if (ReaderText::ParseInt64Sv(tok, v)) out.push_back(v);
        }
    }
}

} // namespace DetailAnsysCdb

inline bool ParseAnsysCdbMesh(
    const uint8_t* data,
    size_t size,
    const std::wstring& /*fullPath*/,
    LoadedMesh3D& mesh)
{
    using namespace DetailAnsysCdb;
    if (!data || size < 16) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.find('\0') != std::string_view::npos) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    std::unordered_map<int64_t, uint32_t> nodeMap;
    std::vector<Vec3f> nodes;
    FemSolidSurface::FemSurfaceBuilder builder;
    bool sawAnsysCmd = false;

    auto addNode = [&](int64_t id, float x, float y, float z) {
        if (id <= 0) return;
        auto [it, inserted] = nodeMap.try_emplace(id, static_cast<uint32_t>(nodes.size()));
        if (inserted) nodes.push_back({ x, y, z });
        else nodes[it->second] = { x, y, z };
    };

    auto mapCornerNodes = [&](const int64_t* rawIds, size_t count, uint32_t* outIdx) -> size_t {
        size_t valid = 0;
        for (size_t i = 0; i < count; ++i) {
            if (rawIds[i] <= 0) break;
            auto it = nodeMap.find(rawIds[i]);
            if (it == nodeMap.end()) return 0;
            outIdx[valid++] = it->second;
        }
        return valid;
    };

    auto addAnsysElementNodes = [&](const int64_t* rawIds, size_t nNodes) {
        if (nNodes == 0) return;
        uint32_t idx[8] = {};
        size_t m = mapCornerNodes(rawIds, std::min<size_t>(nNodes, 8u), idx);
        if (m >= 8) {
            builder.AddCollapsedHex8(idx);
        } else if (m == 6) {
            builder.AddWedge6(idx);
        } else if (m == 5) {
            builder.AddPyramid5(idx);
        } else if (m == 4) {
            // Distinguish 4-node tetrahedron vs 4-node shell quad by non-coplanarity check!
            // Wait: in ANSYS EBLOCK 19-int SOLID format, 4-node tet has numNodes==4 (or 10 for SOLID187)
            // whereas a 4-node SHELL181 quad has all 4 nodes nearly coplanar (volume ~ 0) or n[2]==n[3] for tri!
            if (idx[2] == idx[3]) {
                builder.AddTriShell(idx[0], idx[1], idx[2]);
            } else {
                const Vec3f& p0 = nodes[idx[0]];
                const Vec3f& p1 = nodes[idx[1]];
                const Vec3f& p2 = nodes[idx[2]];
                const Vec3f& p3 = nodes[idx[3]];
                Vec3f e1 = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
                Vec3f e2 = { p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
                Vec3f e3 = { p3.x - p0.x, p3.y - p0.y, p3.z - p0.z };
                Vec3f nrm = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
                float area2 = std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y + nrm.z * nrm.z);
                float edgeLen = std::sqrt(e1.x * e1.x + e1.y * e1.y + e1.z * e1.z) + 1e-6f;
                float vol6 = std::fabs(nrm.x * e3.x + nrm.y * e3.y + nrm.z * e3.z);
                if (area2 > 1e-8f && vol6 > area2 * edgeLen * 0.02f) {
                    builder.AddTet4(idx);
                } else {
                    builder.AddQuadShell(idx[0], idx[1], idx[2], idx[3]);
                }
            }
        } else if (m == 3) {
            builder.AddTriShell(idx[0], idx[1], idx[2]);
        } else if (m == 2) {
            builder.AddBeamEdge(idx[0], idx[1]);
        }
    };

    size_t pos = 0;
    std::string_view rawLine;
    std::vector<int64_t> ints;

    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        std::string_view ln = ReaderText::TrimSv(rawLine);
        if (ln.empty() || ln.front() == '!' || ln.front() == '/') continue;

        std::string headUp = ReaderText::ToUpperAscii(ln.substr(0, std::min<size_t>(ln.size(), 16u)));

        if (headUp.starts_with("NBLOCK")) {
            sawAnsysCmd = true;
            int intCnt = 3, intW = 0, fltW = 0;
            while (ReaderText::NextLineSv(sv, pos, rawLine)) {
                std::string_view nln = ReaderText::TrimSv(rawLine);
                if (nln.empty() || nln.front() == '!') continue;
                if (nln.front() == '(') {
                    ParseFortranNblockFmt(nln, intCnt, intW, fltW);
                    continue;
                }
                std::string nUp = ReaderText::ToUpperAscii(nln.substr(0, std::min<size_t>(nln.size(), 12u)));
                if (nln.starts_with("-1") || nUp.starts_with("N,") || nUp.starts_with("EBLOCK") || nUp == "FINISH") {
                    break;
                }

                // Parse node ID + X, Y, Z either via FORTRAN fixed widths or whitespace tokens
                bool parsedFixed = false;
                if (intW > 0 && fltW > 0 && rawLine.size() >= static_cast<size_t>(intW)) {
                    int64_t nid = 0;
                    if (ReaderText::ParseInt64Sv(ReaderText::TrimSv(rawLine.substr(0, static_cast<size_t>(intW))), nid) && nid > 0) {
                        size_t coordStart = static_cast<size_t>(intCnt * intW);
                        float coords[3] = { 0.0f, 0.0f, 0.0f };
                        if (coordStart < rawLine.size()) {
                            for (int c = 0; c < 3; ++c) {
                                size_t cs = coordStart + static_cast<size_t>(c * fltW);
                                if (cs >= rawLine.size()) break;
                                size_t cl = std::min<size_t>(static_cast<size_t>(fltW), rawLine.size() - cs);
                                ParseAnsysFloat(rawLine.substr(cs, cl), coords[c]);
                            }
                            addNode(nid, coords[0], coords[1], coords[2]);
                            parsedFixed = true;
                        }
                    }
                }
                if (!parsedFixed) {
                    std::string_view rem = nln, tok;
                    std::vector<std::string_view> toks;
                    while (ReaderText::NextTokenSv(rem, tok)) toks.push_back(tok);
                    if (toks.size() >= 2) {
                        int64_t nid = 0;
                        if (ReaderText::ParseInt64Sv(toks[0], nid) && nid > 0) {
                            // If intCnt==3 (id, solid, line) and >= 4 tokens, coords start at index 3 (or 1 if 4 tokens total)
                            size_t cBase = (toks.size() >= 6) ? 3u : 1u;
                            float x = 0, y = 0, z = 0;
                            if (cBase < toks.size()) ParseAnsysFloat(toks[cBase], x);
                            if (cBase + 1 < toks.size()) ParseAnsysFloat(toks[cBase + 1], y);
                            if (cBase + 2 < toks.size()) ParseAnsysFloat(toks[cBase + 2], z);
                            addNode(nid, x, y, z);
                        }
                    }
                }
            }
            continue;
        }

        if (headUp.starts_with("EBLOCK")) {
            sawAnsysCmd = true;
            const bool isSolidFmt = (ReaderText::ToUpperAscii(ln).find("SOLID") != std::string::npos);
            int intCnt = isSolidFmt ? 19 : 11;
            int intW = 0;

            while (ReaderText::NextLineSv(sv, pos, rawLine)) {
                std::string_view eln = ReaderText::TrimSv(rawLine);
                if (eln.empty() || eln.front() == '!') continue;
                if (eln.front() == '(') {
                    ParseFortranEblockFmt(eln, intCnt, intW);
                    continue;
                }
                if (eln.starts_with("-1") || ReaderText::ToUpperAscii(eln).starts_with("FINISH")) {
                    break;
                }

                ParseFixedWidthInts(rawLine, intW, ints);
                if (ints.empty() || ints[0] < 0) break;

                if (isSolidFmt || ints.size() >= 15) {
                    // ANSYS 19-int SOLID format:
                    // [0:mat, 1:type, 2:real, 3:sec, 4:esys, 5:birth, 6:solidref, 7:shape, 8:numNodes, 9:exclude, 10:elemId, 11..18:n1..n8]
                    if (ints.size() >= 13) {
                        int64_t numElemNodes = ints[8];
                        size_t firstLineNodes = std::min<size_t>(ints.size() - 11u, 8u);
                        std::vector<int64_t> eNodes(ints.begin() + 11, ints.begin() + 11 + static_cast<ptrdiff_t>(firstLineNodes));
                        if (numElemNodes > 8 && static_cast<size_t>(numElemNodes) <= 32) {
                            // Consume continuation line(s) for mid-side nodes (SOLID186 20-node, SOLID187 10-node, etc.)
                            size_t got = firstLineNodes;
                            while (got < static_cast<size_t>(numElemNodes) && ReaderText::NextLineSv(sv, pos, rawLine)) {
                                std::vector<int64_t> cont;
                                ParseFixedWidthInts(rawLine, intW, cont);
                                if (cont.empty()) break;
                                got += cont.size();
                            }
                        }
                        size_t cornerCnt = (numElemNodes == 10) ? 4u
                                         : (numElemNodes == 15 || numElemNodes == 6) ? std::min<size_t>(eNodes.size(), 8u)
                                         : eNodes.size();
                        if (numElemNodes == 10 && eNodes.size() >= 4 &&
                            !(eNodes.size() >= 8 && eNodes[2] == eNodes[3])) {
                            addAnsysElementNodes(eNodes.data(), 4);
                        } else {
                            addAnsysElementNodes(eNodes.data(), cornerCnt);
                        }
                    }
                } else if (ints.size() >= 3) {
                    // Compact EBLOCK format: [elemId, n1, n2, ...] or [typeId, realId, matId, elemId, n1..n8]
                    if (ints.size() >= 9 && ints[0] > 0 && ints.size() <= 10) {
                        addAnsysElementNodes(ints.data() + 1, ints.size() - 1u);
                    } else if (ints.size() >= 11) {
                        addAnsysElementNodes(ints.data() + (ints.size() - 8u), 8u);
                    } else {
                        addAnsysElementNodes(ints.data() + 1, ints.size() - 1u);
                    }
                }
            }
            continue;
        }

        // Free-format ANSYS APDL commands: "N, nodeId, x, y, z" and "E, i, j, k, l, ..." / "EN, elemId, i, j, k, l, ..."
        if (headUp.starts_with("N,") || headUp.starts_with("N ")) {
            std::vector<std::string_view> parts;
            size_t p0 = 0;
            while (p0 <= ln.size()) {
                size_t cm = ln.find(',', p0);
                parts.push_back(ReaderText::TrimSv(cm == std::string_view::npos ? ln.substr(p0) : ln.substr(p0, cm - p0)));
                if (cm == std::string_view::npos) break;
                p0 = cm + 1;
            }
            if (parts.size() >= 3 && ReaderText::ToUpperAscii(parts[0]) == "N") {
                int64_t nid = 0;
                float x = 0, y = 0, z = 0;
                if (ReaderText::ParseInt64Sv(parts[1], nid) && nid > 0) {
                    sawAnsysCmd = true;
                    if (parts.size() >= 3 && !parts[2].empty()) ParseAnsysFloat(parts[2], x);
                    if (parts.size() >= 4 && !parts[3].empty()) ParseAnsysFloat(parts[3], y);
                    if (parts.size() >= 5 && !parts[4].empty()) ParseAnsysFloat(parts[4], z);
                    addNode(nid, x, y, z);
                }
            }
        } else if (headUp.starts_with("E,") || headUp.starts_with("EN,")) {
            std::vector<std::string_view> parts;
            size_t p0 = 0;
            while (p0 <= ln.size()) {
                size_t cm = ln.find(',', p0);
                parts.push_back(ReaderText::TrimSv(cm == std::string_view::npos ? ln.substr(p0) : ln.substr(p0, cm - p0)));
                if (cm == std::string_view::npos) break;
                p0 = cm + 1;
            }
            std::string cmd = ReaderText::ToUpperAscii(parts[0]);
            size_t startIdx = (cmd == "EN") ? 2u : 1u;
            if ((cmd == "E" || cmd == "EN") && parts.size() > startIdx + 2u) {
                std::vector<int64_t> en;
                for (size_t k = startIdx; k < parts.size(); ++k) {
                    int64_t v = 0;
                    if (ReaderText::ParseInt64Sv(parts[k], v) && v > 0) en.push_back(v);
                }
                if (en.size() >= 2) {
                    sawAnsysCmd = true;
                    addAnsysElementNodes(en.data(), en.size());
                }
            }
        }
    }

    if (!sawAnsysCmd || !builder.EmitBoundaryToMesh(nodes, mesh)) return false;
    mesh.formatName = L"ANSYS Finite Element Archive (.cdb)";
    return true;
}

} // namespace Formats
