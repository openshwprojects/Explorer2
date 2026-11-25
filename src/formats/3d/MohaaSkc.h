#pragma once
// MohaaSkc.h — 2015 Inc. Medal of Honor: Allied Assault / Spearhead / Breakthrough Animation (.skc, "SKAN" v13/v14)
// Parses skelAnimDataFileHeader_t ("<bone> pos" / "<bone> rot" channels + Frame 0 poses), skinning a
// companion <stem>.skd if present or emitting an articulated 3D skeleton mesh standalone.

namespace Formats {

inline bool ParseMohaaSkc(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    if (!data || size < 96) return false;
    if (std::memcmp(data, "SKAN", 4) != 0) return false;

    const int32_t version = ByteIO::RdI32LE(data + 4);
    if (version != 13 && version != 14) return false;

    const float frameTime         = ByteIO::RdF32LE(data + 16);
    const float totalAngleDelta   = ByteIO::RdF32LE(data + 32);
    const int32_t numChannels     = ByteIO::RdI32LE(data + 36);
    const int32_t ofsChannelNames = ByteIO::RdI32LE(data + 40);
    const int32_t numFrames       = ByteIO::RdI32LE(data + 44);

    if (!std::isfinite(frameTime) || !std::isfinite(totalAngleDelta)) return false;
    if (numChannels < 1 || numChannels > 1024 || numFrames < 1 || numFrames > 65536) return false;
    if (48ULL + static_cast<uint64_t>(numFrames) * 48ULL > size) return false;
    if (ofsChannelNames < 48 || static_cast<uint64_t>(ofsChannelNames) + static_cast<uint64_t>(numChannels) * 32ULL > size) {
        return false;
    }

    // Validate Frame 0 bounds/delta floats (offset 48..91)
    for (int k = 0; k < 11; ++k) {
        if (!std::isfinite(ByteIO::RdF32LE(data + 48u + static_cast<size_t>(k) * 4u))) return false;
    }
    const int32_t f0OfsChannels = ByteIO::RdI32LE(data + 48u + 44u);

    // Read Frame 0 vec4 channel values (either direct ofsChannelData table or v14 per-channel stream table)
    std::vector<std::array<float, 4>> chanVals(static_cast<size_t>(numChannels), { 0.0f, 0.0f, 0.0f, 1.0f });
    if (f0OfsChannels >= 96 && static_cast<uint64_t>(f0OfsChannels) + static_cast<uint64_t>(numChannels) * 16ULL <= size) {
        const uint8_t* cp = data + static_cast<size_t>(f0OfsChannels);
        for (int32_t c = 0; c < numChannels; ++c) {
            for (int j = 0; j < 4; ++j) {
                const float v = ByteIO::RdF32LE(cp + static_cast<size_t>(c) * 16u + static_cast<size_t>(j) * 4u);
                if (!std::isfinite(v) || std::fabs(v) > 1e6f) return false;
                chanVals[static_cast<size_t>(c)][static_cast<size_t>(j)] = v;
            }
        }
    } else if (version == 14) {
        // v14 packed channel offset directory immediately after frame headers
        const size_t dirPos = 48u + static_cast<size_t>(numFrames) * 48u;
        if (dirPos + static_cast<size_t>(numChannels) * 4u > size) return false;
        for (int32_t c = 0; c < numChannels; ++c) {
            const int32_t relOrAbs = ByteIO::RdI32LE(data + dirPos + static_cast<size_t>(c) * 4u);
            if (relOrAbs <= 0) return false;
            const size_t cPos = static_cast<size_t>(relOrAbs);
            // Keyframe header: int16 keyCount, int16 frame0Idx, followed by 4 floats (16B) -> 20B
            if (cPos + 20u > size) return false;
            const int16_t keyCount = ByteIO::RdI16LE(data + cPos);
            if (keyCount < 1) return false;
            for (int j = 0; j < 4; ++j) {
                const float v = ByteIO::RdF32LE(data + cPos + 4u + static_cast<size_t>(j) * 4u);
                if (!std::isfinite(v) || std::fabs(v) > 1e6f) return false;
                chanVals[static_cast<size_t>(c)][static_cast<size_t>(j)] = v;
            }
        }
    } else {
        return false;
    }

    struct SkcBoneChan {
        std::string name;
        float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
        float px = 0.0f, py = 0.0f, pz = 0.0f;
        bool hasPos = false;
        bool hasRot = false;
    };
    std::vector<SkcBoneChan> bones;
    std::unordered_map<std::string, size_t> boneMap;

    for (int32_t c = 0; c < numChannels; ++c) {
        const uint8_t* np = data + static_cast<size_t>(ofsChannelNames) + static_cast<size_t>(c) * 32u;
        size_t nLen = 0;
        while (nLen < 32u && np[nLen] != 0) ++nLen;
        if (nLen == 0) return false;
        std::string fullChan = std::string(ReaderText::TrimSv(std::string_view(reinterpret_cast<const char*>(np), nLen)));
        std::string lowChan  = ReaderText::ToLowerAscii(fullChan);

        bool isPos = false;
        bool isRot = false;
        std::string boneKey = lowChan;
        if (lowChan.size() > 4 && lowChan.compare(lowChan.size() - 4, 4, " pos") == 0) {
            isPos = true;
            boneKey = lowChan.substr(0, lowChan.size() - 4);
        } else if (lowChan.size() > 4 && lowChan.compare(lowChan.size() - 4, 4, " rot") == 0) {
            isRot = true;
            boneKey = lowChan.substr(0, lowChan.size() - 4);
        } else if (lowChan.size() > 4 && lowChan.compare(lowChan.size() - 4, 4, "_pos") == 0) {
            isPos = true;
            boneKey = lowChan.substr(0, lowChan.size() - 4);
        } else if (lowChan.size() > 4 && lowChan.compare(lowChan.size() - 4, 4, "_rot") == 0) {
            isRot = true;
            boneKey = lowChan.substr(0, lowChan.size() - 4);
        } else {
            isPos = true;
        }
        boneKey = std::string(ReaderText::TrimSv(boneKey));
        if (boneKey.empty()) return false;

        size_t bIdx = 0;
        auto it = boneMap.find(boneKey);
        if (it == boneMap.end()) {
            bIdx = bones.size();
            SkcBoneChan bc;
            bc.name = boneKey;
            bones.push_back(std::move(bc));
            boneMap.emplace(boneKey, bIdx);
        } else {
            bIdx = it->second;
        }

        const auto& cv = chanVals[static_cast<size_t>(c)];
        if (isPos) {
            bones[bIdx].px = cv[0];
            bones[bIdx].py = cv[1];
            bones[bIdx].pz = cv[2];
            bones[bIdx].hasPos = true;
        }
        if (isRot) {
            bones[bIdx].qx = cv[0];
            bones[bIdx].qy = cv[1];
            bones[bIdx].qz = cv[2];
            bones[bIdx].qw = cv[3];
            bones[bIdx].hasRot = true;
        }
    }

    if (bones.empty()) return false;
    mesh.animFrameCount = static_cast<size_t>(numFrames);

    // If a companion <stem>.skd skeletal mesh exists alongside this .skc animation, pose & skin it directly!
    if (!fullPath.empty() && fullPath.size() > 4) {
        std::wstring skdPath = fullPath.substr(0, fullPath.size() - 4) + L".skd";
        std::vector<uint8_t> skdBytes;
        if (PathUtils::ReadFileBytes(skdPath, 64 * 1024 * 1024, skdBytes) &&
            skdBytes.size() >= 148 && std::memcmp(skdBytes.data(), "SKMD", 4) == 0)
        {
            const int32_t skdVer   = ByteIO::RdI32LE(skdBytes.data() + 4);
            const int32_t skdBones = ByteIO::RdI32LE(skdBytes.data() + 76);
            const int32_t ofsBones = ByteIO::RdI32LE(skdBytes.data() + 80);
            const int32_t ofsEnd   = ByteIO::RdI32LE(skdBytes.data() + 88);
            const size_t minHdr    = (skdVer == 6) ? 152u : 148u;
            if ((skdVer == 5 || skdVer == 6) && skdBones >= 1 && skdBones <= 512 &&
                ofsBones >= static_cast<int32_t>(minHdr) && ofsEnd > ofsBones &&
                static_cast<size_t>(ofsEnd) <= skdBytes.size())
            {
                auto readFixedStr32 = [](const uint8_t* p) -> std::string {
                    size_t len = 0;
                    while (len < 32u && p[len] != 0) ++len;
                    return std::string(ReaderText::TrimSv(ReaderText::ToLowerAscii(
                        std::string_view(reinterpret_cast<const char*>(p), len))));
                };

                std::vector<std::string> skdNames(static_cast<size_t>(skdBones));
                std::vector<std::string> skdParentNames(static_cast<size_t>(skdBones));
                std::unordered_map<std::string, int32_t> skdNameToIdx;
                std::vector<UberToolsSkel::BoneMat3x4> skdLocal(static_cast<size_t>(skdBones));

                size_t bPos = static_cast<size_t>(ofsBones);
                bool okBones = true;
                for (int32_t b = 0; b < skdBones; ++b) {
                    if (bPos + 84u > static_cast<size_t>(ofsEnd)) { okBones = false; break; }
                    std::string bName = readFixedStr32(skdBytes.data() + bPos + 0u);
                    std::string pName = readFixedStr32(skdBytes.data() + bPos + 32u);
                    int32_t ofsBase = ByteIO::RdI32LE(skdBytes.data() + bPos + 68u);
                    int32_t boneLen = ByteIO::RdI32LE(skdBytes.data() + bPos + 80u);
                    if (boneLen < 84 || bPos + static_cast<size_t>(boneLen) > static_cast<size_t>(ofsEnd) || bName.empty()) {
                        okBones = false;
                        break;
                    }
                    skdNames[static_cast<size_t>(b)]       = bName;
                    skdParentNames[static_cast<size_t>(b)] = pName;
                    skdNameToIdx.emplace(bName, b);

                    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
                    if (ofsBase >= 84 && static_cast<size_t>(ofsBase) + 12u <= static_cast<size_t>(boneLen)) {
                        tx = ByteIO::RdF32LE(skdBytes.data() + bPos + static_cast<size_t>(ofsBase) + 0u);
                        ty = ByteIO::RdF32LE(skdBytes.data() + bPos + static_cast<size_t>(ofsBase) + 4u);
                        tz = ByteIO::RdF32LE(skdBytes.data() + bPos + static_cast<size_t>(ofsBase) + 8u);
                    }
                    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
                    auto animIt = boneMap.find(bName);
                    if (animIt != boneMap.end()) {
                        const auto& ab = bones[animIt->second];
                        if (ab.hasPos) { tx = ab.px; ty = ab.py; tz = ab.pz; }
                        if (ab.hasRot) { qx = ab.qx; qy = ab.qy; qz = ab.qz; qw = ab.qw; }
                    }
                    skdLocal[static_cast<size_t>(b)] = UberToolsSkel::MakeBoneQuatPos(qx, qy, qz, qw, tx, ty, tz);
                    bPos += static_cast<size_t>(boneLen);
                }

                if (okBones) {
                    std::vector<int32_t> skdParents(static_cast<size_t>(skdBones), -1);
                    for (int32_t b = 0; b < skdBones; ++b) {
                        const std::string& pn = skdParentNames[static_cast<size_t>(b)];
                        if (!pn.empty() && pn != "worldbone") {
                            auto pit = skdNameToIdx.find(pn);
                            if (pit == skdNameToIdx.end() || pit->second == b) { okBones = false; break; }
                            skdParents[static_cast<size_t>(b)] = pit->second;
                        }
                    }
                    std::vector<UberToolsSkel::BoneMat3x4> skdWorld;
                    if (okBones && UberToolsSkel::ComputeForwardKinematics(skdParents, skdLocal, skdWorld) &&
                        ParseMohaaSkdWithBones(skdBytes.data(), skdBytes.size(), &skdWorld, mesh))
                    {
                        mesh.animFrameCount = static_cast<size_t>(numFrames);
                        mesh.formatName = (version == 14)
                            ? L"MOHAA Skinned Skeletal Animation (SKC v14 + SKD)"
                            : L"MOHAA Skinned Skeletal Animation (SKC v13 + SKD)";
                        return true;
                    }
                }
            }
        }
    }

    // Build standalone bone chain (root = 0, hierarchical parent chain if not paired with .skd)
    std::vector<int32_t> parents(bones.size(), -1);
    std::vector<UberToolsSkel::BoneMat3x4> localPoses(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        const auto& b = bones[i];
        localPoses[i] = UberToolsSkel::MakeBoneQuatPos(b.qx, b.qy, b.qz, b.qw, b.px, b.py, b.pz);
        if (i > 0) {
            // Connect to root (0) if bone has absolute/pelvis-relative offset, or previous bone along a limb chain
            int32_t par = 0;
            for (size_t j = i; j > 0; --j) {
                const std::string& cand = bones[j - 1].name;
                if (b.name.size() > cand.size() && b.name.compare(0, cand.size(), cand) == 0) {
                    par = static_cast<int32_t>(j - 1);
                    break;
                }
            }
            parents[i] = par;
        }
    }

    std::vector<UberToolsSkel::BoneMat3x4> worldBones;
    if (!UberToolsSkel::ComputeForwardKinematics(parents, localPoses, worldBones)) return false;

    if (!UberToolsSkel::EmitSkeletonPoseMesh(parents, worldBones, mesh)) return false;
    mesh.formatName = (version == 14)
        ? L"MOHAA Skeletal Animation (SKC v14)"
        : L"MOHAA Skeletal Animation (SKC v13)";
    (void)fullPath;
    return true;
}

} // namespace Formats
