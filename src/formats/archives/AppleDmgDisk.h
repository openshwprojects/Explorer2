#pragma once
// AppleDmgDisk.h — Apple UDIF Disk Image reader (.dmg — "koly" trailer + mish runs).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

inline bool BuildAppleDmgDiskReader(
    const uint8_t* data,
    size_t size,
    VirtualDiskReader& outReader)
{
    outReader = {};
    if (!data || size < 512) return false;

    auto decompressAppleAdc = [](const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t expectedLen) -> bool {
        out.clear();
        if (!src || expectedLen > 16 * 1024 * 1024) return false;
        out.reserve(expectedLen);
        size_t p = 0;
        while (p < srcLen && out.size() < expectedLen) {
            uint8_t b = src[p++];
            if ((b & 0x80u) != 0) {
                size_t litLen = static_cast<size_t>(b & 0x7Fu) + 1u;
                if (p + litLen > srcLen || out.size() + litLen > expectedLen) return false;
                out.insert(out.end(), src + p, src + p + litLen);
                p += litLen;
            } else if ((b & 0x40u) != 0) {
                if (p + 2 > srcLen) return false;
                uint8_t b1 = src[p++];
                uint8_t b2 = src[p++];
                size_t matchLen = static_cast<size_t>(b & 0x3Fu) + 4u;
                size_t matchOff = (static_cast<size_t>(b1) << 8) + static_cast<size_t>(b2) + 1u;
                if (matchOff > out.size() || out.size() + matchLen > expectedLen) return false;
                for (size_t k = 0; k < matchLen; ++k) {
                    out.push_back(out[out.size() - matchOff]);
                }
            } else {
                if (p + 1 > srcLen) return false;
                uint8_t b1 = src[p++];
                size_t matchLen = static_cast<size_t>((b >> 2) & 0x0Fu) + 3u;
                size_t matchOff = (static_cast<size_t>(b & 0x03u) << 8) + static_cast<size_t>(b1) + 1u;
                if (matchOff > out.size() || out.size() + matchLen > expectedLen) return false;
                for (size_t k = 0; k < matchLen; ++k) {
                    out.push_back(out[out.size() - matchOff]);
                }
            }
        }
        return out.size() == expectedLen;
    };

    // 6. Apple Disk Image (.dmg — UDIF "koly" trailer or raw disk sectors)
    if (size >= 512 && std::memcmp(data + size - 512, "koly", 4) == 0) {
        const uint8_t* koly = data + size - 512;
        uint64_t dataForkOff = RdU64BE(koly + 24);
        uint64_t xmlOff      = RdU64BE(koly + 216);
        uint64_t xmlLen      = RdU64BE(koly + 224);
        uint64_t secCount    = RdU64BE(koly + 492);
        uint64_t virtSize    = (secCount > 0) ? (secCount * 512ULL) : (size - 512);

        struct DmgRun {
            uint32_t type;
            uint64_t diskStart;
            uint64_t diskLen;
            uint64_t compOff;
            uint64_t compLen;
        };
        auto runs = std::make_shared<std::vector<DmgRun>>();

        if (xmlOff + xmlLen <= size - 512 && xmlLen > 16 && xmlLen <= 16 * 1024 * 1024) {
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
                    for (uint32_t b = 0; b < nBlocks && 204u + (b + 1) * 40u <= mish.size(); ++b) {
                        const uint8_t* ce = mish.data() + 204 + b * 40;
                        uint32_t et = RdU32BE(ce + 0);
                        if (et == 0xFFFFFFFFu) break;
                        if (et == 0x7FFFFFFEu) continue; // BT_COMMENT
                        uint64_t sNum = RdU64BE(ce + 8);
                        uint64_t sCnt = RdU64BE(ce + 16);
                        uint64_t cOff = RdU64BE(ce + 24);
                        uint64_t cLen = RdU64BE(ce + 32);
                        if (sCnt == 0) continue;
                        runs->push_back({ et, (baseSec + sNum) * 512ULL, sCnt * 512ULL, dataForkOff + mishDataStart + cOff, cLen });
                    }
                    mishPartBaseSec = baseSec + partSecs;
                }
            }
        }

        if (!runs->empty()) {
            uint64_t maxEnd = virtSize;
            for (const auto& r : *runs) maxEnd = std::max(maxEnd, r.diskStart + r.diskLen);
            struct DmgCache {
                size_t runIdx = SIZE_MAX;
                std::vector<uint8_t> dec;
            };
            auto runCache = std::make_shared<DmgCache>();
            outReader.virtualSize = maxEnd;
            outReader.formatName  = L"Apple Disk Image (DMG)";
            outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
                if (diskOff + len > maxEnd) return false;
                std::memset(dst, 0, len);
                for (size_t ri = 0; ri < runs->size(); ++ri) {
                    const auto& r = (*runs)[ri];
                    uint64_t rEnd = r.diskStart + r.diskLen;
                    uint64_t qEnd = diskOff + len;
                    if (rEnd <= diskOff || r.diskStart >= qEnd) continue;
                    uint64_t ovStart = std::max(diskOff, r.diskStart);
                    uint64_t ovEnd   = std::min(qEnd, rEnd);
                    size_t copyLen   = static_cast<size_t>(ovEnd - ovStart);
                    size_t dstOff    = static_cast<size_t>(ovStart - diskOff);
                    size_t runRel    = static_cast<size_t>(ovStart - r.diskStart);

                    if (r.type == 0x00000000u || r.type == 0x00000002u) {
                        // Zero-fill / Ignore run
                        std::memset(dst + dstOff, 0, copyLen);
                    } else if (r.type == 0x00000001u) {
                        if (r.compOff + runRel + copyLen > size) return false;
                        std::memcpy(dst + dstOff, data + static_cast<size_t>(r.compOff) + runRel, copyLen);
                    } else if (r.type == 0x80000004u || r.type == 0x80000005u || r.type == 0x80000006u) {
                        if (r.compOff + r.compLen > size || r.diskLen > 16 * 1024 * 1024) return false;
                        if (runCache->runIdx != ri) {
                            std::vector<uint8_t> dec;
                            bool ok = false;
                            const uint8_t* cSrc = data + static_cast<size_t>(r.compOff);
                            size_t cSz = static_cast<size_t>(r.compLen);
                            size_t uSz = static_cast<size_t>(r.diskLen);
                            if (r.type == 0x80000004u) {
                                ok = decompressAppleAdc(cSrc, cSz, dec, uSz);
                            } else if (r.type == 0x80000005u) {
                                ok = ZipDeflate::InflateZlib(cSrc, cSz, dec, uSz) ||
                                     ZipDeflate::InflateRaw(cSrc, cSz, dec, uSz);
                            } else if (r.type == 0x80000006u) {
                                ok = StreamCodecs::DecompressBzip2Stream(cSrc, cSz, dec, uSz);
                            }
                            if (!ok) return false;
                            runCache->runIdx = ri;
                            runCache->dec = std::move(dec);
                        }
                        if (runRel + copyLen > runCache->dec.size()) return false;
                        std::memcpy(dst + dstOff, runCache->dec.data() + runRel, copyLen);
                    }
                }
                return true;
            };
            outReader.allowRawSliceFallback = false;
            return true;
        }

        // Raw data fork before koly trailer
        size_t rawEnd = size - 512;
        outReader.virtualSize = rawEnd;
        outReader.formatName  = L"Apple Disk Image (DMG)";
        outReader.directRawBuffer = true;
        outReader.allowRawSliceFallback = false;
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff + len > rawEnd) return false;
            std::memcpy(dst, data + static_cast<size_t>(diskOff), len);
            return true;
        };
        return true;
    }

    return false;
}

} // namespace DetailArc100

inline bool ParseOrExtractAppleDmgDisk(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    DetailArc100::VirtualDiskReader reader;
    if (!DetailArc100::BuildAppleDmgDiskReader(data, size, reader)) return false;
    return DetailArc100::InspectOrExtractVirtualDiskReader(reader, size, outInfo, extractTargetNorm, outExtracted);
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
