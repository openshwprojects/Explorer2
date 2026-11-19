#pragma once
// WindowsEvtxLog.h — Windows XML Event Log (.evtx — ElfFile + ElfChnk + BinXML)

namespace Formats {

// ============================================================================
// 40. Windows XML Event Log (.evtx — ElfFile + ElfChnk + BinXML Records)
// ============================================================================
inline bool ParseWindowsEvtxLog(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 128 || std::memcmp(raw.data(), "ElfFile\0", 8) != 0) return false;

    const uint64_t firstChunk = DetailSys100::RdU64LE(raw.data() + 8);
    const uint64_t lastChunk  = DetailSys100::RdU64LE(raw.data() + 16);
    const uint64_t nextRecId  = DetailSys100::RdU64LE(raw.data() + 24);
    const uint16_t minVer     = DetailSys100::RdU16LE(raw.data() + 36);
    const uint16_t majVer     = DetailSys100::RdU16LE(raw.data() + 38);
    const uint16_t hdrBlkSz   = DetailSys100::RdU16LE(raw.data() + 40);
    const uint16_t numChunks  = DetailSys100::RdU16LE(raw.data() + 42);

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"EVTX_Header";
    hdrSh.maxCols = 2;
    hdrSh.rows.push_back({ L"Property", L"Value" });
    hdrSh.rows.push_back({ L"Signature", L"ElfFile\\0 (Windows Binary XML Event Log)" });
    hdrSh.rows.push_back({ L"Format Version", std::to_wstring(majVer) + L"." + std::to_wstring(minVer) });
    hdrSh.rows.push_back({ L"Chunk Range", std::to_wstring(firstChunk) + L" .. " + std::to_wstring(lastChunk) + L" (" + std::to_wstring(numChunks) + L" chunks)" });
    hdrSh.rows.push_back({ L"Next Event Record ID", std::to_wstring(nextRecId) });
    hdrSh.rows.push_back({ L"Total File Size", PathUtils::FormatByteSize(std::max<uint64_t>(out.sourceFileSize, raw.size())) });

    SpreadsheetSheet recSh;
    recSh.name = L"Event_Records";
    recSh.maxCols = 5;
    recSh.rows.push_back({ L"Record ID", L"Written Timestamp (UTC)", L"Event ID", L"Provider / Source", L"Extracted Payload Strings" });

    size_t chunkPos = (hdrBlkSz >= 128 && hdrBlkSz <= raw.size()) ? hdrBlkSz : 4096;
    size_t parsedChunks = 0;

    while (chunkPos + 512 <= raw.size() && recSh.rows.size() <= 2000) {
        if (std::memcmp(raw.data() + chunkPos, "ElfChnk\0", 8) != 0) {
            chunkPos += 65536;
            continue;
        }
        ++parsedChunks;
        const size_t chunkEnd = std::min<size_t>(raw.size(), chunkPos + 65536);
        const uint32_t freeSpaceOff = DetailSys100::RdU32LE(raw.data() + chunkPos + 48);
        const size_t recLimit = (freeSpaceOff > 512 && chunkPos + freeSpaceOff <= chunkEnd) ? (chunkPos + freeSpaceOff) : chunkEnd;

        // Pre-collect chunk NameStrings referenced from chunk string table / inline BinXML
        std::vector<std::wstring> chunkStrings;
        for (int sSlot = 0; sSlot < 64; ++sSlot) {
            uint32_t strOff = DetailSys100::RdU32LE(raw.data() + chunkPos + 128 + sSlot * 4);
            int chainGuard = 0;
            while (strOff >= 512 && chunkPos + strOff + 8 <= chunkEnd && chainGuard++ < 64) {
                const uint32_t nextOff = DetailSys100::RdU32LE(raw.data() + chunkPos + strOff);
                const uint16_t cch     = DetailSys100::RdU16LE(raw.data() + chunkPos + strOff + 6);
                if (cch > 0 && cch <= 256 && chunkPos + strOff + 8 + static_cast<size_t>(cch) * 2 <= chunkEnd) {
                    chunkStrings.push_back(DetailSys100::ReadUtf16LEBounded(raw.data() + chunkPos + strOff + 8, static_cast<size_t>(cch) * 2, false));
                }
                if (nextOff == strOff) break;
                strOff = nextOff;
            }
        }

        size_t rPos = chunkPos + 512;
        while (rPos + 28 <= recLimit && recSh.rows.size() <= 2000) {
            if (DetailSys100::RdU32LE(raw.data() + rPos) != 0x00002A2Au) break;
            const uint32_t recSz = DetailSys100::RdU32LE(raw.data() + rPos + 4);
            if (recSz < 28 || rPos + recSz > chunkEnd) break;

            const uint64_t recId   = DetailSys100::RdU64LE(raw.data() + rPos + 8);
            const uint64_t wrTime  = DetailSys100::RdU64LE(raw.data() + rPos + 16);
            const uint8_t* bx      = raw.data() + rPos + 24;
            const size_t   bxLen   = recSz - 28;

            std::wstring eventIdStr = L"-";
            std::wstring providerStr;
            std::vector<std::wstring> texts;

            // Inspect BinXML TemplateInstance (0x0C) value descriptors if present, plus UTF-16 runs
            for (size_t i = 0; i + 10 <= bxLen; ++i) {
                if (bx[i] == 0x0C && i + 14 <= bxLen) {
                    // TemplateInstance: [0x0C][u8 unk][u32 tmplId][u32 tmplDefDataOff]
                    const uint32_t tmplOff = DetailSys100::RdU32LE(bx + i + 6);
                    size_t valTblPos = i + 10;
                    const size_t recRel = (rPos + 24 + i + 10) - chunkPos;
                    if (tmplOff == recRel && valTblPos + 24 <= bxLen) {
                        const uint32_t tmplDataSz = DetailSys100::RdU32LE(bx + valTblPos + 20);
                        valTblPos += 24 + tmplDataSz;
                    }
                    if (valTblPos + 4 <= bxLen) {
                        const uint32_t numVals = DetailSys100::RdU32LE(bx + valTblPos);
                        if (numVals > 0 && numVals <= 128 && valTblPos + 4 + static_cast<size_t>(numVals) * 4 <= bxLen) {
                            size_t dPos = valTblPos + 4;
                            size_t vPos = dPos + static_cast<size_t>(numVals) * 4;
                            for (uint32_t vi = 0; vi < numVals && vPos <= bxLen; ++vi) {
                                const uint16_t vSz   = DetailSys100::RdU16LE(bx + dPos + vi * 4);
                                const uint8_t  vType = bx[dPos + vi * 4 + 2];
                                if (vPos + vSz > bxLen) break;
                                if (vType == 0x01 && vSz >= 2) { // StringType
                                    std::wstring ws = DetailSys100::ReadUtf16LEBounded(bx + vPos, vSz, true);
                                    if (!ws.empty()) {
                                        if (providerStr.empty()) providerStr = ws;
                                        else texts.push_back(ws);
                                    }
                                } else if ((vType == 0x06 || vType == 0x05) && vSz == 2 && eventIdStr == L"-") { // UInt16 EventID
                                    const uint16_t eid = DetailSys100::RdU16LE(bx + vPos);
                                    if (eid > 0) eventIdStr = std::to_wstring(eid);
                                } else if ((vType == 0x08 || vType == 0x07) && vSz == 4 && eventIdStr == L"-") {
                                    const uint32_t eid = DetailSys100::RdU32LE(bx + vPos);
                                    if (eid > 0 && eid <= 65535) eventIdStr = std::to_wstring(eid);
                                }
                                vPos += vSz;
                            }
                            break;
                        }
                    }
                }
            }

            // Scan inline BinXML ValueText (0x05, 0x01, u16 cch, UTF-16) and printable UTF-16 runs
            if (texts.empty() && providerStr.empty()) {
                for (size_t i = 0; i + 6 <= bxLen; ) {
                    if ((bx[i] == 0x05 || bx[i] == 0x45) && bx[i + 1] == 0x01) {
                        const uint16_t cch = DetailSys100::RdU16LE(bx + i + 2);
                        if (cch > 0 && cch <= 512 && i + 4 + static_cast<size_t>(cch) * 2 <= bxLen) {
                            std::wstring ws = DetailSys100::ReadUtf16LEBounded(bx + i + 4, static_cast<size_t>(cch) * 2, true);
                            if (!ws.empty()) {
                                if (providerStr.empty()) providerStr = ws;
                                else if (eventIdStr == L"-" && ws.size() <= 5 && ws[0] >= L'0' && ws[0] <= L'9') eventIdStr = ws;
                                else texts.push_back(ws);
                            }
                            i += 4 + static_cast<size_t>(cch) * 2;
                            continue;
                        }
                    }
                    // Also check UTF-16LE ASCII runs >= 4 chars
                    std::wstring run;
                    size_t j = i;
                    while (j + 2 <= bxLen && bx[j + 1] == 0 && bx[j] >= 32 && bx[j] <= 126) {
                        run.push_back(static_cast<wchar_t>(bx[j]));
                        j += 2;
                    }
                    if (run.size() >= 4) {
                        if (run != L"Event" && run != L"System" && run != L"EventData" && run != L"Data") {
                            if (providerStr.empty()) providerStr = run;
                            else texts.push_back(run);
                        }
                        i = j;
                    } else {
                        ++i;
                    }
                }
            }

            if (providerStr.empty() && !chunkStrings.empty()) providerStr = chunkStrings.front();

            std::wstring joined;
            for (size_t ti = 0; ti < texts.size() && joined.size() < 240; ++ti) {
                if (!joined.empty()) joined += L" | ";
                joined += texts[ti];
            }

            recSh.rows.push_back({
                std::to_wstring(recId),
                DetailSys100::FormatFileTimeUtc(wrTime),
                eventIdStr,
                providerStr.empty() ? L"(BinXML Event)" : providerStr,
                joined.empty() ? (std::to_wstring(recSz) + L" B BinXML") : joined
            });

            rPos += recSz;
        }

        chunkPos += 65536;
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
