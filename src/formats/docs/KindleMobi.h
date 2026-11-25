#pragma once
// KindleMobi.h — Mobipocket / Kindle E-Book (.mobi/.azw/.azw3/.prc) Reader

namespace Formats {
namespace DetailMediaAndScoreDoc {

// ============================================================================
// 10. Mobipocket / Kindle E-Book (.mobi / .azw / .azw3 / .prc — PalmDB + LZ77 + HUFF/CDIC + KF8)
// ============================================================================
struct MobiHuffCdicCtx {
    struct Dict1Entry {
        uint8_t codelen = 0;
        bool term = false;
        uint64_t maxcode = 0;
    };
    struct CdicSlice {
        const uint8_t* base = nullptr; // points to cdic + 16
        size_t len = 0;                // bytes available from cdic + 16
        uint32_t numEntries = 0;
    };

    Dict1Entry dict1[256] = {};
    uint64_t mincode[33] = {};
    uint64_t maxcode[33] = {};
    uint32_t cdicBits = 0;
    std::vector<CdicSlice> slices;

    bool Init(const uint8_t* huff, size_t huffLen) {
        if (!huff || huffLen < 24 || std::memcmp(huff, "HUFF", 4) != 0) return false;
        uint32_t hdrLen = DetailBinarySysDoc::RdU32BE(huff + 4);
        uint32_t off1   = DetailBinarySysDoc::RdU32BE(huff + 8);
        uint32_t off2   = DetailBinarySysDoc::RdU32BE(huff + 12);
        if (hdrLen < 24 || off1 > huffLen || 1024u > huffLen - off1 ||
            off2 > huffLen || 256u > huffLen - off2) {
            return false;
        }

        // Auto-detect BE vs LE on dict1 (HUFF+16 bit 0..1 == 3 indicates LE on rare Palm builds; standard Kindle is BE)
        size_t validBE = 0, validLE = 0;
        for (size_t i = 0; i < 256; ++i) {
            uint32_t vbe = DetailBinarySysDoc::RdU32BE(huff + off1 + i * 4);
            uint32_t vle = DetailBinarySysDoc::RdU32LE(huff + off1 + i * 4);
            uint8_t clBe = vbe & 0x1Fu, clLe = vle & 0x1Fu;
            if (clBe >= 1 && clBe <= 32 && (vbe & 0x60u) == 0 && (!((vbe & 0x80u) != 0) || clBe <= 8)) ++validBE;
            if (clLe >= 1 && clLe <= 32 && (vle & 0x60u) == 0 && (!((vle & 0x80u) != 0) || clLe <= 8)) ++validLE;
        }
        if (std::max(validBE, validLE) == 0) return false;
        const uint32_t huffFlags = DetailBinarySysDoc::RdU32BE(huff + 16);
        const bool useLE = ((huffFlags & 3u) == 3u) ? (validLE >= validBE) : (validLE > validBE + 32u);

        for (size_t i = 0; i < 256; ++i) {
            uint32_t v = useLE ? DetailBinarySysDoc::RdU32LE(huff + off1 + i * 4)
                               : DetailBinarySysDoc::RdU32BE(huff + off1 + i * 4);
            uint8_t cl = static_cast<uint8_t>(v & 0x1Fu);
            bool term = (v & 0x80u) != 0 && cl >= 1 && cl <= 8;
            uint64_t mcRaw = v >> 8;
            dict1[i].codelen = cl;
            dict1[i].term = term;
            dict1[i].maxcode = term ? (((mcRaw + 1ULL) << (32u - cl)) - 1ULL) : 0ULL;
        }

        mincode[0] = 0;
        maxcode[0] = 0;
        for (uint32_t cl = 1; cl <= 32; ++cl) {
            uint32_t mnRaw = useLE ? DetailBinarySysDoc::RdU32LE(huff + off2 + (cl - 1u) * 8u)
                                   : DetailBinarySysDoc::RdU32BE(huff + off2 + (cl - 1u) * 8u);
            uint32_t mxRaw = useLE ? DetailBinarySysDoc::RdU32LE(huff + off2 + (cl - 1u) * 8u + 4u)
                                   : DetailBinarySysDoc::RdU32BE(huff + off2 + (cl - 1u) * 8u + 4u);
            mincode[cl] = (cl == 32u) ? static_cast<uint64_t>(mnRaw) : (static_cast<uint64_t>(mnRaw) << (32u - cl));
            maxcode[cl] = (cl == 32u) ? static_cast<uint64_t>(mxRaw) : (((static_cast<uint64_t>(mxRaw) + 1ULL) << (32u - cl)) - 1ULL);
        }
        return true;
    }

    bool AddCdic(const uint8_t* cdic, size_t cdicLen) {
        if (!cdic || cdicLen < 16 || std::memcmp(cdic, "CDIC", 4) != 0) return false;
        uint32_t hdrLen     = DetailBinarySysDoc::RdU32BE(cdic + 4);
        uint32_t numPhrases = DetailBinarySysDoc::RdU32BE(cdic + 8);
        uint32_t bits       = DetailBinarySysDoc::RdU32BE(cdic + 12);
        if (hdrLen != 16 || bits == 0 || bits > 16) return false;
        if (slices.empty()) cdicBits = bits;
        else if (bits != cdicBits) return false;

        uint32_t maxPerSlice = 1u << bits;
        uint32_t already = static_cast<uint32_t>(slices.size()) * maxPerSlice;
        uint32_t rem = (numPhrases > already) ? (numPhrases - already) : numPhrases;
        uint32_t count = std::min<uint32_t>({ rem, maxPerSlice, static_cast<uint32_t>((cdicLen - 16) / 2) });
        slices.push_back({ cdic + 16, cdicLen - 16, count });
        return true;
    }

    bool UnpackSlice(const uint8_t* src, size_t srcLen, int depth, std::vector<uint8_t>& out, size_t maxOut) const {
        if (depth > 32 || slices.empty() || cdicBits == 0) return false;
        if (srcLen == 0) return true;

        auto loadU64Padded = [&](size_t bytePos) -> uint64_t {
            uint64_t v = 0;
            for (size_t k = 0; k < 8; ++k) {
                uint8_t b = (bytePos + k < srcLen) ? src[bytePos + k] : 0u;
                v = (v << 8) | b;
            }
            return v;
        };

        int64_t bitsLeft = static_cast<int64_t>(srcLen) * 8;
        size_t pos = 0;
        uint64_t x = loadU64Padded(0);
        int n = 32;
        const uint32_t sliceMask = (1u << cdicBits) - 1u;

        while (out.size() < maxOut) {
            uint32_t code = static_cast<uint32_t>((x >> n) & 0xFFFFFFFFULL);
            const auto& d1 = dict1[code >> 24];
            uint32_t cl = d1.codelen;
            if (cl == 0 || cl > 32) return false;
            uint64_t mc = d1.maxcode;
            if (!d1.term) {
                while (cl < 32 && static_cast<uint64_t>(code) < mincode[cl]) {
                    ++cl;
                }
                mc = maxcode[cl];
            }
            n -= static_cast<int>(cl);
            bitsLeft -= static_cast<int64_t>(cl);
            if (bitsLeft < 0) break;

            if (mc < static_cast<uint64_t>(code)) return false;
            uint64_t r = (mc - static_cast<uint64_t>(code)) >> (32u - cl);
            size_t sliceIdx = static_cast<size_t>(r >> cdicBits);
            uint32_t entryIdx = static_cast<uint32_t>(r) & sliceMask;
            if (sliceIdx >= slices.size() || entryIdx >= slices[sliceIdx].numEntries) return false;

            const auto& sl = slices[sliceIdx];
            uint16_t phrOff = DetailBinarySysDoc::RdU16BE(sl.base + static_cast<size_t>(entryIdx) * 2u);
            if (static_cast<size_t>(phrOff) + 2u > sl.len) return false;
            uint16_t blenAndFlag = DetailBinarySysDoc::RdU16BE(sl.base + phrOff);
            size_t blen = blenAndFlag & 0x7FFFu;
            if (static_cast<size_t>(phrOff) + 2u + blen > sl.len) return false;
            const uint8_t* phrBytes = sl.base + phrOff + 2u;

            if ((blenAndFlag & 0x8000u) != 0) {
                size_t take = std::min<size_t>(blen, maxOut - out.size());
                out.insert(out.end(), phrBytes, phrBytes + take);
            } else {
                if (!UnpackSlice(phrBytes, blen, depth + 1, out, maxOut)) return false;
            }

            if (n < 0) {
                pos += 4;
                x = loadU64Padded(pos);
                n += 32;
            }
        }
        return true;
    }
};

inline bool ParseMobiPalmDb(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.size() < 78) return false;
    bool isBookMobi = (std::memcmp(raw.data() + 60, "BOOKMOBI", 8) == 0);
    bool isTextRead = (std::memcmp(raw.data() + 60, "TEXtREAd", 8) == 0);
    if (!isBookMobi && !isTextRead) return false;

    std::wstring pdbName = FixedAsciiField(raw.data(), 32);
    uint16_t numRecords = DetailBinarySysDoc::RdU16BE(raw.data() + 76);
    if (numRecords == 0 || 78ULL + static_cast<size_t>(numRecords) * 8ULL > raw.size()) return false;

    std::vector<uint32_t> recOff(numRecords);
    for (uint16_t i = 0; i < numRecords; ++i) {
        recOff[i] = DetailBinarySysDoc::RdU32BE(raw.data() + 78 + static_cast<size_t>(i) * 8);
    }
    auto getRecEnd = [&](uint16_t idx) -> size_t {
        uint32_t start = recOff[idx];
        size_t best = raw.size();
        for (uint16_t j = idx + 1; j < numRecords; ++j) {
            if (recOff[j] >= start && recOff[j] <= raw.size()) {
                best = recOff[j];
                break;
            }
        }
        return best;
    };

    struct MobiSectionHdr {
        uint16_t rec0Idx = 0;
        uint16_t compression = 0;
        uint32_t textLength = 0;
        uint16_t textRecCnt = 0;
        uint16_t recSize = 0;
        uint16_t encType = 0;
        uint32_t textEncoding = 65001;
        uint32_t mobiVer = 0;
        uint16_t extraDataFlags = 0;
        uint32_t huffRecIdx = 0;
        uint32_t huffRecCnt = 0;
        uint32_t fdstRecIdx = 0xFFFFFFFFu;
        uint32_t fdstFlowCnt = 0;
        uint32_t kf8BoundaryRec = 0xFFFFFFFFu;
        std::wstring fullTitle;
        std::vector<std::pair<std::wstring, std::wstring>> exthMeta;
    };

    auto parseMobiHeaderRecord = [&](uint16_t hdrRecIdx, MobiSectionHdr& mh) -> bool {
        if (hdrRecIdx >= numRecords || recOff[hdrRecIdx] >= raw.size()) return false;
        size_t rEnd = getRecEnd(hdrRecIdx);
        if (rEnd <= recOff[hdrRecIdx] || rEnd - recOff[hdrRecIdx] < 16) return false;
        const uint8_t* r0 = raw.data() + recOff[hdrRecIdx];
        const size_t r0Len = rEnd - recOff[hdrRecIdx];

        mh.rec0Idx     = hdrRecIdx;
        mh.compression = DetailBinarySysDoc::RdU16BE(r0);
        mh.textLength  = DetailBinarySysDoc::RdU32BE(r0 + 4);
        mh.textRecCnt  = DetailBinarySysDoc::RdU16BE(r0 + 8);
        mh.recSize     = DetailBinarySysDoc::RdU16BE(r0 + 10);
        mh.encType     = DetailBinarySysDoc::RdU16BE(r0 + 12);

        auto decodeStr = [&](const uint8_t* p, size_t n) -> std::wstring {
            if (mh.textEncoding == 1252) return Latin1OrCp1252ToWide(p, n);
            return PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n)));
        };

        if (r0Len >= 24 && std::memcmp(r0 + 16, "MOBI", 4) == 0) {
            const uint8_t* mobiHdr = r0 + 16;
            uint32_t mobiHdrLen = DetailBinarySysDoc::RdU32BE(mobiHdr + 4);
            if (r0Len >= 40) {
                mh.textEncoding = DetailBinarySysDoc::RdU32BE(r0 + 28);
                mh.mobiVer      = DetailBinarySysDoc::RdU32BE(r0 + 36);
            }
            if (mobiHdrLen >= 76 && r0Len >= 92) {
                uint32_t fnOff = DetailBinarySysDoc::RdU32BE(r0 + 84);
                uint32_t fnLen = DetailBinarySysDoc::RdU32BE(r0 + 88);
                if (fnOff < r0Len && fnLen > 0 && fnLen <= r0Len - fnOff) {
                    std::wstring ft = decodeStr(r0 + fnOff, fnLen);
                    if (!ft.empty()) mh.fullTitle = ft;
                }
            }
            if (mobiHdrLen >= 104 && r0Len >= 120) {
                mh.huffRecIdx = DetailBinarySysDoc::RdU32BE(mobiHdr + 96);  // r0 + 112
                mh.huffRecCnt = DetailBinarySysDoc::RdU32BE(mobiHdr + 100); // r0 + 116
            }
            if (mh.mobiVer >= 8 && mobiHdrLen >= 184 && r0Len >= 200) {
                mh.fdstRecIdx  = DetailBinarySysDoc::RdU32BE(r0 + 192);
                mh.fdstFlowCnt = DetailBinarySysDoc::RdU32BE(r0 + 196);
            }
            uint32_t exthFlags = (mobiHdrLen >= 116 && r0Len >= 132) ? DetailBinarySysDoc::RdU32BE(r0 + 128) : 0;
            if (mobiHdrLen >= 228 && r0Len >= 244) {
                mh.extraDataFlags = DetailBinarySysDoc::RdU16BE(r0 + 242);
            }

            size_t exthOff = 16ULL + mobiHdrLen;
            if ((exthFlags & 0x40u) != 0 && exthOff + 12 <= r0Len && std::memcmp(r0 + exthOff, "EXTH", 4) == 0) {
                uint32_t exthLen = DetailBinarySysDoc::RdU32BE(r0 + exthOff + 4);
                uint32_t exthCnt = DetailBinarySysDoc::RdU32BE(r0 + exthOff + 8);
                size_t ep = exthOff + 12;
                size_t eLimit = std::min<size_t>(r0Len, exthOff + exthLen);
                for (uint32_t i = 0; i < exthCnt && i < 256 && ep + 8 <= eLimit; ++i) {
                    uint32_t rType = DetailBinarySysDoc::RdU32BE(r0 + ep);
                    uint32_t rLen  = DetailBinarySysDoc::RdU32BE(r0 + ep + 4);
                    if (rLen < 8 || ep + rLen > eLimit) break;
                    const uint8_t* rp = r0 + ep + 8;
                    size_t payLen = rLen - 8;
                    ep += rLen;

                    if (rType == 121 && payLen == 4) { // EXTH 121 = KF8 BOUNDARY record index
                        mh.kf8BoundaryRec = DetailBinarySysDoc::RdU32BE(rp);
                        continue;
                    }
                    auto exthLabel = [](uint32_t t) -> const wchar_t* {
                        switch (t) {
                            case 100: return L"Author";
                            case 101: return L"Publisher";
                            case 103: return L"Description";
                            case 104: return L"ISBN";
                            case 105: return L"Subject";
                            case 106: return L"Publication Date";
                            case 108: return L"Contributor";
                            case 109: return L"Rights";
                            case 113: return L"ASIN";
                            case 503: return L"Updated Title";
                            case 524: return L"Language";
                            default:  return nullptr;
                        }
                    };
                    if (const wchar_t* lbl = exthLabel(rType)) {
                        std::wstring val = decodeStr(rp, payLen);
                        if (!val.empty()) {
                            mh.exthMeta.push_back({ lbl, val });
                            if (rType == 503) mh.fullTitle = val;
                        }
                    }
                }
            }
        }
        return true;
    };

    MobiSectionHdr activeHdr;
    activeHdr.fullTitle = pdbName;
    if (!parseMobiHeaderRecord(0, activeHdr)) return false;

    // Combo MOBI6 + KF8 (.azw3 / .mobi): jump across BOUNDARY record to KF8 Record 0 if present!
    bool isComboKf8 = false;
    if (activeHdr.kf8BoundaryRec != 0xFFFFFFFFu && activeHdr.kf8BoundaryRec + 1u < numRecords) {
        uint16_t bndIdx = static_cast<uint16_t>(activeHdr.kf8BoundaryRec);
        uint16_t kf8Idx = bndIdx;
        if (recOff[bndIdx] + 8 <= raw.size() && std::memcmp(raw.data() + recOff[bndIdx], "BOUNDARY", 8) == 0) {
            kf8Idx = static_cast<uint16_t>(bndIdx + 1u);
        }
        MobiSectionHdr kf8Hdr;
        kf8Hdr.fullTitle = activeHdr.fullTitle;
        if (kf8Idx < numRecords && parseMobiHeaderRecord(kf8Idx, kf8Hdr) && kf8Hdr.mobiVer >= 8 && kf8Hdr.textRecCnt > 0) {
            if (kf8Hdr.exthMeta.empty()) kf8Hdr.exthMeta = std::move(activeHdr.exthMeta);
            activeHdr = std::move(kf8Hdr);
            isComboKf8 = true;
        }
    }

    auto decodeMobiBytes = [&](const uint8_t* p, size_t n) -> std::wstring {
        if (activeHdr.textEncoding == 1252) return Latin1OrCp1252ToWide(p, n);
        return PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n)));
    };

    // Prepare HUFF/CDIC tables when compression == 17480
    MobiHuffCdicCtx huffCtx;
    bool huffReady = false;
    if (activeHdr.encType == 0 && activeHdr.compression == 17480) {
        uint32_t hIdx = activeHdr.huffRecIdx;
        if (activeHdr.rec0Idx > 0 && hIdx < numRecords) {
            if (!(recOff[hIdx] + 4 <= raw.size() && std::memcmp(raw.data() + recOff[hIdx], "HUFF", 4) == 0) &&
                activeHdr.rec0Idx + hIdx < numRecords) {
                hIdx += activeHdr.rec0Idx;
            }
        }
        if (hIdx == 0 || activeHdr.huffRecCnt < 2 || hIdx + activeHdr.huffRecCnt > numRecords) return false;
        size_t hEnd = getRecEnd(static_cast<uint16_t>(hIdx));
        if (recOff[hIdx] >= raw.size() || hEnd <= recOff[hIdx]) return false;
        if (!huffCtx.Init(raw.data() + recOff[hIdx], hEnd - recOff[hIdx])) return false;
        for (uint32_t c = 1; c < activeHdr.huffRecCnt; ++c) {
            uint16_t cIdx = static_cast<uint16_t>(hIdx + c);
            size_t cEnd = getRecEnd(cIdx);
            if (recOff[cIdx] >= raw.size() || cEnd <= recOff[cIdx]) return false;
            if (!huffCtx.AddCdic(raw.data() + recOff[cIdx], cEnd - recOff[cIdx])) return false;
        }
        huffReady = true;
    }

    // Decompress text records (Uncompressed = 1, PalmDOC LZ77 = 2, HUFF/CDIC = 17480)
    std::vector<uint8_t> decomp;
    if (activeHdr.encType == 0 && (activeHdr.compression == 1 || activeHdr.compression == 2 || (activeHdr.compression == 17480 && huffReady))) {
        uint16_t availRecs = (numRecords > activeHdr.rec0Idx + 1) ? static_cast<uint16_t>(numRecords - activeHdr.rec0Idx - 1) : 0;
        uint16_t maxRec = std::min<uint16_t>({ activeHdr.textRecCnt, availRecs, uint16_t(256) });
        const size_t maxTotalOut = 1024u * 1024u;
        decomp.reserve(std::min<size_t>(static_cast<size_t>(maxRec) * 4096u, maxTotalOut));

        for (uint16_t tIdx = 1; tIdx <= maxRec && decomp.size() < maxTotalOut; ++tIdx) {
            uint16_t rIdx = static_cast<uint16_t>(activeHdr.rec0Idx + tIdx);
            if (recOff[rIdx] >= raw.size()) continue;
            size_t rEnd = getRecEnd(rIdx);
            if (rEnd <= recOff[rIdx]) continue;
            const uint8_t* src = raw.data() + recOff[rIdx];
            size_t srcLen = rEnd - recOff[rIdx];

            // Trim trailing MOBI extra record entries & multibyte overlap BEFORE LZ77 or HUFF/CDIC decompression!
            if (activeHdr.extraDataFlags != 0 && srcLen > 0) {
                for (int bit = 15; bit >= 1; --bit) {
                    if ((activeHdr.extraDataFlags & (1u << bit)) != 0 && srcLen > 0) {
                        size_t tailSz = 0;
                        int shift = 0;
                        size_t p = srcLen;
                        while (p > 0 && shift < 28) {
                            uint8_t b = src[--p];
                            tailSz |= static_cast<size_t>(b & 0x7Fu) << shift;
                            shift += 7;
                            if ((b & 0x80u) != 0) break; // Backward varint stop bit
                        }
                        if (tailSz > 0 && tailSz <= srcLen) srcLen -= tailSz;
                    }
                }
                if ((activeHdr.extraDataFlags & 1u) != 0 && srcLen > 0) {
                    size_t mbTrim = (src[srcLen - 1] & 0x03u) + 1u;
                    if (mbTrim <= srcLen) srcLen -= mbTrim;
                }
            }

            if (activeHdr.compression == 1) {
                size_t take = std::min<size_t>(srcLen, maxTotalOut - decomp.size());
                decomp.insert(decomp.end(), src, src + take);
            } else if (activeHdr.compression == 2) {
                size_t i = 0;
                while (i < srcLen && decomp.size() < maxTotalOut) {
                    uint8_t c = src[i++];
                    if (c == 0x00 || (c >= 0x09 && c <= 0x7F)) {
                        decomp.push_back(c);
                    } else if (c >= 0x01 && c <= 0x08) {
                        for (uint8_t k = 0; k < c && i < srcLen && decomp.size() < maxTotalOut; ++k) {
                            decomp.push_back(src[i++]);
                        }
                    } else if (c >= 0x80 && c <= 0xBF) {
                        if (i >= srcLen) break;
                        uint16_t pair = (static_cast<uint16_t>(c) << 8) | src[i++];
                        size_t dist = (pair >> 3) & 0x07FFu;
                        size_t copyLen = (pair & 0x07u) + 3;
                        if (dist == 0 || dist > decomp.size()) continue;
                        for (size_t k = 0; k < copyLen && decomp.size() < maxTotalOut; ++k) {
                            uint8_t b = decomp[decomp.size() - dist];
                            decomp.push_back(b);
                        }
                    } else { // 0xC0..0xFF: ' ' + (c ^ 0x80)
                        decomp.push_back(' ');
                        if (decomp.size() < maxTotalOut) decomp.push_back(static_cast<uint8_t>(c ^ 0x80u));
                    }
                }
            } else if (activeHdr.compression == 17480) {
                if (!huffCtx.UnpackSlice(src, srcLen, 0, decomp, maxTotalOut)) {
                    if (tIdx == 1 && decomp.empty()) return false;
                    break;
                }
            }
        }
    }

    // If KF8 has an FDST flow table (flow 0 = HTML markup, flows 1..N = CSS/SVG), slice Flow 0!
    size_t markupLen = decomp.size();
    uint32_t parsedFlows = 0;
    if (activeHdr.mobiVer >= 8 && activeHdr.fdstRecIdx != 0xFFFFFFFFu && activeHdr.fdstFlowCnt >= 1) {
        uint32_t fRec = activeHdr.rec0Idx + activeHdr.fdstRecIdx;
        if (fRec >= numRecords && activeHdr.fdstRecIdx < numRecords) fRec = activeHdr.fdstRecIdx;
        if (fRec < numRecords && recOff[fRec] + 20 <= raw.size()) {
            size_t fEnd = getRecEnd(static_cast<uint16_t>(fRec));
            const uint8_t* fp = raw.data() + recOff[fRec];
            size_t fLen = fEnd - recOff[fRec];
            if (fLen >= 20 && std::memcmp(fp, "FDST", 4) == 0) {
                uint32_t secOff = DetailBinarySysDoc::RdU32BE(fp + 4);
                uint32_t nFlows = DetailBinarySysDoc::RdU32BE(fp + 8);
                parsedFlows = nFlows;
                if (nFlows >= 1 && secOff + 8 <= fLen) {
                    uint32_t f0Start = DetailBinarySysDoc::RdU32BE(fp + secOff);
                    uint32_t f0End   = DetailBinarySysDoc::RdU32BE(fp + secOff + 4);
                    if (f0Start == 0 && f0End > 0 && f0End < markupLen) {
                        markupLen = f0End;
                    }
                }
            }
        }
    }

    std::wstring rawMarkup = decodeMobiBytes(decomp.data(), markupLen);
    std::vector<std::wstring> chapters = StripHtmlToParagraphs(rawMarkup, 500);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = (activeHdr.mobiVer >= 8)
        ? (L"Kindle Format 8 (KF8) E-Book (" + extLower + L")")
        : (L"Kindle / Mobipocket E-Book (" + extLower + L")");

    SpreadsheetSheet meta;
    meta.name = L"Book_Metadata";
    meta.rows.push_back({ L"Property", L"Value" });
    meta.rows.push_back({ L"Title", activeHdr.fullTitle.empty() ? L"(Untitled)" : activeHdr.fullTitle });
    meta.rows.push_back({ L"PalmDB Name", pdbName });
    meta.rows.push_back({ L"MOBI Version", std::to_wstring(activeHdr.mobiVer) + (isComboKf8 ? L" (Combo MOBI6+KF8)" : L"") });
    meta.rows.push_back({ L"Compression",
        (activeHdr.compression == 1) ? L"Uncompressed (1)" : ((activeHdr.compression == 2) ? L"PalmDOC LZ77 (2)" :
        ((activeHdr.compression == 17480) ? L"HUFF/CDIC (17480)" : std::to_wstring(activeHdr.compression))) });
    meta.rows.push_back({ L"Uncompressed Text Size", PathUtils::FormatByteSize(activeHdr.textLength) });
    meta.rows.push_back({ L"PalmDB Records / Text Records",
        std::to_wstring(numRecords) + L" / " + std::to_wstring(activeHdr.textRecCnt) + L" (blk " + std::to_wstring(activeHdr.recSize) + L" B)" });
    if (parsedFlows > 0) meta.rows.push_back({ L"KF8 FDST Flows", std::to_wstring(parsedFlows) });
    meta.rows.push_back({ L"Encoding", (activeHdr.textEncoding == 65001) ? L"UTF-8 (65001)" : std::to_wstring(activeHdr.textEncoding) });
    if (activeHdr.encType != 0) meta.rows.push_back({ L"DRM Encryption", L"Encrypted (type " + std::to_wstring(activeHdr.encType) + L")" });
    for (const auto& kv : activeHdr.exthMeta) {
        meta.rows.push_back({ L"EXTH " + kv.first, kv.second });
    }
    FinalizeSheetCols(meta);
    out.sheets.push_back(std::move(meta));

    if (!chapters.empty()) {
        SpreadsheetSheet txtSh;
        txtSh.name = L"Book_Text";
        txtSh.rows.push_back({ L"Paragraph #", L"Decompressed E-Book Text" });
        for (size_t i = 0; i < chapters.size(); ++i) {
            txtSh.rows.push_back({ std::to_wstring(i + 1), chapters[i] });
            out.paragraphs.push_back({ chapters[i], 0, false, false });
        }
        FinalizeSheetCols(txtSh);
        out.sheets.push_back(std::move(txtSh));
        ComputeDocCounts(out);
    }

    out.valid = true;
    return true;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseKindleMobiDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) { return DetailMediaAndScoreDoc::ParseMobiPalmDb(raw, extLower, out); }

} // namespace Formats
