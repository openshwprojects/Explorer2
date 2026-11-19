#pragma once
// WindowsMinidumpDmp.h — Windows Crash Minidump (.dmp / .mdmp — MDMP)

namespace Formats {

// ============================================================================
// 41. Windows Crash Minidump (.dmp / .mdmp — MDMP)
// ============================================================================
inline bool ParseWindowsMinidump(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 32 || std::memcmp(raw.data(), "MDMP", 4) != 0) return false;

    const uint16_t ver        = DetailSys100::RdU16LE(raw.data() + 4);
    const uint32_t numStreams = DetailSys100::RdU32LE(raw.data() + 8);
    const uint32_t dirRva     = DetailSys100::RdU32LE(raw.data() + 12);
    const uint32_t timeStamp  = DetailSys100::RdU32LE(raw.data() + 20);
    const uint64_t flags      = DetailSys100::RdU64LE(raw.data() + 24);

    SpreadsheetSheet dirSh;
    dirSh.name = L"Minidump_Streams";
    dirSh.maxCols = 2;
    dirSh.rows = {
        { L"Minidump Property", L"Value" },
        { L"Signature", L"MDMP v" + std::to_wstring(ver) },
        { L"Crash Timestamp", DetailSys100::FormatUnixTimeUtc(timeStamp) },
        { L"Dump Flags", DetailSys100::Hex64(flags) },
        { L"Directory Stream Count", std::to_wstring(numStreams) },
        { L"Dump File Size", PathUtils::FormatByteSize(std::max<uint64_t>(out.sourceFileSize, raw.size())) }
    };

    SpreadsheetSheet modSh;
    modSh.name = L"Minidump_Modules";
    modSh.maxCols = 3;
    modSh.rows.push_back({ L"Module Path", L"Base Address", L"Image Size" });

    if (dirRva > 0 && numStreams <= 4096 && static_cast<uint64_t>(dirRva) + static_cast<uint64_t>(numStreams) * 12ULL <= raw.size()) {
        for (uint32_t i = 0; i < std::min(numStreams, 128u); ++i) {
            const size_t dp = dirRva + static_cast<size_t>(i) * 12;
            const uint32_t stype = DetailSys100::RdU32LE(raw.data() + dp + 0);
            const uint32_t slen  = DetailSys100::RdU32LE(raw.data() + dp + 4);
            const uint32_t srva  = DetailSys100::RdU32LE(raw.data() + dp + 8);

            std::wstring sname = L"Stream Type " + std::to_wstring(stype);
            if (stype == 3) sname = L"ThreadListStream (3)";
            else if (stype == 4) sname = L"ModuleListStream (4)";
            else if (stype == 6) sname = L"ExceptionStream (6)";
            else if (stype == 7) sname = L"SystemInfoStream (7)";
            else if (stype == 8) sname = L"ThreadExListStream (8)";
            else if (stype == 9) sname = L"Memory64ListStream (9)";
            else if (stype == 15) sname = L"MiscInfoStream (15)";

            dirSh.rows.push_back({ sname, PathUtils::FormatByteSize(slen) + L" @ " + DetailSys100::Hex32(srva) });

            if (stype == 6 && slen >= 32 && static_cast<uint64_t>(srva) + 32ULL <= raw.size()) {
                const uint32_t excThread = DetailSys100::RdU32LE(raw.data() + srva + 0);
                const uint32_t excCode   = DetailSys100::RdU32LE(raw.data() + srva + 8);
                const uint64_t excAddr   = DetailSys100::RdU64LE(raw.data() + srva + 24);
                dirSh.rows.push_back({ L"Exception Code / Address", DetailSys100::Hex32(excCode) + L" @ " + DetailSys100::Hex64(excAddr) + L" (TID " + std::to_wstring(excThread) + L")" });
            } else if (stype == 4 && slen >= 4 && static_cast<uint64_t>(srva) + slen <= raw.size()) {
                const uint32_t nMods = DetailSys100::RdU32LE(raw.data() + srva);
                for (uint32_t m = 0; m < std::min(nMods, 512u); ++m) {
                    const size_t mp = srva + 4 + static_cast<size_t>(m) * 108;
                    if (mp + 108 > raw.size()) break;
                    const uint64_t baseAddr = DetailSys100::RdU64LE(raw.data() + mp + 0);
                    const uint32_t imgSz    = DetailSys100::RdU32LE(raw.data() + mp + 8);
                    const uint32_t nameRva  = DetailSys100::RdU32LE(raw.data() + mp + 20);
                    std::wstring mname = L"(module)";
                    if (static_cast<uint64_t>(nameRva) + 4ULL <= raw.size()) {
                        const uint32_t byteLen = std::min<uint32_t>(DetailSys100::RdU32LE(raw.data() + nameRva), 1024u);
                        if (static_cast<uint64_t>(nameRva) + 4ULL + byteLen <= raw.size()) {
                            mname = DetailSys100::ReadUtf16LEBounded(raw.data() + nameRva + 4, byteLen, true);
                        }
                    }
                    modSh.rows.push_back({ mname, DetailSys100::Hex64(baseAddr), PathUtils::FormatByteSize(imgSz) });
                }
            }
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Windows Crash Minidump (.dmp)";
    out.sheets.push_back(std::move(dirSh));
    if (modSh.rows.size() > 1) out.sheets.push_back(std::move(modSh));
    out.valid = true;
    return true;
}


} // namespace Formats
