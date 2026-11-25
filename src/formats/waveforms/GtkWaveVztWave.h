#pragma once
// GtkWaveVztWave.h — GTKWave VZTD v1 Compressed Simulation Trace (.vzt) Reader
// Implements the true GTKWave `vzt_read.c` VZTD v1 (`0x565A5444`, version `0x0001`, `gran <= 32`)
// specification: prefix-compressed facility names, compressed facility geometry (`rows`, `msb`, `lsb`,
// `flags`), multi-codec block decompression (gzip/zlib, bzip2, lzma), MSB-terminated varints (`v32`/`v64`),
// delta timestamps, raw & RLE (`bp_rle`) bitplane dictionaries, string/double tables, and per-lane `vindex`
// reconstruction of 1-bit, multi-bit bus, integer, double, string, and alias facilities.
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailGtkWaveVzt {

constexpr uint32_t kVztSymFBits    = 0u;
constexpr uint32_t kVztSymFInteger = (1u << 0);
constexpr uint32_t kVztSymFDouble  = (1u << 1);
constexpr uint32_t kVztSymFString  = (1u << 2);
constexpr uint32_t kVztSymFAlias   = (1u << 3);

// GTKWave `vzt_rd_get_v32`: 7-bit little-endian varint terminated when `(byte & 0x80) != 0`
inline bool ReadVztV32(const uint8_t* buf, size_t len, size_t& pos, uint32_t& outVal) {
    outVal = 0;
    uint32_t shift = 0;
    for (int i = 0; i < 6; ++i) {
        if (pos >= len) return false;
        const uint8_t b = buf[pos++];
        outVal |= static_cast<uint32_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) != 0u) return true;
        shift += 7u;
    }
    return false;
}

// GTKWave `vzt_rd_get_v64`: 7-bit little-endian varint terminated when `(byte & 0x80) != 0`
inline bool ReadVztV64(const uint8_t* buf, size_t len, size_t& pos, uint64_t& outVal) {
    outVal = 0;
    uint32_t shift = 0;
    for (int i = 0; i < 10; ++i) {
        if (pos >= len) return false;
        const uint8_t b = buf[pos++];
        outVal |= static_cast<uint64_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) != 0u) return true;
        shift += 7u;
    }
    return false;
}

// Decompress a VZT section or block compressed with gzip/zlib (`0`), bzip2 (`1`), or lzma (`2`),
// handling both direct stream magic (`1F 8B` / `78 xx` / `BZ` / `z7` / 5B-13B LZMA1) AND explicit
// 1-byte compression-type prefix (`0 = gzip/zlib`, `1 = bzip2`, `2 = lzma`).
inline bool DecompressVztPayload(
    const uint8_t* comp,
    size_t compLen,
    size_t expectedUncompLen,
    std::vector<uint8_t>& outBuf)
{
    outBuf.clear();
    if (!comp || compLen < 2u || expectedUncompLen == 0u || expectedUncompLen > (64u << 20)) {
        return false;
    }

    auto tryLzma = [&](const uint8_t* p, size_t n) -> bool {
        if (n < 6u) return false;
        // GTKWave `LZMA_write` container: 'z','7' + VZT varint uncompLen + 5-byte LZMA1 props + stream
        if (p[0] == 'z' && p[1] == '7') {
            size_t lp = 2u;
            uint64_t decSz = 0;
            if (!ReadVztV64(p, n, lp, decSz) || decSz == 0 || decSz > (64u << 20)) return false;
            if (lp + 5u >= n) return false;
            const uint8_t propsByte = p[lp];
            const uint32_t dictSz = ByteIO::RdU32LE(p + lp + 1u);
            if (StreamCodecs::DecompressLzma1(propsByte, dictSz, p + lp + 5u, n - lp - 5u,
                                              outBuf, decSz, true, expectedUncompLen + 64u) &&
                outBuf.size() == expectedUncompLen) {
                return true;
            }
            outBuf.clear();
        }
        // Standard 13-byte `.lzma` header `[props:1][dictSize:4LE][uncompSize:8LE]`
        if (n > 13u && p[0] < 225u) {
            const uint64_t hdrUncomp = ByteIO::RdU64LE(p + 5u);
            const bool hasSz = (hdrUncomp != ~0ULL);
            const uint64_t targetSz = hasSz ? hdrUncomp : static_cast<uint64_t>(expectedUncompLen);
            if (StreamCodecs::DecompressLzma1(p[0], ByteIO::RdU32LE(p + 1u), p + 13u, n - 13u,
                                              outBuf, targetSz, hasSz, expectedUncompLen + 64u) &&
                outBuf.size() == expectedUncompLen) {
                return true;
            }
            outBuf.clear();
        }
        // 5-byte LZMA1 properties (`[lc/lp/pb:1][dictSize:4LE]`) without 8-byte size field
        if (n > 5u && p[0] < 225u) {
            if (StreamCodecs::DecompressLzma1(p[0], ByteIO::RdU32LE(p + 1u), p + 5u, n - 5u,
                                              outBuf, static_cast<uint64_t>(expectedUncompLen),
                                              true, expectedUncompLen + 64u) &&
                outBuf.size() == expectedUncompLen) {
                return true;
            }
            outBuf.clear();
        }
        return false;
    };

    auto tryDirect = [&](const uint8_t* p, size_t n) -> bool {
        if (n < 2u) return false;
        // Gzip (`1F 8B`)
        if (p[0] == 0x1Fu && p[1] == 0x8Bu) {
            if (ZipDeflate::InflateGzip(p, n, outBuf, expectedUncompLen + 64u) && outBuf.size() == expectedUncompLen) {
                return true;
            }
            outBuf.clear();
        }
        // Zlib (`78 01`, `78 5E`, `78 9C`, `78 DA`, etc.)
        if ((p[0] & 0x0Fu) == 8u && (((static_cast<uint32_t>(p[0]) << 8) | p[1]) % 31u) == 0u) {
            if (ZipDeflate::InflateZlib(p, n, outBuf, expectedUncompLen + 64u) && outBuf.size() == expectedUncompLen) {
                return true;
            }
            outBuf.clear();
        }
        // Bzip2 (`BZh1`..`BZh9`)
        if (n >= 4u && p[0] == 'B' && p[1] == 'Z' && p[2] == 'h') {
            if (StreamCodecs::DecompressBzip2Stream(p, n, outBuf, expectedUncompLen + 64u) && outBuf.size() == expectedUncompLen) {
                return true;
            }
            outBuf.clear();
        }
        // LZMA1 (`z7` or raw LZMA1 props `0x5D` / `< 225`)
        if ((p[0] == 'z' && p[1] == '7') || p[0] == 0x5Du) {
            if (tryLzma(p, n)) return true;
        }
        return false;
    };

    // 1. Try direct stream magic at `comp[0..compLen)`
    if (tryDirect(comp, compLen)) return true;

    // 2. Try explicit 1-byte `ztype` prefix (`0 = gzip/zlib`, `1 = bzip2`, `2 = lzma`)
    if (compLen >= 3u && comp[0] <= 2u) {
        const uint8_t ztype = comp[0];
        const uint8_t* sub = comp + 1u;
        const size_t subLen = compLen - 1u;
        if (ztype == 0u) {
            if ((ZipDeflate::InflateGzip(sub, subLen, outBuf, expectedUncompLen + 64u) ||
                 ZipDeflate::InflateZlib(sub, subLen, outBuf, expectedUncompLen + 64u) ||
                 ZipDeflate::InflateRaw(sub, subLen, outBuf, expectedUncompLen + 64u)) &&
                outBuf.size() == expectedUncompLen) {
                return true;
            }
            outBuf.clear();
        } else if (ztype == 1u) {
            if (StreamCodecs::DecompressBzip2Stream(sub, subLen, outBuf, expectedUncompLen + 64u) &&
                outBuf.size() == expectedUncompLen) {
                return true;
            }
            outBuf.clear();
        } else if (ztype == 2u) {
            if (tryLzma(sub, subLen)) return true;
        }
    }

    return false;
}

} // namespace DetailGtkWaveVzt

inline bool ParseGtkWaveVztWave(
    const uint8_t* data,
    size_t size,
    const std::wstring& /*fullPath*/,
    ParsedEdaBoard& out)
{
    using namespace DetailGtkWaveVzt;

    // Fixed 36-byte GTKWave VZTD v1 file header (`vzt_write.c` / `vzt_read.c`):
    //   [0..3]   "VZTD" (0x565A5444)
    //   [4..5]   version (uint16BE == 0x0001)
    //   [6]      granule_size (uint8, 1..32)
    //   [7..10]  numfacs (uint32BE)
    //   [11..14] numrealfacs (uint32BE, <= numfacs)
    //   [15..18] numfacbytes (uint32BE)
    //   [19..22] longestname (uint32BE)
    //   [23..26] zfacnamesize (uint32BE)
    //   [27..30] zfacname_predec_size (uint32BE)
    //   [31..34] zfacgeometrysize (uint32BE)
    //   [35]     timescale (int8)
    if (!data || size < 60u || size > (64u << 20)) return false;
    if (ByteIO::RdU32BE(data) != 0x565A5444u) return false;
    const uint16_t version = ByteIO::RdU16BE(data + 4u);
    const uint8_t granuleSize = data[6u];
    if (version != 1u || granuleSize == 0u || granuleSize > 32u) return false;

    const uint32_t numFacs       = ByteIO::RdU32BE(data + 7u);
    const uint32_t numRealFacs   = ByteIO::RdU32BE(data + 11u);
    const uint32_t zfacNameSize  = ByteIO::RdU32BE(data + 23u);
    const uint32_t facPredecSize = ByteIO::RdU32BE(data + 27u);
    const uint32_t zfacGeomSize  = ByteIO::RdU32BE(data + 31u);
    const int8_t   timescaleExp  = static_cast<int8_t>(data[35u]);

    if (numFacs == 0u || numFacs > 65536u || numRealFacs == 0u || numRealFacs > numFacs) return false;
    if (zfacNameSize == 0u || facPredecSize == 0u || facPredecSize > (16u << 20)) return false;
    if (zfacGeomSize == 0u) return false;

    size_t pos = 36u;
    if (zfacNameSize > size - pos) return false;

    // 1. Decompress & reconstruct prefix-compressed `facnames`
    std::vector<uint8_t> facNameBuf;
    if (!DecompressVztPayload(data + pos, zfacNameSize, facPredecSize, facNameBuf)) return false;
    pos += zfacNameSize;

    std::vector<std::string> facNames(numFacs);
    {
        size_t fp = 0u;
        // Facility 0 is NUL-terminated directly (`fgetz`), though gracefully skip optional `0x0000` prefix if 0-prefixed
        if (facNameBuf.size() >= 3u && facNameBuf[0] == 0u && facNameBuf[1] == 0u && facNameBuf[2] >= 0x21u) {
            fp = 2u;
        }
        for (uint32_t i = 0u; i < numFacs; ++i) {
            uint16_t pfxLen = 0u;
            if (i > 0u) {
                if (fp + 2u > facNameBuf.size()) return false;
                pfxLen = ByteIO::RdU16BE(facNameBuf.data() + fp);
                fp += 2u;
                if (pfxLen > facNames[i - 1u].size()) return false;
            }
            const size_t strStart = fp;
            while (fp < facNameBuf.size() && facNameBuf[fp] != 0u) ++fp;
            if (fp >= facNameBuf.size()) return false; // missing NUL terminator
            std::string cur = (i > 0u && pfxLen > 0u) ? facNames[i - 1u].substr(0u, pfxLen) : std::string{};
            cur.append(reinterpret_cast<const char*>(facNameBuf.data() + strStart), fp - strStart);
            if (cur.empty()) cur = "fac_" + std::to_string(i);
            facNames[i] = std::move(cur);
            ++fp;
        }
    }

    // 2. Decompress & decode `facgeometry` (`numFacs * 16` bytes: `[rows, msb, lsb, flags]` BE32)
    if (zfacGeomSize > size - pos) return false;
    const size_t expectedGeomBytes = static_cast<size_t>(numFacs) * 16u;
    std::vector<uint8_t> geomBuf;
    if (!DecompressVztPayload(data + pos, zfacGeomSize, expectedGeomBytes, geomBuf)) return false;
    pos += zfacGeomSize;

    struct VztFacilityDesc {
        uint32_t rows = 0;
        int32_t  msb = 0;
        int32_t  lsb = 0;
        uint32_t flags = 0;
        uint32_t len = 1;
        uint32_t vindexOff = 0;
        uint32_t vindexOffX = 0;
        uint32_t strChanIdx = 0;
        bool     hasXzPlane = false;
        bool     usesStrTable = false;
    };
    std::vector<VztFacilityDesc> facs(numFacs);

    for (uint32_t i = 0u; i < numFacs; ++i) {
        const uint8_t* gp = geomBuf.data() + static_cast<size_t>(i) * 16u;
        VztFacilityDesc& f = facs[i];
        f.rows  = ByteIO::RdU32BE(gp + 0u);
        f.msb   = static_cast<int32_t>(ByteIO::RdU32BE(gp + 4u));
        f.lsb   = static_cast<int32_t>(ByteIO::RdU32BE(gp + 8u));
        f.flags = ByteIO::RdU32BE(gp + 12u);

        if ((f.flags & kVztSymFAlias) != 0u || i >= numRealFacs) {
            f.flags |= kVztSymFAlias;
            if (f.rows >= numRealFacs) return false;
            continue;
        }

        if ((f.flags & kVztSymFInteger) != 0u) {
            f.len = 32u;
        } else if ((f.flags & kVztSymFDouble) != 0u) {
            f.len = 64u;
            f.usesStrTable = true;
        } else if ((f.flags & kVztSymFString) != 0u) {
            f.len = std::clamp<uint32_t>(f.rows == 0u ? 1u : f.rows, 1u, 256u);
            f.usesStrTable = true;
        } else {
            const int64_t diff = std::llabs(static_cast<int64_t>(f.msb) - static_cast<int64_t>(f.lsb)) + 1;
            if (diff <= 0 || diff > 4096) return false;
            f.len = static_cast<uint32_t>(diff);
            f.hasXzPlane = true;
        }
    }

    // Compute `vindex_offset` (value bitplane) and `vindex_offset_x` (X/Z bitplane) per `vzt_read.c`
    uint32_t totalValues = 0u;
    uint32_t totalStrChans = 0u;
    for (uint32_t i = 0u; i < numRealFacs; ++i) {
        VztFacilityDesc& f = facs[i];
        if ((f.flags & kVztSymFAlias) != 0u) continue;
        if (f.usesStrTable) {
            f.strChanIdx = totalStrChans++;
        } else {
            f.vindexOff = totalValues;
            if (f.len > (1u << 20) - totalValues) return false;
            totalValues += f.len;
        }
    }
    for (uint32_t i = 0u; i < numRealFacs; ++i) {
        VztFacilityDesc& f = facs[i];
        if ((f.flags & kVztSymFAlias) == 0u && f.hasXzPlane) {
            f.vindexOffX = totalValues;
            if (f.len > (1u << 20) - totalValues) return false;
            totalValues += f.len;
        }
    }
    // Resolve alias descriptors
    for (uint32_t i = 0u; i < numFacs; ++i) {
        if ((facs[i].flags & kVztSymFAlias) != 0u) {
            const VztFacilityDesc& target = facs[facs[i].rows];
            facs[i].len          = target.len;
            facs[i].vindexOff    = target.vindexOff;
            facs[i].vindexOffX   = target.vindexOffX;
            facs[i].strChanIdx   = target.strChanIdx;
            facs[i].hasXzPlane   = target.hasXzPlane;
            facs[i].usesStrTable = target.usesStrTable;
        }
    }

    std::vector<DetailHwBin::DecodedWaveSignal> sigs(numFacs);
    std::vector<std::string> lastDigState(numFacs);
    std::vector<double>      lastRealVal(numFacs, std::numeric_limits<double>::quiet_NaN());

    for (uint32_t i = 0u; i < numFacs; ++i) {
        sigs[i].name = PathUtils::Utf8ToWide(facNames[i]);
        sigs[i].bitWidth = static_cast<int>(std::min<uint32_t>(facs[i].len, 64u));
        sigs[i].isReal = ((facs[i].flags & kVztSymFDouble) != 0u) ||
                         ((facs[i].flags & kVztSymFAlias) != 0u && (facs[facs[i].rows].flags & kVztSymFDouble) != 0u);
    }

    uint64_t globalStartTick = std::numeric_limits<uint64_t>::max();
    uint64_t globalEndTick   = 0ULL;
    size_t decodedBlocks = 0u;

    // 3. Process compressed value-change blocks until EOF
    while (pos + 24u <= size && decodedBlocks < 4096u) {
        const uint32_t uncompSz  = ByteIO::RdU32BE(data + pos + 0u);
        const uint32_t compSz    = ByteIO::RdU32BE(data + pos + 4u);
        const uint64_t startTick = ByteIO::RdU64BE(data + pos + 8u);
        const uint64_t endTick   = ByteIO::RdU64BE(data + pos + 16u);
        pos += 24u;

        if (uncompSz == 0u || compSz == 0u || startTick > endTick) return false;

        std::vector<uint8_t> blk;
        // Support both standard `compSz` (where `ztype` or gzip/bz2/lzma header is inside `compSz` bytes)
        // AND `1-byte ztype + compSz` layout if `pos + 1 + compSz <= size`.
        bool decompOk = false;
        if (compSz <= size - pos && DecompressVztPayload(data + pos, compSz, uncompSz, blk)) {
            pos += compSz;
            decompOk = true;
        } else if (compSz + 1u <= size - pos && data[pos] <= 2u &&
                   DecompressVztPayload(data + pos, compSz + 1u, uncompSz, blk)) {
            pos += compSz + 1u;
            decompOk = true;
        }
        if (!decompOk) return false;

        globalStartTick = std::min(globalStartTick, startTick);
        globalEndTick   = std::max(globalEndTick, endTick);

        // Decode VZT block (`vzt_rd_block_vch_decode`)
        size_t bp = 0u;
        const size_t bn = blk.size();

        uint32_t numTimeTicks = 0u;
        if (!ReadVztV32(blk.data(), bn, bp, numTimeTicks) || numTimeTicks > 1000000u) return false;

        std::vector<uint64_t> times;
        if (numTimeTicks > 0u) {
            times.resize(numTimeTicks);
            uint64_t d0 = 0ULL;
            if (!ReadVztV64(blk.data(), bn, bp, d0)) return false;
            times[0] = (startTick > 0ULL && d0 == 0ULL) ? startTick : d0;
            for (uint32_t t = 1u; t < numTimeTicks; ++t) {
                uint64_t dt = 0ULL;
                if (!ReadVztV64(blk.data(), bn, bp, dt)) return false;
                times[t] = times[t - 1u] + dt;
            }
            // If `times` were stored purely relative to `startTick` (e.g. `0..span`), shift by `startTick`
            if (startTick > 0ULL && times.front() < startTick && times.back() + startTick <= endTick) {
                for (uint64_t& tv : times) tv += startTick;
            }
        } else {
            times.push_back(startTick);
        }

        uint32_t numSections = 0u;
        uint32_t numDictEntries = 0u;
        if (!ReadVztV32(blk.data(), bn, bp, numSections)) return false;
        if (!ReadVztV32(blk.data(), bn, bp, numDictEntries)) return false;
        if (numSections > 65536u || numDictEntries > 262144u) return false;
        if (totalValues > 0u && (numSections == 0u || numDictEntries == 0u)) return false;

        const size_t totalDictWords = static_cast<size_t>(numDictEntries) * static_cast<size_t>(numSections);
        if (totalDictWords > (16u << 20)) return false;

        std::vector<uint32_t> valDict(totalDictWords, 0u);
        if (totalDictWords > 0u) {
            if (bp >= bn) return false;
            const uint8_t bpRle = blk[bp++];
            if (bpRle == 0u) {
                if (bn - bp < totalDictWords * 4u) return false;
                for (size_t w = 0u; w < totalDictWords; ++w) {
                    valDict[w] = ByteIO::RdU32BE(blk.data() + bp);
                    bp += 4u;
                }
            } else {
                // Bitplane RLE (`bp_rle != 0`): alternating runs of 0-bits and 1-bits (starting with curBit = 0,
                // where an initial run length of 0 switches immediately to 1-bits, per `vzt_read.c`)
                const size_t totalBits = totalDictWords * 32u;
                size_t bitCursor = 0u;
                uint32_t curBit = 0u;
                while (bitCursor < totalBits) {
                    uint32_t runLen = 0u;
                    if (!ReadVztV32(blk.data(), bn, bp, runLen)) return false;
                    if (runLen > totalBits - bitCursor) return false;
                    if (curBit != 0u) {
                        for (uint32_t r = 0u; r < runLen; ++r) {
                            const size_t bi = bitCursor + r;
                            valDict[bi >> 5u] |= (1u << (bi & 31u));
                        }
                    }
                    bitCursor += runLen;
                    curBit ^= 1u;
                }
            }
        }

        // String dictionary (`num_str_entries` NUL-terminated strings for `DOUBLE` / `STRING` facilities)
        uint32_t numStrEntries = 0u;
        if (!ReadVztV32(blk.data(), bn, bp, numStrEntries) || numStrEntries > 262144u) return false;
        std::vector<std::string> strDict(numStrEntries);
        for (uint32_t s = 0u; s < numStrEntries; ++s) {
            const size_t st = bp;
            while (bp < bn && blk[bp] != 0u) ++bp;
            if (bp >= bn) return false;
            strDict[s].assign(reinterpret_cast<const char*>(blk.data() + st), bp - st);
            ++bp;
        }

        // Per-lane dictionary index table `vindex[0 .. totalValues - 1]`
        std::vector<uint32_t> vindex(totalValues, 0u);
        for (uint32_t v = 0u; v < totalValues; ++v) {
            uint32_t idx = 0u;
            if (!ReadVztV32(blk.data(), bn, bp, idx) || idx >= numDictEntries) return false;
            vindex[v] = idx;
        }

        // Optional string-value index table `sindex` (`totalStrChans * times.size()` `v32` indices)
        std::vector<uint32_t> sindex;
        if (totalStrChans > 0u && numStrEntries > 0u && bp < bn) {
            const size_t totalStrSlots = static_cast<size_t>(totalStrChans) * times.size();
            sindex.resize(totalStrSlots, 0u);
            for (size_t k = 0u; k < totalStrSlots && bp < bn; ++k) {
                uint32_t sidx = 0u;
                if (!ReadVztV32(blk.data(), bn, bp, sidx) || sidx >= numStrEntries) return false;
                sindex[k] = sidx;
            }
        }

        // Reconstruct facility transitions across all ticks in this block
        const size_t nTicks = times.size();
        for (uint32_t fi = 0u; fi < numFacs && fi < 64u; ++fi) {
            const VztFacilityDesc& f = facs[fi];
            for (size_t t = 0u; t < nTicks; ++t) {
                const uint64_t tick = times[t];
                const size_t sec = t / static_cast<size_t>(granuleSize);
                const uint32_t bitInSec = static_cast<uint32_t>(t % static_cast<size_t>(granuleSize));

                if (f.usesStrTable) {
                    if (!sindex.empty()) {
                        const uint32_t sidx = sindex[static_cast<size_t>(f.strChanIdx) * nTicks + t];
                        const std::string& sv = strDict[sidx];
                        if (sigs[fi].isReal) {
                            double dv = 0.0;
                            if (ReaderText::ParseDoubleSv(sv, dv) &&
                                (std::isnan(lastRealVal[fi]) || std::fabs(dv - lastRealVal[fi]) > 1e-15)) {
                                sigs[fi].realChanges.push_back({ tick, dv });
                                lastRealVal[fi] = dv;
                            }
                        } else if (sv != lastDigState[fi]) {
                            sigs[fi].changes.push_back({ tick, sv });
                            lastDigState[fi] = sv;
                        }
                    }
                    continue;
                }

                if (sec >= numSections) break;

                const uint32_t wLen = std::min<uint32_t>(f.len, 64u);
                std::string curBits(wLen, '0');
                for (uint32_t b = 0u; b < wLen; ++b) {
                    const uint32_t d0 = vindex[f.vindexOff + b];
                    const uint32_t w0 = valDict[static_cast<size_t>(d0) * numSections + sec];
                    const uint32_t valBit = (w0 >> bitInSec) & 1u;

                    char ch = valBit ? '1' : '0';
                    if (f.hasXzPlane) {
                        const uint32_t dx = vindex[f.vindexOffX + b];
                        const uint32_t wx = valDict[static_cast<size_t>(dx) * numSections + sec];
                        const uint32_t xzBit = (wx >> bitInSec) & 1u;
                        if (xzBit != 0u) {
                            ch = valBit ? 'z' : 'x';
                        }
                    }
                    curBits[b] = ch;
                }

                if (curBits != lastDigState[fi] && sigs[fi].changes.size() < 4096u) {
                    sigs[fi].changes.push_back({ tick, curBits });
                    lastDigState[fi] = std::move(curBits);
                }
            }
        }

        ++decodedBlocks;
    }

    if (decodedBlocks == 0u || pos != size) return false;
    if (globalStartTick > globalEndTick) globalStartTick = 0ULL;

    wchar_t title[220] = {};
    swprintf_s(title, L"GTKWave VZT Compressed Trace (.vzt v%u, gran=%u, 10^%d s, %u signals, %zu blocks)",
               static_cast<unsigned>(version),
               static_cast<unsigned>(granuleSize),
               static_cast<int>(timescaleExp),
               numFacs,
               decodedBlocks);

    if (!DetailHwBin::RenderWaveSignalsToBoard(sigs, globalStartTick, globalEndTick, title, out)) {
        return false;
    }
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

} // namespace Formats
