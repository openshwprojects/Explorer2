#pragma once
// FontOutlineCommon.h — Shared SFNT / TTC / glyf / CFF / CFF2 / COLR-CPAL / CBLC-CBDT / sbix Specimen Engine
// Used by single-format readers TrueTypeFontTtf.h, OpenTypeFontOtf.h, WoffFont.h, and Woff2Font.h.

namespace Formats::FontOutline {

struct SfntTableSlice {
    uint32_t offset = 0;
    uint32_t length = 0;
    bool valid = false;
};

struct ParsedSfnt {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t dirOffset = 0;
    bool isCollection = false;
    uint32_t collectionFontCount = 1;
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
    SfntTableSlice tblCff1;
    SfntTableSlice tblCff2;
    SfntTableSlice tblColr;
    SfntTableSlice tblCpal;
    SfntTableSlice tblCblc;
    SfntTableSlice tblCbdt;
    SfntTableSlice tblSbix;
};

inline uint32_t MakeSfntTag(const char t[4]) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(t[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(t[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(t[2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(t[3]));
}

inline uint32_t CalcSfntChecksum(const uint8_t* p, size_t len) {
    uint32_t sum = 0;
    size_t nLongs = len / 4u;
    for (size_t i = 0; i < nLongs; ++i) {
        sum += DetailLayeredAndHdrImg::RdU32BE(p + i * 4u);
    }
    size_t rem = len & 3u;
    if (rem > 0) {
        uint8_t tail[4] = { 0, 0, 0, 0 };
        for (size_t i = 0; i < rem; ++i) tail[i] = p[nLongs * 4u + i];
        sum += DetailLayeredAndHdrImg::RdU32BE(tail);
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

inline bool ParseSfntAtOffset(const uint8_t* data, size_t size, size_t hdrOff, ParsedSfnt& out) {
    if (!data || hdrOff + 28u > size) return false;

    uint32_t sfntVer = DetailLayeredAndHdrImg::RdU32BE(data + hdrOff);
    bool validFlavor = (sfntVer == 0x00010000u) ||
                       (sfntVer == MakeSfntTag("OTTO")) ||
                       (sfntVer == MakeSfntTag("true")) ||
                       (sfntVer == MakeSfntTag("typ1"));
    if (!validFlavor) return false;

    uint16_t numTables = DetailLayeredAndHdrImg::RdU16BE(data + hdrOff + 4u);
    if (numTables == 0 || numTables > 256u) return false;
    if (hdrOff + 12ull + static_cast<size_t>(numTables) * 16ull > size) return false;

    out.data = data;
    out.size = size;
    out.dirOffset = hdrOff;
    out.flavor = sfntVer;
    out.numTables = numTables;

    int validCoreTables = 0;
    for (uint16_t i = 0; i < numTables; ++i) {
        const uint8_t* rec = data + hdrOff + 12u + static_cast<size_t>(i) * 16u;
        uint32_t tag = DetailLayeredAndHdrImg::RdU32BE(rec + 0u);
        uint32_t off = DetailLayeredAndHdrImg::RdU32BE(rec + 8u);
        uint32_t len = DetailLayeredAndHdrImg::RdU32BE(rec + 12u);
        if (off > size || len > size - off) return false;

        SfntTableSlice sl{ off, len, len > 0 };
        if (tag == MakeSfntTag("name"))      { out.tblName = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("head")) { out.tblHead = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("maxp")) { out.tblMaxp = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("hhea")) { out.tblHhea = sl; }
        else if (tag == MakeSfntTag("hmtx")) { out.tblHmtx = sl; }
        else if (tag == MakeSfntTag("cmap")) { out.tblCmap = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("loca")) { out.tblLoca = sl; }
        else if (tag == MakeSfntTag("glyf")) { out.tblGlyf = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("CFF ")) { out.tblCff1 = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("CFF2")) { out.tblCff2 = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("COLR")) { out.tblColr = sl; }
        else if (tag == MakeSfntTag("CPAL")) { out.tblCpal = sl; }
        else if (tag == MakeSfntTag("CBLC") || tag == MakeSfntTag("EBLC") || tag == MakeSfntTag("bloc")) { out.tblCblc = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("CBDT") || tag == MakeSfntTag("EBDT") || tag == MakeSfntTag("bdat")) { out.tblCbdt = sl; }
        else if (tag == MakeSfntTag("sbix")) { out.tblSbix = sl; ++validCoreTables; }
    }
    if (validCoreTables == 0) return false;

    if (out.tblHead.valid && out.tblHead.length >= 54u) {
        const uint8_t* h = data + out.tblHead.offset;
        uint16_t upem = DetailLayeredAndHdrImg::RdU16BE(h + 18u);
        if (upem >= 16u && upem <= 16384u) out.unitsPerEm = upem;
        out.indexToLocFormat = DetailLayeredAndHdrImg::RdI16BE(h + 50u);
    }
    if (out.tblMaxp.valid && out.tblMaxp.length >= 6u) {
        out.numGlyphs = DetailLayeredAndHdrImg::RdU16BE(data + out.tblMaxp.offset + 4u);
    }
    if (out.tblHhea.valid && out.tblHhea.length >= 36u) {
        const uint8_t* hh = data + out.tblHhea.offset;
        int16_t asc = DetailLayeredAndHdrImg::RdI16BE(hh + 4u);
        int16_t dsc = DetailLayeredAndHdrImg::RdI16BE(hh + 6u);
        if (asc > 0) out.ascent = asc;
        if (dsc < 0) out.descent = dsc;
        out.numHMetrics = DetailLayeredAndHdrImg::RdU16BE(hh + 34u);
    } else {
        out.ascent = static_cast<int16_t>((out.unitsPerEm * 4) / 5);
        out.descent = -static_cast<int16_t>(out.unitsPerEm / 5);
    }

    if (out.tblName.valid && out.tblName.length >= 6u) {
        const uint8_t* nm = data + out.tblName.offset;
        size_t nmLen = out.tblName.length;
        uint16_t count = DetailLayeredAndHdrImg::RdU16BE(nm + 2u);
        uint16_t strStorageOff = DetailLayeredAndHdrImg::RdU16BE(nm + 4u);
        int bestFamPrio = 0, bestSubPrio = 0;

        for (uint16_t i = 0; i < count && 6u + static_cast<size_t>(i + 1) * 12u <= nmLen; ++i) {
            const uint8_t* nr = nm + 6u + static_cast<size_t>(i) * 12u;
            uint16_t platId = DetailLayeredAndHdrImg::RdU16BE(nr + 0u);
            uint16_t langId = DetailLayeredAndHdrImg::RdU16BE(nr + 4u);
            uint16_t nameId = DetailLayeredAndHdrImg::RdU16BE(nr + 6u);
            uint16_t sLen   = DetailLayeredAndHdrImg::RdU16BE(nr + 8u);
            uint16_t sOff   = DetailLayeredAndHdrImg::RdU16BE(nr + 10u);
            if (nameId != 1 && nameId != 2 && nameId != 16 && nameId != 17) continue;
            size_t absOff = static_cast<size_t>(strStorageOff) + sOff;
            if (sLen == 0 || absOff > nmLen || sLen > nmLen - absOff) continue;

            std::wstring decoded;
            int prio = 0;
            if (platId == 3 || platId == 0) {
                if ((sLen & 1u) != 0) continue;
                for (uint16_t k = 0; k < sLen; k += 2u) {
                    wchar_t wc = static_cast<wchar_t>(DetailLayeredAndHdrImg::RdU16BE(nm + absOff + k));
                    if (wc != 0) decoded.push_back(wc);
                }
                prio = (platId == 3 && (langId == 0x0409 || langId == 0)) ? 4 : 3;
            } else if (platId == 1) {
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

inline bool ParseSfntDirectory(const uint8_t* data, size_t size, ParsedSfnt& out, std::vector<std::wstring>* outCollectionFaces = nullptr) {
    out = {};
    if (!data || size < 28u) return false;

    if (std::memcmp(data, "ttcf", 4) == 0) {
        if (size < 16u) return false;
        uint16_t ttcMaj = DetailLayeredAndHdrImg::RdU16BE(data + 4u);
        if (ttcMaj != 1u && ttcMaj != 2u) return false;
        uint32_t numFonts = DetailLayeredAndHdrImg::RdU32BE(data + 8u);
        if (numFonts == 0 || numFonts > 1024u || 12ull + static_cast<uint64_t>(numFonts) * 4ull > size) {
            return false;
        }
        bool gotPrimary = false;
        for (uint32_t f = 0; f < numFonts; ++f) {
            uint32_t dirOff = DetailLayeredAndHdrImg::RdU32BE(data + 12u + static_cast<size_t>(f) * 4u);
            ParsedSfnt face;
            if (!ParseSfntAtOffset(data, size, dirOff, face)) {
                if (f == 0) return false;
                continue;
            }
            face.isCollection = true;
            face.collectionFontCount = numFonts;
            if (outCollectionFaces && !face.familyName.empty() && outCollectionFaces->size() < 64u) {
                std::wstring label = face.familyName;
                if (!face.subfamilyName.empty()) label += L" " + face.subfamilyName;
                outCollectionFaces->push_back(std::move(label));
            }
            if (!gotPrimary) {
                out = std::move(face);
                gotPrimary = true;
            }
        }
        return gotPrimary;
    }

    return ParseSfntAtOffset(data, size, 0u, out);
}

inline uint16_t LookupCmapGlyph(const ParsedSfnt& sfnt, uint32_t codepoint) {
    if (!sfnt.tblCmap.valid || sfnt.tblCmap.length < 4u) return 0;
    const uint8_t* cm = sfnt.data + sfnt.tblCmap.offset;
    size_t cmLen = sfnt.tblCmap.length;
    uint16_t numSub = DetailLayeredAndHdrImg::RdU16BE(cm + 2u);

    uint32_t subOff12 = 0, subOff4 = 0, subOff6 = 0, subOff0 = 0;
    for (uint16_t i = 0; i < numSub && 4u + static_cast<size_t>(i + 1) * 8u <= cmLen; ++i) {
        uint32_t off = DetailLayeredAndHdrImg::RdU32BE(cm + 4u + static_cast<size_t>(i) * 8u + 4u);
        if (off + 4u > cmLen) continue;
        uint16_t fmt = DetailLayeredAndHdrImg::RdU16BE(cm + off);
        if (fmt == 12 && subOff12 == 0) subOff12 = off;
        else if (fmt == 4 && subOff4 == 0) subOff4 = off;
        else if (fmt == 6 && subOff6 == 0) subOff6 = off;
        else if (fmt == 0 && subOff0 == 0) subOff0 = off;
    }

    if (subOff12 != 0 && subOff12 + 16u <= cmLen) {
        uint32_t nGroups = DetailLayeredAndHdrImg::RdU32BE(cm + subOff12 + 12u);
        size_t gBase = subOff12 + 16u;
        size_t maxGroups = std::min<size_t>(nGroups, (cmLen - gBase) / 12u);
        for (size_t g = 0; g < maxGroups; ++g) {
            const uint8_t* gp = cm + gBase + g * 12u;
            uint32_t startCp = DetailLayeredAndHdrImg::RdU32BE(gp + 0u);
            uint32_t endCp   = DetailLayeredAndHdrImg::RdU32BE(gp + 4u);
            uint32_t startGl = DetailLayeredAndHdrImg::RdU32BE(gp + 8u);
            if (codepoint < startCp) break;
            if (codepoint <= endCp) {
                return static_cast<uint16_t>((startGl + (codepoint - startCp)) & 0xFFFFu);
            }
        }
    }

    if (subOff4 != 0 && codepoint <= 0xFFFFu && subOff4 + 14u <= cmLen) {
        uint16_t subLen = DetailLayeredAndHdrImg::RdU16BE(cm + subOff4 + 2u);
        size_t lim = std::min<size_t>(cmLen, subOff4 + (subLen > 14u ? subLen : (cmLen - subOff4)));
        uint16_t segCountX2 = DetailLayeredAndHdrImg::RdU16BE(cm + subOff4 + 6u);
        uint16_t segCount = segCountX2 / 2u;
        size_t endCodeOff   = subOff4 + 14u;
        size_t startCodeOff = endCodeOff + static_cast<size_t>(segCountX2) + 2u;
        size_t idDeltaOff   = startCodeOff + static_cast<size_t>(segCountX2);
        size_t idRangeOff   = idDeltaOff + static_cast<size_t>(segCountX2);
        if (idRangeOff + segCountX2 <= lim) {
            uint16_t cp16 = static_cast<uint16_t>(codepoint);
            for (uint16_t s = 0; s < segCount; ++s) {
                uint16_t endCode = DetailLayeredAndHdrImg::RdU16BE(cm + endCodeOff + s * 2u);
                if (cp16 > endCode) continue;
                uint16_t startCode = DetailLayeredAndHdrImg::RdU16BE(cm + startCodeOff + s * 2u);
                if (cp16 < startCode) break;
                uint16_t idDelta = DetailLayeredAndHdrImg::RdU16BE(cm + idDeltaOff + s * 2u);
                size_t roPos = idRangeOff + s * 2u;
                uint16_t idRange = DetailLayeredAndHdrImg::RdU16BE(cm + roPos);
                if (idRange == 0) {
                    return static_cast<uint16_t>((cp16 + idDelta) & 0xFFFFu);
                }
                size_t glyphIdxPos = roPos + idRange + static_cast<size_t>(cp16 - startCode) * 2u;
                if (glyphIdxPos + 2u <= lim) {
                    uint16_t g = DetailLayeredAndHdrImg::RdU16BE(cm + glyphIdxPos);
                    if (g != 0) return static_cast<uint16_t>((g + idDelta) & 0xFFFFu);
                }
                return 0;
            }
        }
    }

    if (subOff6 != 0 && codepoint <= 0xFFFFu && subOff6 + 10u <= cmLen) {
        uint16_t firstCode = DetailLayeredAndHdrImg::RdU16BE(cm + subOff6 + 6u);
        uint16_t entryCount = DetailLayeredAndHdrImg::RdU16BE(cm + subOff6 + 8u);
        if (codepoint >= firstCode && codepoint < static_cast<uint32_t>(firstCode) + entryCount) {
            size_t idxPos = subOff6 + 10u + static_cast<size_t>(codepoint - firstCode) * 2u;
            if (idxPos + 2u <= cmLen) return DetailLayeredAndHdrImg::RdU16BE(cm + idxPos);
        }
    }

    if (subOff0 != 0 && codepoint < 256u && subOff0 + 6u + 256u <= cmLen) {
        return cm[subOff0 + 6u + codepoint];
    }
    return 0;
}

inline uint16_t GetGlyphAdvance(const ParsedSfnt& sfnt, uint16_t glyphId) {
    if (sfnt.tblHmtx.valid && sfnt.numHMetrics > 0) {
        const uint8_t* hm = sfnt.data + sfnt.tblHmtx.offset;
        size_t hmLen = sfnt.tblHmtx.length;
        uint16_t idx = (glyphId < sfnt.numHMetrics) ? glyphId : static_cast<uint16_t>(sfnt.numHMetrics - 1u);
        if (static_cast<size_t>(idx) * 4u + 2u <= hmLen) {
            uint16_t adv = DetailLayeredAndHdrImg::RdU16BE(hm + static_cast<size_t>(idx) * 4u);
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
        gOff = static_cast<uint32_t>(DetailLayeredAndHdrImg::RdU16BE(loca + static_cast<size_t>(glyphId) * 2u)) * 2u;
        gEnd = static_cast<uint32_t>(DetailLayeredAndHdrImg::RdU16BE(loca + static_cast<size_t>(glyphId + 1) * 2u)) * 2u;
    } else {
        if (static_cast<size_t>(glyphId + 1) * 4u + 4u > locaLen) return false;
        gOff = DetailLayeredAndHdrImg::RdU32BE(loca + static_cast<size_t>(glyphId) * 4u);
        gEnd = DetailLayeredAndHdrImg::RdU32BE(loca + static_cast<size_t>(glyphId + 1) * 4u);
    }
    if (gOff >= gEnd) return true;
    if (gEnd > sfnt.tblGlyf.length || gEnd - gOff < 10u) return false;

    const uint8_t* g = sfnt.data + sfnt.tblGlyf.offset + gOff;
    size_t gLen = gEnd - gOff;
    int16_t numContours = DetailLayeredAndHdrImg::RdI16BE(g + 0u);
    if (depth == 0) {
        outXMin = DetailLayeredAndHdrImg::RdI16BE(g + 2u);
        outXMax = DetailLayeredAndHdrImg::RdI16BE(g + 6u);
    }
    if (numContours == 0) return true;

    if (numContours < 0) {
        size_t cp = 10u;
        uint16_t compFlags = 0x0020u;
        while ((compFlags & 0x0020u) != 0u && cp + 4u <= gLen) {
            compFlags = DetailLayeredAndHdrImg::RdU16BE(g + cp);
            uint16_t childGlyph = DetailLayeredAndHdrImg::RdU16BE(g + cp + 2u);
            cp += 4u;

            float dx = 0.0f, dy = 0.0f;
            if ((compFlags & 0x0001u) != 0u) {
                if (cp + 4u > gLen) break;
                if ((compFlags & 0x0002u) != 0u) {
                    dx = static_cast<float>(DetailLayeredAndHdrImg::RdI16BE(g + cp));
                    dy = static_cast<float>(DetailLayeredAndHdrImg::RdI16BE(g + cp + 2u));
                }
                cp += 4u;
            } else {
                if (cp + 2u > gLen) break;
                if ((compFlags & 0x0002u) != 0u) {
                    dx = static_cast<float>(static_cast<int8_t>(g[cp]));
                    dy = static_cast<float>(static_cast<int8_t>(g[cp + 1u]));
                }
                cp += 2u;
            }

            float m00 = 1.0f, m01 = 0.0f, m10 = 0.0f, m11 = 1.0f;
            if ((compFlags & 0x0008u) != 0u) {
                if (cp + 2u > gLen) break;
                m00 = m11 = static_cast<float>(DetailLayeredAndHdrImg::RdI16BE(g + cp)) / 16384.0f;
                cp += 2u;
            } else if ((compFlags & 0x0040u) != 0u) {
                if (cp + 4u > gLen) break;
                m00 = static_cast<float>(DetailLayeredAndHdrImg::RdI16BE(g + cp)) / 16384.0f;
                m11 = static_cast<float>(DetailLayeredAndHdrImg::RdI16BE(g + cp + 2u)) / 16384.0f;
                cp += 4u;
            } else if ((compFlags & 0x0080u) != 0u) {
                if (cp + 8u > gLen) break;
                m00 = static_cast<float>(DetailLayeredAndHdrImg::RdI16BE(g + cp + 0u)) / 16384.0f;
                m01 = static_cast<float>(DetailLayeredAndHdrImg::RdI16BE(g + cp + 2u)) / 16384.0f;
                m10 = static_cast<float>(DetailLayeredAndHdrImg::RdI16BE(g + cp + 4u)) / 16384.0f;
                m11 = static_cast<float>(DetailLayeredAndHdrImg::RdI16BE(g + cp + 6u)) / 16384.0f;
                cp += 8u;
            }

            std::vector<EdgeSeg> childEdges;
            int16_t cxMin = 0, cxMax = 0;
            if (ExtractGlyphEdges(sfnt, childGlyph, childEdges, cxMin, cxMax, depth + 1)) {
                for (const auto& ce : childEdges) {
                    outEdges.push_back({
                        ce.x0 * m00 + ce.y0 * m10 + dx,
                        ce.x0 * m01 + ce.y0 * m11 + dy,
                        ce.x1 * m00 + ce.y1 * m10 + dx,
                        ce.x1 * m01 + ce.y1 * m11 + dy
                    });
                }
            }
        }
        return true;
    }

    size_t nc = static_cast<size_t>(numContours);
    if (10u + nc * 2u + 2u > gLen) return false;
    std::vector<uint16_t> endPts(nc);
    for (size_t c = 0; c < nc; ++c) {
        endPts[c] = DetailLayeredAndHdrImg::RdU16BE(g + 10u + c * 2u);
        if (c > 0 && endPts[c] <= endPts[c - 1]) return false;
    }
    size_t numPts = static_cast<size_t>(endPts.back()) + 1u;
    if (numPts > 8192u) return false;

    uint16_t instrLen = DetailLayeredAndHdrImg::RdU16BE(g + 10u + nc * 2u);
    size_t fp = 10u + nc * 2u + 2u + instrLen;
    if (fp > gLen) return false;

    std::vector<uint8_t> flags(numPts);
    for (size_t i = 0; i < numPts; ) {
        if (fp >= gLen) return false;
        uint8_t fl = g[fp++];
        flags[i++] = fl;
        if ((fl & 0x08u) != 0u) {
            if (fp >= gLen) return false;
            uint8_t rep = g[fp++];
            for (uint8_t r = 0; r < rep && i < numPts; ++r) flags[i++] = fl;
        }
    }

    std::vector<float> px(numPts), py(numPts);
    int32_t curX = 0;
    for (size_t i = 0; i < numPts; ++i) {
        uint8_t fl = flags[i];
        if ((fl & 0x02u) != 0u) {
            if (fp >= gLen) return false;
            int32_t d = g[fp++];
            curX += ((fl & 0x10u) != 0u) ? d : -d;
        } else if ((fl & 0x10u) == 0u) {
            if (fp + 2u > gLen) return false;
            curX += DetailLayeredAndHdrImg::RdI16BE(g + fp);
            fp += 2u;
        }
        px[i] = static_cast<float>(curX);
    }

    int32_t curY = 0;
    for (size_t i = 0; i < numPts; ++i) {
        uint8_t fl = flags[i];
        if ((fl & 0x04u) != 0u) {
            if (fp >= gLen) return false;
            int32_t d = g[fp++];
            curY += ((fl & 0x20u) != 0u) ? d : -d;
        } else if ((fl & 0x20u) == 0u) {
            if (fp + 2u > gLen) return false;
            curY += DetailLayeredAndHdrImg::RdI16BE(g + fp);
            fp += 2u;
        }
        py[i] = static_cast<float>(curY);
    }

    size_t startIdx = 0;
    for (size_t c = 0; c < nc; ++c) {
        size_t endIdx = endPts[c];
        size_t cnt = endIdx - startIdx + 1u;
        if (cnt >= 2u) {
            auto ptX = [&](size_t k) { return px[startIdx + (k % cnt)]; };
            auto ptY = [&](size_t k) { return py[startIdx + (k % cnt)]; };
            auto isOn = [&](size_t k) { return (flags[startIdx + (k % cnt)] & 0x01u) != 0u; };

            float sx = 0.0f, sy = 0.0f;
            size_t kStart = 0;
            if (isOn(0)) { sx = ptX(0); sy = ptY(0); kStart = 1; }
            else if (isOn(cnt - 1)) { sx = ptX(cnt - 1); sy = ptY(cnt - 1); kStart = 0; }
            else { sx = 0.5f * (ptX(cnt - 1) + ptX(0)); sy = 0.5f * (ptY(cnt - 1) + ptY(0)); kStart = 0; }

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

inline void RasterizeEdgeListToCanvas(
    const ParsedSfnt& sfnt,
    uint16_t glyphId,
    const std::vector<EdgeSeg>& rawEdges,
    float gxMin, float gxMax,
    float originX, float baselineY, float pixelHeight,
    uint8_t inkR, uint8_t inkG, uint8_t inkB, uint8_t inkA,
    std::vector<uint8_t>& bgra, uint32_t imgW, uint32_t imgH,
    float& outAdvPx)
{
    float scale = pixelHeight / static_cast<float>(std::max<uint16_t>(16, sfnt.unitsPerEm));
    outAdvPx = static_cast<float>(GetGlyphAdvance(sfnt, glyphId)) * scale;
    if (outAdvPx <= 1.0f && gxMax > gxMin) {
        outAdvPx = (gxMax - gxMin) * scale + pixelHeight * 0.12f;
    }
    if (rawEdges.empty() || inkA == 0) return;

    std::vector<EdgeSeg> scrEdges;
    scrEdges.reserve(rawEdges.size());
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& e : rawEdges) {
        EdgeSeg se{
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
            uint8_t* dst = &bgra[(static_cast<size_t>(py) * imgW + static_cast<size_t>(px)) * 4u];
            dst[0] = static_cast<uint8_t>(std::clamp(dst[0] * (1.0f - covA) + inkB * covA, 0.0f, 255.0f));
            dst[1] = static_cast<uint8_t>(std::clamp(dst[1] * (1.0f - covA) + inkG * covA, 0.0f, 255.0f));
            dst[2] = static_cast<uint8_t>(std::clamp(dst[2] * (1.0f - covA) + inkR * covA, 0.0f, 255.0f));
            dst[3] = 255;
        }
    }
}

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
    std::vector<EdgeSeg> rawEdges;
    int16_t gxMin = 0, gxMax = 0;
    if (!ExtractGlyphEdges(sfnt, glyphId, rawEdges, gxMin, gxMax, 0)) return false;
    RasterizeEdgeListToCanvas(sfnt, glyphId, rawEdges, static_cast<float>(gxMin), static_cast<float>(gxMax),
                              originX, baselineY, pixelHeight, inkR, inkG, inkB, 255, bgra, imgW, imgH, outAdvancePx);
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

// ============================================================================
// OpenType CFF / CFF2 Type 2 CharString Interpreter
// ============================================================================
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
        outIdx.count = DetailLayeredAndHdrImg::RdU32BE(tbl + pos);
        pos += 4u;
    } else {
        if (pos + 2u > tblLen) return false;
        outIdx.count = DetailLayeredAndHdrImg::RdU16BE(tbl + pos);
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
            if (op == 17u && !args.empty()) outCharStringsOff = static_cast<int32_t>(args.back());
            else if (op == 18u && args.size() >= 2u) {
                outPrivateSize = static_cast<int32_t>(args[args.size() - 2u]);
                outPrivateOff  = static_cast<int32_t>(args[args.size() - 1u]);
            } else if (op == 19u && !args.empty()) outSubrsRelOff = static_cast<int32_t>(args.back());
            else if (op == 0x0C24u && !args.empty()) outFdArrayOff = static_cast<int32_t>(args.back());
            args.clear();
        } else if (b0 == 28u) {
            if (p + 2u > dictLen) break;
            args.push_back(static_cast<double>(DetailLayeredAndHdrImg::RdI16BE(dict + p)));
            p += 2u;
        } else if (b0 == 29u) {
            if (p + 4u > dictLen) break;
            args.push_back(static_cast<double>(static_cast<int32_t>(DetailLayeredAndHdrImg::RdU32BE(dict + p))));
            p += 4u;
        } else if (b0 == 30u) {
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
            args.push_back(DetailArtFont::ParseFloatOr(numStr, 0.0f));
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
    if (!tbl || tblLen < 8u) return false;
    uint8_t major = tbl[0];
    uint8_t hdrSize = tbl[2];
    if (hdrSize < 4u || hdrSize > tblLen) return false;

    outCff.tbl = tbl;
    outCff.tblLen = tblLen;
    int32_t csOff = -1, privSz = 0, privOff = -1, subrsRel = -1, fdArrOff = -1;

    if (major == 2u || isCff2Hint) {
        if (tblLen < 5u || major != 2u) return false;
        outCff.isCff2 = true;
        uint16_t topDictLen = DetailLayeredAndHdrImg::RdU16BE(tbl + 3u);
        if (static_cast<size_t>(hdrSize) + topDictLen > tblLen) return false;
        ParseCffDict(tbl + hdrSize, topDictLen, csOff, privSz, privOff, subrsRel, fdArrOff);
        if (!ParseCffIndex(tbl, tblLen, static_cast<size_t>(hdrSize) + topDictLen, true, outCff.globalSubrs)) return false;
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
        const uint8_t* tdPtr = nullptr; size_t tdLen = 0;
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

    if (privOff < 0 && fdArrOff > 0 && static_cast<size_t>(fdArrOff) < tblLen) {
        CffIndexView fdIdx;
        if (ParseCffIndex(tbl, tblLen, static_cast<size_t>(fdArrOff), outCff.isCff2, fdIdx) && fdIdx.count > 0u) {
            const uint8_t* fd0 = nullptr; size_t fd0Len = 0;
            if (fdIdx.GetItem(0, fd0, fd0Len)) {
                int32_t d1 = -1, d2 = -1, d3 = -1;
                ParseCffDict(fd0, fd0Len, d1, privSz, privOff, d2, d3);
            }
        }
    }
    if (privOff > 0 && privSz > 0 && static_cast<size_t>(privOff) + static_cast<size_t>(privSz) <= tblLen) {
        int32_t d1 = -1, d2 = 0, d3 = -1, d4 = -1;
        ParseCffDict(tbl + privOff, static_cast<size_t>(privSz), d1, d2, d3, subrsRel, d4);
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
    std::vector<EdgeSeg> edges;

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
            st.stack.push_back(static_cast<float>(DetailLayeredAndHdrImg::RdI16BE(cs + p)));
            p += 2u;
        } else if (b0 == 255u) {
            if (p + 4u > csLen) return false;
            st.stack.push_back(static_cast<float>(static_cast<int32_t>(DetailLayeredAndHdrImg::RdU32BE(cs + p))) / 65536.0f);
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
            if (b0 == 1u || b0 == 3u || b0 == 18u || b0 == 23u) {
                dropOptionalWidth(1u);
                st.numHints += static_cast<uint32_t>(st.stack.size() / 2u);
                st.stack.clear();
            } else if (b0 == 19u || b0 == 20u) {
                dropOptionalWidth(1u);
                st.numHints += static_cast<uint32_t>(st.stack.size() / 2u);
                st.stack.clear();
                size_t maskBytes = (st.numHints + 7u) / 8u;
                if (p + maskBytes > csLen) return false;
                p += maskBytes;
            } else if (b0 == 21u) {
                if (!cff.isCff2 && !st.sawFirstMoveOrStem) {
                    if (st.stack.size() > 2u) st.stack.erase(st.stack.begin());
                    st.sawFirstMoveOrStem = true;
                }
                if (st.stack.size() < 2u) return false;
                st.MoveTo(st.stack[0], st.stack[1]);
                st.stack.clear();
            } else if (b0 == 22u) {
                if (!cff.isCff2 && !st.sawFirstMoveOrStem) {
                    if (st.stack.size() > 1u) st.stack.erase(st.stack.begin());
                    st.sawFirstMoveOrStem = true;
                }
                if (st.stack.empty()) return false;
                st.MoveTo(st.stack[0], 0.0f);
                st.stack.clear();
            } else if (b0 == 4u) {
                if (!cff.isCff2 && !st.sawFirstMoveOrStem) {
                    if (st.stack.size() > 1u) st.stack.erase(st.stack.begin());
                    st.sawFirstMoveOrStem = true;
                }
                if (st.stack.empty()) return false;
                st.MoveTo(0.0f, st.stack[0]);
                st.stack.clear();
            } else if (b0 == 5u) {
                for (size_t i = 0; i + 1u < st.stack.size(); i += 2u) st.LineTo(st.stack[i], st.stack[i + 1u]);
                st.stack.clear();
            } else if (b0 == 6u || b0 == 7u) {
                bool horiz = (b0 == 6u);
                for (size_t i = 0; i < st.stack.size(); ++i) {
                    if (horiz) st.LineTo(st.stack[i], 0.0f);
                    else       st.LineTo(0.0f, st.stack[i]);
                    horiz = !horiz;
                }
                st.stack.clear();
            } else if (b0 == 8u) {
                for (size_t i = 0; i + 5u < st.stack.size(); i += 6u) {
                    st.CubicTo(st.stack[i], st.stack[i + 1], st.stack[i + 2],
                               st.stack[i + 3], st.stack[i + 4], st.stack[i + 5]);
                }
                st.stack.clear();
            } else if (b0 == 24u) {
                size_t i = 0;
                for (; i + 7u < st.stack.size(); i += 6u) {
                    st.CubicTo(st.stack[i], st.stack[i + 1], st.stack[i + 2],
                               st.stack[i + 3], st.stack[i + 4], st.stack[i + 5]);
                }
                if (i + 1u < st.stack.size()) st.LineTo(st.stack[i], st.stack[i + 1u]);
                st.stack.clear();
            } else if (b0 == 25u) {
                size_t n = st.stack.size();
                if (n >= 8u) {
                    size_t lineEnd = n - 6u;
                    for (size_t i = 0; i + 1u < lineEnd; i += 2u) st.LineTo(st.stack[i], st.stack[i + 1u]);
                    st.CubicTo(st.stack[lineEnd], st.stack[lineEnd + 1], st.stack[lineEnd + 2],
                               st.stack[lineEnd + 3], st.stack[lineEnd + 4], st.stack[lineEnd + 5]);
                }
                st.stack.clear();
            } else if (b0 == 26u) {
                size_t i = 0;
                float dx1 = 0.0f;
                if ((st.stack.size() & 3u) == 1u) dx1 = st.stack[i++];
                for (; i + 3u < st.stack.size(); i += 4u) {
                    st.CubicTo(dx1, st.stack[i], st.stack[i + 1], st.stack[i + 2], 0.0f, st.stack[i + 3]);
                    dx1 = 0.0f;
                }
                st.stack.clear();
            } else if (b0 == 27u) {
                size_t i = 0;
                float dy1 = 0.0f;
                if ((st.stack.size() & 3u) == 1u) dy1 = st.stack[i++];
                for (; i + 3u < st.stack.size(); i += 4u) {
                    st.CubicTo(st.stack[i], dy1, st.stack[i + 1], st.stack[i + 2], st.stack[i + 3], 0.0f);
                    dy1 = 0.0f;
                }
                st.stack.clear();
            } else if (b0 == 30u || b0 == 31u) {
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
            } else if (b0 == 10u || b0 == 29u) {
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
            } else if (b0 == 11u) {
                return true;
            } else if (b0 == 14u) {
                st.CloseContour();
                st.stack.clear();
                ended = true;
                return true;
            } else if (b0 == 12u) {
                if (p >= csLen) return false;
                uint8_t b1 = cs[p++];
                if (b1 == 35u && st.stack.size() >= 12u) {
                    st.CubicTo(st.stack[0], st.stack[1], st.stack[2], st.stack[3], st.stack[4], st.stack[5]);
                    st.CubicTo(st.stack[6], st.stack[7], st.stack[8], st.stack[9], st.stack[10], st.stack[11]);
                } else if (b1 == 34u && st.stack.size() >= 7u) {
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

// ============================================================================
// Structural CBLC+CBDT and sbix Strike Table Parsers (NO whole-table PNG scanning!)
// ============================================================================
inline bool BlitGlyphBitmapOntoSpecimen(
    const ExtendedImageInfo& gImg,
    uint32_t& curX,
    uint32_t& curY,
    uint32_t& rowH,
    uint32_t W,
    uint32_t H,
    std::vector<uint8_t>& canvasBgra)
{
    if (!gImg.valid || gImg.width == 0 || gImg.height == 0) return false;
    uint32_t drawW = gImg.width;
    uint32_t drawH = gImg.height;
    if (drawW > 96u || drawH > 96u) {
        double sc = 96.0 / static_cast<double>(std::max(drawW, drawH));
        drawW = std::max(1u, static_cast<uint32_t>(std::lround(drawW * sc)));
        drawH = std::max(1u, static_cast<uint32_t>(std::lround(drawH * sc)));
    }
    if (curX + drawW + 16u > W) {
        curX = 24u;
        curY += rowH + 16u;
        rowH = 0u;
    }
    if (curY + drawH > H - 8u) return false;

    for (uint32_t py = 0; py < drawH; ++py) {
        uint32_t sy = (py * gImg.height) / drawH;
        for (uint32_t px = 0; px < drawW; ++px) {
            uint32_t sx = (px * gImg.width) / drawW;
            const uint8_t* sp = &gImg.bgraPixels[(static_cast<size_t>(sy) * gImg.width + sx) * 4u];
            DetailArtFont::BlendPremulBgraPixel(
                &canvasBgra[(static_cast<size_t>(curY + py) * W + (curX + px)) * 4u],
                sp[0], sp[1], sp[2], sp[3], 1.0f, DetailArtFont::LayerBlendOp::SrcOver);
        }
    }
    curX += drawW + 16u;
    rowH = std::max(rowH, drawH);
    return true;
}

inline bool RenderCblcCbdtSpecimen(const ParsedSfnt& sfnt, ExtendedImageInfo& outInfo) {
    if (!sfnt.tblCblc.valid || !sfnt.tblCbdt.valid || sfnt.tblCblc.length < 8u || sfnt.tblCbdt.length < 4u) {
        return false;
    }
    const uint8_t* cblc = sfnt.data + sfnt.tblCblc.offset;
    const size_t cblcLen = sfnt.tblCblc.length;
    const uint8_t* cbdt = sfnt.data + sfnt.tblCbdt.offset;
    const size_t cbdtLen = sfnt.tblCbdt.length;

    uint16_t maj = DetailLayeredAndHdrImg::RdU16BE(cblc + 0u);
    if (maj != 2u && maj != 3u) return false;
    uint32_t numSizes = DetailLayeredAndHdrImg::RdU32BE(cblc + 4u);
    if (numSizes == 0 || numSizes > 256u || 8ull + static_cast<uint64_t>(numSizes) * 48ull > cblcLen) {
        return false;
    }

    // Order strikes by ppemY descending so we render crispest bitmap strike first
    std::vector<uint32_t> strikeOrder(numSizes);
    for (uint32_t i = 0; i < numSizes; ++i) strikeOrder[i] = i;
    std::sort(strikeOrder.begin(), strikeOrder.end(), [&](uint32_t a, uint32_t b) {
        uint8_t pyA = cblc[8u + static_cast<size_t>(a) * 48u + 45u];
        uint8_t pyB = cblc[8u + static_cast<size_t>(b) * 48u + 45u];
        return pyA > pyB;
    });

    const uint32_t W = outInfo.width, H = outInfo.height;

    for (uint32_t sIdx : strikeOrder) {
        const uint8_t* bs = cblc + 8u + static_cast<size_t>(sIdx) * 48u;
        uint32_t subTabArrOff = DetailLayeredAndHdrImg::RdU32BE(bs + 0u);
        uint32_t numSubTabs   = DetailLayeredAndHdrImg::RdU32BE(bs + 8u);
        uint8_t bitDepth      = bs[46u];
        if (numSubTabs == 0 || numSubTabs > 4096u ||
            subTabArrOff > cblcLen || static_cast<uint64_t>(numSubTabs) * 8ull > cblcLen - subTabArrOff) {
            continue;
        }

        uint32_t curX = 24u, curY = 64u, rowH = 0u;
        size_t blitted = 0;

        auto decodeCbdtGlyphSlice = [&](uint16_t imageFmt, const uint8_t* gp, size_t glen,
                                        uint8_t constW, uint8_t constH) -> bool
        {
            ExtendedImageInfo gImg;
            if (imageFmt == 17u) { // SmallGlyphMetrics (5B) + dataLen(u32) + PNG
                if (glen < 9u) return false;
                uint32_t pngLen = DetailLayeredAndHdrImg::RdU32BE(gp + 5u);
                if (pngLen == 0 || pngLen > glen - 9u) return false;
                if (!DetailPngPure::DecodePngToImageInfo(gp + 9u, pngLen, L"CBDT", gImg) &&
                    !DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(gp + 9u, pngLen, L"CBDT", gImg)) {
                    return false;
                }
            } else if (imageFmt == 18u) { // BigGlyphMetrics (8B) + dataLen(u32) + PNG
                if (glen < 12u) return false;
                uint32_t pngLen = DetailLayeredAndHdrImg::RdU32BE(gp + 8u);
                if (pngLen == 0 || pngLen > glen - 12u) return false;
                if (!DetailPngPure::DecodePngToImageInfo(gp + 12u, pngLen, L"CBDT", gImg) &&
                    !DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(gp + 12u, pngLen, L"CBDT", gImg)) {
                    return false;
                }
            } else if (imageFmt == 19u) { // Metrics in CBLC + dataLen(u32) + PNG
                if (glen < 4u) return false;
                uint32_t pngLen = DetailLayeredAndHdrImg::RdU32BE(gp + 0u);
                if (pngLen == 0 || pngLen > glen - 4u) return false;
                if (!DetailPngPure::DecodePngToImageInfo(gp + 4u, pngLen, L"CBDT", gImg) &&
                    !DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(gp + 4u, pngLen, L"CBDT", gImg)) {
                    return false;
                }
            } else if ((imageFmt == 1u || imageFmt == 2u || imageFmt == 5u || imageFmt == 6u || imageFmt == 7u) && bitDepth == 32u) {
                size_t mHdr = (imageFmt == 5u) ? 0u : ((imageFmt == 1u || imageFmt == 2u) ? 5u : 8u);
                if (glen <= mHdr) return false;
                uint32_t gw = (mHdr >= 5u) ? gp[1] : constW;
                uint32_t gh = (mHdr >= 5u) ? gp[0] : constH;
                if (gw == 0 || gh == 0 || glen - mHdr < static_cast<size_t>(gw) * gh * 4u) return false;
                gImg.width = gw; gImg.height = gh; gImg.valid = true;
                gImg.bgraPixels.assign(gp + mHdr, gp + mHdr + static_cast<size_t>(gw) * gh * 4u);
            } else {
                return false;
            }
            return BlitGlyphBitmapOntoSpecimen(gImg, curX, curY, rowH, W, H, outInfo.bgraPixels);
        };

        for (uint32_t st = 0; st < numSubTabs && blitted < 32u; ++st) {
            const uint8_t* ar = cblc + subTabArrOff + static_cast<size_t>(st) * 8u;
            uint16_t firstGid = DetailLayeredAndHdrImg::RdU16BE(ar + 0u);
            uint16_t lastGid  = DetailLayeredAndHdrImg::RdU16BE(ar + 2u);
            uint32_t addOff   = DetailLayeredAndHdrImg::RdU32BE(ar + 4u);
            if (lastGid < firstGid || addOff > cblcLen - subTabArrOff || cblcLen - (subTabArrOff + addOff) < 8u) {
                continue;
            }
            const uint8_t* sub = cblc + subTabArrOff + addOff;
            size_t subRem = cblcLen - (subTabArrOff + addOff);
            uint16_t indexFmt = DetailLayeredAndHdrImg::RdU16BE(sub + 0u);
            uint16_t imageFmt = DetailLayeredAndHdrImg::RdU16BE(sub + 2u);
            uint32_t imgDataOff = DetailLayeredAndHdrImg::RdU32BE(sub + 4u);
            if (imgDataOff >= cbdtLen) continue;

            uint32_t gCount = static_cast<uint32_t>(lastGid) - firstGid + 1u;
            if (indexFmt == 1u || indexFmt == 3u) {
                size_t elemSz = (indexFmt == 1u) ? 4u : 2u;
                if (8ull + static_cast<uint64_t>(gCount + 1u) * elemSz > subRem) continue;
                for (uint32_t k = 0; k < gCount && blitted < 32u; ++k) {
                    uint32_t o0 = (indexFmt == 1u)
                        ? DetailLayeredAndHdrImg::RdU32BE(sub + 8u + k * 4u)
                        : DetailLayeredAndHdrImg::RdU16BE(sub + 8u + k * 2u);
                    uint32_t o1 = (indexFmt == 1u)
                        ? DetailLayeredAndHdrImg::RdU32BE(sub + 8u + (k + 1u) * 4u)
                        : DetailLayeredAndHdrImg::RdU16BE(sub + 8u + (k + 1u) * 2u);
                    if (o1 <= o0 || static_cast<uint64_t>(imgDataOff) + o1 > cbdtLen) continue;
                    if (decodeCbdtGlyphSlice(imageFmt, cbdt + imgDataOff + o0, o1 - o0, 0, 0)) {
                        ++blitted;
                    }
                }
            } else if (indexFmt == 2u || indexFmt == 5u) {
                if (subRem < 20u) continue;
                uint32_t imgSize = DetailLayeredAndHdrImg::RdU32BE(sub + 8u);
                uint8_t bigH = sub[12u], bigW = sub[13u];
                uint32_t nEntries = gCount;
                if (indexFmt == 5u) {
                    if (subRem < 24u) continue;
                    nEntries = DetailLayeredAndHdrImg::RdU32BE(sub + 20u);
                }
                if (imgSize == 0) continue;
                for (uint32_t k = 0; k < nEntries && blitted < 32u; ++k) {
                    uint64_t rel = static_cast<uint64_t>(k) * imgSize;
                    if (static_cast<uint64_t>(imgDataOff) + rel + imgSize > cbdtLen) break;
                    if (decodeCbdtGlyphSlice(imageFmt, cbdt + imgDataOff + rel, imgSize, bigW, bigH)) {
                        ++blitted;
                    }
                }
            } else if (indexFmt == 4u) {
                if (subRem < 12u) continue;
                uint32_t numPairs = DetailLayeredAndHdrImg::RdU32BE(sub + 8u);
                if (12ull + static_cast<uint64_t>(numPairs + 1u) * 4ull > subRem) continue;
                for (uint32_t k = 0; k < numPairs && blitted < 32u; ++k) {
                    uint16_t o0 = DetailLayeredAndHdrImg::RdU16BE(sub + 12u + k * 4u + 2u);
                    uint16_t o1 = DetailLayeredAndHdrImg::RdU16BE(sub + 12u + (k + 1u) * 4u + 2u);
                    if (o1 <= o0 || static_cast<uint64_t>(imgDataOff) + o1 > cbdtLen) continue;
                    if (decodeCbdtGlyphSlice(imageFmt, cbdt + imgDataOff + o0, o1 - o0, 0, 0)) {
                        ++blitted;
                    }
                }
            }
        }

        if (blitted > 0) {
            outInfo.formatName += L" [CBDT Color Bitmap]";
            return true;
        }
    }
    return false;
}

inline bool RenderSbixSpecimen(const ParsedSfnt& sfnt, ExtendedImageInfo& outInfo) {
    if (!sfnt.tblSbix.valid || sfnt.tblSbix.length < 12u) return false;
    const uint8_t* sbix = sfnt.data + sfnt.tblSbix.offset;
    const size_t sbixLen = sfnt.tblSbix.length;

    uint16_t version = DetailLayeredAndHdrImg::RdU16BE(sbix + 0u);
    uint32_t numStrikes = DetailLayeredAndHdrImg::RdU32BE(sbix + 4u);
    if (version == 0 || numStrikes == 0 || numStrikes > 256u ||
        8ull + static_cast<uint64_t>(numStrikes) * 4ull > sbixLen) {
        return false;
    }

    // Determine numGlyphs from maxp (if maxp absent, infer from strike offset table bounds)
    uint32_t numGlyphs = sfnt.numGlyphs;
    struct StrikeDesc { uint32_t off; uint16_t ppem; };
    std::vector<StrikeDesc> strikes;
    for (uint32_t s = 0; s < numStrikes; ++s) {
        uint32_t sOff = DetailLayeredAndHdrImg::RdU32BE(sbix + 8u + s * 4u);
        if (sOff + 8u <= sbixLen) {
            strikes.push_back({ sOff, DetailLayeredAndHdrImg::RdU16BE(sbix + sOff) });
        }
    }
    if (strikes.empty()) return false;
    std::sort(strikes.begin(), strikes.end(), [](const StrikeDesc& a, const StrikeDesc& b) {
        return a.ppem > b.ppem;
    });

    const uint32_t W = outInfo.width, H = outInfo.height;

    for (const auto& st : strikes) {
        size_t sRem = sbixLen - st.off;
        const uint8_t* sp = sbix + st.off;
        uint32_t gCount = numGlyphs;
        if (gCount == 0) {
            uint32_t firstDataOff = DetailLayeredAndHdrImg::RdU32BE(sp + 4u);
            if (firstDataOff < 8u || ((firstDataOff - 4u) % 4u) != 0u) continue;
            gCount = (firstDataOff - 4u) / 4u - 1u;
        }
        if (gCount == 0 || gCount > 65535u || 4ull + static_cast<uint64_t>(gCount + 1u) * 4ull > sRem) {
            continue;
        }

        auto resolveSbixGlyph = [&](auto& self, uint32_t gid, int depth, ExtendedImageInfo& gImg) -> bool {
            if (depth > 4 || gid >= gCount) return false;
            uint32_t d0 = DetailLayeredAndHdrImg::RdU32BE(sp + 4u + gid * 4u);
            uint32_t d1 = DetailLayeredAndHdrImg::RdU32BE(sp + 4u + (gid + 1u) * 4u);
            if (d1 <= d0 || d1 - d0 < 8u || d1 > sRem) return false;
            const uint8_t* gd = sp + d0;
            size_t payLen = (d1 - d0) - 8u;
            const uint8_t* pay = gd + 8u;
            if (std::memcmp(gd + 4u, "dupe", 4) == 0 && payLen >= 2u) {
                uint16_t targetGid = DetailLayeredAndHdrImg::RdU16BE(pay);
                return self(self, targetGid, depth + 1, gImg);
            }
            if (std::memcmp(gd + 4u, "png ", 4) == 0) {
                return DetailPngPure::DecodePngToImageInfo(pay, payLen, L"sbix", gImg) ||
                       DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(pay, payLen, L"sbix", gImg);
            }
            if (std::memcmp(gd + 4u, "jpg ", 4) == 0 || std::memcmp(gd + 4u, "tiff", 4) == 0) {
                return DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(pay, payLen, L"sbix", gImg);
            }
            return false;
        };

        uint32_t curX = 24u, curY = 64u, rowH = 0u;
        size_t blitted = 0;
        for (uint32_t gid = 0; gid < gCount && blitted < 32u; ++gid) {
            ExtendedImageInfo gImg;
            if (resolveSbixGlyph(resolveSbixGlyph, gid, 0, gImg)) {
                if (BlitGlyphBitmapOntoSpecimen(gImg, curX, curY, rowH, W, H, outInfo.bgraPixels)) {
                    ++blitted;
                }
            }
        }
        if (blitted > 0) {
            outInfo.formatName += L" [sbix Color Bitmap]";
            return true;
        }
    }
    return false;
}

inline bool RenderCffOrColorFontSpecimen(const ParsedSfnt& sfnt, ExtendedImageInfo& outInfo) {
    const uint32_t W = outInfo.width, H = outInfo.height;

    ParsedCffFont cff;
    bool hasCff = false;
    if (sfnt.tblCff2.valid) hasCff = ParseCffTable(sfnt.data + sfnt.tblCff2.offset, sfnt.tblCff2.length, true, cff);
    if (!hasCff && sfnt.tblCff1.valid) hasCff = ParseCffTable(sfnt.data + sfnt.tblCff1.offset, sfnt.tblCff1.length, false, cff);

    auto getOutlineEdges = [&](uint16_t gid, std::vector<EdgeSeg>& edges, float& xmn, float& xmx) -> bool {
        edges.clear();
        if (sfnt.tblGlyf.valid && sfnt.tblLoca.valid) {
            int16_t iMin = 0, iMax = 0;
            if (ExtractGlyphEdges(sfnt, gid, edges, iMin, iMax, 0) && !edges.empty()) {
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

    std::string asciiHeader;
    for (wchar_t wc : sfnt.familyName) if (wc >= 32 && wc < 127) asciiHeader.push_back(static_cast<char>(wc));
    if (asciiHeader.empty()) asciiHeader = sfnt.tblCff2.valid ? "OPENTYPE CFF2 SPECIMEN" : "OPENTYPE FONT SPECIMEN";
    DetailLayeredAndHdrImg::DrawBitmapText5x7(outInfo.bgraPixels, W, H, 14, 13, asciiHeader, 2, 240, 246, 255);

    size_t drawnItems = 0;

    // 1. COLR v0 + CPAL multi-layer color vector glyphs
    if (sfnt.tblColr.valid && sfnt.tblColr.length >= 14u && sfnt.tblCpal.valid && sfnt.tblCpal.length >= 14u) {
        const uint8_t* colr = sfnt.data + sfnt.tblColr.offset;
        const uint8_t* cpal = sfnt.data + sfnt.tblCpal.offset;
        uint16_t numBase   = DetailLayeredAndHdrImg::RdU16BE(colr + 2u);
        uint32_t baseOff   = DetailLayeredAndHdrImg::RdU32BE(colr + 4u);
        uint32_t layerOff  = DetailLayeredAndHdrImg::RdU32BE(colr + 8u);
        uint16_t numLayers = DetailLayeredAndHdrImg::RdU16BE(colr + 12u);

        uint16_t numPalEntries = DetailLayeredAndHdrImg::RdU16BE(cpal + 2u);
        uint16_t numPalettes   = DetailLayeredAndHdrImg::RdU16BE(cpal + 4u);
        uint16_t numColorRecs  = DetailLayeredAndHdrImg::RdU16BE(cpal + 6u);
        uint32_t colorRecOff   = DetailLayeredAndHdrImg::RdU32BE(cpal + 8u);

        if (numBase > 0 && numPalettes > 0 && baseOff + static_cast<size_t>(numBase) * 6u <= sfnt.tblColr.length &&
            layerOff + static_cast<size_t>(numLayers) * 4u <= sfnt.tblColr.length &&
            colorRecOff + static_cast<size_t>(numColorRecs) * 4u <= sfnt.tblCpal.length)
        {
            uint16_t firstColorIdx = DetailLayeredAndHdrImg::RdU16BE(cpal + 12u);
            float curX = 24.0f, curY = 140.0f;
            for (uint16_t b = 0; b < numBase && curY < static_cast<float>(H) - 24.0f; ++b) {
                const uint8_t* br = colr + baseOff + static_cast<size_t>(b) * 6u;
                uint16_t baseGid  = DetailLayeredAndHdrImg::RdU16BE(br + 0u);
                uint16_t firstLay = DetailLayeredAndHdrImg::RdU16BE(br + 2u);
                uint16_t layCount = DetailLayeredAndHdrImg::RdU16BE(br + 4u);
                float advPx = 48.0f;
                bool anyLay = false;
                for (uint16_t l = 0; l < layCount && firstLay + l < numLayers; ++l) {
                    const uint8_t* lr = colr + layerOff + static_cast<size_t>(firstLay + l) * 4u;
                    uint16_t layGid = DetailLayeredAndHdrImg::RdU16BE(lr + 0u);
                    uint16_t palIdx = DetailLayeredAndHdrImg::RdU16BE(lr + 2u);
                    uint8_t cr = 24, cg = 32, cb = 44, ca = 255;
                    if (palIdx != 0xFFFFu && palIdx < numPalEntries &&
                        static_cast<size_t>(firstColorIdx) + palIdx < numColorRecs)
                    {
                        const uint8_t* cp = cpal + colorRecOff + (static_cast<size_t>(firstColorIdx) + palIdx) * 4u;
                        cb = cp[0]; cg = cp[1]; cr = cp[2]; ca = cp[3];
                    }
                    std::vector<EdgeSeg> edges;
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

    // 2. Structural CBLC+CBDT and sbix strike table bitmap glyphs
    if (RenderCblcCbdtSpecimen(sfnt, outInfo)) return true;
    if (RenderSbixSpecimen(sfnt, outInfo)) return true;

    // 3. CFF / CFF2 cubic Bezier specimen lines
    if (hasCff) {
        auto drawLineCff = [&](std::wstring_view txt, float x, float baseY, float pxH, uint8_t r, uint8_t g, uint8_t b) {
            float curX = x;
            for (wchar_t wc : txt) {
                if (wc == L' ') { curX += pxH * 0.30f; continue; }
                uint16_t gid = LookupCmapGlyph(sfnt, static_cast<uint32_t>(wc));
                if (gid == 0 && wc >= L'a' && wc <= L'z') {
                    gid = LookupCmapGlyph(sfnt, static_cast<uint32_t>(wc - L'a' + L'A'));
                }
                if (gid == 0) { curX += pxH * 0.30f; continue; }
                std::vector<EdgeSeg> edges;
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
                std::vector<EdgeSeg> edges;
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

inline bool RenderSfntFontSpecimen(
    const uint8_t* sfntPtr,
    size_t sfntLen,
    std::wstring_view containerKindOverride,
    ExtendedImageInfo& outInfo)
{
    ParsedSfnt sfnt;
    std::vector<std::wstring> collectionFaces;
    if (!ParseSfntDirectory(sfntPtr, sfntLen, sfnt, &collectionFaces)) {
        return false;
    }
    if (!collectionFaces.empty()) {
        outInfo.layerNames = std::move(collectionFaces);
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
    std::wstring kindTag;
    if (!containerKindOverride.empty()) {
        kindTag = std::wstring(containerKindOverride);
    } else if (sfnt.isCollection) {
        kindTag = L"TrueType Collection (.ttc, " + std::to_wstring(sfnt.collectionFontCount) + L" faces)";
    } else if (sfnt.flavor == MakeSfntTag("OTTO")) {
        kindTag = L"OpenType CFF";
    } else {
        kindTag = L"TrueType";
    }
    outInfo.formatName = dispFamily + L" " + dispSub + L" — " + kindTag +
                         (sfnt.numGlyphs > 0 ? (L" (" + std::to_wstring(sfnt.numGlyphs) + L" glyphs)") : L"");

    outInfo.bgraPixels.resize(static_cast<size_t>(W) * H * 4u);
    for (uint32_t y = 0; y < H; ++y) {
        bool inBanner = (y < 42u);
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t r = inBanner ? 26 : 250;
            uint8_t g = inBanner ? 34 : 250;
            uint8_t b = inBanner ? 48 : 247;
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * W + x) * 4u], r, g, b, 255);
        }
    }

    const bool hasColorOrCff2 = sfnt.tblColr.valid || sfnt.tblCbdt.valid || sfnt.tblSbix.valid || sfnt.tblCff2.valid;
    if (hasColorOrCff2 && RenderCffOrColorFontSpecimen(sfnt, outInfo)) {
        outInfo.valid = true;
        return true;
    }

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
                bmi.bmiHeader.biHeight = -static_cast<LONG>(H);
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
                        for (size_t i = 0; i < static_cast<size_t>(W) * H; ++i) {
                            outInfo.bgraPixels[i * 4u + 3u] = 255;
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

    if (!gdiRenderedRealFont) {
        if (!sfnt.tblGlyf.valid || !sfnt.tblLoca.valid || sfnt.numGlyphs == 0) {
            if (RenderCffOrColorFontSpecimen(sfnt, outInfo)) {
                outInfo.valid = true;
                return true;
            }
            return false;
        }

        std::string asciiHeader;
        for (wchar_t wc : dispFamily) {
            if (wc >= 32 && wc < 127) asciiHeader.push_back(static_cast<char>(wc));
        }
        if (asciiHeader.empty()) asciiHeader = "TRUETYPE SPECIMEN";
        DetailLayeredAndHdrImg::DrawBitmapText5x7(outInfo.bgraPixels, W, H, 14, 13, asciiHeader, 2, 240, 246, 255);

        size_t totalDrawn = 0;
        totalDrawn += RasterizeTextLineGlyf(sfnt, L"ABCDEFGHIJKLM",          18.0f, 82.0f,  34.0f, 20, 26, 36,  outInfo.bgraPixels, W, H);
        totalDrawn += RasterizeTextLineGlyf(sfnt, L"NOPQRSTUVWXYZ",          18.0f, 122.0f, 34.0f, 20, 26, 36,  outInfo.bgraPixels, W, H);
        totalDrawn += RasterizeTextLineGlyf(sfnt, L"abcdefghijklm",          18.0f, 160.0f, 30.0f, 28, 36, 48,  outInfo.bgraPixels, W, H);
        totalDrawn += RasterizeTextLineGlyf(sfnt, L"nopqrstuvwxyz",          18.0f, 196.0f, 30.0f, 28, 36, 48,  outInfo.bgraPixels, W, H);
        totalDrawn += RasterizeTextLineGlyf(sfnt, L"0123456789 !@#$%^&*()",  18.0f, 232.0f, 28.0f, 45, 78, 138, outInfo.bgraPixels, W, H);
        totalDrawn += RasterizeTextLineGlyf(sfnt, L"The quick brown fox jumps over the lazy dog.", 18.0f, 266.0f, 22.0f, 55, 62, 74, outInfo.bgraPixels, W, H);
        totalDrawn += RasterizeTextLineGlyf(sfnt, L"Aa Bb Cc 123",           18.0f, 322.0f, 46.0f, 24, 52, 102, outInfo.bgraPixels, W, H);
        totalDrawn += RasterizeTextLineGlyf(sfnt, L"WATERFALL 64",           290.0f, 380.0f, 60.0f, 18, 24, 34, outInfo.bgraPixels, W, H);

        if (totalDrawn == 0) {
            float curX = 18.0f;
            float curY = 110.0f;
            for (uint16_t gid = 0; gid < sfnt.numGlyphs && curY < static_cast<float>(H) - 20.0f; ++gid) {
                std::vector<EdgeSeg> testEdges;
                int16_t xmn = 0, xmx = 0;
                if (!ExtractGlyphEdges(sfnt, gid, testEdges, xmn, xmx, 0) || testEdges.empty()) continue;
                float adv = 0.0f;
                if (RasterizeGlyphContours(sfnt, gid, curX, curY, 54.0f, 22, 30, 44, outInfo.bgraPixels, W, H, adv)) {
                    ++totalDrawn;
                    curX += std::max(36.0f, adv + 10.0f);
                    if (curX > static_cast<float>(W) - 60.0f) {
                        curX = 18.0f;
                        curY += 72.0f;
                    }
                }
            }
        }

        if (totalDrawn == 0) return false;
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats::FontOutline
