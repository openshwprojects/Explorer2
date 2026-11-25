#pragma once
// WindowsPrefetchPf.h — Windows Execution Prefetch (.pf — SCCA & MAM XPRESS_HUFF)

namespace Formats {

// ============================================================================
// 39. Windows Execution Prefetch (.pf — SCCA & Win10/11 MAM XPRESS_HUFF)
// ============================================================================
inline bool ParseWindowsPrefetch(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    const bool isMam = (raw.size() >= 12 && raw[0] == 'M' && raw[1] == 'A' && raw[2] == 'M' && (raw[3] & 0x0Fu) == 0x04u);
    const bool isRawScca = (raw.size() >= 84 && std::memcmp(raw.data() + 4, "SCCA", 4) == 0);
    if (!isMam && !isRawScca) return false;

    std::vector<uint8_t> decompressed;
    const uint8_t* scca = raw.data();
    size_t sccaLen = raw.size();

    if (isMam) {
#ifndef _WIN32
        return false;   // Windows 10+ compressed Prefetch: Xpress Huffman comes from ntdll, which Linux does not have
#else
        const uint32_t uncompSz = DetailBinarySysDoc::RdU32LE(raw.data() + 4);
        if (uncompSz < 84 || uncompSz > 32u * 1024u * 1024u) return false;

        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (!hNtdll) return false;

        using FnRtlGetCompressionWorkSpaceSize = LONG(NTAPI*)(USHORT, PULONG, PULONG);
        using FnRtlDecompressBufferEx = LONG(NTAPI*)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, PULONG, PVOID);

        auto pGetWs = reinterpret_cast<FnRtlGetCompressionWorkSpaceSize>(GetProcAddress(hNtdll, "RtlGetCompressionWorkSpaceSize"));
        auto pDecomp = reinterpret_cast<FnRtlDecompressBufferEx>(GetProcAddress(hNtdll, "RtlDecompressBufferEx"));
        if (!pGetWs || !pDecomp) return false;

        constexpr USHORT kXpressHuff = 4;
        ULONG bufWs = 0, fragWs = 0;
        if (pGetWs(kXpressHuff, &bufWs, &fragWs) != 0) return false;

        std::vector<uint8_t> ws(std::max<ULONG>(fragWs, 4096u), 0);
        decompressed.resize(uncompSz, 0);

        const size_t tryOffs[2] = { (raw[3] & 0x80u) ? size_t(12) : size_t(8), (raw[3] & 0x80u) ? size_t(8) : size_t(12) };
        bool okDecomp = false;
        for (size_t hdrOff : tryOffs) {
            if (hdrOff >= raw.size()) continue;
            ULONG finalSz = 0;
            const LONG st = pDecomp(
                kXpressHuff,
                decompressed.data(),
                static_cast<ULONG>(decompressed.size()),
                const_cast<PUCHAR>(raw.data() + hdrOff),
                static_cast<ULONG>(raw.size() - hdrOff),
                &finalSz,
                ws.data());
            if (st == 0 && finalSz >= 84 && std::memcmp(decompressed.data() + 4, "SCCA", 4) == 0) {
                decompressed.resize(finalSz);
                okDecomp = true;
                break;
            }
        }
        if (!okDecomp) return false;
        scca = decompressed.data();
        sccaLen = decompressed.size();
#endif
    }

    const uint32_t sccaVer = DetailBinarySysDoc::RdU32LE(scca + 0);
    if (sccaVer != 17 && sccaVer != 23 && sccaVer != 26 && sccaVer != 30 && sccaVer != 31) return false;

    const std::wstring exeName = DetailBinarySysDoc::ReadUtf16LEBounded(scca + 16, 60, true);
    const uint32_t pfHash      = DetailBinarySysDoc::RdU32LE(scca + 76);

    uint32_t metricsOff = 0, metricsCnt = 0, fnStringsOff = 0, fnStringsSz = 0, volsCnt = 0, runCount = 0;
    std::vector<std::wstring> runTimes;

    if (sccaLen >= 156) {
        metricsOff   = DetailBinarySysDoc::RdU32LE(scca + 84);
        metricsCnt   = DetailBinarySysDoc::RdU32LE(scca + 88);
        fnStringsOff = DetailBinarySysDoc::RdU32LE(scca + 100);
        fnStringsSz  = DetailBinarySysDoc::RdU32LE(scca + 104);
        volsCnt      = DetailBinarySysDoc::RdU32LE(scca + 112);

        if (sccaVer == 17) {
            const uint64_t ft = DetailBinarySysDoc::RdU64LE(scca + 120);
            if (ft != 0) runTimes.push_back(DetailBinarySysDoc::FormatFileTimeUtc(ft));
            runCount = DetailBinarySysDoc::RdU32LE(scca + 144);
        } else if (sccaVer == 23 && sccaLen >= 160) {
            const uint64_t ft = DetailBinarySysDoc::RdU64LE(scca + 128);
            if (ft != 0) runTimes.push_back(DetailBinarySysDoc::FormatFileTimeUtc(ft));
            runCount = DetailBinarySysDoc::RdU32LE(scca + 152);
        } else if ((sccaVer == 26 || sccaVer >= 30) && sccaLen >= 212) {
            for (int t = 0; t < 8; ++t) {
                const uint64_t ft = DetailBinarySysDoc::RdU64LE(scca + 128 + t * 8);
                if (ft != 0) runTimes.push_back(DetailBinarySysDoc::FormatFileTimeUtc(ft));
            }
            const size_t rcOff = (sccaVer >= 30 && metricsOff == 296) ? 200 : 208;
            if (rcOff + 4 <= sccaLen) runCount = DetailBinarySysDoc::RdU32LE(scca + rcOff);
        }
    }

    std::wstring verLabel = L"SCCA v" + std::to_wstring(sccaVer);
    if (sccaVer == 17) verLabel += L" (Windows XP / 2003)";
    else if (sccaVer == 23) verLabel += L" (Windows Vista / 7)";
    else if (sccaVer == 26) verLabel += L" (Windows 8 / 8.1)";
    else if (sccaVer >= 30) verLabel += L" (Windows 10 / 11)";

    SpreadsheetSheet infoSh;
    infoSh.name = L"Prefetch_Forensics";
    infoSh.maxCols = 2;
    infoSh.rows.push_back({ L"Property", L"Value" });
    infoSh.rows.push_back({ L"Container Encoding", isMam ? L"MAM XPRESS Huffman Compressed" : L"Raw Uncompressed SCCA" });
    infoSh.rows.push_back({ L"Prefetch Version", verLabel });
    infoSh.rows.push_back({ L"Executable Name", exeName });
    infoSh.rows.push_back({ L"Prefetch Hash", DetailBinarySysDoc::Hex32(pfHash) });
    infoSh.rows.push_back({ L"Execution Run Count", std::to_wstring(runCount) });
    infoSh.rows.push_back({ L"File Metrics Count", std::to_wstring(metricsCnt) });
    infoSh.rows.push_back({ L"Volumes Count", std::to_wstring(volsCnt) });
    infoSh.rows.push_back({ L"Uncompressed SCCA Size", PathUtils::FormatByteSize(sccaLen) });
    for (size_t i = 0; i < runTimes.size(); ++i) {
        infoSh.rows.push_back({ L"Last Execution #" + std::to_wstring(i + 1), runTimes[i] });
    }

    SpreadsheetSheet filesSh;
    filesSh.name = L"Referenced_Files";
    filesSh.maxCols = 2;
    filesSh.rows.push_back({ L"#", L"Loaded Module / File Path" });

    if (fnStringsOff > 0 && fnStringsSz >= 2 && static_cast<uint64_t>(fnStringsOff) < sccaLen) {
        const size_t avail = std::min<size_t>(fnStringsSz, sccaLen - fnStringsOff);
        const uint8_t* fp = scca + fnStringsOff;
        std::wstring cur;
        int idx = 1;
        for (size_t off = 0; off + 2 <= avail && filesSh.rows.size() <= 3000; off += 2) {
            const wchar_t wc = static_cast<wchar_t>(DetailBinarySysDoc::RdU16LE(fp + off));
            if (wc == 0) {
                if (!cur.empty()) {
                    filesSh.rows.push_back({ std::to_wstring(idx++), cur });
                    cur.clear();
                }
            } else if (wc >= 0x20) {
                cur.push_back(wc);
            }
        }
        if (!cur.empty() && filesSh.rows.size() <= 3000) {
            filesSh.rows.push_back({ std::to_wstring(idx++), cur });
        }
    }
    infoSh.rows.push_back({ L"Decoded File Paths", std::to_wstring(filesSh.rows.size() - 1) });

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Windows Execution Prefetch (.pf)";
    out.sheets.push_back(std::move(infoSh));
    if (filesSh.rows.size() > 1) out.sheets.push_back(std::move(filesSh));
    out.valid = true;
    return true;
}


} // namespace Formats
