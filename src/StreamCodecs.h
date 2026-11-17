#pragma once
// StreamCodecs.h — in-process decompressors implemented in ArchiveExplorer.cpp.
// Every decoder clears `out` first; maxOut caps how much it may produce.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Explorer2::StreamCodecs {

// Snappy raw block (varint uncompressed length, then literal / copy elements). With wholeInput every input byte
// must belong to the block (callers that probe framing use it to tell a wrong start apart).
inline bool SnappyDecompress(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out,
                             size_t maxOut = 32 * 1024 * 1024, bool wholeInput = false) {
    out.clear();
    if (!src || srcLen == 0) return false;
    size_t pos = 0;
    uint64_t uncompLen = 0;
    int shift = 0;
    bool term = false;
    while (pos < srcLen && shift < 35) {
        uint8_t b = src[pos++];
        uncompLen |= static_cast<uint64_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0) { term = true; break; }
        shift += 7;
    }
    if (!term || uncompLen > maxOut) return false;
    out.reserve(static_cast<size_t>(uncompLen));
    auto rd = [&](size_t at, int bytes) {
        uint32_t v = 0;
        for (int i = 0; i < bytes; ++i) v |= static_cast<uint32_t>(src[at + static_cast<size_t>(i)]) << (8 * i);
        return v;
    };
    while (pos < srcLen && out.size() < uncompLen) {
        const uint8_t tag = src[pos++];
        const uint8_t elemType = tag & 0x03u;
        if (elemType == 0) {
            uint32_t len = tag >> 2;
            if (len >= 60) {
                const int extraBytes = static_cast<int>(len - 59);
                if (pos + static_cast<size_t>(extraBytes) > srcLen) return false;
                len = rd(pos, extraBytes);
                pos += static_cast<size_t>(extraBytes);
            }
            const size_t litLen = static_cast<size_t>(len) + 1;
            if (litLen > srcLen - pos || out.size() + litLen > uncompLen) return false;
            out.insert(out.end(), src + pos, src + pos + litLen);
            pos += litLen;
        } else {
            size_t copyLen = 0, copyOff = 0;
            if (elemType == 1) {
                if (pos + 1 > srcLen) return false;
                copyLen = ((tag >> 2) & 0x07u) + 4;
                copyOff = (static_cast<size_t>(tag & 0xE0u) << 3) | src[pos++];
            } else {
                const int offBytes = (elemType == 2) ? 2 : 4;
                if (pos + static_cast<size_t>(offBytes) > srcLen) return false;
                copyLen = static_cast<size_t>(tag >> 2) + 1;
                copyOff = rd(pos, offBytes);
                pos += static_cast<size_t>(offBytes);
            }
            if (copyOff == 0 || copyOff > out.size() || out.size() + copyLen > uncompLen) return false;
            for (size_t i = 0; i < copyLen; ++i) out.push_back(out[out.size() - copyOff]);
        }
    }
    return out.size() == uncompLen && (!wholeInput || pos == srcLen);
}

// LZMA1 raw stream (props byte + dictionary size come from the container)
bool DecompressLzma1(
    uint8_t propsByte,
    uint32_t dictSize,
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& out,
    uint64_t uncompressedLen,
    bool hasKnownSize = true,
    size_t maxOut = 128 * 1024 * 1024);

// LZMA2 chunk stream
bool DecompressLzma2(
    uint8_t dictPropByte,
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& out,
    size_t maxOut = 128 * 1024 * 1024,
    size_t* outBytesConsumed = nullptr,
    uint64_t expectedOut = 0);

// In-place XZ/7z filters
void ApplyBcjX86Decode(uint8_t* data, size_t size, uint32_t ip = 0);
void ApplyXzDeltaDecode(uint8_t* data, size_t size, size_t dist);

// Whole-stream containers
bool DecompressXzStream(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut = 128 * 1024 * 1024);
bool DecompressBzip2Stream(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut = 128 * 1024 * 1024);
bool DecompressLz4Frame(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut = 128 * 1024 * 1024);
bool DecompressZstdFrame(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut = 128 * 1024 * 1024);

} // namespace Explorer2::StreamCodecs
