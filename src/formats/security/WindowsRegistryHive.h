#pragma once
// WindowsRegistryHive.h — Windows Registry Binary Hive (.hiv / regf)

namespace Formats {

inline bool ParseWindowsRegistryHiveDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #92. Windows Registry Binary Hive ("regf" — .hiv or NTUSER.DAT)
    if ((raw.size() >= 48 && std::memcmp(raw.data(), "regf", 4) == 0) && (ext == L".hiv" || ext == L".dat")) {
        uint32_t seq1     = DetailDocs200::RdU32LE(raw.data() + 4);
        uint32_t seq2     = DetailDocs200::RdU32LE(raw.data() + 8);
        uint32_t majVer   = DetailDocs200::RdU32LE(raw.data() + 20);
        uint32_t minVer   = DetailDocs200::RdU32LE(raw.data() + 24);
        uint32_t rootCell = DetailDocs200::RdU32LE(raw.data() + 36);
        std::wstring hiveName = (raw.size() >= 112) ? DetailDocs200::ReadUtf16LE(raw.data() + 48, 64) : L"(Root Hive)";

        SpreadsheetSheet keysSh;
        keysSh.name = L"Hive_Keys_And_Values";
        keysSh.maxCols = 4;
        keysSh.rows.push_back({ L"Cell Kind", L"Key / Value Name", L"Subkeys / RegType", L"Decoded Data" });

        // Walk hbin blocks starting at offset 4096
        size_t pos = 4096;
        while (pos + 32 <= raw.size() && keysSh.rows.size() < 500) {
            if (std::memcmp(raw.data() + pos, "hbin", 4) == 0) {
                uint32_t hbinSize = DetailDocs200::RdU32LE(raw.data() + pos + 8);
                if (hbinSize < 32) break;
                size_t cpos = pos + 32;
                size_t hend = std::min(raw.size(), pos + hbinSize);
                while (cpos + 8 <= hend && keysSh.rows.size() < 500) {
                    int32_t rawSz = DetailDocs200::RdI32LE(raw.data() + cpos);
                    size_t absSz = static_cast<size_t>(rawSz < 0 ? -static_cast<int64_t>(rawSz) : rawSz);
                    if (absSz < 8 || cpos + absSz > hend) break;
                    if (rawSz < 0 && absSz >= 24) { // Allocated cell
                        const uint8_t* cell = raw.data() + cpos + 4;
                        size_t cLen = absSz - 4;
                        if (cLen >= 76 && cell[0] == 'n' && cell[1] == 'k') {
                            uint16_t flags  = DetailDocs200::RdU16LE(cell + 2);
                            uint32_t subCnt = DetailDocs200::RdU32LE(cell + 20);
                            uint32_t valCnt = DetailDocs200::RdU32LE(cell + 36);
                            uint16_t nLen   = DetailDocs200::RdU16LE(cell + 72);
                            if (76u + nLen <= cLen) {
                                std::wstring kn = (flags & 0x20u)
                                    ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(cell + 76), nLen))
                                    : DetailDocs200::ReadUtf16LE(cell + 76, nLen);
                                keysSh.rows.push_back({ L"Key (nk)", kn,
                                    std::to_wstring(subCnt) + L" subkeys", std::to_wstring(valCnt) + L" values" });
                            }
                        } else if (cLen >= 20 && cell[0] == 'v' && cell[1] == 'k') {
                            uint16_t nLen  = DetailDocs200::RdU16LE(cell + 2);
                            uint32_t dLenR = DetailDocs200::RdU32LE(cell + 4);
                            uint32_t dOff  = DetailDocs200::RdU32LE(cell + 8);
                            uint32_t dType = DetailDocs200::RdU32LE(cell + 12);
                            uint16_t vFlg  = DetailDocs200::RdU16LE(cell + 16);
                            std::wstring vn = L"(Default)";
                            if (nLen > 0 && 20u + nLen <= cLen) {
                                vn = (vFlg & 1u)
                                    ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(cell + 20), nLen))
                                    : DetailDocs200::ReadUtf16LE(cell + 20, nLen);
                            }
                            bool inlineData = (dLenR & 0x80000000u) != 0;
                            uint32_t dLen = dLenR & 0x7FFFFFFFu;
                            std::wstring tName = (dType == 1) ? L"REG_SZ" : (dType == 2) ? L"REG_EXPAND_SZ" :
                                                 (dType == 3) ? L"REG_BINARY" : (dType == 4) ? L"REG_DWORD" :
                                                 (dType == 7) ? L"REG_MULTI_SZ" : (dType == 11) ? L"REG_QWORD" : (L"TYPE_" + std::to_wstring(dType));
                            std::wstring dVal;
                            if (inlineData && dType == 4) {
                                wchar_t b[32] = {}; swprintf_s(b, L"0x%08X (%u)", dOff, dOff); dVal = b;
                            } else if (!inlineData && dLen > 0 && 4096ull + dOff + 4 + dLen <= raw.size()) {
                                const uint8_t* dp = raw.data() + 4096 + dOff + 4;
                                if (dType == 1 || dType == 2 || dType == 7) dVal = DetailDocs200::ReadUtf16LE(dp, std::min<uint32_t>(dLen, 256));
                                else if (dType == 4 && dLen >= 4) dVal = std::to_wstring(DetailDocs200::RdU32LE(dp));
                                else dVal = DetailDocs200::HexBytes(dp, dLen, 16);
                            }
                            keysSh.rows.push_back({ L"Value (vk)", vn, tName, dVal });
                        }
                    }
                    cpos += absSz;
                }
                pos += hbinSize;
            } else {
                pos += 4096;
            }
        }

        SpreadsheetSheet hdrSh;
        hdrSh.name = L"Hive_Header";
        hdrSh.maxCols = 2;
        hdrSh.rows.push_back({ L"regf Field", L"Value" });
        hdrSh.rows.push_back({ L"Embedded Hive Name", hiveName.empty() ? L"(Unnamed)" : hiveName });
        hdrSh.rows.push_back({ L"Hive Format Version", std::to_wstring(majVer) + L"." + std::to_wstring(minVer) });
        hdrSh.rows.push_back({ L"Sequence Numbers", std::to_wstring(seq1) + L" / " + std::to_wstring(seq2) + (seq1 == seq2 ? L" (Clean)" : L" (Dirty)") });
        hdrSh.rows.push_back({ L"Root Key Cell Offset", L"0x" + DetailSys100::Hex32(rootCell).substr(2) });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Windows Registry Binary Hive (" + ext + L")";
        out.sheets.push_back(std::move(keysSh));
        out.sheets.push_back(std::move(hdrSh));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
