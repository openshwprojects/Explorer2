#include "SpreadsheetDocViewer.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Explorer2 {

namespace {

static D2D1_COLOR_F SdvColor(uint32_t rgb, float a = 1.0f) {
    return PathUtils::ColorFromRgb(rgb, a);
}

static std::wstring DecodeXmlEntities(std::wstring_view in) {
    std::wstring out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == L'&') {
            if (in.substr(i, 5) == L"&amp;")  { out.push_back(L'&');  i += 4; continue; }
            if (in.substr(i, 4) == L"&lt;")   { out.push_back(L'<');  i += 3; continue; }
            if (in.substr(i, 4) == L"&gt;")   { out.push_back(L'>');  i += 3; continue; }
            if (in.substr(i, 6) == L"&quot;") { out.push_back(L'"');  i += 5; continue; }
            if (in.substr(i, 6) == L"&apos;") { out.push_back(L'\''); i += 5; continue; }
        }
        out.push_back(in[i]);
    }
    return out;
}

static std::wstring ColIndexToExcelLetters(int colIdx) {
    std::wstring out;
    int c = colIdx;
    while (c >= 0) {
        out.insert(out.begin(), static_cast<wchar_t>(L'A' + (c % 26)));
        c = (c / 26) - 1;
    }
    return out;
}

static int ExcelCellRefToColIndex(std::string_view cellRef, int* outRowIdx = nullptr) {
    int col = 0;
    size_t i = 0;
    while (i < cellRef.size() && ((cellRef[i] >= 'A' && cellRef[i] <= 'Z') || (cellRef[i] >= 'a' && cellRef[i] <= 'z'))) {
        char ch = (cellRef[i] >= 'a') ? static_cast<char>(cellRef[i] - 'a' + 'A') : cellRef[i];
        col = col * 26 + (ch - 'A' + 1);
        ++i;
    }
    if (outRowIdx) {
        int r = std::atoi(std::string(cellRef.substr(i)).c_str());
        *outRowIdx = std::max(0, r - 1);
    }
    return std::max(0, col - 1);
}

static bool ParseCsvOrTsv(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;

    std::wstring text;
    if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        size_t wlen = (raw.size() - 2) / 2;
        text.resize(wlen);
        std::memcpy(text.data(), raw.data() + 2, wlen * 2);
    } else {
        size_t start = (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) ? 3 : 0;
        text = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + start), raw.size() - start));
    }

    // Auto-detect delimiter from first line outside quotes
    wchar_t delim = (extLower == L".tsv") ? L'\t' : L',';
    if (extLower != L".tsv") {
        int commas = 0, semis = 0, tabs = 0, pipes = 0;
        bool inQ = false;
        for (size_t i = 0; i < std::min<size_t>(text.size(), 2048); ++i) {
            wchar_t c = text[i];
            if (c == L'"') inQ = !inQ;
            else if (!inQ) {
                if (c == L'\n') break;
                if (c == L',') ++commas;
                else if (c == L';') ++semis;
                else if (c == L'\t') ++tabs;
                else if (c == L'|') ++pipes;
            }
        }
        int best = commas;
        if (semis > best) { best = semis; delim = L';'; }
        if (tabs > best)  { best = tabs;  delim = L'\t'; }
        if (pipes > best) { best = pipes; delim = L'|'; }
    }

    out.formatTitle = (delim == L'\t') ? L"TSV Table (Tab-Separated)"
                    : ((delim == L';') ? L"CSV Table (Semicolon ;)" : L"CSV Table (Comma ,)");

    SpreadsheetSheet sheet;
    sheet.name = L"CSV";
    std::vector<std::wstring> curRow;
    std::wstring curCell;
    bool inQuotes = false;

    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (inQuotes) {
            if (c == L'"') {
                if (i + 1 < text.size() && text[i + 1] == L'"') {
                    curCell.push_back(L'"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                curCell.push_back(c);
            }
        } else {
            if (c == L'"' && curCell.empty()) {
                inQuotes = true;
            } else if (c == delim) {
                curRow.push_back(std::move(curCell));
                curCell.clear();
            } else if (c == L'\r' || c == L'\n') {
                if (c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
                curRow.push_back(std::move(curCell));
                curCell.clear();
                if (!curRow.empty() && !(curRow.size() == 1 && curRow[0].empty())) {
                    sheet.maxCols = std::max(sheet.maxCols, curRow.size());
                    sheet.rows.push_back(std::move(curRow));
                }
                curRow.clear();
                if (sheet.rows.size() >= 10000) break;
            } else {
                curCell.push_back(c);
            }
        }
    }
    if (!curCell.empty() || !curRow.empty()) {
        curRow.push_back(std::move(curCell));
        sheet.maxCols = std::max(sheet.maxCols, curRow.size());
        sheet.rows.push_back(std::move(curRow));
    }

    if (sheet.rows.empty()) {
        sheet.rows.push_back({ L"" });
        sheet.maxCols = 1;
    }
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}

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

    out.valid = !out.sheets.empty();
    return out.valid;
}

static bool ParseWordDocx(const std::wstring& fullPath, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::WordDocx;
    out.isSpreadsheet = false;
    out.formatTitle = L"Microsoft Word Document (.docx)";

    std::vector<uint8_t> docBytes;
    if (!ZipDeflate::ExtractZipFileByName(fullPath, L"word/document.xml", docBytes) &&
        !ZipDeflate::ExtractZipFileByName(fullPath, L"content.xml", docBytes)) {
        return false;
    }

    std::string_view sv(reinterpret_cast<const char*>(docBytes.data()), docBytes.size());
    size_t pPos = 0;
    while ((pPos = sv.find("<w:p", pPos)) != std::string_view::npos) {
        char after = (pPos + 4 < sv.size()) ? sv[pPos + 4] : ' ';
        if (after != '>' && after != ' ' && after != '/') {
            pPos += 4;
            continue;
        }
        size_t pGt = sv.find('>', pPos);
        if (pGt == std::string_view::npos) break;
        if (sv[pGt - 1] == '/') { pPos = pGt + 1; continue; }
        size_t endP = sv.find("</w:p>", pGt);
        if (endP == std::string_view::npos) break;
        std::string_view pBlk = sv.substr(pGt + 1, endP - (pGt + 1));

        DocParagraph para;
        if (pBlk.find("Heading1") != std::string_view::npos || pBlk.find("heading 1") != std::string_view::npos || pBlk.find("Title") != std::string_view::npos) {
            para.headingLevel = 1;
        } else if (pBlk.find("Heading2") != std::string_view::npos || pBlk.find("heading 2") != std::string_view::npos) {
            para.headingLevel = 2;
        } else if (pBlk.find("Heading3") != std::string_view::npos) {
            para.headingLevel = 3;
        }
        if (pBlk.find("<w:numPr>") != std::string_view::npos) {
            para.isBullet = true;
        }

        size_t tPos = 0;
        while ((tPos = pBlk.find("<w:t", tPos)) != std::string_view::npos) {
            size_t tGt = pBlk.find('>', tPos);
            if (tGt == std::string_view::npos) break;
            if (pBlk[tGt - 1] == '/') { tPos = tGt + 1; continue; }
            size_t endT = pBlk.find("</w:t>", tGt);
            if (endT == std::string_view::npos) break;
            para.text += DecodeXmlEntities(PathUtils::Utf8ToWide(pBlk.substr(tGt + 1, endT - (tGt + 1))));
            tPos = endT + 6;
        }

        if (!para.text.empty()) {
            out.charCount += para.text.size();
            bool inWord = false;
            for (wchar_t ch : para.text) {
                if (ch > L' ') {
                    if (!inWord) { ++out.wordCount; inWord = true; }
                } else {
                    inWord = false;
                }
            }
            out.paragraphs.push_back(std::move(para));
        }
        pPos = endP + 6;
    }

    if (out.paragraphs.empty()) {
        out.paragraphs.push_back({ L"(Pusty dokument Word .docx / Empty Word document)", 0, false, false });
    }
    out.valid = true;
    return true;
}

static bool ParseRtfDocument(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::RichTextRtf;
    out.isSpreadsheet = false;
    out.formatTitle = L"Rich Text Document (.rtf)";

    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    std::string curLine;
    int braceDepth = 0;
    int skipDepth = -1;

    auto flushPara = [&]() {
        if (!curLine.empty()) {
            std::wstring w = PathUtils::Utf8ToWide(curLine);
            out.charCount += w.size();
            bool inW = false;
            for (wchar_t c : w) {
                if (c > L' ') { if (!inW) { ++out.wordCount; inW = true; } }
                else inW = false;
            }
            out.paragraphs.push_back({ std::move(w), 0, false, false });
            curLine.clear();
        }
    };

    for (size_t i = 0; i < sv.size(); ++i) {
        char c = sv[i];
        if (c == '{') {
            ++braceDepth;
            if (i + 2 < sv.size() && sv[i + 1] == '\\' && sv[i + 2] == '*') {
                if (skipDepth < 0) skipDepth = braceDepth;
            }
            continue;
        }
        if (c == '}') {
            if (braceDepth == skipDepth) skipDepth = -1;
            --braceDepth;
            continue;
        }
        if (skipDepth >= 0) continue;

        if (c == '\\') {
            if (i + 1 < sv.size() && (sv[i + 1] == '\\' || sv[i + 1] == '{' || sv[i + 1] == '}')) {
                curLine.push_back(sv[++i]);
                continue;
            }
            size_t kwStart = ++i;
            while (i < sv.size() && ((sv[i] >= 'a' && sv[i] <= 'z') || (sv[i] >= 'A' && sv[i] <= 'Z'))) ++i;
            std::string_view kw = sv.substr(kwStart, i - kwStart);
            while (i < sv.size() && ((sv[i] >= '0' && sv[i] <= '9') || sv[i] == '-')) ++i;
            if (i < sv.size() && sv[i] == ' ') ++i;
            --i;

            if (kw == "fonttbl" || kw == "colortbl" || kw == "stylesheet" || kw == "info" || kw == "pict") {
                skipDepth = braceDepth;
            } else if (kw == "par" || kw == "line") {
                flushPara();
            } else if (kw == "tab") {
                curLine += "    ";
            }
            continue;
        }
        if (c != '\r' && c != '\n') {
            curLine.push_back(c);
        }
    }
    flushPara();
    out.valid = !out.paragraphs.empty();
    return out.valid;
}

} // namespace

bool SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(const std::wstring& extLower) {
    return extLower == L".csv"  || extLower == L".tsv"  || extLower == L".psv"  ||
           extLower == L".xlsx" || extLower == L".xlsm" || extLower == L".xls"  ||
           extLower == L".ods"  || extLower == L".docx" || extLower == L".docm" ||
           extLower == L".odt"  || extLower == L".rtf";
}

bool SpreadsheetDocViewer::InspectOfficeFile(const std::wstring& fullPath, ParsedOfficeDocument& outDoc) {
    outDoc = ParsedOfficeDocument{};
    outDoc.filePath = fullPath;
    std::wstring ext = PathUtils::GetExtension(fullPath);

    if (ext == L".xlsx" || ext == L".xlsm" || ext == L".ods") {
        return ParseExcelXlsx(fullPath, outDoc);
    }
    if (ext == L".docx" || ext == L".docm" || ext == L".odt") {
        return ParseWordDocx(fullPath, outDoc);
    }

    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(fullPath, 32 * 1024 * 1024, raw) || raw.empty()) {
        return false;
    }

    if (ext == L".rtf" || (raw.size() >= 5 && std::memcmp(raw.data(), "{\\rtf", 5) == 0)) {
        return ParseRtfDocument(raw, outDoc);
    }
    if (ext == L".csv" || ext == L".tsv" || ext == L".psv") {
        return ParseCsvOrTsv(raw, ext, outDoc);
    }

    // Legacy .xls (OLE2 BIFF8) string table extractor
    if (ext == L".xls") {
        outDoc.kind = OfficeFileKind::ExcelXls;
        outDoc.isSpreadsheet = true;
        outDoc.formatTitle = L"Microsoft Excel 97-2003 (.xls)";
        SpreadsheetSheet sheet;
        sheet.name = L"XLS";
        std::vector<std::wstring> curRow;
        std::string curAscii;
        for (size_t i = 0; i < raw.size(); ++i) {
            uint8_t b = raw[i];
            if (b >= 32 && b <= 126) {
                curAscii.push_back(static_cast<char>(b));
            } else {
                if (curAscii.size() >= 3) {
                    curRow.push_back(PathUtils::Utf8ToWide(curAscii));
                    if (curRow.size() >= 6) {
                        sheet.maxCols = 6;
                        sheet.rows.push_back(std::move(curRow));
                        curRow.clear();
                    }
                }
                curAscii.clear();
            }
        }
        if (!curRow.empty()) {
            sheet.maxCols = std::max(sheet.maxCols, curRow.size());
            sheet.rows.push_back(std::move(curRow));
        }
        if (sheet.rows.empty()) {
            sheet.rows.push_back({ L"(Binary XLS Workbook)" });
            sheet.maxCols = 1;
        }
        outDoc.sheets.push_back(std::move(sheet));
        outDoc.valid = true;
        return true;
    }

    return false;
}

void SpreadsheetDocViewer::Initialize(HWND parentHwnd) {
    m_parentHwnd = parentHwnd;
}

void SpreadsheetDocViewer::Shutdown() {
    Clear();
}

void SpreadsheetDocViewer::Clear() {
    m_currentPath.clear();
    m_doc = ParsedOfficeDocument{};
    m_activeSheetIdx = 0;
    m_selectedRow = 0;
    m_selectedCol = 0;
    m_sortedCol = -1;
    m_topRow = 0;
    m_leftCol = 0;
    m_docScrollY = 0.0f;
}

void SpreadsheetDocViewer::LoadFile(const std::wstring& fullPath, bool forceReload) {
    if (!forceReload && m_currentPath == fullPath && !m_currentPath.empty()) {
        return;
    }
    m_currentPath = fullPath;
    m_activeSheetIdx = 0;
    m_selectedRow = 0;
    m_selectedCol = 0;
    m_sortedCol = -1;
    m_topRow = 0;
    m_leftCol = 0;
    m_docScrollY = 0.0f;
    InspectOfficeFile(fullPath, m_doc);
    RecomputeColumnStats(0);
}

void SpreadsheetDocViewer::RecomputeColumnStats(int colIdx) {
    m_statNumericCount = 0;
    m_statSum = 0.0;
    m_statAvg = 0.0;
    m_statMin = 0.0;
    m_statMax = 0.0;

    if (!m_doc.isSpreadsheet || m_activeSheetIdx < 0 || static_cast<size_t>(m_activeSheetIdx) >= m_doc.sheets.size()) {
        return;
    }
    const auto& rows = m_doc.sheets[m_activeSheetIdx].rows;
    size_t startRow = (rows.size() > 1) ? 1 : 0; // Skip header row if >1 row
    for (size_t r = startRow; r < rows.size(); ++r) {
        if (colIdx < 0 || static_cast<size_t>(colIdx) >= rows[r].size()) continue;
        const std::wstring& cell = rows[r][colIdx];
        if (cell.empty()) continue;
        wchar_t* endPtr = nullptr;
        double v = std::wcstod(cell.c_str(), &endPtr);
        if (endPtr != cell.c_str() && std::isfinite(v)) {
            if (m_statNumericCount == 0) {
                m_statMin = v;
                m_statMax = v;
            } else {
                m_statMin = std::min(m_statMin, v);
                m_statMax = std::max(m_statMax, v);
            }
            m_statSum += v;
            ++m_statNumericCount;
        }
    }
    if (m_statNumericCount > 0) {
        m_statAvg = m_statSum / static_cast<double>(m_statNumericCount);
    }
}

void SpreadsheetDocViewer::SortActiveSheetByColumn(int colIdx) {
    if (!m_doc.isSpreadsheet || m_activeSheetIdx < 0 || static_cast<size_t>(m_activeSheetIdx) >= m_doc.sheets.size()) {
        return;
    }
    auto& rows = m_doc.sheets[m_activeSheetIdx].rows;
    if (rows.size() <= 2) return;

    if (m_sortedCol == colIdx) {
        m_sortAscending = !m_sortAscending;
    } else {
        m_sortedCol = colIdx;
        m_sortAscending = true;
    }

    bool asc = m_sortAscending;
    // Keep row 0 as frozen header row, sort rows[1..end]
    std::sort(rows.begin() + 1, rows.end(), [colIdx, asc](const std::vector<std::wstring>& a, const std::vector<std::wstring>& b) {
        std::wstring_view sa = (colIdx >= 0 && static_cast<size_t>(colIdx) < a.size()) ? std::wstring_view(a[colIdx]) : std::wstring_view{};
        std::wstring_view sb = (colIdx >= 0 && static_cast<size_t>(colIdx) < b.size()) ? std::wstring_view(b[colIdx]) : std::wstring_view{};
        wchar_t* endA = nullptr;
        wchar_t* endB = nullptr;
        double da = sa.empty() ? 0.0 : std::wcstod(sa.data(), &endA);
        double db = sb.empty() ? 0.0 : std::wcstod(sb.data(), &endB);
        if (!sa.empty() && !sb.empty() && endA != sa.data() && endB != sb.data()) {
            return asc ? (da < db) : (da > db);
        }
        int cmp = CompareStringOrdinal(sa.data(), static_cast<int>(sa.size()), sb.data(), static_cast<int>(sb.size()), TRUE);
        return asc ? (cmp == CSTR_LESS_THAN) : (cmp == CSTR_GREATER_THAN);
    });
}

void SpreadsheetDocViewer::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& rect,
    Lang lang,
    D2D1_POINT_2F /*mousePt*/)
{
    const bool pl = (lang == Lang::PL);
    m_toolbarHits.clear();

    float tbH = 30.0f;
    float statusH = 26.0f;
    D2D1_RECT_F tbR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + tbH);
    brush->SetColor(SdvColor(0xF8FAFC));
    rt->FillRectangle(tbR, brush);
    brush->SetColor(SdvColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(tbR.left, tbR.bottom - 0.5f), D2D1::Point2F(tbR.right, tbR.bottom - 0.5f), brush, 1.0f);

    float bx = tbR.left + 6.0f;
    auto addBtn = [&](int id, float w, const wchar_t* label, bool active = false) {
        if (bx + w > tbR.right - 4.0f) return;
        D2D1_RECT_F br = D2D1::RectF(bx, tbR.top + 4.0f, bx + w, tbR.bottom - 4.0f);
        m_toolbarHits.push_back({ id, br });
        PathUtils::DrawToolbarButton(rt, brush, fmtSmall, br, label, active, m_hoverBtnId == id);
        bx += w + 4.0f;
    };

    if (m_doc.isSpreadsheet) {
        for (size_t s = 0; s < std::min<size_t>(m_doc.sheets.size(), 5); ++s) {
            std::wstring sName = L"\xD83D\xDCCA " + m_doc.sheets[s].name;
            float sw = std::clamp(static_cast<float>(sName.size()) * 7.5f + 16.0f, 64.0f, 120.0f);
            addBtn(BTN_SHEET_BASE + static_cast<int>(s), sw, sName.c_str(), static_cast<int>(s) == m_activeSheetIdx);
        }
        addBtn(BTN_SORT_ASC_DESC, pl ? 78.0f : 72.0f, pl ? L"\x21C5 Sortuj kol." : L"\x21C5 Sort Col", m_sortedCol >= 0);
        addBtn(BTN_COPY_CELL,     pl ? 82.0f : 76.0f, pl ? L"Kopiuj kom." : L"Copy Cell");
        addBtn(BTN_COPY_ALL,      pl ? 76.0f : 72.0f, pl ? L"Kopiuj CSV" : L"Copy CSV");
    } else {
        addBtn(BTN_COPY_ALL, pl ? 110.0f : 100.0f, pl ? L"\xD83D\xDCCB Kopiuj tekst" : L"\xD83D\xDCCB Copy Text");
    }

    D2D1_RECT_F bodyR = D2D1::RectF(rect.left, tbR.bottom, rect.right, std::max(tbR.bottom + 20.0f, rect.bottom - statusH));

    if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
        const auto& sheet = m_doc.sheets[std::clamp(m_activeSheetIdx, 0, static_cast<int>(m_doc.sheets.size()) - 1)];

        // Formula / Active Cell Inspector Strip (24px)
        float fxH = 24.0f;
        D2D1_RECT_F fxR = D2D1::RectF(bodyR.left, bodyR.top, bodyR.right, bodyR.top + fxH);
        brush->SetColor(SdvColor(0xEFF6FF));
        rt->FillRectangle(fxR, brush);
        brush->SetColor(SdvColor(0xDBEAFE));
        rt->DrawLine(D2D1::Point2F(fxR.left, fxR.bottom - 0.5f), D2D1::Point2F(fxR.right, fxR.bottom - 0.5f), brush, 1.0f);

        std::wstring cellRef = ColIndexToExcelLetters(m_selectedCol) + std::to_wstring(m_selectedRow + 1);
        std::wstring cellVal;
        if (m_selectedRow >= 0 && static_cast<size_t>(m_selectedRow) < sheet.rows.size() &&
            m_selectedCol >= 0 && static_cast<size_t>(m_selectedCol) < sheet.rows[m_selectedRow].size()) {
            cellVal = sheet.rows[m_selectedRow][m_selectedCol];
        }
        std::wstring fxText = L"[" + cellRef + L"]   fx = " + cellVal;
        brush->SetColor(SdvColor(0x1E3A8A));
        rt->DrawText(fxText.c_str(), static_cast<UINT32>(fxText.size()), fmtSmall,
            D2D1::RectF(fxR.left + 8.0f, fxR.top, fxR.right - 8.0f, fxR.bottom), brush);

        // Column Letters Header (A, B, C...) + Grid
        float colHdrH = 22.0f;
        float rowNumW = 46.0f;
        float cellW = 108.0f;
        float rowH = 21.0f;

        m_colHeaderRect = D2D1::RectF(bodyR.left + rowNumW, fxR.bottom, bodyR.right, fxR.bottom + colHdrH);
        m_gridBodyRect = D2D1::RectF(bodyR.left, m_colHeaderRect.bottom, bodyR.right, bodyR.bottom);

        brush->SetColor(SdvColor(0xF1F5F9));
        rt->FillRectangle(D2D1::RectF(bodyR.left, fxR.bottom, bodyR.right, m_colHeaderRect.bottom), brush);
        brush->SetColor(SdvColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(bodyR.left, m_colHeaderRect.bottom - 0.5f), D2D1::Point2F(bodyR.right, m_colHeaderRect.bottom - 0.5f), brush, 1.0f);

        int visCols = std::max(1, static_cast<int>((m_colHeaderRect.right - m_colHeaderRect.left) / cellW) + 1);
        int visRows = std::max(1, static_cast<int>((m_gridBodyRect.bottom - m_gridBodyRect.top) / rowH));

        rt->PushAxisAlignedClip(D2D1::RectF(bodyR.left, fxR.bottom, bodyR.right, bodyR.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        // Draw Column Letters
        fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        for (int c = 0; c < visCols; ++c) {
            int colIdx = m_leftCol + c;
            if (colIdx >= static_cast<int>( std::max<size_t>(sheet.maxCols, 8) )) break;
            float cx1 = m_colHeaderRect.left + c * cellW;
            D2D1_RECT_F chR = D2D1::RectF(cx1, m_colHeaderRect.top, cx1 + cellW, m_colHeaderRect.bottom);
            if (colIdx == m_selectedCol) {
                brush->SetColor(SdvColor(0xDBEAFE));
                rt->FillRectangle(chR, brush);
            }
            std::wstring colLbl = ColIndexToExcelLetters(colIdx);
            if (colIdx == m_sortedCol) colLbl += (m_sortAscending ? L" \x25B2" : L" \x25BC");
            brush->SetColor(SdvColor(0x334155));
            rt->DrawText(colLbl.c_str(), static_cast<UINT32>(colLbl.size()), fmtBold, chR, brush);
            brush->SetColor(SdvColor(0xCBD5E1));
            rt->DrawLine(D2D1::Point2F(chR.right, chR.top), D2D1::Point2F(chR.right, m_gridBodyRect.bottom), brush, 0.8f);
        }
        fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        // Draw Rows
        for (int r = 0; r <= visRows; ++r) {
            int rowIdx = (r == 0) ? 0 : (m_topRow + r); // Freeze Row 0 at top!
            if (r > 0 && m_topRow == 0) rowIdx = r;
            if (rowIdx >= static_cast<int>(sheet.rows.size())) break;

            float ry = m_gridBodyRect.top + r * rowH;
            D2D1_RECT_F rowNumR = D2D1::RectF(bodyR.left, ry, bodyR.left + rowNumW, ry + rowH);
            brush->SetColor(rowIdx == 0 ? SdvColor(0xE2E8F0) : SdvColor(0xF8FAFC));
            rt->FillRectangle(rowNumR, brush);

            wchar_t rnBuf[24] = {};
            swprintf_s(rnBuf, L"%d", rowIdx + 1);
            fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            brush->SetColor(SdvColor(0x64748B));
            rt->DrawText(rnBuf, static_cast<UINT32>(wcslen(rnBuf)), fmtSmall, rowNumR, brush);
            fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

            const auto& rowVec = sheet.rows[rowIdx];
            for (int c = 0; c < visCols; ++c) {
                int colIdx = m_leftCol + c;
                if (colIdx >= static_cast<int>(std::max<size_t>(sheet.maxCols, 8))) break;
                float cx1 = m_colHeaderRect.left + c * cellW;
                D2D1_RECT_F cellR = D2D1::RectF(cx1, ry, cx1 + cellW, ry + rowH);

                if (rowIdx == m_selectedRow && colIdx == m_selectedCol) {
                    brush->SetColor(SdvColor(0xDBEAFE));
                    rt->FillRectangle(cellR, brush);
                    brush->SetColor(SdvColor(0x0078D7));
                    rt->DrawRectangle(D2D1::RectF(cellR.left + 1.0f, cellR.top + 1.0f, cellR.right - 1.0f, cellR.bottom - 1.0f), brush, 1.6f);
                } else if (rowIdx == 0) {
                    brush->SetColor(SdvColor(0xF8FAFC));
                    rt->FillRectangle(cellR, brush);
                } else if (rowIdx % 2 == 1) {
                    brush->SetColor(SdvColor(0xFCFDFF));
                    rt->FillRectangle(cellR, brush);
                }

                if (static_cast<size_t>(colIdx) < rowVec.size() && !rowVec[colIdx].empty()) {
                    brush->SetColor(rowIdx == 0 ? SdvColor(0x0F172A) : SdvColor(0x1E293B));
                    IDWriteTextFormat* tf = (rowIdx == 0) ? fmtBold : fmtSmall;
                    rt->DrawText(rowVec[colIdx].c_str(), static_cast<UINT32>(rowVec[colIdx].size()), tf,
                        D2D1::RectF(cellR.left + 5.0f, cellR.top, cellR.right - 4.0f, cellR.bottom), brush);
                }
            }

            brush->SetColor(rowIdx == 0 ? SdvColor(0x94A3B8) : SdvColor(0xE2E8F0));
            rt->DrawLine(D2D1::Point2F(bodyR.left, ry + rowH), D2D1::Point2F(bodyR.right, ry + rowH), brush, rowIdx == 0 ? 1.2f : 0.7f);
        }

        rt->PopAxisAlignedClip();
    } else {
        // Rich Document View (.docx, .odt, .rtf)
        m_gridBodyRect = bodyR;
        brush->SetColor(SdvColor(0xF1F5F9));
        rt->FillRectangle(bodyR, brush);

        D2D1_RECT_F pageR = D2D1::RectF(bodyR.left + 12.0f, bodyR.top + 10.0f, bodyR.right - 12.0f, bodyR.bottom - 10.0f);
        brush->SetColor(SdvColor(0xFFFFFF));
        rt->FillRoundedRectangle(D2D1::RoundedRect(pageR, 4.0f, 4.0f), brush);
        brush->SetColor(SdvColor(0xCBD5E1));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(pageR, 4.0f, 4.0f), brush, 1.0f);

        rt->PushAxisAlignedClip(pageR, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float y = pageR.top + 14.0f - m_docScrollY;
        float textW = std::max(80.0f, pageR.right - pageR.left - 32.0f);
        int charsPerLine = std::max(20, static_cast<int>(textW / 6.8f));

        for (const auto& para : m_doc.paragraphs) {
            int lines = std::max(1, static_cast<int>((para.text.size() + charsPerLine - 1) / charsPerLine));
            float blockH = lines * 20.0f + (para.headingLevel > 0 ? 10.0f : 6.0f);
            if (y + blockH >= pageR.top && y <= pageR.bottom) {
                float xLeft = pageR.left + 16.0f + (para.isBullet ? 14.0f : 0.0f);
                if (para.isBullet) {
                    brush->SetColor(SdvColor(0x0078D7));
                    rt->DrawText(L"\x2022", 1, fmtBold, D2D1::RectF(pageR.left + 16.0f, y, xLeft, y + 20.0f), brush);
                }
                IDWriteTextFormat* tf = (para.headingLevel > 0) ? fmtBold : fmtRegular;
                brush->SetColor(para.headingLevel == 1 ? SdvColor(0x1E3A8A) : SdvColor(0x0F172A));
                DWRITE_WORD_WRAPPING prevWrap = tf->GetWordWrapping();
                tf->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                rt->DrawText(para.text.c_str(), static_cast<UINT32>(para.text.size()), tf,
                    D2D1::RectF(xLeft, y, pageR.right - 16.0f, y + blockH), brush);
                tf->SetWordWrapping(prevWrap);
            }
            y += blockH;
        }
        rt->PopAxisAlignedClip();
    }

    // Bottom Status Strip (Rows x Cols + Live Numeric Column Stats: Sum, Avg, Min, Max, or Word/Char Count)
    D2D1_RECT_F stR = D2D1::RectF(rect.left, bodyR.bottom, rect.right, rect.bottom);
    brush->SetColor(SdvColor(0xF1F5F9));
    rt->FillRectangle(stR, brush);
    brush->SetColor(SdvColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(stR.left, stR.top), D2D1::Point2F(stR.right, stR.top), brush, 1.0f);

    wchar_t stBuf[320] = {};
    if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
        const auto& sh = m_doc.sheets[std::clamp(m_activeSheetIdx, 0, static_cast<int>(m_doc.sheets.size()) - 1)];
        if (m_statNumericCount > 0) {
            swprintf_s(stBuf,
                pl ? L"%s (%zu wierszy \xD7 %zu kol.)   \x2022   Kol %s: Suma = %.2f  |  Śr = %.2f  |  Min = %.2f  |  Max = %.2f"
                   : L"%s (%zu rows \xD7 %zu cols)   \x2022   Col %s: Sum = %.2f  |  Avg = %.2f  |  Min = %.2f  |  Max = %.2f",
                m_doc.formatTitle.c_str(), sh.rows.size(), sh.maxCols,
                ColIndexToExcelLetters(m_selectedCol).c_str(),
                m_statSum, m_statAvg, m_statMin, m_statMax);
        } else {
            swprintf_s(stBuf,
                pl ? L"%s   \x2022   Wierszy: %zu   |   Kolumn: %zu   |   Arkuszy: %zu"
                   : L"%s   \x2022   Rows: %zu   |   Columns: %zu   |   Sheets: %zu",
                m_doc.formatTitle.c_str(), sh.rows.size(), sh.maxCols, m_doc.sheets.size());
        }
    } else {
        swprintf_s(stBuf,
            pl ? L"%s   \x2022   Akapitów: %zu   |   Słów: %zu   |   Znaków: %zu"
               : L"%s   \x2022   Paragraphs: %zu   |   Words: %zu   |   Characters: %zu",
            m_doc.formatTitle.c_str(), m_doc.paragraphs.size(), m_doc.wordCount, m_doc.charCount);
    }
    brush->SetColor(SdvColor(0x334155));
    rt->DrawText(stBuf, static_cast<UINT32>(wcslen(stBuf)), fmtSmall,
        D2D1::RectF(stR.left + 8.0f, stR.top, stR.right - 8.0f, stR.bottom), brush);
}

bool SpreadsheetDocViewer::OnMouseDown(float x, float y, bool rightButton, Lang lang, std::wstring* outStatusToast) {
    if (rightButton) return false;
    const bool pl = (lang == Lang::PL);

    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            if (hit.id >= BTN_SHEET_BASE && hit.id < BTN_SHEET_BASE + 32) {
                m_activeSheetIdx = hit.id - BTN_SHEET_BASE;
                m_topRow = 0;
                m_leftCol = 0;
                m_selectedRow = 0;
                m_selectedCol = 0;
                RecomputeColumnStats(0);
                return true;
            }
            if (hit.id == BTN_SORT_ASC_DESC) {
                SortActiveSheetByColumn(m_selectedCol);
                return true;
            }
            if (hit.id == BTN_COPY_CELL && m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
                const auto& sh = m_doc.sheets[m_activeSheetIdx];
                if (m_selectedRow >= 0 && static_cast<size_t>(m_selectedRow) < sh.rows.size() &&
                    m_selectedCol >= 0 && static_cast<size_t>(m_selectedCol) < sh.rows[m_selectedRow].size()) {
                    PathUtils::CopyWideToClipboard(m_parentHwnd, sh.rows[m_selectedRow][m_selectedCol]);
                    if (outStatusToast) *outStatusToast = pl ? L"\x2713 Skopiowano komórkę do schowka" : L"\x2713 Copied cell to clipboard";
                }
                return true;
            }
            if (hit.id == BTN_COPY_ALL) {
                std::wstring out;
                if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
                    const auto& sh = m_doc.sheets[m_activeSheetIdx];
                    for (const auto& r : sh.rows) {
                        for (size_t c = 0; c < r.size(); ++c) {
                            if (c > 0) out += L"\t";
                            out += r[c];
                        }
                        out += L"\r\n";
                    }
                } else {
                    for (const auto& p : m_doc.paragraphs) {
                        out += p.text + L"\r\n\r\n";
                    }
                }
                PathUtils::CopyWideToClipboard(m_parentHwnd, out);
                if (outStatusToast) *outStatusToast = pl ? L"\x2713 Skopiowano zawartość do schowka" : L"\x2713 Copied content to clipboard";
                return true;
            }
        }
    }

    if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
        if (PathUtils::PtInRectF(x, y, m_colHeaderRect)) {
            int c = static_cast<int>((x - m_colHeaderRect.left) / 108.0f);
            int colIdx = m_leftCol + std::max(0, c);
            m_selectedCol = colIdx;
            SortActiveSheetByColumn(colIdx);
            RecomputeColumnStats(colIdx);
            return true;
        }
        if (PathUtils::PtInRectF(x, y, m_gridBodyRect)) {
            int r = static_cast<int>((y - m_gridBodyRect.top) / 21.0f);
            int c = static_cast<int>((x - (m_gridBodyRect.left + 46.0f)) / 108.0f);
            const auto& sh = m_doc.sheets[m_activeSheetIdx];
            int rowIdx = (r == 0) ? 0 : (m_topRow + r);
            if (rowIdx >= 0 && static_cast<size_t>(rowIdx) < sh.rows.size()) {
                m_selectedRow = rowIdx;
            }
            if (c >= 0) {
                m_selectedCol = m_leftCol + c;
                RecomputeColumnStats(m_selectedCol);
            }
            return true;
        }
    }
    return false;
}

bool SpreadsheetDocViewer::OnMouseMove(float x, float y) {
    int newHov = 0;
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newHov = hit.id;
            break;
        }
    }
    if (newHov != m_hoverBtnId) {
        m_hoverBtnId = newHov;
        return true;
    }
    return false;
}

bool SpreadsheetDocViewer::OnMouseUp() {
    return false;
}

bool SpreadsheetDocViewer::OnMouseWheel(short delta, float x, float y) {
    if (!PathUtils::PtInRectF(x, y, m_gridBodyRect)) return false;
    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;

    if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
        const auto& sh = m_doc.sheets[m_activeSheetIdx];
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            int maxLeft = std::max(0, static_cast<int>(sh.maxCols) - 2);
            m_leftCol = std::clamp(m_leftCol - notches, 0, maxLeft);
        } else {
            int maxTop = std::max(0, static_cast<int>(sh.rows.size()) - 4);
            m_topRow = std::clamp(m_topRow - notches * 3, 0, maxTop);
        }
        return true;
    } else {
        m_docScrollY = std::max(0.0f, m_docScrollY - notches * 48.0f);
        return true;
    }
}

} // namespace Explorer2
