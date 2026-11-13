#pragma once
    // 5. Quake II MD2 Parser (.md2 - magic "IDP2" 0x32504449, version 8)
    if (ext == L".md2" || (buf.size() >= 68 && std::memcmp(buf.data(), "IDP2", 4) == 0)) {
        if (buf.size() >= 68 && std::memcmp(buf.data(), "IDP2", 4) == 0) {
            struct Md2Header {
                int32_t ident, version, skinwidth, skinheight, framesize;
                int32_t num_skins, num_xyz, num_st, num_tris, num_glcmds, num_frames;
                int32_t ofs_skins, ofs_st, ofs_tris, ofs_frames, ofs_glcmds, ofs_end;
            } hdr = {};
            std::memcpy(&hdr, buf.data(), sizeof(hdr));
            // The counts and offsets come straight from the file. They are checked in 64-bit arithmetic: in
            // 32 bits a huge count wraps around, passes the test and sends the loops below far past the buffer.
            const bool sane = hdr.version == 8 && hdr.num_xyz > 0 && hdr.num_tris > 0 &&
                              hdr.ofs_frames >= 68 && hdr.ofs_tris >= 68;
            const uint64_t firstFrameEnd = sane
                ? static_cast<uint64_t>(hdr.ofs_frames) + 40ULL + static_cast<uint64_t>(hdr.num_xyz) * 4ULL : 0ULL;
            const uint64_t trianglesEnd = sane
                ? static_cast<uint64_t>(hdr.ofs_tris) + static_cast<uint64_t>(hdr.num_tris) * 12ULL : 0ULL;
            if (sane && firstFrameEnd <= buf.size() && trianglesEnd <= buf.size())
            {
                mesh.formatName = L"Quake II Model (.md2)";
                mesh.animFrameCount = static_cast<size_t>(std::max(1, hdr.num_frames));
                const uint8_t* framePtr = buf.data() + hdr.ofs_frames;
                float scale[3] = {}, translate[3] = {};
                std::memcpy(scale, framePtr, 12);
                std::memcpy(translate, framePtr + 12, 12);
                const uint8_t* vertsPtr = framePtr + 40;
                mesh.vertices.reserve(hdr.num_xyz);
                for (int i = 0; i < hdr.num_xyz; ++i) {
                    const uint8_t* vp = vertsPtr + i * 4;
                    float vx = vp[0] * scale[0] + translate[0];
                    float vy = vp[1] * scale[1] + translate[1];
                    float vz = vp[2] * scale[2] + translate[2];
                    mesh.vertices.push_back({ vx, vz, -vy });
                }
                const uint8_t* trisPtr = buf.data() + hdr.ofs_tris;
                mesh.tris.reserve(hdr.num_tris);
                for (int t = 0; t < hdr.num_tris; ++t) {
                    uint16_t vi[3] = {};
                    std::memcpy(vi, trisPtr + t * 12, 6);
                    if (vi[0] < mesh.vertices.size() && vi[1] < mesh.vertices.size() && vi[2] < mesh.vertices.size()) {
                        mesh.tris.push_back({ vi[0], vi[2], vi[1], {} });
                    }
                }
                FinalizeMeshBoundsAndNormals(mesh);
                return mesh;
            }
        }
    }

