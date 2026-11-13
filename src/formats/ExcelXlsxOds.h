#pragma once

static bool ParseExcelXlsx(const std::wstring& fullPath, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::ExcelXlsx;
    out.isSpreadsheet = true;
    out.formatTitle = L"Microsoft Excel Workbook (.xlsx)";

    std::vector<ZipDeflate::ArchiveItemMeta> zipEntries;
    if (!ZipDeflate::ListZipEntriesFromFile(fullPath, zipEntries)) return false;

    // 1. Read xl/sharedStrings.xml if present
    std::vector<std::wstring> sharedStrings;
    std::vector<uint8_t> ssBytes;
    if (ZipDeflate::ExtractZipFileByName(fullPath, L"xl/sharedStrings.xml", ssBytes) && !ssBytes.empty()) {
        std::string_view sv(reinterpret_cast<const char*>(ssBytes.data()), ssBytes.size());
        size_t pos = 0;
        while ((pos = sv.find("<si", pos)) != std::string_view::npos) {
            size_t endSi = sv.find("</si>", pos);
            if (endSi == std::string_view::npos) break;
            std::string_view siBlk = sv.substr(pos, endSi - pos);
            std::wstring combined;
            size_t tPos = 0;
            while ((tPos = siBlk.find("<t", tPos)) != std::string_view::npos) {
                size_t gt = siBlk.find('>', tPos);
                if (gt == std::string_view::npos || siBlk[gt - 1] == '/') { tPos += 2; continue; }
                size_t endT = siBlk.find("</t>", gt);
                if (endT == std::string_view::npos) break;
                combined += DecodeXmlEntities(PathUtils::Utf8ToWide(siBlk.substr(gt + 1, endT - (gt + 1))));
                tPos = endT + 4;
            }
            sharedStrings.push_back(std::move(combined));
            pos = endSi + 5;
        }
    }

    // 2. Read sheet names from xl/workbook.xml
    std::vector<std::wstring> sheetNames;
    std::vector<uint8_t> wbBytes;
    if (ZipDeflate::ExtractZipFileByName(fullPath, L"xl/workbook.xml", wbBytes) && !wbBytes.empty()) {
        std::string_view sv(reinterpret_cast<const char*>(wbBytes.data()), wbBytes.size());
        size_t pos = 0;
        while ((pos = sv.find("<sheet ", pos)) != std::string_view::npos) {
            size_t gt = sv.find('>', pos);
            if (gt == std::string_view::npos) break;
            std::string_view tag = sv.substr(pos, gt - pos);
            size_t nmPos = tag.find("name=\"");
            if (nmPos != std::string_view::npos) {
                nmPos += 6;
                size_t endQ = tag.find('"', nmPos);
                if (endQ != std::string_view::npos) {
                    sheetNames.push_back(DecodeXmlEntities(PathUtils::Utf8ToWide(tag.substr(nmPos, endQ - nmPos))));
                }
            }
            pos = gt + 1;
        }
    }

    // 3. Parse worksheets xl/worksheets/sheet1.xml .. sheetN.xml
    for (int sIdx = 1; sIdx <= 16; ++sIdx) {
        wchar_t relPath[64] = {};
        swprintf_s(relPath, L"xl/worksheets/sheet%d.xml", sIdx);
        std::vector<uint8_t> shBytes;
        if (!ZipDeflate::ExtractZipFileByName(fullPath, relPath, shBytes) || shBytes.empty()) {
            if (sIdx == 1) continue;
            break;
        }

        SpreadsheetSheet sheet;
        sheet.name = (static_cast<size_t>(sIdx - 1) < sheetNames.size())
            ? sheetNames[sIdx - 1]
            : (L"Sheet" + std::to_wstring(sIdx));

        std::string_view sv(reinterpret_cast<const char*>(shBytes.data()), shBytes.size());
        size_t rPos = 0;
        while ((rPos = sv.find("<row", rPos)) != std::string_view::npos) {
            size_t rGt = sv.find('>', rPos);
            if (rGt == std::string_view::npos) break;
            if (sv[rGt - 1] == '/') { rPos = rGt + 1; continue; }
            size_t endRow = sv.find("</row>", rGt);
            if (endRow == std::string_view::npos) break;
            std::string_view rowBlk = sv.substr(rGt + 1, endRow - (rGt + 1));

            std::vector<std::wstring> rowCells;
            size_t cPos = 0;
            while ((cPos = rowBlk.find("<c", cPos)) != std::string_view::npos) {
                size_t cGt = rowBlk.find('>', cPos);
                if (cGt == std::string_view::npos) break;
                std::string_view cTag = rowBlk.substr(cPos, cGt - cPos + 1);
                bool selfClosing = (cGt > cPos && rowBlk[cGt - 1] == '/');
                size_t endC = selfClosing ? cGt : rowBlk.find("</c>", cGt);
                if (endC == std::string_view::npos) break;
                std::string_view cBody = selfClosing ? std::string_view{} : rowBlk.substr(cGt + 1, endC - (cGt + 1));

                int colIdx = static_cast<int>(rowCells.size());
                size_t refPos = cTag.find("r=\"");
                if (refPos != std::string_view::npos) {
                    refPos += 3;
                    size_t refEnd = cTag.find('"', refPos);
                    if (refEnd != std::string_view::npos) {
                        colIdx = ExcelCellRefToColIndex(cTag.substr(refPos, refEnd - refPos));
                    }
                }
                bool isSharedStr = (cTag.find("t=\"s\"") != std::string_view::npos);
                bool isInlineStr = (cTag.find("t=\"inlineStr\"") != std::string_view::npos || cTag.find("t=\"str\"") != std::string_view::npos);

                std::wstring cellVal;
                if (isInlineStr) {
                    size_t tOpen = cBody.find("<t");
                    if (tOpen != std::string_view::npos) {
                        size_t tGt = cBody.find('>', tOpen);
                        size_t tEnd = cBody.find("</t>", tGt);
                        if (tGt != std::string_view::npos && tEnd != std::string_view::npos) {
                            cellVal = DecodeXmlEntities(PathUtils::Utf8ToWide(cBody.substr(tGt + 1, tEnd - (tGt + 1))));
                        }
                    }
                }
                if (cellVal.empty()) {
                    size_t vOpen = cBody.find("<v>");
                    size_t vEnd = cBody.find("</v>", vOpen);
                    if (vOpen != std::string_view::npos && vEnd != std::string_view::npos) {
                        std::string_view rawV = cBody.substr(vOpen + 3, vEnd - (vOpen + 3));
                        if (isSharedStr) {
                            int ssIdx = std::atoi(std::string(rawV).c_str());
                            if (ssIdx >= 0 && static_cast<size_t>(ssIdx) < sharedStrings.size()) {
                                cellVal = sharedStrings[ssIdx];
                            }
                        } else {
                            cellVal = DecodeXmlEntities(PathUtils::Utf8ToWide(rawV));
                        }
                    }
                }

                if (colIdx >= 0 && colIdx < 256) {
                    if (static_cast<size_t>(colIdx) >= rowCells.size()) {
                        rowCells.resize(static_cast<size_t>(colIdx) + 1);
                    }
                    rowCells[colIdx] = std::move(cellVal);
                }
                cPos = selfClosing ? (cGt + 1) : (endC + 4);
            }

            if (!rowCells.empty()) {
                sheet.maxCols = std::max(sheet.maxCols, rowCells.size());
                sheet.rows.push_back(std::move(rowCells));
            }
            rPos = endRow + 6;
            if (sheet.rows.size() >= 10000) break;
        }

        if (!sheet.rows.empty()) {
            out.sheets.push_back(std::move(sheet));
        }
    }

    // 4. OpenDocument Spreadsheet (.ods) fallback via content.xml (<table:table> -> <table:table-row> -> <table:table-cell>)
    if (out.sheets.empty()) {
        std::vector<uint8_t> odsBytes;
        if (ZipDeflate::ExtractZipFileByName(fullPath, L"content.xml", odsBytes) && !odsBytes.empty()) {
            out.formatTitle = L"OpenDocument Spreadsheet (.ods)";
            std::string_view sv(reinterpret_cast<const char*>(odsBytes.data()), odsBytes.size());
            size_t tPos = 0;
            int tblIdx = 1;
            while ((tPos = sv.find("<table:table", tPos)) != std::string_view::npos) {
                char afterT = (tPos + 12 < sv.size()) ? sv[tPos + 12] : ' ';
                if (afterT != ' ' && afterT != '>') { tPos += 12; continue; }
                size_t tGt = sv.find('>', tPos);
                if (tGt == std::string_view::npos) break;
                size_t endTbl = sv.find("</table:table>", tGt);
                if (endTbl == std::string_view::npos) break;
                std::string_view tblTag = sv.substr(tPos, tGt - tPos);
                std::string_view tblBlk = sv.substr(tGt + 1, endTbl - (tGt + 1));

                SpreadsheetSheet sheet;
                sheet.name = L"Sheet" + std::to_wstring(tblIdx++);
                size_t nmPos = tblTag.find("table:name=\"");
                if (nmPos != std::string_view::npos) {
                    nmPos += 12;
                    size_t nmEnd = tblTag.find('"', nmPos);
                    if (nmEnd != std::string_view::npos) {
                        sheet.name = DecodeXmlEntities(PathUtils::Utf8ToWide(tblTag.substr(nmPos, nmEnd - nmPos)));
                    }
                }

                size_t rPos = 0;
                while ((rPos = tblBlk.find("<table:table-row", rPos)) != std::string_view::npos) {
                    size_t rGt = tblBlk.find('>', rPos);
                    if (rGt == std::string_view::npos) break;
                    if (tblBlk[rGt - 1] == '/') { rPos = rGt + 1; continue; }
                    size_t endRow = tblBlk.find("</table:table-row>", rGt);
                    if (endRow == std::string_view::npos) break;
                    std::string_view rowBlk = tblBlk.substr(rGt + 1, endRow - (rGt + 1));

                    std::vector<std::wstring> rowCells;
                    bool anyNonEmpty = false;
                    size_t cPos = 0;
                    while ((cPos = rowBlk.find("<table:table-cell", cPos)) != std::string_view::npos) {
                        size_t cGt = rowBlk.find('>', cPos);
                        if (cGt == std::string_view::npos) break;
                        bool selfClosing = (tblBlk.data() && rowBlk[cGt - 1] == '/');
                        size_t endC = selfClosing ? cGt : rowBlk.find("</table:table-cell>", cGt);
                        if (endC == std::string_view::npos) break;
                        std::string_view cBody = selfClosing ? std::string_view{} : rowBlk.substr(cGt + 1, endC - (cGt + 1));

                        std::wstring cellText;
                        size_t pOpen = 0;
                        while ((pOpen = cBody.find("<text:p", pOpen)) != std::string_view::npos) {
                            size_t pGt = cBody.find('>', pOpen);
                            size_t pEnd = (pGt != std::string_view::npos) ? cBody.find("</text:p>", pGt) : std::string_view::npos;
                            if (pGt == std::string_view::npos || pEnd == std::string_view::npos) break;
                            std::string_view inner = cBody.substr(pGt + 1, pEnd - (pGt + 1));
                            std::string stripped;
                            bool inTag = false;
                            for (char ch : inner) {
                                if (ch == '<') inTag = true;
                                else if (ch == '>') inTag = false;
                                else if (!inTag) stripped.push_back(ch);
                            }
                            if (!cellText.empty() && !stripped.empty()) cellText += L" ";
                            cellText += DecodeXmlEntities(PathUtils::Utf8ToWide(stripped));
                            pOpen = pEnd + 9;
                        }
                        if (!cellText.empty()) anyNonEmpty = true;
                        rowCells.push_back(std::move(cellText));
                        cPos = selfClosing ? (cGt + 1) : (endC + 19);
                    }
                    if (anyNonEmpty) {
                        sheet.maxCols = std::max(sheet.maxCols, rowCells.size());
                        sheet.rows.push_back(std::move(rowCells));
                    }
                    rPos = endRow + 18;
                }
                if (!sheet.rows.empty()) {
                    out.sheets.push_back(std::move(sheet));
                }
                tPos = endTbl + 14;
            }
        }
    }

    out.valid = !out.sheets.empty();
    return out.valid;
}
