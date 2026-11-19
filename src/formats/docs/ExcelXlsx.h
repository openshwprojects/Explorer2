#pragma once
// ExcelXlsx.h — Microsoft Excel OOXML Workbook (.xlsx / .xlsm / .xltx) Reader

namespace Formats {

inline bool ParseExcelXlsx(const std::wstring& fullPath, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::ExcelXlsx;
    out.isSpreadsheet = true;
    out.formatTitle = DetailExcelOds::EndsWithWideCI(fullPath, L".xlsm")
        ? L"Microsoft Excel Macro-Enabled Workbook (.xlsm)"
        : (DetailExcelOds::EndsWithWideCI(fullPath, L".xltx")
            ? L"Microsoft Excel Template (.xltx)"
            : L"Microsoft Excel Workbook (.xlsx)");

    std::vector<ZipDeflate::ArchiveItemMeta> zipEntries;
    if (!ZipDeflate::ListZipEntriesFromFile(fullPath, zipEntries) || zipEntries.empty()) {
        return false;
    }

    // ------------------------------------------------------------------------
    // 1. Parse xl/_rels/workbook.xml.rels (Relationship Id -> Target + Type)
    // ------------------------------------------------------------------------
    struct OoxmlRelEntry {
        std::string id;
        std::wstring target;
        std::string type;
    };
    std::vector<OoxmlRelEntry> wbRels;
    std::wstring sharedStringsPath = L"xl/sharedStrings.xml";
    std::wstring stylesPath = L"xl/styles.xml";

    std::vector<uint8_t> relsBytes;
    if (DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, L"xl/_rels/workbook.xml.rels", relsBytes)) {
        std::string_view rsv(reinterpret_cast<const char*>(relsBytes.data()), relsBytes.size());
        size_t pos = 0;
        size_t tStart = 0, tGt = 0;
        bool selfClose = false;
        while (DetailExcelOds::FindXmlElemOpen(rsv, "Relationship", pos, tStart, tGt, selfClose)) {
            std::string_view tag = rsv.substr(tStart, tGt - tStart + 1);
            std::string_view idSv, tgtSv, typeSv;
            if (DetailExcelOds::GetXmlAttr(tag, "Id", idSv) && DetailExcelOds::GetXmlAttr(tag, "Target", tgtSv)) {
                DetailExcelOds::GetXmlAttr(tag, "Type", typeSv);
                std::wstring resolved = DetailExcelOds::NormalizeOoxmlTarget(tgtSv, "xl");
                if (typeSv.find("/sharedStrings") != std::string_view::npos && !resolved.empty()) {
                    sharedStringsPath = resolved;
                } else if (typeSv.find("/styles") != std::string_view::npos && !resolved.empty()) {
                    stylesPath = resolved;
                }
                wbRels.push_back({ std::string(idSv), std::move(resolved), std::string(typeSv) });
            }
            pos = tGt + 1;
        }
    }

    // ------------------------------------------------------------------------
    // 2. Parse xl/workbook.xml (date1904 + ordered <sheet name="..." r:id="..."/>)
    // ------------------------------------------------------------------------
    struct WorkbookSheetRef {
        std::wstring name;
        std::wstring zipPath;
        bool isNonWorksheet = false;
    };
    std::vector<WorkbookSheetRef> sheetRefs;
    bool is1904DateSystem = false;
    bool hasWorkbookXml = false;

    std::vector<uint8_t> wbBytes;
    if (DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, L"xl/workbook.xml", wbBytes)) {
        hasWorkbookXml = true;
        std::string_view wbsv(reinterpret_cast<const char*>(wbBytes.data()), wbBytes.size());

        size_t prStart = 0, prGt = 0;
        bool prSelf = false;
        if (DetailExcelOds::FindXmlElemOpen(wbsv, "workbookPr", 0, prStart, prGt, prSelf)) {
            std::string_view prTag = wbsv.substr(prStart, prGt - prStart + 1);
            std::string_view d1904Sv;
            if (DetailExcelOds::GetXmlAttr(prTag, "date1904", d1904Sv)) {
                d1904Sv = DetailExcelOds::TrimAsciiSV(d1904Sv);
                is1904DateSystem = (d1904Sv == "1" || DetailExcelOds::AsciiEqCI(d1904Sv, "true"));
            }
        }

        size_t pos = 0, sStart = 0, sGt = 0;
        bool sSelf = false;
        int ordIdx = 1;
        while (DetailExcelOds::FindXmlElemOpen(wbsv, "sheet", pos, sStart, sGt, sSelf) && sheetRefs.size() < 128) {
            std::string_view sTag = wbsv.substr(sStart, sGt - sStart + 1);
            std::string_view nameSv, ridSv;
            std::wstring shName = DetailExcelOds::GetXmlAttr(sTag, "name", nameSv)
                ? DetailExcelOds::DecodeSpreadsheetXml(nameSv, true)
                : (L"Sheet" + std::to_wstring(ordIdx));

            bool gotRid = DetailExcelOds::GetXmlAttr(sTag, "r:id", ridSv);
            if (!gotRid) gotRid = DetailExcelOds::GetXmlAttr(sTag, "id", ridSv, true);

            std::wstring targetPath;
            bool nonWs = false;
            if (gotRid && !ridSv.empty()) {
                for (const auto& rel : wbRels) {
                    if (rel.id == ridSv) {
                        targetPath = rel.target;
                        if (rel.type.find("chartsheet") != std::string::npos ||
                            rel.type.find("macrosheet") != std::string::npos ||
                            rel.type.find("dialogsheet") != std::string::npos)
                        {
                            nonWs = true;
                        }
                        break;
                    }
                }
            }
            if (targetPath.empty()) {
                targetPath = L"xl/worksheets/sheet" + std::to_wstring(ordIdx) + L".xml";
            }
            sheetRefs.push_back({ std::move(shName), std::move(targetPath), nonWs });
            ++ordIdx;
            pos = sGt + 1;
        }
    }

    if (!hasWorkbookXml || sheetRefs.empty()) {
        // Fallback ONLY if workbook.xml wasn't present or listed zero sheets
        for (int sIdx = 1; sIdx <= 16; ++sIdx) {
            std::wstring fbPath = L"xl/worksheets/sheet" + std::to_wstring(sIdx) + L".xml";
            std::vector<uint8_t> probe;
            if (!DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, fbPath, probe)) {
                if (sIdx == 1) continue;
                break;
            }
            sheetRefs.push_back({ L"Sheet" + std::to_wstring(sIdx), std::move(fbPath), false });
        }
    }

    // ------------------------------------------------------------------------
    // 3. Parse Shared Strings & Styles if OpenXML sheets are present
    // ------------------------------------------------------------------------
    if (!sheetRefs.empty()) {
        std::vector<std::wstring> sharedStrings;
        std::vector<uint8_t> ssBytes;
        if (DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, sharedStringsPath, ssBytes)) {
            std::string_view ssv(reinterpret_cast<const char*>(ssBytes.data()), ssBytes.size());
            size_t pos = 0, siStart = 0, siGt = 0;
            bool siSelf = false;
            while (DetailExcelOds::FindXmlElemOpen(ssv, "si", pos, siStart, siGt, siSelf)) {
                if (siSelf) {
                    sharedStrings.emplace_back();
                    pos = siGt + 1;
                } else {
                    size_t bodyEnd = 0, afterClose = 0;
                    if (!DetailExcelOds::FindXmlElemClose(ssv, "si", siGt + 1, bodyEnd, afterClose)) break;
                    sharedStrings.push_back(DetailExcelOds::ExtractOoxmlStringItemText(ssv.substr(siGt + 1, bodyEnd - (siGt + 1))));
                    pos = afterClose;
                }
            }
        }

        // Parse xl/styles.xml (<numFmts>, <cellStyleXfs>, <cellXfs>)
        struct CustomNumFmt {
            uint32_t id = 0;
            std::string code;
        };
        std::vector<CustomNumFmt> customFmts;
        std::vector<uint32_t> styleXfFmtIds;
        std::vector<DetailExcelOds::ExcelStyleFmt> cellXfs;

        std::vector<uint8_t> stBytes;
        if (DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, stylesPath, stBytes)) {
            std::string_view stsv(reinterpret_cast<const char*>(stBytes.data()), stBytes.size());

            size_t nfOpen = 0, nfGt = 0;
            bool nfSelf = false;
            if (DetailExcelOds::FindXmlElemOpen(stsv, "numFmts", 0, nfOpen, nfGt, nfSelf) && !nfSelf) {
                size_t nfEnd = 0, nfAfter = 0;
                if (DetailExcelOds::FindXmlElemClose(stsv, "numFmts", nfGt + 1, nfEnd, nfAfter)) {
                    std::string_view nfBlk = stsv.substr(nfGt + 1, nfEnd - (nfGt + 1));
                    size_t p = 0, fOpen = 0, fGt = 0;
                    bool fSelf = false;
                    while (DetailExcelOds::FindXmlElemOpen(nfBlk, "numFmt", p, fOpen, fGt, fSelf)) {
                        std::string_view fTag = nfBlk.substr(fOpen, fGt - fOpen + 1);
                        std::string_view idSv, codeSv;
                        if (DetailExcelOds::GetXmlAttr(fTag, "numFmtId", idSv) && DetailExcelOds::GetXmlAttr(fTag, "formatCode", codeSv)) {
                            uint32_t id = static_cast<uint32_t>(DetailExcelOds::ParseIntSV(idSv, 0));
                            customFmts.push_back({ id, std::string(codeSv) });
                        }
                        p = fGt + 1;
                    }
                }
            }

            size_t csxOpen = 0, csxGt = 0;
            bool csxSelf = false;
            if (DetailExcelOds::FindXmlElemOpen(stsv, "cellStyleXfs", 0, csxOpen, csxGt, csxSelf) && !csxSelf) {
                size_t csxEnd = 0, csxAfter = 0;
                if (DetailExcelOds::FindXmlElemClose(stsv, "cellStyleXfs", csxGt + 1, csxEnd, csxAfter)) {
                    std::string_view csxBlk = stsv.substr(csxGt + 1, csxEnd - (csxGt + 1));
                    size_t p = 0, xOpen = 0, xGt = 0;
                    bool xSelf = false;
                    while (DetailExcelOds::FindXmlElemOpen(csxBlk, "xf", p, xOpen, xGt, xSelf)) {
                        std::string_view xTag = csxBlk.substr(xOpen, xGt - xOpen + 1);
                        std::string_view idSv;
                        uint32_t fid = DetailExcelOds::GetXmlAttr(xTag, "numFmtId", idSv)
                            ? static_cast<uint32_t>(DetailExcelOds::ParseIntSV(idSv, 0)) : 0u;
                        styleXfFmtIds.push_back(fid);
                        if (xSelf) {
                            p = xGt + 1;
                        } else {
                            size_t xe = 0, xa = 0;
                            p = DetailExcelOds::FindXmlElemClose(csxBlk, "xf", xGt + 1, xe, xa) ? xa : (xGt + 1);
                        }
                    }
                }
            }

            size_t cxOpen = 0, cxGt = 0;
            bool cxSelf = false;
            if (DetailExcelOds::FindXmlElemOpen(stsv, "cellXfs", 0, cxOpen, cxGt, cxSelf) && !cxSelf) {
                size_t cxEnd = 0, cxAfter = 0;
                if (DetailExcelOds::FindXmlElemClose(stsv, "cellXfs", cxGt + 1, cxEnd, cxAfter)) {
                    std::string_view cxBlk = stsv.substr(cxGt + 1, cxEnd - (cxGt + 1));
                    size_t p = 0, xOpen = 0, xGt = 0;
                    bool xSelf = false;
                    while (DetailExcelOds::FindXmlElemOpen(cxBlk, "xf", p, xOpen, xGt, xSelf)) {
                        std::string_view xTag = cxBlk.substr(xOpen, xGt - xOpen + 1);
                        std::string_view idSv, applySv, xfIdSv;
                        uint32_t fid = DetailExcelOds::GetXmlAttr(xTag, "numFmtId", idSv)
                            ? static_cast<uint32_t>(DetailExcelOds::ParseIntSV(idSv, 0)) : 0u;
                        if (DetailExcelOds::GetXmlAttr(xTag, "applyNumberFormat", applySv)) {
                            std::string_view ap = DetailExcelOds::TrimAsciiSV(applySv);
                            if ((ap == "0" || DetailExcelOds::AsciiEqCI(ap, "false")) && fid == 0u &&
                                DetailExcelOds::GetXmlAttr(xTag, "xfId", xfIdSv))
                            {
                                size_t baseIdx = static_cast<size_t>(DetailExcelOds::ParseIntSV(xfIdSv, -1));
                                if (baseIdx < styleXfFmtIds.size()) fid = styleXfFmtIds[baseIdx];
                            }
                        }
                        std::string_view customCode;
                        bool hasCustom = false;
                        for (const auto& cf : customFmts) {
                            if (cf.id == fid) {
                                customCode = cf.code;
                                hasCustom = true;
                                break;
                            }
                        }
                        cellXfs.push_back(DetailExcelOds::ClassifyExcelNumberFormat(fid, customCode, hasCustom));
                        if (xSelf) {
                            p = xGt + 1;
                        } else {
                            size_t xe = 0, xa = 0;
                            p = DetailExcelOds::FindXmlElemClose(cxBlk, "xf", xGt + 1, xe, xa) ? xa : (xGt + 1);
                        }
                    }
                }
            }
        }

        // --------------------------------------------------------------------
        // 4. Parse Worksheets
        // --------------------------------------------------------------------
        constexpr int kMaxExcelCols = 512;
        constexpr size_t kMaxSheetRows = 10000;
        constexpr int64_t kMaxGapRowsPerJump = 64;

        for (const auto& sRef : sheetRefs) {
            std::vector<uint8_t> shBytes;
            if (!DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, sRef.zipPath, shBytes)) {
                continue;
            }
            if (sRef.isNonWorksheet && shBytes.empty()) continue;

            SpreadsheetSheet sheet;
            sheet.name = sRef.name;

            std::string_view sv(reinterpret_cast<const char*>(shBytes.data()), shBytes.size());
            size_t rSearch = 0, rStart = 0, rGt = 0;
            bool rSelf = false;
            int64_t expectedNextRow1 = 1;

            while (DetailExcelOds::FindXmlElemOpen(sv, "row", rSearch, rStart, rGt, rSelf)) {
                std::string_view rTag = sv.substr(rStart, rGt - rStart + 1);
                std::string_view rowNumSv;
                int64_t rowNum1 = expectedNextRow1;
                if (DetailExcelOds::GetXmlAttr(rTag, "r", rowNumSv)) {
                    int64_t parsedR = DetailExcelOds::ParseIntSV(rowNumSv, expectedNextRow1);
                    if (parsedR >= 1) rowNum1 = parsedR;
                }

                if (rSelf) {
                    rSearch = rGt + 1;
                    continue;
                }
                size_t endRow = 0, afterRow = 0;
                if (!DetailExcelOds::FindXmlElemClose(sv, "row", rGt + 1, endRow, afterRow)) break;
                std::string_view rowBlk = sv.substr(rGt + 1, endRow - (rGt + 1));

                std::vector<std::wstring> rowCells;
                size_t cSearch = 0, cStart = 0, cGt = 0;
                bool cSelf = false;
                int nextSequentialCol = 0;

                while (DetailExcelOds::FindXmlElemOpen(rowBlk, "c", cSearch, cStart, cGt, cSelf)) {
                    std::string_view cTag = rowBlk.substr(cStart, cGt - cStart + 1);
                    size_t endC = cGt, afterC = cGt + 1;
                    if (!cSelf && !DetailExcelOds::FindXmlElemClose(rowBlk, "c", cGt + 1, endC, afterC)) break;
                    std::string_view cBody = cSelf ? std::string_view{} : rowBlk.substr(cGt + 1, endC - (cGt + 1));

                    int colIdx = nextSequentialCol;
                    std::string_view refSv;
                    if (DetailExcelOds::GetXmlAttr(cTag, "r", refSv) && !refSv.empty()) {
                        // Strip '$' absolute markers if present before passing to ExcelCellRefToColIndex
                        std::string cleanRef;
                        cleanRef.reserve(refSv.size());
                        for (char rc : refSv) {
                            if (rc != '$') cleanRef.push_back(rc);
                        }
                        if (!cleanRef.empty()) {
                            colIdx = ExcelCellRefToColIndex(cleanRef);
                        }
                    }
                    nextSequentialCol = colIdx + 1;

                    std::string_view tSv, sSv;
                    DetailExcelOds::GetXmlAttr(cTag, "t", tSv);
                    tSv = DetailExcelOds::TrimAsciiSV(tSv);
                    int64_t styleIdx = DetailExcelOds::GetXmlAttr(cTag, "s", sSv)
                        ? DetailExcelOds::ParseIntSV(sSv, -1) : -1;

                    std::wstring cellVal;
                    bool hasValueElem = false;

                    if (tSv == "inlineStr") {
                        size_t isOpen = 0, isGt = 0;
                        bool isSelf = false;
                        if (DetailExcelOds::FindXmlElemOpen(cBody, "is", 0, isOpen, isGt, isSelf) && !isSelf) {
                            size_t isEnd = 0, isAfter = 0;
                            if (DetailExcelOds::FindXmlElemClose(cBody, "is", isGt + 1, isEnd, isAfter)) {
                                cellVal = DetailExcelOds::ExtractOoxmlStringItemText(cBody.substr(isGt + 1, isEnd - (isGt + 1)));
                                hasValueElem = true;
                            }
                        } else {
                            cellVal = DetailExcelOds::ExtractOoxmlStringItemText(cBody);
                            hasValueElem = !cellVal.empty();
                        }
                    } else {
                        size_t vOpen = 0, vGt = 0;
                        bool vSelf = false;
                        if (DetailExcelOds::FindXmlElemOpen(cBody, "v", 0, vOpen, vGt, vSelf) && !vSelf) {
                            size_t vEnd = 0, vAfter = 0;
                            if (DetailExcelOds::FindXmlElemClose(cBody, "v", vGt + 1, vEnd, vAfter)) {
                                hasValueElem = true;
                                std::string_view rawV = cBody.substr(vGt + 1, vEnd - (vGt + 1));
                                if (tSv == "s") {
                                    int64_t ssIdx = DetailExcelOds::ParseIntSV(rawV, -1);
                                    if (ssIdx >= 0 && static_cast<size_t>(ssIdx) < sharedStrings.size()) {
                                        cellVal = sharedStrings[static_cast<size_t>(ssIdx)];
                                    }
                                } else if (tSv == "str") {
                                    cellVal = DetailExcelOds::DecodeSpreadsheetXml(rawV, true);
                                } else if (tSv == "b") {
                                    std::string_view bv = DetailExcelOds::TrimAsciiSV(rawV);
                                    cellVal = (bv == "1" || DetailExcelOds::AsciiEqCI(bv, "true")) ? L"TRUE" : L"FALSE";
                                } else if (tSv == "e") {
                                    cellVal = DetailExcelOds::DecodeSpreadsheetXml(DetailExcelOds::TrimAsciiSV(rawV), false);
                                } else if (tSv == "d") {
                                    cellVal = DetailExcelOds::DecodeSpreadsheetXml(DetailExcelOds::TrimAsciiSV(rawV), false);
                                } else {
                                    // t="n" or omitted t -> numeric cell formatted via styles.xml
                                    DetailExcelOds::ExcelStyleFmt fmt{};
                                    if (styleIdx >= 0 && static_cast<size_t>(styleIdx) < cellXfs.size()) {
                                        fmt = cellXfs[static_cast<size_t>(styleIdx)];
                                    }
                                    cellVal = DetailExcelOds::FormatExcelNumericCell(rawV, fmt, is1904DateSystem);
                                }
                            }
                        }
                    }

                    // Formula fallback when <v> is absent on a formula cell (<f>...</f>)
                    if (!hasValueElem && cellVal.empty() && !cBody.empty()) {
                        size_t fOpen = 0, fGt = 0;
                        bool fSelf = false;
                        if (DetailExcelOds::FindXmlElemOpen(cBody, "f", 0, fOpen, fGt, fSelf) && !fSelf) {
                            size_t fEnd = 0, fAfter = 0;
                            if (DetailExcelOds::FindXmlElemClose(cBody, "f", fGt + 1, fEnd, fAfter)) {
                                std::string_view fRaw = DetailExcelOds::TrimAsciiSV(cBody.substr(fGt + 1, fEnd - (fGt + 1)));
                                if (!fRaw.empty()) {
                                    std::wstring fDec = DetailExcelOds::DecodeSpreadsheetXml(fRaw, false);
                                    if (!fDec.empty()) {
                                        cellVal = (fDec.front() == L'=') ? std::move(fDec) : (L"=" + fDec);
                                    }
                                }
                            }
                        }
                    }

                    if (!cellVal.empty() && colIdx >= 0 && colIdx < kMaxExcelCols) {
                        if (static_cast<size_t>(colIdx) >= rowCells.size()) {
                            rowCells.resize(static_cast<size_t>(colIdx) + 1);
                        }
                        rowCells[static_cast<size_t>(colIdx)] = std::move(cellVal);
                    }
                    cSearch = afterC;
                }

                if (!rowCells.empty()) {
                    if (rowNum1 > expectedNextRow1) {
                        int64_t gap = rowNum1 - expectedNextRow1;
                        if (gap > kMaxGapRowsPerJump) gap = kMaxGapRowsPerJump;
                        for (int64_t g = 0; g < gap && sheet.rows.size() < kMaxSheetRows; ++g) {
                            sheet.rows.emplace_back();
                        }
                    }
                    if (sheet.rows.size() < kMaxSheetRows) {
                        sheet.maxCols = std::max(sheet.maxCols, rowCells.size());
                        sheet.rows.push_back(std::move(rowCells));
                    }
                    expectedNextRow1 = rowNum1 + 1;
                }

                rSearch = afterRow;
                if (sheet.rows.size() >= kMaxSheetRows) break;
            }

            if (!sheet.rows.empty()) {
                out.sheets.push_back(std::move(sheet));
            }
        }
    }

    out.valid = !out.sheets.empty();
    return out.valid;
}

} // namespace Formats
