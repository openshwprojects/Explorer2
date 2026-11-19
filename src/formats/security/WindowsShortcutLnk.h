#pragma once
// WindowsShortcutLnk.h — MS-SHLLINK Windows Shell Shortcut (.lnk)

namespace Formats {

// ============================================================================
// 38. MS-SHLLINK Windows Shell Shortcut (.lnk)
// ============================================================================
inline bool ParseWindowsLnkShortcut(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    static const uint8_t kShlLinkClsid[16] = {
        0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46
    };
    if (raw.size() < 76 || DetailSys100::RdU32LE(raw.data()) != 0x0000004Cu) return false;
    if (std::memcmp(raw.data() + 4, kShlLinkClsid, 16) != 0) return false;

    const uint32_t linkFlags = DetailSys100::RdU32LE(raw.data() + 20);
    const uint32_t fileAttrs = DetailSys100::RdU32LE(raw.data() + 24);
    const uint64_t tCreate   = DetailSys100::RdU64LE(raw.data() + 28);
    const uint64_t tAccess   = DetailSys100::RdU64LE(raw.data() + 36);
    const uint64_t tWrite    = DetailSys100::RdU64LE(raw.data() + 44);
    const uint32_t fileSize  = DetailSys100::RdU32LE(raw.data() + 52);
    const int32_t  iconIdx   = static_cast<int32_t>(DetailSys100::RdU32LE(raw.data() + 56));
    const uint32_t showCmd   = DetailSys100::RdU32LE(raw.data() + 60);
    const uint16_t hotKey    = DetailSys100::RdU16LE(raw.data() + 64);

    std::wstring flagDesc;
    if (linkFlags & 0x0001u) flagDesc += L"HasTargetIDList ";
    if (linkFlags & 0x0002u) flagDesc += L"HasLinkInfo ";
    if (linkFlags & 0x0004u) flagDesc += L"HasName ";
    if (linkFlags & 0x0008u) flagDesc += L"HasRelativePath ";
    if (linkFlags & 0x0010u) flagDesc += L"HasWorkingDir ";
    if (linkFlags & 0x0020u) flagDesc += L"HasArguments ";
    if (linkFlags & 0x0040u) flagDesc += L"HasIconLocation ";
    if (linkFlags & 0x0080u) flagDesc += L"IsUnicode ";
    if (linkFlags & 0x2000u) flagDesc += L"RunAsUser ";

    std::wstring attrDesc;
    if (fileAttrs & 0x01u) attrDesc += L"READONLY ";
    if (fileAttrs & 0x02u) attrDesc += L"HIDDEN ";
    if (fileAttrs & 0x04u) attrDesc += L"SYSTEM ";
    if (fileAttrs & 0x10u) attrDesc += L"DIRECTORY ";
    if (fileAttrs & 0x20u) attrDesc += L"ARCHIVE ";

    std::wstring showStr = std::to_wstring(showCmd);
    if (showCmd == 1) showStr = L"SW_SHOWNORMAL (1)";
    else if (showCmd == 3) showStr = L"SW_SHOWMAXIMIZED (3)";
    else if (showCmd == 7) showStr = L"SW_SHOWMINNOACTIVE (7)";

    std::wstring hkStr = L"None";
    if (hotKey != 0) {
        const uint8_t vk = static_cast<uint8_t>(hotKey & 0xFFu);
        const uint8_t mod = static_cast<uint8_t>((hotKey >> 8) & 0xFFu);
        hkStr.clear();
        if (mod & 0x02u) hkStr += L"Ctrl+";
        if (mod & 0x04u) hkStr += L"Alt+";
        if (mod & 0x01u) hkStr += L"Shift+";
        if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) hkStr.push_back(static_cast<wchar_t>(vk));
        else hkStr += L"VK_" + std::to_wstring(vk);
    }

    std::vector<std::vector<std::wstring>> rows = {
        { L"Field", L"Value" },
        { L"Format", L"MS-SHLLINK Windows Shell Shortcut (.lnk)" },
        { L"Link Flags", DetailSys100::Hex32(linkFlags) + (flagDesc.empty() ? L"" : (L" (" + PathUtils::Trim(flagDesc) + L")")) },
        { L"Target Attributes", attrDesc.empty() ? DetailSys100::Hex32(fileAttrs) : PathUtils::Trim(attrDesc) },
        { L"Creation Time", DetailSys100::FormatFileTimeUtc(tCreate) },
        { L"Access Time", DetailSys100::FormatFileTimeUtc(tAccess) },
        { L"Write Time", DetailSys100::FormatFileTimeUtc(tWrite) },
        { L"Target File Size", PathUtils::FormatByteSize(fileSize) },
        { L"Icon Index", std::to_wstring(iconIdx) },
        { L"Show Command", showStr },
        { L"HotKey", hkStr }
    };

    size_t pos = 76;

    // 1. LinkTargetIDList
    if ((linkFlags & 0x0001u) != 0 && pos + 2 <= raw.size()) {
        const uint16_t idListSize = DetailSys100::RdU16LE(raw.data() + pos);
        pos += 2;
        if (pos + idListSize <= raw.size()) {
            size_t ip = pos;
            const size_t idEnd = pos + idListSize;
            int itemCnt = 0;
            std::wstring idPath;
            while (ip + 2 <= idEnd) {
                const uint16_t itemSz = DetailSys100::RdU16LE(raw.data() + ip);
                if (itemSz == 0) break;
                if (itemSz < 2 || ip + itemSz > idEnd) break;
                const uint8_t typeByte = (itemSz >= 3) ? raw[ip + 2] : 0;
                if (typeByte == 0x2F && itemSz >= 6) {
                    std::string drv = DetailSys100::ReadCStrBounded(raw, ip + 3, itemSz - 3);
                    if (!drv.empty()) idPath += PathUtils::Utf8ToWide(drv);
                } else if ((typeByte & 0x70u) == 0x30u && itemSz >= 15) {
                    std::string shortNm = DetailSys100::ReadCStrBounded(raw, ip + 14, itemSz - 14);
                    if (!shortNm.empty()) {
                        if (!idPath.empty() && idPath.back() != L'\\') idPath.push_back(L'\\');
                        idPath += PathUtils::Utf8ToWide(shortNm);
                    }
                }
                ++itemCnt;
                ip += itemSz;
            }
            rows.push_back({ L"IDList Items", std::to_wstring(itemCnt) + (idPath.empty() ? L"" : (L" (" + idPath + L")")) });
            pos += idListSize;
        }
    }

    // 2. LinkInfo
    if ((linkFlags & 0x0002u) != 0 && pos + 28 <= raw.size()) {
        const uint32_t liSize    = DetailSys100::RdU32LE(raw.data() + pos + 0);
        const uint32_t liHdrSize = DetailSys100::RdU32LE(raw.data() + pos + 4);
        const uint32_t liFlags   = DetailSys100::RdU32LE(raw.data() + pos + 8);
        const uint32_t volOff    = DetailSys100::RdU32LE(raw.data() + pos + 12);
        const uint32_t baseOff   = DetailSys100::RdU32LE(raw.data() + pos + 16);
        const uint32_t netOff    = DetailSys100::RdU32LE(raw.data() + pos + 20);
        const uint32_t sufOff    = DetailSys100::RdU32LE(raw.data() + pos + 24);

        if (liSize >= 28 && pos + liSize <= raw.size()) {
            if ((liFlags & 0x01u) != 0) {
                if (volOff >= 28 && volOff + 16 <= liSize) {
                    const uint32_t drvSerial = DetailSys100::RdU32LE(raw.data() + pos + volOff + 8);
                    const uint32_t lblOff    = DetailSys100::RdU32LE(raw.data() + pos + volOff + 12);
                    std::wstring volLbl;
                    if (lblOff < liSize - volOff) {
                        volLbl = PathUtils::Utf8ToWide(DetailSys100::ReadCStrBounded(raw, pos + volOff + lblOff, liSize - volOff - lblOff));
                    }
                    rows.push_back({ L"Volume Serial / Label", DetailSys100::Hex32(drvSerial) + (volLbl.empty() ? L"" : (L" (" + volLbl + L")")) });
                }
                std::wstring localBase;
                if (liHdrSize >= 0x24 && pos + 32 <= raw.size()) {
                    const uint32_t baseUniOff = DetailSys100::RdU32LE(raw.data() + pos + 28);
                    if (baseUniOff > 0 && baseUniOff < liSize) {
                        localBase = DetailSys100::ReadUtf16LEBounded(raw.data() + pos + baseUniOff, liSize - baseUniOff, true);
                    }
                }
                if (localBase.empty() && baseOff > 0 && baseOff < liSize) {
                    localBase = PathUtils::Utf8ToWide(DetailSys100::ReadCStrBounded(raw, pos + baseOff, liSize - baseOff));
                }
                if (sufOff > 0 && sufOff < liSize) {
                    std::wstring suf = PathUtils::Utf8ToWide(DetailSys100::ReadCStrBounded(raw, pos + sufOff, liSize - sufOff));
                    if (!suf.empty()) localBase += suf;
                }
                if (!localBase.empty()) rows.push_back({ L"Local Base Path", localBase });
            }
            if ((liFlags & 0x02u) != 0 && netOff >= 28 && netOff + 20 <= liSize) {
                const uint32_t netNameOff = DetailSys100::RdU32LE(raw.data() + pos + netOff + 8);
                if (netNameOff < liSize - netOff) {
                    std::wstring unc = PathUtils::Utf8ToWide(DetailSys100::ReadCStrBounded(raw, pos + netOff + netNameOff, liSize - netOff - netNameOff));
                    if (sufOff > 0 && sufOff < liSize) {
                        std::wstring suf = PathUtils::Utf8ToWide(DetailSys100::ReadCStrBounded(raw, pos + sufOff, liSize - sufOff));
                        if (!suf.empty()) {
                            if (!unc.empty() && unc.back() != L'\\' && suf.front() != L'\\') unc.push_back(L'\\');
                            unc += suf;
                        }
                    }
                    if (!unc.empty()) rows.push_back({ L"UNC Network Target", unc });
                }
            }
            pos += liSize;
        }
    }

    // 3. StringData counted strings
    const bool isUnicode = (linkFlags & 0x0080u) != 0;
    auto readCountedStr = [&](const wchar_t* label) {
        if (pos + 2 > raw.size()) return;
        const uint16_t cch = DetailSys100::RdU16LE(raw.data() + pos);
        pos += 2;
        if (isUnicode) {
            const size_t nb = static_cast<size_t>(cch) * 2;
            if (pos + nb > raw.size()) { pos = raw.size(); return; }
            std::wstring s = DetailSys100::ReadUtf16LEBounded(raw.data() + pos, nb, false);
            pos += nb;
            rows.push_back({ label, s });
        } else {
            if (pos + cch > raw.size()) { pos = raw.size(); return; }
            std::wstring s = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + pos), cch));
            pos += cch;
            rows.push_back({ label, s });
        }
    };
    if (linkFlags & 0x0004u) readCountedStr(L"Description (Name)");
    if (linkFlags & 0x0008u) readCountedStr(L"Relative Path");
    if (linkFlags & 0x0010u) readCountedStr(L"Working Directory");
    if (linkFlags & 0x0020u) readCountedStr(L"Command-Line Arguments");
    if (linkFlags & 0x0040u) readCountedStr(L"Icon Location");

    // 4. ExtraData blocks
    while (pos + 8 <= raw.size()) {
        const uint32_t blkSz = DetailSys100::RdU32LE(raw.data() + pos);
        if (blkSz < 8 || pos + blkSz > raw.size()) break;
        const uint32_t blkSig = DetailSys100::RdU32LE(raw.data() + pos + 4);
        if (blkSig == 0xA0000001u && blkSz >= 788) { // EnvironmentVariableDataBlock
            std::wstring envTarget = DetailSys100::ReadUtf16LEBounded(raw.data() + pos + 268, 520, true);
            if (!envTarget.empty()) rows.push_back({ L"Environment Target", envTarget });
        } else if (blkSig == 0xA0000003u && blkSz >= 96) { // TrackerDataBlock
            std::string machId = DetailSys100::ReadCStrBounded(raw, pos + 16, 16);
            if (!machId.empty()) rows.push_back({ L"Tracker Machine ID", PathUtils::Utf8ToWide(machId) });
        }
        pos += blkSz;
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Windows Shell Shortcut (.lnk)";
    SpreadsheetSheet sh;
    sh.name = L"Shortcut_Info";
    sh.maxCols = 2;
    sh.rows = std::move(rows);
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}


} // namespace Formats
