#pragma once
// GtkWaveFst.h — GTKWave FST Binary Waveform (.fst) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailHwBin {

inline bool DecompressFstLz4RawBlock(const uint8_t* src, size_t srcLen, size_t expectedOut, std::vector<uint8_t>& out, size_t maxOut = 32 * 1024 * 1024) {
    out.clear();
    if (!src || expectedOut > maxOut) return false;
    if (expectedOut == 0) return srcLen == 0;
    if (srcLen == 0) return false;
    out.reserve(std::min<size_t>(expectedOut, 4 * 1024 * 1024));

    size_t sp = 0;
    while (sp < srcLen) {
        uint8_t tok = src[sp++];
        size_t litLen = static_cast<size_t>(tok >> 4);
        if (litLen == 15) {
            for (;;) {
                if (sp >= srcLen) return false;
                uint8_t b = src[sp++];
                litLen += b;
                if (b != 255) break;
                if (litLen > maxOut) return false;
            }
        }
        if (sp + litLen > srcLen || out.size() + litLen > maxOut) return false;
        out.insert(out.end(), src + sp, src + sp + litLen);
        sp += litLen;
        if (sp == srcLen) break;

        if (sp + 2 > srcLen) return false;
        size_t offset = static_cast<size_t>(src[sp]) | (static_cast<size_t>(src[sp + 1]) << 8);
        sp += 2;
        if (offset == 0 || offset > out.size()) return false;

        size_t matchLen = static_cast<size_t>(tok & 0x0Fu) + 4u;
        if ((tok & 0x0Fu) == 15u) {
            for (;;) {
                if (sp >= srcLen) return false;
                uint8_t b = src[sp++];
                matchLen += b;
                if (b != 255) break;
                if (matchLen > maxOut) return false;
            }
        }
        if (out.size() + matchLen > maxOut) return false;
        for (size_t i = 0; i < matchLen; ++i) {
            out.push_back(out[out.size() - offset]);
        }
    }
    return out.size() == expectedOut;
}

inline bool DecompressFstFastLzBlock(const uint8_t* src, size_t srcLen, size_t expectedOut, std::vector<uint8_t>& out, size_t maxOut = 32 * 1024 * 1024) {
    out.clear();
    if (!src || srcLen == 0 || expectedOut > maxOut) return false;
    int level = (src[0] >> 5) + 1;
    if (level != 1 && level != 2) return false;
    out.reserve(std::min<size_t>(expectedOut, 4 * 1024 * 1024));

    size_t sp = 0;
    uint32_t ctrl = src[sp++] & 31u;
    for (;;) {
        if (ctrl >= 32u) {
            size_t len = (ctrl >> 5) - 1u;
            size_t ofs = static_cast<size_t>(ctrl & 31u) << 8;
            if (level == 1) {
                if (len == 6u) {
                    if (sp >= srcLen) return false;
                    len += src[sp++];
                }
                if (sp >= srcLen) return false;
                ofs += src[sp++];
            } else {
                if (len == 6u) {
                    for (;;) {
                        if (sp >= srcLen) return false;
                        uint8_t code = src[sp++];
                        len += code;
                        if (code != 255) break;
                        if (len > maxOut) return false;
                    }
                }
                if (sp >= srcLen) return false;
                uint8_t code = src[sp++];
                ofs += code;
                if (code == 255 && (ctrl & 31u) == 31u) {
                    if (sp + 2 > srcLen) return false;
                    ofs = (31u << 8) + 255u + ((static_cast<size_t>(src[sp]) << 8) | src[sp + 1]);
                    sp += 2;
                }
            }
            size_t copyLen = len + 3u;
            size_t lookback = ofs + 1u;
            if (lookback == 0 || lookback > out.size() || out.size() + copyLen > maxOut) return false;
            for (size_t i = 0; i < copyLen; ++i) {
                out.push_back(out[out.size() - lookback]);
            }
        } else {
            size_t litLen = static_cast<size_t>(ctrl) + 1u;
            if (sp + litLen > srcLen || out.size() + litLen > maxOut) return false;
            out.insert(out.end(), src + sp, src + sp + litLen);
            sp += litLen;
        }
        if (sp >= srcLen) break;
        ctrl = src[sp++];
    }
    return expectedOut == 0 ? !out.empty() : (out.size() == expectedOut);
}

// Authentic GTKWave FST (.fst) Binary Waveform Reader (Zlib, LZ4, FastLZ, HIER_LZ4/LZ4DUO, GWRAPPER & Real Analog)
inline bool ParseGtkWaveFst(const uint8_t* data, size_t size, ParsedEdaBoard& out, int wrapDepth = 0) {
    if (!data || size < 33 || wrapDepth > 1) return false;

    // FST_BL_GWRAPPER (9): entire FST image wrapped in [type=9][seclen:8][uncompLen:8][gzip/zlib]
    if (data[0] == 9) {
        if (size < 26) return false;
        uint64_t secLen = DetailSiliconAndBusEda::RdU64BE(data + 1);
        if (secLen < 16 || 1 + secLen > size) return false;
        size_t payLen = static_cast<size_t>(secLen - 8);
        const uint8_t* pay = data + 9;
        uint64_t uncompLen = DetailSiliconAndBusEda::RdU64BE(pay);
        if (uncompLen < 33 || uncompLen > 128ULL * 1024 * 1024) return false;
        std::vector<uint8_t> inner;
        bool okW = false;
        if (payLen > 10 && pay[8] == 0x1F && pay[9] == 0x8B) {
            okW = ZipDeflate::InflateGzip(pay + 8, payLen - 8, inner, static_cast<size_t>(uncompLen) + 64);
        } else if (payLen > 10) {
            okW = ZipDeflate::InflateZlib(pay + 8, payLen - 8, inner, static_cast<size_t>(uncompLen) + 64);
        }
        if (!okW || inner.size() != uncompLen) return false;
        return ParseGtkWaveFst(inner.data(), inner.size(), out, wrapDepth + 1);
    }

    if (data[0] != 0) return false; // Must begin with FST_BL_HDR (0)

    struct FstHandleDef {
        std::wstring name;
        uint32_t bitLen = 1;
        bool isReal = false;
        std::vector<std::pair<uint64_t, std::string>> changes;
        std::vector<std::pair<uint64_t, double>> realChanges;
    };
    struct FstStaticAlias {
        std::wstring name;
        size_t targetZeroBased = 0;
    };
    std::vector<FstHandleDef> handles; // index 0 = handle 1
    std::vector<FstStaticAlias> staticAliases;
    uint64_t hdrStartTime = 0, hdrEndTime = 0;
    std::string writerName;
    int8_t timescale = -9;
    bool writerBigEndian = false;
    bool sawValidHdr = false;

    auto rdWriterDouble = [&](const uint8_t* p) -> double {
        double d = 0.0;
        if (writerBigEndian) {
            uint8_t rev[8] = { p[7], p[6], p[5], p[4], p[3], p[2], p[1], p[0] };
            std::memcpy(&d, rev, 8);
        } else {
            std::memcpy(&d, p, 8);
        }
        return d;
    };

    size_t pos = 0;
    while (pos < size) {
        if (pos + 9 > size) return false; // Truncated block header
        uint8_t blkType = data[pos];
        uint64_t blkLen = DetailSiliconAndBusEda::RdU64BE(data + pos + 1);
        if (blkLen < 8) return false;

        // In fstapi.c, seclen includes the 8-byte length field (next = pos + 1 + blkLen).
        size_t payStart = pos + 9;
        size_t payLen = 0;
        size_t nextPos = 0;
        if (blkLen - 8 <= size - payStart &&
            (pos + 1 + blkLen == size || (pos + 1 + blkLen < size && data[pos + 1 + blkLen] <= 9)))
        {
            payLen = static_cast<size_t>(blkLen - 8);
            nextPos = pos + 1 + static_cast<size_t>(blkLen);
        } else if (blkLen <= size - payStart) {
            payLen = static_cast<size_t>(blkLen);
            nextPos = payStart + payLen;
        } else {
            return false; // Truncated FST section payload!
        }

        const uint8_t* pay = data + payStart;

        if (blkType == 0) { // FST_BL_HDR
            if (payLen < 65) return false;
            hdrStartTime = DetailSiliconAndBusEda::RdU64BE(pay + 0);
            hdrEndTime   = DetailSiliconAndBusEda::RdU64BE(pay + 8);
            double dLE = 0.0, dBE = 0.0;
            std::memcpy(&dLE, pay + 16, 8);
            uint8_t rev[8] = { pay[23], pay[22], pay[21], pay[20], pay[19], pay[18], pay[17], pay[16] };
            std::memcpy(&dBE, rev, 8);
            bool okLE = (std::isfinite(dLE) && std::fabs(dLE - 2.718281828459045) < 1e-3);
            bool okBE = (std::isfinite(dBE) && std::fabs(dBE - 2.718281828459045) < 1e-3);
            if (!okLE && !okBE) return false;
            writerBigEndian = (!okLE && okBE);
            timescale = static_cast<int8_t>(pay[64]);
            if (payLen >= 193) {
                size_t wlen = 0;
                while (wlen < 128 && pay[65 + wlen] != 0) ++wlen;
                writerName.assign(reinterpret_cast<const char*>(pay + 65), wlen);
            }
            sawValidHdr = true;
        } else if (blkType == 4 || blkType == 5 || blkType == 6 || blkType == 7) {
            // FST_BL_HIER (4), FST_BL_HIER_GZ (5), FST_BL_HIER_LZ4 (6), FST_BL_HIER_LZ4DUO (7)
            std::vector<uint8_t> hierBuf;
            const uint8_t* hp = pay;
            size_t hlen = payLen;
            if (blkType == 5) {
                if (payLen < 10) return false;
                size_t zOff = (pay[8] == 0x1F && pay[9] == 0x8B) || ((pay[8] & 0x0F) == 0x08) ? 8 : 0;
                bool okZ = (pay[zOff] == 0x1F && pay[zOff + 1] == 0x8B)
                    ? ZipDeflate::InflateGzip(pay + zOff, payLen - zOff, hierBuf, 32 * 1024 * 1024)
                    : ZipDeflate::InflateZlib(pay + zOff, payLen - zOff, hierBuf, 32 * 1024 * 1024);
                if (!okZ || hierBuf.empty()) return false;
                hp = hierBuf.data();
                hlen = hierBuf.size();
            } else if (blkType == 6) {
                // FST_BL_HIER_LZ4: [uncompLen:8 BE][LZ4 raw block]
                if (payLen < 9) return false;
                uint64_t uncompLen = DetailSiliconAndBusEda::RdU64BE(pay);
                if (uncompLen == 0 || uncompLen > 32 * 1024 * 1024) return false;
                if (!DecompressFstLz4RawBlock(pay + 8, payLen - 8, static_cast<size_t>(uncompLen), hierBuf, 32 * 1024 * 1024)) {
                    return false;
                }
                hp = hierBuf.data();
                hlen = hierBuf.size();
            } else if (blkType == 7) {
                // FST_BL_HIER_LZ4DUO: [uncompLen:8 BE][midLen:8 BE][LZ4 outer -> LZ4 inner]
                if (payLen < 17) return false;
                uint64_t uncompLen = DetailSiliconAndBusEda::RdU64BE(pay + 0);
                uint64_t midLen    = DetailSiliconAndBusEda::RdU64BE(pay + 8);
                if (uncompLen == 0 || midLen == 0 || uncompLen > 32 * 1024 * 1024 || midLen > 32 * 1024 * 1024) return false;
                std::vector<uint8_t> midBuf;
                if (!DecompressFstLz4RawBlock(pay + 16, payLen - 16, static_cast<size_t>(midLen), midBuf, 32 * 1024 * 1024)) {
                    return false;
                }
                if (!DecompressFstLz4RawBlock(midBuf.data(), midBuf.size(), static_cast<size_t>(uncompLen), hierBuf, 32 * 1024 * 1024)) {
                    return false;
                }
                hp = hierBuf.data();
                hlen = hierBuf.size();
            }

            std::vector<std::string> scopeStack;
            size_t hpos = 0;
            while (hpos < hlen && handles.size() < 4096) {
                uint8_t tag = hp[hpos++];
                if (tag == 254) { // FST_ST_VCD_SCOPE
                    if (hpos >= hlen) break;
                    ++hpos; // scopeType
                    size_t s1 = hpos;
                    while (hpos < hlen && hp[hpos] != 0) ++hpos;
                    std::string sname(reinterpret_cast<const char*>(hp + s1), hpos - s1);
                    if (hpos < hlen) ++hpos;
                    while (hpos < hlen && hp[hpos] != 0) ++hpos; // compName
                    if (hpos < hlen) ++hpos;
                    if (!sname.empty()) scopeStack.push_back(std::move(sname));
                } else if (tag == 255) { // FST_ST_VCD_UPSCOPE
                    if (!scopeStack.empty()) scopeStack.pop_back();
                } else if (tag == 252) { // FST_ST_GEN_ATTRBEGIN
                    if (hpos + 2 > hlen) break;
                    hpos += 2;
                    while (hpos < hlen && hp[hpos] != 0) ++hpos;
                    if (hpos < hlen) ++hpos;
                    DetailSiliconAndBusEda::ReadULeb128(hp, hlen, hpos);
                } else if (tag == 253) { // FST_ST_GEN_ATTREND
                    continue;
                } else if (tag <= 30) { // FST_VT_* variable declaration
                    if (hpos >= hlen) break;
                    ++hpos; // varDir
                    size_t n1 = hpos;
                    while (hpos < hlen && hp[hpos] != 0) ++hpos;
                    std::string vname(reinterpret_cast<const char*>(hp + n1), hpos - n1);
                    if (hpos < hlen) ++hpos;
                    uint64_t bitLen = DetailSiliconAndBusEda::ReadULeb128(hp, hlen, hpos);
                    uint64_t alias  = DetailSiliconAndBusEda::ReadULeb128(hp, hlen, hpos);

                    std::string fullName;
                    for (const auto& sc : scopeStack) { fullName += sc; fullName.push_back('.'); }
                    fullName += vname;

                    if (alias == 0) {
                        FstHandleDef hd;
                        hd.name = PathUtils::Utf8ToWide(fullName);
                        hd.isReal = (tag == 3 || tag == 4 || tag == 20 || tag == 29);
                        hd.bitLen = hd.isReal ? 64 : static_cast<uint32_t>(std::clamp<uint64_t>(bitLen, 1, 512));
                        handles.push_back(std::move(hd));
                    } else if (alias <= handles.size() && staticAliases.size() < 256) {
                        staticAliases.push_back({ PathUtils::Utf8ToWide(fullName), static_cast<size_t>(alias - 1) });
                    }
                } else {
                    break;
                }
            }
        } else if ((blkType == 1 || blkType == 3 || blkType == 8) && !handles.empty() && payLen >= 56) {
            // FST_BL_VCDATA / DYN_ALIAS / DYN_ALIAS2
            uint64_t vcStart = DetailSiliconAndBusEda::RdU64BE(pay + 0);
            uint64_t vcEnd   = DetailSiliconAndBusEda::RdU64BE(pay + 8);
            hdrEndTime = std::max(hdrEndTime, vcEnd);

            // 1. Decode Time Table from end of VCDATA block
            uint64_t timeUncompLen = DetailSiliconAndBusEda::RdU64BE(pay + payLen - 24);
            uint64_t timeCompLen   = DetailSiliconAndBusEda::RdU64BE(pay + payLen - 16);
            uint64_t timeCount     = DetailSiliconAndBusEda::RdU64BE(pay + payLen - 8);
            if (timeCompLen > payLen - 32 || timeCount == 0 || timeCount > 1000000) {
                return false;
            }
            size_t timeOff = payLen - 24 - static_cast<size_t>(timeCompLen);
            std::vector<uint8_t> timeRaw;
            const uint8_t* tp = pay + timeOff;
            size_t tlen = static_cast<size_t>(timeCompLen);
            if (timeCompLen != timeUncompLen) {
                if (!ZipDeflate::InflateZlib(tp, tlen, timeRaw, 32 * 1024 * 1024)) return false;
                tp = timeRaw.data();
                tlen = timeRaw.size();
            }
            std::vector<uint64_t> timestamps;
            timestamps.reserve(static_cast<size_t>(std::min<uint64_t>(timeCount, 200000)));
            size_t tcur = 0;
            uint64_t accTime = 0;
            for (uint64_t ti = 0; ti < timeCount && tcur < tlen; ++ti) {
                accTime += DetailSiliconAndBusEda::ReadULeb128(tp, tlen, tcur);
                timestamps.push_back(accTime);
            }
            if (timestamps.empty()) timestamps.push_back(vcStart);

            // 2. Decode Initial Value Bits at pay + 24 (digital char-per-bit + 8-byte IEEE-754 double for isReal)
            size_t vpos = 24;
            uint64_t bitsUncomp = DetailSiliconAndBusEda::ReadULeb128(pay, timeOff, vpos);
            uint64_t bitsComp   = DetailSiliconAndBusEda::ReadULeb128(pay, timeOff, vpos);
            if (bitsComp > timeOff - vpos) return false;
            std::vector<uint8_t> initBits;
            if (bitsComp == bitsUncomp) {
                initBits.assign(pay + vpos, pay + vpos + static_cast<size_t>(bitsComp));
            } else if (bitsComp > 0) {
                if (!ZipDeflate::InflateZlib(pay + vpos, static_cast<size_t>(bitsComp), initBits, 16 * 1024 * 1024) &&
                    !DecompressFstFastLzBlock(pay + vpos, static_cast<size_t>(bitsComp), static_cast<size_t>(bitsUncomp), initBits, 16 * 1024 * 1024)) {
                    return false;
                }
            }
            vpos += static_cast<size_t>(bitsComp);

            size_t bitCursor = 0;
            uint64_t initT = timestamps.front();
            for (auto& h : handles) {
                if (h.isReal) {
                    if (bitCursor + 8 <= initBits.size()) {
                        double dv = rdWriterDouble(initBits.data() + bitCursor);
                        if (h.realChanges.empty() || h.realChanges.back().second != dv) {
                            h.realChanges.push_back({ initT, dv });
                        }
                        bitCursor += 8;
                    }
                } else if (bitCursor + h.bitLen <= initBits.size()) {
                    std::string initState(reinterpret_cast<const char*>(initBits.data() + bitCursor), h.bitLen);
                    if (h.changes.empty() || h.changes.back().second != initState) {
                        h.changes.push_back({ initT, initState });
                    }
                    bitCursor += h.bitLen;
                }
            }

            // 3. Waves header + Position table just before timeOff
            if (vpos + 2 > timeOff || timeOff < 8) { pos = nextPos; continue; }
            uint64_t wavesCount = DetailSiliconAndBusEda::ReadULeb128(pay, timeOff - 8, vpos);
            if (vpos >= timeOff - 8) { pos = nextPos; continue; }
            uint8_t packType = pay[vpos++];
            size_t wavesDataStart = vpos;

            uint64_t posTableLen = DetailSiliconAndBusEda::RdU64BE(pay + timeOff - 8);
            if (posTableLen > (timeOff - 8) - wavesDataStart) return false;
            size_t posTableStart = (timeOff - 8) - static_cast<size_t>(posTableLen);
            size_t wavesDataEnd = posTableStart;

            // Decode per-handle wave chunk lengths from position table
            size_t nHandles = std::min<size_t>(handles.size(), static_cast<size_t>(wavesCount));
            std::vector<int64_t> chunkLens(nHandles, 0); // >0 = byte len, <0 = dynamic alias (-handleIdx1Based)
            size_t pcur = posTableStart;
            if (blkType == 1) {
                for (size_t hi = 0; hi < nHandles && pcur < timeOff - 8; ++hi) {
                    chunkLens[hi] = static_cast<int64_t>(DetailSiliconAndBusEda::ReadULeb128(pay, timeOff - 8, pcur));
                }
            } else if (blkType == 3) {
                size_t hi = 0;
                while (hi < nHandles && pcur < timeOff - 8) {
                    uint64_t v = DetailSiliconAndBusEda::ReadULeb128(pay, timeOff - 8, pcur);
                    if (v == 0) {
                        uint64_t run = DetailSiliconAndBusEda::ReadULeb128(pay, timeOff - 8, pcur);
                        hi += std::max<size_t>(1, static_cast<size_t>(run));
                    } else if ((v & 1ULL) != 0) {
                        chunkLens[hi++] = -static_cast<int64_t>(v >> 1);
                    } else {
                        chunkLens[hi++] = static_cast<int64_t>(v >> 1);
                    }
                }
            } else { // blkType == 8 (DYN_ALIAS2)
                size_t hi = 0;
                while (hi < nHandles && pcur < timeOff - 8) {
                    if (pay[pcur] == 0) {
                        ++pcur;
                        uint64_t run = DetailSiliconAndBusEda::ReadULeb128(pay, timeOff - 8, pcur);
                        hi += std::max<size_t>(1, static_cast<size_t>(run));
                    } else {
                        chunkLens[hi++] = DetailSiliconAndBusEda::ReadSLeb128(pay, timeOff - 8, pcur);
                    }
                }
            }

            // 4. Decode each handle's wave transitions (Zlib 'Z'/0, LZ4 '4', FastLZ 'F', or uncompressed)
            size_t wcur = wavesDataStart;
            for (size_t hi = 0; hi < nHandles; ++hi) {
                if (chunkLens[hi] < 0) {
                    size_t aliasIdx = static_cast<size_t>(-chunkLens[hi]) - 1;
                    if (aliasIdx < hi) {
                        handles[hi].changes = handles[aliasIdx].changes;
                        handles[hi].realChanges = handles[aliasIdx].realChanges;
                    }
                    continue;
                }
                if (chunkLens[hi] == 0) continue;
                size_t clen = static_cast<size_t>(chunkLens[hi]);
                if (clen > wavesDataEnd - wcur) return false;
                const uint8_t* cptr = pay + wcur;
                wcur += clen;

                size_t subPos = 0;
                uint64_t uncompWaveLen = DetailSiliconAndBusEda::ReadULeb128(cptr, clen, subPos);
                std::vector<uint8_t> waveRaw;
                const uint8_t* wp = cptr + subPos;
                size_t wlen = clen - subPos;
                if (uncompWaveLen != 0) {
                    if (uncompWaveLen > 32 * 1024 * 1024) return false;
                    if (packType == '4') {
                        if (!DecompressFstLz4RawBlock(wp, wlen, static_cast<size_t>(uncompWaveLen), waveRaw, 32 * 1024 * 1024)) return false;
                    } else if (packType == 'F') {
                        if (!DecompressFstFastLzBlock(wp, wlen, static_cast<size_t>(uncompWaveLen), waveRaw, 32 * 1024 * 1024)) return false;
                    } else if (packType == 'Z' || packType == 0) {
                        if (!ZipDeflate::InflateZlib(wp, wlen, waveRaw, 32 * 1024 * 1024)) return false;
                    } else {
                        return false;
                    }
                    wp = waveRaw.data();
                    wlen = waveRaw.size();
                }

                FstHandleDef& hd = handles[hi];
                size_t vp = 0;
                size_t timeIdx = 0;
                if (hd.isReal) {
                    while (vp < wlen && hd.realChanges.size() < 8192) {
                        uint64_t vli = DetailSiliconAndBusEda::ReadULeb128(wp, wlen, vp);
                        timeIdx += static_cast<size_t>(vli >> 1);
                        double dv = 0.0;
                        if ((vli & 1ULL) == 0) {
                            if (vp + 8 > wlen) break;
                            dv = rdWriterDouble(wp + vp);
                            vp += 8;
                        } else {
                            size_t s = vp;
                            while (vp < wlen && wp[vp] != 0) ++vp;
                            char b[64] = {};
                            std::memcpy(b, wp + s, std::min<size_t>(vp - s, 63));
                            if (vp < wlen) ++vp;
                            dv = std::strtod(b, nullptr);
                        }
                        if (timeIdx < timestamps.size()) {
                            hd.realChanges.push_back({ timestamps[timeIdx], dv });
                        }
                    }
                    continue;
                }

                while (vp < wlen && hd.changes.size() < 8192) {
                    uint64_t vli = DetailSiliconAndBusEda::ReadULeb128(wp, wlen, vp);
                    if (hd.bitLen == 1) {
                        if ((vli & 1ULL) == 0) {
                            timeIdx += static_cast<size_t>(vli >> 2);
                            char bitCh = ((vli >> 1) & 1ULL) ? '1' : '0';
                            if (timeIdx < timestamps.size()) {
                                hd.changes.push_back({ timestamps[timeIdx], std::string(1, bitCh) });
                            }
                        } else {
                            timeIdx += static_cast<size_t>(vli >> 4);
                            static const char kStates[8] = { 'x','z','h','u','w','l','-','?' };
                            char stCh = kStates[(vli >> 1) & 7ULL];
                            if (timeIdx < timestamps.size()) {
                                hd.changes.push_back({ timestamps[timeIdx], std::string(1, stCh) });
                            }
                        }
                    } else {
                        timeIdx += static_cast<size_t>(vli >> 1);
                        std::string bits;
                        bits.reserve(hd.bitLen);
                        if ((vli & 1ULL) == 0) {
                            size_t nBytes = (hd.bitLen + 7u) / 8u;
                            if (vp + nBytes > wlen) break;
                            for (uint32_t bi = 0; bi < hd.bitLen; ++bi) {
                                uint8_t b = wp[vp + (bi / 8u)];
                                int shift = 7 - static_cast<int>(bi % 8u);
                                bits.push_back(((b >> shift) & 1u) ? '1' : '0');
                            }
                            vp += nBytes;
                        } else {
                            if (vp + hd.bitLen > wlen) break;
                            bits.assign(reinterpret_cast<const char*>(wp + vp), hd.bitLen);
                            vp += hd.bitLen;
                        }
                        if (timeIdx < timestamps.size()) {
                            hd.changes.push_back({ timestamps[timeIdx], std::move(bits) });
                        }
                    }
                }
            }
        }

        pos = nextPos;
    }

    if (!sawValidHdr || handles.empty()) return false;

    std::vector<DecodedWaveSignal> sigs;
    sigs.reserve(handles.size() + staticAliases.size());
    for (const auto& h : handles) {
        DecodedWaveSignal ds;
        ds.name = h.name;
        ds.bitWidth = static_cast<int>(h.bitLen);
        ds.isReal = h.isReal;
        ds.changes = h.changes;
        ds.realChanges = h.realChanges;
        sigs.push_back(std::move(ds));
    }
    for (const auto& al : staticAliases) {
        if (al.targetZeroBased < handles.size() && sigs.size() < 48) {
            const auto& h = handles[al.targetZeroBased];
            DecodedWaveSignal ds;
            ds.name = al.name;
            ds.bitWidth = static_cast<int>(h.bitLen);
            ds.isReal = h.isReal;
            ds.changes = h.changes;
            ds.realChanges = h.realChanges;
            sigs.push_back(std::move(ds));
        }
    }
    std::wstring title = L"GTKWave FST Waveform (" + std::to_wstring(sigs.size()) + L" signals";
    if (!writerName.empty()) title += L", " + PathUtils::Utf8ToWide(writerName);
    title += L", 10^" + std::to_wstring(static_cast<int>(timescale)) + L"s)";
    return RenderWaveSignalsToBoard(sigs, hdrStartTime, hdrEndTime, title, out);
}

} // namespace DetailHwBin

inline bool ParseGtkWaveFstBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 16) return false;
    return DetailHwBin::ParseGtkWaveFst(data, size, out);
}

} // namespace Formats
