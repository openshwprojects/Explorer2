#pragma once
// FontSpecimen.h — TrueType / OpenType CFF & CFF2 / COLR / CBDT / WOFF / WOFF2 Font Specimen Renderer

namespace Formats {

namespace DetailFont {

struct SfntTableSlice {
    uint32_t offset = 0;
    uint32_t length = 0;
    bool valid = false;
};

struct ParsedSfnt {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint32_t flavor = 0;
    uint16_t numTables = 0;
    uint16_t numGlyphs = 0;
    uint16_t unitsPerEm = 1000;
    int16_t indexToLocFormat = 0;
    int16_t ascent = 800;
    int16_t descent = -200;
    uint16_t numHMetrics = 0;
    std::wstring familyName;
    std::wstring subfamilyName;

    SfntTableSlice tblName;
    SfntTableSlice tblHead;
    SfntTableSlice tblMaxp;
    SfntTableSlice tblHhea;
    SfntTableSlice tblHmtx;
    SfntTableSlice tblCmap;
    SfntTableSlice tblLoca;
    SfntTableSlice tblGlyf;
    SfntTableSlice tblCff;
};

inline uint32_t MakeSfntTag(const char t[4]) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(t[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(t[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(t[2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(t[3]));
}

inline uint32_t CalcSfntChecksum(const uint8_t* p, size_t len) {
    uint32_t sum = 0;
    size_t nLongs = len / 4;
    for (size_t i = 0; i < nLongs; ++i) {
        sum += DetailImg100::RdU32BE(p + i * 4);
    }
    size_t rem = len & 3u;
    if (rem > 0) {
        uint8_t tail[4] = { 0, 0, 0, 0 };
        for (size_t i = 0; i < rem; ++i) tail[i] = p[nLongs * 4 + i];
        sum += DetailImg100::RdU32BE(tail);
    }
    return sum;
}

inline void WrU16BE(uint8_t* dst, uint16_t v) {
    dst[0] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    dst[1] = static_cast<uint8_t>(v & 0xFFu);
}

inline void WrU32BE(uint8_t* dst, uint32_t v) {
    dst[0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    dst[1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    dst[2] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    dst[3] = static_cast<uint8_t>(v & 0xFFu);
}

// Reconstruct an uncompressed 4-byte-aligned SFNT .ttf/.otf buffer from a WOFF 1.0 file
inline bool ReconstructSfntFromWoff(const uint8_t* data, size_t size, std::vector<uint8_t>& outSfnt) {
    if (!data || size < 44 || std::memcmp(data, "wOFF", 4) != 0) return false;
    uint32_t flavor = DetailImg100::RdU32BE(data + 4);
    uint16_t numTables = DetailImg100::RdU16BE(data + 12);
    if (numTables == 0 || numTables > 256) return false;
    if (44ull + static_cast<size_t>(numTables) * 20ull > size) return false;

    struct WoffRec {
        uint32_t tag = 0;
        uint32_t woffOffset = 0;
        uint32_t compLength = 0;
        uint32_t origLength = 0;
        uint32_t origChecksum = 0;
        std::vector<uint8_t> rawBytes;
    };
    std::vector<WoffRec> recs(numTables);
    size_t totalDataAligned = 0;

    for (uint16_t i = 0; i < numTables; ++i) {
        const uint8_t* ent = data + 44 + static_cast<size_t>(i) * 20;
        WoffRec& r = recs[i];
        r.tag = DetailImg100::RdU32BE(ent + 0);
        r.woffOffset = DetailImg100::RdU32BE(ent + 4);
        r.compLength = DetailImg100::RdU32BE(ent + 8);
        r.origLength = DetailImg100::RdU32BE(ent + 12);
        r.origChecksum = DetailImg100::RdU32BE(ent + 16);

        if (r.woffOffset > size || r.compLength > size - r.woffOffset) return false;
        if (r.compLength > r.origLength || r.origLength > 64u * 1024u * 1024u) return false;

        if (r.origLength == 0) {
            r.rawBytes.clear();
        } else if (r.compLength < r.origLength) {
            if (!ZipDeflate::InflateZlib(data + r.woffOffset, r.compLength, r.rawBytes, static_cast<size_t>(r.origLength) + 64) ||
                r.rawBytes.size() != r.origLength) {
                return false;
            }
        } else {
            r.rawBytes.assign(data + r.woffOffset, data + r.woffOffset + r.origLength);
        }
        if (r.origChecksum == 0 && !r.rawBytes.empty()) {
            r.origChecksum = CalcSfntChecksum(r.rawBytes.data(), r.rawBytes.size());
        }
        totalDataAligned += (static_cast<size_t>(r.origLength) + 3u) & ~size_t(3u);
    }

    // OpenType/GDI requires table directory sorted ascending by 4-byte tag
    std::sort(recs.begin(), recs.end(), [](const WoffRec& a, const WoffRec& b) {
        return a.tag < b.tag;
    });

    uint16_t entrySelector = 0;
    uint16_t maxPow2 = 1;
    while (static_cast<uint32_t>(maxPow2) * 2u <= numTables) {
        maxPow2 = static_cast<uint16_t>(maxPow2 * 2u);
        ++entrySelector;
    }
    uint16_t searchRange = static_cast<uint16_t>(maxPow2 * 16u);
    uint16_t rangeShift = static_cast<uint16_t>(numTables * 16u - searchRange);

    size_t dirBytes = 12u + static_cast<size_t>(numTables) * 16u;
    outSfnt.assign(dirBytes + totalDataAligned, 0);

    WrU32BE(outSfnt.data() + 0, flavor);
    WrU16BE(outSfnt.data() + 4, numTables);
    WrU16BE(outSfnt.data() + 6, searchRange);
    WrU16BE(outSfnt.data() + 8, entrySelector);
    WrU16BE(outSfnt.data() + 10, rangeShift);

    size_t curOff = dirBytes;
    for (uint16_t i = 0; i < numTables; ++i) {
        uint8_t* recPtr = outSfnt.data() + 12 + static_cast<size_t>(i) * 16;
        WrU32BE(recPtr + 0, recs[i].tag);
        WrU32BE(recPtr + 4, recs[i].origChecksum);
        WrU32BE(recPtr + 8, static_cast<uint32_t>(curOff));
        WrU32BE(recPtr + 12, recs[i].origLength);
        if (!recs[i].rawBytes.empty()) {
            std::memcpy(outSfnt.data() + curOff, recs[i].rawBytes.data(), recs[i].rawBytes.size());
        }
        curOff += (static_cast<size_t>(recs[i].origLength) + 3u) & ~size_t(3u);
    }
    return true;
}

inline bool ParseSfntDirectory(const uint8_t* data, size_t size, ParsedSfnt& out) {
    if (!data || size < 28) return false;
    // Handle TrueType Collection ('ttcf') by jumping to first font offset
    size_t hdrOff = 0;
    if (std::memcmp(data, "ttcf", 4) == 0) {
        if (size < 16) return false;
        uint32_t numFonts = DetailImg100::RdU32BE(data + 8);
        if (numFonts == 0) return false;
        hdrOff = DetailImg100::RdU32BE(data + 12);
        if (hdrOff + 28 > size) return false;
    }

    uint32_t sfntVer = DetailImg100::RdU32BE(data + hdrOff);
    bool validFlavor = (sfntVer == 0x00010000u) ||
                       (sfntVer == MakeSfntTag("OTTO")) ||
                       (sfntVer == MakeSfntTag("true")) ||
                       (sfntVer == MakeSfntTag("typ1"));
    if (!validFlavor) return false;

    uint16_t numTables = DetailImg100::RdU16BE(data + hdrOff + 4);
    if (numTables == 0 || numTables > 256) return false;
    if (hdrOff + 12ull + static_cast<size_t>(numTables) * 16ull > size) return false;

    out.data = data;
    out.size = size;
    out.flavor = sfntVer;
    out.numTables = numTables;

    int validCoreTables = 0;
    for (uint16_t i = 0; i < numTables; ++i) {
        const uint8_t* rec = data + hdrOff + 12 + static_cast<size_t>(i) * 16;
        uint32_t tag = DetailImg100::RdU32BE(rec + 0);
        uint32_t off = DetailImg100::RdU32BE(rec + 8);
        uint32_t len = DetailImg100::RdU32BE(rec + 12);
        if (off > size || len > size - off) return false; // Corrupt/truncated table bounds!

        SfntTableSlice sl{ off, len, true };
        if (tag == MakeSfntTag("name")) { out.tblName = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("head")) { out.tblHead = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("maxp")) { out.tblMaxp = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("hhea")) { out.tblHhea = sl; }
        else if (tag == MakeSfntTag("hmtx")) { out.tblHmtx = sl; }
        else if (tag == MakeSfntTag("cmap")) { out.tblCmap = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("loca")) { out.tblLoca = sl; }
        else if (tag == MakeSfntTag("glyf")) { out.tblGlyf = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("CFF ") || tag == MakeSfntTag("CFF2")) { out.tblCff = sl; ++validCoreTables; }
    }
    if (validCoreTables == 0) return false;

    if (out.tblHead.valid && out.tblHead.length >= 54) {
        const uint8_t* h = data + out.tblHead.offset;
        uint16_t upem = DetailImg100::RdU16BE(h + 18);
        if (upem >= 16 && upem <= 16384) out.unitsPerEm = upem;
        out.indexToLocFormat = DetailImg100::RdI16BE(h + 50);
    }
    if (out.tblMaxp.valid && out.tblMaxp.length >= 6) {
        out.numGlyphs = DetailImg100::RdU16BE(data + out.tblMaxp.offset + 4);
    }
    if (out.tblHhea.valid && out.tblHhea.length >= 36) {
        const uint8_t* hh = data + out.tblHhea.offset;
        int16_t asc = DetailImg100::RdI16BE(hh + 4);
        int16_t dsc = DetailImg100::RdI16BE(hh + 6);
        if (asc > 0) out.ascent = asc;
        if (dsc < 0) out.descent = dsc;
        out.numHMetrics = DetailImg100::RdU16BE(hh + 34);
    } else {
        out.ascent = static_cast<int16_t>((out.unitsPerEm * 4) / 5);
        out.descent = -static_cast<int16_t>(out.unitsPerEm / 5);
    }

    // Parse 'name' table for Family (nameID 1 / 16) and Subfamily (nameID 2 / 17)
    if (out.tblName.valid && out.tblName.length >= 6) {
        const uint8_t* nm = data + out.tblName.offset;
        size_t nmLen = out.tblName.length;
        uint16_t count = DetailImg100::RdU16BE(nm + 2);
        uint16_t strStorageOff = DetailImg100::RdU16BE(nm + 4);
        int bestFamPrio = 0, bestSubPrio = 0;

        for (uint16_t i = 0; i < count && 6u + static_cast<size_t>(i + 1) * 12u <= nmLen; ++i) {
            const uint8_t* nr = nm + 6 + static_cast<size_t>(i) * 12;
            uint16_t platId = DetailImg100::RdU16BE(nr + 0);
            uint16_t encId  = DetailImg100::RdU16BE(nr + 2);
            uint16_t langId = DetailImg100::RdU16BE(nr + 4);
            uint16_t nameId = DetailImg100::RdU16BE(nr + 6);
            uint16_t sLen   = DetailImg100::RdU16BE(nr + 8);
            uint16_t sOff   = DetailImg100::RdU16BE(nr + 10);
            if (nameId != 1 && nameId != 2 && nameId != 16 && nameId != 17) continue;
            size_t absOff = static_cast<size_t>(strStorageOff) + sOff;
            if (sLen == 0 || absOff > nmLen || sLen > nmLen - absOff) continue;

            std::wstring decoded;
            int prio = 0;
            if (platId == 3 || platId == 0) { // Windows / Unicode UTF-16BE
                if ((sLen & 1u) != 0) continue;
                for (uint16_t k = 0; k < sLen; k += 2) {
                    wchar_t wc = static_cast<wchar_t>(DetailImg100::RdU16BE(nm + absOff + k));
                    if (wc != 0) decoded.push_back(wc);
                }
                prio = (platId == 3 && (langId == 0x0409 || langId == 0)) ? 4 : 3;
                (void)encId;
            } else if (platId == 1) { // Macintosh Roman 8-bit
                for (uint16_t k = 0; k < sLen; ++k) {
                    uint8_t ch = nm[absOff + k];
                    if (ch >= 32 && ch < 127) decoded.push_back(static_cast<wchar_t>(ch));
                }
                prio = (langId == 0) ? 2 : 1;
            }
            if (decoded.empty()) continue;

            if (nameId == 1) {
                if (prio > bestFamPrio) { bestFamPrio = prio; out.familyName = std::move(decoded); }
            } else if (nameId == 16 && bestFamPrio == 0) {
                out.familyName = std::move(decoded);
            } else if (nameId == 2) {
                if (prio > bestSubPrio) { bestSubPrio = prio; out.subfamilyName = std::move(decoded); }
            } else if (nameId == 17 && bestSubPrio == 0) {
                out.subfamilyName = std::move(decoded);
            }
        }
    }
    return true;
}

inline uint16_t LookupCmapGlyph(const ParsedSfnt& sfnt, uint32_t codepoint) {
    if (!sfnt.tblCmap.valid || sfnt.tblCmap.length < 4) return 0;
    const uint8_t* cm = sfnt.data + sfnt.tblCmap.offset;
    size_t cmLen = sfnt.tblCmap.length;
    uint16_t numSub = DetailImg100::RdU16BE(cm + 2);

    uint32_t subOff12 = 0, subOff4 = 0, subOff6 = 0, subOff0 = 0;
    for (uint16_t i = 0; i < numSub && 4u + static_cast<size_t>(i + 1) * 8u <= cmLen; ++i) {
        uint32_t off = DetailImg100::RdU32BE(cm + 4 + static_cast<size_t>(i) * 8 + 4);
        if (off + 4 > cmLen) continue;
        uint16_t fmt = DetailImg100::RdU16BE(cm + off);
        if (fmt == 12 && subOff12 == 0) subOff12 = off;
        else if (fmt == 4 && subOff4 == 0) subOff4 = off;
        else if (fmt == 6 && subOff6 == 0) subOff6 = off;
        else if (fmt == 0 && subOff0 == 0) subOff0 = off;
    }

    // Format 12: Segmented coverage (32-bit)
    if (subOff12 != 0 && subOff12 + 16 <= cmLen) {
        uint32_t nGroups = DetailImg100::RdU32BE(cm + subOff12 + 12);
        size_t gBase = subOff12 + 16;
        size_t maxGroups = std::min<size_t>(nGroups, (cmLen - gBase) / 12u);
        for (size_t g = 0; g < maxGroups; ++g) {
            const uint8_t* gp = cm + gBase + g * 12u;
            uint32_t startCp = DetailImg100::RdU32BE(gp + 0);
            uint32_t endCp   = DetailImg100::RdU32BE(gp + 4);
            uint32_t startGl = DetailImg100::RdU32BE(gp + 8);
            if (codepoint < startCp) break;
            if (codepoint <= endCp) {
                return static_cast<uint16_t>((startGl + (codepoint - startCp)) & 0xFFFFu);
            }
        }
    }

    // Format 4: Segment mapping to delta values (16-bit BMP)
    if (subOff4 != 0 && codepoint <= 0xFFFFu && subOff4 + 14 <= cmLen) {
        uint16_t subLen = DetailImg100::RdU16BE(cm + subOff4 + 2);
        size_t lim = std::min<size_t>(cmLen, subOff4 + (subLen > 14 ? subLen : (cmLen - subOff4)));
        uint16_t segCountX2 = DetailImg100::RdU16BE(cm + subOff4 + 6);
        uint16_t segCount = segCountX2 / 2u;
        size_t endCodeOff   = subOff4 + 14;
        size_t startCodeOff = endCodeOff + static_cast<size_t>(segCountX2) + 2u;
        size_t idDeltaOff   = startCodeOff + static_cast<size_t>(segCountX2);
        size_t idRangeOff   = idDeltaOff + static_cast<size_t>(segCountX2);
        if (idRangeOff + segCountX2 <= lim) {
            uint16_t cp16 = static_cast<uint16_t>(codepoint);
            for (uint16_t s = 0; s < segCount; ++s) {
                uint16_t endCode = DetailImg100::RdU16BE(cm + endCodeOff + s * 2u);
                if (cp16 > endCode) continue;
                uint16_t startCode = DetailImg100::RdU16BE(cm + startCodeOff + s * 2u);
                if (cp16 < startCode) break;
                uint16_t idDelta = DetailImg100::RdU16BE(cm + idDeltaOff + s * 2u);
                size_t roPos = idRangeOff + s * 2u;
                uint16_t idRange = DetailImg100::RdU16BE(cm + roPos);
                if (idRange == 0) {
                    return static_cast<uint16_t>((cp16 + idDelta) & 0xFFFFu);
                }
                size_t glyphIdxPos = roPos + idRange + static_cast<size_t>(cp16 - startCode) * 2u;
                if (glyphIdxPos + 2 <= lim) {
                    uint16_t g = DetailImg100::RdU16BE(cm + glyphIdxPos);
                    if (g != 0) return static_cast<uint16_t>((g + idDelta) & 0xFFFFu);
                }
                return 0;
            }
        }
    }

    // Format 6: Trimmed table mapping
    if (subOff6 != 0 && codepoint <= 0xFFFFu && subOff6 + 10 <= cmLen) {
        uint16_t firstCode = DetailImg100::RdU16BE(cm + subOff6 + 6);
        uint16_t entryCount = DetailImg100::RdU16BE(cm + subOff6 + 8);
        if (codepoint >= firstCode && codepoint < static_cast<uint32_t>(firstCode) + entryCount) {
            size_t idxPos = subOff6 + 10 + static_cast<size_t>(codepoint - firstCode) * 2u;
            if (idxPos + 2 <= cmLen) return DetailImg100::RdU16BE(cm + idxPos);
        }
    }

    // Format 0: Byte encoding table
    if (subOff0 != 0 && codepoint < 256 && subOff0 + 6 + 256 <= cmLen) {
        return cm[subOff0 + 6 + codepoint];
    }
    return 0;
}

inline uint16_t GetGlyphAdvance(const ParsedSfnt& sfnt, uint16_t glyphId) {
    if (sfnt.tblHmtx.valid && sfnt.numHMetrics > 0) {
        const uint8_t* hm = sfnt.data + sfnt.tblHmtx.offset;
        size_t hmLen = sfnt.tblHmtx.length;
        uint16_t idx = (glyphId < sfnt.numHMetrics) ? glyphId : static_cast<uint16_t>(sfnt.numHMetrics - 1);
        if (static_cast<size_t>(idx) * 4u + 2u <= hmLen) {
            uint16_t adv = DetailImg100::RdU16BE(hm + static_cast<size_t>(idx) * 4u);
            if (adv > 0) return adv;
        }
    }
    return static_cast<uint16_t>((sfnt.unitsPerEm * 3u) / 5u);
}

struct EdgeSeg {
    float x0, y0, x1, y1;
};

inline bool ExtractGlyphEdges(
    const ParsedSfnt& sfnt,
    uint16_t glyphId,
    std::vector<EdgeSeg>& outEdges,
    int16_t& outXMin,
    int16_t& outXMax,
    int depth = 0)
{
    if (depth > 4 || !sfnt.tblLoca.valid || !sfnt.tblGlyf.valid || sfnt.numGlyphs == 0 || glyphId >= sfnt.numGlyphs) {
        return false;
    }
    const uint8_t* loca = sfnt.data + sfnt.tblLoca.offset;
    size_t locaLen = sfnt.tblLoca.length;
    uint32_t gOff = 0, gEnd = 0;
    if (sfnt.indexToLocFormat == 0) {
        if (static_cast<size_t>(glyphId + 1) * 2u + 2u > locaLen) return false;
        gOff = static_cast<uint32_t>(DetailImg100::RdU16BE(loca + static_cast<size_t>(glyphId) * 2u)) * 2u;
        gEnd = static_cast<uint32_t>(DetailImg100::RdU16BE(loca + static_cast<size_t>(glyphId + 1) * 2u)) * 2u;
    } else {
        if (static_cast<size_t>(glyphId + 1) * 4u + 4u > locaLen) return false;
        gOff = DetailImg100::RdU32BE(loca + static_cast<size_t>(glyphId) * 4u);
        gEnd = DetailImg100::RdU32BE(loca + static_cast<size_t>(glyphId + 1) * 4u);
    }
    if (gOff >= gEnd) return true; // Whitespace / empty glyph
    if (gEnd > sfnt.tblGlyf.length || gEnd - gOff < 10) return false;

    const uint8_t* g = sfnt.data + sfnt.tblGlyf.offset + gOff;
    size_t gLen = gEnd - gOff;
    int16_t numContours = DetailImg100::RdI16BE(g + 0);
    if (depth == 0) {
        outXMin = DetailImg100::RdI16BE(g + 2);
        outXMax = DetailImg100::RdI16BE(g + 6);
    }

    if (numContours == 0) return true;

    // Composite glyph (numContours < 0)
    if (numContours < 0) {
        size_t cp = 10;
        uint16_t compFlags = 0x0020u; // MORE_COMPONENTS
        while ((compFlags & 0x0020u) != 0 && cp + 4 <= gLen) {
            compFlags = DetailImg100::RdU16BE(g + cp);
            uint16_t childGlyph = DetailImg100::RdU16BE(g + cp + 2);
            cp += 4;

            float dx = 0.0f, dy = 0.0f;
            if ((compFlags & 0x0001u) != 0) { // ARG_1_AND_2_ARE_WORDS
                if (cp + 4 > gLen) break;
                if ((compFlags & 0x0002u) != 0) {
                    dx = static_cast<float>(DetailImg100::RdI16BE(g + cp));
                    dy = static_cast<float>(DetailImg100::RdI16BE(g + cp + 2));
                }
                cp += 4;
            } else {
                if (cp + 2 > gLen) break;
                if ((compFlags & 0x0002u) != 0) {
                    dx = static_cast<float>(static_cast<int8_t>(g[cp]));
                    dy = static_cast<float>(static_cast<int8_t>(g[cp + 1]));
                }
                cp += 2;
            }

            float m00 = 1.0f, m01 = 0.0f, m10 = 0.0f, m11 = 1.0f;
            if ((compFlags & 0x0008u) != 0) { // WE_HAVE_A_SCALE
                if (cp + 2 > gLen) break;
                m00 = m11 = static_cast<float>(DetailImg100::RdI16BE(g + cp)) / 16384.0f;
                cp += 2;
            } else if ((compFlags & 0x0040u) != 0) { // WE_HAVE_AN_X_AND_Y_SCALE
                if (cp + 4 > gLen) break;
                m00 = static_cast<float>(DetailImg100::RdI16BE(g + cp)) / 16384.0f;
                m11 = static_cast<float>(DetailImg100::RdI16BE(g + cp + 2)) / 16384.0f;
                cp += 4;
            } else if ((compFlags & 0x0080u) != 0) { // WE_HAVE_A_TWO_BY_TWO
                if (cp + 8 > gLen) break;
                m00 = static_cast<float>(DetailImg100::RdI16BE(g + cp + 0)) / 16384.0f;
                m01 = static_cast<float>(DetailImg100::RdI16BE(g + cp + 2)) / 16384.0f;
                m10 = static_cast<float>(DetailImg100::RdI16BE(g + cp + 4)) / 16384.0f;
                m11 = static_cast<float>(DetailImg100::RdI16BE(g + cp + 6)) / 16384.0f;
                cp += 8;
            }

            std::vector<EdgeSeg> childEdges;
            int16_t cxMin = 0, cxMax = 0;
            if (ExtractGlyphEdges(sfnt, childGlyph, childEdges, cxMin, cxMax, depth + 1)) {
                for (const auto& ce : childEdges) {
                    EdgeSeg te;
                    te.x0 = ce.x0 * m00 + ce.y0 * m10 + dx;
                    te.y0 = ce.x0 * m01 + ce.y0 * m11 + dy;
                    te.x1 = ce.x1 * m00 + ce.y1 * m10 + dx;
                    te.y1 = ce.x1 * m01 + ce.y1 * m11 + dy;
                    outEdges.push_back(te);
                }
            }
        }
        return true;
    }

    // Simple glyph (numContours > 0)
    size_t nc = static_cast<size_t>(numContours);
    if (10u + nc * 2u + 2u > gLen) return false;
    std::vector<uint16_t> endPts(nc);
    for (size_t c = 0; c < nc; ++c) {
        endPts[c] = DetailImg100::RdU16BE(g + 10 + c * 2u);
        if (c > 0 && endPts[c] <= endPts[c - 1]) return false;
    }
    size_t numPts = static_cast<size_t>(endPts.back()) + 1u;
    if (numPts > 8192) return false;

    uint16_t instrLen = DetailImg100::RdU16BE(g + 10 + nc * 2u);
    size_t fp = 10u + nc * 2u + 2u + instrLen;
    if (fp > gLen) return false;

    std::vector<uint8_t> flags(numPts);
    for (size_t i = 0; i < numPts; ) {
        if (fp >= gLen) return false;
        uint8_t fl = g[fp++];
        flags[i++] = fl;
        if ((fl & 0x08u) != 0) { // REPEAT_FLAG
            if (fp >= gLen) return false;
            uint8_t rep = g[fp++];
            for (uint8_t r = 0; r < rep && i < numPts; ++r) flags[i++] = fl;
        }
    }

    std::vector<float> px(numPts), py(numPts);
    int32_t curX = 0;
    for (size_t i = 0; i < numPts; ++i) {
        uint8_t fl = flags[i];
        if ((fl & 0x02u) != 0) { // X_SHORT_VECTOR
            if (fp >= gLen) return false;
            int32_t d = g[fp++];
            curX += ((fl & 0x10u) != 0) ? d : -d;
        } else if ((fl & 0x10u) == 0) {
            if (fp + 2 > gLen) return false;
            curX += DetailImg100::RdI16BE(g + fp);
            fp += 2;
        }
        px[i] = static_cast<float>(curX);
    }

    int32_t curY = 0;
    for (size_t i = 0; i < numPts; ++i) {
        uint8_t fl = flags[i];
        if ((fl & 0x04u) != 0) { // Y_SHORT_VECTOR
            if (fp >= gLen) return false;
            int32_t d = g[fp++];
            curY += ((fl & 0x20u) != 0) ? d : -d;
        } else if ((fl & 0x20u) == 0) {
            if (fp + 2 > gLen) return false;
            curY += DetailImg100::RdI16BE(g + fp);
            fp += 2;
        }
        py[i] = static_cast<float>(curY);
    }

    // Flatten TrueType contours (handling on-curve & off-curve quadratic Beziers with implied midpoints)
    size_t startIdx = 0;
    for (size_t c = 0; c < nc; ++c) {
        size_t endIdx = endPts[c];
        size_t cnt = endIdx - startIdx + 1u;
        if (cnt >= 2) {
            auto ptX = [&](size_t k) { return px[startIdx + (k % cnt)]; };
            auto ptY = [&](size_t k) { return py[startIdx + (k % cnt)]; };
            auto isOn = [&](size_t k) { return (flags[startIdx + (k % cnt)] & 0x01u) != 0; };

            float sx = 0.0f, sy = 0.0f;
            size_t kStart = 0;
            if (isOn(0)) {
                sx = ptX(0); sy = ptY(0);
                kStart = 1;
            } else if (isOn(cnt - 1)) {
                sx = ptX(cnt - 1); sy = ptY(cnt - 1);
                kStart = 0;
            } else {
                sx = 0.5f * (ptX(cnt - 1) + ptX(0));
                sy = 0.5f * (ptY(cnt - 1) + ptY(0));
                kStart = 0;
            }

            float curPx = sx, curPy = sy;
            auto emitQuad = [&](float cx0, float cy0, float qx, float qy, float ex, float ey) {
                float lx = cx0, ly = cy0;
                constexpr int kSteps = 8;
                for (int s = 1; s <= kSteps; ++s) {
                    float t = static_cast<float>(s) / static_cast<float>(kSteps);
                    float omt = 1.0f - t;
                    float nx = omt * omt * cx0 + 2.0f * omt * t * qx + t * t * ex;
                    float ny = omt * omt * cy0 + 2.0f * omt * t * qy + t * t * ey;
                    outEdges.push_back({ lx, ly, nx, ny });
                    lx = nx; ly = ny;
                }
            };

            size_t k = kStart;
            while (k < cnt) {
                if (isOn(k)) {
                    float nx = ptX(k), ny = ptY(k);
                    outEdges.push_back({ curPx, curPy, nx, ny });
                    curPx = nx; curPy = ny;
                    ++k;
                } else {
                    float ctrlX = ptX(k), ctrlY = ptY(k);
                    if (k + 1 < cnt) {
                        if (isOn(k + 1)) {
                            float nx = ptX(k + 1), ny = ptY(k + 1);
                            emitQuad(curPx, curPy, ctrlX, ctrlY, nx, ny);
                            curPx = nx; curPy = ny;
                            k += 2;
                        } else {
                            float midX = 0.5f * (ctrlX + ptX(k + 1));
                            float midY = 0.5f * (ctrlY + ptY(k + 1));
                            emitQuad(curPx, curPy, ctrlX, ctrlY, midX, midY);
                            curPx = midX; curPy = midY;
                            ++k;
                        }
                    } else {
                        emitQuad(curPx, curPy, ctrlX, ctrlY, sx, sy);
                        curPx = sx; curPy = sy;
                        ++k;
                    }
                }
            }
            if (std::abs(curPx - sx) > 1e-3f || std::abs(curPy - sy) > 1e-3f) {
                outEdges.push_back({ curPx, curPy, sx, sy });
            }
        }
        startIdx = endIdx + 1u;
    }
    return true;
}

// 4x4 supersampled non-zero winding scanline rasterizer for TrueType contours
inline bool RasterizeGlyphContours(
    const ParsedSfnt& sfnt,
    uint16_t glyphId,
    float originX,
    float baselineY,
    float pixelHeight,
    uint8_t inkR,
    uint8_t inkG,
    uint8_t inkB,
    std::vector<uint8_t>& bgra,
    uint32_t imgW,
    uint32_t imgH,
    float& outAdvancePx)
{
    float scale = pixelHeight / static_cast<float>(std::max<uint16_t>(16, sfnt.unitsPerEm));
    outAdvancePx = static_cast<float>(GetGlyphAdvance(sfnt, glyphId)) * scale;

    std::vector<EdgeSeg> rawEdges;
    int16_t gxMin = 0, gxMax = 0;
    if (!ExtractGlyphEdges(sfnt, glyphId, rawEdges, gxMin, gxMax, 0)) {
        return false;
    }
    if (rawEdges.empty()) {
        return true;
    }
    if (outAdvancePx <= 1.0f && gxMax > gxMin) {
        outAdvancePx = static_cast<float>(gxMax - gxMin) * scale + pixelHeight * 0.12f;
    }

    std::vector<EdgeSeg> scrEdges;
    scrEdges.reserve(rawEdges.size());
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& e : rawEdges) {
        EdgeSeg se;
        se.x0 = originX + e.x0 * scale;
        se.y0 = baselineY - e.y0 * scale;
        se.x1 = originX + e.x1 * scale;
        se.y1 = baselineY - e.y1 * scale;
        if (std::abs(se.y1 - se.y0) < 1e-5f) continue;
        minX = std::min({ minX, se.x0, se.x1 });
        maxX = std::max({ maxX, se.x0, se.x1 });
        minY = std::min({ minY, se.y0, se.y1 });
        maxY = std::max({ maxY, se.y0, se.y1 });
        scrEdges.push_back(se);
    }
    if (scrEdges.empty()) return true;

    int x0 = std::max(0, static_cast<int>(std::floor(minX)) - 1);
    int x1 = std::min(static_cast<int>(imgW) - 1, static_cast<int>(std::ceil(maxX)) + 1);
    int y0 = std::max(0, static_cast<int>(std::floor(minY)) - 1);
    int y1 = std::min(static_cast<int>(imgH) - 1, static_cast<int>(std::ceil(maxY)) + 1);
    if (x0 > x1 || y0 > y1) return true;

    struct Hit {
        float x;
        int dir;
    };
    std::vector<Hit> hits;
    std::vector<uint8_t> rowCov(static_cast<size_t>(x1 - x0 + 1), 0);

    for (int py = y0; py <= y1; ++py) {
        std::fill(rowCov.begin(), rowCov.end(), uint8_t(0));
        for (int subY = 0; subY < 4; ++subY) {
            float sy = static_cast<float>(py) + (static_cast<float>(subY) + 0.5f) * 0.25f;
            hits.clear();
            for (const auto& e : scrEdges) {
                if ((e.y0 <= sy && e.y1 > sy) || (e.y1 <= sy && e.y0 > sy)) {
                    int dir = (e.y1 > e.y0) ? 1 : -1;
                    float xHit = e.x0 + (sy - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0);
                    hits.push_back({ xHit, dir });
                }
            }
            if (hits.size() < 2) continue;
            std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.x < b.x; });

            int winding = 0;
            for (size_t hi = 0; hi + 1 < hits.size(); ++hi) {
                winding += hits[hi].dir;
                if (winding != 0) {
                    float segL = hits[hi].x;
                    float segR = hits[hi + 1].x;
                    int pxStart = std::max(x0, static_cast<int>(std::floor(segL)));
                    int pxEnd   = std::min(x1, static_cast<int>(std::ceil(segR)));
                    for (int px = pxStart; px <= pxEnd; ++px) {
                        for (int subX = 0; subX < 4; ++subX) {
                            float sx = static_cast<float>(px) + (static_cast<float>(subX) + 0.5f) * 0.25f;
                            if (sx >= segL && sx < segR) {
                                ++rowCov[static_cast<size_t>(px - x0)];
                            }
                        }
                    }
                }
            }
        }

        for (int px = x0; px <= x1; ++px) {
            uint8_t c = rowCov[static_cast<size_t>(px - x0)];
            if (c == 0) continue;
            float a = std::clamp(static_cast<float>(c) / 16.0f, 0.0f, 1.0f);
            uint8_t* dst = &bgra[(static_cast<size_t>(py) * imgW + static_cast<size_t>(px)) * 4];
            dst[0] = static_cast<uint8_t>(dst[0] * (1.0f - a) + inkB * a);
            dst[1] = static_cast<uint8_t>(dst[1] * (1.0f - a) + inkG * a);
            dst[2] = static_cast<uint8_t>(dst[2] * (1.0f - a) + inkR * a);
            dst[3] = 255;
        }
    }
    return true;
}

inline size_t RasterizeTextLineGlyf(
    const ParsedSfnt& sfnt,
    std::wstring_view text,
    float x,
    float baselineY,
    float pixelHeight,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    std::vector<uint8_t>& bgra,
    uint32_t imgW,
    uint32_t imgH)
{
    float curX = x;
    size_t drawnGlyphs = 0;
    for (wchar_t wc : text) {
        if (wc == L' ') {
            uint16_t spGid = LookupCmapGlyph(sfnt, ' ');
            float spAdv = (spGid != 0)
                ? (static_cast<float>(GetGlyphAdvance(sfnt, spGid)) * pixelHeight / std::max<uint16_t>(16, sfnt.unitsPerEm))
                : (pixelHeight * 0.32f);
            curX += std::max(pixelHeight * 0.22f, spAdv);
            continue;
        }
        uint16_t gid = LookupCmapGlyph(sfnt, static_cast<uint32_t>(wc));
        if (gid == 0 && wc >= L'a' && wc <= L'z') {
            gid = LookupCmapGlyph(sfnt, static_cast<uint32_t>(wc - L'a' + L'A'));
        }
        if (gid == 0) {
            curX += pixelHeight * 0.35f;
            continue;
        }
        float adv = 0.0f;
        if (RasterizeGlyphContours(sfnt, gid, curX, baselineY, pixelHeight, r, g, b, bgra, imgW, imgH, adv)) {
            ++drawnGlyphs;
        }
        curX += std::max(pixelHeight * 0.20f, adv);
        if (curX >= static_cast<float>(imgW) - 12.0f) break;
    }
    return drawnGlyphs;
}

inline bool ReconstructWoff2TransformedGlyfLoca(
    const uint8_t* xfGlyf, size_t xfLen,
    std::vector<uint8_t>& outGlyf, std::vector<uint8_t>& outLoca, uint16_t& outIndexFormat)
{
    if (!xfGlyf || xfLen < 36u) return false;
    uint16_t numGlyphs   = DetailImg100::RdU16BE(xfGlyf + 4u);
    uint16_t indexFormat = DetailImg100::RdU16BE(xfGlyf + 6u);
    uint32_t nContourSz  = DetailImg100::RdU32BE(xfGlyf + 8u);
    uint32_t nPointsSz   = DetailImg100::RdU32BE(xfGlyf + 12u);
    uint32_t flagSz      = DetailImg100::RdU32BE(xfGlyf + 16u);
    uint32_t glyphSz     = DetailImg100::RdU32BE(xfGlyf + 20u);
    uint32_t compSz      = DetailImg100::RdU32BE(xfGlyf + 24u);
    uint32_t bboxTotalSz = DetailImg100::RdU32BE(xfGlyf + 28u);
    uint32_t instrSz     = DetailImg100::RdU32BE(xfGlyf + 32u);
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
        if (code == 253u) { if (p + 2u > lim) return false; outU = DetailImg100::RdU16BE(xfGlyf + p); p += 2u; return true; }
        if (code == 255u) { if (p >= lim) return false; outU = static_cast<uint16_t>(253u + xfGlyf[p++]); return true; }
        if (code == 254u) { if (p >= lim) return false; outU = static_cast<uint16_t>(506u + xfGlyf[p++]); return true; }
        outU = code; return true;
    };

    outIndexFormat = indexFormat;
    outGlyf.clear();
    std::vector<uint32_t> glyphOffsets(static_cast<size_t>(numGlyphs) + 1u, 0u);

    for (uint16_t gid = 0; gid < numGlyphs; ++gid) {
        glyphOffsets[gid] = static_cast<uint32_t>(outGlyf.size());
        int16_t nContours = DetailImg100::RdI16BE(xfGlyf + offContour + static_cast<size_t>(gid) * 2u);
        bool hasExplicitBbox = ((xfGlyf[offBboxBmp + (gid >> 3u)] >> (7u - (gid & 7u))) & 1u) != 0u;

        if (nContours == 0) {
            if (hasExplicitBbox) return false; // Empty glyphs cannot have explicit bbox per WOFF2 spec
            continue;
        }

        if (nContours < 0) {
            // Composite glyph
            if (!hasExplicitBbox || pBbox + 8u > endBbox) return false;
            size_t compStart = pComp;
            bool haveInstr = false;
            uint16_t cFlags = 0x0020u;
            while ((cFlags & 0x0020u) != 0u) {
                if (pComp + 4u > endComp) return false;
                cFlags = DetailImg100::RdU16BE(xfGlyf + pComp);
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
            // Simple glyph: decode points, triplets, instructions, and serialize TrueType glyf record
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

            // Encode TrueType flags + X deltas + Y deltas
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

        // Pad glyph record to 4-byte boundary (required for short loca alignment too)
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
    outSfnt.clear();
    if (!data || size < 48u || std::memcmp(data, "wOF2", 4) != 0) return false;

    uint32_t flavor        = DetailImg100::RdU32BE(data + 4u);
    uint32_t woff2Length   = DetailImg100::RdU32BE(data + 8u);
    uint16_t numTables     = DetailImg100::RdU16BE(data + 12u);
    uint16_t reserved      = DetailImg100::RdU16BE(data + 14u);
    uint32_t totalSfntSize = DetailImg100::RdU32BE(data + 16u);
    uint32_t totalCompSize = DetailImg100::RdU32BE(data + 20u);

    bool validFlavor = (flavor == 0x00010000u) ||
                       (flavor == MakeSfntTag("OTTO")) ||
                       (flavor == MakeSfntTag("true")) ||
                       (flavor == MakeSfntTag("ttcf"));
    if (!validFlavor || reserved != 0u || woff2Length < 48u || woff2Length > size ||
        numTables == 0u || numTables > 256u || totalCompSize == 0u ||
        totalSfntSize == 0u || totalSfntSize > (64u << 20u)) {
        return false;
    }

    // Parse WOFF2 table directory & validate stream bounds before unpacking
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
            e.tag = DetailImg100::RdU32BE(data + pos);
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
            if (code == 253u) { if (pos + 2u > size) return false; outU = DetailImg100::RdU16BE(data + pos); pos += 2u; return true; }
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
    // 1. Primary Unpacker: DirectWrite IDWriteFactory5::UnpackFontFile(DWRITE_CONTAINER_TYPE_WOFF2, ...)
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
        if (!outSfnt.empty()) {
            return true;
        }
    }
#endif

    // 2. Pure-C++ Fallback Unpacker: RFC 7932 Brotli + SFNT / glyf / loca reconstruction
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
            e.rawBytes.assign(slice, slice + e.xformLen); // Reconstructed below with loca
        } else if (e.tag == MakeSfntTag("loca") && e.xformVer == 0u) {
            // Empty in stream; reconstructed from glyf
        } else {
            return false; // Unsupported custom table transform
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
        WrU32BE(entries[idxHead].rawBytes.data() + 8u, 0u); // Zero checkSumAdjustment before computing checksums
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

} // namespace DetailFont

namespace DetailArtFont {

// ----------------------------------------------------------------------------
// 6. OpenType CFF / CFF2 Type 2 CharString Rasterizer + COLR/CPAL + CBDT/sbix
// ----------------------------------------------------------------------------
struct CffIndexView {
    const uint8_t* base = nullptr;
    size_t tableRem = 0;
    uint32_t count = 0;
    uint8_t offSize = 0;
    size_t offsetsPos = 0;
    size_t dataBase = 0;
    size_t nextPos = 0;
    bool valid = false;

    uint32_t ReadOff(uint32_t idx) const {
        const uint8_t* p = base + offsetsPos + static_cast<size_t>(idx) * offSize;
        uint32_t v = 0;
        for (uint8_t i = 0; i < offSize; ++i) v = (v << 8) | p[i];
        return v;
    }

    bool GetItem(uint32_t idx, const uint8_t*& outPtr, size_t& outLen) const {
        if (!valid || idx >= count) return false;
        uint32_t o0 = ReadOff(idx);
        uint32_t o1 = ReadOff(idx + 1u);
        if (o0 < 1u || o1 < o0) return false;
        size_t s0 = dataBase + (o0 - 1u);
        size_t s1 = dataBase + (o1 - 1u);
        if (s1 > tableRem) return false;
        outPtr = base + s0;
        outLen = s1 - s0;
        return true;
    }
};

inline bool ParseCffIndex(const uint8_t* tbl, size_t tblLen, size_t pos, bool isCff2, CffIndexView& outIdx) {
    outIdx = {};
    if (isCff2) {
        if (pos + 4u > tblLen) return false;
        outIdx.count = DetailImg100::RdU32BE(tbl + pos);
        pos += 4u;
    } else {
        if (pos + 2u > tblLen) return false;
        outIdx.count = DetailImg100::RdU16BE(tbl + pos);
        pos += 2u;
    }
    if (outIdx.count == 0u) {
        outIdx.base = tbl;
        outIdx.tableRem = tblLen;
        outIdx.nextPos = pos;
        outIdx.valid = true;
        return true;
    }
    if (outIdx.count > 65536u || pos >= tblLen) return false;
    uint8_t offSize = tbl[pos++];
    if (offSize < 1u || offSize > 4u) return false;
    uint64_t offArrayBytes = static_cast<uint64_t>(outIdx.count + 1u) * offSize;
    if (offArrayBytes > tblLen - pos) return false;

    outIdx.base = tbl;
    outIdx.tableRem = tblLen;
    outIdx.offSize = offSize;
    outIdx.offsetsPos = pos;
    outIdx.dataBase = pos + static_cast<size_t>(offArrayBytes);

    uint32_t firstOff = outIdx.ReadOff(0);
    uint32_t lastOff  = outIdx.ReadOff(outIdx.count);
    if (firstOff != 1u || lastOff < 1u || outIdx.dataBase + (lastOff - 1u) > tblLen) return false;
    outIdx.nextPos = outIdx.dataBase + (lastOff - 1u);
    outIdx.valid = true;
    return true;
}

inline void ParseCffDict(
    const uint8_t* dict, size_t dictLen,
    int32_t& outCharStringsOff,
    int32_t& outPrivateSize, int32_t& outPrivateOff,
    int32_t& outSubrsRelOff,
    int32_t& outFdArrayOff)
{
    std::vector<double> args;
    size_t p = 0;
    while (p < dictLen) {
        uint8_t b0 = dict[p++];
        if (b0 <= 21u) {
            uint16_t op = b0;
            if (b0 == 12u) {
                if (p >= dictLen) break;
                op = static_cast<uint16_t>(0x0C00u | dict[p++]);
            }
            if (op == 17u && !args.empty()) {
                outCharStringsOff = static_cast<int32_t>(args.back());
            } else if (op == 18u && args.size() >= 2u) {
                outPrivateSize = static_cast<int32_t>(args[args.size() - 2u]);
                outPrivateOff  = static_cast<int32_t>(args[args.size() - 1u]);
            } else if (op == 19u && !args.empty()) {
                outSubrsRelOff = static_cast<int32_t>(args.back());
            } else if (op == 0x0C24u && !args.empty()) { // 12 36 = FDArray
                outFdArrayOff = static_cast<int32_t>(args.back());
            }
            args.clear();
        } else if (b0 == 28u) {
            if (p + 2u > dictLen) break;
            args.push_back(static_cast<double>(DetailImg100::RdI16BE(dict + p)));
            p += 2u;
        } else if (b0 == 29u) {
            if (p + 4u > dictLen) break;
            int32_t v = static_cast<int32_t>(DetailImg100::RdU32BE(dict + p));
            args.push_back(static_cast<double>(v));
            p += 4u;
        } else if (b0 == 30u) {
            // BCD real number until 0xF nibble
            std::string numStr;
            bool done = false;
            while (p < dictLen && !done) {
                uint8_t nb = dict[p++];
                for (int k = 0; k < 2; ++k) {
                    uint8_t nib = (k == 0) ? (nb >> 4) : (nb & 0x0Fu);
                    if (nib <= 9) numStr.push_back(static_cast<char>('0' + nib));
                    else if (nib == 0x0A) numStr.push_back('.');
                    else if (nib == 0x0B) numStr += "E";
                    else if (nib == 0x0C) numStr += "E-";
                    else if (nib == 0x0E) numStr.push_back('-');
                    else if (nib == 0x0F) { done = true; break; }
                }
            }
            args.push_back(ParseFloatOr(numStr, 0.0f));
        } else if (b0 >= 32u && b0 <= 246u) {
            args.push_back(static_cast<double>(static_cast<int>(b0) - 139));
        } else if (b0 >= 247u && b0 <= 250u) {
            if (p >= dictLen) break;
            args.push_back(static_cast<double>((static_cast<int>(b0) - 247) * 256 + dict[p++] + 108));
        } else if (b0 >= 251u && b0 <= 254u) {
            if (p >= dictLen) break;
            args.push_back(static_cast<double>(-(static_cast<int>(b0) - 251) * 256 - dict[p++] - 108));
        }
    }
}

struct ParsedCffFont {
    const uint8_t* tbl = nullptr;
    size_t tblLen = 0;
    bool isCff2 = false;
    CffIndexView charStrings;
    CffIndexView globalSubrs;
    CffIndexView localSubrs;
    bool valid = false;
};

inline bool ParseCffTable(const uint8_t* tbl, size_t tblLen, bool isCff2Hint, ParsedCffFont& outCff) {
    outCff = {};
    if (!tbl || tblLen < 8) return false;
    uint8_t major = tbl[0];
    uint8_t hdrSize = tbl[2];
    if (hdrSize < 4u || hdrSize > tblLen) return false;

    outCff.tbl = tbl;
    outCff.tblLen = tblLen;

    int32_t csOff = -1, privSz = 0, privOff = -1, subrsRel = -1, fdArrOff = -1;

    if (major == 2u || isCff2Hint) {
        if (tblLen < 5u || major != 2u) return false;
        outCff.isCff2 = true;
        uint16_t topDictLen = DetailImg100::RdU16BE(tbl + 3);
        if (static_cast<size_t>(hdrSize) + topDictLen > tblLen) return false;
        ParseCffDict(tbl + hdrSize, topDictLen, csOff, privSz, privOff, subrsRel, fdArrOff);
        if (!ParseCffIndex(tbl, tblLen, static_cast<size_t>(hdrSize) + topDictLen, true, outCff.globalSubrs)) {
            return false;
        }
    } else if (major == 1u) {
        outCff.isCff2 = false;
        CffIndexView nameIdx, topDictIdx, strIdx;
        if (!ParseCffIndex(tbl, tblLen, hdrSize, false, nameIdx) ||
            !ParseCffIndex(tbl, tblLen, nameIdx.nextPos, false, topDictIdx) ||
            topDictIdx.count == 0u ||
            !ParseCffIndex(tbl, tblLen, topDictIdx.nextPos, false, strIdx) ||
            !ParseCffIndex(tbl, tblLen, strIdx.nextPos, false, outCff.globalSubrs))
        {
            return false;
        }
        const uint8_t* tdPtr = nullptr;
        size_t tdLen = 0;
        if (!topDictIdx.GetItem(0, tdPtr, tdLen)) return false;
        ParseCffDict(tdPtr, tdLen, csOff, privSz, privOff, subrsRel, fdArrOff);
    } else {
        return false;
    }

    if (csOff <= 0 || static_cast<size_t>(csOff) >= tblLen) return false;
    if (!ParseCffIndex(tbl, tblLen, static_cast<size_t>(csOff), outCff.isCff2, outCff.charStrings) ||
        outCff.charStrings.count == 0u) {
        return false;
    }

    // Parse Private DICT (either direct or from FDArray[0]) to locate Local Subrs if present
    if (privOff < 0 && fdArrOff > 0 && static_cast<size_t>(fdArrOff) < tblLen) {
        CffIndexView fdIdx;
        if (ParseCffIndex(tbl, tblLen, static_cast<size_t>(fdArrOff), outCff.isCff2, fdIdx) && fdIdx.count > 0u) {
            const uint8_t* fd0 = nullptr; size_t fd0Len = 0;
            if (fdIdx.GetItem(0, fd0, fd0Len)) {
                int32_t dummyCs = -1, dummySub = -1, dummyFd = -1;
                ParseCffDict(fd0, fd0Len, dummyCs, privSz, privOff, dummySub, dummyFd);
            }
        }
    }
    if (privOff > 0 && privSz > 0 && static_cast<size_t>(privOff) + static_cast<size_t>(privSz) <= tblLen) {
        int32_t dummyCs = -1, dummyPs = 0, dummyPo = -1, dummyFd = -1;
        ParseCffDict(tbl + privOff, static_cast<size_t>(privSz), dummyCs, dummyPs, dummyPo, subrsRel, dummyFd);
        if (subrsRel > 0 && static_cast<size_t>(privOff + subrsRel) < tblLen) {
            ParseCffIndex(tbl, tblLen, static_cast<size_t>(privOff + subrsRel), outCff.isCff2, outCff.localSubrs);
        }
    }

    outCff.valid = true;
    return true;
}

struct CffBuildState {
    std::vector<float> stack;
    float curX = 0.0f, curY = 0.0f;
    float startX = 0.0f, startY = 0.0f;
    bool contourOpen = false;
    bool sawFirstMoveOrStem = false;
    uint32_t numHints = 0;
    float minX = 1e9f, maxX = -1e9f;
    std::vector<DetailFont::EdgeSeg> edges;

    void CloseContour() {
        if (contourOpen) {
            if (std::abs(curX - startX) > 1e-3f || std::abs(curY - startY) > 1e-3f) {
                edges.push_back({ curX, curY, startX, startY });
            }
            contourOpen = false;
        }
    }
    void MoveTo(float dx, float dy) {
        CloseContour();
        curX += dx; curY += dy;
        startX = curX; startY = curY;
        minX = std::min(minX, curX); maxX = std::max(maxX, curX);
        contourOpen = true;
    }
    void LineTo(float dx, float dy) {
        float nx = curX + dx, ny = curY + dy;
        edges.push_back({ curX, curY, nx, ny });
        curX = nx; curY = ny;
        minX = std::min(minX, curX); maxX = std::max(maxX, curX);
        contourOpen = true;
    }
    void CubicTo(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3) {
        float x0 = curX, y0 = curY;
        float x1 = x0 + dx1, y1 = y0 + dy1;
        float x2 = x1 + dx2, y2 = y1 + dy2;
        float x3 = x2 + dx3, y3 = y2 + dy3;
        float lx = x0, ly = y0;
        constexpr int kSteps = 10;
        for (int s = 1; s <= kSteps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(kSteps);
            float u = 1.0f - t;
            float nx = u * u * u * x0 + 3.0f * u * u * t * x1 + 3.0f * u * t * t * x2 + t * t * t * x3;
            float ny = u * u * u * y0 + 3.0f * u * u * t * y1 + 3.0f * u * t * t * y2 + t * t * t * y3;
            edges.push_back({ lx, ly, nx, ny });
            lx = nx; ly = ny;
            minX = std::min(minX, nx); maxX = std::max(maxX, nx);
        }
        curX = x3; curY = y3;
        contourOpen = true;
    }
};

inline int32_t CalcCffSubrBias(uint32_t count) {
    if (count < 1240u) return 107;
    if (count < 33900u) return 1131;
    return 32768;
}

inline bool ExecuteCffCharString(
    const ParsedCffFont& cff,
    const uint8_t* cs,
    size_t csLen,
    CffBuildState& st,
    int callDepth,
    bool& ended)
{
    if (callDepth > 10) return false;
    size_t p = 0;
    auto dropOptionalWidth = [&](size_t modParity) {
        if (!cff.isCff2 && !st.sawFirstMoveOrStem) {
            if ((st.stack.size() % 2u) == modParity && !st.stack.empty()) {
                st.stack.erase(st.stack.begin());
            }
            st.sawFirstMoveOrStem = true;
        }
    };

    while (p < csLen && !ended) {
        uint8_t b0 = cs[p++];
        if (b0 == 28u) {
            if (p + 2u > csLen) return false;
            st.stack.push_back(static_cast<float>(DetailImg100::RdI16BE(cs + p)));
            p += 2u;
        } else if (b0 == 255u) {
            if (p + 4u > csLen) return false;
            int32_t fx = static_cast<int32_t>(DetailImg100::RdU32BE(cs + p));
            st.stack.push_back(static_cast<float>(fx) / 65536.0f);
            p += 4u;
        } else if (b0 >= 32u && b0 <= 246u) {
            st.stack.push_back(static_cast<float>(static_cast<int>(b0) - 139));
        } else if (b0 >= 247u && b0 <= 250u) {
            if (p >= csLen) return false;
            st.stack.push_back(static_cast<float>((static_cast<int>(b0) - 247) * 256 + cs[p++] + 108));
        } else if (b0 >= 251u && b0 <= 254u) {
            if (p >= csLen) return false;
            st.stack.push_back(static_cast<float>(-(static_cast<int>(b0) - 251) * 256 - cs[p++] - 108));
        } else {
            if (b0 == 1u || b0 == 3u || b0 == 18u || b0 == 23u) { // hstem / vstem / hstemhm / vstemhm
                dropOptionalWidth(1u);
                st.numHints += static_cast<uint32_t>(st.stack.size() / 2u);
                st.stack.clear();
            } else if (b0 == 19u || b0 == 20u) { // hintmask / cntrmask
                dropOptionalWidth(1u);
                st.numHints += static_cast<uint32_t>(st.stack.size() / 2u);
                st.stack.clear();
                size_t maskBytes = (st.numHints + 7u) / 8u;
                if (p + maskBytes > csLen) return false;
                p += maskBytes;
            } else if (b0 == 21u) { // rmoveto
                if (!cff.isCff2 && !st.sawFirstMoveOrStem) {
                    if (st.stack.size() > 2u) st.stack.erase(st.stack.begin());
                    st.sawFirstMoveOrStem = true;
                }
                if (st.stack.size() < 2u) return false;
                st.MoveTo(st.stack[0], st.stack[1]);
                st.stack.clear();
            } else if (b0 == 22u) { // hmoveto
                if (!cff.isCff2 && !st.sawFirstMoveOrStem) {
                    if (st.stack.size() > 1u) st.stack.erase(st.stack.begin());
                    st.sawFirstMoveOrStem = true;
                }
                if (st.stack.empty()) return false;
                st.MoveTo(st.stack[0], 0.0f);
                st.stack.clear();
            } else if (b0 == 4u) { // vmoveto
                if (!cff.isCff2 && !st.sawFirstMoveOrStem) {
                    if (st.stack.size() > 1u) st.stack.erase(st.stack.begin());
                    st.sawFirstMoveOrStem = true;
                }
                if (st.stack.empty()) return false;
                st.MoveTo(0.0f, st.stack[0]);
                st.stack.clear();
            } else if (b0 == 5u) { // rlineto
                for (size_t i = 0; i + 1u < st.stack.size(); i += 2u) st.LineTo(st.stack[i], st.stack[i + 1u]);
                st.stack.clear();
            } else if (b0 == 6u || b0 == 7u) { // hlineto / vlineto
                bool horiz = (b0 == 6u);
                for (size_t i = 0; i < st.stack.size(); ++i) {
                    if (horiz) st.LineTo(st.stack[i], 0.0f);
                    else       st.LineTo(0.0f, st.stack[i]);
                    horiz = !horiz;
                }
                st.stack.clear();
            } else if (b0 == 8u) { // rrcurveto
                for (size_t i = 0; i + 5u < st.stack.size(); i += 6u) {
                    st.CubicTo(st.stack[i], st.stack[i + 1], st.stack[i + 2],
                               st.stack[i + 3], st.stack[i + 4], st.stack[i + 5]);
                }
                st.stack.clear();
            } else if (b0 == 24u) { // rcurveline
                size_t i = 0;
                for (; i + 7u < st.stack.size(); i += 6u) {
                    st.CubicTo(st.stack[i], st.stack[i + 1], st.stack[i + 2],
                               st.stack[i + 3], st.stack[i + 4], st.stack[i + 5]);
                }
                if (i + 1u < st.stack.size()) st.LineTo(st.stack[i], st.stack[i + 1u]);
                st.stack.clear();
            } else if (b0 == 25u) { // rlinecurve
                size_t n = st.stack.size();
                if (n >= 8u) {
                    size_t lineEnd = n - 6u;
                    for (size_t i = 0; i + 1u < lineEnd; i += 2u) st.LineTo(st.stack[i], st.stack[i + 1u]);
                    st.CubicTo(st.stack[lineEnd], st.stack[lineEnd + 1], st.stack[lineEnd + 2],
                               st.stack[lineEnd + 3], st.stack[lineEnd + 4], st.stack[lineEnd + 5]);
                }
                st.stack.clear();
            } else if (b0 == 26u) { // vvcurveto
                size_t i = 0;
                float dx1 = 0.0f;
                if ((st.stack.size() & 3u) == 1u) dx1 = st.stack[i++];
                for (; i + 3u < st.stack.size(); i += 4u) {
                    st.CubicTo(dx1, st.stack[i], st.stack[i + 1], st.stack[i + 2], 0.0f, st.stack[i + 3]);
                    dx1 = 0.0f;
                }
                st.stack.clear();
            } else if (b0 == 27u) { // hhcurveto
                size_t i = 0;
                float dy1 = 0.0f;
                if ((st.stack.size() & 3u) == 1u) dy1 = st.stack[i++];
                for (; i + 3u < st.stack.size(); i += 4u) {
                    st.CubicTo(st.stack[i], dy1, st.stack[i + 1], st.stack[i + 2], st.stack[i + 3], 0.0f);
                    dy1 = 0.0f;
                }
                st.stack.clear();
            } else if (b0 == 30u || b0 == 31u) { // vhcurveto / hvcurveto
                bool vh = (b0 == 30u);
                size_t i = 0;
                while (i + 3u < st.stack.size()) {
                    float extra = (i + 5u == st.stack.size()) ? st.stack[i + 4u] : 0.0f;
                    if (vh) st.CubicTo(0.0f, st.stack[i], st.stack[i + 1], st.stack[i + 2], st.stack[i + 3], extra);
                    else    st.CubicTo(st.stack[i], 0.0f, st.stack[i + 1], st.stack[i + 2], extra, st.stack[i + 3]);
                    i += (extra != 0.0f || i + 5u == st.stack.size()) ? 5u : 4u;
                    vh = !vh;
                }
                st.stack.clear();
            } else if (b0 == 10u || b0 == 29u) { // callsubr / callgsubr
                if (st.stack.empty()) return false;
                int32_t subNum = static_cast<int32_t>(std::llround(st.stack.back()));
                st.stack.pop_back();
                const CffIndexView& bank = (b0 == 10u) ? cff.localSubrs : cff.globalSubrs;
                if (!bank.valid || bank.count == 0u) return false;
                int32_t idx = subNum + CalcCffSubrBias(bank.count);
                if (idx < 0 || static_cast<uint32_t>(idx) >= bank.count) return false;
                const uint8_t* subPtr = nullptr; size_t subLen = 0;
                if (!bank.GetItem(static_cast<uint32_t>(idx), subPtr, subLen)) return false;
                if (!ExecuteCffCharString(cff, subPtr, subLen, st, callDepth + 1, ended)) return false;
            } else if (b0 == 11u) { // return
                return true;
            } else if (b0 == 14u) { // endchar
                st.CloseContour();
                st.stack.clear();
                ended = true;
                return true;
            } else if (b0 == 12u) {
                if (p >= csLen) return false;
                uint8_t b1 = cs[p++];
                if (b1 == 35u && st.stack.size() >= 12u) { // flex
                    st.CubicTo(st.stack[0], st.stack[1], st.stack[2], st.stack[3], st.stack[4], st.stack[5]);
                    st.CubicTo(st.stack[6], st.stack[7], st.stack[8], st.stack[9], st.stack[10], st.stack[11]);
                } else if (b1 == 34u && st.stack.size() >= 7u) { // hflex
                    st.CubicTo(st.stack[0], 0.0f, st.stack[1], st.stack[2], st.stack[3], 0.0f);
                    st.CubicTo(st.stack[4], 0.0f, st.stack[5], -st.stack[2], st.stack[6], 0.0f);
                }
                st.stack.clear();
            } else {
                st.stack.clear();
            }
        }
    }
    if (callDepth == 0) st.CloseContour();
    return true;
}

inline void RasterizeEdgeListToCanvas(
    const DetailFont::ParsedSfnt& sfnt,
    uint16_t glyphId,
    const std::vector<DetailFont::EdgeSeg>& rawEdges,
    float gxMin, float gxMax,
    float originX, float baselineY, float pixelHeight,
    uint8_t inkR, uint8_t inkG, uint8_t inkB, uint8_t inkA,
    std::vector<uint8_t>& bgra, uint32_t imgW, uint32_t imgH,
    float& outAdvPx)
{
    float scale = pixelHeight / static_cast<float>(std::max<uint16_t>(16, sfnt.unitsPerEm));
    outAdvPx = static_cast<float>(DetailFont::GetGlyphAdvance(sfnt, glyphId)) * scale;
    if (outAdvPx <= 1.0f && gxMax > gxMin) {
        outAdvPx = (gxMax - gxMin) * scale + pixelHeight * 0.12f;
    }
    if (rawEdges.empty() || inkA == 0) return;

    std::vector<DetailFont::EdgeSeg> scrEdges;
    scrEdges.reserve(rawEdges.size());
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& e : rawEdges) {
        DetailFont::EdgeSeg se{
            originX + e.x0 * scale, baselineY - e.y0 * scale,
            originX + e.x1 * scale, baselineY - e.y1 * scale
        };
        if (std::abs(se.y1 - se.y0) < 1e-5f) continue;
        minX = std::min({ minX, se.x0, se.x1 }); maxX = std::max({ maxX, se.x0, se.x1 });
        minY = std::min({ minY, se.y0, se.y1 }); maxY = std::max({ maxY, se.y0, se.y1 });
        scrEdges.push_back(se);
    }
    if (scrEdges.empty()) return;

    int x0 = std::max(0, static_cast<int>(std::floor(minX)) - 1);
    int x1 = std::min(static_cast<int>(imgW) - 1, static_cast<int>(std::ceil(maxX)) + 1);
    int y0 = std::max(0, static_cast<int>(std::floor(minY)) - 1);
    int y1 = std::min(static_cast<int>(imgH) - 1, static_cast<int>(std::ceil(maxY)) + 1);
    if (x0 > x1 || y0 > y1) return;

    struct Hit { float x; int dir; };
    std::vector<Hit> hits;
    std::vector<uint8_t> rowCov(static_cast<size_t>(x1 - x0 + 1), 0);

    for (int py = y0; py <= y1; ++py) {
        std::fill(rowCov.begin(), rowCov.end(), uint8_t(0));
        for (int subY = 0; subY < 4; ++subY) {
            float sy = static_cast<float>(py) + (static_cast<float>(subY) + 0.5f) * 0.25f;
            hits.clear();
            for (const auto& e : scrEdges) {
                if ((e.y0 <= sy && e.y1 > sy) || (e.y1 <= sy && e.y0 > sy)) {
                    int dir = (e.y1 > e.y0) ? 1 : -1;
                    float xHit = e.x0 + (sy - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0);
                    hits.push_back({ xHit, dir });
                }
            }
            if (hits.size() < 2) continue;
            std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.x < b.x; });
            int winding = 0;
            for (size_t hi = 0; hi + 1 < hits.size(); ++hi) {
                winding += hits[hi].dir;
                if (winding != 0) {
                    int pxS = std::max(x0, static_cast<int>(std::floor(hits[hi].x)));
                    int pxE = std::min(x1, static_cast<int>(std::ceil(hits[hi + 1].x)));
                    for (int px = pxS; px <= pxE; ++px) {
                        for (int subX = 0; subX < 4; ++subX) {
                            float sx = static_cast<float>(px) + (static_cast<float>(subX) + 0.5f) * 0.25f;
                            if (sx >= hits[hi].x && sx < hits[hi + 1].x) ++rowCov[static_cast<size_t>(px - x0)];
                        }
                    }
                }
            }
        }
        for (int px = x0; px <= x1; ++px) {
            uint8_t c = rowCov[static_cast<size_t>(px - x0)];
            if (c == 0) continue;
            float covA = (static_cast<float>(c) / 16.0f) * (static_cast<float>(inkA) / 255.0f);
            uint8_t* dst = &bgra[(static_cast<size_t>(py) * imgW + static_cast<size_t>(px)) * 4];
            dst[0] = static_cast<uint8_t>(std::clamp(dst[0] * (1.0f - covA) + inkB * covA, 0.0f, 255.0f));
            dst[1] = static_cast<uint8_t>(std::clamp(dst[1] * (1.0f - covA) + inkG * covA, 0.0f, 255.0f));
            dst[2] = static_cast<uint8_t>(std::clamp(dst[2] * (1.0f - covA) + inkR * covA, 0.0f, 255.0f));
            dst[3] = 255;
        }
    }
}

inline bool RenderCffOrColorFontSpecimen(
    const uint8_t* sfntPtr,
    size_t sfntLen,
    const DetailFont::ParsedSfnt& sfnt,
    ExtendedImageInfo& outInfo)
{
    const uint32_t W = outInfo.width, H = outInfo.height;

    // Locate optional CFF / CFF2 / COLR / CPAL / CBDT / CBLC / sbix tables from SFNT directory
    size_t hdrOff = 0;
    if (sfntLen >= 16 && std::memcmp(sfntPtr, "ttcf", 4) == 0) hdrOff = DetailImg100::RdU32BE(sfntPtr + 12);
    if (hdrOff + 12 > sfntLen) return false;
    uint16_t numTables = DetailImg100::RdU16BE(sfntPtr + hdrOff + 4);

    DetailFont::SfntTableSlice tblCff1{}, tblCff2{}, tblColr{}, tblCpal{}, tblCbdt{}, tblSbix{};
    for (uint16_t i = 0; i < numTables && hdrOff + 12u + static_cast<size_t>(i + 1) * 16u <= sfntLen; ++i) {
        const uint8_t* rec = sfntPtr + hdrOff + 12 + static_cast<size_t>(i) * 16;
        uint32_t tag = DetailImg100::RdU32BE(rec + 0);
        uint32_t off = DetailImg100::RdU32BE(rec + 8);
        uint32_t len = DetailImg100::RdU32BE(rec + 12);
        if (off > sfntLen || len > sfntLen - off) continue;
        DetailFont::SfntTableSlice sl{ off, len, len > 0 };
        if (tag == DetailFont::MakeSfntTag("CFF ")) tblCff1 = sl;
        else if (tag == DetailFont::MakeSfntTag("CFF2")) tblCff2 = sl;
        else if (tag == DetailFont::MakeSfntTag("COLR")) tblColr = sl;
        else if (tag == DetailFont::MakeSfntTag("CPAL")) tblCpal = sl;
        else if (tag == DetailFont::MakeSfntTag("CBDT")) tblCbdt = sl;
        else if (tag == DetailFont::MakeSfntTag("sbix")) tblSbix = sl;
    }

    ParsedCffFont cff;
    bool hasCff = false;
    if (tblCff2.valid) hasCff = ParseCffTable(sfntPtr + tblCff2.offset, tblCff2.length, true, cff);
    if (!hasCff && tblCff1.valid) hasCff = ParseCffTable(sfntPtr + tblCff1.offset, tblCff1.length, false, cff);

    auto getOutlineEdges = [&](uint16_t gid, std::vector<DetailFont::EdgeSeg>& edges, float& xmn, float& xmx) -> bool {
        edges.clear();
        if (sfnt.tblGlyf.valid && sfnt.tblLoca.valid) {
            int16_t iMin = 0, iMax = 0;
            if (DetailFont::ExtractGlyphEdges(sfnt, gid, edges, iMin, iMax, 0) && !edges.empty()) {
                xmn = static_cast<float>(iMin);
                xmx = static_cast<float>(iMax);
                return true;
            }
        }
        if (hasCff && gid < cff.charStrings.count) {
            const uint8_t* csPtr = nullptr; size_t csLen = 0;
            if (cff.charStrings.GetItem(gid, csPtr, csLen) && csLen > 0) {
                CffBuildState st;
                bool ended = false;
                if (ExecuteCffCharString(cff, csPtr, csLen, st, 0, ended) && !st.edges.empty()) {
                    xmn = (st.minX < st.maxX) ? st.minX : 0.0f;
                    xmx = (st.minX < st.maxX) ? st.maxX : static_cast<float>(sfnt.unitsPerEm / 2);
                    edges = std::move(st.edges);
                    return true;
                }
            }
        }
        return false;
    };

    // Draw header banner text
    std::string asciiHeader;
    for (wchar_t wc : sfnt.familyName) if (wc >= 32 && wc < 127) asciiHeader.push_back(static_cast<char>(wc));
    if (asciiHeader.empty()) asciiHeader = tblCff2.valid ? "OPENTYPE CFF2 SPECIMEN" : "OPENTYPE FONT SPECIMEN";
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, W, H, 14, 13, asciiHeader, 2, 240, 246, 255);

    size_t drawnItems = 0;

    // 1. COLR v0 + CPAL multi-layer color vector glyphs!
    if (tblColr.valid && tblColr.length >= 14 && tblCpal.valid && tblCpal.length >= 14) {
        const uint8_t* colr = sfntPtr + tblColr.offset;
        const uint8_t* cpal = sfntPtr + tblCpal.offset;
        uint16_t numBase   = DetailImg100::RdU16BE(colr + 2);
        uint32_t baseOff   = DetailImg100::RdU32BE(colr + 4);
        uint32_t layerOff  = DetailImg100::RdU32BE(colr + 8);
        uint16_t numLayers = DetailImg100::RdU16BE(colr + 12);

        uint16_t numPalEntries = DetailImg100::RdU16BE(cpal + 2);
        uint16_t numPalettes   = DetailImg100::RdU16BE(cpal + 4);
        uint16_t numColorRecs  = DetailImg100::RdU16BE(cpal + 6);
        uint32_t colorRecOff   = DetailImg100::RdU32BE(cpal + 8);

        if (numBase > 0 && numPalettes > 0 && baseOff + static_cast<size_t>(numBase) * 6u <= tblColr.length &&
            layerOff + static_cast<size_t>(numLayers) * 4u <= tblColr.length &&
            colorRecOff + static_cast<size_t>(numColorRecs) * 4u <= tblCpal.length)
        {
            uint16_t firstColorIdx = DetailImg100::RdU16BE(cpal + 12);
            float curX = 24.0f, curY = 140.0f;
            for (uint16_t b = 0; b < numBase && curY < static_cast<float>(H) - 24.0f; ++b) {
                const uint8_t* br = colr + baseOff + static_cast<size_t>(b) * 6u;
                uint16_t baseGid   = DetailImg100::RdU16BE(br + 0);
                uint16_t firstLay  = DetailImg100::RdU16BE(br + 2);
                uint16_t layCount  = DetailImg100::RdU16BE(br + 4);
                float advPx = 48.0f;
                bool anyLay = false;
                for (uint16_t l = 0; l < layCount && firstLay + l < numLayers; ++l) {
                    const uint8_t* lr = colr + layerOff + static_cast<size_t>(firstLay + l) * 4u;
                    uint16_t layGid = DetailImg100::RdU16BE(lr + 0);
                    uint16_t palIdx = DetailImg100::RdU16BE(lr + 2);
                    uint8_t cr = 24, cg = 32, cb = 44, ca = 255;
                    if (palIdx != 0xFFFFu && palIdx < numPalEntries &&
                        static_cast<size_t>(firstColorIdx) + palIdx < numColorRecs)
                    {
                        const uint8_t* cp = cpal + colorRecOff + (static_cast<size_t>(firstColorIdx) + palIdx) * 4u;
                        cb = cp[0]; cg = cp[1]; cr = cp[2]; ca = cp[3]; // CPAL stores BGRA!
                    }
                    std::vector<DetailFont::EdgeSeg> edges;
                    float xmn = 0.0f, xmx = 0.0f;
                    if (getOutlineEdges(layGid, edges, xmn, xmx)) {
                        RasterizeEdgeListToCanvas(sfnt, baseGid, edges, xmn, xmx, curX, curY, 72.0f,
                                                  cr, cg, cb, ca, outInfo.bgraPixels, W, H, advPx);
                        anyLay = true;
                    }
                }
                if (anyLay) {
                    ++drawnItems;
                    curX += std::max(56.0f, advPx + 14.0f);
                    if (curX > static_cast<float>(W) - 72.0f) { curX = 24.0f; curY += 92.0f; }
                }
            }
            if (drawnItems > 0) {
                outInfo.formatName += L" [COLR/CPAL Color]";
                return true;
            }
        }
    }

    // 2. CBDT / sbix embedded PNG color bitmap glyphs!
    auto scanAndBlitEmbeddedGlyphPngs = [&](const uint8_t* buf, size_t len, const wchar_t* tagLabel) -> bool {
        static const uint8_t kPngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        static const uint8_t kIend[8]   = { 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82 };
        uint32_t curX = 24, curY = 64, rowH = 0;
        size_t count = 0;
        for (size_t i = 0; i + 24 < len && count < 32; ++i) {
            if (std::memcmp(buf + i, kPngSig, 8) == 0) {
                size_t pngLen = len - i;
                for (size_t j = i + 16; j + 8 <= len; ++j) {
                    if (std::memcmp(buf + j, kIend, 8) == 0) { pngLen = (j + 8) - i; break; }
                }
                ExtendedImageInfo gImg;
                if (DetailImg100::DecodeEmbeddedWicBuffer(buf + i, pngLen, L"GlyphPNG", gImg) && gImg.valid) {
                    if (curX + gImg.width + 16 > W) {
                        curX = 24;
                        curY += rowH + 16;
                        rowH = 0;
                    }
                    if (curY + gImg.height > H - 8) break;
                    for (uint32_t py = 0; py < gImg.height; ++py) {
                        for (uint32_t px = 0; px < gImg.width; ++px) {
                            const uint8_t* sp = &gImg.bgraPixels[(static_cast<size_t>(py) * gImg.width + px) * 4u];
                            BlendPremulBgraPixel(
                                &outInfo.bgraPixels[(static_cast<size_t>(curY + py) * W + (curX + px)) * 4u],
                                sp[0], sp[1], sp[2], sp[3], 1.0f, LayerBlendOp::SrcOver);
                        }
                    }
                    curX += gImg.width + 16;
                    rowH = std::max(rowH, gImg.height);
                    ++count;
                    i += pngLen - 1;
                }
            }
        }
        if (count > 0) {
            outInfo.formatName += tagLabel;
            return true;
        }
        return false;
    };

    if (tblCbdt.valid && scanAndBlitEmbeddedGlyphPngs(sfntPtr + tblCbdt.offset, tblCbdt.length, L" [CBDT Color Bitmap]")) {
        return true;
    }
    if (tblSbix.valid && scanAndBlitEmbeddedGlyphPngs(sfntPtr + tblSbix.offset, tblSbix.length, L" [sbix Color Bitmap]")) {
        return true;
    }

    // 3. CFF / CFF2 cubic Bezier specimen lines!
    if (hasCff) {
        auto drawLineCff = [&](std::wstring_view txt, float x, float baseY, float pxH, uint8_t r, uint8_t g, uint8_t b) {
            float curX = x;
            for (wchar_t wc : txt) {
                if (wc == L' ') { curX += pxH * 0.30f; continue; }
                uint16_t gid = DetailFont::LookupCmapGlyph(sfnt, static_cast<uint32_t>(wc));
                if (gid == 0 && wc >= L'a' && wc <= L'z') {
                    gid = DetailFont::LookupCmapGlyph(sfnt, static_cast<uint32_t>(wc - L'a' + L'A'));
                }
                if (gid == 0) { curX += pxH * 0.30f; continue; }
                std::vector<DetailFont::EdgeSeg> edges;
                float xmn = 0.0f, xmx = 0.0f, adv = 0.0f;
                if (getOutlineEdges(gid, edges, xmn, xmx)) {
                    RasterizeEdgeListToCanvas(sfnt, gid, edges, xmn, xmx, curX, baseY, pxH,
                                              r, g, b, 255, outInfo.bgraPixels, W, H, adv);
                    ++drawnItems;
                }
                curX += std::max(pxH * 0.22f, adv);
                if (curX >= static_cast<float>(W) - 14.0f) break;
            }
        };

        drawLineCff(L"ABCDEFGHIJKLM", 18.0f, 92.0f,  36.0f, 20, 26, 36);
        drawLineCff(L"NOPQRSTUVWXYZ", 18.0f, 138.0f, 36.0f, 20, 26, 36);
        drawLineCff(L"abcdefghijklm", 18.0f, 182.0f, 32.0f, 32, 42, 58);
        drawLineCff(L"0123456789 !?", 18.0f, 228.0f, 30.0f, 45, 78, 138);
        drawLineCff(L"OpenType CFF Specimen 64", 18.0f, 310.0f, 48.0f, 22, 48, 98);

        if (drawnItems == 0) {
            float curX = 20.0f, curY = 120.0f;
            uint32_t lim = std::min<uint32_t>(cff.charStrings.count, 64u);
            for (uint32_t gid = 0; gid < lim && curY < static_cast<float>(H) - 24.0f; ++gid) {
                std::vector<DetailFont::EdgeSeg> edges;
                float xmn = 0.0f, xmx = 0.0f, adv = 0.0f;
                if (getOutlineEdges(static_cast<uint16_t>(gid), edges, xmn, xmx)) {
                    RasterizeEdgeListToCanvas(sfnt, static_cast<uint16_t>(gid), edges, xmn, xmx, curX, curY, 56.0f,
                                              22, 30, 44, 255, outInfo.bgraPixels, W, H, adv);
                    ++drawnItems;
                    curX += std::max(42.0f, adv + 12.0f);
                    if (curX > static_cast<float>(W) - 64.0f) { curX = 20.0f; curY += 76.0f; }
                }
            }
        }
        if (drawnItems > 0) {
            if (cff.isCff2 && outInfo.formatName.find(L"CFF2") == std::wstring::npos) {
                outInfo.formatName += L" [CFF2]";
            }
            return true;
        }
    }

    return false;
}


} // namespace DetailArtFont

inline bool DecodeFontSpecimen(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size < 28) return false;
    (void)extLower;

    // ------------------------------------------------------------------------
    // WOFF 2.0 ('wOF2') & WOFF 1.0 ('wOFF') -> Unpack SFNT buffer
    // TTF / OTF / TTC                       -> Direct SFNT buffer
    // ------------------------------------------------------------------------
    std::vector<uint8_t> reconstructedSfnt;
    const uint8_t* sfntPtr = data;
    size_t sfntLen = size;
    bool fromWoff = false;
    bool fromWoff2 = false;

    if (size >= 48 && std::memcmp(data, "wOF2", 4) == 0) {
        if (!DetailFont::ReconstructSfntFromWoff2(data, size, reconstructedSfnt)) {
            return false;
        }
        sfntPtr = reconstructedSfnt.data();
        sfntLen = reconstructedSfnt.size();
        fromWoff2 = true;
    } else if (size >= 44 && std::memcmp(data, "wOFF", 4) == 0) {
        if (!DetailFont::ReconstructSfntFromWoff(data, size, reconstructedSfnt)) {
            return false;
        }
        sfntPtr = reconstructedSfnt.data();
        sfntLen = reconstructedSfnt.size();
        fromWoff = true;
    }

    DetailFont::ParsedSfnt sfnt;
    if (!DetailFont::ParseSfntDirectory(sfntPtr, sfntLen, sfnt)) {
        return false;
    }

    const uint32_t W = 640, H = 400;
    outInfo.width = W;
    outInfo.height = H;
    outInfo.origWidth = W;
    outInfo.origHeight = H;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;

    std::wstring dispFamily = sfnt.familyName.empty() ? L"Unnamed Font" : sfnt.familyName;
    std::wstring dispSub    = sfnt.subfamilyName.empty() ? L"Regular" : sfnt.subfamilyName;
    std::wstring kindTag    = fromWoff2 ? L"WOFF2 WebFont" :
                              (fromWoff ? L"WOFF WebFont" :
                              (sfnt.flavor == DetailFont::MakeSfntTag("OTTO") ? L"OpenType CFF" : L"TrueType"));
    outInfo.formatName = dispFamily + L" " + dispSub + L" — " + kindTag +
                         (sfnt.numGlyphs > 0 ? (L" (" + std::to_wstring(sfnt.numGlyphs) + L" glyphs)") : L"");

    // Initialize warm parchment-white specimen background + top dark header bar
    outInfo.bgraPixels.resize(static_cast<size_t>(W) * H * 4);
    for (uint32_t y = 0; y < H; ++y) {
        bool inBanner = (y < 42);
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t r = inBanner ? 26 : 250;
            uint8_t g = inBanner ? 34 : 250;
            uint8_t b = inBanner ? 48 : 247;
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * W + x) * 4], r, g, b, 255);
        }
    }

    // Prefer native color / CFF2 renderer first when font carries COLR/CPAL, CBDT, sbix, or CFF2 tables
    // (Win32 GDI DrawTextW is monochrome and does not render COLR/CBDT/sbix colors or CFF2 variable outlines!)
    {
        size_t dirOff = (sfntLen >= 16 && std::memcmp(sfntPtr, "ttcf", 4) == 0) ? DetailImg100::RdU32BE(sfntPtr + 12) : 0u;
        bool hasColorOrCff2 = false;
        if (dirOff + 12u <= sfntLen) {
            uint16_t nt = DetailImg100::RdU16BE(sfntPtr + dirOff + 4);
            for (uint16_t i = 0; i < nt && dirOff + 12u + static_cast<size_t>(i + 1) * 16u <= sfntLen; ++i) {
                uint32_t t = DetailImg100::RdU32BE(sfntPtr + dirOff + 12u + static_cast<size_t>(i) * 16u);
                if (t == DetailFont::MakeSfntTag("COLR") || t == DetailFont::MakeSfntTag("CBDT") ||
                    t == DetailFont::MakeSfntTag("sbix") || t == DetailFont::MakeSfntTag("CFF2")) {
                    hasColorOrCff2 = true;
                    break;
                }
            }
        }
        if (hasColorOrCff2 && DetailArtFont::RenderCffOrColorFontSpecimen(sfntPtr, sfntLen, sfnt, outInfo)) {
            outInfo.valid = true;
            return true;
        }
    }

    // Attempt Win32 GDI rendering via AddFontMemResourceEx + DIBSection
    bool gdiRenderedRealFont = false;
#ifdef _WIN32
    if (!sfnt.familyName.empty()) {
        DWORD numAdded = 0;
        HANDLE hFontRes = AddFontMemResourceEx(
            const_cast<uint8_t*>(sfntPtr),
            static_cast<DWORD>(sfntLen),
            nullptr,
            &numAdded);

        if (hFontRes && numAdded > 0) {
            HDC hdc = CreateCompatibleDC(nullptr);
            if (hdc) {
                BITMAPINFO bmi = {};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = static_cast<LONG>(W);
                bmi.bmiHeader.biHeight = -static_cast<LONG>(H); // top-down DIB
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;

                void* dibBits = nullptr;
                HBITMAP hDib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
                if (hDib && dibBits) {
                    std::memcpy(dibBits, outInfo.bgraPixels.data(), outInfo.bgraPixels.size());
                    HGDIOBJ oldBmp = SelectObject(hdc, hDib);
                    SetBkMode(hdc, TRANSPARENT);

                    bool isItalic = (dispSub.find(L"Italic") != std::wstring::npos ||
                                     dispSub.find(L"Oblique") != std::wstring::npos);
                    int weight = (dispSub.find(L"Bold") != std::wstring::npos) ? FW_BOLD : FW_NORMAL;

                    auto makeFont = [&](int pxHeight) -> HFONT {
                        return CreateFontW(
                            -pxHeight, 0, 0, 0, weight, isItalic ? TRUE : FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                            sfnt.familyName.c_str());
                    };

                    HFONT hTestFont = makeFont(28);
                    if (hTestFont) {
                        HGDIOBJ oldFnt = SelectObject(hdc, hTestFont);
                        wchar_t actualFace[128] = {};
                        GetTextFaceW(hdc, 128, actualFace);
                        // Confirm GDI loaded our font face rather than substituting system Arial/MS Sans Serif
                        if (_wcsicmp(actualFace, sfnt.familyName.c_str()) == 0) {
                            gdiRenderedRealFont = true;
                        }
                        SelectObject(hdc, oldFnt);
                        DeleteObject(hTestFont);
                    }

                    if (gdiRenderedRealFont) {
                        auto drawLineGdi = [&](int x, int y, int pxSize, COLORREF col, const std::wstring& txt) {
                            HFONT hf = makeFont(pxSize);
                            if (!hf) return;
                            HGDIOBJ prev = SelectObject(hdc, hf);
                            SetTextColor(hdc, col);
                            RECT rc = { x, y, static_cast<LONG>(W) - 12, static_cast<LONG>(H) - 4 };
                            DrawTextW(hdc, txt.c_str(), static_cast<int>(txt.size()), &rc, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE);
                            SelectObject(hdc, prev);
                            DeleteObject(hf);
                        };

                        std::wstring headerTitle = dispFamily + L"  (" + dispSub + L")";
                        drawLineGdi(14, 8,  22, RGB(245, 248, 255), headerTitle);
                        drawLineGdi(16, 50, 30, RGB(20, 26, 36),    L"ABCDEFGHIJKLM   NOPQRSTUVWXYZ");
                        drawLineGdi(16, 88, 28, RGB(20, 26, 36),    L"abcdefghijklm   nopqrstuvwxyz");
                        drawLineGdi(16, 124, 24, RGB(45, 78, 138),  L"0123456789   !@#$%^&*()_+-=[]{};':\",./<>?");
                        drawLineGdi(16, 162, 16, RGB(55, 62, 74),   L"12pt: The quick brown fox jumps over the lazy dog. 0123456789");
                        drawLineGdi(16, 186, 22, RGB(35, 42, 54),   L"18pt: The quick brown fox jumps over the lazy dog.");
                        drawLineGdi(16, 218, 30, RGB(22, 28, 38),   L"26pt: Pack my box with five dozen liquor jugs.");
                        drawLineGdi(16, 260, 42, RGB(15, 20, 30),   L"36pt: Sphinx of black quartz!");
                        drawLineGdi(16, 318, 56, RGB(28, 56, 108),  L"Aa Bb Cc Gg Qq Rr & 0123");

                        GdiFlush();
                        std::memcpy(outInfo.bgraPixels.data(), dibBits, outInfo.bgraPixels.size());
                        // Restore opaque alpha = 255 (Win32 GDI clears alpha channel on touched text pixels)
                        for (size_t i = 0; i < static_cast<size_t>(W) * H; ++i) {
                            outInfo.bgraPixels[i * 4 + 3] = 255;
                        }
                    }

                    SelectObject(hdc, oldBmp);
                    DeleteObject(hDib);
                }
                DeleteDC(hdc);
            }
            RemoveFontMemResourceEx(hFontRes);
        }
    }
#endif

    // Pure-C++ TrueType 'glyf' / OpenType 'CFF ' / 'CFF2' / Color font rasterizer fallback
    if (!gdiRenderedRealFont) {
        if (!sfnt.tblGlyf.valid || !sfnt.tblLoca.valid || sfnt.numGlyphs == 0) {
            if (DetailArtFont::RenderCffOrColorFontSpecimen(sfntPtr, sfntLen, sfnt, outInfo)) {
                outInfo.valid = true;
                return true;
            }
            return false; // Neither GDI accepted the font nor TrueType/CFF/Color glyphs exist
        }

        std::string asciiHeader;
        for (wchar_t wc : dispFamily) {
            if (wc >= 32 && wc < 127) asciiHeader.push_back(static_cast<char>(wc));
        }
        if (asciiHeader.empty()) asciiHeader = "TRUETYPE SPECIMEN";
        DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, W, H, 14, 13, asciiHeader, 2, 240, 246, 255);

        size_t totalDrawn = 0;
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"ABCDEFGHIJKLM",          18.0f, 82.0f,  34.0f, 20, 26, 36,  outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"NOPQRSTUVWXYZ",          18.0f, 122.0f, 34.0f, 20, 26, 36,  outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"abcdefghijklm",          18.0f, 160.0f, 30.0f, 28, 36, 48,  outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"nopqrstuvwxyz",          18.0f, 196.0f, 30.0f, 28, 36, 48,  outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"0123456789 !@#$%^&*()",  18.0f, 232.0f, 28.0f, 45, 78, 138, outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"The quick brown fox jumps over the lazy dog.", 18.0f, 266.0f, 22.0f, 55, 62, 74, outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"Aa Bb Cc 123",           18.0f, 322.0f, 46.0f, 24, 52, 102, outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"WATERFALL 64",           290.0f, 380.0f, 60.0f, 18, 24, 34, outInfo.bgraPixels, W, H);

        // If cmap didn't map standard ASCII (e.g. symbol/icon font or minimal glyph table), rasterize available glyph IDs directly!
        if (totalDrawn == 0) {
            float curX = 18.0f;
            float curY = 110.0f;
            for (uint16_t gid = 0; gid < sfnt.numGlyphs && curY < static_cast<float>(H) - 20.0f; ++gid) {
                std::vector<DetailFont::EdgeSeg> testEdges;
                int16_t xmn = 0, xmx = 0;
                if (!DetailFont::ExtractGlyphEdges(sfnt, gid, testEdges, xmn, xmx, 0) || testEdges.empty()) continue;
                float adv = 0.0f;
                if (DetailFont::RasterizeGlyphContours(sfnt, gid, curX, curY, 54.0f, 22, 30, 44, outInfo.bgraPixels, W, H, adv)) {
                    ++totalDrawn;
                    curX += std::max(36.0f, adv + 10.0f);
                    if (curX > static_cast<float>(W) - 60.0f) {
                        curX = 18.0f;
                        curY += 72.0f;
                    }
                }
            }
        }

        if (totalDrawn == 0) {
            return false; // Zero renderable glyphs in font!
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
