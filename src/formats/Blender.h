#pragma once
#include "MeshBuild3D.h"
// Blender.h — Blender (.blend) reader for the 3D viewer.

namespace Formats {

// Returns true when the file was handled here; `mesh` then holds the result (or its status message).
// false: not this format, the caller tries the next reader.
inline bool ParseBlendFile(const std::wstring& ext, const std::vector<uint8_t>& buf, uint64_t fileSize, LoadedMesh3D& mesh,
                 uint64_t generation, const std::atomic<uint64_t>* activeGen) {
    using MeshBuild3D::FinalizeMeshBoundsAndNormals;
    (void)ext; (void)fileSize; (void)generation; (void)activeGen;
    // 8. Blender .blend Full DNA1 / SDNA Mesh Parser (Legacy <=2.79, 2.80-3.5, 3.6-4.x SoA, 5.0+ LargeBHead8, Gzip)
    if (ext == L".blend") {
        std::vector<uint8_t> decompBlend;
        const uint8_t* bData = buf.data();
        size_t bSize = buf.size();

        // 8a. Gzip-compressed .blend (0x1F 0x8B)
        if (bSize >= 18 && bData[0] == 0x1F && bData[1] == 0x8B) {
            if (ZipDeflate::InflateGzip(bData, bSize, decompBlend, 256 * 1024 * 1024)) {
                bData = decompBlend.data();
                bSize = decompBlend.size();
            }
        }
        // 8b. Blender 3.0–5.0+ Zstandard frame wrapper (RFC 8878 Compressed/Raw/RLE blocks + skippable seek-table frames)
        else if (bSize >= 12 && std::memcmp(bData, "BLENDER", 7) != 0) {
            auto rd32le = [](const uint8_t* p) -> uint32_t {
                return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
            };
            uint32_t firstMagic = rd32le(bData);
            if (firstMagic == 0xFD2FB528u || (firstMagic & 0xFFFFFFF0u) == 0x184D2A50u) {
                if (StreamCodecs::DecompressZstdFrame(bData, bSize, decompBlend, 256 * 1024 * 1024) &&
                    decompBlend.size() >= 12) {
                    bData = decompBlend.data();
                    bSize = decompBlend.size();
                }
            }
        }

        // 8c. Parse 12-byte legacy/modern header ("BLENDER-v293", "BLENDER_v279") or 17-byte Blender 5.0+ ("BLENDER17-01v0500")
        size_t hdrLen = 0;
        int ptrSize = 0;
        int fmtVer = 0;
        bool isLE = true;
        std::string verStr;

        if (bSize >= 12 && std::memcmp(bData, "BLENDER", 7) == 0) {
            if (bSize >= 17 && bData[7] >= '1' && bData[7] <= '9' && bData[8] >= '0' && bData[8] <= '9') {
                hdrLen = static_cast<size_t>((bData[7] - '0') * 10 + (bData[8] - '0'));
                if (hdrLen >= 17 && hdrLen <= 64 && hdrLen <= bSize &&
                    (bData[9] == '-' || bData[9] == '_') &&
                    (bData[12] == 'v' || bData[12] == 'V'))
                {
                    ptrSize = (bData[9] == '-') ? 8 : 4;
                    fmtVer = (bData[10] - '0') * 10 + (bData[11] - '0');
                    isLE = (bData[12] == 'v');
                    verStr.assign(reinterpret_cast<const char*>(bData + 13), 4);
                } else {
                    hdrLen = 0;
                }
            } else if ((bData[7] == '-' || bData[7] == '_') && (bData[8] == 'v' || bData[8] == 'V')) {
                hdrLen = 12;
                ptrSize = (bData[7] == '-') ? 8 : 4; // '-' = 64-bit (8B), '_' = 32-bit (4B)
                fmtVer = 0;
                isLE = (bData[8] == 'v');
                verStr.assign(reinterpret_cast<const char*>(bData + 9), 3);
            }
        }

        if (hdrLen >= 12 && (ptrSize == 4 || ptrSize == 8)) {
            mesh.formatName = L"Blender 3D (.blend v" + PathUtils::Utf8ToWide(verStr) + L")";

            auto rdU16 = [&](const uint8_t* p) -> uint16_t {
                return isLE
                    ? static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8))
                    : static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
            };
            auto rdU32 = [&](const uint8_t* p) -> uint32_t {
                return isLE
                    ? (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24))
                    : ((static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                       (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]));
            };
            auto rdI32 = [&](const uint8_t* p) -> int32_t {
                return static_cast<int32_t>(rdU32(p));
            };
            auto rdU64 = [&](const uint8_t* p) -> uint64_t {
                return isLE
                    ? (static_cast<uint64_t>(rdU32(p)) | (static_cast<uint64_t>(rdU32(p + 4)) << 32))
                    : ((static_cast<uint64_t>(rdU32(p)) << 32) | static_cast<uint64_t>(rdU32(p + 4)));
            };
            auto rdI64 = [&](const uint8_t* p) -> int64_t {
                return static_cast<int64_t>(rdU64(p));
            };
            auto rdF32 = [&](const uint8_t* p) -> float {
                uint32_t u = rdU32(p);
                float f = 0.0f;
                std::memcpy(&f, &u, 4);
                return f;
            };
            auto rdPtr = [&](const uint8_t* p) -> uint64_t {
                return (ptrSize == 8) ? rdU64(p) : static_cast<uint64_t>(rdU32(p));
            };

            // 8d. Walk all BHead blocks to index oldMemAddr -> FileBlock and locate DNA1
            struct BlendBlock {
                char code[5] = {};
                const uint8_t* data = nullptr;
                size_t size = 0;
                uint64_t oldAddr = 0;
                int32_t sdnaIndex = 0;
                int64_t count = 0;
            };
            std::vector<BlendBlock> blocks;
            blocks.reserve(256);
            std::unordered_map<uint64_t, size_t> addrToBlock;
            size_t dnaBlockIdx = static_cast<size_t>(-1);

            const size_t bheadSize = (fmtVer >= 1) ? 32 : ((ptrSize == 8) ? 24 : 20);
            size_t pos = hdrLen;
            while (pos + bheadSize <= bSize) {
                const uint8_t* bh = bData + pos;
                if (std::memcmp(bh, "ENDB", 4) == 0) break;

                BlendBlock blk;
                std::memcpy(blk.code, bh, 4);
                blk.code[4] = '\0';

                int64_t rawLen = 0;
                if (fmtVer >= 1) {
                    // Blender 5.0+ LargeBHead8: code[4], SDNAnr(4), old(8), len(8), nr(8)
                    blk.sdnaIndex = rdI32(bh + 4);
                    blk.oldAddr   = rdU64(bh + 8);
                    rawLen        = rdI64(bh + 16);
                    blk.count     = rdI64(bh + 24);
                } else if (ptrSize == 8) {
                    // Standard 64-bit SmallBHead8: code[4], len(4), old(8), SDNAnr(4), nr(4)
                    rawLen        = rdI32(bh + 4);
                    blk.oldAddr   = rdU64(bh + 8);
                    blk.sdnaIndex = rdI32(bh + 16);
                    blk.count     = rdI32(bh + 20);
                } else {
                    // Standard 32-bit BHead4: code[4], len(4), old(4), SDNAnr(4), nr(4)
                    rawLen        = rdI32(bh + 4);
                    blk.oldAddr   = rdU32(bh + 8);
                    blk.sdnaIndex = rdI32(bh + 12);
                    blk.count     = rdI32(bh + 16);
                }

                if (rawLen < 0 || static_cast<uint64_t>(rawLen) > bSize - pos - bheadSize) break;
                blk.size = static_cast<size_t>(rawLen);
                blk.data = bh + bheadSize;

                size_t idx = blocks.size();
                if (std::memcmp(blk.code, "DNA1", 4) == 0) {
                    dnaBlockIdx = idx;
                }
                if (blk.oldAddr != 0) {
                    addrToBlock.emplace(blk.oldAddr, idx);
                }
                blocks.push_back(blk);
                pos += bheadSize + blk.size;
            }

            // 8e. Parse DNA1 (SDNA -> NAME -> TYPE -> TLEN -> STRC)
            if (dnaBlockIdx != static_cast<size_t>(-1) && blocks[dnaBlockIdx].size >= 24) {
                const uint8_t* dna = blocks[dnaBlockIdx].data;
                const size_t dnaLen = blocks[dnaBlockIdx].size;

                struct DnaField {
                    std::string cleanName;
                    uint16_t typeIdx = 0;
                    bool isPointer = false;
                    size_t elemCount = 1;
                    size_t offset = 0;
                    size_t byteSize = 0;
                };
                struct DnaStruct {
                    uint16_t typeIdx = 0;
                    std::vector<DnaField> fields;
                };

                std::vector<std::string_view> names;
                std::vector<std::string_view> types;
                std::vector<uint16_t> tlens;
                std::vector<DnaStruct> structs;
                std::unordered_map<uint16_t, int32_t> typeToStruct;
                std::unordered_map<std::string_view, int32_t> nameToStruct;
                bool dnaValid = false;

                if (std::memcmp(dna, "SDNANAME", 8) == 0) {
                    size_t dp = 8;
                    auto align4 = [&]() { dp = (dp + 3u) & ~static_cast<size_t>(3u); };

                    uint32_t nNames = rdU32(dna + dp); dp += 4;
                    if (nNames <= 131072) {
                        names.reserve(nNames);
                        for (uint32_t i = 0; i < nNames && dp < dnaLen; ++i) {
                            size_t s = dp;
                            while (dp < dnaLen && dna[dp] != 0) ++dp;
                            if (dp >= dnaLen) break;
                            names.emplace_back(reinterpret_cast<const char*>(dna + s), dp - s);
                            ++dp;
                        }
                        align4();
                    }

                    if (names.size() == nNames && dp + 8 <= dnaLen && std::memcmp(dna + dp, "TYPE", 4) == 0) {
                        dp += 4;
                        uint32_t nTypes = rdU32(dna + dp); dp += 4;
                        if (nTypes <= 65536) {
                            types.reserve(nTypes);
                            for (uint32_t i = 0; i < nTypes && dp < dnaLen; ++i) {
                                size_t s = dp;
                                while (dp < dnaLen && dna[dp] != 0) ++dp;
                                if (dp >= dnaLen) break;
                                types.emplace_back(reinterpret_cast<const char*>(dna + s), dp - s);
                                ++dp;
                            }
                            align4();
                        }

                        if (types.size() == nTypes && dp + 4 + static_cast<size_t>(nTypes) * 2 <= dnaLen &&
                            std::memcmp(dna + dp, "TLEN", 4) == 0)
                        {
                            dp += 4;
                            tlens.resize(nTypes);
                            for (uint32_t i = 0; i < nTypes; ++i) {
                                tlens[i] = rdU16(dna + dp);
                                dp += 2;
                            }
                            align4();

                            if (dp + 8 <= dnaLen && std::memcmp(dna + dp, "STRC", 4) == 0) {
                                dp += 4;
                                uint32_t nStrc = rdU32(dna + dp); dp += 4;
                                if (nStrc <= 65536) {
                                    structs.resize(nStrc);
                                    dnaValid = true;
                                    for (uint32_t s = 0; s < nStrc; ++s) {
                                        if (dp + 4 > dnaLen) { dnaValid = false; break; }
                                        uint16_t stType = rdU16(dna + dp);
                                        uint16_t nFields = rdU16(dna + dp + 2);
                                        dp += 4;
                                        if (stType >= types.size() || dp + static_cast<size_t>(nFields) * 4 > dnaLen) {
                                            dnaValid = false;
                                            break;
                                        }
                                        structs[s].typeIdx = stType;
                                        structs[s].fields.reserve(nFields);
                                        typeToStruct[stType] = static_cast<int32_t>(s);
                                        nameToStruct[types[stType]] = static_cast<int32_t>(s);

                                        size_t curOff = 0;
                                        for (uint16_t f = 0; f < nFields; ++f) {
                                            uint16_t ft = rdU16(dna + dp);
                                            uint16_t fn = rdU16(dna + dp + 2);
                                            dp += 4;
                                            if (ft >= types.size() || fn >= names.size()) {
                                                dnaValid = false;
                                                break;
                                            }
                                            std::string_view raw = names[fn];
                                            DnaField df;
                                            df.typeIdx = ft;
                                            df.isPointer = !raw.empty() && (raw[0] == '*' || raw[0] == '(');

                                            // Parse clean identifier and fixed array dimensions e.g. "*mpoly", "co[3]", "obmat[4][4]"
                                            size_t ns = 0;
                                            while (ns < raw.size() && (raw[ns] == '*' || raw[ns] == '(')) ++ns;
                                            size_t ne = ns;
                                            while (ne < raw.size() && raw[ne] != '[' && raw[ne] != ')') ++ne;
                                            df.cleanName.assign(raw.substr(ns, ne - ns));

                                            size_t arrPos = ne;
                                            size_t countProd = 1;
                                            while (arrPos < raw.size()) {
                                                size_t lb = raw.find('[', arrPos);
                                                if (lb == std::string_view::npos) break;
                                                size_t rb = raw.find(']', lb + 1);
                                                if (rb == std::string_view::npos) break;
                                                size_t dim = 0;
                                                for (size_t k = lb + 1; k < rb; ++k) {
                                                    if (raw[k] >= '0' && raw[k] <= '9') {
                                                        dim = dim * 10 + static_cast<size_t>(raw[k] - '0');
                                                    }
                                                }
                                                if (dim > 0) countProd *= dim;
                                                arrPos = rb + 1;
                                            }
                                            df.elemCount = countProd;
                                            size_t baseSz = df.isPointer ? static_cast<size_t>(ptrSize) : static_cast<size_t>(tlens[ft]);
                                            df.byteSize = baseSz * df.elemCount;
                                            df.offset = curOff;
                                            curOff += df.byteSize;
                                            structs[s].fields.push_back(std::move(df));
                                        }
                                        if (!dnaValid) break;
                                    }
                                }
                            }
                        }
                    }
                }

                if (dnaValid) {
                    struct FieldLookup {
                        size_t offset = 0;
                        size_t byteSize = 0;
                        size_t elemCount = 0;
                        uint16_t typeIdx = 0;
                        bool isPointer = false;
                        bool found = false;
                    };

                    // Exact SDNA field lookup supporting struct index or struct typeName, plus nested "vdata.layers" paths
                    auto getFieldOffset = [&](int32_t structIdx, std::string_view typeName, std::string_view fieldPath) -> FieldLookup {
                        if (structIdx < 0 && !typeName.empty()) {
                            auto it = nameToStruct.find(typeName);
                            if (it != nameToStruct.end()) structIdx = it->second;
                        }
                        if (structIdx < 0 || static_cast<size_t>(structIdx) >= structs.size()) return {};

                        size_t accumOff = 0;
                        int32_t curStruct = structIdx;
                        std::string_view rem = fieldPath;
                        while (!rem.empty()) {
                            size_t dot = rem.find('.');
                            std::string_view part = (dot == std::string_view::npos) ? rem : rem.substr(0, dot);
                            const DnaField* matched = nullptr;
                            for (const auto& f : structs[curStruct].fields) {
                                if (f.cleanName == part) {
                                    matched = &f;
                                    break;
                                }
                            }
                            if (!matched) return {};
                            accumOff += matched->offset;
                            if (dot == std::string_view::npos) {
                                if (!typeName.empty() && structs[structIdx].typeIdx < types.size() &&
                                    types[structs[structIdx].typeIdx] != typeName &&
                                    types[matched->typeIdx] != typeName)
                                {
                                    // Optional expected field type mismatch
                                    return {};
                                }
                                return { accumOff, matched->byteSize, matched->elemCount, matched->typeIdx, matched->isPointer, true };
                            }
                            if (matched->isPointer) return {};
                            auto sit = typeToStruct.find(matched->typeIdx);
                            if (sit == typeToStruct.end()) return {};
                            curStruct = sit->second;
                            rem = rem.substr(dot + 1);
                        }
                        return {};
                    };

                    auto resolveBlock = [&](uint64_t ptr) -> const BlendBlock* {
                        if (ptr == 0) return nullptr;
                        auto it = addrToBlock.find(ptr);
                        return (it != addrToBlock.end()) ? &blocks[it->second] : nullptr;
                    };

                    auto readIntField = [&](const uint8_t* base, size_t maxSz, int32_t sIdx, std::string_view fname, int64_t& outVal) -> bool {
                        FieldLookup fl = getFieldOffset(sIdx, {}, fname);
                        if (!fl.found || fl.isPointer || fl.offset + fl.byteSize > maxSz) return false;
                        std::string_view tname = types[fl.typeIdx];
                        if ((tname == "int" || tname == "uint") && fl.byteSize >= 4) {
                            outVal = rdI32(base + fl.offset);
                            return true;
                        }
                        if ((tname == "int64_t" || tname == "uint64_t") && fl.byteSize >= 8) {
                            outVal = rdI64(base + fl.offset);
                            return true;
                        }
                        if ((tname == "short" || tname == "ushort") && fl.byteSize >= 2) {
                            outVal = static_cast<int16_t>(rdU16(base + fl.offset));
                            return true;
                        }
                        return false;
                    };

                    auto readPtrField = [&](const uint8_t* base, size_t maxSz, int32_t sIdx, std::string_view fname) -> uint64_t {
                        FieldLookup fl = getFieldOffset(sIdx, {}, fname);
                        if (!fl.found || !fl.isPointer || fl.offset + static_cast<size_t>(ptrSize) > maxSz) return 0;
                        return rdPtr(base + fl.offset);
                    };

                    // 8f. Collect Object (OB) world transforms referencing Mesh blocks (ob.data -> ob.obmat[4][4])
                    struct ObXform {
                        float m[16] = {
                            1,0,0,0,
                            0,1,0,0,
                            0,0,1,0,
                            0,0,0,1
                        };
                        bool flipWinding = false;
                    };
                    std::unordered_map<uint64_t, std::vector<ObXform>> meshTransforms;

                    for (const auto& blk : blocks) {
                        if (blk.code[0] == 'O' && blk.code[1] == 'B' && blk.count >= 1 &&
                            blk.sdnaIndex >= 0 && static_cast<size_t>(blk.sdnaIndex) < structs.size())
                        {
                            size_t stSize = tlens[structs[blk.sdnaIndex].typeIdx];
                            if (stSize == 0 || stSize > blk.size) continue;
                            FieldLookup fData  = getFieldOffset(blk.sdnaIndex, {}, "data");
                            FieldLookup fObmat = getFieldOffset(blk.sdnaIndex, {}, "obmat");
                            FieldLookup fLoc   = getFieldOffset(blk.sdnaIndex, {}, "loc");
                            FieldLookup fScale = getFieldOffset(blk.sdnaIndex, {}, "scale");
                            if (!fScale.found) fScale = getFieldOffset(blk.sdnaIndex, {}, "size");
                            if (!fData.found || !fData.isPointer) continue;

                            int64_t nObjs = std::min<int64_t>(blk.count, static_cast<int64_t>(blk.size / stSize));
                            for (int64_t o = 0; o < nObjs; ++o) {
                                const uint8_t* obPtr = blk.data + static_cast<size_t>(o) * stSize;
                                if (fData.offset + static_cast<size_t>(ptrSize) > stSize) continue;
                                uint64_t dataAddr = rdPtr(obPtr + fData.offset);
                                if (dataAddr == 0) continue;

                                ObXform xf;
                                bool validMat = false;
                                if (fObmat.found && fObmat.byteSize >= 64 && fObmat.offset + 64 <= stSize) {
                                    for (int k = 0; k < 16; ++k) {
                                        xf.m[k] = rdF32(obPtr + fObmat.offset + k * 4);
                                        if (!std::isfinite(xf.m[k])) xf.m[k] = 0.0f;
                                    }
                                    // Upper-left 3x3 determinant (column-major m[col*4 + row])
                                    float det =
                                        xf.m[0] * (xf.m[5] * xf.m[10] - xf.m[9] * xf.m[6]) -
                                        xf.m[4] * (xf.m[1] * xf.m[10] - xf.m[9] * xf.m[2]) +
                                        xf.m[8] * (xf.m[1] * xf.m[6]  - xf.m[5] * xf.m[2]);
                                    if (std::fabs(det) > 1e-7f) {
                                        validMat = true;
                                        xf.flipWinding = (det < 0.0f);
                                    }
                                }
                                if (!validMat) {
                                    float sx = 1.0f, sy = 1.0f, sz = 1.0f;
                                    if (fScale.found && fScale.byteSize >= 12 && fScale.offset + 12 <= stSize) {
                                        float rx = rdF32(obPtr + fScale.offset + 0);
                                        float ry = rdF32(obPtr + fScale.offset + 4);
                                        float rz = rdF32(obPtr + fScale.offset + 8);
                                        if (std::fabs(rx * ry * rz) > 1e-7f) { sx = rx; sy = ry; sz = rz; }
                                    }
                                    float lx = 0.0f, ly = 0.0f, lz = 0.0f;
                                    if (fLoc.found && fLoc.byteSize >= 12 && fLoc.offset + 12 <= stSize) {
                                        lx = rdF32(obPtr + fLoc.offset + 0);
                                        ly = rdF32(obPtr + fLoc.offset + 4);
                                        lz = rdF32(obPtr + fLoc.offset + 8);
                                    }
                                    xf = ObXform{};
                                    xf.m[0] = sx; xf.m[5] = sy; xf.m[10] = sz;
                                    xf.m[12] = lx; xf.m[13] = ly; xf.m[14] = lz;
                                    xf.flipWinding = (sx * sy * sz < 0.0f);
                                }
                                meshTransforms[dataAddr].push_back(xf);
                            }
                        }
                    }

                    // Helper to search a CustomData embedded struct (vdata/vert_data, ldata/corner_data, pdata/face_data, fdata)
                    int32_t cdLayerStruct = -1;
                    {
                        auto it = nameToStruct.find("CustomDataLayer");
                        if (it != nameToStruct.end()) cdLayerStruct = it->second;
                    }
                    auto findCustomDataLayer = [&](const uint8_t* meBase, size_t meSize, int32_t meStruct,
                                                   std::string_view cdField1, std::string_view cdField2,
                                                   std::string_view wantName, int32_t wantType) -> const BlendBlock*
                    {
                        if (cdLayerStruct < 0) return nullptr;
                        size_t cdlStride = tlens[structs[cdLayerStruct].typeIdx];
                        if (cdlStride == 0) return nullptr;

                        std::string pLayers = std::string(cdField1) + ".layers";
                        std::string pTot    = std::string(cdField1) + ".totlayer";
                        FieldLookup flLayers = getFieldOffset(meStruct, {}, pLayers);
                        FieldLookup flTot    = getFieldOffset(meStruct, {}, pTot);
                        if (!flLayers.found && !cdField2.empty()) {
                            pLayers = std::string(cdField2) + ".layers";
                            pTot    = std::string(cdField2) + ".totlayer";
                            flLayers = getFieldOffset(meStruct, {}, pLayers);
                            flTot    = getFieldOffset(meStruct, {}, pTot);
                        }
                        if (!flLayers.found || !flLayers.isPointer || !flTot.found) return nullptr;
                        if (flLayers.offset + static_cast<size_t>(ptrSize) > meSize || flTot.offset + 4 > meSize) return nullptr;

                        uint64_t layersAddr = rdPtr(meBase + flLayers.offset);
                        int32_t totLayer = rdI32(meBase + flTot.offset);
                        if (layersAddr == 0 || totLayer <= 0 || totLayer > 4096) return nullptr;
                        const BlendBlock* layersBlk = resolveBlock(layersAddr);
                        if (!layersBlk) return nullptr;

                        FieldLookup flType = getFieldOffset(cdLayerStruct, {}, "type");
                        FieldLookup flName = getFieldOffset(cdLayerStruct, {}, "name");
                        FieldLookup flData = getFieldOffset(cdLayerStruct, {}, "data");
                        if (!flData.found || !flData.isPointer) return nullptr;

                        int32_t maxL = std::min<int32_t>(totLayer, static_cast<int32_t>(layersBlk->size / cdlStride));
                        for (int32_t i = 0; i < maxL; ++i) {
                            const uint8_t* lp = layersBlk->data + static_cast<size_t>(i) * cdlStride;
                            if (flData.offset + static_cast<size_t>(ptrSize) > cdlStride) continue;
                            if (!wantName.empty() && flName.found && flName.offset + flName.byteSize <= cdlStride) {
                                size_t nLen = 0;
                                while (nLen < flName.byteSize && lp[flName.offset + nLen] != 0) ++nLen;
                                std::string_view layerName(reinterpret_cast<const char*>(lp + flName.offset), nLen);
                                if (layerName == wantName) {
                                    return resolveBlock(rdPtr(lp + flData.offset));
                                }
                            }
                            if (wantType >= 0 && flType.found && flType.offset + 4 <= cdlStride) {
                                int32_t lType = rdI32(lp + flType.offset);
                                if (lType == wantType) {
                                    return resolveBlock(rdPtr(lp + flData.offset));
                                }
                            }
                        }
                        return nullptr;
                    };

                    // 8g. Decode all ME (Mesh) datablocks across Blender <=2.79, 2.80-3.5, 3.6-4.x, and 5.0+
                    for (const auto& blk : blocks) {
                        if (blk.code[0] != 'M' || blk.code[1] != 'E' || blk.count < 1) continue;
                        if (blk.sdnaIndex < 0 || static_cast<size_t>(blk.sdnaIndex) >= structs.size()) continue;
                        const int32_t meStruct = blk.sdnaIndex;
                        const size_t meStride = tlens[structs[meStruct].typeIdx];
                        if (meStride == 0 || meStride > blk.size) continue;

                        int64_t totvert = 0;
                        if (!readIntField(blk.data, meStride, meStruct, "verts_num", totvert)) {
                            readIntField(blk.data, meStride, meStruct, "totvert", totvert);
                        }
                        if (totvert <= 0 || totvert > 2000000) continue;

                        // 1. Extract local 3D vertices
                        std::vector<Vec3f> localVerts;
                        const BlendBlock* posLayerBlk = findCustomDataLayer(blk.data, meStride, meStruct, "vert_data", "vdata", "position", -1);
                        if (posLayerBlk && posLayerBlk->size >= static_cast<size_t>(totvert) * 12) {
                            localVerts.resize(static_cast<size_t>(totvert));
                            for (int64_t v = 0; v < totvert; ++v) {
                                const uint8_t* vp = posLayerBlk->data + static_cast<size_t>(v) * 12;
                                localVerts[static_cast<size_t>(v)] = { rdF32(vp + 0), rdF32(vp + 4), rdF32(vp + 8) };
                            }
                        } else {
                            const BlendBlock* mvertBlk = resolveBlock(readPtrField(blk.data, meStride, meStruct, "mvert"));
                            if (!mvertBlk) {
                                mvertBlk = findCustomDataLayer(blk.data, meStride, meStruct, "vert_data", "vdata", {}, 0 /*CD_MVERT*/);
                            }
                            if (mvertBlk) {
                                size_t vStride = 0;
                                size_t coOff = 0;
                                if (mvertBlk->sdnaIndex > 0 && static_cast<size_t>(mvertBlk->sdnaIndex) < structs.size()) {
                                    FieldLookup flCo = getFieldOffset(mvertBlk->sdnaIndex, "MVert", "co");
                                    if (flCo.found && flCo.byteSize >= 12) {
                                        vStride = tlens[structs[mvertBlk->sdnaIndex].typeIdx];
                                        coOff = flCo.offset;
                                    }
                                }
                                if (vStride == 0 && mvertBlk->size >= static_cast<size_t>(totvert) * 12 &&
                                    (mvertBlk->size % static_cast<size_t>(totvert)) == 0)
                                {
                                    vStride = mvertBlk->size / static_cast<size_t>(totvert);
                                }
                                if (vStride >= coOff + 12 && mvertBlk->size >= static_cast<size_t>(totvert) * vStride) {
                                    localVerts.resize(static_cast<size_t>(totvert));
                                    for (int64_t v = 0; v < totvert; ++v) {
                                        const uint8_t* vp = mvertBlk->data + static_cast<size_t>(v) * vStride + coOff;
                                        localVerts[static_cast<size_t>(v)] = { rdF32(vp + 0), rdF32(vp + 4), rdF32(vp + 8) };
                                    }
                                }
                            }
                        }
                        if (localVerts.empty()) continue;

                        // 2. Extract polygon/face topology into triangles
                        std::vector<std::array<uint32_t, 3>> localTris;

                        int64_t totpoly = 0, totloop = 0, totface = 0;
                        if (!readIntField(blk.data, meStride, meStruct, "faces_num", totpoly)) {
                            readIntField(blk.data, meStride, meStruct, "totpoly", totpoly);
                        }
                        if (!readIntField(blk.data, meStride, meStruct, "corners_num", totloop)) {
                            readIntField(blk.data, meStride, meStruct, "totloop", totloop);
                        }
                        readIntField(blk.data, meStride, meStruct, "totface", totface);

                        // Path A: Blender 3.6 - 5.0+ SoA (face_offset_indices / poly_offset_indices + ".corner_verts")
                        uint64_t polyOffAddr = readPtrField(blk.data, meStride, meStruct, "face_offset_indices");
                        if (polyOffAddr == 0) polyOffAddr = readPtrField(blk.data, meStride, meStruct, "poly_offset_indices");
                        const BlendBlock* polyOffBlk = resolveBlock(polyOffAddr);
                        const BlendBlock* cornerVertsBlk = findCustomDataLayer(blk.data, meStride, meStruct, "corner_data", "ldata", ".corner_verts", -1);

                        if (totpoly > 0 && totpoly <= 4000000 && totloop >= 3 &&
                            polyOffBlk && polyOffBlk->size >= static_cast<size_t>(totpoly + 1) * 4 &&
                            cornerVertsBlk && cornerVertsBlk->size >= static_cast<size_t>(totloop) * 4)
                        {
                            for (int64_t p = 0; p < totpoly; ++p) {
                                int32_t ls = rdI32(polyOffBlk->data + static_cast<size_t>(p) * 4);
                                int32_t le = rdI32(polyOffBlk->data + static_cast<size_t>(p + 1) * 4);
                                if (ls < 0 || le <= ls + 2 || le > totloop) continue;
                                int32_t v0 = rdI32(cornerVertsBlk->data + static_cast<size_t>(ls) * 4);
                                if (v0 < 0 || v0 >= totvert) continue;
                                for (int32_t k = ls + 1; k + 1 < le; ++k) {
                                    int32_t v1 = rdI32(cornerVertsBlk->data + static_cast<size_t>(k) * 4);
                                    int32_t v2 = rdI32(cornerVertsBlk->data + static_cast<size_t>(k + 1) * 4);
                                    if (v1 >= 0 && v1 < totvert && v2 >= 0 && v2 < totvert) {
                                        localTris.push_back({ static_cast<uint32_t>(v0), static_cast<uint32_t>(v1), static_cast<uint32_t>(v2) });
                                    }
                                }
                            }
                        }

                        // Path B: Blender 2.80 - 3.5 (MPoly + MLoop or MPoly + .corner_verts)
                        if (localTris.empty() && totpoly > 0 && totpoly <= 4000000 && totloop >= 3) {
                            const BlendBlock* mpolyBlk = resolveBlock(readPtrField(blk.data, meStride, meStruct, "mpoly"));
                            if (!mpolyBlk) mpolyBlk = findCustomDataLayer(blk.data, meStride, meStruct, "face_data", "pdata", {}, 25 /*CD_MPOLY*/);
                            const BlendBlock* mloopBlk = resolveBlock(readPtrField(blk.data, meStride, meStruct, "mloop"));
                            if (!mloopBlk) mloopBlk = findCustomDataLayer(blk.data, meStride, meStruct, "corner_data", "ldata", {}, 26 /*CD_MLOOP*/);

                            int32_t mpStruct = (mpolyBlk && mpolyBlk->sdnaIndex > 0) ? mpolyBlk->sdnaIndex : -1;
                            FieldLookup flLs = getFieldOffset(mpStruct, "MPoly", "loopstart");
                            FieldLookup flTl = getFieldOffset(mpStruct, "MPoly", "totloop");
                            size_t mpStride = (flLs.found && flTl.found)
                                ? (mpStruct >= 0 ? tlens[structs[mpStruct].typeIdx] : tlens[structs[nameToStruct["MPoly"]].typeIdx])
                                : 0;

                            if (mpolyBlk && mpStride >= 8 && mpolyBlk->size >= static_cast<size_t>(totpoly) * mpStride) {
                                if (cornerVertsBlk && cornerVertsBlk->size >= static_cast<size_t>(totloop) * 4) {
                                    for (int64_t p = 0; p < totpoly; ++p) {
                                        const uint8_t* pp = mpolyBlk->data + static_cast<size_t>(p) * mpStride;
                                        int32_t ls = rdI32(pp + flLs.offset);
                                        int32_t nl = rdI32(pp + flTl.offset);
                                        if (ls < 0 || nl < 3 || static_cast<int64_t>(ls) + nl > totloop) continue;
                                        int32_t v0 = rdI32(cornerVertsBlk->data + static_cast<size_t>(ls) * 4);
                                        if (v0 < 0 || v0 >= totvert) continue;
                                        for (int32_t k = 1; k + 1 < nl; ++k) {
                                            int32_t v1 = rdI32(cornerVertsBlk->data + static_cast<size_t>(ls + k) * 4);
                                            int32_t v2 = rdI32(cornerVertsBlk->data + static_cast<size_t>(ls + k + 1) * 4);
                                            if (v1 >= 0 && v1 < totvert && v2 >= 0 && v2 < totvert) {
                                                localTris.push_back({ static_cast<uint32_t>(v0), static_cast<uint32_t>(v1), static_cast<uint32_t>(v2) });
                                            }
                                        }
                                    }
                                } else if (mloopBlk) {
                                    int32_t mlStruct = (mloopBlk->sdnaIndex > 0) ? mloopBlk->sdnaIndex : -1;
                                    FieldLookup flV = getFieldOffset(mlStruct, "MLoop", "v");
                                    size_t mlStride = flV.found
                                        ? (mlStruct >= 0 ? tlens[structs[mlStruct].typeIdx] : tlens[structs[nameToStruct["MLoop"]].typeIdx])
                                        : 0;
                                    if (mlStride >= flV.offset + 4 && mloopBlk->size >= static_cast<size_t>(totloop) * mlStride) {
                                        for (int64_t p = 0; p < totpoly; ++p) {
                                            const uint8_t* pp = mpolyBlk->data + static_cast<size_t>(p) * mpStride;
                                            int32_t ls = rdI32(pp + flLs.offset);
                                            int32_t nl = rdI32(pp + flTl.offset);
                                            if (ls < 0 || nl < 3 || static_cast<int64_t>(ls) + nl > totloop) continue;
                                            int32_t v0 = rdI32(mloopBlk->data + static_cast<size_t>(ls) * mlStride + flV.offset);
                                            if (v0 < 0 || v0 >= totvert) continue;
                                            for (int32_t k = 1; k + 1 < nl; ++k) {
                                                int32_t v1 = rdI32(mloopBlk->data + static_cast<size_t>(ls + k) * mlStride + flV.offset);
                                                int32_t v2 = rdI32(mloopBlk->data + static_cast<size_t>(ls + k + 1) * mlStride + flV.offset);
                                                if (v1 >= 0 && v1 < totvert && v2 >= 0 && v2 < totvert) {
                                                    localTris.push_back({ static_cast<uint32_t>(v0), static_cast<uint32_t>(v1), static_cast<uint32_t>(v2) });
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Path C: Legacy Blender <= 2.79 (MFace: v1, v2, v3, v4)
                        if (localTris.empty() && totface > 0 && totface <= 4000000) {
                            const BlendBlock* mfaceBlk = resolveBlock(readPtrField(blk.data, meStride, meStruct, "mface"));
                            if (!mfaceBlk) mfaceBlk = findCustomDataLayer(blk.data, meStride, meStruct, "fdata", {}, {}, 1 /*CD_MFACE*/);
                            if (mfaceBlk) {
                                int32_t mfStruct = (mfaceBlk->sdnaIndex > 0) ? mfaceBlk->sdnaIndex : -1;
                                FieldLookup f1 = getFieldOffset(mfStruct, "MFace", "v1");
                                FieldLookup f2 = getFieldOffset(mfStruct, "MFace", "v2");
                                FieldLookup f3 = getFieldOffset(mfStruct, "MFace", "v3");
                                FieldLookup f4 = getFieldOffset(mfStruct, "MFace", "v4");
                                size_t mfStride = (f1.found && f2.found && f3.found && f4.found)
                                    ? (mfStruct >= 0 ? tlens[structs[mfStruct].typeIdx] : tlens[structs[nameToStruct["MFace"]].typeIdx])
                                    : 0;
                                if (mfStride >= 16 && mfaceBlk->size >= static_cast<size_t>(totface) * mfStride) {
                                    for (int64_t f = 0; f < totface; ++f) {
                                        const uint8_t* fp = mfaceBlk->data + static_cast<size_t>(f) * mfStride;
                                        int32_t v1 = rdI32(fp + f1.offset);
                                        int32_t v2 = rdI32(fp + f2.offset);
                                        int32_t v3 = rdI32(fp + f3.offset);
                                        int32_t v4 = rdI32(fp + f4.offset);
                                        if (v1 >= 0 && v1 < totvert && v2 >= 0 && v2 < totvert && v3 >= 0 && v3 < totvert) {
                                            localTris.push_back({ static_cast<uint32_t>(v1), static_cast<uint32_t>(v2), static_cast<uint32_t>(v3) });
                                            if (v4 > 0 && v4 < totvert) {
                                                localTris.push_back({ static_cast<uint32_t>(v1), static_cast<uint32_t>(v3), static_cast<uint32_t>(v4) });
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (localTris.empty()) continue;

                        // 3. Apply Object (OB) obmat[4][4] world transforms (or emit once in local space if unreferenced)
                        std::vector<ObXform> xforms;
                        auto xit = meshTransforms.find(blk.oldAddr);
                        if (xit != meshTransforms.end() && !xit->second.empty()) {
                            xforms = xit->second;
                        } else {
                            xforms.push_back(ObXform{});
                        }

                        for (const auto& xf : xforms) {
                            uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                            for (const auto& lv : localVerts) {
                                float wx = xf.m[0] * lv.x + xf.m[4] * lv.y + xf.m[8]  * lv.z + xf.m[12];
                                float wy = xf.m[1] * lv.x + xf.m[5] * lv.y + xf.m[9]  * lv.z + xf.m[13];
                                float wz = xf.m[2] * lv.x + xf.m[6] * lv.y + xf.m[10] * lv.z + xf.m[14];
                                mesh.vertices.push_back({ wx, wz, -wy });
                            }
                            for (const auto& lt : localTris) {
                                if (xf.flipWinding) {
                                    mesh.tris.push_back({ baseV + lt[0], baseV + lt[2], baseV + lt[1], {} });
                                } else {
                                    mesh.tris.push_back({ baseV + lt[0], baseV + lt[1], baseV + lt[2], {} });
                                }
                            }
                        }
                    }

                    FinalizeMeshBoundsAndNormals(mesh);
                    if (mesh.loadedOk) return true;
                }
            }
        }
    }
    return false;
}

} // namespace Formats
