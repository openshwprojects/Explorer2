#pragma once
// FujifilmRaf.h — Fujifilm RAF Exact Header-Offset Bayer/X-Trans CFA & Preview RAW Decoder (.raf)

namespace Formats {

inline bool DecodeFujifilmRaf(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 108u || std::memcmp(data, "FUJIFILMCCD-RAW", 15) != 0) return false;
    const std::wstring label = L"Fujifilm RAF RAW (.raf)";

    // Fujifilm RAF big-endian directory header offsets:
    //   84..87: JPEG preview offset,  88..91: JPEG preview byte length
    //   92..95: CFA header offset,    96..99: CFA header byte length
    //  100..103: CFA data offset,   104..107: CFA data byte length
    const uint32_t jpgOff     = DetailLayeredAndHdrImg::RdU32BE(data + 84);
    const uint32_t jpgLen     = DetailLayeredAndHdrImg::RdU32BE(data + 88);
    const uint32_t cfaHdrOff  = DetailLayeredAndHdrImg::RdU32BE(data + 92);
    const uint32_t cfaHdrLen  = DetailLayeredAndHdrImg::RdU32BE(data + 96);
    const uint32_t cfaDataOff = DetailLayeredAndHdrImg::RdU32BE(data + 100);
    const uint32_t cfaDataLen = DetailLayeredAndHdrImg::RdU32BE(data + 104);

    // 1. Decode Fujifilm RAF CFA sensor data at exact [cfaHdrOff..+cfaHdrLen] & [cfaDataOff..+cfaDataLen]
    if (cfaDataOff >= 108u && cfaDataLen >= 16u && static_cast<uint64_t>(cfaDataOff) + cfaDataLen <= size) {
        if (DetailVectorAndRawImg::ParseTiffRawIfds(data + cfaDataOff, cfaDataLen, label, outInfo)) {
            return true;
        }
    }

    if (cfaHdrOff >= 108u && static_cast<uint64_t>(cfaHdrOff) + 8u <= size &&
        cfaDataOff >= 108u && cfaDataLen > 0u && static_cast<uint64_t>(cfaDataOff) + cfaDataLen <= size)
    {
        const size_t hdrEnd = (cfaHdrLen >= 8u && static_cast<uint64_t>(cfaHdrOff) + cfaHdrLen <= size)
                                  ? static_cast<size_t>(cfaHdrOff + cfaHdrLen)
                                  : size;
        const uint32_t numRecs = DetailLayeredAndHdrImg::RdU32BE(data + cfaHdrOff);
        size_t rp = static_cast<size_t>(cfaHdrOff) + 4u;
        uint32_t rawW = 0, rawH = 0;
        uint32_t cropTop = 0, cropLeft = 0, cropH = 0, cropW = 0;
        bool hasCropOrigin = false, hasCropSize = false;
        bool hasWb = false;
        float neutral[3] = { 1.0f, 1.0f, 1.0f };
        std::vector<uint8_t> cfaPat = { 0, 1, 1, 2 };
        uint16_t cfaRows = 2, cfaCols = 2;

        for (uint32_t r = 0; r < std::min<uint32_t>(numRecs, 256u) && rp + 4u <= hdrEnd; ++r) {
            const uint16_t rTag = DetailLayeredAndHdrImg::RdU16BE(data + rp);
            const uint16_t rLen = DetailLayeredAndHdrImg::RdU16BE(data + rp + 2);
            rp += 4u;
            if (rp + rLen > hdrEnd) break;
            if (rTag == 0x0100u && rLen >= 4u) {
                rawH = DetailLayeredAndHdrImg::RdU16BE(data + rp);
                rawW = DetailLayeredAndHdrImg::RdU16BE(data + rp + 2);
            } else if (rTag == 0x0110u && rLen >= 4u) {
                cropTop  = DetailLayeredAndHdrImg::RdU16BE(data + rp);
                cropLeft = DetailLayeredAndHdrImg::RdU16BE(data + rp + 2);
                hasCropOrigin = true;
            } else if (rTag == 0x0111u && rLen >= 4u) {
                cropH = DetailLayeredAndHdrImg::RdU16BE(data + rp);
                cropW = DetailLayeredAndHdrImg::RdU16BE(data + rp + 2);
                hasCropSize = true;
            } else if (rTag == 0x0131u && rLen == 36u) {
                bool validXTrans = true;
                for (int k = 0; k < 36; ++k) {
                    if (data[rp + k] > 2u) { validXTrans = false; break; }
                }
                if (validXTrans) {
                    cfaRows = 6;
                    cfaCols = 6;
                    cfaPat.assign(data + rp, data + rp + 36);
                }
            } else if (rTag == 0x2FF0u && rLen >= 8u) {
                const uint16_t wbG = DetailLayeredAndHdrImg::RdU16BE(data + rp + 0);
                const uint16_t wbR = DetailLayeredAndHdrImg::RdU16BE(data + rp + 2);
                const uint16_t wbB = DetailLayeredAndHdrImg::RdU16BE(data + rp + 6);
                if (wbG > 0 && wbR > 0 && wbB > 0) {
                    neutral[0] = static_cast<float>(wbG) / static_cast<float>(wbR);
                    neutral[1] = 1.0f;
                    neutral[2] = static_cast<float>(wbG) / static_cast<float>(wbB);
                    hasWb = true;
                }
            }
            rp += rLen;
        }

        if (rawW >= 2u && rawH >= 2u && rawW <= 16384u && rawH <= 16384u) {
            const size_t pxCount = static_cast<size_t>(rawW) * rawH;
            const uint8_t* rawBytes = data + cfaDataOff;
            size_t rawBytesLen = cfaDataLen;
            if (rawBytesLen > pxCount * 2u && (rawBytesLen - pxCount * 2u) <= 4096u &&
                ((rawBytesLen - pxCount * 2u) % 512u) == 0u)
            {
                const size_t hdrSkip = rawBytesLen - pxCount * 2u;
                rawBytes += hdrSkip;
                rawBytesLen -= hdrSkip;
            }

            std::vector<uint16_t> samples;
            uint16_t bps = 14;
            if (rawBytesLen >= pxCount * 2u) {
                size_t leSmall = 0, beSmall = 0;
                const size_t probeN = std::min<size_t>(pxCount, 512u);
                for (size_t k = 0; k < probeN; ++k) {
                    if (DetailLayeredAndHdrImg::RdU16LE(rawBytes + k * 2u) <= 16383u) ++leSmall;
                    if (DetailLayeredAndHdrImg::RdU16BE(rawBytes + k * 2u) <= 16383u) ++beSmall;
                }
                const bool useBe = (beSmall > leSmall);
                bps = 16;
                DetailVectorAndRawImg::UnpackRawSamplesFromBytes(rawBytes, rawBytesLen, rawW, rawH, 1, 16, useBe, 1, samples);
            } else if (rawBytesLen >= (pxCount * 14u + 7u) / 8u) {
                bps = 14;
                DetailVectorAndRawImg::UnpackRawSamplesFromBytes(rawBytes, rawBytesLen, rawW, rawH, 1, 14, true, 1, samples);
            } else if (rawBytesLen >= (pxCount * 12u + 7u) / 8u) {
                bps = 12;
                DetailVectorAndRawImg::UnpackRawSamplesFromBytes(rawBytes, rawBytesLen, rawW, rawH, 1, 12, true, 1, samples);
            }

            if (samples.size() == pxCount) {
                bool useAct = false;
                uint32_t act[4] = {};
                if (hasCropSize && cropW >= 2u && cropH >= 2u) {
                    const uint32_t ct = hasCropOrigin ? cropTop : 0u;
                    const uint32_t cl = hasCropOrigin ? cropLeft : 0u;
                    if (ct + cropH <= rawH && cl + cropW <= rawW) {
                        act[0] = ct;
                        act[1] = cl;
                        act[2] = ct + cropH;
                        act[3] = cl + cropW;
                        useAct = true;
                    }
                }
                DetailVectorAndRawImg::DemosaicAndToneMapSensor(
                    samples, rawW, rawH, 1, 32803, bps,
                    cfaRows, cfaCols, cfaPat, 0, 0, {}, 0.0f,
                    hasWb, neutral, useAct, act, 1, rawW, rawH, label, outInfo);
                if (outInfo.valid) return true;
            }
        }
    }

    // 2. Exact JPEG preview slice at header bytes [84..87] offset & [88..91] length
    if (jpgOff >= 108u && jpgLen >= 32u && static_cast<uint64_t>(jpgOff) + jpgLen <= size) {
        const uint8_t* jp = data + jpgOff;
        if (jp[0] == 0xFFu && jp[1] == 0xD8u && jp[2] == 0xFFu) {
            DetailBlpJpeg::RawJpegDecoded jd;
            if (DetailBlpJpeg::DecodeBaselineJpegRawChannels(jp, jpgLen, jd) &&
                jd.width > 0 && jd.height > 0 && (jd.components == 3u || jd.components == 1u))
            {
                outInfo.width = jd.width;
                outInfo.height = jd.height;
                outInfo.origWidth = jd.width;
                outInfo.origHeight = jd.height;
                outInfo.channels = 4u;
                outInfo.bitsPerChannel = 8u;
                outInfo.formatName = label;
                outInfo.bgraPixels.resize(static_cast<size_t>(jd.width) * jd.height * 4u);
                for (size_t i = 0; i < static_cast<size_t>(jd.width) * jd.height; ++i) {
                    uint8_t r = 0, g = 0, b = 0;
                    if (jd.components == 3u) {
                        int yv = jd.pixels[i * 3u + 0u];
                        int cb = static_cast<int>(jd.pixels[i * 3u + 1u]) - 128;
                        int cr = static_cast<int>(jd.pixels[i * 3u + 2u]) - 128;
                        r = static_cast<uint8_t>(std::clamp(yv + ((1436 * cr + 512) >> 10), 0, 255));
                        g = static_cast<uint8_t>(std::clamp(yv - ((352 * cb + 731 * cr + 512) >> 10), 0, 255));
                        b = static_cast<uint8_t>(std::clamp(yv + ((1815 * cb + 512) >> 10), 0, 255));
                    } else {
                        r = g = b = jd.pixels[i];
                    }
                    WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, 255u);
                }
                outInfo.valid = true;
                return true;
            }
            if (DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(jp, jpgLen, label, outInfo)) {
                return true;
            }
        }
    }

    return false;
}

} // namespace Formats
