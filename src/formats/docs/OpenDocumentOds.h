#pragma once
// OpenDocumentOds.h — OASIS OpenDocument Spreadsheet (.ods) Reader

namespace Formats {

inline bool ParseOpenDocumentOds(const std::wstring& fullPath, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::ExcelXlsx;
    out.isSpreadsheet = true;
    out.sheets.clear();

    std::vector<ZipDeflate::ArchiveItemMeta> zipEntries;
    if (!ZipDeflate::ListZipEntriesFromFile(fullPath, zipEntries) || zipEntries.empty()) {
        return false;
    }

    if (out.sheets.empty()) {
        std::vector<uint8_t> odsBytes;
        if (DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, L"content.xml", odsBytes)) {
            std::string_view sv(reinterpret_cast<const char*>(odsBytes.data()), odsBytes.size());

            // Guard against non-spreadsheet ODF documents (e.g. .odt text documents containing a table)
            bool hasOdfSpreadsheet = (sv.find("<office:spreadsheet") != std::string_view::npos ||
                                      sv.find(":spreadsheet") != std::string_view::npos);
            bool hasOdfTextBody = (sv.find("<office:text>") != std::string_view::npos ||
                                   sv.find("<office:text ") != std::string_view::npos);
            if (hasOdfSpreadsheet || !hasOdfTextBody) {
                std::vector<uint8_t> mimeBytes;
                if (DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, L"mimetype", mimeBytes)) {
                    std::string_view msv = DetailExcelOds::TrimAsciiSV(
                        std::string_view(reinterpret_cast<const char*>(mimeBytes.data()), mimeBytes.size()));
                    if (!msv.empty() && msv.find("opendocument.spreadsheet") == std::string_view::npos) {
                        out.valid = false;
                        return false;
                    }
                }

                out.formatTitle = L"OpenDocument Spreadsheet (.ods)";
                constexpr size_t kMaxOdsCols = 512;
                constexpr size_t kMaxOdsRows = 10000;
                constexpr size_t kMaxOdsGapRows = 64;
                constexpr size_t kMaxNonEmptyRowRepeat = 256;

                size_t tSearch = 0, tStart = 0, tGt = 0;
                bool tSelf = false;
                int tblIdx = 1;

                while (DetailExcelOds::FindXmlElemOpen(sv, "table", tSearch, tStart, tGt, tSelf) && out.sheets.size() < 128) {
                    std::string_view tblTag = sv.substr(tStart, tGt - tStart + 1);
                    if (tSelf) {
                        tSearch = tGt + 1;
                        continue;
                    }
                    size_t endTbl = 0, afterTbl = 0;
                    if (!DetailExcelOds::FindXmlElemClose(sv, "table", tGt + 1, endTbl, afterTbl)) break;
                    std::string_view tblBlk = sv.substr(tGt + 1, endTbl - (tGt + 1));

                    SpreadsheetSheet sheet;
                    std::string_view nmSv;
                    sheet.name = DetailExcelOds::GetXmlAttr(tblTag, "table:name", nmSv, true)
                        ? DetailExcelOds::DecodeSpreadsheetXml(nmSv, false)
                        : (L"Sheet" + std::to_wstring(tblIdx));
                    ++tblIdx;

                    size_t pendingEmptyRows = 0;
                    size_t rSearch = 0, rStart = 0, rGt = 0;
                    bool rSelf = false;

                    while (DetailExcelOds::FindXmlElemOpen(tblBlk, "table-row", rSearch, rStart, rGt, rSelf)) {
                        std::string_view rTag = tblBlk.substr(rStart, rGt - rStart + 1);
                        std::string_view rRepSv;
                        size_t rowRepeat = 1;
                        if (DetailExcelOds::GetXmlAttr(rTag, "table:number-rows-repeated", rRepSv, true)) {
                            int64_t rr = DetailExcelOds::ParseIntSV(rRepSv, 1);
                            if (rr > 1) rowRepeat = static_cast<size_t>(rr > 1048576LL ? 1048576LL : rr);
                        }

                        if (rSelf) {
                            pendingEmptyRows = std::min<size_t>(pendingEmptyRows + rowRepeat, 1048576u);
                            rSearch = rGt + 1;
                            continue;
                        }
                        size_t endRow = 0, afterRow = 0;
                        if (!DetailExcelOds::FindXmlElemClose(tblBlk, "table-row", rGt + 1, endRow, afterRow)) break;
                        std::string_view rowBlk = tblBlk.substr(rGt + 1, endRow - (rGt + 1));

                        std::vector<std::wstring> rowCells;
                        size_t pendingEmptyCols = 0;

                        // Walk <table:table-cell> and <table:covered-table-cell> in exact document order
                        size_t cPos = 0;
                        while (cPos < rowBlk.size()) {
                            size_t lt = rowBlk.find('<', cPos);
                            if (lt == std::string_view::npos || lt + 1 >= rowBlk.size()) break;
                            char next = rowBlk[lt + 1];
                            if (next == '!' || next == '?' || next == '/') {
                                if (next == '!' && lt + 3 < rowBlk.size() && rowBlk[lt + 2] == '-' && rowBlk[lt + 3] == '-') {
                                    size_t endCmt = rowBlk.find("-->", lt + 4);
                                    if (endCmt == std::string_view::npos) break;
                                    cPos = endCmt + 3;
                                    continue;
                                }
                                size_t gt = rowBlk.find('>', lt + 1);
                                if (gt == std::string_view::npos) break;
                                cPos = gt + 1;
                                continue;
                            }

                            size_t nameEnd = lt + 1;
                            while (nameEnd < rowBlk.size() && !DetailExcelOds::IsTagNameStop(rowBlk[nameEnd])) ++nameEnd;
                            std::string_view local = DetailExcelOds::StripXmlNsPrefix(rowBlk.substr(lt + 1, nameEnd - (lt + 1)));
                            size_t cGt = DetailExcelOds::FindTagCloseGt(rowBlk, lt);
                            if (cGt == std::string_view::npos) break;

                            if (local != "table-cell" && local != "covered-table-cell") {
                                cPos = cGt + 1;
                                continue;
                            }

                            std::string_view cTag = rowBlk.substr(lt, cGt - lt + 1);
                            size_t prev = cGt;
                            while (prev > lt + 1 && (rowBlk[prev - 1] == ' ' || rowBlk[prev - 1] == '\t' || rowBlk[prev - 1] == '\r' || rowBlk[prev - 1] == '\n')) --prev;
                            bool cSelf = (prev > lt + 1 && rowBlk[prev - 1] == '/');

                            size_t endC = cGt, afterC = cGt + 1;
                            if (!cSelf && !DetailExcelOds::FindXmlElemClose(rowBlk, local, cGt + 1, endC, afterC)) break;
                            std::string_view cBody = cSelf ? std::string_view{} : rowBlk.substr(cGt + 1, endC - (cGt + 1));

                            std::string_view cRepSv;
                            size_t colRepeat = 1;
                            if (DetailExcelOds::GetXmlAttr(cTag, "table:number-columns-repeated", cRepSv, true)) {
                                int64_t cr = DetailExcelOds::ParseIntSV(cRepSv, 1);
                                if (cr > 1) colRepeat = static_cast<size_t>(cr > 65536LL ? 65536LL : cr);
                            }

                            std::wstring cellText;
                            if (local == "table-cell") {
                                cellText = DetailExcelOds::ExtractOdfCellText(cTag, cBody);
                            }

                            // CRITICAL TRAILING-REPEAT GUARD:
                            // Only flush pendingEmptyCols when a subsequent non-empty cell appears in the same row!
                            if (cellText.empty()) {
                                pendingEmptyCols = std::min<size_t>(pendingEmptyCols + colRepeat, 65536u);
                            } else {
                                while (pendingEmptyCols > 0 && rowCells.size() < kMaxOdsCols) {
                                    rowCells.emplace_back();
                                    --pendingEmptyCols;
                                }
                                pendingEmptyCols = 0;
                                size_t avail = (rowCells.size() < kMaxOdsCols) ? (kMaxOdsCols - rowCells.size()) : 0u;
                                size_t emitCnt = std::min<size_t>(colRepeat, avail);
                                for (size_t k = 0; k < emitCnt; ++k) {
                                    rowCells.push_back(cellText);
                                }
                            }
                            cPos = afterC;
                        }

                        // CRITICAL TRAILING-ROW-REPEAT GUARD:
                        // Trailing empty rows (e.g. LibreOffice Calc number-rows-repeated="1048575") stay in pendingEmptyRows
                        // and are discarded at </table:table> unless a non-empty row follows!
                        if (rowCells.empty()) {
                            pendingEmptyRows = std::min<size_t>(pendingEmptyRows + rowRepeat, 1048576u);
                        } else {
                            size_t gapToFlush = std::min<size_t>(pendingEmptyRows, kMaxOdsGapRows);
                            for (size_t g = 0; g < gapToFlush && sheet.rows.size() < kMaxOdsRows; ++g) {
                                sheet.rows.emplace_back();
                            }
                            pendingEmptyRows = 0;

                            sheet.maxCols = std::max(sheet.maxCols, rowCells.size());
                            size_t emitRows = std::min<size_t>(rowRepeat, kMaxNonEmptyRowRepeat);
                            for (size_t k = 0; k < emitRows && sheet.rows.size() < kMaxOdsRows; ++k) {
                                if (k + 1 == emitRows || sheet.rows.size() + 1 >= kMaxOdsRows) {
                                    sheet.rows.push_back(std::move(rowCells));
                                    break;
                                } else {
                                    sheet.rows.push_back(rowCells);
                                }
                            }
                        }

                        rSearch = afterRow;
                        if (sheet.rows.size() >= kMaxOdsRows) break;
                    }

                    if (!sheet.rows.empty()) {
                        out.sheets.push_back(std::move(sheet));
                    }
                    tSearch = afterTbl;
                }
            }
        }
    }

    out.valid = !out.sheets.empty();
    return out.valid;

}

} // namespace Formats
