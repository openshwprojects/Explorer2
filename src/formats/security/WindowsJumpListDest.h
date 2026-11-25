#pragma once
// WindowsJumpListDest.h — Windows Automatic Destinations Jump List (.automaticdestinations-ms) Reader

namespace Formats {
namespace DetailJumpList {

inline std::wstring FormatGuidLE(const uint8_t* g) {
    wchar_t buf[64] = {};
    swprintf_s(buf, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        ::Explorer2::ByteIO::RdU32LE(g + 0),
        ::Explorer2::ByteIO::RdU16LE(g + 4),
        ::Explorer2::ByteIO::RdU16LE(g + 6),
        g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return buf;
}

inline bool IsAllZero16(const uint8_t* p) {
    for (int i = 0; i < 16; ++i) if (p[i] != 0) return false;
    return true;
}

inline bool ParseHexStreamId(const std::wstring& s, uint64_t& outId) {
    if (s.empty() || s.size() > 16) return false;
    uint64_t v = 0;
    for (wchar_t c : s) {
        uint32_t d = 0;
        if (c >= L'0' && c <= L'9') d = static_cast<uint32_t>(c - L'0');
        else if (c >= L'a' && c <= L'f') d = static_cast<uint32_t>(c - L'a' + 10);
        else if (c >= L'A' && c <= L'F') d = static_cast<uint32_t>(c - L'A' + 10);
        else return false;
        v = (v << 4) | d;
    }
    outId = v;
    return true;
}

struct EmbeddedLnkInfo {
    std::wstring streamName;
    std::wstring targetPath;
    std::wstring arguments;
    std::wstring workingDir;
    std::wstring writeTimeUtc;
    uint32_t targetSize = 0;
};

inline bool SummarizeEmbeddedLnk(const std::vector<uint8_t>& lnk, EmbeddedLnkInfo& info) {
    static const uint8_t kClsid[16] = {
        0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46
    };
    if (lnk.size() < 76 || ::Explorer2::ByteIO::RdU32LE(lnk.data()) != 0x0000004Cu) return false;
    if (std::memcmp(lnk.data() + 4, kClsid, 16) != 0) return false;

    const uint32_t linkFlags = ::Explorer2::ByteIO::RdU32LE(lnk.data() + 20);
    const uint64_t tWrite    = ::Explorer2::ByteIO::RdU64LE(lnk.data() + 44);
    info.targetSize          = ::Explorer2::ByteIO::RdU32LE(lnk.data() + 52);
    info.writeTimeUtc        = DetailBinarySysDoc::FormatFileTimeUtc(tWrite);

    size_t pos = 76;
    // 1. Skip / inspect LinkTargetIDList
    if ((linkFlags & 0x0001u) != 0) {
        if (pos + 2 > lnk.size()) return true;
        uint16_t idSz = ::Explorer2::ByteIO::RdU16LE(lnk.data() + pos);
        pos += 2;
        if (pos + idSz > lnk.size()) return true;
        pos += idSz;
    }
    // 2. LinkInfo -> LocalBasePath / UNC
    if ((linkFlags & 0x0002u) != 0 && pos + 28 <= lnk.size()) {
        uint32_t liSize    = ::Explorer2::ByteIO::RdU32LE(lnk.data() + pos + 0);
        uint32_t liHdrSize = ::Explorer2::ByteIO::RdU32LE(lnk.data() + pos + 4);
        uint32_t liFlags   = ::Explorer2::ByteIO::RdU32LE(lnk.data() + pos + 8);
        uint32_t baseOff   = ::Explorer2::ByteIO::RdU32LE(lnk.data() + pos + 16);
        uint32_t netOff    = ::Explorer2::ByteIO::RdU32LE(lnk.data() + pos + 20);
        uint32_t sufOff    = ::Explorer2::ByteIO::RdU32LE(lnk.data() + pos + 24);
        if (liSize >= 28 && pos + liSize <= lnk.size()) {
            if ((liFlags & 0x01u) != 0) {
                if (liHdrSize >= 0x24 && pos + 32 <= lnk.size()) {
                    uint32_t uniOff = ::Explorer2::ByteIO::RdU32LE(lnk.data() + pos + 28);
                    if (uniOff > 0 && uniOff < liSize) {
                        info.targetPath = DetailBinarySysDoc::ReadUtf16LEBounded(lnk.data() + pos + uniOff, liSize - uniOff, true);
                    }
                }
                if (info.targetPath.empty() && baseOff > 0 && baseOff < liSize) {
                    info.targetPath = PathUtils::Utf8ToWide(DetailBinarySysDoc::ReadCStrBounded(lnk, pos + baseOff, liSize - baseOff));
                }
                if (sufOff > 0 && sufOff < liSize) {
                    info.targetPath += PathUtils::Utf8ToWide(DetailBinarySysDoc::ReadCStrBounded(lnk, pos + sufOff, liSize - sufOff));
                }
            } else if ((liFlags & 0x02u) != 0 && netOff >= 28 && netOff + 20 <= liSize) {
                uint32_t netNameOff = ::Explorer2::ByteIO::RdU32LE(lnk.data() + pos + netOff + 8);
                if (netNameOff < liSize - netOff) {
                    info.targetPath = PathUtils::Utf8ToWide(DetailBinarySysDoc::ReadCStrBounded(lnk, pos + netOff + netNameOff, liSize - netOff - netNameOff));
                }
            }
            pos += liSize;
        }
    }
    // 3. StringData counted strings
    const bool isUni = (linkFlags & 0x0080u) != 0;
    auto readCounted = [&](std::wstring& dst) {
        if (pos + 2 > lnk.size()) return;
        uint16_t cch = ::Explorer2::ByteIO::RdU16LE(lnk.data() + pos);
        pos += 2;
        if (isUni) {
            size_t nb = static_cast<size_t>(cch) * 2u;
            if (pos + nb > lnk.size()) { pos = lnk.size(); return; }
            dst = DetailBinarySysDoc::ReadUtf16LEBounded(lnk.data() + pos, nb, false);
            pos += nb;
        } else {
            if (pos + cch > lnk.size()) { pos = lnk.size(); return; }
            dst = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(lnk.data() + pos), cch));
            pos += cch;
        }
    };
    std::wstring tmp;
    if (linkFlags & 0x0004u) readCounted(tmp); // Name
    if (linkFlags & 0x0008u) { readCounted(tmp); if (info.targetPath.empty()) info.targetPath = tmp; } // RelativePath
    if (linkFlags & 0x0010u) readCounted(info.workingDir);
    if (linkFlags & 0x0020u) readCounted(info.arguments);
    return true;
}

} // namespace DetailJumpList

inline bool ParseWindowsJumpListDest(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    ::Explorer2::Ole2::CompoundFile cf;
    if (!::Explorer2::Ole2::ParseCompoundFile(raw.data(), raw.size(), cf)) return false;

    const ::Explorer2::Ole2::DirEntry* destEntry = ::Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"DestList");
    if (!destEntry) return false;

    std::vector<uint8_t> dl;
    if (!::Explorer2::Ole2::ExtractStream(cf, *destEntry, dl, 16u * 1024u * 1024u) || dl.size() < 32) {
        return false;
    }

    // Extract & summarize all hex-named MS-SHLLINK streams inside the OLE2 Jump List
    std::map<uint64_t, DetailJumpList::EmbeddedLnkInfo> lnkMap;
    SpreadsheetSheet lnkSh;
    lnkSh.name = L"JumpList_LNK_Streams";
    lnkSh.rows.push_back({ L"Stream Name (Hex)", L"Entry ID", L"Resolved Target Path", L"Arguments", L"Working Dir", L"Target Write Time (UTC)", L"Target Size" });
    for (const auto& e : cf.entries) {
        if (e.objectType != 2 || e.name.empty()) continue;
        uint64_t eid = 0;
        if (!DetailJumpList::ParseHexStreamId(e.name, eid)) continue;
        std::vector<uint8_t> sbytes;
        if (!::Explorer2::Ole2::ExtractStream(cf, e, sbytes, 2u * 1024u * 1024u)) continue;
        DetailJumpList::EmbeddedLnkInfo info;
        info.streamName = e.name;
        if (DetailJumpList::SummarizeEmbeddedLnk(sbytes, info)) {
            lnkSh.rows.push_back({
                e.name,
                std::to_wstring(eid),
                info.targetPath,
                info.arguments,
                info.workingDir,
                info.writeTimeUtc,
                PathUtils::FormatByteSize(info.targetSize)
            });
            lnkMap[eid] = std::move(info);
        }
    }

    // Parse DestList MRU/MFU stream header (32 bytes)
    const uint32_t dlVersion   = ::Explorer2::ByteIO::RdU32LE(dl.data() + 0);
    const uint32_t dlEntries   = ::Explorer2::ByteIO::RdU32LE(dl.data() + 4);
    const uint32_t dlPinned    = ::Explorer2::ByteIO::RdU32LE(dl.data() + 8);
    const uint64_t dlLastId    = ::Explorer2::ByteIO::RdU64LE(dl.data() + 16);
    const uint64_t dlActions   = ::Explorer2::ByteIO::RdU64LE(dl.data() + 24);
    if (dlVersion < 1 || dlVersion > 10) return false;

    const bool isWin10Layout = (dlVersion >= 3);
    const size_t fixedHdrLen = isWin10Layout ? 130u : 114u;

    SpreadsheetSheet destSh;
    destSh.name = L"JumpList_DestList";
    destSh.rows.push_back({
        L"#", L"Entry ID (Hex)", L"Pin Status", L"Access Count",
        L"Last Recorded Access (UTC)", L"Hostname", L"DestList Path / URI",
        L"Cross-Ref LNK Target", L"Volume / Object Droid GUIDs"
    });

    size_t pos = 32;
    size_t parsedEntries = 0;
    while (pos + fixedHdrLen <= dl.size() && parsedEntries < 5000) {
        const uint8_t* ep = dl.data() + pos;
        std::wstring host = DetailMediaAndScoreDoc::FixedAsciiField(ep + 72, 16);
        std::wstring guids;
        if (!DetailJumpList::IsAllZero16(ep + 8))  guids += L"Vol=" + DetailJumpList::FormatGuidLE(ep + 8);
        if (!DetailJumpList::IsAllZero16(ep + 24)) {
            if (!guids.empty()) guids += L" ";
            guids += L"Obj=" + DetailJumpList::FormatGuidLE(ep + 24);
        }

        uint64_t entryId = 0;
        std::wstring accessCntStr;
        uint64_t lastFt = 0;
        uint32_t pinVal = 0xFFFFFFFFu;
        uint16_t pathChars = 0;

        if (isWin10Layout) {
            entryId      = ::Explorer2::ByteIO::RdU32LE(ep + 88);
            lastFt       = ::Explorer2::ByteIO::RdU64LE(ep + 100);
            pinVal       = ::Explorer2::ByteIO::RdU32LE(ep + 108);
            accessCntStr = std::to_wstring(::Explorer2::ByteIO::RdU32LE(ep + 116));
            pathChars    = ::Explorer2::ByteIO::RdU16LE(ep + 128);
        } else {
            entryId      = ::Explorer2::ByteIO::RdU64LE(ep + 88);
            accessCntStr = AiDataDetail::FmtDouble(::Explorer2::ByteIO::RdF32LE(ep + 96));
            lastFt       = ::Explorer2::ByteIO::RdU64LE(ep + 100);
            pinVal       = ::Explorer2::ByteIO::RdU32LE(ep + 108);
            pathChars    = ::Explorer2::ByteIO::RdU16LE(ep + 112);
        }

        const size_t pathBytes = static_cast<size_t>(pathChars) * 2u;
        if (fixedHdrLen + pathBytes > dl.size() - pos) break;
        std::wstring pathStr = DetailBinarySysDoc::ReadUtf16LEBounded(ep + fixedHdrLen, pathBytes, false);
        pos += fixedHdrLen + pathBytes;
        if (isWin10Layout && pos + 4 <= dl.size() && ::Explorer2::ByteIO::RdU32LE(dl.data() + pos) == 0u) {
            pos += 4; // Win10/11 4-byte zero trailer
        }

        wchar_t hexId[24] = {};
        swprintf_s(hexId, L"%llx", static_cast<unsigned long long>(entryId));

        std::wstring lnkTarget;
        auto itLnk = lnkMap.find(entryId);
        if (itLnk != lnkMap.end()) {
            lnkTarget = itLnk->second.targetPath;
            if (!itLnk->second.arguments.empty()) {
                lnkTarget += lnkTarget.empty() ? itLnk->second.arguments : (L" " + itLnk->second.arguments);
            }
        }

        ++parsedEntries;
        destSh.rows.push_back({
            std::to_wstring(parsedEntries),
            hexId,
            (pinVal == 0xFFFFFFFFu) ? L"Unpinned" : (L"Pinned #" + std::to_wstring(pinVal)),
            accessCntStr,
            DetailBinarySysDoc::FormatFileTimeUtc(lastFt),
            host,
            pathStr,
            lnkTarget,
            guids
        });
    }

    SpreadsheetSheet sumSh;
    sumSh.name = L"JumpList_Summary";
    sumSh.rows.push_back({ L"Jump List Forensics Field", L"Value" });
    sumSh.rows.push_back({ L"DestList Format Version",
        L"v" + std::to_wstring(dlVersion) + (isWin10Layout ? L" (Windows 10 / 11 — 130B entries)" : L" (Windows 7 / 8 — 114B entries)") });
    sumSh.rows.push_back({ L"Declared Entries / Decoded", std::to_wstring(dlEntries) + L" / " + std::to_wstring(parsedEntries) });
    sumSh.rows.push_back({ L"Pinned Entries", std::to_wstring(dlPinned) });
    sumSh.rows.push_back({ L"Last Issued Entry ID", std::to_wstring(dlLastId) });
    sumSh.rows.push_back({ L"Total Recorded Actions", std::to_wstring(dlActions) });
    sumSh.rows.push_back({ L"Embedded LNK Streams", std::to_wstring(lnkMap.size()) });
    FinalizeSheetCols(sumSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Windows Jump List AutomaticDestinations (.automaticdestinations-ms)";
    if (destSh.rows.size() > 1) { FinalizeSheetCols(destSh); out.sheets.push_back(std::move(destSh)); }
    if (lnkSh.rows.size() > 1)  { FinalizeSheetCols(lnkSh);  out.sheets.push_back(std::move(lnkSh));  }
    out.sheets.push_back(std::move(sumSh));
    out.valid = true;
    return true;
}

} // namespace Formats
