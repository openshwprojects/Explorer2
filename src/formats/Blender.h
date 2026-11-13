#pragma once
    // 8. Blender .blend Parser (BLENDER-v / BLENDER_v or Gzipped .blend)
    if (ext == L".blend") {
        std::vector<uint8_t> decompBlend;
        const uint8_t* bData = buf.data();
        size_t bSize = buf.size();
        if (bSize >= 18 && bData[0] == 0x1F && bData[1] == 0x8B) {
            if (ZipDeflate::InflateGzip(bData, bSize, decompBlend, 128 * 1024 * 1024)) {
                bData = decompBlend.data();
                bSize = decompBlend.size();
            }
        }
        if (bSize >= 12 && std::memcmp(bData, "BLENDER", 7) == 0) {
            bool is64Bit = (bData[7] == '_');
            bool isLittleEndian = (bData[8] == 'v');
            char verStr[4] = { static_cast<char>(bData[9]), static_cast<char>(bData[10]), static_cast<char>(bData[11]), 0 };
            mesh.formatName = L"Blender 3D (.blend v" + PathUtils::Utf8ToWide(verStr) + L")";

            if (isLittleEndian) {
                size_t bheadSize = is64Bit ? 24 : 20;
                size_t pos = 12;
                bool inMeshBlock = false;
                while (pos + bheadSize <= bSize) {
                    const uint8_t* bh = bData + pos;
                    if (std::memcmp(bh, "ENDB", 4) == 0) break;
                    int32_t dataLen = 0;
                    int32_t nr = 0;
                    std::memcpy(&dataLen, bh + 4, 4);
                    std::memcpy(&nr, bh + (is64Bit ? 20 : 16), 4);
                    if (dataLen < 0 || pos + bheadSize + static_cast<size_t>(dataLen) > bSize) break;
                    const uint8_t* payload = bh + bheadSize;

                    if (bh[0] == 'M' && bh[1] == 'E') {
                        inMeshBlock = true;
                    } else if (bh[0] != 'D' || bh[1] != 'A' || bh[2] != 'T' || bh[3] != 'A') {
                        inMeshBlock = false;
                    } else if (inMeshBlock && nr >= 3 && dataLen >= nr * 12) {
                        int elemBytes = dataLen / nr;
                        if (elemBytes == 12 || elemBytes == 16 || elemBytes == 20) {
                            bool allReasonableFloats = true;
                            int checkCount = std::min(nr, 32);
                            float minC = 1e9f, maxC = -1e9f;
                            for (int i = 0; i < checkCount; ++i) {
                                float xyz[3] = {};
                                std::memcpy(xyz, payload + i * elemBytes, 12);
                                for (int k = 0; k < 3; ++k) {
                                    if (!std::isfinite(xyz[k]) || std::abs(xyz[k]) > 1e6f) {
                                        allReasonableFloats = false;
                                        break;
                                    }
                                    minC = std::min(minC, xyz[k]);
                                    maxC = std::max(maxC, xyz[k]);
                                }
                                if (!allReasonableFloats) break;
                            }
                            if (allReasonableFloats && (maxC - minC) > 1e-4f) {
                                uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                                for (int i = 0; i < nr; ++i) {
                                    float xyz[3] = {};
                                    std::memcpy(xyz, payload + i * elemBytes, 12);
                                    mesh.vertices.push_back({ xyz[0], xyz[2], -xyz[1] });
                                }
                                for (int i = 0; i + 2 < nr; i += 3) {
                                    mesh.tris.push_back({ baseV + static_cast<uint32_t>(i),
                                                          baseV + static_cast<uint32_t>(i + 1),
                                                          baseV + static_cast<uint32_t>(i + 2), {} });
                                }
                            }
                        }
                    }
                    pos += bheadSize + static_cast<size_t>(dataLen);
                }
                FinalizeMeshBoundsAndNormals(mesh);
                if (mesh.loadedOk) return mesh;
            }
        }
    }

