#pragma once
// RenderWareCommon.h — Shared RenderWare 3.x binary chunk stream primitives.
// Included inside namespace Explorer2.
namespace Formats {
namespace RwStreamDetail {

inline uint16_t RwU16(const uint8_t* p) {
    uint16_t v = 0;
    std::memcpy(&v, p, 2);
    return v;
}

inline uint32_t RwU32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

inline int32_t RwI32(const uint8_t* p) {
    int32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

inline float RwF32(const uint8_t* p) {
    float v = 0.0f;
    std::memcpy(&v, p, 4);
    return v;
}

inline uint32_t UnpackRwLibraryVersion(uint32_t stamp) {
    if ((stamp & 0xFFFF0000u) == 0u) {
        return stamp << 8; // Old unpacked RW 3.0/3.1 format (e.g. 0x0310 -> 0x31000)
    }
    return (((stamp >> 14) & 0x3FF00u) + 0x30000u) | ((stamp >> 16) & 0x3Fu);
}

inline bool IsPlausibleRwVersion(uint32_t stamp) {
    uint32_t ver = UnpackRwLibraryVersion(stamp);
    return ver >= 0x30000u && ver <= 0x39FFF;
}

struct RwChunkHdr {
    uint32_t type = 0;
    uint32_t size = 0;
    uint32_t stamp = 0;
    size_t payloadOffset = 0;
    size_t endOffset = 0;
};

inline bool ReadRwChunk(const uint8_t* data, size_t limit, size_t& pos, RwChunkHdr& out) {
    if (!data || pos + 12 > limit) return false;
    out.type  = RwU32(data + pos + 0);
    out.size  = RwU32(data + pos + 4);
    out.stamp = RwU32(data + pos + 8);
    if (!IsPlausibleRwVersion(out.stamp)) return false;
    if (out.size > limit - (pos + 12)) return false;
    out.payloadOffset = pos + 12;
    out.endOffset = out.payloadOffset + out.size;
    pos = out.endOffset;
    return true;
}

} // namespace RwStreamDetail
} // namespace Formats
