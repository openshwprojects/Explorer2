#pragma once
// Lz4Block.h — the one raw LZ4 block decoder (LZ4 block format: token, literals, 16-bit offset, match).
// Used by the .lz4 frame reader, USD crate / VTK XML / Parquet / SquashFS-style containers.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Explorer2::Lz4 {

// Appends the decoded bytes of one raw LZ4 block to `out`.
//   historyStart  lowest index of `out` a match may copy from: out.size() before the call for an
//                 independent block, the start of the frame output for linked blocks.
//   maxOut        hard cap on out.size().
//   stopWhenFull  stop (successfully) as soon as out.size() reaches maxOut and ignore the rest of the
//                 input; for containers that store the exact decoded size and pad compressed blocks.
// Returns false for truncated or malformed input, a match that reaches before historyStart, or output
// that would grow past maxOut. Bytes decoded before the failure stay in `out`.
inline bool DecodeBlockAppend(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out,
                              size_t historyStart, size_t maxOut, bool stopWhenFull = false) {
    if (!src && srcLen != 0) return false;
    if (historyStart > out.size()) return false;
    size_t p = 0;
    while (p < srcLen && !(stopWhenFull && out.size() >= maxOut)) {
        const uint8_t token = src[p++];

        size_t litLen = token >> 4;
        if (litLen == 15u) {
            uint8_t s = 0;
            do {
                if (p >= srcLen) return false;
                s = src[p++];
                litLen += s;
                if (litLen > maxOut) return false;
            } while (s == 255u);
        }
        if (litLen > srcLen - p || litLen > maxOut - out.size()) return false;
        out.insert(out.end(), src + p, src + p + litLen);
        p += litLen;

        // The last sequence of a block is literals only
        if (p == srcLen || (stopWhenFull && out.size() >= maxOut)) break;
        if (srcLen - p < 2) return false;
        const size_t offset = static_cast<size_t>(src[p]) | (static_cast<size_t>(src[p + 1]) << 8);
        p += 2;
        if (offset == 0 || offset > out.size() - historyStart) return false;

        size_t matchLen = (token & 0x0Fu) + 4u;
        if ((token & 0x0Fu) == 15u) {
            uint8_t s = 0;
            do {
                if (p >= srcLen) return false;
                s = src[p++];
                matchLen += s;
                if (matchLen > maxOut) return false;
            } while (s == 255u);
        }
        if (matchLen > maxOut - out.size()) return false;

        const size_t base = out.size();
        out.resize(base + matchLen);
        uint8_t* d = out.data();
        const size_t from = base - offset;
        if (offset >= matchLen) {
            std::memcpy(d + base, d + from, matchLen);
        } else {
            for (size_t k = 0; k < matchLen; ++k) d[base + k] = d[from + k]; // overlapping run
        }
    }
    return true;
}

// Decodes an independent block whose decoded size is known exactly; `out` is replaced.
inline bool DecodeBlockExact(const uint8_t* src, size_t srcLen, size_t decodedLen, std::vector<uint8_t>& out) {
    out.clear();
    // LZ4 cannot expand more than ~255:1, so a damaged size field cannot force a huge reservation
    out.reserve(decodedLen < srcLen * 255u + 64u ? decodedLen : srcLen * 255u + 64u);
    return DecodeBlockAppend(src, srcLen, out, 0, decodedLen, true) && out.size() == decodedLen;
}

} // namespace Explorer2::Lz4
