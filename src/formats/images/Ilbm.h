#pragma once
// Ilbm.h — Amiga IFF-ILBM / PBM / ACBM Interleaved & Deep Bitmap (.iff / .ilbm / .lbm) Decoder
// Supports: Indexed 1..8-bit, OCS 4-bit CMAP scaling, EHB (Extra Half-Brite),
//           HAM6 & HAM8 (Hold-And-Modify), Deep 24-bit RGB & 32-bit RGBA bitplanes,
//           mskHasMask interleaved mask plane, mskHasTransparentColor, ByteRun1 & ACBM ABIT.

namespace Formats {

inline bool DecodeILBM(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 40 || std::memcmp(data, "FORM", 4) != 0) return false;

    const bool isIlbm = (std::memcmp(data + 8, "ILBM", 4) == 0);
    const bool isPbm  = (std::memcmp(data + 8, "PBM ", 4) == 0);
    const bool isAcbm = (std::memcmp(data + 8, "ACBM", 4) == 0);
    if (!isIlbm && !isPbm && !isAcbm) return false;

    auto rdBE16 = [](const uint8_t* p) -> uint16_t {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
    };
    auto rdBE32 = [](const uint8_t* p) -> uint32_t {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
    };

    const uint32_t formSize = rdBE32(data + 4);
    if (formSize < 4u) return false;
    const size_t limit = std::min<size_t>(size, static_cast<size_t>(formSize) + 8u);

    uint16_t w = 0, h = 0;
    uint8_t nPlanes = 0, masking = 0, compression = 0;
    uint16_t transparentColor = 0;
    bool hasBmhd = false;
    bool hasCmap = false;
    size_t cmapCols = 0;
    uint8_t cmap[256][3] = {};
    uint32_t camgFlags = 0;
    bool hasCamg = false;
    const uint8_t* bodyPtr = nullptr;
    size_t bodyLen = 0;

    size_t pos = 12;
    while (pos + 8 <= limit) {
        const uint8_t* ck = data + pos;
        uint32_t ckLen = rdBE32(ck + 4);
        pos += 8;
        if (ckLen > limit - pos) return false;

        if (std::memcmp(ck, "BMHD", 4) == 0) {
            if (ckLen < 20) return false;
            w = rdBE16(data + pos);
            h = rdBE16(data + pos + 2);
            nPlanes = data[pos + 8];
            masking = data[pos + 9];
            compression = data[pos + 10];
            transparentColor = rdBE16(data + pos + 12);
            hasBmhd = true;
        } else if (std::memcmp(ck, "CMAP", 4) == 0) {
            cmapCols = std::min<size_t>(256, ckLen / 3);
            bool allLowNibbleZero = (cmapCols > 0);
            bool anyHighNibbleNonZero = false;
            for (size_t i = 0; i < cmapCols; ++i) {
                for (int c = 0; c < 3; ++c) {
                    uint8_t v = data[pos + i * 3 + c];
                    cmap[i][c] = v;
                    if ((v & 0x0Fu) != 0u) allLowNibbleZero = false;
                    if ((v & 0xF0u) != 0u) anyHighNibbleNonZero = true;
                }
            }
            // Amiga OCS 4-bit palette stored in upper nibbles (0x00, 0x10 .. 0xF0) -> expand to 0x00..0xFF
            if (allLowNibbleZero && anyHighNibbleNonZero) {
                for (size_t i = 0; i < cmapCols; ++i) {
                    for (int c = 0; c < 3; ++c) {
                        cmap[i][c] = static_cast<uint8_t>(cmap[i][c] | (cmap[i][c] >> 4));
                    }
                }
            }
            hasCmap = (cmapCols > 0);
        } else if (std::memcmp(ck, "CAMG", 4) == 0 && ckLen >= 4) {
            camgFlags = rdBE32(data + pos);
            hasCamg = true;
        } else if (std::memcmp(ck, "BODY", 4) == 0 || (isAcbm && std::memcmp(ck, "ABIT", 4) == 0)) {
            bodyPtr = data + pos;
            bodyLen = ckLen;
        }
        pos += ckLen + (ckLen & 1u);
    }

    if (!hasBmhd || w == 0 || h == 0 || w > 8192 || h > 8192 || nPlanes == 0 || !bodyPtr || bodyLen == 0) {
        return false;
    }
    if (compression > 1u) return false;
    if (isPbm && nPlanes != 8u) return false;
    if (!isPbm && nPlanes > 8u && nPlanes != 24u && nPlanes != 32u) return false;

    const bool hasMaskPlane = (masking == 1u); // mskHasMask
    const bool hasTransCol  = (masking == 2u); // mskHasTransparentColor
    const uint32_t planesPerRow = static_cast<uint32_t>(nPlanes) + (hasMaskPlane ? 1u : 0u);
    const size_t rowBytesPerPlane = ((static_cast<size_t>(w) + 15u) / 16u) * 2u;
    const size_t pbmRowBytes = static_cast<size_t>(w) + (w & 1u);

    size_t unpackedSize = 0;
    if (isPbm) {
        unpackedSize = pbmRowBytes * h * (hasMaskPlane ? 2u : 1u);
    } else {
        unpackedSize = rowBytesPerPlane * planesPerRow * h;
    }
    if (unpackedSize == 0 || unpackedSize > 256ULL * 1024 * 1024) return false;

    std::vector<uint8_t> raw(unpackedSize, 0);
    if (compression == 1u) { // ByteRun1 (PackBits)
        size_t s = 0, d = 0;
        while (s < bodyLen && d < unpackedSize) {
            int8_t n = static_cast<int8_t>(bodyPtr[s++]);
            if (n >= 0) {
                size_t cnt = static_cast<size_t>(n) + 1u;
                if (cnt > bodyLen - s || cnt > unpackedSize - d) return false;
                std::memcpy(&raw[d], &bodyPtr[s], cnt);
                s += cnt;
                d += cnt;
            } else if (n != -128) {
                if (s >= bodyLen) return false;
                uint8_t v = bodyPtr[s++];
                size_t cnt = static_cast<size_t>(-static_cast<int>(n)) + 1u;
                if (cnt > unpackedSize - d) return false;
                std::memset(&raw[d], v, cnt);
                d += cnt;
            }
        }
        if (d < unpackedSize) {
            // Some PBM writers omit the trailing pad byte on uncompressed width when odd
            if (!(isPbm && (w & 1u) != 0u && d >= static_cast<size_t>(w) * h)) {
                return false;
            }
        }
    } else {
        if (isPbm && (w & 1u) != 0u && bodyLen == static_cast<size_t>(w) * h && !hasMaskPlane) {
            for (uint16_t y = 0; y < h; ++y) {
                std::memcpy(&raw[static_cast<size_t>(y) * pbmRowBytes], &bodyPtr[static_cast<size_t>(y) * w], w);
            }
        } else {
            if (bodyLen < unpackedSize) return false;
            std::memcpy(raw.data(), bodyPtr, unpackedSize);
        }
    }

    // Default greyscale ramp if an indexed ILBM lacks CMAP (e.g. 1-bit monochrome / 8-bit greyscale)
    if (!hasCmap && nPlanes <= 8u) {
        uint32_t maxIdx = (1u << nPlanes) - 1u;
        for (uint32_t i = 0; i <= maxIdx && i < 256u; ++i) {
            uint8_t g = static_cast<uint8_t>((i * 255u) / std::max(1u, maxIdx));
            cmap[i][0] = cmap[i][1] = cmap[i][2] = g;
        }
        cmapCols = static_cast<size_t>(maxIdx) + 1u;
    }

    const bool isHam = (!isPbm && hasCamg && (camgFlags & 0x0800u) != 0u &&
                        (nPlanes == 5u || nPlanes == 6u || nPlanes == 7u || nPlanes == 8u));
    const bool isHam8 = (isHam && (nPlanes == 7u || nPlanes == 8u));
    const bool isEhb = (!isPbm && !isHam && nPlanes == 6u &&
                        ((hasCamg && (camgFlags & 0x0080u) != 0u) || (hasCmap && cmapCols <= 32u)));

    if (isEhb) {
        for (size_t i = 0; i < 32u; ++i) {
            cmap[32u + i][0] = static_cast<uint8_t>(cmap[i][0] >> 1);
            cmap[32u + i][1] = static_cast<uint8_t>(cmap[i][1] >> 1);
            cmap[32u + i][2] = static_cast<uint8_t>(cmap[i][2] >> 1);
        }
    }

    const bool hasAlphaOut = (nPlanes == 32u || hasMaskPlane || (hasTransCol && !isHam && nPlanes <= 8u));
    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = hasAlphaOut ? 4u : 3u;
    outInfo.bitsPerChannel = 8u;
    if (isHam8)             outInfo.formatName = L"Amiga IFF-ILBM HAM8 (.ilbm)";
    else if (isHam)         outInfo.formatName = L"Amiga IFF-ILBM HAM6 (.ilbm)";
    else if (isEhb)         outInfo.formatName = L"Amiga IFF-ILBM EHB 64-Color (.ilbm)";
    else if (nPlanes == 24) outInfo.formatName = L"Amiga IFF-ILBM 24-bit Truecolor (.ilbm)";
    else if (nPlanes == 32) outInfo.formatName = L"Amiga IFF-ILBM 32-bit RGBA (.ilbm)";
    else if (isPbm)         outInfo.formatName = L"DeluxePaint IFF-PBM 8-bit (.lbm)";
    else                    outInfo.formatName = L"Amiga IFF-ILBM (.ilbm)";

    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4u);

    auto readPlaneBit = [&](uint16_t y, uint32_t planeIdx, uint16_t x) -> uint32_t {
        const uint8_t* planeRow = nullptr;
        if (isAcbm) {
            // ACBM ABIT stores each full bitplane contiguously before the next bitplane
            planeRow = &raw[(static_cast<size_t>(planeIdx) * h + y) * rowBytesPerPlane];
        } else {
            planeRow = &raw[(static_cast<size_t>(y) * planesPerRow + planeIdx) * rowBytesPerPlane];
        }
        return (static_cast<uint32_t>(planeRow[x >> 3]) >> (7u - (x & 7u))) & 1u;
    };

    for (uint16_t y = 0; y < h; ++y) {
        uint8_t curR = cmap[0][0];
        uint8_t curG = cmap[0][1];
        uint8_t curB = cmap[0][2];

        for (uint16_t x = 0; x < w; ++x) {
            uint8_t r = 0, g = 0, b = 0, a = 255u;

            if (isPbm) {
                uint8_t idx = raw[static_cast<size_t>(y) * pbmRowBytes * (hasMaskPlane ? 2u : 1u) + x];
                r = cmap[idx][0];
                g = cmap[idx][1];
                b = cmap[idx][2];
                if (hasMaskPlane) {
                    uint8_t m = raw[(static_cast<size_t>(y) * 2u + 1u) * pbmRowBytes + x];
                    a = (m == 0u) ? 0u : 255u;
                } else if (hasTransCol && idx == transparentColor) {
                    a = 0u;
                }
            } else if (nPlanes == 24u || nPlanes == 32u) {
                uint32_t rv = 0, gv = 0, bv = 0, av = 255u;
                for (uint32_t p = 0; p < 8u; ++p) rv |= (readPlaneBit(y, p, x) << p);
                for (uint32_t p = 0; p < 8u; ++p) gv |= (readPlaneBit(y, 8u + p, x) << p);
                for (uint32_t p = 0; p < 8u; ++p) bv |= (readPlaneBit(y, 16u + p, x) << p);
                if (nPlanes == 32u) {
                    av = 0u;
                    for (uint32_t p = 0; p < 8u; ++p) av |= (readPlaneBit(y, 24u + p, x) << p);
                }
                r = static_cast<uint8_t>(rv);
                g = static_cast<uint8_t>(gv);
                b = static_cast<uint8_t>(bv);
                a = static_cast<uint8_t>(av);
                if (hasMaskPlane && readPlaneBit(y, nPlanes, x) == 0u) {
                    a = 0u;
                }
            } else {
                uint32_t idx = 0;
                for (uint32_t p = 0; p < nPlanes; ++p) {
                    idx |= (readPlaneBit(y, p, x) << p);
                }
                if (isHam8) {
                    const uint32_t valBits = nPlanes - 2u; // 6 for HAM8 (or 5 for 7-plane HAM)
                    const uint32_t ctrl = (idx >> valBits) & 3u;
                    const uint32_t val  = idx & ((1u << valBits) - 1u);
                    const uint8_t expVal = (valBits == 6u)
                        ? static_cast<uint8_t>((val << 2) | (val >> 4))
                        : static_cast<uint8_t>((val << 3) | (val >> 2));
                    if (ctrl == 0u) {
                        curR = cmap[val][0];
                        curG = cmap[val][1];
                        curB = cmap[val][2];
                    } else if (ctrl == 1u) {
                        curB = expVal;
                    } else if (ctrl == 2u) {
                        curR = expVal;
                    } else {
                        curG = expVal;
                    }
                    r = curR; g = curG; b = curB;
                } else if (isHam) { // HAM6 / HAM5
                    const uint32_t valBits = nPlanes - 2u; // 4 for HAM6 (or 3 for 5-plane HAM)
                    const uint32_t ctrl = (idx >> valBits) & 3u;
                    const uint32_t val  = idx & ((1u << valBits) - 1u);
                    const uint8_t expVal = (valBits == 4u)
                        ? static_cast<uint8_t>((val << 4) | val)
                        : static_cast<uint8_t>((val << 5) | (val << 2) | (val >> 1));
                    if (ctrl == 0u) {
                        curR = cmap[val][0];
                        curG = cmap[val][1];
                        curB = cmap[val][2];
                    } else if (ctrl == 1u) {
                        curB = expVal;
                    } else if (ctrl == 2u) {
                        curR = expVal;
                    } else {
                        curG = expVal;
                    }
                    r = curR; g = curG; b = curB;
                } else {
                    r = cmap[idx][0];
                    g = cmap[idx][1];
                    b = cmap[idx][2];
                    if (hasTransCol && idx == transparentColor) {
                        a = 0u;
                    }
                }
                if (hasMaskPlane && readPlaneBit(y, nPlanes, x) == 0u) {
                    a = 0u;
                }
            }

            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4u], r, g, b, a);
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
