#pragma once
// LzxDecoder.h — Microsoft LZX decompression as CAB and CHM use it (window 2^15 .. 2^21 bytes): verbatim,
// aligned-offset and uncompressed blocks, the three repeated offsets, the x86 E8 call translation, 32 KB frames with
// the bitstream realigned after each, and decoder resets every resetInterval frames (CHM), where the stream may be
// resynchronised from a table of frame offsets.
//
// The whole output is decoded into one buffer, which also serves as the LZX window, so matches are copied straight
// from it (a stream's matches never reach back past a reset point, nor further than the window).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Explorer2::Lzx {

class Decoder {
public:
    // in: the compressed stream; windowBits: 15..21; resetIntervalFrames: frames (32 KB of output) between decoder
    // resets, 0 for none; frameOffsets: compressed offset of each frame (CHM's reset table), used to resynchronise
    // at reset points, may be empty
    Decoder(const uint8_t* in, size_t inLen, int windowBits, uint32_t resetIntervalFrames,
            const std::vector<uint64_t>& frameOffsets)
        : m_in(in), m_inLen(inLen), m_windowBits(windowBits), m_resetFrames(resetIntervalFrames), m_frameOffsets(frameOffsets) {}

    // The first outLen bytes of the uncompressed stream, E8 translation undone; false on a damaged stream (out then
    // holds what was decoded before the damage)
    bool Decode(size_t outLen, std::vector<uint8_t>& out) {
        out.clear();
        if (m_windowBits < 15 || m_windowBits > 21 || outLen == 0) return outLen == 0;
        m_posSlots = (m_windowBits == 21) ? 50u : (m_windowBits == 20) ? 42u : static_cast<uint32_t>(m_windowBits) * 2u;
        m_mainSyms = 256u + m_posSlots * 8u;
        m_win.assign(outLen, 0);
        m_mainLen.assign(kMainMax, 0);
        m_lenLen.assign(kLenSyms, 0);
        ResetState();
        size_t pos = 0, frame = 0;
        bool ok = true;
        while (pos < outLen) {
            if (m_resetFrames && frame > 0 && frame % m_resetFrames == 0) {
                // A block that claims to run across a reset point ends there: help compilers write such lengths, and
                // the next interval starts afresh at its reset-table offset (as libmspack does)
                ResetState();
                if (frame < m_frameOffsets.size()) SeekByte(static_cast<size_t>(m_frameOffsets[frame]));
            }
            const size_t frameEnd = std::min(outLen, (frame + 1) * kFrame);
            if (!DecodeFrame(pos, frameEnd)) { ok = false; break; }
            // realign the bitstream to a 16-bit boundary after every frame
            if (m_bits > 0 && !Ensure(16)) { if (pos < outLen) { ok = false; break; } }
            if (m_bits & 15) Drop(m_bits & 15);
            ++frame;
        }
        out.assign(m_win.begin(), m_win.begin() + static_cast<std::ptrdiff_t>(pos));
        UndoE8(out);
        return ok && pos == outLen;
    }

private:
    static constexpr size_t kFrame = 32768;
    static constexpr uint32_t kMainMax = 256 + 50 * 8;
    static constexpr uint32_t kLenSyms = 249;
    static constexpr int kTableBits = 16;

    // ---- bit input: 16-bit little-endian words, consumed from the most significant bit ----
    bool Ensure(int need) {
        while (m_bits < need) {
            if (m_pos + 1 < m_inLen) {
                const uint32_t w = static_cast<uint32_t>(m_in[m_pos]) | (static_cast<uint32_t>(m_in[m_pos + 1]) << 8);
                m_pos += 2;
                m_buf |= static_cast<uint64_t>(w) << (48 - m_bits);
                m_bits += 16;
            } else {
                return false;
            }
        }
        return true;
    }
    uint32_t Peek(int n) const { return n ? static_cast<uint32_t>(m_buf >> (64 - n)) : 0u; }
    void Drop(int n) { m_buf <<= n; m_bits -= n; }
    bool Read(int n, uint32_t& v) {
        if (n == 0) { v = 0; return true; }
        if (n > 16) {
            uint32_t hi = 0, lo = 0;
            if (!Read(n - 16, hi) || !Read(16, lo)) return false;
            v = (hi << 16) | lo;
            return true;
        }
        if (!Ensure(n)) return false;
        v = Peek(n);
        Drop(n);
        return true;
    }
    void SeekByte(size_t pos) {
        m_pos = std::min(pos, m_inLen);
        m_buf = 0;
        m_bits = 0;
    }

    // ---- canonical Huffman tables: entry = (length << 11) | symbol (lengths reach 16, symbols stay under 2048),
    //      0xFFFF where no code ----
    static bool Build(const uint8_t* lens, uint32_t n, std::vector<uint16_t>& table, bool allowEmpty) {
        table.assign(size_t(1) << kTableBits, 0xFFFFu);
        uint32_t count[17] = {};
        uint32_t used = 0;
        for (uint32_t i = 0; i < n; ++i) {
            if (lens[i] > 16) return false;
            if (lens[i]) { ++count[lens[i]]; ++used; }
        }
        if (used == 0) return allowEmpty;
        uint32_t next[18] = {};
        uint32_t code = 0;
        for (int b = 1; b <= 16; ++b) {
            code = (code + count[b - 1]) << 1;
            next[b] = code;
        }
        // the code must not be over-subscribed
        uint64_t kraft = 0;
        for (int b = 1; b <= 16; ++b) kraft += static_cast<uint64_t>(count[b]) << (16 - b);
        if (kraft > (uint64_t(1) << 16)) return false;
        for (uint32_t s = 0; s < n; ++s) {
            const uint32_t l = lens[s];
            if (!l) continue;
            const uint32_t c = next[l]++;
            const uint32_t first = c << (kTableBits - l), span = 1u << (kTableBits - l);
            if (first + span > table.size()) return false;
            const uint16_t v = static_cast<uint16_t>((l << 11) | s);
            for (uint32_t k = 0; k < span; ++k) table[first + k] = v;
        }
        return true;
    }
    bool Sym(const std::vector<uint16_t>& table, uint32_t& sym) {
        Ensure(kTableBits); // at the stream's end fewer bits may remain: the code still has to fit in them
        if (m_bits <= 0) return false;
        const uint16_t e = table[Peek(kTableBits)];
        if (e == 0xFFFFu) return false;
        const int l = e >> 11;
        if (l > m_bits) return false;
        Drop(l);
        sym = e & 0x07FFu;
        return true;
    }

    // Code lengths [first, last) as deltas from the previous ones, through a 20-symbol pretree
    bool ReadLengths(std::vector<uint8_t>& lens, uint32_t first, uint32_t last) {
        uint8_t pre[20];
        for (int i = 0; i < 20; ++i) {
            uint32_t v = 0;
            if (!Read(4, v)) return false;
            pre[i] = static_cast<uint8_t>(v);
        }
        std::vector<uint16_t> preTable;
        if (!Build(pre, 20, preTable, false)) return false;
        uint32_t x = first;
        while (x < last) {
            uint32_t z = 0;
            if (!Sym(preTable, z)) return false;
            if (z == 17 || z == 18) {
                uint32_t run = 0;
                if (!Read(z == 17 ? 4 : 5, run)) return false;
                run += (z == 17) ? 4 : 20;
                while (run-- && x < last) lens[x++] = 0;
            } else if (z == 19) {
                uint32_t run = 0, z2 = 0;
                if (!Read(1, run) || !Sym(preTable, z2) || z2 > 16) return false;
                run += 4;
                const uint8_t v = static_cast<uint8_t>((lens[x] + 17 - z2) % 17);
                while (run-- && x < last) lens[x++] = v;
            } else {
                lens[x] = static_cast<uint8_t>((lens[x] + 17 - z) % 17);
                ++x;
            }
        }
        return true;
    }

    void ResetState() {
        m_r0 = m_r1 = m_r2 = 1;
        m_headerRead = false;
        m_blockRemaining = 0;
        m_blockType = 0;
        std::fill(m_mainLen.begin(), m_mainLen.end(), 0);
        std::fill(m_lenLen.begin(), m_lenLen.end(), 0);
    }

    bool ReadBlockHeader() {
        if (m_blockType == 3 && (m_blockLength & 1) && m_pos < m_inLen) ++m_pos; // padding after an odd stored block
        uint32_t type = 0, hi = 0, lo = 0;
        if (!Read(3, type) || !Read(16, hi) || !Read(8, lo)) return false;
        m_blockType = type;
        m_blockLength = m_blockRemaining = (hi << 8) | lo;
        if (m_blockLength == 0) return false;
        if (type == 2) { // aligned-offset: its own 8-symbol tree, then the verbatim trees
            uint8_t al[8];
            for (int i = 0; i < 8; ++i) {
                uint32_t v = 0;
                if (!Read(3, v)) return false;
                al[i] = static_cast<uint8_t>(v);
            }
            if (!Build(al, 8, m_alignTable, false)) return false;
        }
        if (type == 1 || type == 2) {
            if (!ReadLengths(m_mainLen, 0, 256) || !ReadLengths(m_mainLen, 256, m_mainSyms)) return false;
            if (!Build(m_mainLen.data(), m_mainSyms, m_mainTable, false)) return false;
            if (m_mainLen[0xE8]) m_intelStarted = true;
            if (!ReadLengths(m_lenLen, 0, kLenSyms)) return false;
            m_lenEmpty = std::all_of(m_lenLen.begin(), m_lenLen.end(), [](uint8_t v) { return v == 0; });
            if (!Build(m_lenLen.data(), kLenSyms, m_lenTable, true)) return false;
            return true;
        }
        if (type == 3) { // stored: byte-aligned R0..R2, then the bytes themselves
            m_intelStarted = true;
            if (m_bits == 0) Ensure(16); // 1..16 padding bits, never 0
            m_buf = 0;
            m_bits = 0;
            if (m_pos + 12 > m_inLen) return false;
            m_r0 = Le32(m_in + m_pos);
            m_r1 = Le32(m_in + m_pos + 4);
            m_r2 = Le32(m_in + m_pos + 8);
            m_pos += 12;
            return true;
        }
        return false;
    }

    // Decodes output bytes until pos reaches frameEnd (a match may run a little past it)
    bool DecodeFrame(size_t& pos, size_t frameEnd) {
        const size_t outLen = m_win.size();
        while (pos < frameEnd) {
            if (!m_headerRead) {
                uint32_t intel = 0;
                if (!Read(1, intel)) return false;
                m_intelSize = 0;
                if (intel) {
                    uint32_t hi = 0, lo = 0;
                    if (!Read(16, hi) || !Read(16, lo)) return false;
                    m_intelSize = static_cast<int32_t>((hi << 16) | lo);
                }
                m_headerRead = true;
            }
            if (m_blockRemaining == 0 && !ReadBlockHeader()) return false;
            if (m_blockType == 3) {
                const size_t n = std::min<size_t>({ m_blockRemaining, frameEnd - pos });
                if (m_pos + n > m_inLen) return false;
                std::memcpy(m_win.data() + pos, m_in + m_pos, n);
                m_pos += n;
                pos += n;
                m_blockRemaining -= static_cast<uint32_t>(n);
                continue;
            }
            // verbatim / aligned: symbols until the block or the frame is done
            while (m_blockRemaining > 0 && pos < frameEnd) {
                uint32_t sym = 0;
                if (!Sym(m_mainTable, sym)) return false;
                if (sym < 256) {
                    m_win[pos++] = static_cast<uint8_t>(sym);
                    --m_blockRemaining;
                    continue;
                }
                sym -= 256;
                uint32_t len = sym & 7;
                if (len == 7) {
                    uint32_t extra = 0;
                    if (m_lenEmpty || !Sym(m_lenTable, extra)) return false;
                    len += extra;
                }
                len += 2;
                const uint32_t slot = sym >> 3;
                uint32_t off = 0;
                if (slot == 0) {
                    off = m_r0;
                } else if (slot == 1) {
                    off = m_r1; m_r1 = m_r0; m_r0 = off;
                } else if (slot == 2) {
                    off = m_r2; m_r2 = m_r0; m_r0 = off;
                } else {
                    const uint32_t extraBits = (slot >= 36) ? 17u : kExtraBits[slot];
                    off = kPositionBase[slot] - 2;
                    if (m_blockType == 2 && extraBits >= 3) {
                        uint32_t verbatim = 0, aligned = 0;
                        if (extraBits > 3 && !Read(static_cast<int>(extraBits - 3), verbatim)) return false;
                        if (!Sym(m_alignTable, aligned)) return false;
                        off += (verbatim << 3) + aligned;
                    } else if (extraBits > 0) {
                        uint32_t verbatim = 0;
                        if (!Read(static_cast<int>(extraBits), verbatim)) return false;
                        off += verbatim;
                    } else {
                        off = 1; // slot 3
                    }
                    m_r2 = m_r1; m_r1 = m_r0; m_r0 = off;
                }
                if (off == 0 || off > pos || len > outLen - pos) {
                    if (off == 0 || off > pos) return false;
                    len = static_cast<uint32_t>(outLen - pos); // the output ends inside this match
                }
                uint8_t* d = m_win.data() + pos;
                const uint8_t* s = d - off;
                for (uint32_t k = 0; k < len; ++k) d[k] = s[k]; // may overlap: byte by byte
                pos += len;
                m_blockRemaining = (len >= m_blockRemaining) ? 0 : m_blockRemaining - len;
            }
        }
        return true;
    }

    static uint32_t Le32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }

    // Undoes the x86 call translation frame by frame (the window keeps the translated bytes the matches refer to)
    void UndoE8(std::vector<uint8_t>& out) const {
        if (!m_intelStarted || m_intelSize == 0) return;
        int32_t curpos = 0;
        for (size_t frameStart = 0, frame = 0; frameStart < out.size() && frame < 32768; frameStart += kFrame, ++frame) {
            const size_t frameSize = std::min(kFrame, out.size() - frameStart);
            if (frameSize <= 10) { curpos += static_cast<int32_t>(frameSize); continue; }
            uint8_t* data = out.data() + frameStart;
            uint8_t* const end = data + frameSize - 10;
            int32_t pos = curpos;
            while (data < end) {
                if (*data++ != 0xE8) { ++pos; continue; }
                const int32_t abs = static_cast<int32_t>(Le32(data));
                if (abs >= -pos && abs < m_intelSize) {
                    const int32_t rel = (abs >= 0) ? abs - pos : abs + m_intelSize;
                    data[0] = static_cast<uint8_t>(rel);
                    data[1] = static_cast<uint8_t>(rel >> 8);
                    data[2] = static_cast<uint8_t>(rel >> 16);
                    data[3] = static_cast<uint8_t>(rel >> 24);
                }
                data += 4;
                pos += 5;
            }
            curpos += static_cast<int32_t>(frameSize);
        }
    }

    static constexpr uint8_t kExtraBits[52] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
                                                12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
                                                17, 17, 17, 17 };
    static constexpr uint32_t kPositionBase[52] = {
        0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 6144,
        8192, 12288, 16384, 24576, 32768, 49152, 65536, 98304, 131072, 196608, 262144, 393216, 524288, 655360, 786432,
        917504, 1048576, 1179648, 1310720, 1441792, 1572864, 1703936, 1835008, 1966080, 2097152, 2228224 };

    const uint8_t* m_in;
    size_t m_inLen;
    int m_windowBits;
    uint32_t m_resetFrames;
    const std::vector<uint64_t>& m_frameOffsets;

    size_t m_pos = 0;
    uint64_t m_buf = 0;
    int m_bits = 0;

    uint32_t m_posSlots = 0, m_mainSyms = 0;
    std::vector<uint8_t> m_win, m_mainLen, m_lenLen;
    std::vector<uint16_t> m_mainTable, m_lenTable, m_alignTable;
    bool m_lenEmpty = true;
    uint32_t m_r0 = 1, m_r1 = 1, m_r2 = 1;
    bool m_headerRead = false;
    uint32_t m_blockType = 0, m_blockLength = 0, m_blockRemaining = 0;
    bool m_intelStarted = false;
    int32_t m_intelSize = 0;
};

} // namespace Explorer2::Lzx
