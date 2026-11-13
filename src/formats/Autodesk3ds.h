#pragma once
    // 7. 3D Studio Mesh Parser (.3ds - Main Chunk 0x4D4D)
    if (ext == L".3ds" && buf.size() >= 16) {
        uint16_t mainId = 0;
        std::memcpy(&mainId, buf.data(), 2);
        if (mainId == 0x4D4D) {
            mesh.formatName = L"Autodesk 3D Studio (.3ds)";
            uint32_t curBaseVert = 0;
            size_t pos = 6;
            while (pos + 6 <= buf.size()) {
                uint16_t cid = 0;
                uint32_t clen = 0;
                std::memcpy(&cid, buf.data() + pos, 2);
                std::memcpy(&clen, buf.data() + pos + 2, 4);
                if (clen < 6 || pos + clen > buf.size()) break;

                if (cid == 0x3D3D || cid == 0x4100) {
                    pos += 6;
                } else if (cid == 0x4000) {
                    size_t p = pos + 6;
                    while (p < pos + clen && buf[p] != 0) ++p;
                    if (p < pos + clen) ++p;
                    pos = p;
                } else if (cid == 0x4110 && pos + 8 <= buf.size()) {
                    uint16_t nVerts = 0;
                    std::memcpy(&nVerts, buf.data() + pos + 6, 2);
                    curBaseVert = static_cast<uint32_t>(mesh.vertices.size());
                    size_t vp = pos + 8;
                    for (uint16_t v = 0; v < nVerts && vp + 12 <= pos + clen; ++v) {
                        float xyz[3] = {};
                        std::memcpy(xyz, buf.data() + vp, 12);
                        mesh.vertices.push_back({ xyz[0], xyz[2], -xyz[1] });
                        vp += 12;
                    }
                    pos += clen;
                } else if (cid == 0x4120 && pos + 8 <= buf.size()) {
                    uint16_t nFaces = 0;
                    std::memcpy(&nFaces, buf.data() + pos + 6, 2);
                    size_t fp = pos + 8;
                    for (uint16_t f = 0; f < nFaces && fp + 8 <= pos + clen; ++f) {
                        uint16_t fidx[4] = {};
                        std::memcpy(fidx, buf.data() + fp, 8);
                        uint32_t i0 = curBaseVert + fidx[0];
                        uint32_t i1 = curBaseVert + fidx[1];
                        uint32_t i2 = curBaseVert + fidx[2];
                        if (i0 < mesh.vertices.size() && i1 < mesh.vertices.size() && i2 < mesh.vertices.size()) {
                            mesh.tris.push_back({ i0, i1, i2, {} });
                        }
                        fp += 8;
                    }
                    pos += clen;
                } else {
                    pos += clen;
                }
            }
            FinalizeMeshBoundsAndNormals(mesh);
            if (mesh.loadedOk) return mesh;
        }
    }

