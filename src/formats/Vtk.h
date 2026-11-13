#pragma once
    // 11b. Visualization Toolkit Legacy 3D Mesh (.vtk - ASCII & BINARY POINTS + POLYGONS / CELLS / TRIANGLE_STRIPS)
    if (ext == L".vtk") {
        mesh.formatName = L"Visualization Toolkit 3D (.vtk)";
        std::string_view sv(reinterpret_cast<const char*>(buf.data()), buf.size());
        bool isBinary = (sv.substr(0, std::min<size_t>(512, sv.size())).find("\nBINARY") != std::string_view::npos ||
                         sv.substr(0, std::min<size_t>(512, sv.size())).find("\r\nBINARY") != std::string_view::npos);
        size_t ptsPos = sv.find("POINTS ");
        if (ptsPos != std::string_view::npos) {
            size_t eol = sv.find('\n', ptsPos);
            if (eol != std::string_view::npos) {
                std::string hdr(sv.substr(ptsPos + 7, eol - (ptsPos + 7)));
                char* typeStart = nullptr;
                size_t numPts = static_cast<size_t>(std::strtoul(hdr.c_str(), &typeStart, 10));
                bool isDoublePts = (hdr.find("double") != std::string::npos || hdr.find("DOUBLE") != std::string::npos);
                mesh.vertices.reserve(std::min<size_t>(numPts, 250000));
                const char* cur = sv.data() + eol + 1;
                const char* end = sv.data() + sv.size();

                auto readBEFloat32 = [](const uint8_t* p) -> float {
                    uint32_t u = (static_cast<uint32_t>(p[0]) << 24) |
                                 (static_cast<uint32_t>(p[1]) << 16) |
                                 (static_cast<uint32_t>(p[2]) << 8)  |
                                  static_cast<uint32_t>(p[3]);
                    float f = 0.0f;
                    std::memcpy(&f, &u, 4);
                    return f;
                };
                auto readBEFloat64 = [](const uint8_t* p) -> float {
                    uint64_t u = (static_cast<uint64_t>(p[0]) << 56) |
                                 (static_cast<uint64_t>(p[1]) << 48) |
                                 (static_cast<uint64_t>(p[2]) << 40) |
                                 (static_cast<uint64_t>(p[3]) << 32) |
                                 (static_cast<uint64_t>(p[4]) << 24) |
                                 (static_cast<uint64_t>(p[5]) << 16) |
                                 (static_cast<uint64_t>(p[6]) << 8)  |
                                  static_cast<uint64_t>(p[7]);
                    double d = 0.0;
                    std::memcpy(&d, &u, 8);
                    return static_cast<float>(d);
                };
                auto readBEUInt32 = [](const uint8_t* p) -> uint32_t {
                    return (static_cast<uint32_t>(p[0]) << 24) |
                           (static_cast<uint32_t>(p[1]) << 16) |
                           (static_cast<uint32_t>(p[2]) << 8)  |
                            static_cast<uint32_t>(p[3]);
                };

                if (isBinary) {
                    size_t stride = isDoublePts ? 24 : 12;
                    for (size_t i = 0; i < numPts && (cur + stride) <= end; ++i) {
                        const uint8_t* bp = reinterpret_cast<const uint8_t*>(cur);
                        float x = isDoublePts ? readBEFloat64(bp)      : readBEFloat32(bp);
                        float y = isDoublePts ? readBEFloat64(bp + 8)  : readBEFloat32(bp + 4);
                        float z = isDoublePts ? readBEFloat64(bp + 16) : readBEFloat32(bp + 8);
                        if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                            mesh.vertices.push_back({ x, y, z });
                        } else {
                            mesh.vertices.push_back({ 0.0f, 0.0f, 0.0f });
                        }
                        cur += stride;
                    }
                } else {
                    for (size_t i = 0; i < numPts && cur < end; ++i) {
                        char* next = nullptr;
                        float x = std::strtof(cur, &next);
                        if (next == cur) break;
                        cur = next;
                        float y = std::strtof(cur, &next); cur = next;
                        float z = std::strtof(cur, &next); cur = next;
                        mesh.vertices.push_back({ x, y, z });
                    }
                }

                // Parse POLYGONS, CELLS, or TRIANGLE_STRIPS
                size_t searchFrom = static_cast<size_t>(cur - sv.data());
                bool isTriStrip = false;
                size_t polyPos = sv.find("POLYGONS ", searchFrom);
                if (polyPos == std::string_view::npos) {
                    polyPos = sv.find("TRIANGLE_STRIPS ", searchFrom);
                    if (polyPos != std::string_view::npos) isTriStrip = true;
                }
                if (polyPos == std::string_view::npos) {
                    polyPos = sv.find("CELLS ", searchFrom);
                }
                if (polyPos != std::string_view::npos) {
                    size_t pEol = sv.find('\n', polyPos);
                    if (pEol != std::string_view::npos) {
                        size_t spacePos = sv.find(' ', polyPos);
                        size_t numPolys = (spacePos != std::string_view::npos && spacePos < pEol)
                            ? static_cast<size_t>(std::strtoul(std::string(sv.substr(spacePos + 1, pEol - (spacePos + 1))).c_str(), nullptr, 10))
                            : 0;
                        const char* pCur = sv.data() + pEol + 1;
                        std::vector<uint32_t> faceIdx;
                        for (size_t p = 0; p < numPolys && pCur < end; ++p) {
                            uint32_t nVerts = 0;
                            if (isBinary) {
                                if (pCur + 4 > end) break;
                                nVerts = readBEUInt32(reinterpret_cast<const uint8_t*>(pCur));
                                pCur += 4;
                                if (nVerts == 0 || nVerts > 65536 || pCur + static_cast<size_t>(nVerts) * 4 > end) break;
                                faceIdx.clear();
                                faceIdx.reserve(nVerts);
                                for (uint32_t k = 0; k < nVerts; ++k) {
                                    faceIdx.push_back(readBEUInt32(reinterpret_cast<const uint8_t*>(pCur)));
                                    pCur += 4;
                                }
                            } else {
                                char* next = nullptr;
                                unsigned long nv = std::strtoul(pCur, &next, 10);
                                if (next == pCur || nv == 0 || nv > 65536) break;
                                nVerts = static_cast<uint32_t>(nv);
                                pCur = next;
                                faceIdx.clear();
                                faceIdx.reserve(nVerts);
                                for (uint32_t k = 0; k < nVerts && pCur < end; ++k) {
                                    unsigned long idx = std::strtoul(pCur, &next, 10);
                                    if (next == pCur) break;
                                    pCur = next;
                                    faceIdx.push_back(static_cast<uint32_t>(idx));
                                }
                            }
                            if (isTriStrip) {
                                for (size_t k = 0; k + 2 < faceIdx.size(); ++k) {
                                    uint32_t i0 = (k & 1) ? faceIdx[k + 1] : faceIdx[k];
                                    uint32_t i1 = (k & 1) ? faceIdx[k]     : faceIdx[k + 1];
                                    uint32_t i2 = faceIdx[k + 2];
                                    if (i0 < mesh.vertices.size() && i1 < mesh.vertices.size() && i2 < mesh.vertices.size()) {
                                        mesh.tris.push_back({ i0, i1, i2, {} });
                                    }
                                }
                            } else {
                                for (size_t k = 1; k + 1 < faceIdx.size(); ++k) {
                                    uint32_t i0 = faceIdx[0], i1 = faceIdx[k], i2 = faceIdx[k + 1];
                                    if (i0 < mesh.vertices.size() && i1 < mesh.vertices.size() && i2 < mesh.vertices.size()) {
                                        mesh.tris.push_back({ i0, i1, i2, {} });
                                    }
                                }
                            }
                        }
                    }
                }
                if (mesh.tris.empty() && mesh.vertices.size() >= 3) {
                    for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
                        mesh.tris.push_back({ static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2), {} });
                    }
                }
                FinalizeMeshBoundsAndNormals(mesh);
                if (mesh.loadedOk) return mesh;
            }
        }
    }
