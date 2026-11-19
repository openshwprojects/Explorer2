#pragma once
// UnrealUtx.h — Unreal Engine 1 / 2 Texture Package Decoder (.utx)

namespace Formats {

inline bool DecodeUnrealUtx(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 36) return false;
    if (DetailImg100::RdU32LE(data) != 0x9E2A83C1u) return false;

    uint16_t ueVersion    = DetailImg100::RdU16LE(data + 4);
    uint16_t licenseeVer  = DetailImg100::RdU16LE(data + 6);
    uint32_t packageFlags = DetailImg100::RdU32LE(data + 8);
    uint32_t nameCount    = DetailImg100::RdU32LE(data + 12);
    uint32_t nameOffset   = DetailImg100::RdU32LE(data + 16);
    uint32_t exportCount  = DetailImg100::RdU32LE(data + 20);
    uint32_t exportOffset = DetailImg100::RdU32LE(data + 24);
    uint32_t importCount  = DetailImg100::RdU32LE(data + 28);
    uint32_t importOffset = DetailImg100::RdU32LE(data + 32);
    (void)licenseeVer;
    (void)packageFlags;

    if (nameCount == 0 || exportCount == 0 || nameCount > 262144 || exportCount > 262144 || importCount > 262144) {
        return false;
    }
    if (nameOffset >= size || exportOffset >= size || (importCount > 0 && importOffset >= size)) {
        return false;
    }

    // Unreal Engine FCompactIndex reader (UnFile.h)
    auto rdCompactIndex = [&](size_t& p, size_t limit, int32_t& outVal) -> bool {
        if (p >= limit) return false;
        uint8_t b0 = data[p++];
        bool neg = (b0 & 0x80u) != 0;
        uint32_t val = b0 & 0x3Fu;
        if ((b0 & 0x40u) != 0) {
            for (int shift = 6; shift < 32; shift += 7) {
                if (p >= limit) return false;
                uint8_t bn = data[p++];
                if (shift == 27) {
                    val |= (static_cast<uint32_t>(bn & 0x1Fu) << shift);
                    break;
                }
                val |= (static_cast<uint32_t>(bn & 0x7Fu) << shift);
                if ((bn & 0x80u) == 0) break;
            }
        }
        outVal = neg ? -static_cast<int32_t>(val) : static_cast<int32_t>(val);
        return true;
    };

    // 1. Parse NameTable
    std::vector<std::string> names;
    names.reserve(std::min<uint32_t>(nameCount, 4096));
    {
        size_t p = nameOffset;
        for (uint32_t i = 0; i < nameCount; ++i) {
            std::string nm;
            if (ueVersion < 64) {
                size_t s = p;
                while (p < size && data[p] != 0) ++p;
                if (p >= size) return false;
                nm.assign(reinterpret_cast<const char*>(data + s), p - s);
                ++p; // skip '\0'
            } else {
                int32_t slen = 0;
                if (!rdCompactIndex(p, size, slen)) return false;
                if (slen > 0) {
                    size_t byteCount = static_cast<size_t>(slen);
                    if (byteCount > size - p || byteCount > 4096) return false;
                    size_t trimLen = (data[p + byteCount - 1] == 0) ? (byteCount - 1) : byteCount;
                    nm.assign(reinterpret_cast<const char*>(data + p), trimLen);
                    p += byteCount;
                } else if (slen < 0) {
                    size_t charCount = static_cast<size_t>(-slen);
                    if (charCount > 4096 || charCount * 2 > size - p) return false;
                    for (size_t c = 0; c < charCount; ++c) {
                        uint16_t wc = DetailImg100::RdU16LE(data + p + c * 2);
                        if (wc == 0) break;
                        nm.push_back(wc < 128 ? static_cast<char>(wc) : '?');
                    }
                    p += charCount * 2;
                }
            }
            if (p + 4 > size) return false;
            p += 4; // uint32LE ObjectFlags
            names.push_back(std::move(nm));
        }
    }

    auto getName = [&](int32_t idx) -> std::string_view {
        if (idx >= 0 && static_cast<size_t>(idx) < names.size()) return names[static_cast<size_t>(idx)];
        return {};
    };

    // 2. Parse ImportTable
    struct UeImport {
        int32_t classPackage = 0;
        int32_t className = 0;
        int32_t packageIndex = 0;
        int32_t objectName = 0;
    };
    std::vector<UeImport> imports;
    imports.reserve(std::min<uint32_t>(importCount, 4096));
    if (importCount > 0) {
        size_t p = importOffset;
        for (uint32_t i = 0; i < importCount; ++i) {
            UeImport imp;
            if (!rdCompactIndex(p, size, imp.classPackage)) return false;
            if (!rdCompactIndex(p, size, imp.className)) return false;
            if (p + 4 > size) return false;
            imp.packageIndex = static_cast<int32_t>(DetailImg100::RdU32LE(data + p));
            p += 4;
            if (!rdCompactIndex(p, size, imp.objectName)) return false;
            imports.push_back(imp);
        }
    }

    // 3. Parse ExportTable
    struct UeExport {
        int32_t classIndex = 0;
        int32_t superIndex = 0;
        int32_t groupIndex = 0;
        int32_t objectName = 0;
        uint32_t objectFlags = 0;
        int32_t serialSize = 0;
        int32_t serialOffset = 0;
    };
    std::vector<UeExport> exports;
    exports.reserve(std::min<uint32_t>(exportCount, 4096));
    {
        size_t p = exportOffset;
        for (uint32_t i = 0; i < exportCount; ++i) {
            UeExport exp;
            if (!rdCompactIndex(p, size, exp.classIndex)) return false;
            if (!rdCompactIndex(p, size, exp.superIndex)) return false;
            if (p + 4 > size) return false;
            exp.groupIndex = static_cast<int32_t>(DetailImg100::RdU32LE(data + p));
            p += 4;
            if (!rdCompactIndex(p, size, exp.objectName)) return false;
            if (p + 4 > size) return false;
            exp.objectFlags = DetailImg100::RdU32LE(data + p);
            p += 4;
            if (!rdCompactIndex(p, size, exp.serialSize)) return false;
            if (exp.serialSize > 0) {
                if (!rdCompactIndex(p, size, exp.serialOffset)) return false;
            }
            exports.push_back(exp);
        }
    }

    auto resolveClassName = [&](int32_t classIdx) -> std::string_view {
        if (classIdx < 0) {
            size_t ii = static_cast<size_t>(-classIdx - 1);
            if (ii < imports.size()) return getName(imports[ii].objectName);
        } else if (classIdx > 0) {
            size_t ei = static_cast<size_t>(classIdx - 1);
            if (ei < exports.size()) return getName(exports[ei].objectName);
        }
        return {};
    };

    struct TexProps {
        uint8_t format = 0;      // ETextureFormat: 0=P8, 1=RGBA7, 2=RGB16, 3=DXT1, 4=RGB8, 5=RGBA8, 7=DXT3, 8=DXT5
        int32_t paletteRef = 0;  // Object reference (>0 => exports[paletteRef - 1])
        uint32_t uSize = 0;
        uint32_t vSize = 0;
        bool bMasked = false;
    };

    auto parseTaggedProperties = [&](size_t& p, size_t endLimit, uint32_t objFlags, TexProps& outProps) -> bool {
        // Skip optional FStateFrame if RF_HasStack (0x02000000) is set
        if ((objFlags & 0x02000000u) != 0) {
            int32_t node = 0, stateNode = 0;
            if (!rdCompactIndex(p, endLimit, node) || !rdCompactIndex(p, endLimit, stateNode)) return false;
            if (p + 12 > endLimit) return false;
            p += 12; // ProbeMask (8 bytes) + LatentAction (4 bytes, or 2 in old UE)
            if (node != 0) {
                int32_t off = 0;
                if (!rdCompactIndex(p, endLimit, off)) return false;
            }
        }

        for (int guard = 0; guard < 512; ++guard) {
            int32_t nameIdx = 0;
            if (!rdCompactIndex(p, endLimit, nameIdx)) return false;
            std::string_view propName = getName(nameIdx);
            if (propName.empty()) return false;
            if (DetailImg100::EqualsIgnoreCaseAscii(propName, "None")) {
                return true;
            }

            if (p >= endLimit) return false;
            uint8_t info = data[p++];
            uint8_t propType = info & 0x0Fu;
            uint8_t sizeCode = (info >> 4) & 0x07u;
            bool arrayOrBool = (info & 0x80u) != 0;

            if (propType == 0 || propType > 15) return false;
            if (propType == 10) { // StructProperty: struct FName precedes size
                int32_t structNameIdx = 0;
                if (!rdCompactIndex(p, endLimit, structNameIdx)) return false;
            }

            size_t propValSize = 0;
            if (propType == 3) { // BoolProperty: value stored directly in bit 7 of info byte
                propValSize = 0;
                if (DetailImg100::EqualsIgnoreCaseAscii(propName, "bMasked")) {
                    outProps.bMasked = arrayOrBool;
                }
            } else {
                switch (sizeCode) {
                case 0: propValSize = 1; break;
                case 1: propValSize = 2; break;
                case 2: propValSize = 4; break;
                case 3: propValSize = 12; break;
                case 4: propValSize = 16; break;
                case 5:
                    if (p >= endLimit) return false;
                    propValSize = data[p++];
                    break;
                case 6:
                    if (p + 2 > endLimit) return false;
                    propValSize = DetailImg100::RdU16LE(data + p);
                    p += 2;
                    break;
                case 7:
                    if (p + 4 > endLimit) return false;
                    propValSize = DetailImg100::RdU32LE(data + p);
                    p += 4;
                    break;
                }
                if (arrayOrBool) { // ArrayIndex encoding
                    if (p >= endLimit) return false;
                    uint8_t ai0 = data[p++];
                    if ((ai0 & 0xC0u) == 0xC0u) {
                        if (p + 3 > endLimit) return false;
                        p += 3;
                    } else if ((ai0 & 0x80u) != 0) {
                        if (p + 1 > endLimit) return false;
                        p += 1;
                    }
                }
            }

            if (propValSize > endLimit - p) return false;
            size_t valPos = p;
            if (DetailImg100::EqualsIgnoreCaseAscii(propName, "Format") && propValSize == 1) {
                outProps.format = data[valPos];
            } else if (DetailImg100::EqualsIgnoreCaseAscii(propName, "Palette") && propType == 5) {
                size_t rp = valPos;
                int32_t refIdx = 0;
                if (rdCompactIndex(rp, valPos + propValSize, refIdx)) {
                    outProps.paletteRef = refIdx;
                }
            } else if (DetailImg100::EqualsIgnoreCaseAscii(propName, "USize") && propValSize == 4) {
                outProps.uSize = DetailImg100::RdU32LE(data + valPos);
            } else if (DetailImg100::EqualsIgnoreCaseAscii(propName, "VSize") && propValSize == 4) {
                outProps.vSize = DetailImg100::RdU32LE(data + valPos);
            } else if (DetailImg100::EqualsIgnoreCaseAscii(propName, "UBits") && propValSize == 1 && outProps.uSize == 0 && data[valPos] <= 13) {
                outProps.uSize = 1u << data[valPos];
            } else if (DetailImg100::EqualsIgnoreCaseAscii(propName, "VBits") && propValSize == 1 && outProps.vSize == 0 && data[valPos] <= 13) {
                outProps.vSize = 1u << data[valPos];
            }
            p += propValSize;
        }
        return false;
    };

    // 4. Decode all Palette exports first (256 * 4 RGBA FColor entries)
    struct UePalette {
        bool valid = false;
        std::array<uint8_t, 1024> rgba{};
    };
    std::vector<UePalette> palettes(exports.size());
    int firstValidPalette = -1;

    for (size_t ei = 0; ei < exports.size(); ++ei) {
        const auto& exp = exports[ei];
        if (exp.serialSize <= 0 || exp.serialOffset < 0) continue;
        size_t startOff = static_cast<size_t>(exp.serialOffset);
        size_t len = static_cast<size_t>(exp.serialSize);
        if (startOff > size || len > size - startOff) continue;

        std::string_view cls = resolveClassName(exp.classIndex);
        if (!DetailImg100::EqualsIgnoreCaseAscii(cls, "Palette")) continue;

        size_t p = startOff;
        size_t endOff = startOff + len;
        TexProps dummy{};
        if (!parseTaggedProperties(p, endOff, exp.objectFlags, dummy)) continue;

        int32_t numColors = 0;
        if (!rdCompactIndex(p, endOff, numColors) || numColors <= 0 || numColors > 4096) continue;
        size_t count = static_cast<size_t>(numColors);
        if (count * 4 > endOff - p) continue;

        UePalette pal;
        for (size_t c = 0; c < 256; ++c) {
            pal.rgba[c * 4 + 0] = static_cast<uint8_t>(c);
            pal.rgba[c * 4 + 1] = static_cast<uint8_t>(c);
            pal.rgba[c * 4 + 2] = static_cast<uint8_t>(c);
            pal.rgba[c * 4 + 3] = 255;
        }
        size_t copyCount = std::min<size_t>(count, 256);
        bool anyNonZeroAlpha = false;
        for (size_t c = 0; c < copyCount; ++c) {
            pal.rgba[c * 4 + 0] = data[p + c * 4 + 0];
            pal.rgba[c * 4 + 1] = data[p + c * 4 + 1];
            pal.rgba[c * 4 + 2] = data[p + c * 4 + 2];
            pal.rgba[c * 4 + 3] = data[p + c * 4 + 3];
            if (pal.rgba[c * 4 + 3] != 0) anyNonZeroAlpha = true;
        }
        if (!anyNonZeroAlpha) {
            for (size_t c = 0; c < copyCount; ++c) pal.rgba[c * 4 + 3] = 255;
        }
        pal.valid = true;
        palettes[ei] = pal;
        if (firstValidPalette < 0) firstValidPalette = static_cast<int>(ei);
    }

    // 5. Locate and decode largest valid Texture export
    uint32_t bestArea = 0;
    ExtendedImageInfo bestTex;

    for (size_t ei = 0; ei < exports.size(); ++ei) {
        const auto& exp = exports[ei];
        if (exp.serialSize <= 0 || exp.serialOffset < 0) continue;
        size_t startOff = static_cast<size_t>(exp.serialOffset);
        size_t len = static_cast<size_t>(exp.serialSize);
        if (startOff > size || len > size - startOff) continue;

        std::string_view cls = resolveClassName(exp.classIndex);
        if (!DetailImg100::EqualsIgnoreCaseAscii(cls, "Texture") &&
            !DetailImg100::EqualsIgnoreCaseAscii(cls, "FractalTexture") &&
            !DetailImg100::EqualsIgnoreCaseAscii(cls, "WetTexture") &&
            !DetailImg100::EqualsIgnoreCaseAscii(cls, "FireTexture")) {
            continue;
        }

        size_t p = startOff;
        size_t endOff = startOff + len;
        TexProps props{};
        if (!parseTaggedProperties(p, endOff, exp.objectFlags, props)) continue;
        if (p >= endOff) continue;

        // Read FMipmap count (FCompactIndex in Unreal TArray<FMipmap>)
        int32_t mipCount = 0;
        if (!rdCompactIndex(p, endOff, mipCount) || mipCount <= 0 || mipCount > 32) continue;

        // Parse mip0: support both TLazyArray (with 4-byte skipPos / UE1 widthOffset) and direct DataArray + USize + VSize
        const uint8_t* mipBytes = nullptr;
        size_t mipLen = 0;
        uint32_t mipW = props.uSize;
        uint32_t mipH = props.vSize;

        auto tryParseMipHeader = [&](size_t mp, const uint8_t*& outPtr, size_t& outLen, uint32_t& outW, uint32_t& outH) -> bool {
            int32_t dataCount = 0;
            if (!rdCompactIndex(mp, endOff, dataCount) || dataCount <= 0) return false;
            size_t dLen = static_cast<size_t>(dataCount);
            if (dLen > endOff - mp) return false;
            outPtr = data + mp;
            outLen = dLen;
            size_t tail = mp + dLen;
            if (tail + 8 <= endOff) {
                uint32_t tw = DetailImg100::RdU32LE(data + tail + 0);
                uint32_t th = DetailImg100::RdU32LE(data + tail + 4);
                if (tw >= 1 && tw <= 8192 && th >= 1 && th <= 8192) {
                    outW = tw;
                    outH = th;
                }
            }
            return (outW >= 1 && outW <= 8192 && outH >= 1 && outH <= 8192);
        };

        bool gotMip = false;
        // Variant A: UE2 TLazyArray (uint32LE skipPos followed by FCompactIndex count + bytes, where skipPos == mpAfter + count)
        if (!gotMip && p + 5 <= endOff) {
            uint32_t skipPos = DetailImg100::RdU32LE(data + p);
            size_t mp = p + 4;
            int32_t dc = 0;
            if (rdCompactIndex(mp, endOff, dc) && dc > 0 && mp + static_cast<size_t>(dc) == static_cast<size_t>(skipPos)) {
                gotMip = tryParseMipHeader(p + 4, mipBytes, mipLen, mipW, mipH);
            }
        }
        // Variant B: UE1 UT99 FMipmap (uint32LE widthOffset + TLazyArray uint32LE skipPos + FCompactIndex count + bytes)
        if (!gotMip && p + 9 <= endOff) {
            uint32_t skipPos = DetailImg100::RdU32LE(data + p + 4);
            size_t mp = p + 8;
            int32_t dc = 0;
            if (rdCompactIndex(mp, endOff, dc) && dc > 0 && mp + static_cast<size_t>(dc) == static_cast<size_t>(skipPos)) {
                gotMip = tryParseMipHeader(p + 8, mipBytes, mipLen, mipW, mipH);
            }
        }
        // Variant C: Direct DataArray (FCompactIndex count + bytes + uint32LE USize + uint32LE VSize)
        if (!gotMip) {
            gotMip = tryParseMipHeader(p, mipBytes, mipLen, mipW, mipH);
        }

        if (!gotMip || !mipBytes || mipW == 0 || mipH == 0 || mipW * mipH <= bestArea) continue;

        size_t pxCount = static_cast<size_t>(mipW) * mipH;
        uint32_t bw4 = (mipW + 3u) / 4u;
        uint32_t bh4 = (mipH + 3u) / 4u;
        size_t blocks4 = static_cast<size_t>(bw4) * bh4;

        ExtendedImageInfo cand;
        cand.width = mipW;
        cand.height = mipH;
        cand.origWidth = mipW;
        cand.origHeight = mipH;
        cand.channels = 4;
        cand.bitsPerChannel = 8;
        bool decodedTex = false;

        if (props.format == 0x00) { // TEXF_P8
            if (mipLen >= pxCount) {
                const UePalette* pal = nullptr;
                if (props.paletteRef > 0 && static_cast<size_t>(props.paletteRef - 1) < palettes.size() &&
                    palettes[static_cast<size_t>(props.paletteRef - 1)].valid) {
                    pal = &palettes[static_cast<size_t>(props.paletteRef - 1)];
                } else if (firstValidPalette >= 0) {
                    pal = &palettes[static_cast<size_t>(firstValidPalette)];
                }
                cand.bgraPixels.resize(pxCount * 4);
                for (size_t i = 0; i < pxCount; ++i) {
                    uint8_t idx = mipBytes[i];
                    if (pal) {
                        uint8_t a = (props.bMasked && idx == 0) ? 0u : pal->rgba[idx * 4 + 3];
                        WritePremulBGRA(&cand.bgraPixels[i * 4],
                                        pal->rgba[idx * 4 + 0], pal->rgba[idx * 4 + 1], pal->rgba[idx * 4 + 2], a);
                    } else {
                        WritePremulBGRA(&cand.bgraPixels[i * 4], idx, idx, idx, 255);
                    }
                }
                cand.formatName = L"Unreal Engine Texture P8 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x01) { // TEXF_RGBA7 (BGRA with 0..127 components)
            if (mipLen >= pxCount * 4) {
                cand.bgraPixels.resize(pxCount * 4);
                for (size_t i = 0; i < pxCount; ++i) {
                    uint8_t b = static_cast<uint8_t>(std::min(255, static_cast<int>(mipBytes[i * 4 + 0]) * 2));
                    uint8_t g = static_cast<uint8_t>(std::min(255, static_cast<int>(mipBytes[i * 4 + 1]) * 2));
                    uint8_t r = static_cast<uint8_t>(std::min(255, static_cast<int>(mipBytes[i * 4 + 2]) * 2));
                    uint8_t a = static_cast<uint8_t>(std::min(255, static_cast<int>(mipBytes[i * 4 + 3]) * 2));
                    WritePremulBGRA(&cand.bgraPixels[i * 4], r, g, b, a ? a : 255u);
                }
                cand.formatName = L"Unreal Engine Texture RGBA7 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x02) { // TEXF_RGB16 (RGB565)
            if (mipLen >= pxCount * 2) {
                cand.bgraPixels.resize(pxCount * 4);
                for (size_t i = 0; i < pxCount; ++i) {
                    uint8_t r, g, b;
                    GpuTex::UnpackRgb565(DetailImg100::RdU16LE(mipBytes + i * 2), r, g, b);
                    WritePremulBGRA(&cand.bgraPixels[i * 4], r, g, b, 255);
                }
                cand.formatName = L"Unreal Engine Texture RGB16 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x03) { // TEXF_DXT1
            if (mipLen >= blocks4 * 8) {
                cand.bgraPixels.assign(pxCount * 4, 0);
                uint8_t blk[64];
                for (uint32_t by = 0; by < bh4; ++by) {
                    for (uint32_t bx = 0; bx < bw4; ++bx) {
                        GpuTex::DecodeBC1Block(mipBytes + (static_cast<size_t>(by) * bw4 + bx) * 8, blk, true);
                        GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, mipW, mipH, cand.bgraPixels);
                    }
                }
                cand.formatName = L"Unreal Engine Texture DXT1 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x04) { // TEXF_RGB8
            if (mipLen >= pxCount * 3) {
                cand.bgraPixels.resize(pxCount * 4);
                for (size_t i = 0; i < pxCount; ++i) {
                    WritePremulBGRA(&cand.bgraPixels[i * 4], mipBytes[i * 3 + 2], mipBytes[i * 3 + 1], mipBytes[i * 3 + 0], 255);
                }
                cand.formatName = L"Unreal Engine Texture RGB8 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x05) { // TEXF_RGBA8 (serialized as BGRA8 FColor in UE2)
            if (mipLen >= pxCount * 4) {
                cand.bgraPixels.resize(pxCount * 4);
                for (size_t i = 0; i < pxCount; ++i) {
                    WritePremulBGRA(&cand.bgraPixels[i * 4],
                                    mipBytes[i * 4 + 2], mipBytes[i * 4 + 1], mipBytes[i * 4 + 0], mipBytes[i * 4 + 3]);
                }
                cand.formatName = L"Unreal Engine Texture RGBA8 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x07) { // TEXF_DXT3
            if (mipLen >= blocks4 * 16) {
                cand.bgraPixels.assign(pxCount * 4, 0);
                uint8_t blk[64];
                for (uint32_t by = 0; by < bh4; ++by) {
                    for (uint32_t bx = 0; bx < bw4; ++bx) {
                        GpuTex::DecodeBC2Block(mipBytes + (static_cast<size_t>(by) * bw4 + bx) * 16, blk);
                        GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, mipW, mipH, cand.bgraPixels);
                    }
                }
                cand.formatName = L"Unreal Engine Texture DXT3 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x08) { // TEXF_DXT5
            if (mipLen >= blocks4 * 16) {
                cand.bgraPixels.assign(pxCount * 4, 0);
                uint8_t blk[64];
                for (uint32_t by = 0; by < bh4; ++by) {
                    for (uint32_t bx = 0; bx < bw4; ++bx) {
                        GpuTex::DecodeBC3Block(mipBytes + (static_cast<size_t>(by) * bw4 + bx) * 16, blk);
                        GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, mipW, mipH, cand.bgraPixels);
                    }
                }
                cand.formatName = L"Unreal Engine Texture DXT5 (.utx)";
                decodedTex = true;
            }
        }

        if (decodedTex) {
            std::string_view texObjName = getName(exp.objectName);
            if (!texObjName.empty()) {
                cand.layerNames.push_back(PathUtils::Utf8ToWide(texObjName));
            }
            cand.valid = true;
            bestArea = mipW * mipH;
            bestTex = std::move(cand);
        }
    }

    if (!bestTex.valid || bestArea == 0) {
        return false;
    }
    outInfo = std::move(bestTex);
    return true;
}

} // namespace Formats
