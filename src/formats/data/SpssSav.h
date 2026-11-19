#pragma once
// SpssSav.h — SPSS System File (.sav) Reader

namespace Formats {
namespace AiDataDetail {

// ============================================================================
// 11. SPSS System File (.sav — $FL2 / $FL3)
// ============================================================================
inline bool ParseSpssSav(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 176) return false;
    if (std::memcmp(raw.data(), "$FL2", 4) != 0 && std::memcmp(raw.data(), "$FL3", 4) != 0) return false;

    int32_t layoutLE = RdI32LE(raw.data() + 60);
    int32_t layoutBE = RdI32BE(raw.data() + 60);
    bool be = false;
    if (layoutLE == 2 || layoutLE == 3) be = false;
    else if (layoutBE == 2 || layoutBE == 3) be = true;
    else return false;

    auto rdI32 = [&](size_t off) { return be ? RdI32BE(raw.data() + off) : RdI32LE(raw.data() + off); };
    auto rdF64 = [&](size_t off) { return be ? RdF64BE(raw.data() + off) : RdF64LE(raw.data() + off); };

    int32_t nominalCaseSize = rdI32(64);
    int32_t compressed = rdI32(68);
    int32_t ncases = rdI32(76);
    double bias = rdF64(80);
    std::wstring cDate = CleanAsciiFixed(raw.data() + 88, 9);
    std::wstring cTime = CleanAsciiFixed(raw.data() + 97, 8);
    std::wstring fileLabel = CleanAsciiFixed(raw.data() + 105, 64);

    struct SavCol {
        std::string shortName;
        std::wstring displayName;
        std::wstring label;
        int32_t width = 0; // 0 = numeric, >0 = string bytes
        size_t slotCount = 1;
        std::map<double, std::wstring> numValLabels;
    };
    std::vector<SavCol> cols;
    std::vector<int> slotToCol; // >=0 logical col index (head), -1 continuation slot

    size_t pos = 176;
    bool foundEnd = false;
    std::vector<std::pair<double, std::wstring>> pendingLabels;

    while (pos + 4 <= raw.size()) {
        int32_t recType = rdI32(pos);
        pos += 4;
        if (recType == 999) {
            if (pos + 4 <= raw.size()) pos += 4; // filler int32
            foundEnd = true;
            break;
        }
        if (recType == 2) { // Variable Record (28B remaining fixed)
            if (pos + 28 > raw.size()) return false;
            int32_t vType = rdI32(pos);
            int32_t hasLbl = rdI32(pos + 4);
            int32_t nMiss = rdI32(pos + 8);
            std::string sname(reinterpret_cast<const char*>(raw.data() + pos + 20), 8);
            while (!sname.empty() && (sname.back() == ' ' || sname.back() == '\0')) sname.pop_back();
            pos += 28;

            std::wstring vLabel;
            if (hasLbl == 1) {
                if (pos + 4 > raw.size()) return false;
                int32_t lblLen = rdI32(pos);
                pos += 4;
                if (lblLen < 0 || static_cast<size_t>(lblLen) > raw.size() - pos) return false;
                vLabel = CleanAsciiFixed(raw.data() + pos, static_cast<size_t>(lblLen));
                pos += (static_cast<size_t>(lblLen) + 3u) & ~size_t(3u);
            }
            if (nMiss != 0) {
                size_t mSlots = static_cast<size_t>(std::abs(nMiss));
                if (mSlots * 8 > raw.size() - pos) return false;
                pos += mSlots * 8;
            }

            if (vType == -1) { // Continuation slot of previous string variable
                slotToCol.push_back(-1);
                if (!cols.empty()) cols.back().slotCount++;
            } else if (vType >= 0) {
                slotToCol.push_back(static_cast<int>(cols.size()));
                SavCol c;
                c.shortName = sname;
                c.displayName = PathUtils::Utf8ToWide(sname);
                c.label = vLabel;
                c.width = vType;
                c.slotCount = 1;
                cols.push_back(std::move(c));
            } else {
                return false;
            }
        } else if (recType == 3) { // Value Labels
            if (pos + 4 > raw.size()) return false;
            int32_t lblCnt = rdI32(pos);
            pos += 4;
            pendingLabels.clear();
            if (lblCnt < 0 || lblCnt > 65536) return false;
            for (int32_t i = 0; i < lblCnt; ++i) {
                if (pos + 9 > raw.size()) return false;
                double v = rdF64(pos);
                uint8_t slen = raw[pos + 8];
                pos += 8;
                size_t adv = ((static_cast<size_t>(slen) + 1u + 7u) & ~size_t(7u));
                if (adv > raw.size() - pos) return false;
                pendingLabels.emplace_back(v, CleanAsciiFixed(raw.data() + pos + 1, slen));
                pos += adv;
            }
        } else if (recType == 4) { // Value Label Variable Indices (1-based into slotToCol)
            if (pos + 4 > raw.size()) return false;
            int32_t vCnt = rdI32(pos);
            pos += 4;
            if (vCnt < 0 || static_cast<size_t>(vCnt) * 4 > raw.size() - pos) return false;
            for (int32_t i = 0; i < vCnt; ++i) {
                int32_t slotIdx1 = rdI32(pos + static_cast<size_t>(i) * 4);
                if (slotIdx1 >= 1 && static_cast<size_t>(slotIdx1 - 1) < slotToCol.size()) {
                    int colIdx = slotToCol[static_cast<size_t>(slotIdx1 - 1)];
                    if (colIdx >= 0 && static_cast<size_t>(colIdx) < cols.size()) {
                        for (const auto& kv : pendingLabels) cols[static_cast<size_t>(colIdx)].numValLabels[kv.first] = kv.second;
                    }
                }
            }
            pos += static_cast<size_t>(vCnt) * 4;
        } else if (recType == 6) { // Document Record
            if (pos + 4 > raw.size()) return false;
            int32_t nLines = rdI32(pos);
            pos += 4;
            if (nLines < 0 || static_cast<size_t>(nLines) * 80 > raw.size() - pos) return false;
            pos += static_cast<size_t>(nLines) * 80;
        } else if (recType == 7) { // Extended Info Record
            if (pos + 12 > raw.size()) return false;
            int32_t subType = rdI32(pos);
            int32_t elemSize = rdI32(pos + 4);
            int32_t elemCnt  = rdI32(pos + 8);
            pos += 12;
            if (elemSize <= 0 || elemCnt < 0 || static_cast<uint64_t>(elemSize) * static_cast<uint64_t>(elemCnt) > raw.size() - pos) return false;
            size_t totalBytes = static_cast<size_t>(elemSize) * static_cast<size_t>(elemCnt);
            if (subType == 13) { // Long Variable Names: SHORT=LongName\tSHORT2=LongName2
                std::string mapStr(reinterpret_cast<const char*>(raw.data() + pos), totalBytes);
                size_t cur = 0;
                while (cur < mapStr.size()) {
                    size_t tab = mapStr.find('\t', cur);
                    std::string pair = mapStr.substr(cur, tab == std::string::npos ? std::string::npos : (tab - cur));
                    size_t eq = pair.find('=');
                    if (eq != std::string::npos) {
                        std::string k = pair.substr(0, eq);
                        std::string v = pair.substr(eq + 1);
                        while (!v.empty() && (v.back() == '\0' || v.back() == ' ')) v.pop_back();
                        for (auto& c : cols) {
                            if (c.shortName == k && !v.empty()) c.displayName = PathUtils::Utf8ToWide(v);
                        }
                    }
                    if (tab == std::string::npos) break;
                    cur = tab + 1;
                }
            }
            pos += totalBytes;
        } else {
            return false;
        }
    }

    if (!foundEnd || cols.empty()) return false;

    SpreadsheetSheet varSh;
    varSh.name = L"SPSS_Variables";
    varSh.rows.push_back({ L"#", L"Variable Name", L"Type", L"Variable Label", L"File Info" });
    varSh.rows.push_back({ L"(meta)", fileLabel, compressed == 1 ? L"Bytecode Compressed" : (compressed == 2 ? L"ZSAV" : L"Uncompressed"),
        L"Cases: " + std::to_wstring(ncases), cDate + L" " + cTime });
    for (size_t i = 0; i < cols.size(); ++i) {
        varSh.rows.push_back({
            std::to_wstring(i + 1),
            cols[i].displayName,
            cols[i].width == 0 ? L"Numeric (F8)" : (L"String (A" + std::to_wstring(cols[i].width) + L")"),
            cols[i].label,
            L""
        });
    }
    FinalizeSheetCols(varSh);

    SpreadsheetSheet dataSh;
    dataSh.name = L"SPSS_Data";
    std::vector<std::wstring> hdr;
    for (const auto& c : cols) hdr.push_back(c.displayName);
    dataSh.rows.push_back(std::move(hdr));

    size_t slotsPerCase = slotToCol.size();
    if (nominalCaseSize > 0 && static_cast<size_t>(nominalCaseSize) > slotsPerCase) {
        slotsPerCase = static_cast<size_t>(nominalCaseSize);
    }

    // Decode SPSS 8-byte slots (uncompressed or bytecode-compressed)
    uint8_t cluster[8] = {};
    int clusterIdx = 8;
    bool eofReached = false;

    auto readNextSlot = [&](uint8_t outSlot[8], bool& isSysMiss, bool& isNumVal, double& numVal) -> bool {
        isSysMiss = false;
        isNumVal = false;
        numVal = 0.0;
        if (compressed == 0) {
            if (pos + 8 > raw.size()) return false;
            std::memcpy(outSlot, raw.data() + pos, 8);
            pos += 8;
            double dv = be ? RdF64BE(outSlot) : RdF64LE(outSlot);
            if (!std::isfinite(dv) || std::fabs(dv) > 1e307) isSysMiss = true;
            else { isNumVal = true; numVal = dv; }
            return true;
        }
        if (compressed != 1) return false;

        while (!eofReached) {
            if (clusterIdx >= 8) {
                if (pos + 8 > raw.size()) return false;
                std::memcpy(cluster, raw.data() + pos, 8);
                pos += 8;
                clusterIdx = 0;
            }
            uint8_t code = cluster[clusterIdx++];
            if (code == 0) continue; // padding/ignore
            if (code == 252) { eofReached = true; return false; }
            if (code == 253) { // literal 8-byte slot
                if (pos + 8 > raw.size()) return false;
                std::memcpy(outSlot, raw.data() + pos, 8);
                pos += 8;
                double dv = be ? RdF64BE(outSlot) : RdF64LE(outSlot);
                if (!std::isfinite(dv) || std::fabs(dv) > 1e307) isSysMiss = true;
                else { isNumVal = true; numVal = dv; }
                return true;
            }
            if (code == 254) { // 8 spaces
                std::memset(outSlot, ' ', 8);
                return true;
            }
            if (code == 255) { // SYSMISS
                std::memset(outSlot, 0, 8);
                isSysMiss = true;
                return true;
            }
            // 1..251 -> compressed integer/value = code - bias
            isNumVal = true;
            numVal = static_cast<double>(code) - bias;
            std::memset(outSlot, ' ', 8);
            return true;
        }
        return false;
    };

    size_t maxCases = (ncases > 0) ? static_cast<size_t>(std::min<int32_t>(ncases, 500)) : 500;
    for (size_t r = 0; r < maxCases && slotsPerCase > 0; ++r) {
        std::vector<std::wstring> row(cols.size());
        std::string strAccum;
        int activeStrCol = -1;
        bool rowOk = true;

        for (size_t s = 0; s < slotsPerCase; ++s) {
            uint8_t slot[8] = {};
            bool sysMiss = false, isNum = false;
            double nv = 0.0;
            if (!readNextSlot(slot, sysMiss, isNum, nv)) { rowOk = false; break; }

            if (s >= slotToCol.size()) continue;
            int cIdx = slotToCol[s];
            if (cIdx >= 0) {
                if (activeStrCol >= 0) {
                    size_t w = static_cast<size_t>(cols[static_cast<size_t>(activeStrCol)].width);
                    if (strAccum.size() > w) strAccum.resize(w);
                    row[static_cast<size_t>(activeStrCol)] = PathUtils::Trim(CleanAsciiFixed(reinterpret_cast<const uint8_t*>(strAccum.data()), strAccum.size()));
                    strAccum.clear();
                    activeStrCol = -1;
                }
                const auto& col = cols[static_cast<size_t>(cIdx)];
                if (col.width == 0) {
                    if (sysMiss || !isNum) {
                        row[static_cast<size_t>(cIdx)] = L"SYSMIS";
                    } else {
                        auto it = col.numValLabels.find(nv);
                        if (it != col.numValLabels.end()) {
                            row[static_cast<size_t>(cIdx)] = FmtDouble(nv) + L" (" + it->second + L")";
                        } else {
                            row[static_cast<size_t>(cIdx)] = FmtDouble(nv);
                        }
                    }
                } else {
                    activeStrCol = cIdx;
                    strAccum.assign(reinterpret_cast<const char*>(slot), 8);
                }
            } else if (activeStrCol >= 0) {
                strAccum.append(reinterpret_cast<const char*>(slot), 8);
            }
        }
        if (activeStrCol >= 0) {
            size_t w = static_cast<size_t>(cols[static_cast<size_t>(activeStrCol)].width);
            if (strAccum.size() > w) strAccum.resize(w);
            row[static_cast<size_t>(activeStrCol)] = PathUtils::Trim(CleanAsciiFixed(reinterpret_cast<const uint8_t*>(strAccum.data()), strAccum.size()));
        }
        if (!rowOk) break;
        dataSh.rows.push_back(std::move(row));
    }
    FinalizeSheetCols(dataSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"SPSS Statistical System Dataset (.sav)";
    if (dataSh.rows.size() > 1) {
        out.sheets.push_back(std::move(dataSh));
        out.sheets.push_back(std::move(varSh));
    } else {
        out.sheets.push_back(std::move(varSh));
    }
    out.valid = true;
    return true;
}

} // namespace AiDataDetail

inline bool ParseSpssSav(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return AiDataDetail::ParseSpssSav(raw, out); }

} // namespace Formats
