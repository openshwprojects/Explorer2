#pragma once
// DirectXMeshX.h — DirectX Legacy Mesh (.x — text & binary) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// ============================================================================
// #41. DirectX Legacy Mesh (.x — "xof 0303txt" / "xof 0302txt" / "xof ...bin ")
// ============================================================================
inline bool ParseDirectXMeshX(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 16 || std::memcmp(data, "xof ", 4) != 0) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    std::string_view fmtTag = sv.substr(8, 4);
    bool isDouble = (sv.substr(12, 4) == "0064");

    if (fmtTag == "txt ") {
        mesh.formatName = L"DirectX 3D Mesh (.x)";
        auto isIdentChar = [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        };
        size_t mpos = sv.find("Mesh", 16);
        while (mpos != std::string_view::npos) {
            bool wordBefore = (mpos > 0 && isIdentChar(sv[mpos - 1]));
            bool wordAfter  = (mpos + 4 < sv.size() && isIdentChar(sv[mpos + 4]));
            if (wordBefore || wordAfter) {
                mpos = sv.find("Mesh", mpos + 4);
                continue;
            }
            // Check not preceded by "template "
            size_t lineStart = sv.rfind('\n', mpos);
            lineStart = (lineStart == std::string_view::npos) ? 16 : (lineStart + 1);
            if (sv.substr(lineStart, mpos - lineStart).find("template") != std::string_view::npos) {
                mpos = sv.find("Mesh", mpos + 4);
                continue;
            }
            size_t brace = sv.find('{', mpos + 4);
            if (brace == std::string_view::npos) break;

            const char* p = sv.data() + brace + 1;
            const char* end = sv.data() + sv.size();
            auto skipSpaceComments = [&]() {
                while (p < end) {
                    if (*p == '/' && p + 1 < end && p[1] == '/') { while (p < end && *p != '\n') ++p; }
                    else if (*p == '#') { while (p < end && *p != '\n') ++p; }
                    else if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ';' || *p == ',') ++p;
                    else break;
                }
            };
            auto nextInt = [&]() -> int {
                skipSpaceComments();
                if (p >= end || ((*p < '0' || *p > '9') && *p != '-')) return -1;
                char* ep = nullptr;
                long v = std::strtol(p, &ep, 10);
                if (ep == p) return -1;
                p = ep;
                return static_cast<int>(v);
            };
            auto nextFloat = [&]() -> float {
                skipSpaceComments();
                if (p >= end) return 0.0f;
                char* ep = nullptr;
                float v = std::strtof(p, &ep);
                if (ep == p) return 0.0f;
                p = ep;
                return std::isfinite(v) ? v : 0.0f;
            };

            int nVerts = nextInt();
            if (nVerts > 0 && nVerts <= 500000) {
                uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                for (int i = 0; i < nVerts && p < end; ++i) {
                    float x = nextFloat(), y = nextFloat(), z = nextFloat();
                    mesh.vertices.push_back({ x, y, z });
                }
                int nFaces = nextInt();
                for (int f = 0; f < nFaces && f < 500000 && p < end; ++f) {
                    int cnt = nextInt();
                    if (cnt < 3 || cnt > 64) break;
                    std::vector<uint32_t> idx(static_cast<size_t>(cnt));
                    bool okPoly = true;
                    for (int k = 0; k < cnt; ++k) {
                        int vi = nextInt();
                        if (vi < 0 || vi >= nVerts) { okPoly = false; break; }
                        idx[static_cast<size_t>(k)] = vBase + static_cast<uint32_t>(vi);
                    }
                    if (!okPoly) break;
                    for (int k = 1; k + 1 < cnt; ++k) {
                        mesh.tris.push_back({ idx[0], idx[static_cast<size_t>(k)], idx[static_cast<size_t>(k + 1)], {} });
                    }
                }
            }
            mpos = sv.find("Mesh", static_cast<size_t>(p - sv.data()));
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    if (fmtTag == "bin ") {
        mesh.formatName = L"DirectX Binary Mesh (.x)";
        size_t pos = 16;
        int braceDepth = 0;
        int meshDepth = -1;
        bool pendingMesh = false;
        bool inTemplate = false;
        std::vector<int32_t> meshInts;
        std::vector<float> meshFloats;

        auto flushBinaryMesh = [&]() {
            if (meshInts.size() < 5 || meshFloats.size() < 9) {
                meshInts.clear(); meshFloats.clear();
                return;
            }
            int32_t nVerts = meshInts[0];
            if (nVerts > 0 && nVerts <= 500000 && meshFloats.size() >= static_cast<size_t>(nVerts) * 3) {
                uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                for (int32_t i = 0; i < nVerts; ++i) {
                    mesh.vertices.push_back({
                        meshFloats[static_cast<size_t>(i) * 3 + 0],
                        meshFloats[static_cast<size_t>(i) * 3 + 1],
                        meshFloats[static_cast<size_t>(i) * 3 + 2]
                    });
                }
                size_t ip = 1;
                if (ip < meshInts.size()) {
                    int32_t nFaces = meshInts[ip++];
                    for (int32_t f = 0; f < nFaces && f < 500000 && ip < meshInts.size(); ++f) {
                        int32_t cnt = meshInts[ip++];
                        if (cnt < 3 || cnt > 64 || ip + static_cast<size_t>(cnt) > meshInts.size()) break;
                        bool valid = true;
                        for (int32_t k = 0; k < cnt; ++k) {
                            if (meshInts[ip + static_cast<size_t>(k)] < 0 || meshInts[ip + static_cast<size_t>(k)] >= nVerts) {
                                valid = false; break;
                            }
                        }
                        if (valid) {
                            uint32_t i0 = vBase + static_cast<uint32_t>(meshInts[ip]);
                            for (int32_t k = 1; k + 1 < cnt; ++k) {
                                mesh.tris.push_back({
                                    i0,
                                    vBase + static_cast<uint32_t>(meshInts[ip + static_cast<size_t>(k)]),
                                    vBase + static_cast<uint32_t>(meshInts[ip + static_cast<size_t>(k + 1)]), {}
                                });
                            }
                        }
                        ip += static_cast<size_t>(cnt);
                    }
                }
            }
            meshInts.clear();
            meshFloats.clear();
        };

        while (pos + 2 <= size) {
            uint16_t tok = DetailMeshSciCad::RdU16LE(data + pos);
            pos += 2;
            if (tok == 1) { // TOKEN_NAME
                if (pos + 4 > size) break;
                uint32_t len = DetailMeshSciCad::RdU32LE(data + pos); pos += 4;
                if (pos + len > size) break;
                std::string_view nm(reinterpret_cast<const char*>(data + pos), len);
                pos += len;
                if (!inTemplate && meshDepth < 0 && nm == "Mesh") pendingMesh = true;
            } else if (tok == 2) { // TOKEN_STRING
                if (pos + 4 > size) break;
                uint32_t len = DetailMeshSciCad::RdU32LE(data + pos); pos += 4;
                if (pos + len + 2 > size) break;
                pos += len + 2; // string + terminator token
            } else if (tok == 3) { // TOKEN_INTEGER
                if (pos + 4 > size) break;
                int32_t v = static_cast<int32_t>(DetailMeshSciCad::RdU32LE(data + pos)); pos += 4;
                if (meshDepth >= 0 && braceDepth == meshDepth) meshInts.push_back(v);
            } else if (tok == 5) { // TOKEN_GUID
                if (pos + 16 > size) break;
                pos += 16;
            } else if (tok == 6) { // TOKEN_INTEGER_LIST
                if (pos + 4 > size) break;
                uint32_t cnt = DetailMeshSciCad::RdU32LE(data + pos); pos += 4;
                if (cnt > 4000000 || pos + static_cast<size_t>(cnt) * 4 > size) break;
                if (meshDepth >= 0 && braceDepth == meshDepth) {
                    for (uint32_t i = 0; i < cnt; ++i) {
                        meshInts.push_back(static_cast<int32_t>(DetailMeshSciCad::RdU32LE(data + pos + static_cast<size_t>(i) * 4)));
                    }
                }
                pos += static_cast<size_t>(cnt) * 4;
            } else if (tok == 7) { // TOKEN_FLOAT_LIST
                if (pos + 4 > size) break;
                uint32_t cnt = DetailMeshSciCad::RdU32LE(data + pos); pos += 4;
                size_t elemSz = isDouble ? 8 : 4;
                if (cnt > 4000000 || pos + static_cast<size_t>(cnt) * elemSz > size) break;
                if (meshDepth >= 0 && braceDepth == meshDepth) {
                    for (uint32_t i = 0; i < cnt; ++i) {
                        float fv = isDouble
                            ? static_cast<float>(DetailMeshSciCad::RdF64LE(data + pos + static_cast<size_t>(i) * 8))
                            : DetailMeshSciCad::RdF32LE(data + pos + static_cast<size_t>(i) * 4);
                        meshFloats.push_back(fv);
                    }
                }
                pos += static_cast<size_t>(cnt) * elemSz;
            } else if (tok == 10) { // TOKEN_OBRACE
                ++braceDepth;
                if (pendingMesh && !inTemplate) {
                    meshDepth = braceDepth;
                    pendingMesh = false;
                    meshInts.clear();
                    meshFloats.clear();
                }
            } else if (tok == 11) { // TOKEN_CBRACE
                if (meshDepth >= 0 && braceDepth == meshDepth) {
                    flushBinaryMesh();
                    meshDepth = -1;
                }
                --braceDepth;
                if (braceDepth <= 0) { braceDepth = 0; inTemplate = false; }
                pendingMesh = false;
            } else if (tok == 31) { // TOKEN_TEMPLATE
                inTemplate = true;
                pendingMesh = false;
            } else if ((tok >= 12 && tok <= 20) || (tok >= 40 && tok <= 52)) {
                // Zero-payload punctuation / template primitive keyword tokens
            } else {
                break;
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    return false;
}

} // namespace Formats
