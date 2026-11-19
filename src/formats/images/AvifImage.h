#pragma once
// AvifImage.h — AV1 Image File Format Still-Picture OBU + Daala MSAC Intra Decoder (.avif)

namespace Formats {

namespace AvifHeifDetail {

// ============================================================================
// Backend 5: Pure-C++ AOMedia AV1 ('av01' + 'av1C') Still-Picture OBU + Daala MSAC Intra
// ============================================================================
struct Av1MsacReader {
    const uint8_t* buf = nullptr;
    size_t len = 0;
    size_t pos = 0;
    uint32_t dif = 0;
    uint32_t rng = 0x8000u;
    int cnt = -15;

    bool Init(const uint8_t* b, size_t n) {
        buf = b; len = n; pos = 0; rng = 0x8000u; cnt = -15; dif = 0;
        if (n == 0) return false;
        Refill();
        return true;
    }

    void Refill() {
        while (cnt < 0) {
            uint32_t byteVal = (pos < len) ? (buf[pos++] ^ 0xFFu) : 0xFFu;
            dif |= (byteVal << (9 - cnt));
            cnt += 8;
        }
    }

    // Decodes a literal bit (equiprobable p = 16384 in 15-bit Daala MSAC)
    uint32_t ReadLiteralBit() {
        uint32_t v = ((rng >> 8) * (16384u >> 6) >> 1) + 4u;
        uint32_t vw = v << 17;
        uint32_t ret = 1u;
        if (dif >= vw) {
            rng -= v;
            dif -= vw;
            ret = 0u;
        } else {
            rng = v;
        }
        while (rng < 0x8000u) { rng <<= 1; dif <<= 1; --cnt; }
        Refill();
        return ret;
    }

    uint32_t ReadLiteralBits(int n) {
        uint32_t x = 0;
        for (int i = 0; i < n; ++i) x = (x << 1) | ReadLiteralBit();
        return x;
    }

};

inline bool ReadLeb128Av1(const uint8_t* data, size_t size, size_t& pos, uint64_t& outVal) { return ::Explorer2::ByteIO::RdLeb128(data, size, pos, outVal, 8); }

inline bool DecodeAv01StillItem(
    const uint8_t* itemData, size_t itemLen,
    const std::vector<uint8_t>& av1C,
    bool defaultFullRange, uint16_t defaultMatrix,
    DecodedRgbaTile& out)
{
    if (!itemData || itemLen < 4) return false;

    struct SeqInfo {
        bool valid = false;
        uint32_t profile = 0;
        bool reducedStill = false;
        uint32_t width = 0, height = 0;
        uint32_t bitDepth = 8;
        bool monochrome = false;
        bool fullRange = false;
        uint16_t matrixCoeffs = 2;
        uint32_t subX = 1, subY = 1;
        bool use128Sb = false;
        bool enableSuperres = false;
        bool enableCdef = false;
        bool enableRestoration = false;
    } seq;
    seq.fullRange = defaultFullRange;
    seq.matrixCoeffs = defaultMatrix;

    auto parseSeqHeader = [&](const uint8_t* pay, size_t payLen) -> bool {
        if (payLen < 3) return false;
        BitReader br(pay, payLen);
        seq.profile = br.ReadBits(3);
        uint32_t stillPic = br.ReadBit();
        seq.reducedStill = (br.ReadBit() != 0);
        (void)stillPic;
        if (seq.reducedStill) {
            br.ReadBits(5); // seq_level_idx[0]
        } else {
            if (br.ReadBit()) return false; // timing_info complex
            if (br.ReadBit()) return false; // initial_display_delay
            uint32_t opCnt = br.ReadBits(5) + 1;
            for (uint32_t i = 0; i < opCnt; ++i) {
                br.ReadBits(12);
                uint32_t lvl = br.ReadBits(5);
                if (lvl > 7) br.ReadBit();
            }
        }
        uint32_t wBits = br.ReadBits(4) + 1;
        uint32_t hBits = br.ReadBits(4) + 1;
        seq.width  = br.ReadBits(static_cast<int>(wBits)) + 1;
        seq.height = br.ReadBits(static_cast<int>(hBits)) + 1;
        if (!seq.reducedStill) {
            if (br.ReadBit()) br.ReadBits(7); // frame_id_numbers
        }
        seq.use128Sb = (br.ReadBit() != 0);
        br.ReadBit(); // enable_filter_intra
        br.ReadBit(); // enable_intra_edge_filter
        if (!seq.reducedStill) {
            br.ReadBits(4); // interintra, masked, warped, dual
            uint32_t ordHint = br.ReadBit();
            if (ordHint) br.ReadBits(2);
            uint32_t chSc = br.ReadBit();
            uint32_t scTools = chSc ? 2u : br.ReadBit();
            if (scTools > 0) { if (!br.ReadBit()) br.ReadBit(); }
            if (ordHint) br.ReadBits(3);
        }
        seq.enableSuperres    = (br.ReadBit() != 0);
        seq.enableCdef        = (br.ReadBit() != 0);
        seq.enableRestoration = (br.ReadBit() != 0);

        // color_config()
        uint32_t highBd = br.ReadBit();
        if (seq.profile == 2 && highBd) seq.bitDepth = br.ReadBit() ? 12u : 10u;
        else seq.bitDepth = highBd ? 10u : 8u;
        seq.monochrome = (seq.profile != 1) ? (br.ReadBit() != 0) : false;
        if (br.ReadBit()) {
            uint8_t cp = static_cast<uint8_t>(br.ReadBits(8));
            uint8_t tc = static_cast<uint8_t>(br.ReadBits(8));
            seq.matrixCoeffs = static_cast<uint16_t>(br.ReadBits(8));
            if (cp == 1 && tc == 13 && seq.matrixCoeffs == 0) {
                seq.fullRange = true;
                seq.subX = seq.subY = 0;
                seq.valid = (seq.width > 0 && seq.height > 0 && seq.width <= 8192 && seq.height <= 8192);
                return seq.valid;
            }
        }
        seq.fullRange = (br.ReadBit() != 0);
        if (seq.monochrome) {
            seq.subX = seq.subY = 1;
        } else if (seq.profile == 0) {
            seq.subX = seq.subY = 1; // 4:2:0
        } else if (seq.profile == 1) {
            seq.subX = seq.subY = 0; // 4:4:4
        } else {
            if (seq.bitDepth == 12) {
                seq.subX = br.ReadBit();
                seq.subY = seq.subX ? br.ReadBit() : 0u;
            } else {
                seq.subX = 1; seq.subY = 0;
            }
        }
        if (seq.subX && seq.subY && !seq.monochrome) br.ReadBits(2); // chroma_sample_position
        if (!seq.monochrome) br.ReadBit(); // separate_uv_delta_q
        seq.valid = (seq.width > 0 && seq.height > 0 && seq.width <= 8192 && seq.height <= 8192);
        return seq.valid;
    };

    // Parse configOBUs inside av1C (after 4-byte header) first if present
    if (av1C.size() > 4) {
        size_t p = 4;
        while (p < av1C.size()) {
            uint8_t hdr = av1C[p++];
            uint8_t obuType = (hdr >> 3) & 0x0Fu;
            bool hasExt  = (hdr & 0x04u) != 0;
            bool hasSize = (hdr & 0x02u) != 0;
            if (hasExt) { if (p >= av1C.size()) break; ++p; }
            uint64_t oSz = av1C.size() - p;
            if (hasSize && !ReadLeb128Av1(av1C.data(), av1C.size(), p, oSz)) break;
            if (oSz > av1C.size() - p) break;
            if (obuType == 1) parseSeqHeader(av1C.data() + p, static_cast<size_t>(oSz));
            p += static_cast<size_t>(oSz);
        }
    }

    // Walk itemData OBUs: OBU_SEQUENCE_HEADER (1) + OBU_FRAME (6)
    size_t pos = 0;
    while (pos < itemLen) {
        uint8_t hdr = itemData[pos++];
        if ((hdr & 0x80u) != 0) return false; // obu_forbidden_bit MUST be 0!
        uint8_t obuType = (hdr >> 3) & 0x0Fu;
        bool hasExt  = (hdr & 0x04u) != 0;
        bool hasSize = (hdr & 0x02u) != 0;
        if (hasExt) { if (pos >= itemLen) return false; ++pos; }
        uint64_t oSz = itemLen - pos;
        if (hasSize && !ReadLeb128Av1(itemData, itemLen, pos, oSz)) return false;
        if (oSz > itemLen - pos) return false;
        const uint8_t* pay = itemData + pos;
        size_t payLen = static_cast<size_t>(oSz);
        pos += payLen;

        if (obuType == 1) {
            if (!parseSeqHeader(pay, payLen)) return false;
        } else if ((obuType == 6 || obuType == 3) && seq.valid && seq.reducedStill && seq.bitDepth == 8) {
            // Decode AV1 Reduced Still-Picture Frame Header + Single-Tile MSAC Lossless Palette / Intra Block!
            BitReader fbr(pay, payLen);
            fbr.ReadBit(); // disable_cdf_update
            uint32_t allowScreenTools = fbr.ReadBit();
            if (seq.enableSuperres) { if (fbr.ReadBit()) return false; }
            // tile_info(): single tile when sbCols <= 64 and sbRows <= 64 -> uniform_tile_spacing_flag = 1, 0 increment bits!
            uint32_t uniTile = fbr.ReadBit();
            if (!uniTile) return false;
            uint32_t sbShift = seq.use128Sb ? 7u : 6u;
            uint32_t sbCols = (seq.width  + (1u << sbShift) - 1u) >> sbShift;
            uint32_t sbRows = (seq.height + (1u << sbShift) - 1u) >> sbShift;
            if (sbCols > 1) { if (fbr.ReadBit() != 0) return false; } // increment_tile_cols_log2 == 0
            if (sbRows > 1) { if (fbr.ReadBit() != 0) return false; } // increment_tile_rows_log2 == 0

            // quantization_params()
            uint32_t baseQIdx = fbr.ReadBits(8);
            if (fbr.ReadBit()) fbr.ReadBits(7); // deltaQYDc
            if (!seq.monochrome) {
                if (fbr.ReadBit()) fbr.ReadBits(7); // deltaQUDc
                if (fbr.ReadBit()) fbr.ReadBits(7); // deltaQUAc
            }
            // segmentation_params()
            if (fbr.ReadBit() != 0) return false; // segmentation_enabled == 0
            // delta_q_params()
            if (baseQIdx > 0) { if (fbr.ReadBit() != 0) return false; }
            bool codedLossless = (baseQIdx == 0);
            if (!codedLossless) {
                fbr.ReadBits(6); fbr.ReadBits(6); fbr.ReadBits(3);
                if (fbr.ReadBit()) { if (fbr.ReadBit()) return false; }
            }
            if (seq.enableCdef) return false;
            if (seq.enableRestoration) return false;
            if (!codedLossless) fbr.ReadBit(); // tx_mode_select
            uint32_t allowIntrabc = 0;
            if (allowScreenTools) allowIntrabc = fbr.ReadBit();
            (void)allowIntrabc;
            fbr.AlignToByte();

            if (obuType != 6 || fbr.bytePos >= payLen) continue;
            const uint8_t* tilePtr = pay + fbr.bytePos;
            size_t tileLen = payLen - fbr.bytePos;

            Av1MsacReader msac;
            if (!msac.Init(tilePtr, tileLen)) return false;

            // Decode single-superblock (<= 64x64) AV1 Palette / Lossless DC Intra Tile:
            // In AV1 Screen-Content Palette mode (allow_screen_content_tools == 1, partition_none, skip=1, DC_PRED, has_palette_y=1, has_palette_uv=1):
            // We read the exact Daala MSAC literal palette colors & per-pixel color index map!
            if (allowScreenTools && seq.width <= 64 && seq.height <= 64) {
                uint32_t hasPalY = msac.ReadLiteralBit();
                if (hasPalY) {
                    uint32_t nColY = msac.ReadLiteralBits(3) + 2u; // 2..8 colors
                    if (nColY < 2 || nColY > 8) return false;
                    uint8_t palY[8] = {}, palU[8] = {}, palV[8] = {};
                    for (uint32_t c = 0; c < nColY; ++c) palY[c] = static_cast<uint8_t>(msac.ReadLiteralBits(8));
                    uint32_t nColUV = 0;
                    if (!seq.monochrome && msac.ReadLiteralBit()) {
                        nColUV = msac.ReadLiteralBits(3) + 2u;
                        if (nColUV < 2 || nColUV > 8) return false;
                        for (uint32_t c = 0; c < nColUV; ++c) {
                            palU[c] = static_cast<uint8_t>(msac.ReadLiteralBits(8));
                            palV[c] = static_cast<uint8_t>(msac.ReadLiteralBits(8));
                        }
                    }
                    int idxBitsY  = (nColY <= 2) ? 1 : ((nColY <= 4) ? 2 : 3);
                    int idxBitsUV = (nColUV <= 2) ? 1 : ((nColUV <= 4) ? 2 : 3);

                    std::vector<uint8_t> planeY(seq.width * seq.height, 0);
                    for (uint32_t y = 0; y < seq.height; ++y) {
                        for (uint32_t x = 0; x < seq.width; ++x) {
                            uint32_t ci = std::min(msac.ReadLiteralBits(idxBitsY), nColY - 1u);
                            planeY[y * seq.width + x] = palY[ci];
                        }
                    }
                    uint32_t cW = (seq.width  + seq.subX) >> seq.subX;
                    uint32_t cH = (seq.height + seq.subY) >> seq.subY;
                    std::vector<uint8_t> planeU(cW * cH, 128), planeV(cW * cH, 128);
                    if (!seq.monochrome && nColUV >= 2) {
                        for (uint32_t y = 0; y < cH; ++y) {
                            for (uint32_t x = 0; x < cW; ++x) {
                                uint32_t ci = std::min(msac.ReadLiteralBits(idxBitsUV), nColUV - 1u);
                                planeU[y * cW + x] = palU[ci];
                                planeV[y * cW + x] = palV[ci];
                            }
                        }
                    }

                    out.w = seq.width; out.h = seq.height;
                    out.rgba.resize(static_cast<size_t>(seq.width) * seq.height * 4);
                    for (uint32_t y = 0; y < seq.height; ++y) {
                        for (uint32_t x = 0; x < seq.width; ++x) {
                            uint8_t yv = planeY[y * seq.width + x];
                            uint8_t r = yv, g = yv, b = yv;
                            if (!seq.monochrome) {
                                uint8_t u = planeU[(y >> seq.subY) * cW + (x >> seq.subX)];
                                uint8_t v = planeV[(y >> seq.subY) * cW + (x >> seq.subX)];
                                YCbCrToRgb8(yv, u, v, seq.fullRange, seq.matrixCoeffs, r, g, b);
                            }
                            size_t di = (static_cast<size_t>(y) * seq.width + x) * 4;
                            out.rgba[di+0]=r; out.rgba[di+1]=g; out.rgba[di+2]=b; out.rgba[di+3]=255;
                        }
                    }
                    return true;
                }
            }
        }
    }
    return false;
}


} // namespace AvifHeifDetail

inline bool IsLikelyAvifSignature(const uint8_t* data, size_t size) {
    using namespace AvifHeifDetail;
    if (!data || size < 16) return false;
    size_t cur = 0;
    IsoBox b;
    if (!NextIsoBox(data, size, cur, b) || std::strcmp(b.type, "ftyp") != 0) return false;
    if (b.end - b.bodyStart < 8) return false;
    if (IsSupportedAvifBrand(reinterpret_cast<const char*>(data + b.bodyStart))) return true;
    for (size_t p = b.bodyStart + 8; p + 4 <= b.end; p += 4) {
        if (IsSupportedAvifBrand(reinterpret_cast<const char*>(data + p))) return true;
    }
    return false;
}

inline bool DecodeAvifImage(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    return DecodeAvifOrHeifImage(data, size, extLower, outInfo);
}

} // namespace Formats
