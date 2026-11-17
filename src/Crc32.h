#pragma once
// Crc32.h — table-driven CRC-32 checksums shared by readers, extractors and the self-tests.
//   Crc32   IEEE 802.3 / zlib / PNG / ZIP polynomial (reflected 0xEDB88320)
//   Crc32C  Castagnoli polynomial (reflected 0x82F63B78; iSCSI, ext4, E57, Snappy framing)
// Both take the previous result as `crc` to continue a running checksum (zlib crc32() convention):
//   uint32_t c = Crc32(a, na); c = Crc32(b, nb, c);  ==  Crc32(a+b)
#include <array>
#include <cstddef>
#include <cstdint>

namespace Explorer2::Checksum {

namespace Detail {
template <uint32_t Poly>
inline const std::array<uint32_t, 256>& ReflectedTable() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256u; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (Poly & (0u - (c & 1u)));
            t[i] = c;
        }
        return t;
    }();
    return table;
}

template <uint32_t Poly>
inline uint32_t Update(const uint8_t* p, size_t len, uint32_t crc) {
    const auto& t = ReflectedTable<Poly>();
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) crc = t[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}
} // namespace Detail

inline uint32_t Crc32(const uint8_t* p, size_t len, uint32_t crc = 0) {
    return Detail::Update<0xEDB88320u>(p, len, crc);
}

inline uint32_t Crc32C(const uint8_t* p, size_t len, uint32_t crc = 0) {
    return Detail::Update<0x82F63B78u>(p, len, crc);
}

} // namespace Explorer2::Checksum
