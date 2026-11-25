#pragma once
// BioWareGffStruct.h — BioWare Generic File Format Inspector (.gff — "GFF / UTC / UTI / DLG / IFO V3.2 & V3.3")
// Parses all 6 GFF V3.2/V3.3 pools (Struct, Field, Label, FieldData, FieldIndices, ListIndices),
// all 19 GFF field types (primitives, CExoString, CResRef, CExoLocString, VOID, Orientation, Vector,
// nested Struct, and List) with strict 64-bit bounds checks and cycle-safe Struct traversal.

namespace Formats {

namespace DetailBioWareGff {

struct GffHeader {
    char     fileType[5]{};
    char     fileVer[5]{};
    uint32_t structOff = 0, structCnt = 0;
    uint32_t fieldOff  = 0, fieldCnt  = 0;
    uint32_t labelOff  = 0, labelCnt  = 0;
    uint32_t fdataOff  = 0, fdataBytes = 0;
    uint32_t findxOff  = 0, findxBytes = 0;
    uint32_t lindxOff  = 0, lindxBytes = 0;
};

inline const wchar_t* FieldTypeName(uint32_t t) {
    switch (t) {
    case  0u: return L"BYTE";
    case  1u: return L"CHAR";
    case  2u: return L"WORD";
    case  3u: return L"SHORT";
    case  4u: return L"DWORD";
    case  5u: return L"INT";
    case  6u: return L"DWORD64";
    case  7u: return L"INT64";
    case  8u: return L"FLOAT";
    case  9u: return L"DOUBLE";
    case 10u: return L"CExoString";
    case 11u: return L"CResRef";
    case 12u: return L"CExoLocString";
    case 13u: return L"VOID";
    case 14u: return L"Struct";
    case 15u: return L"List";
    case 16u: return L"Orientation";
    case 17u: return L"Vector";
    case 18u: return L"StrRef";
    default:  return L"Unknown";
    }
}

inline bool WalkGffStruct(
    const uint8_t* data,
    const GffHeader& h,
    uint32_t structIdx,
    const std::wstring& pathPrefix,
    int depth,
    std::vector<uint8_t>& activeAncestors,
    SpreadsheetSheet& outSheet)
{
    if (depth > 32 || structIdx >= h.structCnt) return false;
    if (activeAncestors[structIdx] != 0u) return false; // Cycle bomb!
    if (outSheet.rows.size() >= 16384u) return true;

    activeAncestors[structIdx] = 1u;
    const uint8_t* st = data + h.structOff + static_cast<size_t>(structIdx) * 12u;
    const uint32_t dataOrOff = ByteIO::RdU32LE(st + 4u);
    const uint32_t nFields   = ByteIO::RdU32LE(st + 8u);

    std::vector<uint32_t> fieldIdxs;
    if (nFields == 1u) {
        if (dataOrOff >= h.fieldCnt) { activeAncestors[structIdx] = 0u; return false; }
        fieldIdxs.push_back(dataOrOff);
    } else if (nFields > 1u) {
        if (nFields > 16384u || static_cast<uint64_t>(dataOrOff) + static_cast<uint64_t>(nFields) * 4ULL > h.findxBytes) {
            activeAncestors[structIdx] = 0u;
            return false;
        }
        fieldIdxs.reserve(nFields);
        const uint8_t* fiPtr = data + h.findxOff + dataOrOff;
        for (uint32_t i = 0; i < nFields; ++i) {
            const uint32_t fIdx = ByteIO::RdU32LE(fiPtr + static_cast<size_t>(i) * 4u);
            if (fIdx >= h.fieldCnt) { activeAncestors[structIdx] = 0u; return false; }
            fieldIdxs.push_back(fIdx);
        }
    }

    const uint8_t* fdata = data + h.fdataOff;

    for (uint32_t fIdx : fieldIdxs) {
        if (outSheet.rows.size() >= 16384u) break;
        const uint8_t* fld      = data + h.fieldOff + static_cast<size_t>(fIdx) * 12u;
        const uint32_t fType    = ByteIO::RdU32LE(fld + 0u);
        const uint32_t lblIdx   = ByteIO::RdU32LE(fld + 4u);
        const uint32_t fValOff  = ByteIO::RdU32LE(fld + 8u);
        if (lblIdx >= h.labelCnt || fType > 18u) {
            activeAncestors[structIdx] = 0u;
            return false;
        }

        const char* lblPtr = reinterpret_cast<const char*>(data + h.labelOff + static_cast<size_t>(lblIdx) * 16u);
        const size_t lblLen = strnlen(lblPtr, 16u);
        if (lblLen == 0u) { activeAncestors[structIdx] = 0u; return false; }
        std::wstring lblW = BioWareCommon::DecodeBioWareString(std::string_view(lblPtr, lblLen));
        std::wstring fullPath = pathPrefix.empty() ? lblW : (pathPrefix + L"." + lblW);

        if (fType == 14u) { // Nested Struct
            outSheet.rows.push_back({ fullPath, L"Struct", L"{...}" });
            if (!WalkGffStruct(data, h, fValOff, fullPath, depth + 1, activeAncestors, outSheet)) {
                activeAncestors[structIdx] = 0u;
                return false;
            }
            continue;
        }
        if (fType == 15u) { // List of Structs
            if (static_cast<uint64_t>(fValOff) + 4ULL > h.lindxBytes) {
                activeAncestors[structIdx] = 0u;
                return false;
            }
            const uint8_t* liPtr = data + h.lindxOff + fValOff;
            const uint32_t listCnt = ByteIO::RdU32LE(liPtr);
            if (listCnt > 32768u ||
                static_cast<uint64_t>(fValOff) + 4ULL + static_cast<uint64_t>(listCnt) * 4ULL > h.lindxBytes) {
                activeAncestors[structIdx] = 0u;
                return false;
            }
            outSheet.rows.push_back({ fullPath, L"List", L"[" + std::to_wstring(listCnt) + L" items]" });
            for (uint32_t li = 0; li < listCnt; ++li) {
                const uint32_t childStructIdx = ByteIO::RdU32LE(liPtr + 4u + static_cast<size_t>(li) * 4u);
                std::wstring itemPrefix = fullPath + L"[" + std::to_wstring(li) + L"]";
                if (!WalkGffStruct(data, h, childStructIdx, itemPrefix, depth + 1, activeAncestors, outSheet)) {
                    activeAncestors[structIdx] = 0u;
                    return false;
                }
            }
            continue;
        }

        std::wstring valStr;
        wchar_t numBuf[128] = {};
        switch (fType) {
        case 0u: valStr = std::to_wstring(fValOff & 0xFFu); break;
        case 1u: valStr = std::to_wstring(static_cast<int8_t>(fValOff & 0xFFu)); break;
        case 2u: valStr = std::to_wstring(fValOff & 0xFFFFu); break;
        case 3u: valStr = std::to_wstring(static_cast<int16_t>(fValOff & 0xFFFFu)); break;
        case 4u: valStr = std::to_wstring(fValOff); break;
        case 5u: valStr = std::to_wstring(static_cast<int32_t>(fValOff)); break;
        case 6u:
            if (static_cast<uint64_t>(fValOff) + 8ULL > h.fdataBytes) { activeAncestors[structIdx] = 0u; return false; }
            valStr = std::to_wstring(ByteIO::RdU64LE(fdata + fValOff));
            break;
        case 7u:
            if (static_cast<uint64_t>(fValOff) + 8ULL > h.fdataBytes) { activeAncestors[structIdx] = 0u; return false; }
            valStr = std::to_wstring(static_cast<int64_t>(ByteIO::RdU64LE(fdata + fValOff)));
            break;
        case 8u: {
            float fv = 0.0f;
            std::memcpy(&fv, &fValOff, 4);
            swprintf_s(numBuf, L"%.4g", fv);
            valStr = numBuf;
            break;
        }
        case 9u: {
            if (static_cast<uint64_t>(fValOff) + 8ULL > h.fdataBytes) { activeAncestors[structIdx] = 0u; return false; }
            uint64_t dbits = ByteIO::RdU64LE(fdata + fValOff);
            double dv = 0.0;
            std::memcpy(&dv, &dbits, 8);
            swprintf_s(numBuf, L"%.6g", dv);
            valStr = numBuf;
            break;
        }
        case 10u: { // CExoString: uint32 len + char[len]
            if (static_cast<uint64_t>(fValOff) + 4ULL > h.fdataBytes) { activeAncestors[structIdx] = 0u; return false; }
            const uint32_t slen = ByteIO::RdU32LE(fdata + fValOff);
            if (static_cast<uint64_t>(fValOff) + 4ULL + slen > h.fdataBytes) { activeAncestors[structIdx] = 0u; return false; }
            valStr = BioWareCommon::DecodeBioWareString(
                std::string_view(reinterpret_cast<const char*>(fdata + fValOff + 4u), slen));
            break;
        }
        case 11u: { // CResRef: uint8 len + char[len]
            if (static_cast<uint64_t>(fValOff) + 1ULL > h.fdataBytes) { activeAncestors[structIdx] = 0u; return false; }
            const uint8_t rlen = fdata[fValOff];
            if (static_cast<uint64_t>(fValOff) + 1ULL + rlen > h.fdataBytes) { activeAncestors[structIdx] = 0u; return false; }
            valStr = BioWareCommon::DecodeBioWareString(
                std::string_view(reinterpret_cast<const char*>(fdata + fValOff + 1u), rlen));
            break;
        }
        case 12u: { // CExoLocString: uint32 totalSz + uint32 strRef + uint32 strCnt + [int32 id, uint32 len, char[]]
            if (static_cast<uint64_t>(fValOff) + 12ULL > h.fdataBytes) { activeAncestors[structIdx] = 0u; return false; }
            const uint32_t totSz  = ByteIO::RdU32LE(fdata + fValOff + 0u);
            const uint32_t strRef = ByteIO::RdU32LE(fdata + fValOff + 4u);
            const uint32_t subCnt = ByteIO::RdU32LE(fdata + fValOff + 8u);
            if (static_cast<uint64_t>(fValOff) + 4ULL + totSz > h.fdataBytes || subCnt > 256u) {
                activeAncestors[structIdx] = 0u;
                return false;
            }
            if (strRef != 0xFFFFFFFFu) {
                valStr = L"StrRef=" + std::to_wstring(strRef);
            }
            size_t sp = fValOff + 12u;
            const size_t subEnd = fValOff + 4u + totSz;
            for (uint32_t si = 0; si < subCnt && sp + 8u <= subEnd; ++si) {
                const uint32_t strId = ByteIO::RdU32LE(fdata + sp + 0u);
                const uint32_t sLen  = ByteIO::RdU32LE(fdata + sp + 4u);
                sp += 8u;
                if (sLen > subEnd - sp) { activeAncestors[structIdx] = 0u; return false; }
                std::wstring subTxt = BioWareCommon::DecodeBioWareString(
                    std::string_view(reinterpret_cast<const char*>(fdata + sp), sLen));
                sp += sLen;
                if (!valStr.empty()) valStr += L" | ";
                valStr += std::wstring(BioWareCommon::LanguageIdName(strId >> 1)) + L": " + subTxt;
            }
            break;
        }
        case 13u: { // VOID
            if (static_cast<uint64_t>(fValOff) + 4ULL > h.fdataBytes) { activeAncestors[structIdx] = 0u; return false; }
            const uint32_t blen = ByteIO::RdU32LE(fdata + fValOff);
            if (static_cast<uint64_t>(fValOff) + 4ULL + blen > h.fdataBytes) { activeAncestors[structIdx] = 0u; return false; }
            valStr = L"<" + std::to_wstring(blen) + L" bytes>";
            break;
        }
        case 16u: { // Orientation (4 floats)
            if (static_cast<uint64_t>(fValOff) + 16ULL > h.fdataBytes) { activeAncestors[structIdx] = 0u; return false; }
            float q[4]{};
            std::memcpy(q, fdata + fValOff, 16);
            swprintf_s(numBuf, L"(%.3g, %.3g, %.3g, %.3g)", q[0], q[1], q[2], q[3]);
            valStr = numBuf;
            break;
        }
        case 17u: { // Vector (3 floats)
            if (static_cast<uint64_t>(fValOff) + 12ULL > h.fdataBytes) { activeAncestors[structIdx] = 0u; return false; }
            float v[3]{};
            std::memcpy(v, fdata + fValOff, 12);
            swprintf_s(numBuf, L"(%.3g, %.3g, %.3g)", v[0], v[1], v[2]);
            valStr = numBuf;
            break;
        }
        case 18u: { // StrRef
            if (static_cast<uint64_t>(fValOff) + 4ULL > h.fdataBytes) { activeAncestors[structIdx] = 0u; return false; }
            valStr = L"StrRef=" + std::to_wstring(ByteIO::RdU32LE(fdata + fValOff));
            break;
        }
        default:
            break;
        }

        outSheet.rows.push_back({ std::move(fullPath), FieldTypeName(fType), std::move(valStr) });
    }

    activeAncestors[structIdx] = 0u;
    return true;
}

} // namespace DetailBioWareGff

inline bool ParseBioWareGffStruct(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    if (raw.size() < 68u || raw.size() > (64u << 20)) return false;

    const uint8_t* data = raw.data();
    const size_t   size = raw.size();

    // Validate 4-char printable FileType + "V3.2" or "V3.3" FileVersion
    for (int i = 0; i < 4; ++i) {
        const uint8_t c = data[i];
        if (c < 0x20u || c > 0x7Eu) return false;
    }
    if (std::memcmp(data + 4u, "V3.2", 4) != 0 && std::memcmp(data + 4u, "V3.3", 4) != 0) {
        return false;
    }

    DetailBioWareGff::GffHeader h{};
    std::memcpy(h.fileType, data + 0u, 4u);
    std::memcpy(h.fileVer,  data + 4u, 4u);
    h.structOff  = ByteIO::RdU32LE(data + 0x08u);
    h.structCnt  = ByteIO::RdU32LE(data + 0x0Cu);
    h.fieldOff   = ByteIO::RdU32LE(data + 0x10u);
    h.fieldCnt   = ByteIO::RdU32LE(data + 0x14u);
    h.labelOff   = ByteIO::RdU32LE(data + 0x18u);
    h.labelCnt   = ByteIO::RdU32LE(data + 0x1Cu);
    h.fdataOff   = ByteIO::RdU32LE(data + 0x20u);
    h.fdataBytes = ByteIO::RdU32LE(data + 0x24u);
    h.findxOff   = ByteIO::RdU32LE(data + 0x28u);
    h.findxBytes = ByteIO::RdU32LE(data + 0x2Cu);
    h.lindxOff   = ByteIO::RdU32LE(data + 0x30u);
    h.lindxBytes = ByteIO::RdU32LE(data + 0x34u);

    if (h.structCnt == 0u || h.structCnt > 524288u || h.fieldCnt == 0u || h.labelCnt == 0u) {
        return false;
    }
    auto checkRegion = [&](uint32_t off, uint64_t byteLen) -> bool {
        if (byteLen == 0ULL) return true;
        return off >= 56u && static_cast<uint64_t>(off) + byteLen <= size;
    };
    if (!checkRegion(h.structOff, static_cast<uint64_t>(h.structCnt) * 12ULL) ||
        !checkRegion(h.fieldOff,  static_cast<uint64_t>(h.fieldCnt)  * 12ULL) ||
        !checkRegion(h.labelOff,  static_cast<uint64_t>(h.labelCnt)  * 16ULL) ||
        !checkRegion(h.fdataOff,  h.fdataBytes) ||
        !checkRegion(h.findxOff,  h.findxBytes) ||
        !checkRegion(h.lindxOff,  h.lindxBytes)) {
        return false;
    }

    SpreadsheetSheet sheet;
    sheet.name = L"GFF_Fields";
    sheet.rows.push_back({ L"FieldPath", L"Type", L"Value" });

    std::vector<uint8_t> activeAncestors(h.structCnt, 0u);
    if (!DetailBioWareGff::WalkGffStruct(data, h, 0u, L"", 0, activeAncestors, sheet) ||
        sheet.rows.size() <= 1u) {
        return false;
    }

    std::wstring typeW = PathUtils::Utf8ToWide(h.fileType);
    while (!typeW.empty() && typeW.back() == L' ') typeW.pop_back();
    std::wstring verW = PathUtils::Utf8ToWide(h.fileVer);

    wchar_t title[180] = {};
    swprintf_s(title, L"BioWare GFF Hierarchy (%ls %ls, %u structs, %u fields)",
               typeW.c_str(), verW.c_str(), h.structCnt, h.fieldCnt);
    return BioWareCommon::EmitSpreadsheetDocument(outDoc, title, std::move(sheet));
}

} // namespace Formats
