#pragma once
// WindowsRegistryHive.h — Windows Registry Binary Hive (.hiv / regf — Hierarchical Key Tree, vk & db Big Data)

namespace Formats {
namespace DetailRegHive {

inline bool ReadHiveCellPayload(
    SourceBytes& src,
    uint64_t hiveSize,
    uint32_t cellRelOff,
    size_t maxPayloadBytes,
    std::vector<uint8_t>& outBuf)
{
    outBuf.clear();
    if (cellRelOff == 0xFFFFFFFFu) return false;
    const uint64_t absOff = 4096ULL + cellRelOff;
    if (absOff + 8ULL > hiveSize) return false;
    uint8_t szHdr[4] = {};
    if (!DetailBinarySysDoc::ReadSourceExact(src, absOff, szHdr, 4)) return false;
    const int32_t rawSz = DetailStructuredDataDoc::RdI32LE(szHdr);
    const int64_t absSz = (rawSz < 0) ? -static_cast<int64_t>(rawSz) : static_cast<int64_t>(rawSz);
    if (absSz <= 4 || absOff + static_cast<uint64_t>(absSz) > hiveSize) return false;
    const size_t payLen = static_cast<size_t>(std::min<uint64_t>(static_cast<uint64_t>(absSz - 4), maxPayloadBytes));
    return DetailBinarySysDoc::ReadSourceVec(src, absOff + 4ULL, payLen, outBuf);
}

inline bool ReadHiveValueDataBytes(
    SourceBytes& src,
    uint64_t hiveSize,
    uint32_t dataCellOff,
    uint32_t dataLen,
    std::vector<uint8_t>& outData)
{
    outData.clear();
    if (dataLen == 0 || dataCellOff == 0xFFFFFFFFu) return false;
    const size_t capLen = std::min<size_t>(dataLen, 8192u);
    std::vector<uint8_t> cell;
    if (!ReadHiveCellPayload(src, hiveSize, dataCellOff, std::max<size_t>(capLen, 16u), cell) || cell.empty()) {
        return false;
    }

    // Check for Windows XP+ Big Data ("db") multi-segment cell (typically used when dataLen > 16344)
    // db record: [2B "db"][u16 segCount][u32 segListCellOff]
    if (dataLen > 16344u && cell.size() >= 8u && cell[0] == 'd' && cell[1] == 'b') {
        const uint16_t segCount   = DetailStructuredDataDoc::RdU16LE(cell.data() + 2);
        const uint32_t segListOff = DetailStructuredDataDoc::RdU32LE(cell.data() + 4);
        std::vector<uint8_t> segList;
        if (segCount > 0 && segCount <= 512u &&
            ReadHiveCellPayload(src, hiveSize, segListOff, static_cast<size_t>(segCount) * 4u, segList))
        {
            outData.reserve(capLen);
            for (uint16_t s = 0; s < segCount && outData.size() < capLen && (s + 1u) * 4u <= segList.size(); ++s) {
                const uint32_t segCellOff = DetailStructuredDataDoc::RdU32LE(segList.data() + s * 4u);
                const size_t want = capLen - outData.size();
                std::vector<uint8_t> segPay;
                if (!ReadHiveCellPayload(src, hiveSize, segCellOff, std::min<size_t>(want, 16344u), segPay) || segPay.empty()) break;
                const size_t take = std::min<size_t>(want, segPay.size());
                outData.insert(outData.end(), segPay.begin(), segPay.begin() + static_cast<ptrdiff_t>(take));
            }
            return !outData.empty();
        }
    }

    outData.assign(cell.begin(), cell.begin() + static_cast<ptrdiff_t>(std::min<size_t>(cell.size(), capLen)));
    return true;
}

inline std::wstring FormatRegistryValueData(uint32_t dType, const uint8_t* dp, size_t dLen) {
    if (!dp || dLen == 0) return L"(empty)";
    if (dType == 1 || dType == 2) { // REG_SZ / REG_EXPAND_SZ
        return DetailBinarySysDoc::ReadUtf16LEBounded(dp, std::min<size_t>(dLen, 1024u), true);
    }
    if (dType == 7) { // REG_MULTI_SZ: NUL-separated UTF-16LE strings terminated by double-NUL
        std::wstring joined;
        size_t i = 0;
        const size_t lim = std::min<size_t>(dLen & ~size_t(1u), 2048u);
        while (i + 2 <= lim) {
            size_t s = i;
            while (i + 2 <= lim && DetailBinarySysDoc::RdU16LE(dp + i) != 0) i += 2;
            if (i > s) {
                std::wstring item = DetailBinarySysDoc::ReadUtf16LEBounded(dp + s, i - s, false);
                if (!item.empty()) {
                    if (!joined.empty()) joined += L" | ";
                    joined += item;
                }
            }
            i += 2;
            if (i + 2 <= lim && DetailBinarySysDoc::RdU16LE(dp + i) == 0) break;
        }
        return joined;
    }
    if (dType == 4 && dLen >= 4) { // REG_DWORD
        uint32_t v = DetailStructuredDataDoc::RdU32LE(dp);
        wchar_t b[48] = {};
        swprintf_s(b, L"0x%08X (%u)", v, v);
        return b;
    }
    if (dType == 5 && dLen >= 4) { // REG_DWORD_BIG_ENDIAN
        uint32_t v = DetailStructuredDataDoc::RdU32BE(dp);
        wchar_t b[48] = {};
        swprintf_s(b, L"0x%08X (%u)", v, v);
        return b;
    }
    if (dType == 11 && dLen >= 8) { // REG_QWORD
        uint64_t v = DetailStructuredDataDoc::RdU64LE(dp);
        wchar_t b[64] = {};
        swprintf_s(b, L"0x%016llX (%llu)", static_cast<unsigned long long>(v), static_cast<unsigned long long>(v));
        return b;
    }
    return DetailStructuredDataDoc::HexBytes(dp, dLen, 24);
}

inline std::wstring RegTypeName(uint32_t dType) {
    switch (dType) {
        case 0:  return L"REG_NONE";
        case 1:  return L"REG_SZ";
        case 2:  return L"REG_EXPAND_SZ";
        case 3:  return L"REG_BINARY";
        case 4:  return L"REG_DWORD";
        case 5:  return L"REG_DWORD_BIG_ENDIAN";
        case 6:  return L"REG_LINK";
        case 7:  return L"REG_MULTI_SZ";
        case 8:  return L"REG_RESOURCE_LIST";
        case 9:  return L"REG_FULL_RESOURCE_DESCRIPTOR";
        case 10: return L"REG_RESOURCE_REQUIREMENTS_LIST";
        case 11: return L"REG_QWORD";
        default: return L"TYPE_" + std::to_wstring(dType);
    }
}

inline void CollectSubkeyListOffsets(
    SourceBytes& src,
    uint64_t hiveSize,
    uint32_t listCellOff,
    std::vector<uint32_t>& outNkOffsets,
    int depth)
{
    if (depth > 6 || listCellOff == 0xFFFFFFFFu || outNkOffsets.size() >= 4096) return;
    std::vector<uint8_t> cell;
    if (!ReadHiveCellPayload(src, hiveSize, listCellOff, 65536, cell) || cell.size() < 4) return;
    const uint16_t cnt = DetailStructuredDataDoc::RdU16LE(cell.data() + 2);
    if ((cell[0] == 'l' && (cell[1] == 'f' || cell[1] == 'h'))) {
        for (uint16_t i = 0; i < cnt && 4u + (i + 1u) * 8u <= cell.size() && outNkOffsets.size() < 4096; ++i) {
            outNkOffsets.push_back(DetailStructuredDataDoc::RdU32LE(cell.data() + 4 + i * 8u));
        }
    } else if (cell[0] == 'l' && cell[1] == 'i') {
        for (uint16_t i = 0; i < cnt && 4u + (i + 1u) * 4u <= cell.size() && outNkOffsets.size() < 4096; ++i) {
            outNkOffsets.push_back(DetailStructuredDataDoc::RdU32LE(cell.data() + 4 + i * 4u));
        }
    } else if (cell[0] == 'r' && cell[1] == 'i') {
        for (uint16_t i = 0; i < cnt && 4u + (i + 1u) * 4u <= cell.size() && outNkOffsets.size() < 4096; ++i) {
            uint32_t subListOff = DetailStructuredDataDoc::RdU32LE(cell.data() + 4 + i * 4u);
            CollectSubkeyListOffsets(src, hiveSize, subListOff, outNkOffsets, depth + 1);
        }
    }
}

inline void WalkRegistryKeyTree(
    SourceBytes& src,
    uint64_t hiveSize,
    uint32_t nkCellOff,
    const std::wstring& parentPath,
    bool isRoot,
    std::unordered_set<uint32_t>& visitedNk,
    SpreadsheetSheet& keysSh,
    int depth)
{
    if (depth > 32 || keysSh.rows.size() >= 3000 || !visitedNk.insert(nkCellOff).second) return;
    std::vector<uint8_t> cell;
    if (!ReadHiveCellPayload(src, hiveSize, nkCellOff, 2048, cell) || cell.size() < 76) return;
    if (cell[0] != 'n' || cell[1] != 'k') return;

    const uint16_t flags      = DetailStructuredDataDoc::RdU16LE(cell.data() + 2);
    const uint64_t lastWrFt   = DetailBinarySysDoc::RdU64LE(cell.data() + 4);
    const uint32_t subCnt     = DetailStructuredDataDoc::RdU32LE(cell.data() + 20);
    const uint32_t subListOff = DetailStructuredDataDoc::RdU32LE(cell.data() + 28);
    const uint32_t valCnt     = DetailStructuredDataDoc::RdU32LE(cell.data() + 36);
    const uint32_t valListOff = DetailStructuredDataDoc::RdU32LE(cell.data() + 40);
    const uint16_t nLen       = DetailStructuredDataDoc::RdU16LE(cell.data() + 72);
    if (76u + nLen > cell.size()) return;

    std::wstring keyName = (flags & 0x20u)
        ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(cell.data() + 76), nLen))
        : DetailStructuredDataDoc::ReadUtf16LE(cell.data() + 76, nLen);

    std::wstring fullKeyPath = isRoot
        ? (L"\\" + keyName)
        : (parentPath == L"\\" ? (L"\\" + keyName) : (parentPath + L"\\" + keyName));

    keysSh.rows.push_back({
        L"Key (nk)",
        fullKeyPath,
        std::to_wstring(subCnt) + L" subkeys, " + std::to_wstring(valCnt) + L" values",
        lastWrFt > 0 ? (L"LastWrite: " + DetailBinarySysDoc::FormatFileTimeUtc(lastWrFt)) : L""
    });

    // Decode all vk values owned by this key
    if (valCnt > 0 && valCnt <= 4096u && valListOff != 0xFFFFFFFFu) {
        std::vector<uint8_t> vList;
        if (ReadHiveCellPayload(src, hiveSize, valListOff, static_cast<size_t>(valCnt) * 4u, vList)) {
            for (uint32_t vi = 0; vi < valCnt && (vi + 1u) * 4u <= vList.size() && keysSh.rows.size() < 3000; ++vi) {
                const uint32_t vkOff = DetailStructuredDataDoc::RdU32LE(vList.data() + vi * 4u);
                std::vector<uint8_t> vk;
                if (!ReadHiveCellPayload(src, hiveSize, vkOff, 1024, vk) || vk.size() < 20 || vk[0] != 'v' || vk[1] != 'k') continue;
                const uint16_t vnLen = DetailStructuredDataDoc::RdU16LE(vk.data() + 2);
                const uint32_t dLenR = DetailStructuredDataDoc::RdU32LE(vk.data() + 4);
                const uint32_t dOff  = DetailStructuredDataDoc::RdU32LE(vk.data() + 8);
                const uint32_t dType = DetailStructuredDataDoc::RdU32LE(vk.data() + 12);
                const uint16_t vFlg  = DetailStructuredDataDoc::RdU16LE(vk.data() + 16);

                std::wstring vn = L"(Default)";
                if (vnLen > 0 && 20u + vnLen <= vk.size()) {
                    vn = (vFlg & 1u)
                        ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(vk.data() + 20), vnLen))
                        : DetailStructuredDataDoc::ReadUtf16LE(vk.data() + 20, vnLen);
                }

                const bool inlineData = (dLenR & 0x80000000u) != 0;
                const uint32_t dLen   = dLenR & 0x7FFFFFFFu;
                std::wstring dVal;
                if (inlineData) {
                    uint8_t inlBytes[4] = {};
                    std::memcpy(inlBytes, vk.data() + 8, 4);
                    dVal = FormatRegistryValueData(dType, inlBytes, std::min<uint32_t>(dLen, 4u));
                } else if (dLen > 0) {
                    std::vector<uint8_t> payBytes;
                    if (ReadHiveValueDataBytes(src, hiveSize, dOff, dLen, payBytes)) {
                        dVal = FormatRegistryValueData(dType, payBytes.data(), payBytes.size());
                    }
                }
                keysSh.rows.push_back({
                    L"Value (vk)",
                    fullKeyPath + L" :: " + vn,
                    RegTypeName(dType),
                    std::move(dVal)
                });
            }
        }
    }

    // Recurse into subkeys via lf / lh / li / ri index lists
    if (subCnt > 0 && subListOff != 0xFFFFFFFFu) {
        std::vector<uint32_t> childNkOffsets;
        childNkOffsets.reserve(std::min<uint32_t>(subCnt, 512u));
        CollectSubkeyListOffsets(src, hiveSize, subListOff, childNkOffsets, 0);
        for (uint32_t childOff : childNkOffsets) {
            if (keysSh.rows.size() >= 3000) break;
            WalkRegistryKeyTree(src, hiveSize, childOff, fullKeyPath, false, visitedNk, keysSh, depth + 1);
        }
    }
}

} // namespace DetailRegHive

inline bool ParseWindowsRegistryHiveDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    if ((ext != L".hiv" && ext != L".dat") || raw.size() < 48 || std::memcmp(raw.data(), "regf", 4) != 0) {
        return false;
    }

    SourceBytes src(raw, out);
    const uint64_t hiveSize = src.Size();
    uint8_t hdr[112] = {};
    const size_t hdrRd = static_cast<size_t>(std::min<uint64_t>(hiveSize, 112ULL));
    if (!DetailBinarySysDoc::ReadSourceExact(src, 0, hdr, hdrRd)) return false;

    const uint32_t seq1     = DetailStructuredDataDoc::RdU32LE(hdr + 4);
    const uint32_t seq2     = DetailStructuredDataDoc::RdU32LE(hdr + 8);
    const uint64_t regfFt   = DetailBinarySysDoc::RdU64LE(hdr + 12);
    const uint32_t majVer   = DetailStructuredDataDoc::RdU32LE(hdr + 20);
    const uint32_t minVer   = DetailStructuredDataDoc::RdU32LE(hdr + 24);
    const uint32_t rootCell = DetailStructuredDataDoc::RdU32LE(hdr + 36);
    const uint32_t hbinsLen = DetailStructuredDataDoc::RdU32LE(hdr + 40);
    const std::wstring hiveName = (hdrRd >= 112) ? DetailStructuredDataDoc::ReadUtf16LE(hdr + 48, 64) : L"(Root Hive)";

    SpreadsheetSheet keysSh;
    keysSh.name = L"Hive_Keys_And_Values";
    keysSh.maxCols = 4;
    keysSh.rows.push_back({ L"Cell Kind", L"Hierarchical Key / Value Path", L"Subkeys / RegType", L"Decoded Data" });

    std::unordered_set<uint32_t> visitedNk;
    if (hiveSize > 4096ULL && 4096ULL + rootCell + 80ULL <= hiveSize) {
        DetailRegHive::WalkRegistryKeyTree(src, hiveSize, rootCell, L"", true, visitedNk, keysSh, 0);
    }

    // Fallback linear hbin scan ONLY if rootCell was unreachable in a carved/truncated hive fragment
    if (keysSh.rows.size() == 1 && hiveSize > 4096ULL) {
        uint64_t pos = 4096ULL;
        std::vector<uint8_t> hbinBuf;
        while (pos + 32ULL <= hiveSize && keysSh.rows.size() < 500) {
            uint8_t hbh[32] = {};
            if (!DetailBinarySysDoc::ReadSourceExact(src, pos, hbh, 32) || std::memcmp(hbh, "hbin", 4) != 0) { pos += 4096ULL; continue; }
            const uint32_t hbinSize = DetailStructuredDataDoc::RdU32LE(hbh + 8);
            if (hbinSize < 32u) break;
            const size_t rdLen = static_cast<size_t>(std::min<uint64_t>(hbinSize, hiveSize - pos));
            if (DetailBinarySysDoc::ReadSourceVec(src, pos, rdLen, hbinBuf)) {
                size_t cpos = 32u;
                while (cpos + 8u <= hbinBuf.size() && keysSh.rows.size() < 500) {
                    const int32_t rawSz = DetailStructuredDataDoc::RdI32LE(hbinBuf.data() + cpos);
                    const size_t absSz = static_cast<size_t>(rawSz < 0 ? -static_cast<int64_t>(rawSz) : rawSz);
                    if (absSz < 8u || cpos + absSz > hbinBuf.size()) break;
                    if (rawSz < 0 && absSz >= 80u && hbinBuf[cpos + 4] == 'n' && hbinBuf[cpos + 5] == 'k') {
                        const uint32_t relOff = static_cast<uint32_t>((pos - 4096ULL) + cpos);
                        DetailRegHive::WalkRegistryKeyTree(src, hiveSize, relOff, L"", visitedNk.empty(), visitedNk, keysSh, 0);
                    }
                    cpos += absSz;
                }
            }
            pos += hbinSize;
        }
    }

    if (!src.ReadFailed()) out.sourceReadFully = true;

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"Hive_Header";
    hdrSh.maxCols = 2;
    hdrSh.rows.push_back({ L"regf Field", L"Value" });
    hdrSh.rows.push_back({ L"Embedded Hive Name", hiveName.empty() ? L"(Unnamed)" : hiveName });
    hdrSh.rows.push_back({ L"Hive Format Version", std::to_wstring(majVer) + L"." + std::to_wstring(minVer) });
    hdrSh.rows.push_back({ L"Last Header Timestamp", regfFt > 0 ? DetailBinarySysDoc::FormatFileTimeUtc(regfFt) : L"-" });
    hdrSh.rows.push_back({ L"Sequence Numbers", std::to_wstring(seq1) + L" / " + std::to_wstring(seq2) + (seq1 == seq2 ? L" (Clean)" : L" (Dirty)") });
    hdrSh.rows.push_back({ L"Root Key Cell Offset", DetailBinarySysDoc::Hex32(rootCell) });
    hdrSh.rows.push_back({ L"Hive Bins Data Size", PathUtils::FormatByteSize(hbinsLen) });
    hdrSh.rows.push_back({ L"Keys Walked", std::to_wstring(visitedNk.size()) });

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Windows Registry Binary Hive (" + ext + L")";
    out.sheets.push_back(std::move(keysSh));
    out.sheets.push_back(std::move(hdrSh));
    out.valid = true;
    return true;
}

} // namespace Formats
