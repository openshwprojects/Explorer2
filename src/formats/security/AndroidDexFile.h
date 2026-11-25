#pragma once
// AndroidDexFile.h — Android Dalvik Executable (.dex)

namespace Formats {

// ============================================================================
// 44. Android Dalvik Executable (.dex — dex\n035\0 .. 041\0)
// ============================================================================
inline bool ParseAndroidDexFile(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 112 || std::memcmp(raw.data(), "dex\n", 4) != 0 || raw[7] != 0) return false;
    const uint32_t hdrSize   = DetailBinarySysDoc::RdU32LE(raw.data() + 36);
    const uint32_t endianTag = DetailBinarySysDoc::RdU32LE(raw.data() + 40);
    if (hdrSize != 0x70u || endianTag != 0x12345678u) return false;

    const std::wstring dexVer = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + 4), 3));
    const uint32_t strCnt   = DetailBinarySysDoc::RdU32LE(raw.data() + 56);
    const uint32_t strOff   = DetailBinarySysDoc::RdU32LE(raw.data() + 60);
    const uint32_t typCnt   = DetailBinarySysDoc::RdU32LE(raw.data() + 64);
    const uint32_t typOff   = DetailBinarySysDoc::RdU32LE(raw.data() + 68);
    const uint32_t prtCnt   = DetailBinarySysDoc::RdU32LE(raw.data() + 72);
    const uint32_t prtOff   = DetailBinarySysDoc::RdU32LE(raw.data() + 76);
    const uint32_t fldCnt   = DetailBinarySysDoc::RdU32LE(raw.data() + 80);
    const uint32_t mthCnt   = DetailBinarySysDoc::RdU32LE(raw.data() + 88);
    const uint32_t mthOff   = DetailBinarySysDoc::RdU32LE(raw.data() + 92);
    const uint32_t clsCnt   = DetailBinarySysDoc::RdU32LE(raw.data() + 96);
    const uint32_t clsOff   = DetailBinarySysDoc::RdU32LE(raw.data() + 100);

    auto getDexStr = [&](uint32_t idx) -> std::wstring {
        if (idx >= strCnt || static_cast<uint64_t>(strOff) + static_cast<uint64_t>(idx) * 4ULL + 4ULL > raw.size()) return L"";
        size_t p = DetailBinarySysDoc::RdU32LE(raw.data() + strOff + idx * 4);
        if (p >= raw.size()) return L"";
        // Skip ULEB128 utf16_size
        for (int k = 0; k < 5 && p < raw.size(); ++k) {
            if ((raw[p++] & 0x80u) == 0) break;
        }
        std::string s;
        while (p < raw.size() && raw[p] != 0 && s.size() < 512) {
            if (raw[p] == 0xC0 && p + 1 < raw.size() && raw[p + 1] == 0x80) { p += 2; continue; }
            s.push_back(static_cast<char>(raw[p++]));
        }
        return PathUtils::Utf8ToWide(s);
    };

    auto prettyDesc = [](std::wstring d) -> std::wstring {
        int arr = 0;
        while (!d.empty() && d[0] == L'[') { ++arr; d.erase(d.begin()); }
        std::wstring base = d;
        if (d == L"V") base = L"void";
        else if (d == L"Z") base = L"boolean";
        else if (d == L"B") base = L"byte";
        else if (d == L"S") base = L"short";
        else if (d == L"C") base = L"char";
        else if (d == L"I") base = L"int";
        else if (d == L"J") base = L"long";
        else if (d == L"F") base = L"float";
        else if (d == L"D") base = L"double";
        else if (d.size() > 2 && d.front() == L'L' && d.back() == L';') {
            base = d.substr(1, d.size() - 2);
            for (wchar_t& c : base) if (c == L'/') c = L'.';
        }
        for (int i = 0; i < arr; ++i) base += L"[]";
        return base;
    };

    auto getDexType = [&](uint32_t tIdx) -> std::wstring {
        if (tIdx >= typCnt || static_cast<uint64_t>(typOff) + static_cast<uint64_t>(tIdx) * 4ULL + 4ULL > raw.size()) return L"";
        const uint32_t sIdx = DetailBinarySysDoc::RdU32LE(raw.data() + typOff + tIdx * 4);
        return prettyDesc(getDexStr(sIdx));
    };

    auto getDexProto = [&](uint32_t pIdx) -> std::wstring {
        if (pIdx >= prtCnt || static_cast<uint64_t>(prtOff) + static_cast<uint64_t>(pIdx) * 12ULL + 12ULL > raw.size()) return L"()";
        const size_t pp = prtOff + static_cast<size_t>(pIdx) * 12;
        const uint32_t retTypeIdx = DetailBinarySysDoc::RdU32LE(raw.data() + pp + 4);
        const uint32_t paramsOff  = DetailBinarySysDoc::RdU32LE(raw.data() + pp + 8);
        std::wstring sig = L"(";
        if (paramsOff > 0 && static_cast<uint64_t>(paramsOff) + 4ULL <= raw.size()) {
            const uint32_t nParams = std::min<uint32_t>(DetailBinarySysDoc::RdU32LE(raw.data() + paramsOff), 64u);
            for (uint32_t k = 0; k < nParams && static_cast<uint64_t>(paramsOff) + 4ULL + (k + 1) * 2ULL <= raw.size(); ++k) {
                if (k > 0) sig += L", ";
                sig += getDexType(DetailBinarySysDoc::RdU16LE(raw.data() + paramsOff + 4 + k * 2));
            }
        }
        sig += L"): " + getDexType(retTypeIdx);
        return sig;
    };

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"DEX_Header";
    hdrSh.maxCols = 2;
    hdrSh.rows = {
        { L"DEX Header Field", L"Value" },
        { L"DEX Version", L"0" + dexVer },
        { L"String IDs", std::to_wstring(strCnt) },
        { L"Type IDs", std::to_wstring(typCnt) },
        { L"Proto IDs", std::to_wstring(prtCnt) },
        { L"Field IDs", std::to_wstring(fldCnt) },
        { L"Method IDs", std::to_wstring(mthCnt) },
        { L"Class Definitions", std::to_wstring(clsCnt) }
    };

    SpreadsheetSheet clsSh;
    clsSh.name = L"DEX_Classes";
    clsSh.maxCols = 4;
    clsSh.rows.push_back({ L"#", L"Class Name", L"Superclass", L"Source File" });
    for (uint32_t i = 0; i < std::min(clsCnt, 2000u); ++i) {
        const size_t cp = clsOff + static_cast<size_t>(i) * 32;
        if (cp + 32 > raw.size()) break;
        const uint32_t cIdx   = DetailBinarySysDoc::RdU32LE(raw.data() + cp + 0);
        const uint32_t supIdx = DetailBinarySysDoc::RdU32LE(raw.data() + cp + 8);
        const uint32_t srcIdx = DetailBinarySysDoc::RdU32LE(raw.data() + cp + 16);
        clsSh.rows.push_back({
            std::to_wstring(i),
            getDexType(cIdx),
            (supIdx == 0xFFFFFFFFu) ? L"(none)" : getDexType(supIdx),
            (srcIdx == 0xFFFFFFFFu) ? L"-" : getDexStr(srcIdx)
        });
    }

    SpreadsheetSheet mthSh;
    mthSh.name = L"DEX_Methods";
    mthSh.maxCols = 3;
    mthSh.rows.push_back({ L"Defining Class", L"Method Name", L"Prototype" });
    for (uint32_t i = 0; i < std::min(mthCnt, 2000u); ++i) {
        const size_t mp = mthOff + static_cast<size_t>(i) * 8;
        if (mp + 8 > raw.size()) break;
        const uint16_t cIdx = DetailBinarySysDoc::RdU16LE(raw.data() + mp + 0);
        const uint16_t pIdx = DetailBinarySysDoc::RdU16LE(raw.data() + mp + 2);
        const uint32_t nIdx = DetailBinarySysDoc::RdU32LE(raw.data() + mp + 4);
        mthSh.rows.push_back({ getDexType(cIdx), getDexStr(nIdx), getDexProto(pIdx) });
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Android Dalvik Executable (.dex)";
    out.sheets.push_back(std::move(hdrSh));
    if (clsSh.rows.size() > 1) out.sheets.push_back(std::move(clsSh));
    if (mthSh.rows.size() > 1) out.sheets.push_back(std::move(mthSh));
    out.valid = true;
    return true;
}


} // namespace Formats
