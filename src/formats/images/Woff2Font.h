#pragma once
// Woff2Font.h — W3C Web Open Font Format 2.0 ("wOF2" .woff2) Brotli + Transformed glyf/loca Unpacker & Specimen Decoder

namespace Formats {

namespace DetailWoff2 {

inline bool ReconstructWoff2TransformedGlyfLoca(
    const uint8_t* xfGlyf, size_t xfLen,
    std::vector<uint8_t>& outGlyf, std::vector<uint8_t>& outLoca, uint16_t& outIndexFormat)
{
    using FontOutline::WrU16BE;
    using FontOutline::WrU32BE;
    if (!xfGlyf || xfLen < 36u) return false;
    uint16_t numGlyphs   = DetailLayeredAndHdrImg::RdU16BE(xfGlyf + 4u);
    uint16_t indexFormat = DetailLayeredAndHdrImg::RdU16BE(xfGlyf + 6u);
    uint32_t nContourSz  = DetailLayeredAndHdrImg::RdU32BE(xfGlyf + 8u);
    uint32_t nPointsSz   = DetailLayeredAndHdrImg::RdU32BE(xfGlyf + 12u);
    uint32_t flagSz      = DetailLayeredAndHdrImg::RdU32BE(xfGlyf + 16u);
    uint32_t glyphSz     = DetailLayeredAndHdrImg::RdU32BE(xfGlyf + 20u);
    uint32_t compSz      = DetailLayeredAndHdrImg::RdU32BE(xfGlyf + 24u);
    uint32_t bboxTotalSz = DetailLayeredAndHdrImg::RdU32BE(xfGlyf + 28u);
    uint32_t instrSz     = DetailLayeredAndHdrImg::RdU32BE(xfGlyf + 32u);
    if (numGlyphs == 0u || indexFormat > 1u) return false;

    uint64_t totalSub = 36ull + nContourSz + nPointsSz + flagSz + glyphSz + compSz + bboxTotalSz + instrSz;
    if (totalSub > xfLen || nContourSz < static_cast<uint32_t>(numGlyphs) * 2u) return false;

    uint32_t bboxBitmapBytes = ((static_cast<uint32_t>(numGlyphs) + 31u) >> 5u) * 4u;
    if (bboxTotalSz < bboxBitmapBytes) return false;

    size_t offContour = 36u;
    size_t offPoints  = offContour + nContourSz;
    size_t offFlags   = offPoints  + nPointsSz;
    size_t offGlyph   = offFlags   + flagSz;
    size_t offComp    = offGlyph   + glyphSz;
    size_t offBboxBmp = offComp    + compSz;
    size_t offBboxStr = offBboxBmp + bboxBitmapBytes;
    size_t offInstr   = offBboxBmp + bboxTotalSz;

    size_t pPoints = offPoints, endPoints = offFlags;
    size_t pFlags  = offFlags,  endFlags  = offGlyph;
    size_t pGlyph  = offGlyph,  endGlyph  = offComp;
    size_t pComp   = offComp,   endComp   = offBboxBmp;
    size_t pBbox   = offBboxStr, endBbox  = offInstr;
    size_t pInstr  = offInstr,  endInstr  = offInstr + instrSz;

    auto rd255UShort = [&](size_t& p, size_t lim, uint16_t& outU) -> bool {
        if (p >= lim) return false;
        uint8_t code = xfGlyf[p++];
        if (code == 253u) { if (p + 2u > lim) return false; outU = DetailLayeredAndHdrImg::RdU16BE(xfGlyf + p); p += 2u; return true; }
        if (code == 255u) { if (p >= lim) return false; outU = static_cast<uint16_t>(253u + xfGlyf[p++]); return true; }
        if (code == 254u) { if (p >= lim) return false; outU = static_cast<uint16_t>(506u + xfGlyf[p++]); return true; }
        outU = code; return true;
    };

    outIndexFormat = indexFormat;
    outGlyf.clear();
    std::vector<uint32_t> glyphOffsets(static_cast<size_t>(numGlyphs) + 1u, 0u);

    for (uint16_t gid = 0; gid < numGlyphs; ++gid) {
        glyphOffsets[gid] = static_cast<uint32_t>(outGlyf.size());
        int16_t nContours = DetailLayeredAndHdrImg::RdI16BE(xfGlyf + offContour + static_cast<size_t>(gid) * 2u);
        bool hasExplicitBbox = ((xfGlyf[offBboxBmp + (gid >> 3u)] >> (7u - (gid & 7u))) & 1u) != 0u;

        if (nContours == 0) {
            if (hasExplicitBbox) return false;
            continue;
        }

        if (nContours < 0) {
            if (!hasExplicitBbox || pBbox + 8u > endBbox) return false;
            size_t compStart = pComp;
            bool haveInstr = false;
            uint16_t cFlags = 0x0020u;
            while ((cFlags & 0x0020u) != 0u) {
                if (pComp + 4u > endComp) return false;
                cFlags = DetailLayeredAndHdrImg::RdU16BE(xfGlyf + pComp);
                if ((cFlags & 0x0100u) != 0u) haveInstr = true;
                pComp += 4u;
                pComp += ((cFlags & 0x0001u) != 0u) ? 4u : 2u;
                if ((cFlags & 0x0008u) != 0u) pComp += 2u;
                else if ((cFlags & 0x0040u) != 0u) pComp += 4u;
                else if ((cFlags & 0x0080u) != 0u) pComp += 8u;
                if (pComp > endComp) return false;
            }
            uint16_t instrLen = 0u;
            if (haveInstr) {
                if (!rd255UShort(pGlyph, endGlyph, instrLen) || pInstr + instrLen > endInstr) return false;
            }
            size_t recStart = outGlyf.size();
            outGlyf.resize(recStart + 10u);
            WrU16BE(outGlyf.data() + recStart, static_cast<uint16_t>(nContours));
            std::memcpy(outGlyf.data() + recStart + 2u, xfGlyf + pBbox, 8u);
            pBbox += 8u;
            outGlyf.insert(outGlyf.end(), xfGlyf + compStart, xfGlyf + pComp);
            if (haveInstr) {
                uint8_t il[2]; WrU16BE(il, instrLen);
                outGlyf.insert(outGlyf.end(), il, il + 2u);
                outGlyf.insert(outGlyf.end(), xfGlyf + pInstr, xfGlyf + pInstr + instrLen);
                pInstr += instrLen;
            }
        } else {
            uint16_t nc = static_cast<uint16_t>(nContours);
            std::vector<uint16_t> endPts(nc, 0u);
            uint32_t totalPts = 0u;
            for (uint16_t c = 0; c < nc; ++c) {
                uint16_t np = 0u;
                if (!rd255UShort(pPoints, endPoints, np) || np == 0u) return false;
                totalPts += np;
                if (totalPts > 65535u) return false;
                endPts[c] = static_cast<uint16_t>(totalPts - 1u);
            }

            std::vector<int16_t> dxArr(totalPts, 0), dyArr(totalPts, 0);
            std::vector<uint8_t> onCurveArr(totalPts, 0u);
            int32_t curX = 0, curY = 0;
            int32_t xMin = 32767, yMin = 32767, xMax = -32768, yMax = -32768;

            for (uint32_t pi = 0; pi < totalPts; ++pi) {
                if (pFlags >= endFlags) return false;
                uint8_t fl = xfGlyf[pFlags++];
                onCurveArr[pi] = ((fl & 0x80u) == 0u) ? 1u : 0u;
                uint8_t f = fl & 0x7Fu;
                int32_t dx = 0, dy = 0;
                if (f < 10u) {
                    if (pGlyph >= endGlyph) return false;
                    int32_t mag = (static_cast<int32_t>(f & 14u) << 7) + xfGlyf[pGlyph++];
                    dy = ((f & 1u) != 0u) ? mag : -mag;
                } else if (f < 20u) {
                    if (pGlyph >= endGlyph) return false;
                    uint8_t fi = static_cast<uint8_t>(f - 10u);
                    int32_t mag = (static_cast<int32_t>(fi & 14u) << 7) + xfGlyf[pGlyph++];
                    dx = ((fi & 1u) != 0u) ? mag : -mag;
                } else if (f < 84u) {
                    if (pGlyph >= endGlyph) return false;
                    uint8_t b0 = xfGlyf[pGlyph++];
                    uint8_t fi = static_cast<uint8_t>(f - 20u);
                    int32_t mx = 1 + static_cast<int32_t>(fi & 0x30u) + (b0 >> 4u);
                    int32_t my = 1 + (static_cast<int32_t>(fi & 0x0Cu) << 2) + (b0 & 0x0Fu);
                    dx = ((fi & 1u) != 0u) ? mx : -mx;
                    dy = ((fi & 2u) != 0u) ? my : -my;
                } else if (f < 120u) {
                    if (pGlyph + 2u > endGlyph) return false;
                    uint8_t b0 = xfGlyf[pGlyph++], b1 = xfGlyf[pGlyph++];
                    uint8_t fi = static_cast<uint8_t>(f - 84u);
                    int32_t mx = 1 + ((static_cast<int32_t>(fi / 12u) << 8) + b0);
                    int32_t my = 1 + (((static_cast<int32_t>(fi % 12u) >> 2) << 8) + b1);
                    dx = ((fi & 1u) != 0u) ? mx : -mx;
                    dy = ((fi & 2u) != 0u) ? my : -my;
                } else if (f < 124u) {
                    if (pGlyph + 3u > endGlyph) return false;
                    uint8_t b0 = xfGlyf[pGlyph++], b1 = xfGlyf[pGlyph++], b2 = xfGlyf[pGlyph++];
                    int32_t mx = (static_cast<int32_t>(b0) << 4) + (b1 >> 4u);
                    int32_t my = (static_cast<int32_t>(b1 & 0x0Fu) << 8) + b2;
                    dx = ((f & 1u) != 0u) ? mx : -mx;
                    dy = ((f & 2u) != 0u) ? my : -my;
                } else {
                    if (pGlyph + 4u > endGlyph) return false;
                    uint8_t b0 = xfGlyf[pGlyph++], b1 = xfGlyf[pGlyph++], b2 = xfGlyf[pGlyph++], b3 = xfGlyf[pGlyph++];
                    int32_t mx = (static_cast<int32_t>(b0) << 8) + b1;
                    int32_t my = (static_cast<int32_t>(b2) << 8) + b3;
                    dx = ((f & 1u) != 0u) ? mx : -mx;
                    dy = ((f & 2u) != 0u) ? my : -my;
                }
                dxArr[pi] = static_cast<int16_t>(dx);
                dyArr[pi] = static_cast<int16_t>(dy);
                curX += dx;
                curY += dy;
                xMin = std::min(xMin, curX); xMax = std::max(xMax, curX);
                yMin = std::min(yMin, curY); yMax = std::max(yMax, curY);
            }

            uint16_t instrLen = 0u;
            if (!rd255UShort(pGlyph, endGlyph, instrLen) || pInstr + instrLen > endInstr) return false;

            size_t recStart = outGlyf.size();
            outGlyf.resize(recStart + 10u + static_cast<size_t>(nc) * 2u + 2u + instrLen);
            WrU16BE(outGlyf.data() + recStart + 0u, nc);
            if (hasExplicitBbox) {
                if (pBbox + 8u > endBbox) return false;
                std::memcpy(outGlyf.data() + recStart + 2u, xfGlyf + pBbox, 8u);
                pBbox += 8u;
            } else {
                WrU16BE(outGlyf.data() + recStart + 2u, static_cast<uint16_t>(static_cast<int16_t>(xMin)));
                WrU16BE(outGlyf.data() + recStart + 4u, static_cast<uint16_t>(static_cast<int16_t>(yMin)));
                WrU16BE(outGlyf.data() + recStart + 6u, static_cast<uint16_t>(static_cast<int16_t>(xMax)));
                WrU16BE(outGlyf.data() + recStart + 8u, static_cast<uint16_t>(static_cast<int16_t>(yMax)));
            }
            for (uint16_t c = 0; c < nc; ++c) {
                WrU16BE(outGlyf.data() + recStart + 10u + static_cast<size_t>(c) * 2u, endPts[c]);
            }
            size_t ilOff = recStart + 10u + static_cast<size_t>(nc) * 2u;
            WrU16BE(outGlyf.data() + ilOff, instrLen);
            if (instrLen > 0u) {
                std::memcpy(outGlyf.data() + ilOff + 2u, xfGlyf + pInstr, instrLen);
                pInstr += instrLen;
            }

            std::vector<uint8_t> ttFlags(totalPts, 0u), xBytes, yBytes;
            for (uint32_t pi = 0; pi < totalPts; ++pi) {
                uint8_t tf = onCurveArr[pi];
                int16_t dx = dxArr[pi], dy = dyArr[pi];
                if (dx == 0) {
                    tf |= 0x10u;
                } else if (dx >= -255 && dx <= 255) {
                    tf |= 0x02u | ((dx > 0) ? 0x10u : 0x00u);
                    xBytes.push_back(static_cast<uint8_t>(std::abs(dx)));
                } else {
                    xBytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(dx) >> 8u));
                    xBytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(dx) & 0xFFu));
                }
                if (dy == 0) {
                    tf |= 0x20u;
                } else if (dy >= -255 && dy <= 255) {
                    tf |= 0x04u | ((dy > 0) ? 0x20u : 0x00u);
                    yBytes.push_back(static_cast<uint8_t>(std::abs(dy)));
                } else {
                    yBytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(dy) >> 8u));
                    yBytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(dy) & 0xFFu));
                }
                ttFlags[pi] = tf;
            }
            outGlyf.insert(outGlyf.end(), ttFlags.begin(), ttFlags.end());
            outGlyf.insert(outGlyf.end(), xBytes.begin(), xBytes.end());
            outGlyf.insert(outGlyf.end(), yBytes.begin(), yBytes.end());
        }

        while ((outGlyf.size() & 3u) != 0u) outGlyf.push_back(0u);
    }
    glyphOffsets[numGlyphs] = static_cast<uint32_t>(outGlyf.size());
    if (indexFormat == 0u && (outGlyf.size() / 2u) > 0xFFFFu) {
        outIndexFormat = 1u;
    }
    if (outIndexFormat == 0u) {
        outLoca.resize((static_cast<size_t>(numGlyphs) + 1u) * 2u);
        for (size_t i = 0; i <= numGlyphs; ++i) {
            WrU16BE(outLoca.data() + i * 2u, static_cast<uint16_t>(glyphOffsets[i] >> 1u));
        }
    } else {
        outLoca.resize((static_cast<size_t>(numGlyphs) + 1u) * 4u);
        for (size_t i = 0; i <= numGlyphs; ++i) {
            WrU32BE(outLoca.data() + i * 4u, glyphOffsets[i]);
        }
    }
    return true;
}

inline bool ReconstructSfntFromWoff2(const uint8_t* data, size_t size, std::vector<uint8_t>& outSfnt) {
    using namespace FontOutline;
    outSfnt.clear();
    if (!data || size < 48u || std::memcmp(data, "wOF2", 4) != 0) return false;

    uint32_t flavor        = DetailLayeredAndHdrImg::RdU32BE(data + 4u);
    uint32_t woff2Length   = DetailLayeredAndHdrImg::RdU32BE(data + 8u);
    uint16_t numTables     = DetailLayeredAndHdrImg::RdU16BE(data + 12u);
    uint16_t reserved      = DetailLayeredAndHdrImg::RdU16BE(data + 14u);
    uint32_t totalSfntSize = DetailLayeredAndHdrImg::RdU32BE(data + 16u);
    uint32_t totalCompSize = DetailLayeredAndHdrImg::RdU32BE(data + 20u);

    bool validFlavor = (flavor == 0x00010000u) ||
                       (flavor == MakeSfntTag("OTTO")) ||
                       (flavor == MakeSfntTag("true")) ||
                       (flavor == MakeSfntTag("ttcf"));
    if (!validFlavor || reserved != 0u || woff2Length < 48u || woff2Length > size ||
        numTables == 0u || numTables > 256u || totalCompSize == 0u ||
        totalSfntSize == 0u || totalSfntSize > (64u << 20u)) {
        return false;
    }

    static const char kWoff2KnownTags[63][5] = {
        "cmap","head","hhea","hmtx","maxp","name","OS/2","post","cvt ","fpgm","glyf","loca","prep","CFF ","VORG","EBDT",
        "EBLC","gasp","hdmx","kern","LTSH","PCLT","VDMX","vhea","vmtx","BASE","GDEF","GPOS","GSUB","EBSC","JSTF","MATH",
        "CBDT","CBLC","COLR","CPAL","SVG ","sbix","acnt","avar","bdat","bloc","bsln","cvar","fdsc","feat","fmtx","fvar",
        "gvar","hsty","just","lcar","mort","morx","opbd","prop","trak","Zapf","Silf","Glat","Gloc","Feat","Sill"
    };

    size_t pos = 48u;
    auto rdUIntBase128 = [&](uint32_t& outVal) -> bool {
        uint32_t accum = 0u;
        for (int i = 0; i < 5; ++i) {
            if (pos >= size) return false;
            uint8_t b = data[pos++];
            if (i == 0 && b == 0x80u) return false;
            if ((accum & 0xFE000000u) != 0u) return false;
            accum = (accum << 7u) | static_cast<uint32_t>(b & 0x7Fu);
            if ((b & 0x80u) == 0u) { outVal = accum; return true; }
        }
        return false;
    };

    struct Woff2Entry {
        uint32_t tag = 0;
        uint8_t xformVer = 0;
        uint32_t origLen = 0;
        uint32_t xformLen = 0;
        std::vector<uint8_t> rawBytes;
    };
    std::vector<Woff2Entry> entries;
    entries.reserve(numTables);
    uint64_t totalUnpackedSum = 0ull;

    for (uint16_t i = 0; i < numTables; ++i) {
        if (pos >= size) return false;
        uint8_t flags = data[pos++];
        uint8_t tagIdx = flags & 0x3Fu;
        uint8_t xformVer = (flags >> 6u) & 0x03u;
        Woff2Entry e{};
        e.xformVer = xformVer;
        if (tagIdx == 63u) {
            if (pos + 4u > size) return false;
            e.tag = DetailLayeredAndHdrImg::RdU32BE(data + pos);
            pos += 4u;
        } else {
            e.tag = MakeSfntTag(kWoff2KnownTags[tagIdx]);
        }
        if (!rdUIntBase128(e.origLen)) return false;
        bool isGlyfOrLoca = (e.tag == MakeSfntTag("glyf") || e.tag == MakeSfntTag("loca"));
        bool hasXformLen = isGlyfOrLoca ? (xformVer == 0u) : (xformVer != 0u);
        if (hasXformLen) {
            if (!rdUIntBase128(e.xformLen)) return false;
        } else {
            e.xformLen = e.origLen;
        }
        if (e.tag == MakeSfntTag("loca") && xformVer == 0u && e.xformLen != 0u) return false;
        totalUnpackedSum += e.xformLen;
        if (totalUnpackedSum > (64ull << 20u)) return false;
        entries.push_back(std::move(e));
    }

    if (flavor == MakeSfntTag("ttcf")) {
        if (pos + 4u > size) return false;
        pos += 4u;
        auto rd255UShort = [&](uint16_t& outU) -> bool {
            if (pos >= size) return false;
            uint8_t code = data[pos++];
            if (code == 253u) { if (pos + 2u > size) return false; outU = DetailLayeredAndHdrImg::RdU16BE(data + pos); pos += 2u; return true; }
            if (code == 255u) { if (pos >= size) return false; outU = static_cast<uint16_t>(253u + data[pos++]); return true; }
            if (code == 254u) { if (pos >= size) return false; outU = static_cast<uint16_t>(506u + data[pos++]); return true; }
            outU = code; return true;
        };
        uint16_t nFonts = 0u;
        if (!rd255UShort(nFonts) || nFonts == 0u) return false;
        for (uint16_t f = 0; f < nFonts; ++f) {
            uint16_t nt = 0u;
            if (!rd255UShort(nt) || pos + 4u > size) return false;
            pos += 4u;
            for (uint16_t k = 0; k < nt; ++k) {
                uint16_t idx = 0u;
                if (!rd255UShort(idx)) return false;
            }
        }
    }

    if (totalCompSize > size - pos) return false;
    const uint8_t* compStream = data + pos;

#ifdef _WIN32
    {
        IDWriteFactory5* dwrite5 = nullptr;
        HRESULT hr = ::DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory5),
            reinterpret_cast<IUnknown**>(&dwrite5));
        if (SUCCEEDED(hr) && dwrite5) {
            IDWriteFontFileStream* fontStream = nullptr;
            if (SUCCEEDED(dwrite5->UnpackFontFile(
                    DWRITE_CONTAINER_TYPE_WOFF2,
                    data,
                    static_cast<UINT32>(size),
                    &fontStream)) && fontStream)
            {
                UINT64 sfntSize = 0;
                if (SUCCEEDED(fontStream->GetFileSize(&sfntSize)) && sfntSize >= 28u && sfntSize <= (64ull << 20u)) {
                    const void* fragPtr = nullptr;
                    void* fragCtx = nullptr;
                    if (SUCCEEDED(fontStream->ReadFileFragment(&fragPtr, 0, sfntSize, &fragCtx)) && fragPtr) {
                        const uint8_t* bytes = static_cast<const uint8_t*>(fragPtr);
                        outSfnt.assign(bytes, bytes + static_cast<size_t>(sfntSize));
                        fontStream->ReleaseFileFragment(fragCtx);
                    }
                }
                fontStream->Release();
            }
            dwrite5->Release();
        }
        if (!outSfnt.empty()) return true;
    }
#endif

    if (flavor == MakeSfntTag("ttcf")) return false;
    std::vector<uint8_t> unpacked;
    if (!DetailBrotli::Decompress(compStream, totalCompSize, unpacked, static_cast<size_t>(totalUnpackedSum)) ||
        unpacked.size() != static_cast<size_t>(totalUnpackedSum)) {
        return false;
    }

    int idxGlyf = -1, idxLoca = -1, idxHead = -1;
    size_t uOff = 0u;
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& e = entries[i];
        if (uOff + e.xformLen > unpacked.size()) return false;
        const uint8_t* slice = unpacked.data() + uOff;
        uOff += e.xformLen;

        if (e.tag == MakeSfntTag("glyf")) idxGlyf = static_cast<int>(i);
        else if (e.tag == MakeSfntTag("loca")) idxLoca = static_cast<int>(i);
        else if (e.tag == MakeSfntTag("head")) idxHead = static_cast<int>(i);

        bool isGlyfOrLoca = (e.tag == MakeSfntTag("glyf") || e.tag == MakeSfntTag("loca"));
        bool untransformed = isGlyfOrLoca ? (e.xformVer == 3u) : (e.xformVer == 0u);
        if (untransformed) {
            e.rawBytes.assign(slice, slice + e.xformLen);
        } else if (e.tag == MakeSfntTag("glyf") && e.xformVer == 0u) {
            e.rawBytes.assign(slice, slice + e.xformLen);
        } else if (e.tag == MakeSfntTag("loca") && e.xformVer == 0u) {
            // Reconstructed below from glyf
        } else {
            return false;
        }
    }

    if (idxGlyf >= 0 && entries[idxGlyf].xformVer == 0u) {
        if (idxLoca < 0) return false;
        std::vector<uint8_t> recGlyf, recLoca;
        uint16_t recIdxFmt = 0u;
        if (!ReconstructWoff2TransformedGlyfLoca(
                entries[idxGlyf].rawBytes.data(), entries[idxGlyf].rawBytes.size(),
                recGlyf, recLoca, recIdxFmt)) {
            return false;
        }
        entries[idxGlyf].rawBytes = std::move(recGlyf);
        entries[idxGlyf].origLen  = static_cast<uint32_t>(entries[idxGlyf].rawBytes.size());
        entries[idxLoca].rawBytes = std::move(recLoca);
        entries[idxLoca].origLen  = static_cast<uint32_t>(entries[idxLoca].rawBytes.size());
        if (idxHead >= 0 && entries[idxHead].rawBytes.size() >= 54u) {
            WrU16BE(entries[idxHead].rawBytes.data() + 50u, recIdxFmt);
        }
    }

    if (idxHead >= 0 && entries[idxHead].rawBytes.size() >= 12u) {
        WrU32BE(entries[idxHead].rawBytes.data() + 8u, 0u);
    }

    std::sort(entries.begin(), entries.end(), [](const Woff2Entry& a, const Woff2Entry& b) {
        return a.tag < b.tag;
    });

    size_t totalDataAligned = 0u;
    for (const auto& e : entries) {
        totalDataAligned += (e.rawBytes.size() + 3u) & ~size_t(3u);
    }

    uint16_t entrySelector = 0u;
    uint16_t maxPow2 = 1u;
    while (static_cast<uint32_t>(maxPow2) * 2u <= numTables) {
        maxPow2 = static_cast<uint16_t>(maxPow2 * 2u);
        ++entrySelector;
    }
    uint16_t searchRange = static_cast<uint16_t>(maxPow2 * 16u);
    uint16_t rangeShift  = static_cast<uint16_t>(numTables * 16u - searchRange);

    size_t dirBytes = 12u + static_cast<size_t>(numTables) * 16u;
    outSfnt.assign(dirBytes + totalDataAligned, 0u);
    WrU32BE(outSfnt.data() + 0u, flavor);
    WrU16BE(outSfnt.data() + 4u, numTables);
    WrU16BE(outSfnt.data() + 6u, searchRange);
    WrU16BE(outSfnt.data() + 8u, entrySelector);
    WrU16BE(outSfnt.data() + 10u, rangeShift);

    size_t curOff = dirBytes;
    size_t headTableOff = 0u;
    for (uint16_t i = 0; i < numTables; ++i) {
        const auto& e = entries[i];
        uint32_t csum = e.rawBytes.empty() ? 0u : CalcSfntChecksum(e.rawBytes.data(), e.rawBytes.size());
        uint8_t* rec = outSfnt.data() + 12u + static_cast<size_t>(i) * 16u;
        WrU32BE(rec + 0u, e.tag);
        WrU32BE(rec + 4u, csum);
        WrU32BE(rec + 8u, static_cast<uint32_t>(curOff));
        WrU32BE(rec + 12u, static_cast<uint32_t>(e.rawBytes.size()));
        if (e.tag == MakeSfntTag("head") && e.rawBytes.size() >= 12u) {
            headTableOff = curOff;
        }
        if (!e.rawBytes.empty()) {
            std::memcpy(outSfnt.data() + curOff, e.rawBytes.data(), e.rawBytes.size());
        }
        curOff += (e.rawBytes.size() + 3u) & ~size_t(3u);
    }

    if (headTableOff != 0u) {
        uint32_t fontCsum = CalcSfntChecksum(outSfnt.data(), outSfnt.size());
        WrU32BE(outSfnt.data() + headTableOff + 8u, 0xB1B0AFBAu - fontCsum);
    }
    return true;
}

} // namespace DetailWoff2

inline bool DecodeWoff2Font(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    std::vector<uint8_t> sfnt;
    if (!DetailWoff2::ReconstructSfntFromWoff2(data, size, sfnt)) return false;
    return FontOutline::RenderSfntFontSpecimen(sfnt.data(), sfnt.size(), L"WOFF2 WebFont", outInfo);
}

} // namespace Formats
