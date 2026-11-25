#pragma once
// AppleDmgDisk.h — Apple UDIF Disk Image reader (.dmg — "koly" trailer + mish runs).

#include "../common/ArchiveFormatCommon.h"
#include "../common/VirtualDiskCommon.h"
#include "../../LzfseDecode.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

inline bool BuildAppleDmgDiskReader(
    const uint8_t* data,
    size_t size,
    VirtualDiskReader& outReader)
{
    outReader = {};
    if (!data || size < 512) return false;

    auto decompressAppleAdc = [](const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t expectedLen) -> bool {
        out.clear();
        if (!src || expectedLen > 32 * 1024 * 1024) return false;
        out.reserve(expectedLen);
        size_t p = 0;
        while (p < srcLen && out.size() < expectedLen) {
            uint8_t b = src[p++];
            if ((b & 0x80u) != 0) {
                size_t litLen = static_cast<size_t>(b & 0x7Fu) + 1u;
                if (litLen > srcLen - p || litLen > expectedLen - out.size()) return false;
                out.insert(out.end(), src + p, src + p + litLen);
                p += litLen;
            } else if ((b & 0x40u) != 0) {
                if (srcLen - p < 2) return false;
                uint8_t b1 = src[p++];
                uint8_t b2 = src[p++];
                size_t matchLen = static_cast<size_t>(b & 0x3Fu) + 4u;
                size_t matchOff = (static_cast<size_t>(b1) << 8) + static_cast<size_t>(b2) + 1u;
                if (matchOff > out.size() || matchLen > expectedLen - out.size()) return false;
                for (size_t k = 0; k < matchLen; ++k) {
                    out.push_back(out[out.size() - matchOff]);
                }
            } else {
                if (srcLen - p < 1) return false;
                uint8_t b1 = src[p++];
                size_t matchLen = static_cast<size_t>((b >> 2) & 0x0Fu) + 3u;
                size_t matchOff = (static_cast<size_t>(b & 0x03u) << 8) + static_cast<size_t>(b1) + 1u;
                if (matchOff > out.size() || matchLen > expectedLen - out.size()) return false;
                for (size_t k = 0; k < matchLen; ++k) {
                    out.push_back(out[out.size() - matchOff]);
                }
            }
        }
        return out.size() == expectedLen;
    };

    auto decompressDmgLzma = [](const uint8_t* cSrc, size_t cSz, std::vector<uint8_t>& dec, size_t uSz) -> bool {
        // Apple UDIF 0x80000008 (kUDIFBlockTypeLZMA) stores standard XZ streams (FD 37 7A 58 5A 00),
        // or in some third-party tools raw LZMA1 / LZMA-alone headers.
        if (cSz >= 6 && cSrc[0] == 0xFDu && std::memcmp(cSrc + 1, "7zXZ\0", 5) == 0) {
            return StreamCodecs::DecompressXzStream(cSrc, cSz, dec, uSz) && dec.size() == uSz;
        }
        if (StreamCodecs::DecompressXzStream(cSrc, cSz, dec, uSz) && dec.size() == uSz) {
            return true;
        }
        if (cSz >= 13) {
            const uint8_t props = cSrc[0];
            const uint32_t dictSz = RdU32LE(cSrc + 1);
            const uint64_t hdrUncomp = RdU64LE(cSrc + 5);
            const bool known = (hdrUncomp != UINT64_MAX);
            if (props < 225u && (!known || hdrUncomp == uSz)) {
                if (StreamCodecs::DecompressLzma1(props, dictSz, cSrc + 13, cSz - 13, dec, uSz, true, uSz) &&
                    dec.size() == uSz) {
                    return true;
                }
            }
        }
        if (cSz >= 5 && cSrc[0] < 225u) {
            const uint8_t props = cSrc[0];
            const uint32_t dictSz = RdU32LE(cSrc + 1);
            if (StreamCodecs::DecompressLzma1(props, dictSz, cSrc + 5, cSz - 5, dec, uSz, true, uSz) &&
                dec.size() == uSz) {
                return true;
            }
        }
        return false;
    };

    // Apple Disk Image (.dmg — UDIF "koly" trailer)
    if (size >= 512 && std::memcmp(data + size - 512, "koly", 4) == 0) {
        const size_t kolyOff = size - 512;
        const uint8_t* koly = data + kolyOff;
        uint64_t dataForkOff = RdU64BE(koly + 24);
        uint64_t xmlOff      = RdU64BE(koly + 216);
        uint64_t xmlLen      = RdU64BE(koly + 224);
        uint64_t secCount    = RdU64BE(koly + 492);
        if (secCount > (UINT64_MAX / 512ULL)) return false;
        uint64_t virtSize    = (secCount > 0) ? (secCount * 512ULL) : kolyOff;

        struct DmgRun {
            uint32_t type;
            uint64_t diskStart;
            uint64_t diskLen;
            uint64_t compOff;
            uint64_t compLen;
        };
        auto runs = std::make_shared<std::vector<DmgRun>>();

        if (xmlLen > 0) {
            if (xmlOff > kolyOff || xmlLen > kolyOff - xmlOff || xmlLen < 16 || xmlLen > 16 * 1024 * 1024) {
                return false;
            }
            std::string_view xml(reinterpret_cast<const char*>(data + static_cast<size_t>(xmlOff)), static_cast<size_t>(xmlLen));
            size_t searchPos = 0;
            uint64_t mishPartBaseSec = 0;
            while (searchPos < xml.size() && runs->size() < 65536) {
                size_t d1 = xml.find("<data>", searchPos);
                if (d1 == std::string_view::npos) break;
                d1 += 6;
                size_t d2 = xml.find("</data>", d1);
                if (d2 == std::string_view::npos) break;
                searchPos = d2 + 7;

                std::vector<uint8_t> mish;
                if (Base64DecodeBlock(xml.substr(d1, d2 - d1), mish) &&
                    mish.size() >= 244 && std::memcmp(mish.data(), "mish", 4) == 0)
                {
                    uint64_t firstSec = RdU64BE(mish.data() + 8);
                    uint64_t partSecs = RdU64BE(mish.data() + 16);
                    uint64_t mishDataStart = RdU64BE(mish.data() + 24);
                    uint32_t nBlocks  = RdU32BE(mish.data() + 200);
                    uint64_t baseSec  = (firstSec > 0 || runs->empty()) ? firstSec : mishPartBaseSec;
                    for (uint32_t b = 0; b < nBlocks && static_cast<uint64_t>(b + 1ULL) * 40ULL <= mish.size() - 204u; ++b) {
                        const uint8_t* ce = mish.data() + 204 + static_cast<size_t>(b) * 40;
                        uint32_t et = RdU32BE(ce + 0);
                        if (et == 0xFFFFFFFFu) break;
                        if (et == 0x7FFFFFFEu) continue; // BT_COMMENT
                        uint64_t sNum = RdU64BE(ce + 8);
                        uint64_t sCnt = RdU64BE(ce + 16);
                        uint64_t cOff = RdU64BE(ce + 24);
                        uint64_t cLen = RdU64BE(ce + 32);
                        if (sCnt == 0) continue;
                        if (baseSec > (UINT64_MAX / 512ULL) - sNum) return false;
                        const uint64_t runSecStart = baseSec + sNum;
                        if (sCnt > (UINT64_MAX / 512ULL) - runSecStart) return false;
                        if (dataForkOff > UINT64_MAX - mishDataStart) return false;
                        const uint64_t forkBase = dataForkOff + mishDataStart;
                        if (cOff > UINT64_MAX - forkBase) return false;
                        runs->push_back({ et, runSecStart * 512ULL, sCnt * 512ULL, forkBase + cOff, cLen });
                    }
                    if (partSecs <= UINT64_MAX - baseSec) {
                        mishPartBaseSec = baseSec + partSecs;
                    }
                }
            }
        }

        if (!runs->empty()) {
            uint64_t maxEnd = virtSize;
            for (const auto& r : *runs) {
                if (r.diskLen <= UINT64_MAX - r.diskStart) {
                    maxEnd = std::max(maxEnd, r.diskStart + r.diskLen);
                }
            }
            struct DmgCache {
                size_t runIdx = SIZE_MAX;
                std::vector<uint8_t> dec;
            };
            auto runCache = std::make_shared<DmgCache>();
            outReader.virtualSize = maxEnd;
            outReader.formatName  = L"Apple Disk Image (DMG)";
            outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
                if (diskOff > maxEnd || len > maxEnd - diskOff) return false;
                if (len == 0) return true;
                std::memset(dst, 0, len);
                const uint64_t qEnd = diskOff + len;
                for (size_t ri = 0; ri < runs->size(); ++ri) {
                    const auto& r = (*runs)[ri];
                    const uint64_t rEnd = r.diskStart + r.diskLen;
                    if (rEnd <= diskOff || r.diskStart >= qEnd) continue;
                    const uint64_t ovStart = std::max(diskOff, r.diskStart);
                    const uint64_t ovEnd   = std::min(qEnd, rEnd);
                    const size_t copyLen   = static_cast<size_t>(ovEnd - ovStart);
                    const size_t dstOff    = static_cast<size_t>(ovStart - diskOff);
                    const size_t runRel    = static_cast<size_t>(ovStart - r.diskStart);

                    if (r.type == 0x00000000u || r.type == 0x00000002u) {
                        // BT_ZERO / BT_IGNORE
                        std::memset(dst + dstOff, 0, copyLen);
                    } else if (r.type == 0x00000001u) {
                        // BT_RAW
                        if (r.compOff > size || runRel > size - static_cast<size_t>(r.compOff) ||
                            copyLen > size - static_cast<size_t>(r.compOff) - runRel) {
                            return false;
                        }
                        std::memcpy(dst + dstOff, data + static_cast<size_t>(r.compOff) + runRel, copyLen);
                    } else if (r.type == 0x80000004u || r.type == 0x80000005u || r.type == 0x80000006u ||
                               r.type == 0x80000007u || r.type == 0x80000008u)
                    {
                        if (r.compOff > size || r.compLen > size - static_cast<size_t>(r.compOff) ||
                            r.diskLen > 32ULL * 1024 * 1024) {
                            return false;
                        }
                        if (runCache->runIdx != ri) {
                            std::vector<uint8_t> dec;
                            bool ok = false;
                            const uint8_t* cSrc = data + static_cast<size_t>(r.compOff);
                            const size_t cSz = static_cast<size_t>(r.compLen);
                            const size_t uSz = static_cast<size_t>(r.diskLen);
                            if (r.type == 0x80000004u) {
                                ok = decompressAppleAdc(cSrc, cSz, dec, uSz);
                            } else if (r.type == 0x80000005u) {
                                ok = (ZipDeflate::InflateZlib(cSrc, cSz, dec, uSz) && dec.size() == uSz) ||
                                     (ZipDeflate::InflateRaw(cSrc, cSz, dec, uSz) && dec.size() == uSz);
                            } else if (r.type == 0x80000006u) {
                                ok = StreamCodecs::DecompressBzip2Stream(cSrc, cSz, dec, uSz) && dec.size() == uSz;
                            } else if (r.type == 0x80000007u) {
                                ok = ::Explorer2::Lzfse::Decompress(cSrc, cSz, dec, uSz) && dec.size() == uSz;
                            } else if (r.type == 0x80000008u) {
                                ok = decompressDmgLzma(cSrc, cSz, dec, uSz);
                            }
                            if (!ok || dec.size() != uSz) return false;
                            runCache->runIdx = ri;
                            runCache->dec = std::move(dec);
                        }
                        if (runRel > runCache->dec.size() || copyLen > runCache->dec.size() - runRel) return false;
                        std::memcpy(dst + dstOff, runCache->dec.data() + runRel, copyLen);
                    } else {
                        // Never silently return zero-filled data on unknown or unsupported block run types
                        return false;
                    }
                }
                return true;
            };
            outReader.allowRawSliceFallback = false;
            return true;
        }

        if (xmlLen > 0) {
            return false;
        }

        // Uncompressed raw data fork before koly trailer (only when no blkx XML table is present)
        size_t rawEnd = kolyOff;
        outReader.virtualSize = rawEnd;
        outReader.formatName  = L"Apple Disk Image (DMG)";
        outReader.directRawBuffer = true;
        outReader.allowRawSliceFallback = false;
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff > rawEnd || len > rawEnd - static_cast<size_t>(diskOff)) return false;
            std::memcpy(dst, data + static_cast<size_t>(diskOff), len);
            return true;
        };
        return true;
    }

    return false;
}

} // namespace DetailDiskAndGameArc

inline bool ParseOrExtractAppleDmgDisk(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    DetailDiskAndGameArc::VirtualDiskReader reader;
    if (!DetailDiskAndGameArc::BuildAppleDmgDiskReader(data, size, reader)) return false;
    return DetailDiskAndGameArc::InspectOrExtractVirtualDiskReader(reader, size, outInfo, extractTargetNorm, outExtracted);
}

inline bool InspectAppleDmgDisk(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractAppleDmgDisk(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractAppleDmgDiskEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return ParseOrExtractAppleDmgDisk(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
