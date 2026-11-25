#pragma once
// CanonCr3.h — Canon EOS CR3 ISOBMFF ('ftypcrx ') Structural CRAW/CMP1 & Preview RAW Decoder (.cr3)

namespace Formats {

namespace DetailVectorAndRawImg {

// ============================================================================
// 5. Canon CR3 'CRAW' + 'CMP1' Sensor Track Unpacker (SOF3 Lossless JPEG / 14-bit CFA)
// ============================================================================
struct Cr3Cmp1TrackInfo {
    bool hasCmp1 = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint16_t bitDepth = 14;
    uint8_t cfaLayout = 0; // 0=RGGB, 1=GRBG, 2=GBRG, 3=BGGR
    uint64_t mdatOffset = 0;
    uint32_t mdatSize = 0;
};

inline void ScanCr3Cmp1InTrak(
    const uint8_t* fileData, size_t fileSize,
    size_t startPos, size_t endPos, int depth,
    Cr3Cmp1TrackInfo& curTrack,
    std::vector<Cr3Cmp1TrackInfo>& outTracks)
{
    if (depth > 10 || startPos + 8 > endPos || endPos > fileSize) return;
    std::vector<uint64_t> offsets;
    std::vector<uint32_t> sizes;

    size_t pos = startPos;
    while (pos + 8 <= endPos) {
        uint64_t boxLen = DetailLayeredAndHdrImg::RdU32BE(fileData + pos);
        size_t hdrLen = 8;
        if (boxLen == 1) {
            if (pos + 16 > endPos) break;
            boxLen = DetailLayeredAndHdrImg::RdU64BE(fileData + pos + 8);
            hdrLen = 16;
        } else if (boxLen == 0) {
            boxLen = endPos - pos;
        }
        if (boxLen < hdrLen || pos + boxLen > endPos) break;
        const char* bt = reinterpret_cast<const char*>(fileData + pos + 4);
        size_t pay = pos + hdrLen;
        size_t boxEnd = pos + static_cast<size_t>(boxLen);
        size_t payLen = boxEnd - pay;

        if (std::memcmp(bt, "trak", 4) == 0) {
            Cr3Cmp1TrackInfo tr;
            ScanCr3Cmp1InTrak(fileData, fileSize, pay, boxEnd, depth + 1, tr, outTracks);
            if (tr.hasCmp1 && tr.mdatOffset > 0 && tr.mdatSize > 0) outTracks.push_back(tr);
        } else if (std::memcmp(bt, "moov", 4) == 0 || std::memcmp(bt, "mdia", 4) == 0 ||
                   std::memcmp(bt, "minf", 4) == 0 || std::memcmp(bt, "stbl", 4) == 0) {
            ScanCr3Cmp1InTrak(fileData, fileSize, pay, boxEnd, depth + 1, curTrack, outTracks);
        } else if (std::memcmp(bt, "stsd", 4) == 0 && payLen >= 16) {
            ScanCr3Cmp1InTrak(fileData, fileSize, pay + 8, boxEnd, depth + 1, curTrack, outTracks);
        } else if (std::memcmp(bt, "CRAW", 4) == 0 && payLen >= 78) {
            // VisualSampleEntry ('CRAW'): width at +24, height at +26, child boxes ('CMP1', 'CDI1') at +78 / +82 / +86
            curTrack.width  = DetailLayeredAndHdrImg::RdU16BE(fileData + pay + 24);
            curTrack.height = DetailLayeredAndHdrImg::RdU16BE(fileData + pay + 26);
            for (size_t childOff : { size_t(78), size_t(82), size_t(86) }) {
                if (childOff + 8 <= payLen &&
                    (std::memcmp(fileData + pay + childOff + 4, "CMP1", 4) == 0 ||
                     std::memcmp(fileData + pay + childOff + 4, "CDI1", 4) == 0 ||
                     std::memcmp(fileData + pay + childOff + 4, "JPEG", 4) == 0)) {
                    ScanCr3Cmp1InTrak(fileData, fileSize, pay + childOff, boxEnd, depth + 1, curTrack, outTracks);
                    break;
                }
            }
        } else if (std::memcmp(bt, "CMP1", 4) == 0 && payLen >= 24) {
            // Canon CMP1 header:
            //   [0..1] reserved, [2..3] hdrSize, [4..5] version, [6..7] subVer,
            //   [8..11] fWidth, [12..15] fHeight, [16..19] tWidth, [20..23] tHeight,
            //   [24] bitDepth (nBits), [25] (nPlanes << 4) | cfaLayout
            const uint8_t* cp = fileData + pay;
            uint32_t fw = DetailLayeredAndHdrImg::RdU32BE(cp + 8);
            uint32_t fh = DetailLayeredAndHdrImg::RdU32BE(cp + 12);
            if (fw > 0 && fh > 0 && fw <= 16384 && fh <= 16384) {
                curTrack.width = fw;
                curTrack.height = fh;
            }
            if (payLen >= 25u) {
                const uint8_t bd = cp[24];
                if (bd >= 8u && bd <= 16u) curTrack.bitDepth = bd;
            }
            if (payLen >= 26u) {
                curTrack.cfaLayout = cp[25] & 0x03u;
            }
            curTrack.hasCmp1 = true;
        } else if (std::memcmp(bt, "stco", 4) == 0 && payLen >= 12) {
            offsets.push_back(DetailLayeredAndHdrImg::RdU32BE(fileData + pay + 8));
        } else if (std::memcmp(bt, "co64", 4) == 0 && payLen >= 16) {
            offsets.push_back(DetailLayeredAndHdrImg::RdU64BE(fileData + pay + 8));
        } else if (std::memcmp(bt, "stsz", 4) == 0 && payLen >= 12) {
            uint32_t fsz = DetailLayeredAndHdrImg::RdU32BE(fileData + pay + 4);
            if (fsz > 0) sizes.push_back(fsz);
            else if (payLen >= 16) sizes.push_back(DetailLayeredAndHdrImg::RdU32BE(fileData + pay + 12));
        }
        pos = boxEnd;
    }
    if (!offsets.empty() && !sizes.empty()) {
        curTrack.mdatOffset = offsets[0];
        curTrack.mdatSize = sizes[0];
    }
}

inline bool DecodeCr3JpegExactSlice(const uint8_t* jp, size_t jlen, const std::wstring& label, ExtendedImageInfo& outCand) {
    if (!jp || jlen < 32u || jp[0] != 0xFFu || jp[1] != 0xD8u || jp[2] != 0xFFu) return false;

    DetailBlpJpeg::RawJpegDecoded jd;
    if (DetailBlpJpeg::DecodeBaselineJpegRawChannels(jp, jlen, jd) &&
        jd.width > 0 && jd.height > 0 && (jd.components == 3u || jd.components == 1u))
    {
        outCand = {};
        outCand.width = jd.width;
        outCand.height = jd.height;
        outCand.origWidth = jd.width;
        outCand.origHeight = jd.height;
        outCand.channels = 4u;
        outCand.bitsPerChannel = 8u;
        outCand.formatName = label;
        outCand.bgraPixels.resize(static_cast<size_t>(jd.width) * jd.height * 4u);
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
            WritePremulBGRA(&outCand.bgraPixels[i * 4u], r, g, b, 255u);
        }
        outCand.valid = true;
        return true;
    }

    return DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(jp, jlen, label, outCand) &&
           outCand.valid && outCand.width > 0 && outCand.height > 0 && !outCand.bgraPixels.empty();
}

// Canon CR3 structural ISOBMFF box walker (moov/trak/mdia/minf/stbl sample offsets + Canon uuid CMT1..4 / PRVW / THMB)
inline void WalkCr3IsobmffBoxes(
    const uint8_t* fileData, size_t fileSize,
    size_t startPos, size_t endPos, int depth,
    const std::wstring& label,
    ExtendedImageInfo& bestImg,
    uint32_t& metaOrigW, uint32_t& metaOrigH, uint16_t& metaOrient)
{
    if (depth > 10 || startPos + 8 > endPos || endPos > fileSize) return;

    // 85c0b687-820f-11e0-8111-f4ce462b6a48: Canon metadata container (CMT1..CMT4 EXIF/MakerNote TIFF, THMB)
    static constexpr uint8_t kUuidCanonMeta[16] = {
        0x85, 0xC0, 0xB6, 0x87, 0x82, 0x0F, 0x11, 0xE0,
        0x81, 0x11, 0xF4, 0xCE, 0x46, 0x2B, 0x6A, 0x48
    };
    // eaf42b5e-1c98-4b88-b9fb-b7dc406e4d16: Canon preview container (8-byte flags/pad + PRVW box)
    static constexpr uint8_t kUuidCanonPrvw[16] = {
        0xEA, 0xF4, 0x2B, 0x5E, 0x1C, 0x98, 0x4B, 0x88,
        0xB9, 0xFB, 0xB7, 0xDC, 0x40, 0x6E, 0x4D, 0x16
    };

    auto considerJpegSlice = [&](const uint8_t* jp, size_t jlen) {
        if (jlen < 32u || jp < fileData || static_cast<size_t>(jp - fileData) + jlen > fileSize) return;
        ExtendedImageInfo cand;
        if (DecodeCr3JpegExactSlice(jp, jlen, label, cand)) {
            const uint64_t newArea = static_cast<uint64_t>(cand.width) * cand.height;
            const uint64_t curArea = bestImg.valid ? (static_cast<uint64_t>(bestImg.width) * bestImg.height) : 0ull;
            if (newArea > curArea) {
                bestImg = std::move(cand);
            }
        }
    };

    std::vector<uint64_t> stblOffsets;
    std::vector<uint32_t> stblSizes;

    size_t pos = startPos;
    while (pos + 8 <= endPos) {
        uint64_t boxLen = DetailLayeredAndHdrImg::RdU32BE(fileData + pos);
        size_t hdrLen = 8;
        if (boxLen == 1) {
            if (pos + 16 > endPos) break;
            boxLen = DetailLayeredAndHdrImg::RdU64BE(fileData + pos + 8);
            hdrLen = 16;
        } else if (boxLen == 0) {
            boxLen = endPos - pos;
        }
        if (boxLen < hdrLen || pos + boxLen > endPos) break;

        const char* btype = reinterpret_cast<const char*>(fileData + pos + 4);
        const size_t payloadStart = pos + hdrLen;
        const size_t boxEnd = pos + static_cast<size_t>(boxLen);
        const size_t payloadLen = boxEnd - payloadStart;

        if (std::memcmp(btype, "moov", 4) == 0 ||
            std::memcmp(btype, "trak", 4) == 0 ||
            std::memcmp(btype, "mdia", 4) == 0 ||
            std::memcmp(btype, "minf", 4) == 0 ||
            std::memcmp(btype, "stbl", 4) == 0 ||
            std::memcmp(btype, "dinf", 4) == 0)
        {
            WalkCr3IsobmffBoxes(fileData, fileSize, payloadStart, boxEnd, depth + 1, label, bestImg, metaOrigW, metaOrigH, metaOrient);
        } else if (std::memcmp(btype, "uuid", 4) == 0 && payloadLen >= 16) {
            const uint8_t* uuid = fileData + payloadStart;
            if (std::memcmp(uuid, kUuidCanonMeta, 16) == 0) {
                WalkCr3IsobmffBoxes(fileData, fileSize, payloadStart + 16u, boxEnd, depth + 1, label, bestImg, metaOrigW, metaOrigH, metaOrient);
            } else if (std::memcmp(uuid, kUuidCanonPrvw, 16) == 0) {
                // Canon preview uuid box stores an 8-byte header (or 0-byte in minimal writers) before child PRVW box
                if (payloadLen >= 24u && std::memcmp(fileData + payloadStart + 20u, "PRVW", 4) == 0) {
                    WalkCr3IsobmffBoxes(fileData, fileSize, payloadStart + 16u, boxEnd, depth + 1, label, bestImg, metaOrigW, metaOrigH, metaOrient);
                } else if (payloadLen > 24u) {
                    WalkCr3IsobmffBoxes(fileData, fileSize, payloadStart + 24u, boxEnd, depth + 1, label, bestImg, metaOrigW, metaOrigH, metaOrient);
                }
            }
        } else if (std::memcmp(btype, "PRVW", 4) == 0 && payloadLen >= 16) {
            // Canon PRVW fullbox: [verFlags:4][unk:2][w:2][h:2][unk:2][jpegSize:4][jpeg...]
            const uint32_t jsz = DetailLayeredAndHdrImg::RdU32BE(fileData + payloadStart + 12u);
            if (jsz >= 32u && 16u + static_cast<size_t>(jsz) <= payloadLen) {
                considerJpegSlice(fileData + payloadStart + 16u, jsz);
            }
        } else if (std::memcmp(btype, "THMB", 4) == 0 && payloadLen >= 16) {
            // Canon THMB fullbox: [verFlags:4][w:2][h:2][jpegSize:4][unk:4][jpeg...] (or [jpeg] at +12 when ver==0)
            const uint32_t jsz = DetailLayeredAndHdrImg::RdU32BE(fileData + payloadStart + 8u);
            if (jsz >= 32u && 16u + static_cast<size_t>(jsz) <= payloadLen &&
                fileData[payloadStart + 16u] == 0xFFu && fileData[payloadStart + 17u] == 0xD8u)
            {
                considerJpegSlice(fileData + payloadStart + 16u, jsz);
            } else if (jsz >= 32u && 12u + static_cast<size_t>(jsz) <= payloadLen &&
                       fileData[payloadStart + 12u] == 0xFFu && fileData[payloadStart + 13u] == 0xD8u)
            {
                considerJpegSlice(fileData + payloadStart + 12u, jsz);
            }
        } else if ((std::memcmp(btype, "CMT1", 4) == 0 || std::memcmp(btype, "CMT2", 4) == 0) && payloadLen >= 16) {
            // Canon EXIF / IFD0 TIFF header inside CR3
            const uint8_t* tp = fileData + payloadStart;
            const bool tbe = (tp[0] == 'M' && tp[1] == 'M');
            if ((tp[0] == 'I' && tp[1] == 'I') || tbe) {
                const uint32_t ifd0 = Rd32(tp + 4, tbe);
                if (ifd0 >= 8u && ifd0 + 2u <= payloadLen) {
                    const uint16_t ne = Rd16(tp + ifd0, tbe);
                    if (ifd0 + 2u + static_cast<size_t>(ne) * 12u <= payloadLen) {
                        std::vector<uint32_t> vals;
                        for (uint16_t e = 0; e < ne; ++e) {
                            const uint8_t* ent = tp + ifd0 + 2u + static_cast<size_t>(e) * 12u;
                            const uint16_t tag = Rd16(ent, tbe);
                            if ((tag == 0x0100 || tag == 0xA002) && ReadTiffU32Values(tp, payloadLen, ent, tbe, vals, 1)) {
                                metaOrigW = std::max(metaOrigW, vals[0]);
                            } else if ((tag == 0x0101 || tag == 0xA003) && ReadTiffU32Values(tp, payloadLen, ent, tbe, vals, 1)) {
                                metaOrigH = std::max(metaOrigH, vals[0]);
                            } else if (tag == 0x0112 && std::memcmp(btype, "CMT1", 4) == 0 &&
                                       ReadTiffU32Values(tp, payloadLen, ent, tbe, vals, 1) && vals[0] >= 1 && vals[0] <= 8)
                            {
                                metaOrient = static_cast<uint16_t>(vals[0]);
                            }
                        }
                    }
                }
            }
        } else if (std::memcmp(btype, "stco", 4) == 0 && payloadLen >= 8) {
            uint32_t cnt = std::min<uint32_t>(DetailLayeredAndHdrImg::RdU32BE(fileData + payloadStart + 4), 64u);
            for (uint32_t k = 0; k < cnt && 8u + (k + 1u) * 4u <= payloadLen; ++k) {
                stblOffsets.push_back(DetailLayeredAndHdrImg::RdU32BE(fileData + payloadStart + 8u + k * 4u));
            }
        } else if (std::memcmp(btype, "co64", 4) == 0 && payloadLen >= 8) {
            uint32_t cnt = std::min<uint32_t>(DetailLayeredAndHdrImg::RdU32BE(fileData + payloadStart + 4), 64u);
            for (uint32_t k = 0; k < cnt && 8u + (k + 1u) * 8u <= payloadLen; ++k) {
                stblOffsets.push_back(DetailLayeredAndHdrImg::RdU64BE(fileData + payloadStart + 8u + k * 8u));
            }
        } else if (std::memcmp(btype, "stsz", 4) == 0 && payloadLen >= 12) {
            const uint32_t fixedSz = DetailLayeredAndHdrImg::RdU32BE(fileData + payloadStart + 4);
            uint32_t cnt = std::min<uint32_t>(DetailLayeredAndHdrImg::RdU32BE(fileData + payloadStart + 8), 64u);
            if (fixedSz > 0) {
                for (uint32_t k = 0; k < std::max(1u, cnt); ++k) stblSizes.push_back(fixedSz);
            } else {
                for (uint32_t k = 0; k < cnt && 12u + (k + 1u) * 4u <= payloadLen; ++k) {
                    stblSizes.push_back(DetailLayeredAndHdrImg::RdU32BE(fileData + payloadStart + 12u + k * 4u));
                }
            }
        }

        pos = boxEnd;
    }

    const size_t nChunks = std::min(stblOffsets.size(), stblSizes.size());
    for (size_t k = 0; k < nChunks; ++k) {
        if (stblOffsets[k] < fileSize && stblSizes[k] >= 32u && stblOffsets[k] + stblSizes[k] <= fileSize) {
            considerJpegSlice(fileData + static_cast<size_t>(stblOffsets[k]), stblSizes[k]);
        }
    }
}

} // namespace DetailVectorAndRawImg

inline bool DecodeCanonCr3(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 24u || std::memcmp(data + 4, "ftyp", 4) != 0) return false;
    const uint32_t ftypLen = DetailLayeredAndHdrImg::RdU32BE(data);
    if (ftypLen < 16u || ftypLen > size) return false;
    bool hasCrxBrand = (std::memcmp(data + 8, "crx ", 4) == 0);
    for (size_t p = 16u; !hasCrxBrand && p + 4u <= ftypLen; p += 4u) {
        if (std::memcmp(data + p, "crx ", 4) == 0) hasCrxBrand = true;
    }
    if (!hasCrxBrand) return false;

    const std::wstring label = L"Canon CR3 RAW (.cr3)";

    ExtendedImageInfo bestImg;
    uint32_t metaW = 0, metaH = 0;
    uint16_t metaOrient = 1;
    DetailVectorAndRawImg::WalkCr3IsobmffBoxes(data, size, 0, size, 0, label, bestImg, metaW, metaH, metaOrient);

    // Unpack Canon CR3 'CRAW' + 'CMP1' RAW sensor track structurally via stbl sample offsets
    std::vector<DetailVectorAndRawImg::Cr3Cmp1TrackInfo> cmpTracks;
    DetailVectorAndRawImg::Cr3Cmp1TrackInfo dummyTr;
    DetailVectorAndRawImg::ScanCr3Cmp1InTrak(data, size, 0, size, 0, dummyTr, cmpTracks);
    for (const auto& ct : cmpTracks) {
        const uint64_t ctArea = static_cast<uint64_t>(ct.width) * ct.height;
        const uint64_t curArea = bestImg.valid ? (static_cast<uint64_t>(bestImg.width) * bestImg.height) : 0ull;
        if (ctArea <= curArea || ct.width < 2 || ct.height < 2) continue;
        if (ct.mdatOffset >= size || ct.mdatSize > size - static_cast<size_t>(ct.mdatOffset)) continue;

        const uint8_t* slice = data + static_cast<size_t>(ct.mdatOffset);
        const size_t sliceLen = ct.mdatSize;

        // Track 0/1 CRAW with JPEG sample inside mdat
        ExtendedImageInfo jpgCand;
        if (DetailVectorAndRawImg::DecodeCr3JpegExactSlice(slice, sliceLen, label, jpgCand)) {
            bestImg = std::move(jpgCand);
            continue;
        }

        std::vector<uint16_t> cfaSamples;
        uint16_t bps = ct.bitDepth;

        DetailVectorAndRawImg::LosslessJpegResult lj;
        if (DetailVectorAndRawImg::DecodeLosslessJpegSOF3(slice, sliceLen, lj)) {
            bps = lj.precision;
            const size_t expectedPx = static_cast<size_t>(ct.width) * ct.height;
            if (lj.components == 4 && lj.width * 2u == ct.width && lj.height * 2u == ct.height) {
                cfaSamples.assign(expectedPx, 0);
                for (uint32_t qy = 0; qy < lj.height; ++qy) {
                    for (uint32_t qx = 0; qx < lj.width; ++qx) {
                        const uint16_t* q = &lj.samples[(static_cast<size_t>(qy) * lj.width + qx) * 4u];
                        cfaSamples[static_cast<size_t>(qy * 2u + 0u) * ct.width + (qx * 2u + 0u)] = q[0];
                        cfaSamples[static_cast<size_t>(qy * 2u + 0u) * ct.width + (qx * 2u + 1u)] = q[1];
                        cfaSamples[static_cast<size_t>(qy * 2u + 1u) * ct.width + (qx * 2u + 0u)] = q[2];
                        cfaSamples[static_cast<size_t>(qy * 2u + 1u) * ct.width + (qx * 2u + 1u)] = q[3];
                    }
                }
            } else if (lj.samples.size() >= expectedPx) {
                cfaSamples.assign(lj.samples.begin(), lj.samples.begin() + expectedPx);
            }
        } else {
            DetailVectorAndRawImg::UnpackRawSamplesFromBytes(slice, sliceLen, ct.width, ct.height, 1, bps, true, 1, cfaSamples);
        }

        if (cfaSamples.size() == static_cast<size_t>(ct.width) * ct.height) {
            std::vector<uint8_t> cfaPat = { 0, 1, 1, 2 };
            if (ct.cfaLayout == 1)      cfaPat = { 1, 0, 2, 1 };
            else if (ct.cfaLayout == 2) cfaPat = { 1, 2, 0, 1 };
            else if (ct.cfaLayout == 3) cfaPat = { 2, 1, 1, 0 };
            float neutral[3] = { 1.0f, 1.0f, 1.0f };
            uint32_t act[4] = {};
            ExtendedImageInfo rawCand;
            DetailVectorAndRawImg::DemosaicAndToneMapSensor(
                cfaSamples, ct.width, ct.height, 1, 32803, bps,
                2, 2, cfaPat, 0, 0, {}, static_cast<float>((1u << bps) - 1u),
                false, neutral, false, act, 1, std::max(metaW, ct.width), std::max(metaH, ct.height),
                label, rawCand);
            if (rawCand.valid) {
                bestImg = std::move(rawCand);
                break;
            }
        }
    }

    if (bestImg.valid) {
        if (metaOrient > 1 && metaOrient <= 8) {
            DetailVectorAndRawImg::ApplyExifOrientationBGRA(metaOrient, bestImg.width, bestImg.height, bestImg.bgraPixels);
            if (metaOrient >= 5) std::swap(metaW, metaH);
        }
        if (metaW > bestImg.origWidth) {
            bestImg.origWidth = metaW;
            bestImg.origHeight = metaH;
        }
        outInfo = std::move(bestImg);
        return true;
    }
    return false;
}

} // namespace Formats
