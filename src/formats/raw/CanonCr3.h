#pragma once
// CanonCr3.h — Canon EOS CR3 ISOBMFF ('ftypcrx ') CRAW/CMP1 & Preview RAW Decoder (.cr3)

namespace Formats {

namespace DetailImg200 {

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
        uint64_t boxLen = DetailImg100::RdU32BE(fileData + pos);
        size_t hdrLen = 8;
        if (boxLen == 1) {
            if (pos + 16 > endPos) break;
            boxLen = DetailImg100::RdU64BE(fileData + pos + 8);
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
            // stsd: [verFlags:4][entryCount:4][entry0...]
            ScanCr3Cmp1InTrak(fileData, fileSize, pay + 8, boxEnd, depth + 1, curTrack, outTracks);
        } else if (std::memcmp(bt, "CRAW", 4) == 0 && payLen >= 78) {
            // VisualSampleEntry ('CRAW'): width at +24, height at +26, child boxes ('CMP1', 'CDI1') at +78 (or +82)
            curTrack.width  = DetailImg100::RdU16BE(fileData + pay + 24);
            curTrack.height = DetailImg100::RdU16BE(fileData + pay + 26);
            for (size_t childOff : { size_t(78), size_t(82), size_t(86) }) {
                if (childOff + 8 <= payLen &&
                    (std::memcmp(fileData + pay + childOff + 4, "CMP1", 4) == 0 ||
                     std::memcmp(fileData + pay + childOff + 4, "CDI1", 4) == 0)) {
                    ScanCr3Cmp1InTrak(fileData, fileSize, pay + childOff, boxEnd, depth + 1, curTrack, outTracks);
                    break;
                }
            }
        } else if (std::memcmp(bt, "CMP1", 4) == 0 && payLen >= 24) {
            // Canon CMP1 header: [unk:4][hdrSz:2][ver:2][fWidth:4][fHeight:4][tWidth:4][tHeight:4][bitDepth:1][planesCfa:1]...
            const uint8_t* cp = fileData + pay;
            uint32_t fw = DetailImg100::RdU32BE(cp + 8);
            uint32_t fh = DetailImg100::RdU32BE(cp + 12);
            if (fw > 0 && fh > 0 && fw <= 16384 && fh <= 16384) {
                curTrack.width = fw;
                curTrack.height = fh;
            }
            uint8_t bd = cp[24 < payLen ? 24 : 20];
            if (bd >= 8 && bd <= 16) curTrack.bitDepth = bd;
            if (payLen >= 26) curTrack.cfaLayout = cp[25] & 0x03u;
            curTrack.hasCmp1 = true;
        } else if (std::memcmp(bt, "stco", 4) == 0 && payLen >= 12) {
            offsets.push_back(DetailImg100::RdU32BE(fileData + pay + 8));
        } else if (std::memcmp(bt, "co64", 4) == 0 && payLen >= 16) {
            offsets.push_back(DetailImg100::RdU64BE(fileData + pay + 8));
        } else if (std::memcmp(bt, "stsz", 4) == 0 && payLen >= 12) {
            uint32_t fsz = DetailImg100::RdU32BE(fileData + pay + 4);
            if (fsz > 0) sizes.push_back(fsz);
            else if (payLen >= 16) sizes.push_back(DetailImg100::RdU32BE(fileData + pay + 12));
        }
        pos = boxEnd;
    }
    if (!offsets.empty() && !sizes.empty()) {
        curTrack.mdatOffset = offsets[0];
        curTrack.mdatSize = sizes[0];
    }
}

// Canon CR3 recursive ISOBMFF box walker (moov/trak/mdia/minf/stbl chunk offsets + Canon uuid PRVW & THMB)
inline void WalkCr3IsobmffBoxes(
    const uint8_t* fileData, size_t fileSize,
    size_t startPos, size_t endPos, int depth,
    const std::wstring& label,
    ExtendedImageInfo& bestImg,
    uint32_t& metaOrigW, uint32_t& metaOrigH, uint16_t& metaOrient)
{
    if (depth > 10 || startPos + 8 > endPos || endPos > fileSize) return;

    static const uint8_t kUuidCanonMeta[16] = {
        0x85, 0xC0, 0xB6, 0x87, 0x82, 0x0F, 0x11, 0xE0,
        0x81, 0x11, 0xF4, 0xCE, 0x46, 0x2B, 0x6A, 0x48
    };
    static const uint8_t kUuidCanonPrvw[16] = {
        0xEA, 0xF4, 0x2B, 0x5E, 0x1C, 0x98, 0x4B, 0x88,
        0xB9, 0xFB, 0xB7, 0xDC, 0x40, 0x6E, 0x4D, 0x16
    };

    auto considerJpegSlice = [&](const uint8_t* jp, size_t jlen) {
        if (jlen < 32 || jp < fileData || static_cast<size_t>(jp - fileData) + jlen > fileSize) return;
        if (jp[0] != 0xFF || jp[1] != 0xD8 || jp[2] != 0xFF) return;
        ExtendedImageInfo cand;
        if (DetailImg100::DecodeEmbeddedWicBuffer(jp, jlen, label, cand) &&
            cand.valid && cand.width > 0 && cand.height > 0 && !cand.bgraPixels.empty())
        {
            uint64_t newArea = static_cast<uint64_t>(cand.width) * cand.height;
            uint64_t curArea = bestImg.valid ? (static_cast<uint64_t>(bestImg.width) * bestImg.height) : 0ull;
            if (newArea > curArea) {
                bestImg = std::move(cand);
            }
        }
    };

    // Track stbl chunk offset + sample size within a single stbl box
    std::vector<uint64_t> stblOffsets;
    std::vector<uint32_t> stblSizes;

    size_t pos = startPos;
    while (pos + 8 <= endPos) {
        uint64_t boxLen = DetailImg100::RdU32BE(fileData + pos);
        size_t hdrLen = 8;
        if (boxLen == 1) {
            if (pos + 16 > endPos) break;
            boxLen = (static_cast<uint64_t>(DetailImg100::RdU32BE(fileData + pos + 8)) << 32) |
                     DetailImg100::RdU32BE(fileData + pos + 12);
            hdrLen = 16;
        } else if (boxLen == 0) {
            boxLen = endPos - pos;
        }
        if (boxLen < hdrLen || pos + boxLen > endPos) break;

        const char* btype = reinterpret_cast<const char*>(fileData + pos + 4);
        size_t payloadStart = pos + hdrLen;
        size_t boxEnd = pos + static_cast<size_t>(boxLen);
        size_t payloadLen = boxEnd - payloadStart;

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
                WalkCr3IsobmffBoxes(fileData, fileSize, payloadStart + 16, boxEnd, depth + 1, label, bestImg, metaOrigW, metaOrigH, metaOrient);
            } else if (std::memcmp(uuid, kUuidCanonPrvw, 16) == 0) {
                // Canon preview uuid box has 8-byte flags/padding after UUID, then child PRVW box
                if (payloadLen > 24) {
                    WalkCr3IsobmffBoxes(fileData, fileSize, payloadStart + 24, boxEnd, depth + 1, label, bestImg, metaOrigW, metaOrigH, metaOrient);
                }
                ExtendedImageInfo cand;
                if (FindAndDecodeBestEmbeddedRaster(fileData + payloadStart + 16, payloadLen - 16, label, cand)) {
                    if (!bestImg.valid || static_cast<uint64_t>(cand.width) * cand.height > static_cast<uint64_t>(bestImg.width) * bestImg.height) {
                        bestImg = std::move(cand);
                    }
                }
            } else {
                ExtendedImageInfo cand;
                if (FindAndDecodeBestEmbeddedRaster(fileData + payloadStart + 16, payloadLen - 16, label, cand)) {
                    if (!bestImg.valid || static_cast<uint64_t>(cand.width) * cand.height > static_cast<uint64_t>(bestImg.width) * bestImg.height) {
                        bestImg = std::move(cand);
                    }
                }
            }
        } else if (std::memcmp(btype, "PRVW", 4) == 0 && payloadLen >= 16) {
            // Canon PRVW: [reserved:6][w:2][h:2][unk:2][jpegSize:4][jpeg...]
            uint32_t jsz = DetailImg100::RdU32BE(fileData + payloadStart + 12);
            if (jsz > 32 && 16u + static_cast<size_t>(jsz) <= payloadLen) {
                considerJpegSlice(fileData + payloadStart + 16, jsz);
            }
            ExtendedImageInfo cand;
            if (FindAndDecodeBestEmbeddedRaster(fileData + payloadStart, payloadLen, label, cand)) {
                if (!bestImg.valid || static_cast<uint64_t>(cand.width) * cand.height > static_cast<uint64_t>(bestImg.width) * bestImg.height) {
                    bestImg = std::move(cand);
                }
            }
        } else if (std::memcmp(btype, "THMB", 4) == 0 && payloadLen >= 16) {
            // Canon THMB: [verFlags:4][w:2][h:2][jpegSize:4][unk:4][jpeg...] (or jpeg at +12)
            uint32_t jsz = DetailImg100::RdU32BE(fileData + payloadStart + 8);
            if (jsz > 32 && 16u + static_cast<size_t>(jsz) <= payloadLen) {
                considerJpegSlice(fileData + payloadStart + 16, jsz);
            } else if (jsz > 32 && 12u + static_cast<size_t>(jsz) <= payloadLen) {
                considerJpegSlice(fileData + payloadStart + 12, jsz);
            }
            ExtendedImageInfo cand;
            if (FindAndDecodeBestEmbeddedRaster(fileData + payloadStart, payloadLen, label, cand)) {
                if (!bestImg.valid || static_cast<uint64_t>(cand.width) * cand.height > static_cast<uint64_t>(bestImg.width) * bestImg.height) {
                    bestImg = std::move(cand);
                }
            }
        } else if (std::memcmp(btype, "CMT1", 4) == 0 && payloadLen >= 16) {
            // Canon IFD0 TIFF header inside CR3
            const uint8_t* tp = fileData + payloadStart;
            bool tbe = (tp[0] == 'M' && tp[1] == 'M');
            if ((tp[0] == 'I' && tp[1] == 'I') || tbe) {
                uint32_t ifd0 = Rd32(tp + 4, tbe);
                if (ifd0 >= 8 && ifd0 + 2 <= payloadLen) {
                    uint16_t ne = Rd16(tp + ifd0, tbe);
                    if (ifd0 + 2u + static_cast<size_t>(ne) * 12u <= payloadLen) {
                        std::vector<uint32_t> vals;
                        for (uint16_t e = 0; e < ne; ++e) {
                            const uint8_t* ent = tp + ifd0 + 2u + static_cast<size_t>(e) * 12u;
                            uint16_t tag = Rd16(ent, tbe);
                            if (tag == 0x0100 && ReadTiffU32Values(tp, payloadLen, ent, tbe, vals, 1)) metaOrigW = std::max(metaOrigW, vals[0]);
                            else if (tag == 0x0101 && ReadTiffU32Values(tp, payloadLen, ent, tbe, vals, 1)) metaOrigH = std::max(metaOrigH, vals[0]);
                            else if (tag == 0x0112 && ReadTiffU32Values(tp, payloadLen, ent, tbe, vals, 1) && vals[0] >= 1 && vals[0] <= 8) {
                                metaOrient = static_cast<uint16_t>(vals[0]);
                            }
                        }
                    }
                }
            }
        } else if (std::memcmp(btype, "stco", 4) == 0 && payloadLen >= 8) {
            uint32_t cnt = DetailImg100::RdU32BE(fileData + payloadStart + 4);
            cnt = std::min<uint32_t>(cnt, 64u);
            for (uint32_t k = 0; k < cnt && 8u + (k + 1u) * 4u <= payloadLen; ++k) {
                stblOffsets.push_back(DetailImg100::RdU32BE(fileData + payloadStart + 8u + k * 4u));
            }
        } else if (std::memcmp(btype, "co64", 4) == 0 && payloadLen >= 8) {
            uint32_t cnt = DetailImg100::RdU32BE(fileData + payloadStart + 4);
            cnt = std::min<uint32_t>(cnt, 64u);
            for (uint32_t k = 0; k < cnt && 8u + (k + 1u) * 8u <= payloadLen; ++k) {
                uint64_t off64 = (static_cast<uint64_t>(DetailImg100::RdU32BE(fileData + payloadStart + 8u + k * 8u)) << 32) |
                                 DetailImg100::RdU32BE(fileData + payloadStart + 12u + k * 8u);
                stblOffsets.push_back(off64);
            }
        } else if (std::memcmp(btype, "stsz", 4) == 0 && payloadLen >= 12) {
            uint32_t fixedSz = DetailImg100::RdU32BE(fileData + payloadStart + 4);
            uint32_t cnt     = DetailImg100::RdU32BE(fileData + payloadStart + 8);
            cnt = std::min<uint32_t>(cnt, 64u);
            if (fixedSz > 0) {
                for (uint32_t k = 0; k < std::max(1u, cnt); ++k) stblSizes.push_back(fixedSz);
            } else {
                for (uint32_t k = 0; k < cnt && 12u + (k + 1u) * 4u <= payloadLen; ++k) {
                    stblSizes.push_back(DetailImg100::RdU32BE(fileData + payloadStart + 12u + k * 4u));
                }
            }
        }

        pos = boxEnd;
    }

    size_t nChunks = std::min(stblOffsets.size(), stblSizes.size());
    for (size_t k = 0; k < nChunks; ++k) {
        if (stblOffsets[k] < fileSize && stblSizes[k] > 32 && stblOffsets[k] + stblSizes[k] <= fileSize) {
            considerJpegSlice(fileData + static_cast<size_t>(stblOffsets[k]), stblSizes[k]);
        }
    }
}

} // namespace DetailImg200

inline bool DecodeCanonCr3(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
        if (size < 16 || std::memcmp(data + 4, "ftyp", 4) != 0) return false;
        std::wstring label = L"Canon CR3 RAW (.cr3)";

        ExtendedImageInfo bestImg;
        uint32_t metaW = 0, metaH = 0;
        uint16_t metaOrient = 1;
        DetailImg200::WalkCr3IsobmffBoxes(data, size, 0, size, 0, label, bestImg, metaW, metaH, metaOrient);

        // Unpack Canon CR3 'CRAW' + 'CMP1' RAW sensor track directly when no larger embedded JPEG preview exists
        std::vector<DetailImg200::Cr3Cmp1TrackInfo> cmpTracks;
        DetailImg200::Cr3Cmp1TrackInfo dummyTr;
        DetailImg200::ScanCr3Cmp1InTrak(data, size, 0, size, 0, dummyTr, cmpTracks);
        for (const auto& ct : cmpTracks) {
            uint64_t ctArea = static_cast<uint64_t>(ct.width) * ct.height;
            uint64_t curArea = bestImg.valid ? (static_cast<uint64_t>(bestImg.width) * bestImg.height) : 0ull;
            if (ctArea <= curArea || ct.width < 2 || ct.height < 2) continue;
            if (ct.mdatOffset >= size || ct.mdatSize > size - static_cast<size_t>(ct.mdatOffset)) continue;

            const uint8_t* slice = data + static_cast<size_t>(ct.mdatOffset);
            size_t sliceLen = ct.mdatSize;
            std::vector<uint16_t> cfaSamples;
            uint16_t bps = ct.bitDepth;

            DetailImg200::LosslessJpegResult lj;
            if (DetailImg200::DecodeLosslessJpegSOF3(slice, sliceLen, lj)) {
                bps = lj.precision;
                size_t expectedPx = static_cast<size_t>(ct.width) * ct.height;
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
                DetailImg200::UnpackRawSamplesFromBytes(slice, sliceLen, ct.width, ct.height, 1, bps, true, 1, cfaSamples);
            }

            if (cfaSamples.size() == static_cast<size_t>(ct.width) * ct.height) {
                std::vector<uint8_t> cfaPat = { 0, 1, 1, 2 };
                if (ct.cfaLayout == 1)      cfaPat = { 1, 0, 2, 1 };
                else if (ct.cfaLayout == 2) cfaPat = { 1, 2, 0, 1 };
                else if (ct.cfaLayout == 3) cfaPat = { 2, 1, 1, 0 };
                float neutral[3] = { 1.0f, 1.0f, 1.0f };
                uint32_t act[4] = {};
                ExtendedImageInfo rawCand;
                DetailImg200::DemosaicAndToneMapSensor(
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

        if (!bestImg.valid) {
            DetailImg200::FindAndDecodeBestEmbeddedRaster(data, size, label, bestImg, 16);
        }
        if (bestImg.valid) {
            if (metaOrient > 1 && metaOrient <= 8) {
                DetailImg200::ApplyExifOrientationBGRA(metaOrient, bestImg.width, bestImg.height, bestImg.bgraPixels);
                if (metaOrient >= 5) std::swap(metaW, metaH);
            }
            if (metaW > bestImg.origWidth) { bestImg.origWidth = metaW; bestImg.origHeight = metaH; }
            outInfo = std::move(bestImg);
            return true;
        }
        return DetailImg100::DecodeEmbeddedWicBuffer(data, size, label, outInfo);
}

} // namespace Formats
