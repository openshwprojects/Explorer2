#pragma once
// FujifilmRaf.h — Fujifilm RAF Bayer & 6x6 X-Trans CFA RAW Decoder (.raf)

namespace Formats {

inline bool DecodeFujifilmRaf(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
        if (size < 92 || std::memcmp(data, "FUJIFILMCCD-RAW", 15) != 0) return false;
        std::wstring label = L"Fujifilm RAF RAW (.raf)";

        // 1. Attempt decoding Fujifilm RAF CFA sensor data FIRST (0x5C..0x5F BE CFA header + 0x64..0x6B BE CFA payload)
        if (size >= 108) {
            uint32_t cfaHdrOff  = DetailImg100::RdU32BE(data + 92);
            uint32_t cfaDataOff = DetailImg100::RdU32BE(data + 100);
            uint32_t cfaDataLen = DetailImg100::RdU32BE(data + 104);
            if (cfaDataOff > 0 && cfaDataLen >= 16 && static_cast<uint64_t>(cfaDataOff) + cfaDataLen <= size) {
                // Some Fujifilm RAF sensors store an embedded TIFF/DNG CFA container directly at cfaDataOff
                if (DetailImg200::ParseTiffRawIfds(data + cfaDataOff, cfaDataLen, label, outInfo)) {
                    return true;
                }
            }
            if (cfaHdrOff > 0 && static_cast<uint64_t>(cfaHdrOff) + 8 <= size &&
                cfaDataOff > 0 && cfaDataLen > 0 && static_cast<uint64_t>(cfaDataOff) + cfaDataLen <= size)
            {
                uint32_t numRecs = DetailImg100::RdU32BE(data + cfaHdrOff);
                size_t rp = static_cast<size_t>(cfaHdrOff) + 4u;
                uint32_t rawW = 0, rawH = 0;
                uint32_t cropTop = 0, cropLeft = 0, cropH = 0, cropW = 0;
                bool hasCropOrigin = false, hasCropSize = false;
                bool hasWb = false;
                float neutral[3] = { 1.0f, 1.0f, 1.0f };
                std::vector<uint8_t> cfaPat = { 0, 1, 1, 2 };
                uint16_t cfaRows = 2, cfaCols = 2;

                for (uint32_t r = 0; r < std::min<uint32_t>(numRecs, 256u) && rp + 4 <= size; ++r) {
                    uint16_t rTag = DetailImg100::RdU16BE(data + rp);
                    uint16_t rLen = DetailImg100::RdU16BE(data + rp + 2);
                    rp += 4;
                    if (rp + rLen > size) break;
                    if (rTag == 0x0100 && rLen >= 4) {
                        rawH = DetailImg100::RdU16BE(data + rp);
                        rawW = DetailImg100::RdU16BE(data + rp + 2);
                    } else if (rTag == 0x0110 && rLen >= 4) {
                        cropTop  = DetailImg100::RdU16BE(data + rp);
                        cropLeft = DetailImg100::RdU16BE(data + rp + 2);
                        hasCropOrigin = true;
                    } else if (rTag == 0x0111 && rLen >= 4) {
                        cropH = DetailImg100::RdU16BE(data + rp);
                        cropW = DetailImg100::RdU16BE(data + rp + 2);
                        hasCropSize = true;
                    } else if (rTag == 0x0131 && rLen == 36) {
                        // Fujifilm 6x6 X-Trans filter array (0=R, 1=G, 2=B)
                        bool validXTrans = true;
                        for (int k = 0; k < 36; ++k) {
                            if (data[rp + k] > 2u) { validXTrans = false; break; }
                        }
                        if (validXTrans) {
                            cfaRows = 6; cfaCols = 6;
                            cfaPat.assign(data + rp, data + rp + 36);
                        }
                    } else if (rTag == 0x2FF0 && rLen >= 8) {
                        // Fuji WB_GRGBLevels: [G, R, G2, B]
                        uint16_t wbG = DetailImg100::RdU16BE(data + rp + 0);
                        uint16_t wbR = DetailImg100::RdU16BE(data + rp + 2);
                        uint16_t wbB = DetailImg100::RdU16BE(data + rp + 6);
                        if (wbG > 0 && wbR > 0 && wbB > 0) {
                            neutral[0] = static_cast<float>(wbG) / static_cast<float>(wbR);
                            neutral[1] = 1.0f;
                            neutral[2] = static_cast<float>(wbG) / static_cast<float>(wbB);
                            hasWb = true;
                        }
                    }
                    rp += rLen;
                }

                if (rawW >= 2 && rawH >= 2 && rawW <= 16384 && rawH <= 16384) {
                    const size_t pxCount = static_cast<size_t>(rawW) * rawH;
                    const uint8_t* rawBytes = data + cfaDataOff;
                    size_t rawBytesLen = cfaDataLen;
                    // Some uncompressed Fuji RAFs prepend a 2048-byte subheader ahead of the 16-bit sensor array
                    if (rawBytesLen > pxCount * 2u && (rawBytesLen - pxCount * 2u) <= 4096u &&
                        ((rawBytesLen - pxCount * 2u) % 512u) == 0u)
                    {
                        size_t hdrSkip = rawBytesLen - pxCount * 2u;
                        rawBytes += hdrSkip;
                        rawBytesLen -= hdrSkip;
                    }

                    std::vector<uint16_t> samples;
                    uint16_t bps = 14;
                    if (rawBytesLen >= pxCount * 2u) {
                        size_t leSmall = 0, beSmall = 0;
                        size_t probeN = std::min<size_t>(pxCount, 512u);
                        for (size_t k = 0; k < probeN; ++k) {
                            if (DetailImg100::RdU16LE(rawBytes + k * 2u) <= 16383u) ++leSmall;
                            if (DetailImg100::RdU16BE(rawBytes + k * 2u) <= 16383u) ++beSmall;
                        }
                        bool useBe = (beSmall > leSmall);
                        bps = 16;
                        DetailImg200::UnpackRawSamplesFromBytes(rawBytes, rawBytesLen, rawW, rawH, 1, 16, useBe, 1, samples);
                    } else if (rawBytesLen >= (pxCount * 14u + 7u) / 8u) {
                        bps = 14;
                        DetailImg200::UnpackRawSamplesFromBytes(rawBytes, rawBytesLen, rawW, rawH, 1, 14, true, 1, samples);
                    } else if (rawBytesLen >= (pxCount * 12u + 7u) / 8u) {
                        bps = 12;
                        DetailImg200::UnpackRawSamplesFromBytes(rawBytes, rawBytesLen, rawW, rawH, 1, 12, true, 1, samples);
                    }

                    if (samples.size() == pxCount) {
                        bool useAct = false;
                        uint32_t act[4] = {};
                        if (hasCropSize && cropW >= 2 && cropH >= 2) {
                            uint32_t ct = hasCropOrigin ? cropTop : 0u;
                            uint32_t cl = hasCropOrigin ? cropLeft : 0u;
                            if (ct + cropH <= rawH && cl + cropW <= rawW) {
                                act[0] = ct; act[1] = cl; act[2] = ct + cropH; act[3] = cl + cropW;
                                useAct = true;
                            }
                        }
                        DetailImg200::DemosaicAndToneMapSensor(
                            samples, rawW, rawH, 1, 32803, bps,
                            cfaRows, cfaCols, cfaPat, 0, 0, {}, 0.0f,
                            hasWb, neutral, useAct, act, 1, rawW, rawH, label, outInfo);
                        if (outInfo.valid) return true;
                    }
                }
            }
        }

        // 2. Fallback to embedded preview JPEG at bytes 84..91 BE (0x54..0x5B) for proprietary compressed RAF blocks
        uint32_t jpgOff = DetailImg100::RdU32BE(data + 84);
        uint32_t jpgLen = DetailImg100::RdU32BE(data + 88);
        if (jpgOff > 0 && jpgLen > 32 && static_cast<uint64_t>(jpgOff) + jpgLen <= size) {
            DetailBlpJpeg::RawJpegDecoded jd;
            if (DetailBlpJpeg::DecodeBaselineJpegRawChannels(data + jpgOff, jpgLen, jd) &&
                jd.width > 0 && jd.height > 0 && (jd.components == 3u || jd.components == 1u))
            {
                outInfo.width = jd.width;
                outInfo.height = jd.height;
                outInfo.origWidth = jd.width;
                outInfo.origHeight = jd.height;
                outInfo.channels = 4;
                outInfo.bitsPerChannel = 8;
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
            if (DetailImg100::DecodeEmbeddedWicBuffer(data + jpgOff, jpgLen, label, outInfo)) {
                return true;
            }
        }

        if (DetailImg200::FindAndDecodeBestEmbeddedRaster(data, size, label, outInfo, 64)) return true;
        return DetailImg100::DecodeEmbeddedWicBuffer(data, size, label, outInfo);
}

} // namespace Formats
