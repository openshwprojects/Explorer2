#pragma once
// GroupPolicyPol.h — Windows Group Policy Registry.pol (.pol)

namespace Formats {

inline bool ParseGroupPolicyPolDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #94. Windows Group Policy Registry.pol ("PReg\x01\x00\x00\x00")
    if ((raw.size() >= 8 && std::memcmp(raw.data(), "PReg\x01\x00\x00\x00", 8) == 0) || ext == L".pol") {
        if (raw.size() < 8 || std::memcmp(raw.data(), "PReg", 4) != 0) return false;
        uint32_t ver = DetailDocs200::RdU32LE(raw.data() + 4);

        SpreadsheetSheet sh;
        sh.name = L"GroupPolicy_Rules";
        sh.maxCols = 4;
        sh.rows.push_back({ L"Registry Key", L"Value Name", L"Reg Type", L"Policy Data" });

        size_t pos = 8;
        while (pos + 16 <= raw.size() && sh.rows.size() < 500) {
            if (DetailDocs200::RdU16LE(raw.data() + pos) != 0x005Bu) { pos += 2; continue; } // '['
            pos += 2;
            std::wstring key = DetailDocs200::ReadUtf16LE(raw.data() + pos, raw.size() - pos);
            pos += (key.size() + 1) * 2;
            if (pos + 2 > raw.size() || DetailDocs200::RdU16LE(raw.data() + pos) != 0x003Bu) break; // ';'
            pos += 2;
            std::wstring val = DetailDocs200::ReadUtf16LE(raw.data() + pos, raw.size() - pos);
            pos += (val.size() + 1) * 2;
            if (pos + 10 > raw.size() || DetailDocs200::RdU16LE(raw.data() + pos) != 0x003Bu) break; // ';'
            pos += 2;
            uint32_t rType = DetailDocs200::RdU32LE(raw.data() + pos); pos += 4;
            if (pos + 6 > raw.size() || DetailDocs200::RdU16LE(raw.data() + pos) != 0x003Bu) break; // ';'
            pos += 2;
            uint32_t rSize = DetailDocs200::RdU32LE(raw.data() + pos); pos += 4;
            if (pos + 2 > raw.size() || DetailDocs200::RdU16LE(raw.data() + pos) != 0x003Bu) break; // ';'
            pos += 2;
            if (rSize > raw.size() - pos) break;

            std::wstring tStr = (rType == 1) ? L"REG_SZ" : (rType == 2) ? L"REG_EXPAND_SZ" :
                                (rType == 4) ? L"REG_DWORD" : (rType == 11) ? L"REG_QWORD" : L"REG_BINARY";
            std::wstring dStr;
            if (rType == 4 && rSize >= 4) dStr = std::to_wstring(DetailDocs200::RdU32LE(raw.data() + pos));
            else if (rType == 1 || rType == 2) dStr = DetailDocs200::ReadUtf16LE(raw.data() + pos, rSize);
            else dStr = DetailDocs200::HexBytes(raw.data() + pos, rSize, 16);

            pos += rSize;
            if (pos + 2 <= raw.size() && DetailDocs200::RdU16LE(raw.data() + pos) == 0x005Du) pos += 2; // ']'
            sh.rows.push_back({ key, val, tStr, dStr });
        }

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Windows Group Policy Registry.pol (PReg v" + std::to_wstring(ver) + L")";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
