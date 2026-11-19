#pragma once
#include "../../ByteIO.h"
#include "../common/MeshBuild3D.h"
// C3dMocap.h — Zero-dependency C++20 C3D Biomechanical 3D Motion Capture (.c3d) reader.

namespace Formats {

namespace DetailC3d {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdI16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdF32LE;
using ::Explorer2::ByteIO::RdF64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdI16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdI32BE;
using ::Explorer2::ByteIO::RdU64BE;
using ::Explorer2::ByteIO::RdF32BE;
using ::Explorer2::ByteIO::RdF64BE;

inline std::string_view TrimSv(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

inline std::string ToLowerStr(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

inline std::string ToUpperStr(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

inline bool NextTokenSv(std::string_view& line, std::string_view& outTok) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r')) line.remove_prefix(1);
    if (line.empty()) return false;
    size_t i = 0;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
    outTok = line.substr(0, i);
    line.remove_prefix(i);
    return true;
}

inline bool ParseFloatSv(std::string_view tok, float& outVal) {
    tok = TrimSv(tok);
    if (tok.empty()) return false;
    if (tok.front() == '+') tok.remove_prefix(1);
    if (tok.empty()) return false;
    float v = 0.0f;
    auto res = std::from_chars(tok.data(), tok.data() + tok.size(), v);
    if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size() || !std::isfinite(v)) return false;
    outVal = v;
    return true;
}

inline bool ParseDoubleSv(std::string_view tok, double& outVal) {
    tok = TrimSv(tok);
    if (tok.empty()) return false;
    if (tok.front() == '+') tok.remove_prefix(1);
    if (tok.empty()) return false;
    double v = 0.0;
    auto res = std::from_chars(tok.data(), tok.data() + tok.size(), v);
    if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size() || !std::isfinite(v)) return false;
    outVal = v;
    return true;
}

inline bool ParseIntSv(std::string_view tok, int& outVal) {
    tok = TrimSv(tok);
    if (tok.empty()) return false;
    if (tok.front() == '+') tok.remove_prefix(1);
    if (tok.size() >= 3 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        uint32_t u = 0;
        auto res = std::from_chars(tok.data() + 2, tok.data() + tok.size(), u, 16);
        if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size()) return false;
        outVal = static_cast<int>(u);
        return true;
    }
    int v = 0;
    auto res = std::from_chars(tok.data(), tok.data() + tok.size(), v, 10);
    if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size()) return false;
    outVal = v;
    return true;
}

inline bool IsSafeRelativeSubFile(std::string_view rel) {
    if (rel.empty() || rel.size() > 260) return false;
    if (rel.front() == '/' || rel.front() == '\\') return false;
    if (rel.find(':') != std::string_view::npos) return false;
    if (rel.find("..") != std::string_view::npos) return false;
    for (char c : rel) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32u) return false;
    }
    return true;
}

inline float DotVec3(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3f SubVec3(const Vec3f& a, const Vec3f& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}


inline float RdVaxF32(const uint8_t* p) {
    uint16_t w0 = RdU16LE(p + 0);
    uint16_t w1 = RdU16LE(p + 2);
    uint32_t expVax = (w0 >> 7u) & 0xFFu;
    if (expVax == 0u) return 0.0f;
    uint32_t sign = (static_cast<uint32_t>(w0) & 0x8000u) << 16u;
    uint32_t mant = (static_cast<uint32_t>(w0 & 0x7Fu) << 16u) | static_cast<uint32_t>(w1);
    if (expVax <= 2u) {
        // Subnormal IEEE-754
        uint32_t fullMant = (1u << 23u) | mant;
        uint32_t shift = 3u - expVax;
        uint32_t bits = sign | (fullMant >> shift);
        float f = 0.0f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    uint32_t bits = sign | ((expVax - 2u) << 23u) | mant;
    float f = 0.0f;
    std::memcpy(&f, &bits, 4);
    return f;
}

inline uint16_t RdC3dU16(const uint8_t* p, uint8_t procType) {
    return (procType == 86u) ? RdU16BE(p) : RdU16LE(p);
}

inline int16_t RdC3dI16(const uint8_t* p, uint8_t procType) {
    return static_cast<int16_t>(RdC3dU16(p, procType));
}

inline int32_t RdC3dI32(const uint8_t* p, uint8_t procType) {
    return (procType == 86u) ? RdI32BE(p) : RdI32LE(p);
}

inline float RdC3dF32(const uint8_t* p, uint8_t procType) {
    if (procType == 85u) return RdVaxF32(p);
    return (procType == 86u) ? RdF32BE(p) : RdF32LE(p);
}

inline std::string CleanC3dMarkerLabel(std::string_view raw) {
    raw = TrimSv(raw);
    // Strip optional subject prefix (e.g. "Subj01:LSHO" -> "LSHO")
    size_t colon = raw.find_last_of(':');
    if (colon != std::string_view::npos && colon + 1u < raw.size()) {
        raw = raw.substr(colon + 1u);
    }
    return ToUpperStr(TrimSv(raw));
}

} // namespace DetailC3d

inline bool ParseC3dMocapMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace DetailC3d;
    if (!data || size < 1024u || data[1] != 0x50u || data[0] < 2u) return false;

    size_t paramSector = static_cast<size_t>(data[0]);
    size_t paramByteOff = (paramSector - 1u) * 512u;
    if (paramByteOff + 4u > size) return false;

    uint8_t numParamBlocks = data[paramByteOff + 2u];
    uint8_t procType       = data[paramByteOff + 3u]; // 84=Intel LE, 85=DEC VAX, 86=MIPS/SGI BE
    if (procType < 84u || procType > 86u) return false;

    // 512-byte C3D Header (1-based 16-bit words -> byte offset = (word - 1) * 2)
    uint32_t numPoints       = RdC3dU16(data + 2u, procType);  // word 2
    uint32_t analogPerFrame  = RdC3dU16(data + 4u, procType);  // word 3
    uint32_t firstFrame      = RdC3dU16(data + 6u, procType);  // word 4
    uint32_t lastFrame       = RdC3dU16(data + 8u, procType);  // word 5
    float    scaleFactor     = RdC3dF32(data + 12u, procType); // words 7..8
    uint32_t dataStartSector = RdC3dU16(data + 16u, procType); // word 9
    float    frameRate       = RdC3dF32(data + 20u, procType); // words 11..12
    (void)frameRate;

    uint32_t declaredFrames = (lastFrame >= firstFrame && firstFrame > 0u) ? (lastFrame - firstFrame + 1u) : 1u;

    // Walk Parameter Section groups & parameters
    std::unordered_map<int, std::string> groupNames;
    std::vector<std::string> pointLabels;
    std::vector<std::string> pointLabels2;

    size_t paramEnd = std::min(size, paramByteOff + std::max<size_t>(1u, numParamBlocks) * 512u);
    size_t pPos = paramByteOff + 4u;
    for (int guard = 0; guard < 8192 && pPos + 4u <= paramEnd; ++guard) {
        int8_t rawNameLen = static_cast<int8_t>(data[pPos]);
        int8_t rawId      = static_cast<int8_t>(data[pPos + 1u]);
        size_t nameLen    = static_cast<size_t>(std::abs(static_cast<int>(rawNameLen)));
        if (nameLen == 0u || rawId == 0 || pPos + 2u + nameLen + 2u > paramEnd) break;

        std::string recName = ToUpperStr(std::string_view(reinterpret_cast<const char*>(data + pPos + 2u), nameLen));
        size_t nextPtrPos = pPos + 2u + nameLen;
        uint16_t nextOff  = RdC3dU16(data + nextPtrPos, procType);
        size_t bodyPos    = nextPtrPos + 2u;

        if (rawId < 0) {
            groupNames[-static_cast<int>(rawId)] = recName;
        } else {
            auto itGrp = groupNames.find(static_cast<int>(rawId));
            std::string grp = (itGrp != groupNames.end()) ? itGrp->second : std::string{};
            if (bodyPos + 2u <= paramEnd) {
                int8_t elemSz   = static_cast<int8_t>(data[bodyPos]);
                uint8_t numDims = data[bodyPos + 1u];
                size_t dimPos   = bodyPos + 2u;
                if (numDims <= 7u && dimPos + numDims <= paramEnd && elemSz != 0) {
                    size_t elemBytes = static_cast<size_t>(std::abs(static_cast<int>(elemSz)));
                    size_t totalElems = 1u;
                    uint8_t dims[7] = {};
                    bool overflow = false;
                    for (uint8_t d = 0; d < numDims; ++d) {
                        dims[d] = data[dimPos + d];
                        if (dims[d] == 0u) { totalElems = 0u; }
                        else if (totalElems > 16000000u / dims[d]) { overflow = true; }
                        else { totalElems *= dims[d]; }
                    }
                    size_t valPos = dimPos + numDims;
                    if (!overflow && elemBytes <= 8u && valPos + totalElems * elemBytes <= paramEnd && grp == "POINT") {
                        const uint8_t* vp = data + valPos;
                        if (recName == "USED" && totalElems >= 1u && elemBytes >= 2u) {
                            int32_t u = (elemBytes == 4u) ? RdC3dI32(vp, procType) : RdC3dU16(vp, procType);
                            if (u > 0 && u <= 16384) numPoints = static_cast<uint32_t>(u);
                        } else if (recName == "SCALE" && totalElems >= 1u && elemSz == 4) {
                            float sc = RdC3dF32(vp, procType);
                            if (std::isfinite(sc) && std::fabs(sc) > 1e-12f) scaleFactor = sc;
                        } else if (recName == "DATA_START" && totalElems >= 1u && elemBytes >= 2u) {
                            int32_t ds = (elemBytes == 4u) ? RdC3dI32(vp, procType) : RdC3dU16(vp, procType);
                            if (ds >= 2) dataStartSector = static_cast<uint32_t>(ds);
                        } else if ((recName == "FRAMES" || recName == "LONG_FRAMES") && totalElems >= 1u) {
                            int32_t fr = 0;
                            if (elemSz == 4) fr = static_cast<int32_t>( std::fabs(RdC3dF32(vp, procType)) > 0.5f &&
                                                  (RdC3dI32(vp, procType) < 0 || RdC3dI32(vp, procType) > 10000000)
                                                  ? std::lround(RdC3dF32(vp, procType)) : RdC3dI32(vp, procType) );
                            else if (elemBytes == 2u) fr = RdC3dU16(vp, procType);
                            if (fr > 0 && fr <= 10000000) declaredFrames = static_cast<uint32_t>(fr);
                        } else if ((recName == "LABELS" || recName == "LABELS2") && elemSz == -1 && numDims >= 1u) {
                            size_t strLen = dims[0];
                            size_t strCnt = (numDims >= 2u) ? (totalElems / std::max<size_t>(1u, strLen)) : 1u;
                            auto& dst = (recName == "LABELS") ? pointLabels : pointLabels2;
                            if (strLen > 0u) {
                                dst.clear();
                                for (size_t s = 0; s < strCnt; ++s) {
                                    std::string_view svLbl(reinterpret_cast<const char*>(vp + s * strLen), strLen);
                                    dst.push_back(CleanC3dMarkerLabel(svLbl));
                                }
                            }
                        }
                    }
                }
            }
        }

        if (nextOff == 0u || nextPtrPos + nextOff <= pPos || nextPtrPos + nextOff > paramEnd) break;
        pPos = nextPtrPos + nextOff;
    }

    if (!pointLabels2.empty()) {
        pointLabels.insert(pointLabels.end(), pointLabels2.begin(), pointLabels2.end());
    }

    if (numPoints == 0u || numPoints > 16384u || dataStartSector < 2u ||
        !std::isfinite(scaleFactor) || std::fabs(scaleFactor) < 1e-12f)
    {
        return false;
    }

    size_t dataOff = static_cast<size_t>(dataStartSector - 1u) * 512u;
    if (dataOff >= size) return false;

    const bool isFloat = (scaleFactor < 0.0f);
    const size_t ptBytes = isFloat ? 16u : 8u;
    const size_t anBytes = isFloat ? 4u : 2u;
    const size_t frameStride = static_cast<size_t>(numPoints) * ptBytes + static_cast<size_t>(analogPerFrame) * anBytes;
    if (frameStride == 0u || size - dataOff < static_cast<size_t>(numPoints) * ptBytes) return false;

    size_t maxFramesInBuf = (size - dataOff) / frameStride;
    if (maxFramesInBuf == 0u) maxFramesInBuf = 1u;
    size_t actualFrames = std::min<size_t>(maxFramesInBuf, std::max<size_t>(1u, declaredFrames));

    struct MarkerSample {
        Vec3f pos = {};
        bool valid = false;
    };

    std::vector<MarkerSample> frameMarkers;
    size_t chosenFrameValid = 0u;

    // Decode first frame containing valid (non-occluded) 3D markers (typically frame 0)
    size_t scanLimit = std::min<size_t>(actualFrames, 256u);
    for (size_t f = 0; f < scanLimit; ++f) {
        size_t fOff = dataOff + f * frameStride;
        if (fOff + static_cast<size_t>(numPoints) * ptBytes > size) break;

        std::vector<MarkerSample> cur(numPoints);
        size_t validCnt = 0u;
        for (uint32_t m = 0; m < numPoints; ++m) {
            const uint8_t* mp = data + fOff + static_cast<size_t>(m) * ptBytes;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            bool occluded = false;
            if (isFloat) {
                x = RdC3dF32(mp + 0u, procType);
                y = RdC3dF32(mp + 4u, procType);
                z = RdC3dF32(mp + 8u, procType);
                float camRes = RdC3dF32(mp + 12u, procType);
                occluded = !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                           !std::isfinite(camRes) || (camRes < 0.0f);
            } else {
                int16_t ix = RdC3dI16(mp + 0u, procType);
                int16_t iy = RdC3dI16(mp + 2u, procType);
                int16_t iz = RdC3dI16(mp + 4u, procType);
                int16_t iw4 = RdC3dI16(mp + 6u, procType);
                occluded = (iw4 < 0);
                x = static_cast<float>(ix) * scaleFactor;
                y = static_cast<float>(iy) * scaleFactor;
                z = static_cast<float>(iz) * scaleFactor;
            }
            if (!occluded && std::fabs(x) < 1e8f && std::fabs(y) < 1e8f && std::fabs(z) < 1e8f) {
                // Convert biomechanical Z-up (x, y, z) -> Explorer2 Y-up (x, z, -y)
                cur[m].pos = { x, z, -y };
                cur[m].valid = true;
                ++validCnt;
            }
        }
        if (validCnt > chosenFrameValid) {
            chosenFrameValid = validCnt;
            frameMarkers = std::move(cur);
            if (f == 0u && validCnt >= 1u) break; // Prefer frame 0 whenever valid!
        }
    }

    if (chosenFrameValid == 0u) return false;

    mesh.vertices.clear();
    mesh.tris.clear();
    mesh.animFrameCount = std::max<size_t>(1u, actualFrames);

    Vec3f bmin = {}, bmax = {};
    bool first = true;
    for (const auto& ms : frameMarkers) {
        if (!ms.valid) continue;
        if (first) { bmin = bmax = ms.pos; first = false; }
        else {
            bmin.x = std::min(bmin.x, ms.pos.x); bmin.y = std::min(bmin.y, ms.pos.y); bmin.z = std::min(bmin.z, ms.pos.z);
            bmax.x = std::max(bmax.x, ms.pos.x); bmax.y = std::max(bmax.y, ms.pos.y); bmax.z = std::max(bmax.z, ms.pos.z);
        }
    }
    float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 1.0f });
    float markerRad = std::max(span * 0.022f, 1e-3f);
    float boneRad   = markerRad * 0.38f;

    // 1. Emit 3D marker octahedron at each valid non-occluded marker
    for (const auto& ms : frameMarkers) {
        if (ms.valid) MeshBuild3D::EmitOctahedron(mesh, ms.pos, markerRad);
    }

    // 2. Connect Plug-in Gait / Vicon anatomical marker pairs when POINT:LABELS match
    std::unordered_map<std::string, size_t> labelToIdx;
    for (size_t i = 0; i < pointLabels.size() && i < frameMarkers.size(); ++i) {
        if (!pointLabels[i].empty() && frameMarkers[i].valid) {
            labelToIdx[pointLabels[i]] = i;
        }
    }

    static const std::pair<const char*, const char*> kAnatomicalPairs[] = {
        {"LFHD", "RFHD"}, {"LBHD", "RBHD"}, {"LFHD", "LBHD"}, {"RFHD", "RBHD"},
        {"LFHD", "C7"},   {"RFHD", "C7"},   {"C7", "CLAV"},   {"C7", "T10"},
        {"CLAV", "STRN"}, {"T10", "STRN"},  {"T10", "SACR"},  {"T10", "LPSI"}, {"T10", "RPSI"},
        {"CLAV", "LSHO"}, {"C7", "LSHO"},   {"LSHO", "LELB"}, {"LELB", "LWRA"}, {"LELB", "LWRB"},
        {"LWRA", "LWRB"}, {"LWRA", "LFIN"}, {"LSHO", "LUPA"}, {"LELB", "LFRA"},
        {"CLAV", "RSHO"}, {"C7", "RSHO"},   {"RSHO", "RELB"}, {"RELB", "RWRA"}, {"RELB", "RWRB"},
        {"RWRA", "RWRB"}, {"RWRA", "RFIN"}, {"RSHO", "RUPA"}, {"RELB", "RFRA"},
        {"LASI", "RASI"}, {"LASI", "LPSI"}, {"RASI", "RPSI"}, {"LPSI", "RPSI"},
        {"LASI", "SACR"}, {"RASI", "SACR"}, {"CLAV", "LASI"}, {"CLAV", "RASI"},
        {"LASI", "LKNE"}, {"LKNE", "LANK"}, {"LANK", "LHEE"}, {"LANK", "LTOE"}, {"LHEE", "LTOE"},
        {"RASI", "RKNE"}, {"RKNE", "RANK"}, {"RANK", "RHEE"}, {"RANK", "RTOE"}, {"RHEE", "RTOE"}
    };

    size_t bonesAdded = 0u;
    for (const auto& pr : kAnatomicalPairs) {
        auto itA = labelToIdx.find(pr.first);
        auto itB = labelToIdx.find(pr.second);
        if (itA != labelToIdx.end() && itB != labelToIdx.end() && itA->second != itB->second) {
            MeshBuild3D::EmitEdgeStrut(mesh, frameMarkers[itA->second].pos, frameMarkers[itB->second].pos, boneRad);
            ++bonesAdded;
        }
    }

    // Fallback sequential marker struts within reasonable anatomical distance if unlabeled
    if (bonesAdded == 0u && chosenFrameValid >= 2u) {
        float maxDist2 = (span * 0.55f) * (span * 0.55f);
        int prevValid = -1;
        for (size_t i = 0; i < frameMarkers.size(); ++i) {
            if (!frameMarkers[i].valid) continue;
            if (prevValid >= 0) {
                Vec3f d = SubVec3(frameMarkers[i].pos, frameMarkers[static_cast<size_t>(prevValid)].pos);
                if (DotVec3(d, d) <= maxDist2) {
                    MeshBuild3D::EmitEdgeStrut(mesh, frameMarkers[static_cast<size_t>(prevValid)].pos,
                                               frameMarkers[i].pos, boneRad);
                }
            }
            prevValid = static_cast<int>(i);
        }
    }

    mesh.formatName = L"C3D Biomechanical Motion Capture (.c3d)";
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
