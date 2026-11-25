#pragma once
// QuakeMipTexCommon.h — Shared DRY mip-chain & printable-name validator for Quake II / Raven / Ritual
// 16-level mipmap texture headers (.m8, .m32, .swl, etc.).

namespace Formats::QuakeMipCommon {

inline bool Validate16LevelMipChain(
    const uint8_t* data,
    size_t fileSize,
    size_t headerBytes,
    size_t widthArrOff,
    size_t heightArrOff,
    size_t offsetsArrOff,
    uint32_t bytesPerTexel,
    uint32_t& outW0,
    uint32_t& outH0,
    uint32_t& outOff0)
{
    if (!data || fileSize <= headerBytes || bytesPerTexel == 0u) return false;

    const uint32_t w0   = ByteIO::RdU32LE(data + widthArrOff);
    const uint32_t h0   = ByteIO::RdU32LE(data + heightArrOff);
    const uint32_t off0 = ByteIO::RdU32LE(data + offsetsArrOff);
    if (w0 == 0u || h0 == 0u || w0 > 4096u || h0 > 4096u || off0 < headerBytes) {
        return false;
    }
    const uint64_t mip0Bytes = static_cast<uint64_t>(w0) * static_cast<uint64_t>(h0) * bytesPerTexel;
    if (static_cast<uint64_t>(off0) + mip0Bytes > fileSize) return false;

    for (size_t m = 1u; m < 16u; ++m) {
        const uint32_t wm   = ByteIO::RdU32LE(data + widthArrOff   + m * 4u);
        const uint32_t hm   = ByteIO::RdU32LE(data + heightArrOff  + m * 4u);
        const uint32_t offm = ByteIO::RdU32LE(data + offsetsArrOff + m * 4u);
        if (offm != 0u && wm != 0u && hm != 0u) {
            const uint64_t mipBytes = static_cast<uint64_t>(wm) * static_cast<uint64_t>(hm) * bytesPerTexel;
            if (offm < headerBytes || static_cast<uint64_t>(offm) + mipBytes > fileSize) {
                return false;
            }
        }
    }

    outW0   = w0;
    outH0   = h0;
    outOff0 = off0;
    return true;
}

} // namespace Formats::QuakeMipCommon
