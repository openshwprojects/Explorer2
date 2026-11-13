#pragma once
    // 6. Quake III MD3 Parser (.md3 - magic "IDP3" 0x33504449, version 15)
    if (ext == L".md3" || (buf.size() >= 108 && std::memcmp(buf.data(), "IDP3", 4) == 0)) {
        if (buf.size() >= 108 && std::memcmp(buf.data(), "IDP3", 4) == 0) {
            int32_t version = 0, numFrames = 0, numSurfaces = 0, ofsSurfaces = 0;
            std::memcpy(&version, buf.data() + 4, 4);
            std::memcpy(&numFrames, buf.data() + 72, 4);
            std::memcpy(&numSurfaces, buf.data() + 80, 4);
            std::memcpy(&ofsSurfaces, buf.data() + 96, 4);
            if (numSurfaces > 0 && ofsSurfaces >= 108 && static_cast<size_t>(ofsSurfaces) < buf.size()) {
                mesh.formatName = L"Quake III Model (.md3)";
                mesh.animFrameCount = static_cast<size_t>(std::max(1, numFrames));
                size_t surfOff = static_cast<size_t>(ofsSurfaces);
                for (int s = 0; s < numSurfaces && surfOff + 108 <= buf.size(); ++s) {
                    const uint8_t* sp = buf.data() + surfOff;
                    if (std::memcmp(sp, "IDP3", 4) != 0) break;
                    int32_t sVerts = 0, sTris = 0, ofsTris = 0, ofsXyz = 0, ofsEnd = 0;
                    std::memcpy(&sVerts, sp + 76, 4);
                    std::memcpy(&sTris,  sp + 80, 4);
                    std::memcpy(&ofsTris, sp + 84, 4);
                    std::memcpy(&ofsXyz,  sp + 100, 4);
                    std::memcpy(&ofsEnd,  sp + 104, 4);
                    if (sVerts > 0 && sTris > 0 &&
                        ofsXyz > 0 && surfOff + ofsXyz + static_cast<size_t>(sVerts) * 8 <= buf.size() &&
                        ofsTris > 0 && surfOff + ofsTris + static_cast<size_t>(sTris) * 12 <= buf.size())
                    {
                        uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                        const uint8_t* vp = sp + ofsXyz;
                        for (int v = 0; v < sVerts; ++v) {
                            int16_t xyz[4] = {};
                            std::memcpy(xyz, vp + v * 8, 8);
                            mesh.vertices.push_back({ xyz[0] / 64.0f, xyz[2] / 64.0f, -xyz[1] / 64.0f });
                        }
                        const uint8_t* tp = sp + ofsTris;
                        for (int t = 0; t < sTris; ++t) {
                            int32_t idx[3] = {};
                            std::memcpy(idx, tp + t * 12, 12);
                            if (idx[0] >= 0 && idx[0] < sVerts && idx[1] >= 0 && idx[1] < sVerts && idx[2] >= 0 && idx[2] < sVerts) {
                                mesh.tris.push_back({ baseV + static_cast<uint32_t>(idx[0]),
                                                      baseV + static_cast<uint32_t>(idx[2]),
                                                      baseV + static_cast<uint32_t>(idx[1]), {} });
                            }
                        }
                    }
                    if (ofsEnd <= 0) break;
                    surfOff += static_cast<size_t>(ofsEnd);
                }
                FinalizeMeshBoundsAndNormals(mesh);
                if (mesh.loadedOk) return mesh;
            }
        }
    }

