#pragma once
// GtkWaveLxt2.h — GTKWave LXT2 InterLaced eXtensible Trace v2 (.lxt2, magic 0x1380) binary waveform reader
// Implements full GTKWave lxt2_read specification:
//   - 30-byte big-endian header (hdrid=0x1380, version, granule_size 32/64, numfacs, timescale)
//   - Prefix-compressed facility name dictionary (gzip/zlib/raw)
//   - Facility geometry table (rows, msb, lsb, flags: BITS, INTEGER, DOUBLE, STRING, ALIAS)
//   - Multi-block granule stream (TIME & TIME_PARTIAL sectors, 32/64-bit change bitmasks, string
//     dictionary, and all LXT2_RD_ENC_* delta/shift/arithmetic/dictionary value-change opcodes)
// With strict 64-bit bounds validation (hardened against all CVE-2023-38583 / TALOS-2023-1818..1827 vectors).
// Included inside namespace Explorer2 in EdaReadersVlsiWave.cpp (do NOT put #include <...> here).

namespace Formats {

namespace DetailGtkWaveLxt2 {

constexpr uint16_t kHdrId = 0x1380u;
constexpr uint32_t kSymInt    = 0x01u;
constexpr uint32_t kSymDouble = 0x02u;
constexpr uint32_t kSymString = 0x04u;
constexpr uint32_t kSymAlias  = 0x08u;

inline uint32_t RdVarBE(const uint8_t* p, uint8_t width) {
    switch (width) {
    case 1: return p[0];
    case 2: return ByteIO::RdU16BE(p);
    case 3: return (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
    case 4: return ByteIO::RdU32BE(p);
    default: return 0;
    }
}

inline bool DecompressLxt2Blob(
    const uint8_t* src,
    size_t compLen,
    size_t expectedUncomp,
    std::vector<uint8_t>& out)
{
    out.clear();
    if (compLen == 0) return expectedUncomp == 0;
    if (expectedUncomp > 64u * 1024u * 1024u) return false;

    if (compLen >= 10 && src[0] == 0x1F && src[1] == 0x8B) {
        if (!ZipDeflate::InflateGzip(src, compLen, out, 64u * 1024u * 1024u)) return false;
        return expectedUncomp == 0 || out.size() == expectedUncomp;
    }
    if (expectedUncomp > 0 && compLen == expectedUncomp) {
        out.assign(src, src + compLen);
        return true;
    }
    if (ZipDeflate::InflateZlib(src, compLen, out, 64u * 1024u * 1024u)) {
        return expectedUncomp == 0 || out.size() == expectedUncomp;
    }
    if (expectedUncomp == 0) {
        out.assign(src, src + compLen);
        return true;
    }
    return false;
}

inline uint64_t BitsToU64(const std::string& bits) {
    uint64_t v = 0;
    for (char c : bits) {
        v = (v << 1) | ((c == '1') ? 1ULL : 0ULL);
    }
    return v;
}

inline void U64ToBits(uint64_t v, std::string& bits) {
    const size_t n = bits.size();
    for (size_t i = 0; i < n; ++i) {
        const size_t bitIdx = n - 1u - i;
        bits[i] = (bitIdx < 64u && ((v >> bitIdx) & 1ULL)) ? '1' : '0';
    }
}

inline const wchar_t* TimescaleUnitStr(int8_t exp10) {
    switch (exp10) {
    case -15: return L"1 fs";
    case -14: return L"10 fs";
    case -13: return L"100 fs";
    case -12: return L"1 ps";
    case -11: return L"10 ps";
    case -10: return L"100 ps";
    case -9:  return L"1 ns";
    case -8:  return L"10 ns";
    case -7:  return L"100 ns";
    case -6:  return L"1 us";
    case -3:  return L"1 ms";
    case  0:  return L"1 s";
    default:  return L"ticks";
    }
}

} // namespace DetailGtkWaveLxt2

inline bool ParseGtkWaveLxt2(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 62 || size > 128u * 1024u * 1024u) return false;
    if (ByteIO::RdU16BE(data) != DetailGtkWaveLxt2::kHdrId) return false;

    const uint16_t version       = ByteIO::RdU16BE(data + 2);
    const uint8_t  granuleSize   = data[4];
    const uint32_t numFacs       = ByteIO::RdU32BE(data + 5);
    const uint32_t numFacBytes   = ByteIO::RdU32BE(data + 9);
    const uint32_t longestName   = ByteIO::RdU32BE(data + 13);
    const uint32_t zFacNameSize  = ByteIO::RdU32BE(data + 17);
    const uint32_t zFacPredecSz  = ByteIO::RdU32BE(data + 21);
    const uint32_t zFacGeomSize  = ByteIO::RdU32BE(data + 25);
    const int8_t   timescaleExp  = static_cast<int8_t>(data[29]);
    (void)numFacBytes;

    if (version == 0 || version > 0x0010u) return false;
    if (granuleSize == 0 || granuleSize > 64u) return false;
    if (numFacs == 0 || numFacs > 65536u) return false;
    if (longestName == 0 || longestName > 16384u) return false;
    if (zFacNameSize == 0 || zFacGeomSize == 0) return false;
    if (30ULL + static_cast<uint64_t>(zFacNameSize) + static_cast<uint64_t>(zFacGeomSize) > size) return false;

    // 1. Decompress & reconstruct prefix-compressed facility names
    std::vector<uint8_t> facNameBytes;
    if (!DetailGtkWaveLxt2::DecompressLxt2Blob(data + 30, zFacNameSize, zFacPredecSz, facNameBytes)) {
        return false;
    }

    std::vector<std::string> facNames(numFacs);
    {
        size_t np = 0;
        std::string prev;
        for (uint32_t i = 0; i < numFacs; ++i) {
            if (np + 3 > facNameBytes.size()) return false;
            const uint16_t pfx = ByteIO::RdU16BE(facNameBytes.data() + np);
            np += 2;
            if (pfx > prev.size() || pfx > longestName) return false;
            const size_t sfxStart = np;
            while (np < facNameBytes.size() && facNameBytes[np] != 0) ++np;
            if (np >= facNameBytes.size()) return false;
            const size_t sfxLen = np - sfxStart;
            ++np; // consume NUL
            if (static_cast<uint64_t>(pfx) + sfxLen == 0 || static_cast<uint64_t>(pfx) + sfxLen > longestName + 16u) {
                return false;
            }
            std::string cur = prev.substr(0, pfx);
            cur.append(reinterpret_cast<const char*>(facNameBytes.data() + sfxStart), sfxLen);
            facNames[i] = cur;
            prev = std::move(cur);
        }
    }

    // 2. Decompress & parse facility geometry table (16 bytes per facility)
    const size_t expectedGeomBytes = static_cast<size_t>(numFacs) * 16u;
    std::vector<uint8_t> geomBytes;
    if (!DetailGtkWaveLxt2::DecompressLxt2Blob(
            data + 30u + zFacNameSize, zFacGeomSize, expectedGeomBytes, geomBytes) ||
        geomBytes.size() != expectedGeomBytes)
    {
        return false;
    }

    struct FacMeta {
        uint32_t rows = 0;
        int32_t msb = 0;
        int32_t lsb = 0;
        uint32_t flags = 0;
        int bitLen = 1;
        std::string curBits;
    };
    std::vector<FacMeta> facs(numFacs);
    for (uint32_t i = 0; i < numFacs; ++i) {
        const uint8_t* gp = geomBytes.data() + static_cast<size_t>(i) * 16u;
        facs[i].rows  = ByteIO::RdU32BE(gp + 0);
        facs[i].msb   = static_cast<int32_t>(ByteIO::RdU32BE(gp + 4));
        facs[i].lsb   = static_cast<int32_t>(ByteIO::RdU32BE(gp + 8));
        facs[i].flags = ByteIO::RdU32BE(gp + 12);

        if (facs[i].flags & DetailGtkWaveLxt2::kSymAlias) {
            if (facs[i].rows >= numFacs) return false;
            facs[i].bitLen = 1;
        } else if (facs[i].flags & DetailGtkWaveLxt2::kSymInt) {
            facs[i].bitLen = 32;
        } else if (facs[i].flags & (DetailGtkWaveLxt2::kSymDouble | DetailGtkWaveLxt2::kSymString)) {
            facs[i].bitLen = 1;
        } else {
            const int64_t span = std::abs(static_cast<int64_t>(facs[i].msb) - static_cast<int64_t>(facs[i].lsb)) + 1;
            if (span < 1 || span > 1024) return false;
            facs[i].bitLen = static_cast<int>(span);
        }
        facs[i].curBits.assign(static_cast<size_t>(facs[i].bitLen), '0');
    }

    std::vector<DetailHwBin::DecodedWaveSignal> sigs(numFacs);
    for (uint32_t i = 0; i < numFacs; ++i) {
        std::wstring wname = PathUtils::Utf8ToWide(facNames[i]);
        if (!(facs[i].flags & (DetailGtkWaveLxt2::kSymDouble | DetailGtkWaveLxt2::kSymString | DetailGtkWaveLxt2::kSymInt)) &&
            facs[i].bitLen > 1 && facNames[i].find('[') == std::string::npos) {
            wchar_t rng[32] = {};
            swprintf_s(rng, L"[%d:%d]", facs[i].msb, facs[i].lsb);
            wname += rng;
        }
        sigs[i].name = std::move(wname);
        sigs[i].bitWidth = facs[i].bitLen;
        sigs[i].isReal = (facs[i].flags & DetailGtkWaveLxt2::kSymDouble) != 0;
    }

    // 3. Process compressed/uncompressed LXT2 trace blocks
    size_t blkPos = 30u + static_cast<size_t>(zFacNameSize) + static_cast<size_t>(zFacGeomSize);
    size_t blocksParsed = 0;
    size_t totalChanges = 0;
    uint64_t globalMinT = UINT64_MAX, globalMaxT = 0;
    const size_t granMaskBytes = (granuleSize <= 32u) ? 4u : 8u;

    while (blkPos < size) {
        // Optional trailing 0 byte EOF marker
        if (blkPos + 1 == size && data[blkPos] == 0) break;
        if (size - blkPos < 32u) return false;

        const uint64_t uncompSiz = ByteIO::RdU64BE(data + blkPos + 0);
        const uint64_t compSiz   = ByteIO::RdU64BE(data + blkPos + 8);
        const uint64_t startTime = ByteIO::RdU64BE(data + blkPos + 16);
        const uint64_t endTime   = ByteIO::RdU64BE(data + blkPos + 24);
        blkPos += 32u;

        if (uncompSiz == 0 && compSiz == 0) break;
        if (uncompSiz < 13ULL || uncompSiz > 64ULL * 1024ULL * 1024ULL) return false;
        if (compSiz == 0 || compSiz > size - blkPos || startTime > endTime) return false;

        std::vector<uint8_t> mem;
        if (!DetailGtkWaveLxt2::DecompressLxt2Blob(data + blkPos, static_cast<size_t>(compSiz), static_cast<size_t>(uncompSiz), mem)) {
            return false;
        }
        blkPos += static_cast<size_t>(compSiz);

        globalMinT = std::min(globalMinT, startTime);
        globalMaxT = std::max(globalMaxT, endTime);

        const size_t U = mem.size();
        const uint32_t numDict   = ByteIO::RdU32BE(mem.data() + U - 12u);
        const uint32_t dictBytes = ByteIO::RdU32BE(mem.data() + U - 8u);
        const uint32_t numMap    = ByteIO::RdU32BE(mem.data() + U - 4u);

        if (numDict > 262144u || numMap > 262144u) return false;
        const uint64_t tailNeed = 12ULL + static_cast<uint64_t>(dictBytes) + static_cast<uint64_t>(numMap) * granMaskBytes;
        if (tailNeed > U) return false;

        const size_t dictStart = U - 12u - static_cast<size_t>(dictBytes);
        const size_t mapStart  = dictStart - static_cast<size_t>(numMap) * granMaskBytes;

        // Parse string dictionary
        std::vector<std::string_view> dictStrs;
        dictStrs.reserve(numDict);
        {
            size_t dp = dictStart;
            const size_t dictEnd = U - 12u;
            for (uint32_t di = 0; di < numDict; ++di) {
                if (dp >= dictEnd) return false;
                const size_t st = dp;
                while (dp < dictEnd && mem[dp] != 0) ++dp;
                if (dp >= dictEnd) return false;
                dictStrs.emplace_back(reinterpret_cast<const char*>(mem.data() + st), dp - st);
                ++dp;
            }
        }

        // Parse change bitmasks
        std::vector<uint64_t> mapMasks(numMap);
        for (uint32_t mi = 0; mi < numMap; ++mi) {
            const uint8_t* mp = mem.data() + mapStart + static_cast<size_t>(mi) * granMaskBytes;
            mapMasks[mi] = (granMaskBytes == 4u) ? static_cast<uint64_t>(ByteIO::RdU32BE(mp)) : ByteIO::RdU64BE(mp);
        }

        // Parse granule sections in [0 .. mapStart)
        size_t p = 0;
        while (p < mapStart) {
            const uint8_t sectTyp = mem[p++];
            if (sectTyp != 0u && sectTyp != 2u) return false;

            uint32_t strtFac = 0, endFac = numFacs;
            if (sectTyp == 2u) {
                if (mapStart - p < 4u) return false;
                strtFac = ByteIO::RdU32BE(mem.data() + p);
                p += 4u;
                if (strtFac >= numFacs) return false;
                endFac = std::min<uint32_t>(numFacs, strtFac + 128u);
            }

            if (p >= mapStart) return false;
            const uint8_t nTimes = mem[p++];
            if (nTimes == 0 || nTimes > granuleSize) return false;
            if (mapStart - p < static_cast<size_t>(nTimes) * 8u + 2u) return false;

            uint64_t timeTable[64] = {};
            for (uint8_t ti = 0; ti < nTimes; ++ti) {
                timeTable[ti] = ByteIO::RdU64BE(mem.data() + p);
                p += 8u;
            }

            const uint8_t mapIdxW = mem[p++];
            if (mapIdxW < 1u || mapIdxW > 4u) return false;

            struct ChangingFac {
                uint32_t facIdx = 0;
                uint64_t mask = 0;
                uint32_t relOff = 0;
            };
            std::vector<ChangingFac> changing;
            for (uint32_t fi = strtFac; fi < endFac; ++fi) {
                if (facs[fi].flags & DetailGtkWaveLxt2::kSymAlias) continue;
                if (mapStart - p < mapIdxW) return false;
                const uint32_t mIdx = DetailGtkWaveLxt2::RdVarBE(mem.data() + p, mapIdxW);
                p += mapIdxW;
                if (mIdx > numMap) return false;
                if (mIdx > 0) changing.push_back({ fi, mapMasks[mIdx - 1u], 0 });
            }

            if (p >= mapStart) return false;
            const uint8_t curposW = mem[p++];
            if (curposW < 1u || curposW > 4u) return false;
            if (mapStart - p < changing.size() * static_cast<size_t>(curposW)) return false;

            for (auto& cf : changing) {
                cf.relOff = DetailGtkWaveLxt2::RdVarBE(mem.data() + p, curposW);
                p += curposW;
            }

            const size_t valBase = p;
            size_t maxEndVp = valBase;

            for (const auto& cf : changing) {
                if (cf.relOff > mapStart - valBase) return false;
                size_t vp = valBase + cf.relOff;
                FacMeta& fm = facs[cf.facIdx];

                for (uint8_t ti = 0; ti < nTimes; ++ti) {
                    if (((cf.mask >> ti) & 1ULL) == 0) continue;
                    if (vp >= mapStart) return false;
                    const uint8_t vch = mem[vp++];

                    if (vch <= 1u) {
                        std::fill(fm.curBits.begin(), fm.curBits.end(), static_cast<char>('0' + vch));
                    } else if (vch == 2u) { // INV
                        for (char& ch : fm.curBits) {
                            if (ch == '0') ch = '1';
                            else if (ch == '1') ch = '0';
                        }
                    } else if (vch == 3u || vch == 4u) { // LSH0 / LSH1
                        if (fm.curBits.size() > 1u) {
                            std::memmove(&fm.curBits[0], &fm.curBits[1], fm.curBits.size() - 1u);
                        }
                        fm.curBits.back() = static_cast<char>('0' + (vch - 3u));
                    } else if (vch == 5u || vch == 6u) { // RSH0 / RSH1
                        if (fm.curBits.size() > 1u) {
                            std::memmove(&fm.curBits[1], &fm.curBits[0], fm.curBits.size() - 1u);
                        }
                        fm.curBits.front() = static_cast<char>('0' + (vch - 5u));
                    } else if (vch >= 7u && vch <= 10u) { // ADD1..ADD4
                        uint64_t x = DetailGtkWaveLxt2::BitsToU64(fm.curBits);
                        x += static_cast<uint64_t>(vch - 7u + 1u);
                        DetailGtkWaveLxt2::U64ToBits(x, fm.curBits);
                    } else if (vch >= 11u && vch <= 14u) { // SUB1..SUB4
                        uint64_t x = DetailGtkWaveLxt2::BitsToU64(fm.curBits);
                        x -= static_cast<uint64_t>(vch - 11u + 1u);
                        DetailGtkWaveLxt2::U64ToBits(x, fm.curBits);
                    } else if (vch == 15u) { // X
                        std::fill(fm.curBits.begin(), fm.curBits.end(), 'x');
                    } else if (vch == 16u) { // Z
                        std::fill(fm.curBits.begin(), fm.curBits.end(), 'z');
                    } else if (vch == 17u) { // BLACKOUT
                        continue;
                    } else if (vch >= 18u && vch <= 21u) { // DICT_START + (0..3 -> 1..4 byte index)
                        const uint8_t idxW = static_cast<uint8_t>(vch - 17u);
                        if (mapStart - vp < idxW) return false;
                        const uint32_t dIdx = DetailGtkWaveLxt2::RdVarBE(mem.data() + vp, idxW);
                        vp += idxW;
                        if (dIdx >= dictStrs.size()) return false;
                        std::string_view ds = dictStrs[dIdx];

                        if (fm.flags & DetailGtkWaveLxt2::kSymDouble) {
                            double dv = 0.0;
                            if (ReaderText::ParseDoubleSv(ds, dv) && std::isfinite(dv)) {
                                sigs[cf.facIdx].realChanges.emplace_back(timeTable[ti], dv);
                                ++totalChanges;
                            }
                            continue;
                        }
                        // Left-pad multi-bit vector with '0' (or leading x/z) to bitLen
                        const char padCh = (!ds.empty() && (ds.front() == 'x' || ds.front() == 'z' || ds.front() == 'X' || ds.front() == 'Z'))
                            ? static_cast<char>(std::tolower(static_cast<unsigned char>(ds.front())))
                            : '0';
                        if (ds.size() >= fm.curBits.size()) {
                            fm.curBits.assign(ds.data() + (ds.size() - fm.curBits.size()), fm.curBits.size());
                        } else {
                            const size_t padN = fm.curBits.size() - ds.size();
                            std::fill(fm.curBits.begin(), fm.curBits.begin() + padN, padCh);
                            std::memcpy(&fm.curBits[padN], ds.data(), ds.size());
                        }
                    } else {
                        return false;
                    }

                    if (!(fm.flags & DetailGtkWaveLxt2::kSymDouble)) {
                        sigs[cf.facIdx].changes.emplace_back(timeTable[ti], fm.curBits);
                        ++totalChanges;
                    }
                }
                maxEndVp = std::max(maxEndVp, vp);
            }
            p = maxEndVp;
        }
        ++blocksParsed;
    }

    if (blocksParsed == 0 || totalChanges == 0) return false;

    // Populate alias signals from canonical target facilities
    for (uint32_t i = 0; i < numFacs; ++i) {
        if (facs[i].flags & DetailGtkWaveLxt2::kSymAlias) {
            uint32_t tgt = facs[i].rows;
            int hops = 0;
            while (tgt < numFacs && (facs[tgt].flags & DetailGtkWaveLxt2::kSymAlias) && hops++ < 16) {
                tgt = facs[tgt].rows;
            }
            if (tgt < numFacs && !(facs[tgt].flags & DetailGtkWaveLxt2::kSymAlias)) {
                sigs[i].bitWidth = sigs[tgt].bitWidth;
                sigs[i].isReal   = sigs[tgt].isReal;
                sigs[i].changes  = sigs[tgt].changes;
                sigs[i].realChanges = sigs[tgt].realChanges;
            }
        }
    }

    if (globalMinT > globalMaxT) { globalMinT = 0; globalMaxT = 100; }
    wchar_t title[200] = {};
    swprintf_s(title, L"GTKWave LXT2 Trace (%u signals, %zu blocks, %llu..%llu @ %ls)",
        numFacs, blocksParsed,
        static_cast<unsigned long long>(globalMinT),
        static_cast<unsigned long long>(globalMaxT),
        DetailGtkWaveLxt2::TimescaleUnitStr(timescaleExp));
    return DetailHwBin::RenderWaveSignalsToBoard(sigs, globalMinT, globalMaxT, title, out);
}

} // namespace Formats
