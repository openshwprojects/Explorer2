#pragma once
// WindowsEvtxLog.h — Windows XML Event Log (.evtx — ElfFile + ElfChnk + True BinXML & TemplateDef AST Decoder)

namespace Formats {
namespace DetailEvtxBinXml {

struct SubVal {
    uint8_t type = 0;
    const uint8_t* ptr = nullptr;
    size_t size = 0;
    size_t chunkRelOff = 0;
};

struct EvtxEventData {
    std::wstring timeCreated;
    std::wstring provider;
    std::wstring eventId;
    std::wstring level;
    std::wstring channel;
    std::wstring computer;
    std::vector<std::wstring> dataFields;
    std::vector<std::wstring> bareValues;
};

inline bool ReadChunkNameString(const uint8_t* chunk, size_t chunkLen, uint32_t nameOff, std::wstring& outName, size_t& outInlineBytes) {
    outName.clear();
    outInlineBytes = 0;
    if (nameOff + 10u > chunkLen) return false;
    const uint16_t cch = DetailBinarySysDoc::RdU16LE(chunk + nameOff + 6);
    const size_t totalBytes = 10u + static_cast<size_t>(cch) * 2u;
    if (cch > 1024u || nameOff + totalBytes > chunkLen) return false;
    outName = DetailBinarySysDoc::ReadUtf16LEBounded(chunk + nameOff + 8, static_cast<size_t>(cch) * 2u, false);
    outInlineBytes = totalBytes;
    return true;
}

inline std::wstring FormatWindowsSid(const uint8_t* p, size_t len) {
    if (len < 8) return DetailBinarySysDoc::HexBytes(p, len);
    const uint8_t rev = p[0];
    const uint8_t subCnt = p[1];
    if (8u + static_cast<size_t>(subCnt) * 4u > len) return DetailBinarySysDoc::HexBytes(p, len);
    uint64_t auth = 0;
    for (int i = 0; i < 6; ++i) auth = (auth << 8) | p[2 + i];
    std::wstring s = L"S-" + std::to_wstring(rev) + L"-" + std::to_wstring(auth);
    for (uint8_t i = 0; i < subCnt; ++i) {
        s += L"-" + std::to_wstring(DetailBinarySysDoc::RdU32LE(p + 8 + static_cast<size_t>(i) * 4u));
    }
    return s;
}

inline std::wstring FormatSystemTimeUtc(const uint8_t* p, size_t len) {
    if (len < 16) return {};
    const uint16_t yr = DetailBinarySysDoc::RdU16LE(p + 0);
    const uint16_t mo = DetailBinarySysDoc::RdU16LE(p + 2);
    const uint16_t dy = DetailBinarySysDoc::RdU16LE(p + 6);
    const uint16_t hh = DetailBinarySysDoc::RdU16LE(p + 8);
    const uint16_t mm = DetailBinarySysDoc::RdU16LE(p + 10);
    const uint16_t ss = DetailBinarySysDoc::RdU16LE(p + 12);
    const uint16_t ms = DetailBinarySysDoc::RdU16LE(p + 14);
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u.%03u UTC", yr, mo, dy, hh, mm, ss, ms);
    return buf;
}

inline void EvalBinXmlStream(
    const uint8_t* chunk,
    size_t chunkLen,
    size_t startOff,
    size_t endOff,
    const std::vector<SubVal>* subs,
    std::vector<std::wstring>& elemStack,
    EvtxEventData& outEv,
    int depth);

inline std::wstring DecodeBinXmlTypedValue(
    const uint8_t* chunk,
    size_t chunkLen,
    uint8_t valType,
    const uint8_t* p,
    size_t sz,
    size_t valChunkRelOff,
    std::vector<std::wstring>& elemStack,
    EvtxEventData& outEv,
    int depth)
{
    if (p == nullptr || sz == 0 || valType == 0x00u) return {};
    if ((valType & 0x80u) != 0 && valType != 0x81u) {
        const uint8_t elemType = valType & 0x7Fu;
        size_t elemSz = 0;
        if (elemType == 0x03 || elemType == 0x04) elemSz = 1;
        else if (elemType == 0x05 || elemType == 0x06) elemSz = 2;
        else if (elemType == 0x07 || elemType == 0x08 || elemType == 0x0B || elemType == 0x14) elemSz = 4;
        else if (elemType == 0x09 || elemType == 0x0A || elemType == 0x0C || elemType == 0x11 || elemType == 0x15) elemSz = 8;
        else if (elemType == 0x0F || elemType == 0x12) elemSz = 16;
        if (elemSz > 0) {
            std::wstring arr;
            for (size_t off = 0; off + elemSz <= sz && off < elemSz * 32u; off += elemSz) {
                if (!arr.empty()) arr += L", ";
                arr += DecodeBinXmlTypedValue(chunk, chunkLen, elemType, p + off, elemSz, valChunkRelOff + off, elemStack, outEv, depth + 1);
            }
            return arr;
        }
    }

    switch (valType & 0x7Fu) {
        case 0x01: // UnicodeString
            return DetailBinarySysDoc::ReadUtf16LEBounded(p, sz, true);
        case 0x02: { // AnsiString
            size_t n = sz;
            while (n > 0 && p[n - 1] == 0) --n;
            return PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n));
        }
        case 0x03: return std::to_wstring(static_cast<int8_t>(p[0]));
        case 0x04: return std::to_wstring(p[0]);
        case 0x05: return (sz >= 2) ? std::to_wstring(static_cast<int16_t>(DetailBinarySysDoc::RdU16LE(p))) : std::wstring{};
        case 0x06: return (sz >= 2) ? std::to_wstring(DetailBinarySysDoc::RdU16LE(p)) : std::wstring{};
        case 0x07: return (sz >= 4) ? std::to_wstring(static_cast<int32_t>(DetailBinarySysDoc::RdU32LE(p))) : std::wstring{};
        case 0x08: return (sz >= 4) ? std::to_wstring(DetailBinarySysDoc::RdU32LE(p)) : std::wstring{};
        case 0x09: return (sz >= 8) ? std::to_wstring(static_cast<int64_t>(DetailBinarySysDoc::RdU64LE(p))) : std::wstring{};
        case 0x0A: return (sz >= 8) ? std::to_wstring(DetailBinarySysDoc::RdU64LE(p)) : std::wstring{};
        case 0x0B: {
            if (sz < 4) return {};
            uint32_t u = DetailBinarySysDoc::RdU32LE(p); float fv = 0.0f; std::memcpy(&fv, &u, 4);
            wchar_t b[48] = {}; swprintf_s(b, L"%.6g", fv); return b;
        }
        case 0x0C: {
            if (sz < 8) return {};
            uint64_t u = DetailBinarySysDoc::RdU64LE(p); double dv = 0.0; std::memcpy(&dv, &u, 8);
            wchar_t b[64] = {}; swprintf_s(b, L"%.10g", dv); return b;
        }
        case 0x0D: {
            uint32_t bv = (sz >= 4) ? DetailBinarySysDoc::RdU32LE(p) : p[0];
            return bv != 0 ? L"true" : L"false";
        }
        case 0x0E: // Binary
            return DetailBinarySysDoc::HexBytes(p, std::min<size_t>(sz, 32));
        case 0x0F: // GUID
            return (sz >= 16) ? DetailBinarySysDoc::FormatGuidLE(p) : DetailBinarySysDoc::HexBytes(p, sz);
        case 0x10: // SizeT
            return (sz >= 8) ? DetailBinarySysDoc::Hex64(DetailBinarySysDoc::RdU64LE(p))
                 : (sz >= 4) ? DetailBinarySysDoc::Hex32(DetailBinarySysDoc::RdU32LE(p)) : std::wstring{};
        case 0x11: // FILETIME
            return (sz >= 8) ? DetailBinarySysDoc::FormatFileTimeUtc(DetailBinarySysDoc::RdU64LE(p)) : std::wstring{};
        case 0x12: // SYSTEMTIME
            return FormatSystemTimeUtc(p, sz);
        case 0x13: // SID
            return FormatWindowsSid(p, sz);
        case 0x14: // HexInt32
            return (sz >= 4) ? DetailBinarySysDoc::Hex32(DetailBinarySysDoc::RdU32LE(p)) : std::wstring{};
        case 0x15: // HexInt64
            return (sz >= 8) ? DetailBinarySysDoc::Hex64(DetailBinarySysDoc::RdU64LE(p)) : std::wstring{};
        case 0x21: { // Nested BinXML fragment inside chunk
            if (valChunkRelOff + sz <= chunkLen && depth < 8) {
                EvalBinXmlStream(chunk, chunkLen, valChunkRelOff, valChunkRelOff + sz, nullptr, elemStack, outEv, depth + 1);
            }
            return {};
        }
        default:
            return DetailBinarySysDoc::HexBytes(p, std::min<size_t>(sz, 16));
    }
}

inline std::wstring FormatEvtxLevel(const std::wstring& rawLvl) {
    if (rawLvl == L"1") return L"Critical (1)";
    if (rawLvl == L"2") return L"Error (2)";
    if (rawLvl == L"3") return L"Warning (3)";
    if (rawLvl == L"4") return L"Information (4)";
    if (rawLvl == L"5") return L"Verbose (5)";
    if (rawLvl == L"0") return L"LogAlways (0)";
    return rawLvl;
}

inline void EvalBinXmlStream(
    const uint8_t* chunk,
    size_t chunkLen,
    size_t startOff,
    size_t endOff,
    const std::vector<SubVal>* subs,
    std::vector<std::wstring>& elemStack,
    EvtxEventData& outEv,
    int depth)
{
    if (depth > 8 || startOff >= endOff || endOff > chunkLen) return;

    struct ElemFrame {
        std::wstring name;
        std::wstring dataNameAttr;
        std::wstring textAcc;
    };
    std::vector<ElemFrame> localFrames;

    auto currentSection = [&]() -> int {
        // 1 = System, 2 = EventData, 3 = UserData
        for (const auto& e : elemStack) {
            if (_wcsicmp(e.c_str(), L"System") == 0) return 1;
            if (_wcsicmp(e.c_str(), L"EventData") == 0) return 2;
            if (_wcsicmp(e.c_str(), L"UserData") == 0) return 3;
        }
        return 0;
    };

    auto finishElement = [&]() {
        if (localFrames.empty()) return;
        ElemFrame fr = std::move(localFrames.back());
        localFrames.pop_back();
        if (!elemStack.empty()) elemStack.pop_back();

        std::wstring val = PathUtils::Trim(fr.textAcc);
        if (val.empty()) return;
        const int sec = currentSection();
        if (sec == 1 || _wcsicmp(fr.name.c_str(), L"EventID") == 0 ||
            _wcsicmp(fr.name.c_str(), L"Level") == 0 ||
            _wcsicmp(fr.name.c_str(), L"Channel") == 0 ||
            _wcsicmp(fr.name.c_str(), L"Computer") == 0)
        {
            if (_wcsicmp(fr.name.c_str(), L"EventID") == 0 && outEv.eventId.empty()) outEv.eventId = val;
            else if (_wcsicmp(fr.name.c_str(), L"Level") == 0 && outEv.level.empty()) outEv.level = FormatEvtxLevel(val);
            else if (_wcsicmp(fr.name.c_str(), L"Channel") == 0 && outEv.channel.empty()) outEv.channel = val;
            else if (_wcsicmp(fr.name.c_str(), L"Computer") == 0 && outEv.computer.empty()) outEv.computer = val;
            else if (_wcsicmp(fr.name.c_str(), L"Provider") == 0 && outEv.provider.empty()) outEv.provider = val;
        } else if (sec == 2) {
            if (!fr.dataNameAttr.empty()) outEv.dataFields.push_back(fr.dataNameAttr + L"=" + val);
            else outEv.dataFields.push_back(val);
        } else if (sec == 3) {
            outEv.dataFields.push_back(fr.name + L"=" + val);
        } else {
            outEv.bareValues.push_back(val);
        }
    };

    auto appendText = [&](const std::wstring& txt) {
        if (txt.empty()) return;
        if (!localFrames.empty()) localFrames.back().textAcc += txt;
        else outEv.bareValues.push_back(txt);
    };

    auto readScalarOrSubPiece = [&](size_t& p, std::wstring& outVal, bool& outIsNullCond) -> bool {
        outVal.clear();
        outIsNullCond = false;
        if (p >= endOff) return false;
        const uint8_t tok = chunk[p];
        if (tok == 0x05u || tok == 0x45u) { // ValueText
            if (p + 4u > endOff) return false;
            const uint16_t cch = DetailBinarySysDoc::RdU16LE(chunk + p + 2);
            if (p + 4u + static_cast<size_t>(cch) * 2u > endOff) return false;
            outVal = DetailBinarySysDoc::ReadUtf16LEBounded(chunk + p + 4, static_cast<size_t>(cch) * 2u, true);
            p += 4u + static_cast<size_t>(cch) * 2u;
            return true;
        }
        if (tok == 0x07u || tok == 0x47u) { // CDATA
            if (p + 3u > endOff) return false;
            const uint16_t cch = DetailBinarySysDoc::RdU16LE(chunk + p + 1);
            if (p + 3u + static_cast<size_t>(cch) * 2u > endOff) return false;
            outVal = DetailBinarySysDoc::ReadUtf16LEBounded(chunk + p + 3, static_cast<size_t>(cch) * 2u, true);
            p += 3u + static_cast<size_t>(cch) * 2u;
            return true;
        }
        if (tok == 0x08u || tok == 0x48u) { // CharRef
            if (p + 3u > endOff) return false;
            uint16_t ch = DetailBinarySysDoc::RdU16LE(chunk + p + 1);
            if (ch != 0) outVal.push_back(static_cast<wchar_t>(ch));
            p += 3u;
            return true;
        }
        if (tok == 0x09u || tok == 0x49u) { // EntityRef
            if (p + 5u > endOff) return false;
            uint32_t nOff = DetailBinarySysDoc::RdU32LE(chunk + p + 1);
            p += 5u;
            std::wstring ent; size_t inl = 0;
            if (ReadChunkNameString(chunk, chunkLen, nOff, ent, inl)) {
                if (nOff == p) p += inl;
                if (ent == L"amp") outVal = L"&";
                else if (ent == L"lt") outVal = L"<";
                else if (ent == L"gt") outVal = L">";
                else if (ent == L"quot") outVal = L"\"";
                else if (ent == L"apos") outVal = L"'";
                else outVal = L"&" + ent + L";";
            }
            return true;
        }
        if (tok == 0x0Du || tok == 0x0Eu) { // NormalSubstitution / ConditionalSubstitution
            if (p + 4u > endOff) return false;
            const uint16_t subId = DetailBinarySysDoc::RdU16LE(chunk + p + 1);
            const uint8_t declType = chunk[p + 3];
            p += 4u;
            if (subs && subId < subs->size()) {
                const auto& sv = (*subs)[subId];
                if (sv.type == 0x00u || sv.size == 0) {
                    if (tok == 0x0Eu) outIsNullCond = true;
                    return true;
                }
                const uint8_t effType = (sv.type != 0) ? sv.type : declType;
                outVal = DecodeBinXmlTypedValue(chunk, chunkLen, effType, sv.ptr, sv.size, sv.chunkRelOff, elemStack, outEv, depth);
            } else if (tok == 0x0Eu) {
                outIsNullCond = true;
            }
            return true;
        }
        return false;
    };

    size_t pos = startOff;
    bool inOpenTagAttrs = false;
    while (pos < endOff) {
        const uint8_t tok = chunk[pos];
        if (tok == 0x00u) {
            ++pos;
            break;
        }
        if (tok == 0x0Fu) { // FragmentHeader [0x0F, maj, min, flags]
            if (pos + 4u > endOff) break;
            pos += 4u;
            continue;
        }
        if (tok == 0x01u || tok == 0x41u) { // OpenStartElement
            const bool hasAttrs = (tok == 0x41u);
            if (pos + 11u > endOff) break;
            const uint32_t nameOff = DetailBinarySysDoc::RdU32LE(chunk + pos + 7);
            pos += 11u;
            std::wstring elemName;
            size_t inlBytes = 0;
            if (!ReadChunkNameString(chunk, chunkLen, nameOff, elemName, inlBytes)) break;
            if (nameOff == pos) {
                if (pos + inlBytes > endOff) break;
                pos += inlBytes;
            }
            if (hasAttrs) {
                if (pos + 4u > endOff) break;
                pos += 4u; // attrListDataSize
            }
            elemStack.push_back(elemName);
            localFrames.push_back({ elemName, {}, {} });
            inOpenTagAttrs = hasAttrs;
            continue;
        }
        if (tok == 0x06u || tok == 0x46u) { // Attribute
            if (pos + 5u > endOff) break;
            const uint32_t attrNameOff = DetailBinarySysDoc::RdU32LE(chunk + pos + 1);
            pos += 5u;
            std::wstring attrName;
            size_t inlBytes = 0;
            if (!ReadChunkNameString(chunk, chunkLen, attrNameOff, attrName, inlBytes)) break;
            if (attrNameOff == pos) {
                if (pos + inlBytes > endOff) break;
                pos += inlBytes;
            }
            std::wstring attrVal;
            bool nullCond = false;
            while (pos < endOff) {
                const uint8_t peek = chunk[pos];
                if (peek == 0x06u || peek == 0x46u || peek == 0x02u || peek == 0x03u || peek == 0x00u) break;
                std::wstring piece;
                bool pieceNull = false;
                if (!readScalarOrSubPiece(pos, piece, pieceNull)) break;
                if (pieceNull && attrVal.empty()) nullCond = true;
                else { nullCond = false; attrVal += piece; }
            }
            if (!nullCond && !attrVal.empty() && !localFrames.empty()) {
                const auto& curElem = localFrames.back().name;
                if (_wcsicmp(curElem.c_str(), L"Provider") == 0) {
                    if ((_wcsicmp(attrName.c_str(), L"Name") == 0 || _wcsicmp(attrName.c_str(), L"EventSourceName") == 0) && outEv.provider.empty()) {
                        outEv.provider = attrVal;
                    } else if (_wcsicmp(attrName.c_str(), L"Guid") == 0 && outEv.provider.empty()) {
                        outEv.provider = attrVal;
                    }
                } else if (_wcsicmp(curElem.c_str(), L"TimeCreated") == 0 && _wcsicmp(attrName.c_str(), L"SystemTime") == 0) {
                    outEv.timeCreated = attrVal;
                } else if (_wcsicmp(curElem.c_str(), L"Data") == 0 && _wcsicmp(attrName.c_str(), L"Name") == 0) {
                    localFrames.back().dataNameAttr = attrVal;
                } else if (currentSection() == 3 && _wcsicmp(attrName.c_str(), L"xmlns") != 0) {
                    outEv.dataFields.push_back(curElem + L"." + attrName + L"=" + attrVal);
                }
            }
            continue;
        }
        if (tok == 0x02u) { // CloseStartElement
            ++pos;
            inOpenTagAttrs = false;
            continue;
        }
        if (tok == 0x03u || tok == 0x04u) { // CloseEmptyElement / EndElement
            ++pos;
            inOpenTagAttrs = false;
            finishElement();
            continue;
        }
        if (tok == 0x0Au) { // PITarget
            if (pos + 5u > endOff) break;
            uint32_t nOff = DetailBinarySysDoc::RdU32LE(chunk + pos + 1);
            pos += 5u;
            std::wstring dummy; size_t inl = 0;
            if (ReadChunkNameString(chunk, chunkLen, nOff, dummy, inl) && nOff == pos) pos += std::min(inl, endOff - pos);
            continue;
        }
        if (tok == 0x0Bu) { // PIData
            if (pos + 3u > endOff) break;
            uint16_t cch = DetailBinarySysDoc::RdU16LE(chunk + pos + 1);
            pos += std::min<size_t>(endOff - pos, 3u + static_cast<size_t>(cch) * 2u);
            continue;
        }
        if (tok == 0x0Cu) { // TemplateInstance [0x0C][u8 unk][u32 tmplId][u32 tmplDefOff]
            if (pos + 10u > endOff) break;
            const uint32_t tmplDefOff = DetailBinarySysDoc::RdU32LE(chunk + pos + 6);
            pos += 10u;
            if (tmplDefOff + 24u > chunkLen) break;
            const uint32_t tmplDataSz = DetailBinarySysDoc::RdU32LE(chunk + tmplDefOff + 20);
            if (tmplDefOff + 24ULL + tmplDataSz > chunkLen) break;
            if (tmplDefOff == pos) {
                if (pos + 24ULL + tmplDataSz > endOff) break;
                pos += 24u + tmplDataSz;
            }
            if (pos + 4u > endOff) break;
            const uint32_t numSubs = DetailBinarySysDoc::RdU32LE(chunk + pos);
            pos += 4u;
            if (numSubs > 1024u || pos + static_cast<size_t>(numSubs) * 4u > endOff) break;
            const size_t descBase = pos;
            size_t valPos = descBase + static_cast<size_t>(numSubs) * 4u;
            std::vector<SubVal> instSubs(numSubs);
            bool subsOk = true;
            for (uint32_t si = 0; si < numSubs; ++si) {
                const uint16_t vSz = DetailBinarySysDoc::RdU16LE(chunk + descBase + si * 4u);
                const uint8_t  vTy = chunk[descBase + si * 4u + 2];
                if (valPos + vSz > endOff) { subsOk = false; break; }
                instSubs[si] = { vTy, chunk + valPos, vSz, valPos };
                valPos += vSz;
            }
            if (!subsOk) break;
            pos = valPos;

            EvalBinXmlStream(
                chunk,
                chunkLen,
                tmplDefOff + 24u,
                tmplDefOff + 24u + tmplDataSz,
                &instSubs,
                elemStack,
                outEv,
                depth + 1);
            continue;
        }
        // Scalar value / substitution token in element body
        std::wstring piece;
        bool nullCond = false;
        if (readScalarOrSubPiece(pos, piece, nullCond)) {
            if (!nullCond && !inOpenTagAttrs) appendText(piece);
            continue;
        }
        break; // Unrecognized token -> halt stream cleanly without scraping
    }
}

} // namespace DetailEvtxBinXml

inline bool ParseWindowsEvtxLog(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    SourceBytes src(raw, out);
    uint8_t fileHdr[128] = {};
    if (src.Size() < 128 || !DetailBinarySysDoc::ReadSourceExact(src, 0, fileHdr, 128) || std::memcmp(fileHdr, "ElfFile\0", 8) != 0) {
        return false;
    }

    const uint64_t fileSize   = src.Size();
    const uint64_t firstChunk = DetailBinarySysDoc::RdU64LE(fileHdr + 8);
    const uint64_t lastChunk  = DetailBinarySysDoc::RdU64LE(fileHdr + 16);
    const uint64_t nextRecId  = DetailBinarySysDoc::RdU64LE(fileHdr + 24);
    const uint16_t minVer     = DetailBinarySysDoc::RdU16LE(fileHdr + 36);
    const uint16_t majVer     = DetailBinarySysDoc::RdU16LE(fileHdr + 38);
    const uint16_t hdrBlkSz   = DetailBinarySysDoc::RdU16LE(fileHdr + 40);
    const uint16_t numChunks  = DetailBinarySysDoc::RdU16LE(fileHdr + 42);
    const uint32_t fileFlags  = DetailBinarySysDoc::RdU32LE(fileHdr + 120);

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"EVTX_Header";
    hdrSh.maxCols = 2;
    hdrSh.rows.push_back({ L"Property", L"Value" });
    hdrSh.rows.push_back({ L"Signature", L"ElfFile\\0 (Windows Binary XML Event Log)" });
    hdrSh.rows.push_back({ L"Format Version", std::to_wstring(majVer) + L"." + std::to_wstring(minVer) });
    hdrSh.rows.push_back({ L"Chunk Range", std::to_wstring(firstChunk) + L" .. " + std::to_wstring(lastChunk) + L" (" + std::to_wstring(numChunks) + L" chunks)" });
    hdrSh.rows.push_back({ L"Next Event Record ID", std::to_wstring(nextRecId) });
    hdrSh.rows.push_back({ L"Header Flags", (fileFlags & 1u) ? L"Dirty (0x1)" : ((fileFlags & 2u) ? L"Full (0x2)" : L"Clean (0x0)") });
    hdrSh.rows.push_back({ L"Total File Size", PathUtils::FormatByteSize(fileSize) });

    SpreadsheetSheet recSh;
    recSh.name = L"Event_Records";
    recSh.maxCols = 8;
    recSh.rows.push_back({ L"Record ID", L"TimeCreated / Written (UTC)", L"Provider", L"Event ID", L"Level", L"Channel", L"Computer", L"EventData / Payload" });

    uint64_t chunkPos = (hdrBlkSz >= 128 && hdrBlkSz <= fileSize) ? hdrBlkSz : 4096ULL;
    size_t parsedChunks = 0;
    std::vector<uint8_t> chunkBuf;

    while (chunkPos + 512ULL <= fileSize && recSh.rows.size() <= 2500) {
        const size_t chunkLen = static_cast<size_t>(std::min<uint64_t>(65536ULL, fileSize - chunkPos));
        if (!DetailBinarySysDoc::ReadSourceVec(src, chunkPos, chunkLen, chunkBuf) || chunkBuf.size() < 512) break;
        if (std::memcmp(chunkBuf.data(), "ElfChnk\0", 8) != 0) {
            chunkPos += 65536ULL;
            continue;
        }
        ++parsedChunks;
        const uint32_t freeSpaceOff = DetailBinarySysDoc::RdU32LE(chunkBuf.data() + 48);
        const size_t recLimit = (freeSpaceOff > 512u && freeSpaceOff <= chunkLen) ? static_cast<size_t>(freeSpaceOff) : chunkLen;

        size_t rPos = 512u;
        while (rPos + 28u <= recLimit && recSh.rows.size() <= 2500) {
            if (DetailBinarySysDoc::RdU32LE(chunkBuf.data() + rPos) != 0x00002A2Au) break;
            const uint32_t recSz = DetailBinarySysDoc::RdU32LE(chunkBuf.data() + rPos + 4);
            if (recSz < 28u || rPos + recSz > chunkLen) break;

            const uint64_t recId  = DetailBinarySysDoc::RdU64LE(chunkBuf.data() + rPos + 8);
            const uint64_t wrTime = DetailBinarySysDoc::RdU64LE(chunkBuf.data() + rPos + 16);

            DetailEvtxBinXml::EvtxEventData ev;
            std::vector<std::wstring> elemStack;
            DetailEvtxBinXml::EvalBinXmlStream(
                chunkBuf.data(),
                chunkLen,
                rPos + 24u,
                rPos + recSz - 4u,
                nullptr,
                elemStack,
                ev,
                0);

            // Handle minimal standalone ValueText fragments (without <Event> wrapper)
            if (ev.provider.empty() && ev.dataFields.empty() && !ev.bareValues.empty()) {
                ev.provider = ev.bareValues[0];
                for (size_t i = 1; i < ev.bareValues.size(); ++i) ev.dataFields.push_back(ev.bareValues[i]);
            }

            std::wstring payloadStr;
            for (size_t i = 0; i < ev.dataFields.size() && payloadStr.size() < 512; ++i) {
                if (!payloadStr.empty()) payloadStr += L" | ";
                payloadStr += ev.dataFields[i];
            }
            if (payloadStr.empty() && !ev.bareValues.empty()) {
                payloadStr = ev.bareValues[0];
            }

            recSh.rows.push_back({
                std::to_wstring(recId),
                ev.timeCreated.empty() ? DetailBinarySysDoc::FormatFileTimeUtc(wrTime) : ev.timeCreated,
                ev.provider.empty() ? L"(Event)" : ev.provider,
                ev.eventId.empty() ? L"-" : ev.eventId,
                ev.level.empty() ? L"-" : ev.level,
                ev.channel.empty() ? L"-" : ev.channel,
                ev.computer.empty() ? L"-" : ev.computer,
                payloadStr
            });

            rPos += recSz;
        }
        chunkPos += 65536ULL;
    }

    if (!src.ReadFailed() && chunkPos >= fileSize) {
        out.sourceReadFully = true;
    }

    hdrSh.rows.push_back({ L"Verified Chunks / Events", std::to_wstring(parsedChunks) + L" / " + std::to_wstring(recSh.rows.size() - 1) });
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Windows XML Event Log (.evtx)";
    out.sheets.push_back(std::move(hdrSh));
    if (recSh.rows.size() > 1) out.sheets.push_back(std::move(recSh));
    out.valid = true;
    return true;
}

} // namespace Formats
